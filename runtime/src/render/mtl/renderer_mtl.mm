// SPDX-License-Identifier: GPL-3.0-only
// The Metal renderer (mtl/renderer_mtl.h): device, the window's layer, frames and present. The scene target, draws,
// copies and screen probes are renderer_mtl_scene.mm and renderer_mtl_draw.mm.
#include "renderer_mtl_internal.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/launch_args.h"
#include "galaxy/platform_time.h"
#include "galaxy/render/program_table.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/run_options.h"

#include <TargetConditionals.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace galaxy::render {

using namespace mtl_detail;

namespace {

bool trace_present_stats_enabled() {
    return ::galaxy::diagnosing(::galaxy::kDiagnosePresentStats);
}

// The vendor a device name tells, for the settings' adapter line (Metal names no vendor id).
unsigned vendor_of(const std::string& name) {
    const auto has = [&name](const char* part) { return name.find(part) != std::string::npos; };
    return has("Apple") ? 0x106Bu : has("AMD") || has("Radeon") ? 0x1002u : has("Intel") ? 0x8086u
           : has("NVIDIA") || has("GeForce") ? 0x10DEu : 0u;
}

}  // namespace

void RendererMtl::test_lose_device() {
    std::cerr << "[Renderer] Metal has no device loss to rehearse\n";
}

bool RendererMtl::initialize(int window_width, int window_height, const FrameMap& frame) {
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
        if (!create_device() || !create_layouts() || !configure_layer()) {
            shutdown();
            return false;
        }
        report_adapter();
        if (!create_scene_targets(scene_map_, wanted_msaa_samples())) {
            shutdown();
            return false;
        }
        for (Frame& made : frames_) {
            made.gpu_time = std::make_shared<std::array<std::atomic<double>, 2>>();
            made.pass_time = std::make_shared<std::array<std::atomic<double>, static_cast<std::size_t>(GpuPass::Count) + 1u>>();
        }
        // The rings are Metal buffers with addresses in the renderer's own space; their sizes follow the other
        // renderers'. The vertex and row rings keep the last frame readable for motion.
        gpu::Device* const device = this->device();
        const auto ring = [&](UploadRing& target, const char* name, unsigned segments, bool chunks) {
            return target.initialize(device, name, UploadRing::budget(kFirstRingBytes, segments), segments, kFirstRingBytes, chunks);
        };
        vertex_ring_.set_place(gpu::UploadPlace::GpuLocal);
        constant_ring_.set_place(gpu::UploadPlace::GpuLocal);
        if (!ring(vertex_ring_, "vertex", kRingSegments, true) || !ring(index_ring_, "index", kFramesInFlight, true) ||
            !ring(constant_ring_, "constant", kFramesInFlight, true) || !ring(matrix_ring_, "matrix", kRingSegments, false)) {
            std::cerr << "[Renderer] upload rings unavailable\n";
            shutdown();
            return false;
        }
        gpu::Encoder::Layout encoding{};
        encoding.texture_arrays = bindless_;
        encoder_.configure(encoding);
        encoding_layout_ = encoding;
        reset_sampler_frame();
        // Presentation post-processing, compiled on a worker; the plain blit presents until it is ready.
        // The game's bloom, compiled on a worker; the game draws its own chains until it is ready.
        bloom_.initialize(draw_layout_.depth);
        if (const RenderConfig cfg = get_render_config(); cfg.post_process) {
            post_.initialize(post_process_settings_from(cfg), draw_layout_.scene_color, draw_layout_.depth);
            if (!ao_.initialize(draw_layout_.depth)) {
                std::cerr << "[ambient-occlusion] off on this device" << std::endl;
            }
            if (!scene_camera_.initialize(draw_layout_.depth)) {
                std::cerr << "[scene-camera] off on this device" << std::endl;
            }
        } else {
            PostProcessStatus off{};
            std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "CONFIG OFF");
            set_post_process_status(off);
            std::cerr << "[post-process] off; presenting with the plain blit\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "[Renderer] Metal initialize failed: " << error.what() << '\n';
        init_error_ = error.what();
        shutdown();
        return false;
    }
    std::cout << "[Renderer] Metal scene target=" << scene_map_.width << 'x' << scene_map_.height << " depth=D32FS8\n";
    return true;
}

bool RendererMtl::create_device() {
    mtl::Context& context = mtl::Context::get();
    if (!context.initialize(std::string(get_render_config().gpu.data()))) {
        return false;
    }
    const mtl::DeviceCaps& caps = context.caps();
    std::cout << "[gpu] adapter=\"" << context.device_name() << "\" api=metal family="
              << (caps.apple_gpu ? "apple" : "mac") << caps.apple_family << " unified-memory=" << (caps.unified_memory ? 1 : 0)
              << '\n';
    return true;
}

