// SPDX-License-Identifier: GPL-3.0-only
// The Direct3D 11 renderer (d3d11/renderer_d3d11.h): device, swap chain, frames, present, the draws' replay, samplers and
// tables. The scene target, its passes, copies, pictures, probes and the renderer's parts are renderer_d3d11_scene.cpp.
#include "galaxy/render/d3d11/renderer_d3d11.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/launch_args.h"
#include "galaxy/platform_time.h"
#include "galaxy/render/host_window_win32.h"
#include "galaxy/render/program_table.h"
#include "galaxy/render/texture_store.h"

#include "sm50/utility_shaders_dxbc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace galaxy::render {

namespace {

bool trace_present_stats_enabled() {
    return ::galaxy::diagnosing(::galaxy::kDiagnosePresentStats);
}

// The default sampler of draws without one of their own: wrap, linear, mip point, every level (renderer_mtl_draw.mm).
constexpr SamplerKey kDefaultSamplerKey{1u | (1u << 2u) | (1u << 4u) | (5u << 5u), 0.0f, 32.0f, 0.0f};

// A constant buffer's window at `offset` (16-byte constants, 256-byte steps): as much of the buffer as a shader can see.
UINT constants_in(std::uint64_t size, std::uint64_t offset) noexcept {
    const std::uint64_t left = size > offset ? (size - offset) / 16u : 0u;
    return static_cast<UINT>(std::min<std::uint64_t>(D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT, left & ~std::uint64_t{15}));
}

// RendererD3D11::GpuPass by name: the settings page's, and present-stats' gpu-pass-ms (Direct3D 12's names).
constexpr const char* kPassTimeNames[] = {"scene", "resolve", "frame read", "display copy", "bloom", "scene aa", "scene ao",
                                          "present", "camera"};
constexpr const char* kPassStatNames[] = {"scene", "resolve", "frame-read", "display-copy", "bloom", "scene-aa", "scene-ao",
                                          "present", "camera"};

}  // namespace

// FSR 3 frame generation is not built on Direct3D 11 (its interface is Direct3D 12's and Vulkan's).
bool fsr_frame_generation_compiled() noexcept { return false; }

void RendererD3D11::lose_device() {
    std::cerr << "[Renderer] Direct3D 11 has no device loss to rehearse\n";
}

bool RendererD3D11::initialize(int window_width, int window_height, const FrameMap& frame) {
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
        if (!create_device() || !create_states() || !create_swap_chain()) {
            shutdown();
            return false;
        }
        report_adapter();
        if (!create_scene_targets(scene_map_, wanted_msaa_samples())) {
            shutdown();
            return false;
        }
        // The rings are upload memory with addresses in the renderer's own space; their sizes follow the other renderers'.
        // No ring keeps a frame before the last one readable: nothing on Direct3D 11 reads last frame's vertices (motion).
        gpu::Device* const device = this->device();
        const auto ring = [&](UploadRing& target, const char* name, bool chunks) {
            return target.initialize(device, name, UploadRing::budget(kFirstRingBytes, kFramesInFlight), kFramesInFlight,
                                     kFirstRingBytes, chunks);
        };
        if (!ring(vertex_ring_, "vertex", true) || !ring(index_ring_, "index", true) || !ring(constant_ring_, "constant", true) ||
            !ring(matrix_ring_, "matrix", false)) {
            std::cerr << "[Renderer] upload rings unavailable\n";
            shutdown();
            return false;
        }
        encoder_.attach(&pass_ops_);
        reset_sampler_frame();
        ID3D11Device1* const d3d = d3d11::Context::get().device();
        if (!bloom_.initialize(d3d)) {
            std::cerr << "[game-bloom] off on this device; the game's own chain draws it\n";
        }
        if (const RenderConfig cfg = get_render_config(); cfg.post_process) {
            if (!post_.initialize(d3d, context(), post_process_settings_from(cfg))) {
                std::cerr << "[post-process] unavailable; presenting with the plain blit\n";
            }
            set_post_process_status(post_.status());
            if (!ao_.initialize(d3d)) {
                std::cerr << "[ambient-occlusion] off on this device" << std::endl;
            }
            if (!scene_camera_.initialize(d3d)) {
                std::cerr << "[scene-camera] off on this device" << std::endl;
            }
        } else {
            PostProcessStatus off{};
            std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "CONFIG OFF");
            set_post_process_status(off);
            std::cerr << "[post-process] off; presenting with the plain blit\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "[Renderer] Direct3D 11 initialize failed: " << error.what() << '\n';
        init_error_ = error.what();
        shutdown();
        return false;
    }
    std::cout << "[Renderer] Direct3D 11 scene target=" << scene_map_.width << 'x' << scene_map_.height << " depth=D32FS8\n";
    return true;
}

bool RendererD3D11::create_device() {
    d3d11::Context& context = d3d11::Context::get();
    if (!context.initialize(std::string(get_render_config().gpu.data()), launch_args().gpu_validation)) {
        return false;
    }
    const d3d11::DeviceCaps& caps = context.caps();
    std::cout << "[gpu] adapter=\"" << context.device_name() << "\" api=d3d11 feature-level=" << std::hex
              << static_cast<unsigned>(caps.level) << std::dec << " flip-model=" << (caps.flip_model ? 1 : 0)
              << " tearing=" << (caps.tearing ? 1 : 0) << '\n';
    // Every draw binds its own table of sixteen maps: the programs' bound-table form.
    program_use_bound_tables(true);
    draw_layout_ = d3d11::RenderLayout{};
    draw_layout_.max_samples = std::clamp(caps.max_samples, 1u, 8u);
    return true;
}

bool RendererD3D11::create_states() {
    ID3D11Device1* const device = d3d11::Context::get().device();
    const auto check = [](HRESULT hr, const char* what) {
        if (FAILED(hr)) {
            throw std::runtime_error(std::string("the renderer's ") + what + " could not be made");
        }
    };
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    check(device->CreateSamplerState(&sampler, &linear_clamp_), "linear sampler");
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    check(device->CreateSamplerState(&sampler, &point_clamp_), "point sampler");

    D3D11_BUFFER_DESC constants{};
    constants.Usage = D3D11_USAGE_DYNAMIC;
    constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    constants.ByteWidth = 32u;  // a DrawRoot (program.hlsli cbuffer Draw), padded to 16-byte steps
    check(device->CreateBuffer(&constants, nullptr, &root_constants_), "draw constants");
    constants.ByteWidth = 256u;
    check(device->CreateBuffer(&constants, nullptr, &utility_constants_), "utility constants");

    check(device->CreateVertexShader(g_utility_BlitVs, sizeof(g_utility_BlitVs), nullptr, &full_screen_vs_), "full-screen vertex shader");
    const struct {
        Utility which;
        const unsigned char* bytes;
        std::size_t size;
    } pixel[] = {
        {Utility::Blit, g_utility_BlitPs, sizeof(g_utility_BlitPs)},
        {Utility::Clear, g_utility_ClearPs, sizeof(g_utility_ClearPs)},
        {Utility::ClearDepth, g_utility_ClearDepthPs, sizeof(g_utility_ClearDepthPs)},
        {Utility::DepthResolve, g_utility_DepthResolvePs, sizeof(g_utility_DepthResolvePs)},
        {Utility::Convert, g_utility_SceneConversionPs, sizeof(g_utility_SceneConversionPs)},
        {Utility::DisplayFilter, g_utility_DisplayFilterPs, sizeof(g_utility_DisplayFilterPs)},
    };
    for (const auto& shader : pixel) {
        check(device->CreatePixelShader(shader.bytes, shader.size, nullptr, &utility_ps_[static_cast<std::size_t>(shader.which)]),
              "utility pixel shader");
    }
    check(device->CreateComputeShader(g_utility_ScreenProbeCs, sizeof(g_utility_ScreenProbeCs), nullptr, &probe_cs_), "probe shader");

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    raster.ScissorEnable = TRUE;
    check(device->CreateRasterizerState(&raster, &utility_raster_), "utility rasterizer");
    for (unsigned mask = 0; mask < write_masks_.size(); ++mask) {
        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].SrcBlend = blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlend = blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blend.RenderTarget[0].BlendOp = blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].RenderTargetWriteMask = static_cast<UINT8>(mask);
        check(device->CreateBlendState(&blend, &write_masks_[mask]), "write mask");
    }
    // The renderer's own draws: no depth use; the depth clear's draw (depth and stencil replaced, as Direct3D 12's
    // ClearDepthStencilView of a rectangle does); the depth resolve (depth written whatever is there, stencil kept).
    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
    depth.StencilReadMask = depth.StencilWriteMask = 0xFFu;
    depth.FrontFace = depth.BackFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
    check(device->CreateDepthStencilState(&depth, &depth_uses_[static_cast<std::size_t>(DepthUse::None)]), "depth state");
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    check(device->CreateDepthStencilState(&depth, &depth_uses_[static_cast<std::size_t>(DepthUse::Resolve)]), "depth state");
    depth.StencilEnable = TRUE;
    depth.FrontFace = depth.BackFace = {D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE,
                                        D3D11_COMPARISON_ALWAYS};
    check(device->CreateDepthStencilState(&depth, &depth_uses_[static_cast<std::size_t>(DepthUse::Clear)]), "depth state");
    return true;
}

