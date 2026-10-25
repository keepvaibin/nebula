// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Presentation post-processing as the settings and the overlay see it, for every renderer: the settings, the plan a
// frame runs, the passes' constants and start-up self-test, the status the overlay reports and its menu text. The
// chains are post_process.h (D3D12) and vk/post_process_vk.h (Vulkan), both running post_process.hlsl.

#include <array>
#include <cstdint>
#include <string>

namespace galaxy::gx {

struct RenderConfig;

struct PostProcessSettings {
    enum class Aa : std::uint8_t { kOff = 0, kSmaa = 1, kAuto = 2, kFxaa = 3 };
    enum class Upscaler : std::uint8_t { kBilinear = 0, kFsr1 = 1 };
    Aa aa = Aa::kAuto;
    std::uint8_t smaa_quality = 2;  // 0 low, 1 medium, 2 high, 3 ultra
    Upscaler upscaler = Upscaler::kFsr1;
    std::uint8_t sharpness = 20;
};

// config.ini's aa, smaa_quality, upscaler (bilinear or FSR 1; the temporal upscalers are not this chain's) and
// sharpness.
[[nodiscard]] PostProcessSettings post_process_settings_from(const RenderConfig& cfg) noexcept;

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

// post_process.hlsl's PostConstants: root constants on D3D12, push constants on Vulkan.
struct PostConstants {
    float rt_metrics[4];  // 1/w, 1/h, w, h of the pass input
    std::uint32_t con0[4];
    std::uint32_t con1[4];
    std::uint32_t con2[4];
    std::uint32_t con3[4];
    float params[4];         // resample: source size, target size
    std::int32_t origin[4];  // the pass viewport's origin in the target
};
static_assert(sizeof(PostConstants) == 28u * 4u);

// rt_metrics for a pass reading a `width` x `height` input.
void set_post_input_size(PostConstants& constants, unsigned width, unsigned height) noexcept;
// FsrEasuCon with the input viewport equal to the input resource.
void set_fsr_easu_constants(PostConstants& constants, float in_width, float in_height, float out_width,
                            float out_height) noexcept;
// FsrRcasCon: sharpness 0-100 (100 strongest).
void set_fsr_rcas_constants(PostConstants& constants, unsigned sharpness_percent) noexcept;
// The deband pass (post_process.hlsl PASS_DEBAND) on a picture `height` lines tall: its reach (48 pixels at 1080
// lines) and threshold (1.5 levels of 255).
void set_deband_constants(PostConstants& constants, unsigned height) noexcept;
// The deband pass runs (GALAXY_DEBAND=0: off, for comparisons).
[[nodiscard]] bool deband_wanted() noexcept;

// The AA start-up self-test: a 32 x 32 white-over-black edge stepping down one row every four columns, the jagged
// case both passes exist for. A working pass blends a few dozen pixels along the steps and leaves flat areas alone.
inline constexpr unsigned kPostSelfTestSize = 32u;
// The pattern as RGBA8 rows `pitch` bytes apart.
void write_post_self_test_pattern(std::uint8_t* rgba, unsigned pitch) noexcept;
struct PostSelfTestResult {
    unsigned changed = 0;  // pixels near the edge the pass changed
    unsigned stray = 0;    // pixels away from it the pass changed
    bool passed = false;
};
[[nodiscard]] PostSelfTestResult check_post_self_test(const std::uint8_t* pattern, const std::uint8_t* result,
                                                      unsigned pitch) noexcept;

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

// Whether AMD FidelityFX FSR 3.1 is compiled in (temporal_upscaler.cpp; D3D12 only).
[[nodiscard]] bool fsr3_available() noexcept;

}  // namespace galaxy::gx
