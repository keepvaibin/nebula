#pragma once

// Raw CP/XF/BP register bitfield decode for the GX command stream.
// Header-only constexpr, no Windows/D3D12 includes — unit-testable offline
// against captured .gxdump streams.
//
// Bit layouts follow the Wii GX hardware registers as programmed by the
// RVL SDK GX library (the only producer of the RMGE01 FIFO stream).

#include <cstdint>

namespace galaxy::gx {

[[nodiscard]] constexpr std::uint32_t bits(
    std::uint32_t value,
    unsigned lo,
    unsigned width) {
    return (value >> lo) & ((1u << width) - 1u);
}

[[nodiscard]] constexpr bool bit(std::uint32_t value, unsigned index) {
    return ((value >> index) & 1u) != 0u;
}

// ---------------------------------------------------------------------------
// BP (Blitting Processor / pixel pipeline) register addresses.
// A LOAD_BP_REG payload is one u32: register = bits 31:24, value = bits 23:0.
// ---------------------------------------------------------------------------
namespace bp {
inline constexpr std::uint8_t kGenMode = 0x00;
inline constexpr std::uint8_t kDisplayCopyFilterBase = 0x01; // 0x01-0x04
inline constexpr std::uint8_t kIndMtxBase = 0x06;       // 0x06-0x0E: 3x2 ind mtx
inline constexpr std::uint8_t kIndMtxCount = 9;
inline constexpr std::uint8_t kIndMask = 0x0F;
inline constexpr std::uint8_t kBpMask = 0xFE;           // one-shot write mask
inline constexpr std::uint8_t kIndCmdBase = 0x10;       // 0x10-0x1F per ind stage
inline constexpr std::uint8_t kScissorTopLeft = 0x20;
inline constexpr std::uint8_t kScissorBottomRight = 0x21;
inline constexpr std::uint8_t kSuLpSize = 0x22;         // line/point size
// BPMEM_PERF0_TRI/BPMEM_PERF0_QUAD: real-hardware perf-query triangle/quad
// counters. Confirmed inert for rendered output in Dolphin's BPWritten
// reference (both cases are a bare `return;` feeding only the perf-query
// counter subsystem, never TEV/shader/blend/rasterizer state).
inline constexpr std::uint8_t kPerf0TriBase = 0x23;     // 0x23-0x24
inline constexpr std::uint8_t kRas1Ss0 = 0x25;          // ind tex scale 0
inline constexpr std::uint8_t kRas1Ss1 = 0x26;          // ind tex scale 1
inline constexpr std::uint8_t kIndRef = 0x27;           // ind stage tex refs
inline constexpr std::uint8_t kTevOrderBase = 0x28;     // 0x28-0x2F, 2 stages/reg
inline constexpr std::uint8_t kTexCoordSizeBase = 0x30; // 0x30-0x3F, S/T pairs
inline constexpr std::uint8_t kTexCoordSizeCount = 16;
inline constexpr std::uint8_t kZMode = 0x40;
inline constexpr std::uint8_t kBlendMode = 0x41;        // PE_CMODE0
inline constexpr std::uint8_t kConstAlpha = 0x42;       // PE_CMODE1
inline constexpr std::uint8_t kPeControl = 0x43;
inline constexpr std::uint8_t kFieldMask = 0x44;
inline constexpr std::uint8_t kPeDone = 0x45;           // GXDrawDone signal
inline constexpr std::uint8_t kBusClock0 = 0x46;
inline constexpr std::uint8_t kPeToken = 0x47;          // GXSetDrawSync
inline constexpr std::uint8_t kPeTokenInt = 0x48;       // token w/ interrupt
inline constexpr std::uint8_t kCopySrcTopLeft = 0x49;
inline constexpr std::uint8_t kCopySrcSize = 0x4A;
inline constexpr std::uint8_t kCopyDestAddr = 0x4B;     // physical >> 5
inline constexpr std::uint8_t kCopyDestStride = 0x4D;   // bytes >> 5
inline constexpr std::uint8_t kCopyYScale = 0x4E;       // 8.8 fixed inverse
inline constexpr std::uint8_t kCopyClearAR = 0x4F;
inline constexpr std::uint8_t kCopyClearGB = 0x50;
inline constexpr std::uint8_t kCopyClearZ = 0x51;       // 24-bit Z
inline constexpr std::uint8_t kCopyExecute = 0x52;
inline constexpr std::uint8_t kCopyFilter0 = 0x53;
inline constexpr std::uint8_t kCopyFilter1 = 0x54;
inline constexpr std::uint8_t kCopyClearBoundingBox1 = 0x55;
inline constexpr std::uint8_t kCopyClearBoundingBox2 = 0x56;
inline constexpr std::uint8_t kCopyClearPixelPerf = 0x57;
inline constexpr std::uint8_t kRevBits = 0x58;
inline constexpr std::uint8_t kScissorOffset = 0x59;
inline constexpr std::uint8_t kTlutSrcAddr = 0x64;      // main memory >> 5
inline constexpr std::uint8_t kTlutDest = 0x65;         // TMEM offset + count
inline constexpr std::uint8_t kTexInvalidate = 0x66;
inline constexpr std::uint8_t kPerf1 = 0x67;
inline constexpr std::uint8_t kFieldMode = 0x68;
inline constexpr std::uint8_t kBusClock1 = 0x69;
// Texture units: registers for maps 0-3; maps 4-7 are at +0x20.
inline constexpr std::uint8_t kTexMode0Base = 0x80;     // wrap/filter/lod bias
inline constexpr std::uint8_t kTexMode1Base = 0x84;     // min/max lod
inline constexpr std::uint8_t kTexImage0Base = 0x88;    // width/height/format
inline constexpr std::uint8_t kTexImage1Base = 0x8C;    // TMEM even (ignored)
inline constexpr std::uint8_t kTexImage2Base = 0x90;    // TMEM odd (ignored)
inline constexpr std::uint8_t kTexImage3Base = 0x94;    // physical addr >> 5
inline constexpr std::uint8_t kTexTlutBase = 0x98;      // TLUT ref
inline constexpr std::uint8_t kTexHighBankOffset = 0x20;  // maps 4-7
inline constexpr std::uint8_t kTevColorEnvBase = 0xC0;  // even regs, 16 stages
inline constexpr std::uint8_t kTevAlphaEnvBase = 0xC1;  // odd regs, 16 stages
inline constexpr std::uint8_t kTevRegisterBase = 0xE0;  // 0xE0-0xE7 RA/BG pairs
inline constexpr std::uint8_t kFogRangeBase = 0xE8;     // 0xE8-0xED
inline constexpr std::uint8_t kFogRangeCount = 6;
inline constexpr std::uint8_t kFogParam0 = 0xEE;
inline constexpr std::uint8_t kFogParam1 = 0xEF;
inline constexpr std::uint8_t kFogParam2 = 0xF0;
inline constexpr std::uint8_t kFogParam3 = 0xF1;
inline constexpr std::uint8_t kFogColor = 0xF2;
inline constexpr std::uint8_t kAlphaCompare = 0xF3;
inline constexpr std::uint8_t kTevZEnv0 = 0xF4;
inline constexpr std::uint8_t kTevZEnv1 = 0xF5;
inline constexpr std::uint8_t kTevKSelBase = 0xF6;      // 0xF6-0xFD, 2 stages/reg
}  // namespace bp

// ---------------------------------------------------------------------------
// CP (Command Processor) register addresses (LOAD_CP_REG: u8 reg + u32 value).
// ---------------------------------------------------------------------------
namespace cp {
// 0x00/0x10/0x20: real-hardware CP registers of unknown purpose (Dolphin's
// own CPMemory.h names them UNKNOWN_00/UNKNOWN_10/UNKNOWN_20). Confirmed
// inert for vertex/geometry state in Dolphin's LoadCPReg reference
// implementation — stored but never consulted by any downstream state.
inline constexpr std::uint8_t kUnknown00 = 0x00;
inline constexpr std::uint8_t kUnknown10 = 0x10;
inline constexpr std::uint8_t kUnknown20 = 0x20;
inline constexpr std::uint8_t kMatrixIndexA = 0x30;
inline constexpr std::uint8_t kMatrixIndexB = 0x40;
inline constexpr std::uint8_t kVcdLo = 0x50;
inline constexpr std::uint8_t kVcdHi = 0x60;
inline constexpr std::uint8_t kVatABase = 0x70;         // +vtxfmt (0-7)
inline constexpr std::uint8_t kVatBBase = 0x80;
inline constexpr std::uint8_t kVatCBase = 0x90;
inline constexpr std::uint8_t kArrayBaseBase = 0xA0;    // +attr index (0-15)
inline constexpr std::uint8_t kArrayStrideBase = 0xB0;
}  // namespace cp

// ---------------------------------------------------------------------------
// XF (Transform Unit) memory addresses (LOAD_XF_REG: count, base, values).
// ---------------------------------------------------------------------------
namespace xf {
inline constexpr std::uint16_t kPosMatricesBase = 0x0000;  // 0x000-0x0FF
inline constexpr std::uint16_t kNormalMatricesBase = 0x0400;
inline constexpr std::uint16_t kPostMatricesBase = 0x0500;
inline constexpr std::uint16_t kLightsBase = 0x0600;
// XFMEM_ERROR..XFMEM_UNKNOWN_1007 (0x1000-0x1007) and XFMEM_VTXSPECS
// (0x1008): real Flipper XF status/diagnostic registers. Confirmed inert for
// rendering output (Dolphin's XFRegWritten treats 0x1000-0x1006 as literal
// no-ops, 0x1007 is unhandled/unknown-but-inert, and 0x1008 only toggles an
// internal CP/XF consistency-check flag with no shader/geometry effect).
inline constexpr std::uint16_t kStatusRegsBase = 0x1000;
inline constexpr std::uint16_t kNumChannels = 0x1009;
inline constexpr std::uint16_t kDualTexTrans = 0x1012;  // bit 0 = dual-tex
                                                        // (post-matrix) enable
// 0x1013-0x1017: XFMEM_UNKNOWN_GROUP_1, genuinely unknown/undocumented on
// real hardware (Dolphin's own XFRegWritten default case: analytics/log
// only, no xfmem state mutation). 0x1018-0x1019: XFMEM_SETMATRIXINDA/B, the
// XF-side latch of the current position/texture matrix index. This backend
// already sources every matrix-index consumer (vertex_loader.cpp,
// shader_gen.cpp, gx_backend.cpp) exclusively from the CP-side
// kMatrixIndexA/kMatrixIndexB copy the real SDK keeps synchronized, so the
// XF latch is write-only/unread here — inert, not stubbed.
inline constexpr std::uint16_t kUnknownGroup1Base = 0x1013;  // 0x1013-0x1019
inline constexpr std::uint16_t kAmbientColorBase = 0x100A;  // 0x100A-0x100B
inline constexpr std::uint16_t kMaterialColorBase = 0x100C; // 0x100C-0x100D
inline constexpr std::uint16_t kChannelCtrlBase = 0x100E;   // color0/1, alpha0/1
inline constexpr std::uint16_t kProjectionBase = 0x1020;    // 6 floats + type
inline constexpr std::uint16_t kNumTexGens = 0x103F;
inline constexpr std::uint16_t kTexGenBase = 0x1040;        // 0x1040-0x1047
inline constexpr std::uint16_t kPostTexGenBase = 0x1050;    // dual-tex transforms
inline constexpr std::uint16_t kViewportBase = 0x101A;      // 6 floats
}  // namespace xf

// ---------------------------------------------------------------------------
// Enumerations.
// ---------------------------------------------------------------------------

enum class TevColorArg : std::uint8_t {
    CPrev = 0, APrev, C0, A0, C1, A1, C2, A2,
    TexColor, TexAlpha, RasColor, RasAlpha, One, Half, Konst, Zero,
};

enum class TevAlphaArg : std::uint8_t {
    APrev = 0, A0, A1, A2, TexAlpha, RasAlpha, Konst, Zero,
};

// bias == Compare turns the stage into a comparator; the comparison width is
// then derived from (scale << 1) | op and the comparison itself from op.
enum class TevBias : std::uint8_t { Zero = 0, AddHalf, SubHalf, Compare };
enum class TevScale : std::uint8_t { X1 = 0, X2, X4, Half };
enum class TevCompareKind : std::uint8_t { R8 = 0, GR16, BGR24, RGB8 };
enum class TevCompareOp : std::uint8_t { Gt = 0, Eq };

enum class CompareFunc : std::uint8_t {
    Never = 0, Less, Equal, LEqual, Greater, NEqual, GEqual, Always,
};

enum class AlphaTestLogic : std::uint8_t { And = 0, Or, Xor, Xnor };

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

enum class VcdType : std::uint8_t { None = 0, Direct, Index8, Index16 };

enum class ComponentFormat : std::uint8_t { U8 = 0, S8, U16, S16, F32 };

enum class ColorComponentFormat : std::uint8_t {
    RGB565 = 0, RGB888, RGBX8888, RGBA4444, RGBA6666, RGBA8888,
};

enum class TexGenType : std::uint8_t {
    Regular = 0, EmbossMap = 1, Color0 = 2, Color1 = 3,
};

// ---------------------------------------------------------------------------
// Decoded register views.
// ---------------------------------------------------------------------------

struct GenMode {
    std::uint8_t num_texgens;       // 0-8
    std::uint8_t num_color_chans;   // 0-2
    std::uint8_t num_tev_stages;    // 1-16 (register stores count-1)
    CullMode cull;
    std::uint8_t num_ind_stages;    // 0-4
    bool z_freeze;
};

[[nodiscard]] constexpr GenMode decode_gen_mode(std::uint32_t v) {
    return GenMode{
        static_cast<std::uint8_t>(bits(v, 0, 4)),
        static_cast<std::uint8_t>(bits(v, 4, 3)),
        static_cast<std::uint8_t>(bits(v, 10, 4) + 1u),
        static_cast<CullMode>(bits(v, 14, 2)),
        static_cast<std::uint8_t>(bits(v, 16, 3)),
        bit(v, 19),
    };
}

struct ZMode {
    bool test_enable;
    CompareFunc func;
    bool update_enable;
};

[[nodiscard]] constexpr ZMode decode_z_mode(std::uint32_t v) {
    return ZMode{
        bit(v, 0),
        static_cast<CompareFunc>(bits(v, 1, 3)),
        bit(v, 4),
    };
}

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

[[nodiscard]] constexpr BlendMode decode_blend(
    std::uint32_t cmode0,
    std::uint32_t cmode1) {
    return BlendMode{
        bit(cmode0, 0),
        bit(cmode0, 1),
        bit(cmode0, 2),
        bit(cmode0, 3),
        bit(cmode0, 4),
        static_cast<BlendFactor>(bits(cmode0, 5, 3)),
        static_cast<BlendFactor>(bits(cmode0, 8, 3)),
        bit(cmode0, 11),
        static_cast<std::uint8_t>(bits(cmode0, 12, 4)),
        bit(cmode1, 8),
        static_cast<std::uint8_t>(bits(cmode1, 0, 8)),
    };
}

struct PeControl {
    std::uint8_t pixel_format;      // 0=RGB8_Z24, 1=RGBA6_Z24, 2=RGB565_Z16, ...
    std::uint8_t z_format;
    bool early_z;                   // zcomploc: Z test before texture
};

[[nodiscard]] constexpr PeControl decode_pe_control(std::uint32_t v) {
    return PeControl{
        static_cast<std::uint8_t>(bits(v, 0, 3)),
        static_cast<std::uint8_t>(bits(v, 3, 3)),
        bit(v, 6),
    };
}

// One TREF register holds the order for two stages; `half` selects which.
struct TevOrder {
    std::uint8_t texmap;            // 0-7
    std::uint8_t texcoord;          // 0-7
    bool tex_enable;
    std::uint8_t ras_channel;       // BP-mapped: 0/1 color, 5=alpha bump,
                                    // 6=normalized alpha bump, 7=zero.
};

[[nodiscard]] constexpr TevOrder decode_tev_order(
    std::uint32_t v,
    unsigned half) {
    const std::uint32_t h = (half != 0) ? bits(v, 12, 12) : bits(v, 0, 12);
    return TevOrder{
        static_cast<std::uint8_t>(bits(h, 0, 3)),
        static_cast<std::uint8_t>(bits(h, 3, 3)),
        bit(h, 6),
        static_cast<std::uint8_t>(bits(h, 7, 3)),
    };
}

// Fully describes one TEV stage; the unit of shader keying and the bit-packed
// uber-shader stage array.  Assembled by GxState from four BP register reads.
struct TevStageConfig {
    // Color env (BP 0xC0 + 2*stage).
    TevColorArg color_a, color_b, color_c, color_d;
    TevBias color_bias;
    bool color_sub;                 // op bit (add/sub, or compare op selector)
    bool color_clamp;
    TevScale color_scale;
    std::uint8_t color_dest;        // 0=PREV, 1-3=REG0-2
    // Alpha env (BP 0xC1 + 2*stage).
    TevAlphaArg alpha_a, alpha_b, alpha_c, alpha_d;
    TevBias alpha_bias;
    bool alpha_sub;
    bool alpha_clamp;
    TevScale alpha_scale;
    std::uint8_t alpha_dest;
    std::uint8_t ras_swap;          // swap table selects (alpha env low bits)
    std::uint8_t tex_swap;
    // Order (TREF half).
    TevOrder order;
    // Konstant selects (KSEL half), 5-bit each.
    std::uint8_t kcsel;
    std::uint8_t kasel;

