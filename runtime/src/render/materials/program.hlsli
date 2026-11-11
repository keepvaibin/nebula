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
                               // z the mirror's, w the facing's
    float4 view_stage_a;       // with bit 3: rgb the stage's ambient colour A, w kShadowTint (RT-MASTER-PLAN 3.4)
    float4 view_stage_k;       // with bit 3: rgb the stage's key colour K
    float4 view_volume[3];     // with bit 3 in the scene's view: [0] the depth's eye terms, [1] the viewport; [2].x the
                               // light volume's place + 1 (traced_air; 0 none), .y its sampler, .z the indirect ratio's
                               // place + 1 (traced_o), .w the scattered light's place + 1 (traced_scatter)
    float4 view_transmit;      // with bit 3 in the scene's view: x T's place (glass and water seen through, bent;
                               // rt_transmit.hlsl), y 1 where drawn
    float4 view_blend_guide;   // with bit 3 in the scene's view: x the guide blended draws compare with
                               // (traced_by_air, traced_mirror_own), y 1 the second layer's, 2 the opaque layer's, 0 none
    float4 view_look;          // with bit 3: the painted composite's look (traced_painted): x kForm, y the stage's m
                               // (what the art is taken to hold of the sun's facing), z kDebake, w kAmbientLook
    float4 view_dev[2];        // with bit 3: the development tunables (rt_dev.h from base_floor): [0] the values, [1] 1
                               // where the run gave one; x base_floor
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
// The pixel's traced light (the light pass's): rgb V, the sun's share a channel; a O, the sky's share. All of every
// light when the view carries none (view_flags bit 3). A lit program sets g_traced_lit (program_lit.hlsli).
static bool g_traced_lit = false;
// A lit program's channel 0 light level (the generator's TRACED_LEVEL), so the added light takes its colour before
// lighting: c / level.
static float3 g_traced_level = float3(1.0, 1.0, 1.0);
float3 traced_level(float3 level) {
    g_traced_level = level;
    return level;
}
#define TRACED_LEVEL(x) traced_level(x)
// The light volume stands for the pixel's light (traced_air; the master plan's 15.2): a particle that is no light (draw
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
// Whether the mirror picture at the pixel is this draw's own (FB with FA and FC): never for a draw whose blend adds
// (draw flag 32: a glow shows no reflection of its own); where the second layer's guide names a surface here, only for
// the draw at that surface's depth; else it is the opaque layer's (opaque draws, decals on them): yes.
bool traced_mirror_own(float4 pixel) {
    if ((draw_flags & 32u) != 0u) {
        return false;
    }
    if (view_blend_guide.y < 0.5 || view_blend_guide.y > 1.5) {
        return true;
    }
    const float4 guide = gTextures[uint(view_blend_guide.x)].Load(int3(pixel.xy, 0));
    if (guide.a == 0.0) {
        return true;
    }
    const float4 d = view_volume[0];
    const float away = d.y / ((pixel.z - d.z) / max(d.w - d.z, 1e-7) + d.x);
    return abs(away - guide.r) <= guide.r * 0.002 + 1.0;
}
float4 traced_at(float4 pixel) {
    if ((view_flags & 8u) == 0u || traced_by_air(pixel)) {
        return float4(1.0, 1.0, 1.0, 1.0);  // the volume's share is all of its light (PROGRAM_LIGHT)
    }
    return gTextures[uint(view_traced.x)].Load(int3(pixel.xy, 0));
}
// The colour under it: darkened by the ground's share, and lit again by what glows near the pixel (the added light:
// each emitter's colour by how much of it reaches the surface, as a share of the surface's own colour). Then what the
// surface mirrors, over it (rgb, already by how much the surface mirrors; a: how much of the colour under it stays,
// plus 2 on a blended surface: there what stays is taken from its alpha, and the mirror is given back the share
// the blend would take of it).
float4 traced_mirror(float3 colour, float alpha, float4 pixel) {
    if (!traced_mirror_own(pixel)) {
        return float4(colour, alpha);  // another surface's mirror, or a glow (traced_mirror_own)
    }
    const float4 mirror = gTextures[uint(view_traced.z)].Load(int3(pixel.xy, 0));
    if (mirror.a < -0.5) {
        return float4(colour, alpha);  // a substitute: the environment fetch took the mirror in (env_fetch)
    }
    if (mirror.a > 3.5) {
        // Glass and water (RT-MASTER-PLAN 13.2, 13.3): F = a - 4, the body's share of the alpha in the added
        // picture's a, rgb F R.
        const float f = saturate(mirror.a - 4.0);
        const float body = (1.0 - f) * saturate(gTextures[uint(view_traced.y)].Load(int3(pixel.xy, 0)).a) * alpha;
        const float covered = f + body;
        if (view_transmit.y > 0.5) {
            // High (rt_transmit.hlsl): what lies behind traced bent; the rest of the pixel is it, covered whole.
            const float4 seen = gTextures[uint(view_transmit.x)].Load(int3(pixel.xy, 0));
            if (seen.a > 0.5) {
                return float4(body * colour + mirror.rgb + (1.0 - covered) * seen.rgb, 1.0);
            }
        }
        return float4((body * colour + mirror.rgb) / max(covered, 1e-4), covered);
    }
    if (mirror.a > 1.5) {
        // Blended: its alpha by what stays (glass clears where a real mirror stands for its painted one), and the
        // mirror by that alpha's inverse, so the blend gives all of it.
        const float stays = alpha * (mirror.a - 2.0);
        return float4(colour + mirror.rgb / max(stays, 0.04), stays);
    }
    return float4(colour * mirror.a + mirror.rgb, alpha);
}
// A painted environment map's texel where the light pass traced what the surface mirrors (RT-MASTER-PLAN 12.4
// SUBSTITUTE): the mirror picture holds rgb R tinted by the map's own colour and the class's gain, a -(1 + keep), keep
// how much of the painted shine comes back where R is dark. Elsewhere the texel as it is.
float4 env_fetch(float4 painted, float4 pixel) {
    if ((view_flags & 8u) == 0u || !traced_mirror_own(pixel)) {
        return painted;
    }
    const float4 mirror = gTextures[uint(view_traced.z)].Load(int3(pixel.xy, 0));
    return mirror.a < -0.5 ? float4(mirror.rgb + painted.rgb * (-mirror.a - 1.0), painted.a) : painted;
}
// The program's fetch of a painted environment map (the generator wraps it); `i` holds the pixel.
#define ENV_FETCH(t) env_fetch(t, i.pos)
// The light under a painted surface (RT-MASTER-PLAN 3.4): its art holds the stage's light, ambient A and key K by how
// it faces the key (nl); the traced light keeps the key's part by V and the ambient's by O, a channel at a time
// (a shadow takes the sky's colour, and a coloured thing's shadow its colour) and in luminance, mixed by
// kShadowTint. In open sun with nothing near: 1.
// The relight (DIRECTOR.md C3): the art is taken to hold A + K m, m = lerp(nl, the stage's m, kForm): kForm 0 is the
// above (m = nl, the art's own facing divided out); 1 every painted mesh by the traced sun's facing (a side away from
// the sun keeps A O of A + K m, a top facing it gains K (1 - m)). The ambient by kAmbientLook (D4: a look dial).
// O a channel at a time (FB 22.10, plan 11): the sky and bounce light's ratio (picture view_volume[2].z - 1, rt_indirect_up)
// scaled to the light's O (the ratio itself where O is its mean; 0 with O), for an opaque draw of the scene view where
// that pass ran; else O as it is. `coloured` says which.
float3 traced_o(float4 light, float4 pixel, out bool coloured) {
    coloured = false;
    if (view_volume[2].z == 0.0 || (draw_flags & 8u) != 0u || traced_by_air(pixel)) {
        return light.aaa;
    }
    const float3 ratio = gTextures[uint(view_volume[2].z) - 1u].Load(int3(pixel.xy, 0)).rgb;
    const float mean = (ratio.r + ratio.g + ratio.b) * (1.0 / 3.0);
    coloured = mean > 1e-4;
    return coloured ? ratio * (light.a / mean) : light.aaa;
}
float3 traced_painted(float4 light, float nl, float3 o3, bool coloured) {
    const float3 v = light.rgb, a = view_stage_a.rgb, k = view_stage_k.rgb;
    const float o = light.a;
    const float m = lerp(nl, view_look.y, view_look.x);
    const float ga = view_look.w;
    const float3 held = a + k * m;
    const float3 channel = float3(held.r > 1e-4 ? (ga * a.r * o3.r + k.r * nl * v.r) / held.r : 1.0,
                                  held.g > 1e-4 ? (ga * a.g * o3.g + k.g * nl * v.g) / held.g : 1.0,
                                  held.b > 1e-4 ? (ga * a.b * o3.b + k.b * nl * v.b) / held.b : 1.0);
    const float3 luminance = float3(0.2126, 0.7152, 0.0722);
    const float al = dot(a, luminance), kl = dot(k, luminance);
    const float ao = coloured ? dot(a * o3, luminance) : al * o;
    const float held_l = al + kl * m;
    const float mean = held_l > 1e-4 ? (ga * ao + kl * nl * dot(v, luminance)) / held_l : 1.0;
    return lerp(float3(mean, mean, mean), channel, view_stage_a.w);
}
float4 traced_colour(float4 c, float4 pixel) {
    if ((view_flags & 8u) == 0u) {
        return c;
    }
    const float4 light = gTextures[uint(view_traced.x)].Load(int3(pixel.xy, 0));
    const float3 added = gTextures[uint(view_traced.y)].Load(int3(pixel.xy, 0)).rgb;
    if ((view_flags & 16u) != 0u) {
        return float4(float3(light.r, light.a, light.b) * 0.7 + added, c.a);
    }
    // A lit program took its key by V and its fill and ambient by O inside its light sums: nothing more here.
    const float nl = g_traced_lit ? 1.0 : saturate(gTextures[uint(view_traced.w)].Load(int3(pixel.xy, 0)).r);
    bool coloured = false;
    const float3 o3 = g_traced_lit ? light.aaa : traced_o(light, pixel, coloured);
    const float3 share = g_traced_lit ? float3(1.0, 1.0, 1.0) : traced_painted(light, nl, o3, !g_traced_lit && coloured);
    if ((view_flags & 64u) != 0u) {
        // The share alone (--diagnostics rt-signals, S; a lit program's is 1), as x / (1 + x): the scene target is unorm.
        return float4(share / (1.0 + share), c.a);
    }
    // The added light by the surface's colour before lighting (a painted program's colour is that already).
    const float kBaseFloor = view_dev[1].x > 0.5 ? view_dev[0].x : 0.2;  // rt_dev.h base_floor
    const float3 base = g_traced_lit ? c.rgb / max(g_traced_level, kBaseFloor) : c.rgb;
    return traced_mirror(c.rgb * share + base * added, c.a, pixel);
}
// The light in the air (rt/rt_volume.hlsl, the master plan's 15.2), for a draw that is lit and not a light (draw flag
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
    const float4 air = gVolumes[uint(view_volume[2].w) - 1u].SampleLevel(gSamplers[uint(view_volume[2].y)], inner, 0.0);
    return float4(c.rgb * air.a + air.rgb, c.a);  // what the air lets through of the colour, and its own light (FB)
}
#if NATIVE_PREPASS
float3 program_fog(float3 c, float3 colour, float amount) { return lerp(c, colour, amount); }
float4 program_fogged(float4 c) { return c; }
#else
// The game's fog over the traced light (15.3; FB): a program's fog step (the generated fog line, native_fog) keeps its
// colour and amount, and PROGRAM_LIGHT puts the light on the surface's colour first and the fog over it.
static float3 g_fog_colour = float3(0.0, 0.0, 0.0);
static float g_fog_amount = 0.0;
float3 program_fog(float3 c, float3 colour, float amount) {
    g_fog_colour = colour;
    g_fog_amount = amount;
    return c;
}
float4 program_fogged(float4 c) { return g_fog_amount > 0.0 ? float4(lerp(c.rgb, g_fog_colour, g_fog_amount), c.a) : c; }
#endif
// A program returns inside its entry, whose input `i` holds the pixel. The light on the surface, the game's fog, the
// light in the air between the eye and the surface.
#define PROGRAM_LIGHT(c) traced_scatter(program_fogged(traced_by_air(i.pos) ? traced_air(c, i.pos) : traced_colour(c, i.pos)), i.pos)
#else
// Fog at once (the same math as the fog step had in place).
float3 program_fog(float3 c, float3 colour, float amount) { return lerp(c, colour, amount); }
#define PROGRAM_LIGHT(c) (c)
#endif

#ifndef ENV_FETCH
#define ENV_FETCH(t) (t)
#endif
#ifndef TRACED_LEVEL
#define TRACED_LEVEL(x) (x)
#endif

#if NATIVE_PREPASS && NATIVE_PREPASS_ITEM
// A Vulkan device without geometry shaders gives a pixel stage no primitive number (D11): the item only, and the
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
