// SPDX-License-Identifier: GPL-3.0-only
// The J3D materials' framework (E-04 step 3): every generated runtime/src/render/materials/j3d/m<id>.hlsl includes this.
// Each material declares the constants it reads before including this (its struct MaterialConstants, in the order of
// tools/dev/j3d_compact.py SLOTS; the game packs them by the mask its shader's table gives): always fog, fog_color, ovr
// and ovr_coord. Here: the native framework, and the vertex lighting from the draw's lights (float math).
//   reg1-3      the colour registers at the draw (0-1 scale; signed, beyond 1 possible)
//   kcolor0-3   the constant colours
//   mat_color0-1, amb_color0-1  channels 0 and 1: material and ambient colour
//   ind_mtx0-5  indirect matrix m: rows 2m and 2m + 1 (xyz), its scale applied
//   fog         start, end, near, far; fog_color: rgb, w the fog type (0 none)
//   alpha_ref   the alpha test's references 0 and 1 (0-1)
//   tex_scale0-7  xy: texture coordinate i's scale (texels per unit), zw: map i's size (texels)
//   ovr         a pass that replaces the material's colour (j3d_override): x the mode (0 none), y and z alphas A0 and A1
//               (0-1), w the alpha reference (drawn above it; -1 always)
//   ovr_coord   x: coordinate 0 replaced (j3d_vertex_override; 0 none, 1 the vertex's normal)

#include "native_material.hlsli"

#if NATIVE_PIXEL_LIT
// The game's lights at the pixel. Per pixel: the light formulas of the vertex stage on the interpolated position and
// normal. Enhanced (draw_indices.w bit 1): the lit colour channel adds its lights in linear light, each along its
// authored falloff, under a soft shoulder; the ambient is a sky hemisphere about the player's up whose reflection takes
// its share from the diffuse light; the first light's highlight is GGX, its lobe widened by the normal's change across
// the pixel (Tokuyoshi and Kaplanyan 2019); an environment map gains Fresnel. Highlights are added after the stages.
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

bool j3d_enhanced() { return (draw_indices.w & 2u) != 0u; }

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

// Light n at P: direction towards it, the spot attenuation or none, colour.
void enh_light(uint n, bool spot, float3 P, out float3 ldir, out float attn, out float3 lcol) {
    const uint lcw = asuint(native_light_row(n, 0u).w);
    const float4 le1 = native_light_row(n, 1u);
    const float4 le2 = native_light_row(n, 2u);
    const float4 le3 = native_light_row(n, 3u);
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
    lcol = float3((lcw >> 24) & 255u, (lcw >> 16) & 255u, (lcw >> 8) & 255u) / 255.0;
}

