#pragma once

// The drawing state's typed values (blend, depth, TEV orders, texture sampling and images, texgens, channels) and
// their enumerations.

#include <cstdint>

namespace galaxy::render {

enum class CompareFunc : std::uint8_t {
    Never = 0, Less, Equal, LEqual, Greater, NEqual, GEqual, Always,
};

enum class BlendFactor : std::uint8_t {
    Zero = 0, One, SrcColor, InvSrcColor,
    SrcAlpha, InvSrcAlpha, DstAlpha, InvDstAlpha,
};

enum class CullMode : std::uint8_t { None = 0, Front, Back, All };

enum class TexFormat : std::uint8_t {
    I4 = 0x0, I8 = 0x1, IA4 = 0x2, IA8 = 0x3,
    RGB565 = 0x4, RGB5A3 = 0x5, RGBA8 = 0x6,
    C4 = 0x8, C8 = 0x9, C14X2 = 0xA, CMPR = 0xE,
};

enum class TlutFormat : std::uint8_t { IA8 = 0, RGB565 = 1, RGB5A3 = 2 };

enum class TexWrap : std::uint8_t { Clamp = 0, Repeat, Mirror };

enum class TexMagFilter : std::uint8_t { Near = 0, Linear };

enum class TexMinFilter : std::uint8_t {
    Near = 0, NearMipNear, NearMipLinear,
    Linear = 4, LinearMipNear, LinearMipLinear,
};

enum class FogType : std::uint8_t {
    Off = 0, Linear = 2, Exp = 4, Exp2 = 5, RevExp = 6, RevExp2 = 7,
};

// Draw opcode bits 7:3.
enum class PrimitiveClass : std::uint8_t {
    Quads = 0x10, Quads2 = 0x11, Triangles = 0x12,
    TriangleStrip = 0x13, TriangleFan = 0x14,
    Lines = 0x15, LineStrip = 0x16, Points = 0x17,
};

enum class TexGenType : std::uint8_t {
    Regular = 0, EmbossMap = 1, Color0 = 2, Color1 = 3,
};

struct GenMode {
    std::uint8_t num_texgens;       // 0-8
    std::uint8_t num_color_chans;   // 0-2
    std::uint8_t num_tev_stages;    // 1-16 (register stores count-1)
    CullMode cull;
    std::uint8_t num_ind_stages;    // 0-4
    bool z_freeze;
};

struct ZMode {
    bool test_enable;
    CompareFunc func;
    bool update_enable;
};

struct BlendMode {
    bool blend_enable;
    bool logic_op_enable;
    bool dither;
    bool color_update;
    bool alpha_update;
    BlendFactor dst_factor;
    BlendFactor src_factor;
    bool subtract;
    std::uint8_t logic_mode;        // 4-bit GX logic op
    bool const_alpha_enable;        // PE_CMODE1
    std::uint8_t const_alpha;
};

// GX logic op 3: the source unchanged.
inline constexpr std::uint8_t kLogicCopy = 3u;

// The blend the pixel engine performs, as the draw's result shows it: a blend that writes the fragment's colour unchanged
// is no blend. The pixel engine blends (or subtracts) where blending is on, else applies the logic op where that is on
// (Dolphin's BlendingState). A blend of ONE and ZERO, and the logic op COPY, write the source exactly as an unblended draw
// does; so does DSTALPHA and INVDSTALPHA where the EFB has no alpha (`efb_has_alpha` false: its alpha reads as one). The
// game draws opaque surfaces with such states (the far side of Bonefin's ocean sphere: BLEND ONE ZERO, depth written),
// and whatever tells translucent draws from opaque ones (the pipeline's blend key, the motion target, the ray tracer's
// geometry and its translucent lighting) must take them for the opaque draws they are.
[[nodiscard]] constexpr BlendMode effective_blend(BlendMode b, bool efb_has_alpha) {
    const auto factor = [efb_has_alpha](BlendFactor f) {
        if (!efb_has_alpha && f == BlendFactor::DstAlpha) {
            return BlendFactor::One;
        }
        if (!efb_has_alpha && f == BlendFactor::InvDstAlpha) {
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

struct PeControl {
    std::uint8_t pixel_format;      // 0=RGB8_Z24, 1=RGBA6_Z24, 2=RGB565_Z16, ...
    std::uint8_t z_format;
    bool early_z;                   // zcomploc: Z test before texture
};

struct TexImage {
    std::uint16_t width;            // texels (register stores size-1)
    std::uint16_t height;
    TexFormat format;
    std::uint64_t address;       // physical byte address (reg value << 5)

    [[nodiscard]] bool operator==(const TexImage&) const = default;
};

struct TexMode {
    TexWrap wrap_s;
    TexWrap wrap_t;
    TexMagFilter mag_filter;
    TexMinFilter min_filter;
    std::int16_t lod_bias_x32;      // signed, 1/32 LOD units
    std::uint8_t max_aniso;         // 0=1x, 1=2x, 2=4x
    std::uint8_t min_lod_x16;       // unsigned, 1/16 LOD units
    std::uint8_t max_lod_x16;

    [[nodiscard]] bool operator==(const TexMode&) const = default;
};

struct TlutRef {
    std::uint16_t tmem_offset;      // TLUT slot in TMEM high bank
    TlutFormat format;

    [[nodiscard]] bool operator==(const TlutRef&) const = default;
};

struct ScissorRect {
    std::int32_t x0, y0, x1, y1;    // inclusive, screen pixel space
};

struct EfbCopyParams {
    std::uint16_t src_x, src_y;
    std::uint16_t src_width, src_height;
    std::uint64_t dest_addr;        // the destination's address
    std::uint8_t channels;          // the layout the picture is read as (efb_copy.h: NebulaTargetChannels)
    bool clamp_top, clamp_bottom;
    bool half_scale;                // vertical 2x downsample
    bool clear;                     // clear EFB rect after copy
    bool copy_to_xfb;               // true = display copy, false = texture copy
    bool intensity;                 // intensity conversion (I and IA texture copies)
    std::uint8_t clear_color[4];    // red, green, blue, alpha
    std::uint32_t clear_z;          // 24-bit depth
    // Through the display's vertical filter (NEBULA_CAPTURE_FILTERED, frame_show): each line half its own colour and
    // half the line above's, as the game's television modes asked. Otherwise the line alone.
    bool filtered;

    // The filter's weights on the line above, the line and the line below, in 64ths.
    [[nodiscard]] constexpr std::uint32_t filter_upper() const { return filtered ? 32u : 0u; }
    [[nodiscard]] constexpr std::uint32_t filter_middle() const { return filtered ? 32u : 64u; }
    [[nodiscard]] constexpr std::uint32_t filter_lower() const { return 0u; }
};

struct XfTexGen {
    bool generate_stq;              // false = ST output
    bool input_form_abc1;           // input has implicit w=1
    TexGenType type;
    std::uint8_t source_row;        // position/normal/color/tex0-7 input row
    std::uint8_t emboss_source;
    std::uint8_t emboss_light;

    [[nodiscard]] bool operator==(const XfTexGen&) const = default;
};

struct XfPostTexGen {
    std::uint8_t matrix_index;      // post-transform matrix row
    bool normalize;

    [[nodiscard]] bool operator==(const XfPostTexGen&) const = default;
};

struct XfChannelCtrl {
    bool material_from_vertex;      // matsource
    bool lighting_enable;
    std::uint8_t light_mask;        // 8 lights
    bool ambient_from_vertex;       // ambsource
    std::uint8_t diffuse_func;      // 0=none, 1=signed, 2=clamped
    std::uint8_t atten_func;        // Dolphin XFMemory.h AttenuationFunc:
                                    // 0=none, 1=specular, 2=dir, 3=spot
};

[[nodiscard]] constexpr bool cull_all_suppresses_primitive(
    CullMode cull, PrimitiveClass primitive) noexcept {
    return cull == CullMode::All &&
        (primitive == PrimitiveClass::Triangles ||
         primitive == PrimitiveClass::TriangleStrip ||
         primitive == PrimitiveClass::TriangleFan ||
         primitive == PrimitiveClass::Quads ||
         primitive == PrimitiveClass::Quads2);
}
[[nodiscard]] constexpr std::uint8_t cmpr_interpolated_channel(
    std::uint8_t primary, std::uint8_t secondary) {
    return static_cast<std::uint8_t>((5u * primary + 3u * secondary) >> 3u);
}
// Compile-time checks of the helpers above.
namespace bitfield_checks {

// Blends as typed values: enable, logic op, dither, colour and alpha update, destination and source factors, subtract,
// logic mode, constant alpha.
constexpr BlendMode blend_of(bool blend, bool logic, BlendFactor dst, BlendFactor src, bool subtract, std::uint8_t mode) {
    return BlendMode{blend, logic, false, true, false, dst, src, subtract, mode, false, 0u};
}
constexpr auto kBlend = blend_of(true, false, BlendFactor::InvSrcAlpha, BlendFactor::SrcAlpha, false, 0u);

// effective_blend: an alpha blend stays a blend; BLEND ONE ZERO and logic COPY write the source (no blend, no logic op);
// DSTALPHA / INVDSTALPHA write the source only where the EFB has no alpha; a subtract of ONE ZERO still subtracts.
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
