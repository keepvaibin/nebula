// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan renderer (renderer_vk.h): device, swapchain, frames, present and the overlay. The scene target, draws,
// copies and screen probes are renderer_vk_frame.cpp.

#include "renderer_vk_internal.h"
#include "galaxy/run_options.h"
#include "galaxy/render/vk/streamline_vk.h"
#if defined(_WIN32)
#include "galaxy/render/streamline_bridge.h"
#endif
#include "galaxy/render/vk/renderer_vk.h"
#include "galaxy/platform_time.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/render/host_window.h"
#if defined(_WIN32)
#include "galaxy/render/display_modes.h"
#include "galaxy/render/host_window_win32.h"
#endif
#include "galaxy/render/program_table.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/game_paths.h"

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

namespace galaxy::render {

void RendererVk::test_lose_device() {
    throw std::runtime_error("[RendererVk] VK_ERROR_DEVICE_LOST (test)");
}

namespace {

bool trace_present_stats_enabled() {
    static const bool enabled = ::galaxy::run_option_flag(::galaxy::run_options().trace_present_stats, false);
    return enabled;
}

constexpr std::uint32_t kPushConstantBytes = 64u;
constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

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
using vk_detail::image_barrier;
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

bool RendererVk::initialize(int window_width, int window_height, const FrameMap& frame) {
    scene_map_ = frame;
    frame_slot_ = 0;
    frame_index_ = 0;
    if (window_ != nullptr) {
        window_shown_ = true;
    } else {
        const host_window::OpenedWindow opened = host_window::open_game_window(*window_owner_, window_width, window_height);
        if (opened.window == nullptr) {
            return false;
        }
        window_ = opened.window;
        window_shown_ = opened.shown;
    }
    try {
        if (!create_surface_and_device() || !create_layouts() || !create_swapchain()) {
            shutdown();
            return false;
        }
        report_adapter();
        const VkDevice device = vk::Context::get().device();
        {
            VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool_info.queueFamilyIndex = vk::Context::get().queue_family();
            vk::check(vkCreateCommandPool(device, &pool_info, nullptr, &utility_pool_), "vkCreateCommandPool");
        }
        if (!create_scene_targets(scene_map_, wanted_msaa_samples()) || !create_utility_pipelines()) {
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
            frame.descriptors = create_pool(device, kFrameSets, vk_detail::frame_pool(!bindless_));
            VkSemaphoreCreateInfo semaphore_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vk::check(vkCreateSemaphore(device, &semaphore_info, nullptr, &frame.acquired), "vkCreateSemaphore");
        }
        if (vk::Context::get().properties().limits.timestampComputeAndGraphics == VK_TRUE) {
            VkQueryPoolCreateInfo query_info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            query_info.queryCount = kFramesInFlight * 2u;
            vk::check(vkCreateQueryPool(device, &query_info, nullptr, &timestamps_), "vkCreateQueryPool");
            timestamp_period_ns_ = vk::Context::get().properties().limits.timestampPeriod;
        }
        // The rings are Vulkan buffers with GPU addresses; their sizes follow the D3D12 renderer's. The vertex and row rings
        // keep the last frame readable for motion.
        const auto ring = [&](UploadRing& target, const char* name, std::size_t bytes, unsigned segments) {
            return target.initialize(device, name, UploadRing::budget(bytes, segments), segments, bytes);
        };
        if (!ring(vertex_ring_, "vertex", kVertexRingBytes, kRingSegments) || !ring(index_ring_, "index", kIndexRingBytes, kFramesInFlight) ||
            !ring(constant_ring_, "constant", kConstantRingBytes, kFramesInFlight) || !ring(matrix_ring_, "matrix", kMatrixRingBytes, kRingSegments)) {
            std::cerr << "[Renderer] upload rings unavailable\n";
            shutdown();
            return false;
        }
        gpu::Encoder::Layout encoding{};
        encoding.layout = draw_layout_.layout;
        encoding.constants_set = [](void* renderer, const VkBuffer* buffers, gpu::Address rows, gpu::Address instances,
                                    gpu::Address lists) {
            return static_cast<RendererVk*>(renderer)->constants_set(buffers, rows, instances, lists);
        };
        encoding.resolve = [](void*, gpu::Address address, VkBuffer& buffer, VkDeviceSize& offset) {
            const vk::Context::BufferOffset resolved = vk::Context::get().resolve(address);
            buffer = resolved.buffer;
            offset = resolved.offset;
        };
        encoding.renderer = this;
        encoder_.configure(encoding);
        reset_sampler_frame();
        // The game's bloom, compiled on a worker; the game draws its own chains until it is ready.
        bloom_.initialize();
        // Presentation post-processing, compiled on a worker; the plain blit presents until it is ready.
        const RenderConfig cfg = get_render_config();
        if (cfg.post_process) {
            post_.initialize(post_process_settings_from(cfg), draw_layout_.scene_pass, color_pass_, rgba8_pass_);
            if (!ao_.initialize()) {
                std::cerr << "[ambient-occlusion] off on this device" << std::endl;
            }
            if (!scene_camera_.initialize()) {
                std::cerr << "[scene-camera] off on this device" << std::endl;
            }
            post_.set_present_pass(present_pass_);
        } else {
            PostProcessStatus off{};
            std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "CONFIG OFF");
            set_post_process_status(off);
            std::cerr << "[post-process] off; presenting with the plain blit\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "[Renderer] Vulkan initialize failed: " << error.what() << '\n';
        init_error_ = error.what();
        shutdown();
        return false;
    }
    std::cout << "[Renderer] Vulkan scene target=" << scene_map_.width << 'x' << scene_map_.height
              << " depth=" << (depth_format_ == VK_FORMAT_D24_UNORM_S8_UINT ? "D24S8" : "D32FS8") << '\n';
    return true;
}

void RendererVk::report_adapter() {
    const vk::Context& context = vk::Context::get();
    const VkPhysicalDeviceProperties& properties = context.properties();
    const bool rtx = properties.vendorID == 0x10DEu && std::strstr(properties.deviceName, "RTX") != nullptr;
    host_window::rtx_adapter.store(rtx, std::memory_order_relaxed);
    const char* const preference = ::galaxy::run_options().gpu_preference;
    record_render_adapter(properties.deviceName, properties.vendorID, properties.deviceID, context.subsys_id(),
                          preference != nullptr && (preference[0] == 'l' || preference[0] == 'L' || preference[0] == 'm' ||
                                                    preference[0] == 'M'),
                          false);
    RenderFeatureCaps caps{};
    caps.adapter_vendor_id = properties.vendorID;
    caps.adapter_device_id = properties.deviceID;
    caps.adapter_subsys_id = context.subsys_id();
    caps.adapter_luid_low = static_cast<std::uint32_t>(context.luid());
    caps.adapter_luid_high = static_cast<std::uint32_t>(context.luid() >> 32u);
    std::snprintf(caps.adapter_description.data(), caps.adapter_description.size(), "%s", properties.deviceName);
    caps.msaa = make_render_feature_status(true, msaa_max_samples_ > 1u, true, true,
                                           msaa_max_samples_ > 1u ? "the scene target is multisampled"
                                                                  : "the GPU has no multisampled scene target formats");
    caps.taa = make_render_feature_status(false, false, false, false,
                                          "motion vectors, jitter, and history resources are not built");
    caps.dxr = make_render_feature_status(false, false, false, false, "ray tracing runs on Direct3D 12");
#if defined(_WIN32)
    const bool dlss_ready = streamline::dlss_supported();
#else
    const bool dlss_ready = false;
#endif
    caps.streamline_bridge_built = dlss_ready;
    caps.streamline = make_render_feature_status(dlss_ready, dlss_ready, dlss_ready, false,
                                                 dlss_ready ? "NVIDIA Streamline is running"
                                                            : "NVIDIA Streamline is not running");
    caps.reflex = make_render_feature_status(false, false, false, false, "Streamline Reflex integration is not built");
    caps.dlss_super_resolution = make_render_feature_status(
        dlss_ready, dlss_ready, dlss_ready, dlss_ready,
        dlss_ready ? "DLSS super resolution is available" : "DLSS needs Streamline and an NVIDIA RTX GPU");
    caps.dlss_frame_generation = make_render_feature_status(
        false, false, false, false, "DLFG requires Streamline, Reflex, motion vectors, and UI tagging");
    set_render_feature_caps(caps);
}

bool RendererVk::create_surface_and_device() {
    const bool prefer_integrated = [] {
        const char* value = ::galaxy::run_options().gpu_preference;
        return value != nullptr && (value[0] == 'l' || value[0] == 'L' || value[0] == 'm' || value[0] == 'M');
    }();
    const bool validation = ::galaxy::run_option_flag(::galaxy::run_options().vulkan_validation, false);
    const host_window::NativeWindow window = window_;
    const auto make_surface = [window](VkInstance instance) {
        return host_window::create_vulkan_surface(instance, window);
    };
    if (!vk::Context::get().initialize(get_render_config().gpu.data(), prefer_integrated, validation,
                                       host_window::vulkan_instance_extensions(), make_surface, &surface_)) {
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
    // Scene depth: 32-bit float with stencil (the shadow volumes), where reversed Z keeps its precision; D24S8 where the
    // device lacks it (Vulkan guarantees one of the two) or its D32F clears are broken (DeviceQuirks::depth_d24).
    const auto usable = [&context](VkFormat candidate) {
        VkFormatProperties format{};
        vkGetPhysicalDeviceFormatProperties(context.physical_device(), candidate, &format);
        constexpr VkFormatFeatureFlags kNeeded = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        return (format.optimalTilingFeatures & kNeeded) == kNeeded;
    };
    depth_format_ = !context.quirks().depth_d24 && usable(VK_FORMAT_D32_SFLOAT_S8_UINT) ? VK_FORMAT_D32_SFLOAT_S8_UINT
                    : usable(VK_FORMAT_D24_UNORM_S8_UINT)                                ? VK_FORMAT_D24_UNORM_S8_UINT
                                                                                         : VK_FORMAT_D32_SFLOAT_S8_UINT;

    const VkShaderStageFlags stage_passes = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    // Descriptor indexing, or each draw's own tables (shader_compile_vk.h kBoundTableSlots); the programs, loaded by the form
    // chosen here, are built for one.
    bindless_ = context.bindless();
    // The image set's binding: a runtime-sized array, its length chosen when a set is allocated (up to the layout's count).
    const VkDescriptorBindingFlags sampler_flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                                   VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                                   VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    const VkDescriptorBindingFlags image_flags = sampler_flags | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    texture_array_limit_ = 0u;
    if (bindless_) {
        // The device's answer for the image layout: whether it can be made and how long its variable binding may be.
        const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, context.texture_array_max(),
                                                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutBindingFlagsCreateInfo flag_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
        flag_info.bindingCount = 1;
        flag_info.pBindingFlags = &image_flags;
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        info.pNext = &flag_info;
        info.bindingCount = 1;
        info.pBindings = &binding;
        VkDescriptorSetVariableDescriptorCountLayoutSupport variable{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT};
        VkDescriptorSetLayoutSupport support{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT};
        support.pNext = &variable;
        vkGetDescriptorSetLayoutSupport(device, &info, &support);
        texture_array_limit_ = std::min(context.texture_array_max(), variable.maxVariableDescriptorCount);
        if (support.supported != VK_TRUE || texture_array_limit_ < vk::kBindlessTextureSlotsMin) {
            std::fprintf(stderr, "[vulkan] textures: the image array layout is not supported (up to %u slots); bound tables\n",
                         texture_array_limit_);
            bindless_ = false;
            texture_array_limit_ = 0u;
        }
    }
    if (!bindless_ && context.table_slots() < vk::kBoundTableSlots) {
        throw std::runtime_error("the GPU's per-stage descriptor limits hold fewer than " + std::to_string(vk::kBoundTableSlots) +
                                 " maps in a table, and it has no descriptor indexing");
    }
    program_use_bound_tables(!bindless_);
    static_assert(kSamplerHeapSlots == vk::kBindlessSamplerSlots);
    {
        // Set 0: b0, b1, b2, the composed rows, the instance table and, with descriptor indexing, the frame's texture lists.
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, stage_passes, nullptr},
            {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, stage_passes, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, stage_passes, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stage_passes, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stage_passes, nullptr},
            {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        };
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = bindless_ ? 6 : 5;
        info.pBindings = bindings;
        vk::check(vkCreateDescriptorSetLayout(device, &info, nullptr, &draw_layout_.sets[0]), "set 0 layout");
    }
    {
        // Sets 1 and 2: the images and the samplers. With descriptor indexing each is one array over every texture and
        // every sampler, written while bound or in use by earlier frames, with slots left empty; the image binding is the
        // last of its set and takes its length when a set is allocated (at most texture_array_limit_); else a table of
        // one draw.
        for (unsigned set = 1; set <= 2; ++set) {
            const bool images = set == 1;
            VkDescriptorSetLayoutBindingFlagsCreateInfo flag_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
            flag_info.bindingCount = 1;
            flag_info.pBindingFlags = images ? &image_flags : &sampler_flags;
            const VkDescriptorSetLayoutBinding binding{
                0, images ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_SAMPLER,
                bindless_ ? (images ? texture_array_limit_ : vk::kBindlessSamplerSlots) : vk::kBoundTableSlots,
                VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            info.bindingCount = 1;
            info.pBindings = &binding;
            if (bindless_) {
                info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
                info.pNext = &flag_info;
            }
            vk::check(vkCreateDescriptorSetLayout(device, &info, nullptr, &draw_layout_.sets[set]), images ? "set 1 layout" : "set 2 layout");
        }
    }
    if (bindless_) {
        // The sampler array: one set of the cache's length (kSamplerHeapSlots), made once and kept. The image set is
        // made when the texture store is first attached (install_texture_array), sized by its views.
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_SAMPLER, vk::kBindlessSamplerSlots};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        pool.maxSets = 1;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        vk::check(vkCreateDescriptorPool(device, &pool, nullptr, &bindless_pool_), "sampler array descriptor pool");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = bindless_pool_;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &draw_layout_.sets[2];
        vk::check(vkAllocateDescriptorSets(device, &allocate, &bindless_samplers_), "sampler array descriptor set");
    }
    {
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        // b3: the draw's words (gpu::DrawRoot).
        const VkPushConstantRange constants{stage_passes, 0u, static_cast<std::uint32_t>(sizeof(gpu::DrawRoot))};
        info.setLayoutCount = 3;
        info.pSetLayouts = draw_layout_.sets.data();
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &constants;
        vk::check(vkCreatePipelineLayout(device, &info, nullptr, &draw_layout_.layout), "draw pipeline layout");
    }
    linear_clamp_ = create_sampler(device, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST);
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
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        };
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 3;
        info.pBindings = bindings;
        vk::check(vkCreateDescriptorSetLayout(device, &info, nullptr, &probe_set_layout_), "probe set layout");
        const VkPushConstantRange count{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(std::uint32_t)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &probe_set_layout_;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &count;
        vk::check(vkCreatePipelineLayout(device, &layout, nullptr, &probe_layout_), "probe pipeline layout");
    }
    const VkPushConstantRange push{kPushStages, 0, kPushConstantBytes};
    {
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 1;
        info.pSetLayouts = &utility_set_layout_;
        info.pushConstantRangeCount = 1;
        info.pPushConstantRanges = &push;
        vk::check(vkCreatePipelineLayout(device, &info, nullptr, &utility_layout_), "utility pipeline layout");
    }

