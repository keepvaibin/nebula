// SPDX-License-Identifier: GPL-3.0-only
// A draw's own TEV program at a ray hit: the GX colour and alpha combiners as the uber pixel shader runs them
// (uber_ps.hlsl, integer arithmetic, Dolphin's formulation), over the hit's texture coordinates, its vertices'
// colours and the draw's registers. Indirect textures move the stages' coordinates as the raster's do (ShaderGenerator's
// integer formulation; RtTevIndirect past the stages).
//
// The including shader declares, with RT_TEXTURED (shader model 6.6, the descriptor heap):
//   ByteAddressBuffer g_tev       per draw a 128-byte record (RtTevMaterial, rt_types.h), then the stages
//   ByteAddressBuffer g_tri_uv    per triangle kTriUvStride bytes (ray_tracing.hlsl GeometryCS)
//   SamplerState g_wrap_point, g_wrap_linear (repeating samplers)
//   float4 g_up                   the de-light maps' switch (g_up.w > 0; rt_lighting.hlsl)
//   float kDelightRange           the de-light maps' range (rt_lighting.hlsl)
// tev_combine itself reads only its record buffer and the samplers (GlowEmitterCS runs it on the glow records).

static const uint kTevRecordBytes = 128u;
static const uint kTevStageBytes = 32u;
// Per triangle: its vertices' coordinates in the draw's two texture slots (3 x float2 each), colour 0 and colour 1
// (3 x RGBA8 each).
static const uint kTriUvStride = 72u;

int4 tev_rgba8(uint c)
{
    return int4(int(c >> 24u), int((c >> 16u) & 0xFFu), int((c >> 8u) & 0xFFu), int(c & 0xFFu));
}

float4 tev_rgba8f(uint c)
{
    return float4(tev_rgba8(c)) / 255.0f;
}

int4 tev_swizzle(int4 v, uint codes)
{
    return int4(v[codes & 3u], v[(codes >> 2u) & 3u], v[(codes >> 4u) & 3u], v[(codes >> 6u) & 3u]);
}

int3 tev_u8(int3 v)
{
    return v & int3(255, 255, 255);
}

int3 tev_color_arg(uint sel, int4 prev, int4 c0, int4 c1, int4 c2, int4 texc, int4 rasc, int4 kon)
{
    switch (sel) {
    case 0u: return prev.rgb;
    case 1u: return prev.aaa;
    case 2u: return c0.rgb;
    case 3u: return c0.aaa;
    case 4u: return c1.rgb;
    case 5u: return c1.aaa;
    case 6u: return c2.rgb;
    case 7u: return c2.aaa;
    case 8u: return texc.rgb;
    case 9u: return texc.aaa;
    case 10u: return rasc.rgb;
    case 11u: return rasc.aaa;
    case 12u: return int3(255, 255, 255);
    case 13u: return int3(128, 128, 128);
    case 14u: return kon.rgb;
    default: return int3(0, 0, 0);
    }
}

int tev_alpha_arg(uint sel, int4 prev, int4 c0, int4 c1, int4 c2, int4 texc, int4 rasc, int4 kon)
{
    switch (sel) {
    case 0u: return prev.a;
    case 1u: return c0.a;
    case 2u: return c1.a;
    case 3u: return c2.a;
    case 4u: return texc.a;
    case 5u: return rasc.a;
    case 6u: return kon.a;
    default: return 0;
    }
}

// A vertex colour with its baked light removed: its hue at full brightness (black stays black).
float3 tev_debake(float3 c)
{
    const float m = max(c.r, max(c.g, c.b));
    return m > (1.0f / 255.0f) ? c / m : c;
}

int tev_fixed_konst(uint k)
{
    return k < 4u ? int(255u - 32u * k) : int(256u - 32u * k);
}

// The konstant colour selection (kcsel) and alpha selection (kasel) over K0-K3 (RGBA8 words at `record` + 24 of `tev`).
int4 tev_konst(ByteAddressBuffer tev, uint record, uint kcsel, uint kasel)
{
    int3 rgb = int3(0, 0, 0);
    if (kcsel <= 7u) {
        const int f = tev_fixed_konst(kcsel);
        rgb = int3(f, f, f);
    } else if (kcsel >= 12u && kcsel <= 15u) {
        rgb = tev_rgba8(tev.Load(record + 24u + (kcsel - 12u) * 4u)).rgb;
    } else if (kcsel >= 16u) {
        const int c = tev_rgba8(tev.Load(record + 24u + ((kcsel - 16u) % 4u) * 4u))[(kcsel - 16u) / 4u];
        rgb = int3(c, c, c);
    }
    int a = 0;
    if (kasel <= 7u) {
        a = tev_fixed_konst(kasel);
    } else if (kasel >= 16u) {
        a = tev_rgba8(tev.Load(record + 24u + ((kasel - 16u) % 4u) * 4u))[(kasel - 16u) / 4u];
    }
    return int4(rgb, a);
}

