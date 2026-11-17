// SPDX-License-Identifier: GPL-3.0-only
// What every program shares: the view, the composed rows, the instance table, lights, fog's forms and the output variants.
// A program is one stage: programs/v*.hlsl and p*.hlsl (crates/nebula-import/src/cook/programs.rs) and the written ones
// beside this file. Bindings: b0 the view, b1 a generated vertex stage's material constants, b2 a generated pixel stage's
// and both stages' of a written program, b3 the draw's root constants, t16 the composed rows, t17 the instance table;
// the textures and samplers: TEX/SAMP below (every texture at t0 and every sampler at s0 in space1, the draws' lists at
// t18; or, under NEBULA_BOUND_TABLES, the draw's own table at t0 and s0 of NEBULA_TABLE_SLOTS maps).
// Vulkan sets (shader_compile_vk.cpp kDrawBindings): set 0 b0-b2 at bindings 0-2 (dynamic), t16 at 3, t17 at 4, t18 at 5
// (storage buffers); set 1 binding 0 the images (t0, space1 or the bound t0); set 2 binding 0 the samplers (s0, space1 or
// the bound s0); b3 the push constants. The compiler defines NEBULA_VULKAN, and on a device without descriptor indexing
// NEBULA_BOUND_TABLES with NEBULA_TABLE_SLOTS; otherwise the arrays are runtime-sized.
//
// Output variants (defines): NATIVE_REACTIVE (SV_Target1: the reactive mask), NATIVE_LAYER (with NATIVE_REACTIVE: the
// effects layer's colour and transmission at SV_Target2-3), NATIVE_MOTION (SV_Target1: the motion vector), NATIVE_SAMPLE
// (inputs per sample: an alpha test decides each sample of a multisampled target). Ray tracing's two: NATIVE_PREPASS
// (SV_Target0 is the draw's item and nothing else: the scene's depth and items before the tracer's light pass) and
// NATIVE_TRACED (the pixel's traced light, rt/rt_light.hlsl: every program's colour by the ground's share, a lit
// program's key and point light by theirs, program_lit.hlsli; with NATIVE_MOTION too).

#ifndef PROGRAM_HLSLI
#define PROGRAM_HLSLI

#include "nebula_limits.hlsli"

cbuffer View : register(b0) {
    row_major float4x4 projection;
    float4 motion_prev_proj[2];
    float4 motion_params;      // xy the jitter, z this frame's half viewport, w last frame's
    float4 line_point_raster;  // the viewport's width and height in pixels, the half line width and half point size
    float3 view_sky_up;        // the player's up in eye space, unit length
    uint view_flags;           // bit 0 view_sky_up known, bit 1 enhanced, bit 2 wide target, bit 3 traced light,
                               // bit 4 the traced light shown in place of the colour (--diagnostics rt-light), bit 9
                               // the camera under water with the traced light (traced_water_hue), bit 10 each water
                               // draw's way shown (rt_dev transmit_why 2: traced_sea_why)
    float4 view_scale;         // x: the frame scale; y: particles' time in this frame; z: their time in the frame before
    float4 view_traced;        // with bit 3: x the traced light's place among the textures, y the added light's,
                               // z the mirror's, w the facing's
    float4 view_volume[3];     // with bit 3 in the scene's view: [0] the depth's eye terms, [1] the viewport; [2].x the
                               // light volume's place + 1 (traced_air; 0 none), .y its sampler, .z free, .w the
                               // scattered light's place + 1 (traced_scatter)
    float4 view_transmit;      // with bit 3 in the scene's view: x T's place (glass and water seen through, bent;
                               // rt_transmit.hlsl), y 1 where drawn
    float4 view_blend_guide;   // with bit 3 in the scene's view: x the guide blended draws compare with
                               // (traced_by_air, traced_mirror_own), y 1 the second layer's, 2 the opaque layer's, 0 none
};

cbuffer Draw : register(b3) {
    uint draw_first_instance;
    uint draw_flags;
    float draw_half_line;   // a widened line's half width in pixels
    float draw_half_point;  // a widened point's half size in pixels
    uint draw_table;        // the draw's first entry in draw_tables
};

StructuredBuffer<float4> rows : register(t16);
StructuredBuffer<uint4> instances : register(t17);  // row base, last frame's row base, light base, world row base

// The painted light taken out of a vertex colour (model_write.rs debake_gains) under the traced light (view bit 3): the
// cooker's gain a channel, signed bytes of log2 x 42 (a mesh without gains reads 0: 1); over 1, bounded by its largest
// channel (its hue kept). The tracer's hits take the same (rt_shade.hlsli rt_vertex_gain).
float3 traced_vertex_gain(float3 c, uint gain) {
    if ((view_flags & 8u) == 0u) {
        return c;
    }
    const int3 steps = int3(gain << 24u, gain << 16u, gain << 8u) >> 24;
    const float3 g = max(c, 0.0) * exp2(float3(steps) / 42.0);
    return g / max(max(max(g.r, g.g), g.b), 1.0);
}

// The same gain as a multiplier, for a mesh painted by its map alone (programs.rs map_gained): its traced
// vertex forms pass it, its traced pixel forms take what they show through traced_map_shown.
float3 traced_map_gain(uint gain) {
    if ((view_flags & 8u) == 0u) {
        return float3(1.0, 1.0, 1.0);
    }
    const int3 steps = int3(gain << 24u, gain << 16u, gain << 8u) >> 24;
    return exp2(float3(steps) / 42.0);
}
// What a map-painted pixel shows, de-lit: bounded by its largest channel as traced_vertex_gain. A zero multiplier
// (a vertex form that did not pass it) leaves it.
float3 traced_map_shown(float3 c, float3 gain) {
    if ((view_flags & 8u) == 0u || !(gain.r + gain.g + gain.b > 0.0)) {
        return c;
    }
    const float3 g = max(c, 0.0) * gain;
    return g / max(max(max(g.r, g.g), g.b), 1.0);
}

