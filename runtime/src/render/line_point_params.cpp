// LinePointValues and the viewport as the line/point geometry shaders' parameters.

#include "galaxy/render/line_point_params.h"

#include "galaxy/render/frame_pass.h"

#include "galaxy/render/render_config.h"

#include <algorithm>
#include <cmath>

namespace galaxy::render {

LinePointRasterParams make_line_point_raster_params(
    const LinePointValues& values,
    float viewport_width,
    float viewport_height,
    RasterScale scale) noexcept {
    const bool valid_viewport =
        std::isfinite(viewport_width) &&
        std::isfinite(viewport_height) &&
        viewport_width > 0.0f && viewport_height > 0.0f;
    const float logical_width = valid_viewport
        ? viewport_width
        : static_cast<float>(kFrameUnits);
    const float logical_height = valid_viewport
        ? viewport_height
        : static_cast<float>(kFrameUnits);
    return LinePointRasterParams{
        logical_width * scale.x,
        logical_height * scale.y,
        values.line_width * scale.y,
        values.point_size * scale.y,
        values.line_coords,
        values.point_coords,
        values.line_divisor,
        values.point_divisor,
    };
}

}  // namespace galaxy::render