int4 tev_register(ByteAddressBuffer tev, uint record, uint r)
{
    const uint2 halves = tev.Load2(record + 40u + r * 8u);
    const float4 v = float4(f16tof32(halves.x), f16tof32(halves.x >> 16u), f16tof32(halves.y), f16tof32(halves.y >> 16u));
    return int4(round(v * 255.0f));
}

bool tev_alpha_compare(uint comp, int value, int reference)
{
    switch (comp) {
    case 0u: return false;
    case 1u: return value < reference;
    case 2u: return value == reference;
    case 3u: return value <= reference;
    case 4u: return value > reference;
    case 5u: return value != reference;
    case 6u: return value >= reference;
    default: return true;
    }
}

static const uint kTevGreyTexture = 1u;   // tev_evaluate_mode: every texture a mid grey (128)
static const uint kTevBakedColours = 2u;  // tev_evaluate_mode: the vertices' colours as the game baked them
static const uint kTevNoEnvironment = 4u; // tev_evaluate_mode: its environment maps (painted reflections) black
static const uint kTevWhiteEnvironment = 8u;  // tev_evaluate_mode: its environment maps white (its reflectance)
// tev_evaluate_cone without a footprint: every texture's level-0 texel at the coordinates (the traced hits).
static const float kTevNoCone = -1.0e30f;

// A texture coordinate under GX addressing `mode` (0 clamp, 1 repeat, 2 mirror) for a repeating sampler: clamped and
// mirrored coordinates kept `half` (half a texel of the level sampled) inside, so the filter never reaches the far edge.
float tev_address(float u, uint mode, float half)
{
    if (mode == 1u) {
        return u;
    }
    if (mode == 2u) {
        u = 1.0f - abs(frac(u * 0.5f) * 2.0f - 1.0f);
    }
    return clamp(u, half, 1.0f - half);
}

float2 tev_addressed(float2 uv, uint wrap, float2 half)
{
    return float2(tev_address(uv.x, wrap & 3u, half.x), tev_address(uv.y, (wrap >> 2u) & 3u, half.y));
}

// Texture `map` at `coords` (a stage's, under its addressing `wrap`): with a footprint (`cone`: log2 of the pixel's
// width in eye units over its triangle's square-root area, as tev_cone gives it) filtered at the level the footprint
// covers there, the coordinates changing by `d1`, `d2` along the triangle's edges, as the raster samples it; without
// (the traced hits), its level-0 texel, repeating. The hits keep repeating: under their stages' clamping the Good Egg's
// bounce light lost 8%, so some hit coordinates lie outside the texture where the raster's do not (an open question
// for the ray tracing review).
float4 tev_sample(Texture2D<float4> map, float2 coords, uint wrap, float2 d1, float2 d2, float cone)
{
    if (cone <= kTevNoCone) {
        return map.SampleLevel(g_wrap_point, coords, 0.0f);
    }
    uint width, height, levels;
    map.GetDimensions(0u, width, height, levels);
    const float2 size = float2(width, height);
    const float det = abs(d1.x * d2.y - d2.x * d1.y) * size.x * size.y;
    const float lod = clamp(0.5f * log2(max(det, 1e-30f)) + cone, 0.0f, float(levels - 1u));
    return map.SampleLevel(g_wrap_linear, tev_addressed(coords, wrap, 0.5f * exp2(lod) / size), lod);
}

// A de-light map at a stage's coordinates (as tev_sample).
float tev_sample_shading(Texture2D<float> map, float2 coords, uint wrap, float2 d1, float2 d2, float cone)
{
    if (cone <= kTevNoCone) {
        return map.SampleLevel(g_wrap_point, coords, 0.0f);
    }
    uint width, height, levels;
    map.GetDimensions(0u, width, height, levels);
    const float2 size = float2(width, height);
    const float det = abs(d1.x * d2.y - d2.x * d1.y) * size.x * size.y;
    const float lod = clamp(0.5f * log2(max(det, 1e-30f)) + cone, 0.0f, float(levels - 1u));
    return map.SampleLevel(g_wrap_linear, tev_addressed(coords, wrap, 0.5f * exp2(lod) / size), lod);
}

