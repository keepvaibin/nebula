// ShaderGenerator: HLSL source for VertexShaderKey, and the fixed geometry, curved-surface and curve-sort stages. The
// pixel stage is the material shader (material_ps.hlsl, compiled at build time).
//
// Generated shaders use the uber pipelines' constant buffer layout (uber_constants.h) and root
// signature, so promoting a draw from the uber pipeline to a specialized PSO is a pipeline swap.
//
// Behaviour references (clean-room, behaviour only):
//   texgen, post-matrix, q==0:  Dolphin VertexShaderGen.cpp
//   lighting:                   Dolphin LightingShaderGen.cpp, XFMemory.h

#include "galaxy/render/shader_gen.h"

#include "galaxy/render/gx_bitfields.h"
#include "galaxy/render/draw_matrices.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/uber_constants.h"
#include "galaxy/render/render_vertex.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace galaxy::render {

// The matrix row text reads the blocks of draw_matrices.h: texture rows from row 30, a light's four rows.
static_assert(draw_rows::kTexture == 30u && draw_rows::kLightWords == 16u);
// The light text (emit_light, ppl_add_light, enh_light, the emboss texgen; uber_vs.hlsl add_light) reads a light
// as four float4: the colour in the first's w, the angle terms, the distance terms, position and direction after.
static_assert(draw_rows::kLightColorWord == 3u && draw_rows::kLightAngleWord == 4u &&
              draw_rows::kLightDistanceWord == 7u && draw_rows::kLightPositionWord == 10u &&
              draw_rows::kLightDirectionWord == 13u);
// VertexConstants::row_bases at c88 (the generated vertex shader's cbuffer and the pixel shader's light reads).
static_assert(offsetof(VertexConstants, row_bases) == 88u * 16u);

namespace {

// Append to an ostringstream using a printf-style format; text without arguments as it is.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)  // suppress snprintf security warning
#endif
template <typename... Args>
static void emit(std::ostringstream& s, const char* fmt, Args&&... args) {
    if constexpr (sizeof...(Args) == 0) {
        s << fmt;
    } else {
        char buf[4096];
        std::snprintf(buf, sizeof(buf), fmt, std::forward<Args>(args)...);
        s << buf;
    }
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Names for describe().
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

static constexpr const char* kVsConstantsDef = R"HLSL(
cbuffer VertexConstants : register(b0) {
    float4x4 projection;
    // C++ packs texgen_config as uint32_t[8][2] (tight, 64 bytes).  HLSL pads
    // each array element to 16 bytes, so uint2[8] would occupy 128 bytes and
    // shift every field after it — num_texgens/flags/pos_base then
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
    uint     pos_base;
    uint     matrix_index_a;
    uint     matrix_index_b;
    uint     sky_up;
    uint     sky_up_known;
    float4   inline_pos_matrix[3];
    float4   inline_tex_matrices[24];
    float4   inline_post_matrices[24];
};
)HLSL";

// The draw's matrices and lights (draw_matrices.h), after a cbuffer with pos_base and row_bases.
static constexpr const char* kVsMatrixRows = R"HLSL(
StructuredBuffer<float4> mtx_rows : register(t3, space1);  // the matrix ring, or a replay's output
// Position and texture rows 0-63 (positions from pos_base, textures from row_bases.y), normal rows from row_bases.x,
// post-transform rows from row_bases.z, light n's four rows at row_bases.w + 4n.
float4 matrix_row(uint r) { return mtx_rows[r < 30u ? pos_base + r : row_bases.y + (r - 30u)]; }
float4 normal_row(uint q) { return mtx_rows[row_bases.x + q]; }
float4 post_row(uint q) { return mtx_rows[row_bases.z + q]; }
float4 light_row(uint n, uint w) { return mtx_rows[row_bases.w + 4u * n + w]; }
// Position row r at the frame before (motion vectors): the rows from `base`, or kMotionOwnRows: the draw's own.
float4 matrix_row_prev(uint base, uint r) { return base == 0xFFFFFFFFu ? matrix_row(r) : mtx_rows[base + r]; }
)HLSL";

// Motion-vector variant: VertexConstants continues past the matrices to the
// motion fields in the struct's tail padding (uber_constants.h, c73-c79).
static constexpr const char* kVsMotionConstantMembers = R"HLSL(    float4   motion_skip_[8];
    // A..D, then E, F, type (0 perspective) of the draw's previous projection.
    float4   motion_prev_proj[2];
    // Previous position matrix (inline draws), or the camera delta that maps
    // a current eye-space position to its previous one.
    float4   motion_prev_pos[3];
    // xy: this draw's projection jitter (NDC); zw: half viewport, pixels.
    float4   motion_params;
    // x: 0 previous palette rows at y, 1 motion_prev_pos * current eye.
    uint4    motion_ctrl;
)HLSL";

// Interpolants of the motion variant, after every other member: unjittered current and previous clip
// x, y, w. z carries the pixel scale.
static constexpr const char* kMotionInterpolants =
    "    float4   mv_cur    : TEXCOORD12;\n"
    "    float4   mv_prev   : TEXCOORD13;\n";

// Curved surfaces (kVsKeyCurved): the vertex's curve data in (RenderVertex::curve), and to the hull and domain shaders
// the eye-space position, the displacement normal turned as normals are (w the clip jitter the draw adds, x and y),
// and the crease normal (turned, packed), the place with its first crease neighbour and the second neighbour.
static constexpr const char* kCurvedInput = "    uint3    curve    : BEND;\n";
static constexpr const char* kCurvedInterpolants =
    "    float4   curve_pos : TEXCOORD14;\n"
    "    float4   curve_nrm : TEXCOORD15;\n"
    "    uint4    curve_data : TEXCOORD16;\n";

// Curved surfaces' packed normals (curved_surfaces.h's pack_normal): octahedral, two 16-bit halves, zero for none.
static constexpr const char* kCurveCodec = R"HLSL(
float3 curve_unpack(uint packed) {
    if (packed == 0u) {
        return float3(0.0, 0.0, 0.0);
    }
    const float2 p = (float2(float(packed >> 16u), float(packed & 0xFFFFu)) - 32768.0) / 32767.0;
    float3 n = float3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}
uint curve_pack(float3 n) {
    const float sum = abs(n.x) + abs(n.y) + abs(n.z);
    if (!(sum > 0.0)) {
        return 0u;
    }
    float2 p = n.xy / sum;
    if (n.z < 0.0) {
        p = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    }
    const uint2 h = uint2(round(clamp(p, -1.0, 1.0) * 32767.0) + 32768.0);
    return (h.x << 16u) | h.y;
}
)HLSL";

// The normal corner `a` bends the edge to corner `b` by (curved_surfaces.h): its place's crease normal when `b` is one
// of its crease neighbours, else its corner's displacement normal. Both triangles holding an edge pick alike.
static constexpr const char* kCurveEdgeNormal = R"HLSL(
float3 curve_edge_normal(VSOut a, VSOut b) {
    const uint place = b.curve_data.y & 0xFFFFu;
    if (place != 0xFFFFu && (place == (a.curve_data.y >> 16u) || place == (a.curve_data.z & 0xFFFFu))) {
        return curve_unpack(a.curve_data.x);
    }
    return a.curve_nrm.xyz;
}
)HLSL";