// The draw's textures: TEX(n) and SAMP(n) are its n-th map and that map's sampler.
#if NEBULA_BOUND_TABLES
// Devices without descriptor indexing (Vulkan only): the draw's own table of NEBULA_TABLE_SLOTS maps, bound per draw.
#ifndef NEBULA_TABLE_SLOTS
#error NEBULA_BOUND_TABLES needs NEBULA_TABLE_SLOTS
#endif
Texture2D tex[NEBULA_TABLE_SLOTS] : register(t0);
SamplerState samp[NEBULA_TABLE_SLOTS] : register(s0);
#define TEX(n) tex[n]
#define SAMP(n) samp[n]
#else
// Every texture and sampler in one array each, bound once; the draw's list holds each map's places in them (x the
// texture, y the sampler), the same for every pixel of the draw.
// Unbounded on both back ends: Direct3D 12 binds the descriptor heap, and in SPIR-V they are OpTypeRuntimeArray
// (capability RuntimeDescriptorArray) over the image and sampler sets, whose lengths the renderer chooses.
Texture2D gTextures[] : register(t0, space1);
SamplerState gSamplers[] : register(s0, space1);
StructuredBuffer<uint2> draw_tables : register(t18);
#define TEX(n) gTextures[draw_tables[draw_table + (n)].x]
#define SAMP(n) gSamplers[draw_tables[draw_table + (n)].y]
#endif

// A draw matrix is six rows: three of its place, three of its normal.
struct Place {
    uint row;        // this vertex's matrix in the composed rows
    uint world_row;  // the same in the world rows, for coordinates generated in world space
    uint light;      // this draw's light set
    float3 eye;
    float4 clip;
#if NATIVE_MOTION
    float4 mv_cur;
    float4 mv_prev;
#endif
};

float3 row_times_point(uint r, float4 p) {
    return float3(dot(rows[r], p), dot(rows[r + 1u], p), dot(rows[r + 2u], p));
}

float3 row_times_direction(uint r, float3 n) {
    return float3(dot(rows[r].xyz, n), dot(rows[r + 1u].xyz, n), dot(rows[r + 2u].xyz, n));
}

Place native_place_moved(float3 pos, float3 before, uint draw_matrix, uint instance) {
    const uint4 draw = instances[draw_first_instance + instance];
    Place p;
    p.row = draw.x + draw_matrix * 6u;
    p.world_row = draw.w + draw_matrix * 6u;
    p.light = draw.z;
    const float4 at = float4(pos, 1.0);
    p.eye = row_times_point(p.row, at);
    p.clip = mul(projection, float4(p.eye, 1.0));
#if NATIVE_MOTION
    const float3 prev_eye = row_times_point(draw.y + draw_matrix * 6u, float4(before, 1.0));
    const float4 pp0 = motion_prev_proj[0];
    const float3 prev_clip = motion_prev_proj[1].z == 0.0
        ? float3(pp0.x * prev_eye.x + pp0.y * prev_eye.z, pp0.z * prev_eye.y + pp0.w * prev_eye.z, -prev_eye.z)
        : float3(pp0.x * prev_eye.x + pp0.y, pp0.z * prev_eye.y + pp0.w, 1.0);
    p.mv_cur = float4(p.clip.xy - motion_params.xy * p.clip.w, motion_params.z, p.clip.w);
    p.mv_prev = float4(prev_clip.xy, motion_params.w, prev_clip.z);
#endif
    return p;
}

Place native_place(float3 pos, uint draw_matrix, uint instance) {
    return native_place_moved(pos, pos, draw_matrix, instance);
}

// The vertex's normal for lighting: through the matrix's normal rows, unit length or zero.
float3 lit_normal(Place p, float3 nrm) {
    const float3 n = row_times_direction(p.row + 3u, nrm);
    return dot(n, n) > 0.0 ? normalize(n) : n;
}

// What an environment map reads: the normal turned into eye space and not normalized. The model's scale stays in it, and
// maps are authored with it: a large model repeats its map. A vertex with its own matrix takes the normal rows.
float3 eye_normal_rigid(Place p, float3 nrm) { return row_times_direction(p.row, nrm); }
float3 eye_normal_skinned(Place p, float3 nrm) { return row_times_direction(p.row + 3u, nrm); }
float3 world_position(Place p, float3 pos) { return row_times_point(p.world_row, float4(pos, 1.0)); }
float3 world_normal_rigid(Place p, float3 nrm) { return row_times_direction(p.world_row, nrm); }
float3 world_normal_skinned(Place p, float3 nrm) { return row_times_direction(p.world_row + 3u, nrm); }

// A draw's light set at `base` (Place.light): a header row (x the number of lights), then four rows a light. There is
// no limit on the count; a light number past it lights nothing.
uint light_count(uint base) { return uint(rows[base].x); }
uint light_row(uint base, uint n) { return base + 1u + 4u * n; }