    [[nodiscard]] constexpr bool color_is_compare() const {
        return color_bias == TevBias::Compare;
    }
    [[nodiscard]] constexpr TevCompareKind color_compare_kind() const {
        return static_cast<TevCompareKind>(
            static_cast<std::uint8_t>(color_scale));
    }
    [[nodiscard]] constexpr TevCompareOp color_compare_op() const {
        return color_sub ? TevCompareOp::Eq : TevCompareOp::Gt;
    }
};

[[nodiscard]] constexpr TevStageConfig assemble_tev_stage(
    std::uint32_t color_env,
    std::uint32_t alpha_env,
    std::uint32_t tref_value,
    unsigned tref_half,
    std::uint8_t kcsel,
    std::uint8_t kasel) {
    return TevStageConfig{
        static_cast<TevColorArg>(bits(color_env, 12, 4)),
        static_cast<TevColorArg>(bits(color_env, 8, 4)),
        static_cast<TevColorArg>(bits(color_env, 4, 4)),
        static_cast<TevColorArg>(bits(color_env, 0, 4)),
        static_cast<TevBias>(bits(color_env, 16, 2)),
        bit(color_env, 18),
        bit(color_env, 19),
        static_cast<TevScale>(bits(color_env, 20, 2)),
        static_cast<std::uint8_t>(bits(color_env, 22, 2)),
        static_cast<TevAlphaArg>(bits(alpha_env, 13, 3)),
        static_cast<TevAlphaArg>(bits(alpha_env, 10, 3)),
        static_cast<TevAlphaArg>(bits(alpha_env, 7, 3)),
        static_cast<TevAlphaArg>(bits(alpha_env, 4, 3)),
        static_cast<TevBias>(bits(alpha_env, 16, 2)),
        bit(alpha_env, 18),
        bit(alpha_env, 19),
        static_cast<TevScale>(bits(alpha_env, 20, 2)),
        static_cast<std::uint8_t>(bits(alpha_env, 22, 2)),
        static_cast<std::uint8_t>(bits(alpha_env, 0, 2)),
        static_cast<std::uint8_t>(bits(alpha_env, 2, 2)),
        decode_tev_order(tref_value, tref_half),
        kcsel,
        kasel,
    };
}

// One KSEL register carries the konst selects for two stages plus one row of
// the channel swap tables.
struct TevKSel {
    std::uint8_t swap_red;          // swap table entry (2 bits)
    std::uint8_t swap_green;
    std::uint8_t kcsel_even;        // stage 2n
    std::uint8_t kasel_even;
    std::uint8_t kcsel_odd;         // stage 2n+1
    std::uint8_t kasel_odd;
};

[[nodiscard]] constexpr TevKSel decode_tev_ksel(std::uint32_t v) {
    return TevKSel{
        static_cast<std::uint8_t>(bits(v, 0, 2)),
        static_cast<std::uint8_t>(bits(v, 2, 2)),
        static_cast<std::uint8_t>(bits(v, 4, 5)),
        static_cast<std::uint8_t>(bits(v, 9, 5)),
        static_cast<std::uint8_t>(bits(v, 14, 5)),
        static_cast<std::uint8_t>(bits(v, 19, 5)),
    };
}

struct AlphaCompare {
    std::uint8_t ref0;
    std::uint8_t ref1;
    CompareFunc comp0;
    CompareFunc comp1;
    AlphaTestLogic logic;
};

[[nodiscard]] constexpr AlphaCompare decode_alpha_compare(std::uint32_t v) {
    return AlphaCompare{
        static_cast<std::uint8_t>(bits(v, 0, 8)),
        static_cast<std::uint8_t>(bits(v, 8, 8)),
        static_cast<CompareFunc>(bits(v, 16, 3)),
        static_cast<CompareFunc>(bits(v, 19, 3)),
        static_cast<AlphaTestLogic>(bits(v, 22, 2)),
    };
}

// Fog parameters are kept mostly raw: the packed-float a/b/c fields are
// converted to host floats by the constant-buffer fill, not here.
struct FogParams {
    std::uint32_t a_raw;            // BP 0xEE
    std::uint32_t b_magnitude_raw;  // BP 0xEF
    std::uint32_t b_shift_raw;      // BP 0xF0
    std::uint32_t c_raw;            // BP 0xF1 low bits
    FogType type;                   // BP 0xF1 bits 21-23
    bool projection;                // BP 0xF1 bit 20: 0 = perspective,
                                    // 1 = orthographic (Dolphin BPMemory.h
                                    // FogParam3::proj, FogProjection enum)
    std::uint32_t color_rgb;        // BP 0xF2 bits 0-23
};

// GX fog A (BP 0xEE) and C (BP 0xF1 bits 0-19) are 20-bit packed floats:
// mantissa bits 0-10, exponent bits 11-18, sign bit 19.  Reassembled into an
// IEEE-754 single as (sign<<31)|(exp<<23)|(mant<<12) — Dolphin BPMemory.h
// FogParam0::FloatValue / FogParam3::GetC.
[[nodiscard]] constexpr std::uint32_t fog_param_float_bits(
    std::uint32_t raw20) {
    const std::uint32_t mant = bits(raw20, 0, 11);
    const std::uint32_t exp  = bits(raw20, 11, 8);
    const std::uint32_t sign = bits(raw20, 19, 1);
    return (sign << 31) | (exp << 23) | (mant << 12);
}

[[nodiscard]] constexpr FogParams decode_fog(
    std::uint32_t p0,
    std::uint32_t p1,
    std::uint32_t p2,
    std::uint32_t p3,
    std::uint32_t color) {
    return FogParams{
        p0,
        p1,
        p2,
        bits(p3, 0, 20),
        static_cast<FogType>(bits(p3, 21, 3)),
        bit(p3, 20),
        bits(color, 0, 24),
    };
}

struct TexImage {
    std::uint16_t width;            // texels (register stores size-1)
    std::uint16_t height;
    TexFormat format;
    std::uint32_t guest_addr;       // physical byte address (reg value << 5)
};

[[nodiscard]] constexpr TexImage decode_tex_image(
    std::uint32_t image0,
    std::uint32_t image3) {
    return TexImage{
        static_cast<std::uint16_t>(bits(image0, 0, 10) + 1u),
        static_cast<std::uint16_t>(bits(image0, 10, 10) + 1u),
        static_cast<TexFormat>(bits(image0, 20, 4)),
        bits(image3, 0, 24) << 5,
    };
}

struct TexMode {
    TexWrap wrap_s;
    TexWrap wrap_t;
    TexMagFilter mag_filter;
    TexMinFilter min_filter;
    std::int16_t lod_bias_x32;      // signed, 1/32 LOD units
    std::uint8_t max_aniso;         // 0=1x, 1=2x, 2=4x
    std::uint8_t min_lod_x16;       // unsigned, 1/16 LOD units
    std::uint8_t max_lod_x16;
};

[[nodiscard]] constexpr TexMode decode_tex_mode(
    std::uint32_t mode0,
    std::uint32_t mode1) {
    const std::uint32_t raw_bias = bits(mode0, 9, 8);
    // Sign-extend the 8-bit bias field.
    const std::int16_t bias = static_cast<std::int16_t>(
        static_cast<std::int8_t>(raw_bias));
    return TexMode{
        static_cast<TexWrap>(bits(mode0, 0, 2)),
        static_cast<TexWrap>(bits(mode0, 2, 2)),
        static_cast<TexMagFilter>(bits(mode0, 4, 1)),
        static_cast<TexMinFilter>(bits(mode0, 5, 3)),
        bias,
        static_cast<std::uint8_t>(bits(mode0, 19, 2)),
        static_cast<std::uint8_t>(bits(mode1, 0, 8)),
        static_cast<std::uint8_t>(bits(mode1, 8, 8)),
    };
}

struct TlutRef {
    std::uint16_t tmem_offset;      // TLUT slot in TMEM high bank
    TlutFormat format;
};

[[nodiscard]] constexpr TlutRef decode_tlut_ref(std::uint32_t v) {
    return TlutRef{
        static_cast<std::uint16_t>(bits(v, 0, 10)),
        static_cast<TlutFormat>(bits(v, 10, 2)),
    };
}

struct ScissorRect {
    std::int32_t x0, y0, x1, y1;    // inclusive, EFB pixel space
};

// Scissor register coordinates carry a +342 hardware offset.
[[nodiscard]] constexpr ScissorRect decode_scissor(
    std::uint32_t top_left,
    std::uint32_t bottom_right) {
    constexpr std::int32_t kOffset = 342;
    return ScissorRect{
        static_cast<std::int32_t>(bits(top_left, 12, 12)) - kOffset,
        static_cast<std::int32_t>(bits(top_left, 0, 12)) - kOffset,
        static_cast<std::int32_t>(bits(bottom_right, 12, 12)) - kOffset,
        static_cast<std::int32_t>(bits(bottom_right, 0, 12)) - kOffset,
    };
}

struct EfbCopyParams {
    std::uint16_t src_x, src_y;
    std::uint16_t src_width, src_height;
    std::uint32_t dest_addr;        // physical bytes (reg << 5)
    std::uint32_t dest_stride;      // bytes (reg << 5)
    std::uint32_t y_scale_raw;      // 8.8 fixed-point inverse scale
    std::uint8_t target_format;     // copy pixel format
    std::uint8_t gamma;             // 0=1.0, 1=1.7, 2=2.2
    bool clamp_top, clamp_bottom;
    bool yuv;                       // RGB->YUV conversion (XFB copies)
    bool half_scale;                // vertical 2x downsample
    bool scale_y;                   // PE bit 10: use inverse BP 0x4E y scale
    bool clear;                     // clear EFB rect after copy
    std::uint8_t frame_to_field;
    bool copy_to_xfb;               // true = display copy, false = texture copy
    bool intensity_format;          // raw PE_COPY_EXECUTE bit 15
    bool auto_conv;                 // raw PE_COPY_EXECUTE bit 16
    bool intensity;                 // intensity conversion = bit15 && bit16
    std::uint32_t clear_color_ar;   // latched BP 0x4F
    std::uint32_t clear_color_gb;   // latched BP 0x50
    std::uint32_t clear_z24;        // latched BP 0x51
    std::uint32_t filter0_raw;      // latched BP 0x53 (copy filter w0-w3)
    std::uint32_t filter1_raw;      // latched BP 0x54 (copy filter w4-w6)

