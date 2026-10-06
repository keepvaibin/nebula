#pragma once

#include "galaxy/content_viewport.h"

#include <atomic>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace galaxy {

// Opt-in RMGE01 aspect experiments. The game's native widescreen mode uses
// 832 logical horizontal units over a fixed 640-unit guest EFB (whose native
// pixel scale is selected separately). Keep that anamorphic relationship while
// changing the game-owned logical width.
// Dusklight CC0 m_Do_graphic.cpp::updateRenderSize/setTvSize derives its camera
// and HUD space from one logical aspect; RMGE01's corresponding source-owned
// boundary is MR::getScreenWidth at 0x803F6B44, with a second guarded hook
// for CameraContext::getAspect. A separate opt-in selects the game's 4:3
// path. Neither experiment claims arbitrary aspect or game validation.
struct ExperimentalUltrawideAspect {
    double content_aspect = kWiiContentAspect;
    unsigned guest_screen_width = 832;
};

[[nodiscard]] inline std::optional<ExperimentalUltrawideAspect>
make_experimental_ultrawide_aspect(double content_aspect) noexcept;

// Host-owned at a completed VI boundary. Packed into one atomic publication so
// camera, screen width, NW4R, presentation and IR can never observe half a
// resize. The upper word is a generation; the low word stores positive client
// pixels, or zero for an unsupported shape (which safely uses native 16:9).
inline std::atomic<std::uint64_t> g_experimental_dynamic_aspect_word{0u};

[[nodiscard]] inline bool experimental_dynamic_aspect_requested() {
    static const bool requested = [] {
        char* setting_buffer = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&setting_buffer, &length,
                      "GALAXY_EXPERIMENTAL_DYNAMIC_ASPECT") != 0) {
            throw std::runtime_error("cannot read dynamic aspect setting");
        }
        const std::unique_ptr<char, decltype(&std::free)> setting_owner(
            setting_buffer, &std::free);
        const std::string_view setting = setting_owner
            ? std::string_view(setting_owner.get()) : std::string_view{};
        if (setting.empty() || setting == "0") {
            return false;
        }
        if (setting != "1") {
            throw std::invalid_argument(
                "GALAXY_EXPERIMENTAL_DYNAMIC_ASPECT requires 0 or 1");
        }
        return true;
    }();
    return requested;
}

[[nodiscard]] inline std::uint32_t experimental_dynamic_aspect_extent(
    int width, int height) noexcept {
    if (width <= 1 || height <= 1 || width > 65535 || height > 65535) {
        return 0u;
    }
    const double aspect = static_cast<double>(width) / height;
    // The first native slice only supports RMGE01's selected widescreen path.
    // At 4:3 and unsupported extremes retain the game's default fitted 16:9
    // presentation rather than stretching or mutating guest camera state.
    if (!make_experimental_ultrawide_aspect(aspect)) {
        return 0u;
    }
    return (static_cast<std::uint32_t>(width) << 16u) |
           static_cast<std::uint32_t>(height);
}

[[nodiscard]] inline bool experimental_dynamic_aspect_change_pending(
    int width, int height) noexcept {
    const std::uint32_t next = experimental_dynamic_aspect_extent(width, height);
    const std::uint32_t current = static_cast<std::uint32_t>(
        g_experimental_dynamic_aspect_word.load(std::memory_order_acquire));
    return next != current;
}

inline void experimental_latch_dynamic_aspect(int width, int height) noexcept {
    const std::uint32_t next = experimental_dynamic_aspect_extent(width, height);
    const std::uint64_t current =
        g_experimental_dynamic_aspect_word.load(std::memory_order_acquire);
    if (static_cast<std::uint32_t>(current) == next) {
        return;
    }
    const std::uint64_t generation = (current >> 32u) + 1u;
    g_experimental_dynamic_aspect_word.store(
        (generation << 32u) | next, std::memory_order_release);
}

