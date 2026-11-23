// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan renderer (renderer_vk.h): device, swapchain, frames, present and the overlay. The scene target, draws,
// and copies are renderer_vk_frame.cpp.

#include "renderer_vk_internal.h"
#include "galaxy/pacing_recorder.h"
#include "galaxy/benchmark.h"
#include "galaxy/frame_timeline.h"
#include "galaxy/launch_args.h"
#include "galaxy/whole_lines.h"
#include "galaxy/render/present_clock.h"
#include "galaxy/render/vk/streamline_vk.h"
#include "galaxy/render/vk/swapchain_extent.h"
#if defined(_WIN32)
#include "galaxy/render/streamline_bridge.h"
#endif
#include "galaxy/render/vk/renderer_vk.h"
#include "galaxy/platform_time.h"

#include "galaxy/content_viewport.h"
#include "galaxy/display_aspect.h"
#include "galaxy/render/host_window.h"
#if defined(_WIN32)
#include "galaxy/render/display_modes.h"
#include "galaxy/render/os_hdr.h"
#include "galaxy/render/host_window_win32.h"
#endif
#include "galaxy/render/program_table.h"
#include "galaxy/render/device_memory.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/game_paths.h"
#include "galaxy/render/idle_clock.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

namespace galaxy::render {

void RendererVk::lose_device() {
    vk::Context::get().simulate_device_loss();
}

namespace {

bool trace_present_stats_enabled() {
    return ::galaxy::diagnosing(::galaxy::kDiagnosePresentStats);
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

// Clockwise quarter turns of `transform` (turned_rect's three; any other is drawn upright, as there).
unsigned quarter_turns(VkSurfaceTransformFlagBitsKHR transform) {
    switch (transform) {
    case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
        return 1u;
    case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
        return 2u;
    case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
        return 3u;
    default:
        return 0u;
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
using vk_detail::kFrameUtilitySamplers;

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
    frame_extent_ = scene_extent_ = scene_rectangle_ = frame;
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
            frame.descriptors =
                create_pool(device, vk_detail::kFirstFrameSets, vk_detail::frame_pool(!bindless_, vk_detail::kFirstFrameSets));
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
        // The vertex, index and constant rings take a request past their segment and spill in a chunk; the matrix ring's rows are
        // indexed from its base, so it only grows between frames.
        const auto ring = [&](UploadRing& target, const char* name, std::size_t bytes, unsigned segments, bool chunks) {
            (void)bytes;
            return target.initialize(device, name, UploadRing::budget(kFirstRingBytes, segments), segments, kFirstRingBytes, chunks);
        };
        // The vertex and constant rings are read by the GPU every frame and written once, in order: video memory the CPU maps,
        // where the device has it (vk::Context::device_mapped_memory); the others stay in host memory.
        vertex_ring_.set_place(gpu::UploadPlace::GpuLocal);
        constant_ring_.set_place(gpu::UploadPlace::GpuLocal);
        if (!ring(vertex_ring_, "vertex", kVertexRingBytes, kRingSegments, true) ||
            !ring(index_ring_, "index", kIndexRingBytes, kFramesInFlight, true) ||
            !ring(constant_ring_, "constant", kConstantRingBytes, kFramesInFlight, true) ||
            !ring(matrix_ring_, "matrix", kMatrixRingBytes, kRingSegments, false)) {
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
        encoding_layout_ = encoding;
        reset_sampler_frame();
        // The game's bloom, built at the first frame with a composite for every scene target pass (8-bit and wide colour at
        // each sample count, and the texture targets' pass); the game draws its own chains until it is ready.
        {
            std::vector<vk::GameBloomVk::ScenePass> scene_passes;
            for (unsigned samples_log2 = 0u; samples_log2 < 4u; ++samples_log2) {
                for (const unsigned wide : {0u, unsigned{kSceneVariantHdr}}) {
                    if (const VkRenderPass pass = draw_layout_.pass_for(samples_log2 | wide, false); pass != VK_NULL_HANDLE) {
                        scene_passes.push_back({pass, samples_log2});
                    }
                }
            }
            if (draw_layout_.target_pass != VK_NULL_HANDLE) {
                scene_passes.push_back({draw_layout_.target_pass, 0u});
            }
            bloom_.initialize(std::move(scene_passes));
        }
        // draw.post's programs compile with the linked HLSL front end (vk/mod_shader_compile_vk.cpp).
        set_mod_post_compiler(&compile_mod_post_stage, true);
        // Presentation post-processing, compiled on a worker; the plain blit presents until it is ready.
        const RenderConfig cfg = get_render_config();
        if (cfg.post_process) {
            post_.initialize(post_process_settings_from(cfg), draw_layout_.scene_pass, color_pass_, rgba8_pass_);
            if (!ao_.initialize()) {
                std::cerr << "[ambient-occlusion] off on this device" << std::endl;
            } else {
                ao_.prepare_pipelines(scene_passes(false), scene_passes(true));
            }
            if (!scene_camera_.initialize()) {
                std::cerr << "[scene-camera] off on this device" << std::endl;
            } else {
                scene_camera_.prepare_pipelines(scene_passes(false), scene_passes(true));
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
    record_render_adapter(properties.deviceName, properties.vendorID, properties.deviceID, context.subsys_id());
    // The GPU shares the system's memory: an integrated one.
    set_device_unified_memory(properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
    {
        // The version number decoded by the driver's own scheme. driverInfo is free text: Qualcomm's is several lines of
        // build ids and dates, longer than a report's driver field.
        const vk::DriverVersion& v = context.identity().driver_version;
        record_render_driver(std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch));
    }
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
    const bool validation = ::galaxy::launch_args().gpu_validation >= 1u;
    const host_window::NativeWindow window = window_;
    const auto make_surface = [window](VkInstance instance) {
        return host_window::create_vulkan_surface(instance, window);
    };
    if (!vk::Context::get().initialize(get_render_config().gpu.data(), validation,
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
    const auto usable = [&context](VkFormat candidate, VkFormatFeatureFlags needed) {
        VkFormatProperties format{};
        vkGetPhysicalDeviceFormatProperties(context.physical_device(), candidate, &format);
        return (format.optimalTilingFeatures & needed) == needed;
    };
    constexpr VkFormatFeatureFlags kSampledDepth = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    // Neither sampled: the one the spec promises as an attachment (D32FS8 when its attachment bit is not reported).
    depth_format_ = !context.quirks().depth_d24 && usable(VK_FORMAT_D32_SFLOAT_S8_UINT, kSampledDepth) ? VK_FORMAT_D32_SFLOAT_S8_UINT
                    : usable(VK_FORMAT_D24_UNORM_S8_UINT, kSampledDepth)                                ? VK_FORMAT_D24_UNORM_S8_UINT
                    : usable(VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ? VK_FORMAT_D24_UNORM_S8_UINT
                                                                                                        : VK_FORMAT_D32_SFLOAT_S8_UINT;
    if (!usable(depth_format_, kSampledDepth)) {
        std::fprintf(stderr, "[vulkan] neither D32FS8 nor D24S8 is a sampled depth format here; the depth reads are undefined\n");
    }

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
    texture_array_whole_ = false;
    if (bindless_) {
        // The device's answer for the image layout: whether it can be made and how long its variable binding may be.
        const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, context.texture_array_max(),
                                                   VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
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
        const auto make_layout = [&](unsigned set) {
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
        };
        make_layout(1);
        make_layout(2);
        // A set of kBindlessTextureSlotsMin images from a pool of exactly that room: a conformant driver gives it (the
        // variable length decides the room); a driver that takes the room from the layout's bound refuses it, and gets a
        // layout of kBindlessTextureSlotsWhole with every set allocated whole (install_texture_array).
        const auto variable_set_fits = [&](std::uint32_t slots) {
            const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, slots};
            VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
            pool_info.maxSets = 1;
            pool_info.poolSizeCount = 1;
            pool_info.pPoolSizes = &size;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            vk::check(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool), "image array probe pool");
            VkDescriptorSetVariableDescriptorCountAllocateInfo variable{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
            variable.descriptorSetCount = 1;
            variable.pDescriptorCounts = &slots;
            VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocate.pNext = &variable;
            allocate.descriptorPool = pool;
            allocate.descriptorSetCount = 1;
            allocate.pSetLayouts = &draw_layout_.sets[1];
            VkDescriptorSet set = VK_NULL_HANDLE;
            const VkResult made = vkAllocateDescriptorSets(device, &allocate, &set);
            vkDestroyDescriptorPool(device, pool, nullptr);
            return made == VK_SUCCESS;
        };
        if (bindless_ && texture_array_limit_ > vk::kBindlessTextureSlotsMin && !variable_set_fits(vk::kBindlessTextureSlotsMin)) {
            vkDestroyDescriptorSetLayout(device, draw_layout_.sets[1], nullptr);
            draw_layout_.sets[1] = VK_NULL_HANDLE;
            texture_array_limit_ = std::min(texture_array_limit_, vk::kBindlessTextureSlotsWhole);
            texture_array_whole_ = true;
            make_layout(1);
            std::fprintf(stderr, "[vulkan] textures: the driver sizes an image set by its layout's bound; the array is %u slots, "
                                 "allocated whole\n", texture_array_limit_);
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
        // The 2D layer's merge (layer_merge_shader_source): the layer as drawn and as it started, read with Load.
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        };
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 2;
        info.pBindings = bindings;
        vk::check(vkCreateDescriptorSetLayout(device, &info, nullptr, &layer2d_set_layout_), "2D layer set layout");
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &layer2d_set_layout_;
        vk::check(vkCreatePipelineLayout(device, &layout, nullptr, &layer2d_layout_), "2D layer pipeline layout");
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
            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            vk::check(vkCreateRenderPass(device, &info, nullptr,
                &draw_layout_.discard_depth_clear_passes[1][wide ? 1u : 0u][samples_log2]),
                "colour replacement, depth and stencil clearing render pass");
            attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            vk::check(vkCreateRenderPass(device, &info, nullptr,
                &draw_layout_.discard_depth_clear_passes[0][wide ? 1u : 0u][samples_log2]),
                "colour replacement and depth clearing render pass");
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
    // The single-sample depth target alone (resolve_scene_depth): the colour is neither loaded nor stored.
    {
        VkAttachmentDescription attachment{};
        attachment.format = depth_format_;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        const VkAttachmentReference depth{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &depth;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].dstStageMask = dependencies[0].srcStageMask;
        dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1] = dependencies[0];
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &depth_alone_pass_), "depth render pass (depth alone)");
    }
    // The effects layer needs its format drawn and blended, and four colour attachments with independent blending.
    {
        VkFormatProperties layer_format{};
        vkGetPhysicalDeviceFormatProperties(context.physical_device(), vk::kLayerFormat, &layer_format);
        constexpr VkFormatFeatureFlags kNeeded = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                                 VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(context.physical_device(), &features);
        layer_targets_ok_ = (layer_format.optimalTilingFeatures & kNeeded) == kNeeded &&
                            context.properties().limits.maxColorAttachments >= 4u &&
                            context.properties().limits.maxFragmentOutputAttachments >= 4u && features.independentBlend == VK_TRUE;
    }
    // A temporal scene's passes: as the single-sample scene pass, with the motion vectors or the reactive mask next to the
    // scene target (RendererD3D12 binds them as render target 1); aux 2 index 2 adds the effects layer's colour and
    // transmittance as render targets 2 and 3 (attachments 3 and 4).
    for (const bool wide : {false, true})
    for (unsigned aux = 0u; aux < (layer_targets_ok_ ? 3u : 2u); ++aux) {
        const bool layer = aux == 2u;
        VkAttachmentDescription attachments[5]{};
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
        attachments[3] = attachments[0];
        attachments[3].format = vk::kLayerFormat;
        attachments[4] = attachments[3];
        const VkAttachmentReference colors[4] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                 {4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        const VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = layer ? 4u : 2u;
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
        info.attachmentCount = layer ? 5u : 3u;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &draw_layout_.aux_passes[wide ? 1u : 0u][aux]),
                  "temporal scene render pass");
        // The clearing form clears the scene's targets; the layer's two keep what begin_layer_window cleared them to.
        for (unsigned i = 0u; i < 3u; ++i) {
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &draw_layout_.aux_passes_clear[wide ? 1u : 0u][aux]),
                  "temporal scene clearing render pass");
    }
    // One target written whole (copies; RGBA8: a window-format picture): its old contents are dropped; it ends shader-readable.
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
        attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &color_pass_wide_), "wide copy render pass");
        attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &rgba8_pass_), "RGBA8 render pass");
        // The scene's depth as a picture (publish_scene_depth_copy).
        VkFormatProperties r32{};
        vkGetPhysicalDeviceFormatProperties(context.physical_device(), VK_FORMAT_R32_SFLOAT, &r32);
        constexpr VkFormatFeatureFlags kDepthCopyNeeds = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        depth_copy_format_ = (r32.optimalTilingFeatures & kDepthCopyNeeds) == kDepthCopyNeeds ? VK_FORMAT_R32_SFLOAT
                                                                                              : VK_FORMAT_R16_SFLOAT;
        attachment.format = depth_copy_format_;
        vk::check(vkCreateRenderPass(device, &info, nullptr, &depth_copy_pass_), "depth copy render pass");
    }
    draw_layout_.depth_format = depth_format_;
    set_curved_surfaces_offered(true);
    // The scene in a part of its targets, every pass reading that part (dynamic_resolution.h).
    set_dynamic_resolution_offered(true);
    host_window::rtx_adapter.store(vk::Context::get().properties().vendorID == 0x10DEu &&
                                       std::strstr(vk::Context::get().properties().deviceName, "RTX") != nullptr,
                                   std::memory_order_relaxed);
    draw_layout_.depth_clamp = context.depth_clamp();
    draw_layout_.depth_clip_enable = context.depth_clip_enable();
    persistent_pool_ = create_pool(device, vk_detail::kFirstPersistentSets,
                                   vk_detail::constants_sets(2u, {.images = vk_detail::kFirstPersistentSets * vk::kBoundTableSlots,
                                                                  .samplers = vk_detail::kFirstPersistentSets * vk::kBoundTableSlots}));
    return true;
}

