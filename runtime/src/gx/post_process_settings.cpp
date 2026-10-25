// SPDX-License-Identifier: GPL-3.0-only
// Post-processing settings, pass planning, FSR 1 constants, the AA self-test and the overlay's status text.
#include "galaxy/gx/post_process_settings.h"
#include "galaxy/gx/render_config.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <mutex>

namespace galaxy::gx {

PostProcessSettings post_process_settings_from(const RenderConfig& cfg) noexcept {
    PostProcessSettings post{};
    post.aa = static_cast<PostProcessSettings::Aa>(std::min(cfg.aa_mode, 3u));
    post.smaa_quality = static_cast<std::uint8_t>(std::min(cfg.smaa_quality, 3u));
    post.upscaler = static_cast<PostProcessSettings::Upscaler>(std::min(cfg.upscaler, 1u));
    post.sharpness = static_cast<std::uint8_t>(std::min(cfg.sharpness, 100u));
    return post;
}

void set_post_input_size(PostConstants& constants, unsigned width, unsigned height) noexcept {
    constants.rt_metrics[0] = 1.0f / static_cast<float>(width);
    constants.rt_metrics[1] = 1.0f / static_cast<float>(height);
    constants.rt_metrics[2] = static_cast<float>(width);
    constants.rt_metrics[3] = static_cast<float>(height);
}

void set_deband_constants(PostConstants& constants, unsigned height) noexcept {
    constants.params[0] = std::max(8.0f, 48.0f * static_cast<float>(height) / 1080.0f);
    constants.params[1] = 1.5f / 255.0f;
}

bool deband_wanted() noexcept {
    static const bool wanted = [] {
#if defined(_MSC_VER)
        char value[8]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value), "GALAXY_DEBAND") != 0 || length <= 1u || value[0] != '0';
#else
        const char* value = std::getenv("GALAXY_DEBAND");
        return value == nullptr || value[0] != '0';
#endif
    }();
    return wanted;
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
        settings.upscaler == PostProcessSettings::Upscaler::kFsr1;
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

}  // namespace

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

#if defined(NEBULA_GPU_VULKAN)
// Temporal upscaling is D3D12 only (temporal_upscaler.cpp).
bool fsr3_available() noexcept { return false; }
#endif

}  // namespace galaxy::gx