bool RendererMtl::create_layouts() {
    const mtl::DeviceCaps& caps = mtl::Context::get().caps();
    // Every texture and sampler in one array each where the device reads them so, and holds as many distinct
    // samplers as a scene of the game names; else each draw binds its table of sixteen.
    bindless_ = caps.texture_arrays && caps.sampler_array_max >= 512u;
    if (const char* forced = std::getenv("NEBULA_METAL_BOUND_TABLES"); forced != nullptr && forced[0] == '1') {
        bindless_ = false;
    }
    program_use_bound_tables(!bindless_);
    std::cout << "[metal] draws read their textures from " << (bindless_ ? "one array" : "bound tables") << '\n';
    draw_layout_ = mtl::RenderLayout{};
    draw_layout_.texture_arrays = bindless_;
    draw_layout_.layer_targets = true;  // four colour targets and blended RGBA16F: every Metal device
    draw_layout_.max_samples = std::clamp(caps.max_samples, 1u, 8u);
    @autoreleasepool {
        id<MTLDevice> const metal = metal_device();
        MTLSamplerDescriptor* const sampler = [MTLSamplerDescriptor new];
        sampler.minFilter = MTLSamplerMinMagFilterLinear;
        sampler.magFilter = MTLSamplerMinMagFilterLinear;
        sampler.mipFilter = MTLSamplerMipFilterNotMipmapped;
        sampler.sAddressMode = sampler.tAddressMode = sampler.rAddressMode = MTLSamplerAddressModeClampToEdge;
        linear_clamp_ = own([metal newSamplerStateWithDescriptor:sampler]);
        // The renderer's own draws: no depth use, depth written whatever is there, and a test against the reversed
        // depth (a point is seen where it is as near as the scene there or nearer) that writes nothing.
        MTLDepthStencilDescriptor* const depth = [MTLDepthStencilDescriptor new];
        depth.depthCompareFunction = MTLCompareFunctionAlways;
        depth.depthWriteEnabled = NO;
        depth_uses_[static_cast<std::size_t>(DepthUse::None)] = own([metal newDepthStencilStateWithDescriptor:depth]);
        depth.depthWriteEnabled = YES;
        depth_uses_[static_cast<std::size_t>(DepthUse::Write)] = own([metal newDepthStencilStateWithDescriptor:depth]);
        depth.depthCompareFunction = MTLCompareFunctionGreaterEqual;
        depth.depthWriteEnabled = NO;
        depth_uses_[static_cast<std::size_t>(DepthUse::TestNearer)] = own([metal newDepthStencilStateWithDescriptor:depth]);
        NSError* error = nil;
        id<MTLComputePipelineState> const probe =
            [metal newComputePipelineStateWithFunction:(__bridge id<MTLFunction>)utility_function(mtl::UtilityShader::ScreenProbeCs)
                                                 error:&error];
        if (probe == nil) {
            throw std::runtime_error(std::string("the screen probe pipeline: ") +
                                     (error != nil ? error.localizedDescription.UTF8String : ""));
        }
        probe_pipeline_ = own(probe);
    }
    if (bindless_) {
        sampler_array_slots_ = std::min<std::uint32_t>(kSamplerHeapSlots, caps.sampler_array_max);
        sampler_array_ = mtl::Context::get().create_buffer(std::uint64_t{sampler_array_slots_} * 8u, mtl::Storage::Shared,
                                                          "sampler array");
        if (!sampler_array_) {
            throw std::runtime_error("the sampler array could not be made");
        }
    }
    return true;
}

void RendererMtl::report_adapter() {
    const mtl::Context& context = mtl::Context::get();
    const std::string& name = context.device_name();
    const unsigned vendor = vendor_of(name);
    host_window::rtx_adapter.store(false, std::memory_order_relaxed);
    record_render_adapter(name.c_str(), vendor, 0u, 0u, false);
    RenderFeatureCaps caps{};
    caps.adapter_vendor_id = vendor;
    std::snprintf(caps.adapter_description.data(), caps.adapter_description.size(), "%s", name.c_str());
    const bool msaa = draw_layout_.max_samples > 1u;
    caps.msaa = make_render_feature_status(true, msaa, true, true,
                                           msaa ? "the scene target is multisampled"
                                                : "the GPU has no multisampled scene target formats");
    caps.taa = make_render_feature_status(false, false, false, false, "motion vectors, jitter, and history resources are not built");
    caps.dxr = make_render_feature_status(false, false, false, false, "ray tracing runs on Direct3D 12");
    caps.streamline_bridge_built = false;
    caps.streamline = make_render_feature_status(false, false, false, false, "NVIDIA Streamline is not running");
    caps.reflex = make_render_feature_status(false, false, false, false, "Streamline Reflex integration is not built");
    caps.dlss_super_resolution = make_render_feature_status(false, false, false, false, "DLSS needs Streamline and an NVIDIA RTX GPU");
    caps.dlss_frame_generation = make_render_feature_status(false, false, false, false,
                                                            "DLFG requires Streamline, Reflex, motion vectors, and UI tagging");
    set_render_feature_caps(caps);
}

bool RendererMtl::wants_vsync() const {
    return get_render_config().vsync_interval != 0u;
}

bool RendererMtl::wants_hdr() const {
    return false;
}

void RendererMtl::read_display_hdr() {
    hdr_read_ = true;
    hdr_post_ready_ = post_.ready();
}

// The window's layer for this device at the window's size in pixels: 8-bit drawables whose values are the display's
// own (the programs write display values), shown as sRGB.
bool RendererMtl::configure_layer() {
    CAMetalLayer* const layer = (__bridge CAMetalLayer*)host_window::metal_layer(window_);
    if (layer == nil) {
        std::cerr << "[Renderer] the window has no Metal layer\n";
        return false;
    }
    const std::uint32_t width = std::max(host_window::backbuffer_width, 1u);
    const std::uint32_t height = std::max(host_window::backbuffer_height, 1u);
    const bool vsync = wants_vsync();
    @autoreleasepool {
        layer.device = metal_device();
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.opaque = YES;
        layer.presentsWithTransaction = NO;
        layer.allowsNextDrawableTimeout = YES;
        CGColorSpaceRef const space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        layer.colorspace = space;
        CGColorSpaceRelease(space);
        layer.drawableSize = CGSizeMake(width, height);
#if TARGET_OS_OSX
        layer.displaySyncEnabled = vsync ? YES : NO;
#endif
    }
    if (layer_.width != width || layer_.height != height || layer_.vsync != vsync || layer_.layer == nullptr) {
        std::fprintf(stderr, "[metal] layer %ux%u BGRA8 vsync=%d\n", width, height, vsync ? 1 : 0);
    }
    layer_.layer = (__bridge void*)layer;
    layer_.width = width;
    layer_.height = height;
    layer_.format = mtl::Format::Bgra8;
    layer_.vsync = vsync;
    layer_.hdr_asked = wants_hdr();
    layer_.hdr = false;
    layer_dirty_ = false;
    return true;
}

RendererMtl::Kept RendererMtl::take_apart_lost_device() {
    keep_window_ = true;
    try {
        shutdown();
    } catch (const std::exception& error) {
        std::cerr << "[RendererMtl] the lost device's shutdown: " << error.what() << "\n";
    }
    return Kept{window_};
}

