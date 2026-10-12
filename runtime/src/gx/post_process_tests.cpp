// plan_post_process, the AA status texts, and the start-up self-test on the
// WARP software device (no GPU or window needed).
#include "galaxy/gx/post_process.h"

#include <dxgi1_4.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

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
    expect_text(anti_aliasing_row_text(2, 0, live(Aa::kAuto, 0, AaRun::kSmaa)), "ANTI-ALIASING: AUTO - SMAA LOW");
    PostProcessStatus ss = live(Aa::kAuto, 2, AaRun::kNone, false);
    ss.last_supersampled = true;
    expect_text(anti_aliasing_row_text(2, 2, ss), "ANTI-ALIASING: AUTO - OFF, SUPERSAMPLED");
    expect_text(anti_aliasing_row_text(0, 2, live(Aa::kOff, 2, AaRun::kNone, false)), "ANTI-ALIASING: OFF");
    expect_text(anti_aliasing_row_text(3, 2, live(Aa::kFxaa, 2, AaRun::kFxaa)), "ANTI-ALIASING: FXAA");
    PostProcessStatus fallback = live(Aa::kSmaa, 2, AaRun::kFxaa);
    fallback.smaa_ok = false;
    std::snprintf(fallback.smaa_reason.data(), fallback.smaa_reason.size(), "SELF-TEST");
    expect_text(anti_aliasing_row_text(1, 2, fallback), "ANTI-ALIASING: SMAA - NOT RUNNING: SELF-TEST, FXAA ON");
    PostProcessStatus none = live(Aa::kSmaa, 2, AaRun::kNone);
    std::snprintf(none.skip_reason.data(), none.skip_reason.size(), "NO MEMORY");
    expect_text(anti_aliasing_row_text(1, 2, none), "ANTI-ALIASING: SMAA - NOT RUNNING: NO MEMORY");
    // An unapplied overlay change shows the choice alone.
    expect_text(anti_aliasing_row_text(3, 2, live(Aa::kSmaa, 2, AaRun::kSmaa)), "ANTI-ALIASING: FXAA");
    expect_text(anti_aliasing_row_text(1, 0, live(Aa::kSmaa, 2, AaRun::kSmaa)), "ANTI-ALIASING: SMAA");
    PostProcessStatus off{};
    std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "INIT FAILED");
    expect_text(anti_aliasing_row_text(1, 2, off), "ANTI-ALIASING: SMAA - NOT RUNNING: INIT FAILED");
    expect_text(post_process_caps_text(off), "POST-PROCESS: OFF (INIT FAILED)");
    expect_text(post_process_caps_text(live(Aa::kSmaa, 2, AaRun::kSmaa)), "POST-PROCESS: SMAA PASS  FXAA PASS");
    expect_text(post_process_caps_text(fallback), "POST-PROCESS: SMAA FAIL (SELF-TEST)  FXAA PASS");
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
    galaxy::gx::PostProcessor nothing;
    expect(!nothing.initialize(nullptr, nullptr, PostProcessSettings{}), "no device: initialize fails");
    expect(!nothing.status().initialized && !nothing.status().smaa_ok, "no device: nothing available");
}

}  // namespace

int main() {
    plans();
    texts();
    self_test_on_warp();
    if (failures != 0) {
        std::cerr << failures << " post-process checks failed\n";
        return 1;
    }
    std::cout << "post-process checks passed\n";
    return 0;
}
