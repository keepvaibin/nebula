#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

namespace galaxy::input {

// The screen-to-EFB transform used by RMGE01's 803F6D84. X is anamorphic;
// Y already denotes a logical EFB row. The depth field stores precisely the
// GXPeekZ center texel for each Wii pixel, not an interpolated depth scalar.
struct MousePointerQuery {
    float screen_x{}, screen_y{};
    std::uint16_t efb_x{}, efb_y{};
};

struct MousePointerPosition { float x{},y{}; };

// Screen-only menus accept the full logical edge without inventing a depth
// pixel there. Their explicit no-Z consumers use this position alone.
[[nodiscard]] inline std::optional<MousePointerPosition> mouse_pointer_position(
    float x,float y,unsigned width,unsigned height) noexcept {
    if (!std::isfinite(x)||!std::isfinite(y)||x < -1.0f||x>1.0f||y < -1.0f||y>1.0f||
        !width||!height||height>528u) return {};
    return MousePointerPosition{(x+1.0f)*(width*0.5f),(y+1.0f)*(height*0.5f)};
}

[[nodiscard]] inline std::optional<MousePointerQuery> mouse_pointer_query(
    float x, float y, unsigned screen_width, unsigned screen_height,
    unsigned framebuffer_width) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || x < -1.0f || x > 1.0f ||
        y < -1.0f || y > 1.0f || screen_width == 0u ||
        screen_height == 0u || screen_height > 528u ||
        framebuffer_width == 0u || framebuffer_width > 640u) return {};
    float sx = (x + 1.0f) * (static_cast<float>(screen_width) * 0.5f);
    float sy = (y + 1.0f) * (static_cast<float>(screen_height) * 0.5f);
    // Normalized +1 denotes the inclusive physical content edge. Represent it
    // by the nearest interior logical coordinate and query THAT coordinate's
    // GX pixel. This retains subpixel positions across the final logical pixel
    // without pairing an out-of-range coordinate with a borrowed depth value.
    if (sx >= static_cast<float>(screen_width))
        sx=std::nextafter(static_cast<float>(screen_width),0.0f);
    if (sy >= static_cast<float>(screen_height))
        sy=std::nextafter(static_cast<float>(screen_height),0.0f);
    float ex = (sx / static_cast<float>(screen_width)) *
        static_cast<float>(framebuffer_width);
    // Float division/multiplication can round the interior X up to fbWidth.
    // Move the logical position inward until the original transform is valid.
    if (ex >= static_cast<float>(framebuffer_width)) {
        sx=std::nextafter(sx,0.0f);
        ex=(sx/static_cast<float>(screen_width))*static_cast<float>(framebuffer_width);
    }
    if (ex < 0.0f || ex >= static_cast<float>(framebuffer_width)) return {};
    return MousePointerQuery{sx, sy, static_cast<std::uint16_t>(ex),
        static_cast<std::uint16_t>(sy)};
}

} // namespace galaxy::input
