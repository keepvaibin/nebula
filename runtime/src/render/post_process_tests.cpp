// plan_post_process, the AA status texts, the start-up self-test and the scene
// anti-aliasing on the WARP software device (no GPU or window needed).
#include "galaxy/render/post_process.h"
#include "galaxy/render/renderer_d3d12.h"

#include <dxgi1_4.h>

#include "galaxy/render/render_config.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using galaxy::render::AaRun;
using galaxy::render::PostProcessPlan;
using galaxy::render::PostProcessSettings;
using galaxy::render::PostProcessStatus;
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

bool close_to(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

// HDR10: the PQ curve, the paper white, the roll-off to the display's peak, the colour primaries and the constants the
// shader reads (post_process.hlsl FinalWriteHdr is these formulas; the SDR write is unchanged).
void hdr_output() {
    using namespace galaxy::render;
    expect(close_to(pq_encode(0.0f), 0.0f, 1e-5f), "pq of black");
    expect(close_to(pq_encode(10000.0f), 1.0f, 1e-5f), "pq of the top of the range");
    expect(close_to(pq_encode(100.0f), 0.5081f, 1e-3f), "pq of 100 nits");
    expect(close_to(pq_encode(1000.0f), 0.7518f, 1e-3f), "pq of 1000 nits");
    for (const float nits : {0.1f, 1.0f, 100.0f, 203.0f, 1000.0f, 4000.0f}) {
        expect(close_to(pq_decode(pq_encode(nits)) / nits, 1.0f, 2e-3f), "pq round trip");
    }
    float code[3]{};
    const float white[3] = {1.0f, 1.0f, 1.0f};
    hdr10_encode(white, 200u, 1000u, code);
    expect(close_to(code[0], pq_encode(200.0f), 2e-3f) && close_to(code[1], code[0], 2e-3f) && close_to(code[2], code[0], 2e-3f),
           "SDR white lands on the paper white, neutral");
    hdr10_encode(white, 400u, 1000u, code);
    expect(close_to(code[0], pq_encode(400.0f), 2e-3f), "a higher paper white is brighter");
    const float black[3] = {0.0f, 0.0f, 0.0f};
    hdr10_encode(black, 200u, 1000u, code);
    expect(close_to(code[0], 0.0f, 1e-5f) && close_to(code[1], 0.0f, 1e-5f) && close_to(code[2], 0.0f, 1e-5f), "black stays black");
    const float red[3] = {1.0f, 0.0f, 0.0f};
    hdr10_encode(red, 200u, 1000u, code);
    expect(code[0] > code[1] && code[1] > code[2], "sRGB red in BT.2020: red leads, green next");
    // Highlights above the paper white (bloom): brighter than white, inside the peak, never past it.
    const float bright[3] = {2.0f, 2.0f, 2.0f};
    hdr10_encode(bright, 200u, 1000u, code);
    expect(code[0] > pq_encode(200.0f) && pq_decode(code[0]) <= 1001.0f, "highlights rise above the paper white");
    // A display under the source range: the curve ends at its peak, rising all the way up.
    const float huge[3] = {4.0f, 4.0f, 4.0f};
    float last = 0.0f;
    bool rising = true;
    for (const float level : {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f}) {
        const float picture[3] = {level, level, level};
        hdr10_encode(picture, 200u, 600u, code);
        const float nits = pq_decode(code[0]);
        rising = rising && nits >= last - 0.5f && nits <= 600.5f;
        last = nits;
    }
    expect(rising, "roll-off to a 600 nit display rises and never passes the peak");
    hdr10_encode(huge, 200u, 600u, code);
    expect(close_to(pq_decode(code[0]), 600.0f, 2.0f), "the top of the source range reaches the peak");
    // A hue-preserving roll-off: the channels keep their ratios.
    const float tint[3] = {3.0f, 1.5f, 0.75f};
    hdr10_encode(tint, 200u, 600u, code);
    const float ratio = pq_decode(code[1]) / pq_decode(code[0]);
    float untouched[3]{};
    hdr10_encode(tint, 200u, 10000u, untouched);
    expect(close_to(ratio, pq_decode(untouched[1]) / pq_decode(untouched[0]), 2e-3f), "roll-off keeps the hue");

    // The levels and the constants word.
    RenderConfig cfg{};
    cfg.hdr = true;
    cfg.paper_white = 10u;
    DisplayHdr display{};
    display.available = true;
    PostOutput levels = post_output_for(cfg, display);
    expect(levels.hdr && levels.paper_white_nits == kPaperWhiteMin && levels.peak_nits == 1000u, "paper white floor, 1000 nit default peak");
    cfg.paper_white = 9999u;
    display.peak_nits = 20000u;
    levels = post_output_for(cfg, display);
    expect(levels.paper_white_nits == kPaperWhiteMax && levels.peak_nits == 10000u, "paper white ceiling, PQ range");
    display.peak_nits = 120u;
    cfg.paper_white = 300u;
    levels = post_output_for(cfg, display);
    expect(levels.peak_nits == 300u, "the peak stays at or above the paper white");
    levels.paper_white_nits = 200u;
    levels.peak_nits = 1000u;
    const std::int32_t word = post_output_word(levels);
    expect((word & 1023) == 200 && ((word >> 10) & 16383) == 1000, "the constants word holds paper white and peak");
    PostConstants constants{};
    set_post_output(constants, levels);
    expect(constants.origin[2] == 2 && constants.origin[3] == word, "HDR last write constants");
    set_post_output(constants, PostOutput{});
    expect(constants.origin[2] == 1 && constants.origin[3] == 0, "SDR last write constants are the window's, as before HDR");
    // HDR is wanted only with the setting and a display that offers it.
    cfg.hdr = false;
    expect(!hdr_output_wanted(cfg, display), "HDR off in the settings");
    cfg.hdr = true;
    display.available = false;
    expect(!hdr_output_wanted(cfg, display), "no HDR display");
    display.available = true;
    expect(hdr_output_wanted(cfg, display), "HDR on and offered");
    // The static metadata: BT.2020 primaries, the display's range.
    display.peak_nits = 800u;
    display.full_frame_nits = 600u;
    display.min_nits_10000 = 50u;
    const HdrMetadata metadata = hdr10_metadata(display, 200u);
    expect(close_to(metadata.red[0], 0.708f, 1e-4f) && close_to(metadata.blue[1], 0.046f, 1e-4f) && close_to(metadata.white[0], 0.3127f, 1e-4f),
           "BT.2020 primaries");
    expect(close_to(metadata.max_luminance, 800.0f, 0.5f) && close_to(metadata.min_luminance, 0.005f, 1e-5f) &&
               close_to(metadata.max_frame_average_light, 600.0f, 0.5f),
           "metadata luminances");
}

void plans() {
    using galaxy::render::plan_post_process;
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
    expect(p.supersampled && !p.aa_wanted && !p.smaa && p.resample && p.resample_to_target && !p.copy, "auto skips supersampled");
    p = plan_post_process(3840, 2736, 2560, 1440, with(Aa::kSmaa));
    expect(p.smaa && p.resample, "forced smaa on a supersampled image");
    // 6x into 3840x2160: 1.0x horizontally, so not supersampled.
    p = plan_post_process(3840, 2736, 3840, 2160, with(Aa::kAuto));
    expect(!p.supersampled && p.smaa && !p.easu && p.resample, "6x into 4K is not supersampled");
    // Same size: RCAS alone writes the window; without RCAS the copy does.
    p = plan_post_process(1280, 720, 1280, 720, with(Aa::kOff, 20));
    expect(p.rcas && !p.resample && !p.easu && !p.copy, "same size + rcas");
    p = plan_post_process(1280, 720, 1280, 720, with(Aa::kOff, 0));
    expect(!p.rcas && p.resample && p.resample_to_target && p.copy, "same size copy");
    // Enlarging with RCAS: EASU into the intermediate, RCAS to the window.
    p = plan_post_process(640, 456, 1920, 1080, with(Aa::kOff, 20));
    expect(p.easu && p.rcas && !p.resample, "easu then rcas");
    PostProcessSettings bilinear = with(Aa::kOff, 0);
    bilinear.upscaler = PostProcessSettings::Upscaler::kBilinear;
    p = plan_post_process(640, 456, 1920, 1080, bilinear);
    expect(!p.easu && p.resample && p.resample_to_target && !p.copy, "bilinear enlargement");
    p = plan_post_process(1280, 720, 1280, 720, bilinear);
    expect(!p.easu && p.resample && p.resample_to_target && p.copy, "same size copy with bilinear");
    p = plan_post_process(1280, 720, 1280, 721, bilinear);
    expect(p.resample_to_target && !p.copy, "one row taller is a resample");
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
    using galaxy::render::anti_aliasing_row_text;
    using galaxy::render::post_process_caps_text;
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

// The start's rules for a pass that cannot run, and the self-test's judge on pictures that must fail.
void fallbacks() {
    using galaxy::render::aa_availability;
    const auto all = aa_availability(true, true, true);
    expect(all.smaa_ok && all.fxaa_ok && all.smaa_reason[0] == '\0' && all.fxaa_reason[0] == '\0', "both passes");
    const auto no_smaa = aa_availability(false, true, true);
    expect(!no_smaa.smaa_ok && no_smaa.fxaa_ok && std::strcmp(no_smaa.smaa_reason, "PIPELINES") == 0,
           "smaa pipelines missing: reported, fxaa kept");
    const auto no_lookup = aa_availability(true, false, true);
    expect(!no_lookup.smaa_ok && std::strcmp(no_lookup.smaa_reason, "LOOKUP") == 0, "lookup textures missing");
    const auto none = aa_availability(false, true, false);
    expect(!none.smaa_ok && !none.fxaa_ok && std::strcmp(none.fxaa_reason, "PIPELINES") == 0, "no aa pipelines");
    using galaxy::render::kPostSelfTestSize;
    constexpr unsigned kPitch = kPostSelfTestSize * 4u;
    std::array<std::uint8_t, kPitch * kPostSelfTestSize> pattern{};
    galaxy::render::write_post_self_test_pattern(pattern.data(), kPitch);
    expect(!galaxy::render::check_post_self_test(pattern.data(), pattern.data(), kPitch).passed,
           "a pass that changes nothing fails the self-test");
    std::array<std::uint8_t, kPitch * kPostSelfTestSize> stray = pattern;
    for (unsigned y = 0; y < kPostSelfTestSize; ++y) {
        for (unsigned x = 0; x < kPostSelfTestSize; ++x) {
            std::uint8_t* p = stray.data() + y * kPitch + x * 4u;
            p[0] = p[1] = p[2] = static_cast<std::uint8_t>(255u - p[0]);
        }
    }
    expect(!galaxy::render::check_post_self_test(pattern.data(), stray.data(), kPitch).passed,
           "a pass that changes flat areas fails the self-test");
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
    galaxy::render::PostProcessor post;
    expect(post.initialize(device.Get(), queue.Get(), PostProcessSettings{}), "initialize on WARP");
    const PostProcessStatus& s = post.status();
    expect(s.initialized, "status initialized");
    expect(s.smaa_ok, "smaa self-test passes");
    expect(s.fxaa_ok, "fxaa self-test passes");
    expect(device->GetDeviceRemovedReason() == S_OK, "device alive after self-test");
    galaxy::render::PostProcessor nothing;
    expect(!nothing.initialize(nullptr, nullptr, PostProcessSettings{}), "no device: initialize fails");
    expect(!nothing.status().initialized && !nothing.status().smaa_ok, "no device: nothing available");
}

// The scene passes on a 64x64 scene target-like target with a stair-stepped edge and an alpha ramp:
// they change colour near the edge and never the alpha; with the scene's image traded to them (RendererD3D12's
// swap_scene_color) the image that takes its place ends as the copy's scene target does, every bit.
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
            // The scene's format: ten bits a colour, two of alpha.
            const std::uint32_t v = y < 20u + x / 4u ? 1023u : 0u;
            const std::uint32_t texel = v | v << 10u | v << 20u | (x * 4u / 64u) << 30u;
            std::memcpy(&pattern[y * kPitch + x * 4u], &texel, sizeof(texel));
        }
    }
    const auto run = [&](Aa aa, unsigned display, AaRun& ran, bool trade = false) {
        std::array<unsigned char, kPitch * kSize> out{};
        PostProcessSettings settings{};
        settings.aa = aa;
        galaxy::render::PostProcessor post;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Fence> fence;
        ComPtr<ID3D12DescriptorHeap> rtv_heap;
        ComPtr<ID3D12Resource> target, other, upload, readback;
        galaxy::render::DescriptorRing ring;
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
        td.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
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
            FAILED(device->CreateCommittedResource(&def, D3D12_HEAP_FLAG_NONE, &td,
                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                   IID_PPV_ARGS(&other))) ||
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
        buffer.PlacedFootprint.Footprint = {DXGI_FORMAT_R10G10B10A2_UNORM, kSize, kSize, 1, kPitch};
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
        ring.bind(list.Get());
        galaxy::render::PostProcessor::SceneTrade traded;
        if (trade) {
            // The scene's image goes to the passes shader-readable; the scene target's view takes the other image.
            traded = [&](ComPtr<ID3D12Resource>) {
                b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                list->ResourceBarrier(1, &b);
                device->CreateRenderTargetView(other.Get(), nullptr, rtv);
                ComPtr<ID3D12Resource> scene = std::move(target);
                target = other;
                return scene;
            };
        }
        ran = post.record_scene_aa(list.Get(), ring, target.Get(), rtv, kSize, kSize, kSize, kSize, display,
                                   display, 1u, 1u, 1u, traded);
        b.Transition.pResource = target.Get();
        texture.pResource = target.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &b);
        buffer.pResource = readback.Get();
        list->CopyTextureRegion(&buffer, 0, 0, 0, &texture, nullptr);
        ring.flush();
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
        unsigned columns = 0u;
        for (unsigned x = 0; x < kSize; ++x) {
            bool column = false;
            for (unsigned y = 0; y < kSize; ++y) {
                const unsigned i = y * kPitch + x * 4u;
                std::uint32_t got = 0u;
                std::uint32_t was = 0u;
                std::memcpy(&got, &out[i], sizeof(got));
                std::memcpy(&was, &pattern[i], sizeof(was));
                alpha_kept = alpha_kept && got >> 30u == was >> 30u;
                if ((got & 0x3FFFFFFFu) != (was & 0x3FFFFFFFu)) {
                    ++changed;
                    column = true;
                }
            }
            columns += column;
        }
        std::string what = std::string(name) + " runs on the scene";
        expect(ran == want, what.c_str());
        what = std::string(name) + " keeps the alpha the game left";
        expect(alpha_kept, what.c_str());
        what = std::string(name) + " smooths the edge";
        expect(changed >= 8u, what.c_str());
        if (want == AaRun::kFxaa) {
            // The edge runs along the rows, a step down every four columns: FXAA blends across it in every column,
            // not only at the steps.
            what = std::string(name) + " blends along the whole edge";
            expect(columns == kSize, what.c_str());
        }
        AaRun traded_ran = AaRun::kNone;
        const auto traded = run(aa, kSize, traded_ran, true);
        what = std::string(name) + " on the traded image ends as on the copy";
        expect(traded_ran == want && traded == out, what.c_str());
    };
    check(Aa::kSmaa, AaRun::kSmaa, "SMAA");
    check(Aa::kFxaa, AaRun::kFxaa, "FXAA");
    AaRun ran = AaRun::kSmaa;
    const auto out = run(Aa::kAuto, kSize / 2u, ran);
    expect(ran == AaRun::kNone && out == pattern, "AUTO leaves a supersampled scene alone");
    expect(device->GetDeviceRemovedReason() == S_OK, "device alive after the scene passes");
}

}  // namespace

int main() {
    hdr_output();
    plans();
    texts();
    fallbacks();
    self_test_on_warp();
    scene_aa_on_warp();
    if (failures != 0) {
        std::cerr << failures << " post-process checks failed\n";
        return 1;
    }
    std::cout << "post-process checks passed\n";
    return 0;
}
