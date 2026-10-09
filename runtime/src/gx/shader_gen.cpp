// shader_gen.cpp — ShaderGenerator: PixelShaderKey and VertexShaderKey ->
// HLSL source strings.
//
// Generated shaders share the same constant buffer layout (uber_constants.h)
// and root signature as the uber pipelines so promoting a draw from the uber
// pipeline to a specialized PSO is a pure pipeline-pointer swap.
//
// TEV fidelity rule: regular and compare-mode stages operate on the 8-bit
// integer lattice.  Fog and alpha testing round from the same lattice before
// applying their fixed-function behavior.
//
// Semantic references (clean-room, behavior only — no Dolphin code copied):
//   texgen/post-matrix/q==0:  Dolphin VertexShaderGen.cpp
//   lighting:                 Dolphin LightingShaderGen.cpp, XFMemory.h
//   TEV compare/swap/fog:     Dolphin PixelShaderGen.cpp, BPMemory.h
//
// For M6 the TEV loop is emitted as fully unrolled, straight-line HLSL.

#include "galaxy/gx/shader_gen.h"

#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/uber_constants.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace galaxy::gx {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Append to an ostringstream using a printf-style format.
#pragma warning(push)
#pragma warning(disable : 4996)  // suppress snprintf security warning
template <typename... Args>
static void emit(std::ostringstream& s, const char* fmt, Args&&... args) {
    char buf[4096];
    std::snprintf(buf, sizeof(buf), fmt, std::forward<Args>(args)...);
    s << buf;
}
#pragma warning(pop)

// ---- TevColorArg -> HLSL int expression (color .rgb channel) --------------
static const char* color_arg_rgb(TevColorArg arg) {
    switch (arg) {
    case TevColorArg::CPrev:    return "tev_prev_i.rgb";
    case TevColorArg::APrev:    return "tev_prev_i.aaa";
    case TevColorArg::C0:       return "tev_c_i[0].rgb";
    case TevColorArg::A0:       return "tev_c_i[0].aaa";
    case TevColorArg::C1:       return "tev_c_i[1].rgb";
    case TevColorArg::A1:       return "tev_c_i[1].aaa";
    case TevColorArg::C2:       return "tev_c_i[2].rgb";
    case TevColorArg::A2:       return "tev_c_i[2].aaa";
    case TevColorArg::TexColor: return "tex_color_i.rgb";
    case TevColorArg::TexAlpha: return "tex_color_i.aaa";
    case TevColorArg::RasColor: return "ras_color_i.rgb";
    case TevColorArg::RasAlpha: return "ras_color_i.aaa";
    case TevColorArg::One:      return "int3(255,255,255)";
    case TevColorArg::Half:     return "int3(128,128,128)";
    case TevColorArg::Konst:    return "konst_i.rgb";
    case TevColorArg::Zero:     return "int3(0,0,0)";
    }
    return "int3(0,0,0)";
}

// ---- TevAlphaArg -> HLSL int expression (scalar) ---------------------------
static const char* alpha_arg(TevAlphaArg arg) {
    switch (arg) {
    case TevAlphaArg::APrev:    return "tev_prev_i.a";
    case TevAlphaArg::A0:       return "tev_c_i[0].a";
    case TevAlphaArg::A1:       return "tev_c_i[1].a";
    case TevAlphaArg::A2:       return "tev_c_i[2].a";
    case TevAlphaArg::TexAlpha: return "tex_color_i.a";
    case TevAlphaArg::RasAlpha: return "ras_color_i.a";
    case TevAlphaArg::Konst:    return "konst_i.a";
    case TevAlphaArg::Zero:     return "0";
    }
    return "0";
}

// ---- TEV integer combiner helpers -----------------------------------------
static const char* tev_bias_i(TevBias bias) {
    switch (bias) {
    case TevBias::Zero:    return "0";
    case TevBias::AddHalf: return "128";
    case TevBias::SubHalf: return "-128";
    case TevBias::Compare: break;
    }
    return "0";
}

static const char* tev_scale_left(TevScale scale) {
    switch (scale) {
    case TevScale::X1:   return "";
    case TevScale::X2:   return " << 1";
    case TevScale::X4:   return " << 2";
    case TevScale::Half: return "";
    }
    return "";
}

static const char* tev_scale_right(TevScale scale) {
    return scale == TevScale::Half ? " >> 1" : "";
}

static const char* tev_lerp_round_bias(bool subtract) {
    return subtract ? "127" : "128";
}

// ---- TEV destination name --------------------------------------------------
static const char* tev_dest_name(unsigned dest) {
    switch (dest) {
    case 0: return "tev_prev_i";
    case 1: return "tev_c_i[0]";
    case 2: return "tev_c_i[1]";
    case 3: return "tev_c_i[2]";
    }
    return "tev_prev_i";
}

// ---- Swap-table swizzle suffix ---------------------------------------------
// `codes` packs four 2-bit channel selects (r=0,g=1,b=2,a=3) for r/g/b/a in
// ascending bit pairs.  Returns ".xyzw"-style suffix, or "" for identity.
static std::string swap_suffix(std::uint32_t codes) {
    static const char kCh[4] = {'r', 'g', 'b', 'a'};
    const unsigned c0 = (codes >> 0) & 3u;
    const unsigned c1 = (codes >> 2) & 3u;
    const unsigned c2 = (codes >> 4) & 3u;
    const unsigned c3 = (codes >> 6) & 3u;
    if (c0 == 0u && c1 == 1u && c2 == 2u && c3 == 3u) {
        return std::string();  // identity — skip the swizzle entirely
    }
    char buf[6] = {'.', kCh[c0], kCh[c1], kCh[c2], kCh[c3]};
    return std::string(buf, 5);
}

static std::uint32_t indirect_cmd(
    const PixelShaderKey& key,
    unsigned tev_stage) {
    return key.ind_cmd[tev_stage / 4u][tev_stage & 3u];
}

static bool indirect_cmd_active(std::uint32_t cmd) {
    return cmd != 0u;
}

static unsigned indirect_order_texmap(
    const PixelShaderKey& key,
    unsigned ind_stage) {
    return (key.ind_ref >> (ind_stage * 6u)) & 0x7u;
}

static unsigned indirect_order_texcoord(
    const PixelShaderKey& key,
    unsigned ind_stage) {
    return (key.ind_ref >> (ind_stage * 6u + 3u)) & 0x7u;
}

static unsigned indirect_scale_shift(
    const PixelShaderKey& key,
    unsigned ind_stage,
    bool t_coord) {
    const std::uint32_t raw = key.ind_scale[ind_stage / 2u];
    const unsigned pair_shift = (ind_stage & 1u) ? 8u : 0u;
    return (raw >> (pair_shift + (t_coord ? 4u : 0u))) & 0xFu;
}

static unsigned indirect_wrap_mask(std::uint32_t wrap) {
    switch (wrap) {
    case 1: return (256u << 7u) - 1u;
    case 2: return (128u << 7u) - 1u;
    case 3: return (64u << 7u) - 1u;
    case 4: return (32u << 7u) - 1u;
    case 5: return (16u << 7u) - 1u;
    default: return 0u;
    }
}

// ---- Alpha compare function -> HLSL boolean expression --------------------
// ref is the HLSL expression for the reference value (already normalised).
static void emit_alpha_test(
    std::ostringstream& s,
    CompareFunc comp,
    const char* sample,
    const char* ref_val) {
    // Returns the pass condition (if false -> discard).
    switch (comp) {
    case CompareFunc::Never:
        emit(s, "false");
        break;
    case CompareFunc::Less:
        emit(s, "(%s < %s)", sample, ref_val);
        break;
    case CompareFunc::Equal:
        emit(s, "(%s == %s)", sample, ref_val);
        break;
    case CompareFunc::LEqual:
        emit(s, "(%s <= %s)", sample, ref_val);
        break;
    case CompareFunc::Greater:
        emit(s, "(%s > %s)", sample, ref_val);
        break;
    case CompareFunc::NEqual:
        emit(s, "(%s != %s)", sample, ref_val);
        break;
    case CompareFunc::GEqual:
        emit(s, "(%s >= %s)", sample, ref_val);
        break;
    case CompareFunc::Always:
        emit(s, "true");
        break;
    }
}

enum class AlphaTestStaticResult : std::uint8_t {
    Undetermined,
    Pass,
    Fail,
};

[[nodiscard]] static AlphaTestStaticResult alpha_test_static_result(
    CompareFunc comp0,
    CompareFunc comp1,
    AlphaTestLogic logic) {
    switch (logic) {
    case AlphaTestLogic::And:
        if (comp0 == CompareFunc::Always && comp1 == CompareFunc::Always) {
            return AlphaTestStaticResult::Pass;
        }
        if (comp0 == CompareFunc::Never || comp1 == CompareFunc::Never) {
            return AlphaTestStaticResult::Fail;
        }
        break;
    case AlphaTestLogic::Or:
        if (comp0 == CompareFunc::Always || comp1 == CompareFunc::Always) {
            return AlphaTestStaticResult::Pass;
        }
        if (comp0 == CompareFunc::Never && comp1 == CompareFunc::Never) {
            return AlphaTestStaticResult::Fail;
        }
        break;
    case AlphaTestLogic::Xor:
        if ((comp0 == CompareFunc::Always && comp1 == CompareFunc::Never) ||
            (comp0 == CompareFunc::Never && comp1 == CompareFunc::Always)) {
            return AlphaTestStaticResult::Pass;
        }
        if ((comp0 == CompareFunc::Always && comp1 == CompareFunc::Always) ||
            (comp0 == CompareFunc::Never && comp1 == CompareFunc::Never)) {
            return AlphaTestStaticResult::Fail;
        }
        break;
    case AlphaTestLogic::Xnor:
        if ((comp0 == CompareFunc::Always && comp1 == CompareFunc::Never) ||
            (comp0 == CompareFunc::Never && comp1 == CompareFunc::Always)) {
            return AlphaTestStaticResult::Fail;
        }
        if ((comp0 == CompareFunc::Always && comp1 == CompareFunc::Always) ||
            (comp0 == CompareFunc::Never && comp1 == CompareFunc::Never)) {
            return AlphaTestStaticResult::Pass;
        }
        break;
    }
    return AlphaTestStaticResult::Undetermined;
}

// ---- fog type name (for the describe() helper) ----------------------------
static const char* fog_type_name(FogType t) {
    switch (t) {
    case FogType::Off:     return "off";
    case FogType::Linear:  return "linear";
    case FogType::Exp:     return "exp";
    case FogType::Exp2:    return "exp2";
    case FogType::RevExp:  return "rev_exp";
    case FogType::RevExp2: return "rev_exp2";
    }
    return "unknown";
}

