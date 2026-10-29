// plan_post_process, the AA status texts, the start-up self-test and the scene
// anti-aliasing on the WARP software device (no GPU or window needed).
#include "galaxy/gx/post_process.h"
#include "galaxy/gx/renderer_d3d12.h"

#include <dxgi1_4.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using galaxy::gx::AaRun;
using galaxy::gx::PostProcessPlan;
using galaxy::gx::PostProcessSettings;
using galaxy::gx::PostProcessStatus;
using Aa = PostProcessSettings::Aa;

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::cerr << "FAILED: " << what << '\n';
        ++failures;
    }
}

void expect_text(const std::string& got, const char* want) {
    if (got != want) {
        std::cerr << "FAILED: \"" << got << "\" != \"" << want << "\"\n";
        ++failures;
    }
}

PostProcessSettings with(Aa aa, unsigned sharpness = 0u) {
    PostProcessSettings s{};
    s.aa = aa;
    s.sharpness = static_cast<std::uint8_t>(sharpness);
    return s;
}

void plans() {
    using galaxy::gx::plan_post_process;
    // 1x game image (640x456) into an 854x480 window: enlarged, not supersampled.
    PostProcessPlan p = plan_post_process(640, 456, 854, 480, with(Aa::kSmaa));
    expect(p.aa_wanted && p.smaa && !p.fxaa && p.easu && !p.resample && !p.rcas, "smaa + easu at 1x");
    p = plan_post_process(640, 456, 854, 480, with(Aa::kAuto));
    expect(p.smaa && !p.supersampled, "auto runs smaa below 1.5x");
    p = plan_post_process(640, 456, 854, 480, with(Aa::kOff));
    expect(!p.aa_wanted && !p.smaa && !p.fxaa, "off runs no aa");
    p = plan_post_process(640, 456, 854, 480, with(Aa::kFxaa));
    expect(p.aa_wanted && p.fxaa && !p.smaa, "fxaa selected");
    // SMAA unavailable: FXAA replaces it; both unavailable: wanted, nothing runs.
    p = plan_post_process(640, 456, 854, 480, with(Aa::kSmaa), false, true);
    expect(p.aa_wanted && !p.smaa && p.fxaa, "fxaa replaces a failed smaa");
    p = plan_post_process(640, 456, 854, 480, with(Aa::kAuto), false, false);
    expect(p.aa_wanted && !p.smaa && !p.fxaa, "no aa pass when both failed");
    p = plan_post_process(640, 456, 854, 480, with(Aa::kFxaa), true, false);
    expect(p.aa_wanted && !p.fxaa && !p.smaa, "a failed fxaa is not replaced by smaa");
    // 6x (3840x2736) into 2560x1440: supersampled on both axes.
    p = plan_post_process(3840, 2736, 2560, 1440, with(Aa::kAuto));
    expect(p.supersampled && !p.aa_wanted && !p.smaa && p.resample && p.resample_to_target, "auto skips supersampled");
    p = plan_post_process(3840, 2736, 2560, 1440, with(Aa::kSmaa));
    expect(p.smaa && p.resample, "forced smaa on a supersampled image");
    // 6x into 3840x2160: 1.0x horizontally, so not supersampled.
    p = plan_post_process(3840, 2736, 3840, 2160, with(Aa::kAuto));
    expect(!p.supersampled && p.smaa && !p.easu && p.resample, "6x into 4K is not supersampled");
    // Same size: RCAS alone writes the window; without RCAS the copy does.
    p = plan_post_process(1280, 720, 1280, 720, with(Aa::kOff, 20));
    expect(p.rcas && !p.resample && !p.easu, "same size + rcas");
    p = plan_post_process(1280, 720, 1280, 720, with(Aa::kOff, 0));
    expect(!p.rcas && p.resample && p.resample_to_target, "same size copy");
    // Enlarging with RCAS: EASU into the intermediate, RCAS to the window.
    p = plan_post_process(640, 456, 1920, 1080, with(Aa::kOff, 20));
    expect(p.easu && p.rcas && !p.resample, "easu then rcas");
    PostProcessSettings bilinear = with(Aa::kOff, 0);
    bilinear.upscaler = PostProcessSettings::Upscaler::kBilinear;
    p = plan_post_process(640, 456, 1920, 1080, bilinear);
    expect(!p.easu && p.resample && p.resample_to_target, "bilinear enlargement");
    p = plan_post_process(0, 456, 1920, 1080, with(Aa::kSmaa));
    expect(!p.aa_wanted && !p.smaa && !p.resample, "empty source draws nothing");
}

