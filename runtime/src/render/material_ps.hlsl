// The material pixel shader: one source, compiled when Nebula is built (material_shaders.h lists the variants and the
// build compiles each to DXBC and SPIR-V). A draw's material program (its combiner stages, indirect stages, alpha test,
// fog and output form) is data in PixelConstants (material_shaders.h pack_material_program), read here per draw; the
// variant fixes only what changes the shader's interface: the lighting inputs, the render targets and early depth.
//
// Colour is written at 8 bits (the renderer never takes a 6-bit colour target's precision or dither: build_pixel_shader_key
// keeps true colour), alpha at 6 bits as the game's targets hold it.
//
// The arithmetic is the float form on the 0..255 scale: inputs A, B and C read a register's low 8 bits (wrap8), the
// compare stages and the alpha test compare 8-bit values (q8), the result keeps its low 8 bits.
//
// Variant defines (0 or 1): MAT_PPL per-pixel GX lighting, MAT_ENH enhanced lighting, MAT_MOTION motion target,
// MAT_REACTIVE reactive mask, MAT_LAYER effects layer targets, MAT_DEPTH depth export, MAT_DUAL dual-source alpha,
// MAT_EARLY early depth test.

#ifndef MAT_PPL
#define MAT_PPL 0
#endif
#ifndef MAT_ENH
#define MAT_ENH 0
#endif
#ifndef MAT_MOTION
#define MAT_MOTION 0
#endif
#ifndef MAT_REACTIVE
#define MAT_REACTIVE 0
#endif
#ifndef MAT_LAYER
#define MAT_LAYER 0
#endif
#ifndef MAT_DEPTH
#define MAT_DEPTH 0
#endif
#ifndef MAT_DUAL
#define MAT_DUAL 0
#endif
#ifndef MAT_EARLY
#define MAT_EARLY 1
#endif
#define MAT_LIGHT_INPUTS (MAT_PPL || MAT_ENH)

// PixelConstants (uber_constants.h), member for member.
cbuffer PixelConstants : register(b1) {
    uint4    stage_words[16];      // per stage: colour, alpha, order, selects
    float4   registers_in[4];      // PREV, REG0-2 as signed values over 255
    float4   constant_colours[4];  // the material's constant colours K0-K3
    float4   alpha_refs;
    float4   fog_params[2];
    float4   tex_dims[8];          // xy the map's size, zw its coordinate's scale
    float4   ztex_params;
    uint4    indirect_words[4];    // per stage: its indirect command (material_shaders.h)
    int4     indirect_matrix[6];
    uint4    indirect_misc;        // x orders, y-z scales, w stage count | enhanced material << 8
    uint4    program_header;       // x stages|coords|channels|fog, y alpha test|flags, z outputs|options|scale, w light
};
Texture2D    tex[8] : register(t0);
SamplerState samp[8] : register(s0);

struct PSIn {
    float4   pos       : SV_Position;
    float4   col0      : COLOR0;
    float4   col1      : COLOR1;
    float3   uv[8]     : TEXCOORD;
#if MAT_LIGHT_INPUTS
    float3   ppl_pos   : TEXCOORD8;
    float3   ppl_nrm   : TEXCOORD9;
    float4   ppl_vcol0 : TEXCOORD10;
    float4   ppl_vcol1 : TEXCOORD11;
#endif
#if MAT_MOTION
    float4   mv_cur    : TEXCOORD12;
    float4   mv_prev   : TEXCOORD13;
#endif
};

// A register's low 8 bits on the 0..255 scale (values within half a step of 256 count as 256), and an input's 8-bit
// value for the exact comparisons. Byte masks one component at a time on Vulkan (Mali returns the constant vector of a
// vector AND, device_quirks_vk.cpp).
float wrap8(float x) { return x - 256.0 * floor((x + 0.5) / 256.0); }
float3 wrap8(float3 x) { return x - 256.0 * floor((x + 0.5) / 256.0); }
float4 wrap8(float4 x) { return x - 256.0 * floor((x + 0.5) / 256.0); }
int q8(float x) { return int(floor(x + 0.5)) & 255; }
#if defined(NEBULA_VULKAN)
int3 q8(float3 x) { const int3 v = int3(floor(x + 0.5)); return int3(v.x & 255, v.y & 255, v.z & 255); }
#else
int3 q8(float3 x) { return int3(floor(x + 0.5)) & 255; }
#endif

// A clear ice or crystal draw's tint under physical light (RenderCore: ztex_params.w between -1 and 0 the share it loses;
// rt_materials.h kRtIceTintKept): a colour of its art (8-bit sRGB over 255) with that share of its chroma taken, its
// luminance kept (linear light, Rec. 709). Its rasterized colours take it, and the texels of its maps that are textures
// (alpha_refs.z bits 8-15; not the screen captures it shows through it); RenderCore clears its registers.
float3 tint_cleared(float3 c, float share) {
    float3 l = saturate(c);
    l = l <= 0.04045 ? l / 12.92 : pow((l + 0.055) / 1.055, 2.4);
    l = lerp(l, dot(l, float3(0.2126, 0.7152, 0.0722)).xxx, share);
    return l <= 0.0031308 ? l * 12.92 : 1.055 * pow(l, 1.0 / 2.4) - 0.055;
}

float4 sample_map(uint map, float2 uv) {
    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    [branch] switch (map) {
    case 0u: result = tex[0].Sample(samp[0], uv); break;
    case 1u: result = tex[1].Sample(samp[1], uv); break;
    case 2u: result = tex[2].Sample(samp[2], uv); break;
    case 3u: result = tex[3].Sample(samp[3], uv); break;
    case 4u: result = tex[4].Sample(samp[4], uv); break;
    case 5u: result = tex[5].Sample(samp[5], uv); break;
    case 6u: result = tex[6].Sample(samp[6], uv); break;
    default: result = tex[7].Sample(samp[7], uv); break;
    }
    return result;
}

// Components picked by four 2-bit selects (r, g, b, a in ascending bit pairs).
float4 swizzle(float4 v, uint codes) {
    return float4(v[codes & 3u], v[(codes >> 2u) & 3u], v[(codes >> 4u) & 3u], v[(codes >> 6u) & 3u]);
}

// Colour input `sel` (0 PREV, 1 PREV alpha, 2-7 REG0-2 colour/alpha, 8-9 texel, 10-11 rasterized, 12 one, 13 half,
// 14 constant colour, 15 zero) on the 0..255 scale.
float3 colour_input(uint sel, float4 r[4], float4 texc, float4 rasc, float4 kc) {
    float3 v = float3(0.0, 0.0, 0.0);
    [branch] switch (sel) {
    case 0u:  v = r[0].rgb; break;
    case 1u:  v = r[0].aaa; break;
    case 2u:  v = r[1].rgb; break;
    case 3u:  v = r[1].aaa; break;
    case 4u:  v = r[2].rgb; break;
    case 5u:  v = r[2].aaa; break;
    case 6u:  v = r[3].rgb; break;
    case 7u:  v = r[3].aaa; break;
    case 8u:  v = texc.rgb; break;
    case 9u:  v = texc.aaa; break;
    case 10u: v = rasc.rgb; break;
    case 11u: v = rasc.aaa; break;
    case 12u: v = float3(255.0, 255.0, 255.0); break;
    case 13u: v = float3(128.0, 128.0, 128.0); break;
    case 14u: v = kc.rgb; break;
    default:  v = float3(0.0, 0.0, 0.0); break;
    }
    return v;
}

// Alpha input `sel` (0 PREV, 1-3 REG0-2, 4 texel, 5 rasterized, 6 constant, 7 zero).
float alpha_input(uint sel, float4 r[4], float4 texc, float4 rasc, float4 kc) {
    float v = 0.0;
    [branch] switch (sel) {
    case 0u: v = r[0].a; break;
    case 1u: v = r[1].a; break;
    case 2u: v = r[2].a; break;
    case 3u: v = r[3].a; break;
    case 4u: v = texc.a; break;
    case 5u: v = rasc.a; break;
    case 6u: v = kc.a; break;
    default: v = 0.0; break;
    }
    return v;
}