// ---- compare function name (for the describe() helper) --------------------
static const char* compare_func_name(CompareFunc f) {
    switch (f) {
    case CompareFunc::Never:   return "NEVER";
    case CompareFunc::Less:    return "LT";
    case CompareFunc::Equal:   return "EQ";
    case CompareFunc::LEqual:  return "LE";
    case CompareFunc::Greater: return "GT";
    case CompareFunc::NEqual:  return "NE";
    case CompareFunc::GEqual:  return "GE";
    case CompareFunc::Always:  return "ALWAYS";
    }
    return "?";
}

// ===========================================================================
// Common HLSL preamble shared by VS and PS
// ===========================================================================

static constexpr const char* kVsConstantsDef = R"HLSL(
cbuffer GxVsConstants : register(b0) {
    float4x4 projection;
    // C++ packs texgen_config as uint32_t[8][2] (tight, 64 bytes).  HLSL pads
    // each array element to 16 bytes, so uint2[8] would occupy 128 bytes and
    // shift every field after it — num_texgens/flags/mtx_palette_base then
    // read PAST the 256-byte allocation (garbage palette base => degenerate
    // positions, the boot96-98 invisible-frame root cause).  uint4[4] keeps
    // the tight 64-byte footprint; element i of the C++ array is
    // texgen_config[i/2].xy or .zw.
    uint4    texgen_config[4];
    float4   chan_ambient[2];
    float4   chan_material[2];
    uint     num_texgens;
    uint     num_chans;
    uint     flags;
    uint     mtx_palette_base;
    uint     matrix_index_a;
    uint     matrix_index_b;
    uint     pad0;
    uint     pad1;
    float4   inline_pos_matrix[3];
    float4   inline_tex_matrices[24];
    float4   inline_post_matrices[24];
};
StructuredBuffer<float4> mtx_palette : register(t0, space1);
)HLSL";

static constexpr const char* kPsConstantsDef = R"HLSL(
cbuffer GxPsConstants : register(b1) {
    uint4    stage_config[16];
    float4   tev_regs[4];
    float4   konst[4];
    float4   alpha_refs;
    float4   fog_params[2];
    // xy = texture image width/height, zw = GX texcoord S/T scale.
    float4   tex_dims[8];
    float4   ztex_params;
    uint4    ind_cmd[4];
    int4     ind_mtx[6];
    uint4    ind_misc;
};
Texture2D    tex[8] : register(t0);
SamplerState samp[8] : register(s0);
)HLSL";

// ===========================================================================
// VS struct declarations (fixed 144-byte GxVertexOut input layout)
// ===========================================================================
// Interpolators carry float3 texcoords (S, T, Q) — projection texgens divide
// by Q in the PS (Dolphin: texture_coord is vec3, divide at sample time).
static constexpr const char* kVsStructs = R"HLSL(
struct VSIn {
    float3   pos      : POSITION;
    float3   nrm      : NORMAL;
    float3   tangent  : TANGENT;
    float3   binormal : BINORMAL;
    uint     color0   : COLOR0;
    uint     color1   : COLOR1;
    float2   uv[8]    : TEXCOORD;
    uint4    mtx_idx  : BLENDINDICES;
};
struct VSOut {
    float4   pos      : SV_Position;
    float4   col0     : COLOR0;
    float4   col1     : COLOR1;
    float3   uv[8]    : TEXCOORD;
};
)HLSL";

// ===========================================================================
// PS struct declarations
// ===========================================================================
static constexpr const char* kPsStructs = R"HLSL(
struct PSIn {
    float4   pos      : SV_Position;
    float4   col0     : COLOR0;
    float4   col1     : COLOR1;
    float3   uv[8]    : TEXCOORD;
};
)HLSL";

// D3D12 exposes only fixed one-pixel line/point rasterization. These geometry
// shaders implement GX BP 0x22 exactly by expanding to solid quads. Constants
// are in physical pixels, so widths scale with the EFB while the clip-space
// result remains correct for every valid XF viewport.
static constexpr char kLineGeometryShader[] = R"HLSL(
cbuffer GxLinePointConstants : register(b0) {
    float4 line_point_raster : packoffset(c65);
    uint4  line_point_tex_offsets : packoffset(c66);
};
struct VSOut {
    float4 pos : SV_Position;
    float4 col0 : COLOR0;
    float4 col1 : COLOR1;
    float3 uv[8] : TEXCOORD;
};
void apply_line_texcoord_offset(inout VSOut vertex) {
    uint mask = line_point_tex_offsets.x;
    uint divisor = line_point_tex_offsets.z;
    if (divisor == 0u) {
        return;
    }
    float tex_offset = 1.0 / float(divisor);
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        if ((mask & (1u << i)) != 0u) {
            vertex.uv[i].x += tex_offset;
        }
    }
}
[maxvertexcount(4)]
void main(line VSOut input_vertices[2], inout TriangleStream<VSOut> output) {
    VSOut start = input_vertices[0];
    VSOut end = input_vertices[1];
    float2 delta = abs(end.pos.xy / end.pos.w - start.pos.xy / start.pos.w);
    bool is_tall = line_point_raster.y * delta.y >
        line_point_raster.x * delta.x;
    float2 offset = is_tall
        ? float2(line_point_raster.z / line_point_raster.x, 0.0)
        : float2(0.0, -line_point_raster.z / line_point_raster.y);

    VSOut vertex = start;
    vertex.pos.xy -= offset * vertex.pos.w;
    output.Append(vertex);
    vertex = start;
    vertex.pos.xy += offset * vertex.pos.w;
    apply_line_texcoord_offset(vertex);
    output.Append(vertex);
    vertex = end;
    vertex.pos.xy -= offset * vertex.pos.w;
    output.Append(vertex);
    vertex = end;
    vertex.pos.xy += offset * vertex.pos.w;
    apply_line_texcoord_offset(vertex);
    output.Append(vertex);
    output.RestartStrip();
}
)HLSL";

static constexpr char kPointGeometryShader[] = R"HLSL(
cbuffer GxLinePointConstants : register(b0) {
    float4 line_point_raster : packoffset(c65);
    uint4  line_point_tex_offsets : packoffset(c66);
};
struct VSOut {
    float4 pos : SV_Position;
    float4 col0 : COLOR0;
    float4 col1 : COLOR1;
    float3 uv[8] : TEXCOORD;
};
void emit_point_vertex(
    VSOut center,
    float2 signs,
    bool is_right,
    bool is_bottom,
    inout TriangleStream<VSOut> output) {
    float2 offset = signs * line_point_raster.ww /
        line_point_raster.xy;
    center.pos.xy += offset * center.pos.w;
    uint divisor = line_point_tex_offsets.w;
    if (divisor != 0u) {
        float tex_offset = 1.0 / float(divisor);
        uint mask = line_point_tex_offsets.y;
        [unroll] for (uint i = 0u; i < 8u; ++i) {
            if ((mask & (1u << i)) != 0u) {
                center.uv[i].xy += float2(
                    is_right ? tex_offset : 0.0,
                    is_bottom ? tex_offset : 0.0);
            }
        }
    }
    output.Append(center);
}
[maxvertexcount(4)]
void main(point VSOut input_vertices[1], inout TriangleStream<VSOut> output) {
    VSOut center = input_vertices[0];
    emit_point_vertex(center, float2(-1.0,  1.0), false, false, output);
    emit_point_vertex(center, float2( 1.0,  1.0), true,  false, output);
    emit_point_vertex(center, float2(-1.0, -1.0), false, true,  output);
    emit_point_vertex(center, float2( 1.0, -1.0), true,  true,  output);
    output.RestartStrip();
}
)HLSL";

// ===========================================================================
// VS emission helpers
// ===========================================================================

