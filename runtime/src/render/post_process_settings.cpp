// SPDX-License-Identifier: GPL-3.0-only
// Post-processing settings, pass planning, FSR 1 constants, the AA self-test and the overlay's status text.
#if defined(NEBULA_GPU_VULKAN)
#include "galaxy/render/vk/vk_context.h"
#elif defined(NEBULA_GPU_METAL)
#include "galaxy/render/mtl/mtl_context.h"
#endif
#include "galaxy/render/post_process_settings.h"
#include "galaxy/run_options.h"
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

void rgba8_to_ten_bit(const std::uint8_t* rgba, std::uint8_t* words, unsigned width, unsigned height,
                      unsigned pitch) noexcept {
    for (unsigned y = 0u; y < height; ++y) {
        const std::uint8_t* from = rgba + static_cast<std::size_t>(y) * pitch;
        std::uint8_t* to = words + static_cast<std::size_t>(y) * pitch;
        for (unsigned x = 0u; x < width; ++x) {
            const auto wide = [](std::uint8_t v) { return (static_cast<std::uint32_t>(v) * 1023u + 127u) / 255u; };
            const std::uint32_t word = wide(from[x * 4u]) | wide(from[x * 4u + 1u]) << 10u |
                                       wide(from[x * 4u + 2u]) << 20u |
                                       static_cast<std::uint32_t>(from[x * 4u + 3u] >> 6u) << 30u;
            std::memcpy(to + x * 4u, &word, 4u);
        }
    }
}

void ten_bit_to_rgba8(const std::uint8_t* words, std::uint8_t* rgba, unsigned width, unsigned height,
                      unsigned word_pitch, unsigned rgba_pitch) noexcept {
    for (unsigned y = 0u; y < height; ++y) {
        const std::uint8_t* from = words + static_cast<std::size_t>(y) * word_pitch;
        std::uint8_t* to = rgba + static_cast<std::size_t>(y) * rgba_pitch;
        for (unsigned x = 0u; x < width; ++x) {
            std::uint32_t word = 0u;
            std::memcpy(&word, from + x * 4u, 4u);
            const auto narrow = [](std::uint32_t v) {
                return static_cast<std::uint8_t>(((v & 1023u) * 255u + 511u) / 1023u);
            };
            to[x * 4u] = narrow(word);
            to[x * 4u + 1u] = narrow(word >> 10u);
            to[x * 4u + 2u] = narrow(word >> 20u);
            to[x * 4u + 3u] = static_cast<std::uint8_t>((word >> 30u) * 85u);
        }
    }
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
    return plan;
}

namespace {

std::mutex g_status_mutex;
PostProcessStatus g_status{};
std::mutex g_hdr_mutex;
DisplayHdr g_hdr{};

}  // namespace

DisplayHdr get_display_hdr() {
    const std::lock_guard<std::mutex> lock(g_hdr_mutex);
    return g_hdr;
}

void set_display_hdr(const DisplayHdr& display) {
    const std::lock_guard<std::mutex> lock(g_hdr_mutex);
    g_hdr = display;
}

unsigned clamp_paper_white(unsigned nits) noexcept {
    return std::clamp(nits, kPaperWhiteMin, kPaperWhiteMax);
}

bool hdr_output_wanted(const RenderConfig& cfg, const DisplayHdr& display) noexcept {
    return cfg.hdr && display.available;
}

PostOutput post_output_for(const RenderConfig& cfg, const DisplayHdr& display) noexcept {
    PostOutput output;
    output.hdr = true;
    output.paper_white_nits = clamp_paper_white(cfg.paper_white);
    const unsigned reported = display.peak_nits != 0u ? display.peak_nits : 1000u;
    output.peak_nits = std::clamp(reported, std::max(output.paper_white_nits, 100u), 10000u);
    return output;
}

std::int32_t post_output_word(const PostOutput& output) noexcept {
    const unsigned paper = std::min(output.paper_white_nits, 1023u);
    const unsigned peak = std::min(output.peak_nits, 10000u);
    return static_cast<std::int32_t>(paper | (peak << 10u));
}