// Interpolated redraw: a sub-frame blends a CPU-written vertex buffer with the
// matching draw's vertices of the frame before (RenderVertex, 144-byte stride;
// the renderer binds them at t1, space1 and the blend at b2). The uber vertex
// shader carries the same code.
static constexpr const char* kVertexBlendDef = R"HLSL(
cbuffer VertexBlend : register(b2) {
    uint  vb_mode;   // 0: the draw's own vertices only
    uint  vb_frame;  // element of the presented frame's values (a zero element in game frames)
    uint  vb_slot;   // the pair's verdict
    uint  vb_last;   // the last vertex of the earlier draw
    uint  vb_base;   // its first vertex's byte offset in vb_prev (0 where the view starts there)
};
ByteAddressBuffer vb_prev : register(t1, space1);
// Per pair: vertices, positions unchanged, largest move, extent (the GPU
// check of the pairing, renderer_d3d12.cpp kBlendValidateCs). Element 8192:
// the presented frame's t, the t of the frame presented before it and its
// jitter (render pixels); element 8193 stays zero.
StructuredBuffer<uint4> vb_verdict : register(t2, space1);
static float vb_t;
static float vb_t_prev;
static float2 vb_jitter;
// The vertex's object-space position in the frame presented before (motion vectors).
static float3 vb_pos_prev;
// A colour that turns visible or invisible (its alpha, the low byte, leaves or reaches zero) shows the new colour,
// and a channel that jumps by more than half its range its new value, as a coordinate step does: blended, a pane
// invisible in one frame and shading in the next lets the scene behind it flash between them (closing the pause menu
// at 240 fps). uber_vs.hlsl has the same.
uint vb_color(uint a, uint b) {
    if (((a & 0xFFu) == 0u) != ((b & 0xFFu) == 0u)) {
        return b;
    }
    uint c = 0u;
    [unroll] for (uint s = 0u; s < 32u; s += 8u) {
        const float x = float((a >> s) & 0xFFu);
        const float y = float((b >> s) & 0xFFu);
        c |= uint(abs(y - x) > 128.0 ? y : round(lerp(x, y, vb_t))) << s;
    }
    return c;
}
// A whole-texture jump is a wrap; a step of more than an eighth (an atlas
// frame) shows the new coordinate.
float vb_coord(float a, float b) {
    const float d = b - a;
    const float w = abs(d) > 0.5 ? a + round(d) : a;
    return abs(b - w) > 0.125 ? b : lerp(w, b, vb_t);
}
void vertex_blend(inout VSIn v, uint id) {
    // Root views have no bounds: every read stays inside its buffer even if
    // a compiler moves it above the checks.
    const uint4 frame = vb_verdict[min(vb_frame, 8193u)];
    vb_t = asfloat(frame.x);
    vb_t_prev = asfloat(frame.y);
    vb_jitter = asfloat(frame.zw);
    vb_pos_prev = v.pos;
    if (vb_mode == 0u) {
        return;
    }
    // The same geometry one frame earlier: positions unchanged, all moved by
    // at most a quarter of its extent, or any by at most 2% (a reordered or
    // different vertex set shows this frame's vertices).
    const uint4 verdict = vb_verdict[min(vb_slot, 8191u)];
    const float worst = asfloat(verdict.z);
    const float extent = asfloat(verdict.w);
    if (verdict.y != verdict.x && !(verdict.y == 0u && worst <= 0.25 * extent) &&
        !(worst <= 0.02 * extent)) {
        return;
    }
    const uint at = vb_base + min(id, vb_last) * 144u;
    const float3 earlier = asfloat(vb_prev.Load3(at));
    vb_pos_prev = lerp(earlier, v.pos, vb_t_prev);
    if (vb_t >= 1.0) {
        return;  // the game frame's own vertices
    }
    v.pos = lerp(earlier, v.pos, vb_t);
    const float3 n = lerp(asfloat(vb_prev.Load3(at + 12u)), v.nrm, vb_t);
    const float nn = dot(n, n);
    if (nn > 0.0) {
        v.nrm = n * sqrt(dot(v.nrm, v.nrm) / nn);
    }
    const uint2 c = vb_prev.Load2(at + 48u);
    v.color0 = vb_color(c.x, v.color0);
    v.color1 = vb_color(c.y, v.color1);
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        const float2 uv = asfloat(vb_prev.Load2(at + 56u + 8u * i));
        v.uv[i] = float2(vb_coord(uv.x, v.uv[i].x), vb_coord(uv.y, v.uv[i].y));
    }
}
)HLSL";

// Physical ray tracing's de-baked vertex colours (kVsFeatureDebake).
static constexpr const char* kVsDebakeDef = R"HLSL(
// Physical ray tracing (flags bit 6): what a de-baked vertex colour is raised by: the model's baked light at its most
// lit facing (debake_peak: its light at the player's up, RenderCore::flush_draw_state) over the light at this vertex's
// object-space normal `nrm` (nine spherical harmonics a channel, debake_sh; RenderCore::fit_debake_light), 1/4 to 4.
// No normal: 1.
float3 nebula_debake_gain(float3 nrm) {
    const float len2 = dot(nrm, nrm);
    if (!(len2 > 0.25)) {
        return float3(1.0, 1.0, 1.0);
    }
    const float3 n = nrm * rsqrt(len2);
    const float y[9] = {0.282095, 0.488603 * n.y, 0.488603 * n.z, 0.488603 * n.x, 1.092548 * n.x * n.y,
                        1.092548 * n.y * n.z, 0.315392 * (3.0 * n.z * n.z - 1.0), 1.092548 * n.x * n.z,
                        0.546274 * (n.x * n.x - n.y * n.y)};
    float3 light = float3(0.0, 0.0, 0.0);
    [unroll] for (uint i = 0u; i < 9u; ++i) {
        light.r += debake_sh[i >> 2u][i & 3u] * y[i];
        light.g += debake_sh[(9u + i) >> 2u][(9u + i) & 3u] * y[i];
        light.b += debake_sh[(18u + i) >> 2u][(18u + i) & 3u] * y[i];
    }
    const float3 peak = max(debake_peak.rgb, 1e-3);
    return clamp(peak / max(light, peak * 0.25), 0.25, 4.0);
}

// A vertex colour with its baked light removed: its hue at full brightness (black stays black).
float3 nebula_debake(float3 c) {
    const float m = max(c.r, max(c.g, c.b));
    return m > (1.0 / 255.0) ? c / m : c;
}

)HLSL";

static std::string insert_before_close(const char* text, const char* members, bool last) {
    std::string out(text);
    const std::size_t at = last ? out.rfind("};") : out.find("};");
    out.insert(at, members);
    return out;
}

// VS struct declarations (fixed 144-byte RenderVertex input layout).
// Interpolators carry float3 texcoords (S, T, Q). Projective texgens divide by Q in the PS.
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

// Per-pixel lighting variant: eye-space position and normal plus the raw
// vertex colors (material/ambient sources) follow the texcoords.
static constexpr const char* kVsStructsPpl = R"HLSL(
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
    float4   pos       : SV_Position;
    float4   col0      : COLOR0;
    float4   col1      : COLOR1;
    float3   uv[8]     : TEXCOORD;
    float3   ppl_pos   : TEXCOORD8;
    float3   ppl_nrm   : TEXCOORD9;
    float4   ppl_vcol0 : TEXCOORD10;
    float4   ppl_vcol1 : TEXCOORD11;
};
)HLSL";