// Constant selects: 0-7 fixed fractions, 8-11 none, colour 12-15 K0-K3, 16-31 one component of a K replicated.
float fixed_fraction(uint k) {
    return k < 4u ? float(255u - 32u * k) : float(256u - 32u * k);
}
float3 constant_rgb(uint k) {
    float3 v = float3(0.0, 0.0, 0.0);
    if (k <= 7u) {
        const float f = fixed_fraction(k);
        v = float3(f, f, f);
    } else if (k >= 12u && k <= 15u) {
        v = saturate(constant_colours[k - 12u].rgb) * 255.0;
    } else if (k >= 16u) {
        const float c = constant_colours[(k - 16u) % 4u][(k - 16u) / 4u];
        v = saturate(float3(c, c, c)) * 255.0;
    }
    return v;
}
float constant_a(uint k) {
    float v = 0.0;
    if (k <= 7u) {
        v = fixed_fraction(k);
    } else if (k >= 16u) {
        v = saturate(constant_colours[(k - 16u) % 4u][(k - 16u) / 4u]) * 255.0;
    }
    return v;
}

float bias_value(uint bias) {
    return bias == 1u ? 128.0 : bias == 2u ? -128.0 : 0.0;
}
float scale_value(uint scale) {
    return scale == 1u ? 2.0 : scale == 2u ? 4.0 : scale == 3u ? 0.5 : 1.0;
}

bool alpha_compare(uint comp, uint sample, uint ref_val) {
    bool r = false;
    [branch] switch (comp) {
    case 0u: r = false; break;
    case 1u: r = sample < ref_val; break;
    case 2u: r = sample == ref_val; break;
    case 3u: r = sample <= ref_val; break;
    case 4u: r = sample > ref_val; break;
    case 5u: r = sample != ref_val; break;
    case 6u: r = sample >= ref_val; break;
    default: r = true; break;
    }
    return r;
}

uint indirect_wrap_mask(uint wrap) {
    return wrap >= 1u && wrap <= 5u ? ((512u >> wrap) << 7u) - 1u : 0u;
}

#if MAT_LIGHT_INPUTS
// The draw's lights and channels (VertexConstants, root CBV b0) and its light block.
cbuffer PplConstants : register(b0) {
    float4 ppl_chan_ambient[2]  : packoffset(c8);
    float4 ppl_chan_material[2] : packoffset(c10);
    uint   ppl_num_chans        : packoffset(c12.y);
    uint   ppl_flags            : packoffset(c12.z);
    uint   ppl_sky_up           : packoffset(c13.z);
    uint   ppl_sky_up_known     : packoffset(c13.w);
    uint4  ppl_channel          : packoffset(c72);
    uint4  ppl_row_bases        : packoffset(c88);
};
StructuredBuffer<float4> mtx_rows : register(t3, space1);  // the matrix ring, or a replay's output
// Light n's rows w (0-3) from the draw's light block.
float4 ppl_light_row(uint n, uint w) { return mtx_rows[ppl_row_bases.w + 4u * n + w]; }
#endif

#if MAT_PPL
// Per-pixel lighting: the lit parts of the two colour channels, evaluated with the vertex stage's light formulas on the
// interpolated eye-space position and renormalized normal. `finite` (program_header.z bit 11, the key's finite light
// attenuation) keeps the attenuation operands at most 1e18, so a zero coefficient nulls an out-of-range cosine.
void ppl_add_light(uint n, uint ctrl, bool alpha_part, bool finite, float3 wpos, float3 nrm,
                   inout float3 lacc, inout float lacc_a) {
    uint  lcw  = asuint(ppl_light_row(n, 0u).w);
    float4 le1 = ppl_light_row(n, 1u);
    float4 le2 = ppl_light_row(n, 2u);
    float4 le3 = ppl_light_row(n, 3u);
    float3 lpos  = float3(le2.z, le2.w, le3.x);
    float3 ldvec = float3(le3.y, le3.z, le3.w);
    float3 cosA  = le1.xyz;
    float3 distA = float3(le1.w, le2.x, le2.y);
    const uint atten_func = (ctrl >> 9u) & 3u;
    const uint diffuse_func = (ctrl >> 7u) & 3u;
    float3 ldir;
    float attn;
    if (atten_func == 1u) {
        float3 ldelta = lpos - wpos;
        float ldd = dot(ldelta, ldelta);
        ldir = (ldd > 0.0) ? ldelta * rsqrt(ldd) : nrm;
        attn = (dot(nrm, ldir) >= 0.0) ? max(0.0, dot(nrm, ldvec)) : 0.0;
        if (finite) {
            attn = min(attn, 1.0e18);
        }
        float3 distAttn = distA;
        if (diffuse_func != 0u) {
            float dad = dot(distA, distA);
            distAttn = (dad > 0.0) ? distA * rsqrt(dad) : distA;
        }
        float denom = dot(distAttn, float3(1.0, attn, attn*attn));
        attn = (denom != 0.0)
             ? max(0.0, dot(cosA, float3(1.0, attn, attn*attn))) / denom
             : 0.0;
    } else if (atten_func == 3u) {
        float3 ldelta = lpos - wpos;
        float dist2 = dot(ldelta, ldelta);
        float dist  = sqrt(dist2);
        ldir = (dist > 0.0) ? ldelta / dist : nrm;
        attn = max(0.0, dot(ldir, ldvec));
        if (finite) {
            attn = min(attn, 1.0e18);
            dist = min(dist, 1.0e18);
            dist2 = min(dist2, 1.0e36);
        }
        float denom = dot(distA, float3(1.0, dist, dist2));
        attn = (denom != 0.0)
             ? max(0.0, cosA.x + cosA.y*attn + cosA.z*attn*attn) / denom
             : 0.0;
    } else {
        float3 ldelta = lpos - wpos;
        float ldd = dot(ldelta, ldelta);
        ldir = (ldd > 0.0) ? ldelta * rsqrt(ldd) : nrm;
        attn = 1.0;
    }
    if (alpha_part) {
        float lcol = float(lcw & 255u);
        if (diffuse_func == 1u) {
            lacc_a += attn * dot(ldir, nrm) * lcol;
        } else if (diffuse_func == 2u) {
            lacc_a += attn * max(0.0, dot(ldir, nrm)) * lcol;
        } else {
            lacc_a += attn * lcol;
        }
    } else {
        float3 lcol = float3(
            float((lcw >> 24) & 255u),
            float((lcw >> 16) & 255u),
            float((lcw >>  8) & 255u));
        if (diffuse_func == 1u) {
            lacc += attn * dot(ldir, nrm) * lcol;
        } else if (diffuse_func == 2u) {
            lacc += attn * max(0.0, dot(ldir, nrm)) * lcol;
        } else {
            lacc += attn * lcol;
        }
    }
}