// Emits one light's contribution to the channel accumulator.
//   - `n`         light index 0-7 (XF light objects at palette element
//                 0x180 + 4n: elem+0.w = RGBA8 color (raw bits), elem+1 =
//                 {a0,a1,a2,k0}, elem+2 = {k1,k2,pos.x,pos.y},
//                 elem+3 = {pos.z,dir.x,dir.y,dir.z} — XFMemory.h Light).
//   - `ctrl`      the channel control (attn / diffuse functions).
//   - `alpha`     accumulate the light's alpha into `lacc_a` instead of its
//                 rgb into `lacc`.
// Attenuation/diffuse forms follow Dolphin LightingShaderGen.cpp exactly.
static void emit_light(
    std::ostringstream& s,
    unsigned n,
    const XfChannelCtrl& ctrl,
    bool alpha) {
    emit(s, "        {  // light %u\n", n);
    const unsigned e = 0x180u + 4u * n;
    emit(s,
        "            uint  lcw  = asuint(mtx_palette[mtx_palette_base + %uu].w);\n"
        "            float4 le1 = mtx_palette[mtx_palette_base + %uu];\n"
        "            float4 le2 = mtx_palette[mtx_palette_base + %uu];\n"
        "            float4 le3 = mtx_palette[mtx_palette_base + %uu];\n",
        e, e + 1u, e + 2u, e + 3u);
    s << "            float3 lpos  = float3(le2.z, le2.w, le3.x);\n";
    s << "            float3 ldvec = float3(le3.y, le3.z, le3.w);\n";
    s << "            float3 cosA  = le1.xyz;\n";
    s << "            float3 distA = float3(le1.w, le2.x, le2.y);\n";
    if (alpha) {
        s << "            float lcol = float(lcw & 255u);\n";
    } else {
        s << "            float3 lcol = float3(\n"
             "                float((lcw >> 24) & 255u),\n"
             "                float((lcw >> 16) & 255u),\n"
             "                float((lcw >>  8) & 255u));\n";
    }

    // Attenuation (XF chan ctrl bits 9-10; Dolphin AttenuationFunc:
    // 0=None, 1=Spec, 2=Dir, 3=Spot).  Every normalize/divide is guarded:
    // a light positioned exactly at the vertex, a zero distA vector, or a
    // zero attenuation denominator must yield 0/fallback, never NaN/INF
    // bleeding into the vertex colors (hardware does fixed-point math and
    // has no NaN to propagate).
    switch (ctrl.atten_func) {
    case 1u:  // Spec
        s << "            float3 ldelta = lpos - wpos.xyz;\n";
        s << "            float ldd = dot(ldelta, ldelta);\n";
        s << "            float3 ldir = (ldd > 0.0)"
             " ? ldelta * rsqrt(ldd) : nrm;\n";
        s << "            float attn = (dot(nrm, ldir) >= 0.0)"
             " ? max(0.0, dot(nrm, ldvec)) : 0.0;\n";
        // Dolphin: distAttn is normalized unless diffusefunc == None.
        if (ctrl.diffuse_func == 0u) {
            s << "            float3 distAttn = distA;\n";
        } else {
            s << "            float dad = dot(distA, distA);\n";
            s << "            float3 distAttn = (dad > 0.0)"
                 " ? distA * rsqrt(dad) : distA;\n";
        }
        s << "            float denom = dot(distAttn, float3(1.0, attn,"
             " attn*attn));\n";
        s << "            attn = (denom != 0.0)\n"
             "                 ? max(0.0, dot(cosA, float3(1.0, attn,"
             " attn*attn))) / denom\n"
             "                 : 0.0;\n";
        break;
    case 3u:  // Spot
        s << "            float3 ldelta = lpos - wpos.xyz;\n";
        s << "            float dist2 = dot(ldelta, ldelta);\n";
        s << "            float dist  = sqrt(dist2);\n";
        s << "            float3 ldir = (dist > 0.0)"
             " ? ldelta / dist : nrm;\n";
        s << "            float attn = max(0.0, dot(ldir, ldvec));\n";
        s << "            float denom = dot(distA, float3(1.0, dist,"
             " dist2));\n";
        s << "            attn = (denom != 0.0)\n"
             "                 ? max(0.0, cosA.x + cosA.y*attn +"
             " cosA.z*attn*attn) / denom\n"
             "                 : 0.0;\n";
        break;
    default:  // 0 = None, 2 = Dir
        // The old form normalized FIRST and tested length() after — by then
        // a zero vector is already NaN and the fallback never fired.
        s << "            float3 ldelta = lpos - wpos.xyz;\n";
        s << "            float ldd = dot(ldelta, ldelta);\n";
        s << "            float3 ldir = (ldd > 0.0)"
             " ? ldelta * rsqrt(ldd) : nrm;\n";
        s << "            float attn = 1.0;\n";
        break;
    }

    // Diffuse function (0=none, 1=signed, 2=clamped).
    const char* acc = alpha ? "lacc_a_i" : "lacc_i";
    switch (ctrl.diffuse_func) {
    case 1u:  // Sign
        emit(s,
             "            %s += int%s(round(attn * dot(ldir, nrm) * lcol));\n",
             acc, alpha ? "" : "3");
        break;
    case 2u:  // Clamp
        emit(s,
             "            %s += int%s(round(attn * max(0.0,"
             " dot(ldir, nrm)) * lcol));\n",
             acc, alpha ? "" : "3");
        break;
    default:  // None
        emit(s, "            %s += int%s(round(attn * lcol));\n",
             acc, alpha ? "" : "3");
        break;
    }
    s << "        }\n";
}

}  // anonymous namespace

std::string_view ShaderGenerator::line_geometry_shader_source() noexcept {
    return std::string_view{
        kLineGeometryShader, sizeof(kLineGeometryShader) - 1u};
}

std::string_view ShaderGenerator::point_geometry_shader_source() noexcept {
    return std::string_view{
        kPointGeometryShader, sizeof(kPointGeometryShader) - 1u};
}

// ===========================================================================
// generate_vs
// ===========================================================================

