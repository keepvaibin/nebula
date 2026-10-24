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

// Opt-in RMGE01 aspect experiments. The game's native widescreen mode uses 832 logical horizontal
// units over a fixed 640-unit guest EFB, whose native pixel scale is selected separately. The
// anamorphic relationship is kept while the game-owned logical width changes.
// The Dusklight (CC0) m_Do_graphic.cpp::updateRenderSize/setTvSize derives camera and HUD space from
// one logical aspect. RMGE01's corresponding source-owned boundary is MR::getScreenWidth at
// 0x803F6B44, with a second guarded hook for CameraContext::getAspect. A separate opt-in selects the
// game's 4:3 path. Neither experiment claims arbitrary aspect support or game validation.
struct ExperimentalUltrawideAspect {
    double content_aspect = kWiiContentAspect;
    unsigned guest_screen_width = 832;
};

[[nodiscard]] inline std::optional<ExperimentalUltrawideAspect>
make_experimental_ultrawide_aspect(double content_aspect) noexcept;

// The game's own 4:3 mode (GALAXY_EXPERIMENTAL_NATIVE_4_3), with its 608-unit screen and authored 4:3
// layouts. A 4:3 picture from the live aspect path keeps the wide mode at 624 units and fits the wide
// UI, as other narrow aspects do.
[[nodiscard]] inline bool native_four_three(
    const ExperimentalUltrawideAspect& policy) noexcept {
    return policy.guest_screen_width == 608u;
}

// Host-owned, set at a completed VI boundary. Packed into one atomic publication so camera, screen
// width, NW4R, presentation and IR can never see a half-finished resize. The upper word is a generation.
// The low word holds positive client pixels, or zero for an unsupported shape, which falls back to
// native 16:9.
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
    // Shapes beyond 9:21..32:9 are clamped into it (the presentation fits the picture); a degenerate client
    // keeps the game's fitted 16:9 picture.
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
    // Every shape: wider than 16:9 the view widens (Hor+), below 4:3 it keeps 4:3's width and grows taller (Vert+,
    // display_native.c and CameraContext::getRenderFovy); beyond 32:9 or below 9:21 the picture keeps those shapes
    // and the presentation fits it to the output.
    constexpr double kMaximumAspect = 32.0 / 9.0;
    constexpr double kMinimumAspect = 9.0 / 21.0;
    constexpr double kWideLogicalWidth = 832.0;
    if (!std::isfinite(content_aspect) || content_aspect <= 0.0) {
        return std::nullopt;
    }
    const double aspect = std::clamp(content_aspect, kMinimumAspect, kMaximumAspect);
    // Round to an even horizontal unit, matching RMGE01's 608/832 widths (624 at 4:3 and narrower).
    const unsigned width = static_cast<unsigned>(
        std::lround(kWideLogicalWidth *
                    (std::max(aspect, 4.0 / 3.0) / kWiiContentAspect) * 0.5)) * 2u;
    if (width <= 608u || width > 1664u) {
        return std::nullopt;
    }
    return ExperimentalUltrawideAspect{aspect, width};
}

[[nodiscard]] inline std::optional<double>
parse_experimental_ultrawide_aspect(std::string_view text) {
    // Decimal and ratio forms allow exact settings such as 21:9, regardless of the window's resize timing.
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
                "GALAXY_EXPERIMENTAL_ULTRAWIDE_ASPECT requires a positive "
                "aspect (for example 16:10, 21:9 or 9:16)");
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

}  // namespace galaxy
