// SPDX-License-Identifier: GPL-3.0-only
// The game's lights at the pixel, for a program's lit form. Per pixel: the light formulas of the vertex stage on the
// interpolated position and normal. Enhanced (view_flags bit 1): the lit colour channel adds its lights in linear light,
// each along its authored falloff, under a soft shoulder; the ambient is a sky hemisphere about the player's up whose
// reflection takes its share from the diffuse light; the first light's highlight is GGX, its lobe widened by the normal's
// change across the pixel (Tokuyoshi and Kaplanyan 2019); an environment map gains Fresnel. Highlights are added after the
// stages.

#ifndef PROGRAM_LIT_HLSLI
#define PROGRAM_LIT_HLSLI

static const float kEnhRamp = 0.0;           // a light's falloff: 0 as authored (gamma space), 1 linear Lambert
static const float kEnhHemi = 0.30;          // ambient: 1 + hemi * N.up, mean 1 over the sphere
static const float kEnhSky = 1.0;            // the sky's reflection: 1 physical (F0 0.04 at the surface's roughness)
static const float kEnhKnee = 0.80;          // linear level where the shoulder starts
static const float kEnhSheen = 3.14159265;   // dielectric highlight: pi, a light's colour being irradiance / pi
static const float kEnhSheenRough = 0.45;
static const float kEnhMetal = 3.14159265;   // metal highlight (F0 the surface colour)
static const float kEnhMetalRough = 0.35;
static const float kEnhMetalF0 = 0.50;       // least peak of a metal's F0 (its colour raised in its own hue)
static const float kEnhEnvBase = 1.0;        // environment lookup gain facing the viewer
static const float kEnhEnvGraze = 0.80;      // gain added at grazing angles, Schlick (1 - N.V)^5

bool enh_on() { return (view_flags & 2u) != 0u; }