void ppl_apply(inout float4 col0, inout float4 col1, float3 wpos, float3 nrm_in,
               float4 vtx_col0, float4 vtx_col1, bool finite) {
    const float3 nrm = dot(nrm_in, nrm_in) > 0.0 ? normalize(nrm_in) : nrm_in;
    [unroll] for (uint ch = 0u; ch < 2u; ++ch) {
        if (ch >= ppl_num_chans) {
            continue;
        }
        const float4 vtx = ch == 0u ? vtx_col0 : vtx_col1;
        const uint cc = ppl_channel[ch];
        const uint ca = ppl_channel[2u + ch];
        float4 result = ch == 0u ? col0 : col1;
        // A specular channel (attenuation function 1) keeps the vertex stage's colour: per pixel its highlight
        // peaks above the per-vertex values the game's stages were sized for, and a stage that overflows wraps
        // (Mario's skin and gloves turned cyan and magenta). Channels that overflow by design (a Luma's blue wraps
        // to its yellow) keep their wrap.
        if (((cc >> 1u) & 1u) != 0u && ((cc >> 9u) & 3u) != 1u) {
            const float3 mat_c = ((cc & 1u) != 0u ? vtx : ppl_chan_material[ch]).rgb;
            float3 lacc = (((cc >> 6u) & 1u) != 0u ? vtx : ppl_chan_ambient[ch]).rgb * 255.0;
            float unused_a = 0.0;
            const uint mask = ((cc >> 2u) & 0xFu) | (((cc >> 11u) & 0xFu) << 4u);
            [loop] for (uint n = 0u; n < 8u; ++n) {
                if (((mask >> n) & 1u) != 0u) {
                    ppl_add_light(n, cc, false, finite, wpos, nrm, lacc, unused_a);
                }
            }
            result.rgb = mat_c * (clamp(lacc, 0.0, 255.0) / 255.0);
        }
        if (((ca >> 1u) & 1u) != 0u && ((ca >> 9u) & 3u) != 1u) {
            const float mat_a = (ca & 1u) != 0u ? vtx.a : ppl_chan_material[ch].a;
            float lacc_a = ((((ca >> 6u) & 1u) != 0u ? vtx.a : ppl_chan_ambient[ch].a)) * 255.0;
            float3 unused_c = float3(0.0, 0.0, 0.0);
            const uint mask = ((ca >> 2u) & 0xFu) | (((ca >> 11u) & 0xFu) << 4u);
            [loop] for (uint n = 0u; n < 8u; ++n) {
                if (((mask >> n) & 1u) != 0u) {
                    ppl_add_light(n, ca, true, finite, wpos, nrm, unused_c, lacc_a);
                }
            }
            result.a = mat_a * (clamp(lacc_a, 0.0, 255.0) / 255.0);
        }
        if (ch == 0u) {
            col0 = result;
        } else {
            col1 = result;
        }
    }
}
#endif

#if MAT_ENH
// Enhanced lighting (enhanced_lighting.h, its default tuning). The lit colour channel 0 adds its lights in linear light,
// each along its authored falloff (kEnhRamp blends in the linear ramp between the channel's ambient and
// ambient-plus-light colours), under a soft shoulder; the ambient is a sky hemisphere about the player's up whose
// reflection takes its share from the diffuse light. The key light's highlight: dielectric, or tinted by the surface
// colour on environment-mapped (metal) draws. Other channels keep the vertex stage's colours.
static const float kEnhRamp = 0;
static const float kEnhHemi = 0.300000012;
static const float kEnhSky = 1;
static const float kEnhKnee = 0.800000012;
static const float kEnhSheen = 3.14159274;
static const float kEnhSheenRough = 0.449999988;
static const float kEnhMetal = 3.14159274;
static const float kEnhMetalRough = 0.349999994;
static const float kEnhMetalF0 = 0.5;
static const float kEnhEnvBase = 1;
static const float kEnhEnvGraze = 0.800000012;

float3 enh_lin(float3 c) { return pow(max(c, 0.0), 2.2); }
float3 enh_srgb(float3 c) { return pow(max(c, 0.0), 1.0 / 2.2); }

// Identity up to the knee, then an exponential shoulder towards 1.
float3 enh_shoulder(float3 x) {
    const float3 over = kEnhKnee + (1.0 - kEnhKnee) * (1.0 - exp((kEnhKnee - x) / (1.0 - kEnhKnee)));
    return x > kEnhKnee ? over : x;
}

float enh_ggx(float NoH, float a2) {
    const float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}

float enh_vis(float NoL, float NoV, float a) {
    return 0.5 / max(NoL * (NoV * (1.0 - a) + a) + NoV * (NoL * (1.0 - a) + a), 1e-4);
}