PostProcessStatus live(Aa aa, unsigned quality, AaRun ran, bool wanted = true) {
    PostProcessStatus s{};
    s.initialized = true;
    s.smaa_ok = true;
    s.fxaa_ok = true;
    s.live_aa = static_cast<std::uint8_t>(aa);
    s.live_quality = static_cast<std::uint8_t>(quality);
    s.last_run = ran;
    s.last_wanted = wanted;
    s.frames = 10;
    return s;
}

void texts() {
    using galaxy::gx::anti_aliasing_row_text;
    using galaxy::gx::post_process_caps_text;
    expect_text(anti_aliasing_row_text(1, 3, live(Aa::kSmaa, 3, AaRun::kSmaa)), "ANTI-ALIASING: SMAA ULTRA");
    expect_text(anti_aliasing_row_text(2, 0, live(Aa::kAuto, 0, AaRun::kSmaa)), "ANTI-ALIASING: AUTO (SMAA LOW)");
    PostProcessStatus ss = live(Aa::kAuto, 2, AaRun::kNone, false);
    ss.last_supersampled = true;
    expect_text(anti_aliasing_row_text(2, 2, ss), "ANTI-ALIASING: AUTO (SUPERSAMPLED)");
    expect_text(anti_aliasing_row_text(0, 2, live(Aa::kOff, 2, AaRun::kNone, false)), "ANTI-ALIASING: OFF");
    expect_text(anti_aliasing_row_text(3, 2, live(Aa::kFxaa, 2, AaRun::kFxaa)), "ANTI-ALIASING: FXAA");
    PostProcessStatus fallback = live(Aa::kSmaa, 2, AaRun::kFxaa);
    fallback.smaa_ok = false;
    std::snprintf(fallback.smaa_reason.data(), fallback.smaa_reason.size(), "SELF-TEST");
    expect_text(anti_aliasing_row_text(1, 2, fallback), "ANTI-ALIASING: FXAA (SMAA: SELF-TEST)");
    PostProcessStatus none = live(Aa::kSmaa, 2, AaRun::kNone);
    std::snprintf(none.skip_reason.data(), none.skip_reason.size(), "NO MEMORY");
    expect_text(anti_aliasing_row_text(1, 2, none), "ANTI-ALIASING: NOT RUNNING (NO MEMORY)");
    // An unapplied overlay change shows the choice alone.
    expect_text(anti_aliasing_row_text(3, 2, live(Aa::kSmaa, 2, AaRun::kSmaa)), "ANTI-ALIASING: FXAA");
    expect_text(anti_aliasing_row_text(1, 0, live(Aa::kSmaa, 2, AaRun::kSmaa)), "ANTI-ALIASING: SMAA");
    PostProcessStatus off{};
    std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "INIT FAILED");
    expect_text(anti_aliasing_row_text(1, 2, off), "ANTI-ALIASING: NOT RUNNING (INIT FAILED)");
    expect_text(post_process_caps_text(off), "POST-PROCESS: OFF (INIT FAILED)");
    expect_text(post_process_caps_text(live(Aa::kSmaa, 2, AaRun::kSmaa)), "POST-PROCESS: SMAA PASS, FXAA PASS");
    expect_text(post_process_caps_text(fallback), "POST-PROCESS: SMAA FAIL, FXAA PASS");
    PostProcessStatus auto_fxaa = fallback;
    auto_fxaa.live_aa = static_cast<std::uint8_t>(Aa::kAuto);
    expect_text(anti_aliasing_row_text(2, 2, auto_fxaa), "ANTI-ALIASING: AUTO (FXAA)");
    // Every row fits the overlay panel at text scale 4 (39 characters).
    PostProcessStatus worst = fallback;
    std::snprintf(worst.smaa_reason.data(), worst.smaa_reason.size(), "DEVICE LOST");
    expect(anti_aliasing_row_text(1, 2, worst).size() <= 39u, "fallback row fits the panel");
    std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "FAILED");
    expect(anti_aliasing_row_text(2, 2, off).size() <= 39u, "not-running row fits the panel");
}