void RendererMtl::shutdown() {
    mtl::Context& context = mtl::Context::get();
    if (context.ready()) {
        if (scene_pass_open_) {
            [pass_of(pass_) endEncoding];
            drop(pass_);
            scene_pass_open_ = false;
        }
        for (mtl::Object& pass : record_encoders_) {
            if (pass != nullptr) {
                [pass_of(pass) endEncoding];
                drop(pass);
            }
        }
        for (RecordRun& run : record_runs_) {
            [(__bridge id<MTLParallelRenderCommandEncoder>)run.parallel endEncoding];
            drop(run.parallel);
        }
        record_runs_.clear();
        wait_for_gpu_idle();
        if (mirrored_store_ != nullptr) {
            mirrored_store_->backend().set_open_pass(nullptr);
            mirrored_store_->backend().set_upload_list(nullptr);
        }
        mirrored_store_ = nullptr;
        temporal_.reset();
        post_.shutdown();
        bloom_.shutdown();
        ao_.shutdown();
        scene_camera_.shutdown();
        motion_ = MotionTargets{};
        scene_ = wide_scene_ = alt_scene_ = target_scene_ = SceneSet{};
        target_slots_.clear();
        for (auto& [key, picture] : pictures_) {
            mtl::release(picture.sampled);
        }
        pictures_.clear();
        frame_pass_targets_.clear();
        capture_target_.reset();
        scene_capture_ = backbuffer_capture_ = display_capture_ = Readback{};
        probe_slots_.clear();
        held_probe_slots_.clear();
        pass_visibility_.reset();
        for (Frame& frame : frames_) {
            if (frame.upload_blit != nullptr) {
                [(__bridge id<MTLBlitCommandEncoder>)frame.upload_blit endEncoding];
                drop(frame.upload_blit);
            }
            drop(frame.uploads);
            for (mtl::Object& list : frame.lists) {
                mtl::release(list);
            }
            frame.lists.clear();
            frame.retired.clear();
            frame.retired_buffers.clear();
            for (mtl::Object& object : frame.retired_objects) {
                mtl::release(object);
            }
            frame.retired_objects.clear();
            frame.tables.clear();
            frame.tables_used = 0u;
            frame.visibility.clear();
            frame.visibility_used = 0u;
            frame.fence = 0u;
        }
        commands_ = nullptr;
        drop(drawable_);
        for (auto& [key, pipeline] : utility_pipelines_) {
            mtl::release(pipeline);
        }
        utility_pipelines_.clear();
        for (mtl::Object& function : utility_functions_) {
            drop(function);
        }
        drop(probe_pipeline_);
        for (mtl::Object& state : depth_uses_) {
            drop(state);
        }
        drop(linear_clamp_);
        for (auto& [key, sampler] : samplers_) {
            mtl::release(sampler);
        }
        samplers_.clear();
        sampler_tables_.clear();
        default_sampler_table_ = gpu::Table{};
        sampler_array_.reset();
        sampler_slots_.reset();
        sampler_slots_ready_ = false;
        persistent_tables_.clear();
        retired_tables_.clear();
        free_tables_.clear();
        for (TextureListBuffer& list : texture_lists_) {
            list = TextureListBuffer{};
        }
        zero_vertices_.reset();
        zero_stream_bytes_ = 0u;
        zero_stream_address_ = 0u;
        {
            std::vector<gpu::BufferRef> old;
            for (UploadRing* ring : {&vertex_ring_, &index_ring_, &constant_ring_, &matrix_ring_}) {
                ring->release_into(old);
            }
        }
        if (layer_.layer != nullptr) {
            ((__bridge CAMetalLayer*)layer_.layer).device = nil;
        }
        layer_ = Layer{};
        context.collect();
    }
    surface_lock_ = {};
    context.shutdown();
    present_pacer_.reset();
    if (!keep_window_ && window_owner_ != nullptr) {
        host_window::close_game_window(*window_owner_);
        window_ = nullptr;
        window_shown_ = false;
    }
}

void RendererMtl::grow_rings() {
    // A ring whose frames took over half their budget (or spilled) grows; one whose last 600 frames stayed under a
    // quarter of it gives half of its memory back.
    if (ring_growth_failed_ ||
        (!vertex_ring_.wants_growth() && !index_ring_.wants_growth() && !constant_ring_.wants_growth() &&
         !matrix_ring_.wants_growth() && !vertex_ring_.wants_shrink() && !index_ring_.wants_shrink() &&
         !constant_ring_.wants_shrink() && !matrix_ring_.wants_shrink())) {
        return;
    }
    // No wait for the GPU: a ring is made again as a new buffer. The old buffer and its chunks are released here,
    // which hands them to the context to release once the work committed so far has completed.
    gpu::Device* const device = this->device();
    std::vector<gpu::BufferRef> old;
    const auto place = [&](UploadRing& ring, const char* name, unsigned segments, bool chunks) {
        const std::size_t had = ring.capacity();
        std::size_t capacity = ring.grown_capacity(segments);
        if (capacity == 0u) {
            ring_growth_failed_ = true;
            capacity = had;
        } else if (ring.wants_shrink()) {
            capacity = ring.shrunk_capacity(segments);
        }
        if (capacity != had) {
            std::cerr << "[gpu-rings] " << (capacity > had ? "grow " : "shrink ") << name << " frame-high-water-kb="
                      << (ring.high_water() >> 10u) << " mb=" << (had >> 20u) << "->" << (capacity >> 20u) << '\n';
        }
        ring.release_into(old);
        if (ring.initialize(device, name, UploadRing::budget(capacity, segments), segments, capacity, chunks)) {
            return true;
        }
        ring_growth_failed_ = true;
        std::cerr << "[gpu-rings] grow " << name << " failed\n";
        return ring.initialize(device, name, UploadRing::budget(had, segments), segments, had, chunks);
    };
    if (!place(vertex_ring_, "vertex", kRingSegments, true) || !place(index_ring_, "index", kFramesInFlight, true) ||
        !place(constant_ring_, "constant", kFramesInFlight, true) || !place(matrix_ring_, "matrix", kRingSegments, false)) {
        throw std::runtime_error("upload ring resize failed");
    }
    history_segment_ = 0u;
    history_segment_fence_.fill(0u);
    history_used_ = true;
}