#if NATIVE_TRACED
// The pixel's traced light (program.hlsli traced_at, RT-MASTER-PLAN 3.4): how much of the light set's key (light 0:
// V, a channel) and fill (light 1: O, the sky's share: the fill is the artists' bounce light) reaches it, and none
// of its point light (light 4: an emitter, in the added light; all of it when view_flags bit 32 says the local lights
// are off); all of lights 2 and 3 (alpha tricks). Its ambient by O.
// A program whose colour sums no key (no lit colour channel it reads has light 0: the colour is the vertex colour with
// only the alpha lit, or fill alone) begins with TRACED_UNKEYED (cook/programs.rs; D1): its sums take nothing traced
// but the lamps' removal, and traced_colour gives its colour the Painted share as an unlit program's.
static float4 g_traced = float4(1.0, 1.0, 1.0, 1.0);
static float3 g_traced_o = float3(1.0, 1.0, 1.0);  // O a channel at a time (program.hlsli traced_o; FB 22.10)
static bool g_traced_keyed = true;
#define TRACED_UNKEYED g_traced_keyed = false
// c76 (2.9): the plain form's channel 0 light set in parts, as its sum is made (ambient_term, traced_light_term) and
// taken by traced_set_level: the ambient (0 to 1, before O), the fill as it falls (by O), the key and the other lights
// as traced, and the key and fill without their facing (the set's full light), in the sum's units (255 full).
static bool g_set_seen = false;
static float3 g_set_amb = float3(0.0, 0.0, 0.0);
static float3 g_set_shade = float3(0.0, 0.0, 0.0);
static float3 g_set_rest = float3(0.0, 0.0, 0.0);
static float3 g_set_full = float3(0.0, 0.0, 0.0);
static float g_set_reflect = 1.0;  // the enhanced form's diffuse share after the sky's reflection (enh_sky_reflection)
// c78 (7.6): a face part's kind (TRACED_FACE, written by cook/programs.rs c77 first in its lit program): 2 an eye, 1
// another face part, 0 none. An eye's light never under kEyeFloor of its set's full light (traced_set_level); a face
// part's shadow at most half deep (enh_begin_traced).
static uint g_traced_face = 0u;
static const float kEyeFloor = 0.6;
#define TRACED_FACE(k) g_traced_face = (k)
// The traced sun as light 0 (c52; the director 11-11): the pixel's traced facing max(n . s, 0) (the Facing picture's
// r), -1 where the pixel takes the light set's (no relight, a draw traced_at gives all of every light).
static float g_traced_facing = -1.0;
float traced_sun_strength() { return view_dev[1].y > 0.5 ? view_dev[0].y : 1.0; }
// Light 1's diffuse factor under the relight (c53): the sky's hemisphere about the player's up, by kForm from the
// game's (a fill in the camera's space on most light sets).
// c69 (the director 23:05): at half the hemisphere's 0.5 + 0.5 n.up: a directional light's facing max(n.l, 0) averages
// 1/4 over a sphere of normals, the hemisphere 1/2, so the game's fill keeps its energy over all normals (c53 gave it
// twice, flattening every character).
float traced_sky_facing(float3 nrm) {
    return 0.5 * saturate(0.5 + 0.5 * dot(nrm * rsqrt(max(dot(nrm, nrm), 1e-12)), view_sky_up));
}
float traced_fill_facing(uint n, uint diffuse, float facing, float3 nrm) {
    if (n != 1u || diffuse == 0u || !g_traced_keyed || g_traced_facing < 0.0 || (view_flags & 1u) == 0u) {
        return facing;
    }
    return lerp(facing, traced_sky_facing(nrm), saturate(view_look.x));
}
// Light 0's diffuse factor under the relight: the traced facing by kSun, by kForm from the game's.
float traced_key_facing(uint n, uint diffuse, float facing) {
    if (n != 0u || diffuse == 0u || !g_traced_keyed || g_traced_facing < 0.0) {
        return facing;
    }
    return lerp(facing, g_traced_facing * traced_sun_strength(), saturate(view_look.x));
}
// The highlight of light 0, or of light 1 where it comes first, under the relight: the sun's in the eye's space
// (view_relight[1..3].w), where known (the director 11-11: no light follows the camera in a traced frame).
float3 traced_key_dir(uint n, float3 ldir) {
    const float3 sun = float3(view_relight[1].w, view_relight[2].w, view_relight[3].w);
    return n <= 1u && g_traced_keyed && g_traced_facing >= 0.0 && view_look.x >= 0.5 && dot(sun, sun) > 0.5 ? sun : ldir;
}
float3 traced_light(uint n) {
    const float lamp = (view_flags & 32u) != 0u ? 1.0 : 0.0;
    if (n == 4u) {
        return float3(lamp, lamp, lamp);
    }
    if (!g_traced_keyed) {
        return float3(1.0, 1.0, 1.0);
    }
    return n == 0u ? g_traced.rgb : n == 1u ? g_traced_o : float3(1.0, 1.0, 1.0);
}
// The ambient by O: a colour's a channel at a time, an alpha's by O itself (a program that sums no key: as the game's).
float ambient_term(float x) { return g_traced_keyed ? x * g_traced.a : x; }
float3 ambient_term(float3 x) {
    g_set_amb = x;  // c76
    g_set_seen = true;
    return g_traced_keyed ? x * g_traced_o : x;
}
float4 ambient_term(float4 x) { return g_traced_keyed ? float4(x.rgb * g_traced_o, x.a * g_traced.a) : x; }
#define AMBIENT_TERM(x) ambient_term(x)
#else
#define AMBIENT_TERM(x) (x)
#define TRACED_UNKEYED
#define TRACED_FACE(k)
#endif

float3 enh_lin(float3 c) { return pow(max(c, 0.0), 2.2); }
float3 enh_srgb(float3 c) { return pow(max(c, 0.0), 1.0 / 2.2); }