    // MSAA: the counts the device renders the scene's format and the depth format with and samples the depth with.
    {
        const VkPhysicalDeviceLimits& limits = context.properties().limits;
        VkSampleCountFlags counts = limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts &
            limits.sampledImageDepthSampleCounts;
        VkImageFormatProperties image{};
        if (vkGetPhysicalDeviceImageFormatProperties(context.physical_device(), vk::kSceneColorFormat,
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
    // The scene target pass keeps both targets in their attachment layouts; whoever samples them transitions and returns them.
    // With MSAA its targets are multisampled and a third attachment, the single-sample colour, takes their resolve.
    for (const unsigned kind : {0u, 1u, 2u})
    for (unsigned samples_log2 = 0u; (1u << samples_log2) <= msaa_max_samples_; ++samples_log2) {
        const bool wide = kind == 1u;
        const bool target = kind == 2u;
        if (target && samples_log2 != 0u) {
            continue;
        }
        VkAttachmentDescription attachments[3]{};
        attachments[0].format = wide ? VK_FORMAT_R16G16B16A16_SFLOAT
                                : target ? vk::kTargetColorFormat : vk::kSceneColorFormat;
        attachments[0].samples = static_cast<VkSampleCountFlagBits>(1u << samples_log2);
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1] = attachments[0];
        attachments[1].format = depth_format_;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
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
        // The pass against the passes before and after it: attachment writes complete before the next pass's attachment
        // accesses. Whoever copies from or samples the scene target, or writes it outside a pass, changes its layout
        // with a barrier of its own (it ends in the attachment layout), so no pass dependency names those stages.
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = dependencies[0].srcStageMask;
        dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1] = dependencies[0];
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = samples_log2 != 0u ? 3u : 2u;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;
        vk::check(vkCreateRenderPass(device, &info, nullptr,
                                     wide                ? &draw_layout_.wide_passes[samples_log2]
                                     : target            ? &draw_layout_.target_pass
                                     : samples_log2 != 0u ? &draw_layout_.ms_passes[samples_log2]
                                                         : &draw_layout_.scene_pass),
                  "scene target render pass");
        if (!target) {
            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            vk::check(vkCreateRenderPass(device, &info, nullptr,
                &draw_layout_.discard_color_passes[wide ? 1u : 0u][samples_log2]), "colour replacement render pass");
            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            vk::check(vkCreateRenderPass(device, &info, nullptr,
                &draw_layout_.depth_clear_passes[0][wide ? 1u : 0u][samples_log2]), "depth clearing render pass");
            attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            vk::check(vkCreateRenderPass(device, &info, nullptr,
                &draw_layout_.depth_clear_passes[1][wide ? 1u : 0u][samples_log2]), "depth and stencil clearing render pass");
        }
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        vk::check(vkCreateRenderPass(device, &info, nullptr,
                                     wide                ? &draw_layout_.wide_passes_clear[samples_log2]
                                     : target            ? &draw_layout_.target_pass_clear
                                     : samples_log2 != 0u ? &draw_layout_.ms_passes_clear[samples_log2]
                                                         : &draw_layout_.scene_pass_clear),
                  "scene target clearing render pass");
    }
    // A temporal scene's passes: as the single-sample scene pass, with the motion vectors or the reactive mask next to the
    // scene target (RendererD3D12 binds them as render target 1).
    for (const bool wide : {false, true})
    for (unsigned aux = 0u; aux < 2u; ++aux) {
        VkAttachmentDescription attachments[3]{};
        attachments[0].format = wide ? VK_FORMAT_R16G16B16A16_SFLOAT : vk::kSceneColorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1] = attachments[0];
        attachments[1].format = depth_format_;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[2] = attachments[0];
        attachments[2].format = aux == 0u ? VK_FORMAT_R16G16_SFLOAT : VK_FORMAT_R8_UNORM;
        const VkAttachmentReference colors[2] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        const VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 2;
        subpass.pColorAttachments = colors;
        subpass.pDepthStencilAttachment = &depth;
        // As the scene target's passes: the compute passes that read or write these targets change their layouts with
        // temporal_barriers.
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = dependencies[0].srcStageMask;
        dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1] = dependencies[0];
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = 3;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &draw_layout_.aux_passes[wide ? 1u : 0u][aux]),
                  "temporal scene render pass");
        for (VkAttachmentDescription& attachment : attachments) {
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &draw_layout_.aux_passes_clear[wide ? 1u : 0u][aux]),
                  "temporal scene clearing render pass");
    }
    // One target written whole (copies; RGBA8: screen probes): its old contents are dropped; it ends shader-readable.
    {
        VkAttachmentDescription attachment{};
        attachment.format = vk::kSceneColorFormat;
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
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &rgba8_pass_), "RGBA8 render pass");
    }
    draw_layout_.depth_format = depth_format_;
    set_curved_surfaces_offered(true);
    host_window::rtx_adapter.store(vk::Context::get().properties().vendorID == 0x10DEu &&
                                       std::strstr(vk::Context::get().properties().deviceName, "RTX") != nullptr,
                                   std::memory_order_relaxed);
    draw_layout_.depth_clamp = context.depth_clamp();
    draw_layout_.depth_clip_enable = context.depth_clip_enable();
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
    if (caps.maxImageExtent.width == 0u || caps.maxImageExtent.height == 0u) {
        return false;  // minimised: try again later
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
    // RGBA8 first, the post chain's own: a raw copy between the two orders swaps red and blue.
    bool chosen = false;
    for (const VkFormat wanted : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}) {
        for (const VkSurfaceFormatKHR& candidate : formats) {
            if (!chosen && candidate.format == wanted && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                format = candidate;
                chosen = true;
            }
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
    if (!wants_vsync()) {
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
    // of its own; the image then has the display's natural orientation. Not under AMD's frame generation: it blends
    // the presented pictures by the scene's motion, which is in the picture's own orientation.
    const bool fsr = fsr_generation_wanted();
    VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    switch (fsr ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform) {
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
    if (swapchain_fsr_) {
        vkDeviceWaitIdle(device);
        fsr_generation_.destroy(swapchain_.handle);
    }
    if (fsr || swapchain_fsr_) {
        // AMD's is made anew: its handle is no swap chain Vulkan could take as the old one.
        vkDeviceWaitIdle(device);
        destroy_swapchain();
        swapchain_fsr_ = false;
        info.oldSwapchain = VK_NULL_HANDLE;
    }
    bool amd = false;
    vk::SwapchainCalls calls{vkDestroySwapchainKHR, vkGetSwapchainImagesKHR, vkAcquireNextImageKHR, vkQueuePresentKHR};
    if (fsr) {
        amd = vk::FsrFrameGenerationVk::create_swap_chain(info, handle, calls);
        if (!amd) {
            calls = {vkDestroySwapchainKHR, vkGetSwapchainImagesKHR, vkAcquireNextImageKHR, vkQueuePresentKHR};
            handle = VK_NULL_HANDLE;
        }
    }
    if (!amd) {
        const VkResult created = vkCreateSwapchainKHR(device, &info, nullptr, &handle);
        if (created == VK_ERROR_SURFACE_LOST_KHR || created == VK_ERROR_NATIVE_WINDOW_IN_USE_KHR) {
            surface_lost_ = true;
            return false;
        }
        vk::check(created, "vkCreateSwapchainKHR");
    }
    destroy_swapchain();
    swapchain_calls_ = calls;
    swapchain_fsr_ = amd;
    swapchain_.handle = handle;
    present_timing_.refresh_ns = 0;  // the new swapchain's, at the next report
    present_timing_.last_actual = 0;
    swapchain_.format = format.format;
    swapchain_.extent = extent;
    swapchain_.image = image_extent;
    swapchain_.transform = transform;
    swapchain_.surface_transform = caps.currentTransform;
    swapchain_.copyable = (info.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
    swapchain_.mode = mode;
    swapchain_calls_.get_images(device, handle, &count, nullptr);
    swapchain_.images.resize(count);
    swapchain_calls_.get_images(device, handle, &count, swapchain_.images.data());
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
        // PowerVR clears by command instead (DeviceQuirks::clear_by_command, present_display).
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
        swapchain_calls_.destroy(device, swapchain_.handle, nullptr);
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
    return caps.currentTransform != swapchain_.surface_transform ||
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

VkShaderModule RendererVk::utility_module(vk::UtilitySpirv shader) {
    const std::span<const std::uint32_t> spirv = vk::utility_spirv(shader);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = spirv.size_bytes();
    info.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vk::check(vkCreateShaderModule(vk::Context::get().device(), &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

VkPipeline RendererVk::create_utility_pipeline(VkRenderPass pass, VkPipelineLayout layout, vk::UtilitySpirv vs_shader,
                                               vk::UtilitySpirv ps_shader, VkColorComponentFlags write_mask,
                                               bool blend, bool strip, bool depth_write,
                                               VkSampleCountFlagBits samples) {
    const VkDevice device = vk::Context::get().device();
    const VkShaderModule vs = utility_module(vs_shader);
    const VkShaderModule ps = utility_module(ps_shader);
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
    conversion_pipeline_ = create_utility_pipeline(color_pass_, utility_layout_, vk::UtilitySpirv::BlitVs,
                                                   vk::UtilitySpirv::SceneConversionPs, kAll, false, false, false);
    display_filter_pipeline_ = create_utility_pipeline(color_pass_, utility_layout_, vk::UtilitySpirv::BlitVs,
                                                       vk::UtilitySpirv::DisplayFilterPs, kAll, false, false, false);
    {
        const VkDevice device = vk::Context::get().device();
        const VkShaderModule module = utility_module(vk::UtilitySpirv::ScreenProbeCs);
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module;
        info.stage.pName = "main";
        info.layout = probe_layout_;
        const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &probe_pipeline_);
        vkDestroyShaderModule(device, module, nullptr);
        vk::check(result, "screen probe pipeline");
    }
    for (unsigned kind = 0u; kind < static_cast<unsigned>(SceneUtility::Count); ++kind) {
        (void)scene_utility_pipeline(static_cast<SceneUtility>(kind));
    }
    if (msaa_max_samples_ > 1u) {
        depth_resolve_pipeline_ = create_utility_pipeline(draw_layout_.scene_pass, utility_layout_,
                                                          vk::UtilitySpirv::BlitVs, vk::UtilitySpirv::DepthResolvePs, 0u, false,
                                                          false, true);
    }
    blit_pipeline_ = create_utility_pipeline(present_pass_, utility_layout_, vk::UtilitySpirv::PresentVs, vk::UtilitySpirv::BlitPs,
                                             kAll, false, false, false);
    return true;
}

VkPipeline RendererVk::scene_utility_pipeline(SceneUtility kind) {
    const unsigned samples_log2 = scene_samples_log2();
    VkPipeline& pipeline = scene_utility_pipelines_[static_cast<unsigned>(kind)][scene_variant()];
    if (pipeline != VK_NULL_HANDLE) {
        return pipeline;
    }
    if ((kind == SceneUtility::ClearRgba || kind == SceneUtility::ClearDepth) &&
        !vk::Context::get().quirks().clear_with_draw) {
        return VK_NULL_HANDLE;
    }
    const VkRenderPass pass = draw_layout_.pass_for(scene_variant(), false);
    const auto samples = static_cast<VkSampleCountFlagBits>(1u << samples_log2);
    constexpr VkColorComponentFlags kRgb = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT;
    switch (kind) {
    case SceneUtility::ClearRgb:
    case SceneUtility::ClearAlpha:
    case SceneUtility::ClearRgba:
        pipeline = create_utility_pipeline(
            pass, utility_layout_, vk::UtilitySpirv::BlitVs, vk::UtilitySpirv::ClearPs,
            kind == SceneUtility::ClearRgb ? kRgb
                : kind == SceneUtility::ClearAlpha ? VK_COLOR_COMPONENT_A_BIT : kRgb | VK_COLOR_COMPONENT_A_BIT,
            false, false, false, samples);
        break;
    case SceneUtility::ClearDepth:
        pipeline = create_utility_pipeline(pass, utility_layout_, vk::UtilitySpirv::BlitVs,
                                           vk::UtilitySpirv::ClearDepthPs, 0u, false, false, true, samples);
        break;
    case SceneUtility::Count:
        break;
    }
    return pipeline;
}

VkPipeline RendererVk::probe_test_pipeline() {
    const unsigned aux = scene_pass_aux_;
    if (target_pass_active_ || aux > 2u) {
        return VK_NULL_HANDLE;
    }
    VkPipeline& pipeline = probe_test_pipelines_[aux][scene_variant()];
    if (pipeline != VK_NULL_HANDLE) {
        return pipeline;
    }
    const VkDevice device = vk::Context::get().device();
    // The scene pass this aux target opens: the motion and reactive passes are single-sample with a second target.
    const VkRenderPass pass = aux != 0u ? draw_layout_.aux_pass_for(scene_variant(), aux) : draw_layout_.pass_for(scene_variant(), false);
    const auto samples = aux != 0u ? VK_SAMPLE_COUNT_1_BIT : static_cast<VkSampleCountFlagBits>(1u << scene_samples_log2());
    const VkShaderModule vs = utility_module(vk::UtilitySpirv::BlitVs);
    const VkShaderModule ps = utility_module(vk::UtilitySpirv::ClearDepthPs);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    // The first sample, as the probe dispatch reads a multisampled depth (its resolve keeps sample 0).
    const VkSampleMask first_sample = 1u;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = samples;
    multisample.pSampleMask = &first_sample;
    // Reversed depth: the point is seen where it is as near as the scene there or nearer.
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    VkPipelineColorBlendAttachmentState targets[2]{};  // write masks 0
    VkPipelineColorBlendStateCreateInfo blend_info{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend_info.attachmentCount = aux != 0u ? 2u : 1u;
    blend_info.pAttachments = targets;
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
    info.layout = utility_layout_;
    info.renderPass = pass;
    const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, ps, nullptr);
    if (result != VK_SUCCESS) {
        pipeline = VK_NULL_HANDLE;
        std::fprintf(stderr, "[RendererVk] probe test pipeline: %d; probes read after the pass\n", static_cast<int>(result));
    }
    return pipeline;
}

RendererVk::Kept RendererVk::take_apart_lost_device() {
    keep_window_ = true;
    try {
        shutdown();
    } catch (const std::exception& error) {
        std::cerr << "[RendererVk] the lost device's shutdown: " << error.what() << "\n";
    }
    return Kept{window_};
}

void RendererVk::shutdown() {
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
#if defined(NEBULA_DEV)
        if (inspect_query_pool_ != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, inspect_query_pool_, nullptr);
            inspect_query_pool_ = VK_NULL_HANDLE;
        }
        inspect_motion_buffer_.reset();
#endif
        if (swapchain_fsr_) {
            fsr_generation_.destroy(swapchain_.handle);
        }
        // Release every renderer-owned child while its device and allocator still exist.
        // Member destructors run after Context::shutdown and cannot perform this cleanup then.
        temporal_pending_ = {};
        temporal_.reset();
        destroy_motion_framebuffers();
        motion_ = MotionTargets{};
        ao_.shutdown();
        scene_camera_.shutdown();
        for (SceneSet* set : {&scene_, &wide_scene_, &alt_scene_, &target_scene_}) destroy_scene_set(*set);
        for (auto& [key, slot] : target_slots_) destroy_scene_set(slot.set);
        target_slots_.clear();
        for (auto& [key, picture] : pictures_) {
            if (picture.sampled != VK_NULL_HANDLE) vkDestroyImageView(device, picture.sampled, nullptr);
        }
        pictures_.clear();
        for (VkPipeline* pipeline : {&conversion_pipeline_, &probe_pipeline_, &display_filter_pipeline_,
             &depth_resolve_pipeline_, &blit_pipeline_}) {
            if (*pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, *pipeline, nullptr);
            *pipeline = VK_NULL_HANDLE;
        }
        for (auto& variants : scene_utility_pipelines_) {
            for (VkPipeline& pipeline : variants) {
                if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }
        for (auto& variants : probe_test_pipelines_) {
            for (VkPipeline& pipeline : variants) {
                if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }
        for (auto& [key, sampler] : samplers_) vkDestroySampler(device, sampler, nullptr);
        samplers_.clear();
        sampler_slots_.reset();
        sampler_slots_ready_ = false;
        if (bindless_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, bindless_pool_, nullptr);
        bindless_pool_ = VK_NULL_HANDLE;
        if (bindless_images_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, bindless_images_pool_, nullptr);
        bindless_images_pool_ = VK_NULL_HANDLE;
        bindless_images_ = bindless_samplers_ = VK_NULL_HANDLE;
        texture_array_slots_ = texture_array_limit_ = null_texture_slot_ = 0u;
        mirrored_backend_ = nullptr;
        mirrored_store_ = nullptr;
        texture_lists_ = {};
        texture_lists_used_ = 0u;
        post_.shutdown();
        bloom_.shutdown();
        oriented_.reset();
        fg_hudless_scene_color_.reset();
        fg_hudless_display_.reset();
        fg_hudless_target_.reset();
        turn_source_ = VK_NULL_HANDLE;
        zero_vertices_.reset();
        frame_pass_targets_.clear();
        for (Frame& frame : frames_) {
            frame.retired.clear();
    frame.retired_buffers.clear();
            destroy_retired_pools(frame);
            if (frame.pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, frame.pool, nullptr);
            if (frame.descriptors != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, frame.descriptors, nullptr);
            for (VkDescriptorPool pool : frame.overflow_descriptors) vkDestroyDescriptorPool(device, pool, nullptr);
            if (frame.acquired != VK_NULL_HANDLE) vkDestroySemaphore(device, frame.acquired, nullptr);
            frame = Frame{};
        }
        if (utility_pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device, utility_pool_, nullptr);
        utility_pool_ = VK_NULL_HANDLE;
        output_scene_active_ = false;
        for (ProbeSlot& slot : probe_slots_) {
            if (slot.queries != VK_NULL_HANDLE) vkDestroyQueryPool(device, slot.queries, nullptr);
        }
        probe_slots_.clear();
        held_probe_slots_.clear();
        scene_capture_ = Readback{};
        backbuffer_capture_ = Readback{};
        display_capture_ = Readback{};
        display_capture_pending_path_.clear();
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
        timestamp_pending_.fill(false);
        native_scene_gpu_ema_ms_ = 0.0;
        frame_scene_drawn_.fill(false);
        pass_mark_count_.fill(0u);
        stats_pending_.fill(false);
        pass_ms_.fill(0.0);
        stats_sum_.fill(0.0);
        profile_samples_ = 0;
        profile_frame_ = pass_recording_ = false;
        destroy_swapchain();
        for (VkRenderPass pass :
             {present_pass_, color_pass_, rgba8_pass_, draw_layout_.scene_pass, draw_layout_.scene_pass_clear,
              draw_layout_.target_pass, draw_layout_.target_pass_clear}) {
            if (pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, pass, nullptr);
        }
        wide_base_sampled_ = false;
        for (auto* group : {&draw_layout_.discard_color_passes, &draw_layout_.depth_clear_passes[0],
                            &draw_layout_.depth_clear_passes[1]}) {
            for (auto& passes : *group) {
                for (VkRenderPass& pass : passes) {
                    if (pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, pass, nullptr);
                    pass = VK_NULL_HANDLE;
                }
            }
        }
        for (auto* group : {&draw_layout_.aux_passes, &draw_layout_.aux_passes_clear})
        for (auto& passes : *group) {
            for (VkRenderPass& pass : passes) {
                if (pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, pass, nullptr);
                pass = VK_NULL_HANDLE;
            }
        }
        temporal_pending_ = {};
        temporal_.reset();
        destroy_motion_framebuffers();
        motion_ = MotionTargets{};
        for (auto* passes : {&draw_layout_.ms_passes, &draw_layout_.ms_passes_clear, &draw_layout_.wide_passes, &draw_layout_.wide_passes_clear}) {
            for (VkRenderPass& pass : *passes) {
                if (pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, pass, nullptr);
                pass = VK_NULL_HANDLE;
            }
        }
        present_pass_ = color_pass_ = rgba8_pass_ = draw_layout_.target_pass = draw_layout_.target_pass_clear =
            draw_layout_.scene_pass = draw_layout_.scene_pass_clear = VK_NULL_HANDLE;
        for (VkPipelineLayout layout : {draw_layout_.layout, utility_layout_, probe_layout_}) {
            if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout, nullptr);
        }
        draw_layout_.layout = utility_layout_ = probe_layout_ = VK_NULL_HANDLE;
        for (VkDescriptorSetLayout layout : {draw_layout_.sets[0], draw_layout_.sets[1], draw_layout_.sets[2],
                                             utility_set_layout_, probe_set_layout_}) {
            if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
        draw_layout_.sets.fill(VK_NULL_HANDLE);
        utility_set_layout_ = probe_set_layout_ = VK_NULL_HANDLE;
        if (linear_clamp_ != VK_NULL_HANDLE) vkDestroySampler(device, linear_clamp_, nullptr);
        linear_clamp_ = VK_NULL_HANDLE;
        context.collect();
    }
    if (surface_ != VK_NULL_HANDLE && context.instance() != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(context.instance(), surface_, nullptr);
    }
    surface_ = VK_NULL_HANDLE;
    context.shutdown();
    present_pacer_.reset();
    if (!keep_window_) {
        host_window::close_game_window(*window_owner_);
        window_ = nullptr;
        window_shown_ = false;
    }
}

void RendererVk::grow_rings() {
    if (ring_growth_failed_ || (!vertex_ring_.wants_growth() && !index_ring_.wants_growth() && !constant_ring_.wants_growth() &&
                                !matrix_ring_.wants_growth())) {
        return;
    }
    wait_for_gpu_idle();
    const VkDevice device = vk::Context::get().device();
    const auto place = [&](UploadRing& ring, const char* name, unsigned segments) {
        const std::size_t had = ring.capacity();
        std::size_t capacity = ring.grown_capacity(segments);
        if (capacity == 0u) {
            ring_growth_failed_ = true;
            capacity = had;
        }
        if (capacity == had) {
            return ring.resegment(UploadRing::budget(capacity, segments), segments);
        }
        std::cerr << "[gpu-rings] grow " << name << " frame-high-water-kb=" << (ring.high_water() >> 10u)
                  << " mb=" << (had >> 20u) << "->" << (capacity >> 20u) << '\n';
        ring.reset();
        if (ring.initialize(device, name, UploadRing::budget(capacity, segments), segments, capacity)) {
            return true;
        }
        ring_growth_failed_ = true;
        std::cerr << "[gpu-rings] grow " << name << " failed\n";
        return ring.initialize(device, name, UploadRing::budget(had, segments), segments, had);
    };
    if (!place(vertex_ring_, "vertex", kRingSegments) || !place(index_ring_, "index", kFramesInFlight) ||
        !place(constant_ring_, "constant", kFramesInFlight) || !place(matrix_ring_, "matrix", kRingSegments)) {
        throw std::runtime_error("upload ring resize failed");
    }
    history_segment_ = 0u;
    history_segment_fence_.fill(0u);
    history_used_ = true;
}

void RendererVk::trace_rings() const {
    const auto line = [](const char* name, const UploadRing& ring) {
        std::cerr << "[memory] ring " << name << ' ' << static_cast<double>(ring.capacity()) / (1024.0 * 1024.0)
                  << " MB, a frame took at most " << static_cast<double>(ring.high_water()) / (1024.0 * 1024.0) << " MB\n";
    };
    line("vertex", vertex_ring_);
    line("index", index_ring_);
    line("constant", constant_ring_);
    line("matrix", matrix_ring_);
}

void RendererVk::wait_for_gpu_idle() {
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

void RendererVk::begin_frame() {
    host_window::poll_render_config_file();
    if (!streamline_frame_open_) {
        vk::streamline_vk::open_frame(swapchain_fsr_);
        streamline_frame_open_ = true;
    }
    follow_window_and_resize();
    apply_window_size();
    grow_rings();
    prepare_temporal_upscaler();
    if (temporal_interp_suspended_ && temporal_interp_key() != temporal_interp_key_) {
        // The settings changed: decided again.
        temporal_interp_suspended_ = false;
    }
    sync_post_process();
    bloom_.poll_ready();

    frame_slot_ = static_cast<unsigned>(frame_index_ % kFramesInFlight);
    Frame& frame = frames_[frame_slot_];
    frame_ = &frame;
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    if (frame.fence != 0u) {
        wait_for_fence_value(frame.fence);
    }
    if (timestamp_pending_[frame_slot_] && timestamps_ != VK_NULL_HANDLE) {
        std::uint64_t ticks[2]{};
        if (vkGetQueryPoolResults(device, timestamps_, frame_slot_ * 2u, 2u, sizeof(ticks), ticks,
                                  sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ticks[1] >= ticks[0]) {
            const double gpu_ms = static_cast<double>(ticks[1] - ticks[0]) * timestamp_period_ns_ / 1'000'000.0;
            if (frame_scene_drawn_[frame_slot_]) {
                native_scene_gpu_ema_ms_ = native_scene_gpu_ema_ms_ == 0.0 ? gpu_ms :
                    native_scene_gpu_ema_ms_ + (gpu_ms - native_scene_gpu_ema_ms_) * 0.1;
            }
            if (frame_generated_[frame_slot_]) {
                generated_gpu_ms_ += gpu_ms;
                ++generated_gpu_samples_;
                generated_gpu_ema_ms_ = generated_gpu_ema_ms_ == 0.0 ? gpu_ms : generated_gpu_ema_ms_ * 0.9 + gpu_ms * 0.1;
            }
            stats_.gpu_ms += gpu_ms;
            ++stats_.gpu_samples;
            gpu_window_.ms += gpu_ms;
            gpu_window_.max_ms = std::max(gpu_window_.max_ms, gpu_ms);
            ++gpu_window_.frames;
        }
        timestamp_pending_[frame_slot_] = false;
    }
    read_gpu_passes();
    prepare_gpu_pass_queries();
    context.collect();
    frame.retired.clear();
    frame.retired_buffers.clear();
    destroy_retired_pools(frame);
    reset_sampler_frame();
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
#if defined(NEBULA_DEV)
    if (inspect_queries_reset_) {
        vkCmdResetQueryPool(commands_, inspect_query_pool_, 0u, inspect_query_capacity_);
        inspect_queries_reset_ = false;
    }
#endif
    if (timestamps_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(frame.commands, timestamps_, frame_slot_ * 2u, 2u);
        vkCmdWriteTimestamp(frame.commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, timestamps_, frame_slot_ * 2u);
    }
    pass_recording_ = false;
    if (profile_frame_) {
        vkCmdResetQueryPool(frame.uploads, pass_queries_, kFramesInFlight * kGpuPassSlots + frame_slot_ * 2u, 2u);
        vkCmdResetQueryPool(frame.commands, pass_queries_, frame_slot_ * kGpuPassSlots, kGpuPassSlots);
        current_pass_ = GpuPass::Count;
        pass_recording_ = true;
        gpu_pass(GpuPass::Scene);
    }
    if (profile_frame_ && stats_queries_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(frame.commands, stats_queries_, frame_slot_, 1u);
        vkCmdBeginQuery(frame.commands, stats_queries_, frame_slot_, 0);
    }
    // A frame that wrote neither keeps appending, so the last walk's stay readable across present-only frames.
    if (history_used_) {
        history_segment_ = (history_segment_ + 1u) % kRingSegments;
        if (history_segment_fence_[history_segment_] != 0u) {
            wait_for_fence_value(history_segment_fence_[history_segment_]);
        }
        vertex_ring_.begin_frame(history_segment_);
        matrix_ring_.begin_frame(history_segment_);
    }
    history_begin_vertex_ = vertex_ring_.cursor();
    history_begin_matrix_ = matrix_ring_.cursor();
    index_ring_.begin_frame(frame_slot_);
    constant_ring_.begin_frame(frame_slot_);
    constants_sets_.clear();
    texture_lists_used_ = 0u;
    encoder_.attach(frame.commands);
    reset_draw_bindings();
    scene_pass_open_ = false;
    swap_image_ = UINT32_MAX;
    scene_drawn_ = false;
}

std::uint64_t RendererVk::end_frame(bool present_swap_chain, SubmissionCallback on_submitted,
                                    void* submission_context, bool generated) {
    Frame& frame = frames_[frame_slot_];
    end_scene_pass();
    if (timestamps_ != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(frame.commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timestamps_,
                            frame_slot_ * 2u + 1u);
    }
    if (pass_queries_ != VK_NULL_HANDLE) {
        gpu_pass(GpuPass::Count);
    }
    pass_recording_ = false;
    if (profile_frame_ && stats_queries_ != VK_NULL_HANDLE) {
        vkCmdEndQuery(frame.commands, stats_queries_, frame_slot_);
        stats_pending_[frame_slot_] = true;
    }
    vk::check(vkEndCommandBuffer(frame.uploads), "vkEndCommandBuffer");
    vk::check(vkEndCommandBuffer(frame.commands), "vkEndCommandBuffer");
    const VkCommandBuffer buffers[] = {frame.uploads, frame.commands};
    const bool presenting = present_swap_chain && swap_image_ != UINT32_MAX;
    vk::Context& context = vk::Context::get();
    if (presenting) {
        vk::streamline_vk::mark_submit_start();
    }
    const std::uint64_t fence = presenting
        ? context.submit_frame(buffers, frame.acquired, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               swapchain_.rendered[swap_image_])
        : context.submit_frame(buffers);
    if (presenting) {
        vk::streamline_vk::mark_submit_end();
    }
    frame.fence = fence;
    last_submitted_ = fence;
    history_used_ = vertex_ring_.cursor() != history_begin_vertex_ || matrix_ring_.cursor() != history_begin_matrix_;
    history_segment_fence_[history_segment_] = fence;
    frame_generated_[frame_slot_] = generated;
    frame_scene_drawn_[frame_slot_] = scene_drawn_;
    timestamp_pending_[frame_slot_] = timestamps_ != VK_NULL_HANDLE;
    for (ProbeSlot& probes : probe_slots_) {
        if (probes.fence == kProbesInFrame) {
            probes.fence = fence;
        }
    }
    if (scene_capture_.buffer && scene_capture_.fence == 0u && !scene_capture_pending_path_.empty()) {
        scene_capture_.fence = fence;
    }
    if (backbuffer_capture_.buffer && backbuffer_capture_.fence == 0u &&
        !backbuffer_capture_pending_path_.empty()) {
        backbuffer_capture_.fence = fence;
    }
    if (display_capture_.buffer && display_capture_.fence == 0u && !display_capture_pending_path_.empty()) {
        display_capture_.fence = fence;
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
                         "fence waits %.1f, fence polls %.1f ms/s; persistent descriptor pools %zu\n", per_second(cpu - last_cpu),
                         per_second(driver[0]), per_second(driver[1]), per_second(driver[2]), per_second(driver[3]),
                         per_second(driver[4]), 1u + persistent_overflow_.size());
        }
        last_cpu = cpu;
        last_time = now;
        if (gpu_window_.frames != 0u) {
            const double frames = static_cast<double>(gpu_window_.frames);
            std::fprintf(stderr, "[vk-gpu] frame %.2f ms average, %.2f ms longest; %.1f scene target passes and %.1f scene target copies a "
                         "frame; scene target %ux%u, swap chain %ux%u\n", gpu_window_.ms / frames, gpu_window_.max_ms,
                         static_cast<double>(gpu_window_.passes) / 600.0,
                         static_cast<double>(gpu_window_.copies) / 600.0, scene_map_.width,
                         scene_map_.height, swapchain_.image.width, swapchain_.image.height);
            std::fprintf(stderr, "[vk-gpu] scene target opened a frame (closed by > opened by):");
            for (const GpuWindow::Open& open : gpu_window_.opens) {
                if (open.count != 0u) {
                    std::fprintf(stderr, " %s > %s%s %.2f;", open.closed, open.opened, open.cleared ? " (clear)" : "",
                                 static_cast<double>(open.count) / 600.0);
                }
            }
            std::fprintf(stderr, "\n");
        }
        if (!trace_present_stats_enabled() && profile_samples_ != 0u) {
            static constexpr const char* names[] = {"scene", "resolve", "frame-read", "display-copy", "bloom", "scene-aa",
                                                    "scene-ao", "upscale", "rt", "present", "camera",
                                                    "ao-distance", "ao-occlusion", "ao-denoise", "ao-upsample", "ao-apply"};
            std::fprintf(stderr, "[gpu-profile] %u sampled submissions; GPU ms:", profile_samples_);
            for (std::size_t p = 0; p < std::size(names); ++p)
                std::fprintf(stderr, " %s=%.3f", names[p], pass_ms_[p] / profile_samples_);
            std::fprintf(stderr, "\n");
            pass_ms_.fill(0.0);
            profile_samples_ = 0;
        }
        gpu_window_ = GpuWindow{};
    }
    return fence;
}

// Presents the acquired image after the submission that draws it (end_frame's and a generated frame's), paced to the
// frame rate, on Android at the display slot it was meant for (VK_GOOGLE_display_timing).
void RendererVk::present_swapchain_image() {
    vk::Context& context = vk::Context::get();
    const RenderConfig cfg = get_render_config();
    const double pace_wait_ms = present_pacer_.pace(cfg.vsync_interval, presented_max_fps(cfg));
    const std::int64_t present_start = galaxy::qpc_now();
    {
        // The target rate: dynamic multi frame generation reaches it, a fixed multiplier is the largest multiple of 60 at or
        // below it. Only while the game shows play (render_config.h); otherwise its own frames alone.
        unsigned generated = 0u;
        if (frame_generation_running() && frame_generation_allowed()) {
            const double rate = present_frame_rate(cfg.max_fps, cfg.vsync_interval, display_refresh_hz_);
            generated = static_cast<unsigned>(std::clamp<long>(static_cast<long>(rate / kGameStepRate + 1e-3) - 1, 1,
                                                               static_cast<long>(host_window::upscaler_caps().frame_generation_max)));
        }
        vk::streamline_vk::set_generation(generated, host_window::upscaler_caps().frame_generation_dynamic,
                                          cfg.max_fps > 0.0f ? cfg.max_fps : 0.0f);
    }
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &swapchain_.rendered[swap_image_];
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain_.handle;
    info.pImageIndices = &swap_image_;
    VkPresentTimeGOOGLE present_time{};
    VkPresentTimesInfoGOOGLE present_times{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE};
    std::int64_t frame_start = 0, period = 0, vsync = 0, now = 0;
    if (context.display_timing() && host_window::present_grid(frame_start, period, vsync, now)) {
        PresentTiming& timing = present_timing_;
        // Interpolating, slots step by one presented frame: the game's frame period times 60 / the present rate
        // (90 Hz: 1.5 presents per game frame).
        const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, display_refresh_hz_);
        const std::int64_t step = std::max<std::int64_t>(1, std::llround(1e9 / rate));
        // The slot after the previous frame's, on the grid of the latest frame start; two frame periods after that start
        // when it is out of reach (the first frame, after a stall) or the frames ran ahead of it.
        std::int64_t slot = timing.last_slot + step;
        const std::int64_t first = frame_start + 2 * period;
        if (timing.last_slot == 0 || slot > first + period || slot < frame_start + step) {
            slot = first;
        } else {
            slot = frame_start + (slot - frame_start + step / 2) / step * step;
        }
        // A frame too late for its slot takes a later one: a slot is in reach once the frame can be shown by then, at
        // least a vsync on (the compositor latches about one ahead) and as long as this device's frames have lately
        // taken from their present to their first possible showing (lead_ns). A 120 Hz panel's vsync is shorter than
        // the GPU's work and the compositor's latch together: asking a vsync ahead there, frames miss their slots.
        const std::int64_t reach = std::max(vsync, timing.lead_ns);
        while (slot - reach < now) {
            slot += step;
        }
        timing.last_slot = slot;
        timing.vsync_ns = vsync;
        present_time.presentID = ++timing.next_id;
        present_time.desiredPresentTime = static_cast<std::uint64_t>(slot - vsync / 2);
        const auto at = present_time.presentID % timing.requested.size();
        timing.requested[at] = slot;
        timing.called[at] = now;
        timing.requested_id[at] = present_time.presentID;
        timing.requested_period[at] = step;
        present_times.swapchainCount = 1;
        present_times.pTimes = &present_time;
        info.pNext = &present_times;
    } else {
        present_timing_.last_slot = 0;
    }
    const std::int64_t present_cpu = vk::thread_cpu_ns();
    if (swapchain_fsr_) {
        vk::TemporalImage hudless{};
        if (fg_hudless_ready_) {
            hudless = {fg_hudless_target_->image, fg_hudless_target_->view, VK_IMAGE_ASPECT_COLOR_BIT, VK_FORMAT_R8G8B8A8_UNORM, fg_hudless_target_->width,
                       fg_hudless_target_->height, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT};
        }
        // Without a HUD-less picture the swap chain's is the source: on a BGRA8 surface that frame generates nothing.
        fsr_generation_.configure(swapchain_.handle,
                                  frame_generation_running() && frame_generation_allowed() && (fg_hudless_ready_ || swapchain_.format == VK_FORMAT_R8G8B8A8_UNORM),
                                  fg_hudless_ready_ ? &hudless : nullptr,
                                  static_cast<unsigned>(fsr_rect_.left), static_cast<unsigned>(fsr_rect_.top), static_cast<unsigned>(fsr_rect_.width),
                                  static_cast<unsigned>(fsr_rect_.height));
        fg_hudless_ready_ = false;
    }
    vk::streamline_vk::mark_present_start();
    const VkResult result = swapchain_calls_.present(context.queue(), &info);
    vk::streamline_vk::mark_present_end();
    streamline_frame_open_ = false;
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
// one frame period after the frame before it. Every 600 frames to the log.
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
    const std::int64_t half_vsync = (timing.vsync_ns != 0 ? timing.vsync_ns : kGameStepNs / 2) / 2;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto actual = static_cast<std::int64_t>(past[i].actualPresentTime);
        const auto at = past[i].presentID % timing.requested.size();
        const bool identified = timing.requested_id[at] == past[i].presentID;
        const std::int64_t requested = identified ? timing.requested[at] : 0;
        const auto expected = identified ? timing.requested_period[at] : kGameStepNs;
        timing.margin_sum += static_cast<std::int64_t>(past[i].presentMargin);
        if (identified && timing.called[at] != 0) {
            const std::int64_t vsync = timing.vsync_ns != 0 ? timing.vsync_ns : kGameStepNs;
            const auto earliest = static_cast<std::int64_t>(past[i].earliestPresentTime);
            if (actual > requested + half_vsync && earliest > requested + half_vsync) {
                // At most four vsyncs, and a vsync more per late frame: a stall (the app put away, a GPU hitch) is not
                // the device's pace.
                const std::int64_t needed = std::clamp<std::int64_t>(earliest - timing.called[at], 0, 4 * vsync);
                timing.lead_ns = std::max(timing.lead_ns, std::min(needed, std::max(timing.lead_ns, vsync) + vsync));
                timing.lead_max = std::max(timing.lead_max, needed);
                timing.on_time_run = 0;
            } else if (actual <= requested + half_vsync && ++timing.on_time_run >= 240u) {
                timing.on_time_run = 0;
                timing.lead_ns = std::max<std::int64_t>(0, timing.lead_ns - vsync / 4);
            }
        }
        if (requested == 0) {
            ++timing.unrequested;
        } else if (actual <= requested + half_vsync) {
            ++timing.on_slot;
        } else {
            ++timing.late;
            if (static_cast<std::int64_t>(past[i].earliestPresentTime) > requested + half_vsync) ++timing.available_late;
            else ++timing.delayed_late;
        }
        if (timing.last_actual != 0) {
            const auto interval = actual - timing.last_actual;
            if (interval > 0) {
                timing.interval_sum += interval;
                timing.interval_max = std::max(timing.interval_max, interval);
                ++timing.intervals;
                if (std::llabs(interval - expected) < half_vsync) ++timing.even;
            } else ++timing.duplicates;
        }
        timing.last_actual = actual;
        if (++timing.frames == 600u) {
            std::fprintf(stderr, "[present-timing] %u frames: %u shown at their requested slot, %u late, %u without one "
                         "(unmatched); %u at the target interval; actual-hz %.2f, interval-max %.2f ms, duplicate-times %u; "
                         "late(available/delayed)=%u/%u margin %.2f ms (driver refresh %.2f, display %.2f ms); "
                         "present lead %.2f ms (a late frame's need, highest %.2f)\n",
                         timing.frames, timing.on_slot, timing.late, timing.unrequested, timing.even,
                         timing.interval_sum > 0 ? 1e9 * timing.intervals / double(timing.interval_sum) : 0.0,
                         double(timing.interval_max) / 1e6, timing.duplicates, timing.available_late, timing.delayed_late,
                         double(timing.margin_sum) / 1e6 / timing.frames,
                         double(timing.refresh_ns) / 1e6, double(timing.vsync_ns) / 1e6, double(timing.lead_ns) / 1e6,
                         double(timing.lead_max) / 1e6);
            timing.lead_max = 0;
            timing.frames = timing.on_slot = timing.late = timing.unrequested = timing.even = 0u;
            timing.duplicates = timing.available_late = timing.delayed_late = timing.intervals = 0u;
            timing.interval_sum = timing.interval_max = timing.margin_sum = 0;
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
    static std::uint64_t new_pictures_start = 0;
    static std::uint64_t pictures_made_start = 0;
    const DisplayPresentStats progress = display_present_stats();
    const std::uint64_t copies = pictures_made();
    const double presents = static_cast<double>(stats_.presents);
    std::ostringstream message;
    message << "[present-stats] fps=" << presents / seconds << " new-picture-hz="
            << static_cast<double>(progress.new_pictures_shown - new_pictures_start) / seconds
            << " pictures-made-hz=" << static_cast<double>(copies - pictures_made_start) / seconds
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
        static constexpr const char* kPassNames[] = {"scene", "resolve", "frame-read", "display-copy", "bloom", "scene-aa",
                                                     "scene-ao", "upscale", "rt", "present", "camera",
                                                     "ao-distance", "ao-occlusion", "ao-denoise", "ao-upsample", "ao-apply"};
        static_assert(std::size(kPassNames) == static_cast<std::size_t>(GpuPass::Count));
        const double profiled = static_cast<double>(std::max(profile_samples_, 1u));
        message << " gpu-pass-ms=";
        for (std::size_t pass = 0; pass < std::size(kPassNames); ++pass) {
            message << (pass == 0 ? "" : ",") << kPassNames[pass] << ':' << pass_ms_[pass] / profiled;
        }
        pass_ms_.fill(0.0);
        profile_samples_ = 0;
        // Flag bits in order: vertex, clipped primitives, fragment, compute.
        const std::array<double, 4>& st = stats_sum_;
        message << " gpu-stats=vs:" << st[0] / profiled << ",prims:" << st[1] / profiled << ",ps:" << st[2] / profiled
                << ",cs:" << st[3] / profiled;
        stats_sum_.fill(0.0);
    }
    message << " api=vulkan\n";
    std::cerr << message.str();
    new_pictures_start = progress.new_pictures_shown;
    pictures_made_start = copies;
    stats_ = PresentStats{};
    stats_.window_start_qpc = now;
    stats_.last_present_qpc = now;
}

void RendererVk::gpu_pass(GpuPass pass) {
    current_pass_ = pass;
    unsigned& marks = pass_mark_count_[frame_slot_];
    const VkCommandBuffer commands = frames_[frame_slot_].commands;
    // Tiled GPUs execute a render pass tile by tile. Interior timestamps do not delimit whole-image work;
    // record only at existing pass boundaries, never split a pass to measure it.
    if (scene_pass_open_ || pass_queries_ == VK_NULL_HANDLE || !pass_recording_ || commands_ != commands ||
        marks + 1u >= kGpuPassSlots || (marks != 0u && pass_marks_[frame_slot_][marks - 1u] == pass)) {
        return;
    }
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pass_queries_,
                        frame_slot_ * kGpuPassSlots + marks);
    pass_marks_[frame_slot_][marks++] = pass;
}

void RendererVk::prepare_gpu_pass_queries() {
    const bool full = ::galaxy::run_option_flag(::galaxy::run_options().gpu_passes, false);
    // A prime stride samples across real/generated/present-only cadence instead of repeatedly hitting one phase: one
    // frame in 61, a timestamp at each pass boundary (no pass is split), so every log has each pass's GPU time.
    // Developer profiling can explicitly record every submission.
    profile_frame_ = timestamps_ != VK_NULL_HANDLE && (full || frame_index_ % 61u == 0u);
    if (!profile_frame_) return;
    const VkDevice device = vk::Context::get().device();
    if (pass_queries_ == VK_NULL_HANDLE) {
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = kFramesInFlight * (kGpuPassSlots + 2u);
        vk::check(vkCreateQueryPool(device, &info, nullptr, &pass_queries_), "GPU pass query pool");
    }
    if (full && stats_queries_ == VK_NULL_HANDLE && vk::Context::get().pipeline_statistics()) {
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        info.queryCount = kFramesInFlight;
        info.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
        vk::check(vkCreateQueryPool(device, &info, nullptr, &stats_queries_), "GPU invocation query pool");
    }
}

// The slot's previous frame (its fence passed): each mark's time to the next.
void RendererVk::read_gpu_passes() {
    const VkDevice device = vk::Context::get().device();
    const double ms_per_tick = timestamp_period_ns_ / 1'000'000.0;
    if (pass_queries_ == VK_NULL_HANDLE) {
        return;
    }
    const unsigned marks = pass_mark_count_[frame_slot_];
    std::array<std::uint64_t, kGpuPassSlots> ticks{};
    if (marks > 1u && vkGetQueryPoolResults(device, pass_queries_, frame_slot_ * kGpuPassSlots, marks,
                                            marks * sizeof(std::uint64_t), ticks.data(), sizeof(std::uint64_t),
                                            VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
        ++profile_samples_;
        for (unsigned i = 0; i + 1u < marks; ++i) {
            if (ticks[i + 1u] >= ticks[i]) {
                pass_ms_[static_cast<std::size_t>(pass_marks_[frame_slot_][i])] +=
                    static_cast<double>(ticks[i + 1u] - ticks[i]) * ms_per_tick;
            }
        }
    }
    pass_mark_count_[frame_slot_] = 0u;
    std::array<std::uint64_t, 4> counts{};
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
// FSR 3 frame generation: chosen, with an upscaler that gives it depth and motion (FSR 3, XeSS, SGSR 2), and not DLSS,
// whose presents are Streamline's.
bool RendererVk::fsr_generation_wanted() const {
    const RenderConfig cfg = get_render_config();
    const vk::Context& context = vk::Context::get();
    return fsr_frame_generation_compiled() && cfg.frame_generation == 2u &&
           (cfg.upscaler == 2u || cfg.upscaler == 4u || cfg.upscaler == 5u) &&
           context.generation_present_queue() != VK_NULL_HANDLE && context.temporal_upscaling();
}

bool RendererVk::wants_vsync() const {
    // DLSS Frame Generation says whether it can vsync; AMD's paces whichever mode it is given.
    return get_render_config().vsync_interval != 0u &&
           (!frame_generation_running() || fsr_generation_wanted() || vk::streamline_vk::generation_vsync());
}

#if defined(_WIN32)
void RendererVk::reconcile_display_state() {
    if (window_ == nullptr || host_window::hidden_window_mode_enabled()) {
        return;
    }
    const RenderConfig cfg = get_render_config();
    const unsigned mode = std::min(cfg.display_mode, kDisplayBorderless);
    AppliedDisplay& applied = applied_display_;
    const bool same_display = !host_window::display_changed.exchange(false, std::memory_order_acq_rel) && applied.valid &&
                              applied.mode == mode && applied.monitor == cfg.monitor;
    if (same_display) {
        if (mode != kDisplayWindowed ||
            (applied.window_width == cfg.window_width && applied.window_height == cfg.window_height) ||
            host_window::move_size_active.load(std::memory_order_acquire)) {
            return;
        }
    }
    const std::vector<MonitorInfo> monitors = enumerate_monitors();
    const MonitorInfo* monitor = find_monitor(monitors, widen_utf8(cfg.monitor.data()));
    if (monitor == nullptr) {
        return;
    }
    host_window::WindowPlacement placement{};
    if (mode == kDisplayWindowed) {
        placement.style = host_window::kWindowStyle;
        placement.rect = host_window::windowed_rect_on_monitor(*monitor, cfg.window_width, cfg.window_height);
        placement.borderless = false;
    } else {
        placement.style = WS_POPUP;
        placement.rect = monitor->bounds;
        placement.borderless = true;
    }
    DWORD_PTR result = 0u;
    if (SendMessageTimeoutW(window_, host_window::kPlaceWindowMessage, 0u, reinterpret_cast<LPARAM>(&placement), SMTO_NORMAL,
                            2000u, &result) == 0) {
        std::cerr << "[Renderer] display change: the window did not respond\n";
        return;
    }
    // The size message of the placement reaches the swap chain through pending_resize_extent; a surface whose size did
    // not change is remade only if the monitor or transform did (surface_changed).
    swapchain_dirty_ = true;
    applied.valid = true;
    applied.mode = mode;
    applied.monitor = cfg.monitor;
    applied.window_width = cfg.window_width;
    applied.window_height = cfg.window_height;
    std::cout << "[Renderer] display " << (mode == kDisplayWindowed ? "windowed" : "borderless") << ' '
              << narrow_utf8(monitor->device) << '\n';
}
#endif

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
#if defined(_WIN32)
    reconcile_display_state();
#endif
    const std::uint64_t extent = host_window::pending_resize_extent.exchange(0u, std::memory_order_acq_rel);
    if (extent != 0u) {
        const auto width = static_cast<std::uint32_t>(extent >> 32u);
        const auto height = static_cast<std::uint32_t>(extent & 0xffffffffu);
        if (width != swapchain_.extent.width || height != swapchain_.extent.height) {
            swapchain_dirty_ = true;
        }
    }
    if (wants_vsync() != (swapchain_.mode == VK_PRESENT_MODE_FIFO_KHR) || fsr_generation_wanted() != swapchain_fsr_) {
        swapchain_dirty_ = true;
    }
    if (swapchain_dirty_) {
        vk::streamline_vk::generation_off();
        vkDeviceWaitIdle(vk::Context::get().device());
        create_swapchain();
    }
    if (display_refresh_checks_++ % 120u == 0u) {
        refresh_display_rate();
    }
}

bool RendererVk::present(const DisplayTexture* source, bool capture_backbuffer, bool black) {
    const GpuPassScope pass{*this, GpuPass::Present};
    present_black_ = black;
    if (source == nullptr || !source->texture) {
        throw std::runtime_error("present requested without a valid display picture blit source");
    }
    debug_log_backbuffer_readback();
    end_scene_pass();
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
    VkResult result = swapchain_calls_.acquire(context.device(), swapchain_.handle, kAcquireTimeoutNs, frame.acquired,
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
    vk::Texture& display_picture = *source->texture;
    if (display_picture.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        image_barrier(commands, display_picture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                      VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, 1u);
        const VkClearColorValue black{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(commands, display_picture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
        image_barrier(commands, display_picture.image, VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::CopyDest, gpu::Use::PixelRead,
                      gpu::Contents::Keep, 1u);
        display_picture.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    vk::Texture* shown = &display_picture;
    if (capture_backbuffer && !present_black_) {
        // A test snapshot also keeps the picture itself, before the post chain scales it.
        static_cast<void>(debug_copy_selected_display_to_readback(*source));
    }

    const auto aspect = galaxy::experimental_effective_host_aspect();
    const auto content_aspect = galaxy::presentation_content_aspect(
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect, source->prerecorded_movie);
    const int bb_w = static_cast<int>(swapchain_.extent.width);
    const int bb_h = static_cast<int>(swapchain_.extent.height);
    galaxy::ContentViewport content = galaxy::fit_content_viewport(bb_w, bb_h, content_aspect);
    if (content.width <= 0 || content.height <= 0) {
        content = galaxy::ContentViewport{0, 0, bb_w, bb_h};
    }
    (void)galaxy::g_presentation_content.publish(bb_w, bb_h, content);
    if (frame_generation_running()) {
        fsr_rect_ = content;
        vk::streamline_vk::tag_backbuffer_region(commands, static_cast<unsigned>(content.left), static_cast<unsigned>(content.top),
                                                 static_cast<unsigned>(content.width), static_cast<unsigned>(content.height));
        present_fg_hudless(*source, content);
    }

    // The post chain's passes before the swap chain's: the picture scaled (and sharpened) to its size on screen. A
    // turned swap chain takes it through `oriented_` and the blit, which turns it.
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_frame_set(layout); };
    vk::PostProcessorVk::PresentStep step{};
    const vk::Texture* picture = shown;
    if (post_.ready() && !present_black_) {
        const bool turned = swapchain_.transform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        const auto width = static_cast<std::uint32_t>(content.width);
        const auto height = static_cast<std::uint32_t>(content.height);
        step = post_.prepare_present(commands, *shown, shown->width, shown->height, width, height, !turned,
                                     allocate);
        if (turned && step.pipeline != VK_NULL_HANDLE) {
            if (!oriented_ || oriented_->width != width || oriented_->height != height) {
                oriented_ = context.create_texture(width, height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
                                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                                   true, "post-process picture");
                VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                info.renderPass = rgba8_pass_;
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
                begin.renderPass = rgba8_pass_;
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

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
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
    // the post chain's last pass, or the blit of the display picture (or of the chain's turned picture).
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
    vkCmdEndRenderPass(commands);
    host_window::native_settings_present();
    reset_draw_bindings();
    const auto hand_to_amd = [&] {
        if (swapchain_fsr_) {
            image_barrier(commands, swapchain_.images[image], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
    };

    if (capture_backbuffer && swapchain_.copyable && !backbuffer_capture_path_.empty() &&
        backbuffer_capture_pending_path_.empty()) {
        // The finished image, copied out for a test snapshot (PRESENT_SRC -> TRANSFER_SRC and back).
        const std::uint64_t bytes = static_cast<std::uint64_t>(bb_w) * bb_h * 4u;
        if (!backbuffer_capture_.buffer || backbuffer_capture_.buffer->size < bytes) {
            backbuffer_capture_.buffer = context.create_readback_buffer(bytes, "backbuffer capture");
        }
        if (backbuffer_capture_.buffer) {
            image_barrier(commands, swapchain_.images[image], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                          VK_ACCESS_2_TRANSFER_READ_BIT, 1u);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {swapchain_.image.width, swapchain_.image.height, 1};
            vkCmdCopyImageToBuffer(commands, swapchain_.images[image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   backbuffer_capture_.buffer->buffer, 1, &region);
            image_barrier(commands, swapchain_.images[image], VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::CopySource,
                          gpu::Use::Present, gpu::Contents::Keep, 1u);
            backbuffer_capture_.fence = 0u;
            backbuffer_capture_pending_path_ = backbuffer_capture_path_;
            backbuffer_capture_width_ = swapchain_.image.width;
            backbuffer_capture_height_ = swapchain_.image.height;
            backbuffer_capture_bgra_ = swapchain_.format == VK_FORMAT_B8G8R8A8_UNORM;
            hand_to_amd();
            return true;
        }
    }
    hand_to_amd();
    return false;
}

}  // namespace galaxy::render
