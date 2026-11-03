#pragma once

// The drawing state's typed values (blend, depth, cull, counts, primitives, texgens, channels) and their enumerations.

#include "galaxy/render/texture_sampler.h"
#include <cstdint>

namespace galaxy::render {

enum class CompareFunc : std::uint8_t {
    Never = 0, Less, Equal, LEqual, Greater, NEqual, GEqual, Always,
};

// NEBULA_FACTOR_*: each colour factor names its own side (DstColor is the destination's colour).
enum class BlendFactor : std::uint8_t {
    Zero = 0, One, SrcColor, InvSrcColor,
    SrcAlpha, InvSrcAlpha, DstAlpha, InvDstAlpha,
    DstColor, InvDstColor,
};

enum class CullMode : std::uint8_t { None = 0, Front, Back, All };

// Texture pack file names carry a format code (tex1_WxH_<hash>_<code>).
enum class TexturePackFormat : std::uint8_t {
    Gray8 = 0x1, GrayAlpha8 = 0x3, Rgb565 = 0x4, Packed16 = 0x5, Rgba8 = 0x6,
    Palette4 = 0x8, Palette8 = 0x9, Palette14 = 0xA, Block4 = 0xE,
};



// How a draw's vertices group: NebulaPrimitive's values.
enum class Primitive : std::uint8_t {
    Quads = 0, Triangles, TriangleStrip, TriangleFan, Lines, LineStrip, Points,
};

enum class TexGenType : std::uint8_t {
    Regular = 0, EmbossMap = 1, Color0 = 2, Color1 = 3,
};

// A draw's counts (texture coordinates, colour channels, shader stages, indirect stages) and its cull.
struct DrawCounts {
    std::uint8_t num_texgens;       // 0-8
    std::uint8_t num_color_chans;   // 0-2
    std::uint8_t num_stages;    // 1-16
    CullMode cull;
    std::uint8_t num_ind_stages;    // 0-4
};

struct ZMode {
    bool test_enable;
    CompareFunc func;
    bool update_enable;
};

struct BlendMode {
    bool blend_enable;
    bool logic_op_enable;
    bool color_update;
    bool alpha_update;
    BlendFactor dst_factor;
    BlendFactor src_factor;
    bool subtract;
    std::uint8_t logic_mode;        // VkLogicOp's order (clear, and, reverse and, copy, ..., set)
    bool const_alpha_enable;        // the frame's alpha written as const_alpha
    std::uint8_t const_alpha;
};

// Logic op 3 (copy): the source unchanged.
inline constexpr std::uint8_t kLogicCopy = 3u;

// The blend the pixel engine performs, as the draw's result shows it: a blend that writes the fragment's colour unchanged
// is no blend. The draw blends (or subtracts) where blending is on, else applies the logic op where that is on. A blend of ONE and ZERO, and the logic op COPY, write the source exactly as an unblended draw
// does; so does DSTALPHA and INVDSTALPHA where the scene target has no alpha (`scene_has_alpha` false: its alpha reads as one). The
// game draws opaque surfaces with such states (the far side of Bonefin's ocean sphere: BLEND ONE ZERO, depth written),
// and whatever tells translucent draws from opaque ones (the pipeline's blend key, the motion target, the ray tracer's
// geometry and its translucent lighting) must take them for the opaque draws they are.
[[nodiscard]] constexpr BlendMode effective_blend(BlendMode b, bool scene_has_alpha) {
    const auto factor = [scene_has_alpha](BlendFactor f) {
        if (!scene_has_alpha && f == BlendFactor::DstAlpha) {
            return BlendFactor::One;
        }
        if (!scene_has_alpha && f == BlendFactor::InvDstAlpha) {
            return BlendFactor::Zero;
        }
        return f;
    };
    const bool replaces = b.blend_enable ? !b.subtract && factor(b.src_factor) == BlendFactor::One &&
                                               factor(b.dst_factor) == BlendFactor::Zero
                                         : b.logic_op_enable && b.logic_mode == kLogicCopy;
    if (replaces) {
        b.blend_enable = false;
        b.logic_op_enable = false;
        b.subtract = false;
        b.src_factor = BlendFactor::One;
        b.dst_factor = BlendFactor::Zero;
        b.logic_mode = kLogicCopy;
    }
    return b;
}

struct ScissorRect {
    std::int32_t x0, y0, x1, y1;    // inclusive, screen pixel space
};

// A texture coordinate's generation (J3DNativeTexGen): its kind, the input row it reads (position, normal, binormal,
// tangent, coordinates), and for an emboss coordinate the coordinate it offsets and the light.
struct TexGen {
    TexGenType type;
    std::uint8_t source_row;
    std::uint8_t emboss_source;
    std::uint8_t emboss_light;

