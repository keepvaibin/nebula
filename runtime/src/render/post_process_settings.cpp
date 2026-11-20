// SPDX-License-Identifier: GPL-3.0-only
// Post-processing settings, pass planning, FSR 1 constants, the AA self-test and the overlay's status text.
#if defined(NEBULA_GPU_VULKAN)
#include "galaxy/render/vk/vk_context.h"
#elif defined(NEBULA_GPU_METAL)
#include "galaxy/render/mtl/mtl_context.h"
#endif
#include "galaxy/render/post_process_settings.h"
#include "galaxy/render/render_config.h"

#include <cstring>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace galaxy::render {

PostProcessSettings post_process_settings_from(const RenderConfig& cfg) noexcept {
    PostProcessSettings post{};
    post.aa = static_cast<PostProcessSettings::Aa>(std::min(cfg.aa_mode, 3u));
    post.smaa_quality = static_cast<std::uint8_t>(std::min(cfg.smaa_quality, 3u));
    post.upscaler = cfg.upscaler == 6u && metalfx_available()
        ? PostProcessSettings::Upscaler::kMetalFx
        : static_cast<PostProcessSettings::Upscaler>(std::min(cfg.upscaler, 1u));
    post.sharpness = static_cast<std::uint8_t>(std::min(cfg.sharpness, 100u));
    return post;
}

void set_post_input_size(PostConstants& constants, unsigned width, unsigned height) noexcept {
    constants.rt_metrics[0] = 1.0f / static_cast<float>(width);
    constants.rt_metrics[1] = 1.0f / static_cast<float>(height);
    constants.rt_metrics[2] = static_cast<float>(width);
    constants.rt_metrics[3] = static_cast<float>(height);
}

void set_post_input_part(PostConstants& constants, unsigned width, unsigned height) noexcept {
    const bool whole = width == 0u || height == 0u ||
                       (static_cast<float>(width) >= constants.rt_metrics[2] && static_cast<float>(height) >= constants.rt_metrics[3]);
    constants.params[0] = whole ? 0.0f : static_cast<float>(width);
    constants.params[1] = whole ? 0.0f : static_cast<float>(height);
}

void set_fsr_easu_constants(PostConstants& c, float in_w, float in_h, float out_w, float out_h) noexcept {
    const auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };
    c.con0[0] = bits(in_w / out_w);
    c.con0[1] = bits(in_h / out_h);
    c.con0[2] = bits(0.5f * in_w / out_w - 0.5f);
    c.con0[3] = bits(0.5f * in_h / out_h - 0.5f);
    c.con1[0] = bits(1.0f / in_w);
    c.con1[1] = bits(1.0f / in_h);
    c.con1[2] = bits(1.0f / in_w);
    c.con1[3] = bits(-1.0f / in_h);
    c.con2[0] = bits(-1.0f / in_w);
    c.con2[1] = bits(2.0f / in_h);
    c.con2[2] = bits(1.0f / in_w);
    c.con2[3] = bits(2.0f / in_h);
    c.con3[0] = bits(0.0f);
    c.con3[1] = bits(4.0f / in_h);
    c.con3[2] = 0u;
    c.con3[3] = 0u;
}

// Sharpness in stops, 0 = strongest; the 32-bit path reads con.x.
void set_fsr_rcas_constants(PostConstants& c, unsigned sharpness_percent) noexcept {
    const float stops = 2.0f * (1.0f - static_cast<float>(std::min(sharpness_percent, 100u)) / 100.0f);
    c.con0[0] = std::bit_cast<std::uint32_t>(std::exp2(-stops));
    c.con0[1] = 0u;
    c.con0[2] = 0u;
    c.con0[3] = 0u;
}

namespace {

unsigned self_test_edge_row(unsigned x) noexcept {
    return 10u + x / 4u;
}

}  // namespace

void write_post_self_test_pattern(std::uint8_t* rgba, unsigned pitch) noexcept {
    for (unsigned y = 0; y < kPostSelfTestSize; ++y) {
        for (unsigned x = 0; x < kPostSelfTestSize; ++x) {
            std::uint8_t* p = rgba + static_cast<std::size_t>(y) * pitch + x * 4u;
            const std::uint8_t v = y < self_test_edge_row(x) ? 255u : 0u;
            p[0] = p[1] = p[2] = v;
            p[3] = 255u;
        }
    }
}

