#pragma once

// The drawing state the game's records set, as typed values: the material record's and the state ops' (requirement
// E-02: the renderer reads no register words). It is built beside the register file (GxState) while the readers move
// to it one group at a time; a check mode compares the two at every draw.

#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/native_material.h"
#include "galaxy/gx/native_state.h"

#include <string>

namespace galaxy::gx {

// One texture map: its image, sampling (with the edge LOD and bias clamp enables) and palette.
struct TextureMap {
    TexImage image{1u, 1u, TexFormat::I4, 0u};
    TexMode mode{};
    bool edge_lod = true;
    bool bias_clamp = false;
    TlutRef tlut{};

    [[nodiscard]] bool operator==(const TextureMap&) const = default;
};

// One stage's indirect texturing (GXSetTevIndirect).
struct IndirectStage {
    std::uint8_t stage, format, bias, alpha, matrix, wrap_s, wrap_t;
    bool lod, prev;
};

struct NativeDrawState {
    // Fixed-function state: the counts and cull of the generation mode, depth, blend (as the game set it; the
    // pixel engine's effective blend is effective_blend's), the pixel format and the alpha compare.
    GenMode gen{0u, 0u, 1u, CullMode::None, 0u, false};
    ZMode z{false, CompareFunc::Never, false};
    BlendMode blend{false, false, false, false, false, BlendFactor::Zero, BlendFactor::Zero, false, 0u, false, 0u};
    PeControl pe{0u, 0u, false};
    AlphaCompare alpha{0u, 0u, CompareFunc::Never, CompareFunc::Never, AlphaTestLogic::And};

    // TEV: the stages, the four swap tables (each channel's source: red, green, blue, alpha), the fog's type, the
    // Z texture (op 0 off, 1 add, 2 replace; type 0 Z8, 1 Z16, 2 Z24X8; bias in Z24 units).
    TevStageConfig tev[16]{};
    std::uint8_t swap[4][4]{};
    FogType fog_type{FogType::Off};
    struct { std::uint8_t op, type; std::uint32_t bias; } z_texture{};
    // Indirect texturing: each TEV stage's, and the four indirect stages' map, coordinate and scales.
    IndirectStage ind[16]{};
    struct { std::uint8_t map, coord, scale_s, scale_t; } ind_order[4]{};
    // Indirect matrices (GXSetIndTexMtx): 1.10 terms by row (s, t) and column, and the scale exponent (held as
    // exponent + 17 in six bits: an unset matrix's is -17).
    struct { std::int16_t m[2][3]{}; std::int8_t scale = -17; } ind_matrix[3]{};

    // TEV registers PREV and REG0-2 (signed 11-bit; red, green, blue, alpha) and the four konst colours.
    std::int16_t tev_color[4][4]{};
    std::uint8_t konst[4][4]{};
    // Fog (GXSetFog): A and C as the pixel engine holds them (floats cut to 11 mantissa bits), B as its 24-bit
    // mantissa and shift, the projection (true: orthographic) and the colour.
    struct { float a, c; std::uint32_t b_magnitude, b_shift; bool projection; std::uint8_t color[3]; } fog{};

    // Vertex lighting: the channel controls (colour 0, colour 1, alpha 0, alpha 1) and the material and ambient
    // colours (RGBA) of channels 0 and 1.
    XfChannelCtrl channel[4]{};
    std::uint8_t material_color[2][4]{};
    std::uint8_t ambient_color[2][4]{};
    // Texture coordinate generation and each coordinate's post-transform matrix.
    XfTexGen texgen[8]{};
    XfPostTexGen post_texgen[8]{};
    // The projection's six terms and type (0 perspective), the viewport (half width, half height, depth range,
    // centre x, centre y, far).
    float projection[6]{};
    std::uint8_t projection_type = 0u;
    float viewport[6]{};
    // The current matrices: the position/normal matrix row and each texture coordinate's.
    std::uint8_t position_matrix = 0u;
    std::uint8_t texture_matrix[8]{};
    // Lines and points: width and size (sixths of a pixel), their texture coordinate offsets.
    struct { std::uint8_t line_width, point_size, line_offset, point_offset; } line_point{};
    // Each coordinate's rasterizer scale (s, t: the scale minus one, bias, cylindrical wrap) and its line and point
    // offset enables.
    struct {
        std::uint16_t size_minus_one[2];
        bool bias[2], wrap[2], line_offset, point_offset;
    } coord[8]{};