[[nodiscard]] inline std::optional<ExperimentalUltrawideAspect>
make_experimental_ultrawide_aspect(double content_aspect) noexcept {
    constexpr double kMaximumAspect = 32.0 / 9.0;
    constexpr double kWideLogicalWidth = 832.0;
    if (!std::isfinite(content_aspect) ||
        content_aspect <= 4.0 / 3.0 ||
        content_aspect > kMaximumAspect) {
        return std::nullopt;
    }
    // Round to an even horizontal unit, matching RMGE01's 608/832 widths.
    const unsigned width = static_cast<unsigned>(
        std::lround(kWideLogicalWidth *
                    (content_aspect / kWiiContentAspect) * 0.5)) * 2u;
    if (width <= 608u || width > 1664u) {
        return std::nullopt;
    }
    return ExperimentalUltrawideAspect{content_aspect, width};
}

[[nodiscard]] inline std::optional<double>
parse_experimental_ultrawide_aspect(std::string_view text) {
    // Decimal and ratio forms permit exact settings such as 21:9 without
    // depending on the current window's resize timing.
    if (text.empty() || text.size() > 32u) {
        return std::nullopt;
    }
    const std::string input{text};
    char* end = nullptr;
    const double numerator = std::strtod(input.c_str(), &end);
    if (end == input.c_str() || !std::isfinite(numerator)) {
        return std::nullopt;
    }
    double aspect = numerator;
    if (*end == ':') {
        char* ratio_end = nullptr;
        const double denominator = std::strtod(end + 1, &ratio_end);
        if (ratio_end == end + 1 || *ratio_end != '\0' ||
            !std::isfinite(denominator) || denominator <= 0.0) {
            return std::nullopt;
        }
        aspect /= denominator;
    } else if (*end != '\0') {
        return std::nullopt;
    }
    if (!make_experimental_ultrawide_aspect(aspect)) {
        return std::nullopt;
    }
    return aspect;
}

[[nodiscard]] inline bool experimental_rmge01_native_four_three_requested() {
    static const bool requested = [] {
        char* setting_buffer = nullptr;
        std::size_t setting_length = 0;
        if (_dupenv_s(&setting_buffer, &setting_length,
                      "GALAXY_EXPERIMENTAL_NATIVE_4_3") != 0) {
            throw std::runtime_error("cannot read native 4:3 setting");
        }
        const std::unique_ptr<char, decltype(&std::free)> setting_owner(
            setting_buffer, &std::free);
        const std::string_view setting = setting_owner
            ? std::string_view(setting_owner.get()) : std::string_view{};
        if (setting.empty() || setting == "0") {
            return false;
        }
        if (setting == "1") {
            return true;
        }
        throw std::invalid_argument(
            "GALAXY_EXPERIMENTAL_NATIVE_4_3 requires 0 or 1");
    }();
    return requested;
}

[[nodiscard]] inline const std::optional<ExperimentalUltrawideAspect>&
experimental_ultrawide_aspect_from_env() {
    static const std::optional<ExperimentalUltrawideAspect> policy = [] {
        char* setting_buffer = nullptr;
        std::size_t setting_length = 0;
        if (_dupenv_s(&setting_buffer, &setting_length,
                      "GALAXY_EXPERIMENTAL_ULTRAWIDE_ASPECT") != 0) {
            throw std::runtime_error("cannot read ultrawide aspect setting");
        }
        const std::unique_ptr<char, decltype(&std::free)> setting_owner(
            setting_buffer, &std::free);
        const char* setting = setting_owner.get();
        if (experimental_dynamic_aspect_requested() &&
            (experimental_rmge01_native_four_three_requested() ||
             (setting != nullptr && *setting != '\0'))) {
            throw std::invalid_argument(
                "dynamic aspect conflicts with fixed aspect experiments");
        }
        if (experimental_rmge01_native_four_three_requested()) {
            if (setting != nullptr && *setting != '\0') {
                throw std::invalid_argument(
                    "native 4:3 and ultrawide aspect experiments conflict");
            }
            return std::optional<ExperimentalUltrawideAspect>{
                ExperimentalUltrawideAspect{4.0 / 3.0, 608u}};
        }
        if (setting == nullptr || *setting == '\0') {
            return std::optional<ExperimentalUltrawideAspect>{};
        }
        const auto parsed = parse_experimental_ultrawide_aspect(setting);
        if (!parsed) {
            throw std::invalid_argument(
                "GALAXY_EXPERIMENTAL_ULTRAWIDE_ASPECT requires an aspect "
                "above 4:3 and at most 32:9 (for example 16:10 or 21:9)");
        }
        return make_experimental_ultrawide_aspect(*parsed);
    }();
    return policy;
}

