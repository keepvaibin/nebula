// LinePointValues and the viewport as the line/point geometry shaders' parameters.

#include "galaxy/render/line_point_raster.h"

#include "galaxy/render/render_config.h"

#include <algorithm>
#include <cmath>

namespace galaxy::render {

LinePointRasterParams make_line_point_raster_params(
    const LinePointValues& values,
    float viewport_half_width,
    float viewport_half_height,
    unsigned efb_scale,
    unsigned fallback_efb_width,
    unsigned fallback_efb_height) noexcept {
    const unsigned scale = std::clamp(efb_scale, 1u, kMaxEfbScale);
    const bool valid_viewport =
        std::isfinite(viewport_half_width) &&
        std::isfinite(viewport_half_height) &&
        viewport_half_width > 0.0f && viewport_half_height < 0.0f;
    const float logical_width = valid_viewport
        ? 2.0f * viewport_half_width
        : static_cast<float>(std::max(fallback_efb_width, 1u));
    const float logical_height = valid_viewport
        ? -2.0f * viewport_half_height
        : static_cast<float>(std::max(fallback_efb_height, 1u));
    const float physical_scale = static_cast<float>(scale);
    // Texture-offset selector to the n of its 1/n offset.
    static constexpr std::array<std::uint32_t, 8> kTextureOffsetDivisors{
        0u, 16u, 8u, 4u, 2u, 1u, 1u, 1u};
    return LinePointRasterParams{
        logical_width * physical_scale,
        logical_height * physical_scale,
        (static_cast<float>(values.line_width) / 6.0f) * physical_scale,
        (static_cast<float>(values.point_size) / 6.0f) * physical_scale,
        values.line_coords,
        values.point_coords,
        kTextureOffsetDivisors[values.line_offset & 7u],
        kTextureOffsetDivisors[values.point_offset & 7u],
    };
}

}  // namespace galaxy::render