void RendererD3D11::report_adapter() {
    const d3d11::Context& context = d3d11::Context::get();
    const std::string& name = context.device_name();
    const unsigned vendor = context.caps().vendor;
    host_window::rtx_adapter.store(false, std::memory_order_relaxed);
    record_render_adapter(name.c_str(), vendor, 0u, 0u, false);
    RenderFeatureCaps caps{};
    caps.adapter_vendor_id = vendor;
    std::snprintf(caps.adapter_description.data(), caps.adapter_description.size(), "%s", name.c_str());
    const bool msaa = draw_layout_.max_samples > 1u;
    caps.msaa = make_render_feature_status(true, msaa, true, true,
                                           msaa ? "the scene target is multisampled" : "the GPU has no multisampled scene target formats");
    caps.taa = make_render_feature_status(false, false, false, false, "temporal upscaling runs on Direct3D 12 and Vulkan");
    caps.dxr = make_render_feature_status(false, false, false, false, "ray tracing runs on Direct3D 12");
    caps.streamline_bridge_built = false;
    caps.streamline = make_render_feature_status(false, false, false, false, "NVIDIA Streamline runs on Direct3D 12");
    caps.reflex = make_render_feature_status(false, false, false, false, "Reflex runs on Direct3D 12");
    caps.dlss_super_resolution = make_render_feature_status(false, false, false, false, "DLSS runs on Direct3D 12");
    caps.dlss_frame_generation = make_render_feature_status(false, false, false, false, "frame generation runs on Direct3D 12");
    set_render_feature_caps(caps);
}

bool RendererD3D11::wants_vsync() const {
    return get_render_config().vsync_interval != 0u;
}

bool RendererD3D11::wants_hdr() const {
    return false;
}

// A flip-model swap chain of kSwapChainBuffers 8-bit buffers at the window's size, a waitable object and a latency of one
// frame (renderer_d3d12.cpp create_swap_chain), tearing where the system offers it so that VSync off presents at once.
bool RendererD3D11::create_swap_chain() {
    d3d11::Context& context = d3d11::Context::get();
    const d3d11::DeviceCaps& caps = context.caps();
    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.BufferCount = kSwapChainBuffers;
    scd.Width = std::max(host_window::backbuffer_width, 1u);
    scd.Height = std::max(host_window::backbuffer_height, 1u);
    scd.Format = swap_format_;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // FLIP_DISCARD from Windows 10; FLIP_SEQUENTIAL before it.
    scd.SwapEffect = caps.flip_model ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.SampleDesc.Count = 1;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | (caps.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u);
    tearing_ = caps.tearing;
    d3d11::ComPtr<IDXGISwapChain1> chain;
    if (FAILED(context.factory()->CreateSwapChainForHwnd(context.device(), window_, &scd, nullptr, nullptr, &chain)) ||
        FAILED(chain.As(&swap_chain_))) {
        init_error_ = "the swap chain could not be made";
        std::cerr << "[Renderer] " << init_error_ << '\n';
        return false;
    }
    context.factory()->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(swap_chain_->SetMaximumFrameLatency(1u))) {
        std::cerr << "[present] SetMaximumFrameLatency failed\n";
    }
    frame_latency_waitable_ = swap_chain_->GetFrameLatencyWaitableObject();
    {
        static int logged_tearing = -1;
        if (logged_tearing != (tearing_ ? 1 : 0)) {
            logged_tearing = tearing_ ? 1 : 0;
            std::cerr << "[present] flip-model swap chain, "
                      << (tearing_ ? "VSync off presents with ALLOW_TEARING (no wait for the display)"
                                   : "VSync off still waits for the compositor (tearing not offered here)")
                      << ", VSync on: Present(1..4) paced by the latency object" << '\n';
        }
    }
    swap_width_ = scd.Width;
    swap_height_ = scd.Height;
    swap_vsync_ = wants_vsync();
    d3d11::ComPtr<ID3D11Texture2D> buffer;
    if (FAILED(swap_chain_->GetBuffer(0u, IID_PPV_ARGS(&buffer))) ||
        FAILED(context.device()->CreateRenderTargetView(buffer.Get(), nullptr, &backbuffer_rtv_))) {
        init_error_ = "the swap chain's buffer could not be drawn into";
        return false;
    }
    swap_dirty_ = false;
    return true;
}

bool RendererD3D11::resize_swap_chain(std::uint32_t width, std::uint32_t height) {
    if (!swap_chain_) {
        return false;
    }
    // Every reference to the buffers goes first (the immediate context's included).
    context()->OMSetRenderTargets(0u, nullptr, nullptr);
    backbuffer_rtv_.Reset();
    context()->Flush();
    DXGI_SWAP_CHAIN_DESC1 desc{};
    swap_chain_->GetDesc1(&desc);
    if (FAILED(swap_chain_->ResizeBuffers(kSwapChainBuffers, width, height, swap_format_, desc.Flags))) {
        std::cerr << "[Renderer] resize: ResizeBuffers failed\n";
        return false;
    }
    d3d11::ComPtr<ID3D11Texture2D> buffer;
    if (FAILED(swap_chain_->GetBuffer(0u, IID_PPV_ARGS(&buffer))) ||
        FAILED(d3d11::Context::get().device()->CreateRenderTargetView(buffer.Get(), nullptr, &backbuffer_rtv_))) {
        throw std::runtime_error("the swap chain's buffer could not be drawn into after a resize");
    }
    swap_width_ = width;
    swap_height_ = height;
    swap_vsync_ = wants_vsync();
    swap_dirty_ = false;
    return true;
}

RendererD3D11::Kept RendererD3D11::take_apart_lost_device() {
    keep_window_ = true;
    try {
        shutdown();
    } catch (const std::exception& error) {
        std::cerr << "[RendererD3D11] the lost device's shutdown: " << error.what() << "\n";
    }
    return Kept{window_};
}