// A point of a draw for tev_combine: its coordinates in both texture slots and their change along its triangle's edges
// (tev_sample's footprint), and its rasterised colours 0 and 1 (the vertices', where the record takes them).
struct TevPoint {
    float2 uv[2];
    float2 edge1[2];
    float2 edge2[2];
    float4 vertex0;
    float4 vertex1;
};

// RtTevIndirect (rt_types.h), at `ind_at` past a record's stages: byte offsets of its parts.
static const uint kTevIndScale = 4u;
static const uint kTevIndMatrix = 36u;
static const uint kTevIndCoordSize = 84u;

// An indirect coordinate's GX wrap (ShaderGenerator indirect_wrap_mask): 1/128 texels within 256 >> (wrap - 1) texels;
// 6 and 7 zero.
int tev_ind_wrap(int c, uint wrap)
{
    return wrap == 0u ? c : wrap >= 6u ? 0 : c & int((256u >> (wrap - 1u)) * 128u - 1u);
}

// An indirect matrix row at `at`: x, y, z and its shift (int16 pairs).
int4 tev_ind_row(ByteAddressBuffer tev, uint at)
{
    const uint2 w = tev.Load2(at);
    return int4(asint(w.x << 16u) >> 16, asint(w.x) >> 16, asint(w.y << 16u) >> 16, asint(w.y) >> 16);
}

// A stage's indirect coordinate op (ShaderGenerator): its coordinates `stage_coord` (1/128 texels of their GX size)
// moved by its indirect stage's texel through an indirect matrix, wrapped, and added to the previous stage's `tevcoord`
// or not; its bump alpha (`alphabump`). `count` indirect stages; their texels `ind_tex`.
int2 tev_indirect_coord(ByteAddressBuffer tev, uint ind_at, uint ind_cmd, int2 stage_coord, int3 ind_tex[4], uint count,
                        int2 tevcoord, inout int alphabump)
{
    int2 trans = int2(0, 0);
    const uint bt = ind_cmd & 3u;
    if (ind_cmd != 0u && bt < count) {
        const uint fmt = (ind_cmd >> 2u) & 3u;
        const uint bias = (ind_cmd >> 4u) & 7u;
        const uint bs = (ind_cmd >> 7u) & 3u;
        const uint mtx = (ind_cmd >> 9u) & 3u;
        const uint mtx_id = (ind_cmd >> 11u) & 3u;
        const int3 texel = ind_tex[bt];
        if (bs != 0u) {
            const int c = bs == 1u ? texel.x : bs == 2u ? texel.y : texel.z;
            alphabump = (c << (fmt == 0u ? 0u : 6u - fmt)) & 248;
        }
        int3 crd = texel >> (fmt == 0u ? 0u : fmt + 2u);
        const int add = fmt == 0u ? -128 : 1;
        crd += int3((bias & 1u) != 0u ? add : 0, (bias & 2u) != 0u ? add : 0, (bias & 4u) != 0u ? add : 0);
        if (mtx != 0u) {
            const uint row = ind_at + kTevIndMatrix + (mtx - 1u) * 16u;
            const int4 s_row = tev_ind_row(tev, row);
            const int4 t_row = tev_ind_row(tev, row + 8u);
            if (mtx_id == 0u) {
                trans = int2(s_row.x * crd.x + s_row.y * crd.y + s_row.z * crd.z,
                             t_row.x * crd.x + t_row.y * crd.y + t_row.z * crd.z) >> 3;
            } else if (mtx_id == 1u) {
                trans = (stage_coord * crd.xx) >> 8;
            } else if (mtx_id == 2u) {
                trans = (stage_coord * crd.yy) >> 8;
            }
            trans = s_row.w >= 0 ? trans >> s_row.w : trans << (-s_row.w);
        }
    }
    const int2 wrapped = int2(tev_ind_wrap(stage_coord.x, (ind_cmd >> 13u) & 7u),
                              tev_ind_wrap(stage_coord.y, (ind_cmd >> 16u) & 7u));
    const int2 c = ((ind_cmd >> 20u) & 1u) != 0u ? tevcoord + wrapped + trans : wrapped + trans;
    return (c << 8) >> 8;
}

