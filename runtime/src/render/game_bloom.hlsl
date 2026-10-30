// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom at the internal resolution (game_bloom.h): BloomEffect's and BloomEffectSimple's chains as one
// operator on the EFB, with the game's own thresholds, blur radii and intensities.
//
// DownCS: the source's region in boxes of c_size.z x c_size.z texels (each texel clamped to 1, as the 8-bit EFB keeps
// it) into u0.
// BoxCS: a box of c_params.x texels along axis c_size.w (the game's 4 x 4 pixel workspace texel at the internal
// resolution); with c_params.y >= 0 the selection after it: the colour where its intensity (GX_TF_I8) or channel
// c_params.z (GX_CTF_R8/G8/B8) exceeds the threshold, else black.
// BlurCS: axis 0, up to three Gaussians (widths c_sigma.xyz in texels) of t0 into u0..u2; axis 1, the Gaussians of
// t0..t2 summed, times c_params.x, clamped to 1 when c_params.y is 1, into u0.
// CompositeVS/CompositePS: the kept scene plus the bloom (bilinear from its levels) over the game's frame.

cbuffer BloomConstants : register(b0) {
    uint4 c_size;     // the output's texels (x, y); Down: the box (z); Box, Blur: the Gaussians (z), the axis (w)
    float4 c_sigma;   // Blur: the Gaussians' widths along the axis (texels)
    float4 c_params;  // per pass (above); Composite: intensity, strength, kind (0 scene, 1 normal, 2 simple), 1: 8 bits
    float4 c_uv;      // Down: the source region (texels: x, y); Composite: frame pixel to level-1 UV (x, y), level-2 (z, w)
};

Texture2D<float4> t_source : register(t0);
Texture2D<float4> t_second : register(t1);
Texture2D<float4> t_third : register(t2);
// Vulkan (game_bloom_vk.cpp) writes the work images as storage images of a known format: no device feature needed.
#ifdef BLOOM_STORAGE_FORMAT
#define WORK_FORMAT [[vk::image_format("rgba16f")]]
#else
#define WORK_FORMAT
#endif
WORK_FORMAT RWTexture2D<float4> u_first : register(u0);
WORK_FORMAT RWTexture2D<float4> u_second : register(u1);
WORK_FORMAT RWTexture2D<float4> u_third : register(u2);
SamplerState s_linear : register(s0);

[numthreads(8, 8, 1)]
void DownCS(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= c_size.xy)) {
        return;
    }
    const uint2 region = uint2(c_uv.xy);
    const uint2 base = id.xy * c_size.z;
    float3 sum = 0.0f;
    uint count = 0u;
    [loop] for (uint j = 0u; j < c_size.z; ++j) {
        [loop] for (uint i = 0u; i < c_size.z; ++i) {
            const uint2 p = base + uint2(i, j);
            if (all(p < region)) {
                sum += saturate(t_source.Load(int3(p, 0)).rgb);
                ++count;
            }
        }
    }
    u_first[id.xy] = float4(count != 0u ? sum / float(count) : 0.0f, 1.0f);
}

[numthreads(8, 8, 1)]
void BoxCS(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= c_size.xy)) {
        return;
    }
    // The texels' overlap with [centre - width / 2, centre + width / 2] (texel i spans [i, i + 1]).
    const uint axis = c_size.w;
    const int last = int(axis == 0u ? c_size.x : c_size.y) - 1;
    const float centre = float(axis == 0u ? id.x : id.y) + 0.5f;
    const float lo = centre - 0.5f * c_params.x;
    const float hi = centre + 0.5f * c_params.x;
    float3 sum = 0.0f;
    [loop] for (int i = int(floor(lo)); float(i) < hi; ++i) {
        const float w = min(float(i + 1), hi) - max(float(i), lo);
        int2 p = int2(id.xy);
        if (axis == 0u) {
            p.x = clamp(i, 0, last);
        } else {
            p.y = clamp(i, 0, last);
        }
        sum += t_source.Load(int3(p, 0)).rgb * w;
    }
    float3 c = sum / c_params.x;
    if (c_params.y >= 0.0f) {
        // The 8-bit value the copy would hold: intensity as the EFB copy converts it (BT.601, 16-235), or a channel.
        const uint channel = uint(c_params.z);
        const float value = channel == 0u ? dot(c, float3(66.0f, 129.0f, 25.0f)) * (255.0f / 256.0f) + 16.0f
                                          : c[min(channel, 3u) - 1u] * 255.0f;
        c = floor(value + 0.5f) > c_params.y ? c : 0.0f;
    }
    u_first[id.xy] = float4(c, 1.0f);
}