// D3D12 exposes only fixed one-pixel line/point rasterization. These geometry
// shaders implement GX BP 0x22 exactly by expanding to solid quads. Constants
// are in physical pixels, so widths scale with the EFB while the clip-space
// result remains correct for every valid XF viewport.
static constexpr char kLineGeometryShader[] = R"HLSL(
cbuffer LinePointConstants : register(b0) {
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
cbuffer LinePointConstants : register(b0) {
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

// Emits one light's contribution to the channel accumulator.
//   - `n`      light index 0-7. XF light objects sit at palette element 0x180 + 4n:
//              elem+0.w = RGBA8 colour (raw bits), elem+1 = {a0,a1,a2,k0},
//              elem+2 = {k1,k2,pos.x,pos.y}, elem+3 = {pos.z,dir.x,dir.y,dir.z} (XFMemory.h Light).
//   - `ctrl`   the channel control (attenuation and diffuse functions).
//   - `alpha`  accumulate the light's alpha into `lacc_a` instead of its rgb into `lacc`.
//   - `finite` kVsKeyFiniteLightAttn: the attenuation operands (cosine, distance) stay at most
//              1e18 (squares 1e36), so a zero coefficient nulls an out-of-range cosine as on the
//              hardware. SMG's loadLightDiffuse never sets the light direction, and a stale
//              direction such as 6e36 otherwise turns 0 * cos^2 into NaN and drops the light.
// Attenuation and diffuse forms follow Dolphin LightingShaderGen.cpp.
static void emit_light(
    std::ostringstream& s,
    unsigned n,
    const XfChannelCtrl& ctrl,
    bool alpha,
    bool finite) {
    emit(s, "        {  // light %u\n", n);
    emit(s,
        "            uint  lcw  = asuint(light_row(%uu, 0u).w);\n"
        "            float4 le1 = light_row(%uu, 1u);\n"
        "            float4 le2 = light_row(%uu, 2u);\n"
        "            float4 le3 = light_row(%uu, 3u);\n",
        n, n, n, n);
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
    // 0=None, 1=Spec, 2=Dir, 3=Spot). Every normalize and divide is guarded:
    // a light at the vertex, a zero distA or a zero denominator yields 0 or
    // the fallback, never NaN or INF in the vertex colours (the hardware's
    // fixed-point math has no NaN).
    switch (ctrl.atten_func) {
    case 1u:  // Spec
        s << "            float3 ldelta = lpos - wpos.xyz;\n";
        s << "            float ldd = dot(ldelta, ldelta);\n";
        s << "            float3 ldir = (ldd > 0.0)"
             " ? ldelta * rsqrt(ldd) : nrm;\n";
        s << "            float attn = (dot(nrm, ldir) >= 0.0)"
             " ? max(0.0, dot(nrm, ldvec)) : 0.0;\n";
        if (finite) {
            s << "            attn = min(attn, 1.0e18);\n";
        }
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
        if (finite) {
            s << "            attn = min(attn, 1.0e18);\n";
            s << "            dist = min(dist, 1.0e18);\n";
            s << "            dist2 = min(dist2, 1.0e36);\n";
        }
        s << "            float denom = dot(distA, float3(1.0, dist,"
             " dist2));\n";
        s << "            attn = (denom != 0.0)\n"
             "                 ? max(0.0, cosA.x + cosA.y*attn +"
             " cosA.z*attn*attn) / denom\n"
             "                 : 0.0;\n";
        break;
    default:  // 0 = None, 2 = Dir
        // Test the length before normalizing. After normalize(), a zero vector is already NaN and the
        // fallback never fires.
        s << "            float3 ldelta = lpos - wpos.xyz;\n";
        s << "            float ldd = dot(ldelta, ldelta);\n";
        s << "            float3 ldir = (ldd > 0.0)"
             " ? ldelta * rsqrt(ldd) : nrm;\n";
        s << "            float attn = 1.0;\n";
        break;
    }

    // Diffuse function (0=none, 1=signed, 2=clamped).
    const char* acc = alpha ? "lacc_a" : "lacc";
    switch (ctrl.diffuse_func) {
    case 1u:  // Sign
        emit(s, "            %s += attn * dot(ldir, nrm) * lcol;\n", acc);
        break;
    case 2u:  // Clamp
        emit(s, "            %s += attn * max(0.0, dot(ldir, nrm)) * lcol;\n", acc);
        break;
    default:  // None
        emit(s, "            %s += attn * lcol;\n", acc);
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

// Curved surfaces' patch stages. The hull shader passes the vertices through and gives each edge as many segments as
// keep every segment's arc within a quarter pixel of its chord: an arc of angle a over chord c bulges c tan(a/4) / 2,
// about c a / 8, so n segments bulge c a / (8 n^2) and n = sqrt(c a / 2) pixels, a twice the chord's tilt from the
// tangent plane at the end that tilts it most. An edge's count depends only on its two vertices, so the two
// triangles sharing it split it alike (no cracks). The domain shader blends the three vertices at the point, then
// moves it half way to the blend of its projections onto the corners' tangent planes (Phong tessellation's shape
// factor, here one half: a sampled circle's edge midpoint then moves c sin(a) / 8 against the circle's
// c tan(a/4) / 2, onto the circle within 3 % for a 16-gon and 11 % for a hexagon, where three quarters overshoots it
// by 46 and 33 %). The move is a sum over the triangle's edges, each end projecting by the normal it bends that edge
// by (curve_edge_normal), so an edge two triangles share moves alike in both. The move goes into the clip position
// (with the draw's jitter), the per-pixel lighting position and the motion vectors. Fractional odd steps let a count
// change smoothly as the camera moves.
static constexpr const char* kCurvePatch = R"HLSL(
struct CurvePatch {
    float edges[3] : SV_TessFactor;
    float inside   : SV_InsideTessFactor;
};
)HLSL";

// An edge's segments squared, before the clamp to 1-16: its chord on screen (pixels) times its turn, halved. `a` and
// `b` are its ends' clip positions, `qa` and `qb` their eye positions, `na` and `nb` the normals each end bends it by.
// Zero for an edge that stays straight. The hull shader and the sort (curve_sort_source) both use it.
static constexpr const char* kCurveValue = R"HLSL(
float curve_value(float4 a, float4 b, float3 qa, float3 qb, float3 na, float3 nb, float2 half_viewport) {
    const float3 edge = qb - qa;
    const float edge_length = length(edge);
    if (a.w <= 0.0 || b.w <= 0.0 || !(edge_length > 0.0)) {
        return 0.0;
    }
    const float3 along = edge / edge_length;
    const float tilt = max(abs(dot(along, na)), abs(dot(along, nb)));
    if (!(tilt > 0.0)) {
        return 0.0;
    }
    const float angle = 2.0 * asin(min(tilt, 1.0));
    const float chord = length((a.xy / a.w - b.xy / b.w) * half_viewport);
    return chord * angle * 0.5;
}
)HLSL";

static constexpr const char* kCurvedHull = R"HLSL(
float curve_segments(VSOut a, VSOut b) {
    return clamp(sqrt(curve_value(a.pos, b.pos, a.curve_pos.xyz, b.curve_pos.xyz, curve_edge_normal(a, b),
                                  curve_edge_normal(b, a), 0.5 * line_point_raster.xy)), 1.0, 16.0);
}

// Whether the patch lies wholly past one of the view's side planes or behind the eye, however far its curve bulges:
// the domain shader moves a point at most three quarters of its distance to the farthest corner, so the eye-space
// sphere about the corners' centre through the farthest corner, widened by 0.75 of the longest edge, bounds it.
bool curve_outside(InputPatch<VSOut, 3> p) {
    const float3 q0 = p[0].curve_pos.xyz;
    const float3 q1 = p[1].curve_pos.xyz;
    const float3 q2 = p[2].curve_pos.xyz;
    const float3 centre = (q0 + q1 + q2) / 3.0;
    const float edge = sqrt(max(dot(q1 - q0, q1 - q0), max(dot(q2 - q1, q2 - q1), dot(q0 - q2, q0 - q2))));
    const float radius = sqrt(max(dot(q0 - centre, q0 - centre),
                                  max(dot(q1 - centre, q1 - centre), dot(q2 - centre, q2 - centre)))) +
                         0.75 * edge;
    const float4 planes[5] = {projection[3] - projection[0], projection[3] + projection[0],
                              projection[3] - projection[1], projection[3] + projection[1], projection[3]};
    [unroll] for (uint i = 0u; i < 5u; ++i) {
        if (dot(planes[i].xyz, centre) + planes[i].w + radius * length(planes[i].xyz) < 0.0) {
            return true;
        }
    }
    return false;
}

CurvePatch curve_patch(InputPatch<VSOut, 3> p) {
    CurvePatch c;
    if (curve_outside(p)) {
        c.edges[0] = 0.0;
        c.edges[1] = 0.0;
        c.edges[2] = 0.0;
        c.inside = 0.0;
        return c;
    }
    c.edges[0] = curve_segments(p[1], p[2]);
    c.edges[1] = curve_segments(p[2], p[0]);
    c.edges[2] = curve_segments(p[0], p[1]);
    c.inside = max(c.edges[0], max(c.edges[1], c.edges[2]));
    return c;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("curve_patch")]
[maxtessfactor(16.0)]
VSOut main(InputPatch<VSOut, 3> p, uint i : SV_OutputControlPointID) {
    return p[i];
}
)HLSL";

static constexpr const char* kCurvedDomain = R"HLSL(
[domain("tri")]
DSOut main(CurvePatch c, float3 b : SV_DomainLocation, const OutputPatch<VSOut, 3> p) {
    DSOut o;
    o.pos = b.x * p[0].pos + b.y * p[1].pos + b.z * p[2].pos;
    o.col0 = b.x * p[0].col0 + b.y * p[1].col0 + b.z * p[2].col0;
    o.col1 = b.x * p[0].col1 + b.y * p[1].col1 + b.z * p[2].col1;
CURVE_EXTRA_BLEND
    // Phong's move, edge by edge: -alpha times the sum over edges (i, k) of b_i b_k ((e.n_ik) n_ik - (e.n_ki) n_ki),
    // e the edge from i to k and n_ik the normal i bends it by.
    float3 move = float3(0.0, 0.0, 0.0);
    [unroll] for (uint i = 0u; i < 3u; ++i) {
        const uint k = (i + 1u) % 3u;
        const float3 e = p[k].curve_pos.xyz - p[i].curve_pos.xyz;
        const float3 ni = curve_edge_normal(p[i], p[k]);
        const float3 nk = curve_edge_normal(p[k], p[i]);
        move -= b[i] * b[k] * (dot(e, ni) * ni - dot(e, nk) * nk);
    }
    const float3 d = 0.5 * move;
    const float4 dc = mul(projection, float4(d, 0.0));
    const float2 jitter = float2(p[0].curve_pos.w, p[0].curve_nrm.w);
    o.pos += float4(dc.xy + jitter * dc.w, dc.z, dc.w);
CURVE_EXTRA_MOVE
    return o;
}
)HLSL";

