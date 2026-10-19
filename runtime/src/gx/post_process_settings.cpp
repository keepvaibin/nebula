// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/post_process_settings.h"

#include <algorithm>
#include <mutex>

namespace galaxy::gx {

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
// Temporal upscaling is D3D12-only so far (temporal_upscaler.cpp).
bool fsr3_available() noexcept { return false; }
#endif

}  // namespace galaxy::gx
