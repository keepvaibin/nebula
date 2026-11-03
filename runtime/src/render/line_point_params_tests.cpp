// Tests of the line and point parameters and of the corners they become (native_shapes.h widen_lines_points).
#include "galaxy/render/drawing_state.h"
#include "galaxy/render/line_point_params.h"
#include "galaxy/render/native_shapes.h"
#include "galaxy/render/render_vertex.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

namespace {

bool expect(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", label);
        return false;
    }
    return true;
}

bool expect_near(float actual, float expected, const char* label) {
    if (std::abs(actual - expected) > 0.0001f) {
        std::fprintf(
            stderr,
            "FAILED: %s (actual=%g expected=%g)\n",
            label,
            static_cast<double>(actual),
            static_cast<double>(expected));
        return false;
    }
    return true;
}

}  // namespace

int main() {
    bool passed = true;
    try {
        const galaxy::render::LinePointRasterParams decoded =
            galaxy::render::make_line_point_raster_params(
                {1.0f / 6.0f, 255.0f / 6.0f, 8u, 1u, 0x81u, 0x88u}, 20.0f, 40.0f, galaxy::render::RasterScale{3.0f, 3.0f});
        passed &= expect_near(
            decoded.viewport_width_pixels,
            60.0f,
            "viewport width uses the frame scale");
        passed &= expect_near(
            decoded.viewport_height_pixels,
            120.0f,
            "viewport height uses the frame scale");
        passed &= expect_near(
            decoded.line_width_pixels,
            0.5f,
            "line width keeps sixth-of-a-pixel values");
        passed &= expect_near(
            decoded.point_size_pixels,
            127.5f,
            "point size keeps sixth-of-a-pixel values");
        passed &= expect(
            decoded.line_texcoord_mask == 0x81u &&
                decoded.point_texcoord_mask == 0x88u,
            "line/point texture-coordinate masks pass through independently");
        passed &= expect(
            decoded.line_texcoord_divisor == 8u &&
                decoded.point_texcoord_divisor == 1u,
            "line/point texture-offset selectors give their divisors");

        const galaxy::render::LinePointRasterParams fallback =
            galaxy::render::make_line_point_raster_params(
                {1.0f, 2.0f}, 0.0f, 0.0f, galaxy::render::RasterScale{1280.0f / 65536.0f, 720.0f / 65536.0f});
        passed &= expect_near(
            fallback.viewport_width_pixels,
            1280.0f,
            "unset viewport covers the frame's width");
        passed &= expect_near(
            fallback.viewport_height_pixels,
            720.0f,
            "unset viewport covers the frame's height");

        galaxy::render::NativeDrawState state;
        (void)state.consume_dirty();
        galaxy::render::shapes::PassState line{};
        line.line_width = 1.0f;  // one pixel
        state.apply_pass(line);
        passed &= expect(
            (state.consume_dirty() & galaxy::render::kStateDirtyVertexConstants) != 0u,
            "a line width refreshes line/point GPU constants");
        galaxy::render::state::Op viewport{};
        viewport.kind = galaxy::render::state::kViewport;
        viewport.f[0] = 32.0f;
        viewport.f[1] = -32.0f;
        state.apply(viewport);
        passed &= expect(
            (state.consume_dirty() & galaxy::render::kStateDirtyVertexConstants) != 0u,
            "a viewport's width and height refresh line/point GPU constants");

        namespace shapes = galaxy::render::shapes;
        galaxy::render::RenderVertex in[3]{};
        in[1].position[0] = 5.0f;
        in[1].mtx_indices[0] = 3u;
        std::vector<galaxy::render::RenderVertex> corners(shapes::widened_count(shapes::kLineStrip, 3u));
        passed &= expect(corners.size() == 8u, "a strip of three vertices is two lines of four corners");
        shapes::widen_lines_points(shapes::kLineStrip, 3u, in, corners.data());
        passed &= expect(corners[0].tangent[0] == 5.0f && corners[0].curve[0] == 3u, "a corner holds its other end and that end's row");
        passed &= expect((corners[1].mtx_indices[2] & galaxy::render::kVertexWidenRight) != 0u, "a line's second corner is its moved edge");
        passed &= expect(shapes::widened_count(shapes::kPoints, 2u) == 8u, "two points are eight corners");
        passed &= expect(shapes::widened_count(shapes::kQuads, 4u) == 0u, "a quad is not widened");
        std::vector<std::uint16_t> indices;
        shapes::primitive_indices(shapes::kWideQuads, 4u, indices);
        passed &= expect(indices == std::vector<std::uint16_t>{0u, 1u, 2u, 2u, 1u, 3u}, "four corners are a strip");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: unexpected exception: %s\n", error.what());
        passed = false;
    }
    return passed ? 0 : 1;
}