// A normalised Gaussian of `sigma` texels along `step` (UV per texel) about `uv`, two texels a bilinear tap.
float3 gaussian(Texture2D<float4> source, float2 uv, float2 step, float sigma) {
    const int radius = int(ceil(3.0f * sigma));
    const float k = -0.5f / max(sigma * sigma, 1e-4f);
    float3 sum = source.SampleLevel(s_linear, uv, 0.0f).rgb;
    float weights = 1.0f;
    [loop] for (int i = 1; i <= radius; i += 2) {
        const float w0 = exp(k * float(i * i));
        const float w1 = i + 1 <= radius ? exp(k * float((i + 1) * (i + 1))) : 0.0f;
        const float w = w0 + w1;
        const float offset = (float(i) * w0 + float(i + 1) * w1) / w;
        sum += (source.SampleLevel(s_linear, uv + step * offset, 0.0f).rgb +
                source.SampleLevel(s_linear, uv - step * offset, 0.0f).rgb) * w;
        weights += 2.0f * w;
    }
    return sum / weights;
}

[numthreads(8, 8, 1)]
void BlurCS(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= c_size.xy)) {
        return;
    }
    const float2 texel = 1.0f / float2(c_size.xy);
    const float2 uv = (float2(id.xy) + 0.5f) * texel;
    const uint count = c_size.z;
    if (c_size.w == 0u) {
        const float2 step = float2(texel.x, 0.0f);
        u_first[id.xy] = float4(gaussian(t_source, uv, step, c_sigma.x), 1.0f);
        if (count > 1u) {
            u_second[id.xy] = float4(gaussian(t_source, uv, step, c_sigma.y), 1.0f);
        }
        if (count > 2u) {
            u_third[id.xy] = float4(gaussian(t_source, uv, step, c_sigma.z), 1.0f);
        }
        return;
    }
    const float2 step = float2(0.0f, texel.y);
    float3 sum = gaussian(t_source, uv, step, c_sigma.x);
    if (count > 1u) {
        sum += gaussian(t_second, uv, step, c_sigma.y);
    }
    if (count > 2u) {
        sum += gaussian(t_third, uv, step, c_sigma.z);
    }
    sum *= c_params.x;
    u_first[id.xy] = float4(c_params.y != 0.0f ? saturate(sum) : sum, 1.0f);
}

float4 CompositeVS(uint id : SV_VertexID) : SV_Position {
    const float2 uv = float2((id << 1u) & 2u, id & 2u);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

// t_source the kept scene, t_second level 1 (normal: the first blur's sum; simple: its one blur), t_third level 2.
// Kind 0 (the game's own chain restoring its kept scene): the scene alone.
float4 CompositePS(float4 position : SV_Position) : SV_Target0 {
    const float3 scene = t_source.Load(int3(int2(position.xy), 0)).rgb;
    float3 bloom = 0.0f;
    if (c_params.z == 1.0f) {
        const float3 first = t_second.SampleLevel(s_linear, position.xy * c_uv.xy, 0.0f).rgb;
        const float3 second = t_third.SampleLevel(s_linear, position.xy * c_uv.zw, 0.0f).rgb;
        bloom = saturate(c_params.x * (first + second)) * c_params.y;
    } else if (c_params.z == 2.0f) {
        bloom = t_second.SampleLevel(s_linear, position.xy * c_uv.xy, 0.0f).rgb * (c_params.x * c_params.y);
    }
    if (c_params.w == 0.0f) {
        return float4(scene + bloom, 1.0f);
    }
    // Into 8 bits: the scene as it was, the bloom stochastically rounded (interleaved gradient noise, Jimenez 2014), so
    // its gradients come out smooth instead of in bands.
    const float u = frac(52.9829189f * frac(dot(floor(position.xy), float2(0.06711056f, 0.00583715f))));
    return float4(saturate((round(saturate(scene) * 255.0f) + floor(bloom * 255.0f + u)) / 255.0f), 1.0f);
}