#if RT_TEXTURED
// The indirect stages' texels at point `pt` (s, t, u from alpha, blue, green, as the raster reads them; record
// `record`, its RtTevIndirect at `ind_at`), filtered over `cone` as tev_sample; `count` returns their number.
void tev_indirect_maps(ByteAddressBuffer tev, uint record, uint header, uint ind_at, TevPoint pt, float cone,
                       out int3 ind_tex[4], out uint count)
{
    ind_tex[0] = int3(0, 0, 0);
    ind_tex[1] = int3(0, 0, 0);
    ind_tex[2] = int3(0, 0, 0);
    ind_tex[3] = int3(0, 0, 0);
    const uint order = tev.Load(ind_at);
    count = min((order >> 24u) & 7u, 4u);
    const uint texgens = header >> 28u;
    [loop] for (uint i = 0u; i < count; ++i) {
        const uint map = (order >> (6u * i)) & 7u;
        const uint coord_raw = (order >> (6u * i + 3u)) & 7u;
        const uint slot = (header >> (8u + 2u * (coord_raw < texgens ? coord_raw : 0u))) & 1u;
        const float2 scale = asfloat(tev.Load2(ind_at + kTevIndScale + i * 8u));
        const uint view = tev.Load(record + 72u + map * 4u);
        const float2 coords = texgens != 0u ? pt.uv[slot] * scale : float2(0.0f, 0.0f);
        if (view != 0xFFFFFFFFu && all(abs(coords) < 1.0e4f)) {
            const Texture2D<float4> ind_map = ResourceDescriptorHeap[NonUniformResourceIndex(view)];
            // Repeating (GX wrap 1 on both axes).
            const float4 texel = tev_sample(ind_map, coords, 5u, pt.edge1[slot] * scale, pt.edge2[slot] * scale, cone);
            ind_tex[i] = int3(round(saturate(texel.abg) * 255.0f));
        }
    }
}
#endif