PostSelfTestResult check_post_self_test(const std::uint8_t* pattern, const std::uint8_t* result,
                                        unsigned pitch) noexcept {
    PostSelfTestResult out{};
    for (unsigned y = 0; y < kPostSelfTestSize; ++y) {
        for (unsigned x = 0; x < kPostSelfTestSize; ++x) {
            const std::uint8_t* a = pattern + static_cast<std::size_t>(y) * pitch + x * 4u;
            const std::uint8_t* c = result + static_cast<std::size_t>(y) * pitch + x * 4u;
            int delta = 0;
            for (unsigned k = 0; k < 3u; ++k) delta = std::max(delta, std::abs(int(a[k]) - int(c[k])));
            const int distance = std::abs(int(y) - int(self_test_edge_row(x)));
            if (delta > 24) ++out.changed;
            if (distance >= 3 && delta > 4) ++out.stray;
        }
    }
    out.passed = out.changed >= 8u && out.stray == 0u;
    return out;
}

AaAvailability aa_availability(bool smaa_pipelines, bool lookup_textures, bool fxaa_pipeline) noexcept {
    AaAvailability out{};
    out.smaa_ok = smaa_pipelines && lookup_textures;
    out.fxaa_ok = fxaa_pipeline;
    out.smaa_reason = !smaa_pipelines ? "PIPELINES" : !lookup_textures ? "LOOKUP" : "";
    out.fxaa_reason = fxaa_pipeline ? "" : "PIPELINES";
    return out;
}

PostProcessPlan plan_post_process(
    unsigned source_width, unsigned source_height,
    unsigned target_width, unsigned target_height,
    const PostProcessSettings& settings,
    bool smaa_available, bool fxaa_available) noexcept {
    PostProcessPlan plan{};
    if (source_width == 0u || source_height == 0u ||
        target_width == 0u || target_height == 0u) {
        return plan;
    }
    // Supersampled: at least 1.5 source pixels per window pixel on both axes.
    plan.supersampled =
        source_width * 2u >= target_width * 3u &&
        source_height * 2u >= target_height * 3u;
    const bool smaa_wanted = settings.aa == PostProcessSettings::Aa::kSmaa ||
        (settings.aa == PostProcessSettings::Aa::kAuto && !plan.supersampled);
    plan.aa_wanted = smaa_wanted || settings.aa == PostProcessSettings::Aa::kFxaa;
    plan.smaa = smaa_wanted && smaa_available;
    plan.fxaa = plan.aa_wanted && !plan.smaa && fxaa_available;
    const bool same_size =
        source_width == target_width && source_height == target_height;
    const bool enlarging = target_width >= source_width &&
        target_height >= source_height && !same_size;
    plan.easu = enlarging &&
        (settings.upscaler == PostProcessSettings::Upscaler::kFsr1 ||
         settings.upscaler == PostProcessSettings::Upscaler::kMetalFx);
    plan.rcas = settings.sharpness > 0u;
    // The box filter handles reduction, mixed axes, bilinear enlargement and
    // the plain copy when neither EASU nor RCAS writes the window.
    plan.resample = !plan.easu && !(same_size && plan.rcas);
    plan.resample_to_target = plan.resample && !plan.rcas;
    plan.copy = plan.resample_to_target && same_size;
    return plan;
}

namespace {

std::mutex g_status_mutex;
PostProcessStatus g_status{};
std::mutex g_hdr_mutex;
DisplayHdr g_hdr{};
HdrCalibration g_calibration{};

}  // namespace

DisplayHdr get_display_hdr() {
    const std::lock_guard<std::mutex> lock(g_hdr_mutex);
    return g_hdr;
}

void set_display_hdr(const DisplayHdr& display) {
    const std::lock_guard<std::mutex> lock(g_hdr_mutex);
    g_hdr = display;
}

void set_hdr_calibration(const HdrCalibration& calibration) noexcept {
    const std::lock_guard<std::mutex> lock(g_hdr_mutex);
    g_calibration = calibration;
}

HdrCalibration hdr_calibration() noexcept {
    const std::lock_guard<std::mutex> lock(g_hdr_mutex);
    return g_calibration;
}


unsigned clamp_paper_white(unsigned nits) noexcept {
    return std::clamp(nits, kPaperWhiteMin, kPaperWhiteMax);
}