std::string ShaderGenerator::generate_vs(const VertexShaderKey& key) const {
    std::ostringstream s;

    // Header comment
    const std::string key_desc = ShaderGenerator::describe(key);
    s << "// Specialized VS: " << key_desc << "\n";
    s << "// Generated by Nebula ShaderGenerator\n\n";

    s << kVsConstantsDef;
    s << kVsStructs;

    const bool dual_tex = (key.flags & 0x04u) != 0u;

    // Does any active channel actually light?  (Channel controls beyond
    // num_chans are zeroed by the key builder.)
    bool lighting_used = false;
    for (unsigned ch = 0; ch < key.num_chans && ch < 2u; ++ch) {
        if (decode_xf_channel_ctrl(key.channel[ch]).lighting_enable ||
            decode_xf_channel_ctrl(key.channel[2 + ch]).lighting_enable) {
            lighting_used = true;
        }
    }
    bool emboss_used = false;
    for (unsigned i = 0; i < key.num_texgens && i < kMaxTexGens; ++i) {
        if (decode_xf_texgen(key.texgen[i]).type == TexGenType::EmbossMap) {
            emboss_used = true;
        }
    }

    if (lighting_used || emboss_used) {
        // Word-granular palette fetch: normal-matrix rows (XF 0x400 + 3 words
        // per row) are not float4-aligned, so they cannot be read as whole
        // palette elements.  Dynamic vector subscripting is legal SM5.
        s << R"HLSL(
float xfw(uint w) {
    float4 v = mtx_palette[mtx_palette_base + (w >> 2u)];
    return v[w & 3u];
}
)HLSL";
    }

    // ---- main() ----
    s << "VSOut main(VSIn v) {\n";

    // Unpack position/normal matrix index from the BLENDINDICES semantic.
    // The GX PNMATIDX value IS the XF row index (PNMTX0=0, PNMTX1=3, ...);
    // a matrix occupies rows n..n+2.  The loader resolves the non-per-vertex
    // default from CP MatrixIndexA, so v.mtx_idx is always authoritative.
    // Clamp to the 64-row pos-matrix region so a garbage index can never
    // read past the palette snapshot (the matrix palette is a root SRV —
    // out-of-bounds reads are NOT clamped by hardware and can page-fault
    // the GPU).
    s << "    uint pn_row = min(v.mtx_idx.x & 0x3Fu, 61u);\n";
    s << "    float3 pos_in = v.pos;\n";
    if ((key.flags & 0x01u) != 0u) {
        s << "    uint pnmtx_row = pn_row + mtx_palette_base;\n";
        s << "    float4 wpos = float4(\n";
        s << "        dot(mtx_palette[pnmtx_row+0u], float4(pos_in, 1.0)),\n";
        s << "        dot(mtx_palette[pnmtx_row+1u], float4(pos_in, 1.0)),\n";
        s << "        dot(mtx_palette[pnmtx_row+2u], float4(pos_in, 1.0)),\n";
        s << "        1.0);\n\n";
    } else {
        s << "    float4 wpos = float4(\n";
        s << "        dot(inline_pos_matrix[0], float4(pos_in, 1.0)),\n";
        s << "        dot(inline_pos_matrix[1], float4(pos_in, 1.0)),\n";
        s << "        dot(inline_pos_matrix[2], float4(pos_in, 1.0)),\n";
        s << "        1.0);\n\n";
    }

    s << "    VSOut o = (VSOut)0;\n";
    s << "    o.pos = mul(projection, wpos);\n\n";

    // Unpack RGBA8 vertex colors (zero when the VCD carries no color attr).
    s << "    float4 vtx_col0 = float4(\n";
    s << "        float((v.color0 >> 24u) & 0xFFu) / 255.0,\n";
    s << "        float((v.color0 >> 16u) & 0xFFu) / 255.0,\n";
    s << "        float((v.color0 >>  8u) & 0xFFu) / 255.0,\n";
    s << "        float((v.color0       ) & 0xFFu) / 255.0);\n";
    s << "    float4 vtx_col1 = float4(\n";
    s << "        float((v.color1 >> 24u) & 0xFFu) / 255.0,\n";
    s << "        float((v.color1 >> 16u) & 0xFFu) / 255.0,\n";
    s << "        float((v.color1 >>  8u) & 0xFFu) / 255.0,\n";
    s << "        float((v.color1       ) & 0xFFu) / 255.0);\n\n";

    if (lighting_used) {
        // Lighting-space normal: transform by the normal (inverse-transpose)
        // matrix — 3-float rows at XF words 0x400 + 3*(posidx & 31)
        // (Dolphin VertexShaderGen I_NORMALMATRICES[posidx & 31]).  The
        // transformed normal is renormalized like Dolphin; the zero guard
        // keeps draws without a normal attribute NaN-free.
        s << "    uint nrm_base = 0x400u + 3u * (pn_row & 31u);\n";
        s << "    float3 nrm_t = float3(\n";
        s << "        dot(float3(xfw(nrm_base+0u), xfw(nrm_base+1u),"
             " xfw(nrm_base+2u)), v.nrm),\n";
        s << "        dot(float3(xfw(nrm_base+3u), xfw(nrm_base+4u),"
             " xfw(nrm_base+5u)), v.nrm),\n";
        s << "        dot(float3(xfw(nrm_base+6u), xfw(nrm_base+7u),"
             " xfw(nrm_base+8u)), v.nrm));\n";
        s << "    float3 nrm = (dot(nrm_t, nrm_t) > 0.0)"
             " ? normalize(nrm_t) : nrm_t;\n\n";
    }

    // GX color channels: the rasterized color comes from the channel
    // controls.  matsource selects material register vs vertex color; when
    // lighting is enabled the per-light sum replaces the old ambient
    // approximation (Dolphin LightingShaderGen).  Results are quantized to
    // the 8-bit lattice — GX accumulates channels in 8-bit registers.
    for (unsigned ch = 0; ch < 2; ++ch) {
        const char* vtx = (ch == 0) ? "vtx_col0" : "vtx_col1";
        const char* out = (ch == 0) ? "o.col0" : "o.col1";
        if (ch >= key.num_chans) {
            emit(s, "    %s = %s;\n", out, vtx);
            continue;
        }
        const XfChannelCtrl cc = decode_xf_channel_ctrl(key.channel[ch]);
        const XfChannelCtrl ca = decode_xf_channel_ctrl(key.channel[2 + ch]);

        emit(s, "    {  // channel %u (matsrc=%u light=%u amask=0x%02X)\n",
             ch,
             cc.material_from_vertex ? 1u : 0u,
             cc.lighting_enable ? 1u : 0u,
             ca.light_mask);

        // Color part.
        emit(s, "        int3 mat_c_i = int3(round(%s.rgb * 255.0));\n",
             cc.material_from_vertex ? vtx
                                     : (ch == 0 ? "chan_material[0]"
                                                : "chan_material[1]"));
        if (cc.lighting_enable) {
            emit(s, "        int3 lacc_i = int3(round(%s.rgb * 255.0));\n",
                 cc.ambient_from_vertex ? vtx
                                        : (ch == 0 ? "chan_ambient[0]"
                                                   : "chan_ambient[1]"));
            for (unsigned n = 0; n < 8; ++n) {
                if ((cc.light_mask >> n) & 1u) {
                    emit_light(s, n, cc, /*alpha=*/false);
                }
            }
            s << "        lacc_i = clamp(lacc_i, int3(0,0,0),"
                 " int3(255,255,255));\n";
            emit(s,
                "        %s.rgb = float3((mat_c_i * (lacc_i +"
                " (lacc_i >> 7))) >> 8) / 255.0;\n", out);
        } else {
            emit(s, "        %s.rgb = float3(mat_c_i) / 255.0;\n", out);
        }

        // Alpha part (separate channel control register).
        emit(s, "        int mat_a_i = int(round(%s * 255.0));\n",
             ca.material_from_vertex
                 ? ((ch == 0) ? "vtx_col0.a" : "vtx_col1.a")
                 : (ch == 0 ? "chan_material[0].a" : "chan_material[1].a"));
        if (ca.lighting_enable) {
            emit(s, "        int lacc_a_i = int(round(%s * 255.0));\n",
                 ca.ambient_from_vertex
                     ? ((ch == 0) ? "vtx_col0.a" : "vtx_col1.a")
                     : (ch == 0 ? "chan_ambient[0].a"
                                : "chan_ambient[1].a"));
            for (unsigned n = 0; n < 8; ++n) {
                if ((ca.light_mask >> n) & 1u) {
                    emit_light(s, n, ca, /*alpha=*/true);
                }
            }
            s << "        lacc_a_i = clamp(lacc_a_i, 0, 255);\n";
            emit(s,
                "        %s.a = float((mat_a_i * (lacc_a_i +"
                " (lacc_a_i >> 7))) >> 8) / 255.0;\n", out);
        } else {
            emit(s, "        %s.a = float(mat_a_i) / 255.0;\n", out);
        }
        s << "    }\n";
    }
    s << "\n";

    // ---- Texgens ----------------------------------------------------------
    // Full pipeline per Dolphin VertexShaderGen: input-row selection (RAW
    // pos / RAW normal / tex0-7), 2x4 or 3x4 texture-matrix multiply
    // (per-vertex index or CP MatrixIndexA/B default), optional normalize +
    // post-transform (dual-tex) matrix, q==0 clamp.  The PS divides by Q.
    const unsigned ntg = key.num_texgens;
    emit(s, "    // %u active texgen(s)\n", ntg);

    for (unsigned i = 0; i < ntg && i < kMaxTexGens; ++i) {
        const XfTexGen tg = decode_xf_texgen(key.texgen[i]);

        emit(s, "    {  // texgen[%u]: type=%u src=%u stq=%u abc1=%u\n",
             i,
             static_cast<unsigned>(tg.type),
             static_cast<unsigned>(tg.source_row),
             tg.generate_stq ? 1u : 0u,
             tg.input_form_abc1 ? 1u : 0u);

        switch (tg.type) {
        case TexGenType::Color0:
            // Dolphin: texture coord = (lit color 0).xy, q = 1.
            emit(s, "        o.uv[%u] = float3(o.col0.rg, 1.0);\n", i);
            break;
        case TexGenType::Color1:
            emit(s, "        o.uv[%u] = float3(o.col1.rg, 1.0);\n", i);
            break;
        case TexGenType::EmbossMap: {
            // Both vertex decode routes supply zero for absent NBT vectors.
            // Keep transformed basis scale: it controls emboss magnitude.
            s << "        uint emboss_base = 0x400u + 3u * (pn_row & 31u);\n";
            s << "        float3x3 emboss_matrix = float3x3(\n"
                 "            xfw(emboss_base), xfw(emboss_base+1u), xfw(emboss_base+2u),\n"
                 "            xfw(emboss_base+3u), xfw(emboss_base+4u), xfw(emboss_base+5u),\n"
                 "            xfw(emboss_base+6u), xfw(emboss_base+7u), xfw(emboss_base+8u));\n";
            s << "        float3 emboss_tangent = mul(emboss_matrix, v.tangent);\n"
                 "        float3 emboss_binormal = mul(emboss_matrix, v.binormal);\n";
            const unsigned light = 0x180u + 4u * tg.emboss_light;
            emit(s,
                 "        float4 emboss_l2 = mtx_palette[mtx_palette_base + %uu];\n"
                 "        float4 emboss_l3 = mtx_palette[mtx_palette_base + %uu];\n",
                 light + 2u, light + 3u);
            s << "        float3 emboss_delta = float3(emboss_l2.zw, emboss_l3.x) - wpos.xyz;\n"
                 "        float emboss_dist2 = dot(emboss_delta, emboss_delta);\n"
                 "        float3 emboss_ldir = (emboss_dist2 > 0.0) ? emboss_delta * rsqrt(emboss_dist2) : float3(0,0,0);\n";
            emit(s,
                 "        o.uv[%u] = o.uv[%u] + float3(dot(emboss_ldir, emboss_tangent), dot(emboss_ldir, emboss_binormal), 0.0);\n",
                 i, tg.emboss_source);
            break;
        }
        case TexGenType::Regular:
        default: {
            // 1. Input row (Dolphin SourceRow: 0 Geom, 1 Normal, 2 Colors,
            //    3/4 BinormalT/B, 5..12 Tex0..Tex7).  Geom and Normal use
            //    the RAW (untransformed) attributes.
            s << "        float4 coord = float4(0.0, 0.0, 1.0, 1.0);\n";
            if (tg.source_row == 0u) {
                s << "        coord.xyz = v.pos;\n";
            } else if (tg.source_row == 1u) {
                s << "        coord.xyz = v.nrm;\n";
            } else if (tg.source_row == 3u) {
                s << "        coord.xyz = v.tangent;\n";
            } else if (tg.source_row == 4u) {
                s << "        coord.xyz = v.binormal;\n";
            } else if (tg.source_row >= 5u && tg.source_row <= 12u) {
                emit(s, "        coord = float4(v.uv[%u], 1.0, 1.0);\n",
                     tg.source_row - 5u);
            }
            // Color source rows keep the (0,0,1,1) default; colors are only
            // valid for Color0/1 texgen types.

            // 2. Input form AB11 forces the implicit third component to 1
            //    (Dolphin: if (inputform == AB11) coord.z = 1.0).
            if (!tg.input_form_abc1) {
                s << "        coord.z = 1.0;\n";
            }

            // 3. Texture matrix row index: per-vertex when the VCD carries
            //    it (loader packs gens 0-7 across mtx_idx.xy/z),
            //    else the CP MatrixIndexA/B default.  Rows are clamped to
            //    the 64-row matrix region (rows n..n+2 must stay inside).
            const bool per_vertex =
                ((key.texmtx_idx_mask >> i) & 1u) != 0u && i < 8u;
            if (per_vertex) {
                const char* component = i < 3u ? "x" : i < 7u ? "y" : "z";
                const unsigned shift = i < 3u
                    ? 8u + 8u * i
                    : i < 7u ? 8u * (i - 3u) : 0u;
                emit(s,
                    "        uint tm = min((v.mtx_idx.%s >> %uu) & 0xFFu,"
                    " 61u) + mtx_palette_base;\n",
                    component,
                    shift);
            }

            // 4. 2x4 (ST) or 3x4 (STQ) multiply.
            if (per_vertex) {
                if (tg.generate_stq) {
                    s << "        float3 res = float3(\n"
                         "            dot(coord, mtx_palette[tm+0u]),\n"
                         "            dot(coord, mtx_palette[tm+1u]),\n"
                         "            dot(coord, mtx_palette[tm+2u]));\n";
                } else {
                    s << "        float3 res = float3(\n"
                         "            dot(coord, mtx_palette[tm+0u]),\n"
                         "            dot(coord, mtx_palette[tm+1u]),\n"
                         "            1.0);\n";
                }
            } else {
                const unsigned inline_base = i * 3u;
                if (tg.generate_stq) {
                    emit(s,
                        "        float3 res = float3(\n"
                        "            dot(coord, inline_tex_matrices[%u]),\n"
                        "            dot(coord, inline_tex_matrices[%u]),\n"
                        "            dot(coord, inline_tex_matrices[%u]));\n",
                        inline_base, inline_base + 1u, inline_base + 2u);
                } else {
                    emit(s,
                        "        float3 res = float3(\n"
                        "            dot(coord, inline_tex_matrices[%u]),\n"
                        "            dot(coord, inline_tex_matrices[%u]),\n"
                        "            1.0);\n",
                        inline_base, inline_base + 1u);
                }
            }

            // 5. Post-transform (dual-tex) matrix: XF 0x500 region =
            //    palette elements 0x140+n; optional pre-normalize.
            if (dual_tex) {
                const XfPostTexGen pt =
                    decode_xf_post_texgen(key.post_texgen[i]);
                if (pt.normalize) {
                    // Guarded: normalize(0) is NaN, and a degenerate texgen
                    // input (zero normal/position) must not poison the UV.
                    s << "        res = (dot(res, res) > 0.0)"
                         " ? normalize(res) : res;\n";
                }
                const unsigned post_base = i * 3u;
                emit(s,
                    "        float4 P0 = inline_post_matrices[%u];\n"
                    "        float4 P1 = inline_post_matrices[%u];\n"
                    "        float4 P2 = inline_post_matrices[%u];\n",
                    post_base, post_base + 1u, post_base + 2u);
                s << "        res = float3(dot(P0.xyz, res) + P0.w,\n"
                     "                     dot(P1.xyz, res) + P1.w,\n"
                     "                     dot(P2.xyz, res) + P2.w);\n";
            }

            // 6. q==0 special case (hardware quirk Dolphin reproduces).
            s << "        if (res.z == 0.0)"
                 " res.xy = clamp(res.xy * 0.5, -1.0, 1.0);\n";
            emit(s, "        o.uv[%u] = res;\n", i);
            break;
        }
        }
        s << "    }\n";
    }
    s << "\n    return o;\n}\n";

    return s.str();
}

// ===========================================================================
// generate_ps
// ===========================================================================

