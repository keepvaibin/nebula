// SPDX-License-Identifier: GPL-3.0-only
// The J3D materials' framework (E-04 step 3): every generated runtime/src/render/materials/j3d/m<id>.hlsl includes this.
// One constant layout for all of them (the game packs it from the material's values: J3DNativeMaterial), the native
// framework, and the vertex lighting from the draw's lights (float math).

struct MaterialConstants {
    float4 reg[4];        // the TEV registers PREV, REG0-2 at the draw (0-1 scale; signed, beyond 1 possible)
    float4 konst[4];      // konst colours 0-3
    float4 mat_color[2];  // channels 0 and 1: material colour
    float4 amb_color[2];  // and ambient colour
    float4 ind_mtx[6];    // indirect matrix m: rows 2m and 2m + 1 (xyz), its scale applied
    float4 fog;           // start, end, near, far
    float4 fog_color;     // rgb
    float4 alpha_ref;     // the alpha test's references 0 and 1 (0-1)
};

#include "native_material.hlsli"

// Light n's contribution to a lit channel, in 0-255 units (rgb, and a for an alpha channel): its colour by its
// attenuation (1 specular, 2 directional, 3 spot) and diffuse function (0 none, 1 signed, 2 clamped), at the eye-space
// position and normal. The light rows hold the colour (raw RGBA8 in row 0's w), the cosine and distance attenuation
// coefficients, the position and direction.
float4 j3d_light(uint n, uint atten, uint diffuse, float3 pos, float3 nrm) {
    const uint lcw = asuint(native_light_row(n, 0u).w);
    const float4 le1 = native_light_row(n, 1u);
    const float4 le2 = native_light_row(n, 2u);
    const float4 le3 = native_light_row(n, 3u);
    const float3 lpos = float3(le2.z, le2.w, le3.x);
    const float3 ldvec = float3(le3.y, le3.z, le3.w);
    const float3 cos_a = le1.xyz;
    const float3 dist_a = float3(le1.w, le2.x, le2.y);
    const float4 lcol = float4((lcw >> 24u) & 255u, (lcw >> 16u) & 255u, (lcw >> 8u) & 255u, lcw & 255u);
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

// A texture coordinate's projection: s and t over q where its generation made three.
float2 j3d_coord(float4 uv) {
    return uv.z != 0.0 && uv.w != 0.0 ? uv.xy / uv.z : uv.xy;
}
