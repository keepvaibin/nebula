// The picture's shape the game draws for. The host latches the output's client size at a frame start (host_main.cpp);
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

// The game's widescreen mode spans 832 logical units over its 640-unit scene target; another shape keeps that anamorphic
// relation and changes the logical width. Camera and HUD follow one logical aspect, the policy of Dusklight's (CC0)
// m_Do_graphic.cpp::updateRenderSize/setTvSize.
struct DisplayAspect {
    double content_aspect = kDesignContentAspect;
    unsigned game_screen_width = 832;
};

[[nodiscard]] inline std::optional<DisplayAspect>
make_display_aspect(double content_aspect) noexcept;

// Set at a completed frame and read as one value, so camera, screen width, layouts, presentation and pointer never
// see half a resize. The upper word is a generation; the lower holds width << 16 | height in client pixels, or zero
// for a shape that falls back to the game's 16:9.
inline std::atomic<std::uint64_t> g_display_aspect_word{0u};

[[nodiscard]] inline std::uint32_t display_aspect_extent(
    int width, int height) noexcept {
    if (width <= 1 || height <= 1 || width > 65535 || height > 65535) {
        return 0u;
    }
    const double aspect = static_cast<double>(width) / height;
    if (!make_display_aspect(aspect)) {
        return 0u;
    }
    return (static_cast<std::uint32_t>(width) << 16u) |
           static_cast<std::uint32_t>(height);
}

[[nodiscard]] inline bool display_aspect_change_pending(
    int width, int height) noexcept {
    const std::uint32_t next = display_aspect_extent(width, height);
    const std::uint32_t current = static_cast<std::uint32_t>(
        g_display_aspect_word.load(std::memory_order_acquire));
    return next != current;
}

inline void latch_display_aspect(int width, int height) noexcept {
    const std::uint32_t next = display_aspect_extent(width, height);
    const std::uint64_t current =
        g_display_aspect_word.load(std::memory_order_acquire);
    if (static_cast<std::uint32_t>(current) == next) {
        return;
    }
    const std::uint64_t generation = (current >> 32u) + 1u;
    g_display_aspect_word.store(
        (generation << 32u) | next, std::memory_order_release);
}

[[nodiscard]] inline std::optional<DisplayAspect>
make_display_aspect(double content_aspect) noexcept {
    // Wider than 16:9 the view widens (Hor+); narrower than 4:3 it keeps 4:3's width and grows taller (Vert+,
    // display_native.c and CameraContext::getRenderFovy). Beyond kWidestContentAspect or kNarrowestContentAspect the
    // picture keeps those shapes and the presentation fits it to the output.
    constexpr double kWideLogicalWidth = 832.0;
    if (!std::isfinite(content_aspect) || content_aspect <= 0.0) {
        return std::nullopt;
    }
    const double aspect = std::clamp(content_aspect, kNarrowestContentAspect, kWidestContentAspect);
    // An even number of units, as the game's own 608 and 832 (624 at 4:3 and narrower; 4992 at the widest).
    const unsigned width = static_cast<unsigned>(
        std::lround(kWideLogicalWidth *
                    (std::max(aspect, 4.0 / 3.0) / kDesignContentAspect) * 0.5)) * 2u;
    return DisplayAspect{aspect, width};
}

[[nodiscard]] inline std::optional<DisplayAspect>
display_aspect_from_word(std::uint64_t word) noexcept {
    const auto extent = static_cast<std::uint32_t>(word);
    const auto width = static_cast<unsigned>(extent >> 16u);
    const auto height = static_cast<unsigned>(extent & 0xffffu);
    if (width <= 1u || height <= 1u) {
        return std::nullopt;
    }
    return make_display_aspect(
        static_cast<double>(width) / height);
}

// The latched shape; none before the first latch or for a degenerate client.
[[nodiscard]] inline std::optional<DisplayAspect>
latched_display_aspect() {
    return display_aspect_from_word(
        g_display_aspect_word.load(std::memory_order_acquire));
}

// The latched picture's width over its height (kNarrowestContentAspect to kWidestContentAspect); the game's 16:9 before
// the first latch. Any thread.
[[nodiscard]] inline double latched_content_aspect() {
    const std::optional<DisplayAspect> aspect = latched_display_aspect();
    return aspect ? aspect->content_aspect : kDesignContentAspect;
}

}  // namespace galaxy