// Light n's contribution to a lit channel, in 0-255 units (rgb, and a for an alpha channel): its colour by its attenuation
// (1 specular, 2 directional, 3 spot) and diffuse function (0 none, 1 signed, 2 clamped), at the eye-space position and
// normal. Four rows: colour; cosine attenuation, distance attenuation x; distance y and z, position x and y; position z,
// direction.
float4 light_term(uint base, uint n, uint atten, uint diffuse, float3 pos, float3 nrm) {
    if (n >= light_count(base)) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    const uint at = light_row(base, n);
    const float4 lcol = rows[at];
    const float4 le1 = rows[at + 1u];
    const float4 le2 = rows[at + 2u];
    const float4 le3 = rows[at + 3u];
    const float3 lpos = float3(le2.z, le2.w, le3.x);
    const float3 ldvec = float3(le3.y, le3.z, le3.w);
    const float3 cos_a = le1.xyz;
    const float3 dist_a = float3(le1.w, le2.x, le2.y);
    const float3 delta = lpos - pos;
    const float dd = dot(delta, delta);
    float3 ldir = dd > 0.0 ? delta * rsqrt(dd) : nrm;
    float attn = 1.0;
    if (atten == 1u) {
        attn = dot(nrm, ldir) >= 0.0 ? min(max(0.0, dot(nrm, ldvec)), 1.0e18) : 0.0;
        const float dad = dot(dist_a, dist_a);
        const float3 dist_attn = diffuse == 0u ? dist_a : (dad > 0.0 ? dist_a * rsqrt(dad) : dist_a);
        const float denom = dot(dist_attn, float3(1.0, attn, attn * attn));
        attn = denom != 0.0 ? max(0.0, dot(cos_a, float3(1.0, attn, attn * attn))) / denom : 0.0;
    } else if (atten == 3u) {
        const float dist = min(sqrt(dd), 1.0e18);
        const float dist2 = min(dd, 1.0e36);
        ldir = dist > 0.0 ? delta / dist : nrm;
        attn = min(max(0.0, dot(ldir, ldvec)), 1.0e18);
        const float denom = dot(dist_a, float3(1.0, dist, dist2));
        attn = denom != 0.0 ? max(0.0, cos_a.x + cos_a.y * attn + cos_a.z * attn * attn) / denom : 0.0;
    }
    const float facing = diffuse == 1u ? dot(ldir, nrm) : diffuse == 2u ? max(0.0, dot(ldir, nrm)) : 1.0;
    return attn * facing * lcol;
}

// A stage takes a register as an operand a, b or c through its low 8 bits: a value an unclamped stage left past 1 or below
// 0 wraps.
float low8(float x) { return x - floor(x * 255.0 / 256.0) * 256.0 / 255.0; }
float3 low8(float3 x) { return x - floor(x * 255.0 / 256.0) * 256.0 / 255.0; }
float4 low8(float4 x) { return x - floor(x * 255.0 / 256.0) * 256.0 / 255.0; }

// Fog curves over how far into the range a pixel is. Linear is 2, 3 and their orthographic forms.
float fog_amount(float ze, float start, float end) { return saturate((ze - start) / max(end - start, 1.0e-6)); }
float fog_exp(float f) { return 1.0 - exp2(-8.0 * f); }
float fog_exp2(float f) { return 1.0 - exp2(-8.0 * f * f); }
float fog_rev_exp(float f) { return exp2(-8.0 * (1.0 - f)); }
float fog_rev_exp2(float f) { const float g = 1.0 - f; return exp2(-8.0 * g * g); }

#if NATIVE_MOTION
#define NEBULA_TEXCOORD_IMPL(n) TEXCOORD##n
#define NEBULA_TEXCOORD(n) NEBULA_TEXCOORD_IMPL(n)
// The motion varyings follow the program's own coordinates: the text that uses them defines the two indices (numbers).
#define VARYINGS_MOTION float4 mv_cur : NEBULA_TEXCOORD(NEBULA_MOTION_CURRENT_INDEX); float4 mv_prev : NEBULA_TEXCOORD(NEBULA_MOTION_PREVIOUS_INDEX);
#define PLACE_OUT(o, p) o.pos = p.clip; o.mv_cur = p.mv_cur; o.mv_prev = p.mv_prev
#elif NATIVE_PREPASS && !NATIVE_PREPASS_ITEM
// The prepass's pixel stage takes the primitive's number where the motion form's varyings would stand: the last of its
// inputs, which no vertex stage writes. (NATIVE_PREPASS_ITEM: a device that cannot give it, below.)
#define VARYINGS_MOTION uint primitive_ : SV_PrimitiveID;
#define PLACE_OUT(o, p) o.pos = p.clip
#else
#define VARYINGS_MOTION
#define PLACE_OUT(o, p) o.pos = p.clip
#endif
#if NATIVE_SAMPLE
#define PER_SAMPLE sample
#else
#define PER_SAMPLE
#endif


#if NATIVE_PREPASS
struct NativeOut { uint2 item : SV_Target0; };
#elif NATIVE_LAYER
struct NativeOut { float4 color : SV_Target0; float reactive : SV_Target1; float4 layer : SV_Target2;
                   float4 trans : SV_Target3; };
#elif NATIVE_REACTIVE
struct NativeOut { float4 color : SV_Target0; float reactive : SV_Target1; };
#elif NATIVE_MOTION
struct NativeOut { float4 color : SV_Target0; float2 motion : SV_Target1; };
#else
struct NativeOut { float4 color : SV_Target0; };
#endif

// The colour as it is; the target rounds it once.
NativeOut native_out_of(float4 c) {
    NativeOut o;
#if NATIVE_PREPASS
    o.item = uint2(draw_first_instance + 1u, 0u);  // the draw's item, from 1 (0: a pixel no draw of the prepass holds); y: its triangle
#else
    o.color = c;
#endif
#if NATIVE_REACTIVE
    o.reactive = min(saturate(c.a), 0.9);
#endif
#if NATIVE_LAYER
    o.layer = c;
    o.trans = c;
#endif
#if NATIVE_MOTION
    o.motion = float2(0.0, 0.0);
#endif
    return o;
}

