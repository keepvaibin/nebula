// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Presentation post-processing as the settings and the overlay see it, for every renderer: the settings, the plan a
// frame runs, the status the overlay reports and its menu text. The D3D12 chain is post_process.h.

#include <array>
#include <cstdint>
#include <string>

namespace galaxy::gx {

struct PostProcessSettings {
    enum class Aa : std::uint8_t { kOff = 0, kSmaa = 1, kAuto = 2, kFxaa = 3 };
    enum class Upscaler : std::uint8_t { kBilinear = 0, kFsr1 = 1 };
    Aa aa = Aa::kAuto;
    std::uint8_t smaa_quality = 2;  // 0 low, 1 medium, 2 high, 3 ultra
    Upscaler upscaler = Upscaler::kFsr1;
    std::uint8_t sharpness = 20;
};

// Which passes one frame runs, decided from sizes and settings alone so the
// choice is testable without a device.
struct PostProcessPlan {
    bool aa_wanted = false; // the settings ask for an AA pass this frame
    bool supersampled = false;
    bool smaa = false;
    bool fxaa = false;
    bool easu = false;     // EASU into the window-sized intermediate
    bool resample = false; // box filter (or identity copy) step
    bool rcas = false;
    // The resample writes the window directly unless RCAS follows it.
    bool resample_to_target = false;
};

// smaa_available / fxaa_available: the pass passed its start-up self-test.
[[nodiscard]] PostProcessPlan plan_post_process(
    unsigned source_width, unsigned source_height,
    unsigned target_width, unsigned target_height,
    const PostProcessSettings& settings,
    bool smaa_available = true, bool fxaa_available = true) noexcept;

enum class AaRun : std::uint8_t { kNone = 0, kSmaa = 1, kFxaa = 2 };

// What the presentation chain can do and did last frame; published by the
// renderer for the settings overlay and the logs.
struct PostProcessStatus {
    bool initialized = false;  // the chain exists (otherwise the plain blit)
    bool smaa_ok = false;      // pipelines, lookup textures and self-test
    bool fxaa_ok = false;
    std::array<char, 40> smaa_reason{};  // why SMAA is unavailable
    std::array<char, 40> fxaa_reason{};
    std::array<char, 40> skip_reason{};  // why last frame's wanted AA did not run
    AaRun last_run = AaRun::kNone;
    bool last_wanted = false;
    bool last_supersampled = false;
    std::uint8_t live_aa = 0;       // PostProcessSettings::Aa the chain uses
    std::uint8_t live_quality = 0;
    std::uint64_t frames = 0;       // totals since start
    std::uint64_t aa_wanted_frames = 0;
    std::uint64_t smaa_frames = 0;
    std::uint64_t fxaa_frames = 0;
    std::uint64_t aa_skipped_frames = 0;  // wanted, no AA pass ran
};

// Overlay row text. draft_aa is the overlay's unapplied value: the live state
// is shown only while it matches what the chain runs.
[[nodiscard]] std::string anti_aliasing_row_text(
    unsigned draft_aa, unsigned draft_quality, const PostProcessStatus& status);
[[nodiscard]] std::string post_process_caps_text(const PostProcessStatus& status);

// Thread-safe copy for readers off the render thread.
[[nodiscard]] PostProcessStatus get_post_process_status();
void set_post_process_status(const PostProcessStatus& status);

// AMD FidelityFX FSR 3.1 is built into this runtime (temporal_upscaler.cpp; the D3D12 renderer).
[[nodiscard]] bool fsr3_available() noexcept;

}  // namespace galaxy::gx
