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
    // That write at the picture's own size: each pixel its texel (the copy pass, without the box filter's arithmetic).
    bool copy = false;
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
    // 8-bit window (dithered), 2 the last write as HDR10 (set_post_output). origin[3]: with 2, the levels
    // (post_output_word).
    std::int32_t origin[4];
    // turn[0]: the target turned by 0-3 quarter turns clockwise (a pre-rotated Vulkan swap chain image), so the passes
    // turn its pixels back to the picture's; turn[1..2]: the picture's width and height (every last write's, set_post_output).
    std::int32_t turn[4];
};
// 128 bytes: Vulkan's least push constant size.
static_assert(sizeof(PostConstants) == 32u * 4u);

// rt_metrics for a pass reading a `width` x `height` input.
void set_post_input_size(PostConstants& constants, unsigned width, unsigned height) noexcept;
// The AA passes: the part of the input (from its top left) that holds the picture, when that is less than all of it
// (dynamic resolution); their reads stay in it. After set_post_input_size.
void set_post_input_part(PostConstants& constants, unsigned width, unsigned height) noexcept;
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
// The scene's light past white (with HDR output the scene is kept in floating point: additive light past white, the
// enhanced lighting's highlights) up to this many times the paper white, in linear light, is the source range the
// roll-off fits into the display's: kept as it is on a display with that range, rolled into the peak on one without.
// Above it, the peak.
inline constexpr float kHdrHeadroom = 8.0f;
[[nodiscard]] unsigned clamp_paper_white(unsigned nits) noexcept;

// The display's levels as the HDR calibration finds them (ui/hdr_calibration.h; RenderConfig hdr_peak, hdr_black), by the
// three steps of the HDR Gaming Interest Group's guidelines: the peak, where a pattern at the test level vanishes into a
// surround at the top of the PQ range (the display clips both alike); the black, the highest level a pattern on true black
// still vanishes at; the paper white over the game's own picture.
inline constexpr unsigned kHdrPeakMin = 100u;
inline constexpr unsigned kHdrPeakMax = 10000u;
// The peak's levels, from kHdrPeakMin to kHdrPeakMax: 25 nits apart to 1000, then 50 to 2000, 100 to 4000, 250 above.
inline constexpr unsigned kHdrPeakSteps = 100u;  // the last step's number
[[nodiscard]] constexpr unsigned hdr_peak_at(unsigned step) noexcept {
    // Each band: from its start, steps of its spacing up to the next band's start.
    constexpr unsigned kStarts[] = {100u, 1000u, 2000u, 4000u, 10000u};
    constexpr unsigned kSpacings[] = {25u, 50u, 100u, 250u};
    for (unsigned band = 0u; band < 4u; ++band) {
        const unsigned count = (kStarts[band + 1u] - kStarts[band]) / kSpacings[band];
        if (step < count) {
            return kStarts[band] + step * kSpacings[band];
        }
        step -= count;
    }
    return kHdrPeakMax;
}
static_assert(hdr_peak_at(0u) == kHdrPeakMin && hdr_peak_at(kHdrPeakSteps) == kHdrPeakMax &&
              hdr_peak_at(kHdrPeakSteps - 1u) == kHdrPeakMax - 250u);
