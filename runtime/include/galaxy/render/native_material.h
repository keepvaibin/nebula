// SPDX-License-Identifier: GPL-3.0-only
// Native materials: the renderer's copy of the game's material record.
#pragma once

#include "galaxy/render/draw_matrices.h"
#include "galaxy/render/draw_types.h"

#include <cstdint>

namespace galaxy::render {
class DrawMatrices;
}

namespace galaxy::render::material {

// The renderer's copy of NebulaNativeMaterial (nebula_game_api.h): a shader compiled at build, its constants, textures
// and fixed-function state in the API's own enums.
struct Native {
    std::uint32_t shader;
    std::uint8_t blend_op, blend_src, blend_dst, logic_op;
    std::uint8_t depth_test, depth_func, depth_write, early_depth;
    std::uint8_t cull, color_write, alpha_write, light_mask;
    float constants[128];
    struct {
        std::uint64_t image;
        std::uint8_t repeat_s, repeat_t;
        std::uint8_t mag_linear, min_linear, mip_filter, anisotropy, edge_lod;
        float lod_bias, min_lod, max_lod;
        std::uint8_t reserved[3];
    } textures[8];
    NativeLight light[8];
    std::uint8_t texgen_post[8];
    std::uint32_t constants_only;
};
inline constexpr std::uint8_t kNativeBlendReplace = 0u, kNativeBlendAdd = 1u, kNativeBlendSubtract = 2u,
                              kNativeBlendLogic = 3u;
inline constexpr std::uint8_t kNativeFactorSrcColor = 2u, kNativeFactorInvSrcColor = 3u, kNativeFactorDstColor = 8u,
                              kNativeFactorInvDstColor = 9u;
inline constexpr std::uint8_t kNativeCullNone = 0u, kNativeCullBack = 1u, kNativeCullFront = 2u;
// blend_op, depth_test, cull: the drawing state's as it is (nebula_game_api.h NEBULA_NATIVE_KEPT).
inline constexpr std::uint8_t kNativeKept = 0xFFu;

// the state texgen word for `type` and `source` (J3DGDSetTexCoordGen's packing).
[[nodiscard]] TexGen tex_gen(std::uint8_t type, std::uint8_t source);
// A texgen's post-transform: 3n for matrix n, 61 for the identity, as its matrix row, and its normalize flag.
[[nodiscard]] inline PostTexGen post_tex_gen(std::uint32_t post_row, std::uint32_t normalize) {
    return PostTexGen{static_cast<std::uint8_t>(post_row & 63u), (normalize & 1u) != 0u};
}

}  // namespace galaxy::render::material