void self_test_on_warp() {
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qd{};
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) ||
        FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
        std::cerr << "FAILED: WARP device\n";
        ++failures;
        return;
    }
    galaxy::gx::PostProcessor post;
    expect(post.initialize(device.Get(), queue.Get(), PostProcessSettings{}), "initialize on WARP");
    const PostProcessStatus& s = post.status();
    expect(s.initialized, "status initialized");
    expect(s.smaa_ok, "smaa self-test passes");
    expect(s.fxaa_ok, "fxaa self-test passes");
    expect(device->GetDeviceRemovedReason() == S_OK, "device alive after self-test");
    // Injected failures: each AA pass is replaced or reported, the chain stays up.
    _putenv_s("GALAXY_TEST_POST_FAULT", "selftest");
    galaxy::gx::PostProcessor no_smaa;
    expect(no_smaa.initialize(device.Get(), queue.Get(), PostProcessSettings{}), "initialize with a failing smaa self-test");
    expect(!no_smaa.status().smaa_ok && no_smaa.status().fxaa_ok &&
               std::strcmp(no_smaa.status().smaa_reason.data(), "SELF-TEST") == 0,
           "smaa self-test failure reported, fxaa kept");
    _putenv_s("GALAXY_TEST_POST_FAULT", "smaa,fxaa");
    galaxy::gx::PostProcessor no_aa;
    expect(no_aa.initialize(device.Get(), queue.Get(), PostProcessSettings{}), "scaling survives failed aa pipelines");
    expect(!no_aa.status().smaa_ok && !no_aa.status().fxaa_ok &&
               std::strcmp(no_aa.status().smaa_reason.data(), "PIPELINES") == 0,
           "aa pipeline failures reported");
    _putenv_s("GALAXY_TEST_POST_FAULT", "");
    galaxy::gx::PostProcessor nothing;
    expect(!nothing.initialize(nullptr, nullptr, PostProcessSettings{}), "no device: initialize fails");
    expect(!nothing.status().initialized && !nothing.status().smaa_ok, "no device: nothing available");
}