// Each draw's (CurveDraw) triangles kept whole where every edge's curve_value is under kCurveFlat (the hull shader
// gives such an edge one segment and the domain shader moves nothing), with margin for the two stages' rounding; an
// edge with an end near the eye plane stays a patch. Three passes (classify, offsets, scatter) keep each list in the
// draw's order. A split draw's flat triangles come first and draw from its own indices: the sort takes the rest. A
// draw's output: two DrawIndexedArguments (whole, patches), then the whole triangles' 32-bit indices, then the
// patches'. The positions, displacement and crease normals and their pick per edge are the curved vertex shader's and
// curve_edge_normal's.
static constexpr const char* kCurveSort = R"HLSL(
struct CurveDraw {
    uint vertices;   // byte offset of the draw's first vertex
    uint indices;    // byte offset of its 16-bit indices
    uint triangles;
    uint constants;  // byte offset of its vertex constants
    uint palette;    // byte offset of its matrix palette
    uint flags;      // bit 0: position matrices from the palette
    uint output;     // byte offset of its output
    uint scratch;    // byte offset of its blocks' masks of whole triangles, then their offsets
};
#ifndef NEBULA_VULKAN
#define VK_BINDING(n)
#else
#define VK_BINDING(n) [[vk::binding(n, 0)]]
#endif
VK_BINDING(0) StructuredBuffer<CurveDraw> draws : register(t0);
VK_BINDING(5) StructuredBuffer<uint2> blocks : register(t5);  // a group's draw and first triangle
VK_BINDING(1) ByteAddressBuffer vertex_data : register(t1);
VK_BINDING(2) ByteAddressBuffer index_data : register(t2);
VK_BINDING(3) ByteAddressBuffer constant_data : register(t3);
VK_BINDING(4) ByteAddressBuffer palette_data : register(t4);
VK_BINDING(6) RWByteAddressBuffer sorted : register(u0);

static const float kCurveFlat = 0.8;
static const float kNearEye = 0.01;

struct Corner {
    float4 clip;
    float3 eye;
    float3 bend;
    float3 crease;
    uint place;
    uint first_neighbour;
    uint second_neighbour;
};

float3 turned(float3x3 cofactor, uint packed) {
    const float3 n = mul(cofactor, curve_unpack(packed));
    return dot(n, n) > 0.0 ? normalize(n) : n;
}

// A group's draw: its projection (rows 0-3), the inline position matrix (4-6), the half viewport (7.xy) and, for
// palette draws, the 64 position-matrix rows from its palette base (8-71), loaded once per group.
groupshared float4 draw_rows[72];

void load_draw_rows(CurveDraw d, uint tid) {
    if (tid < 4u) {
        draw_rows[tid] = asfloat(constant_data.Load4(d.constants + 16u * tid));
    } else if (tid < 7u) {
        draw_rows[tid] = asfloat(constant_data.Load4(d.constants + CB_INLINE_POS + 16u * (tid - 4u)));
    } else if (tid == 7u) {
        draw_rows[7] = float4(0.5 * asfloat(constant_data.Load2(d.constants + CB_RASTER)), 0.0, 0.0);
    }
    if ((d.flags & 1u) != 0u) {
        // The position and texture rows from the draw's blocks in the matrix ring (VertexConstants pos_base and
        // row_bases.y).
        const uint at = tid < 30u ? constant_data.Load(d.constants + CB_POS_BASE) + tid
                                  : constant_data.Load(d.constants + CB_ROW_BASES + 4u) + (tid - 30u);
        draw_rows[8u + tid] = asfloat(palette_data.Load4(16u * at));
    }
    GroupMemoryBarrierWithGroupSync();
}