// The TEV output of the draw whose record starts at `record` in `tev` (RtTevMaterial, then its stages) at point `pt`
// (display-encoded RGBA, 0-1), and whether its alpha passes the draw's alpha compare. `mode`: the kTev* flags above.
// `cone`: the footprint its textures are filtered over (tev_sample; kTevNoCone: none). `delight`: the material
// texture's painted light taken out by its de-light map (the record's pad[1]).
float4 tev_combine(ByteAddressBuffer tev, uint record, TevPoint pt, uint mode, float cone, bool delight,
                   out bool alpha_pass)
{
    const uint header = tev.Load(record);
    const uint stages = min(header & 0x1Fu, 16u);
    const uint stage_base = tev.Load(record + 104u);
    const uint4 channels = tev.Load4(record + 8u);   // colour 0 material, its light, colour 1 material, its light
    float4 ras0 = tev_rgba8f(channels.x);
    float4 ras1 = tev_rgba8f(channels.z);
    if ((header & (1u << 24u)) != 0u) ras0.rgb = pt.vertex0.rgb;
    if ((header & (1u << 25u)) != 0u) ras0.a = pt.vertex0.a;
    if ((header & (1u << 26u)) != 0u) ras1.rgb = pt.vertex1.rgb;
    if ((header & (1u << 27u)) != 0u) ras1.a = pt.vertex1.a;
    const int4 ras_in0 = int4(round(saturate(ras0 * tev_rgba8f(channels.y)) * 255.0f));
    const int4 ras_in1 = int4(round(saturate(ras1 * tev_rgba8f(channels.w)) * 255.0f));

    int4 prev = tev_register(tev, record, 0u);
    int4 c0 = tev_register(tev, record, 1u);
    int4 c1 = tev_register(tev, record, 2u);
    int4 c2 = tev_register(tev, record, 3u);
    int4 texc = int4(0, 0, 0, 0);
    int4 rasc = int4(0, 0, 0, 0);
    // Indirect texturing (RtTevStage.pad[1] bit 31): its texels read at the first stage, the coordinates carried from
    // stage to stage, the bump alpha.
    const uint ind_at = stage_base + stages * kTevStageBytes;
    int3 ind_tex[4] = {int3(0, 0, 0), int3(0, 0, 0), int3(0, 0, 0), int3(0, 0, 0)};
    uint ind_count = 0u;
    int2 tevcoord = int2(0, 0);
    int alphabump = 0;
    [loop] for (uint s = 0u; s < stages; ++s) {
        const uint at = stage_base + s * kTevStageBytes;
        const uint4 packed = tev.Load4(at);
        const float2 scale = asfloat(tev.Load2(at + 16u));
        const uint color_env = packed.x;
        const uint alpha_env = packed.y;
        const uint order = packed.z;
        const uint ksel = packed.w;
        const uint texmap = order & 7u;
        const uint texcoord = (order >> 3u) & 7u;
        const bool tex_enable = ((order >> 6u) & 1u) != 0u;
        const uint ras_channel = (order >> 7u) & 7u;
        const uint tex_swizzle = (ksel >> 18u) & 0xFFu;
        const uint ras_swizzle = (ksel >> 10u) & 0xFFu;

        // As the raster: a stage without coordinates (the draw has no texture generators) reads zero, a disabled one
        // white.
        const uint texgens = header >> 28u;
        const uint stage_word = tev.Load(at + 28u);
        const uint wrap = stage_word & 0xFu;
        const uint ind_cmd = (stage_word >> 8u) & 0x1FFFFFu;
        const uint slot = (header >> (8u + 2u * texcoord)) & 1u;
#if RT_TEXTURED
        // Indirect texturing: every stage carries the coordinates (an add-previous stage reads the one before's).
        if ((stage_word >> 31u) != 0u) {
            if (s == 0u) {
                tev_indirect_maps(tev, record, header, ind_at, pt, cone, ind_tex, ind_count);
            }
            int2 stage_coord = int2(0, 0);
            if (texgens != 0u) {
                const uint size = tev.Load(ind_at + kTevIndCoordSize + (texcoord < texgens ? texcoord : 0u) * 4u);
                stage_coord = int2(round(pt.uv[slot] * float2((size & 0xFFFFu) + 1u, (size >> 16u) + 1u) * 128.0f));
            }
            tevcoord = tev_indirect_coord(tev, ind_at, ind_cmd, stage_coord, ind_tex, ind_count, tevcoord, alphabump);
        }
#endif
        if (tex_enable && texgens != 0u) {
            texc = int4(255, 255, 255, 255);
#if RT_TEXTURED
            const uint view = tev.Load(record + 72u + texmap * 4u);
            float2 coords = pt.uv[slot] * scale;
            const float2 d1 = pt.edge1[slot] * scale;
            const float2 d2 = pt.edge2[slot] * scale;
            if (view != 0xFFFFFFFFu && all(abs(coords) < 1.0e4f)) {
                const Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(view)];
                // A stage with an indirect command samples at the moved coordinates (1/128 texels of the map).
                if (ind_cmd != 0u && (stage_word >> 31u) != 0u) {
                    uint map_w, map_h, map_levels;
                    map.GetDimensions(0u, map_w, map_h, map_levels);
                    coords = float2(tevcoord) / (128.0f * float2(max(map_w, 1u), max(map_h, 1u)));
                }
                float4 texel = saturate(tev_sample(map, coords, wrap, d1, d2, cone));
                // Physical: the material texture's albedo, the light painted into it taken out (its de-light map,
                // record pad[1]; rt_delight.hlsl).
                const uint painted_view = tev.Load(record + 112u);
                if (painted_view != 0xFFFFFFFFu && delight && texmap == (tev.Load(record + 116u) & 7u)) {
                    const Texture2D<float> painted = ResourceDescriptorHeap[NonUniformResourceIndex(painted_view)];
                    float3 lin = select(texel.rgb <= 0.04045f, texel.rgb / 12.92f,
                                        pow((texel.rgb + 0.055f) / 1.055f, 2.4f)) *
                                 exp(-tev_sample_shading(painted, coords, wrap, d1, d2, cone) * kDelightRange);
                    // Past 1 it keeps its hue (not each channel clipped).
                    lin /= max(max(lin.r, max(lin.g, lin.b)), 1.0f);
                    texel.rgb = select(lin <= 0.0031308f, lin * 12.92f, 1.055f * pow(saturate(lin), 1.0f / 2.4f) - 0.055f);
                }
                texc = tev_swizzle(int4(round(saturate(texel) * 255.0f)), tex_swizzle);
            }
#endif
            if ((mode & kTevGreyTexture) != 0u) {
                texc = int4(128, 128, 128, 255);
            }
            if ((mode & kTevNoEnvironment) != 0u && tev.Load(at + 24u) != 0u) {
                texc = int4(0, 0, 0, texc.a);
            }
            if ((mode & kTevWhiteEnvironment) != 0u && tev.Load(at + 24u) != 0u) {
                texc = int4(255, 255, 255, texc.a);
            }
        } else {
            texc = texgens != 0u ? int4(255, 255, 255, 255) : int4(0, 0, 0, 0);
        }
        // Channels 5 and 6: the bump alpha, and it normalised (libogc's GX_ALPHA_BUMP, GX_ALPHA_BUMPN).
        const int bump_n = alphabump | (alphabump >> 5);
        rasc = ras_channel == 0u ? tev_swizzle(ras_in0, ras_swizzle)
               : ras_channel == 1u ? tev_swizzle(ras_in1, ras_swizzle)
               : ras_channel == 5u ? tev_swizzle(int4(alphabump, alphabump, alphabump, alphabump), ras_swizzle)
               : ras_channel == 6u ? tev_swizzle(int4(bump_n, bump_n, bump_n, bump_n), ras_swizzle)
                                   : int4(0, 0, 0, 0);
        const int4 kon = tev_konst(tev, record, ksel & 0x1Fu, (ksel >> 5u) & 0x1Fu);

        const uint color_bias = (color_env >> 16u) & 3u;
        const bool color_sub = ((color_env >> 18u) & 1u) != 0u;
        const uint color_scale = (color_env >> 20u) & 3u;
        const int3 ca = tev_color_arg((color_env >> 12u) & 0xFu, prev, c0, c1, c2, texc, rasc, kon);
        const int3 cb = tev_color_arg((color_env >> 8u) & 0xFu, prev, c0, c1, c2, texc, rasc, kon);
        const int3 cc = tev_color_arg((color_env >> 4u) & 0xFu, prev, c0, c1, c2, texc, rasc, kon);
        const int3 cd = tev_color_arg(color_env & 0xFu, prev, c0, c1, c2, texc, rasc, kon);
        const int3 cmp_a = tev_u8(ca);
        const int3 cmp_b = tev_u8(cb);
        int3 color_out;
        if (color_bias == 3u) {
            const int3 ci = tev_u8(cc);
            int3 cmp;
            if (color_scale == 0u) {
                cmp = (color_sub ? cmp_a.x == cmp_b.x : cmp_a.x > cmp_b.x) ? ci : int3(0, 0, 0);
            } else if (color_scale == 1u) {
                const int av = cmp_a.x + (cmp_a.y << 8);
                const int bv = cmp_b.x + (cmp_b.y << 8);
                cmp = (color_sub ? av == bv : av > bv) ? ci : int3(0, 0, 0);
            } else if (color_scale == 2u) {
                const int av = cmp_a.x + (cmp_a.y << 8) + (cmp_a.z << 16);
                const int bv = cmp_b.x + (cmp_b.y << 8) + (cmp_b.z << 16);
                cmp = (color_sub ? av == bv : av > bv) ? ci : int3(0, 0, 0);
            } else {
                cmp = color_sub ? (int3(1, 1, 1) - sign(abs(cmp_a - cmp_b))) * ci : max(sign(cmp_a - cmp_b), int3(0, 0, 0)) * ci;
            }
            color_out = cd + cmp;
        } else {
            const int3 ai = cmp_a;
            const int3 bi = cmp_b;
            const int3 ci = tev_u8(cc);
            const uint left = color_scale == 1u ? 1u : color_scale == 2u ? 2u : 0u;
            const int3 lhs = (cd + (color_bias == 1u ? 128 : color_bias == 2u ? -128 : 0)) << left;
            int3 mixv = ((ai << 8) + (bi - ai) * (ci + (ci >> 7))) << left;
            mixv = color_scale != 3u ? (mixv + (color_sub ? 127 : 128)) >> 8 : mixv >> 8;
            color_out = color_sub ? lhs - mixv : lhs + mixv;
            if (color_scale == 3u) {
                color_out = color_out >> 1;
            }
        }
        color_out = ((color_env >> 19u) & 1u) != 0u ? clamp(color_out, 0, 255) : clamp(color_out, -1024, 1023);

        const uint alpha_bias = (alpha_env >> 16u) & 3u;
        const bool alpha_sub = ((alpha_env >> 18u) & 1u) != 0u;
        const uint alpha_scale = (alpha_env >> 20u) & 3u;
        const int aa = tev_alpha_arg((alpha_env >> 13u) & 7u, prev, c0, c1, c2, texc, rasc, kon);
        const int ab = tev_alpha_arg((alpha_env >> 10u) & 7u, prev, c0, c1, c2, texc, rasc, kon);
        const int ac = tev_alpha_arg((alpha_env >> 7u) & 7u, prev, c0, c1, c2, texc, rasc, kon);
        const int ad = tev_alpha_arg((alpha_env >> 4u) & 7u, prev, c0, c1, c2, texc, rasc, kon);
        int alpha_out;
        if (alpha_bias == 3u) {
            bool cmp;
            if (alpha_scale == 3u) {
                cmp = alpha_sub ? (aa & 255) == (ab & 255) : (aa & 255) > (ab & 255);
            } else if (alpha_scale == 0u) {
                cmp = alpha_sub ? cmp_a.x == cmp_b.x : cmp_a.x > cmp_b.x;
            } else if (alpha_scale == 1u) {
                const int av = cmp_a.x + (cmp_a.y << 8);
                const int bv = cmp_b.x + (cmp_b.y << 8);
                cmp = alpha_sub ? av == bv : av > bv;
            } else {
                const int av = cmp_a.x + (cmp_a.y << 8) + (cmp_a.z << 16);
                const int bv = cmp_b.x + (cmp_b.y << 8) + (cmp_b.z << 16);
                cmp = alpha_sub ? av == bv : av > bv;
            }
            alpha_out = ad + (cmp ? (ac & 255) : 0);
        } else {
            const int ai = aa & 255;
            const int bi = ab & 255;
            const int ci = ac & 255;
            const uint left = alpha_scale == 1u ? 1u : alpha_scale == 2u ? 2u : 0u;
            const int lhs = (ad + (alpha_bias == 1u ? 128 : alpha_bias == 2u ? -128 : 0)) << left;
            int mixv = ((ai << 8) + (bi - ai) * (ci + (ci >> 7))) << left;
            mixv = alpha_scale != 3u ? (mixv + (alpha_sub ? 127 : 128)) >> 8 : mixv >> 8;
            alpha_out = alpha_sub ? lhs - mixv : lhs + mixv;
            if (alpha_scale == 3u) {
                alpha_out = alpha_out >> 1;
            }
        }
        alpha_out = ((alpha_env >> 19u) & 1u) != 0u ? clamp(alpha_out, 0, 255) : clamp(alpha_out, -1024, 1023);

        switch ((color_env >> 22u) & 3u) {
        case 0u: prev.rgb = color_out; break;
        case 1u: c0.rgb = color_out; break;
        case 2u: c1.rgb = color_out; break;
        default: c2.rgb = color_out; break;
        }
        switch ((alpha_env >> 22u) & 3u) {
        case 0u: prev.a = alpha_out; break;
        case 1u: c0.a = alpha_out; break;
        case 2u: c1.a = alpha_out; break;
        default: c2.a = alpha_out; break;
        }
        // The last stage's destination reaches the pixel engine.
        if (s + 1u == stages) {
            const uint color_dest = (color_env >> 22u) & 3u;
            const uint alpha_dest = (alpha_env >> 22u) & 3u;
            prev.rgb = color_dest == 1u ? c0.rgb : color_dest == 2u ? c1.rgb : color_dest == 3u ? c2.rgb : prev.rgb;
            prev.a = alpha_dest == 1u ? c0.a : alpha_dest == 2u ? c1.a : alpha_dest == 3u ? c2.a : prev.a;
        }
    }
    const int4 fragment = prev & int4(255, 255, 255, 255);
    // The alpha compare (ref0 | ref1 << 8 | comp0 << 16 | comp1 << 19 | logic << 22).
    const uint test = tev.Load(record + 4u);
    const bool test0 = tev_alpha_compare((test >> 16u) & 7u, fragment.a, int(test & 0xFFu));
    const bool test1 = tev_alpha_compare((test >> 19u) & 7u, fragment.a, int((test >> 8u) & 0xFFu));
    const uint logic = (test >> 22u) & 3u;
    alpha_pass = logic == 0u ? test0 && test1 : logic == 1u ? test0 || test1 : logic == 2u ? test0 != test1 : test0 == test1;
    return float4(fragment) / 255.0f;
}

