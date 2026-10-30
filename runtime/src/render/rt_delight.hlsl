// SPDX-License-Identifier: GPL-3.0-only
// Texture de-lighting for the physical ray-traced lighting (GALAXY_RT_PHYSICAL): the light painted into a texture
// (bevel highlights, groove and corner shadows, the sheen on a metal band) split from its colour, so the traced sun and
// sky light the surface in its place. Ray-traced lighting only: the game's own picture keeps its textures.
//
// Method: Retinex (Land & McCann 1971) with chromaticity as the reflectance cue (Funt, Drew & Brockington 1992;
// Grosse et al. 2009). Shading scales a texel's brightness and keeps its chromaticity; a change of material changes
// its chromaticity, or its brightness by more than shading does. A texel's level is the mean log luminance of the
// texels around it (a Gaussian of 12% of the texture's size) weighted by likeness to it in chromaticity (kTauChroma)
// and log luminance (kTauLog: a step past about 4x is paint, not light): the base layer of a bilateral filter (Durand
// & Dorsey 2002). Its shading is its log luminance less that level, its albedo the texel over exp(shading).
// Limits: brightness changes at one chromaticity (wood grain, a dark outline on a same-coloured face) count as
// shading, as in every Retinex; the composite turns them into relief, not into flat colour.
//
// The map holds the shading in log units as R8_SNORM over +-kDelightRange with a mip chain of its means.
// DelightCS writes level 0 to the scratch buffer, four texels per thread (one 32-bit word); DelightMipCS each further
// level as the mean of the 2 x 2 above. The ray tracer copies the levels into the map (SceneRayTracer::run_delight).

static const float kDelightRange = 1.25f;   // log units at +-127
static const float kTauChroma = 0.05f;      // rg chromaticity
static const float kTauLog = 0.8f;          // log luminance

cbuffer RtDelight : register(b0)
{
    uint d_source;        // DelightCS: the texture's view in the descriptor heap
    uint2 d_size;         // DelightCS: its level 0
    uint d_source_levels; // DelightCS: its levels
    uint d_out;           // the level written: its first byte in the scratch buffer, its row pitch, its size
    uint d_out_pitch;
    uint2 d_out_size;
    uint d_in;            // DelightMipCS: the level above
    uint d_in_pitch;
    uint2 d_in_size;
    uint d_wrap;          // the texture's addressing as the draw that made the map sets it (GX: 0 clamp, 1 repeat,
                          // 2 mirror; s bits 0-1, t bits 2-3); DelightCS: the share taken out (bits 8-15, of 255)
};
RWByteAddressBuffer d_scratch : register(u0);

struct DelightTexel {
    float log_l;
    float2 chroma;
    bool valid;
};

DelightTexel delight_texel(float4 c)
{
    const float3 e = saturate(c.rgb);
    const float3 lin = select(e <= 0.04045f, e / 12.92f, pow((e + 0.055f) / 1.055f, 2.4f));
    DelightTexel t;
    t.log_l = log(dot(lin, float3(0.2126f, 0.7152f, 0.0722f)) + 1e-3f);
    t.chroma = lin.rg / (lin.r + lin.g + lin.b + 3e-3f);
    t.valid = c.a >= 0.5f;
    return t;
}

// Texel `x` of an axis `n` texels long under GX addressing `mode`.
int delight_address(int x, int n, uint mode)
{
    if (mode == 1u) {
        return (x % n + n) % n;
    }
    if (mode == 2u) {
        const int m = (x % (2 * n) + 2 * n) % (2 * n);
        return m < n ? m : 2 * n - 1 - m;
    }
    return clamp(x, 0, n - 1);
}

uint delight_byte(float s)
{
    return uint(int(round(clamp(s, -127.0f, 127.0f)))) & 0xFFu;
}

[numthreads(8, 8, 1)]
void DelightCS(uint3 id : SV_DispatchThreadID)
{
    const uint words = (d_size.x + 3u) / 4u;
    if (id.x >= words || id.y >= d_size.y) {
        return;
    }
    const Texture2D<float4> source = ResourceDescriptorHeap[d_source];
    const float sigma = max(1.5f, 0.12f * float(max(d_size.x, d_size.y)));
    // Taps every sigma / 4 texels out to 2 sigma, read from the level whose texels are that wide (its means).
    const uint stride = max(1u, uint(sigma * 0.25f));
    const uint mip = min(firstbithigh(stride), d_source_levels - 1u);
    const int reach = int(ceil(2.0f * sigma / float(stride)));
    const float inv_s = 1.0f / (2.0f * sigma * sigma);
    const float inv_c = 1.0f / (2.0f * kTauChroma * kTauChroma);
    const float inv_l = 1.0f / (2.0f * kTauLog * kTauLog);
    const int2 size = int2(d_size);
    uint word = 0u;
    [loop] for (uint k = 0u; k < 4u; ++k) {
        const uint x = id.x * 4u + k;
        float s = 0.0f;
        if (x < d_size.x) {
            const DelightTexel centre = delight_texel(source.Load(int3(x, id.y, 0)));
            if (centre.valid) {
                float sum = 0.0f;
                float weight = 0.0f;
                [loop] for (int j = -reach; j <= reach; ++j) {
                    [loop] for (int i = -reach; i <= reach; ++i) {
                        const int2 o = int2(i, j) * int(stride);
                        const int2 p = int2(int(x), int(id.y)) + o;
                        const int2 at = int2(delight_address(p.x, size.x, d_wrap & 3u),
                                             delight_address(p.y, size.y, (d_wrap >> 2u) & 3u));
                        const DelightTexel t = delight_texel(source.Load(int3(at >> mip, mip)));
                        if (!t.valid) {
                            continue;
                        }
                        const float2 dc = t.chroma - centre.chroma;
                        const float dl = t.log_l - centre.log_l;
                        const float w = exp(-float(dot(o, o)) * inv_s - dot(dc, dc) * inv_c - dl * dl * inv_l);
                        sum += w * t.log_l;
                        weight += w;
                    }
                }
                if (weight > 1e-6f) {
                    s = (centre.log_l - sum / weight) * (127.0f / kDelightRange) *
                        (float((d_wrap >> 8u) & 0xFFu) / 255.0f);
                }
            }
        }
        word |= delight_byte(s) << (8u * k);
    }
    d_scratch.Store(d_out + id.y * d_out_pitch + id.x * 4u, word);
}

float delight_read(uint2 p)
{
    const uint at = d_in + p.y * d_in_pitch + p.x;
    const uint word = d_scratch.Load(at & ~3u);
    return float(int(word << (24u - 8u * (at & 3u))) >> 24);
}

[numthreads(8, 8, 1)]
void DelightMipCS(uint3 id : SV_DispatchThreadID)
{
    const uint words = (d_out_size.x + 3u) / 4u;
    if (id.x >= words || id.y >= d_out_size.y) {
        return;
    }
    uint word = 0u;
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const uint x = id.x * 4u + k;
        if (x < d_out_size.x) {
            const uint2 a = min(uint2(x, id.y) * 2u, d_in_size - 1u);
            const uint2 b = min(a + 1u, d_in_size - 1u);
            const float mean = 0.25f * (delight_read(a) + delight_read(uint2(b.x, a.y)) + delight_read(uint2(a.x, b.y)) +
                                        delight_read(b));
            word |= delight_byte(mean) << (8u * k);
        }
    }
    d_scratch.Store(d_out + id.y * d_out_pitch + id.x * 4u, word);
}