// The scene passes on a 64x64 EFB-like target with a stair-stepped edge and an alpha ramp:
// they change colour near the edge and never the alpha.
void scene_aa_on_warp() {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qd{};
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) ||
        FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
        std::cerr << "FAILED: WARP device\n";
        ++failures;
        return;
    }
    constexpr unsigned kSize = 64u;
    constexpr unsigned kPitch = kSize * 4u;
    std::array<unsigned char, kPitch * kSize> pattern{};
    for (unsigned y = 0; y < kSize; ++y) {
        for (unsigned x = 0; x < kSize; ++x) {
            unsigned char* px = &pattern[y * kPitch + x * 4u];
            const unsigned char v = y < 20u + x / 4u ? 255u : 0u;
            px[0] = px[1] = px[2] = v;
            px[3] = static_cast<unsigned char>(x * 4u);
        }
    }
    const auto run = [&](Aa aa, unsigned display, AaRun& ran) {
        std::array<unsigned char, kPitch * kSize> out{};
        PostProcessSettings settings{};
        settings.aa = aa;
        galaxy::gx::PostProcessor post;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Fence> fence;
        ComPtr<ID3D12DescriptorHeap> rtv_heap;
        ComPtr<ID3D12Resource> target, upload, readback;
        galaxy::gx::DescriptorRing ring;
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 1;
        D3D12_HEAP_PROPERTIES def{}, up{}, rb{};
        def.Type = D3D12_HEAP_TYPE_DEFAULT;
        up.Type = D3D12_HEAP_TYPE_UPLOAD;
        rb.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = kSize;
        td.Height = kSize;
        td.DepthOrArraySize = 1;
        td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = pattern.size();
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        unsigned char* mapped = nullptr;
        if (!post.initialize(device.Get(), queue.Get(), settings) ||
            FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
            FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&list))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
            FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap))) ||
            !ring.initialize(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64u, 1u) ||
            FAILED(device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr, IID_PPV_ARGS(&target))) ||
            FAILED(device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(&upload))) ||
            FAILED(device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr, IID_PPV_ARGS(&readback))) ||
            FAILED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) {
            std::cerr << "FAILED: scene AA test setup\n";
            ++failures;
            return out;
        }
        std::memcpy(mapped, pattern.data(), pattern.size());
        upload->Unmap(0, nullptr);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target.Get(), nullptr, rtv);
        ring.begin_frame(0);
        D3D12_TEXTURE_COPY_LOCATION buffer{};
        buffer.pResource = upload.Get();
        buffer.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        buffer.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, kSize, kSize, 1, kPitch};
        D3D12_TEXTURE_COPY_LOCATION texture{};
        texture.pResource = target.Get();
        texture.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&texture, 0, 0, 0, &buffer, nullptr);
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = target.Get();
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1, &b);
        ID3D12DescriptorHeap* heaps[] = {ring.heap()};
        list->SetDescriptorHeaps(1, heaps);
        ran = post.record_scene_aa(list.Get(), ring, target.Get(), rtv, kSize, kSize, kSize, kSize, display,
                                   display, 1u, 1u);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &b);
        buffer.pResource = readback.Get();
        list->CopyTextureRegion(&buffer, 0, 0, 0, &texture, nullptr);
        list->Close();
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(fence.Get(), 1);
        HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        fence->SetEventOnCompletion(1, done);
        WaitForSingleObject(done, INFINITE);
        CloseHandle(done);
        readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memcpy(out.data(), mapped, out.size());
        readback->Unmap(0, nullptr);
        return out;
    };
    const auto check = [&](Aa aa, AaRun want, const char* name) {
        AaRun ran = AaRun::kNone;
        const auto out = run(aa, kSize, ran);
        bool alpha_kept = true;
        unsigned changed = 0u;
        for (unsigned y = 0; y < kSize; ++y) {
            for (unsigned x = 0; x < kSize; ++x) {
                const unsigned i = y * kPitch + x * 4u;
                alpha_kept = alpha_kept && out[i + 3u] == pattern[i + 3u];
                const bool same = out[i] == pattern[i] && out[i + 1u] == pattern[i + 1u] &&
                                  out[i + 2u] == pattern[i + 2u];
                if (!same) {
                    ++changed;
                }
            }
        }
        std::string what = std::string(name) + " runs on the scene";
        expect(ran == want, what.c_str());
        what = std::string(name) + " keeps the alpha the game left";
        expect(alpha_kept, what.c_str());
        what = std::string(name) + " smooths the edge";
        expect(changed >= 8u, what.c_str());
    };
    check(Aa::kSmaa, AaRun::kSmaa, "SMAA");
    check(Aa::kFxaa, AaRun::kFxaa, "FXAA");
    AaRun ran = AaRun::kSmaa;
    const auto out = run(Aa::kAuto, kSize / 2u, ran);
    expect(ran == AaRun::kNone && out == pattern, "AUTO leaves a supersampled scene alone");
    expect(device->GetDeviceRemovedReason() == S_OK, "device alive after the scene passes");
}