Corner corner(CurveDraw d, uint vertex) {
    const uint at = d.vertices + vertex * VERTEX_STRIDE;
    const float3 pos = asfloat(vertex_data.Load3(at));
    const uint pn_row = min(vertex_data.Load(at + VERTEX_MTX) & 0x3Fu, 61u);
    const uint third = vertex_data.Load(at + VERTEX_MTX + 8u);
    const uint3 curve = vertex_data.Load3(at + VERTEX_CURVE);
    const uint first_row = (d.flags & 1u) != 0u ? 8u + pn_row : 4u;
    float4 rows[3];
    rows[0] = draw_rows[first_row];
    rows[1] = draw_rows[first_row + 1u];
    rows[2] = draw_rows[first_row + 2u];
    const float4 p = float4(pos, 1.0);
    const float4 wpos = float4(dot(rows[0], p), dot(rows[1], p), dot(rows[2], p), 1.0);
    const float3x3 cofactor = float3x3(cross(rows[1].xyz, rows[2].xyz), cross(rows[2].xyz, rows[0].xyz),
                                       cross(rows[0].xyz, rows[1].xyz));
    const float4x4 projection = float4x4(draw_rows[0], draw_rows[1], draw_rows[2], draw_rows[3]);
    Corner c;
    c.clip = mul(projection, wpos);
    c.eye = wpos.xyz;
    c.bend = turned(cofactor, curve.x);
    c.crease = turned(cofactor, curve.y);
    c.place = curve.z & 0xFFFFu;
    c.first_neighbour = curve.z >> 16u;
    c.second_neighbour = third >> 16u;
    return c;
}

float3 edge_normal(Corner a, Corner b) {
    if (b.place != 0xFFFFu && (b.place == a.first_neighbour || b.place == a.second_neighbour)) {
        return a.crease;
    }
    return a.bend;
}

bool edge_whole(Corner a, Corner b, float2 half_viewport) {
    if (abs(a.clip.w) < kNearEye || abs(b.clip.w) < kNearEye) {
        return false;
    }
    return curve_value(a.clip, b.clip, a.eye, b.eye, edge_normal(a, b), edge_normal(b, a), half_viewport) < kCurveFlat;
}

uint index_at(uint at) {
    const uint word = index_data.Load(at & ~3u);
    return (at & 2u) != 0u ? word >> 16u : word & 0xFFFFu;
}

groupshared uint block_mask[2];
groupshared uint scan[64];