[[nodiscard]] inline std::optional<ExperimentalUltrawideAspect>
experimental_aspect_from_dynamic_word(std::uint64_t word) noexcept {
    const auto extent = static_cast<std::uint32_t>(word);
    const auto width = static_cast<unsigned>(extent >> 16u);
    const auto height = static_cast<unsigned>(extent & 0xffffu);
    if (width <= 1u || height <= 1u) {
        return std::nullopt;
    }
    return make_experimental_ultrawide_aspect(
        static_cast<double>(width) / height);
}

[[nodiscard]] inline std::optional<ExperimentalUltrawideAspect>
experimental_effective_host_aspect() {
    if (experimental_dynamic_aspect_requested()) {
        // Also validate that no mutually exclusive fixed policy was set.
        (void)experimental_ultrawide_aspect_from_env();
        return experimental_aspect_from_dynamic_word(
            g_experimental_dynamic_aspect_word.load(
                std::memory_order_acquire));
    }
    return experimental_ultrawide_aspect_from_env();
}

template <typename ServicesLike>
[[nodiscard]] inline std::optional<ExperimentalUltrawideAspect>
experimental_effective_guest_aspect(const ServicesLike* services) {
    if (!experimental_dynamic_aspect_requested()) {
        return experimental_ultrawide_aspect_from_env();
    }
    if (services == nullptr ||
        services->experimental_aspect_word == nullptr) {
        throw std::runtime_error(
            "dynamic aspect requires native frame-latched service");
    }
    return experimental_aspect_from_dynamic_word(
        services->experimental_aspect_word(services->user));
}

[[nodiscard]] inline ContentViewport experimental_ultrawide_viewport(
    int surface_width,
    int surface_height,
    const ExperimentalUltrawideAspect& policy) noexcept {
    return fit_content_viewport(
        surface_width, surface_height, policy.content_aspect);
}

[[nodiscard]] inline float experimental_nw4r_canvas_width(
    const ExperimentalUltrawideAspect& policy) noexcept {
    if (policy.guest_screen_width == 608u &&
        policy.content_aspect == 4.0 / 3.0) {
        return 608.0f;
    }
    return static_cast<float>(
        608.0 * std::max(1.0, policy.content_aspect / kWiiContentAspect));
}

[[nodiscard]] inline float experimental_nw4r_vertical_fit_scale(
    const ExperimentalUltrawideAspect& policy) noexcept {
    // Native 4:3 has its own authored layout. Other narrower-than-wide
    // aspects fit the wide UI uniformly rather than cropping or squeezing it.
    if (policy.content_aspect == 4.0 / 3.0) return 1.0f;
    return static_cast<float>(std::max(1.0, kWiiContentAspect / policy.content_aspect));
}