// Identity up to the knee, then an exponential shoulder towards 1.
float3 enh_shoulder(float3 x) {
    const float3 over = kEnhKnee + (1.0 - kEnhKnee) * (1.0 - exp((kEnhKnee - x) / (1.0 - kEnhKnee)));
    return lerp(x, over, step(kEnhKnee, x));
}

float enh_ggx(float NoH, float a2) {
    const float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}

float enh_vis(float NoL, float NoV, float a) {
    return 0.5 / max(NoL * (NoV * (1.0 - a) + a) + NoV * (NoL * (1.0 - a) + a), 1e-4);
}

// Light n at P: direction towards it, the spot attenuation or none, colour. The set at `base` begins with its header
// row (program.hlsli light_row), and a light number past its count lights nothing, as light_term has it.
void enh_light(uint base, uint n, bool spot, float3 P, out float3 ldir, out float attn, out float3 lcol) {
    const uint at = light_row(base, n);
    if (n >= light_count(base)) {
        ldir = float3(0.0, 0.0, 1.0);
        attn = 0.0;
        lcol = float3(0.0, 0.0, 0.0);
        return;
    }
    const float4 le1 = rows[at + 1u];
    const float4 le2 = rows[at + 2u];
    const float4 le3 = rows[at + 3u];
    const float3 ldelta = float3(le2.z, le2.w, le3.x) - P;
    const float dist2 = dot(ldelta, ldelta);
    const float dist = sqrt(dist2);
    ldir = dist > 0.0 ? ldelta / dist : float3(0.0, 0.0, 1.0);
    attn = 1.0;
    if (spot) {
        const float c = min(max(0.0, dot(ldir, le3.yzw)), 1.0e18);
        const float denom = dot(float3(le1.w, le2.x, le2.y), float3(1.0, min(dist, 1.0e18), min(dist2, 1.0e36)));
        attn = denom != 0.0 ? max(0.0, le1.x + le1.y * c + le1.z * c * c) / denom : 0.0;
    }
    lcol = rows[at].rgb / 255.0;
}

// The player's up in eye space, else the view's up.
float3 enh_up() {
    return (view_flags & 1u) != 0u ? view_sky_up : float3(0.0, 1.0, 0.0);
}

struct EnhShade {
    bool valid;       // the draw has a normal
    float3 N;         // as the lights take it (two-sided surfaces keep it from behind)
    float3 Nv;        // facing the viewer: the reflections' normal
    float3 V;
    float NoV;
    float a2_filter;  // the lobes' widening for the normal's change across the pixel
    float env_gain;   // environment lookups: Fresnel gain
    float3 spec;      // dielectric highlight and sky reflection, linear, added after the stages
    float3 metal;     // metal lobe, linear, times the surface's Fresnel after the stages
    float metal_fw;   // its Schlick weight
};

static EnhShade g_enh;

EnhShade enh_begin(float3 P, float3 n) {
    EnhShade e = (EnhShade)0;
    e.valid = dot(n, n) > 0.0;
    e.N = e.valid ? normalize(n) : float3(0.0, 0.0, 0.0);
    e.V = dot(P, P) > 0.0 ? normalize(-P) : float3(0.0, 0.0, 1.0);
    e.Nv = dot(e.N, e.V) < 0.0 ? -e.N : e.N;
    e.NoV = e.valid ? min(abs(dot(e.N, e.V)), 1.0) : 1.0;
    const float g = 1.0 - e.NoV;
    e.env_gain = e.valid ? kEnhEnvBase + kEnhEnvGraze * (g * g) * (g * g) * g : 1.0;
    const float3 dx = ddx(e.N);
    const float3 dy = ddy(e.N);
    e.a2_filter = e.valid ? min(0.5 * (dot(dx, dx) + dot(dy, dy)), 0.18) : 0.0;
    return e;
}

