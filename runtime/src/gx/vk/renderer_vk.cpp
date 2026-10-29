// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan GX renderer (renderer_vk.h): device, swapchain, frames, present and the overlay. The EFB, draws,
// copies and peeks are renderer_vk_efb.cpp.

#include "renderer_vk_internal.h"
#include "galaxy/gx/vk/renderer_vk.h"
#include "galaxy/platform_time.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/gx/game_asset_extract.h"
#include "galaxy/gx/host_window.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/native_host.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace galaxy::gx {

namespace {

bool env_flag(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
}

bool trace_present_stats_enabled() {
    static const bool enabled = env_flag("GALAXY_TRACE_PRESENT_STATS", false);
    return enabled;
}

// The binding layouts of the renderer's own shaders (annotate()).
constexpr vk::RegisterBinding kUtilityBindings[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"s0", 0, 1}};
constexpr vk::RegisterBinding kConversionBindings[] = {
    {"b0", 0, 0, true}, {"t0", 0, 0}, {"s0", 0, 1}, {"s1", 0, 2}};
constexpr std::uint32_t kPushConstantBytes = 64u;
constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// The XFB into the swap chain image (the present pass's blit), turned for a pre-rotated image: `transform` is the
// image's VkSurfaceTransformFlagBitsKHR (identity, 90, 180 or 270 degrees clockwise). t is the position in the
// viewport, which already covers the picture's turned rectangle; uv is the picture's point shown there.
constexpr char kPresentVs[] = R"HLSL(
cbuffer C : register(b0) { uint transform; };
struct V { float4 pos:SV_Position; float2 uv:TEXCOORD0; };
V main(uint id : SV_VertexID) {
    V o;
    float2 t = float2(id == 1 ? 2 : 0, id == 2 ? 2 : 0);
    o.pos = float4(t.x * 2 - 1, 1 - t.y * 2, 0.5, 1);
    o.uv = transform == 2u ? float2(t.y, 1 - t.x)
         : transform == 4u ? float2(1 - t.x, 1 - t.y)
         : transform == 8u ? float2(1 - t.y, t.x)
         : t;
    return o;
}
)HLSL";

// The picture's rectangle (in the window as displayed, `width` x `height`) in an image turned by `transform`.
VkRect2D turned_rect(VkSurfaceTransformFlagBitsKHR transform, int width, int height, int left, int top, int w, int h) {
    switch (transform) {
    case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
        return {{height - top - h, left}, {static_cast<std::uint32_t>(h), static_cast<std::uint32_t>(w)}};
    case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
        return {{width - left - w, height - top - h}, {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)}};
    case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
        return {{top, width - left - w}, {static_cast<std::uint32_t>(h), static_cast<std::uint32_t>(w)}};
    default:
        return {{left, top}, {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)}};
    }
}

}  // namespace

VkDescriptorPool vk_detail::create_pool(VkDevice device, std::uint32_t sets, const PoolSizes& counts) {
    std::vector<VkDescriptorPoolSize> sizes;
    if (counts.images != 0u) sizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, counts.images});
    if (counts.samplers != 0u) sizes.push_back({VK_DESCRIPTOR_TYPE_SAMPLER, counts.samplers});
    if (counts.dynamic_uniforms != 0u) {
        sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, counts.dynamic_uniforms});
    }
    if (counts.dynamic_storage != 0u) {
        sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, counts.dynamic_storage});
    }
    if (counts.storage != 0u) sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, counts.storage});
    if (counts.storage_images != 0u) sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, counts.storage_images});
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.maxSets = sets;
    info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    vk::check(vkCreateDescriptorPool(device, &info, nullptr, &pool), "vkCreateDescriptorPool");
    return pool;
}

using vk_detail::create_pool;
using vk_detail::kFrameSets;
using vk_detail::kFrameUtilitySamplers;
using vk_detail::kPersistentPool;
using vk_detail::kPersistentSets;

namespace {

VkSampler create_sampler(VkDevice device, VkFilter filter, VkSamplerMipmapMode mip) {
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = filter;
    info.minFilter = filter;
    info.mipmapMode = mip;
    info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = 0.0f;
    VkSampler sampler = VK_NULL_HANDLE;
    vk::check(vkCreateSampler(device, &info, nullptr, &sampler), "vkCreateSampler");
    return sampler;
}

}  // namespace

std::size_t RendererVk::ConstantsSetKeyHash::operator()(const ConstantsSetKey& key) const noexcept {
    std::uint64_t hash = mix64(reinterpret_cast<std::uint64_t>(key.b0));
    hash = combine64(hash, reinterpret_cast<std::uint64_t>(key.b1));
    hash = combine64(hash, reinterpret_cast<std::uint64_t>(key.earlier));
    hash = combine64(hash, key.whole_palette ? 1u : 0u);
    return static_cast<std::size_t>(combine64(hash, reinterpret_cast<std::uint64_t>(key.palette)));
}