// A picture held from the top left of a taller texture (a handed-over EFB) presents bit for bit as the same picture
// in a texture its own size, through every scaling path and window size. Run once debanded and once without
// (--held-no-deband), since debanding reads the picture first and the passes after it read its own-sized output.
void held_picture_on_warp() {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qd{};
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) ||
        FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
        std::cerr << "FAILED: WARP device\n";
        ++failures;
        return;
    }
    constexpr unsigned kWidth = 64u;
    constexpr unsigned kHeight = 40u;
    constexpr unsigned kHeldHeight = 52u;
    constexpr unsigned kPitch = kWidth * 4u;
    // Gradients with a little noise (debanding and EASU both act on them), different rows below the picture.
    std::vector<unsigned char> held(kPitch * kHeldHeight);
    std::uint32_t seed = 0x2545f491u;
    for (unsigned y = 0; y < kHeldHeight; ++y) {
        for (unsigned x = 0; x < kWidth; ++x) {
            seed = seed * 1664525u + 1013904223u;
            unsigned char* px = &held[y * kPitch + x * 4u];
            const unsigned noise = (seed >> 24) & 3u;
            px[0] = static_cast<unsigned char>(y < kHeight ? x * 2u + noise : seed >> 8);
            px[1] = static_cast<unsigned char>(y < kHeight ? y * 3u + noise : seed >> 16);
            px[2] = static_cast<unsigned char>(y < kHeight ? ((x / 8u + y / 8u) & 1u) * 200u + noise : seed);
            px[3] = 255u;
        }
    }
    struct Window {
        unsigned width;
        unsigned height;
    };
    const Window windows[] = {{128u, 80u}, {200u, 125u}, {64u, 40u}, {32u, 20u}, {48u, 30u}, {96u, 30u}, {40u, 64u}};
    struct Setting {
        PostProcessSettings::Upscaler upscaler;
        unsigned sharpness;
        const char* name;
    };
    const Setting settings_list[] = {
        {PostProcessSettings::Upscaler::kBilinear, 0u, "bilinear"},
        {PostProcessSettings::Upscaler::kBilinear, 20u, "bilinear sharpened"},
        {PostProcessSettings::Upscaler::kFsr1, 0u, "FSR 1 unsharpened"},
        {PostProcessSettings::Upscaler::kFsr1, 20u, "FSR 1"},
    };
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 1;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&list))) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) ||
        FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtv_heap)))) {
        std::cerr << "FAILED: held picture test setup\n";
        ++failures;
        return;
    }
    list->Close();
    std::uint64_t fence_value = 0u;
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const auto execute = [&] {
        list->Close();
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(fence.Get(), ++fence_value);
        fence->SetEventOnCompletion(fence_value, done);
        WaitForSingleObject(done, INFINITE);
    };
    D3D12_HEAP_PROPERTIES def{}, up{}, rb{};
    def.Type = D3D12_HEAP_TYPE_DEFAULT;
    up.Type = D3D12_HEAP_TYPE_UPLOAD;
    rb.Type = D3D12_HEAP_TYPE_READBACK;
    const auto texture_desc = [](unsigned width, unsigned height, D3D12_RESOURCE_FLAGS flags) {
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = width;
        td.Height = height;
        td.DepthOrArraySize = 1;
        td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Flags = flags;
        return td;
    };
    const auto buffer_desc = [](std::size_t bytes) {
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = bytes;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return bd;
    };
    const auto barrier = [&](ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = resource;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        list->ResourceBarrier(1, &b);
    };
    // The picture in a texture its own size and in a taller one, both shader resources.
    ComPtr<ID3D12Resource> sources[2];
    ComPtr<ID3D12Resource> upload;
    const D3D12_RESOURCE_DESC upload_desc = buffer_desc(held.size());
    unsigned char* mapped = nullptr;
    bool ready = SUCCEEDED(device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &upload_desc,
                                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                           IID_PPV_ARGS(&upload))) &&
                 SUCCEEDED(upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped)));
    if (ready) {
        std::memcpy(mapped, held.data(), held.size());
        upload->Unmap(0, nullptr);
        allocator->Reset();
        list->Reset(allocator.Get(), nullptr);
        for (unsigned i = 0; i < 2u && ready; ++i) {
            const unsigned height = i == 0u ? kHeight : kHeldHeight;
            const D3D12_RESOURCE_DESC td = texture_desc(kWidth, height, D3D12_RESOURCE_FLAG_NONE);
            ready = SUCCEEDED(device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td,
                                                              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                              IID_PPV_ARGS(&sources[i])));
            if (ready) {
                D3D12_TEXTURE_COPY_LOCATION buffer{};
                buffer.pResource = upload.Get();
                buffer.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                buffer.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, kWidth, height, 1, kPitch};
                D3D12_TEXTURE_COPY_LOCATION texture{};
                texture.pResource = sources[i].Get();
                texture.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                list->CopyTextureRegion(&texture, 0, 0, 0, &buffer, nullptr);
                barrier(sources[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
        }
        execute();
    }
    if (!ready) {
        std::cerr << "FAILED: held picture sources\n";
        ++failures;
        CloseHandle(done);
        return;
    }
    unsigned compared = 0u;
    for (const Setting& setting : settings_list) {
        PostProcessSettings settings{};
        settings.aa = Aa::kOff;
        settings.upscaler = setting.upscaler;
        settings.sharpness = static_cast<std::uint8_t>(setting.sharpness);
        galaxy::gx::PostProcessor post;
        galaxy::gx::DescriptorRing ring;
        if (!post.initialize(device.Get(), queue.Get(), settings) ||
            !ring.initialize(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64u, 1u)) {
            std::cerr << "FAILED: held picture post-process (" << setting.name << ")\n";
            ++failures;
            continue;
        }
        expect(post.reads_held_pictures(), "the held-picture passes are built");
        std::uint64_t frame = 0u;
        for (const Window& window : windows) {
            const unsigned pitch = (window.width * 4u + 255u) & ~255u;
            std::vector<unsigned char> outputs[2];
            for (unsigned i = 0; i < 2u; ++i) {
                ComPtr<ID3D12Resource> target, readback;
                const D3D12_RESOURCE_DESC td =
                    texture_desc(window.width, window.height, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
                const D3D12_RESOURCE_DESC bd = buffer_desc(static_cast<std::size_t>(pitch) * window.height);
                if (FAILED(device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td,
                                                           D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                           IID_PPV_ARGS(&target))) ||
                    FAILED(device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &bd,
                                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                           IID_PPV_ARGS(&readback)))) {
                    std::cerr << "FAILED: held picture target\n";
                    ++failures;
                    break;
                }
                const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
                device->CreateRenderTargetView(target.Get(), nullptr, rtv);
                allocator->Reset();
                list->Reset(allocator.Get(), nullptr);
                ring.begin_frame(0);
                ID3D12DescriptorHeap* heaps[] = {ring.heap()};
                list->SetDescriptorHeaps(1, heaps);
                const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(window.width),
                                              static_cast<float>(window.height), 0.0f, 1.0f};
                const D3D12_RECT scissor{0, 0, static_cast<LONG>(window.width), static_cast<LONG>(window.height)};
                list->RSSetScissorRects(1, &scissor);
                post.record(list.Get(), ring, sources[i].Get(), kWidth, kHeight, rtv, viewport, ++frame, 1u);
                barrier(target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION buffer{};
                buffer.pResource = readback.Get();
                buffer.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                buffer.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, window.width, window.height, 1,
                                                    pitch};
                D3D12_TEXTURE_COPY_LOCATION texture{};
                texture.pResource = target.Get();
                texture.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                list->CopyTextureRegion(&buffer, 0, 0, 0, &texture, nullptr);
                execute();
                readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
                outputs[i].resize(static_cast<std::size_t>(window.width) * 4u * window.height);
                for (unsigned y = 0; y < window.height; ++y) {
                    std::memcpy(&outputs[i][static_cast<std::size_t>(y) * window.width * 4u],
                                mapped + static_cast<std::size_t>(y) * pitch, window.width * 4u);
                }
                readback->Unmap(0, nullptr);
            }
            if (outputs[0].empty() || outputs[0].size() != outputs[1].size()) {
                continue;
            }
            std::size_t differing = 0u;
            for (std::size_t i = 0; i < outputs[0].size(); ++i) {
                differing += outputs[0][i] != outputs[1][i] ? 1u : 0u;
            }
            ++compared;
            if (differing != 0u) {
                std::cerr << "FAILED: held picture, " << setting.name << " into " << window.width << 'x'
                          << window.height << ": " << differing << " of " << outputs[0].size()
                          << " bytes differ from the picture in its own texture\n";
                ++failures;
            }
        }
    }
    CloseHandle(done);
    expect(compared == std::size(settings_list) * std::size(windows), "every held picture compared");
    expect(device->GetDeviceRemovedReason() == S_OK, "device alive after the held pictures");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--held-no-deband") == 0) {
        _putenv_s("GALAXY_DEBAND", "0");
        held_picture_on_warp();
        if (failures != 0) {
            std::cerr << failures << " held picture checks failed\n";
            return 1;
        }
        std::cout << "held picture checks passed\n";
        return 0;
    }
    plans();
    texts();
    self_test_on_warp();
    scene_aa_on_warp();
    held_picture_on_warp();
    if (failures != 0) {
        std::cerr << failures << " post-process checks failed\n";
        return 1;
    }
    std::cout << "post-process checks passed\n";
    return 0;
}