void set_post_output(PostConstants& constants, const PostOutput& output) noexcept {
    constants.origin[2] = output.hdr ? 2 : 1;
    constants.origin[3] = output.hdr ? post_output_word(output) : 0;
}

float pq_encode(float nits) noexcept {
    const float y = std::pow(std::clamp(nits / 10000.0f, 0.0f, 1.0f), 0.1593017578125f);
    return std::pow((0.8359375f + 18.8515625f * y) / (1.0f + 18.6875f * y), 78.84375f);
}

float pq_decode(float code) noexcept {
    const float p = std::pow(std::clamp(code, 0.0f, 1.0f), 1.0f / 78.84375f);
    return 10000.0f * std::pow(std::max(p - 0.8359375f, 0.0f) / (18.8515625f - 18.6875f * p), 1.0f / 0.1593017578125f);
}

void hdr10_encode(const float picture[3], unsigned paper_white_nits, unsigned peak_nits, float code[3]) noexcept {
    // Gamma 2.2 decoded (the picture is graded for a 2.2 display), then BT.709 to BT.2020, then nits.
    const float r = std::pow(std::max(picture[0], 0.0f), 2.2f);
    const float g = std::pow(std::max(picture[1], 0.0f), 2.2f);
    const float b = std::pow(std::max(picture[2], 0.0f), 2.2f);
    const float paper = static_cast<float>(paper_white_nits);
    float nits[3] = {(0.627404f * r + 0.329283f * g + 0.043313f * b) * paper,
                     (0.069097f * r + 0.919540f * g + 0.011362f * b) * paper,
                     (0.016391f * r + 0.088013f * g + 0.895595f * b) * paper};
    // BT.2390 on the brightest channel, the other two scaled with it (the hue stays), black level 0.
    const float display_peak = static_cast<float>(peak_nits);
    const float source_peak = std::min(paper * kHdrHeadroom, 10000.0f);
    const float brightest = std::max(nits[0], std::max(nits[1], nits[2]));
    if (brightest > 0.0f) {
        float shown = brightest;
        if (display_peak < source_peak) {
            const float source_code = pq_encode(source_peak);
            float e = std::min(pq_encode(brightest) / source_code, 1.0f);
            const float max_lum = pq_encode(display_peak) / source_code;
            const float knee = std::max(1.5f * max_lum - 0.5f, 0.0f);
            if (e > knee) {
                const float t = (e - knee) / (1.0f - knee);
                const float t2 = t * t;
                const float t3 = t2 * t;
                e = (2.0f * t3 - 3.0f * t2 + 1.0f) * knee + (t3 - 2.0f * t2 + t) * (1.0f - knee) + (-2.0f * t3 + 3.0f * t2) * max_lum;
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

HdrMetadata hdr10_metadata(const DisplayHdr& display, unsigned paper_white_nits) noexcept {
    HdrMetadata metadata{};
    metadata.red[0] = 0.708f;
    metadata.red[1] = 0.292f;
    metadata.green[0] = 0.170f;
    metadata.green[1] = 0.797f;
    metadata.blue[0] = 0.131f;
    metadata.blue[1] = 0.046f;
    metadata.white[0] = 0.3127f;
    metadata.white[1] = 0.3290f;
    RenderConfig reported{};
    reported.paper_white = paper_white_nits;
    const PostOutput levels = post_output_for(reported, display);
    metadata.max_luminance = static_cast<float>(levels.peak_nits);
    metadata.min_luminance = static_cast<float>(display.min_nits_10000) / 10000.0f;
    metadata.max_content_light = static_cast<float>(levels.peak_nits);
    metadata.max_frame_average_light = static_cast<float>(
        display.full_frame_nits != 0u ? std::min(display.full_frame_nits, levels.peak_nits) : levels.peak_nits / 2u);
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

#if defined(NEBULA_GPU_METAL)
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
