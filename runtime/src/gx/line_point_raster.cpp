#include "galaxy/gx/line_point_raster.h"
#include "galaxy/gx/render_config.h"

#include <algorithm>
#include <cmath>

namespace galaxy::gx {

LinePointRasterParams make_line_point_raster_params(
    std::uint32_t su_lp_size,
    const std::array<std::uint32_t, 8>& texcoord_s_registers,
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

    std::uint32_t line_texcoord_mask = 0u;
    std::uint32_t point_texcoord_mask = 0u;
    for (unsigned texcoord = 0u; texcoord < texcoord_s_registers.size(); ++texcoord) {
        const std::uint32_t reg = texcoord_s_registers[texcoord];
        if ((reg & (1u << 18u)) != 0u) {
            line_texcoord_mask |= 1u << texcoord;
        }
        if ((reg & (1u << 19u)) != 0u) {
            point_texcoord_mask |= 1u << texcoord;
        }
    }

    // GX's line/point texture-offset selectors encode reciprocal divisors.
    // Selector zero disables the offset; 1..7 map exactly as on hardware.
    static constexpr std::array<std::uint32_t, 8> kTextureOffsetDivisors{
        0u, 16u, 8u, 4u, 2u, 1u, 1u, 1u};
    const unsigned line_offset = (su_lp_size >> 16u) & 0x7u;
    const unsigned point_offset = (su_lp_size >> 19u) & 0x7u;

    return LinePointRasterParams{
        logical_width * physical_scale,
        logical_height * physical_scale,
        (static_cast<float>(su_lp_size & 0xFFu) / 6.0f) * physical_scale,
        (static_cast<float>((su_lp_size >> 8u) & 0xFFu) / 6.0f) *
            physical_scale,
        line_texcoord_mask,
        point_texcoord_mask,
        kTextureOffsetDivisors[line_offset],
        kTextureOffsetDivisors[point_offset],
    };
}

}  // namespace galaxy::gx