std::string ShaderGenerator::generate_ps(const PixelShaderKey& key) const {
    std::ostringstream s;

    // Header comment
    const std::string key_desc = ShaderGenerator::describe(key);
    s << "// Specialized PS: " << key_desc << "\n";
    s << "// Generated by Nebula ShaderGenerator\n\n";

    s << kPsConstantsDef;
    s << kPsStructs;

    // ---- main() ----
    // Z-texturing (flags bit1): the fragment depth is replaced by (or offset
    // with) a value sampled from the LAST TEV stage's texture, so the PS must
    // export SV_Depth.  SMG's clearEfb idiom: fullscreen quad + Z24X8 texture
    // of 0xFFFFFF + GX_ZT_REPLACE = clear color AND depth(far) in one draw.
    const bool ztex_on = (key.flags & 0x02u) != 0u;
    const bool early_depth = (key.flags & 0x01u) == 0u;
    const bool export_depth = ztex_on && !early_depth;
    const unsigned ztex_op = (key.flags >> 3) & 0x3u;
    const unsigned ztex_fmt = (key.flags >> 5) & 0x3u;
    // Dual-source dst-alpha (flags bit7): SV_Target0 carries the constant
    // alpha written to the EFB, SV_Target1 carries the TEV alpha the blender
    // reads back as SRC1_ALPHA (Dolphin PixelShaderGen ocol0/ocol1).  Both
    // targets feed blend slot 0 — no second render target is bound.
    const bool dual_src = (key.flags & 0x80u) != 0u;
    if (export_depth && dual_src) {
        s << "struct PSOutZ { float4 color : SV_Target0; "
             "float4 color1 : SV_Target1; float depth : SV_Depth; };\n";
        s << "PSOutZ main(PSIn pin) {\n";
    } else if (export_depth) {
        s << "struct PSOutZ { float4 color : SV_Target0; "
             "float depth : SV_Depth; };\n";
        s << "PSOutZ main(PSIn pin) {\n";
    } else if (dual_src) {
        s << "struct PSOut { float4 color : SV_Target0; "
             "float4 color1 : SV_Target1; };\n";
        if (early_depth) s << "[earlydepthstencil]\n";
        s << "PSOut main(PSIn pin) {\n";
    } else {
        if (early_depth) s << "[earlydepthstencil]\n";
        s << "float4 main(PSIn pin) : SV_Target0 {\n";
    }

    // TEV working registers: PREV, REG0, REG1, REG2.
    // Dolphin keeps these as int4s. A/B/C inputs are masked to 8-bit, D stays
    // signed/raw, and unclamped outputs stay in the -1024..1023 range.
    s << "    int4 tev_prev_i = int4(round(tev_regs[0] * 255.0));\n";
    s << "    int4 tev_c_i[3];\n";
    s << "    tev_c_i[0] = int4(round(tev_regs[1] * 255.0));\n";
    s << "    tev_c_i[1] = int4(round(tev_regs[2] * 255.0));\n";
    s << "    tev_c_i[2] = int4(round(tev_regs[3] * 255.0));\n";
    // Per-stage working variables, declared once (stages assign them).
    s << "    int4 tex_color_i = int4(0,0,0,0);\n";
    s << "    int4 last_raw_tex_i = int4(0,0,0,0);\n";
    s << "    int4 ras_color_i = int4(0,0,0,0);\n";
    s << "    int4 konst_i = int4(0,0,0,0);\n";
    s << "    int alphabump_i = 0;\n";
    s << "    int2 tevcoord_i = int2(0,0);\n";
    s << "    int3 ind_tex_i[4] = {"
         "int3(0,0,0), int3(0,0,0), int3(0,0,0), int3(0,0,0)};\n\n";

    if (key.num_ind_stages != 0u) {
        emit(s, "    // %u indirect texture stage(s)\n",
             key.num_ind_stages);
        for (unsigned ind = 0; ind < key.num_ind_stages && ind < 4u; ++ind) {
            const unsigned texmap = indirect_order_texmap(key, ind);
            const unsigned texcoord_raw = indirect_order_texcoord(key, ind);
            const unsigned texcoord =
                texcoord_raw < key.num_texgens ? texcoord_raw : 0u;
            const unsigned scale_s =
                indirect_scale_shift(key, ind, false);
            const unsigned scale_t =
                indirect_scale_shift(key, ind, true);
            if (key.num_texgens != 0u) {
                emit(s,
                    "    {\n"
                    "        float3 ind_tq = pin.uv[%u];\n"
                    "        float2 ind_uv = (ind_tq.z == 0.0)"
                    " ? ind_tq.xy : ind_tq.xy / ind_tq.z;\n"
                    "        int2 ind_fix = int2(round(ind_uv * tex_dims[%u].zw * 128.0));\n"
                    "        ind_fix = int2(ind_fix.x >> %u, ind_fix.y >> %u);\n"
                    "        float2 ind_tuv = (float2(ind_fix) / 128.0) / tex_dims[%u].xy;\n"
                    "        float4 ind_sample = tex[%u].Sample(samp[%u], ind_tuv);\n"
                    "        ind_tex_i[%u] = int3(round(saturate(ind_sample.abg) * 255.0));\n"
                    "    }\n",
                    texcoord,
                    texcoord,
                    scale_s,
                    scale_t,
                    texmap,
                    texmap,
                    texmap,
                    ind);
            } else {
                emit(s,
                    "    {\n"
                    "        float4 ind_sample = tex[%u].Sample(samp[%u], float2(0.0, 0.0));\n"
                    "        ind_tex_i[%u] = int3(round(saturate(ind_sample.abg) * 255.0));\n"
                    "    }\n",
                    texmap,
                    texmap,
                    ind);
            }
        }
        s << '\n';
    }

    // Unroll TEV stages.
    const unsigned n_stages = key.num_tev_stages;
    emit(s, "    // %u TEV stage(s)\n", n_stages);

    for (unsigned st = 0; st < n_stages; ++st) {
        const TevStagePacked& packed = key.stages[st];

        // Decode the packed fields back to individual enums for code generation.
        const auto color_d = static_cast<TevColorArg>((packed.color_env      ) & 0xFu);
        const auto color_c = static_cast<TevColorArg>((packed.color_env >>  4) & 0xFu);
        const auto color_b = static_cast<TevColorArg>((packed.color_env >>  8) & 0xFu);
        const auto color_a = static_cast<TevColorArg>((packed.color_env >> 12) & 0xFu);
        const auto color_bias  = static_cast<TevBias> ((packed.color_env >> 16) & 0x3u);
        const bool color_sub   = ((packed.color_env >> 18) & 1u) != 0u;
        const bool color_clamp = ((packed.color_env >> 19) & 1u) != 0u;
        const auto color_scale = static_cast<TevScale>((packed.color_env >> 20) & 0x3u);
        const unsigned color_dest = (packed.color_env >> 22) & 0x3u;

        const auto alpha_d = static_cast<TevAlphaArg>((packed.alpha_env >>  4) & 0x7u);
        const auto alpha_c = static_cast<TevAlphaArg>((packed.alpha_env >>  7) & 0x7u);
        const auto alpha_b = static_cast<TevAlphaArg>((packed.alpha_env >> 10) & 0x7u);
        const auto alpha_a = static_cast<TevAlphaArg>((packed.alpha_env >> 13) & 0x7u);
        const auto alpha_bias  = static_cast<TevBias> ((packed.alpha_env >> 16) & 0x3u);
        const bool alpha_sub   = ((packed.alpha_env >> 18) & 1u) != 0u;
        const bool alpha_clamp = ((packed.alpha_env >> 19) & 1u) != 0u;
        const auto alpha_scale = static_cast<TevScale>((packed.alpha_env >> 20) & 0x3u);
        const unsigned alpha_dest = (packed.alpha_env >> 22) & 0x3u;

        const unsigned texmap    = (packed.order     ) & 0x7u;
        const unsigned texcoord_raw = (packed.order >> 3) & 0x7u;
        const unsigned texcoord =
            (key.num_texgens != 0u && texcoord_raw < key.num_texgens)
                ? texcoord_raw
                : 0u;
        const bool     tex_en    = ((packed.order >> 6) & 1u) != 0u;
        const unsigned ras_chan  = (packed.order >> 7) & 0x7u;

        const std::uint8_t kcsel = static_cast<std::uint8_t>((packed.ksel     ) & 0x1Fu);
        const std::uint8_t kasel = static_cast<std::uint8_t>((packed.ksel >> 5) & 0x1Fu);
        // Resolved swap-table swizzles baked by build_pixel_shader_key
        // (ras bits 10-17, tex bits 18-25; 2 bits per channel r/g/b/a).
        const std::string ras_swz = swap_suffix((packed.ksel >> 10) & 0xFFu);
        const std::string tex_swz = swap_suffix((packed.ksel >> 18) & 0xFFu);

        emit(s, "\n    // ---- TEV stage %u ----\n", st);
        const std::uint32_t ind_raw = indirect_cmd(key, st);
        const bool has_indirect_cmd =
            key.num_ind_stages != 0u && indirect_cmd_active(ind_raw);
        const unsigned ind_bt = ind_raw & 0x3u;
        const unsigned ind_fmt = (ind_raw >> 2u) & 0x3u;
        const unsigned ind_bias = (ind_raw >> 4u) & 0x7u;
        const unsigned ind_bs = (ind_raw >> 7u) & 0x3u;
        const unsigned ind_mtx = (ind_raw >> 9u) & 0x3u;
        const unsigned ind_mtx_id = (ind_raw >> 11u) & 0x3u;
        const unsigned ind_wrap_s = (ind_raw >> 13u) & 0x7u;
        const unsigned ind_wrap_t = (ind_raw >> 16u) & 0x7u;
        const bool ind_add_prev = ((ind_raw >> 20u) & 1u) != 0u;
        const bool has_indirect_sample =
            has_indirect_cmd && ind_bt < key.num_ind_stages && ind_bt < 4u;
        bool later_indirect_addprev = false;
        for (unsigned future = st + 1u; future < n_stages; ++future) {
            const std::uint32_t future_ind_raw = indirect_cmd(key, future);
            if (key.num_ind_stages != 0u &&
                indirect_cmd_active(future_ind_raw) &&
                ((future_ind_raw >> 20u) & 1u) != 0u) {
                later_indirect_addprev = true;
                break;
            }
        }
        const bool indirect_coord_needed = has_indirect_cmd || later_indirect_addprev;
        if (has_indirect_sample && ind_bs != 0u) {
            static constexpr unsigned kAlphaShift[4] = {0u, 5u, 4u, 3u};
            static constexpr const char* kAlphaComponent[4] = {
                "x", "x", "y", "z"};
            emit(s,
                "    alphabump_i = (ind_tex_i[%u].%s << %u) & 248;\n",
                ind_bt,
                kAlphaComponent[ind_bs],
                kAlphaShift[ind_fmt]);
        }

        // Apply the TEV indirect coordinate op before any texture lookup.
        // Disabled texture stages can still update this state when a later
        // add-prev stage can observe it.
        if (indirect_coord_needed) {
            static constexpr unsigned kFmtShift[4] = {0u, 3u, 4u, 5u};
            const int bias_add = ind_fmt == 0u ? -128 : 1;
            const unsigned wrap_s_mask = indirect_wrap_mask(ind_wrap_s);
            const unsigned wrap_t_mask = indirect_wrap_mask(ind_wrap_t);
            emit(s,
                "    {\n"
                "        int2 stage_tevcoord = int2(0, 0);\n");
            if (key.num_texgens != 0u) {
                emit(s,
                    "        float3 tq = pin.uv[%u];\n"
                    "        float2 tuv = (tq.z == 0.0) ? tq.xy : tq.xy / tq.z;\n"
                    "        stage_tevcoord = int2(round(tuv * tex_dims[%u].zw * 128.0));\n",
                    texcoord,
                    texcoord);
            }
            s << "        int2 wrappedcoord = stage_tevcoord;\n";
            if (has_indirect_sample) {
                emit(s, "        int3 iindtevcrd = ind_tex_i[%u] >> %u;\n",
                     ind_bt,
                     kFmtShift[ind_fmt]);
                if ((ind_bias & 0x1u) != 0u) {
                    emit(s, "        iindtevcrd.x += %d;\n", bias_add);
                }
                if ((ind_bias & 0x2u) != 0u) {
                    emit(s, "        iindtevcrd.y += %d;\n", bias_add);
                }
                if ((ind_bias & 0x4u) != 0u) {
                    emit(s, "        iindtevcrd.z += %d;\n", bias_add);
                }
                if (ind_mtx != 0u && ind_mtx <= 3u) {
                    const unsigned row = (ind_mtx - 1u) * 2u;
                    if (ind_mtx_id == 0u) {
                        emit(s,
                            "        int2 indtevtrans = int2(\n"
                            "            (ind_mtx[%u].x * iindtevcrd.x + ind_mtx[%u].y * iindtevcrd.y + ind_mtx[%u].z * iindtevcrd.z) >> 3,\n"
                            "            (ind_mtx[%u].x * iindtevcrd.x + ind_mtx[%u].y * iindtevcrd.y + ind_mtx[%u].z * iindtevcrd.z) >> 3);\n",
                            row, row, row,
                            row + 1u, row + 1u, row + 1u);
                    } else if (ind_mtx_id == 1u) {
                        s << "        int2 indtevtrans = "
                             "(stage_tevcoord * iindtevcrd.xx) >> 8;\n";
                    } else if (ind_mtx_id == 2u) {
                        s << "        int2 indtevtrans = "
                             "(stage_tevcoord * iindtevcrd.yy) >> 8;\n";
                    } else {
                        s << "        int2 indtevtrans = int2(0, 0);\n";
                    }
                    emit(s,
                        "        if (ind_mtx[%u].w >= 0) {\n"
                        "            indtevtrans = indtevtrans >> ind_mtx[%u].w;\n"
                        "        } else {\n"
                        "            indtevtrans = indtevtrans << (-ind_mtx[%u].w);\n"
                        "        }\n",
                        row, row, row);
                } else {
                    s << "        int2 indtevtrans = int2(0, 0);\n";
                }
            } else {
                s << "        int2 indtevtrans = int2(0, 0);\n";
            }
            if (ind_wrap_s == 0u) {
                s << "        wrappedcoord.x = stage_tevcoord.x;\n";
            } else if (ind_wrap_s >= 6u) {
                s << "        wrappedcoord.x = 0;\n";
            } else {
                emit(s, "        wrappedcoord.x = stage_tevcoord.x & %u;\n",
                     wrap_s_mask);
            }
            if (ind_wrap_t == 0u) {
                s << "        wrappedcoord.y = stage_tevcoord.y;\n";
            } else if (ind_wrap_t >= 6u) {
                s << "        wrappedcoord.y = 0;\n";
            } else {
                emit(s, "        wrappedcoord.y = stage_tevcoord.y & %u;\n",
                     wrap_t_mask);
            }
            if (ind_add_prev) {
                s << "        tevcoord_i += wrappedcoord + indtevtrans;\n";
            } else {
                s << "        tevcoord_i = wrappedcoord + indtevtrans;\n";
            }
            s << "        tevcoord_i = (tevcoord_i << 8) >> 8;\n"
                 "    }\n";
        }

        if (tex_en && key.num_texgens != 0u) {
            if (has_indirect_cmd) {
                emit(s,
                    "    {\n"
                    "        float2 ind_tuv = (float2(tevcoord_i) / 128.0) / tex_dims[%u].xy;\n"
                    "        float4 tex_sample = tex[%u].Sample(samp[%u], ind_tuv);\n"
                    "        last_raw_tex_i = int4(round(saturate(tex_sample) * 255.0));\n"
                    "        tex_color_i = last_raw_tex_i%s;\n"
                    "    }\n",
                    texmap,
                    texmap,
                    texmap,
                    tex_swz.c_str());
            } else {
                emit(s,
                    "    {\n"
                    "        float3 tq = pin.uv[%u];\n"
                    "        float2 tuv = (tq.z == 0.0) ? tq.xy : tq.xy / tq.z;\n"
                    "        int2 tex_fix = int2(round(tuv * tex_dims[%u].zw * 128.0));\n"
                    "        float2 tex_tuv = (float2(tex_fix) / 128.0) / tex_dims[%u].xy;\n"
                    "        float4 tex_sample = tex[%u].Sample(samp[%u], tex_tuv);\n"
                    "        last_raw_tex_i = int4(round(saturate(tex_sample) * 255.0));\n"
                    "        tex_color_i = last_raw_tex_i%s;\n"
                    "    }\n",
                    texcoord,
                    texcoord,
                    texmap,
                    texmap,
                    texmap,
                    tex_swz.c_str());
            }
        } else if (key.num_texgens != 0u) {
            // GX supplies white when this TEV stage disables its texture
            // lookup but the pipeline has texture coordinates. This lets a
            // later stage use TEXC/TEXA as a neutral multiplier.
            s << "    tex_color_i = int4(255,255,255,255);\n";
        } else {
            // With no texgens at all, GX instead supplies black.
            s << "    tex_color_i = int4(0,0,0,0);\n";
        }

        // Rasterized color source (with the stage's ras swap swizzle).
        // libogc maps GX_COLORZERO/ALPHA_BUMP/ALPHA_BUMPN to BP channels
        // 7/5/6 respectively; channels other than color0/1 and bump read zero.
        if (ras_chan == 0u) {
            emit(s, "    ras_color_i = int4(round(saturate(pin.col0%s) * 255.0));\n",
                 ras_swz.c_str());
        } else if (ras_chan == 1u) {
            emit(s, "    ras_color_i = int4(round(saturate(pin.col1%s) * 255.0));\n",
                 ras_swz.c_str());
        } else if (ras_chan == 5u) {
            emit(s,
                "    ras_color_i = int4(alphabump_i, alphabump_i, alphabump_i, alphabump_i)%s;\n",
                ras_swz.c_str());
        } else if (ras_chan == 6u) {
            emit(s,
                "    { int abn = alphabump_i | (alphabump_i >> 5); "
                "ras_color_i = int4(abn, abn, abn, abn)%s; }\n",
                ras_swz.c_str());
        } else {
            s << "    ras_color_i = int4(0,0,0,0);\n";
        }

        // GX selector numbering: 0-7 fixed fractions, 8-11 invalid,
        // color 12-15 K0-K3.rgb, and 16-31 component replication.
        // Alpha treats 8-15 as invalid and uses the same component groups.
        auto emit_konst_rgb = [&](std::uint8_t kc) {
            static constexpr unsigned fixed[8] = {
                255u, 223u, 191u, 159u, 128u, 96u, 64u, 32u};
            if (kc <= 7u) {
                emit(s, "int3(%u,%u,%u)", fixed[kc], fixed[kc], fixed[kc]);
                return;
            }
            if (kc <= 11u) {
                s << "int3(0,0,0)";
                return;
            }
            if (kc <= 15u) {
                emit(s, "int3(round(saturate(konst[%u].rgb) * 255.0))",
                     kc - 12u);
                return;
            }
            static constexpr const char* component[4] = {
                "rrr", "ggg", "bbb", "aaa"};
            const unsigned group = (kc - 16u) / 4u;
            const unsigned index = (kc - 16u) % 4u;
            emit(s, "int3(round(saturate(konst[%u].%s) * 255.0))",
                 index, component[group]);
        };
        auto emit_konst_a = [&](std::uint8_t ka) {
            static constexpr unsigned fixed[8] = {
                255u, 223u, 191u, 159u, 128u, 96u, 64u, 32u};
            if (ka <= 7u) {
                emit(s, "%u", fixed[ka]);
                return;
            }
            if (ka <= 15u) {
                s << "0";
                return;
            }
            static constexpr const char* component[4] = {
                "r", "g", "b", "a"};
            const unsigned group = (ka - 16u) / 4u;
            const unsigned index = (ka - 16u) % 4u;
            emit(s, "int(round(saturate(konst[%u].%s) * 255.0))",
                 index, component[group]);
        };

        s << "    konst_i = int4("; emit_konst_rgb(kcsel);
        s << ", "; emit_konst_a(kasel); s << ");\n";

        // TEV color combiner.
        const char* ca = color_arg_rgb(color_a);
        const char* cb = color_arg_rgb(color_b);
        const char* cc = color_arg_rgb(color_c);
        const char* cd = color_arg_rgb(color_d);
        const char* dest_c = tev_dest_name(color_dest);
        // Color and alpha combiners observe the same stage-entry registers.
        // The alpha R8/GR16/BGR24 comparisons reuse COLOR A/B selectors.
        if (alpha_bias == TevBias::Compare && alpha_scale != TevScale::Half) {
            emit(s, "    int3 alpha_compare_a_%u = int3(%s) & int3(255,255,255);\n", st, ca);
            emit(s, "    int3 alpha_compare_b_%u = int3(%s) & int3(255,255,255);\n", st, cb);
        }

        if (color_bias == TevBias::Compare) {
            // Compare mode: dest = clamp(d + ((a OP b) ? c : 0), 0, 255)
            // with operand width from the SCALE field and op from the SUB
            // bit (Dolphin PixelShaderGen WriteTevRegular comparison path).
            // Computed on int(round(x*255)) so the comparisons are exact;
            // the result is already on the 8-bit lattice.
            const auto kind =
                static_cast<TevCompareKind>(static_cast<unsigned>(color_scale));
            const bool is_eq = color_sub;  // SUB bit: 0 = GT, 1 = EQ
            s << "    {\n";
            emit(s, "        int3 ca_i = int3(%s) & int3(255,255,255);\n", ca);
            emit(s, "        int3 cb_i = int3(%s) & int3(255,255,255);\n", cb);
            emit(s, "        int3 cc_i = int3(%s) & int3(255,255,255);\n", cc);
            emit(s, "        int3 cd_i = int3(%s);\n", cd);
            switch (kind) {
            case TevCompareKind::R8:
                emit(s,
                    "        int3 cmp_i = (ca_i.x %s cb_i.x)"
                    " ? cc_i : int3(0,0,0);\n",
                    is_eq ? "==" : ">");
                break;
            case TevCompareKind::GR16:
                s << "        int av = ca_i.x + (ca_i.y << 8);\n";
                s << "        int bv = cb_i.x + (cb_i.y << 8);\n";
                emit(s,
                    "        int3 cmp_i = (av %s bv) ? cc_i : int3(0,0,0);\n",
                    is_eq ? "==" : ">");
                break;
            case TevCompareKind::BGR24:
                s << "        int av = ca_i.x + (ca_i.y << 8)"
                     " + (ca_i.z << 16);\n";
                s << "        int bv = cb_i.x + (cb_i.y << 8)"
                     " + (cb_i.z << 16);\n";
                emit(s,
                    "        int3 cmp_i = (av %s bv) ? cc_i : int3(0,0,0);\n",
                    is_eq ? "==" : ">");
                break;
            case TevCompareKind::RGB8:
            default:
                if (is_eq) {
                    s << "        int3 cmp_i = (int3(1,1,1) -"
                         " sign(abs(ca_i - cb_i))) * cc_i;\n";
                } else {
                    s << "        int3 cmp_i = max(sign(ca_i - cb_i),"
                         " int3(0,0,0)) * cc_i;\n";
                }
                break;
            }
            emit(s,
                "        int3 outv = cd_i + cmp_i;\n");
            if (color_clamp) {
                s << "        outv = clamp(outv, int3(0,0,0), int3(255,255,255));\n";
            } else {
                s << "        outv = clamp(outv, int3(-1024,-1024,-1024), int3(1023,1023,1023));\n";
            }
            emit(s, "        %s.rgb = outv;\n", dest_c);
            s << "    }\n";
        } else {
            // GX regular TEV is integer math on the 8-bit lattice:
            // d (+/-) lerp(a,b,c), where c=255 promotes to 256 before the
            // final >>8.  Keeping the stage in int space preserves dark eye
            // and UI-detail materials that float lerp over-brightens.
            s << "    {\n";
            emit(s, "        int3 ai = int3(%s) & int3(255,255,255);\n", ca);
            emit(s, "        int3 bi = int3(%s) & int3(255,255,255);\n", cb);
            emit(s, "        int3 ci = int3(%s) & int3(255,255,255);\n", cc);
            emit(s, "        int3 di = int3(%s);\n", cd);
            emit(s, "        int3 lhs = (di + int3(%s,%s,%s))%s;\n",
                 tev_bias_i(color_bias), tev_bias_i(color_bias),
                 tev_bias_i(color_bias), tev_scale_left(color_scale));
            s << "        int3 mixv = (ai << 8) + (bi - ai) * (ci + (ci >> 7));\n";
            emit(s, "        mixv = mixv%s;\n", tev_scale_left(color_scale));
            if (color_scale != TevScale::Half) {
                emit(s, "        mixv = (mixv + int3(%s,%s,%s)) >> 8;\n",
                     tev_lerp_round_bias(color_sub),
                     tev_lerp_round_bias(color_sub),
                     tev_lerp_round_bias(color_sub));
            } else {
                s << "        mixv = mixv >> 8;\n";
            }
            emit(s, "        int3 outv = (lhs %c mixv)%s;\n",
                 color_sub ? '-' : '+', tev_scale_right(color_scale));
            if (color_clamp) {
                s << "        outv = clamp(outv, int3(0,0,0), int3(255,255,255));\n";
            } else {
                s << "        outv = clamp(outv, int3(-1024,-1024,-1024), int3(1023,1023,1023));\n";
            }
            emit(s, "        %s.rgb = outv;\n", dest_c);
            s << "    }\n";
        }
        s << "\n";

        // TEV alpha combiner.
        const char* aa = alpha_arg(alpha_a);
        const char* ab = alpha_arg(alpha_b);
        const char* ac = alpha_arg(alpha_c);
        const char* ad = alpha_arg(alpha_d);
        const char* dest_a = tev_dest_name(alpha_dest);

        if (alpha_bias == TevBias::Compare) {
            // Alpha compare: kinds 0-2 (R8/GR16/BGR24) compare the COLOR
            // selects of the stage (Dolphin's tevin registers are int4s
            // whose rgb comes from the color select); kind 3 (A8) compares
            // the alpha selects.  Result: d + (cond ? c : 0), exact ints.
            const auto kind =
                static_cast<TevCompareKind>(static_cast<unsigned>(alpha_scale));
            const bool is_eq = alpha_sub;
            s << "    {\n";
            if (kind == TevCompareKind::RGB8) {  // value 3 = A8 for alpha
                emit(s, "        int aa_i = (%s) & 255;\n", aa);
                emit(s, "        int ab_i = (%s) & 255;\n", ab);
                emit(s, "        bool acmp = (aa_i %s ab_i);\n",
                     is_eq ? "==" : ">");
            } else {
                emit(s, "        int3 ca_i = alpha_compare_a_%u;\n", st);
                emit(s, "        int3 cb_i = alpha_compare_b_%u;\n", st);
                if (kind == TevCompareKind::R8) {
                    emit(s, "        bool acmp = (ca_i.x %s cb_i.x);\n",
                         is_eq ? "==" : ">");
                } else if (kind == TevCompareKind::GR16) {
                    s << "        int av = ca_i.x + (ca_i.y << 8);\n";
                    s << "        int bv = cb_i.x + (cb_i.y << 8);\n";
                    emit(s, "        bool acmp = (av %s bv);\n",
                         is_eq ? "==" : ">");
                } else {  // BGR24
                    s << "        int av = ca_i.x + (ca_i.y << 8)"
                         " + (ca_i.z << 16);\n";
                    s << "        int bv = cb_i.x + (cb_i.y << 8)"
                         " + (cb_i.z << 16);\n";
                    emit(s, "        bool acmp = (av %s bv);\n",
                         is_eq ? "==" : ">");
                }
            }
            emit(s, "        int ac_i = (%s) & 255;\n", ac);
            emit(s, "        int ad_i = %s;\n", ad);
            s << "        int outv = ad_i + (acmp ? ac_i : 0);\n";
            if (alpha_clamp) {
                s << "        outv = clamp(outv, 0, 255);\n";
            } else {
                s << "        outv = clamp(outv, -1024, 1023);\n";
            }
            emit(s, "        %s.a = outv;\n", dest_a);
            s << "    }\n";
        } else {
            s << "    {\n";
            emit(s, "        int ai = (%s) & 255;\n", aa);
            emit(s, "        int bi = (%s) & 255;\n", ab);
            emit(s, "        int ci = (%s) & 255;\n", ac);
            emit(s, "        int di = %s;\n", ad);
            emit(s, "        int lhs = (di + %s)%s;\n",
                 tev_bias_i(alpha_bias), tev_scale_left(alpha_scale));
            s << "        int mixv = (ai << 8) + (bi - ai) * (ci + (ci >> 7));\n";
            emit(s, "        mixv = mixv%s;\n", tev_scale_left(alpha_scale));
            if (alpha_scale != TevScale::Half) {
                emit(s, "        mixv = (mixv + %s) >> 8;\n",
                     tev_lerp_round_bias(alpha_sub));
            } else {
                s << "        mixv = mixv >> 8;\n";
            }
            emit(s, "        int outv = (lhs %c mixv)%s;\n",
                 alpha_sub ? '-' : '+', tev_scale_right(alpha_scale));
            if (alpha_clamp) {
                s << "        outv = clamp(outv, 0, 255);\n";
            } else {
                s << "        outv = clamp(outv, -1024, 1023);\n";
            }
            emit(s, "        %s.a = outv;\n", dest_a);
            s << "    }\n";
        }
    }

    // The last TEV stage is what reaches the PE, regardless of which TEV
    // register that stage wrote.  Dolphin mirrors the hardware by copying the
    // last stage's non-PREV destination into PREV before alpha/fog/output.
    // Materials that end on REG0-2 (notably multi-pass highlight/indirect
    // effects) otherwise render as stale PREV.
    if (n_stages != 0u) {
        const TevStagePacked& last = key.stages[n_stages - 1u];
        const unsigned last_color_dest = (last.color_env >> 22) & 0x3u;
        const unsigned last_alpha_dest = (last.alpha_env >> 22) & 0x3u;
        if (last_color_dest != 0u) {
            emit(s, "    tev_prev_i.rgb = %s.rgb;\n",
                 tev_dest_name(last_color_dest));
        }
        if (last_alpha_dest != 0u) {
            emit(s, "    tev_prev_i.a = %s.a;\n",
                 tev_dest_name(last_alpha_dest));
        }
    }

    // The PE consumes the final TEV result on the 8-bit lattice.  Unclamped
    // TEV stages can hold wider signed intermediates, but alpha compare,
    // fog/color output, and the alpha==1 blend quirk see the low 8 bits.
    s << "\n    int4 frag_i = tev_prev_i & int4(255,255,255,255);\n";
    s << "    float4 frag = float4(frag_i) / 255.0;\n\n";

    // Alpha test.
    const auto comp0 = static_cast<CompareFunc>(key.alpha_comp0);
    const auto comp1 = static_cast<CompareFunc>(key.alpha_comp1);
    const auto logic = static_cast<AlphaTestLogic>(key.alpha_logic);

    // Dolphin evaluates alpha test on the final 8-bit TEV integer (`prev.a`),
    // not on normalized floats.  Keeping refs/sample on that lattice avoids
    // tiny threshold mistakes on cutout-heavy file-select materials.
    const AlphaTestStaticResult alpha_static =
        alpha_test_static_result(comp0, comp1, logic);

    if (alpha_static == AlphaTestStaticResult::Fail) {
        s << "    discard;\n";
    } else if (alpha_static != AlphaTestStaticResult::Pass) {
        s << "    // Alpha test\n";
        s << "    uint a_sample = uint(frag_i.a);\n";
        s << "    uint a_ref0 = uint(round(saturate(alpha_refs.x) * 255.0));\n";
        s << "    uint a_ref1 = uint(round(saturate(alpha_refs.y) * 255.0));\n";
        s << "    bool test0 = ";
        emit_alpha_test(s, comp0, "a_sample", "a_ref0");
        s << ";\n";
        s << "    bool test1 = ";
        emit_alpha_test(s, comp1, "a_sample", "a_ref1");
        s << ";\n";
        s << "    bool alpha_pass = ";
        switch (logic) {
        case AlphaTestLogic::And:  s << "test0 && test1"; break;
        case AlphaTestLogic::Or:   s << "test0 || test1"; break;
        case AlphaTestLogic::Xor:  s << "test0 != test1"; break;
        case AlphaTestLogic::Xnor: s << "test0 == test1"; break;
        }
        s << ";\n";
        s << "    if (!alpha_pass) discard;\n\n";
    }

    // Hardware tests in Dolphin show alpha value 1 passes alpha test but
    // behaves like 0 for blending/writes. Low-alpha UI masks rely on this.
    s << "    uint tev_alpha_i = uint(frag_i.a);\n";
    s << "    if (tev_alpha_i == 1u) frag.a = 0.0;\n\n";

    const bool rgba6 = (key.output_flags & 0x7u) == 1u;
    const bool dither = (key.output_flags & 0x08u) != 0u;
    if (rgba6 && dither) {
        const unsigned efb_scale =
            std::min<unsigned>(key.efb_scale_minus_one,
                               kMaxEfbScale - 1u) + 1u;
        s << "    // Flipper RGBA6 2x2 Bayer dither, applied before fog.\n";
        s << "    int3 dither_rgb = int3(round(saturate(frag.rgb) * 255.0));\n";
        if (key.efb_scale_minus_one == 0xffu) {
            // Single-sample pixel centers N+0.5 remain safely inside the
            // same logical pixel after a float reciprocal/multiply: at the
            // supported scales/extents, rounding cannot cross a boundary.
            // Keep legacy key branches unchanged for persistent DXBC reuse.
            s << "    int2 dither_xy = int2(pin.pos.xy * ztex_params.y) & 1;\n";
        } else if (efb_scale == 1u) {
            // Keep the native-resolution shader byte-for-byte equivalent.
            s << "    int2 dither_xy = int2(pin.pos.xy) & 1;\n";
        } else {
            emit(s,
                "    int2 dither_xy = (int2(pin.pos.xy) / %u) & 1;\n",
                efb_scale);
        }
        s << "    dither_rgb = (dither_rgb - (dither_rgb >> 6))"
             " + (dither_xy.x ^ dither_xy.y) * 2 + dither_xy.y;\n";
        s << "    frag.rgb = float3(dither_rgb) / 255.0;\n\n";
    }

    // Z-texture arithmetic is unsigned 24-bit hardware arithmetic. Disabled
    // later stages and TEV swaps cannot change the last raw texture sample.
    if (ztex_on || key.fog_type != static_cast<std::uint8_t>(FogType::Off)) {
        s << "    uint zcoord_i = uint(clamp((1.0 - pin.pos.z) * 16777216.0, 0.0, 16777215.0));\n";
    }
    if (ztex_on) {
        switch (ztex_fmt) {
        case 0:
            s << "    uint ztex_i = uint(last_raw_tex_i.a & 255);\n";
            break;
        case 1:
            s << "    uint ztex_i = uint((last_raw_tex_i.a & 255) * 256 + (last_raw_tex_i.r & 255));\n";
            break;
        default:
            s << "    uint ztex_i = uint((last_raw_tex_i.r & 255) * 65536 + (last_raw_tex_i.g & 255) * 256 + (last_raw_tex_i.b & 255));\n";
            break;
        }
        s << "    uint zbias_i = uint(round(ztex_params.x * 16777216.0));\n";
        if (ztex_op == 1u) {
            s << "    zcoord_i = (zcoord_i + ztex_i + zbias_i) & 0xFFFFFFu;\n";
        } else {
            s << "    zcoord_i = (ztex_i + zbias_i) & 0xFFFFFFu;\n";
        }
    }

    // Fog (type is baked, params are uniforms).
    //   fog_params[0] = (A_float, b_magnitude, b_shift, C_float)
    //   fog_params[1] = (color.r, color.g, color.b, 1.0 = orthographic)
    // Depth source is the rasterized z, NOT the TEV output (the old code
    // sampled frag.z — the output color's blue channel).  Our depth buffer
    // is REVERSED (depth = 1 - z24/2^24, see fill_projection), so the GX
    // 24-bit z is (1 - SV_Position.z) * 2^24 — same form as Dolphin's
    // reversed-depth path in PixelShaderGen (zCoord/fog block) with the
    // equations from PixelShaderManager::SetFogParamChanged.
    const auto fog_type = static_cast<FogType>(key.fog_type);
    if (fog_type != FogType::Off) {
        s << "    // Fog\n";
        s << "    uint fog_zc = zcoord_i;\n";
        s << "    float fog_ze;\n";
        s << "    if (fog_params[1].w == 0.0) {  // perspective\n";
        s << "        fog_ze = (fog_params[0].x * 16777216.0)\n";
        s << "               / float(max(1u, uint(fog_params[0].y)"
             " - (fog_zc >> uint(fog_params[0].z))));\n";
        s << "    } else {                       // orthographic\n";
        s << "        fog_ze = fog_params[0].x * float(fog_zc)"
             " / 16777216.0;\n";
        s << "    }\n";
        s << "    float fog_f = saturate(fog_ze - fog_params[0].w);\n";
        switch (fog_type) {
        case FogType::Exp:
            s << "    fog_f = 1.0 - exp2(-8.0 * fog_f);\n";
            break;
        case FogType::Exp2:
            s << "    fog_f = 1.0 - exp2(-8.0 * fog_f * fog_f);\n";
            break;
        case FogType::RevExp:
            s << "    fog_f = exp2(-8.0 * (1.0 - fog_f));\n";
            break;
        case FogType::RevExp2:
            s << "    fog_f = 1.0 - fog_f;\n";
            s << "    fog_f = exp2(-8.0 * fog_f * fog_f);\n";
            break;
        case FogType::Linear:
        default:
            break;  // linear: fog_f as-is
        }
        // Integer blend like hardware: (c*(256-ifog) + fogc*ifog) >> 8.
        s << "    int fog_i = int(round(fog_f * 256.0));\n";
        s << "    int3 fog_rgb_i = int3(round(frag.rgb * 255.0));\n";
        s << "    int3 fog_color_i = int3(round(saturate(fog_params[1].rgb) * 255.0));\n";
        s << "    frag.rgb = float3((fog_rgb_i * (256 - fog_i)"
             " + fog_color_i * fog_i) >> 8) / 255.0;\n\n";
    }

    // Convert the 8-bit TEV result to the physical EFB precision. Alpha is
    // always 6-bit; RGBA6 also quantizes RGB to 6-bit. Dual-source blending
    // consumes the original 8-bit TEV alpha through SV_Target1.
    if (dual_src) {
        s << "    float tev_alpha = saturate(frag.a);\n";
    }
    s << "    uint4 efb_color = uint4(round(saturate(frag) * 255.0));\n";
    if (rgba6) {
        s << "    frag.rgb = float3(efb_color.rgb >> 2) / 63.0;\n";
    } else {
        s << "    frag.rgb = float3(efb_color.rgb) / 255.0;\n";
    }
    if (key.flags & 0x04u) {
        s << "    uint dst_alpha6 = uint(round(saturate(alpha_refs.w)"
             " * 255.0)) >> 2;\n";
        s << "    frag.a = float(dst_alpha6) / 63.0;\n";
    } else {
        s << "    frag.a = float(efb_color.a >> 2) / 63.0;\n";
    }
    s << "\n";

    if (export_depth) {
        s << "    PSOutZ zout;\n";
        s << "    zout.color = frag;\n";
        if (dual_src) {
            s << "    zout.color1 = float4(0.0, 0.0, 0.0, tev_alpha);\n";
        }
        s << "    zout.depth = 1.0 - float(zcoord_i) / 16777216.0;\n";
        s << "    return zout;\n}\n";
    } else if (dual_src) {
        s << "    PSOut o;\n";
        s << "    o.color = frag;\n";
        s << "    o.color1 = float4(0.0, 0.0, 0.0, tev_alpha);\n";
        s << "    return o;\n}\n";
    } else {
        s << "    return frag;\n}\n";
    }

    return s.str();
}