    [[nodiscard]] bool operator==(const TexGen&) const = default;
};

// A texture coordinate's second matrix (its row) and whether the coordinate is normalized before it.
struct PostTexGen {
    std::uint8_t matrix_index;
    bool normalize;

    [[nodiscard]] bool operator==(const PostTexGen&) const = default;
};

// A colour channel's lighting (J3DNativeChannel).
struct ChannelControl {
    bool material_from_vertex;
    bool lighting_enable;
    std::uint8_t light_mask;        // 8 lights
    bool ambient_from_vertex;
    std::uint8_t diffuse_func;      // 0 none, 1 signed, 2 clamped
    std::uint8_t atten_func;        // 0 none, 1 specular, 2 directional, 3 spot
};

[[nodiscard]] constexpr bool cull_all_suppresses_primitive(
    CullMode cull, Primitive primitive) noexcept {
    return cull == CullMode::All &&
        (primitive == Primitive::Triangles ||
         primitive == Primitive::TriangleStrip ||
         primitive == Primitive::TriangleFan ||
         primitive == Primitive::Quads);
}
// A block record's third and fourth colours (nebula_cooked_texture.h NEBULA_TEXTURE_BLOCK4): 5/8 and 3/8.
[[nodiscard]] constexpr std::uint8_t block4_interpolated_channel(
    std::uint8_t primary, std::uint8_t secondary) {
    return static_cast<std::uint8_t>((5u * primary + 3u * secondary) >> 3u);
}
// Compile-time checks of the helpers above.
namespace bitfield_checks {

// Blends as typed values: enable, logic op, colour and alpha update, destination and source factors, subtract,
// logic mode, constant alpha.
constexpr BlendMode blend_of(bool blend, bool logic, BlendFactor dst, BlendFactor src, bool subtract, std::uint8_t mode) {
    return BlendMode{blend, logic, true, false, dst, src, subtract, mode, false, 0u};
}
constexpr auto kBlend = blend_of(true, false, BlendFactor::InvSrcAlpha, BlendFactor::SrcAlpha, false, 0u);

// effective_blend: an alpha blend stays a blend; BLEND ONE ZERO and logic COPY write the source (no blend, no logic op);
// DSTALPHA / INVDSTALPHA write the source only where the scene target has no alpha; a subtract of ONE ZERO still subtracts.
static_assert(effective_blend(kBlend, true).blend_enable);
static_assert(effective_blend(kBlend, false).blend_enable);
constexpr auto kBlendOneZero = blend_of(true, false, BlendFactor::Zero, BlendFactor::One, false, 0u);
static_assert(kBlendOneZero.blend_enable && !effective_blend(kBlendOneZero, true).blend_enable);
static_assert(!effective_blend(kBlendOneZero, false).logic_op_enable);
static_assert(effective_blend(kBlendOneZero, true).color_update);
constexpr auto kBlendDstAlpha = blend_of(true, false, BlendFactor::InvDstAlpha, BlendFactor::DstAlpha, false, 0u);
static_assert(!effective_blend(kBlendDstAlpha, false).blend_enable);
static_assert(effective_blend(kBlendDstAlpha, true).blend_enable);
constexpr auto kSubtractOneZero = blend_of(true, false, BlendFactor::Zero, BlendFactor::One, true, 0u);
static_assert(effective_blend(kSubtractOneZero, true).blend_enable && effective_blend(kSubtractOneZero, true).subtract);
constexpr auto kLogicCopyMode = blend_of(false, true, BlendFactor::Zero, BlendFactor::Zero, false, 3u);
static_assert(!effective_blend(kLogicCopyMode, true).logic_op_enable);
constexpr auto kLogicXor = blend_of(false, true, BlendFactor::Zero, BlendFactor::Zero, false, 6u);
static_assert(effective_blend(kLogicXor, true).logic_op_enable);

}  // namespace bitfield_checks

}  // namespace galaxy::render
