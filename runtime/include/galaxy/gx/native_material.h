// SPDX-License-Identifier: GPL-3.0-only
// Native materials: a J3D material as the game sends it (nebula_game_api.h NebulaMaterial, the same layout: the host
// copies it), applied to the renderer's drawing state in the order J3D's material display list set it.
#pragma once

#include <cstdint>

namespace galaxy::gx {
struct FifoSink;
class GxState;
}

namespace galaxy::gx::material {

inline constexpr std::uint16_t kLoadsFog = 1u << 0;
inline constexpr std::uint16_t kLoadsDither = 1u << 1;
inline constexpr std::uint16_t kLoadsTevColor = 1u << 2;
inline constexpr std::uint16_t kLoadsKonstColor = 1u << 3;
inline constexpr std::uint16_t kLoadsKonstSelect = 1u << 4;
inline constexpr std::uint16_t kLoadsSwapTable = 1u << 5;
inline constexpr std::uint16_t kLoadsAmbient = 1u << 6;
inline constexpr std::uint16_t kLoadsLights = 1u << 7;
inline constexpr std::uint16_t kLoadsTev = 1u << 8;
inline constexpr std::uint16_t kLoadsChannels = 1u << 9;
inline constexpr std::uint16_t kLoadsIndirect = 1u << 10;

struct TevStage {
    std::uint8_t tex_coord, tex_map, channel;
    std::uint8_t color_in[4], color_op, color_bias, color_scale, color_clamp, color_reg;
    std::uint8_t alpha_in[4], alpha_op, alpha_bias, alpha_scale, alpha_clamp, alpha_reg;
    std::uint8_t ras_swap, tex_swap, konst_color, konst_alpha;
    std::uint8_t ind_stage, ind_format, ind_bias, ind_matrix, ind_wrap_s, ind_wrap_t, ind_prev, ind_lod, ind_alpha;
};

struct Texture {
    std::uint64_t image;  // host-side: the image's physical address (0: no texture)
    std::uint16_t width, height;
    std::uint8_t format, wrap_s, wrap_t, min_filter, mag_filter, mipmaps, edge_lod, anisotropy;
    float min_lod, max_lod, lod_bias;
    std::uint64_t palette;  // host-side: the palette's physical address
    std::uint16_t palette_entries;
    std::uint8_t palette_format, bias_clamp;
};

struct Light {
    float position[3], direction[3];
    float angle[3], distance[3];
    std::uint8_t color[4];
};

struct Material {
    std::uint8_t tev_stages, tex_gens, channels, ind_stages, cull;
    std::uint8_t dither, z_compare_location;
    std::uint8_t origin;  // 1 a packet's list, 2 a differed list, 3 a shared list, 0 built at the draw
    std::uint16_t loads;
    TevStage tev[16];
    std::int16_t tev_color[4][4];
    std::uint8_t konst_color[4][4];
    std::uint8_t swap_table[4][4];
    struct { std::uint8_t coord, map, scale_s, scale_t; } ind_order[4];
    struct { float m[2][3]; std::int8_t scale; std::uint8_t reserved[3]; } ind_matrix[3];
    struct { std::uint8_t type, source, matrix, normalize, post_matrix, reserved[3]; } tex_gen[8];
    struct { std::uint16_t scale_s, scale_t; std::uint8_t bias_s, bias_t, wrap_s, wrap_t; } tex_coord_scale[8];
    Texture texture[8];
    struct { std::uint8_t enable, material_source, ambient_source, light_mask, diffuse, attenuation, reserved[2]; } channel[4];
    std::uint8_t material_color[2][4], ambient_color[2][4];
    std::uint8_t light_mask;
    Light light[8];
    struct { std::uint8_t type, adjust_enable; std::uint16_t adjust_center; float start, end, near_z, far_z;
             std::uint8_t color[4]; std::uint16_t adjust[10]; } fog;
    struct { std::uint8_t comp0, ref0, op, comp1, ref1, reserved[3]; } alpha_compare;
    struct { std::uint8_t type, source, destination, logic_op; } blend;
    struct { std::uint8_t enable, func, update, reserved; } z_mode;
    std::uint32_t diff_flags;  // 0: the whole material; else J3D's diff flags (apply sets only what they name)
    struct { std::uint8_t present, listed, id, rows; float m[12]; } texture_matrix[8];
};

// GXSetTexCoordGen's texgen word for `type` and `source` (J3DGDSetTexCoordGen's packing).
[[nodiscard]] std::uint32_t tex_gen_word(std::uint8_t type, std::uint8_t source);

// Sets `state` as J3D's material display list did (J3DMaterial::makeDisplayList_private's order: TEV block,
// indirect block, pixel block, mode, texgens, colour block, channel and texgen counts); `actions` (or null) takes
// what the writes start, as the parser's sink would (a palette's copy).
void apply(const Material& material, GxState& state, FifoSink* actions = nullptr);

}  // namespace galaxy::gx::material