void RendererMtl::trace_rings() const {
    const auto line = [](const char* name, const UploadRing& ring) {
        std::cerr << "[memory] ring " << name << ' ' << static_cast<double>(ring.capacity()) / (1024.0 * 1024.0)
                  << " MB, a frame took at most " << static_cast<double>(ring.high_water()) / (1024.0 * 1024.0) << " MB\n";
    };
    line("vertex", vertex_ring_);
    line("index", index_ring_);
    line("constant", constant_ring_);
    line("matrix", matrix_ring_);
}

void RendererMtl::wait_for_gpu_idle() {
    mtl::Context& context = mtl::Context::get();
    if (!context.ready()) {
        return;
    }
    const std::uint64_t committed = context.submitted_value();
    if (committed != 0u) {
        (void)context.wait(committed, 10'000'000'000ull);
    }
    context.collect();
}

bool RendererMtl::quit_requested() const noexcept {
    return host_window::quit_request.load(std::memory_order_acquire);
}

UploadBuffer RendererMtl::create_upload_buffer(std::size_t bytes, const char* name) {
    return gpu::create_upload_memory(device(), bytes, name);
}

std::uint64_t RendererMtl::completed_fence_value() const {
    return mtl::Context::get().completed_value();
}

void RendererMtl::wait_for_fence_value(std::uint64_t fence_value) {
    if (fence_value == 0u) {
        throw std::runtime_error("invalid frame-specific GPU fence wait");
    }
    if (!mtl::Context::get().wait(fence_value, 10'000'000'000ull)) {
        throw std::runtime_error("frame-specific GPU completion wait timed out or the device was lost");
    }
}

namespace mtl {
void pipeline_compile_stats(std::uint64_t out[5]) noexcept;  // pipelines_mtl.mm
}

namespace {
constexpr const char* kPassTimeNames[] = {"scene", "resolve", "frame read", "display copy", "bloom", "scene aa", "scene ao",
                                          "upscale", "ray tracing", "present", "camera", "ao distance", "ao occlusion",
                                          "ao denoise", "ao upsample", "ao apply", "uploads"};
}  // namespace

void RendererMtl::next_command_buffer(GpuPass pass) {
    @autoreleasepool {
        id<MTLCommandBuffer> const made = [queue() commandBuffer];
        if (made == nil) {
            throw std::runtime_error("no command buffer");
        }
        list_pass_ = pass == GpuPass::Count ? GpuPass::Scene : pass;
        if (profile_frame_) {
            made.label = @(kPassTimeNames[static_cast<std::size_t>(list_pass_)]);
            const auto times = frame_->pass_time;
            const std::size_t at = static_cast<std::size_t>(list_pass_);
            [made addCompletedHandler:^(id<MTLCommandBuffer> done) {
                if (done.GPUEndTime > done.GPUStartTime && done.GPUStartTime > 0.0) {
                    (*times)[at].fetch_add(done.GPUEndTime - done.GPUStartTime, std::memory_order_relaxed);
                }
            }];
        } else {
            made.label = @"frame";
        }
        frame_->lists.push_back(own(made));
        commands_ = frame_->lists.back();
    }
}

void RendererMtl::begin_frame() {
    host_window::poll_render_config_file();
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
    bloom_.trim_idle();
    ao_.trim_idle();
    frame_slot_ = static_cast<unsigned>(frame_index_ % kFramesInFlight);
    Frame& frame = frames_[frame_slot_];
    frame_ = &frame;
    mtl::Context& context = mtl::Context::get();
    if (frame.fence != 0u) {
        wait_for_fence_value(frame.fence);
        // The GPU's time on the slot's last frame: from its first command buffer's start to its last one's end.
        const double start = (*frame.gpu_time)[0].exchange(0.0, std::memory_order_acq_rel);
        const double end = (*frame.gpu_time)[1].exchange(0.0, std::memory_order_acq_rel);
        if (start > 0.0 && end >= start) {
            const double gpu_ms = (end - start) * 1000.0;
            if (frame_scene_drawn_[frame_slot_]) {
                native_scene_gpu_ema_ms_ = native_scene_gpu_ema_ms_ == 0.0 ? gpu_ms
                                           : native_scene_gpu_ema_ms_ + (gpu_ms - native_scene_gpu_ema_ms_) * 0.1;
            }
            if (frame_generated_[frame_slot_]) {
                generated_gpu_ms_ += gpu_ms;
                ++generated_gpu_samples_;
                generated_gpu_ema_ms_ = generated_gpu_ema_ms_ == 0.0 ? gpu_ms : generated_gpu_ema_ms_ * 0.9 + gpu_ms * 0.1;
            }
            if (frame.pass_timed) {
                // The slot's last frame by pass; the page's running average of each over the frames that ran it.
                for (std::size_t pass = 0u; pass < frame.pass_time->size(); ++pass) {
                    const double ms = (*frame.pass_time)[pass].exchange(0.0, std::memory_order_acq_rel) * 1000.0;
                    pass_window_ms_[pass] += ms;
                    if (ms > 0.0 && pass < pass_ema_ms_.size()) {
                        const float before = pass_ema_ms_[pass].load(std::memory_order_relaxed);
                        const float value = static_cast<float>(ms);
                        pass_ema_ms_[pass].store(before == 0.0f ? value : before + (value - before) * 0.1f,
                                                 std::memory_order_relaxed);
                    }
                }
                ++pass_window_frames_;
            }
            stats_.gpu_ms += gpu_ms;
            ++stats_.gpu_samples;
            gpu_window_.ms += gpu_ms;
            gpu_window_.max_ms = std::max(gpu_window_.max_ms, gpu_ms);
            ++gpu_window_.frames;
        }
    }
    {
        std::string what;
        if (context.take_failure(what)) {
            throw std::runtime_error("a command buffer failed: " + what);
        }
    }
    context.collect();
    frame.retired.clear();
    frame.retired_buffers.clear();
    for (mtl::Object& object : frame.retired_objects) {
        mtl::release(object);
    }
    frame.retired_objects.clear();
    frame.tables_used = 0u;
    // The slot's probe results are read (their fence passed with the slot's): zero for the passes of this frame.
    for (std::size_t i = 0; i < frame.visibility_used; ++i) {
        std::memset(frame.visibility[i]->mapped, 0, frame.visibility[i]->size);
    }
    frame.visibility_used = 0u;
    pass_visibility_.reset();
    pass_visibility_used_ = 0u;
    trim_idle_effect_targets();
    if (frame_index_ % 120u == 0u) {
        sweep_target_slots();
    }
    reset_sampler_frame();
    // Passes are timed only when asked for; with neither asked nothing is split or read.
    const bool timed = ::galaxy::diagnosing(::galaxy::kDiagnoseGpuPasses) || gpu_timing_wanted_.load(std::memory_order_relaxed);
    if (!timed && profile_frame_) {
        for (std::atomic<float>& average : pass_ema_ms_) {
            average.store(0.0f, std::memory_order_relaxed);
        }
    }
    profile_frame_ = timed;
    frame.pass_timed = timed;
    @autoreleasepool {
        id<MTLCommandBuffer> const uploads = [queue() commandBuffer];
        uploads.label = @"texture uploads";
        if (timed) {
            const auto times = frame.pass_time;
            [uploads addCompletedHandler:^(id<MTLCommandBuffer> done) {
                if (done.GPUEndTime > done.GPUStartTime && done.GPUStartTime > 0.0) {
                    times->back().fetch_add(done.GPUEndTime - done.GPUStartTime, std::memory_order_relaxed);
                }
            }];
        }
        frame.uploads = own(uploads);
        frame.upload_blit = own([uploads blitCommandEncoder]);
    }
    current_pass_ = GpuPass::Scene;
    next_command_buffer(GpuPass::Scene);
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
    texture_lists_used_ = 0u;
    encoder_.attach(nullptr, 0u, 0u);
    reset_draw_bindings();
    scene_pass_open_ = false;
    scene_drawn_ = false;
    record_passes_ = 0u;
    if (mirrored_store_ != nullptr) {
        mirrored_store_->backend().set_upload_list(frame.upload_blit);
    }
}

std::uint64_t RendererMtl::end_frame(bool present_swap_chain, SubmissionCallback on_submitted, void* submission_context,
                                     bool generated) {
    Frame& frame = frames_[frame_slot_];
    end_scene_pass();
    mtl::Context& context = mtl::Context::get();
    if (mirrored_store_ != nullptr) {
        mirrored_store_->backend().set_upload_list(nullptr);
    }
    const bool presenting = present_swap_chain && drawable_ != nullptr;
    double pace_wait_ms = 0.0;
    std::int64_t present_start = 0;
    std::uint64_t fence = 0u;
    @autoreleasepool {
        // The uploads first: TextureStore never writes a texture after a draw of the same frame read it.
        [(__bridge id<MTLBlitCommandEncoder>)frame.upload_blit endEncoding];
        drop(frame.upload_blit);
        (void)context.commit(frame.uploads);
        drop(frame.uploads);
        const std::shared_ptr<std::array<std::atomic<double>, 2>> times = frame.gpu_time;
        (*times)[0].store(0.0, std::memory_order_release);
        (*times)[1].store(0.0, std::memory_order_release);
        [commands_of(frame.lists.front()) addCompletedHandler:^(id<MTLCommandBuffer> done) {
            (*times)[0].store(done.GPUStartTime, std::memory_order_release);
        }];
        [commands_of(frame.lists.back()) addCompletedHandler:^(id<MTLCommandBuffer> done) {
            (*times)[1].store(done.GPUEndTime, std::memory_order_release);
        }];
        if (presenting) {
            // On a variable refresh display the pacer holds presents under the cap whatever the VSync setting; a
            // frame under the cap goes out as soon as it is ready.
            const RenderConfig cfg = get_render_config();
            const float variable_rate = variable_refresh_ ? static_cast<float>(display_refresh_hz_) : 0.0f;
            const float paced_fps = variable_refresh_
                ? (presented_max_fps(cfg) > 0.0f ? std::min(presented_max_fps(cfg), variable_rate) : variable_rate)
                : presented_max_fps(cfg);
            pace_wait_ms = present_pacer_.pace(variable_refresh_ ? 0u : cfg.vsync_interval, paced_fps);
            present_start = galaxy::qpc_now();
            if (trace_present_stats_enabled()) {
                const std::shared_ptr<Shown> shown = shown_;
                const double handed = CACurrentMediaTime();
                const double refresh = display_refresh_hz_ > 1.0 ? 1.0 / static_cast<double>(display_refresh_hz_) : 1.0 / 60.0;
                [(__bridge id<CAMetalDrawable>)drawable_ addPresentedHandler:^(id<MTLDrawable> drawable) {
                    const double at = drawable.presentedTime;
                    const std::lock_guard<std::mutex> lock(shown->mutex);
                    if (at <= 0.0) {
                        ++shown->never;
                        return;
                    }
                    if (shown->last > 0.0) {
                        const long count = std::lround((at - shown->last) / refresh);
                        ++shown->refreshes[static_cast<std::size_t>(std::clamp(count, 0l, 4l))];
                    }
                    shown->last = at;
                    shown->latency += at - handed;
                    shown->latency_max = std::max(shown->latency_max, at - handed);
                    ++shown->count;
                }];
            }
            // Shown as soon as the frame's commands are scheduled: never at a time asked for.
            [commands_of(frame.lists.back()) presentDrawable:(__bridge id<CAMetalDrawable>)drawable_];
        }
        for (std::size_t i = 0; i + 1u < frame.lists.size(); ++i) {
            (void)context.commit(frame.lists[i]);
        }
        fence = context.commit_frame(frame.lists.back());
        for (mtl::Object& list : frame.lists) {
            mtl::release(list);
        }
        frame.lists.clear();
        commands_ = nullptr;
        drop(drawable_);
    }
    surface_lock_ = {};
    frame.fence = fence;
    last_submitted_ = fence;
    history_used_ = vertex_ring_.cursor() != history_begin_vertex_ || matrix_ring_.cursor() != history_begin_matrix_;
    history_segment_fence_[history_segment_] = fence;
    frame_generated_[frame_slot_] = generated;
    frame_scene_drawn_[frame_slot_] = scene_drawn_;
    for (ProbeSlot& probes : probe_slots_) {
        if (probes.fence == kProbesInFrame) {
            probes.fence = fence;
        }
    }
    if (scene_capture_.buffer && scene_capture_.fence == 0u && !scene_capture_pending_path_.empty()) {
        scene_capture_.fence = fence;
    }
    if (backbuffer_capture_.buffer && backbuffer_capture_.fence == 0u && !backbuffer_capture_pending_path_.empty()) {
        backbuffer_capture_.fence = fence;
    }
    if (display_capture_.buffer && display_capture_.fence == 0u && !display_capture_pending_path_.empty()) {
        display_capture_.fence = fence;
    }
    if (on_submitted != nullptr) {
        on_submitted(submission_context, fence);
    }
    if (presenting) {
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
        if (!window_shown_ && window_ != nullptr && !host_window::hidden_window_mode_enabled()) {
            window_shown_ = true;
            host_window::show_window(window_);
        }
    }
    ++frame_index_;
    if (frame_index_ % 600u == 0u) {
        if (gpu_window_.frames != 0u) {
            const double frames = static_cast<double>(gpu_window_.frames);
            std::fprintf(stderr,
                         "[metal-gpu] frame %.2f ms average, %.2f ms longest; %.1f scene target passes and %.1f scene target "
                         "copies a frame; scene target %ux%u, layer %ux%u\n",
                         gpu_window_.ms / frames, gpu_window_.max_ms, static_cast<double>(gpu_window_.passes) / 600.0,
                         static_cast<double>(gpu_window_.copies) / 600.0, scene_map_.width, scene_map_.height, layer_.width,
                         layer_.height);
            std::fprintf(stderr, "[metal-gpu] scene target opened a frame (closed by > opened by):");
            for (const GpuWindow::Open& open : gpu_window_.opens) {
                if (open.count != 0u) {
                    std::fprintf(stderr, " %s > %s%s %.2f;", open.closed, open.opened, open.cleared ? " (clear)" : "",
                                 static_cast<double>(open.count) / 600.0);
                }
            }
            std::fprintf(stderr, "\n");
        }
        if (pass_window_frames_ != 0u) {
            std::fprintf(stderr, "[metal-gpu] passes, ms a frame:");
            for (std::size_t pass = 0u; pass < pass_window_ms_.size(); ++pass) {
                if (pass_window_ms_[pass] > 0.0) {
                    std::fprintf(stderr, " %s=%.3f", kPassTimeNames[pass], pass_window_ms_[pass] / pass_window_frames_);
                }
            }
            std::fprintf(stderr, "\n");
        }
        {
            std::uint64_t compiled[5]{};
            mtl::pipeline_compile_stats(compiled);
            std::fprintf(stderr, "[metal-gpu] programs made %llu (%.1f MB) in %.1f s of threads' time; pipelines %llu in %.1f s\n",
                         static_cast<unsigned long long>(compiled[0]), static_cast<double>(compiled[4]) / 1e6,
                         static_cast<double>(compiled[1]) / 1e6, static_cast<unsigned long long>(compiled[2]),
                         static_cast<double>(compiled[3]) / 1e6);
        }
        pass_window_ms_.fill(0.0);
        pass_window_frames_ = 0u;
        gpu_window_ = GpuWindow{};
    }
    return fence;
}

void RendererMtl::report_present_stats() {
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
            << static_cast<double>(progress.new_pictures_shown - new_pictures_start) / seconds << " pictures-made-hz="
            << static_cast<double>(copies - pictures_made_start) / seconds << " frame-ms(avg/max)="
            << (stats_.intervals != 0u ? stats_.interval_ms / stats_.intervals : 0.0) << '/' << stats_.interval_max_ms
            << " pace-wait-ms(avg/max)=" << stats_.pace_wait_ms / presents << '/' << stats_.pace_wait_max_ms
            << " present-ms(avg/max)=" << stats_.present_ms / presents << '/' << stats_.present_max_ms << " vsync="
            << get_render_config().vsync_interval << " gpu-ms(avg)=";
    if (stats_.gpu_samples != 0u) {
        message << stats_.gpu_ms / static_cast<double>(stats_.gpu_samples);
    } else {
        message << "unavailable";
    }
    message << " gpu-samples=" << stats_.gpu_samples << " gpu-total-ms=" << stats_.gpu_ms;
    {
        // On the screen: how many refreshes each picture stayed up (all "1" is an even 60 pictures a second on a 60 Hz
        // display; a "2" beside a "0" or a short run is a stutter the eye sees), and hand-over to screen.
        const std::lock_guard<std::mutex> lock(shown_->mutex);
        message << " shown-for-refreshes(0/1/2/3/4+)=" << shown_->refreshes[0] << '/' << shown_->refreshes[1] << '/'
                << shown_->refreshes[2] << '/' << shown_->refreshes[3] << '/' << shown_->refreshes[4]
                << " never-shown=" << shown_->never << " to-screen-ms(avg/max)="
                << (shown_->count != 0u ? shown_->latency * 1000.0 / static_cast<double>(shown_->count) : 0.0) << '/'
                << shown_->latency_max * 1000.0;
        shown_->refreshes = {};
        shown_->never = 0u;
        shown_->latency = shown_->latency_max = 0.0;
        shown_->count = 0u;
    }
    message << " api=metal\n";
    std::cerr << message.str();
    new_pictures_start = progress.new_pictures_shown;
    pictures_made_start = copies;
    stats_ = PresentStats{};
    stats_.window_start_qpc = now;
    stats_.last_present_qpc = now;
}

void RendererMtl::gpu_pass(GpuPass pass) {
    current_pass_ = pass;
    // Timed: what is recorded from here is in a command buffer of its own. An encoder still open on the one before (the
    // scene pass a part drew its result into) stays there and counts as that one's; begin_scene_pass_with begins a
    // buffer for the scene pass itself, so the game's draws count as the scene's.
    if (profile_frame_ && frame_ != nullptr && commands_ != nullptr && pass != GpuPass::Count && pass != list_pass_ &&
        !scene_pass_open_) {
        next_command_buffer(pass);
    }
}

std::size_t RendererMtl::gpu_pass_times(GpuPassTime* out, std::size_t capacity) const noexcept {
    std::size_t count = 0u;
    for (std::size_t pass = 0u; pass < pass_ema_ms_.size() && count < capacity; ++pass) {
        if (const float ms = pass_ema_ms_[pass].load(std::memory_order_relaxed); ms > 0.0f) {
            out[count++] = {kPassTimeNames[pass], ms};
        }
    }
    return count;
}

void RendererMtl::sync_post_process() {
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

bool RendererMtl::vblank_grid(std::int64_t& vblank, std::int64_t& period) const noexcept {
    return window_ != nullptr && host_window::vblank_grid(window_, vblank, period);
}

void RendererMtl::refresh_display_rate() {
    const double mode_hz = host_window::display_refresh_hz(window_);
    if (mode_hz <= 1.0) {
        return;
    }
    // The vblanks' own rate (where the platform times them): a display that varies its refresh with the presents
    // shows a rate well under the mode's.
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
    if (std::abs(hz - display_refresh_hz_) >= 0.1) {
        if (!variable) {
            std::cerr << "[display] refresh-hz=" << hz << '\n';
        }
        display_refresh_hz_ = hz;
    }
}

// The layer follows the window's size and the VSync setting, between frames.
void RendererMtl::follow_window_and_resize() {
    if (host_window::surface_withdrawn.load(std::memory_order_acquire)) {
        return;
    }
    const std::uint64_t extent = host_window::pending_resize_extent.exchange(0u, std::memory_order_acq_rel);
    if (extent != 0u) {
        const auto width = static_cast<std::uint32_t>(extent >> 32u);
        const auto height = static_cast<std::uint32_t>(extent & 0xffffffffu);
        if (width != layer_.width || height != layer_.height) {
            layer_dirty_ = true;
        }
        // The window's size in pixels, as the layer is made for it.
        if (width != 0u && height != 0u) {
            host_window::backbuffer_width = width;
            host_window::backbuffer_height = height;
        }
    }
    if (host_window::backbuffer_width != layer_.width || host_window::backbuffer_height != layer_.height) {
        layer_dirty_ = true;
    }
    if (wants_vsync() != layer_.vsync || wants_hdr() != layer_.hdr_asked) {
        layer_dirty_ = true;
    }
    if (!hdr_read_ || post_.ready() != hdr_post_ready_) {
        read_display_hdr();
    }
    if (layer_dirty_) {
        // A window with no area (minimised) is not followed until it has one again.
        if (host_window::backbuffer_width == 0u || host_window::backbuffer_height == 0u) {
            return;
        }
        (void)configure_layer();
    }
    if (display_refresh_checks_++ % 120u == 0u) {
        refresh_display_rate();
    }
}

void RendererMtl::log_video_memory(const VideoMemoryOutside& outside) {
    const auto mb = [](std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    (void)video_memory(usage, budget);
    const auto set_bytes = [](const SceneSet& set) {
        return allocation_bytes(set.color.get()) + allocation_bytes(set.depth.get()) + allocation_bytes(set.ms_color.get()) +
               allocation_bytes(set.ms_depth.get());
    };
    std::uint64_t scene = set_bytes(scene_) + set_bytes(alt_scene_) + set_bytes(target_scene_) +
                          allocation_bytes(wide_scene_.color.get()) + allocation_bytes(wide_scene_.ms_color.get());
    for (const gpu::TextureRef* texture : {&motion_.motion, &motion_.reactive, &motion_.layer_color, &motion_.layer_trans,
                                           &capture_target_}) {
        scene += allocation_bytes(texture->get());
    }
    std::uint64_t slots = 0u;
    for (const auto& entry : target_slots_) {
        slots += set_bytes(entry.second.set);
    }
    std::uint64_t frame_pass = 0u;
    for (const auto& entry : frame_pass_targets_) {
        frame_pass += allocation_bytes(entry.second.texture.get());
    }
    std::uint64_t retired_textures = 0u;
    std::uint64_t retired_buffers = 0u;
    for (const Frame& frame : frames_) {
        for (const gpu::TextureRef& texture : frame.retired) {
            retired_textures += allocation_bytes(texture.get());
        }
        for (const gpu::BufferRef& buffer : frame.retired_buffers) {
            retired_buffers += buffer ? buffer->size : 0u;
        }
    }
    std::uint64_t rings = 0u;
    std::uint64_t chunks = 0u;
    for (const UploadRing* ring : {&vertex_ring_, &index_ring_, &constant_ring_, &matrix_ring_}) {
        rings += ring->capacity();
        chunks += ring->live_chunk_bytes();
    }
    TextureBackend::HeapStats heaps{};
    if (mirrored_store_ != nullptr) {
        heaps = mirrored_store_->backend().heap_stats();
    }
    std::fprintf(stderr,
                 "[vram] breakdown allocated=%.0f budget=%.0f MB | texture heaps=%llu %.0f used=%.1f textures=%llu | "
                 "scene-targets=%.1f target-slots=%zu %.1f frame-pass=%zu %.1f pictures=%u %.1f retired=%.1f | "
                 "rings=%.0f chunks=%.0f | staging=%.0f mesh-pages=%u (empty %u) %.0f live %.1f slots=%u %.1f "
                 "retired-buffers=%.1f | pipelines=%llu\n",
                 mb(usage), mb(budget), static_cast<unsigned long long>(heaps.heaps), mb(heaps.heap_bytes), mb(heaps.used_bytes),
                 static_cast<unsigned long long>(heaps.placed), mb(scene), target_slots_.size(), mb(slots),
                 frame_pass_targets_.size(), mb(frame_pass), outside.picture_count, mb(outside.picture_bytes),
                 mb(retired_textures), mb(rings), mb(chunks), mb(outside.staging_bytes), outside.mesh_pages,
                 outside.mesh_empty_pages, mb(outside.mesh_page_bytes), mb(outside.mesh_live_bytes), outside.slots,
                 mb(outside.slot_bytes), mb(retired_buffers), static_cast<unsigned long long>(outside.pipelines));
}

// The picture as the window shows it: black, then the picture in its content rectangle.
void RendererMtl::draw_presented_picture(mtl::Object target, mtl::Format format, const mtl::Texture& picture,
                                         const galaxy::ContentViewport& content, bool) {
    const gpu::Rect area = gpu::rect(content.left, content.top, content.left + content.width, content.top + content.height);
    // The post chain's passes before the target's own: the picture scaled (and sharpened) to its size on screen.
    mtl::PostProcessor::PresentStep step{};
    if (post_.ready() && !present_black_) {
        step = post_.prepare_present(commands_, picture, picture.width, picture.height, static_cast<unsigned>(content.width),
                                     static_cast<unsigned>(content.height), format);
    }
    const mtl::Object pass = begin_color_pass(target, true, "present");
    if (step.pipeline != nullptr) {
        post_.finish_present(pass, step, area, layer_.hdr ? hdr_output_ : PostOutput{});
    } else if (!present_black_) {
        UtilityKey key;
        key.vertex = mtl::UtilityShader::PresentVs;
        key.fragment = mtl::UtilityShader::BlitPs;
        key.colors[0] = format;
        key.color_count = 1u;
        key.write_mask = static_cast<std::uint8_t>(kWriteAll);
        // The picture is not turned: the layer's drawables have the window's orientation.
        const std::uint32_t transform[4] = {1u, 0u, 0u, 0u};
        utility_draw(pass, utility_pipeline(key), DepthUse::None, layer_.width, layer_.height, area, transform, sizeof(transform),
                     picture.texture, &area);
    }
    end_own_pass(pass);
}

bool RendererMtl::present(const DisplayTexture* source, bool capture_backbuffer, bool black) {
    const GpuPassScope scope{*this, GpuPass::Present};
    present_black_ = black;
    if (source == nullptr || !source->texture) {
        throw std::runtime_error("present requested without a valid display picture blit source");
    }
    debug_log_backbuffer_readback();
    end_scene_pass();
    surface_lock_ = std::unique_lock<std::timed_mutex>(host_window::surface_use);
    if (layer_dirty_ || layer_.layer == nullptr || host_window::surface_withdrawn.load(std::memory_order_acquire)) {
        surface_lock_ = {};
        return false;  // followed at the next frame boundary
    }
    const mtl::Texture& shown = *source->texture;
    if (capture_backbuffer && !present_black_) {
        // A test snapshot also keeps the picture itself, before the post chain scales it.
        static_cast<void>(debug_copy_selected_display_to_readback(*source));
    }
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const auto content_aspect = galaxy::presentation_content_aspect(
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect, source->prerecorded_movie);
    const int bb_w = static_cast<int>(layer_.width);
    const int bb_h = static_cast<int>(layer_.height);
    galaxy::ContentViewport content = galaxy::fit_content_viewport(bb_w, bb_h, content_aspect);
    if (content.width <= 0 || content.height <= 0) {
        content = galaxy::ContentViewport{0, 0, bb_w, bb_h};
    }
    (void)galaxy::g_presentation_content.publish(bb_w, bb_h, content);
    @autoreleasepool {
        // The drawable is taken as late as the frame allows: it is one of a few, and held until it is shown.
        id<CAMetalDrawable> const drawable = [(__bridge CAMetalLayer*)layer_.layer nextDrawable];
        if (drawable == nil) {
            surface_lock_ = {};
            return false;
        }
        drawable_ = own(drawable);
        draw_presented_picture((__bridge void*)drawable.texture, layer_.format, shown, content, true);
    }
    host_window::native_settings_present();
    reset_draw_bindings();
    // Screenshots and test pictures: the same picture drawn again into a texture that can be read (a drawable is not).
    if (capture_backbuffer && !backbuffer_capture_path_.empty() && backbuffer_capture_pending_path_.empty()) {
        const std::uint32_t width = layer_.width;
        const std::uint32_t height = layer_.height;
        capture_used_frame_ = frame_index_;
        if (!capture_target_ || capture_target_->width != width || capture_target_->height != height) {
            if (capture_target_) {
                retire(std::move(capture_target_));
            }
            capture_target_ = mtl::Context::get().create_texture(width, height, 1u, mtl::Format::Rgba8,
                                                                 mtl::kUseTarget | mtl::kUseSample, mtl::Storage::Private,
                                                                 "presented picture capture");
        }
        const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * 4u;
        if (!backbuffer_capture_.buffer || backbuffer_capture_.buffer->size < bytes) {
            backbuffer_capture_.buffer = mtl::Context::get().create_buffer(bytes, mtl::Storage::Shared, "backbuffer capture");
        }
        if (capture_target_ && backbuffer_capture_.buffer) {
            draw_presented_picture(capture_target_->texture, mtl::Format::Rgba8, shown, content, true);
            @autoreleasepool {
                id<MTLBlitCommandEncoder> const blit = [commands_of(commands_) blitCommandEncoder];
                blit.label = @"capture";
                [blit copyFromTexture:texture_of(capture_target_)
                                 sourceSlice:0
                                 sourceLevel:0
                                sourceOrigin:MTLOriginMake(0, 0, 0)
                                  sourceSize:MTLSizeMake(width, height, 1)
                                    toBuffer:buffer_of(backbuffer_capture_.buffer)
                           destinationOffset:0
                      destinationBytesPerRow:NSUInteger{width} * 4u
                    destinationBytesPerImage:NSUInteger{width} * height * 4u];
                [blit endEncoding];
            }
            backbuffer_capture_.fence = 0u;
            backbuffer_capture_pending_path_ = backbuffer_capture_path_;
            backbuffer_capture_width_ = width;
            backbuffer_capture_height_ = height;
            return true;
        }
    }
    return false;
}

}  // namespace galaxy::render