// c58: a dielectric highlight's roughness and strength, a painted highlight sprite's under the relight (enh_sprite,
// by kForm; the generator calls it for a material with a sprite), else kEnhSheenRough and 1.
static float g_enh_sheen_rough = kEnhSheenRough;
static float g_enh_sheen_gain = 1.0;
void enh_sprite(float rough, float gain) {
    const float k = (view_flags & 8u) != 0u ? saturate(view_look.x) : 0.0;
    g_enh_sheen_rough = lerp(kEnhSheenRough, rough, k);
    g_enh_sheen_gain = lerp(1.0, gain, k);
}
// c58: a painted highlight sprite's texel: under the relight with enhanced lighting its spot is not drawn (by kForm),
// the traced sun's highlight stands for it; else as it is.
float4 highlight_fetch(float4 t) {
    const float k = enh_on() && (view_flags & 8u) != 0u ? saturate(view_look.x) : 0.0;
    return float4(t.rgb * (1.0 - k), t.a);
}
#define HIGHLIGHT_FETCH(t) highlight_fetch(t)
// A light's highlight. `radiance` is its colour in linear light: the diffuse term is the colour times N.L, so the colour
// is the light's irradiance over pi and the lobe takes kEnhSheen = pi.
void enh_highlight(inout EnhShade e, float3 ldir, float3 radiance, bool metal) {
    const float NoL = saturate(dot(e.Nv, ldir));
    if (NoL <= 0.0) {
        return;
    }
    const float rough = metal ? kEnhMetalRough : g_enh_sheen_rough;  // c58: a sprite's
    const float a2 = min(rough * rough * rough * rough + e.a2_filter, 1.0);
    const float a = sqrt(a2);
    const float3 H = normalize(ldir + e.V);
    const float NoH = saturate(dot(e.Nv, H));
    const float fw = pow(1.0 - saturate(dot(e.V, H)), 5.0);
    const float lobe = NoL * enh_ggx(NoH, a2) * enh_vis(NoL, e.NoV, a);
    if (metal) {
        e.metal = radiance * (lobe * kEnhMetal);
        e.metal_fw = fw;
    } else {
        e.spec += radiance * (lobe * kEnhSheen * g_enh_sheen_gain * (0.04 + 0.96 * fw));
    }
}

// The ambient's sky term: a hemisphere about the up direction (mean 1 over the sphere).
float enh_sky(EnhShade e) {
    return e.valid ? 1.0 + kEnhHemi * dot(e.N, enh_up()) : 1.0;
}

// The sky's reflection off a dielectric (F0 0.04) along the reflected view direction: Karis' analytic fit of the
// split-sum environment BRDF ("Physically Based Shading on Mobile", 2014). Adds it to the highlights; returns the
// diffuse light's share.
float enh_sky_reflection(inout EnhShade e, float3 sky) {
    if (!e.valid) {
        return 1.0;
    }
    const float4 r = kEnhSheenRough * float4(-1.0, -0.0275, -0.572, 0.022) + float4(1.0, 0.0425, 1.04, -0.04);
    const float a004 = min(r.x * r.x, exp2(-9.28 * e.NoV)) * r.x + r.y;
    const float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    const float reflected = saturate(0.04 * ab.x + ab.y) * kEnhSky;
    const float3 R = reflect(-e.V, e.Nv);
    e.spec += sky * (reflected * (1.0 + kEnhHemi * dot(R, enh_up())));
#if NATIVE_TRACED
    g_set_reflect = 1.0 - reflected;  // c76
#endif
    return 1.0 - reflected;
}