// Draw `draw`'s TEV output at triangle `tri`, barycentrics `b` (display-encoded RGBA, 0-1), and whether its alpha
// passes the draw's alpha compare. A draw without a record (`count` records) is white and passes. `mode`: the
// kTev* flags above. `cone`: the footprint its textures are filtered over (tev_sample; kTevNoCone: none).
float4 tev_evaluate_cone(uint draw, uint count, uint tri, float2 b, uint mode, float cone, out bool alpha_pass)
{
    alpha_pass = true;
    if (draw >= count) {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    const uint record = draw * kTevRecordBytes;
    const uint header = g_tev.Load(record);
    // The hit's coordinates in both slots and its rasterised colours (channel material x its light).
    const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
    const uint a = tri * kTriUvStride;
    const float4 uv0_01 = asfloat(g_tri_uv.Load4(a));
    const float2 uv0_2 = asfloat(g_tri_uv.Load2(a + 16u));
    const float4 uv1_01 = asfloat(g_tri_uv.Load4(a + 24u));
    const float2 uv1_2 = asfloat(g_tri_uv.Load2(a + 40u));
    const uint3 col0 = g_tri_uv.Load3(a + 48u);
    const uint3 col1 = g_tri_uv.Load3(a + 60u);
    TevPoint pt;
    pt.uv[0] = uv0_01.xy * w.x + uv0_01.zw * w.y + uv0_2 * w.z;
    pt.uv[1] = uv1_01.xy * w.x + uv1_01.zw * w.y + uv1_2 * w.z;
    pt.edge1[0] = uv0_01.zw - uv0_01.xy;
    pt.edge1[1] = uv1_01.zw - uv1_01.xy;
    pt.edge2[0] = uv0_2 - uv0_01.xy;
    pt.edge2[1] = uv1_2 - uv1_01.xy;
    float4 v0[3] = {tev_rgba8f(col0.x), tev_rgba8f(col0.y), tev_rgba8f(col0.z)};
    float4 v1[3] = {tev_rgba8f(col1.x), tev_rgba8f(col1.y), tev_rgba8f(col1.z)};
    // Bits 5-6: the baked light of the vertices' colour removed per vertex and set to the program's neutral light
    // (record + 108: the levels), as the vertex shaders do.
    // Bits 16-17: a lit channel lit at its neutral light (the record's light word), its vertex colour its material.
    const uint levels = g_tev.Load(record + 108u);
    const bool baked = (mode & kTevBakedColours) != 0u;
    [unroll] for (uint k = 0u; k < 3u; ++k) {
        if ((header & 0x20u) != 0u && !baked) v0[k].rgb = tev_debake(v0[k].rgb) * (float(levels & 0xFFu) / 255.0f);
        if ((header & 0x40u) != 0u && !baked) v1[k].rgb = tev_debake(v1[k].rgb) * (float((levels >> 8u) & 0xFFu) / 255.0f);
    }
    pt.vertex0 = v0[0] * w.x + v0[1] * w.y + v0[2] * w.z;
    pt.vertex1 = v1[0] * w.x + v1[1] * w.y + v1[2] * w.z;
    return tev_combine(g_tev, record, pt, mode, cone, g_up.w > 0.0f, alpha_pass);
}

// The texture coordinate the table's glowing region is tested at (tev_glows_at), in the region's 0-1 space.
float2 tev_region_uv(uint draw, uint count, uint tri, float2 b)
{
    if (draw >= count) {
        return float2(-1.0f, -1.0f);
    }
    const uint record = draw * kTevRecordBytes;
    const uint info = g_tev.Load(record + 116u);
    const uint a = tri * kTriUvStride + ((info >> 3u) & 1u) * 24u;
    const float4 uv01 = asfloat(g_tri_uv.Load4(a));
    const float2 uv2 = asfloat(g_tri_uv.Load2(a + 16u));
    return frac((uv01.xy * (1.0f - b.x - b.y) + uv01.zw * b.x + uv2 * b.y) * asfloat(g_tev.Load2(record + 120u)));
}

// Whether triangle `tri` at barycentrics `b` lies in its emitter's glowing region of the material texture (record
// pad[2] bits 8-31: u0, v0, u1, v1 in 63rds; 0: all of it), at its coordinates there wrapped into the texture.
bool tev_glows_at(uint draw, uint count, uint tri, float2 b)
{
    if (draw >= count) {
        return true;
    }
    const uint record = draw * kTevRecordBytes;
    const uint info = g_tev.Load(record + 116u);
    const uint region = info >> 8u;
    if (region == 0u) {
        return true;
    }
    const float4 box = float4(float(region & 63u), float((region >> 6u) & 63u), float((region >> 12u) & 63u),
                              float((region >> 18u) & 63u)) / 63.0f;
    const uint a = tri * kTriUvStride + ((info >> 3u) & 1u) * 24u;
    const float4 uv01 = asfloat(g_tri_uv.Load4(a));
    const float2 uv2 = asfloat(g_tri_uv.Load2(a + 16u));
    const float2 uv = frac((uv01.xy * (1.0f - b.x - b.y) + uv01.zw * b.x + uv2 * b.y) * asfloat(g_tev.Load2(record + 120u)));
    return all(uv >= box.xy) && all(uv <= box.zw);
}

float4 tev_evaluate_mode(uint draw, uint count, uint tri, float2 b, uint mode, out bool alpha_pass)
{
    return tev_evaluate_cone(draw, count, tri, b, mode, kTevNoCone, alpha_pass);
}

float4 tev_evaluate(uint draw, uint count, uint tri, float2 b, out bool alpha_pass)
{
    return tev_evaluate_mode(draw, count, tri, b, 0u, alpha_pass);
}