    TextureMap texture[8]{};

    // Palettes (GXLoadTlut and J3D's textures): each 512-byte place's source address (0: never loaded), and a
    // revision that changes whenever a place's address does.
    std::uint32_t palette_place[1024]{};
    std::uint32_t palette_revision = 0u;

    // Copy settings: the clear colour (RGBA) and depth, the vertical filter's taps, the display copy's Y scale (8.8).
    struct {
        std::uint8_t clear_color[4];
        std::uint32_t clear_z;
        std::uint8_t vfilter[7];
        std::uint16_t y_scale;
    } copy_setup{};

    // The scissor box (screen pixels, corners inclusive) and offset (GXSetScissor, GXSetScissorBoxOffset).
    ScissorRect scissor{-342, -342, -342, -342};
    ScissorOffset scissor_offset{};

    // A state op (galaxy/gx/native_state.h), as the GX function it names sets the state.
    void apply(const state::Op& op);
    // A material record: the whole material, or (diff_flags) the parts J3D's change flags name.
    void apply(const material::Material& material);
};

// The indirect state in the pixel shader's packed form (its key and constants): a stage's command, the four stages'
// maps and coordinates, a pair of stages' scales.
[[nodiscard]] std::uint32_t ind_cmd_word(const IndirectStage& stage);
// A TEV stage's colour and alpha operations in the pixel shader's packed form (its constants).
[[nodiscard]] std::uint32_t tev_color_word(const TevStageConfig& stage);
[[nodiscard]] std::uint32_t tev_alpha_word(const TevStageConfig& stage);
[[nodiscard]] std::uint32_t ind_ref_word(const NativeDrawState& state);
[[nodiscard]] std::uint32_t ind_scale_word(const NativeDrawState& state, unsigned pair);
// The shaders' packed forms of the vertex state (their constants and keys).
[[nodiscard]] std::uint32_t channel_word(const XfChannelCtrl& channel);
[[nodiscard]] std::uint32_t texgen_word(const XfTexGen& texgen);
[[nodiscard]] std::uint32_t post_texgen_word(const XfPostTexGen& post);
[[nodiscard]] std::uint32_t rgba_word(const std::uint8_t* rgba);
// The matrix indices as two words (position and coordinates 0-3; coordinates 4-7), six bits each.
[[nodiscard]] std::uint32_t matrix_index_a(const NativeDrawState& state);
[[nodiscard]] std::uint32_t matrix_index_b(const NativeDrawState& state);
[[nodiscard]] std::uint32_t line_point_word(const NativeDrawState& state);
// Coordinate `c`'s s (with the line and point offsets) and t scale words.
[[nodiscard]] std::uint32_t coord_s_word(const NativeDrawState& state, unsigned c);
[[nodiscard]] std::uint32_t coord_t_word(const NativeDrawState& state, unsigned c);

// A texture map's packed forms (the check mode's comparison with the register file).
[[nodiscard]] std::uint32_t tex_mode0_word(const TextureMap& map);
[[nodiscard]] std::uint32_t tex_mode1_word(const TextureMap& map);
[[nodiscard]] std::uint32_t tex_image0_word(const TextureMap& map);
[[nodiscard]] std::uint32_t tlut_word(const TextureMap& map);

// Map `map`'s palette address (0 without a palette format or a loaded place).
[[nodiscard]] std::uint32_t palette_address(const NativeDrawState& state, unsigned map);

// A copy op's parameters (GXCopyDisp, GXCopyTex) with the copy settings, and the pixel format the copy reads (Z24
// for a depth-format texture copy, as GXCopyTex switches it).
[[nodiscard]] EfbCopyParams efb_copy_params(const NativeDrawState& state, const state::Op& copy);
[[nodiscard]] std::uint8_t copy_pixel_format(const NativeDrawState& state, const state::Op& copy);

// Konst colour `k` as RGBA8 (red in the top byte).
[[nodiscard]] std::uint32_t konst_rgba(const NativeDrawState& state, unsigned k);

// The fields where `state` differs from `expected` (empty when equal), for the check mode.
[[nodiscard]] std::string draw_state_differences(const NativeDrawState& state, const NativeDrawState& expected);

}  // namespace galaxy::gx