// One light of the lit channel; the first one's highlight.
void enh_add_light(uint base, uint n, bool spot, uint diffuse, bool first, bool metal, float3 eye, float3 amb, float3 amb_lin,
                   inout float3 acc) {
    float3 ldir;
    float attn;
    float3 lcol;
    enh_light(base, n, spot, eye, ldir, attn, lcol);
    float facing = diffuse == 1u ? dot(g_enh.N, ldir) : diffuse == 2u ? max(dot(g_enh.N, ldir), 0.0) : 1.0;
#if NATIVE_TRACED
    const float3 lcol_set = lcol;  // c76: the set's own colour
    lcol *= traced_light(n);
    facing = traced_key_facing(n, diffuse, facing);  // light 0: the traced sun (c52)
    facing = traced_fill_facing(n, diffuse, facing, g_enh.N);  // light 1: the sky's hemisphere (c53)
#endif
    const float x = attn * facing;
    const float3 added = lerp(enh_lin(amb + lcol * x) - amb_lin, (enh_lin(amb + lcol) - amb_lin) * x, kEnhRamp);
    acc += added;
#if NATIVE_TRACED
    // c76: the set's parts (traced_set_level), in linear light.
    if (n == 1u) {
        g_set_shade += added;
    } else {
        g_set_rest += added;
    }
    if (n <= 1u) {
        g_set_full += enh_lin(amb + lcol_set * attn) - amb_lin;
    }
#endif
    if (first && diffuse != 0u && g_enh.valid) {
#if NATIVE_TRACED
        ldir = traced_key_dir(n, ldir);  // the sun's (c52)
#endif
        enh_highlight(g_enh, ldir, enh_lin(lcol) * attn, metal);
    }
}

// An unlit environment-mapped draw: the highlight of light 0, the last actor key light the game loaded, when it has a
// colour.
void enh_key_highlight(uint base, inout EnhShade e, float3 P) {
    if (!e.valid) {
        return;
    }
    float3 ldir;
    float attn;
    float3 lcol;
    enh_light(base, 0u, false, P, ldir, attn, lcol);
#if NATIVE_TRACED
    lcol *= traced_light(0u);
#endif
    if (any(lcol > 0.0)) {
#if NATIVE_TRACED
        ldir = traced_key_dir(0u, ldir);  // the sun's (c52)
#endif
        enh_highlight(e, ldir, enh_lin(lcol), true);
    }
}

// The highlights added to the stages' result in linear light. A metal's F0 is its own colour raised to a peak of at
// least kEnhMetalF0, so metals baked dull still reflect their light in their own hue.
float3 enh_finish(float3 frag, EnhShade e) {
    const float3 c = enh_lin(saturate(frag));
    const float peak = max(max(c.r, c.g), max(c.b, 1e-4));
    const float3 f0 = saturate(c * (max(peak, kEnhMetalF0) / peak));
    const float3 lit = c + e.spec + e.metal * (f0 + (1.0 - f0) * e.metal_fw);
    return enh_srgb((view_flags & 4u) != 0u ? lit : enh_shoulder(lit));
}

float4 enh_env(float4 t) {
    return enh_on() ? float4(saturate(t.rgb * g_enh.env_gain), t.a) : t;
}

