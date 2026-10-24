// The picture's shape the game draws for. The host latches the output's client size at a retrace (host_main.cpp);
// the renderer, the game (display_native.c) and the pointer read it from one atomic word.
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

// The game's widescreen mode spans 832 logical units over its 640-unit EFB; another shape keeps that anamorphic
// relation and changes the logical width. Camera and HUD follow one logical aspect, the policy of Dusklight's (CC0)
// m_Do_graphic.cpp::updateRenderSize/setTvSize.
struct ExperimentalUltrawideAspect {
    double content_aspect = kWiiContentAspect;
    unsigned guest_screen_width = 832;
};

[[nodiscard]] inline std::optional<ExperimentalUltrawideAspect>
make_experimental_ultrawide_aspect(double content_aspect) noexcept;

// Set at a completed retrace and read as one value, so camera, screen width, layouts, presentation and pointer never
// see half a resize. The upper word is a generation; the lower holds width << 16 | height in client pixels, or zero
// for a shape that falls back to the game's 16:9.
inline std::atomic<std::uint64_t> g_experimental_dynamic_aspect_word{0u};

[[nodiscard]] inline std::uint32_t experimental_dynamic_aspect_extent(
    int width, int height) noexcept {
    if (width <= 1 || height <= 1 || width > 65535 || height > 65535) {
        return 0u;
    }
    const double aspect = static_cast<double>(width) / height;
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
    // Wider than 16:9 the view widens (Hor+); narrower than 4:3 it keeps 4:3's width and grows taller (Vert+,
    // display_native.c and CameraContext::getRenderFovy). Beyond 32:9 or 9:21 the picture keeps those shapes and the
    // presentation fits it to the output.
    constexpr double kMaximumAspect = 32.0 / 9.0;
    constexpr double kMinimumAspect = 9.0 / 21.0;
    constexpr double kWideLogicalWidth = 832.0;
    if (!std::isfinite(content_aspect) || content_aspect <= 0.0) {
        return std::nullopt;
    }
    const double aspect = std::clamp(content_aspect, kMinimumAspect, kMaximumAspect);
    // An even number of units, as the game's own 608 and 832 (624 at 4:3 and narrower).
    const unsigned width = static_cast<unsigned>(
        std::lround(kWideLogicalWidth *
                    (std::max(aspect, 4.0 / 3.0) / kWiiContentAspect) * 0.5)) * 2u;
    if (width <= 608u || width > 1664u) {
        return std::nullopt;
    }
    return ExperimentalUltrawideAspect{aspect, width};
}

// The smallest integer EFB scale at which the 2D layouts keep the Wii's own density: past 16:9 their logical width
// spreads over the same 640 columns (half of them at 32:9), narrower than 4:3 their logical height over the same rows.
[[nodiscard]] inline unsigned layout_density_scale(
    const std::optional<ExperimentalUltrawideAspect>& shape) noexcept {
    if (!shape) {
        return 1u;
    }
    const double wide = shape->guest_screen_width / 832.0;
    const double tall = (4.0 / 3.0) / std::min(shape->content_aspect, 4.0 / 3.0);
    return static_cast<unsigned>(std::ceil(std::max({1.0, wide, tall}) - 1e-6));
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

// The latched shape; none before the first latch or for a degenerate client.
[[nodiscard]] inline std::optional<ExperimentalUltrawideAspect>
experimental_effective_host_aspect() {
    return experimental_aspect_from_dynamic_word(
        g_experimental_dynamic_aspect_word.load(std::memory_order_acquire));
}

}  // namespace galaxy
