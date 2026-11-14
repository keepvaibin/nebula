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
static float4 g_traced = float4(1.0, 1.0, 1.0, 1.0);
float3 traced_light(uint n) {
    const float lamp = (view_flags & 32u) != 0u ? 1.0 : 0.0;
    return n == 0u ? g_traced.rgb : n == 1u ? g_traced.aaa : n == 4u ? float3(lamp, lamp, lamp) : float3(1.0, 1.0, 1.0);
}
#define AMBIENT_TERM(x) ((x) * g_traced.a)
#else
#define AMBIENT_TERM(x) (x)
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

// A light's highlight. `radiance` is its colour in linear light: the diffuse term is the colour times N.L, so the colour
// is the light's irradiance over pi and the lobe takes kEnhSheen = pi.
void enh_highlight(inout EnhShade e, float3 ldir, float3 radiance, bool metal) {
    const float NoL = saturate(dot(e.Nv, ldir));
    if (NoL <= 0.0) {
        return;
    }
    const float rough = metal ? kEnhMetalRough : kEnhSheenRough;
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
        e.spec += radiance * (lobe * kEnhSheen * (0.04 + 0.96 * fw));
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
    return 1.0 - reflected;
}

// One light of the lit channel; the first one's highlight.
void enh_add_light(uint base, uint n, bool spot, uint diffuse, bool first, bool metal, float3 eye, float3 amb, float3 amb_lin,
                   inout float3 acc) {
    float3 ldir;
    float attn;
    float3 lcol;
    enh_light(base, n, spot, eye, ldir, attn, lcol);
    const float facing = diffuse == 1u ? dot(g_enh.N, ldir) : diffuse == 2u ? max(dot(g_enh.N, ldir), 0.0) : 1.0;
#if NATIVE_TRACED
    lcol *= traced_light(n);
#endif
    const float x = attn * facing;
    acc += lerp(enh_lin(amb + lcol * x) - amb_lin, (enh_lin(amb + lcol) - amb_lin) * x, kEnhRamp);
    if (first && diffuse != 0u && g_enh.valid) {
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

// Ray tracing's forms keep the full call (their compiled bytes stay as they were).
#if NATIVE_TRACED || NATIVE_PREPASS
#define enh_finish_bare(frag) enh_finish(frag, g_enh)
#else
// enh_finish of a program that adds no highlight (its spec and metal stay zero, so lit is the colour's own linear value):
// the wide target takes the colour itself, enh_srgb(enh_lin(x)) being x to pow's rounding.
float3 enh_finish_bare(float3 frag) {
    const float3 x = saturate(frag);
    [branch] if ((view_flags & 4u) != 0u) {
        return x;
    }
    return enh_srgb(enh_shoulder(enh_lin(x)));
}
#endif

float4 enh_env(float4 t) {
    return enh_on() ? float4(saturate(t.rgb * g_enh.env_gain), t.a) : t;
}

#if NATIVE_TRACED
// A lit program begins with enh_begin(i.lit_eye, i.lit_nrm) in its entry, whose input `i` holds the pixel (i.pos): the
// traced form reads the pixel's light there, and every light_term after it is taken by its share.
EnhShade enh_begin_traced(float3 P, float3 n, float4 pixel) {
    g_traced = traced_at(pixel);
    g_traced_lit = true;
    return enh_begin(P, n);
}
#define enh_begin(P, n) enh_begin_traced(P, n, i.pos)
#define light_term(base, n, atten, diffuse, pos, nrm) (light_term(base, n, atten, diffuse, pos, nrm) * float4(traced_light(n), 1.0))
#endif

#endif  // PROGRAM_LIT_HLSLI