// The player's up in eye space (octahedral, x and y in snorm16 halves), else the view's up.
float3 enh_up() {
    if ((draw_indices.w & 1u) == 0u) {
        return float3(0.0, 1.0, 0.0);
    }
    const uint packed = draw_indices.z;
    const float2 f = float2(int2(int(packed << 16u), int(packed)) >> 16) / 32767.0;
    float3 n = float3(f, 1.0 - abs(f.x) - abs(f.y));
    const float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
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
void enh_add_light(uint n, bool spot, uint diffuse, bool first, bool metal, float3 eye, float3 amb, float3 amb_lin,
                   inout float3 acc) {
    float3 ldir;
    float attn;
    float3 lcol;
    enh_light(n, spot, eye, ldir, attn, lcol);
    const float facing = diffuse == 1u ? dot(g_enh.N, ldir) : diffuse == 2u ? max(dot(g_enh.N, ldir), 0.0) : 1.0;
    const float x = attn * facing;
    acc += lerp(enh_lin(amb + lcol * x) - amb_lin, (enh_lin(amb + lcol) - amb_lin) * x, kEnhRamp);
    if (first && diffuse != 0u && g_enh.valid) {
        enh_highlight(g_enh, ldir, enh_lin(lcol) * attn, metal);
    }
}

// An unlit environment-mapped draw: the highlight of light 0, the last actor key light the game loaded, when it has a
// colour.
void enh_key_highlight(inout EnhShade e, float3 P) {
    if (!e.valid) {
        return;
    }
    float3 ldir;
    float attn;
    float3 lcol;
    enh_light(0u, false, P, ldir, attn, lcol);
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
    return enh_srgb((draw_indices.w & 4u) != 0u ? lit : enh_shoulder(lit));
}

float4 enh_env(float4 t) {
    return j3d_enhanced() ? float4(saturate(t.rgb * g_enh.env_gain), t.a) : t;
}
#define J3D_ENV(x) enh_env(x)
#define j3d_pixel_begin(i) g_enh = enh_begin(i.lit_eye, i.lit_nrm)
#define j3d_pixel_channels(i) { float4 lit0; float4 lit1; if (j3d_enhanced()) { j3d_channels_enhanced(i.lit_eye, g_enh.N, i.color, i.color1, lit0, lit1); } else { j3d_channels(i.lit_eye, g_enh.N, i.color, i.color1, lit0, lit1); } i.color = lit0; i.color1 = lit1; }
#else
#define J3D_ENV(x) (x)
#endif

// A stage takes a register as an operand a, b or c through its low 8 bits: a value an
// unclamped stage left past 1 or below 0 wraps.
float j3d_in(float x) { return x - floor(x * 255.0 / 256.0) * 256.0 / 255.0; }
float3 j3d_in(float3 x) { return x - floor(x * 255.0 / 256.0) * 256.0 / 255.0; }
// The final result the same way (materials whose last stage does not clamp).
float4 j3d_out(float4 x) { return x - floor(x * 255.0 / 256.0) * 256.0 / 255.0; }

// fog over the colour (native_material.hlsli native_fog) with the material's fog values.
float3 j3d_fog(float3 c, float4 pos) {
    return native_fog(c, pos, material.fog, material.fog_color);
}

// A texture coordinate's projection: s and t over q where its generation made three.
float2 j3d_coord(float4 uv) {
    return uv.z != 0.0 && uv.w != 0.0 ? uv.xy / uv.z : uv.xy;
}

// Coordinate 0 replaced over the material's own stages (J3DNativeMaterial::overrideCoordinate): 1 the vertex's normal
// (the normal through the identity, 2x4, normalized: J3DModelX list 6, the ghost's shell over the screen's copy).
void j3d_vertex_override(NativeVertex v, inout NativeVaryings o) {
    if (material.ovr_coord.x == 1.0) {
        const float3 n = dot(v.nrm, v.nrm) > 0.0 ? normalize(v.nrm) : v.nrm;
        o.uv[0] = float4(n.xy, 1.0, 0.0);
    }
}

// A pass over the material's own geometry, channels and textures that replaces its stages (J3DNativeMaterial::
// override; the game's one-stage programs the game loads over a material): 1 texture 0 at coordinate 0 with alpha 1/4
// (J3DModelX list 2), 2 the channel colour with alpha A0 (MarioSearchLight's volume), 3 the channel colour with alpha
// RASA - K(1 - A0) (its cone: stage 0's constant alpha), 4 the channel colour with alpha (A1 + (A0 - A1) RASA) / 2 (its
// falloff), 5 texture 0 at coordinate 0 halved with stage 0's own alpha (`a0`: the material's stage 0 alpha combiner
// without channels; J3DModelX list 11, the reflection model; `ovr.y` above 0: the alpha written instead). Fog as the
// material's.
float4 j3d_override(float4 t0, float4 ras, float k0, float a0, float4 pos) {
    const uint mode = uint(material.ovr.x);
    float4 c;
    float tested = -1.0;
    if (mode == 1u) {
        c = float4(t0.rgb, 0.25);
    } else if (mode == 2u) {
        c = float4(ras.rgb, material.ovr.y);
    } else if (mode == 3u) {
        c = float4(ras.rgb, saturate(ras.a - k0 * (1.0 - material.ovr.y)));
    } else if (mode == 4u) {
        c = float4(ras.rgb, saturate(lerp(material.ovr.z, material.ovr.y, ras.a) * 0.5));
    } else {
        c = float4(saturate(t0.rgb * 0.5), material.ovr.y > 0.0 ? material.ovr.y : a0);
        tested = a0;
    }
    if (round((tested >= 0.0 ? tested : c.a) * 255.0) <= material.ovr.w) {
        discard;
    }
    c.rgb = j3d_fog(c.rgb, pos);
    return c;
}