// ===========================================================================
// describe — stable human-readable key description
// ===========================================================================

std::string ShaderGenerator::describe(const PixelShaderKey& key) {
    char buf[256];
    const char* fog = fog_type_name(static_cast<FogType>(key.fog_type));
    const char* ac0 = compare_func_name(static_cast<CompareFunc>(key.alpha_comp0));
    const char* ac1 = compare_func_name(static_cast<CompareFunc>(key.alpha_comp1));
    const char* al  = [](AlphaTestLogic l) -> const char* {
        switch (l) {
        case AlphaTestLogic::And:  return "AND";
        case AlphaTestLogic::Or:   return "OR";
        case AlphaTestLogic::Xor:  return "XOR";
        case AlphaTestLogic::Xnor: return "XNOR";
        }
        return "?";
    }(static_cast<AlphaTestLogic>(key.alpha_logic));

    std::snprintf(buf, sizeof(buf),
        "PS:tev=%u tex=%u chans=%u ind=%u fog=%s alpha=%s/%s/%s flags=0x%02x",
        static_cast<unsigned>(key.num_tev_stages),
        static_cast<unsigned>(key.num_texgens),
        static_cast<unsigned>(key.num_chans),
        static_cast<unsigned>(key.num_ind_stages),
        fog, ac0, ac1, al,
        static_cast<unsigned>(key.flags));
    return buf;
}

std::string ShaderGenerator::describe(const VertexShaderKey& key) {
    char buf[128];
    std::snprintf(buf, sizeof(buf),
        "VS:tg=%u chans=%u flags=0x%02x tmidx=0x%02x",
        static_cast<unsigned>(key.num_texgens),
        static_cast<unsigned>(key.num_chans),
        static_cast<unsigned>(key.flags),
        static_cast<unsigned>(key.texmtx_idx_mask));
    return buf;
}

}  // namespace galaxy::gx