// RMGE01 CameraContext::getAspect takes a fixed 16:9 lfs branch at
// 0x800972B0 when the guest widescreen mode is selected. Its SDA2 word at
// r2-0x691C is 0x3FE38E39. The original guest lfs, memory fault and FPU
// availability check still run; change only its canonical loaded f1 for an
// opt-in content aspect so the camera projection matches present/UI/IR policy.
template <typename PpcContextLike>
inline void apply_experimental_rmge01_camera_aspect(
    PpcContextLike* context,
    const std::optional<ExperimentalUltrawideAspect>& policy) {
    if (!policy) {
        return;
    }
    // This instruction is a legal interior entry. A caller entering here
    // under the native 4:3 policy may bypass the preceding wide-mode branch;
    // preserve its original lfs result instead of treating that as a fault.
    if (policy->content_aspect == 4.0 / 3.0) {
        return;
    }
    if (context == nullptr) {
        throw std::runtime_error("RMGE01 camera aspect has no guest context");
    }
    // A legal interior call can supply a different valid SDA2 base. In that
    // case the guest lfs value is authoritative; change only the canonical
    // selected-DOL constant, and leave every other loaded value intact.
    if (context->gpr[2] != 0x806AB280u ||
        std::bit_cast<double>(context->fpr_bits[1]) !=
            static_cast<double>(std::bit_cast<float>(0x3FE38E39u))) {
        return;
    }
    if (policy->content_aspect == kWiiContentAspect) {
        return;
    }
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(
        static_cast<double>(static_cast<float>(policy->content_aspect)));
    context->fpr_bits[1] = bits;
    if ((context->hid2 & 0x20000000u) != 0u) {
        context->ps1_bits[1] = bits;
    }
}

template <typename PpcContextLike, typename ServicesLike>
inline void apply_experimental_rmge01_camera_aspect(
    PpcContextLike* context, const ServicesLike* services) {
    apply_experimental_rmge01_camera_aspect(
        context, experimental_effective_guest_aspect(services));
}

// The complete translated SCGetAspectRatio body has already run its config
// read and final lbz at 0x804D074C. Change both its normalized stack byte
// and r3 return to select RMGE01's own 4:3 render mode and camera path.
template <typename PpcContextLike, typename GuestMemoryLike,
          typename ServicesLike>
inline void apply_experimental_rmge01_native_four_three_aspect_read(
    PpcContextLike* context,
    GuestMemoryLike* memory,
    const ServicesLike* services) {
    if (!experimental_rmge01_native_four_three_requested()) {
        return;
    }
    if (context == nullptr || memory == nullptr || context->gpr[3] > 1u) {
        throw std::runtime_error(
            "RMGE01 native 4:3 requires a normalized SC aspect result");
    }
    if (context->gpr[3] == 1u) {
        // This byte is inside the translated function's live stack frame;
        // the original SC query, validation, loads and faults still run.
        guest_store_u8(memory, context->gpr[1] + 8u,
                       0u, services, 0x804D074Cu);
        context->gpr[3] = 0u;
    }
}

// This hook is included only by the static shard that owns MR::getScreenWidth.
// The shared guest ABI header stays independent of experimental display code.
template <typename PpcContextLike, typename ServicesLike>
inline void apply_experimental_rmge01_ultrawide_screen_width(
    PpcContextLike* context, const ServicesLike* services) {
    const auto policy = experimental_effective_guest_aspect(services);
    if (!policy) {
        return;
    }
    if (policy->guest_screen_width == 608u &&
        policy->content_aspect == 4.0 / 3.0) {
        if (context == nullptr || context->gpr[3] != 608u) {
            throw std::runtime_error(
                "RMGE01 native 4:3 requires the game's 4:3 screen width");
        }
        return;
    }
    if (context == nullptr || context->gpr[3] != 832u) {
        throw std::runtime_error(
            "RMGE01 ultrawide aspect requires the game's 16:9 mode");
    }
    context->gpr[3] = policy->guest_screen_width;
}