// Light n at P: direction towards it, the spot attenuation (function 3) or none, colour. The attenuation operands stay
// finite: a light the game left without a direction keeps its light.
void enh_light(uint n, bool spot, float3 P, out float3 ldir, out float attn, out float3 lcol) {
    const uint lcw = asuint(ppl_light_row(n, 0u).w);
    const float4 le1 = ppl_light_row(n, 1u);
    const float4 le2 = ppl_light_row(n, 2u);
    const float4 le3 = ppl_light_row(n, 3u);
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

// The player's up in eye space (VertexConstants::sky_up: octahedral, x and y in snorm16 halves), else the view's up.
float3 enh_up() {
    if (ppl_sky_up_known == 0u) {
        return float3(0.0, 1.0, 0.0);
    }
    const float2 f = float2(int2(int(ppl_sky_up << 16u), int(ppl_sky_up)) >> 16) / 32767.0;
    float3 n = float3(f, 1.0 - abs(f.x) - abs(f.y));
    const float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

struct EnhShade {
    bool   valid;      // the draw has a normal
    float3 N;          // as the game lights it (two-sided surfaces keep it from behind)
    float3 Nv;         // facing the viewer: the reflections' normal
    float3 V;
    float  NoV;
    float  a2_filter;  // the lobes' widening for the normal's change across the pixel
    float  env_gain;   // environment lookups: Fresnel gain
    float3 spec;       // dielectric highlight and sky reflection, linear, added after the combiners
    float3 metal;      // metal lobe, linear, times the surface's Fresnel after the combiners
    float  metal_fw;   // its Schlick weight
};

EnhShade enh_begin(float3 P, float3 n) {
    EnhShade e = (EnhShade)0;
    e.valid = dot(n, n) > 0.0;
    e.N = e.valid ? normalize(n) : float3(0.0, 0.0, 0.0);
    e.V = dot(P, P) > 0.0 ? normalize(-P) : float3(0.0, 0.0, 1.0);
    e.Nv = dot(e.N, e.V) < 0.0 ? -e.N : e.N;
    e.NoV = e.valid ? min(abs(dot(e.N, e.V)), 1.0) : 1.0;
    const float g = 1.0 - e.NoV;
    e.env_gain = e.valid ? kEnhEnvBase + kEnhEnvGraze * (g * g) * (g * g) * g : 1.0;
    // Tokuyoshi and Kaplanyan 2019: alpha^2 grows by the normal's variance over the pixel's footprint (filter
    // variance 0.25), at most 0.18.
    const float3 dx = ddx(e.N);
    const float3 dy = ddy(e.N);
    e.a2_filter = e.valid ? min(0.5 * (dot(dx, dx) + dot(dy, dy)), 0.18) : 0.0;
    return e;
}

// The material table's lobe for this draw (light word bits 20-30), else the tuning's for the draw's class (`metal`:
// environment-mapped). The table decides metal too: an environment map is also how the game paints the gloss of
// shells, plastics and paint.
void enh_table(bool metal, out float rough, out bool is_metal) {
    const uint w = program_header.w;
    rough = metal ? kEnhMetalRough : kEnhSheenRough;
    is_metal = metal;
    if ((w & 0x40000000u) != 0u) {
        rough = max(float((w >> 20) & 63u) / 63.0, 0.05);
        is_metal = float((w >> 26) & 15u) / 15.0 >= 0.5;
    }
}

// A light's highlight. `radiance` is its colour in linear light: the game's diffuse term is the colour times N.L, so
// the colour is the light's irradiance over pi and the lobe takes kEnhSheen = pi.
void enh_highlight(inout EnhShade e, float3 ldir, float3 radiance, bool env_metal) {
    const float NoL = saturate(dot(e.Nv, ldir));
    if (NoL <= 0.0) {
        return;
    }
    float rough;
    bool metal;
    enh_table(env_metal, rough, metal);
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

// The ambient's sky term: a hemisphere about the up direction (mean 1 over the sphere); 1 while the ray tracer lights
// the scene (`sky_light` false).
float enh_sky(EnhShade e, bool sky_light) {
    return e.valid && sky_light ? 1.0 + kEnhHemi * dot(e.N, enh_up()) : 1.0;
}

// The sky's reflection off a dielectric (F0 0.04) at the surface's roughness, along the reflected view direction:
// Karis' analytic fit of the split-sum environment BRDF ("Physically Based Shading on Mobile", 2014). Adds it to the
// highlights; returns the diffuse light's share (what is not reflected). Metals reflect through their environment maps.
float enh_sky_reflection(inout EnhShade e, float3 sky) {
    float rough;
    bool metal;
    enh_table(false, rough, metal);
    if (!e.valid || metal) {
        return 1.0;
    }
    const float4 r = rough * float4(-1.0, -0.0275, -0.572, 0.022) + float4(1.0, 0.0425, 1.04, -0.04);
    const float a004 = min(r.x * r.x, exp2(-9.28 * e.NoV)) * r.x + r.y;
    const float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    const float reflected = saturate(0.04 * ab.x + ab.y) * kEnhSky;
    const float3 R = reflect(-e.V, e.Nv);
    e.spec += sky * (reflected * (1.0 + kEnhHemi * dot(R, enh_up())));
    return 1.0 - reflected;
}

// Unlit environment-mapped draws: the highlight of light 0, the last actor key light the game loaded
// (loadActorLightInfo), when it has a colour.
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

// The highlights added to the combiners' result in linear light. A metal's F0 is its own colour raised to a peak of at
// least kEnhMetalF0, so metals baked dull still reflect their light in their own hue. `view`: the highlights alone
// (diagnostics).
float3 enh_finish(float3 frag, EnhShade e, bool view) {
    const float3 c = enh_lin(frag);
    const float peak = max(max(c.r, c.g), max(c.b, 1e-4));
    const float3 f0 = saturate(c * (max(peak, kEnhMetalF0) / peak));
    const float3 h = e.spec + e.metal * (f0 + (1.0 - f0) * e.metal_fw);
    return enh_srgb(view ? h : c + h);
}
#endif

#if MAT_MOTION && MAT_DEPTH
struct PSOut { float4 color : SV_Target0; float2 motion : SV_Target1; float depth : SV_Depth; };
#elif MAT_MOTION
struct PSOut { float4 color : SV_Target0; float2 motion : SV_Target1; };
#elif MAT_REACTIVE && MAT_LAYER && MAT_DEPTH
struct PSOut { float4 color : SV_Target0; float reactive : SV_Target1; float4 layer : SV_Target2;
               float4 trans : SV_Target3; float depth : SV_Depth; };
#elif MAT_REACTIVE && MAT_LAYER
struct PSOut { float4 color : SV_Target0; float reactive : SV_Target1; float4 layer : SV_Target2;
               float4 trans : SV_Target3; };
#elif MAT_REACTIVE && MAT_DEPTH
struct PSOut { float4 color : SV_Target0; float reactive : SV_Target1; float depth : SV_Depth; };
#elif MAT_REACTIVE
struct PSOut { float4 color : SV_Target0; float reactive : SV_Target1; };
#elif MAT_DEPTH && MAT_DUAL
struct PSOut { float4 color : SV_Target0; float4 color1 : SV_Target1; float depth : SV_Depth; };
#elif MAT_DEPTH
struct PSOut { float4 color : SV_Target0; float depth : SV_Depth; };
#elif MAT_DUAL
struct PSOut { float4 color : SV_Target0; float4 color1 : SV_Target1; };
#else
struct PSOut { float4 color : SV_Target0; };
#endif

#if MAT_EARLY && !MAT_DEPTH
[earlydepthstencil]
#endif
PSOut main(PSIn pin) {
    const uint n_stages = min(program_header.x & 0xFFu, 16u);
    const uint n_coords = (program_header.x >> 8u) & 0xFFu;
    const uint fog_type = (program_header.x >> 24u) & 0xFFu;
    const uint comp0 = program_header.y & 0xFFu;
    const uint comp1 = (program_header.y >> 8u) & 0xFFu;
    const uint logic = (program_header.y >> 16u) & 0xFFu;
    const uint key_flags = (program_header.y >> 24u) & 0xFFu;
    const uint output_flags = program_header.z & 0xFFu;
    const uint options = (program_header.z >> 8u) & 0xFFu;
    const uint n_indirect = indirect_misc.w & 0xFFu;
    const uint indirect_orders = indirect_misc.x;
    const bool ztex_on = (key_flags & 0x02u) != 0u;
    // Ray tracing's branches (options bit 6, a ray-traced scene's draws).
    const bool rt_scene = (options & 0x40u) != 0u;

    float3 uvs[8];
    [unroll] for (uint u = 0u; u < 8u; ++u) {
        uvs[u] = pin.uv[u];
    }

    // Registers PREV, REG0, REG1, REG2 (0..255 scale; unclamped stages keep -1024..1023).
    float4 r[4];
    r[0] = registers_in[0] * 255.0;
    r[1] = registers_in[1] * 255.0;
    r[2] = registers_in[2] * 255.0;
    r[3] = registers_in[3] * 255.0;
    // Inputs A, B and C of a register: its low 8 bits, clamped instead under ray-traced lighting (an enhanced draw,
    // light word bit 17), as the de-lit colour channel and the sphere map's mean can lift a stage the game kept at 255
    // just past it.
    bool clamp_inputs = false;
#if MAT_PPL
    ppl_apply(pin.col0, pin.col1, pin.ppl_pos, pin.ppl_nrm, pin.ppl_vcol0, pin.ppl_vcol1, (options & 0x08u) != 0u);
#endif
#if MAT_ENH
    const uint enh_material = indirect_misc.w >> 8u;  // the key's enhanced material word, bits 0-22
    const uint enh_env = enh_material & 0xFFu;
    const bool enh_lit = (enh_material & 0x100u) != 0u;
    const bool rt_paths = rt_scene;
    const bool rt_light = rt_paths && (program_header.w & 0x20000u) != 0u;
    clamp_inputs = rt_light;
    // Left of the split column the game's own (per-vertex) lighting.
    const bool enh_on = pin.pos.x >= float(program_header.w & 0xFFFFu);
    EnhShade enh = enh_begin(pin.ppl_pos, pin.ppl_nrm);
    if (enh_lit) {
        // Colour channel 0 (material word bits 10-22): its lights in order, the first one's highlight.
        const uint lights = (enh_material >> 10u) & 0xFFu;
        const uint dfn = (enh_material >> 18u) & 3u;
        const bool spot = (enh_material & (1u << 20u)) != 0u;
        if (enh_on) {
            const float3 enh_mat = (enh_material & (1u << 22u)) != 0u ? pin.ppl_vcol0.rgb : ppl_chan_material[0].rgb;
            const float3 enh_amb = (enh_material & (1u << 21u)) != 0u ? pin.ppl_vcol0.rgb : ppl_chan_ambient[0].rgb;
            // Under ray-traced lighting the channel is the game's own light averaged over direction: its ambient plus
            // each light at the mean facing of a lit surface (0.55; lights without a facing term whole), clamped as the
            // game's channel. The art's level and tint stay; the direction of its camera-following lights leaves, and
            // the tracer's sun gives the direction.
            if (rt_light) {
                float3 enh_avg = enh_amb;
                const float facing = dfn != 0u ? 0.55 : 1.0;
                [loop] for (uint n = 0u; n < 8u; ++n) {
                    if ((lights & (1u << n)) == 0u) continue;
                    float3 ldir;
                    float attn;
                    float3 lcol;
                    enh_light(n, spot, pin.ppl_pos, ldir, attn, lcol);
                    enh_avg += lcol * (attn * facing);
                }
                // In the game's own light units (bit 31) the channel is its full light, the ambient and every light at
                // N.L 1, clamped as the game's channel: the traced light (sky, sun, bounces) multiplies it.
                if ((program_header.w & 0x80000000u) != 0u) {
                    enh_avg = enh_amb;
                    [loop] for (uint n = 0u; n < 8u; ++n) {
                        if ((lights & (1u << n)) == 0u) continue;
                        float3 ldir;
                        float attn;
                        float3 lcol;
                        enh_light(n, spot, pin.ppl_pos, ldir, attn, lcol);
                        enh_avg += lcol * attn;
                    }
                    // Lit by the game's player light rig (flags bit 7, RenderCore::rt_player_rig_draw): its level, not
                    // its hue (a rig such as the prologue's cyan night rig would tint the player); the traced light
                    // gives the colour.
                    if ((ppl_flags & 128u) != 0u) {
                        enh_avg = dot(enh_avg, float3(0.2126, 0.7152, 0.0722)).xxx;
                    }
                    // De-baked (VertexConstants flags bit 4): lit at its program's neutral light (bits 8-15), not the
                    // art's full light, in the full light's hue, since the ambient register or the vertices often hold
                    // the object's own colour.
                    if ((ppl_flags & 16u) != 0u) {
                        const float enh_peak = max(enh_avg.r, max(enh_avg.g, enh_avg.b));
                        enh_avg = (enh_peak > 1e-4 ? enh_avg / enh_peak : float3(1.0, 1.0, 1.0)) *
                                  (float((ppl_flags >> 8u) & 0xFFu) / 255.0);
                    }
                }
                pin.col0.rgb = enh_mat * saturate(enh_avg);
            } else {
                const float3 enh_amb_lin = enh_lin(enh_amb);
                float3 enh_acc = enh_amb_lin * enh_sky(enh, !rt_light);
                bool first = true;
                [loop] for (uint n = 0u; n < 8u; ++n) {
                    if ((lights & (1u << n)) == 0u) continue;
                    float3 ldir;
                    float attn;
                    float3 lcol;
                    enh_light(n, spot, pin.ppl_pos, ldir, attn, lcol);
                    const float x = dfn == 1u ? attn * dot(enh.N, ldir)
                                  : dfn == 2u ? attn * max(dot(enh.N, ldir), 0.0)
                                              : attn;
                    enh_acc += lerp(enh_lin(enh_amb + lcol * x) - enh_amb_lin,
                                    (enh_lin(enh_amb + lcol) - enh_amb_lin) * x, kEnhRamp);
                    if (first && dfn != 0u) {
                        if (enh.valid) enh_highlight(enh, ldir, enh_lin(lcol) * attn, enh_env != 0u);
                    }
                    first = false;
                }
                enh_acc *= enh_sky_reflection(enh, enh_amb_lin);
                pin.col0.rgb = enh_mat * enh_srgb(enh_shoulder(max(enh_acc, 0.0)));
            }
        }
    } else {
        if (enh_on && !rt_light) enh_key_highlight(enh, pin.ppl_pos);
    }
#endif
    // A clear ice or crystal draw's tint: its rasterized colours here, its textures' texels per stage.
    const bool tinted = rt_scene && ztex_params.w < 0.0 && ztex_params.w > -1.0;
    const uint tinted_maps = tinted ? (asuint(alpha_refs.z) >> 8u) & 0xFFu : 0u;
    if (tinted) {
        pin.col0.rgb = tint_cleared(pin.col0.rgb, -ztex_params.w);
        pin.col1.rgb = tint_cleared(pin.col1.rgb, -ztex_params.w);
    }

    float4 tex_colour = float4(0.0, 0.0, 0.0, 0.0);
    int4 last_raw_texel = int4(0, 0, 0, 0);
    float4 ras_colour = float4(0.0, 0.0, 0.0, 0.0);
    float4 const_colour = float4(0.0, 0.0, 0.0, 0.0);
    int alpha_bump = 0;
    int2 coord_fix = int2(0, 0);
    int3 indirect_texel[4] = {int3(0,0,0), int3(0,0,0), int3(0,0,0), int3(0,0,0)};

    // Indirect stages: each map's texel at its coordinate (scaled down by its power of two).
    [loop] for (uint ind = 0u; ind < n_indirect && ind < 4u; ++ind) {
        const uint texmap = (indirect_orders >> (ind * 6u)) & 0x7u;
        const uint texcoord_raw = (indirect_orders >> (ind * 6u + 3u)) & 0x7u;
        const uint texcoord = texcoord_raw < n_coords ? texcoord_raw : 0u;
        const uint scale_word = ind < 2u ? indirect_misc.y : indirect_misc.z;
        const uint pair_shift = (ind & 1u) != 0u ? 8u : 0u;
        const uint scale_s = (scale_word >> pair_shift) & 0xFu;
        const uint scale_t = (scale_word >> (pair_shift + 4u)) & 0xFu;
        if (n_coords != 0u) {
            float3 ind_tq = uvs[texcoord];
            float2 ind_uv = (ind_tq.z == 0.0) ? ind_tq.xy : ind_tq.xy / ind_tq.z;
            int2 ind_fix = int2(round(ind_uv * tex_dims[texcoord].zw * 128.0));
            ind_fix = int2(ind_fix.x >> scale_s, ind_fix.y >> scale_t);
            float2 ind_tuv = (float2(ind_fix) / 128.0) / tex_dims[texmap].xy;
            float4 ind_sample = sample_map(texmap, ind_tuv);
            indirect_texel[ind] = int3(round(saturate(ind_sample.abg) * 255.0));
        } else {
            float4 ind_sample = sample_map(texmap, float2(0.0, 0.0));
            indirect_texel[ind] = int3(round(saturate(ind_sample.abg) * 255.0));
        }
    }

    [loop] for (uint st = 0u; st < n_stages; ++st) {
        const uint4 words = stage_words[st];
        const uint colour_word = words.x;
        const uint alpha_word = words.y;
        const uint order = words.z;
        const uint selects = words.w;

        const uint colour_d = colour_word & 0xFu;
        const uint colour_c = (colour_word >> 4u) & 0xFu;
        const uint colour_b = (colour_word >> 8u) & 0xFu;
        const uint colour_a = (colour_word >> 12u) & 0xFu;
        const uint colour_bias = (colour_word >> 16u) & 0x3u;
        const bool colour_sub = ((colour_word >> 18u) & 1u) != 0u;
        const bool colour_clamp = ((colour_word >> 19u) & 1u) != 0u;
        const uint colour_scale = (colour_word >> 20u) & 0x3u;
        const uint colour_dest = (colour_word >> 22u) & 0x3u;

        const uint alpha_d = (alpha_word >> 4u) & 0x7u;
        const uint alpha_c = (alpha_word >> 7u) & 0x7u;
        const uint alpha_b = (alpha_word >> 10u) & 0x7u;
        const uint alpha_a = (alpha_word >> 13u) & 0x7u;
        const uint alpha_bias = (alpha_word >> 16u) & 0x3u;
        const bool alpha_sub = ((alpha_word >> 18u) & 1u) != 0u;
        const bool alpha_clamp = ((alpha_word >> 19u) & 1u) != 0u;
        const uint alpha_scale = (alpha_word >> 20u) & 0x3u;
        const uint alpha_dest = (alpha_word >> 22u) & 0x3u;

        const uint texmap = order & 0x7u;
        const uint texcoord_raw = (order >> 3u) & 0x7u;
        const uint texcoord = (n_coords != 0u && texcoord_raw < n_coords) ? texcoord_raw : 0u;
        const bool tex_en = ((order >> 6u) & 1u) != 0u;
        const uint ras_chan = (order >> 7u) & 0x7u;

        const uint kc_sel = selects & 0x1Fu;
        const uint ka_sel = (selects >> 5u) & 0x1Fu;
        const uint ras_swz = (selects >> 10u) & 0xFFu;
        const uint tex_swz = (selects >> 18u) & 0xFFu;

        const uint4 ind_word4 = indirect_words[st >> 2u];
        const uint ind_word = ind_word4[st & 3u];
        // Bits 24 and 25 (pack_material_program): the stage has an indirect command, and it or a later stage that adds
        // the previous coordinate needs this stage's coordinate.
        const bool has_indirect_cmd = ((ind_word >> 24u) & 1u) != 0u;
        const bool indirect_coord_needed = ((ind_word >> 25u) & 1u) != 0u;
        const uint ind_bt = ind_word & 0x3u;
        const uint ind_fmt = (ind_word >> 2u) & 0x3u;
        const uint ind_bias = (ind_word >> 4u) & 0x7u;
        const uint ind_bs = (ind_word >> 7u) & 0x3u;
        const uint ind_mtx_sel = (ind_word >> 9u) & 0x3u;
        const uint ind_mtx_id = (ind_word >> 11u) & 0x3u;
        const uint ind_wrap_s = (ind_word >> 13u) & 0x7u;
        const uint ind_wrap_t = (ind_word >> 16u) & 0x7u;
        const bool ind_add_prev = ((ind_word >> 20u) & 1u) != 0u;
        const bool has_indirect_sample = has_indirect_cmd && ind_bt < n_indirect;

        if (has_indirect_sample && ind_bs != 0u) {
            const uint alpha_shift = ind_fmt == 0u ? 0u : ind_fmt == 1u ? 5u : ind_fmt == 2u ? 4u : 3u;
            const int3 ind_value = indirect_texel[ind_bt];
            const int component = ind_bs == 1u ? ind_value.x : ind_bs == 2u ? ind_value.y : ind_value.z;
            alpha_bump = (component << alpha_shift) & 248;
        }

        // The indirect coordinate operation before any texture lookup. Disabled texture stages still update this
        // state when a later add-previous stage observes it.
        if (indirect_coord_needed) {
            const uint fmt_shift = ind_fmt == 0u ? 0u : ind_fmt == 1u ? 3u : ind_fmt == 2u ? 4u : 5u;
            const int bias_add = ind_fmt == 0u ? -128 : 1;
            int2 stage_coord = int2(0, 0);
            if (n_coords != 0u) {
                float3 tq = uvs[texcoord];
                float2 tuv = (tq.z == 0.0) ? tq.xy : tq.xy / tq.z;
                stage_coord = int2(round(tuv * tex_dims[texcoord].zw * 128.0));
            }
            int2 wrapped = stage_coord;
            int2 offset = int2(0, 0);
            if (has_indirect_sample) {
                int3 ind_coord = indirect_texel[ind_bt] >> fmt_shift;
                if ((ind_bias & 0x1u) != 0u) ind_coord.x += bias_add;
                if ((ind_bias & 0x2u) != 0u) ind_coord.y += bias_add;
                if ((ind_bias & 0x4u) != 0u) ind_coord.z += bias_add;
                if (ind_mtx_sel != 0u) {
                    const uint row = (ind_mtx_sel - 1u) * 2u;
                    if (ind_mtx_id == 0u) {
                        offset = int2(
                            (indirect_matrix[row].x * ind_coord.x + indirect_matrix[row].y * ind_coord.y +
                             indirect_matrix[row].z * ind_coord.z) >> 3,
                            (indirect_matrix[row + 1u].x * ind_coord.x + indirect_matrix[row + 1u].y * ind_coord.y +
                             indirect_matrix[row + 1u].z * ind_coord.z) >> 3);
                    } else if (ind_mtx_id == 1u) {
                        offset = (stage_coord * ind_coord.xx) >> 8;
                    } else if (ind_mtx_id == 2u) {
                        offset = (stage_coord * ind_coord.yy) >> 8;
                    }
                    if (indirect_matrix[row].w >= 0) {
                        offset = offset >> indirect_matrix[row].w;
                    } else {
                        offset = offset << (-indirect_matrix[row].w);
                    }
                }
            }
            if (ind_wrap_s == 0u) {
                wrapped.x = stage_coord.x;
            } else if (ind_wrap_s >= 6u) {
                wrapped.x = 0;
            } else {
                wrapped.x = stage_coord.x & int(indirect_wrap_mask(ind_wrap_s));
            }
            if (ind_wrap_t == 0u) {
                wrapped.y = stage_coord.y;
            } else if (ind_wrap_t >= 6u) {
                wrapped.y = 0;
            } else {
                wrapped.y = stage_coord.y & int(indirect_wrap_mask(ind_wrap_t));
            }
            if (ind_add_prev) {
                coord_fix += wrapped + offset;
            } else {
                coord_fix = wrapped + offset;
            }
            coord_fix = (coord_fix << 8) >> 8;
        }

        if (tex_en && n_coords != 0u) {
            float4 tex_sample;
            if (has_indirect_cmd) {
                const float2 ind_tuv = (float2(coord_fix) / 128.0) / tex_dims[texmap].xy;
                tex_sample = saturate(sample_map(texmap, ind_tuv));
            } else {
                // The coordinate in its scale (tex_dims zw) over the map's size.
                const float3 tq = uvs[texcoord];
                const float2 tuv = (tq.z == 0.0) ? tq.xy : tq.xy / tq.z;
                tex_sample = saturate(sample_map(texmap, tuv * tex_dims[texcoord].zw / tex_dims[texmap].xy));
            }
            if (ztex_on) {
                last_raw_texel = int4(round(tex_sample * 255.0));
            }
            tex_colour = swizzle(tex_sample * 255.0, tex_swz);
            if (((tinted_maps >> texmap) & 1u) != 0u) {
                tex_colour.rgb = tint_cleared(tex_colour.rgb / 255.0, -ztex_params.w) * 255.0;
            }
#if MAT_ENH
            // Under ray tracing the game's painted reflection (metals, crystals, sheen) stays as drawn and the traced
            // reflection screens over it; replacing the map by its mean would flatten chrome. Enhanced lighting alone
            // scales it.
            if (!has_indirect_cmd && ((enh_env >> texcoord) & 1u) != 0u && enh_on && !rt_light) {
                tex_colour.rgb = min(tex_colour.rgb * enh.env_gain, 255.0);
            }
#endif
        } else if (n_coords != 0u) {
            // White when the stage disables its texture lookup but the draw has texture coordinates (a later stage
            // uses the texel as a neutral multiplier); black without any coordinates.
            tex_colour = float4(255.0, 255.0, 255.0, 255.0);
        } else {
            tex_colour = float4(0.0, 0.0, 0.0, 0.0);
        }

        // Rasterized colour (with the stage's swizzle): channels 0 and 1, the bump alpha (5) and its normalized form
        // (6); others read zero.
        if (ras_chan == 0u) {
            ras_colour = saturate(swizzle(pin.col0, ras_swz)) * 255.0;
        } else if (ras_chan == 1u) {
            ras_colour = saturate(swizzle(pin.col1, ras_swz)) * 255.0;
        } else if (ras_chan == 5u) {
            ras_colour = swizzle(float4(alpha_bump, alpha_bump, alpha_bump, alpha_bump), ras_swz);
        } else if (ras_chan == 6u) {
            const int abn = alpha_bump | (alpha_bump >> 5);
            ras_colour = swizzle(float4(abn, abn, abn, abn), ras_swz);
        } else {
            ras_colour = float4(0.0, 0.0, 0.0, 0.0);
        }

        const_colour = float4(constant_rgb(kc_sel), constant_a(ka_sel));

        // A, B and C read a register's low 8 bits; D reads the register whole. Colour and alpha observe the same
        // stage-entry registers; the alpha R8/GR16/BGR24 compares reuse the colour A/B inputs.
        float3 ca = colour_input(colour_a, r, tex_colour, ras_colour, const_colour);
        float3 cb = colour_input(colour_b, r, tex_colour, ras_colour, const_colour);
        float3 cc = colour_input(colour_c, r, tex_colour, ras_colour, const_colour);
        const float3 cd = colour_input(colour_d, r, tex_colour, ras_colour, const_colour);
        if (colour_a <= 7u) ca = clamp_inputs ? clamp(ca, 0.0, 255.0) : wrap8(ca);
        if (colour_b <= 7u) cb = clamp_inputs ? clamp(cb, 0.0, 255.0) : wrap8(cb);
        if (colour_c <= 7u) cc = clamp_inputs ? clamp(cc, 0.0, 255.0) : wrap8(cc);
        float aa = alpha_input(alpha_a, r, tex_colour, ras_colour, const_colour);
        float ab = alpha_input(alpha_b, r, tex_colour, ras_colour, const_colour);
        float ac = alpha_input(alpha_c, r, tex_colour, ras_colour, const_colour);
        const float ad = alpha_input(alpha_d, r, tex_colour, ras_colour, const_colour);
        if (alpha_a <= 3u) aa = clamp_inputs ? clamp(aa, 0.0, 255.0) : wrap8(aa);
        if (alpha_b <= 3u) ab = clamp_inputs ? clamp(ab, 0.0, 255.0) : wrap8(ab);
        if (alpha_c <= 3u) ac = clamp_inputs ? clamp(ac, 0.0, 255.0) : wrap8(ac);

        float3 colour_out;
        if (colour_bias == 3u) {
            // Compare: d + ((a OP b) ? c : 0) on the inputs' 8-bit values, the operand width from the scale field and
            // the operation (greater, equal) from the subtract bit.
            const int3 ca_i = q8(ca);
            const int3 cb_i = q8(cb);
            const int3 cc_i = q8(cc);
            int3 cmp_i;
            if (colour_scale == 0u) {
                cmp_i = (colour_sub ? (ca_i.x == cb_i.x) : (ca_i.x > cb_i.x)) ? cc_i : int3(0,0,0);
            } else if (colour_scale == 1u) {
                const int av = ca_i.x + (ca_i.y << 8);
                const int bv = cb_i.x + (cb_i.y << 8);
                cmp_i = (colour_sub ? (av == bv) : (av > bv)) ? cc_i : int3(0,0,0);
            } else if (colour_scale == 2u) {
                const int av = ca_i.x + (ca_i.y << 8) + (ca_i.z << 16);
                const int bv = cb_i.x + (cb_i.y << 8) + (cb_i.z << 16);
                cmp_i = (colour_sub ? (av == bv) : (av > bv)) ? cc_i : int3(0,0,0);
            } else if (colour_sub) {
                cmp_i = (int3(1,1,1) - sign(abs(ca_i - cb_i))) * cc_i;
            } else {
                cmp_i = max(sign(ca_i - cb_i), int3(0,0,0)) * cc_i;
            }
            colour_out = cd + float3(cmp_i);
        } else {
            // d (+/-) lerp(a, b, c), biased and scaled.
            const float3 lerpv = lerp(ca, cb, cc / 255.0);
            colour_out = (cd + bias_value(colour_bias) + (colour_sub ? -lerpv : lerpv)) * scale_value(colour_scale);
        }
        colour_out = colour_clamp ? clamp(colour_out, 0.0, 255.0) : clamp(colour_out, -1024.0, 1023.0);

        float alpha_out;
        if (alpha_bias == 3u) {
            // Alpha compare: widths R8/GR16/BGR24 compare the stage's colour inputs, A8 (scale 3) its alpha inputs.
            bool acmp;
            if (alpha_scale == 3u) {
                const int aa_i = q8(aa);
                const int ab_i = q8(ab);
                acmp = alpha_sub ? (aa_i == ab_i) : (aa_i > ab_i);
            } else {
                const int3 ca_i = q8(ca);
                const int3 cb_i = q8(cb);
                if (alpha_scale == 0u) {
                    acmp = alpha_sub ? (ca_i.x == cb_i.x) : (ca_i.x > cb_i.x);
                } else if (alpha_scale == 1u) {
                    const int av = ca_i.x + (ca_i.y << 8);
                    const int bv = cb_i.x + (cb_i.y << 8);
                    acmp = alpha_sub ? (av == bv) : (av > bv);
                } else {
                    const int av = ca_i.x + (ca_i.y << 8) + (ca_i.z << 16);
                    const int bv = cb_i.x + (cb_i.y << 8) + (cb_i.z << 16);
                    acmp = alpha_sub ? (av == bv) : (av > bv);
                }
            }
            alpha_out = ad + (acmp ? float(q8(ac)) : 0.0);
        } else {
            const float lerpv = lerp(aa, ab, ac / 255.0);
            alpha_out = (ad + bias_value(alpha_bias) + (alpha_sub ? -lerpv : lerpv)) * scale_value(alpha_scale);
        }
        alpha_out = alpha_clamp ? clamp(alpha_out, 0.0, 255.0) : clamp(alpha_out, -1024.0, 1023.0);

        [branch] switch (colour_dest) {
        case 0u: r[0].rgb = colour_out; break;
        case 1u: r[1].rgb = colour_out; break;
        case 2u: r[2].rgb = colour_out; break;
        default: r[3].rgb = colour_out; break;
        }
        [branch] switch (alpha_dest) {
        case 0u: r[0].a = alpha_out; break;
        case 1u: r[1].a = alpha_out; break;
        case 2u: r[2].a = alpha_out; break;
        default: r[3].a = alpha_out; break;
        }
    }

    // The last stage's result reaches the output whichever register it wrote: materials that end on REG0-2 (multi-pass
    // highlight and indirect effects) otherwise show a stale PREV.
    if (n_stages != 0u) {
        const uint4 last = stage_words[n_stages - 1u];
        const uint last_colour_dest = (last.x >> 22u) & 0x3u;
        const uint last_alpha_dest = (last.y >> 22u) & 0x3u;
        [branch] switch (last_colour_dest) {
        case 1u: r[0].rgb = r[1].rgb; break;
        case 2u: r[0].rgb = r[2].rgb; break;
        case 3u: r[0].rgb = r[3].rgb; break;
        default: break;
        }
        [branch] switch (last_alpha_dest) {
        case 1u: r[0].a = r[1].a; break;
        case 2u: r[0].a = r[2].a; break;
        case 3u: r[0].a = r[3].a; break;
        default: break;
        }
    }

    // The output takes the result's low 8 bits: unclamped stages can hold wider signed values, but the alpha test, fog,
    // the colour output and the alpha == 1 rule see only those. Under ray-traced lighting the result clamps like the
    // inputs, so a channel lifted just past 255 does not wrap.
    const float4 frag8 = clamp_inputs ? clamp(r[0], 0.0, 255.0) : wrap8(r[0]);
    float4 frag = saturate(frag8 / 255.0);
    const uint frag_a8 = uint(q8(frag8.a));

#if MAT_ENH
    // The ray tracer's reflections own a reflective draw's highlights while they run.
    const uint enh_skip = (enh_material & 0x200u) != 0u ? 0x30000u : 0x20000u;
    if (enh_on && (!rt_paths || (program_header.w & enh_skip) == 0u)) {
        frag.rgb = enh_finish(frag.rgb, enh, (program_header.w & 0x40000u) != 0u);
    }
    if (enh_lit && (program_header.w & 0x80000u) != 0u) {
        // Diagnostics: colour channel 0 itself, the game's left of the split.
        frag.rgb = saturate(pin.col0.rgb);
    }
#endif

    // Alpha test on the result's 8-bit alpha against the 8-bit references.
    {
        const uint a_ref0 = uint(round(saturate(alpha_refs.x) * 255.0));
        const uint a_ref1 = uint(round(saturate(alpha_refs.y) * 255.0));
        const bool test0 = alpha_compare(comp0, frag_a8, a_ref0);
        const bool test1 = alpha_compare(comp1, frag_a8, a_ref1);
        bool alpha_pass;
        if (logic == 0u) alpha_pass = test0 && test1;
        else if (logic == 1u) alpha_pass = test0 || test1;
        else if (logic == 2u) alpha_pass = test0 != test1;
        else alpha_pass = test0 == test1;
        if (!alpha_pass) discard;
    }

    // Alpha 1 passes the alpha test but blends and writes as 0: low-alpha UI masks rely on it.
    if (frag_a8 == 1u) frag.a = 0.0;

    // Depth as a 24-bit value (the depth buffer is reversed: 1 - z); the Z texture replaces or offsets it with the last
    // stage's raw texel (unsigned 24-bit arithmetic).
    uint zcoord_i = uint(clamp((1.0 - pin.pos.z) * 16777216.0, 0.0, 16777215.0));
    if (ztex_on) {
        const uint ztex_op = (key_flags >> 3u) & 0x3u;
        const uint ztex_fmt = (key_flags >> 5u) & 0x3u;
        uint ztex_i;
        if (ztex_fmt == 0u) {
            ztex_i = uint(last_raw_texel.a & 255);
        } else if (ztex_fmt == 1u) {
            ztex_i = uint((last_raw_texel.a & 255) * 256 + (last_raw_texel.r & 255));
        } else {
            ztex_i = uint((last_raw_texel.r & 255) * 65536 + (last_raw_texel.g & 255) * 256 + (last_raw_texel.b & 255));
        }
        const uint zbias_i = uint(round(ztex_params.x * 16777216.0));
        if (ztex_op == 1u) {
            zcoord_i = (zcoord_i + ztex_i + zbias_i) & 0xFFFFFFu;
        } else {
            zcoord_i = (ztex_i + zbias_i) & 0xFFFFFFu;
        }
    }

    // Fog: fog_params[0] = (A, b magnitude, b shift, C), fog_params[1] = (colour, 1 for orthographic), on the
    // rasterized depth.
    if (fog_type != 0u) {
        const uint fog_zc = zcoord_i;
        float fog_ze;
        if (fog_params[1].w == 0.0) {
            fog_ze = (fog_params[0].x * 16777216.0)
                   / float(max(1u, uint(fog_params[0].y) - (fog_zc >> uint(fog_params[0].z))));
        } else {
            fog_ze = fog_params[0].x * float(fog_zc) / 16777216.0;
        }
        float fog_f = saturate(fog_ze - fog_params[0].w);
        if (fog_type == 4u) {
            fog_f = 1.0 - exp2(-8.0 * fog_f);
        } else if (fog_type == 5u) {
            fog_f = 1.0 - exp2(-8.0 * fog_f * fog_f);
        } else if (fog_type == 6u) {
            fog_f = exp2(-8.0 * (1.0 - fog_f));
        } else if (fog_type == 7u) {
            fog_f = 1.0 - fog_f;
            fog_f = exp2(-8.0 * fog_f * fog_f);
        }
        frag.rgb = lerp(frag.rgb, saturate(fog_params[1].rgb), fog_f);
    }

    // The result at the target's precision: 8-bit colour, 6-bit alpha. Dual-source blending reads the 8-bit alpha
    // through target 1.
    float dual_alpha = saturate(frag.a);
    uint4 target_colour = uint4(round(saturate(frag) * 255.0));
    frag.rgb = float3(target_colour.rgb) / 255.0;
    // Under ray-traced light the scene draws into the HDR scene target (sRGB-encoded past 1), and these act in linear
    // light after the combiners' 8-bit result: a light source's glow (additive, output bit 5) shines at its radiance
    // times the camera's exposure (ztex_params.z); a lit translucent (bit 6) takes the light of the surface behind it
    // (texture 7: the lighting per sample, exposed), unclamped. An opaque draw after the trace (bit 7, such as Rosalina
    // or Freezeflame's ice) takes no emitter's light.
    if ((output_flags & 0x60u) != 0u) {
        float3 lin = frag.rgb <= 0.04045 ? frag.rgb / 12.92 : pow((frag.rgb + 0.055) / 1.055, 2.4);
        if ((output_flags & 0x20u) != 0u) {
            lin *= ztex_params.z;
        }
        // The light texture is read only for a positive scale: 0 (a light source or a clear surface) takes none, and a
        // negative scale carries the table's colour for a light source shown in it.
        if ((output_flags & 0x40u) != 0u && ztex_params.w > 0.0) {
            uint lit_w, lit_h;
            tex[7].GetDimensions(lit_w, lit_h);
            // The light texture's thirds (rt_lighting.hlsl LightTexturePS): water and glass (tex_dims[7].w 2) the
            // surface behind's light, falling water (1-1.5) its own, the rest (the effects) the open air's over water.
            const uint lit_third = lit_w / 3u;
            int2 lit_at = min(int2(pin.pos.xy * ztex_params.w), int2(lit_third, lit_h) - 1);
            lit_at.x += int(lit_third * (tex_dims[7].w >= 1.5 ? 0u : tex_dims[7].w >= 1.0 ? 2u : 1u));
            lin *= (output_flags & 0x80u) != 0u ? min(max(tex[7].Load(int3(lit_at, 0)).rgb, 0.0), 1.15)
                                                : max(tex[7].Load(int3(lit_at, 0)).rgb, 0.0);
            // Falling water (tex_dims[7].w 1 + 0.4 x its aeration, map 7 being the light texture here; xyz the art's
            // clear water colour, sRGB, white where the table names none): its painted colour lies between that colour
            // and white, white water (a foam of bubbles and drops, drawn as painted) by how far towards white
            // (rt_engine.hlsl fall_foam); the rest is clear water, which passes what lies behind it but for the white
            // its bubbles scatter, its aeration, under the same light.
            if (abs(tex_dims[7].w - 1.0) < 0.5) {
                const float3 fall_c = frag.rgb <= 0.04045 ? frag.rgb / 12.92 : pow((frag.rgb + 0.055) / 1.055, 2.4);
                const float3 fall_s = saturate(tex_dims[7].xyz);
                const float3 fall_h = fall_s <= 0.04045 ? fall_s / 12.92 : pow((fall_s + 0.055) / 1.055, 2.4);
                const float3 fall_d = 1.0 - fall_h;
                const float fall_hi = max(fall_c.r, max(fall_c.g, fall_c.b));
                const float fall_foam = dot(fall_d, fall_d) > 1e-3
                                            ? saturate(dot(fall_c - fall_h, fall_d) / dot(fall_d, fall_d))
                                            : (fall_hi > 1e-6 ? min(fall_c.r, min(fall_c.g, fall_c.b)) / fall_hi : 1.0);
                const float fall_haze = saturate((tex_dims[7].w - 1.0) / 0.4);
                const float3 fall_l = max(tex[7].Load(int3(lit_at, 0)).rgb, 0.0);
                const float fall_keep = fall_foam + (1.0 - fall_foam) * fall_haze;
                if (fall_keep > 1e-4) {
                    lin = (fall_foam * lin + (1.0 - fall_foam) * fall_haze * fall_l) / fall_keep;
                }
                target_colour.a = uint(round(float(target_colour.a) * fall_keep));
                dual_alpha *= fall_keep;
            }
        }
        // At most -1 a light source shown in the table's colour (`recolour`); between -1 and 0 a clear ice or crystal
        // draw's tint, cleared from its colour inputs before the combiners.
        if (ztex_params.w <= -1.0) {
            const uint code = uint(-ztex_params.w) - 1u;
            const float3 c = float3(code & 255u, (code >> 8u) & 255u, (code >> 16u) & 255u) / 255.0;
            const float3 hue = c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
            const float peak = max(hue.r, max(hue.g, hue.b));
            lin = peak > 1e-4 ? max(lin.r, max(lin.g, lin.b)) * hue / peak : lin;
        }
        frag.rgb = lin <= 0.0031308 ? lin * 12.92 : 1.055 * pow(lin, 1.0 / 2.4) - 0.055;
    }
    if ((key_flags & 0x04u) != 0u) {
        // Constant destination alpha.
        frag.a = float(uint(round(saturate(alpha_refs.w) * 255.0)) >> 2) / 63.0;
    } else {
#if MAT_REACTIVE
        // A blended draw writing the reactive mask blends single-source at the 8-bit alpha (on an RGBA6 target in
        // place of dual-source blending: RenderCore's motion keys).
        frag.a = float(target_colour.a) / 255.0;
#else
        frag.a = float(target_colour.a >> 2) / 63.0;
#endif
    }

    PSOut o;
    o.color = frag;
#if MAT_MOTION
    float2 motion_px = float2(0.0, 0.0);
    if (pin.mv_cur.w > 0.0 && pin.mv_prev.w > 0.0) {
        motion_px = (pin.mv_prev.xy / pin.mv_prev.w - pin.mv_cur.xy / pin.mv_cur.w) * float2(pin.mv_cur.z, pin.mv_prev.z);
    }
    o.motion = motion_px;
#elif MAT_REACTIVE
    // A ray-traced scene's water and glass (lit translucent kind 2) cover nothing: the water's and clear surfaces'
    // composites take the mask as what the frame's effects cover in front of them. Falling water (kind 1) covers with
    // its white water (its alpha the white share).
    o.reactive = (output_flags & 0x40u) != 0u && tex_dims[7].w > 1.5 ? 0.0 : min(saturate(frag.a), 0.9);
#if MAT_LAYER
    o.layer = frag;
    o.trans = frag;
#endif
#elif MAT_DUAL
    o.color1 = float4(0.0, 0.0, 0.0, dual_alpha);
#endif
#if MAT_DEPTH
    o.depth = 1.0 - float(zcoord_i) / 16777216.0;
#endif
    return o;
}