bool hdr_output_wanted(const RenderConfig& cfg, const DisplayHdr& display) noexcept {
    return cfg.hdr && display.available;
}

PostOutput post_output_for(const RenderConfig& cfg, const DisplayHdr& display) noexcept {
    const HdrCalibration calibration = hdr_calibration();
    PostOutput output;
    output.hdr = true;
    output.paper_white_nits = clamp_paper_white(calibration.active ? calibration.paper_white_nits : cfg.paper_white);
    const unsigned reported = display.peak_nits != 0u ? display.peak_nits : 1000u;
    const unsigned peak = calibration.active ? calibration.peak_nits : cfg.hdr_peak != 0u ? cfg.hdr_peak : reported;
    output.peak_nits = std::clamp(peak, std::max(output.paper_white_nits, kHdrPeakMin), kHdrPeakMax);
    output.black = std::min(calibration.active ? calibration.black : cfg.hdr_black, kHdrBlackSteps);
    output.pattern = calibration.active ? calibration.pattern : HdrPattern::None;
    return output;
}

PostOutput capture_output(const PostOutput& shown, bool highlights) noexcept {
    PostOutput capture;
    if (shown.hdr && shown.pattern != HdrPattern::None) {
        capture.peak_nits = shown.peak_nits;
        capture.black = shown.black;
        capture.pattern = shown.pattern;
    } else if (shown.hdr && highlights) {
        capture.pattern = HdrPattern::Highlights;
    }
    return capture;
}

std::int32_t post_output_word(const PostOutput& output) noexcept {
    static_assert(kPaperWhiteMax < 512u && kHdrPeakMax < 16384u && kHdrBlackSteps < 128u);
    const std::uint32_t paper = std::min(output.paper_white_nits, kPaperWhiteMax);
    const std::uint32_t peak = std::min(output.peak_nits, kHdrPeakMax);
    const std::uint32_t black = std::min(output.black, kHdrBlackSteps);
    const std::uint32_t pattern = static_cast<std::uint32_t>(output.pattern) & 3u;
    return std::bit_cast<std::int32_t>(paper | (peak << 9u) | (black << 23u) | (pattern << 30u));
}

void set_post_output(PostConstants& constants, const PostOutput& output, unsigned picture_width,
                     unsigned picture_height) noexcept {
    constants.origin[2] = output.hdr ? 2 : 1;
    constants.origin[3] = output.hdr || output.pattern != HdrPattern::None ? post_output_word(output) : 0;
    constants.turn[1] = static_cast<std::int32_t>(picture_width);
    constants.turn[2] = static_cast<std::int32_t>(picture_height);
}

float pq_encode(float nits) noexcept {
    const float y = std::pow(std::clamp(nits / 10000.0f, 0.0f, 1.0f), 0.1593017578125f);
    return std::pow((0.8359375f + 18.8515625f * y) / (1.0f + 18.6875f * y), 78.84375f);
}

float pq_decode(float code) noexcept {
    const float p = std::pow(std::clamp(code, 0.0f, 1.0f), 1.0f / 78.84375f);
    return 10000.0f * std::pow(std::max(p - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * p), 1.0f / 0.1593017578125f);
}