namespace {
// The HDR10 (ST 2084) format a surface lists: A2B10G10R10 first, the RGB order of the post chain's own.
std::optional<VkSurfaceFormatKHR> hdr10_format_of(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const VkFormat wanted : {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_FORMAT_A2R10G10B10_UNORM_PACK32}) {
        for (const VkSurfaceFormatKHR& candidate : formats) {
            if (candidate.format == wanted && candidate.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
                return candidate;
            }
        }
    }
    return std::nullopt;
}
}  // namespace

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
        // The surface sets no size (Wayland): the window's last size, else the one it had when it was made.
        const vk::Extent2 chosen = vk::swapchain_extent({extent.width, extent.height}, {window_width_, window_height_},
                                                {host_window::backbuffer_width, host_window::backbuffer_height});
        extent = {chosen.width, chosen.height};
    }
    if (caps.maxImageExtent.width == 0u || caps.maxImageExtent.height == 0u ||
        (caps.currentExtent.width == 0u && caps.currentExtent.height == 0u)) {
        return false;  // minimised: follow_window_and_resize asks again at the next frame boundary without waiting for the device
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
    // HDR10 where it is asked for and the surface lists it, turned from its natural orientation or not (a phone or tablet
    // held across): the post chain's last pass writes a pre-rotated image from the picture's turned-back pixels, the
    // calibration's patterns included, and the plain blit is made again for the HDR10 pass with it.
    const bool hdr_asked = wants_hdr();
    bool hdr_made = false;
    if (hdr_asked) {
        if (const std::optional<VkSurfaceFormatKHR> hdr_format = hdr10_format_of(formats)) {
            format = *hdr_format;
            hdr_made = true;
        }
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physical_device(), surface_, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physical_device(), surface_, &count, modes.data());
    const auto supported = [&](VkPresentModeKHR mode) {
        return std::find(modes.begin(), modes.end(), mode) != modes.end();
    };
    // VSYNC on: FIFO, which a late frame may leave as FIFO_RELAXED (below). Off: mailbox (the newest frame replaces a waiting
    // one, no tearing), else immediate (tearing, as D3D12's ALLOW_TEARING). A surface with neither presents only at its
    // refresh: VSync is on there whatever was chosen (set before wants_vsync reads the settings).
    set_present_waits_for_refresh(!supported(VK_PRESENT_MODE_MAILBOX_KHR) && !supported(VK_PRESENT_MODE_IMMEDIATE_KHR));
    const bool vsync = wants_vsync();
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        mode = supported(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR
            : supported(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR : VK_PRESENT_MODE_FIFO_KHR;
    }

    // The image count, one rule on every platform from the surface's own limits: MAILBOX and IMMEDIATE take 3 (clamped to the
    // surface's minimum and maximum); FIFO the minimum plus one, so a picture can wait behind the one showing without the
    // next acquire waiting for an image the display has not released (Direct3D's three buffers). The queue's depth is the
    // pacing's (present_pacer_, the slot clock, the present wait), not this count.
    std::uint32_t images = mode == VK_PRESENT_MODE_FIFO_KHR ? caps.minImageCount + 1u : 3u;
    images = std::max(images, caps.minImageCount);
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
    // With VSync, a frame that misses its refresh is better shown at once (FIFO_RELAXED may tear) than held for the next one.
    // Where the device takes a present mode per present and the surface says FIFO_RELAXED is compatible with FIFO, the swap
    // chain is made with both and present_swapchain_image chooses; elsewhere it stays FIFO. Not under AMD's wrapped swap chain.
    const std::array<VkPresentModeKHR, 2> switchable{VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR};
    VkSwapchainPresentModesCreateInfoEXT switch_info{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT};
    bool relaxed = false;
    if (!fsr && vsync && mode == VK_PRESENT_MODE_FIFO_KHR && context.present_mode_switching() &&
        supported(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) {
        VkSurfacePresentModeEXT surface_mode{VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_EXT};
        surface_mode.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        VkPhysicalDeviceSurfaceInfo2KHR surface_info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR};
        surface_info.pNext = &surface_mode;
        surface_info.surface = surface_;
        std::array<VkPresentModeKHR, 8> listed{};
        VkSurfacePresentModeCompatibilityEXT compatible{VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_COMPATIBILITY_EXT};
        compatible.presentModeCount = static_cast<std::uint32_t>(listed.size());
        compatible.pPresentModes = listed.data();
        VkSurfaceCapabilities2KHR caps2{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};
        caps2.pNext = &compatible;
        if (vkGetPhysicalDeviceSurfaceCapabilities2KHR(context.physical_device(), &surface_info, &caps2) == VK_SUCCESS) {
            const std::size_t count_listed = std::min<std::size_t>(compatible.presentModeCount, listed.size());
            relaxed = std::find(listed.begin(), listed.begin() + static_cast<std::ptrdiff_t>(count_listed),
                                VK_PRESENT_MODE_FIFO_RELAXED_KHR) != listed.begin() + static_cast<std::ptrdiff_t>(count_listed);
        }
        if (relaxed) {
            switch_info.presentModeCount = static_cast<std::uint32_t>(switchable.size());
            switch_info.pPresentModes = switchable.data();
            switch_info.pNext = info.pNext;
            info.pNext = &switch_info;
        }
    }
    // Present timing (the pacing recorder's display reports) where the device and this surface take it; not under AMD's
    // wrapped swap chain, which presents on its own.
    VkPresentStageFlagsEXT timing_stage = 0;
    if (!fsr && context.present_timing()) {
        VkPhysicalDeviceSurfaceInfo2KHR surface_info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR};
        surface_info.surface = surface_;
        VkPresentTimingSurfaceCapabilitiesEXT timing_caps{VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT};
        VkSurfaceCapabilitiesPresentId2KHR id2_caps{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR};
        VkSurfaceCapabilitiesPresentWait2KHR wait2_caps{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR};
        id2_caps.pNext = &wait2_caps;
        wait2_caps.pNext = &timing_caps;
        VkSurfaceCapabilities2KHR caps2{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};
        caps2.pNext = &id2_caps;
        if (vkGetPhysicalDeviceSurfaceCapabilities2KHR(context.physical_device(), &surface_info, &caps2) == VK_SUCCESS &&
            id2_caps.presentId2Supported == VK_TRUE && wait2_caps.presentWait2Supported == VK_TRUE &&
            timing_caps.presentTimingSupported == VK_TRUE) {
            // The moment the picture is first seen, else the moment its first pixel leaves for the display.
            timing_stage = (timing_caps.presentStageQueries & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT) != 0u
                               ? VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT
                               : timing_caps.presentStageQueries & VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT;
        }
        if (timing_stage != 0u) {
            info.flags |= VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR |
                          VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
        }
    }
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
    swapchain_.color_space = format.colorSpace;
    swapchain_.extent = extent;
    swapchain_.image = image_extent;
    swapchain_.transform = transform;
    swapchain_.surface_transform = caps.currentTransform;
    swapchain_.copyable = (info.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0u;
    swapchain_.mode = mode;
    swapchain_.vsync = vsync;
    swapchain_.relaxed = relaxed;
    swapchain_.ids2 = !amd && timing_stage != 0u;
    swapchain_.timed = false;
    swapchain_.local_clock = false;
    std::string time_domains;  // the swap chain's, logged where none is usable
    swapchain_.timing_stage = timing_stage;
    swapchain_.refresh_qpc = 0;
    swapchain_.timing_properties_counter = ~std::uint64_t{0};
    swapchain_.timing_outstanding = 0u;
    if (swapchain_.ids2) {
        // The results' queue, and the time domain that is the runtime's own clock.
        (void)vkSetSwapchainPresentTimingQueueSizeEXT(device, handle, 32u);
        VkSwapchainTimeDomainPropertiesEXT domains{VK_STRUCTURE_TYPE_SWAPCHAIN_TIME_DOMAIN_PROPERTIES_EXT};
        std::uint64_t domains_counter = 0;
        if (vkGetSwapchainTimeDomainPropertiesEXT(device, handle, &domains, &domains_counter) == VK_SUCCESS &&
            domains.timeDomainCount != 0u) {
            std::vector<VkTimeDomainKHR> kinds(domains.timeDomainCount);
            std::vector<std::uint64_t> ids(domains.timeDomainCount);
            domains.pTimeDomains = kinds.data();
            domains.pTimeDomainIds = ids.data();
            if (vkGetSwapchainTimeDomainPropertiesEXT(device, handle, &domains, &domains_counter) >= VK_SUCCESS) {
                // The runtime's clock first (no mapping), then the present stage's own clock, then the swap chain's.
                // Apple's runtime clock (CLOCK_UPTIME_RAW) is no Vulkan domain: there the swap chain is not timed.
                const auto rank = [](VkTimeDomainKHR kind) {
#if defined(_WIN32)
                    if (kind == VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR) return 0;
#elif !defined(__APPLE__)
                    if (kind == VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR) return 0;
#endif
                    if (kind == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT) return 1;
                    if (kind == VK_TIME_DOMAIN_SWAPCHAIN_LOCAL_EXT) return 2;
                    return 3;
                };
                std::uint32_t best = domains.timeDomainCount;
                for (std::uint32_t i = 0; i < domains.timeDomainCount; ++i) {
                    time_domains += (time_domains.empty() ? "" : ",") + std::to_string(static_cast<int>(kinds[i]));
                    if (rank(kinds[i]) < 3 && (best == domains.timeDomainCount || rank(kinds[i]) < rank(kinds[best]))) {
                        best = i;
                    }
                }
#if !defined(__APPLE__)
                if (best != domains.timeDomainCount) {
                    swapchain_.time_domain = kinds[best];
                    swapchain_.time_domain_id = ids[best];
                    swapchain_.local_clock = rank(kinds[best]) != 0;
                    swapchain_.timed = !swapchain_.local_clock || calibrate_present_clock();
                }
#endif
            }
        }
    }
    swapchain_.hdr_asked = hdr_asked;
    swapchain_.hdr = hdr_made;
    last_present_id_ = 0u;  // present ids belong to a swap chain: nothing to wait for on the new one
    present_id_before_last_ = 0u;
    swapchain_calls_.get_images(device, handle, &count, nullptr);
    swapchain_.images.resize(count);
    swapchain_calls_.get_images(device, handle, &count, swapchain_.images.data());
    std::fprintf(stderr, "[vulkan] swapchain %ux%u transform=%u images=%u; display reports: %s\n", extent.width,
                 extent.height, static_cast<unsigned>(transform), count,
                 swapchain_.timed
                     ? (swapchain_.timing_stage == VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_VISIBLE_BIT_EXT
                            ? (swapchain_.local_clock ? "present timing, first pixel seen, its clock calibrated"
                                                      : "present timing, first pixel seen")
                            : (swapchain_.local_clock ? "present timing, first pixel out, its clock calibrated"
                                                      : "present timing, first pixel out"))
                 : context.display_timing() ? "display timing"
                 : swapchain_.ids2          ? ("none (present timing's clocks " + time_domains + " not usable)").c_str()
                                            : "none");
    if (count != logged_swap_images_ || mode != logged_swap_mode_) {
        // Once, and again only when the count or the mode changes (VSync toggled).
        logged_swap_images_ = count;
        logged_swap_mode_ = mode;
        // The modes by their Vulkan names.
        const char* const name = mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "PRESENT_MODE_IMMEDIATE"
            : mode == VK_PRESENT_MODE_MAILBOX_KHR ? "PRESENT_MODE_MAILBOX"
            : mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR ? "PRESENT_MODE_FIFO_RELAXED" : "PRESENT_MODE_FIFO";
        std::string offered;
        for (const VkPresentModeKHR offer : modes) {
            offered += offer == VK_PRESENT_MODE_IMMEDIATE_KHR  ? " PRESENT_MODE_IMMEDIATE"
                       : offer == VK_PRESENT_MODE_MAILBOX_KHR      ? " PRESENT_MODE_MAILBOX"
                       : offer == VK_PRESENT_MODE_FIFO_KHR         ? " PRESENT_MODE_FIFO"
                       : offer == VK_PRESENT_MODE_FIFO_RELAXED_KHR ? " PRESENT_MODE_FIFO_RELAXED"
                                                                   : " other";
        }
        const bool waits = (context.present_wait() || swapchain_.ids2) && mode == VK_PRESENT_MODE_FIFO_KHR && !amd;
        std::fprintf(stderr, "[vk] swap chain: %u images, %s (offered:%s); late frames %s; frame latency by %s\n", count, name,
                     offered.c_str(), relaxed ? "present as FIFO_RELAXED" : "wait for the next refresh",
                     waits ? "present wait (the last frame must show before the next is queued, on the interpolation's slot clock the one before it; 100 ms at most)" : "the frame fences");
    }

    const bool present_pass_replaced = present_pass_ != VK_NULL_HANDLE;
    if (present_pass_ == VK_NULL_HANDLE || present_pass_format_ != format.format) {
        if (present_pass_ != VK_NULL_HANDLE) {
            post_.set_present_pass(VK_NULL_HANDLE);
            vkDestroyRenderPass(device, present_pass_, nullptr);
            present_pass_ = VK_NULL_HANDLE;
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
        present_pass_format_ = format.format;
        if (present_pass_replaced) {
            // The format changed (HDR10 on or off; the device is idle): the chain's last passes and the plain blit are made again
            // for the new pass.
            post_.set_present_pass(present_pass_);
            if (blit_pipeline_ != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, blit_pipeline_, nullptr);
                blit_pipeline_ = create_utility_pipeline(present_pass_, utility_layout_, vk::UtilitySpirv::PresentVs,
                                                         vk::UtilitySpirv::BlitPs,
                                                         VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
                                                         false, false, false);
            }
        }
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
    if (hdr_made) {
        hdr_output_ = post_output_for(get_render_config(), get_display_hdr());
        apply_hdr_metadata(hdr_output_);
        std::fprintf(stderr, "[hdr] on peak %u nits paper %u (swapchain format %d)\n", hdr_output_.peak_nits,
                     hdr_output_.paper_white_nits, static_cast<int>(format.format));
    } else {
        hdr_output_ = PostOutput{};
        if (hdr_asked) {
            std::fprintf(stderr, "[hdr] the surface lists no HDR10 format: SDR\n");
        }
    }
    {
        DisplayHdr published = get_display_hdr();
        if (published.active != hdr_made) {
            published.active = hdr_made;
            set_display_hdr(published);
        }
    }
    host_window::backbuffer_width = extent.width;
    host_window::backbuffer_height = extent.height;
    swapchain_dirty_ = false;
    return true;
}

// HDR10 asked for and possible now. The post chain writes the encoding in its last pass, so it must be ready with its
// present-pass pipelines; AMD's frame generation swap chain and DLSS frame generation take the picture as SDR.
bool RendererVk::wants_hdr() const {
    return hdr_output_wanted(get_render_config(), get_display_hdr()) && post_.ready() && post_.present_ready() &&
           !fsr_generation_wanted() && !frame_generation_running();
}

// What the display offers, read at the first frame, after a move to another monitor and when the post chain becomes ready:
// the surface lists an HDR10 format (VK_EXT_swapchain_colorspace) and, where Windows describes the output, the output is in HDR
// mode (its figures set the peak).
void RendererVk::read_display_hdr() {
    const vk::Context& context = vk::Context::get();
    hdr_read_ = true;
    hdr_post_ready_ = post_.ready();
    bool offered = false;
    if (context.swapchain_colorspace() && surface_ != VK_NULL_HANDLE) {
        std::uint32_t count = 0;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(context.physical_device(), surface_, &count, nullptr) == VK_SUCCESS && count != 0u) {
            std::vector<VkSurfaceFormatKHR> formats(count);
            if (vkGetPhysicalDeviceSurfaceFormatsKHR(context.physical_device(), surface_, &count, formats.data()) >= VK_SUCCESS) {
                formats.resize(count);
                offered = hdr10_format_of(formats).has_value();
            }
        }
    }
    DisplayHdr hdr{};
    bool display_is_hdr = true;  // no description of the output (not Windows): the surface's listing is all there is
#if defined(_WIN32)
    const OutputHdr output = read_output_hdr(window_);
    if (!(output == hdr_reading_)) {
        hdr_reading_ = output;
        if (output.known) {
            std::fprintf(stderr, "[display] output %s, colour space %d, %u bits, peak %u nits, full frame %u nits\n", output.hdr ? "HDR" : "SDR",
                         output.colour_space, output.bits, output.peak_nits, output.full_frame_nits);
        }
    }
    // Whether the display takes HDR with Windows HDR off, and a display turned off outside the game (os_hdr.h).
    const os_hdr::Display os = os_hdr::read(output.monitor);
    os_hdr::display_changed();
    hdr.switchable = output.known && !output.hdr && os.supported && !os.blocked && context.swapchain_colorspace() && post_.ready() &&
                     post_.present_ready();
    if (output.known) {
        display_is_hdr = output.hdr;
        hdr.peak_nits = output.hdr ? output.peak_nits : 0u;
        hdr.full_frame_nits = output.hdr ? output.full_frame_nits : 0u;
        hdr.min_nits_10000 = output.hdr ? output.min_nits_10000 : 0u;
    }
#endif
    hdr.available = offered && display_is_hdr && post_.ready() && post_.present_ready();
    hdr.active = swapchain_.hdr;
    hdr.held_sdr = get_display_hdr().held_sdr;
    set_display_hdr(hdr);
}

void RendererVk::apply_hdr_metadata(const PostOutput& levels) {
    const vk::Context& context = vk::Context::get();
    if (!context.hdr_metadata() || swapchain_.handle == VK_NULL_HANDLE) {
        return;
    }
    const HdrMetadata m = hdr10_metadata(get_display_hdr(), levels);
    VkHdrMetadataEXT meta{VK_STRUCTURE_TYPE_HDR_METADATA_EXT};
    meta.displayPrimaryRed = {m.red[0], m.red[1]};
    meta.displayPrimaryGreen = {m.green[0], m.green[1]};
    meta.displayPrimaryBlue = {m.blue[0], m.blue[1]};
    meta.whitePoint = {m.white[0], m.white[1]};
    meta.maxLuminance = m.max_luminance;
    meta.minLuminance = m.min_luminance;
    meta.maxContentLightLevel = m.max_content_light;
    meta.maxFrameAverageLightLevel = m.max_frame_average_light;
    vkSetHdrMetadataEXT(context.device(), 1u, &swapchain_.handle, &meta);
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

// After a move to another monitor, a density change or a display mode change: the swap chain's format, colour space and present
// mode must still be among what the surface offers there, else the swap chain is made again; if they are, nothing is rebuilt
// (the refresh rate is read separately and sets the pacing).
bool RendererVk::surface_offers_swapchain() const {
    if (surface_ == VK_NULL_HANDLE || swapchain_.handle == VK_NULL_HANDLE) {
        return true;
    }
    const VkPhysicalDevice physical = vk::Context::get().physical_device();
    std::uint32_t count = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface_, &count, nullptr) != VK_SUCCESS) {
        return true;
    }
    std::vector<VkSurfaceFormatKHR> formats(count);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface_, &count, formats.data()) < VK_SUCCESS) {
        return true;
    }
    formats.resize(count);
    const bool format_offered = formats.empty() || std::any_of(formats.begin(), formats.end(), [this](const VkSurfaceFormatKHR& offer) {
        return offer.format == swapchain_.format && offer.colorSpace == swapchain_.color_space;
    });
    count = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface_, &count, nullptr) != VK_SUCCESS) {
        return format_offered;
    }
    std::vector<VkPresentModeKHR> modes(count);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface_, &count, modes.data()) < VK_SUCCESS) {
        return format_offered;
    }
    modes.resize(count);
    const bool mode_offered = swapchain_.mode == VK_PRESENT_MODE_FIFO_KHR ||
        std::find(modes.begin(), modes.end(), swapchain_.mode) != modes.end();
    return format_offered && mode_offered;
}