// The step whose level is nearest `nits` (the lower of two as near).
[[nodiscard]] constexpr unsigned hdr_peak_step(unsigned nits) noexcept {
    unsigned best = 0u;
    unsigned best_off = hdr_peak_at(0u) > nits ? hdr_peak_at(0u) - nits : nits - hdr_peak_at(0u);
    for (unsigned step = 1u; step <= kHdrPeakSteps; ++step) {
        const unsigned at = hdr_peak_at(step);
        const unsigned off = at > nits ? at - nits : nits - at;
        if (off < best_off) {
            best = step;
            best_off = off;
        }
    }
    return best;
}
// A setting's peak on one of the steps (0 stays 0: the display's report).
[[nodiscard]] constexpr unsigned normalized_hdr_peak(unsigned nits) noexcept {
    return nits == 0u ? 0u : hdr_peak_at(hdr_peak_step(nits));
}
// The black's levels: step s is 2 * (s / kHdrBlackSteps)^2 nits, fine where a panel's black sits (an OLED's 0, an LCD's
// 0.02 to 0.3, an edge-lit LCD's up to 2). The calibration starts at kHdrBlackStart (about 0.5 nits, the star plainly seen
// on nearly every panel), in the middle of its slider: darker to the left down to 0, brighter to the right.
inline constexpr unsigned kHdrBlackSteps = 127u;
inline constexpr float kHdrBlackMostNits = 2.0f;
inline constexpr unsigned kHdrBlackStart = 64u;
[[nodiscard]] constexpr float hdr_black_nits(unsigned step) noexcept {
    const float share = static_cast<float>(step < kHdrBlackSteps ? step : kHdrBlackSteps) / static_cast<float>(kHdrBlackSteps);
    return kHdrBlackMostNits * share * share;
}
// The calibration's test box, drawn by the last write in the picture's pixels: a square kHdrBoxSide of the picture's
// shorter side, centred across the picture, its middle kHdrBoxCentreY of the picture's height from its top. The game's
// window goes below it (NEBULA_HDR_BOX_SIDE, NEBULA_HDR_BOX_CENTRE_Y). Inside, a five-pointed star at the pattern's level
// on the surround's: the symbol that just vanishes into the surround at the level sought (HGIG).
inline constexpr float kHdrBoxSide = 0.34f;
inline constexpr float kHdrBoxCentreY = 0.30f;
// The star's outer radius as a share of the box's side, and how far its middle sits below the box's: its top point
// reaches the radius, its two feet cos 36 degrees of it, so it is lowered by half the difference to sit evenly.
inline constexpr float kHdrStarRadius = 0.40f;
inline constexpr float kHdrStarDrop = 0.0954915f;  // (1 - cos 36) / 2, of the radius
struct HdrBox {
    int left = 0;
    int top = 0;
    int side = 0;
};
[[nodiscard]] constexpr HdrBox hdr_box(int width, int height) noexcept {
    HdrBox box;
    box.side = static_cast<int>(kHdrBoxSide * static_cast<float>(width < height ? width : height) + 0.5f);
    box.left = (width - box.side) / 2;
    box.top = static_cast<int>(kHdrBoxCentreY * static_cast<float>(height) + 0.5f) - box.side / 2;
    return box;
}
// Whether (x, y) is inside a regular five-pointed star whose points reach 1 from its middle, one straight up (y up).
[[nodiscard]] bool hdr_star(float x, float y) noexcept;
enum class HdrPattern : unsigned {
    None = 0,
    Peak = 1,   // surround at 10000 nits, the pattern at the peak
    Black = 2,  // surround at 0, the pattern at the black level
    // A capture's view of the light past white (capture_output with `highlights`; never on the display): the picture in
    // grey, each pixel past the paper white coloured by how far past.
    Highlights = 3,
};
// The calibration's live levels while it is up (any thread): the last write takes them over the settings', with its
// pattern.
struct HdrCalibration {
    bool active = false;
    HdrPattern pattern = HdrPattern::None;
    unsigned peak_nits = 1000u;
    unsigned black = 0u;  // a step of hdr_black_nits
    unsigned paper_white_nits = kPaperWhiteDefault;
};
void set_hdr_calibration(const HdrCalibration& calibration) noexcept;
[[nodiscard]] HdrCalibration hdr_calibration() noexcept;