    // 3-tap vertical copy-filter weights (prev/current/next EFB row), in
    // 1/64 units. The seven 6-bit taps collapse as {w0+w1, w2+w3+w4,
    // w5+w6}: the first pair samples the previous row, the middle three
    // sample the current row, and the final pair samples the next row.
    // Therefore the standard {0,0,21,22,21,0,0} filter is (0,64,0)/64.
    [[nodiscard]] constexpr std::uint32_t filter_upper() const {
        return bits(filter0_raw, 0, 6) + bits(filter0_raw, 6, 6);
    }
    [[nodiscard]] constexpr std::uint32_t filter_middle() const {
        return bits(filter0_raw, 12, 6) + bits(filter0_raw, 18, 6) +
               bits(filter1_raw, 0, 6);
    }
    [[nodiscard]] constexpr std::uint32_t filter_lower() const {
        return bits(filter1_raw, 6, 6) + bits(filter1_raw, 12, 6);
    }
};

[[nodiscard]] constexpr EfbCopyParams decode_efb_copy(
    std::uint32_t src_top_left,
    std::uint32_t src_size,
    std::uint32_t dest_addr,
    std::uint32_t dest_stride,
    std::uint32_t y_scale,
    std::uint32_t clear_ar,
    std::uint32_t clear_gb,
    std::uint32_t clear_z,
    std::uint32_t exec) {
    return EfbCopyParams{
        static_cast<std::uint16_t>(bits(src_top_left, 0, 10)),
        static_cast<std::uint16_t>(bits(src_top_left, 10, 10)),
        static_cast<std::uint16_t>(bits(src_size, 0, 10) + 1u),
        static_cast<std::uint16_t>(bits(src_size, 10, 10) + 1u),
        bits(dest_addr, 0, 24) << 5,
        bits(dest_stride, 0, 10) << 5,
        bits(y_scale, 0, 9),
        static_cast<std::uint8_t>((bits(exec, 3, 1) << 3) | bits(exec, 4, 3)),
        static_cast<std::uint8_t>(bits(exec, 7, 2)),
        bit(exec, 0),
        bit(exec, 1),
        bit(exec, 2),
        bit(exec, 9),
        bit(exec, 10),
        bit(exec, 11),
        static_cast<std::uint8_t>(bits(exec, 12, 2)),
        bit(exec, 14),
        bit(exec, 15),
        bit(exec, 16),
        bit(exec, 15) && bit(exec, 16),
        clear_ar,
        clear_gb,
        clear_z,
        0u, // Filters are latched separately by GxState.
        0u,
    };
}

// XF 0x1020-0x1026: six coefficients plus type word (0=perspective, 1=ortho).
struct Projection {
    float coeffs[6];
    bool orthographic;
};

struct XfTexGen {
    bool generate_stq;              // false = ST output
    bool input_form_abc1;           // input has implicit w=1
    TexGenType type;
    std::uint8_t source_row;        // position/normal/color/tex0-7 input row
    std::uint8_t emboss_source;
    std::uint8_t emboss_light;
};

[[nodiscard]] constexpr XfTexGen decode_xf_texgen(std::uint32_t v) {
    return XfTexGen{
        bit(v, 1),
        bit(v, 2),
        static_cast<TexGenType>(bits(v, 4, 3)),
        static_cast<std::uint8_t>(bits(v, 7, 5)),
        static_cast<std::uint8_t>(bits(v, 12, 3)),
        static_cast<std::uint8_t>(bits(v, 15, 3)),
    };
}

struct XfPostTexGen {
    std::uint8_t matrix_index;      // post-transform matrix row
    bool normalize;
};

[[nodiscard]] constexpr XfPostTexGen decode_xf_post_texgen(std::uint32_t v) {
    return XfPostTexGen{
        static_cast<std::uint8_t>(bits(v, 0, 6)),
        bit(v, 8),
    };
}

struct XfChannelCtrl {
    bool material_from_vertex;      // matsource
    bool lighting_enable;
    std::uint8_t light_mask;        // 8 lights
    bool ambient_from_vertex;       // ambsource
    std::uint8_t diffuse_func;      // 0=none, 1=signed, 2=clamped
    std::uint8_t atten_func;        // Dolphin XFMemory.h AttenuationFunc:
                                    // 0=none, 1=specular, 2=dir, 3=spot
};

[[nodiscard]] constexpr XfChannelCtrl decode_xf_channel_ctrl(std::uint32_t v) {
    return XfChannelCtrl{
        bit(v, 0),
        bit(v, 1),
        static_cast<std::uint8_t>(
            bits(v, 2, 4) | (bits(v, 11, 4) << 4)),
        bit(v, 6),
        static_cast<std::uint8_t>(bits(v, 7, 2)),
        static_cast<std::uint8_t>(bits(v, 9, 2)),
    };
}

// ---------------------------------------------------------------------------
// Vertex descriptor: VCD_LO/VCD_HI (which attributes, direct vs indexed) plus
// VAT A/B/C for one of the eight vertex formats (component types and counts).
// ---------------------------------------------------------------------------

struct VertexAttribute {
    VcdType vcd;                    // not present / direct / index8 / index16
    std::uint8_t count;             // component count selector (e.g. xy vs xyz)
    std::uint8_t format;            // ComponentFormat or ColorComponentFormat
    std::uint8_t shift;             // fixed-point fraction bits
};

struct VertexDescriptor {
    bool has_pn_matrix_index;       // per-vertex position matrix index byte
    bool has_tex_matrix_index[8];   // per-vertex texture matrix index bytes
    VertexAttribute position;
    VertexAttribute normal;         // count: 0=3 normals? 0=single, 1=NBT
    VertexAttribute color[2];
    VertexAttribute texcoord[8];
    bool byte_dequant;              // VAT_A bit 30
    bool normal_index_3;            // VAT_A bit 31 (separate NBT indices)
};

[[nodiscard]] constexpr VertexDescriptor decode_vertex_descriptor(
    std::uint32_t vcd_lo,
    std::uint32_t vcd_hi,
    std::uint32_t vat_a,
    std::uint32_t vat_b,
    std::uint32_t vat_c) {
    VertexDescriptor d{};
    d.has_pn_matrix_index = bit(vcd_lo, 0);
    for (unsigned i = 0; i < 8; ++i) {
        d.has_tex_matrix_index[i] = bit(vcd_lo, 1 + i);
    }
    d.position = VertexAttribute{
        static_cast<VcdType>(bits(vcd_lo, 9, 2)),
        static_cast<std::uint8_t>(bits(vat_a, 0, 1)),
        static_cast<std::uint8_t>(bits(vat_a, 1, 3)),
        static_cast<std::uint8_t>(bits(vat_a, 4, 5)),
    };
    d.normal = VertexAttribute{
        static_cast<VcdType>(bits(vcd_lo, 11, 2)),
        static_cast<std::uint8_t>(bits(vat_a, 9, 1)),
        static_cast<std::uint8_t>(bits(vat_a, 10, 3)),
        0,
    };
    d.color[0] = VertexAttribute{
        static_cast<VcdType>(bits(vcd_lo, 13, 2)),
        static_cast<std::uint8_t>(bits(vat_a, 13, 1)),
        static_cast<std::uint8_t>(bits(vat_a, 14, 3)),
        0,
    };
    d.color[1] = VertexAttribute{
        static_cast<VcdType>(bits(vcd_lo, 15, 2)),
        static_cast<std::uint8_t>(bits(vat_a, 17, 1)),
        static_cast<std::uint8_t>(bits(vat_a, 18, 3)),
        0,
    };
    d.texcoord[0] = VertexAttribute{
        static_cast<VcdType>(bits(vcd_hi, 0, 2)),
        static_cast<std::uint8_t>(bits(vat_a, 21, 1)),
        static_cast<std::uint8_t>(bits(vat_a, 22, 3)),
        static_cast<std::uint8_t>(bits(vat_a, 25, 5)),
    };
    // VAT_B bit 31 is VCacheEnhance, not part of texcoord 4. Its fraction
    // starts at VAT_C bit 0; texcoords 5-7 start at C bits 5, 14 and 23.
    for (unsigned i = 1; i < 8; ++i) {
        std::uint32_t field = 0;
        if (i <= 3u) {
            field = bits(vat_b, (i - 1u) * 9u, 9);
        } else if (i == 4u) {
            field = bits(vat_b, 27, 4) | (bits(vat_c, 0, 5) << 4u);
        } else {
            field = bits(vat_c, 5u + (i - 5u) * 9u, 9);
        }
        d.texcoord[i] = VertexAttribute{
            static_cast<VcdType>(bits(vcd_hi, 2 * i, 2)),
            static_cast<std::uint8_t>(bits(field, 0, 1)),
            static_cast<std::uint8_t>(bits(field, 1, 3)),
            static_cast<std::uint8_t>(bits(field, 4, 5)),
        };
    }
    d.byte_dequant = bit(vat_a, 30);
    d.normal_index_3 = bit(vat_a, 31);
    return d;
}

// Wii LOADTLUT and texture TLUT references use the same absolute TMEM slot
// domain. No implicit 0x80000 bias is added. GameCube's source-address mask
// is deliberately absent from this RMGE01 Wii runtime.
inline constexpr std::uint32_t kTmemBytes = 0x100000u;
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
struct TlutTransfer {
    std::uint32_t source_address;
    std::uint32_t destination_offset;
    std::uint32_t byte_count;
};
[[nodiscard]] constexpr TlutTransfer decode_wii_tlut_transfer(
    std::uint32_t source, std::uint32_t destination) {
    return {
        (source & 0x00FFFFFFu) << 5u,
        (destination & 0x3FFu) << 9u,
        ((destination >> 10u) & 0x7FFu) << 5u,
    };
}

}  // namespace galaxy::gx