#if NATIVE_TRACED
// c74 (A2): a matte dielectric's own reflection under the relight, F0 0.04 at kSheenRough: F over the view's lobe
// (Karis' split sum, as enh_sky_reflection), the sky's light on a white surface here (sky' E O over the held light,
// O with the sky's hue) by F, and the traced sun's GGX lobe by its light (key kSun E over the held light) and V. In the
// eye's space (g_enh's N and V; the sun's direction view_relight[1..3].w). rgb that light, a 1 - F; none (0, 1) where
// the relight, the traced light or the normal is missing.
static const float kSheenRough = 0.7;
float4 traced_sheen(EnhShade e) {
    const float held_l = view_relight[0].w;
    if (!e.valid || !g_traced_keyed || !(view_look.x > 0.0) || (view_flags & 8u) == 0u || (view_flags & 16u) != 0u ||
        (draw_flags & 128u) != 0u || !(held_l > 1e-4)) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float4 r = kSheenRough * float4(-1.0, -0.0275, -0.572, 0.022) + float4(1.0, 0.0425, 1.04, -0.04);
    const float a004 = min(r.x * r.x, exp2(-9.28 * e.NoV)) * r.x + r.y;
    const float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    const float f = saturate(0.04 * ab.x + ab.y);
    float3 sheen = (dot(view_relight[1].rgb, float3(0.2126, 0.7152, 0.0722)) / held_l) * g_traced_o * f;
    const float3 sun = float3(view_relight[1].w, view_relight[2].w, view_relight[3].w);
    if (g_traced_facing >= 0.0 && dot(sun, sun) > 0.5) {
        const float NoL = saturate(dot(e.Nv, sun));
        if (NoL > 0.0) {
            const float a2 = min(kSheenRough * kSheenRough * kSheenRough * kSheenRough + e.a2_filter, 1.0);
            const float3 H = normalize(sun + e.V);
            const float fw = pow(1.0 - saturate(dot(e.V, H)), 5.0);
            const float lobe = NoL * enh_ggx(saturate(dot(e.Nv, H)), a2) * enh_vis(NoL, e.NoV, sqrt(a2)) * 3.14159265;
            sheen += (view_relight[2].rgb / held_l) * g_traced.rgb * (lobe * (0.04 + 0.96 * fw));
        }
    }
    return float4(sheen, 1.0 - f);
}
// A lit program begins with enh_begin(i.lit_eye, i.lit_nrm) in its entry, whose input `i` holds the pixel (i.pos): the
// traced form reads the pixel's light there, and every light_term after it is taken by its share (when its colour
// sums the key: else TRACED_UNKEYED came first, and traced_colour shares its colour as a painted one's).
EnhShade enh_begin_traced(float3 P, float3 n, float4 pixel) {
    g_traced = traced_at(pixel);
    // c78 (7.6): a face part's shadows (the cap brim's, the nose's) at most half deep: V to 0.5 + 0.5 V, by kForm.
    if (g_traced_face != 0u && view_look.x > 0.0) {
        g_traced.rgb = lerp(g_traced.rgb, 0.5 + 0.5 * g_traced.rgb, saturate(view_look.x));
    }
    g_set_seen = false;  // c76: no set parts before the first sum
    g_set_shade = float3(0.0, 0.0, 0.0);
    g_set_rest = float3(0.0, 0.0, 0.0);
    g_set_full = float3(0.0, 0.0, 0.0);
    g_set_reflect = 1.0;
    bool coloured = false;
    g_traced_o = traced_o(g_traced, pixel, coloured);
    // c69, c73: under the relight, the sky light as the environment takes it: O with the sky's hue once (FB b87: the
    // dome at kSkyChroma of its chroma), bounded against white by the environment's own bound (traced_bounded); Painted
    // keeps FB's coloured O.
    if (view_look.x > 0.0) {
        const float3 o_sky = traced_bounded(g_traced_o, 1.0, float3(1.0, 1.0, 1.0), kTintBoundCharacter);
        g_traced_o = lerp(g_traced_o, o_sky, saturate(view_look.x));
    }
    g_traced_lit = g_traced_keyed;
    // c52: the traced facing for light 0 where the relight is on and the pixel's traced light is its own.
    g_traced_facing = -1.0;
    const float3 sun_row = float3(view_relight[1].w, view_relight[2].w, view_relight[3].w);  // a traced sun, this view's
    if (view_look.x > 0.0 && dot(sun_row, sun_row) > 0.5 && (view_flags & 8u) != 0u && (view_flags & 16u) == 0u && (draw_flags & (64u | 128u)) == 0u &&
        !traced_by_air(pixel)) {
        g_traced_facing = saturate(gTextures[uint(view_traced.w)].Load(int3(pixel.xy, 0)).r);
    }
    const EnhShade e = enh_begin(P, n);
    g_traced_sheen = traced_sheen(e);  // c74 (A2)
    return e;
}
#define enh_begin(P, n) enh_begin_traced(P, n, i.pos)
// Light n by its traced share; light 0's diffuse factor the traced sun's (c52): the light set's colour and
// attenuation without its facing, by the traced facing.
float4 traced_light_term(uint base, uint n, uint atten, uint diffuse, float3 pos, float3 nrm) {
    float4 t = light_term(base, n, atten, diffuse, pos, nrm);
    if (n == 0u && atten != 1u && diffuse != 0u && g_traced_keyed && g_traced_facing >= 0.0) {
        const float4 flat = light_term(base, n, atten, 0u, pos, nrm);
        t.rgb = lerp(t.rgb, flat.rgb * (g_traced_facing * traced_sun_strength()), saturate(view_look.x));
    }
    if (n == 1u && atten != 1u && diffuse != 0u && g_traced_keyed && g_traced_facing >= 0.0 && (view_flags & 1u) != 0u) {
        // Light 1: the sky's hemisphere about the player's up in place of its facing (c53).
        const float4 flat = light_term(base, n, atten, 0u, pos, nrm);
        t.rgb = lerp(t.rgb, flat.rgb * traced_sky_facing(nrm), saturate(view_look.x));
    }
    const float4 lit = t * float4(traced_light(n), 1.0);
    // c76: the set's parts for traced_set_level.
    if (n == 1u) {
        g_set_shade += lit.rgb;
    } else {
        g_set_rest += lit.rgb;
    }
    if (n <= 1u) {
        g_set_full += light_term(base, n, atten, 0u, pos, nrm).rgb;
    }
    return lit;
}
#define light_term(base, n, atten, diffuse, pos, nrm) traced_light_term(base, n, atten, diffuse, pos, nrm)
// c76 (EVERYTHING 2.9, the director 11-12): the plain form's channel 0 light in shade (its ambient and fill by O, the
// fill by its facing) keeps the hue of the set's full light (ambient + key + fill without facing: the art's light on
// the zone) within kTintBoundCharacter a channel, its luminance kept, under the relight (by kForm); the level is then
// the rest (key, other lights) and that shade, saturated as the sum; the enhanced form's alike in linear light (its
// ambient enh_amb_lin by the sky term, its sum by the sky reflection's share under its shoulder). The parts are
// cleared here; a program without them keeps its level.
float3 traced_set_level(float3 level) {
    const bool enhanced = enh_on();
    const float scale = enhanced ? 1.0 : 255.0;  // the ambient in the sum's units
    const float3 shade = g_set_amb * scale * g_traced_o + g_set_shade;
    const float3 full = g_set_amb * scale + g_set_full;
    const float3 rest = g_set_rest;
    const float reflect_share = g_set_reflect;
    const bool seen = g_set_seen;
    g_set_seen = false;
    g_set_amb = float3(0.0, 0.0, 0.0);
    g_set_shade = float3(0.0, 0.0, 0.0);
    g_set_rest = float3(0.0, 0.0, 0.0);
    g_set_full = float3(0.0, 0.0, 0.0);
    g_set_reflect = 1.0;
    if (!seen || !g_traced_keyed || !(view_look.x > 0.0)) {
        return level;
    }
    const float3 luminance = float3(0.2126, 0.7152, 0.0722);
    const float shade_l = dot(shade, luminance);
    const float full_l = dot(full, luminance);
    float3 bounded = shade;
    if (shade_l > 1e-4 && full_l > 1e-4) {
        const float3 hue = full / full_l;
        const float3 tint = clamp((shade / shade_l) / max(hue, 1e-4), 1.0 / kTintBoundCharacter, kTintBoundCharacter);
        bounded = lerp(shade, shade_l * tint * hue, saturate(view_look.x));
    }
    float3 sum = rest + bounded;
    // c78 (7.6): THE EYE LIGHT: an eye's light never under kEyeFloor of the set's full light (its hue), by kForm.
    if (g_traced_face == 2u) {
        sum = max(sum, full * (kEyeFloor * saturate(view_look.x)));
    }
    return enhanced ? enh_srgb(enh_shoulder(max(sum * reflect_share, 0.0))) : saturate(sum / 255.0);
}
#undef TRACED_LEVEL
#define TRACED_LEVEL(x) traced_level(traced_set_level(x))
#endif

#endif  // PROGRAM_LIT_HLSLI
