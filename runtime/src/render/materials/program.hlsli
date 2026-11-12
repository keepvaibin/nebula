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
    float4 view_look;          // with bit 3: the painted composite's look (traced_painted): x kForm, y the sun's
                               // height over the player's up, at least 0.5 (the relight's exposure, M3), z kDebake,
                               // w kAmbientLook
    float4 view_dev[2];        // with bit 3: the development tunables (rt_dev.h from base_floor): [0] the values, [1] 1
                               // where the run gave one; x base_floor, y sun_strength, z sky_chroma, w stage_light (traced_painted)
    float4 view_relight[4];    // with bit 3: the relight's view constants (traced_painted; A3-F18, the host's from the
                               // rows above): [0] rgb lin(A), w luma(held); [1] rgb sky' E; [2] rgb key sun_strength E;
                               // [3] rgb held / luma(held); [1..3].w the sun's direction in the eye's space,
                               // the scene's view under the relight, else 0 (program_lit.hlsli traced_key_dir, c52)
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

// The painted light taken out of a vertex colour (D2; model_write.rs debake_gains, v2 c79): the cooker's gain a channel,
// signed bytes of log2 x 42 (a mesh without gains reads 0: 1), by kDebake (view_look.z: 1 under the relight, else 0);
// over 1, bounded by its largest channel (its hue kept).
float3 traced_vertex_gain(float3 c, uint gain) {
    if (!(view_look.z > 0.0)) {
        return c;
    }
    const int3 steps = int3(gain << 24u, gain << 16u, gain << 8u) >> 24;
    const float3 g = max(c, 0.0) * lerp(float3(1.0, 1.0, 1.0), exp2(float3(steps) / 42.0), view_look.z);
    return g / max(max(max(g.r, g.g), g.b), 1.0);
}