bool RendererVk::initialize(int window_width, int window_height, unsigned efb_scale) {
    efb_scale_ = std::clamp(efb_scale, 1u, kMaxEfbScale);
    frame_slot_ = 0;
    frame_index_ = 0;
    const host_window::OpenedWindow opened = host_window::open_game_window(window_owner_, window_width, window_height);
    if (opened.window == nullptr) {
        return false;
    }
    window_ = opened.window;
    window_shown_ = opened.shown;
    try {
        if (!create_surface_and_device() || !create_layouts() || !create_swapchain()) {
            shutdown();
            return false;
        }
        efb_scale_ = std::min(efb_scale_, host_window::display_efb_scale_cap());
        const VkDevice device = vk::Context::get().device();
        {
            VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool_info.queueFamilyIndex = vk::Context::get().queue_family();
            vk::check(vkCreateCommandPool(device, &pool_info, nullptr, &utility_pool_), "vkCreateCommandPool");
            VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc.commandPool = utility_pool_;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            vk::check(vkAllocateCommandBuffers(device, &alloc, &utility_commands_), "vkAllocateCommandBuffers");
        }
        if (!create_efb_targets(efb_scale_, wanted_msaa_samples()) || !create_utility_pipelines()) {
            shutdown();
            return false;
        }
        for (Frame& frame : frames_) {
            VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pool_info.queueFamilyIndex = vk::Context::get().queue_family();
            vk::check(vkCreateCommandPool(device, &pool_info, nullptr, &frame.pool), "vkCreateCommandPool");
            VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc.commandPool = frame.pool;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 2;
            VkCommandBuffer buffers[2]{};
            vk::check(vkAllocateCommandBuffers(device, &alloc, buffers), "vkAllocateCommandBuffers");
            frame.uploads = buffers[0];
            frame.commands = buffers[1];
            frame.descriptors =
                create_pool(device, kFrameSets, {.images = kFrameSets * 8u + 64u, .samplers = kFrameUtilitySamplers,
                                                 .storage_images = vk_detail::kPassStorageImages});
            VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vk::check(vkCreateSemaphore(device, &semaphore_info, nullptr, &frame.acquired), "vkCreateSemaphore");
        }
        if (vk::Context::get().properties().limits.timestampComputeAndGraphics == VK_TRUE) {
            VkQueryPoolCreateInfo query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            query_info.queryCount = kFramesInFlight * 2u;
            vk::check(vkCreateQueryPool(device, &query_info, nullptr, &timestamps_), "vkCreateQueryPool");
            timestamp_period_ns_ = vk::Context::get().properties().limits.timestampPeriod;
            if (env_flag("GALAXY_GPU_DRAWS", false)) {
                query_info.queryCount = kFramesInFlight * kGpuDrawSlots;
                vk::check(vkCreateQueryPool(device, &query_info, nullptr, &draw_queries_), "vkCreateQueryPool");
            }
            if (env_flag("GALAXY_GPU_PASSES", false)) {
                query_info.queryCount = kFramesInFlight * (kGpuPassSlots + 2u);
                vk::check(vkCreateQueryPool(device, &query_info, nullptr, &pass_queries_), "vkCreateQueryPool");
                if (vk::Context::get().pipeline_statistics()) {
                    VkQueryPoolCreateInfo stats_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
                    stats_info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
                    stats_info.queryCount = kFramesInFlight;
                    stats_info.pipelineStatistics =
                        VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
                        VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
                        VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
                        VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT |
                        VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT |
                        VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
                    vk::check(vkCreateQueryPool(device, &stats_info, nullptr, &stats_queries_), "vkCreateQueryPool");
                }
            }
        }
        // The rings are Vulkan buffers with GX addresses; their sizes follow the D3D12 renderer's. Every segmentation
        // shares one buffer per ring.
        const auto ring = [&](UploadRing& target, const char* name, std::size_t bytes) {
            return target.initialize(device, name, UploadRing::budget(bytes, kFramesInFlight), kFramesInFlight,
                                     bytes);
        };
        if (!ring(vertex_ring_, "vertex", 192u << 20u) || !ring(index_ring_, "index", 48u << 20u) ||
            !ring(constant_ring_, "constant", 96u << 20u) || !ring(matrix_ring_, "matrix", 128u << 20u)) {
            std::cerr << "[Renderer] upload rings unavailable\n";
            shutdown();
            return false;
        }
        // Vertex blending's verdicts and frame values: every draw reads them (game frames the zero element).
        blend_verdicts_ = vk::Context::get().create_buffer((vk_detail::kZeroValuesElement + 1u) * 16u,
                                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "blend verdicts");
        if (!blend_verdicts_) {
            shutdown();
            return false;
        }
        std::memset(blend_verdicts_->mapped, 0, blend_verdicts_->size);
        refresh_default_sampler_table();
        // The game's bloom, compiled on a worker; the game draws its own chains until it is ready.
        bloom_.initialize();
        // Presentation post-processing, compiled on a worker; the plain blit presents until it is ready.
        const RenderConfig cfg = get_render_config();
        if (cfg.post_process) {
            post_.initialize(post_process_settings_from(cfg), gx_layout_.efb_pass, color_pass_);
            post_.set_present_pass(present_pass_);
        } else {
            PostProcessStatus off{};
            std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "CONFIG OFF");
            set_post_process_status(off);
            std::cerr << "[post-process] off; presenting with the plain blit\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "[Renderer] Vulkan initialize failed: " << error.what() << '\n';
        shutdown();
        return false;
    }
    std::cout << "[Renderer] Vulkan EFB=" << kEfbWidth * efb_scale_ << 'x' << kEfbHeight * efb_scale_
              << " depth=" << (depth_format_ == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32F") << '\n';
    return true;
}

bool RendererVk::create_surface_and_device() {
    const bool prefer_integrated = [] {
        const char* value = std::getenv("GALAXY_GPU_PREFERENCE");
        return value != nullptr && (value[0] == 'l' || value[0] == 'L' || value[0] == 'm' || value[0] == 'M');
    }();
    const bool validation = env_flag("GALAXY_VULKAN_VALIDATION", false);
    const host_window::NativeWindow window = window_;
    const auto make_surface = [window](VkInstance instance) {
        return host_window::create_vulkan_surface(instance, window);
    };
    if (!vk::Context::get().initialize(prefer_integrated, validation, host_window::vulkan_instance_extensions(),
                                       make_surface, &surface_)) {
        return false;
    }
    const vk::Context& context = vk::Context::get();
    std::cout << "[gpu] adapter=\"" << context.device_name() << "\" api=vulkan vendor=0x" << std::hex
              << context.properties().vendorID << " device=0x" << context.properties().deviceID << std::dec << '\n';
    return true;
}

bool RendererVk::create_layouts() {
    const vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    // EFB depth: 24-bit integer as the console's (and D3D12's D24) where the device has it.
    VkFormatProperties format{};
    vkGetPhysicalDeviceFormatProperties(context.physical_device(), VK_FORMAT_D24_UNORM_S8_UINT, &format);
    depth_format_ = (format.optimalTilingFeatures & (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                     VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) ==
            (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        ? VK_FORMAT_D24_UNORM_S8_UINT
        : VK_FORMAT_D32_SFLOAT;
    {
        VkFormatProperties depth_properties{};
        vkGetPhysicalDeviceFormatProperties(context.physical_device(), depth_format_, &depth_properties);
        depth_linear_ = (depth_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0u;
    }

    const VkShaderStageFlags gx_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
        (context.geometry_shader() ? VK_SHADER_STAGE_GEOMETRY_BIT : 0u);
    {
        // Set 0: b0, b1, the matrix palette, vertex blending's earlier vertices and verdicts.
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, gx_stages, nullptr},
            {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, gx_stages, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        };
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 5;
        info.pBindings = bindings;
        vk::check(vkCreateDescriptorSetLayout(device, &info, nullptr, &gx_layout_.sets[0]), "set 0 layout");
    }
    {
        const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8,
                                                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 1;
        info.pBindings = &binding;
        vk::check(vkCreateDescriptorSetLayout(device, &info, nullptr, &gx_layout_.sets[1]), "set 1 layout");
    }
    {
        const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_SAMPLER, 8, VK_SHADER_STAGE_FRAGMENT_BIT,
                                                   nullptr};
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 1;
        info.pBindings = &binding;
        vk::check(vkCreateDescriptorSetLayout(device, &info, nullptr, &gx_layout_.sets[2]), "set 2 layout");
    }
    {
        // Vertex blending's per-draw values (b2): mode, frame element, verdict slot, last vertex, first vertex's byte.
        const VkPushConstantRange blend{VK_SHADER_STAGE_VERTEX_BIT, 0, 5u * sizeof(std::uint32_t)};
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 3;
        info.pSetLayouts = gx_layout_.sets.data();
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &blend;
        vk::check(vkCreatePipelineLayout(device, &info, nullptr, &gx_layout_.layout), "GX pipeline layout");
    }
    linear_clamp_ = create_sampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST);
    point_clamp_ = create_sampler(device, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST);
    {
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &linear_clamp_},
        };
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 2;
        info.pBindings = bindings;
        vk::check(vkCreateDescriptorSetLayout(device, &info, nullptr, &utility_set_layout_), "utility set layout");
    }
    {
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            // Written per set: linear, or nearest for a depth format the device cannot filter linearly.
            {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &point_clamp_},
        };
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 3;
        info.pBindings = bindings;
        vk::check(vkCreateDescriptorSetLayout(device, &info, nullptr, &conversion_set_layout_),
                  "conversion set layout");
    }
    const VkPushConstantRange push{kPushStages, 0, kPushConstantBytes};
    {
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 1;
        info.pSetLayouts = &utility_set_layout_;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &push;
        vk::check(vkCreatePipelineLayout(device, &info, nullptr, &utility_layout_), "utility pipeline layout");
        info.pSetLayouts = &conversion_set_layout_;
        vk::check(vkCreatePipelineLayout(device, &info, nullptr, &conversion_layout_), "conversion pipeline layout");
    }

    // MSAA: the counts the device renders RGBA8 and the depth format with and samples the depth with.
    {
        const VkPhysicalDeviceLimits& limits = context.properties().limits;
        VkSampleCountFlags counts = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts &
            limits.sampledImageDepthSampleCounts;
        VkImageFormatProperties image{};
        if (vkGetPhysicalDeviceImageFormatProperties(context.physical_device(), VK_FORMAT_R8G8B8A8_UNORM,
                                                     VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &image) == VK_SUCCESS) {
            counts &= image.sampleCounts;
        } else {
            counts = VK_SAMPLE_COUNT_1_BIT;
        }
        if (vkGetPhysicalDeviceImageFormatProperties(context.physical_device(), depth_format_, VK_IMAGE_TYPE_2D,
                                                     VK_IMAGE_TILING_OPTIMAL,
                                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                         VK_IMAGE_USAGE_SAMPLED_BIT,
                                                     0, &image) == VK_SUCCESS) {
            counts &= image.sampleCounts;
        } else {
            counts = VK_SAMPLE_COUNT_1_BIT;
        }
        msaa_max_samples_ = 1u;
        for (unsigned samples = 2u; samples <= 8u && (counts & samples) != 0u; samples *= 2u) {
            msaa_max_samples_ = samples;
        }
    }
    // The EFB pass keeps both targets in their attachment layouts; whoever samples them transitions and returns them.
    // With MSAA its targets are multisampled and a third attachment, the single-sample colour, takes their resolve.
    for (unsigned samples_log2 = 0u; (1u << samples_log2) <= msaa_max_samples_; ++samples_log2) {
        VkAttachmentDescription attachments[3]{};
        attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        attachments[0].samples = static_cast<VkSampleCountFlagBits>(1u << samples_log2);
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1] = attachments[0];
        attachments[1].format = depth_format_;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[2] = attachments[0];
        attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference resolve{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        subpass.pResolveAttachments = samples_log2 != 0u ? &resolve : nullptr;
        subpass.pDepthStencilAttachment = &depth;
        // Work before the pass (copies out of the EFB, uploads) completes before its draws; draws complete before
        // anything after it reads the EFB.
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT |
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = samples_log2 != 0u ? 3u : 2u;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;
        vk::check(vkCreateRenderPass(device, &info, nullptr,
                                     samples_log2 != 0u ? &gx_layout_.ms_passes[samples_log2] : &gx_layout_.efb_pass),
                  "EFB render pass");
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        vk::check(vkCreateRenderPass(device, &info, nullptr,
                                     samples_log2 != 0u ? &gx_layout_.ms_passes_clear[samples_log2]
                                                        : &gx_layout_.efb_pass_clear),
                  "EFB clearing render pass");
    }
    // One RGBA8 target written whole (copies, peeks): its old contents are dropped; it ends shader-readable.
    {
        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = 0;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &color_pass_), "copy render pass");
        attachment.format = VK_FORMAT_R8_UNORM;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &grey_pass_), "one-channel copy render pass");
    }
    gx_layout_.depth_format = depth_format_;
    gx_layout_.geometry_shaders = context.geometry_shader();
    gx_layout_.tessellation = context.tessellation_shader();
    gx_layout_.dual_source_blend = context.dual_source_blend();
    gx_layout_.logic_op = context.logic_op();
    gx_layout_.depth_clamp = context.depth_clamp();
    gx_layout_.depth_clip_enable = context.depth_clip_enable();
    persistent_pool_ = create_pool(device, kPersistentSets, kPersistentPool);
    return true;
}