// MR::screenToEfb uses its own 832 constant instead of getScreenWidth.
// Replace only the exact wide-path integer after the original li. This keeps
// the original floating conversion, GX dimensions and depth query intact.
template <typename PpcContextLike>
inline void apply_experimental_rmge01_efb_screen_width(
    PpcContextLike* context,
    const std::optional<ExperimentalUltrawideAspect>& policy) noexcept {
    if (policy && context && context->gpr[2] == 0x806AB280u &&
        context->gpr[0] == 832u && policy->guest_screen_width != 608u) {
        context->gpr[0] = policy->guest_screen_width;
    }
}

template <typename PpcContextLike, typename ServicesLike>
inline void apply_experimental_rmge01_efb_screen_width(
    PpcContextLike* context, const ServicesLike* services) {
    apply_experimental_rmge01_efb_screen_width(
        context, experimental_effective_guest_aspect(services));
}

// RMGE01 MR::setupDrawForNW4RLayout loads its fixed 608-unit canvas into f3
// at 0x803CA6FC. LayoutManager::draw calls this before nw4r::lyt::Layout::Draw.
// The widened XFB presentation would otherwise stretch every NW4R pane. The
// original guest lfs still executes, including memory/FPU checks; this changes
// only its loaded value to the equivalent wider single-precision constant.
template <typename PpcContextLike>
inline void apply_experimental_rmge01_nw4r_canvas_width(
    PpcContextLike* context,
    const std::optional<ExperimentalUltrawideAspect>& policy) {
    if (!policy) {
        return;
    }
    // The load itself is a legal interior entry. Under the native 4:3 mode,
    // or a valid noncanonical SDA2 base/value, its original guest result is
    // authoritative just as it is for the camera-aspect load.
    if (policy->guest_screen_width == 608u &&
        policy->content_aspect == 4.0 / 3.0) {
        return;
    }
    if (context == nullptr) {
        throw std::runtime_error("RMGE01 NW4R canvas has no guest context");
    }
    if (context->gpr[2] != 0x806AB280u ||
        std::bit_cast<double>(context->fpr_bits[3]) != 608.0) {
        return;
    }
    const float widened = experimental_nw4r_canvas_width(*policy);
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(
        static_cast<double>(widened));
    context->fpr_bits[3] = bits;
    // A PPC lfs also duplicates the result into paired-single lane 1 when
    // HID2.PSE is set. Preserve that architectural relationship.
    if ((context->hid2 & 0x20000000u) != 0u) {
        context->ps1_bits[3] = bits;
    }
}

template <typename PpcContextLike, typename ServicesLike>
inline void apply_experimental_rmge01_nw4r_canvas_width(
    PpcContextLike* context, const ServicesLike* services) {
    apply_experimental_rmge01_nw4r_canvas_width(
        context, experimental_effective_guest_aspect(services));
}

// The inverse screen/layout conversions must use the same canvas as the
// Home uses a separate 608-unit NW4HBM layout, adjusted by 832/608 in wide
// mode. Its original projection uses MR::getScreenWidth, not the game UI
// canvas. Fit the complete authored menu and transform the copied KPAD
// coordinates by the same extents; never mutate the original KPAD status.
template <typename PpcContextLike>
inline void apply_experimental_rmge01_home_geometry(
    PpcContextLike* context, unsigned kind,
    const std::optional<ExperimentalUltrawideAspect>& policy, unsigned input_lane = 0u) {
    if (!policy || policy->content_aspect == 4.0 / 3.0 ||
        policy->content_aspect == kWiiContentAspect) return;
    if (context == nullptr || kind > 3u || input_lane >= 32u)
        throw std::runtime_error("RMGE01 Home geometry has no valid context");
    if (context->gpr[2] != 0x806AB280u) return;
    const unsigned lane = kind == 0u ? 3u : kind == 1u ? 1u : input_lane;
    const float value = static_cast<float>(
        std::bit_cast<double>(context->fpr_bits[lane]));
    if (!std::isfinite(value)) return;
    const float width = static_cast<float>(
        std::max(832u, policy->guest_screen_width));
    if (kind == 0u && value != static_cast<float>(policy->guest_screen_width)) return;
    const float fitted = kind == 0u ? width : value *
        (kind == 2u ? width / 832.0f : experimental_nw4r_vertical_fit_scale(*policy));
    const auto bits = std::bit_cast<std::uint64_t>(static_cast<double>(fitted));
    context->fpr_bits[lane] = bits;
    if ((context->hid2 & 0x20000000u) != 0u) context->ps1_bits[lane] = bits;
}