void RendererD3D11::shutdown() {
    d3d11::Context& context = d3d11::Context::get();
    if (context.ready()) {
        pass_ops_.clear();
        scene_pass_open_ = false;
        context.immediate()->ClearState();
        wait_for_gpu_idle();
        if (mirrored_store_ != nullptr) {
            mirrored_store_->backend().set_upload_list(nullptr);
        }
        mirrored_store_ = nullptr;
        post_.shutdown();
        bloom_.shutdown();
        ao_.shutdown();
        scene_camera_.shutdown();
        scene_ = wide_scene_ = alt_scene_ = target_scene_ = SceneSet{};
        target_slots_.clear();
        pictures_.clear();
        frame_pass_targets_.clear();
        scene_capture_ = backbuffer_capture_ = display_capture_ = Readback{};
        probe_slots_.clear();
        for (Frame& frame : frames_) {
            frame = Frame{};
        }
        samplers_.clear();
        sampler_tables_.clear();
        default_sampler_table_ = gpu::Table{};
        persistent_tables_.clear();
        retired_tables_.clear();
        free_tables_.clear();
        zero_vertices_.reset();
        zero_stream_bytes_ = 0u;
        zero_stream_address_ = 0u;
        {
            std::vector<gpu::BufferRef> old;
            for (UploadRing* ring : {&vertex_ring_, &index_ring_, &constant_ring_, &matrix_ring_}) {
                ring->release_into(old);
            }
        }
        utility_ps_ = {};
        full_screen_vs_.Reset();
        probe_cs_.Reset();
        utility_raster_.Reset();
        write_masks_ = {};
        depth_uses_ = {};
        linear_clamp_.Reset();
        point_clamp_.Reset();
        root_constants_.Reset();
        utility_constants_.Reset();
        backbuffer_rtv_.Reset();
        if (frame_latency_waitable_ != nullptr) {
            CloseHandle(frame_latency_waitable_);
            frame_latency_waitable_ = nullptr;
        }
        swap_chain_.Reset();
        context.immediate()->Flush();
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

void RendererD3D11::grow_rings() {
    // A ring whose frames took over half their budget (or spilled) grows; one whose last 600 frames stayed under a
    // quarter of it gives half of its memory back (renderer_mtl.mm grow_rings).
    if (ring_growth_failed_ ||
        (!vertex_ring_.wants_growth() && !index_ring_.wants_growth() && !constant_ring_.wants_growth() &&
         !matrix_ring_.wants_growth() && !vertex_ring_.wants_shrink() && !index_ring_.wants_shrink() &&
         !constant_ring_.wants_shrink() && !matrix_ring_.wants_shrink())) {
        return;
    }
    // A ring is made again as new upload memory; the old memory goes with the frames that may still read it.
    gpu::Device* const device = this->device();
    std::vector<gpu::BufferRef> old;
    const auto place = [&](UploadRing& ring, const char* name, bool chunks) {
        const std::size_t had = ring.capacity();
        std::size_t capacity = ring.grown_capacity(kFramesInFlight);
        if (capacity == 0u) {
            ring_growth_failed_ = true;
            capacity = had;
        } else if (ring.wants_shrink()) {
            capacity = ring.shrunk_capacity(kFramesInFlight);
        }
        if (capacity != had) {
            std::cerr << "[gpu-rings] " << (capacity > had ? "grow " : "shrink ") << name << " frame-high-water-kb="
                      << (ring.high_water() >> 10u) << " mb=" << (had >> 20u) << "->" << (capacity >> 20u) << '\n';
        }
        ring.release_into(old);
        if (ring.initialize(device, name, UploadRing::budget(capacity, kFramesInFlight), kFramesInFlight, capacity, chunks)) {
            return true;
        }
        ring_growth_failed_ = true;
        std::cerr << "[gpu-rings] grow " << name << " failed\n";
        return ring.initialize(device, name, UploadRing::budget(had, kFramesInFlight), kFramesInFlight, had, chunks);
    };
    if (!place(vertex_ring_, "vertex", true) || !place(index_ring_, "index", true) || !place(constant_ring_, "constant", true) ||
        !place(matrix_ring_, "matrix", false)) {
        throw std::runtime_error("upload ring resize failed");
    }
    for (gpu::BufferRef& buffer : old) {
        frame_->retired_buffers.push_back(std::move(buffer));
    }
}

void RendererD3D11::trace_rings() const {
    const auto line = [](const char* name, const UploadRing& ring) {
        std::cerr << "[memory] ring " << name << ' ' << static_cast<double>(ring.capacity()) / (1024.0 * 1024.0)
                  << " MB, a frame took at most " << static_cast<double>(ring.high_water()) / (1024.0 * 1024.0) << " MB\n";
    };
    line("vertex", vertex_ring_);
    line("index", index_ring_);
    line("constant", constant_ring_);
    line("matrix", matrix_ring_);
}

void RendererD3D11::wait_for_gpu_idle() {
    d3d11::Context& context = d3d11::Context::get();
    if (!context.ready()) {
        return;
    }
    // Everything recorded so far, fenced and waited for.
    (void)context.wait(context.signal());
}

bool RendererD3D11::quit_requested() const noexcept {
    return host_window::quit_request.load(std::memory_order_acquire);
}

UploadBuffer RendererD3D11::create_upload_buffer(std::size_t bytes, const char* name) {
    return gpu::create_upload_memory(device(), bytes, name);
}

std::uint64_t RendererD3D11::completed_fence_value() const {
    return d3d11::Context::get().completed_value();
}

void RendererD3D11::wait_for_fence_value(std::uint64_t fence_value) {
    if (fence_value == 0u) {
        throw std::runtime_error("invalid frame-specific GPU fence wait");
    }
    if (!d3d11::Context::get().wait(fence_value)) {
        throw std::runtime_error("frame-specific GPU completion wait failed or the device was lost");
    }
}

bool RendererD3D11::video_memory(std::uint64_t& usage, std::uint64_t& budget) const noexcept {
    usage = budget = 0u;
    d3d11::ComPtr<IDXGIAdapter3> adapter;
    IDXGIAdapter1* const base = d3d11::Context::get().adapter();
    if (base == nullptr || FAILED(base->QueryInterface(IID_PPV_ARGS(&adapter)))) {
        return false;
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter->QueryVideoMemoryInfo(0u, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        return false;
    }
    usage = info.CurrentUsage;
    budget = info.Budget;
    return true;
}

void RendererD3D11::probe_occlusion() noexcept {
    if (!swap_chain_ || !occluded_) {
        return;
    }
    // A test present draws nothing and moves no buffer: S_OK once the window shows, DXGI_STATUS_OCCLUDED while it does not.
    if (swap_chain_->Present(0u, DXGI_PRESENT_TEST) != DXGI_STATUS_OCCLUDED) {
        occluded_ = false;
    }
}

void RendererD3D11::attach_texture_uploads(TextureStore& cache) {
    mirrored_store_ = &cache;
    cache.backend().set_upload_list(context());
}

void RendererD3D11::begin_frame() {
    host_window::poll_render_config_file();
    follow_window_and_resize();
    apply_window_size();
    frame_slot_ = static_cast<unsigned>(frame_index_ % kFramesInFlight);
    Frame& frame = frames_[frame_slot_];
    frame_ = &frame;
    grow_rings();
    sync_post_process();
    bloom_.trim_idle();
    ao_.trim_idle();
    if (frame.fence != 0u) {
        wait_for_fence_value(frame.fence);
        // The slot's last frame's GPU time, from its timestamps (the fence passed: they are there).
        if (frame.timed) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            UINT64 start = 0u, end = 0u;
            ID3D11DeviceContext1* const ctx = context();
            if (ctx->GetData(frame.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                !disjoint.Disjoint && disjoint.Frequency != 0u &&
                ctx->GetData(frame.frame_time[0].Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
                ctx->GetData(frame.frame_time[1].Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && end >= start) {
                const double gpu_ms = static_cast<double>(end - start) * 1000.0 / static_cast<double>(disjoint.Frequency);
                if (frame.scene_drawn) {
                    native_scene_gpu_ema_ms_ = native_scene_gpu_ema_ms_ == 0.0 ? gpu_ms
                                               : native_scene_gpu_ema_ms_ + (gpu_ms - native_scene_gpu_ema_ms_) * 0.1;
                }
                const float before = frame_gpu_ema_ms_.load(std::memory_order_relaxed);
                frame_gpu_ema_ms_.store(before == 0.0f ? static_cast<float>(gpu_ms)
                                                       : before + (static_cast<float>(gpu_ms) - before) * 0.1f,
                                        std::memory_order_relaxed);
                stats_.gpu_ms += gpu_ms;
                ++stats_.gpu_samples;
                read_pass_times(frame, disjoint.Frequency, end);
            }
        }
    }
    frame.pass_mark_count = 0u;
    frame.retired.clear();
    frame.retired_buffers.clear();
    frame.tables_used = 0u;
    if (frame_index_ % 120u == 0u) {
        sweep_target_slots();
    }
    reset_sampler_frame();
    // The frame's GPU time: always for the scene's average (interpolation_gpu_cost_ms), a query pair a frame.
    ID3D11Device1* const device = d3d11::Context::get().device();
    if (!frame.disjoint) {
        D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP_DISJOINT, 0u};
        frame.timed = SUCCEEDED(device->CreateQuery(&query, &frame.disjoint));
        query.Query = D3D11_QUERY_TIMESTAMP;
        frame.timed = frame.timed && SUCCEEDED(device->CreateQuery(&query, &frame.frame_time[0])) &&
                      SUCCEEDED(device->CreateQuery(&query, &frame.frame_time[1]));
    }
    // The passes' timestamps: for the settings page while it asks, or the diagnostics.
    pass_timing_on_ = frame.timed && (gpu_timing_on() || ::galaxy::diagnosing(::galaxy::kDiagnoseGpuPasses));
    if (pass_timing_on_ && frame.pass_queries.empty()) {
        D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP, 0u};
        frame.pass_queries.resize(kGpuPassSlots);
        for (d3d11::ComPtr<ID3D11Query>& q : frame.pass_queries) {
            if (FAILED(device->CreateQuery(&query, &q))) {
                frame.pass_queries.clear();
                break;
            }
        }
    }
    pass_timing_on_ = pass_timing_on_ && !frame.pass_queries.empty();
    if (frame.timed) {
        context()->Begin(frame.disjoint.Get());
        context()->End(frame.frame_time[0].Get());
        if (pass_timing_on_) {
            context()->End(frame.pass_queries[0].Get());
            frame.pass_marks[0] = GpuPass::Scene;
            frame.pass_mark_count = 1u;
        }
    }
    current_pass_ = GpuPass::Scene;
    index_ring_.begin_frame(frame_slot_);
    constant_ring_.begin_frame(frame_slot_);
    vertex_ring_.begin_frame(frame_slot_);
    matrix_ring_.begin_frame(frame_slot_);
    reset_draw_bindings();
    scene_pass_open_ = false;
    scene_drawn_ = false;
    presented_ = false;
    if (mirrored_store_ != nullptr) {
        mirrored_store_->backend().set_upload_list(context());
    }
}

unsigned RendererD3D11::present_sync_interval(unsigned configured, float max_fps) {
    const bool game_frame = std::exchange(present_game_frame_, false);
    if (configured != 1u || variable_refresh_) {
        return configured;  // a variable refresh display shows a frame at once: whole refreshes mean nothing
    }
    const double hz = display_refresh_hz_;
    if (hz <= 0.0) {
        return 1u;
    }
    // As renderer_d3d12.cpp present_sync_interval: a game frame holds for its whole 60 Hz frame on a display of a multiple.
    const double rate = game_frame ? std::min(kGameStepRate, present_frame_rate(max_fps, configured, hz))
                                   : present_frame_rate(max_fps, configured, hz);
    const double multiple = std::round(hz / rate);
    if (multiple >= 2.0 && multiple <= 4.0 && std::abs(hz - rate * multiple) <= 0.015 * hz) {
        return static_cast<unsigned>(multiple);
    }
    return 1u;
}

std::uint64_t RendererD3D11::end_frame(bool present_swap_chain, SubmissionCallback on_submitted, void* submission_context,
                                       bool) {
    Frame& frame = frames_[frame_slot_];
    end_scene_pass();
    if (mirrored_store_ != nullptr) {
        mirrored_store_->backend().set_upload_list(nullptr);
    }
    ID3D11DeviceContext1* const ctx = context();
    if (frame.timed) {
        ctx->End(frame.frame_time[1].Get());
        ctx->End(frame.disjoint.Get());
    }
    const bool presenting = present_swap_chain && presented_ && swap_chain_ && !occluded_;
    double pace_wait_ms = 0.0;
    double present_ms = 0.0;
    if (presenting) {
        const RenderConfig cfg = get_render_config();
        const UINT vsync = present_sync_interval(std::min(2u, cfg.vsync_interval), cfg.max_fps);
        const UINT flags = vsync == 0u && tearing_ ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        const float variable_rate = variable_refresh_ ? static_cast<float>(display_refresh_hz_) : 0.0f;
        const float paced_fps = variable_refresh_
                                    ? (presented_max_fps(cfg) > 0.0f ? std::min(presented_max_fps(cfg), variable_rate) : variable_rate)
                                    : presented_max_fps(cfg);
        pace_wait_ms = present_pacer_.pace(variable_refresh_ ? 0u : cfg.vsync_interval, paced_fps);
        // The display's own signal that a frame may be queued, awaited asleep and bounded (renderer_d3d12.cpp).
        if (frame_latency_waitable_ != nullptr && vsync != 0u &&
            WaitForSingleObjectEx(frame_latency_waitable_, 100u, TRUE) == WAIT_TIMEOUT && ++latency_timeouts_ <= 4u) {
            std::cerr << "[present] the frame latency object did not signal within 100 ms\n";
        }
        const std::int64_t start = galaxy::qpc_now();
        const HRESULT hr = swap_chain_->Present(vsync, flags);
        const std::int64_t end = galaxy::qpc_now();
        present_ms = static_cast<double>(end - start) * 1000.0 / static_cast<double>(galaxy::qpc_frequency());
        occluded_ = hr == DXGI_STATUS_OCCLUDED;
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            surface_lock_ = {};
            throw std::runtime_error("the Direct3D 11 device was lost at present");
        }
        swap_present_qpc_ = end;
    }
    surface_lock_ = {};
    const std::uint64_t fence = d3d11::Context::get().signal();
    (void)d3d11::Context::get().report_messages();
    frame.fence = fence;
    frame.scene_drawn = scene_drawn_;
    last_submitted_ = fence;
    for (ProbeSlot& probes : probe_slots_) {
        if (probes.fence == kProbesInFrame) {
            probes.fence = fence;
        }
    }
    if (scene_capture_.staging && scene_capture_.fence == 0u && !scene_capture_pending_path_.empty()) {
        scene_capture_.fence = fence;
    }
    if (backbuffer_capture_.staging && backbuffer_capture_.fence == 0u && !backbuffer_capture_pending_path_.empty()) {
        backbuffer_capture_.fence = fence;
    }
    if (display_capture_.staging && display_capture_.fence == 0u && !display_capture_pending_path_.empty()) {
        display_capture_.fence = fence;
    }
    if (on_submitted != nullptr) {
        on_submitted(submission_context, fence);
    }
    if (presenting) {
        // Presents per second over the last second (the FRAME RATE row).
        ++measured_present_count_;
        if (measured_present_window_qpc_ == 0) {
            measured_present_window_qpc_ = swap_present_qpc_;
            measured_present_count_ = 0u;
        } else if (swap_present_qpc_ - measured_present_window_qpc_ >= galaxy::qpc_frequency()) {
            const double hz = static_cast<double>(measured_present_count_) * static_cast<double>(galaxy::qpc_frequency()) /
                              static_cast<double>(swap_present_qpc_ - measured_present_window_qpc_);
            measured_present_hz_.store(hz, std::memory_order_relaxed);
            host_window::measured_present_hz.store(hz, std::memory_order_relaxed);
            measured_present_window_qpc_ = swap_present_qpc_;
            measured_present_count_ = 0u;
        }
        report_present_stats(pace_wait_ms, present_ms);
        if (!window_shown_ && window_ != nullptr && !host_window::hidden_window_mode_enabled()) {
            window_shown_ = true;
            host_window::show_window(window_);
        }
    }
    presented_ = false;
    ++frame_index_;
    return fence;
}

void RendererD3D11::report_present_stats(double pace_wait_ms, double present_ms) {
    if (!trace_present_stats_enabled()) {
        return;
    }
    const std::int64_t now = galaxy::qpc_now();
    if (stats_.window_start_qpc == 0) {
        stats_.window_start_qpc = now;
    }
    if (stats_.last_present_qpc != 0) {
        const double interval = static_cast<double>(now - stats_.last_present_qpc) * 1000.0 / static_cast<double>(galaxy::qpc_frequency());
        stats_.interval_ms += interval;
        stats_.interval_max_ms = std::max(stats_.interval_max_ms, interval);
        ++stats_.intervals;
    }
    stats_.last_present_qpc = now;
    ++stats_.presents;
    stats_.pace_wait_ms += pace_wait_ms;
    stats_.pace_wait_max_ms = std::max(stats_.pace_wait_max_ms, pace_wait_ms);
    stats_.present_ms += present_ms;
    stats_.present_max_ms = std::max(stats_.present_max_ms, present_ms);
    const double seconds = static_cast<double>(now - stats_.window_start_qpc) / static_cast<double>(galaxy::qpc_frequency());
    if (seconds < 2.0) {
        return;
    }
    const double presents = static_cast<double>(stats_.presents);
    std::ostringstream message;
    message << "[present-stats] fps=" << presents / seconds << " frame-ms(avg/max)="
            << (stats_.intervals != 0u ? stats_.interval_ms / static_cast<double>(stats_.intervals) : 0.0) << '/'
            << stats_.interval_max_ms << " pace-wait-ms(avg/max)=" << stats_.pace_wait_ms / presents << '/'
            << stats_.pace_wait_max_ms << " present-ms(avg/max)=" << stats_.present_ms / presents << '/' << stats_.present_max_ms
            << " vsync=" << get_render_config().vsync_interval << " gpu-ms(avg)=";
    if (stats_.gpu_samples != 0u) {
        message << stats_.gpu_ms / static_cast<double>(stats_.gpu_samples);
    } else {
        message << "unavailable";
    }
    message << " gpu-samples=" << stats_.gpu_samples;
    if (::galaxy::diagnosing(::galaxy::kDiagnoseGpuPasses)) {
        static_assert(std::size(kPassStatNames) == static_cast<std::size_t>(GpuPass::Count));
        message << " gpu-pass-ms=";
        for (std::size_t pass = 0u; pass < std::size(kPassStatNames); ++pass) {
            message << (pass == 0u ? "" : ",") << kPassStatNames[pass] << ':' << telemetry_pass_ms_[pass] / presents;
        }
        telemetry_pass_ms_.fill(0.0);
    }
    message << " api=d3d11\n";
    std::cerr << message.str();
    stats_ = PresentStats{};
    stats_.window_start_qpc = now;
    stats_.last_present_qpc = now;
}

// The GPU pass timing rule of every renderer (board vk T1): the time between two timestamps is the work that fills it,
// the pass that began at the first. A scope's end takes a timestamp of the outer pass, so work that continues after it
// (the scene's geometry after the camera's widen draw or the bloom's composite) is the outer pass's. Direct3D 11 has no
// render passes to keep timestamps out of; the draws recorded before the change run first (flush_draws), so they are
// billed to the pass they were recorded in. Only while timing (the pictures do not change: the same draws, same order).
void RendererD3D11::gpu_pass(GpuPass pass) {
    if (pass == current_pass_) {
        return;
    }
    current_pass_ = pass;
    Frame& frame = frames_[frame_slot_];
    if (!pass_timing_on_ || frame.pass_mark_count >= frame.pass_queries.size()) {
        return;
    }
    flush_draws();
    context()->End(frame.pass_queries[frame.pass_mark_count].Get());
    frame.pass_marks[frame.pass_mark_count++] = pass;
}

// The slot's last frame's pass times (its fence passed), to the frame's last timestamp.
void RendererD3D11::read_pass_times(Frame& frame, UINT64 frequency, UINT64 frame_end) {
    const unsigned marks = frame.pass_mark_count;
    if (marks == 0u || frequency == 0u) {
        return;
    }
    std::array<UINT64, kGpuPassSlots + 1u> ticks{};
    ID3D11DeviceContext1* const ctx = context();
    for (unsigned i = 0u; i < marks; ++i) {
        if (ctx->GetData(frame.pass_queries[i].Get(), &ticks[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            return;
        }
    }
    ticks[marks] = frame_end;
    std::array<double, static_cast<std::size_t>(GpuPass::Count)> frame_ms{};
    for (unsigned i = 0u; i < marks; ++i) {
        if (ticks[i + 1u] >= ticks[i]) {
            const double ms = static_cast<double>(ticks[i + 1u] - ticks[i]) * 1000.0 / static_cast<double>(frequency);
            frame_ms[static_cast<std::size_t>(frame.pass_marks[i])] += ms;
        }
    }
    for (std::size_t pass = 0u; pass < frame_ms.size(); ++pass) {
        telemetry_pass_ms_[pass] += frame_ms[pass];
        if (frame_ms[pass] > 0.0) {
            const float before = pass_ema_ms_[pass].load(std::memory_order_relaxed);
            const float value = static_cast<float>(frame_ms[pass]);
            pass_ema_ms_[pass].store(before == 0.0f ? value : before + (value - before) * 0.1f, std::memory_order_relaxed);
        }
    }
}

std::size_t RendererD3D11::gpu_pass_times(GpuPassTime* out, std::size_t capacity) const noexcept {
    static_assert(std::size(kPassTimeNames) == static_cast<std::size_t>(GpuPass::Count));
    std::size_t count = 0u;
    for (std::size_t pass = 0u; pass < std::size(kPassTimeNames) && count < capacity; ++pass) {
        if (const float ms = pass_ema_ms_[pass].load(std::memory_order_relaxed); ms > 0.0f) {
            out[count++] = {kPassTimeNames[pass], ms};
        }
    }
    return count;
}

void RendererD3D11::sync_post_process() {
    if (!post_.ready()) {
        return;
    }
    const PostProcessSettings next = post_process_settings_from(get_render_config());
    const PostProcessSettings& current = post_.settings();
    if (next.aa != current.aa || next.smaa_quality != current.smaa_quality || next.upscaler != current.upscaler ||
        next.sharpness != current.sharpness) {
        post_.apply_settings(next);
        set_post_process_status(post_.status());
    }
}

bool RendererD3D11::vblank_grid(std::int64_t& vblank, std::int64_t& period) const noexcept {
    return window_ != nullptr && host_window::vblank_grid(window_, vblank, period);
}

void RendererD3D11::refresh_display_rate() {
    const double mode_hz = host_window::display_refresh_hz(window_);
    if (mode_hz <= 1.0) {
        return;
    }
    // The vblanks' own rate: a display that varies its refresh with the presents shows a rate well under the mode's.
    std::int64_t vblank = 0;
    std::int64_t period = 0;
    const double measured_hz =
        vblank_grid(vblank, period) && period > 0 ? static_cast<double>(galaxy::qpc_frequency()) / static_cast<double>(period) : 0.0;
    // Tearing is what a variable refresh display needs to engage on a flip-model chain.
    const bool variable = tearing_ && next_variable_refresh(variable_refresh_, mode_hz, measured_hz);
    const double hz = variable ? variable_refresh_cap(mode_hz) : mode_hz;
    if (variable != variable_refresh_) {
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

// The swap chain follows the window's size and the VSync setting, between frames.
void RendererD3D11::follow_window_and_resize() {
    if (host_window::surface_withdrawn.load(std::memory_order_acquire)) {
        return;
    }
    const std::uint64_t extent = host_window::pending_resize_extent.exchange(0u, std::memory_order_acq_rel);
    if (extent != 0u) {
        const auto width = static_cast<std::uint32_t>(extent >> 32u);
        const auto height = static_cast<std::uint32_t>(extent & 0xffffffffu);
        if (width != 0u && height != 0u) {
            host_window::backbuffer_width = width;
            host_window::backbuffer_height = height;
        }
    }
    if (host_window::backbuffer_width != swap_width_ || host_window::backbuffer_height != swap_height_) {
        swap_dirty_ = true;
    }
    // A window with no area (minimised) is not followed until it has one again.
    if (swap_dirty_ && host_window::backbuffer_width != 0u && host_window::backbuffer_height != 0u) {
        (void)resize_swap_chain(host_window::backbuffer_width, host_window::backbuffer_height);
    }
    swap_vsync_ = wants_vsync();
    if (host_window::monitor_changed.exchange(false, std::memory_order_acq_rel) || display_refresh_checks_++ % 120u == 0u) {
        refresh_display_rate();
    }
}

void RendererD3D11::log_video_memory(const VideoMemoryOutside& outside) {
    const auto mb = [](std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    (void)video_memory(usage, budget);
    const auto set_bytes = [](const SceneSet& set) {
        return allocation_bytes(set.color.get()) + allocation_bytes(set.depth.get()) + allocation_bytes(set.ms_color.get()) +
               allocation_bytes(set.ms_depth.get());
    };
    const std::uint64_t scene = set_bytes(scene_) + set_bytes(alt_scene_) + set_bytes(target_scene_) +
                                allocation_bytes(wide_scene_.color.get()) + allocation_bytes(wide_scene_.ms_color.get());
    std::uint64_t slots = 0u;
    for (const auto& entry : target_slots_) {
        slots += set_bytes(entry.second.set);
    }
    std::uint64_t frame_pass = 0u;
    for (const auto& entry : frame_pass_targets_) {
        frame_pass += allocation_bytes(entry.second.texture.get());
    }
    std::uint64_t rings = 0u;
    std::uint64_t chunks = 0u;
    for (const UploadRing* ring : {&vertex_ring_, &index_ring_, &constant_ring_, &matrix_ring_}) {
        rings += ring->capacity();
        chunks += ring->live_chunk_bytes();
    }
    std::fprintf(stderr,
                 "[vram] breakdown allocated=%.0f budget=%.0f MB | scene-targets=%.1f target-slots=%zu %.1f frame-pass=%zu %.1f "
                 "pictures=%u %.1f | rings=%.0f chunks=%.0f | staging=%.0f mesh-pages=%u (empty %u) %.0f live %.1f slots=%u "
                 "%.1f | pipelines=%llu\n",
                 mb(usage), mb(budget), mb(scene), target_slots_.size(), mb(slots), frame_pass_targets_.size(), mb(frame_pass),
                 outside.picture_count, mb(outside.picture_bytes), mb(rings), mb(chunks), mb(outside.staging_bytes),
                 outside.mesh_pages, outside.mesh_empty_pages, mb(outside.mesh_page_bytes), mb(outside.mesh_live_bytes),
                 outside.slots, mb(outside.slot_bytes), static_cast<unsigned long long>(outside.pipelines));
}

// ---- draws ----

void RendererD3D11::reset_draw_bindings() noexcept {
    encoder_.reset();
}

void RendererD3D11::bind_constants(unsigned slot, gpu::Address address, bool vertex, bool pixel) {
    const d3d11::Context::BufferOffset at = d3d11::Context::get().resolve(address);
    ID3D11Buffer* buffer = at.buffer != nullptr ? at.buffer->bound(d3d11::BufferUse::Constants) : nullptr;
    const UINT first = static_cast<UINT>(at.offset / 16u);
    const UINT count = at.buffer != nullptr ? constants_in(at.buffer->size, at.offset) : 0u;
    if (count == 0u) {
        buffer = nullptr;
    }
    ID3D11DeviceContext1* const ctx = context();
    if (vertex) {
        ctx->VSSetConstantBuffers1(slot, 1u, &buffer, &first, &count);
    }
    if (pixel) {
        ctx->PSSetConstantBuffers1(slot, 1u, &buffer, &first, &count);
    }
}

void RendererD3D11::bind_rows(gpu::Address rows, gpu::Address instances) {
    ID3D11ShaderResourceView* views[2] = {};
    const gpu::Address addresses[2] = {rows, instances};
    for (unsigned i = 0; i < 2u; ++i) {
        const d3d11::Context::BufferOffset at = d3d11::Context::get().resolve(addresses[i]);
        views[i] = at.buffer != nullptr ? at.buffer->rows_view(at.offset) : nullptr;
    }
    // t16 the composed rows, t17 the instance table: read by both stages (lights in the pixel stage).
    context()->VSSetShaderResources(16u, 2u, views);
    context()->PSSetShaderResources(16u, 2u, views);
}

// The pass's recorded draws, after every byte of upload memory they read has reached the GPU (d3d11_context.h).
void RendererD3D11::flush_draws() {
    if (pass_ops_.empty()) {
        return;
    }
    d3d11::Context& device = d3d11::Context::get();
    device.upload_written();
    // The draws go to the scene target: a multisampled one's resolve is stale from here.
    scene_.color_dirty |= scene_.samples > 1u;
    ID3D11DeviceContext1* const ctx = context();
    ID3D11Buffer* const root = root_constants_.Get();
    ctx->VSSetConstantBuffers(3u, 1u, &root);
    ctx->PSSetConstantBuffers(3u, 1u, &root);
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    for (const gpu::DrawOp& op : pass_ops_) {
        switch (op.kind) {
        case gpu::DrawOp::Kind::Pipeline: {
            const auto* pipeline = reinterpret_cast<const d3d11::Pipeline*>(static_cast<std::uintptr_t>(op.a[0]));
            if (pipeline == nullptr) {
                break;
            }
            ctx->IASetInputLayout(pipeline->input.Get());
            ctx->VSSetShader(pipeline->vertex.Get(), nullptr, 0u);
            ctx->PSSetShader(pipeline->pixel.Get(), nullptr, 0u);
            ctx->OMSetBlendState(pipeline->blend.Get(), nullptr, pipeline->sample_mask);
            ctx->RSSetState(pipeline->rasterizer.Get());
            ctx->OMSetDepthStencilState(pipeline->depth_stencil.Get(), pipeline->stencil_reference);
            if (pipeline->topology != topology) {
                topology = pipeline->topology;
                ctx->IASetPrimitiveTopology(topology);
            }
            break;
        }
        case gpu::DrawOp::Kind::View:
            bind_constants(0u, op.a[0], true, true);
            break;
        case gpu::DrawOp::Kind::Material: {
            // b1 the vertex stage's material, b2 the pixel stage's (a written program's both stages read b2).
            ID3D11Buffer* buffers[2] = {};
            UINT first[2] = {};
            UINT count[2] = {};
            for (unsigned i = 0; i < 2u; ++i) {
                const d3d11::Context::BufferOffset at = device.resolve(op.a[i]);
                count[i] = at.buffer != nullptr ? constants_in(at.buffer->size, at.offset) : 0u;
                buffers[i] = count[i] != 0u ? at.buffer->bound(d3d11::BufferUse::Constants) : nullptr;
                first[i] = static_cast<UINT>(at.offset / 16u);
            }
            ctx->VSSetConstantBuffers1(1u, 2u, buffers, first, count);
            ctx->PSSetConstantBuffers1(1u, 2u, buffers, first, count);
            break;
        }
        case gpu::DrawOp::Kind::Root: {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(ctx->Map(root, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
                std::memcpy(mapped.pData, op.w, sizeof(gpu::DrawRoot));
                ctx->Unmap(root, 0u);
            }
            break;
        }
        case gpu::DrawOp::Kind::Tables: {
            const auto* textures = reinterpret_cast<const d3d11::BoundTable*>(static_cast<std::uintptr_t>(op.a[0]));
            const auto* samplers = reinterpret_cast<const d3d11::BoundTable*>(static_cast<std::uintptr_t>(op.a[1]));
            if (textures != nullptr) {
                ctx->PSSetShaderResources(0u, textures->count,
                                          reinterpret_cast<ID3D11ShaderResourceView* const*>(textures->items.data()));
            }
            if (samplers != nullptr) {
                ctx->PSSetSamplers(0u, samplers->count, reinterpret_cast<ID3D11SamplerState* const*>(samplers->items.data()));
            }
            break;
        }
        case gpu::DrawOp::Kind::Rows:
            bind_rows(op.a[0], op.a[1]);
            break;
        case gpu::DrawOp::Kind::Scissor: {
            gpu::Rect rect;
            std::memcpy(&rect, op.w, sizeof(rect));
            const D3D11_RECT scissor{rect.left, rect.top, rect.right, rect.bottom};
            ctx->RSSetScissorRects(1u, &scissor);
            break;
        }
        case gpu::DrawOp::Kind::Viewport: {
            gpu::ViewportRect rect;
            std::memcpy(&rect, op.w, sizeof(rect));
            const D3D11_VIEWPORT viewport{rect.x, rect.y, rect.width, rect.height, rect.min_depth, rect.max_depth};
            ctx->RSSetViewports(1u, &viewport);
            break;
        }
        case gpu::DrawOp::Kind::Stream: {
            const d3d11::Context::BufferOffset at = device.resolve(op.a[0]);
            ID3D11Buffer* buffer = at.buffer != nullptr ? at.buffer->bound(d3d11::BufferUse::Geometry) : nullptr;
            const UINT stride = op.w[1];
            const UINT offset = static_cast<UINT>(at.offset);
            ctx->IASetVertexBuffers(op.slot, 1u, &buffer, &stride, &offset);
            break;
        }
        case gpu::DrawOp::Kind::Indices: {
            const d3d11::Context::BufferOffset at = device.resolve(op.a[0]);
            ID3D11Buffer* buffer = at.buffer != nullptr ? at.buffer->bound(d3d11::BufferUse::Geometry) : nullptr;
            ctx->IASetIndexBuffer(buffer, op.w[1] == 4u ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, static_cast<UINT>(at.offset));
            break;
        }
        case gpu::DrawOp::Kind::DrawIndexed:
            ctx->DrawIndexedInstanced(op.w[0], op.w[1], op.w[2], static_cast<INT>(op.w[3]), 0u);
            break;
        case gpu::DrawOp::Kind::Draw:
            ctx->DrawInstanced(op.w[0], op.w[1], op.w[2], 0u);
            break;
        case gpu::DrawOp::Kind::Arrays:
            break;  // never recorded: no arrays on Direct3D 11
        }
    }
    pass_ops_.clear();
}

// A chunk of a recorded pass replayed inline, as the walker would have drawn it.
void RendererD3D11::replay_draws(std::uint8_t aux, const gpu::DrawOp* ops, std::size_t count) {
    set_draw_aux(aux);
    gpu::replay_ops(encoder_, ops, count, [](const gpu::TextureArrays&) {});
}

void RendererD3D11::utility_draw(Utility shader, ID3D11ShaderResourceView* source, const void* constants, std::size_t constant_bytes,
                                 const D3D11_VIEWPORT& viewport, const gpu::Rect& rect, std::uint8_t write_mask, DepthUse depth) {
    flush_draws();
    ID3D11DeviceContext1* const ctx = context();
    if (constants != nullptr && constant_bytes != 0u) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(utility_constants_.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
            std::memcpy(mapped.pData, constants, std::min<std::size_t>(constant_bytes, 256u));
            ctx->Unmap(utility_constants_.Get(), 0u);
        }
        ID3D11Buffer* const buffer = utility_constants_.Get();
        ctx->PSSetConstantBuffers(0u, 1u, &buffer);
    }
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(full_screen_vs_.Get(), nullptr, 0u);
    ctx->PSSetShader(utility_ps_[static_cast<std::size_t>(shader)].Get(), nullptr, 0u);
    ctx->PSSetShaderResources(0u, 1u, &source);
    ID3D11SamplerState* const sampler = linear_clamp_.Get();
    ctx->PSSetSamplers(0u, 1u, &sampler);
    ctx->RSSetState(utility_raster_.Get());
    ctx->RSSetViewports(1u, &viewport);
    const D3D11_RECT scissor{rect.left, rect.top, rect.right, rect.bottom};
    ctx->RSSetScissorRects(1u, &scissor);
    ctx->OMSetBlendState(write_masks_[write_mask & 0xFu].Get(), nullptr, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(depth_uses_[static_cast<std::size_t>(depth)].Get(), 0u);
    ctx->Draw(3u, 0u);
    // What the draws bound is bound again by the next ones; t0 lets go of the source (it may be a target next).
    ID3D11ShaderResourceView* const none = nullptr;
    ctx->PSSetShaderResources(0u, 1u, &none);
    reset_draw_bindings();
}

// ---- samplers and tables ----

ID3D11SamplerState* RendererD3D11::sampler_for(const SamplerKey& key, const RenderConfig& cfg) {
    const SamplerCacheKey cache_key{key, sampler_config_key(cfg)};
    if (const auto it = samplers_.find(cache_key); it != samplers_.end()) {
        return it->second.Get();
    }
    // renderer_d3d12.cpp build_sampler_desc_from_key, field for field.
    const auto wrap = [](std::uint32_t mode) {
        switch (mode) {
        case 0: return D3D11_TEXTURE_ADDRESS_CLAMP;
        case 2: return D3D11_TEXTURE_ADDRESS_MIRROR;
        default: return D3D11_TEXTURE_ADDRESS_WRAP;
        }
    };
    const float cfg_bias = static_cast<float>(std::clamp(cfg.texture_lod_bias, 0u, 8u)) * 0.5f;
    const unsigned anisotropy = std::clamp(cfg.anisotropic_filtering, 1u, 16u);
    const bool mag_linear = (key.filter & 16u) != 0u;
    const std::uint32_t minf = (key.filter >> 5u) & 7u;
    const bool min_linear = minf >= 4u;
    const bool no_anisotropy = (minf & 3u) == 3u;
    const unsigned mip_mode = no_anisotropy ? 2u : minf & 3u;
    const bool use_anisotropic = anisotropy > 1u && mip_mode != 0u && min_linear && mag_linear && !no_anisotropy;
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = use_anisotropic ? D3D11_FILTER_ANISOTROPIC
                                  : D3D11_ENCODE_BASIC_FILTER(min_linear ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                                                              mag_linear ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                                                              mip_mode == 2u ? D3D11_FILTER_TYPE_LINEAR : D3D11_FILTER_TYPE_POINT,
                                                              false);
    desc.AddressU = wrap(key.filter & 3u);
    desc.AddressV = wrap((key.filter >> 2u) & 3u);
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MipLODBias = std::clamp(key.lod_bias + cfg_bias, D3D11_MIP_LOD_BIAS_MIN, D3D11_MIP_LOD_BIAS_MAX);
    desc.MaxAnisotropy = use_anisotropic ? anisotropy : 1u;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    if (mip_mode != 0u) {
        desc.MinLOD = key.min_lod;
        desc.MaxLOD = key.max_lod > key.min_lod ? key.max_lod : key.min_lod;
    }
    d3d11::ComPtr<ID3D11SamplerState> made;
    if (FAILED(d3d11::Context::get().device()->CreateSamplerState(&desc, &made))) {
        throw std::runtime_error("[samplers] a sampler could not be made");
    }
    ID3D11SamplerState* const raw = made.Get();
    samplers_.emplace(cache_key, std::move(made));
    return raw;
}

// A draw's table, as many maps as d3d11::kBoundTableSlots, the rest the null texture.
void RendererD3D11::write_table(d3d11::BoundTable& table, const TextureHandle* handles, unsigned count, const TextureStore& cache) {
    const TextureBackend& backend = cache.backend();
    for (unsigned m = 0; m < d3d11::kBoundTableSlots; ++m) {
        const bool bound = m < count && handles[m].resource != nullptr;
        table.items[m] = bound ? backend.view_at(handles[m].srv_index) : backend.null_view();
    }
    table.count = d3d11::kBoundTableSlots;
}

gpu::Table RendererD3D11::texture_table_for(const TextureHandle* handles, unsigned count, const TextureStore& cache) {
    Frame& frame = *frame_;
    if (frame.tables_used == frame.tables.size()) {
        frame.tables.emplace_back();
    }
    d3d11::BoundTable& table = frame.tables[frame.tables_used++];
    write_table(table, handles, count, cache);
    return gpu::Table{reinterpret_cast<std::uint64_t>(&table)};
}

gpu::Table RendererD3D11::persistent_texture_table(const TextureHandle* handles, unsigned count, const TextureStore& cache,
                                                   gpu::Table old) {
    const std::uint64_t completed = completed_fence_value();
    std::erase_if(retired_tables_, [&](const RetiredTable& retired) {
        if (retired.fence > completed) {
            return false;
        }
        free_tables_.push_back(retired.table);
        return true;
    });
    if (old.ptr != 0u) {
        retire_texture_table(old);
    }
    d3d11::BoundTable* table = nullptr;
    if (!free_tables_.empty()) {
        table = free_tables_.back();
        free_tables_.pop_back();
    } else {
        persistent_tables_.push_back(std::make_unique<d3d11::BoundTable>());
        table = persistent_tables_.back().get();
    }
    write_table(*table, handles, count, cache);
    return gpu::Table{reinterpret_cast<std::uint64_t>(table)};
}

// A table is only names (the views it holds are the store's): it is free again once the draws that bound it are recorded.
// Kept until the GPU is past them all the same, as the other renderers keep theirs.
void RendererD3D11::retire_texture_table(gpu::Table table) {
    retired_tables_.push_back({reinterpret_cast<d3d11::BoundTable*>(table.ptr), last_submitted_ + 1u});
}

// A bound table's samplers; the maps past `count` read the default sampler.
gpu::Table RendererD3D11::sampler_table_for(const SamplerKey* keys, unsigned count) {
    if (count == 0u) {
        return default_sampler_table_;
    }
    const RenderConfig cfg = get_render_config();
    SamplerTableKey table_key = make_sampler_table_key(keys, count, cfg);
    if (const auto it = sampler_tables_.find(table_key); it != sampler_tables_.end()) {
        return gpu::Table{reinterpret_cast<std::uint64_t>(it->second.get())};
    }
    auto table = std::make_unique<d3d11::BoundTable>();
    for (unsigned i = 0; i < d3d11::kBoundTableSlots; ++i) {
        table->items[i] = sampler_for(i < count ? keys[i] : kDefaultSamplerKey, cfg);
    }
    table->count = d3d11::kBoundTableSlots;
    const gpu::Table made{reinterpret_cast<std::uint64_t>(table.get())};
    sampler_tables_.emplace(std::move(table_key), std::move(table));
    return made;
}

// Between frames: the samplers made again when the settings change, which also makes the walker ask for its tables again.
void RendererD3D11::reset_sampler_frame() {
    const RenderConfig cfg = get_render_config();
    const std::uint32_t config = sampler_config_key(cfg);
    if (default_sampler_table_.ptr != 0u && config == sampler_config_) {
        return;
    }
    std::array<SamplerKey, d3d11::kBoundTableSlots> keys{};
    keys.fill(kDefaultSamplerKey);
    default_sampler_table_ = sampler_table_for(keys.data(), d3d11::kBoundTableSlots);
    sampler_config_ = config;
    ++sampler_generation_;
}

gpu::Address RendererD3D11::zero_stream(std::uint32_t bytes) {
    if (bytes > zero_stream_bytes_) {
        // Frames in flight read the old one: it goes with them.
        if (zero_vertices_) {
            frames_[frame_slot_].retired_buffers.push_back(std::move(zero_vertices_));
        }
        zero_stream_bytes_ = std::max(bytes, 1u << 16u);
        UploadBuffer memory = gpu::create_upload_memory(device(), zero_stream_bytes_, "zero stream");
        if (!memory) {
            throw std::runtime_error("the zero stream could not be made");
        }
        std::memset(memory.cpu, 0, zero_stream_bytes_);
        zero_vertices_ = std::move(memory.buffer);
        zero_stream_address_ = memory.gpu;
    }
    return zero_stream_address_;
}

// ---- present ----

// The picture as the window shows it: black, then the picture in its content rectangle.
void RendererD3D11::draw_presented_picture(ID3D11RenderTargetView* target, const d3d11::Texture& picture, unsigned width,
                                           unsigned height, const galaxy::ContentViewport& content) {
    ID3D11DeviceContext1* const ctx = context();
    constexpr float kBlack[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    ctx->ClearRenderTargetView(target, kBlack);
    if (present_black_) {
        return;
    }
    ctx->OMSetRenderTargets(1u, &target, nullptr);
    const D3D11_VIEWPORT viewport{static_cast<float>(content.left), static_cast<float>(content.top),
                                  static_cast<float>(content.width), static_cast<float>(content.height), 0.0f, 1.0f};
    if (post_.ready()) {
        post_.record(ctx, picture, width, height, target, viewport);
        reset_draw_bindings();
        return;
    }
    // The plain blit: the picture's own rectangle (its texture may be larger) stretched over the content rectangle.
    (void)width;
    (void)height;
    utility_draw(Utility::Blit, picture.view.Get(), nullptr, 0u, viewport,
                 gpu::rect(content.left, content.top, content.left + content.width, content.top + content.height));
}

bool RendererD3D11::present(const DisplayTexture* source, bool capture_backbuffer, bool black) {
    const GpuPassScope scope{*this, GpuPass::Present};
    present_black_ = black;
    if (source == nullptr || !source->texture) {
        throw std::runtime_error("present requested without a valid display picture blit source");
    }
    end_scene_pass();
    surface_lock_ = std::unique_lock<std::timed_mutex>(host_window::surface_use);
    if (swap_dirty_ || !swap_chain_ || !backbuffer_rtv_ || host_window::surface_withdrawn.load(std::memory_order_acquire)) {
        surface_lock_ = {};
        return false;  // followed at the next frame boundary
    }
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const auto content_aspect =
        galaxy::presentation_content_aspect(aspect ? aspect->content_aspect : galaxy::kDesignContentAspect, source->prerecorded_movie);
    const int bb_w = static_cast<int>(swap_width_);
    const int bb_h = static_cast<int>(swap_height_);
    galaxy::ContentViewport content = galaxy::fit_content_viewport(bb_w, bb_h, content_aspect);
    if (content.width <= 0 || content.height <= 0) {
        content = galaxy::ContentViewport{0, 0, bb_w, bb_h};
    }
    (void)galaxy::g_presentation_content.publish(bb_w, bb_h, content);
    draw_presented_picture(backbuffer_rtv_.Get(), *source->texture, source->width, source->height, content);
    // The swap chain's buffer is not bound past here: the flip model takes it at present.
    context()->OMSetRenderTargets(0u, nullptr, nullptr);
    presented_ = true;
    host_window::native_settings_present();
    reset_draw_bindings();
    // A test capture of the presented picture: the buffer as drawn, copied before the present takes it.
    if (capture_backbuffer && !present_black_ && !backbuffer_capture_path_.empty() && backbuffer_capture_pending_path_.empty()) {
        d3d11::ComPtr<ID3D11Texture2D> buffer;
        D3D11_TEXTURE2D_DESC desc{};
        if (SUCCEEDED(swap_chain_->GetBuffer(0u, IID_PPV_ARGS(&buffer)))) {
            buffer->GetDesc(&desc);
            desc.BindFlags = 0u;
            desc.MiscFlags = 0u;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            D3D11_TEXTURE2D_DESC had{};
            if (backbuffer_capture_.staging) {
                backbuffer_capture_.staging->GetDesc(&had);
            }
            if (!backbuffer_capture_.staging || had.Width != desc.Width || had.Height != desc.Height) {
                backbuffer_capture_.staging.Reset();
                (void)d3d11::Context::get().device()->CreateTexture2D(&desc, nullptr, &backbuffer_capture_.staging);
            }
            if (backbuffer_capture_.staging) {
                context()->CopyResource(backbuffer_capture_.staging.Get(), buffer.Get());
                backbuffer_capture_.fence = 0u;
                backbuffer_capture_pending_path_ = backbuffer_capture_path_;
                backbuffer_capture_width_ = desc.Width;
                backbuffer_capture_height_ = desc.Height;
                return true;
            }
        }
    }
    return false;
}

}  // namespace galaxy::render
