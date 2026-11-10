// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Presentation post-processing as the settings and the overlay see it, for every renderer: the settings, the plan a
// frame runs, the passes' constants and start-up self-test, the status the overlay reports and its menu text. The
// chains are post_process.h (D3D12) and vk/post_process_vk.h (Vulkan), both running post_process.hlsl.

#include <array>
#include <cstdint>
#include <string>

namespace galaxy::render {

struct RenderConfig;

struct PostProcessSettings {
    enum class Aa : std::uint8_t { kOff = 0, kSmaa = 1, kAuto = 2, kFxaa = 3 };
    // kMetalFx: the Metal renderer's (MetalFX's spatial scaler where FSR 1's EASU pass would run).
    enum class Upscaler : std::uint8_t { kBilinear = 0, kFsr1 = 1, kMetalFx = 2 };
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
    // origin[0..1]: the pass viewport's origin in the target. origin[2]: 0 an intermediate picture, 1 the last write into an
    // 8-bit window (dithered), 2 the last write as HDR10 (set_post_output). origin[3]: with 2, the paper white and the
    // display's peak (post_output_word).
    std::int32_t origin[4];
};
static_assert(sizeof(PostConstants) == 28u * 4u);

// rt_metrics for a pass reading a `width` x `height` input.
void set_post_input_size(PostConstants& constants, unsigned width, unsigned height) noexcept;
// FsrEasuCon with the input viewport equal to the input resource.
void set_fsr_easu_constants(PostConstants& constants, float in_width, float in_height, float out_width,
                            float out_height) noexcept;
// FsrRcasCon: sharpness 0-100 (100 strongest).
void set_fsr_rcas_constants(PostConstants& constants, unsigned sharpness_percent) noexcept;
// HDR10 output. The last write of the chain encodes the display-referred picture either for an 8-bit window (the SDR path,
// untouched by HDR) or, with HDR on and a display in HDR mode, as HDR10: the picture's gamma 2.2 decoded, BT.709 to
// BT.2020, scaled so that 1.0 is the paper white, highlights above it rolled off to the display's peak by the BT.2390 curve,
// PQ encoded and dithered to 10 bits. post_process.hlsl's FinalWriteHdr does that per pixel; hdr10_encode is the same on
// the CPU (tests, and the proof of what the shader computes).
inline constexpr unsigned kPaperWhiteMin = 80u;
inline constexpr unsigned kPaperWhiteMax = 500u;
inline constexpr unsigned kPaperWhiteDefault = 200u;
// Scene values up to this many times the paper white keep their level on a display with the range for it; above it they
// are clipped to the peak (the shader's roll-off takes this as the source's range).
inline constexpr float kHdrHeadroom = 4.0f;
[[nodiscard]] unsigned clamp_paper_white(unsigned nits) noexcept;

struct PostOutput {
    bool hdr = false;
    unsigned paper_white_nits = kPaperWhiteDefault;
    unsigned peak_nits = 1000u;
};
// What the display reports and what the renderer made of it: published by the renderer for the settings page and the logs.
struct DisplayHdr {
    bool available = false;        // the display is in HDR mode and this renderer can write HDR10 to it
    bool active = false;           // the swap chain is HDR10 now
    unsigned peak_nits = 0;        // the output's MaxLuminance (0: not reported)
    unsigned full_frame_nits = 0;  // MaxFullFrameLuminance
    unsigned min_nits_10000 = 0;   // MinLuminance in units of 0.0001 nit
};
[[nodiscard]] DisplayHdr get_display_hdr();
void set_display_hdr(const DisplayHdr& display);
// HDR on in the settings and a display that takes it.
[[nodiscard]] bool hdr_output_wanted(const RenderConfig& cfg, const DisplayHdr& display) noexcept;
// The levels the last write encodes for: the paper white clamped to its range, the display's peak (1000 nits when it
// reports none) kept above the paper white and below the PQ range.
[[nodiscard]] PostOutput post_output_for(const RenderConfig& cfg, const DisplayHdr& display) noexcept;
// origin[3] for an HDR write: paper white in bits 0-9, peak in bits 10-23.
[[nodiscard]] std::int32_t post_output_word(const PostOutput& output) noexcept;
// Sets origin[2] and origin[3] for the chain's last write (the SDR window write for output.hdr false).
void set_post_output(PostConstants& constants, const PostOutput& output) noexcept;
// SMPTE ST 2084 (PQ): luminance in nits <-> code value 0..1.
[[nodiscard]] float pq_encode(float nits) noexcept;
[[nodiscard]] float pq_decode(float code) noexcept;
// `picture` (display-referred, 1.0 = the SDR white) as the PQ code values the HDR write stores.
void hdr10_encode(const float picture[3], unsigned paper_white_nits, unsigned peak_nits, float code[3]) noexcept;
// The static metadata for the display's figures (BT.2020 primaries, D65): luminances in nits.
struct HdrMetadata {
    float red[2];
    float green[2];
    float blue[2];
    float white[2];
    float max_luminance;
    float min_luminance;
    float max_content_light;
    float max_frame_average_light;
};
[[nodiscard]] HdrMetadata hdr10_metadata(const DisplayHdr& display, unsigned paper_white_nits) noexcept;

// The scene's words (10 bits a channel, alpha 2) from and to RGBA8 rows.
void rgba8_to_ten_bit(const std::uint8_t* rgba, std::uint8_t* words, unsigned width, unsigned height,
                      unsigned pitch) noexcept;
void ten_bit_to_rgba8(const std::uint8_t* words, std::uint8_t* rgba, unsigned width, unsigned height,
                      unsigned word_pitch, unsigned rgba_pitch) noexcept;

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

// What the AA passes can do after the start: SMAA needs its pipelines and lookup textures, FXAA its pipeline. A pass
// that cannot run has its reason for the settings overlay ("" when it can).
struct AaAvailability {
    bool smaa_ok = false;
    bool fxaa_ok = false;
    const char* smaa_reason = "";
    const char* fxaa_reason = "";
};
[[nodiscard]] AaAvailability aa_availability(bool smaa_pipelines, bool lookup_textures, bool fxaa_pipeline) noexcept;

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
// Whether this renderer runs Snapdragon Game Super Resolution 2 (upscaler 5; the Vulkan renderer's sgsr2_vk.cpp).
[[nodiscard]] bool sgsr2_available() noexcept;
// Whether this renderer and device run Apple's MetalFX (upscaler 6; the Metal renderer's metalfx_mtl.mm), and whether
// they run its temporal scaler (else the spatial one enlarges the picture). False before there is a device.
[[nodiscard]] bool metalfx_available() noexcept;
[[nodiscard]] bool metalfx_temporal_available() noexcept;

}  // namespace galaxy::render