void hdr10_encode(const float picture[3], unsigned paper_white_nits, unsigned peak_nits, float code[3],
                  float black_nits) noexcept {
    // Gamma 2.2 decoded (the picture is graded for a 2.2 display), then BT.709 to BT.2020, then nits.
    const float r = std::pow(std::max(picture[0], 0.0f), 2.2f);
    const float g = std::pow(std::max(picture[1], 0.0f), 2.2f);
    const float b = std::pow(std::max(picture[2], 0.0f), 2.2f);
    const float paper = static_cast<float>(paper_white_nits);
    float nits[3] = {(0.627404f * r + 0.329283f * g + 0.043313f * b) * paper,
                     (0.069097f * r + 0.919540f * g + 0.011362f * b) * paper,
                     (0.016391f * r + 0.088013f * g + 0.895595f * b) * paper};
    // BT.2390 on the brightest channel, the other two scaled with it (the hue stays): the roll-off to the display's peak,
    // then the black level lift to its darkest visible level (true black goes there too, grey).
    const float display_peak = static_cast<float>(peak_nits);
    const float source_peak = std::min(paper * kHdrHeadroom, 10000.0f);
    const float brightest = std::max(nits[0], std::max(nits[1], nits[2]));
    if (brightest <= 0.0f) {
        for (float& value : nits) {
            value = black_nits;
        }
    } else {
        float shown = brightest;
        const bool roll = display_peak < source_peak;
        if (roll || black_nits > 0.0f) {
            const float source_code = pq_encode(source_peak);
            float e = pq_encode(brightest) / source_code;
            if (roll) {
                e = std::min(e, 1.0f);
                const float max_lum = pq_encode(display_peak) / source_code;
                const float knee = std::max(1.5f * max_lum - 0.5f, 0.0f);
                if (e > knee) {
                    const float t = (e - knee) / (1.0f - knee);
                    const float t2 = t * t;
                    const float t3 = t2 * t;
                    e = (2.0f * t3 - 3.0f * t2 + 1.0f) * knee + (t3 - 2.0f * t2 + t) * (1.0f - knee) +
                        (-2.0f * t3 + 3.0f * t2) * max_lum;
                }
            }
            if (black_nits > 0.0f) {
                const float min_lum = pq_encode(black_nits) / source_code;
                const float rest = std::max(1.0f - e, 0.0f);
                e += min_lum * rest * rest * rest * rest;
            }
            shown = pq_decode(e * source_code);
        }
        const float scale = std::min(shown, display_peak) / brightest;
        for (float& value : nits) {
            value *= scale;
        }
    }
    for (unsigned i = 0u; i < 3u; ++i) {
        code[i] = pq_encode(nits[i]);
    }
}

float hdr_pattern_code(const PostOutput& output, int x, int y, int width, int height) noexcept {
    if (output.pattern == HdrPattern::None || width <= 0 || height <= 0) {
        return -1.0f;
    }
    const HdrBox box = hdr_box(width, height);
    const int side = box.side;
    const int bx = x - box.left;
    const int by = y - box.top;
    if (side <= 0 || bx < 0 || by < 0 || bx >= side || by >= side) {
        return -1.0f;
    }
    // The star, tested at the pixel's centre.
    const float radius = kHdrStarRadius * static_cast<float>(side);
    const float half = static_cast<float>(side) * 0.5f;
    const bool star = hdr_star((static_cast<float>(bx) + 0.5f - half) / radius,
                               (half + kHdrStarDrop * radius - static_cast<float>(by) - 0.5f) / radius);
    if (output.pattern == HdrPattern::Peak) {
        return star ? pq_encode(static_cast<float>(output.peak_nits)) : 1.0f;
    }
    return star ? pq_encode(hdr_black_nits(output.black)) : 0.0f;
}

bool hdr_star(float x, float y) noexcept {
    constexpr float kPi = 3.14159265f;
    constexpr float kSector = kPi * 0.4f;      // 72 degrees, point to point
    constexpr float kInner = 0.381966f;        // the corners between the points: sin 18 / sin 126 of the radius
    const float distance = std::sqrt(x * x + y * y);
    if (distance > 1.0f) {
        return false;
    }
    // The angle from straight up, folded to between a point (0) and the corner beside it (half a sector).
    float angle = std::fmod(std::atan2(x, y) + kPi * 2.0f, kSector);
    if (angle > kSector * 0.5f) {
        angle = kSector - angle;
    }
    // Inside: on the middle's side of the edge from the point (1, 0) to the corner.
    const float edge_x = kInner * std::cos(kSector * 0.5f) - 1.0f;
    const float edge_y = kInner * std::sin(kSector * 0.5f);
    const float at_x = distance * std::cos(angle) - 1.0f;
    const float at_y = distance * std::sin(angle);
    return edge_x * at_y - edge_y * at_x >= 0.0f;
}

HdrMetadata hdr10_metadata(const DisplayHdr& display, const PostOutput& levels) noexcept {
    HdrMetadata metadata{};
    metadata.red[0] = 0.708f;
    metadata.red[1] = 0.292f;
    metadata.green[0] = 0.170f;
    metadata.green[1] = 0.797f;
    metadata.blue[0] = 0.131f;
    metadata.blue[1] = 0.046f;
    metadata.white[0] = 0.3127f;
    metadata.white[1] = 0.3290f;
    const unsigned peak = levels.pattern != HdrPattern::None ? kHdrPeakMax : levels.peak_nits;
    metadata.max_luminance = static_cast<float>(peak);
    metadata.min_luminance = levels.pattern != HdrPattern::None ? 0.0f
                             : levels.black != 0u               ? hdr_black_nits(levels.black)
                                                                : static_cast<float>(display.min_nits_10000) / 10000.0f;
    metadata.max_content_light = static_cast<float>(peak);
    metadata.max_frame_average_light = static_cast<float>(
        display.full_frame_nits != 0u ? std::min(display.full_frame_nits, peak) : peak / 2u);
    return metadata;
}