struct PostOutput {
    bool hdr = false;
    unsigned paper_white_nits = kPaperWhiteDefault;
    unsigned peak_nits = 1000u;
    unsigned black = 0u;  // a step of hdr_black_nits: the display's darkest visible level, which the picture's black lifts to
    HdrPattern pattern = HdrPattern::None;
    friend bool operator==(const PostOutput&, const PostOutput&) = default;
};
// What the display reports and what the renderer made of it: published by the renderer for the settings page and the logs.
struct DisplayHdr {
    bool available = false;        // the display is in HDR mode and this renderer can write HDR10 to it
    bool switchable = false;       // the display takes HDR but Windows has it off: the game turns it on (os_hdr.h)
    bool active = false;           // the swap chain is HDR10 now
    bool held_sdr = false;         // HDR is on in the settings but frame generation keeps the picture SDR
    unsigned peak_nits = 0;        // the output's MaxLuminance (0: not reported)
    unsigned full_frame_nits = 0;  // MaxFullFrameLuminance
    unsigned min_nits_10000 = 0;   // MinLuminance in units of 0.0001 nit
};
[[nodiscard]] DisplayHdr get_display_hdr();
void set_display_hdr(const DisplayHdr& display);
// HDR on in the settings and a display that takes it.
[[nodiscard]] bool hdr_output_wanted(const RenderConfig& cfg, const DisplayHdr& display) noexcept;
// The levels the last write encodes for: the paper white clamped to its range; the peak the calibration found, else the
// display's report (1000 nits when it reports none), kept above the paper white and below the PQ range; the black. While
// the HDR calibration is up, its levels and pattern.
[[nodiscard]] PostOutput post_output_for(const RenderConfig& cfg, const DisplayHdr& display) noexcept;
// origin[3] for an HDR write: paper white in bits 0-8, peak in bits 9-22, black in bits 23-29, the pattern in 30-31.
[[nodiscard]] std::int32_t post_output_word(const PostOutput& output) noexcept;
// A capture of an HDR picture: its SDR picture drawn again, with the calibration's test box as its HDR10 signal's levels
// in grey (a star at 325 nits a mid grey in the white box, at 0.5 nits a dark grey on black), so the picture shows it.
// `highlights` (an HDR picture without a test box): the capture shows its light past white (HdrPattern::Highlights) instead.
[[nodiscard]] PostOutput capture_output(const PostOutput& shown, bool highlights = false) noexcept;
// Sets origin[2] and origin[3] for the chain's last write (the SDR window write for output.hdr false; its origin[3] is the
// word only for a capture's test box), and turn[1..2] the picture's size in it (its pixels from the pass viewport's origin).
void set_post_output(PostConstants& constants, const PostOutput& output, unsigned picture_width,
                     unsigned picture_height) noexcept;
// SMPTE ST 2084 (PQ): luminance in nits <-> code value 0..1.
[[nodiscard]] float pq_encode(float nits) noexcept;
[[nodiscard]] float pq_decode(float code) noexcept;
// `picture` (display-referred, 1.0 = the SDR white) as the PQ code values the HDR write stores; `black_nits` the
// display's darkest visible level (BT.2390's black level lift).
void hdr10_encode(const float picture[3], unsigned paper_white_nits, unsigned peak_nits, float code[3],
                  float black_nits = 0.0f) noexcept;
// The test pattern's code value at the picture's pixel (x, y) of a `width` x `height` picture: the pattern's or the
// surround's inside the box, negative outside it (the picture shows there).
[[nodiscard]] float hdr_pattern_code(const PostOutput& output, int x, int y, int width, int height) noexcept;
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
// From the levels in use: the calibrated peak and black where set, the display's own figures otherwise; while a test
// pattern shows, the whole PQ range (the surround is 10000 nits).
[[nodiscard]] HdrMetadata hdr10_metadata(const DisplayHdr& display, const PostOutput& levels) noexcept;

// The scene's words (10 bits a channel, alpha 2) from and to RGBA8 rows.
void rgba8_to_ten_bit(const std::uint8_t* rgba, std::uint8_t* words, unsigned width, unsigned height,
                      unsigned pitch) noexcept;
void ten_bit_to_rgba8(const std::uint8_t* words, std::uint8_t* rgba, unsigned width, unsigned height,
                      unsigned word_pitch, unsigned rgba_pitch) noexcept;
// The wide scene's RGBA16F rows as the 10-bit scene's words: each channel round(clamp(v, 0, 1) * 1023), the
// alpha round(a * 3), what the 10-bit target would have held; its readbacks then read as the 10-bit target's.
void half4_to_ten_bit(const std::uint8_t* halves, std::uint8_t* words, unsigned width, unsigned height,
                      unsigned half_pitch, unsigned word_pitch) noexcept;

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
