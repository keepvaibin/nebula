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
                               // bit 4 the traced light shown in place of the colour (--diagnostics rt-light)
    float4 view_scale;         // x: the frame scale; y: particles' time in this frame; z: their time in the frame before
    float4 view_traced;        // with bit 3: x the traced light's place among the textures, y the added light's,
                               // z the mirror's
    float4 view_tint;          // with bit 3: rgb what a shadow takes back from the sky (the stage's ambient colour,
                               // by a share): a shadow is the sky's colour, not grey
    float4 view_volume[3];     // with bit 3 in the scene's view: the light volume (traced_air), [2].z 1 where it is
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
#elif NATIVE_PREPASS
// The prepass's pixel stage takes the primitive's number where the motion form's varyings would stand: the last of its
// inputs, which no vertex stage writes.
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
// The pixel's traced light: r the sun's share, g the fill's (all of it), b the lamp's, a the ground's (drop shadows,
// and the sun's shadow on what is not lit). All of every light when the view carries none (view_flags bit 3).
float4 traced_at(float4 pixel) {
    if ((view_flags & 8u) == 0u) {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    return gTextures[uint(view_traced.x)].Load(int3(pixel.xy, 0));
}
// The colour under it: darkened by the ground's share, and lit again by what glows near the pixel (the added light:
// each emitter's colour by how much of it reaches the surface, as a share of the surface's own colour). Then what the
// surface mirrors, over it (rgb, already by how much the surface mirrors; a: how much of the colour under it stays,
// plus 2 on a blended surface: there what stays is taken from its alpha, and the mirror is given back the share
// the blend would take of it).
float4 traced_mirror(float3 colour, float alpha, float4 pixel) {
    const float4 mirror = gTextures[uint(view_traced.z)].Load(int3(pixel.xy, 0));
    if (mirror.a > 1.5) {
        // Blended: its alpha by what stays (glass clears where a real mirror stands for its painted one), and the
        // mirror by that alpha's inverse, so the blend gives all of it.
        const float stays = alpha * (mirror.a - 2.0);
        return float4(colour + mirror.rgb / max(stays, 0.04), stays);
    }
    return float4(colour * mirror.a + mirror.rgb, alpha);
}
float4 traced_colour(float4 c, float4 pixel) {
    if ((view_flags & 8u) == 0u) {
        return c;
    }
    const float4 light = gTextures[uint(view_traced.x)].Load(int3(pixel.xy, 0));
    const float3 added = gTextures[uint(view_traced.y)].Load(int3(pixel.xy, 0)).rgb;
    return (view_flags & 16u) != 0u ? float4(float3(light.r, light.a, light.b) * 0.7 + added, c.a) : traced_mirror(c.rgb * (light.a + (1.0 - light.a) * view_tint.rgb + added), c.a, pixel);
}
// The light in the air (rt/rt_volume.hlsl, the master plan's 15.2), for a draw that is lit and not a light (draw flag
// 4: a particle that is no light): its colour times what of the stage's light and the local lights' reaches the air
// at the pixel's place, one trilinear fetch of the volume (96 x 54 across the viewport view_volume[1], 48 slices
// at z = 50 (20000 / 50)^(k / 48); view_volume[0] the depth's eye terms, as rt_eye_z). Every texture is also a
// Texture3D in space 2 (the same table: renderer_d3d12.cpp, vk shader_compile_vk.cpp); the volume's slot is one.
Texture3D<float4> gVolumes[] : register(t0, space2);
float4 traced_air(float4 c, float4 pixel) {
    if ((view_flags & 8u) == 0u || view_volume[2].z == 0.0 || (view_flags & 16u) != 0u) {
        return traced_colour(c, pixel);
    }
    const float4 d = view_volume[0];
    const float away = d.y / ((pixel.z - d.z) / max(d.w - d.z, 1e-7) + d.x);
    const float3 cells = float3(96.0, 54.0, 48.0);
    const float3 at = float3((pixel.xy - view_volume[1].xy) / view_volume[1].zw, log(max(away, 1.0) / 50.0) / log(400.0));
    const float3 inner = clamp(at, 0.5 / cells, 1.0 - 0.5 / cells);
    const float4 air = gVolumes[uint(view_volume[2].x)].SampleLevel(gSamplers[uint(view_volume[2].y)], inner, 0.0);
    return float4(c.rgb * air.rgb, c.a);
}
// The light the air between the eye and the pixel scatters toward the eye (rt/rt_scatter.hlsl, 15.4; High, a stage
// with air), added, for a draw that covers or alpha-blends (draw flag 8): its picture's place + 1 in view_volume[2].w.
float4 traced_scatter(float4 c, float4 pixel) {
    if ((draw_flags & 8u) == 0u || (view_flags & 8u) == 0u || view_volume[2].w == 0.0 || (view_flags & 16u) != 0u) {
        return c;
    }
    const float4 d = view_volume[0];
    const float away = d.y / ((pixel.z - d.z) / max(d.w - d.z, 1e-7) + d.x);
    const float3 cells = float3(96.0, 54.0, 48.0);
    const float3 at = float3((pixel.xy - view_volume[1].xy) / view_volume[1].zw, log(max(away, 1.0) / 50.0) / log(400.0));
    const float3 inner = clamp(at, 0.5 / cells, 1.0 - 0.5 / cells);
    const float3 air = gVolumes[uint(view_volume[2].w) - 1u].SampleLevel(gSamplers[uint(view_volume[2].y)], inner, 0.0).rgb;
    return float4(c.rgb + air, c.a);
}
// A program returns inside its entry, whose input `i` holds the pixel.
#define PROGRAM_LIGHT(c) traced_scatter((draw_flags & 4u) != 0u ? traced_air(c, i.pos) : traced_colour(c, i.pos), i.pos)
#else
#define PROGRAM_LIGHT(c) (c)
#endif

#if NATIVE_PREPASS
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