PostProcessStatus get_post_process_status() {
    const std::lock_guard<std::mutex> lock(g_status_mutex);
    return g_status;
}

void set_post_process_status(const PostProcessStatus& status) {
    const std::lock_guard<std::mutex> lock(g_status_mutex);
    g_status = status;
}

// Rows stay within 39 characters: the overlay's 1040 px panel at text scale 4.
std::string anti_aliasing_row_text(unsigned draft_aa, unsigned draft_quality,
                                   const PostProcessStatus& status) {
    static constexpr const char* kAa[] = {"OFF", "SMAA", "AUTO", "FXAA"};
    static constexpr const char* kQuality[] = {"LOW", "MEDIUM", "HIGH", "ULTRA"};
    const unsigned aa = std::min(draft_aa, 3u);
    const std::string label = "ANTI-ALIASING: ";
    const std::string name = label + kAa[aa];
    if (aa == 0u) return name;
    const auto reason = [](const std::array<char, 40>& r) {
        return std::string(r[0] != '\0' ? r.data() : "UNKNOWN");
    };
    if (!status.initialized) return label + "NOT RUNNING (" + reason(status.skip_reason) + ")";
    // An unapplied choice, or nothing presented yet: the name alone.
    const bool uses_quality = aa == 1u || aa == 2u;
    if (aa != status.live_aa || (uses_quality && std::min(draft_quality, 3u) != status.live_quality) ||
        status.frames == 0u) {
        return name;
    }
    const std::string smaa = std::string("SMAA ") + kQuality[std::min(draft_quality, 3u)];
    switch (status.last_run) {
    case AaRun::kSmaa:
        return aa == 2u ? name + " (" + smaa + ")" : label + smaa;
    case AaRun::kFxaa:
        if (aa == 3u) return name;
        if (aa == 2u) return name + " (FXAA)";
        return label + "FXAA (SMAA: " + reason(status.smaa_reason) + ")";
    case AaRun::kNone:
        break;
    }
    if (aa == 2u && !status.last_wanted) return name + " (SUPERSAMPLED)";
    return label + "NOT RUNNING (" + reason(status.skip_reason) + ")";
}

std::string post_process_caps_text(const PostProcessStatus& status) {
    if (!status.initialized) {
        return std::string("POST-PROCESS: OFF (") +
               (status.skip_reason[0] != '\0' ? status.skip_reason.data() : "UNKNOWN") + ")";
    }
    return std::string("POST-PROCESS: SMAA ") + (status.smaa_ok ? "PASS" : "FAIL") +
           ", FXAA " + (status.fxaa_ok ? "PASS" : "FAIL");
}

bool sgsr2_available() noexcept {
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
    return true;
#else
    return false;
#endif
}

bool metalfx_available() noexcept {
#if defined(NEBULA_GPU_METAL)
    const mtl::DeviceCaps& caps = mtl::Context::get().caps();
    return caps.metalfx_spatial || caps.metalfx_temporal;
#else
    return false;
#endif
}

bool metalfx_temporal_available() noexcept {
#if defined(NEBULA_GPU_METAL)
    return mtl::Context::get().caps().metalfx_temporal;
#else
    return false;
#endif
}

#if defined(NEBULA_GPU_METAL) || defined(NEBULA_GPU_D3D11)
// AMD's FSR 3 is not this renderer's: its temporal upscalers are MetalFX's and SGSR 2.
bool fsr3_available() noexcept {
    return false;
}
#endif

#if defined(NEBULA_GPU_VULKAN)
// Whether the build holds it (temporal_upscaler_vk.cpp) and, once there is a device, whether it runs there.
bool fsr3_available() noexcept {
#if defined(GALAXY_WITH_FSR3_VK)
    const vk::Context& context = vk::Context::get();
    return context.device() == VK_NULL_HANDLE || context.temporal_upscaling();
#else
    return false;
#endif
}
#endif

}  // namespace galaxy::render