template <typename PpcContextLike, typename ServicesLike>
inline void apply_experimental_rmge01_home_geometry(
    PpcContextLike* context, const ServicesLike* services, unsigned kind, unsigned input_lane = 0u) {
    apply_experimental_rmge01_home_geometry(
        context, kind, experimental_effective_guest_aspect(services), input_lane);
}

// The inverse screen/layout conversions must use the same canvas as the
// NW4R projection. Keep the original lfs and its faults/FPU effects; only
// replace exact canonical constants at the statically admitted load sites.
template <typename PpcContextLike>
inline void apply_experimental_rmge01_layout_constant(
    PpcContextLike* context, unsigned target, bool half_width,
    const std::optional<ExperimentalUltrawideAspect>& policy) {
    if (!policy || policy->content_aspect == 4.0 / 3.0 ||
        policy->content_aspect == kWiiContentAspect) {
        return;
    }
    if (context == nullptr || target >= 32u) {
        throw std::runtime_error("RMGE01 layout constant has no valid context");
    }
    const double original = half_width ? 304.0 : 608.0;
    if (context->gpr[2] != 0x806AB280u ||
        std::bit_cast<double>(context->fpr_bits[target]) != original) {
        return;
    }
    const float canvas = experimental_nw4r_canvas_width(*policy);
    const float value = half_width ? canvas * 0.5f : canvas;
    const auto bits = std::bit_cast<std::uint64_t>(static_cast<double>(value));
    context->fpr_bits[target] = bits;
    if ((context->hid2 & 0x20000000u) != 0u) {
        context->ps1_bits[target] = bits;
    }
}

template <typename PpcContextLike, typename ServicesLike>
inline void apply_experimental_rmge01_layout_constant(
    PpcContextLike* context, const ServicesLike* services,
    unsigned target, bool half_width) {
    apply_experimental_rmge01_layout_constant(
        context, target, half_width, experimental_effective_guest_aspect(services));
}

template <typename PpcContextLike>
inline void apply_experimental_rmge01_layout_vertical_fit(
    PpcContextLike* context, unsigned target, bool inverse,
    const std::optional<ExperimentalUltrawideAspect>& policy) {
    if (!policy || policy->content_aspect == 4.0 / 3.0 ||
        policy->content_aspect >= kWiiContentAspect) return;
    if (context == nullptr || target >= 32u) {
        throw std::runtime_error("RMGE01 layout vertical fit has no valid context");
    }
    // Original operations still run before this finite geometry extension.
    // Noncanonical SDA2 interior callers keep their original coordinates.
    if (context->gpr[2] != 0x806AB280u) return;
    const float value = static_cast<float>(std::bit_cast<double>(context->fpr_bits[target]));
    if (!std::isfinite(value)) return;
    const float scale = experimental_nw4r_vertical_fit_scale(*policy);
    const float fitted = inverse ? value / scale : value * scale;
    const auto bits = std::bit_cast<std::uint64_t>(static_cast<double>(fitted));
    context->fpr_bits[target] = bits;
    if ((context->hid2 & 0x20000000u) != 0u) context->ps1_bits[target] = bits;
}

template <typename PpcContextLike, typename ServicesLike>
inline void apply_experimental_rmge01_layout_vertical_fit(
    PpcContextLike* context, const ServicesLike* services,
    unsigned target, bool inverse) {
    apply_experimental_rmge01_layout_vertical_fit(
        context, target, inverse, experimental_effective_guest_aspect(services));
}

}  // namespace galaxy