// Pass one, a thread a triangle (a group a block of 64 of one draw): its bit in the block's mask of whole triangles.
[numthreads(64, 1, 1)]
void classify(uint3 group : SV_GroupID, uint tid : SV_GroupIndex) {
    const uint2 job = blocks[group.x];
    const CurveDraw d = draws[job.x];
    const uint t = job.y + tid;
    if (tid < 2u) {
        block_mask[tid] = 0u;
    }
    load_draw_rows(d, tid);
    if (t < d.triangles) {
        const float2 half_viewport = draw_rows[7].xy;
        const uint at = d.indices + t * 6u;
        const Corner c0 = corner(d, index_at(at));
        const Corner c1 = corner(d, index_at(at + 2u));
        const Corner c2 = corner(d, index_at(at + 4u));
        if (edge_whole(c1, c2, half_viewport) && edge_whole(c2, c0, half_viewport) &&
            edge_whole(c0, c1, half_viewport)) {
            InterlockedOr(block_mask[tid >> 5u], 1u << (tid & 31u));
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (tid < 2u) {
        sorted.Store(d.scratch + 8u * (job.y / 64u) + 4u * tid, block_mask[tid]);
    }
}

// Pass two, a group a draw: the whole triangles before each block; the lists' draw arguments.
[numthreads(64, 1, 1)]
void offsets(uint3 group : SV_GroupID, uint tid : SV_GroupIndex) {
    const CurveDraw d = draws[group.x];
    const uint block_count = (d.triangles + 63u) / 64u;
    const uint before = d.scratch + 8u * block_count;
    uint total = 0u;
    for (uint first = 0u; first < block_count; first += 64u) {
        const uint b = first + tid;
        const uint2 mask = b < block_count ? sorted.Load2(d.scratch + 8u * b) : uint2(0u, 0u);
        const uint count = countbits(mask.x) + countbits(mask.y);
        scan[tid] = count;
        GroupMemoryBarrierWithGroupSync();
        [unroll] for (uint offset = 1u; offset < 64u; offset <<= 1u) {
            const uint earlier = tid >= offset ? scan[tid - offset] : 0u;
            GroupMemoryBarrierWithGroupSync();
            scan[tid] += earlier;
            GroupMemoryBarrierWithGroupSync();
        }
        if (b < block_count) {
            sorted.Store(before + 4u * b, total + scan[tid] - count);
        }
        total += scan[63];
        GroupMemoryBarrierWithGroupSync();
    }
    if (tid == 0u) {
        sorted.Store4(d.output, uint4(total * 3u, 1u, 0u, 0u));
        sorted.Store4(d.output + 16u, uint4(0u, (d.triangles - total) * 3u, 1u, 0u));
        sorted.Store2(d.output + 32u, uint2(0u, 0u));
    }
}

// Pass three, a thread a triangle: to its list, at the whole triangles (or patches) before it in the draw.
[numthreads(64, 1, 1)]
void scatter(uint3 group : SV_GroupID, uint tid : SV_GroupIndex) {
    const uint2 job = blocks[group.x];
    const CurveDraw d = draws[job.x];
    const uint t = job.y + tid;
    if (t >= d.triangles) {
        return;
    }
    const uint block = job.y / 64u;
    const uint2 mask = sorted.Load2(d.scratch + 8u * block);
    const uint below = tid < 32u ? countbits(mask.x & ((1u << tid) - 1u))
                                 : countbits(mask.x) + countbits(mask.y & ((1u << (tid - 32u)) - 1u));
    const bool whole = ((tid < 32u ? mask.x >> tid : mask.y >> (tid - 32u)) & 1u) != 0u;
    const uint block_count = (d.triangles + 63u) / 64u;
    const uint wholes_before = sorted.Load(d.scratch + 8u * block_count + 4u * block) + below;
    const uint at = d.indices + t * 6u;
    const uint3 v = uint3(index_at(at), index_at(at + 2u), index_at(at + 4u));
    const uint whole_out = d.output + 40u;
    const uint slot = whole ? whole_out + 12u * wholes_before : whole_out + 12u * (d.triangles + t - wholes_before);
    sorted.Store3(slot, v);
}
)HLSL";

std::string ShaderGenerator::curve_sort_source() {
    char defines[256];
    std::snprintf(defines, sizeof(defines),
                  "#define VERTEX_STRIDE %zuu\n#define VERTEX_MTX %zuu\n#define VERTEX_CURVE %zuu\n"
                  "#define CB_POS_BASE %zuu\n#define CB_ROW_BASES %zuu\n#define CB_INLINE_POS %zuu\n"
                  "#define CB_RASTER %zuu\n",
                  sizeof(RenderVertex), offsetof(RenderVertex, mtx_indices), offsetof(RenderVertex, curve),
                  offsetof(VertexConstants, pos_base), offsetof(VertexConstants, row_bases),
                  offsetof(VertexConstants, inline_pos_matrix), offsetof(VertexConstants, line_point_raster));
    return std::string("// Curved surfaces' triangles sorted (ShaderGenerator::curve_sort_source).\n") + defines +
           kCurveCodec + kCurveValue + kCurveSort;
}

// The patch stages' texture coordinates: eight members at the array's semantics (TEXCOORD0-7), not the array. Split
// in those stages, the array's elements went to SPIR-V over the array's length, not the patch's vertices (glslang:
// `p.uv[k]` a float3[8], the other members float3[3]).
static std::string texcoord_members(std::string text) {
    const std::size_t at = text.find("uv[8]");
    const std::size_t start = text.rfind('\n', at) + 1u;
    const std::size_t end = text.find('\n', at) + 1u;
    std::string members;
    for (unsigned i = 0; i < 8u; ++i) {
        members += "    float3   uv" + std::to_string(i) + "      : TEXCOORD" + std::to_string(i) + ";\n";
    }
    return text.replace(start, end - start, members);
}

std::string ShaderGenerator::curved_stage_source(unsigned variant, bool domain) {
    const bool ppl = (variant & 1u) != 0u;
    const bool motion = (variant & 2u) != 0u;
    std::string s = domain ? "// Curved surfaces: domain shader (ShaderGenerator::curved_stage_source).\n"
                           : "// Curved surfaces: hull shader (ShaderGenerator::curved_stage_source).\n";
    // The vertex constants to the motion fields, line_point_raster (c65) in the motion variant's skipped rows.
    std::string members = "    float4   line_point_raster;\n";
    if (motion) {
        members += "    float4   curve_skip_[7];\n"
                   "    float4   motion_prev_proj[2];\n"
                   "    float4   motion_prev_pos[3];\n"
                   "    float4   motion_params;\n";
    }
    s += insert_before_close(kVsConstantsDef, members.c_str(), false);
    // VSOut: what the curved vertex shader writes; DSOut: the same less the curve's, as the pixel shader reads it.
    std::string vs_out = ppl ? kVsStructsPpl : kVsStructs;
    vs_out = texcoord_members(vs_out.substr(vs_out.find("struct VSOut")));
    if (motion) {
        vs_out = insert_before_close(vs_out.c_str(), kMotionInterpolants, true);
    }
    std::string ds_out = vs_out;
    ds_out.replace(ds_out.find("VSOut"), 5u, "DSOut");
    vs_out = insert_before_close(vs_out.c_str(), kCurvedInterpolants, true);
    s += vs_out + ds_out + kCurvePatch + kCurveCodec + kCurveEdgeNormal;
    if (!domain) {
        return s + kCurveValue + kCurvedHull;
    }
    std::string stages = kCurvedDomain;
    std::string blend;
    std::string move;
    for (unsigned i = 0; i < 8u; ++i) {
        const std::string uv = "uv" + std::to_string(i);
        blend += "    o." + uv + " = b.x * p[0]." + uv + " + b.y * p[1]." + uv + " + b.z * p[2]." + uv + ";\n";
    }
    if (ppl) {
        blend += "    o.ppl_pos = b.x * p[0].ppl_pos + b.y * p[1].ppl_pos + b.z * p[2].ppl_pos;\n"
                 "    o.ppl_nrm = b.x * p[0].ppl_nrm + b.y * p[1].ppl_nrm + b.z * p[2].ppl_nrm;\n"
                 "    o.ppl_vcol0 = b.x * p[0].ppl_vcol0 + b.y * p[1].ppl_vcol0 + b.z * p[2].ppl_vcol0;\n"
                 "    o.ppl_vcol1 = b.x * p[0].ppl_vcol1 + b.y * p[1].ppl_vcol1 + b.z * p[2].ppl_vcol1;\n";
        move += "    o.ppl_pos += d;\n";
    }
    if (motion) {
        blend += "    o.mv_cur = b.x * p[0].mv_cur + b.y * p[1].mv_cur + b.z * p[2].mv_cur;\n"
                 "    o.mv_prev = b.x * p[0].mv_prev + b.y * p[1].mv_prev + b.z * p[2].mv_prev;\n";
        // The unjittered clip move; the previous frame's position takes the same eye-space move (a few units, against
        // a frame's turn of the camera).
        move += "    o.mv_cur += float4(dc.xy - motion_params.xy * dc.w, 0.0, dc.w);\n"
                "    const float4 pp0 = motion_prev_proj[0];\n"
                "    o.mv_prev.xy += motion_prev_proj[1].z == 0.0\n"
                "        ? float2(pp0.x * d.x + pp0.y * d.z, pp0.z * d.y + pp0.w * d.z)\n"
                "        : float2(pp0.x * d.x, pp0.z * d.y);\n"
                "    o.mv_prev.w += motion_prev_proj[1].z == 0.0 ? -d.z : 0.0;\n";
    }
    stages.replace(stages.find("CURVE_EXTRA_BLEND\n"), 18u, blend);
    stages.replace(stages.find("CURVE_EXTRA_MOVE\n"), 17u, move);
    return s + stages;
}

std::string ShaderGenerator::generate_vs(const VertexShaderKey& key) const {
    std::ostringstream s;

    const std::string key_desc = ShaderGenerator::describe(key);
    s << "// Specialized VS: " << key_desc << "\n";
    s << "// Generated by Nebula ShaderGenerator\n\n";

    const bool ppl = (key.flags & kVsKeyPerPixelLighting) != 0u;
    const bool enhanced = (key.flags & kVsKeyEnhancedLighting) != 0u;
    const bool ppl_outputs = ppl || enhanced;
    const bool motion = (key.flags & kVsKeyMotion) != 0u;
    const bool curved = (key.flags & kVsKeyCurved) != 0u;
    // Physical ray tracing's facing fit (flags bit 6) at the struct's end (c80, uber_constants.h).
    static constexpr const char* kVsDebakeMembers =
        "    float4   debake_sh[7];\n    float4   debake_peak;\n    uint4    row_bases;\n";
    std::string structs = ppl_outputs ? kVsStructsPpl : kVsStructs;
    if (motion) {
        s << insert_before_close(kVsConstantsDef, (std::string(kVsMotionConstantMembers) + kVsDebakeMembers).c_str(),
                                 false);
        structs = insert_before_close(structs.c_str(), kMotionInterpolants, true);
    } else {
        s << insert_before_close(kVsConstantsDef,
                                 (std::string("    float4   debake_skip_[15];\n") + kVsDebakeMembers).c_str(), false);
    }
    s << kVsMatrixRows;
    if (curved) {
        structs = insert_before_close(structs.c_str(), kCurvedInput, false);
        structs = insert_before_close(structs.c_str(), kCurvedInterpolants, true);
    }
    s << structs;
    if (curved) {
        s << kCurveCodec;
    }
    const bool blend = (key.features & kVsFeatureVertexBlend) != 0u;
    const bool debake = (key.features & kVsFeatureDebake) != 0u;
    if (blend) {
        s << kVertexBlendDef;
    }

    const bool dual_tex = (key.flags & 0x04u) != 0u;

    // Whether any active channel lights (the key builder zeroes channel controls
    // beyond num_chans).
    bool lighting_used = false;
    for (unsigned ch = 0; ch < key.num_chans && ch < 2u; ++ch) {
        if (key.channel[ch].lighting != 0u || key.channel[2 + ch].lighting != 0u) {
            lighting_used = true;
        }
    }
    const bool finite_attn = (key.flags & kVsKeyFiniteLightAttn) != 0u;

    if (debake) {
        s << kVsDebakeDef;
    }

    if (blend) {
        s << "VSOut main(VSIn v, uint vertex_id : SV_VertexID) {\n";
        s << "    vertex_blend(v, vertex_id);\n";
    } else {
        // A game frame's values (kVertexBlendDef's with the frame's zero element): no jitter of a presented frame, the
        // position before the vertex's own.
        s << "VSOut main(VSIn v) {\n";
        if (motion) {
            s << "    const float2 vb_jitter = float2(0.0, 0.0);\n"
                 "    const float3 vb_pos_prev = v.pos;\n";
        }
    }

    // The position/normal matrix's XF row (PNMTX0=0, PNMTX1=3, ...; a matrix occupies rows n..n+2): the vertex's
    // PNMATIDX (BLENDINDICES) where the stream carries one, else the draw's CP MatrixIndexA, the value the loader
    // would put in each vertex (so a vertex's bytes do not depend on the draw). Clamped to the 64-row
    // position and texture rows so a bad index cannot read past the draw's blocks (matrix_row): the rows are a root
    // SRV, and hardware does not clamp out-of-bounds reads, which can page-fault the GPU.
    s << ((key.flags & 0x01u) != 0u ? "    uint pn_row = min(v.mtx_idx.x & 0x3Fu, 61u);\n"
                                    : "    uint pn_row = min(matrix_index_a & 0x3Fu, 61u);\n");
    s << "    float3 pos_in = v.pos;\n";
    if ((key.flags & 0x01u) != 0u) {
        s << "    float4 wpos = float4(\n";
        s << "        dot(matrix_row(pn_row+0u), float4(pos_in, 1.0)),\n";
        s << "        dot(matrix_row(pn_row+1u), float4(pos_in, 1.0)),\n";
        s << "        dot(matrix_row(pn_row+2u), float4(pos_in, 1.0)),\n";
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
    if (motion) {
        // Sub-frames take the presented frame's jitter here (perspective
        // draws); game frames carry theirs in the projection (motion_params.xy).
        s << "    const float2 jitter_ndc = projection[3][3] == 0.0 && motion_params.z != 0.0 &&\n"
             "        motion_params.w != 0.0 ? vb_jitter / motion_params.zw : float2(0.0, 0.0);\n"
             "    o.pos.xy += jitter_ndc * o.pos.w;\n";
        s << "    float3 prev_eye;\n";
        if ((key.flags & 0x01u) != 0u) {
            s << "    if (motion_ctrl.x == 0u) {\n"
                 "        prev_eye = float3(dot(matrix_row_prev(motion_ctrl.y, pn_row+0u), float4(vb_pos_prev, 1.0)),\n"
                 "                          dot(matrix_row_prev(motion_ctrl.y, pn_row+1u), float4(vb_pos_prev, 1.0)),\n"
                 "                          dot(matrix_row_prev(motion_ctrl.y, pn_row+2u), float4(vb_pos_prev, 1.0)));\n"
                 "    } else {\n"
                 "        prev_eye = float3(dot(motion_prev_pos[0], wpos),\n"
                 "                          dot(motion_prev_pos[1], wpos),\n"
                 "                          dot(motion_prev_pos[2], wpos));\n"
                 "    }\n";
        } else {
            s << "    prev_eye = float3(dot(motion_prev_pos[0], float4(vb_pos_prev, 1.0)),\n"
                 "                      dot(motion_prev_pos[1], float4(vb_pos_prev, 1.0)),\n"
                 "                      dot(motion_prev_pos[2], float4(vb_pos_prev, 1.0)));\n";
        }
        s << "    float4 pp0 = motion_prev_proj[0];\n"
             "    float3 prev_clip = (motion_prev_proj[1].z == 0.0)\n"
             "        ? float3(pp0.x * prev_eye.x + pp0.y * prev_eye.z,\n"
             "                 pp0.z * prev_eye.y + pp0.w * prev_eye.z, -prev_eye.z)\n"
             "        : float3(pp0.x * prev_eye.x + pp0.y, pp0.z * prev_eye.y + pp0.w, 1.0);\n"
             "    o.mv_cur = float4(o.pos.xy - (motion_params.xy + jitter_ndc) * o.pos.w,"
             " motion_params.z, o.pos.w);\n"
             "    o.mv_prev = float4(prev_clip.xy, motion_params.w, prev_clip.z);\n\n";
    }

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
    s << "        float((v.color1       ) & 0xFFu) / 255.0);\n";
    if (debake) {
        // Physical ray tracing (flags bits 2-3): an unlit channel's vertex colour is the art's baked light.
        // Bit 6: by the model's facing fit (nebula_debake_gain); else each colour over its brightest channel.
        s << "    if ((flags & 64u) != 0u) {\n"
             "        const float3 debake_gain = nebula_debake_gain(v.nrm);\n"
             "        if ((flags & 4u) != 0u) vtx_col0.rgb = saturate(vtx_col0.rgb * debake_gain);\n"
             "        if ((flags & 8u) != 0u) vtx_col1.rgb = saturate(vtx_col1.rgb * debake_gain);\n"
             "    } else {\n";
        s << "    if ((flags & 4u) != 0u) vtx_col0.rgb = nebula_debake(vtx_col0.rgb) * (float((flags >> 8u) & 0xFFu) / 255.0);\n";
        s << "    if ((flags & 8u) != 0u) vtx_col1.rgb = nebula_debake(vtx_col1.rgb) * (float((flags >> 16u) & 0xFFu) / 255.0);\n";
        s << "    }\n";
    }
    s << "\n";

    if (lighting_used) {
        // Lighting-space normal: transform by the normal (inverse-transpose) matrix, three rows at the normal
        // section's row pn_row & 31 (Dolphin VertexShaderGen I_NORMALMATRICES[posidx & 31]). The result is
        // renormalized as in Dolphin. The zero guard keeps draws without a normal attribute NaN-free.
        s << "    const uint nrm_q = pn_row & 31u;\n";
        s << "    const float3 nrm_r0 = normal_row(nrm_q).xyz;\n"
             "    const float3 nrm_r1 = normal_row(nrm_q + 1u).xyz;\n"
             "    const float3 nrm_r2 = normal_row(nrm_q + 2u).xyz;\n";
        s << "    float3 nrm_t = float3(dot(nrm_r0, v.nrm), dot(nrm_r1, v.nrm), dot(nrm_r2, v.nrm));\n";
        s << "    float3 nrm = (dot(nrm_t, nrm_t) > 0.0)"
             " ? normalize(nrm_t) : nrm_t;\n\n";
    } else if (enhanced) {
        // Unlit draws may leave the normal matrices unloaded: the position
        // matrix turns the normal (exact for rotation and uniform scale).
        const bool palette = (key.flags & 0x01u) != 0u;
        for (unsigned r = 0; r < 3u; ++r) {
            emit(s, "    const float3 nrm_row%u = %s%u%s.xyz;\n", r,
                 palette ? "matrix_row(pn_row+" : "inline_pos_matrix[", r,
                 palette ? "u)" : "]");
        }
        s << "    float3 nrm_t = float3(dot(nrm_row0, v.nrm), dot(nrm_row1, v.nrm),"
             " dot(nrm_row2, v.nrm));\n";
        s << "    float3 nrm = (dot(nrm_t, nrm_t) > 0.0)"
             " ? normalize(nrm_t) : nrm_t;\n\n";
    }

    // GX colour channels: the rasterized colour comes from the channel controls. matsource selects the
    // material register or vertex colour. With lighting enabled, the per-light sum replaces the ambient-only
    // approximation (Dolphin LightingShaderGen). Results are quantized to the 8-bit lattice, since GX
    // accumulates channels in 8-bit registers.
    for (unsigned ch = 0; ch < 2; ++ch) {
        const char* vtx = (ch == 0) ? "vtx_col0" : "vtx_col1";
        const char* out = (ch == 0) ? "o.col0" : "o.col1";
        if (ch >= key.num_chans) {
            emit(s, "    %s = %s;\n", out, vtx);
            continue;
        }
        const XfChannelCtrl cc = channel_of(key.channel[ch]);
        const XfChannelCtrl ca = channel_of(key.channel[2 + ch]);

        emit(s, "    {  // channel %u (matsrc=%u light=%u amask=0x%02X)\n",
             ch,
             cc.material_from_vertex ? 1u : 0u,
             cc.lighting_enable ? 1u : 0u,
             ca.light_mask);

        // Color part: the material colour times the light (ambient plus each light, 0..255 clamped).
        emit(s, "        float3 mat_c = %s.rgb;\n",
             cc.material_from_vertex ? vtx
                                     : (ch == 0 ? "chan_material[0]"
                                                : "chan_material[1]"));
        if (cc.lighting_enable) {
            emit(s, "        float3 lacc = %s.rgb * 255.0;\n",
                 cc.ambient_from_vertex ? vtx
                                        : (ch == 0 ? "chan_ambient[0]"
                                                   : "chan_ambient[1]"));
            for (unsigned n = 0; n < 8; ++n) {
                if ((cc.light_mask >> n) & 1u) {
                    emit_light(s, n, cc, /*alpha=*/false, finite_attn);
                }
            }
            emit(s, "        %s.rgb = mat_c * (clamp(lacc, 0.0, 255.0) / 255.0);\n", out);
        } else {
            emit(s, "        %s.rgb = mat_c;\n", out);
        }

        // Alpha part (separate channel control register).
        emit(s, "        float mat_a = %s;\n",
             ca.material_from_vertex
                 ? ((ch == 0) ? "vtx_col0.a" : "vtx_col1.a")
                 : (ch == 0 ? "chan_material[0].a" : "chan_material[1].a"));
        if (ca.lighting_enable) {
            emit(s, "        float lacc_a = %s * 255.0;\n",
                 ca.ambient_from_vertex
                     ? ((ch == 0) ? "vtx_col0.a" : "vtx_col1.a")
                     : (ch == 0 ? "chan_ambient[0].a"
                                : "chan_ambient[1].a"));
            for (unsigned n = 0; n < 8; ++n) {
                if ((ca.light_mask >> n) & 1u) {
                    emit_light(s, n, ca, /*alpha=*/true, finite_attn);
                }
            }
            emit(s, "        %s.a = mat_a * (clamp(lacc_a, 0.0, 255.0) / 255.0);\n", out);
        } else {
            emit(s, "        %s.a = mat_a;\n", out);
        }
        s << "    }\n";
    }
    s << "\n";

    if (ppl_outputs) {
        s << "    o.ppl_pos = wpos.xyz;\n";
        s << "    o.ppl_nrm = nrm;\n";
        s << "    o.ppl_vcol0 = vtx_col0;\n";
        s << "    o.ppl_vcol1 = vtx_col1;\n\n";
    }

    // Texgens, per Dolphin VertexShaderGen: input-row selection (raw
    // pos / raw normal / tex0-7), 2x4 or 3x4 texture-matrix multiply
    // (per-vertex index or CP MatrixIndexA/B default), optional normalize and
    // post-transform (dual-tex) matrix, q==0 clamp. The PS divides by Q.
    const unsigned ntg = key.num_texgens;
    emit(s, "    // %u active texgen(s)\n", ntg);

    for (unsigned i = 0; i < ntg && i < kMaxTexGens; ++i) {
        const XfTexGen tg = texgen_of(key.texgen[i]);

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
            s << "        uint emboss_q = pn_row & 31u;\n";
            s << "        float3x3 emboss_matrix = float3x3(normal_row(emboss_q).xyz,\n"
                 "                                          normal_row(emboss_q + 1u).xyz,\n"
                 "                                          normal_row(emboss_q + 2u).xyz);\n";
            s << "        float3 emboss_tangent = mul(emboss_matrix, v.tangent);\n"
                 "        float3 emboss_binormal = mul(emboss_matrix, v.binormal);\n";
            emit(s,
                 "        float4 emboss_l2 = light_row(%uu, 2u);\n"
                 "        float4 emboss_l3 = light_row(%uu, 3u);\n",
                 static_cast<unsigned>(tg.emboss_light), static_cast<unsigned>(tg.emboss_light));
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
                    " 61u);\n",
                    component,
                    shift);
            }

            // 4. 2x4 (ST) or 3x4 (STQ) multiply.
            if (per_vertex) {
                if (tg.generate_stq) {
                    s << "        float3 res = float3(\n"
                         "            dot(coord, matrix_row(tm+0u)),\n"
                         "            dot(coord, matrix_row(tm+1u)),\n"
                         "            dot(coord, matrix_row(tm+2u)));\n";
                } else {
                    s << "        float3 res = float3(\n"
                         "            dot(coord, matrix_row(tm+0u)),\n"
                         "            dot(coord, matrix_row(tm+1u)),\n"
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
                if (key.texgen[i].normalize != 0u) {
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
    if (curved) {
        // The displacement normal turned as normals are, by the position matrix's cofactors (right under any scale;
        // its sign does not matter to Phong's planes).
        const bool palette = (key.flags & 0x01u) != 0u;
        for (unsigned r = 0; r < 3u; ++r) {
            emit(s, "    const float3 curve_r%u = %s%u%s.xyz;\n", r,
                 palette ? "matrix_row(pn_row + " : "inline_pos_matrix[", r,
                 palette ? "u)" : "]");
        }
        s << "    const float3x3 curve_cofactor = float3x3(cross(curve_r1, curve_r2), cross(curve_r2, curve_r0),\n"
             "                                             cross(curve_r0, curve_r1));\n"
             "    float3 curve_n = mul(curve_cofactor, curve_unpack(v.curve.x));\n"
             "    float3 curve_c = mul(curve_cofactor, curve_unpack(v.curve.y));\n"
             "    curve_n = dot(curve_n, curve_n) > 0.0 ? normalize(curve_n) : curve_n;\n"
             "    curve_c = dot(curve_c, curve_c) > 0.0 ? normalize(curve_c) : curve_c;\n";
        emit(s, "    o.curve_pos = float4(wpos.xyz, %s);\n", motion ? "jitter_ndc.x" : "0.0");
        emit(s, "    o.curve_nrm = float4(curve_n, %s);\n", motion ? "jitter_ndc.y" : "0.0");
        s << "    o.curve_data = uint4(curve_pack(curve_c), v.curve.z, v.mtx_idx.z >> 16u, 0u);\n";
    }
    s << "\n    return o;\n}\n";

    return s.str();
}

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
        "VS:tg=%u chans=%u flags=0x%02x tmidx=0x%02x features=0x%x",
        static_cast<unsigned>(key.num_texgens),
        static_cast<unsigned>(key.num_chans),
        static_cast<unsigned>(key.flags),
        static_cast<unsigned>(key.texmtx_idx_mask),
        static_cast<unsigned>(key.features));
    return buf;
}

}  // namespace galaxy::render
