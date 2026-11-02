#pragma once

// Line and point raster parameters for the line/point geometry shaders: the game gives both widths in logical frame
// pixels; the expansion works in physical pixels at the frame scale.

#include <array>
#include <cstdint>

namespace galaxy::render {

struct LinePointRasterParams {
    float viewport_width_pixels = 0.0f;
    float viewport_height_pixels = 0.0f;
    float line_width_pixels = 0.0f;
    float point_size_pixels = 0.0f;
    std::uint32_t line_texcoord_mask = 0;
    std::uint32_t point_texcoord_mask = 0;
    std::uint32_t line_texcoord_divisor = 0;
    std::uint32_t point_texcoord_divisor = 0;
};
static_assert(sizeof(LinePointRasterParams) == 32u);

// The game's line width and point size (logical pixels), each one's texture offset (1/n of a texel; 0 none) and the
// coordinates it offsets (bit n: coordinate n).
struct LinePointValues {
    float line_width = 0.0f;
    float point_size = 0.0f;
    std::uint8_t line_divisor = 0;
    std::uint8_t point_divisor = 0;
    std::uint8_t line_coords = 0;
    std::uint8_t point_coords = 0;
};

// `viewport_width` / `viewport_height` are the viewport's size in frame pixels (both > 0); any other viewport (not
// yet set) takes the fallback frame size.
[[nodiscard]] LinePointRasterParams make_line_point_raster_params(
    const LinePointValues& values,
    float viewport_width,
    float viewport_height,
    unsigned scene_target_scale,
    unsigned fallback_scene_width = 640u,
    unsigned fallback_scene_height = 456u) noexcept;

}  // namespace galaxy::render