// c93 (3.3): the same gain as a multiplier, for a mesh painted by its map alone (programs.rs map_gained): its traced
// vertex forms pass it, its traced pixel forms take what they show through traced_map_shown.
float3 traced_map_gain(uint gain) {
    if (!(view_look.z > 0.0)) {
        return float3(1.0, 1.0, 1.0);
    }
    const int3 steps = int3(gain << 24u, gain << 16u, gain << 8u) >> 24;
    return lerp(float3(1.0, 1.0, 1.0), exp2(float3(steps) / 42.0), view_look.z);
}
// c93: what a map-painted pixel shows, de-lit: bounded by its largest channel as traced_vertex_gain. A zero multiplier
// (a vertex form that did not pass it) leaves it.
float3 traced_map_shown(float3 c, float3 gain) {
    if (!(view_look.z > 0.0) || !(gain.r + gain.g + gain.b > 0.0)) {
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
// The pixel's traced light (the light pass's): rgb V, the sun's share a channel; a O, the sky's share. All of every
// light when the view carries none (view_flags bit 3). A lit program sets g_traced_lit (program_lit.hlsli).
static bool g_traced_lit = false;
// A lit program's channel 0 light level (the generator's TRACED_LEVEL), so the added light takes its colour before
// lighting: c / level.
static float3 g_traced_level = float3(1.0, 1.0, 1.0);
// c74 (A2): a lit matte dielectric's own reflection (program_lit.hlsli traced_sheen): rgb the light it mirrors on a
// white surface, a 1 - F (what its colour keeps).
static float4 g_traced_sheen = float4(0.0, 0.0, 0.0, 1.0);
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
    const float4 d = view_volume[0];
    const float away = d.y / ((pixel.z - d.z) / max(d.w - d.z, 1e-7) + d.x);
    return abs(away - guide.r) <= guide.r * 0.002 + 1.0;
}
// sRGB's transfer: a program's colour is display-encoded; the composite's products and sums are on linear light
// (DIRECTOR.md M1) and the result is encoded again. Past 1 both curves carry on (the finish's shoulder takes it, M7).
float3 traced_lin(float3 c) {
    c = max(c, 0.0);
    return lerp(pow((c + 0.055) / 1.055, 2.4), c / 12.92, step(c, 0.04045));
}
// c84 (V-A2 step 2): no pigment is purer than this: no channel of a non-metal's colour under kPurity of its largest,
// in linear light (the 0.002 blue of Good Egg's grass is paint, not a surface). rt_shade.hlsli rt_hit_albedo alike.
static const float kPurity = 0.04;
float3 traced_pure(float3 l, bool metal) {
    return metal ? l : max(l, kPurity * max(max(l.r, l.g), l.b));
}
float3 traced_srgb(float3 l) {
    l = max(l, 0.0);
    return lerp(1.055 * pow(l, 1.0 / 2.4) - 0.055, l * 12.92, step(l, 0.0031308));
}
float4 traced_at(float4 pixel) {
    if ((view_flags & 8u) == 0u || (draw_flags & 64u) != 0u || traced_by_air(pixel)) {
        // The volume's share is all of its light (PROGRAM_LIGHT); a draw shown as it is (flag 64: the sky, a glow)
        // takes all of every light: a lit program's sums are its own.
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if ((draw_flags & 128u) != 0u) {
        return float4(1.0, 1.0, 1.0, 1.0);  // a metal (A3-F4): its light is its mirror's; a lit metal's sums unshadowed
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
    // R is linear (the tracer's pictures); the program's stages are display-encoded: R encoded here, once.
    return mirror.a < -0.5 ? float4(traced_srgb(mirror.rgb) + painted.rgb * (-mirror.a - 1.0), painted.a) : painted;
}
// The program's fetch of a painted environment map (the generator wraps it); `i` holds the pixel.
#define ENV_FETCH(t) env_fetch(t, i.pos)
// The light under a painted surface (LIGHT-MODEL.md section 3): its art holds the stage's light, ambient A and key K by
// how it faces the key (nl), in linear light lin(A + K nl); the traced light keeps the key's part by V and the
// ambient's by O, a channel at a time (a shadow takes the sky's colour, and a coloured thing's shadow its colour)
// and in luminance, mixed by kShadowTint: (lin(A) O + (lin(A + K nl) - lin(A)) V) / lin(A + K nl), affine in O and
// V. In open sun with nothing near: 1.
// The relight (DIRECTOR.md section 12, October's engine v2): the stage's light in linear units from the game's own
// lights (M2: sky = lin(A), full = lin(A + K), sun = (full - sky) kSunStrength), exposed so that a surface facing the
// player's up in open sun shows the full light (M3: E = luma(full) / (luma(sky) + luma(full - sky) max(up.s, 0.5))):
// lit = (sky' O + sun n.s V) E, sky' the sky at kSkyChroma of its chroma (M8) by kAmbientLook (D4); the share is lit
// over the full light in luminance, its colour over the full light's within a stop either way (A3-F8): a channel of
// the stage's light near 0 does not blow up. kForm blends from the Painted form above to it.
// O a channel at a time (FB 22.10, plan 11): the sky and bounce light's ratio (picture view_volume[2].z - 1, rt_indirect_up)
// scaled to the light's O (the ratio itself where O is its mean; 0 with O), for an opaque draw of the scene view where
// that pass ran; else O as it is. `coloured` says which.
float3 traced_o(float4 light, float4 pixel, out bool coloured) {
    coloured = false;
    // Not draw flag 8: that is the air in front (traced_scatter), set on every opaque draw at High (FB b104: it took
    // O's hue from every surface there). Flag 64: a draw shown as it is (c42).
    if (view_volume[2].z == 0.0 || (draw_flags & 64u) != 0u || traced_by_air(pixel)) {
        return light.aaa;
    }
    const float3 ratio = gTextures[uint(view_volume[2].z) - 1u].Load(int3(pixel.xy, 0)).rgb;
    const float mean = (ratio.r + ratio.g + ratio.b) * (1.0 / 3.0);
    coloured = mean > 1e-4;
    return coloured ? ratio * (light.a / mean) : light.aaa;
}
// The sky's chroma kept in shade (M8): the run's sky_chroma where it gives one, else October's 0.4 (the host's sky' in
// view_relight[1], rt_shade.hlsli kSkyChroma).
float traced_sky_chroma() { return view_dev[1].z > 0.5 ? view_dev[0].z : 0.4; }
// A light over a reference light (A3-F8; c50): their luminances' ratio, the light's colour over the reference's colour
// (`held`: the reference over its luminance) held within [0.8, 1.25] a channel, so that a channel of either near 0 does
// not blow up. 1 where the reference has no light. The one bound for the environment (traced_painted) and a lit
// program's sky light (program_lit.hlsli, c69).
// c75 (the director 11-12): the bound a channel is the caller's: the environment (0.5, 2), LIGHT-MODEL section 2's
// (the target's sky-blue shade on Good Egg's white stone is B/R 1.83 times its lit side's); a lit program's sky light,
// the characters', (0.8, 1.25) (B2: Mario in shade is not blue).
static const float kTintBoundEnvironment = 2.0;
static const float kTintBoundCharacter = 1.25;
// c92, c92b (the director 11-12): the draw's bound by its light set's kind, the art's own split: draw flag 2048 a
// character's or object's set (Player, Strong, Weak; c92's skinned body until the host passes the kind), 4096 the
// Planet set (the environment). Neither: a lit program takes the character bound (no blue Mario in shade), a painted
// one the environment's.
float traced_tint_bound(bool lit) {
    if ((draw_flags & 2048u) != 0u) {
        return kTintBoundCharacter;
    }
    if ((draw_flags & 4096u) != 0u) {
        return kTintBoundEnvironment;
    }
    return lit ? kTintBoundCharacter : kTintBoundEnvironment;
}
// c92: O as it enters shade light (the sky term of a painted surface, a lit program's ambient and fill): its luminance
// and kShadowTint (view_stage_a.w) of its own hue: the shade's hue is the art's ambient's, the bounce and the dome
// colour it a little (FB b87's hue stays inside O).
float3 traced_o_shade(float3 o) {
    const float l = dot(o, float3(0.2126, 0.7152, 0.0722));
    return l > 1e-6 ? l * lerp(float3(1.0, 1.0, 1.0), o / l, saturate(view_stage_a.w)) : o;
}
float3 traced_bounded(float3 lit, float held_l, float3 held, float bound) {
    const float lit_l = dot(lit, float3(0.2126, 0.7152, 0.0722));
    const float3 tint = lit_l > 1e-6 && held_l > 1e-4 ? (lit / lit_l) / max(held, 1e-4) : float3(1.0, 1.0, 1.0);
    return held_l > 1e-4 ? (lit_l / held_l) * clamp(tint, 1.0 / bound, bound) : float3(1.0, 1.0, 1.0);
}
float3 traced_painted(float4 light, float nl, float3 o3, bool coloured) {
    const float3 v = light.rgb, a = view_stage_a.rgb, k = view_stage_k.rgb;
    const float o = light.a;
    const float3 luminance = float3(0.2126, 0.7152, 0.0722);
    // Painted (section 3): the art's light lin(A + K nl) divided out, the traced light's put back, in linear light.
    const float3 sk = view_relight[0].rgb, whole = traced_lin(saturate(a + k * nl));  // lin(A): the host's (F18)
    const float3 keyed = max(whole - sk, 0.0);
    const float3 channel = float3(whole.r > 1e-4 ? (sk.r * o3.r + keyed.r * v.r) / whole.r : 1.0,
                                  whole.g > 1e-4 ? (sk.g * o3.g + keyed.g * v.g) / whole.g : 1.0,
                                  whole.b > 1e-4 ? (sk.b * o3.b + keyed.b * v.b) / whole.b : 1.0);
    const float al = dot(sk, luminance), kl = dot(keyed, luminance);
    const float ao = coloured ? dot(sk * o3, luminance) : al * o;
    const float mean = al + kl > 1e-4 ? (ao + dot(keyed * v, luminance)) / (al + kl) : 1.0;
    const float3 painted = lerp(float3(mean, mean, mean), channel, view_stage_a.w);
    if (view_look.x <= 0.0) {
        return painted;
    }
    // The relight (M2, M3, M8): rt_dev.h sun_strength and sky_chroma over 1.0 (D3) and October's 0.4; stage_light 1
    // October's own (the stage's light as it is: a dim or coloured stage dims and tints the art), 0 the art's own
    // brightness kept (divided by the full light). The view's part is the host's (F18, view_relight): lit =
    // (sky' O + key sun_strength nl V) E, held = lerp(full, 1, stage_light).
    // c92 (the director 11-12; c73 withdrawn): sky' a channel at a time, the authored ambient's hue (M2, at kSkyChroma),
    // by O's luminance and kShadowTint of its hue.
    const float3 lit = view_relight[1].rgb * traced_o_shade(o3) + view_relight[2].rgb * (nl * v);
    // A3-F8: lit over held in luminance, its colour over held's within the draw's bound (c92: the environment's or a
    // character's).
    const float3 relit = traced_bounded(lit, view_relight[0].w, view_relight[3].rgb, traced_tint_bound(false));
    return lerp(painted, relit, view_look.x);
}
// c74: the sheen where the pixel mirrors nothing else (its mirror picture a in [0.999, 1.5]: no class's own F), not a
// metal, under the relight, by kForm; else none (rgb 0, a 1).
float4 traced_sheen_here(float4 pixel, bool metal) {
    if (!g_traced_lit || metal || !(view_look.x > 0.0) || !traced_mirror_own(pixel)) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float a = gTextures[uint(view_traced.z)].Load(int3(pixel.xy, 0)).a;
    if (a < 0.999 || a > 1.5) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float k = saturate(view_look.x);
    return float4(g_traced_sheen.rgb * k, lerp(1.0, g_traced_sheen.a, k));
}
// Set where traced_colour gave an exchange's premultiplied mirror (FB b94): the fog over it in the same form.
static bool g_exchanged = false;
// An exchange's draw where it shows no mirror (FB b94): the game's added colour, alpha 0 (premultiplied: an add).
float4 traced_added(float4 c) {
    return float4((draw_flags & 512u) != 0u ? c.rgb * c.a : c.rgb, 0.0);
}
// A material that keeps none of its art's tint (draw flag 1024: a fall, a splash; FB b95, V-D4): its colour's
// luminance alone, in linear light; the light's own colour still goes on it.
float4 traced_untinted(float4 c) {
    if ((draw_flags & 1024u) == 0u) {
        return c;
    }
    const float l = dot(traced_lin(c.rgb), float3(0.2126, 0.7152, 0.0722));
    return float4(traced_srgb(float3(l, l, l)), c.a);
}
float4 traced_colour(float4 c, float4 pixel) {
    if ((view_flags & 8u) == 0u) {
        return (draw_flags & 256u) != 0u ? traced_added(c) : c;
    }
    c = traced_untinted(c);
    const float4 light = gTextures[uint(view_traced.x)].Load(int3(pixel.xy, 0));
    const float3 added = gTextures[uint(view_traced.y)].Load(int3(pixel.xy, 0)).rgb;
    if ((view_flags & 16u) != 0u) {
        return float4(float3(light.r, light.a, light.b) * 0.7 + added, c.a);
    }
    // An added clear shell (draw flag 256; FB b94, EVERYTHING 6.7), drawn premultiplied: where the mirror picture is
    // its glass (a: 4 + F, rgb F R), F R and alpha F, so what is behind keeps 1 - F; elsewhere the game's added colour
    // (by its alpha where the game's add takes it: flag 512) with alpha 0, an add as before.
    if ((draw_flags & 256u) != 0u) {
        if (traced_mirror_own(pixel)) {
            const float4 mirror = gTextures[uint(view_traced.z)].Load(int3(pixel.xy, 0));
            if (mirror.a > 3.5) {
                g_exchanged = true;
                return float4(traced_srgb(mirror.rgb), saturate(mirror.a - 4.0));
            }
        }
        return traced_added(c);
    }
    // A lit program took its key by V and its fill and ambient by O inside its light sums: nothing more here.
    const float nl = g_traced_lit ? 1.0 : saturate(gTextures[uint(view_traced.w)].Load(int3(pixel.xy, 0)).r);
    bool coloured = false;
    const float3 o3 = g_traced_lit ? light.aaa : traced_o(light, pixel, coloured);
    const bool metal = (draw_flags & 128u) != 0u;  // no body: share 1, no added light (A3-F4)
    const float3 share = g_traced_lit || metal ? float3(1.0, 1.0, 1.0) : traced_painted(light, nl, o3, !g_traced_lit && coloured);
    if ((view_flags & 64u) != 0u) {
        // The share alone (--diagnostics rt-signals, S; a lit program's is 1), as x / (1 + x): the scene target is unorm.
        return float4(share / (1.0 + share), c.a);
    }
    // The added light by the surface's colour before lighting (a painted program's colour is that already).
    const float kBaseFloor = view_dev[1].x > 0.5 ? view_dev[0].x : 0.2;  // rt_dev.h base_floor
    const float4 sheen = traced_sheen_here(pixel, metal);  // c74 (A2)
    // An albedo: at most 1 a channel (A3-F7: a highlight in c over a small level is no colour).
    const float3 base = g_traced_lit ? min(c.rgb / max(g_traced_level, kBaseFloor), 1.0) : c.rgb;
    // In linear light (M1): the colour and its base decoded, the share, the added light and the mirror's exchange on
    // them (the tracer's pictures are linear), the result encoded. What the local lights add, rho x D, is at most
    // kAddedMost a channel in linear light (plan s5's guard on the product, A2-05; counted by the tracer, l82).
    const float kAddedMost = 1.5;
    const float3 lamps = metal ? float3(0.0, 0.0, 0.0) : min(traced_pure(traced_lin(base), metal) * added, kAddedMost);
    const float4 seen = traced_mirror((traced_pure(traced_lin(c.rgb), metal) * share * sheen.a + sheen.rgb) + lamps, c.a, pixel);
    // c81 (L4): a glowing body (draw flag 256: its mesh a light this frame) is never duller than its own colour unlit.
    const float3 shown = (draw_flags & 256u) != 0u ? max(seen.rgb, traced_lin(base)) : seen.rgb;
    return float4(traced_srgb(shown), seen.a);
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
    c = traced_untinted(c);  // FB b95
    const float4 d = view_volume[0];
    const float away = d.y / ((pixel.z - d.z) / max(d.w - d.z, 1e-7) + d.x);
    const float3 cells = float3(96.0, 54.0, 48.0);
    const float3 at = float3((pixel.xy - view_volume[1].xy) / view_volume[1].zw, log(max(away, 1.0) / 50.0) / log(400.0));
    const float3 inner = clamp(at, 0.5 / cells, 1.0 - 0.5 / cells);
    const float4 air = gVolumes[uint(view_volume[2].x) - 1u].SampleLevel(gSamplers[uint(view_volume[2].y)], inner, 0.0);
    return float4(traced_srgb(traced_lin(c.rgb) * air.rgb), c.a);  // in linear light (M1)
}
// The light the air between the eye and the pixel scatters toward the eye (rt/rt_scatter.hlsl, 15.4; High, a stage
// with air), added, for a draw that covers or alpha-blends (draw flag 8): its picture's place + 1 in view_volume[2].w.
// One medium (DIRECTOR.md, 11-11): a draw with a fog step of the game's (program_fog ran) has the game's fog for its
// air, and takes none of the traced (FB b56).
static bool g_fog_step = false;
static bool g_fog_live = false;  // and it fogs: an amount here, or a curve whose end lies past its start (A5-A4; FB b79)
float4 traced_scatter(float4 c, float4 pixel) {
    const bool alone = (view_flags & 128u) != 0u;  // signal A: the air's own light alone, x / (1 + x) (FA l65)
    if ((view_flags & 256u) != 0u) {
        return c;  // signal F (program_fogged_at) as it is (FB b68)
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
    // What the air lets through of the colour, and its own light (FB), in linear light (M1).
    return float4(traced_srgb(traced_lin(c.rgb) * air.a + air.rgb), c.a);
}
#if NATIVE_PREPASS
float3 program_fog(float3 c, float3 colour, float amount) { return lerp(c, colour, amount); }
float3 program_fog_curve(float3 c, float3 colour, float amount, float4 curve) { return lerp(c, colour, amount); }
float4 program_fogged(float4 c) { return c; }
float4 program_fogged_at(float4 c, float4 pixel) { return c; }
#else
// The game's fog over the traced light (15.3; FB): a program's fog step (the generated fog line, native_fog) keeps its
// colour and amount, and PROGRAM_LIGHT puts the light on the surface's colour first and the fog over it.
static float3 g_fog_colour = float3(0.0, 0.0, 0.0);
static float g_fog_amount = 0.0;
// The fog step with its curve (the cooker's fog line, native_fog): x its start, y its end, z the pixel's eye depth, w
// its type (GX: 2 linear, 4 exp, 5 exp2, 6 reverse exp, 7 reverse exp2; bit 3 orthographic); 0 none known.
static float4 g_fog_curve = float4(0.0, 0.0, 0.0, 0.0);
float3 program_fog(float3 c, float3 colour, float amount) {
    g_fog_colour = colour;
    g_fog_amount = amount;
    g_fog_step = true;
    // A fog line whose fog is off (amount 0, start = end) leaves the air to the tracer (A5-A4; FB b79).
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
// What of the fog is lit (DIRECTOR.md, one medium; FB b57): the light volume's share (view_volume[2].x) over the fog's
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
float4 program_fogged_at(float4 c, float4 pixel) {
    if ((view_flags & 256u) != 0u) {
        // Signal F (--diagnostics rt-signals; FB b68): the fog's lit share alone, x / (1 + x); 0 with no fog of the game's.
        const float3 f = g_fog_amount > 0.0 ? fog_share(pixel) : float3(0.0, 0.0, 0.0);
        return float4(f / (1.0 + f), c.a);
    }
    // The share is linear light (FB b64), the fog's colour a display value: lit in linear light (M1; c39f).
    // An exchange's colour is premultiplied (FB b94): the fog over it by its alpha.
    const float3 fog = traced_srgb(traced_lin(g_fog_colour) * fog_share(pixel)) * (g_exchanged ? c.a : 1.0);
    return g_fog_amount > 0.0 ? float4(lerp(c.rgb, fog, g_fog_amount), c.a) : c;
}
#endif
// A program returns inside its entry, whose input `i` holds the pixel. The light on the surface, the game's fog, the
// light in the air between the eye and the surface.
// A draw shown as it is (draw flag 64: the sky and what is drawn with it, a glowing surface; D2): its colour and the
// game's fog as authored, and the air in front where the host asks (flag 8: never on the sky).
#define PROGRAM_LIGHT(c) ((draw_flags & 64u) != 0u ? traced_scatter(program_fogged(c), i.pos) \
                                                   : traced_scatter(program_fogged_at(traced_by_air(i.pos) ? traced_air(c, i.pos) : traced_colour(c, i.pos), i.pos), i.pos))
#else
// Fog at once (the same math as the fog step had in place).
float3 program_fog(float3 c, float3 colour, float amount) { return lerp(c, colour, amount); }
float3 program_fog_curve(float3 c, float3 colour, float amount, float4 curve) { return lerp(c, colour, amount); }
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