#if NATIVE_TRACED
// Whether the traced light lights this draw: its program gives its base colour (a lit program's light set put out and
// channel 0's level white, TRACED_LEVEL and program_lit.hlsli; a painted program's art with its painted light taken
// out, traced_vertex_gain), and traced_colour or traced_air light it. A draw shown as it is (flag 64: the sky and what
// is drawn with it, a glowing surface) keeps the game's own light.
bool traced_lights_draw() {
    return (view_flags & 8u) != 0u && (draw_flags & 64u) == 0u;
}
// Channel 0's light level of a lit program: white where the traced light lights the draw.
float3 traced_level(float3 level) {
    return traced_lights_draw() ? float3(1.0, 1.0, 1.0) : level;
}
#define TRACED_LEVEL(x) traced_level(x)
// The light volume stands for the pixel's light (traced_air): a particle that is no light (draw
// flag 4), or a blended draw (flag 16) off the surface its guide names: with the second layer's guide
// (view_blend_guide.y 1), behind that layer's surface there, the nearest blended one (none there: not behind); with
// the opaque layer's (2: no second layer this walk), in front of the opaque surface (a decal on it keeps its light)
// or over the sky. The guide's r is the eye depth (rt_surface.hlsl `away`), the pixel's by the volume's terms.
bool traced_by_air(float4 pixel) {
    if ((view_flags & 8u) == 0u || view_volume[2].x == 0.0 || (view_flags & 16u) != 0u) {
        return false;
    }
    if ((draw_flags & 4u) != 0u) {
        return true;
    }
    if ((draw_flags & 16u) == 0u || view_blend_guide.y == 0.0) {
        return false;
    }
    const float4 d = view_volume[0];
    const float away = d.y / ((pixel.z - d.z) / max(d.w - d.z, 1e-7) + d.x);
    const float4 guide = gTextures[uint(view_blend_guide.x)].Load(int3(pixel.xy, 0));
    if (view_blend_guide.y < 1.5) {
        return guide.a != 0.0 && away > guide.r * 1.002 + 1.0;
    }
    return guide.a == 0.0 || away < guide.r * 0.998 - 1.0;
}
// Whether the pictures at the pixel are this draw's own: never for a draw whose blend adds (draw flag 32: a glow shows
// no reflection of its own); where the second layer's guide names a surface here, only for the draw that is that
// surface's item (the layer's item picture, view_blend_guide.z: its place + 1), else by that surface's depth; where it
// names none, the opaque layer's (opaque draws, decals on them): yes.
bool traced_mirror_own(float4 pixel) {
    if ((draw_flags & (32u | 64u)) != 0u) {
        return false;  // a glow, or a draw shown as it is (flag 64)
    }
    if (view_blend_guide.y < 0.5 || view_blend_guide.y > 1.5) {
        return true;
    }
    const float4 guide = gTextures[uint(view_blend_guide.x)].Load(int3(pixel.xy, 0));
    if (guide.a == 0.0) {
        return true;
    }
    if (view_blend_guide.z > 0.5) {
        const float2 named = gTextures[uint(view_blend_guide.z) - 1u].Load(int3(pixel.xy, 0)).rg;
        return uint(named.r + 0.5) + uint(named.g + 0.5) * 1024u == draw_first_instance + 1u;
    }
    const float4 d = view_volume[0];
    const float away = d.y / ((pixel.z - d.z) / max(d.w - d.z, 1e-7) + d.x);
    return abs(away - guide.r) <= guide.r * 0.002 + 1.0;
}
// sRGB's transfer: a program's colour is display-encoded; the composite's products and sums are on linear light
// and the result is encoded again. Past 1 both curves carry on (the finish's shoulder takes it).
float3 traced_lin(float3 c) {
    c = max(c, 0.0);
    return lerp(pow((c + 0.055) / 1.055, 2.4), c / 12.92, step(c, 0.04045));
}
float3 traced_srgb(float3 l) {
    l = max(l, 0.0);
    return lerp(1.055 * pow(l, 1.0 / 2.4) - 0.055, l * 12.92, step(l, 0.0031308));
}
// Water's hue under the traced light, every body of it alike (the fog under water): half its hue, its brightness
// whole; display values.
float3 traced_water_hue(float3 c) {
    const float3 lin = traced_lin(c);
    const float l = dot(lin, float3(0.2126, 0.7152, 0.0722));
    return traced_srgb(lerp(float3(l, l, l), lin, 0.5));
}
// The same in linear light (a sea's own colour where no T is traced).
float3 traced_water_tint(float3 lin) {
    const float l = dot(lin, float3(0.2126, 0.7152, 0.0722));
    return lerp(float3(l, l, l), lin, 0.5);
}
// The colour every body of water scatters back where it is deep, one for all of them so no two waters meet at a seam:
// the game's own water colour (OceanRing's cWaterColor, 0x76D7FF) in linear light at half its hue, at luminance 0.3.
static const float3 kWaterBody = float3(0.1955, 0.3208, 0.4014);
// Water (draw flag 65536: a sea, a lake, a fall's sheet) that is not the surface the light pass saw at the pixel, where
// that surface's T is traced: nothing of its own (a fall's two faces drew its white art twice). The traced surface
// covers the pixel whole (alpha 1), and the second layer names the last drawn blended surface, not the nearest:
// Beach Bowl's lagoon and its code-drawn sea are one body drawn as two sheets, and the one not named drew the game's
// whole cyan (a band with hard edges near the island).
bool traced_second_sheet(float4 pixel) {
    if ((draw_flags & 65536u) == 0u || view_transmit.y < 0.5 || view_blend_guide.y < 0.5 || view_blend_guide.y > 1.5) {
        return false;
    }
    return gTextures[uint(view_transmit.x)].Load(int3(pixel.xy, 0)).a > 0.5;
}
// The pictures the light passes leave at the pixel (rt/rt_light.hlsl, rt_light_space.hlsl, rt_reflect_out.hlsl):
//   Light (view_traced.x)  rgb the diffuse light in light units (irradiance / pi: what a white Lambert surface shows):
//                          the sun's by its visibility and the surface's facing, the sky and bounce light I; a keep,
//                          what of the base the diffuse body shows, (1 - metal)(1 - the specular albedo);
//   Added (view_traced.y)  rgb the local lights' light D, the same units;
//   Mirror (view_traced.z) rgb the specular radiance: the lobe's albedo times what it sees, and the highlights; a 1 a
//                          surface's lobe, 4 + F glass and water (F R by the exact Fresnel).
// A surface's colour: base keep (Light + D) + its specular radiance + what it emits.
// The light a glowing body (draw flag 16384: its mesh a light this frame) sends of itself, over its lit colour, as a
// share of its base: a hot body (flag 8192: lava, a sun) burns at twice it; any other glows at kGlowEmit. The emission
// table gives each its own luminance in its place.
static const float kGlowEmit = 0.35;
float3 traced_emit(float3 base) {
    if ((draw_flags & 16384u) == 0u) {
        return float3(0.0, 0.0, 0.0);
    }
    return base * ((draw_flags & 8192u) != 0u ? 2.0 : kGlowEmit);
}
// A painted environment map's texel: where the traced light lights the pixel, none (the surface's own lobe mirrors the
// traced scene, Mirror); a glowing body's (draw flag 16384) is its own look and stays. Elsewhere the texel as it is.
float4 env_fetch(float4 painted, float4 pixel) {
    if (!traced_lights_draw() || (draw_flags & 16384u) != 0u || !traced_mirror_own(pixel)) {
        return painted;
    }
    return float4(0.0, 0.0, 0.0, painted.a);
}
// The program's fetch of a painted environment map (the generator wraps it); `i` holds the pixel.
#define ENV_FETCH(t) env_fetch(t, i.pos)
// The sea's own waves under the traced light (ocean_bowl, ocean_ring, water_road). The game adds
// three things: `pattern` (its water texture at two scrolls, multiplied), `ring` (the highlight where that meets a
// level) and `behind` (the screen behind looked up moved by its indirect texture, tinted; a painted reflection), and
// blends by `alpha`. Under the traced light the waves' painted highlight and reflection are the traced mirror's: F R'
// + (1 - F) T', R and T read moved by the same wobble (`warp`: the game's move in pixels) where the pixel's mirror is
// the sea's own; g_sea_done then, and traced_colour returns g_sea_out. Elsewhere the game's colour.
static bool g_sea_done = false;
static float4 g_sea_out = float4(0.0, 0.0, 0.0, 0.0);
// rt_dev transmit_why 2 (view bit 10): a water draw that draws at the pixel shows which way its program took, whole
// for a code-drawn sea (traced_sea), at half for model water (traced_colour): red the second layer names no surface
// here (the sheet is not in it), blue no clear surface's mirror here, magenta the clear mirror but no T, green T.
bool traced_sea_why() {
    return (view_flags & 1024u) != 0u;
}
float4 traced_sea_way(float4 pixel, bool clear, bool seen, float scale) {
    bool named = false;
    if (view_blend_guide.y > 0.5 && view_blend_guide.y < 1.5) {
        named = gTextures[uint(view_blend_guide.x)].Load(int3(pixel.xy, 0)).a != 0.0;
    }
    const float3 way = !named ? float3(1.0, 0.0, 0.0) : !clear ? float3(0.0, 0.0, 1.0) : !seen ? float3(1.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    return float4(way * scale, 1.0);
}
static const float kSeaTint = 0.12;  // where no T is traced: the water's own veil of its colour over the frame behind
#if !NATIVE_PREPASS
// Picture `place` about `at` (pixels): four taps by their bilinear weights, a tap whose a is not over `least` left out
// (another surface's pixel); `unmoved` where less than a quarter is left.
float4 traced_moved(uint place, float2 at, float least, float4 unmoved) {
    uint wide, high;
    gTextures[place].GetDimensions(wide, high);
    const float2 corner = floor(at - 0.5);
    const float2 part = at - 0.5 - corner;
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    float weight = 0.0;
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const bool right = (k & 1u) != 0u, low = (k & 2u) != 0u;
        const int2 tap = clamp(int2(corner) + int2(right ? 1 : 0, low ? 1 : 0), int2(0, 0), int2(wide, high) - 1);
        const float4 t = gTextures[place].Load(int3(tap, 0));
        const float w = (right ? part.x : 1.0 - part.x) * (low ? part.y : 1.0 - part.y);
        if (t.a > least) {
            sum += t * w;
            weight += w;
        }
    }
    return weight > 0.25 ? sum / weight : unmoved;
}
float4 traced_sea(float3 pattern, float3 ring, float3 screen, float3 tint, float3 painted, float alpha, float2 warp,
                  float4 pixel) {
    const float4 game = float4(pattern + ring + screen * tint + painted, alpha);
    if (!traced_lights_draw() || (view_flags & (16u | 64u)) != 0u || (draw_flags & (32u | 128u | 256u)) != 0u) {
        return game;
    }
    g_sea_done = true;
    // A sheet the light pass did not see here (behind the nearest blended surface: the lagoon over Beach Bowl's sea):
    // nothing of its own; the surface that is drawn covers it.
    if (traced_by_air(pixel) || !traced_mirror_own(pixel)) {
        g_sea_out = float4(0.0, 0.0, 0.0, 0.0);
        return g_sea_out;
    }
    const float a = saturate(alpha);
    const uint mirror_at = uint(view_traced.z);
    const float4 mirror_here = gTextures[mirror_at].Load(int3(pixel.xy, 0));
    const float4 light = gTextures[uint(view_traced.x)].Load(int3(pixel.xy, 0));
    const float3 d = gTextures[uint(view_traced.y)].Load(int3(pixel.xy, 0)).rgb;
    if (traced_sea_why()) {
        const bool clear = mirror_here.a > 3.5;
        const bool seen = clear && view_transmit.y > 0.5 && gTextures[uint(view_transmit.x)].Load(int3(pixel.xy, 0)).a > 0.5;
        g_sea_out = traced_sea_way(pixel, clear, seen, 1.0);
        return g_sea_out;
    }
    // The water's own colour where no T is traced: its tint at half its hue, lit.
    const float3 own = traced_water_tint(traced_lin(saturate(tint))) * light.a * (light.rgb + d);
    if (mirror_here.a <= 3.5) {
        // No clear surface's mirror here (the reflections part off): a lobe as any surface's, over the frame behind.
        const float cover = kSeaTint * a;
        g_sea_out = float4(traced_srgb((cover * own + a * mirror_here.rgb) / max(cover, 1e-4)), cover);
        return g_sea_out;
    }
    // The mirror fades with the game's alpha as the waves do (the shore's edge).
    const float4 mirror = traced_moved(mirror_at, pixel.xy + warp * a, 3.5, mirror_here);
    const float f = saturate(mirror.a - 4.0) * a;
    if (view_transmit.y > 0.5) {
        const uint t_at = uint(view_transmit.x);
        const float4 seen = traced_moved(t_at, pixel.xy + warp * a, 0.5, gTextures[t_at].Load(int3(pixel.xy, 0)));
        if (seen.a > 0.5) {
            g_sea_out = float4(traced_srgb(mirror.rgb * a + (1.0 - f) * seen.rgb), 1.0);
            return g_sea_out;
        }
    }
    // No T: F R' and the water's veil over the frame behind, which the blend lets through.
    const float veil = kSeaTint * a;
    const float cover = saturate(f + (1.0 - f) * veil);
    g_sea_out = float4(traced_srgb((mirror.rgb * a + (1.0 - f) * veil * own) / max(cover, 1e-4)), cover);
    return g_sea_out;
}
#else
float4 traced_sea(float3 pattern, float3 ring, float3 screen, float3 tint, float3 painted, float alpha, float2 warp,
                  float4 pixel) {
    return float4(pattern + ring + screen * tint + painted, alpha);  // the prepass: its alpha alone is read
}
#endif
// Set where traced_colour gave an exchange's premultiplied mirror: the fog over it in the same form.
static bool g_exchanged = false;
// An exchange's draw where it shows no mirror: the game's added colour, alpha 0 (premultiplied: an add).
float4 traced_added(float4 c) {
    return float4((draw_flags & 512u) != 0u ? c.rgb * c.a : c.rgb, 0.0);
}
float4 traced_colour(float4 c, float4 pixel) {
    if (!traced_lights_draw()) {
        return (view_flags & 8u) != 0u || (draw_flags & 256u) == 0u ? c : traced_added(c);
    }
    if (g_sea_done) {
        return g_sea_out;  // a sea's composite, made whole by traced_sea
    }
    const bool own = traced_mirror_own(pixel);
    const float4 mirror = own ? gTextures[uint(view_traced.z)].Load(int3(pixel.xy, 0)) : float4(0.0, 0.0, 0.0, 1.0);
    // An added clear shell (draw flag 256), drawn premultiplied: where the mirror picture is its glass (a: 4 + F, rgb
    // F R), F R and alpha F, so what is behind keeps 1 - F; elsewhere the game's added colour (by its alpha where the
    // game's add takes it: flag 512) with alpha 0, an add as before.
    if ((draw_flags & 256u) != 0u) {
        if (mirror.a > 3.5) {
            g_exchanged = true;
            return float4(traced_srgb(mirror.rgb), saturate(mirror.a - 4.0));
        }
        return traced_added(c);
    }
    const float4 light = gTextures[uint(view_traced.x)].Load(int3(pixel.xy, 0));
    const float3 d = gTextures[uint(view_traced.y)].Load(int3(pixel.xy, 0)).rgb;
    const float3 lit = light.a * (light.rgb + d);  // what the diffuse body shows of a white base
    if ((view_flags & 16u) != 0u) {
        return float4(lit / (1.0 + lit), c.a);  // --diagnostics rt-light: the diffuse light, x / (1 + x)
    }
    if ((view_flags & 64u) != 0u) {
        return float4(mirror.rgb / (1.0 + mirror.rgb), c.a);  // --diagnostics rt-signals S: the specular, x / (1 + x)
    }
    if (!own && traced_second_sheet(pixel)) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    if (traced_sea_why() && (draw_flags & 65536u) != 0u) {
        const bool seen = mirror.a > 3.5 && view_transmit.y > 0.5 && gTextures[uint(view_transmit.x)].Load(int3(pixel.xy, 0)).a > 0.5;
        return own ? traced_sea_way(pixel, mirror.a > 3.5, seen, 0.5) : float4(0.5, 0.5, 0.0, 1.0);
    }
    // Water's own colour at half its hue, every body alike (traced_water_tint): a sea, a lake, a fall (flag 1024).
    const float3 base = (draw_flags & (65536u | 1024u)) != 0u ? traced_water_tint(traced_lin(c.rgb)) : traced_lin(c.rgb);
    const float3 diffuse = base * lit + traced_emit(base);
    if (mirror.a > 3.5) {
        // Glass and water: F R + (1 - F) T where T is traced (High: what lies behind, bent), the pixel covered whole;
        // else F R and the surface's own colour over the frame behind, which the blend lets through:
        // cover = F + (1 - F) alpha.
        const float f = saturate(mirror.a - 4.0);
        if (view_transmit.y > 0.5) {
            const float4 seen = gTextures[uint(view_transmit.x)].Load(int3(pixel.xy, 0));
            if (seen.a > 0.5) {
                return float4(traced_srgb(mirror.rgb + (1.0 - f) * seen.rgb), 1.0);
            }
        }
        const float a = saturate(c.a);
        const float cover = f + (1.0 - f) * a;
        return float4(traced_srgb((mirror.rgb + (1.0 - f) * a * diffuse) / max(cover, 1e-4)), cover);
    }
    return float4(traced_srgb(diffuse + mirror.rgb), c.a);
}
// The light in the air (rt/rt_volume.hlsl), for a draw that is lit and not a light (draw flag
// 4: a particle that is no light): its colour times what of the stage's light and the local lights' reaches the air
// at the pixel's place, one trilinear fetch of the volume (96 x 54 across the viewport view_volume[1], 48 slices
// at z = 50 (20000 / 50)^(k / 48); view_volume[0] the depth's eye terms, as rt_eye_z). Every texture is also a
// Texture3D in space 2 (the same table: renderer_d3d12.cpp, vk shader_compile_vk.cpp); the volume's slot is one.
Texture3D<float4> gVolumes[] : register(t0, space2);
float4 traced_air(float4 c, float4 pixel) {
    if ((view_flags & 8u) == 0u || view_volume[2].x == 0.0 || (view_flags & 16u) != 0u) {
        return traced_colour(c, pixel);
    }
    const float4 d = view_volume[0];
    const float away = d.y / ((pixel.z - d.z) / max(d.w - d.z, 1e-7) + d.x);
    const float3 cells = float3(96.0, 54.0, 48.0);
    const float3 at = float3((pixel.xy - view_volume[1].xy) / view_volume[1].zw, log(max(away, 1.0) / 50.0) / log(400.0));
    const float3 inner = clamp(at, 0.5 / cells, 1.0 - 0.5 / cells);
    const float4 air = gVolumes[uint(view_volume[2].x) - 1u].SampleLevel(gSamplers[uint(view_volume[2].y)], inner, 0.0);
    return float4(traced_srgb(traced_lin(c.rgb) * air.rgb), c.a);  // in linear light
}
// The light the air between the eye and the pixel scatters toward the eye (rt/rt_scatter.hlsl, 15.4; High, a stage
// with air), added, for a draw that covers or alpha-blends (draw flag 8): its picture's place + 1 in view_volume[2].w.
// One medium: a draw with a fog step of the game's (program_fog ran) has the game's fog for its
// air, and takes none of the traced.
static bool g_fog_step = false;
static bool g_fog_live = false;  // and it fogs: an amount here, or a curve whose end lies past its start
float4 traced_scatter(float4 c, float4 pixel) {
    const bool alone = (view_flags & 128u) != 0u;  // signal A: the air's own light alone, x / (1 + x)
    if ((view_flags & 256u) != 0u) {
        return c;  // signal F (program_fogged_at) as it is
    }
    if ((draw_flags & 8u) == 0u || (view_flags & 8u) == 0u || view_volume[2].w == 0.0 || (view_flags & 16u) != 0u ||
        (g_fog_step && g_fog_live)) {
        return alone ? float4(0.0, 0.0, 0.0, c.a) : c;
    }
    const float4 d = view_volume[0];
    const float away = d.y / ((pixel.z - d.z) / max(d.w - d.z, 1e-7) + d.x);
    const float3 cells = float3(96.0, 54.0, 48.0);
    const float3 at = float3((pixel.xy - view_volume[1].xy) / view_volume[1].zw, log(max(away, 1.0) / 50.0) / log(400.0));
    const float3 inner = clamp(at, 0.5 / cells, 1.0 - 0.5 / cells);
    const float4 air = gVolumes[uint(view_volume[2].w) - 1u].SampleLevel(gSamplers[uint(view_volume[2].y)], inner, 0.0);
    if (alone) {
        return float4(air.rgb / (1.0 + air.rgb), c.a);
    }
    // What the air lets through of the colour, and its own light, in linear light.
    return float4(traced_srgb(traced_lin(c.rgb) * air.a + air.rgb), c.a);
}
#if NATIVE_PREPASS
float3 program_fog(float3 c, float3 colour, float amount) { return lerp(c, colour, amount); }
float3 program_fog_curve(float3 c, float3 colour, float amount, float4 curve) { return lerp(c, colour, amount); }
float4 program_fogged(float4 c) { return c; }
float4 program_fogged_at(float4 c, float4 pixel) { return c; }
#else
// The game's fog over the traced light: a program's fog step (the generated fog line, native_fog) keeps its
// colour and amount, and PROGRAM_LIGHT puts the light on the surface's colour first and the fog over it.
static float3 g_fog_colour = float3(0.0, 0.0, 0.0);
static float g_fog_amount = 0.0;
// The fog step with its curve (the cooker's fog line, native_fog): x its start, y its end, z the pixel's eye depth, w
// its type (GX: 2 linear, 4 exp, 5 exp2, 6 reverse exp, 7 reverse exp2; bit 3 orthographic); 0 none known.
static float4 g_fog_curve = float4(0.0, 0.0, 0.0, 0.0);
float3 program_fog(float3 c, float3 colour, float amount) {
    // Under water (view bit 9) the fog is the water's: its hue as water's (traced_water_hue).
    g_fog_colour = (view_flags & 512u) != 0u ? traced_water_hue(colour) : colour;
    g_fog_amount = amount;
    g_fog_step = true;
    // A fog line whose fog is off (amount 0, start = end) leaves the air to the tracer.
    g_fog_live = amount > 0.0 || (uint(g_fog_curve.w + 0.5) != 0u && g_fog_curve.y > g_fog_curve.x);
    return c;
}
float4 program_fogged(float4 c) { return g_fog_amount > 0.0 ? float4(lerp(c.rgb, g_fog_colour, g_fog_amount), c.a) : c; }
float3 program_fog_curve(float3 c, float3 colour, float amount, float4 curve) {
    g_fog_curve = curve;
    return program_fog(c, colour, amount);
}
// Where (0 at the start, 1 at the end) the fog's curve reaches `f`: the inverse of fog_exp and the others.
float fog_x_of(float f, uint type) {
    switch (type & 7u) {
    case 4u: return -log2(max(1.0 - f, 1.0e-6)) / 8.0;
    case 5u: return sqrt(-log2(max(1.0 - f, 1.0e-6)) / 8.0);
    case 6u: return 1.0 + log2(max(f, 1.0e-6)) / 8.0;
    case 7u: return 1.0 - sqrt(max(-log2(max(f, 1.0e-6)), 0.0) / 8.0);
    default: return f;
    }
}
// What of the fog is lit (one medium): the light volume's share (view_volume[2].x) over the fog's
// eye way, each step by the fog it adds. 1 in open sun under the whole sky.
float3 fog_share(float4 pixel) {
    const uint type = uint(g_fog_curve.w + 0.5);
    if (type == 0u || (type & 8u) != 0u || (view_flags & 8u) == 0u || view_volume[2].x == 0.0 || (view_flags & 16u) != 0u) {
        return float3(1.0, 1.0, 1.0);
    }
    const float3 cells = float3(96.0, 54.0, 48.0);
    const float2 across = (pixel.xy - view_volume[1].xy) / view_volume[1].zw;
    float3 sum = float3(0.0, 0.0, 0.0);
    for (uint j = 0u; j < 8u; ++j) {
        const float x = saturate(fog_x_of((float(j) + 0.5) / 8.0 * g_fog_amount, type));
        const float z = min(lerp(g_fog_curve.x, g_fog_curve.y, x), g_fog_curve.z);
        const float3 at = clamp(float3(across, log(max(z, 1.0) / 50.0) / log(400.0)), 0.5 / cells, 1.0 - 0.5 / cells);
        sum += gVolumes[uint(view_volume[2].x) - 1u].SampleLevel(gSamplers[uint(view_volume[2].y)], at, 0.0).rgb;
    }
    return sum * 0.125;
}
// The camera under water with the traced light (view bits 3 and 9): the eye's way to the surface is water, one
// medium with the water seen from above (rt_common.hlsli rt_water_absorb, rt_water_cover; program.hlsli
// traced_mirror): what the way lets through of the surface, pure water's absorption at half its hue over the stage's
// clear way (rt_stages.tsv's default, 1770 units), and the water's one colour (kWaterBody) by what it takes, lit as
// the fog is. It stands in for the game's fog there: far things fade into the water's blue, near ones keep their own.
static const float kUnderWaterClear = 1770.0;
float4 traced_under_water(float4 c, float4 pixel) {
    const float4 d = view_volume[0];
    const float away = max(d.y / ((pixel.z - d.z) / max(d.w - d.z, 1e-7) + d.x), 0.0);
    const float3 full = exp(-away / kUnderWaterClear * float3(0.340, 0.0565, 0.0092) / 0.0565);
    const float l = dot(full, float3(0.2126, 0.7152, 0.0722));
    const float3 through = lerp(float3(l, l, l), full, 0.5);
    const float3 own = (1.0 - l) * kWaterBody * fog_share(pixel) * (g_exchanged ? c.a : 1.0);
    return float4(traced_srgb(traced_lin(c.rgb) * through + own), c.a);
}
float4 program_fogged_at(float4 c, float4 pixel) {
    if ((view_flags & (8u | 512u)) == (8u | 512u) && (view_flags & 256u) == 0u) {
        return traced_under_water(c, pixel);
    }
    if ((view_flags & 256u) != 0u) {
        // Signal F (--diagnostics rt-signals): the fog's lit share alone, x / (1 + x); 0 with no fog of the game's.
        const float3 f = g_fog_amount > 0.0 ? fog_share(pixel) : float3(0.0, 0.0, 0.0);
        return float4(f / (1.0 + f), c.a);
    }
    // The share is linear light, the fog's colour a display value: lit in linear light.
    // An exchange's colour is premultiplied: the fog over it by its alpha.
    const float3 fog = traced_srgb(traced_lin(g_fog_colour) * fog_share(pixel)) * (g_exchanged ? c.a : 1.0);
    return g_fog_amount > 0.0 ? float4(lerp(c.rgb, fog, g_fog_amount), c.a) : c;
}
#endif
// A program returns inside its entry, whose input `i` holds the pixel. The light on the surface, the game's fog, the
// light in the air between the eye and the surface.
// A draw shown as it is (draw flag 64: the sky and what is drawn with it, a glowing surface): its colour and the
// game's fog as authored, and the air in front where the host asks (flag 8: never on the sky).
#define PROGRAM_LIGHT(c) ((draw_flags & 64u) != 0u ? traced_scatter(program_fogged(c), i.pos) \
                                                   : traced_scatter(program_fogged_at(traced_by_air(i.pos) ? traced_air(c, i.pos) : traced_colour(c, i.pos), i.pos), i.pos))