// Whether the surface has no pixels to present to (a minimised window): a zero current or largest extent.
bool RendererVk::surface_has_no_area() const {
    VkSurfaceCapabilitiesKHR caps{};
    if (surface_ == VK_NULL_HANDLE ||
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk::Context::get().physical_device(), surface_, &caps) != VK_SUCCESS) {
        return false;
    }
    return caps.maxImageExtent.width == 0u || caps.maxImageExtent.height == 0u ||
           (caps.currentExtent.width == 0u && caps.currentExtent.height == 0u);
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
                                               VkSampleCountFlagBits samples, bool color_attachment) {
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
    // A pass with no colour attachment (the depth resolve's) takes no attachment blend state.
    blend_info.attachmentCount = color_attachment ? 1u : 0u;
    blend_info.pAttachments = color_attachment ? &target : nullptr;
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
    const VkResult result = vkCreateGraphicsPipelines(device, vk::shared_pipeline_cache(), 1, &info, nullptr, &pipeline);
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
    conversion_pipeline_wide_ = create_utility_pipeline(color_pass_wide_, utility_layout_, vk::UtilitySpirv::BlitVs,
                                                        vk::UtilitySpirv::SceneConversionPs, kAll, false, false, false);
    display_filter_pipeline_wide_ = create_utility_pipeline(color_pass_wide_, utility_layout_, vk::UtilitySpirv::BlitVs,
                                                            vk::UtilitySpirv::DisplayFilterPs, kAll, false, false, false);
    depth_copy_pipeline_ = create_utility_pipeline(depth_copy_pass_, utility_layout_, vk::UtilitySpirv::BlitVs,
                                                   vk::UtilitySpirv::DepthCopyPs, VK_COLOR_COMPONENT_R_BIT, false, false,
                                                   false);
    for (unsigned kind = 0u; kind < static_cast<unsigned>(SceneUtility::Count); ++kind) {
        (void)scene_utility_pipeline(static_cast<SceneUtility>(kind));
    }
    if (msaa_max_samples_ > 1u) {
        // The single-sample depth target's pass alone (resolve_scene_depth): the resolve writes depth only.
        depth_resolve_pipeline_ = create_utility_pipeline(depth_alone_pass_, utility_layout_,
                                                          vk::UtilitySpirv::BlitVs, vk::UtilitySpirv::DepthResolvePs, 0u, false,
                                                          false, true, VK_SAMPLE_COUNT_1_BIT, false);
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
    case SceneUtility::Conversion:
    case SceneUtility::DisplayFilter:
        pipeline = create_utility_pipeline(pass, utility_layout_, vk::UtilitySpirv::BlitVs,
                                           kind == SceneUtility::Conversion ? vk::UtilitySpirv::SceneConversionPs
                                                                            : vk::UtilitySpirv::DisplayFilterPs,
                                           kRgb | VK_COLOR_COMPONENT_A_BIT, false, false, false, samples);
        break;
    case SceneUtility::Count:
        break;
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
        for (SceneSet* set : {&scene_, &wide_scene_, &alt_scene_, &target_scene_, &layer2d_set_}) destroy_scene_set(*set);
        retire(std::move(layer2d_base_));
        for (VkPipeline& pipeline : layer2d_merge_pipelines_) {
            if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
        layer2d_live_ = layer2d_failed_ = false;
        for (auto& [key, slot] : target_slots_) destroy_scene_set(slot.set);
        target_slots_.clear();
        for (auto& [key, picture] : pictures_) {
            if (picture.sampled != VK_NULL_HANDLE) vkDestroyImageView(device, picture.sampled, nullptr);
        }
        pictures_.clear();
        for (VkPipeline* pipeline : {&conversion_pipeline_, &display_filter_pipeline_,
                                     &conversion_pipeline_wide_, &display_filter_pipeline_wide_,
             &depth_resolve_pipeline_, &blit_pipeline_, &depth_copy_pipeline_}) {
            if (*pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, *pipeline, nullptr);
            *pipeline = VK_NULL_HANDLE;
        }
        for (auto& variants : scene_utility_pipelines_) {
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
        uploads_store_ = nullptr;
        texture_list_chunks_.clear();
        set_mod_post_compiler(nullptr, true);
        release_mod_post();
        post_.shutdown();
        bloom_.shutdown();
        hdr_capture_.reset();
        fg_hudless_scene_color_.reset();
        fg_hudless_display_.reset();
        fg_hudless_target_.reset();
        turn_source_ = VK_NULL_HANDLE;
        zero_vertices_.reset();
        frame_pass_targets_.clear();
        for (std::vector<RecordCommands>& records : record_commands_) {
            for (RecordCommands& record : records) {
                if (record.pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, record.pool, nullptr);
            }
            records.clear();
        }
        record_used_.fill(0u);
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
        output_source_sampled_ = false;
        captures_.clear();
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
        frame_gpu_.reset_scene_ema();
        frame_scene_drawn_.fill(false);
        pass_mark_count_.fill(0u);
        stats_pending_.fill(false);
        (void)pass_times_.take_window_list();
        stats_sum_.fill(0.0);
        profile_frame_ = pass_recording_ = false;
        destroy_swapchain();
        for (VkRenderPass pass :
             {present_pass_, color_pass_, color_pass_wide_, rgba8_pass_, depth_copy_pass_, depth_alone_pass_, draw_layout_.scene_pass,
              draw_layout_.scene_pass_clear,
              draw_layout_.target_pass, draw_layout_.target_pass_clear}) {
            if (pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, pass, nullptr);
        }
        wide_base_sampled_ = false;
        for (auto* group : {&draw_layout_.discard_color_passes, &draw_layout_.depth_clear_passes[0],
                            &draw_layout_.depth_clear_passes[1], &draw_layout_.discard_depth_clear_passes[0],
                            &draw_layout_.discard_depth_clear_passes[1]}) {
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
        present_pass_ = color_pass_ = color_pass_wide_ = rgba8_pass_ = depth_copy_pass_ = depth_alone_pass_ = draw_layout_.target_pass =
            draw_layout_.target_pass_clear =
            draw_layout_.scene_pass = draw_layout_.scene_pass_clear = VK_NULL_HANDLE;
        for (VkPipelineLayout layout : {draw_layout_.layout, utility_layout_, layer2d_layout_}) {
            if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, layout, nullptr);
        }
        draw_layout_.layout = utility_layout_ = layer2d_layout_ = VK_NULL_HANDLE;
        for (VkDescriptorSetLayout layout : {draw_layout_.sets[0], draw_layout_.sets[1], draw_layout_.sets[2],
                                             utility_set_layout_, layer2d_set_layout_}) {
            if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
        draw_layout_.sets.fill(VK_NULL_HANDLE);
        utility_set_layout_ = layer2d_set_layout_ = VK_NULL_HANDLE;
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
    // A ring whose frames took over half their budget (or spilled) grows; one whose last 30 seconds (and 600 frames) stayed
    // under a quarter of it gives half of its memory back on a frame without the scene (UploadRing::kQuietNs). Only that
    // ring is made again. --diagnostics ring-stress: all made again (at the size each wants) every kRingStressFrames frames.
    const UploadRingSlot rings[] = {{&vertex_ring_, "vertex", kRingSegments, true},
                                    {&index_ring_, "index", kFramesInFlight, true},
                                    {&constant_ring_, "constant", kFramesInFlight, true},
                                    {&matrix_ring_, "matrix", kRingSegments, false}};
    const RingRemake due = upload_rings_due(rings, frame_index_, ring_growth_failed_);
    if (due == RingRemake::None) {
        return;
    }
    const galaxy::bench::RenderSpan span(galaxy::bench::RenderPart::RingResize);
    // No wait for the GPU: a ring is made again as a new buffer. The old buffer and its chunks are released here, which
    // hands them to the context to destroy once the work submitted so far has completed.
    std::vector<gpu::BufferRef> old;
    if (remake_upload_rings(vk::Context::get().device(), rings, due == RingRemake::All, ring_growth_failed_, old)) {
        // The vertex and matrix rings share one segment counter and one fence for each segment (history_segment_,
        // history_segment_fence_). A ring made again is a new buffer, every segment of which is free; a ring that was not made
        // again (the vertex ring, when only the matrix ring grew) keeps the segments the frames in flight write and read, so
        // the counter and the fences stay as they are: with the counter at 0 and the fences gone, the next frame wrote the
        // segment a frame in flight was still drawing from (P-339).
        history_used_ = true;
    }
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
    const galaxy::bench::RenderSpan span(galaxy::bench::RenderPart::FrameWait);
    if (fence_value == 0u) {
        throw std::runtime_error("invalid frame-specific GPU fence wait");
    }
    if (!vk::Context::get().wait(fence_value, 10'000'000'000ull)) {
        throw std::runtime_error("frame-specific GPU completion wait timed out or the device was lost");
    }
}

void RendererVk::begin_frame() {
    host_window::watch_render_config_file();
    if (!streamline_frame_open_) {
        vk::streamline_vk::open_frame(swapchain_fsr_);
        streamline_frame_open_ = true;
    }
    follow_window_and_resize();
    apply_window_size();
    grow_rings();
    prepare_temporal_upscaler();
    apply_dynamic_resolution();
    // draw.post: a program reading motion registers only while a temporal upscaler makes it.
    set_mod_post_motion(temporal_ != nullptr && temporal_mode());
    // A hold of the temporal upscaler on generated frames: decided again when its settings changed, tried again
    // when its wait passed (the governor then measures generated frames again, not by the cost that held it).
    if (temporal_interp_hold_.lift_at_frame_start([this] { return temporal_interp_key() != temporal_interp_key_; },
                                                  galaxy::qpc_to_ns(galaxy::qpc_now()))) {
        frame_gpu_.reset_generated_ema();
        std::cerr << "[interp] temporal upscaling of generated frames tried again\n";
    }
    sync_post_process();
    bloom_.poll_ready();
    bloom_.trim_idle();
    ao_.trim_idle();

    frame_slot_ = static_cast<unsigned>(frame_index_ % kFramesInFlight);
    Frame& frame = frames_[frame_slot_];
    frame_ = &frame;
    vk::Context& context = vk::Context::get();
    const VkDevice device = context.device();
    if (frame.fence != 0u) {
        // --diagnostics timeline: a wait for the slot's last frame, as Direct3D 12 records it (a = its fence value).
        const bool timeline_slot_wait = galaxy::timeline::enabled() && completed_fence_value() < frame.fence;
        if (timeline_slot_wait) {
            galaxy::timeline::record(galaxy::timeline::Event::SlotWaitBegin, frame.fence);
        }
        wait_for_fence_value(frame.fence);
        if (timeline_slot_wait) {
            galaxy::timeline::record(galaxy::timeline::Event::SlotWaitEnd, frame.fence);
        }
    }
    if (timestamp_pending_[frame_slot_] && timestamps_ != VK_NULL_HANDLE) {
        std::uint64_t ticks[2]{};
        if (vkGetQueryPoolResults(device, timestamps_, frame_slot_ * 2u, 2u, sizeof(ticks), ticks,
                                  sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
            ticks[1] >= ticks[0]) {
            const double gpu_ms = static_cast<double>(ticks[1] - ticks[0]) * timestamp_period_ns_ / 1'000'000.0;
            frame_gpu_.add(gpu_ms, frame_scene_drawn_[frame_slot_], frame_generated_[frame_slot_]);
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
    {
        const galaxy::bench::RenderSpan span(galaxy::bench::RenderPart::ResourceFree);
        context.collect();
        frame.retired.clear();
        frame.retired_buffers.clear();
        destroy_retired_pools(frame);
    }
    trim_idle_effect_targets();
    // Once a second of the game (target slots age on idle_clock).
    if (idle_clock() - target_swept_step_ >= kTargetSweepSteps) {
        target_swept_step_ = idle_clock();
        sweep_target_slots();
    }
    reset_sampler_frame();
    vk::check(vkResetCommandPool(device, frame.pool, 0), "vkResetCommandPool");
    for (RecordCommands& record : record_commands_[frame_slot_]) {
        vk::check(vkResetCommandPool(device, record.pool, 0), "vkResetCommandPool (chunk)");
    }
    record_used_[frame_slot_] = 0u;
    vk::check(vkResetDescriptorPool(device, frame.descriptors, 0), "vkResetDescriptorPool");
    // The overflow pools the slot's last frame took are reset and kept for the next; those no frame of the slot has taken
    // for kIdleSteps game steps are destroyed (the newest first: they are the largest).
    for (std::size_t i = 0; i < frame.overflow_used; ++i) {
        vk::check(vkResetDescriptorPool(device, frame.overflow_descriptors[i], 0), "vkResetDescriptorPool (overflow)");
    }
    frame.overflow_peak = std::max(frame.overflow_peak, frame.overflow_used);
    if (idle_since(frame.overflow_peak_step)) {
        if (frame.overflow_descriptors.size() > frame.overflow_peak) {
            while (frame.overflow_descriptors.size() > frame.overflow_peak) {
                vkDestroyDescriptorPool(device, frame.overflow_descriptors.back(), nullptr);
                frame.overflow_descriptors.pop_back();
            }
            // The next pool made doubles the last one kept (kFrameSets, twice that, ... up to kMaxOverflowSets).
            frame.overflow_sets = 0u;
            for (std::size_t i = 0; i < frame.overflow_descriptors.size(); ++i) {
                frame.overflow_sets = frame.overflow_sets == 0u ? vk_detail::kFrameSets
                                      : std::min(frame.overflow_sets * 2u, vk_detail::kMaxOverflowSets);
            }
        }
        frame.overflow_peak = 0u;
        frame.overflow_peak_step = idle_clock();
    }
    frame.overflow_used = 0u;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vk::check(vkBeginCommandBuffer(frame.uploads, &begin), "vkBeginCommandBuffer");
    vk::check(vkBeginCommandBuffer(frame.commands, &begin), "vkBeginCommandBuffer");
    commands_ = frame.commands;
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
    // The transfer queue's copies go out, and the images this frame names are acquired in the upload list (the submission
    // waits for the queue's timeline semaphore).
    const std::uint64_t transfer_wait = uploads_store_ != nullptr ? uploads_store_->backend().submit_uploads() : 0u;
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
                               swapchain_.rendered[swap_image_], transfer_wait)
        : context.submit_frame(buffers, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, transfer_wait);
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
    captures_.submitted(fence);
    if (on_submitted != nullptr) {
        on_submitted(submission_context, fence);
    }
    if (presenting) {
        present_swapchain_image();
    }
    surface_lock_ = {};  // an image acquired but not presented
    swap_image_ = UINT32_MAX;
    ++frame_index_;
    // Every ten seconds (Linux, Android): the render thread's CPU time and how much of it the driver's calls took.
    ++gpu_window_.recorded;
    if (cpu_report_.due()) {
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
                         static_cast<double>(gpu_window_.passes) / gpu_window_.recorded,
                         static_cast<double>(gpu_window_.copies) / gpu_window_.recorded, scene_extent_.width,
                         scene_extent_.height, swapchain_.image.width, swapchain_.image.height);
            galaxy::whole_lines::Line line;
            line.add("[vk-gpu] scene target opened a frame (closed by > opened by):");
            for (const GpuWindow::Open& open : gpu_window_.opens) {
                if (open.count != 0u) {
                    line.add(" %s > %s%s %.2f;", open.closed, open.opened, open.cleared ? " (clear)" : "",
                             static_cast<double>(open.count) / gpu_window_.recorded);
                }
            }
            line.write(stderr);
        }
        if (!trace_present_stats_enabled() && pass_times_.window_frames() != 0u) {
            galaxy::whole_lines::Line line;
            line.add("[gpu-profile] %u sampled submissions; GPU ms:", pass_times_.window_frames());
            line.add("%s", pass_times_.take_window_list().c_str());
            line.write(stderr);
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
    // On a variable refresh display with VSync on the pacer holds presents under the refresh (paced_present_rate); a frame
    // under the cap goes out as soon as it is ready. Interpolated pictures are paced by their slot clock (RenderCore), as
    // on Direct3D: a second wait after the slot would carry a picture past the refresh it was drawn for.
    const float paced_fps = paced_present_rate(presented_max_fps(cfg), cfg.vsync_interval, variable_refresh_, display_refresh_hz_);
    const double pace_wait_ms =
        interpolation_mode() ? 0.0 : present_pacer_.pace(variable_refresh_ ? 0u : cfg.vsync_interval, paced_fps);
    const std::int64_t present_start = galaxy::qpc_now();
    {
        // The target rate: dynamic multi frame generation reaches it, a fixed multiplier is the largest multiple of 60 at or
        // below it. Only while the game shows play (render_config.h); otherwise its own frames alone.
        unsigned generated = 0u;
        if (frame_generation_running() && frame_generation_allowed()) {
            const double rate = present_frame_rate(cfg.max_fps, cfg.vsync_interval, display_refresh_hz_);
            // An unlimited rate takes every frame the library makes.
            const long most = static_cast<long>(host_window::upscaler_caps().frame_generation_max);
            const double multiple = std::isfinite(rate) ? std::min(rate / kGameStepRate + 1e-3, static_cast<double>(most) + 1.0)
                                                        : static_cast<double>(most) + 1.0;
            generated = static_cast<unsigned>(std::clamp<long>(static_cast<long>(multiple) - 1, 1, most));
        }
        vk::streamline_vk::set_generation(generated, host_window::upscaler_caps().frame_generation_dynamic,
                                          cfg.max_fps > 0.0f && std::isfinite(cfg.max_fps) ? cfg.max_fps : 0.0f);
    }
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &swapchain_.rendered[swap_image_];
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain_.handle;
    info.pImageIndices = &swap_image_;
    // The same latency rule as Direct3D 12's waitable object: with VSync, the last frame queued is on the screen before this
    // one is queued; 100 ms bounds a display that stopped. A picture on the interpolation's slot clock (a present moment:
    // one slot a vblank, or every n-th) is paced by the clock already: it waits only for the present before the last, one
    // picture queued behind the one showing at most. Waiting for the last one too would tie it to when the driver reports
    // that one shown, a refresh late now and then on some drivers: the presents would switch between landing in their
    // slot's refresh and the one after, each switch showing a refresh twice. Where the device has no present wait, or under
    // frame generation (whose presents are the library's), the frame fences bound the queue.
    const bool ids = (context.present_wait() || swapchain_.ids2) && swapchain_.mode == VK_PRESENT_MODE_FIFO_KHR &&
                     !swapchain_fsr_ && !frame_generation_running();
    const std::uint64_t wait_id = present_moment_ != 0 ? present_id_before_last_ : last_present_id_;
    if (ids && wait_id != 0u) {
        VkResult waited = VK_SUCCESS;
        if (swapchain_.ids2) {
            VkPresentWait2InfoKHR wait{VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR};
            wait.presentId = wait_id;
            wait.timeout = 100'000'000ull;
            waited = vkWaitForPresent2KHR(context.device(), swapchain_.handle, &wait);
        } else {
            waited = vkWaitForPresentKHR(context.device(), swapchain_.handle, wait_id, 100'000'000ull);
        }
        if (waited == VK_TIMEOUT && ++present_wait_timeouts_ <= 4u) {
            std::fprintf(stderr, "[present] the previous frame was not shown within 100 ms\n");
        }
    }
    std::uint64_t this_present_id = 0u;
    VkPresentIdKHR present_id_info{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
    VkPresentId2KHR present_id2_info{VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR};
    // A swap chain made for present timing: every present has an id (its timing result names it), in id 2's form.
    // Never more asked for than the results' queue holds (32, create_swapchain): a full queue would fail the present,
    // and presents that do not finish (a hidden window) leave their results unread. Only while the window is shown: a
    // present of a window never shown (a hidden test run) that asks for its timing never finishes, and every present
    // wait after it runs out its time (measured: 15 steps a second instead of 60).
    const bool timed_present =
        swapchain_.timed && window_shown_ && !frame_generation_running() && swapchain_.timing_outstanding < 31u;
    if (ids || timed_present) {
        this_present_id = ++present_id_counter_;
        if (swapchain_.ids2) {
            present_id2_info.swapchainCount = 1;
            present_id2_info.pPresentIds = &this_present_id;
            present_id2_info.pNext = info.pNext;
            info.pNext = &present_id2_info;
        } else {
            present_id_info.swapchainCount = 1;
            present_id_info.pPresentIds = &this_present_id;
            present_id_info.pNext = info.pNext;
            info.pNext = &present_id_info;
        }
    }
    // When it reached the display: one stage's time in the runtime's clock, read back by report_present_times.
    VkPresentTimingInfoEXT timing_info{VK_STRUCTURE_TYPE_PRESENT_TIMING_INFO_EXT};
    VkPresentTimingsInfoEXT timings_info{VK_STRUCTURE_TYPE_PRESENT_TIMINGS_INFO_EXT};
    if (timed_present) {
        timing_info.timeDomainId = swapchain_.time_domain_id;
        timing_info.presentStageQueries = swapchain_.timing_stage;
        timings_info.swapchainCount = 1;
        timings_info.pTimingInfos = &timing_info;
        timings_info.pNext = info.pNext;
        info.pNext = &timings_info;
    }
    // A frame that comes a refresh late (the time since the previous present is over a quarter longer than the rate asks for)
    // is shown at once rather than held for the next refresh.
    VkPresentModeKHR this_mode = VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainPresentModeInfoEXT mode_info{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT};
    if (swapchain_.relaxed && swap_present_qpc_ != 0 && !frame_generation_running()) {
        const double since = static_cast<double>(galaxy::qpc_now() - swap_present_qpc_) / static_cast<double>(galaxy::qpc_frequency());
        const double wanted = 1.0 / present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, display_refresh_hz_);
        if (since > wanted * 1.25) {
            this_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        }
        mode_info.swapchainCount = 1;
        mode_info.pPresentModes = &this_mode;
        mode_info.pNext = info.pNext;
        info.pNext = &mode_info;
    }
    VkPresentTimeGOOGLE present_time{};
    VkPresentTimesInfoGOOGLE present_times{VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE};
    std::int64_t frame_start = 0, period = 0, vsync = 0, now = 0;
    // The picture's moment (set_present_moment), for this present only.
    const std::int64_t moment = present_moment_;
    present_moment_ = 0;
    // Presents step by one present period: the game's frame period times 60 / the present rate (90 Hz: 1.5 presents
    // per game frame). Pictures less than a vsync apart (an unlimited rate, render_config.h kUnlimitedFrameRate)
    // have no slot: each is shown at the next vsync, the newest in place of one still queued.
    const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, display_refresh_hz_);
    const std::int64_t step = std::max<std::int64_t>(1, std::llround(1e9 / rate));
    // An interpolated picture is timed by its moment on the display's vsync grid, at any refresh rate; the game's own
    // frames by the game's frame slots, which follow the vsyncs where the refresh rate is a multiple of 60.
    // qpc_now() is CLOCK_MONOTONIC nanoseconds off Windows: the presentation times' clock.
    // Timed only where the swap chain keeps every picture (present_slots.h presents_timed): MAILBOX on Android is the
    // BufferQueue's async mode, whose next present replaces a picture held for its slot.
    const bool timed = context.display_timing() && presents_timed(swap_queue());
    std::int64_t grid = 0;
    const bool by_moment = timed && moment != 0 && vblank_grid(grid, vsync);
    const bool by_frames = !by_moment && timed && host_window::present_grid(frame_start, period, vsync, now);
    if ((by_moment || by_frames) && step >= vsync - vsync / 16) {
        PresentTiming& timing = present_timing_;
        if (by_moment) {
            now = galaxy::qpc_now();
        }
        PresentClock clock;
        clock.by_moment = by_moment;
        clock.moment = moment;
        clock.grid = grid;
        clock.vsync = vsync;
        clock.frame_start = frame_start;
        clock.period = period;
        clock.step = step;
        const std::int64_t slot = timing.slots.place(clock);
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
        present_times.pNext = info.pNext;
        info.pNext = &present_times;
    } else {
        present_timing_.slots.reset();
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
    // --diagnostics timeline: the present's call, as Direct3D 12 records it (a = the present's ordinal).
    static std::uint64_t timeline_present_ordinal = 0u;
    const bool timeline_present = galaxy::timeline::enabled();
    if (timeline_present) {
        galaxy::timeline::record(galaxy::timeline::Event::PresentBegin, ++timeline_present_ordinal, 0u);
    }
    VkResult result = VK_SUCCESS;
    {
        const galaxy::bench::RenderSpan span(galaxy::bench::RenderPart::Present);
        result = swapchain_calls_.present(context.queue(), &info);
    }
    if (timeline_present) {
        galaxy::timeline::record(galaxy::timeline::Event::PresentEnd, timeline_present_ordinal);
    }
    vk::streamline_vk::mark_present_end();
    const bool queued = ids && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR);
    // The pacing recorder: the present's id where its moment on the display will be reported (present timing's, or
    // the display timing's).
    {
        const bool shown = result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
        galaxy::pacing::presented(!shown ? 0u
                                  : timed_present ? this_present_id
                                  : present_times.swapchainCount != 0u ? present_time.presentID
                                                                       : 0u);
    }
    present_id_before_last_ = queued ? last_present_id_ : 0u;
    last_present_id_ = queued ? this_present_id : 0u;
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
    // The imported driver showed a frame: its start finished (GpuDrivers.kt failedStart).
    if (!driver_confirmed_ && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)) {
        driver_confirmed_ = true;
        if (const std::filesystem::path& driver = ::galaxy::launch_args().gpu_driver;
            !driver.empty() && vk::imported_driver_loaded()) {
            std::error_code ignored;
            std::filesystem::remove(driver / "starting", ignored);
        }
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
    if (timed_present) {
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
            ++swapchain_.timing_outstanding;
        }
    }
    if (swapchain_.timed && swapchain_.timing_outstanding != 0u) {
        report_present_times();
    }
    // Android keeps only the last few frames' timings: read them often.
    if (context.display_timing() && present_timing_.next_id % 4u == 0u) {
        report_present_timing();
    }
    if (!window_shown_ && window_ != nullptr && !host_window::hidden_window_mode_enabled()) {
        window_shown_ = true;
        host_window::show_window(window_);
    }
}

// The swap chain's own present clock and the runtime's, read together: a result's time t (nanoseconds) is then
// calibration_qpc + (t - calibration_local) in the runtime's ticks. Read again every second, so neither clock's drift
// adds up.
bool RendererVk::calibrate_present_clock() {
#if defined(__APPLE__)
    return false;
#else
#if defined(_WIN32)
    constexpr VkTimeDomainKHR runtime_clock = VK_TIME_DOMAIN_QUERY_PERFORMANCE_COUNTER_KHR;
#else
    constexpr VkTimeDomainKHR runtime_clock = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
#endif
    VkSwapchainCalibratedTimestampInfoEXT swap{VK_STRUCTURE_TYPE_SWAPCHAIN_CALIBRATED_TIMESTAMP_INFO_EXT};
    swap.swapchain = swapchain_.handle;
    swap.presentStage =
        swapchain_.time_domain == VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT ? swapchain_.timing_stage : VkPresentStageFlagsEXT{0};
    swap.timeDomainId = swapchain_.time_domain_id;
    std::array<VkCalibratedTimestampInfoKHR, 2> infos{};
    infos[0].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
    infos[0].pNext = &swap;
    infos[0].timeDomain = swapchain_.time_domain;
    infos[1].sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
    infos[1].timeDomain = runtime_clock;
    std::array<std::uint64_t, 2> times{};
    std::uint64_t deviation = 0;
    if (vkGetCalibratedTimestampsKHR(vk::Context::get().device(), 2u, infos.data(), times.data(), &deviation) !=
        VK_SUCCESS) {
        return false;
    }
    swapchain_.calibration_local = times[0];
    swapchain_.calibration_qpc = static_cast<std::int64_t>(times[1]);
    return true;
#endif
}

// Present timing's results (VK_EXT_present_timing): each finished present's moment at the swap chain's stage, in the
// runtime's clock, to the pacing recorder; the refresh duration when the swap chain's timing properties change.
void RendererVk::report_present_times() {
    const VkDevice device = vk::Context::get().device();
    if (swapchain_.local_clock && galaxy::qpc_now() - swapchain_.calibration_qpc > galaxy::qpc_frequency()) {
        (void)calibrate_present_clock();  // a failed read keeps the last
    }
    constexpr std::uint32_t kResults = 16u;
    std::array<VkPresentStageTimeEXT, kResults> stages{};
    std::array<VkPastPresentationTimingEXT, kResults> results{};
    for (std::uint32_t i = 0; i < kResults; ++i) {
        results[i].sType = VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_EXT;
        results[i].presentStageCount = 1u;
        results[i].pPresentStages = &stages[i];
    }
    VkPastPresentationTimingInfoEXT info{VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_INFO_EXT};
    info.flags = VK_PAST_PRESENTATION_TIMING_ALLOW_OUT_OF_ORDER_RESULTS_BIT_EXT;
    info.swapchain = swapchain_.handle;
    VkPastPresentationTimingPropertiesEXT properties{VK_STRUCTURE_TYPE_PAST_PRESENTATION_TIMING_PROPERTIES_EXT};
    properties.presentationTimingCount = kResults;
    properties.pPresentationTimings = results.data();
    if (vkGetPastPresentationTimingEXT(device, &info, &properties) < VK_SUCCESS) {
        return;
    }
    if (properties.timingPropertiesCounter != swapchain_.timing_properties_counter) {
        VkSwapchainTimingPropertiesEXT timing{VK_STRUCTURE_TYPE_SWAPCHAIN_TIMING_PROPERTIES_EXT};
        std::uint64_t counter = 0;
        if (vkGetSwapchainTimingPropertiesEXT(device, swapchain_.handle, &timing, &counter) == VK_SUCCESS) {
            swapchain_.timing_properties_counter = counter;
            // Nanoseconds to the runtime's ticks; 0 where the refresh varies.
            swapchain_.refresh_qpc = static_cast<std::int64_t>(static_cast<double>(timing.refreshDuration) *
                                                               static_cast<double>(galaxy::qpc_frequency()) / 1e9);
        }
    }
    const std::uint32_t count = std::min(properties.presentationTimingCount, kResults);
    for (std::uint32_t i = 0; i < count; ++i) {
        const VkPastPresentationTimingEXT& r = results[i];
        if (r.reportComplete != VK_TRUE) {
            continue;
        }
        swapchain_.timing_outstanding -= swapchain_.timing_outstanding != 0u ? 1u : 0u;
        if (r.timeDomainId != swapchain_.time_domain_id) {
            continue;
        }
        if (r.presentStageCount == 0u || stages[i].time == 0u) {
            galaxy::pacing::not_shown(r.presentId);  // complete with no time: it never reached the display
        } else if (swapchain_.local_clock) {
            const double since_ns =
                static_cast<double>(static_cast<std::int64_t>(stages[i].time - swapchain_.calibration_local));
            galaxy::pacing::displayed(r.presentId,
                                      swapchain_.calibration_qpc +
                                          std::llround(since_ns * static_cast<double>(galaxy::qpc_frequency()) / 1e9),
                                      swapchain_.refresh_qpc);
        } else {
            galaxy::pacing::displayed(r.presentId, static_cast<std::int64_t>(stages[i].time), swapchain_.refresh_qpc);
        }
    }
}

// The past presentation times (VK_GOOGLE_display_timing): whether each frame was shown by the slot it asked for, and
// one frame period after the frame before it. Every ten seconds to the log.
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
        if (!swapchain_.timed) {  // present timing's reports, where it is there, are the recorder's
            galaxy::pacing::displayed(past[i].presentID, actual, timing.refresh_ns != 0 ? timing.refresh_ns : timing.vsync_ns);
        }
        const auto at = past[i].presentID % timing.requested.size();
        const bool identified = timing.requested_id[at] == past[i].presentID;
        const std::int64_t requested = identified ? timing.requested[at] : 0;
        const auto expected = identified ? timing.requested_period[at] : kGameStepNs;
        timing.margin_sum += static_cast<std::int64_t>(past[i].presentMargin);
        if (identified && timing.called[at] != 0) {
            timing.slots.observe(requested, static_cast<std::int64_t>(past[i].earliestPresentTime), actual,
                                 static_cast<std::int64_t>(past[i].presentMargin), expected, half_vsync);
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
        ++timing.frames;
        if (timing.report.due()) {
            std::fprintf(stderr, "[present-timing] %u frames: %u shown at their requested slot, %u late, %u without one "
                         "(unmatched); %u at the target interval; actual-hz %.2f, interval-max %.2f ms, duplicate-times %u; "
                         "late(available/delayed)=%u/%u margin %.2f ms (driver refresh %.2f, display %.2f ms); "
                         "pipeline %.2f ms (a late frame missed its slot by %.2f at most)\n",
                         timing.frames, timing.on_slot, timing.late, timing.unrequested, timing.even,
                         timing.interval_sum > 0 ? 1e9 * timing.intervals / double(timing.interval_sum) : 0.0,
                         double(timing.interval_max) / 1e6, timing.duplicates, timing.available_late, timing.delayed_late,
                         double(timing.margin_sum) / 1e6 / timing.frames,
                         double(timing.refresh_ns) / 1e6, double(timing.vsync_ns) / 1e6,
                         double(timing.slots.depth_used()) / 1e6, double(timing.slots.take_late_max()) / 1e6);
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
        const double profiled = static_cast<double>(std::max(pass_times_.window_frames(), 1u));
        message << pass_times_.take_window_field();  // each pass's ms a profiled frame
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
    (void)gpu_pass_mark(pass, false);
}

// A scope ends. A scene pass it opened and leaves open goes on with the outer work (the scene's geometry after the camera's
// widen or the bloom's composite); no timestamp splits a pass on a tiled GPU, so that whole pass is billed to the outer.
void RendererVk::gpu_pass_restore(GpuPass outer) {
    if (scene_pass_open_ && open_pass_mark_ < pass_mark_count_[frame_slot_] &&
        pass_marks_[frame_slot_][open_pass_mark_] == current_pass_) {
        pass_marks_[frame_slot_][open_pass_mark_] = outer;
    }
    gpu_pass(outer);
}

// The mark's index, or kNoPassMark. A scene pass's beginning always takes a mark of its own, so gpu_pass_restore can relabel
// the pass alone.
unsigned RendererVk::gpu_pass_mark(GpuPass pass, bool pass_begins) {
    const bool changed = pass != current_pass_;
    current_pass_ = pass;
    unsigned& marks = pass_mark_count_[frame_slot_];
    const VkCommandBuffer commands = frames_[frame_slot_].commands;
    // The pass by name for RenderDoc and the validation layer.
    if (changed && pass_recording_ && commands_ != VK_NULL_HANDLE && pass != GpuPass::Count && vk::Context::get().debug_labels() &&
        vkCmdInsertDebugUtilsLabelEXT != nullptr) {
        VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = kGpuPassPageNames[static_cast<std::size_t>(pass)];
        vkCmdInsertDebugUtilsLabelEXT(commands_, &label);
    }
    // Tiled GPUs execute a render pass tile by tile. Interior timestamps do not delimit whole-image work;
    // record only at existing pass boundaries, never split a pass to measure it.
    if (scene_pass_open_ || pass_queries_ == VK_NULL_HANDLE || !pass_recording_ || commands_ != commands ||
        marks + 1u >= kGpuPassSlots || (!pass_begins && marks != 0u && pass_marks_[frame_slot_][marks - 1u] == pass)) {
        return kNoPassMark;
    }
    vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pass_queries_,
                        frame_slot_ * kGpuPassSlots + marks);
    pass_marks_[frame_slot_][marks] = pass;
    return marks++;
}

std::size_t RendererVk::gpu_pass_times(GpuPassTime* out, std::size_t capacity) const noexcept {
    return pass_times_.page_times(out, capacity);
}

void RendererVk::prepare_gpu_pass_queries() {
    // Passes are timed only on request: --diagnostics gpu-passes (with the shader invocation counts) or the settings page's
    // GPU TIMING. A timestamp at each pass boundary (no pass is split) of every submission then; with neither nothing is
    // recorded or read.
    const bool full = ::galaxy::diagnosing(::galaxy::kDiagnoseGpuPasses);
    const bool wanted = full || gpu_timing_wanted_.load(std::memory_order_relaxed);
    profile_frame_ = pass_times_.begin_frame(timestamps_ != VK_NULL_HANDLE && wanted);
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
        pass_times_.add_frame(gpu_pass_ms(ticks.data(), pass_marks_[frame_slot_].data(), marks - 1u, ms_per_tick),
                              frame_scene_drawn_[frame_slot_] && !frame_generated_[frame_slot_]);
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

void RendererVk::sync_post_process() {
    const bool was_ready = post_.ready();
    if (!post_.poll_ready()) {
        return;
    }
    if (!was_ready) {
        set_post_process_status(post_.status());
        post_.prepare_pipelines(scene_passes(float_scene_));
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
    const double mode_hz = host_window::display_refresh_hz(window_);
    if (mode_hz <= 1.0) {
        return;
    }
    // The vblanks' own rate (where the platform times them): a display that varies its refresh with the presents shows a rate
    // well under the mode's.
    std::int64_t vblank = 0;
    std::int64_t period = 0;
    const double measured_hz = vblank_grid(vblank, period) && period > 0
        ? static_cast<double>(galaxy::qpc_frequency()) / static_cast<double>(period) : 0.0;
    const bool variable = next_variable_refresh(variable_refresh_, mode_hz, measured_hz);
    const double hz = variable ? variable_refresh_cap(mode_hz) : mode_hz;
    if (variable != variable_refresh_ || (variable && std::abs(hz - display_refresh_hz_) >= 0.1)) {
        if (variable) {
            std::cerr << "[pacing] vrr " << mode_hz << " Hz cap " << hz << '\n';
        } else {
            std::cerr << "[pacing] vrr off\n";
        }
    }
    variable_refresh_ = variable;
    g_variable_refresh.store(variable, std::memory_order_relaxed);
    if (std::abs(hz - display_refresh_hz_) >= 0.1) {
        if (!variable) {
            std::cerr << "[display] refresh-hz=" << hz << '\n';
        }
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
        const vk::Extent2 window = vk::unpack_window_extent(extent);
        const std::uint32_t width = window.width;
        const std::uint32_t height = window.height;
        if (width != 0u && height != 0u) {
            window_width_ = width;
            window_height_ = height;
        }
        if (width != swapchain_.extent.width || height != swapchain_.extent.height) {
            swapchain_dirty_ = true;
        }
    }
    if (wants_vsync() != swapchain_.vsync || fsr_generation_wanted() != swapchain_fsr_) {
        swapchain_dirty_ = true;
    }
    bool monitor_moved = false;
#if defined(_WIN32)
    // The window moved to another monitor, whose rate may differ, or its density or display mode changed: the rate is measured at
    // once, not at the next periodic check, and the swap chain is made again only if its format, colour space or present mode
    // is no longer what the surface offers.
    monitor_moved = host_window::monitor_changed.exchange(false, std::memory_order_acq_rel);
    if (monitor_moved && !swapchain_dirty_ && !surface_offers_swapchain()) {
        std::fprintf(stderr, "[display] the new monitor does not offer the swap chain's format or present mode: made again\n");
        swapchain_dirty_ = true;
    }
#endif
    if (!hdr_read_ || monitor_moved || post_.ready() != hdr_post_ready_) {
        read_display_hdr();
    }
    {
        const RenderConfig cfg = get_render_config();
        DisplayHdr display = get_display_hdr();
        const bool held = cfg.hdr && (fsr_generation_wanted() || frame_generation_running());
        if (held != display.held_sdr) {
            display.held_sdr = held;
            set_display_hdr(display);
        }
#if defined(_WIN32)
        // Windows HDR follows the game's wish for the window's display: on while the picture can be HDR (os_hdr.h). A
        // hidden start (a test's) never switches the display.
        const bool wanted = cfg.hdr && !held && post_.ready() && post_.present_ready() && (display.available || display.switchable) &&
                            !host_window::hidden_window_mode_enabled();
        if (!os_hdr_asked_ || wanted != os_hdr_wanted_ || hdr_reading_.monitor != os_hdr_monitor_) {
            os_hdr_asked_ = true;
            os_hdr_wanted_ = wanted;
            os_hdr_monitor_ = hdr_reading_.monitor;
            os_hdr::want(os_hdr_monitor_, wanted);
        }
#endif
    }
    if (wants_hdr() != swapchain_.hdr_asked) {
        swapchain_dirty_ = true;
    } else if (swapchain_.hdr) {
        // A level changed (the paper white, the peak, the black, the calibration's pattern): the last write's constants and
        // the metadata, not the swap chain.
        const PostOutput levels = post_output_for(get_render_config(), get_display_hdr());
        if (levels != hdr_output_) {
            hdr_output_ = levels;
            apply_hdr_metadata(levels);
            // The calibration's levels change with each step of its slider: logged as it ends.
            if (levels.pattern == HdrPattern::None && !hdr_calibration().active) {
                std::fprintf(stderr, "[hdr] on peak %u nits paper %u black %.4f\n", levels.peak_nits,
                             levels.paper_white_nits, static_cast<double>(hdr_black_nits(levels.black)));
            }
        }
    }
    if (swapchain_dirty_) {
        // A surface with no area (the window minimised) is not retried: nothing is waited for or made until the surface has an
        // area again, and that is noticed here, at the next frame boundary the game's resumed frames bring (the host stops
        // the game while minimised, so no frame comes in between).
        if (surface_has_no_area()) {
            return;
        }
        vk::streamline_vk::generation_off();
        vkDeviceWaitIdle(vk::Context::get().device());
        create_swapchain();
    }
    if (monitor_moved || display_refresh_.due()) {
        refresh_display_rate();
    }
}

bool RendererVk::capture_sdr_picture(VkCommandBuffer commands, const vk::Texture& picture, const galaxy::ContentViewport& content,
                                     const vk::PostProcessorVk::SetAllocator& allocate) {
    vk::Context& context = vk::Context::get();
    const std::uint32_t width = swapchain_.image.width;
    const std::uint32_t height = swapchain_.image.height;
    vk::PostProcessorVk::PresentStep step{};
    if (!present_black_) {
        // The window-pass pipelines (8 bits, dithered): the picture as an SDR window shows it.
        step = post_.prepare_present(commands, picture, picture.width, picture.height, static_cast<unsigned>(content.width),
                                     static_cast<unsigned>(content.height), false, allocate);
        if (step.pipeline == VK_NULL_HANDLE) {
            return false;
        }
    }
    hdr_capture_used_step_ = idle_clock();
    if (!hdr_capture_ || hdr_capture_->width != width || hdr_capture_->height != height) {
        if (hdr_capture_) {
            retire(std::move(hdr_capture_));  // an earlier capture's commands may still read it
        }
        hdr_capture_ = context.create_texture(width, height, 1u, VK_FORMAT_R8G8B8A8_UNORM,
                                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                              true, "HDR capture (SDR picture)");
        if (hdr_capture_) {
            VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            info.renderPass = rgba8_pass_;
            info.attachmentCount = 1;
            info.pAttachments = &hdr_capture_->target_view;
            info.width = width;
            info.height = height;
            info.layers = 1;
            if (vkCreateFramebuffer(context.device(), &info, nullptr, &hdr_capture_->framebuffer) != VK_SUCCESS) {
                hdr_capture_.reset();
            }
        }
    }
    if (!hdr_capture_) {
        return false;
    }
    std::shared_ptr<vk::Buffer> readback = capture_buffer(static_cast<std::uint64_t>(width) * height * 4u, "backbuffer capture");
    if (!readback) {
        return false;
    }
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = rgba8_pass_;
    begin.framebuffer = hdr_capture_->framebuffer;
    begin.renderArea = {{0, 0}, {width, height}};
    vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    const VkClearAttachment attachment{VK_IMAGE_ASPECT_COLOR_BIT, 0, clear};
    const VkClearRect rect{{{0, 0}, {width, height}}, 0, 1};
    vkCmdClearAttachments(commands, 1, &attachment, 1, &rect);
    if (!present_black_) {
        // Turned as the swap chain image is (pre-rotated, of the image's size): the 8-bit copy the SDR capture would read.
        const VkRect2D area = turned_rect(swapchain_.transform, static_cast<int>(swapchain_.extent.width),
                                          static_cast<int>(swapchain_.extent.height), content.left, content.top, content.width,
                                          content.height);
        post_.finish_present(commands, step, area, allocate,
                             capture_output(hdr_output_, ::galaxy::diagnosing(::galaxy::kDiagnoseHdrHighlights)),
                             quarter_turns(swapchain_.transform));
    }
    vkCmdEndRenderPass(commands);
    image_barrier(commands, hdr_capture_->image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, 1u);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(commands, hdr_capture_->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->buffer, 1, &region);
    hdr_capture_->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    captures_.recorded({std::move(readback), 0u, backbuffer_capture_path_, width, height, width * 4u, Captures::Form::Rgba8});
    return true;
}

bool RendererVk::present(const DisplayTexture* source, bool capture_backbuffer, bool black) {
    end_layer_2d();  // a frame that took no picture after its 2D layer
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
    if (!capture_backbuffer && unseen_.now(window_, window_shown_)) {
        // No image while the window cannot be seen (a hidden start, minimised, cloaked): the frame is drawn, nothing is
        // presented, as Direct3D's on DXGI_STATUS_OCCLUDED. A frame that takes a test picture still presents.
        surface_lock_ = {};
        return false;
    }
    std::uint32_t image = 0;
    // A second at most: an image the presentation engine never returns (a surface going away) skips the frame.
    constexpr std::uint64_t kAcquireTimeoutNs = 1'000'000'000ull;
    const std::int64_t acquire_cpu = vk::thread_cpu_ns();
    VkResult result = VK_SUCCESS;
    {
        const galaxy::bench::RenderSpan span(galaxy::bench::RenderPart::Acquire);
        galaxy::timeline::record(galaxy::timeline::Event::AcquireBegin);
        result = swapchain_calls_.acquire(context.device(), swapchain_.handle, kAcquireTimeoutNs, frame.acquired, VK_NULL_HANDLE,
                                          &image);
        const bool acquired = result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
        galaxy::timeline::record(galaxy::timeline::Event::AcquireEnd, acquired ? image : 0u, acquired ? 0u : 1u);
    }
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

    const auto aspect = galaxy::latched_display_aspect();
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

    // The post chain's passes before the swap chain's: the picture scaled (and sharpened) to its size on screen. The last
    // one writes the swap chain image, turned or not: on a turned image each pixel takes the picture's turned-back one, the
    // pixel the blit of an upright copy would show there.
    const auto allocate = [this](VkDescriptorSetLayout layout) { return allocate_frame_set(layout); };
    vk::PostProcessorVk::PresentStep step{};
    if (post_.ready() && !present_black_) {
        step = post_.prepare_present(commands, *shown, shown->width, shown->height,
                                     static_cast<std::uint32_t>(content.width), static_cast<std::uint32_t>(content.height),
                                     true, allocate);
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
    // the post chain's last pass, or the blit of the display picture.
    const VkRect2D scissor =
        turned_rect(swapchain_.transform, bb_w, bb_h, content.left, content.top, content.width, content.height);
    const VkViewport viewport{static_cast<float>(scissor.offset.x),
                              static_cast<float>(scissor.offset.y) + static_cast<float>(scissor.extent.height),
                              static_cast<float>(scissor.extent.width), -static_cast<float>(scissor.extent.height),
                              0.0f, 1.0f};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    if (step.pipeline != VK_NULL_HANDLE) {
        post_.finish_present(commands, step, scissor, allocate, swapchain_.hdr ? hdr_output_ : PostOutput{},
                             quarter_turns(swapchain_.transform));
    } else {
        const VkDescriptorSet set = allocate_frame_set(utility_set_layout_);
        const VkDescriptorImageInfo image_info{VK_NULL_HANDLE, shown->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(context.device(), 1, &write, 0, nullptr);
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

    // Screenshots and test pictures stay SDR: an HDR10 image is not one the gates compare, so the SDR picture is drawn again.
    if (capture_backbuffer && swapchain_.hdr && !backbuffer_capture_path_.empty() &&
        capture_sdr_picture(commands, *shown, content, allocate)) {
        hand_to_amd();
        return true;
    }
    if (capture_backbuffer && swapchain_.copyable && !swapchain_.hdr && !backbuffer_capture_path_.empty()) {
        // The finished image, copied out for a test snapshot (PRESENT_SRC -> TRANSFER_SRC and back).
        std::shared_ptr<vk::Buffer> readback =
            capture_buffer(static_cast<std::uint64_t>(swapchain_.image.width) * swapchain_.image.height * 4u, "backbuffer capture");
        if (readback) {
            image_barrier(commands, swapchain_.images[image], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                          VK_ACCESS_2_TRANSFER_READ_BIT, 1u);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {swapchain_.image.width, swapchain_.image.height, 1};
            vkCmdCopyImageToBuffer(commands, swapchain_.images[image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback->buffer, 1,
                                   &region);
            image_barrier(commands, swapchain_.images[image], VK_IMAGE_ASPECT_COLOR_BIT, gpu::Use::CopySource,
                          gpu::Use::Present, gpu::Contents::Keep, 1u);
            captures_.recorded({std::move(readback), 0u, backbuffer_capture_path_, swapchain_.image.width,
                                swapchain_.image.height, swapchain_.image.width * 4u,
                                swapchain_.format == VK_FORMAT_B8G8R8A8_UNORM ? Captures::Form::Bgra8 : Captures::Form::Rgba8});
            hand_to_amd();
            return true;
        }
    }
    hand_to_amd();
    return false;
}

}  // namespace galaxy::render