bool RendererVk::create_swapchain() {
    const vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    VkSurfaceCapabilitiesKHR caps{};
    const VkResult caps_result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physical_device(), surface_, &caps);
    if (caps_result == VK_ERROR_SURFACE_LOST_KHR) {
        surface_lost_ = true;
        return false;
    }
    vk::check(caps_result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent = {host_window::backbuffer_width, host_window::backbuffer_height};
    }
    extent.width = std::clamp(extent.width, std::max(1u, caps.minImageExtent.width), caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, std::max(1u, caps.minImageExtent.height), caps.maxImageExtent.height);
    if (extent.width == 0u || extent.height == 0u) {
        return false;  // minimised: try again later
    }

    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context.physical_device(), surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(context.physical_device(), surface_, &count, formats.data());
    // UNORM as D3D12's backbuffer: the picture is already in display gamma.
    VkSurfaceFormatKHR format = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
    if (format.format == VK_FORMAT_UNDEFINED) {  // the surface takes any format
        format = {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    for (const VkSurfaceFormatKHR& candidate : formats) {
        if ((candidate.format == VK_FORMAT_B8G8R8A8_UNORM || candidate.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = candidate;
            break;
        }
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physical_device(), surface_, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physical_device(), surface_, &count, modes.data());
    const auto supported = [&](VkPresentModeKHR mode) {
        return std::find(modes.begin(), modes.end(), mode) != modes.end();
    };
    // VSYNC on: FIFO. Off: immediate (tearing allowed, as D3D12's ALLOW_TEARING), else mailbox.
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (get_render_config().vsync_interval == 0u) {
        mode = supported(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR
            : supported(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;
    }

    std::uint32_t images = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount != 0u) {
        images = std::min(images, caps.maxImageCount);
    }
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = images;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageArrayLayers = 1;
    // Transfer source (the backbuffer capture) only where the surface allows it: drivers must offer only the colour
    // attachment use.
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // Pre-rotated to the display's current transform (Android's turned display), so the compositor does no turning
    // of its own; the image then has the display's natural orientation.
    VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    switch (caps.currentTransform) {
    case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
    case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
    case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
        if ((caps.supportedTransforms & caps.currentTransform) != 0u) {
            transform = caps.currentTransform;
        }
        break;
    default:
        break;
    }
    if (transform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR &&
        (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) == 0u) {
        transform = caps.currentTransform;
    }
    const bool quarter_turn =
        transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR || transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    const VkExtent2D image_extent = quarter_turn ? VkExtent2D{extent.height, extent.width} : extent;
    info.imageExtent = image_extent;
    info.preTransform = transform;
    // Opaque, else inherited from the window (Android's usual one), else whatever the surface has.
    const VkCompositeAlphaFlagsKHR alphas = caps.supportedCompositeAlpha;
    info.compositeAlpha = (alphas & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0u ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
        : (alphas & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != 0u || alphas == 0u ? VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
        : static_cast<VkCompositeAlphaFlagBitsKHR>(alphas & (~alphas + 1u));
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain_.handle;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    const VkResult created = vkCreateSwapchainKHR(device, &info, nullptr, &handle);
    if (created == VK_ERROR_SURFACE_LOST_KHR || created == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR) {
        surface_lost_ = true;
        return false;
    }
    vk::check(created, "vkCreateSwapchainKHR");
    destroy_swapchain();
    swapchain_.handle = handle;
    present_timing_.refresh_ns = 0;  // the new swapchain's, at the next report
    present_timing_.last_actual = 0;
    swapchain_.format = format.format;
    swapchain_.extent = extent;
    swapchain_.image = image_extent;
    swapchain_.transform = transform;
    swapchain_.copyable = (info.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
    swapchain_.mode = mode;
    vkGetSwapchainImagesKHR(device, handle, &count, nullptr);
    swapchain_.images.resize(count);
    vkGetSwapchainImagesKHR(device, handle, &count, swapchain_.images.data());
    std::fprintf(stderr, "[vulkan] swapchain %ux%u transform=%u images=%u\n", extent.width, extent.height,
                 static_cast<unsigned>(transform), count);

    if (present_pass_ == VK_NULL_HANDLE || swapchain_.format != format.format) {
        if (present_pass_ != VK_NULL_HANDLE) {
            post_.set_present_pass(VK_NULL_HANDLE);
            vkDestroyRenderPass(device, present_pass_, nullptr);
        }
        VkAttachmentDescription attachment{};
        attachment.format = format.format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        // PowerVR clears by command instead (DeviceQuirks::clear_by_command, present_xfb).
        attachment.loadOp = vk::Context::get().quirks().clear_by_command ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                                                         : VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        // The acquire semaphore is waited at colour output: the image's layout change waits for it too.
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo pass_info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        pass_info.attachmentCount = 1;
        pass_info.pAttachments = &attachment;
        pass_info.subpassCount = 1;
        pass_info.pSubpasses = &subpass;
        pass_info.dependencyCount = 1;
        pass_info.pDependencies = &dependency;
        vk::check(vkCreateRenderPass(device, &pass_info, nullptr, &present_pass_), "present render pass");
    }
    for (VkImage image : swapchain_.images) {
        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format.format;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        vk::check(vkCreateImageView(device, &view_info, nullptr, &view), "swapchain view");
        swapchain_.views.push_back(view);
        VkFramebufferCreateInfo fb_info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb_info.renderPass = present_pass_;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = &view;
        fb_info.width = image_extent.width;
        fb_info.height = image_extent.height;
        fb_info.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        vk::check(vkCreateFramebuffer(device, &fb_info, nullptr, &framebuffer), "swapchain framebuffer");
        swapchain_.framebuffers.push_back(framebuffer);
        VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore rendered = VK_NULL_HANDLE;
        vk::check(vkCreateSemaphore(device, &semaphore_info, nullptr, &rendered), "vkCreateSemaphore");
        swapchain_.rendered.push_back(rendered);
    }
    host_window::backbuffer_width = extent.width;
    host_window::backbuffer_height = extent.height;
    swapchain_dirty_ = false;
    return true;
}

void RendererVk::destroy_swapchain() noexcept {
    const VkDevice device = vk::Context::get().device();
    if (device == VK_NULL_HANDLE) {
        return;
    }
    for (VkFramebuffer framebuffer : swapchain_.framebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);
    for (VkImageView view : swapchain_.views) vkDestroyImageView(device, view, nullptr);
    for (VkSemaphore semaphore : swapchain_.rendered) vkDestroySemaphore(device, semaphore, nullptr);
    if (swapchain_.handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain_.handle, nullptr);
    }
    swapchain_.framebuffers.clear();
    swapchain_.views.clear();
    swapchain_.rendered.clear();
    swapchain_.images.clear();
    swapchain_.handle = VK_NULL_HANDLE;
}

// Whether the surface's size or transform now differs from the swap chain's. SUBOPTIMAL is also how a driver may
// report what the swap chain already handles, which must not remake it every frame.
bool RendererVk::surface_changed() const {
    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk::Context::get().physical_device(), surface_, &caps) != VK_SUCCESS) {
        return true;
    }
    const bool size_known = caps.currentExtent.width != UINT32_MAX;
    return caps.currentTransform != swapchain_.transform ||
        (size_known && (caps.currentExtent.width != swapchain_.extent.width ||
                        caps.currentExtent.height != swapchain_.extent.height));
}

void RendererVk::release_surface() noexcept {
    if (surface_ == VK_NULL_HANDLE) {
        return;
    }
    vk::Context& context = vk::Context::get();
    vkDeviceWaitIdle(context.device());
    destroy_swapchain();
    vkDestroySurfaceKHR(context.instance(), surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
    std::fprintf(stderr, "[vulkan] window surface released\n");
}

bool RendererVk::restore_surface() {
    vk::Context& context = vk::Context::get();
    const VkSurfaceKHR surface = host_window::create_vulkan_surface(context.instance(), window_);
    if (surface == VK_NULL_HANDLE) {
        return false;
    }
    VkBool32 presents = VK_FALSE;
    if (vkGetPhysicalDeviceSurfaceSupportKHR(context.physical_device(), context.queue_family(), surface, &presents) !=
            VK_SUCCESS ||
        presents != VK_TRUE) {
        vkDestroySurfaceKHR(context.instance(), surface, nullptr);
        return false;
    }
    surface_ = surface;
    swapchain_dirty_ = true;
    std::fprintf(stderr, "[vulkan] window surface restored\n");
    return true;
}

VkShaderModule RendererVk::utility_module(std::string_view hlsl, vk::ShaderStage stage, bool conversion) {
    std::string error;
    const std::vector<std::uint32_t> spirv = vk::compile_hlsl(
        vk::annotate(hlsl, conversion ? std::span<const vk::RegisterBinding>(kConversionBindings)
                                      : std::span<const vk::RegisterBinding>(kUtilityBindings)),
        stage, "main", {}, error);
    if (spirv.empty()) {
        throw std::runtime_error("utility shader: " + error);
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = spirv.size() * sizeof(std::uint32_t);
    info.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vk::check(vkCreateShaderModule(vk::Context::get().device(), &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

VkPipeline RendererVk::create_utility_pipeline(VkRenderPass pass, VkPipelineLayout layout, std::string_view vs_source,
                                               std::string_view ps_source, VkColorComponentFlags write_mask,
                                               bool blend, bool strip, bool depth_write,
                                               VkSampleCountFlagBits samples) {
    const VkDevice device = vk::Context::get().device();
    const bool conversion = layout == conversion_layout_;
    const VkShaderModule vs = utility_module(vs_source, vk::ShaderStage::Vertex, conversion);
    const VkShaderModule ps = utility_module(ps_source, vk::ShaderStage::Fragment, conversion);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs,
                 "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps,
                 "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = strip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = samples;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if (depth_write) {
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    }
    VkPipelineColorBlendAttachmentState target{};
    target.colorWriteMask = write_mask;
    if (blend) {
        // The overlay's blend (D3D12 UI pipelines): source alpha over, alpha accumulated.
        target.blendEnable = VK_TRUE;
        target.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        target.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        target.colorBlendOp = VK_BLEND_OP_ADD;
        target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        target.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo blend_info{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend_info.attachmentCount = 1;
    blend_info.pAttachments = &target;
    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend_info;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    info.renderPass = pass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    vk::check(result, "utility pipeline");
    return pipeline;
}

bool RendererVk::create_utility_pipelines() {
    constexpr VkColorComponentFlags kAll = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    conversion_pipeline_ = create_utility_pipeline(color_pass_, conversion_layout_, blit_vertex_shader_source(),
                                                   efb_conversion_shader_source(), kAll, false, false, false);
    grey_conversion_pipeline_ = create_utility_pipeline(grey_pass_, conversion_layout_, blit_vertex_shader_source(),
                                                        efb_conversion_shader_source(), kAll, false, false, false);
    display_filter_pipeline_ = create_utility_pipeline(color_pass_, conversion_layout_, blit_vertex_shader_source(),
                                                       display_filter_shader_source(), kAll, false, false, false);
    for (unsigned kind = 0u; kind < static_cast<unsigned>(EfbUtility::Count); ++kind) {
        (void)efb_utility_pipeline(static_cast<EfbUtility>(kind));
    }
    if (msaa_max_samples_ > 1u) {
        // The depth resolve: each pixel's sample 0 (the console's single sample sits at the pixel centre).
        static constexpr char kDepthResolvePs[] = R"HLSL(
Texture2DMS<float> t0 : register(t0);
float main(float4 pos : SV_Position) : SV_Depth {
    return t0.Load(int2(pos.xy), 0);
}
)HLSL";
        depth_resolve_pipeline_ = create_utility_pipeline(gx_layout_.efb_pass, utility_layout_,
                                                          blit_vertex_shader_source(), kDepthResolvePs, 0u, false,
                                                          false, true);
    }
    blit_pipeline_ = create_utility_pipeline(present_pass_, utility_layout_, kPresentVs, blit_pixel_shader_source(),
                                             kAll, false, false, false);
    ui_solid_pipeline_ = create_utility_pipeline(present_pass_, utility_layout_, ui_solid_quad_vertex_shader_source(),
                                                 ui_solid_quad_pixel_shader_source(), kAll, true, true, false);
    ui_textured_pipeline_ = create_utility_pipeline(
        present_pass_, utility_layout_, ui_textured_quad_vertex_shader_source(),
        ui_textured_quad_pixel_shader_source(), kAll, true, true, false);
    depth_carry_pipeline_ = create_utility_pipeline(
        gx_layout_.efb_pass, utility_layout_, blit_vertex_shader_source(),
        depth_linear_ ? depth_carry_pixel_shader_source() : depth_carry_load_pixel_shader_source(), 0u, false, false,
        true);
    color_carry_pipeline_ = create_utility_pipeline(gx_layout_.efb_pass, utility_layout_, blit_vertex_shader_source(),
                                                    blit_pixel_shader_source(), kAll, false, false, false);
    return true;
}

VkPipeline RendererVk::efb_utility_pipeline(EfbUtility kind) {
    const unsigned samples_log2 = efb_samples_log2();
    VkPipeline& pipeline = efb_utility_pipelines_[static_cast<unsigned>(kind)][samples_log2];
    if (pipeline != VK_NULL_HANDLE) {
        return pipeline;
    }
    if ((kind == EfbUtility::ClearRgba || kind == EfbUtility::ClearDepth) &&
        !vk::Context::get().quirks().clear_with_draw) {
        return VK_NULL_HANDLE;
    }
    const VkRenderPass pass = gx_layout_.pass_for(samples_log2, false);
    const auto samples = static_cast<VkSampleCountFlagBits>(1u << samples_log2);
    constexpr VkColorComponentFlags kRgb = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT;
    switch (kind) {
    case EfbUtility::ClearRgb:
    case EfbUtility::ClearAlpha:
    case EfbUtility::ClearRgba:
        pipeline = create_utility_pipeline(
            pass, utility_layout_, blit_vertex_shader_source(), clear_pixel_shader_source(),
            kind == EfbUtility::ClearRgb ? kRgb
                : kind == EfbUtility::ClearAlpha ? VK_COLOR_COMPONENT_A_BIT : kRgb | VK_COLOR_COMPONENT_A_BIT,
            false, false, false, samples);
        break;
    case EfbUtility::ClearDepth:
        pipeline = create_utility_pipeline(pass, utility_layout_, blit_vertex_shader_source(),
                                           clear_depth_pixel_shader_source(), 0u, false, false, true, samples);
        break;
    case EfbUtility::Count:
        break;
    }
    return pipeline;
}

void RendererVk::shutdown() {
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        post_.shutdown();
        bloom_.shutdown();
        oriented_.reset();
        release_replay_resources();
        efb_copy_dests_.clear();
        efb_copy_dests_bytes_ = 0u;
        for (Frame& frame : frames_) {
            frame.retired.clear();
            if (frame.pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, frame.pool, nullptr);
            if (frame.descriptors != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, frame.descriptors, nullptr);
            for (VkDescriptorPool pool : frame.overflow_descriptors) vkDestroyDescriptorPool(device, pool, nullptr);
            if (frame.acquired != VK_NULL_HANDLE) vkDestroySemaphore(device, frame.acquired, nullptr);
            frame = Frame{};
        }
        for (AsyncField& field : async_fields_) {
            if (field.pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, field.pool, nullptr);
            field = AsyncField{};
        }
        if (utility_pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device, utility_pool_, nullptr);
        utility_pool_ = VK_NULL_HANDLE;
        utility_commands_ = VK_NULL_HANDLE;
        for (VkPipeline pipeline : {conversion_pipeline_, grey_conversion_pipeline_, display_filter_pipeline_,
                                    blit_pipeline_, ui_solid_pipeline_, ui_textured_pipeline_, depth_carry_pipeline_,
                                    color_carry_pipeline_, depth_resolve_pipeline_}) {
            if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
        }
        for (auto& by_samples : efb_utility_pipelines_) {
            for (VkPipeline& pipeline : by_samples) {
                if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }
        conversion_pipeline_ = grey_conversion_pipeline_ = display_filter_pipeline_ = blit_pipeline_ =
            depth_resolve_pipeline_ = VK_NULL_HANDLE;
        ui_solid_pipeline_ = ui_textured_pipeline_ = depth_carry_pipeline_ = color_carry_pipeline_ = VK_NULL_HANDLE;
        for (auto& [key, sampler] : samplers_) vkDestroySampler(device, sampler, nullptr);
        samplers_.clear();
        sampler_tables_.clear();
        constants_sets_.clear();
        for (EfbSet* set : {&efb_, &alt_efb_}) {
            for (VkFramebuffer framebuffer : {set->framebuffer, set->resolve_framebuffer}) {
                if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
            set->framebuffer = set->resolve_framebuffer = VK_NULL_HANDLE;
            set->color.reset();
            set->depth.reset();
            set->ms_color.reset();
            set->ms_depth.reset();
            set->samples = 1u;
        }
        alt_efb_scale_ = 0u;
        output_efb_active_ = false;
        efb_carry_color_.reset();
        efb_carry_depth_.reset();
        corner_texture_.reset();
        peek_target_.reset();
        field_target_.reset();
        peek_readback_ = Readback{};
        for (Readback& readback : pointer_depth_ring_) readback = Readback{};
        efb_capture_ = Readback{};
        backbuffer_capture_ = Readback{};
        vertex_ring_.reset();
        index_ring_.reset();
        constant_ring_.reset();
        matrix_ring_.reset();
        if (persistent_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, persistent_pool_, nullptr);
        for (VkDescriptorPool pool : persistent_overflow_) vkDestroyDescriptorPool(device, pool, nullptr);
        persistent_pool_ = VK_NULL_HANDLE;
        persistent_overflow_.clear();
        if (timestamps_ != VK_NULL_HANDLE) vkDestroyQueryPool(device, timestamps_, nullptr);
        timestamps_ = VK_NULL_HANDLE;
        if (pass_queries_ != VK_NULL_HANDLE) vkDestroyQueryPool(device, pass_queries_, nullptr);
        pass_queries_ = VK_NULL_HANDLE;
        if (stats_queries_ != VK_NULL_HANDLE) vkDestroyQueryPool(device, stats_queries_, nullptr);
        stats_queries_ = VK_NULL_HANDLE;
        if (draw_queries_ != VK_NULL_HANDLE) vkDestroyQueryPool(device, draw_queries_, nullptr);
        draw_queries_ = VK_NULL_HANDLE;
        destroy_swapchain();
        for (VkRenderPass pass :
             {present_pass_, color_pass_, grey_pass_, gx_layout_.efb_pass, gx_layout_.efb_pass_clear}) {
            if (pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, pass, nullptr);
        }
        for (auto* passes : {&gx_layout_.ms_passes, &gx_layout_.ms_passes_clear}) {
            for (VkRenderPass& pass : *passes) {
                if (pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, pass, nullptr);
                pass = VK_NULL_HANDLE;
            }
        }
        present_pass_ = color_pass_ = grey_pass_ = gx_layout_.efb_pass = gx_layout_.efb_pass_clear = VK_NULL_HANDLE;
        for (VkPipelineLayout layout : {gx_layout_.layout, utility_layout_, conversion_layout_}) {
            if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout, nullptr);
        }
        gx_layout_.layout = utility_layout_ = conversion_layout_ = VK_NULL_HANDLE;
        for (VkDescriptorSetLayout layout : {gx_layout_.sets[0], gx_layout_.sets[1], gx_layout_.sets[2],
                                             utility_set_layout_, conversion_set_layout_}) {
            if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
        gx_layout_.sets.fill(VK_NULL_HANDLE);
        utility_set_layout_ = conversion_set_layout_ = VK_NULL_HANDLE;
        for (VkSampler sampler : {linear_clamp_, point_clamp_}) {
            if (sampler != VK_NULL_HANDLE) vkDestroySampler(device, sampler, nullptr);
        }
        linear_clamp_ = point_clamp_ = VK_NULL_HANDLE;
        context.collect();
    }
    if (surface_ != VK_NULL_HANDLE && context.instance() != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(context.instance(), surface_, nullptr);
    }
    surface_ = VK_NULL_HANDLE;
    context.shutdown();
    present_pacer_.reset();
    host_window::close_game_window(window_owner_);
    window_ = nullptr;
    window_shown_ = false;
}

void RendererVk::drain_gpu() {
    const VkDevice device = vk::Context::get().device();
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        vk::Context::get().collect();
    }
}

bool RendererVk::quit_requested() const noexcept {
    return host_window::quit_request.load(std::memory_order_acquire);
}

UploadBuffer RendererVk::create_upload_buffer(std::size_t bytes, const char* name) {
    return gpu::create_upload_memory(device(), bytes, name);
}

UploadBuffer RendererVk::create_resident_vertices(std::size_t bytes, const char* name) {
    std::shared_ptr<vk::Buffer> buffer = vk::Context::get().create_buffer(
        bytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        name, vk::BufferPlace::DeviceMapped);
    if (!buffer) {
        return {};
    }
    UploadBuffer out;
    out.cpu = buffer->mapped;
    out.gpu = buffer->address;
    out.buffer = std::move(buffer);
    return out;
}

std::uint64_t RendererVk::completed_fence_value() const {
    return vk::Context::get().completed_value();
}

void RendererVk::wait_for_fence_value(std::uint64_t fence_value) {
    if (fence_value == 0u) {
        throw std::runtime_error("invalid frame-specific GPU fence wait");
    }
    if (!vk::Context::get().wait(fence_value, 10'000'000'000ull)) {
        throw std::runtime_error("frame-specific GPU completion wait timed out or the device was lost");
    }
}

void RendererVk::begin_frame(bool) {
    host_window::poll_render_config_file();
    follow_window_and_resize();
    apply_pending_efb_scale();
    update_ring_mode();
    sync_post_process();
    bloom_.poll_ready();

    frame_slot_ = static_cast<unsigned>(frame_index_ % kFramesInFlight);
    Frame& frame = frames_[frame_slot_];
    frame_ = &frame;
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    const bool collect_timing = trace_present_stats_enabled();
    const auto wait_start = collect_timing ? std::chrono::steady_clock::now()
                                           : std::chrono::steady_clock::time_point{};
    if (frame.fence != 0u) {
        wait_for_fence_value(frame.fence);
    }
    if (collect_timing) {
        last_frame_slot_wait_us_ = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wait_start).count());
    }
    const auto reset_start = collect_timing ? std::chrono::steady_clock::now()
                                            : std::chrono::steady_clock::time_point{};
    if (timestamp_pending_[frame_slot_] && timestamps_ != VK_NULL_HANDLE) {
        std::uint64_t ticks[2]{};
        if (vkGetQueryPoolResults(device, timestamps_, frame_slot_ * 2u, 2u, sizeof(ticks), ticks,
                                  sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ticks[1] >= ticks[0]) {
            const double gpu_ms = static_cast<double>(ticks[1] - ticks[0]) * timestamp_period_ns_ / 1'000'000.0;
            stats_.gpu_ms += gpu_ms;
            ++stats_.gpu_samples;
            gpu_budget_ms_ += gpu_ms;
            gpu_window_.ms += gpu_ms;
            gpu_window_.max_ms = std::max(gpu_window_.max_ms, gpu_ms);
            ++gpu_window_.frames;
        }
        timestamp_pending_[frame_slot_] = false;
    }
    read_gpu_passes();
    context.collect();
    frame.retired.clear();
    refresh_default_sampler_table();
    vk::check(vkResetCommandPool(device, frame.pool, 0), "vkResetCommandPool");
    vk::check(vkResetDescriptorPool(device, frame.descriptors, 0), "vkResetDescriptorPool");
    for (VkDescriptorPool pool : frame.overflow_descriptors) {
        vkDestroyDescriptorPool(device, pool, nullptr);
    }
    frame.overflow_descriptors.clear();
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk::check(vkBeginCommandBuffer(frame.uploads, &begin), "vkBeginCommandBuffer");
    vk::check(vkBeginCommandBuffer(frame.commands, &begin), "vkBeginCommandBuffer");
    commands_ = frame.commands;
    ++curve_sort_list_;
    curve_sort_[frame_slot_].draws.clear();
    curve_sort_[frame_slot_].bytes = 0u;
    curve_sort_[frame_slot_].sources = {};
    if (timestamps_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(frame.commands, timestamps_, frame_slot_ * 2u, 2u);
        vkCmdWriteTimestamp(frame.commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamps_, frame_slot_ * 2u);
    }
    if (pass_queries_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(frame.uploads, pass_queries_, kFramesInFlight * kGpuPassSlots + frame_slot_ * 2u, 2u);
        vkCmdResetQueryPool(frame.commands, pass_queries_, frame_slot_ * kGpuPassSlots, kGpuPassSlots);
        current_pass_ = GpuPass::Count;
        pass_recording_ = true;
        gpu_pass(GpuPass::Scene);
    }
    if (stats_queries_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(frame.commands, stats_queries_, frame_slot_, 1u);
        vkCmdBeginQuery(frame.commands, stats_queries_, frame_slot_, 0);
    }
    if (draw_queries_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(frame.commands, draw_queries_, frame_slot_ * kGpuDrawSlots, kGpuDrawSlots);
        pass_recording_ = true;
    }
    if (ring_mode_ == RingMode::Interp) {
        // One segment per game frame: its frames (chunks) append to it, so its draws stay replayable until the
        // segment comes round again.
        if (ring_advance_pending_) {
            advance_ring_segment();
        }
    } else {
        vertex_ring_.begin_frame(frame_slot_);
        index_ring_.begin_frame(frame_slot_);
        constant_ring_.begin_frame(frame_slot_);
        matrix_ring_.begin_frame(frame_slot_);
    }
    reset_gx_bindings();
    efb_pass_open_ = false;
    swap_image_ = UINT32_MAX;
    if (efb_carry_color_ || efb_carry_depth_) {
        carry_over_efb_contents();
    }
    if (collect_timing) {
        last_begin_reset_us_ = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - reset_start).count());
    }
}

std::uint64_t RendererVk::end_frame(bool present_swap_chain, SubmissionCallback on_submitted,
                                    void* submission_context) {
    Frame& frame = frames_[frame_slot_];
    end_efb_pass();
    if (timestamps_ != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(frame.commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_,
                            frame_slot_ * 2u + 1u);
    }
    if (pass_queries_ != VK_NULL_HANDLE) {
        gpu_pass(GpuPass::Count);
    }
    pass_recording_ = false;
    if (stats_queries_ != VK_NULL_HANDLE) {
        vkCmdEndQuery(frame.commands, stats_queries_, frame_slot_);
        stats_pending_[frame_slot_] = true;
    }
    if (!curve_sort_[frame_slot_].draws.empty()) {
        const std::uint32_t sort_query = kFramesInFlight * kGpuPassSlots + frame_slot_ * 2u;
        if (pass_queries_ != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(frame.uploads, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pass_queries_, sort_query);
        }
        record_curve_sort(frame.uploads);
        if (pass_queries_ != VK_NULL_HANDLE) {
            vkCmdWriteTimestamp(frame.uploads, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pass_queries_, sort_query + 1u);
            pass_sort_timed_[frame_slot_] = true;
        }
    }
    vk::check(vkEndCommandBuffer(frame.uploads), "vkEndCommandBuffer");
    vk::check(vkEndCommandBuffer(frame.commands), "vkEndCommandBuffer");
    const VkCommandBuffer buffers[] = {frame.uploads, frame.commands};
    const bool presenting = present_swap_chain && swap_image_ != UINT32_MAX;
    vk::Context& context = vk::Context::get();
    const std::uint64_t fence = presenting
        ? context.submit_frame(buffers, frame.acquired, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               swapchain_.rendered[swap_image_])
        : context.submit_frame(buffers);
    frame.fence = fence;
    last_submitted_ = fence;
    if (ring_mode_ == RingMode::Interp) {
        segments_[ring_segment_].fence = std::max(segments_[ring_segment_].fence, fence);
    }
    timestamp_pending_[frame_slot_] = timestamps_ != VK_NULL_HANDLE;
    if (pointer_depth_in_frame_) {
        pointer_depth_fence_ = fence;
        pointer_depth_in_frame_ = false;
    }
    for (AsyncField& field : async_fields_) {
        if (field.fence == kAsyncPeekInFrame) {
            field.fence = fence;
        }
    }
    if (efb_capture_.buffer && efb_capture_.fence == 0u && !efb_capture_pending_path_.empty()) {
        efb_capture_.fence = fence;
    }
    if (backbuffer_capture_.buffer && backbuffer_capture_.fence == 0u &&
        !backbuffer_capture_pending_path_.empty()) {
        backbuffer_capture_.fence = fence;
    }
    if (on_submitted != nullptr) {
        on_submitted(submission_context, fence);
    }
    if (presenting) {
        present_swapchain_image();
    }
    surface_lock_ = {};  // an image acquired but not presented
    swap_image_ = UINT32_MAX;
    ++frame_index_;
    // Every 600 frames (Linux, Android): the render thread's CPU time and how much of it the driver's calls took.
    if (frame_index_ % 600u == 0u) {
        static std::int64_t last_cpu = 0;
        static auto last_time = std::chrono::steady_clock::now();
        const std::int64_t cpu = vk::thread_cpu_ns();
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - last_time).count();
        const auto driver = vk::take_driver_cpu();
        if (cpu != 0 && last_cpu != 0 && seconds > 0.0) {
            const auto per_second = [&](std::int64_t ns) { return static_cast<double>(ns) / 1e6 / seconds; };
            std::fprintf(stderr, "[vk-cpu] render thread %.1f ms/s; in the driver: acquire %.1f, present %.1f, submit %.1f, "
                         "fence waits %.1f, fence polls %.1f ms/s\n", per_second(cpu - last_cpu),
                         per_second(driver[0]), per_second(driver[1]), per_second(driver[2]), per_second(driver[3]),
                         per_second(driver[4]));
        }
        last_cpu = cpu;
        last_time = now;
        if (gpu_window_.frames != 0u) {
            const double frames = static_cast<double>(gpu_window_.frames);
            std::fprintf(stderr, "[vk-gpu] frame %.2f ms average, %.2f ms longest; %.1f EFB passes and %.1f EFB copies a "
                         "frame; EFB %ux%u, swap chain %ux%u\n", gpu_window_.ms / frames, gpu_window_.max_ms,
                         static_cast<double>(gpu_window_.passes) / 600.0,
                         static_cast<double>(gpu_window_.copies) / 600.0, kEfbWidth * efb_scale_,
                         kEfbHeight * efb_scale_, swapchain_.image.width, swapchain_.image.height);
        }
        gpu_window_ = GpuWindow{};
    }
    return fence;
}

// Presents the acquired image after the submission that draws it (end_frame's and a sub-frame's), paced to the
// frame rate, on Android at the display slot it was meant for (VK_GOOGLE_display_timing).
void RendererVk::present_swapchain_image() {
    vk::Context& context = vk::Context::get();
    const RenderConfig cfg = get_render_config();
    const double pace_wait_ms = present_pacer_.pace(cfg.vsync_interval, cfg.max_fps);
    const std::int64_t present_start = galaxy::qpc_now();
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &swapchain_.rendered[swap_image_];
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain_.handle;
    info.pImageIndices = &swap_image_;
    VkPresentTimeGOOGLE present_time{};
    VkPresentTimesInfoGOOGLE present_times{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE};
    std::int64_t retrace = 0, period = 0, vsync = 0, now = 0;
    if (context.display_timing() && host_window::present_grid(retrace, period, vsync, now)) {
        PresentTiming& timing = present_timing_;
        // Interpolating, slots step by one presented frame: the game's retrace period times 60 / the present rate
        // (90 Hz: 1.5 presents per retrace).
        const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, display_refresh_hz_);
        const std::int64_t step = ring_mode_ == RingMode::Interp && rate > 61.0
            ? std::llround(static_cast<double>(period) * 60.0 / rate)
            : period;
        // The slot after the previous frame's, on the grid of the latest retrace; two retraces after that retrace
        // when it is out of reach (the first frame, after a stall) or the frames ran ahead of it.
        std::int64_t slot = timing.last_slot + step;
        const std::int64_t first = retrace + 2 * period;
        if (timing.last_slot == 0 || slot > first + period || slot < retrace + step) {
            slot = first;
        } else {
            slot = retrace + (slot - retrace + step / 2) / step * step;
        }
        // A frame too late for its slot (the compositor takes it about a vsync ahead) takes a later one.
        while (slot - vsync < now) {
            slot += step;
        }
        timing.last_slot = slot;
        timing.vsync_ns = vsync;
        present_time.presentID = ++timing.next_id;
        present_time.desiredPresentTime = static_cast<std::uint64_t>(slot - vsync / 2);
        timing.requested[present_time.presentID % timing.requested.size()] = slot;
        present_times.swapchainCount = 1;
        present_times.pTimes = &present_time;
        info.pNext = &present_times;
    } else {
        present_timing_.last_slot = 0;
    }
    const std::int64_t present_cpu = vk::thread_cpu_ns();
    const VkResult result = vkQueuePresentKHR(context.queue(), &info);
    vk::note_driver_cpu(vk::DriverCall::Present, vk::thread_cpu_ns() - present_cpu);
    surface_lock_ = {};
    if (result == VK_ERROR_OUT_OF_DATE_KHR || (result == VK_SUBOPTIMAL_KHR && surface_changed())) {
        swapchain_dirty_ = true;
    } else if (result == VK_ERROR_SURFACE_LOST_KHR) {
        swapchain_dirty_ = true;
        surface_lost_ = true;
    } else if (result != VK_SUCCESS) {
        vk::check(result, "vkQueuePresentKHR");
    }
    const std::int64_t present_end = galaxy::qpc_now();
    swap_present_qpc_ = present_end;
    if (trace_present_stats_enabled()) {
        const double present_ms = static_cast<double>(present_end - present_start) * 1000.0 /
            static_cast<double>(galaxy::qpc_frequency());
        stats_.present_ms += present_ms;
        stats_.present_max_ms = std::max(stats_.present_max_ms, present_ms);
        stats_.pace_wait_ms += pace_wait_ms;
        stats_.pace_wait_max_ms = std::max(stats_.pace_wait_max_ms, pace_wait_ms);
    }
    // Presents per second over the last second (the FRAME RATE row).
    ++measured_present_count_;
    if (measured_present_window_qpc_ == 0) {
        measured_present_window_qpc_ = present_end;
        measured_present_count_ = 0u;
    } else if (present_end - measured_present_window_qpc_ >= galaxy::qpc_frequency()) {
        const double hz = static_cast<double>(measured_present_count_) * static_cast<double>(galaxy::qpc_frequency()) /
            static_cast<double>(present_end - measured_present_window_qpc_);
        measured_present_hz_.store(hz, std::memory_order_relaxed);
        host_window::measured_present_hz.store(hz, std::memory_order_relaxed);
        measured_present_window_qpc_ = present_end;
        measured_present_count_ = 0u;
    }
    report_present_stats();
    // Android keeps only the last few frames' timings: read them often.
    if (context.display_timing() && present_timing_.next_id % 4u == 0u) {
        report_present_timing();
    }
    if (!window_shown_ && window_ != nullptr && !host_window::hidden_window_mode_enabled()) {
        window_shown_ = true;
        host_window::show_window(window_);
    }
}

// The past presentation times (VK_GOOGLE_display_timing): whether each frame was shown by the slot it asked for, and
// one retrace period after the frame before it. Every 600 frames to the log.
void RendererVk::report_present_timing() {
    const VkDevice device = vk::Context::get().device();
    if (swapchain_.handle == VK_NULL_HANDLE) {
        return;
    }
    PresentTiming& timing = present_timing_;
    if (timing.refresh_ns == 0) {
        VkRefreshCycleDurationGOOGLE cycle{};
        if (vkGetRefreshCycleDurationGOOGLE(device, swapchain_.handle, &cycle) == VK_SUCCESS) {
            timing.refresh_ns = static_cast<std::int64_t>(cycle.refreshDuration);
        }
    }
    std::array<VkPastPresentationTimingGOOGLE, 64> past{};
    auto count = static_cast<std::uint32_t>(past.size());
    if (vkGetPastPresentationTimingGOOGLE(device, swapchain_.handle, &count, past.data()) < VK_SUCCESS) {
        return;
    }
    constexpr std::int64_t kRetraceNs = 1000000000 / 60;
    const std::int64_t half_vsync = (timing.vsync_ns != 0 ? timing.vsync_ns : kRetraceNs / 2) / 2;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto actual = static_cast<std::int64_t>(past[i].actualPresentTime);
        const std::int64_t requested = timing.requested[past[i].presentID % timing.requested.size()];
        if (requested == 0) {
            ++timing.unrequested;
        } else if (actual <= requested + half_vsync) {
            ++timing.on_slot;
        } else {
            ++timing.late;
        }
        if (timing.last_actual != 0 && std::llabs(actual - timing.last_actual - kRetraceNs) < half_vsync) {
            ++timing.even;
        }
        timing.last_actual = actual;
        if (++timing.frames == 600u) {
            std::fprintf(stderr, "[present-timing] %u frames: %u shown at their retrace slot, %u late, %u without one "
                         "(the host's clock); %u shown one retrace after the frame before (refresh %.2f ms)\n",
                         timing.frames, timing.on_slot, timing.late, timing.unrequested, timing.even,
                         static_cast<double>(timing.refresh_ns) / 1e6);
            timing.frames = timing.on_slot = timing.late = timing.unrequested = timing.even = 0u;
        }
    }
}

void RendererVk::report_present_stats() {
    if (!trace_present_stats_enabled()) {
        return;
    }
    const std::int64_t now = galaxy::qpc_now();
    if (stats_.window_start_qpc == 0) {
        stats_.window_start_qpc = now;
    }
    if (stats_.last_present_qpc != 0) {
        const double interval = static_cast<double>(now - stats_.last_present_qpc) * 1000.0 /
            static_cast<double>(galaxy::qpc_frequency());
        stats_.interval_ms += interval;
        stats_.interval_max_ms = std::max(stats_.interval_max_ms, interval);
        ++stats_.intervals;
    }
    stats_.last_present_qpc = now;
    ++stats_.presents;
    const double seconds = static_cast<double>(now - stats_.window_start_qpc) / static_cast<double>(galaxy::qpc_frequency());
    if (seconds < 2.0) {
        return;
    }
    static std::uint64_t first_serial_start = 0;
    static std::uint64_t copies_start = 0;
    const XfbPresentStats progress = xfb_present_stats();
    const std::uint64_t copies = xfb_copy_count();
    const double presents = static_cast<double>(stats_.presents);
    std::ostringstream message;
    message << "[present-stats] fps=" << presents / seconds << " new-serial-hz="
            << static_cast<double>(progress.first_serial_presentations - first_serial_start) / seconds
            << " xfb-production-hz=" << static_cast<double>(copies - copies_start) / seconds
            << " frame-ms(avg/max)=" << (stats_.intervals != 0u ? stats_.interval_ms / stats_.intervals : 0.0)
            << '/' << stats_.interval_max_ms << " pace-wait-ms(avg/max)=" << stats_.pace_wait_ms / presents << '/'
            << stats_.pace_wait_max_ms << " present-ms(avg/max)=" << stats_.present_ms / presents << '/'
            << stats_.present_max_ms << " vsync=" << get_render_config().vsync_interval << " present-mode="
            << static_cast<int>(swapchain_.mode) << " gpu-ms(avg)=";
    if (stats_.gpu_samples != 0u) {
        message << stats_.gpu_ms / static_cast<double>(stats_.gpu_samples);
    } else {
        message << "unavailable";
    }
    message << " gpu-samples=" << stats_.gpu_samples << " gpu-total-ms=" << stats_.gpu_ms;
    if (pass_queries_ != VK_NULL_HANDLE) {
        static constexpr const char* kPassNames[] = {"scene", "resolve", "efb-copy", "xfb-copy", "bloom", "scene-aa",
                                                     "scene-ao", "upscale", "rt", "present", "curve-sort"};
        static_assert(std::size(kPassNames) == static_cast<std::size_t>(GpuPass::Count));
        message << " gpu-pass-ms=";
        for (std::size_t pass = 0; pass < std::size(kPassNames); ++pass) {
            message << (pass == 0 ? "" : ",") << kPassNames[pass] << ':' << pass_ms_[pass] / presents;
        }
        pass_ms_.fill(0.0);
        // Flag bits in order: vertex, clipped primitives, fragment, control patches, evaluation, compute.
        const std::array<double, 6>& st = stats_sum_;
        message << " gpu-stats=vs:" << st[0] / presents << ",hs:" << st[3] / presents << ",ds:" << st[4] / presents
                << ",prims:" << st[1] / presents << ",ps:" << st[2] / presents << ",cs:" << st[5] / presents;
        stats_sum_.fill(0.0);
    }
    message << " api=vulkan\n";
    if (!draw_cost_.empty()) {
        std::vector<std::pair<gpu::Pipeline*, GpuDrawCost>> costs(draw_cost_.begin(), draw_cost_.end());
        std::sort(costs.begin(), costs.end(), [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
        double total = 0.0;
        for (const auto& cost : costs) {
            total += cost.second.ms;
        }
        message << "[gpu-draws] total-ms=" << total / presents << " pipelines=" << costs.size() << '\n';
        for (std::size_t i = 0; i < costs.size() && i < 24u; ++i) {
            message << "[gpu-draws] pso=" << static_cast<const void*>(costs[i].first)
                    << " ms=" << costs[i].second.ms / presents
                    << " draws=" << static_cast<double>(costs[i].second.draws) / presents
                    << " tris=" << static_cast<double>(costs[i].second.triangles) / presents << '\n';
        }
        draw_cost_.clear();
    }
    std::cerr << message.str();
    first_serial_start = progress.first_serial_presentations;
    copies_start = copies;
    stats_ = PresentStats{};
    stats_.window_start_qpc = now;
    stats_.last_present_qpc = now;
}

void RendererVk::gpu_pass(GpuPass pass) {
    if (pass == current_pass_) {
        return;
    }
    current_pass_ = pass;
    unsigned& marks = pass_mark_count_[frame_slot_];
    const VkCommandBuffer commands = frames_[frame_slot_].commands;
    if (pass_queries_ == VK_NULL_HANDLE || !pass_recording_ || commands_ != commands || marks + 1u >= kGpuPassSlots) {
        return;
    }
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pass_queries_,
                        frame_slot_ * kGpuPassSlots + marks);
    pass_marks_[frame_slot_][marks++] = pass;
}

// The slot's previous frame (its fence passed): each mark's time to the next.
void RendererVk::read_gpu_passes() {
    const VkDevice device = vk::Context::get().device();
    const double ms_per_tick = timestamp_period_ns_ / 1'000'000.0;
    std::vector<GpuDrawMark>& draws = draw_marks_[frame_slot_];
    if (!draws.empty()) {
        std::vector<std::uint64_t> stamps(2u * draws.size());
        if (vkGetQueryPoolResults(device, draw_queries_, frame_slot_ * kGpuDrawSlots,
                                  static_cast<std::uint32_t>(stamps.size()), stamps.size() * sizeof(std::uint64_t),
                                  stamps.data(), sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            for (std::size_t i = 0; i < draws.size(); ++i) {
                GpuDrawCost& cost = draw_cost_[draws[i].pipeline];
                if (stamps[2u * i + 1u] >= stamps[2u * i]) {
                    cost.ms += static_cast<double>(stamps[2u * i + 1u] - stamps[2u * i]) * ms_per_tick;
                }
                ++cost.draws;
                cost.triangles += draws[i].triangles;
            }
        }
        draws.clear();
    }
    if (pass_queries_ == VK_NULL_HANDLE) {
        return;
    }
    const unsigned marks = pass_mark_count_[frame_slot_];
    std::array<std::uint64_t, kGpuPassSlots> ticks{};
    if (marks > 1u && vkGetQueryPoolResults(device, pass_queries_, frame_slot_ * kGpuPassSlots, marks,
                                            marks * sizeof(std::uint64_t), ticks.data(), sizeof(std::uint64_t),
                                            VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
        for (unsigned i = 0; i + 1u < marks; ++i) {
            if (ticks[i + 1u] >= ticks[i]) {
                pass_ms_[static_cast<std::size_t>(pass_marks_[frame_slot_][i])] +=
                    static_cast<double>(ticks[i + 1u] - ticks[i]) * ms_per_tick;
            }
        }
    }
    if (pass_sort_timed_[frame_slot_] &&
        vkGetQueryPoolResults(device, pass_queries_, kFramesInFlight * kGpuPassSlots + frame_slot_ * 2u, 2u,
                              2u * sizeof(std::uint64_t), ticks.data(), sizeof(std::uint64_t),
                              VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
        ticks[1] >= ticks[0]) {
        pass_ms_[static_cast<std::size_t>(GpuPass::CurveSort)] += static_cast<double>(ticks[1] - ticks[0]) * ms_per_tick;
    }
    pass_mark_count_[frame_slot_] = 0u;
    pass_sort_timed_[frame_slot_] = false;
    std::array<std::uint64_t, 6> counts{};
    if (stats_pending_[frame_slot_] &&
        vkGetQueryPoolResults(device, stats_queries_, frame_slot_, 1u, sizeof(counts), counts.data(), sizeof(counts),
                              VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
        for (std::size_t i = 0; i < counts.size(); ++i) {
            stats_sum_[i] += static_cast<double>(counts[i]);
        }
    }
    stats_pending_[frame_slot_] = false;
}

// The share of wall time the GPU may spend on the frame before Match Output steps down (RendererD3D12's rule).
constexpr double kGpuBudgetBusy = 0.80;

void RendererVk::update_gpu_budget() {
    const std::int64_t now = galaxy::qpc_now();
    if (gpu_budget_window_qpc_ == 0) {
        gpu_budget_window_qpc_ = now;
        gpu_budget_ms_ = 0.0;
        return;
    }
    const double window_ms = static_cast<double>(now - gpu_budget_window_qpc_) * 1000.0 /
        static_cast<double>(galaxy::qpc_frequency());
    if (window_ms < 2000.0) {
        return;
    }
    const double busy = gpu_budget_ms_ / window_ms;
    gpu_budget_window_qpc_ = now;
    gpu_budget_ms_ = 0.0;
    const RenderConfig cfg = get_render_config();
    if (!cfg.gpu_budget || !cfg.internal_follow_window || efb_scale_ <= 1u || busy <= kGpuBudgetBusy) {
        gpu_budget_over_windows_ = 0u;
        return;
    }
    if (++gpu_budget_over_windows_ < 2u) {
        return;
    }
    gpu_budget_over_windows_ = 0u;
    gpu_budget_cap_ = efb_scale_ - 1u;
    follow_window_width_ = 0u;  // re-derive the scale under the cap at this frame boundary
    std::cerr << "[Renderer] GPU busy " << static_cast<int>(busy * 100.0 + 0.5) << "% at internal resolution "
              << efb_scale_ << "x; Match Output capped at " << gpu_budget_cap_ << "x\n";
}

void RendererVk::sync_post_process() {
    const bool was_ready = post_.ready();
    if (!post_.poll_ready()) {
        return;
    }
    if (!was_ready) {
        set_post_process_status(post_.status());
    }
    const PostProcessSettings next = post_process_settings_from(get_render_config());
    const PostProcessSettings& current = post_.settings();
    if (next.aa != current.aa || next.smaa_quality != current.smaa_quality || next.upscaler != current.upscaler ||
        next.sharpness != current.sharpness) {
        post_.apply_settings(next);
        set_post_process_status(post_.status());
    }
}

bool RendererVk::vblank_grid(std::int64_t& vblank, std::int64_t& period) const noexcept {
    return window_ != nullptr && host_window::vblank_grid(window_, vblank, period);
}

void RendererVk::refresh_display_rate() {
    const double hz = host_window::display_refresh_hz(window_);
    if (hz <= 1.0) {
        return;
    }
    if (std::abs(hz - display_refresh_hz_) >= 0.1) {
        std::cerr << "[display] refresh-hz=" << hz << '\n';
        display_refresh_hz_ = hz;
    }
}

// Swapchain changes (window size, VSYNC) and Match Output's internal resolution, between frames.
void RendererVk::follow_window_and_resize() {
    // The window's surface: dropped while withdrawn or after the driver lost it, made again once it is back.
    if (surface_lost_ || host_window::surface_withdrawn.load(std::memory_order_acquire)) {
        release_surface();
        surface_lost_ = false;
    }
    if (surface_ == VK_NULL_HANDLE &&
        (host_window::surface_withdrawn.load(std::memory_order_acquire) || !restore_surface())) {
        return;
    }
    const std::uint64_t extent = host_window::pending_resize_extent.exchange(0u, std::memory_order_acq_rel);
    if (extent != 0u) {
        const auto width = static_cast<std::uint32_t>(extent >> 32u);
        const auto height = static_cast<std::uint32_t>(extent & 0xffffffffu);
        if (width != swapchain_.extent.width || height != swapchain_.extent.height) {
            swapchain_dirty_ = true;
        }
    }
    const bool vsync = get_render_config().vsync_interval != 0u;
    if (vsync != (swapchain_.mode == VK_PRESENT_MODE_FIFO_KHR)) {
        swapchain_dirty_ = true;
    }
    if (swapchain_dirty_) {
        vkDeviceWaitIdle(vk::Context::get().device());
        create_swapchain();
    }
    if (display_refresh_checks_++ % 120u == 0u) {
        refresh_display_rate();
    }

    update_gpu_budget();
    // Match Output: the smallest scale covering the picture on both axes (the launcher's rule, as RendererD3D12).
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const double content_aspect = aspect ? aspect->content_aspect : galaxy::kWiiContentAspect;
    if (host_window::follow_window_rearm.load(std::memory_order_relaxed) &&
        host_window::follow_window_rearm.exchange(false, std::memory_order_acq_rel)) {
        follow_window_width_ = 0u;
    }
    if (follow_window_retry_ms_ != 0u && galaxy::steady_ms() >= follow_window_retry_ms_) {
        follow_window_retry_ms_ = 0u;
        follow_window_width_ = 0u;
    }
    if (host_window::backbuffer_width == follow_window_width_ &&
        host_window::backbuffer_height == follow_window_height_ && content_aspect == follow_window_aspect_) {
        return;
    }
    if (host_window::move_size_active.load(std::memory_order_acquire)) {
        return;  // an edge drag resizes every frame: settle first
    }
    follow_window_width_ = host_window::backbuffer_width;
    follow_window_height_ = host_window::backbuffer_height;
    follow_window_aspect_ = content_aspect;
    RenderConfig render = get_render_config();
    if (!render.internal_follow_window) {
        return;
    }
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        content_aspect);
    if (content.width <= 0 || content.height <= 0) {
        return;
    }
    const double needed = std::max(content.width / static_cast<double>(kEfbWidth), content.height / 456.0);
    unsigned scale = std::clamp(static_cast<unsigned>(std::ceil(needed - 1e-6)), 1u, kMaxEfbScale);
    if (gpu_budget_cap_ != 0u) {
        scale = std::min(scale, gpu_budget_cap_);
    }
    if (scale != render.efb_scale) {
        render.efb_scale = scale;
        set_render_config(render);
        stage_next_start_efb_scale(scale);
    }
}

bool RendererVk::present(const XfbTexture* source, bool capture_backbuffer, bool black) {
    const GpuPassScope pass{*this, GpuPass::Present};
    present_black_ = black;
    if (source == nullptr || !source->texture) {
        throw std::runtime_error("present requested without a valid XFB blit source");
    }
    debug_log_backbuffer_readback();
    end_efb_pass();
    vk::Context& context = vk::Context::get();
    Frame& frame = *frame_;
    surface_lock_ = std::unique_lock<std::timed_mutex>(host_window::surface_use);
    if (swapchain_dirty_ || swapchain_.handle == VK_NULL_HANDLE ||
        host_window::surface_withdrawn.load(std::memory_order_acquire)) {
        surface_lock_ = {};
        return false;  // remade at the next frame boundary
    }
    std::uint32_t image = 0;
    // A second at most: an image the presentation engine never returns (a surface going away) skips the frame.
    constexpr std::uint64_t kAcquireTimeoutNs = 1'000'000'000ull;
    const std::int64_t acquire_cpu = vk::thread_cpu_ns();
    VkResult result = vkAcquireNextImageKHR(context.device(), swapchain_.handle, kAcquireTimeoutNs, frame.acquired,
                                            VK_NULL_HANDLE, &image);
    vk::note_driver_cpu(vk::DriverCall::Acquire, vk::thread_cpu_ns() - acquire_cpu);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR || result == VK_TIMEOUT ||
        result == VK_NOT_READY) {
        swapchain_dirty_ = true;
        surface_lost_ = result == VK_ERROR_SURFACE_LOST_KHR;
        surface_lock_ = {};
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        swapchain_dirty_ = swapchain_dirty_ || surface_changed();  // presented now, remade after
    } else if (result != VK_SUCCESS) {
        vk::check(result, "vkAcquireNextImageKHR");
    }
    swap_image_ = image;
    const VkCommandBuffer commands = commands_;
    vk::Texture& xfb = *source->texture;
    if (xfb.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = xfb.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
        const VkClearColorValue black{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(commands, xfb.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &barrier);
        xfb.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    const auto aspect = galaxy::experimental_effective_host_aspect();
    const auto content_aspect = galaxy::presentation_content_aspect(
        aspect ? aspect->content_aspect : galaxy::kWiiContentAspect, source->prerecorded_movie);
    const int bb_w = static_cast<int>(swapchain_.extent.width);
    const int bb_h = static_cast<int>(swapchain_.extent.height);
    galaxy::ContentViewport content = galaxy::fit_content_viewport(bb_w, bb_h, content_aspect);
    if (content.width <= 0 || content.height <= 0) {
        content = galaxy::ContentViewport{0, 0, bb_w, bb_h};
    }
    (void)galaxy::g_presentation_content.publish(bb_w, bb_h, content);

    // The post chain's passes before the swap chain's: the picture scaled (and sharpened) to its size on screen. A
    // turned swap chain takes it through `oriented_` and the blit, which turns it.
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_post_set(layout); };
    vk::PostProcessorVk::PresentStep step{};
    const vk::Texture* picture = &xfb;
    if (post_.ready() && !present_black_) {
        const bool turned = swapchain_.transform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        const auto width = static_cast<std::uint32_t>(content.width);
        const auto height = static_cast<std::uint32_t>(content.height);
        step = post_.prepare_present(commands, xfb, xfb.width, xfb.height, width, height, !turned, allocate);
        if (turned && step.pipeline != VK_NULL_HANDLE) {
            if (!oriented_ || oriented_->width != width || oriented_->height != height) {
                oriented_ = context.create_texture(width, height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
                                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                                   true, "post-process picture");
                VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                info.renderPass = color_pass_;
                info.attachmentCount = 1;
                info.width = width;
                info.height = height;
                info.layers = 1;
                if (oriented_) {
                    info.pAttachments = &oriented_->target_view;
                    if (vkCreateFramebuffer(context.device(), &info, nullptr, &oriented_->framebuffer) != VK_SUCCESS) {
                        oriented_.reset();
                    }
                }
            }
            if (oriented_) {
                VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                begin.renderPass = color_pass_;
                begin.framebuffer = oriented_->framebuffer;
                begin.renderArea = {{0, 0}, {width, height}};
                vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
                post_.finish_present(commands, step, {{0, 0}, {width, height}}, allocate);
                vkCmdEndRenderPass(commands);
                oriented_->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                picture = oriented_.get();
            }
            step = {};
        }
    }

    const float surround = source->safety_surround;
    VkClearValue clear{};
    clear.color = {{surround, surround, surround, 1.0f}};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = present_pass_;
    begin.framebuffer = swapchain_.framebuffers[image];
    begin.renderArea = {{0, 0}, swapchain_.image};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    if (vk::Context::get().quirks().clear_by_command) {
        const VkClearAttachment attachment{VK_IMAGE_ASPECT_COLOR_BIT, 0, clear};
        const VkClearRect rect{{{0, 0}, swapchain_.image}, 0, 1};
        vkCmdClearAttachments(commands, 1, &attachment, 1, &rect);
    }

    // The picture, aspect-fitted (D3D-style viewport: y flipped by a negative height), turned into a pre-rotated image:
    // the post chain's last pass, or the blit of the XFB (or of the chain's turned picture).
    const VkDescriptorSet set = allocate_frame_set(utility_set_layout_);
    const VkDescriptorImageInfo image_info{VK_NULL_HANDLE, picture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(context.device(), 1, &write, 0, nullptr);
    const VkRect2D scissor =
        turned_rect(swapchain_.transform, bb_w, bb_h, content.left, content.top, content.width, content.height);
    const VkViewport viewport{static_cast<float>(scissor.offset.x),
                              static_cast<float>(scissor.offset.y) + static_cast<float>(scissor.extent.height),
                              static_cast<float>(scissor.extent.width), -static_cast<float>(scissor.extent.height),
                              0.0f, 1.0f};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    if (step.pipeline != VK_NULL_HANDLE) {
        post_.finish_present(commands, step, scissor, allocate);
    } else {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, blit_pipeline_);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, utility_layout_, 0, 1, &set, 0, nullptr);
        const std::uint32_t transform = static_cast<std::uint32_t>(swapchain_.transform);
        vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(transform), &transform);
        if (!present_black_) {
            vkCmdDraw(commands, 3, 1, 0, 0);
        }
    }

    // The overlays are authored in backbuffer pixels over the whole surface.
    const VkViewport full{0.0f, static_cast<float>(bb_h), static_cast<float>(bb_w), -static_cast<float>(bb_h), 0.0f,
                          1.0f};
    const VkRect2D full_scissor{{0, 0}, swapchain_.image};
    vkCmdSetViewport(commands, 0, 1, &full);
    vkCmdSetScissor(commands, 0, 1, &full_scissor);
    // The overlays are drawn unturned (Windows); turned images belong to platforms whose overlays are their own screens.
    if (swapchain_.transform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        host_window::draw_native_settings_overlay(*this);
        host_window::draw_home_menu(*this);
    }
    vkCmdEndRenderPass(commands);
    reset_gx_bindings();

    if (capture_backbuffer && swapchain_.copyable && !backbuffer_capture_path_.empty() &&
        backbuffer_capture_pending_path_.empty()) {
        // The finished image, copied out for a test snapshot (PRESENT_SRC -> TRANSFER_SRC and back).
        const std::uint64_t bytes = static_cast<std::uint64_t>(bb_w) * bb_h * 4u;
        if (!backbuffer_capture_.buffer || backbuffer_capture_.buffer->size < bytes) {
            backbuffer_capture_.buffer = context.create_readback_buffer(bytes, "backbuffer capture");
        }
        if (backbuffer_capture_.buffer) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = swapchain_.images[image];
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {swapchain_.image.width, swapchain_.image.height, 1};
            vkCmdCopyImageToBuffer(commands, swapchain_.images[image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   backbuffer_capture_.buffer->buffer, 1, &region);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = 0;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &barrier);
            backbuffer_capture_.fence = 0u;
            backbuffer_capture_pending_path_ = backbuffer_capture_path_;
            backbuffer_capture_width_ = swapchain_.image.width;
            backbuffer_capture_height_ = swapchain_.image.height;
            backbuffer_capture_bgra_ = swapchain_.format == VK_FORMAT_B8G8R8A8_UNORM;
            return true;
        }
    }
    return false;
}

void RendererVk::ensure_native_settings_corner_texture_loaded() {
    if (corner_texture_tried_) {
        return;
    }
    corner_texture_tried_ = true;
    if (host_window::native_settings_content_root.empty()) {
        return;
    }
    const auto texture = load_game_texture(host_window::native_settings_content_root,
                                           "files\\LayoutData\\FileSelect.arc", "myfileselwinframe.tpl");
    if (!texture.has_value() || texture->width == 0u || texture->height == 0u) {
        return;
    }
    vk::Context& context = vk::Context::get();
    corner_texture_ = context.create_texture(texture->width, texture->height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
                                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false,
                                             "settings corner");
    std::shared_ptr<vk::Buffer> staging = context.create_buffer(
        static_cast<std::uint64_t>(texture->width) * texture->height * 4u, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        "settings corner upload");
    if (!corner_texture_ || !staging) {
        corner_texture_.reset();
        return;
    }
    std::memcpy(staging->mapped, texture->rgba8.data(), static_cast<std::size_t>(staging->size));
    // Recorded with the frame's uploads, which run before its draws.
    const VkCommandBuffer commands = frames_[frame_slot_].uploads;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = corner_texture_->image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {texture->width, texture->height, 1};
    vkCmdCopyBufferToImage(commands, staging->buffer, corner_texture_->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &region);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
    corner_texture_->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // The staging buffer lives until this frame's submission completes.
    context.defer([staging] {});
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = persistent_pool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &utility_set_layout_;
    vk::check(vkAllocateDescriptorSets(context.device(), &alloc, &corner_set_), "corner set");
    const VkDescriptorImageInfo image_info{VK_NULL_HANDLE, corner_texture_->view,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = corner_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = &image_info;
    vkUpdateDescriptorSets(context.device(), 1, &write, 0, nullptr);
}

void RendererVk::draw_ui_textured_quad(int x, int y, int w, int h, float u0, float v0, float u1, float v1,
                                       const std::array<float, 4>& tint) {
    if (!corner_texture_ || corner_set_ == VK_NULL_HANDLE) {
        return;
    }
    const float constants[14] = {
        static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h),
        u0, v0, u1, v1, tint[0], tint[1], tint[2], tint[3],
        static_cast<float>(host_window::backbuffer_width), static_cast<float>(host_window::backbuffer_height),
    };
    const VkCommandBuffer commands = commands_;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_textured_pipeline_);
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, utility_layout_, 0, 1, &corner_set_, 0,
                            nullptr);
    vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(constants), constants);
    vkCmdDraw(commands, 4, 1, 0, 0);
}

void RendererVk::draw_ui_solid_quad(int x, int y, int w, int h, const std::array<float, 4>& color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    const int left = std::clamp(x, 0, static_cast<int>(host_window::backbuffer_width));
    const int top = std::clamp(y, 0, static_cast<int>(host_window::backbuffer_height));
    const int right = std::clamp(x + w, 0, static_cast<int>(host_window::backbuffer_width));
    const int bottom = std::clamp(y + h, 0, static_cast<int>(host_window::backbuffer_height));
    if (right <= left || bottom <= top) {
        return;
    }
    const UiSolidQuadConstants constants{
        {left, top, right - left, bottom - top},
        {color[0], color[1], color[2], color[3]},
        {host_window::backbuffer_width, host_window::backbuffer_height},
        {0u, 0u},
        0u,
    };
    const VkCommandBuffer commands = commands_;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_solid_pipeline_);
    vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(constants), &constants);
    vkCmdDraw(commands, 4, 1, 0, 0);
}

void RendererVk::draw_ui_glyph(int x, int y, int scale, std::uint64_t bitmap, const std::array<float, 4>& color) {
    if (scale <= 0 || bitmap == 0u || host_window::backbuffer_width == 0u || host_window::backbuffer_height == 0u) {
        return;
    }
    const UiSolidQuadConstants constants{
        {x, y, scale, scale},
        {color[0], color[1], color[2], color[3]},
        {host_window::backbuffer_width, host_window::backbuffer_height},
        {static_cast<std::uint32_t>(bitmap), static_cast<std::uint32_t>(bitmap >> 32u)},
        1u,
    };
    const VkCommandBuffer commands = commands_;
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, ui_solid_pipeline_);
    vkCmdPushConstants(commands, utility_layout_, kPushStages, 0, sizeof(constants), &constants);
    // One instance per cell, row-major; unlit cells have degenerate geometry.
    vkCmdDraw(commands, 4, 35, 0, 0);
}

}  // namespace galaxy::gx
