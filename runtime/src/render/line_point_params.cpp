// LinePointValues and the viewport as the line/point geometry shaders' parameters.

#include "galaxy/render/line_point_params.h"

#include "galaxy/render/render_config.h"

#include <algorithm>
#include <cmath>

namespace galaxy::render {

LinePointRasterParams make_line_point_raster_params(
    const LinePointValues& values,
    float viewport_width,
    float viewport_height,
    unsigned scene_target_scale,
    unsigned fallback_scene_width,
    unsigned fallback_scene_height) noexcept {
    const unsigned scale = std::clamp(scene_target_scale, 1u, kMaxSceneScale);
    const bool valid_viewport =
        std::isfinite(viewport_width) &&
        std::isfinite(viewport_height) &&
        viewport_width > 0.0f && viewport_height > 0.0f;
    const float logical_width = valid_viewport
        ? viewport_width
        : static_cast<float>(std::max(fallback_scene_width, 1u));
    const float logical_height = valid_viewport
        ? viewport_height
        : static_cast<float>(std::max(fallback_scene_height, 1u));
    const float physical_scale = static_cast<float>(scale);
    return LinePointRasterParams{
        logical_width * physical_scale,
        logical_height * physical_scale,
        values.line_width * physical_scale,
        values.point_size * physical_scale,
        values.line_coords,
        values.point_coords,
        values.line_divisor,
        values.point_divisor,
    };
}

}  // namespace galaxy::render