#else
// Fog at once (the same math as the fog step had in place).
float3 program_fog(float3 c, float3 colour, float amount) { return lerp(c, colour, amount); }
float3 program_fog_curve(float3 c, float3 colour, float amount, float4 curve) { return lerp(c, colour, amount); }
#define PROGRAM_LIGHT(c) (c)
// No traced light in this form: a sea's colour is the game's.
float4 traced_sea(float3 pattern, float3 ring, float3 screen, float3 tint, float3 painted, float alpha, float2 warp,
                  float4 pixel) {
    return float4(pattern + ring + screen * tint + painted, alpha);
}
#endif

#ifndef ENV_FETCH
#define ENV_FETCH(t) (t)
#endif
#ifndef TRACED_LEVEL
#define TRACED_LEVEL(x) (x)
#endif

#if NATIVE_PREPASS && NATIVE_PREPASS_ITEM
// A Vulkan device without geometry shaders gives a pixel stage no primitive number: the item only, and the
// surface pass finds the triangle by a ray (rt_surface.hlsl kItemNoTriangle).
#define PROGRAM_RETURN(c) { NativeOut out_ = native_out_of(c); out_.item.y = 0xFFFFFFFFu; return out_; }
#elif NATIVE_PREPASS
// The prepass names the pixel's triangle too (the primitive's number, among a pixel stage's inputs: VARYINGS_MOTION).
#define PROGRAM_RETURN(c) { NativeOut out_ = native_out_of(c); out_.item.y = i.primitive_; return out_; }
#elif NATIVE_MOTION
// The move since the previous frame in pixels (mv_cur.z and mv_prev.z: the half viewport).
#define PROGRAM_RETURN(c) { NativeOut out_ = native_out_of(PROGRAM_LIGHT(c)); \
    if (i.mv_cur.w > 0.0 && i.mv_prev.w > 0.0) { \
        out_.motion = (i.mv_prev.xy / i.mv_prev.w - i.mv_cur.xy / i.mv_cur.w) * float2(i.mv_cur.z, i.mv_prev.z); \
    } return out_; }
#else
#define PROGRAM_RETURN(c) return native_out_of(PROGRAM_LIGHT(c))
#endif

// The inspector's picture of one selected stage; nothing otherwise.
#if defined(NATIVE_SHOW_STAGE)
#define NATIVE_STAGE(n, value) if (n == NATIVE_SHOW_STAGE) { PROGRAM_RETURN(value); }
#else
#define NATIVE_STAGE(n, value)
#endif

#endif  // PROGRAM_HLSLI
