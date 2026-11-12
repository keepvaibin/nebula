// SPDX-License-Identifier: GPL-3.0-only
// The near field of the sky and bounce light (FB b76; COMPLETENESS 1, grounding): per full-size pixel of the opaque
// layer, two cosine-weighted rays about the smooth normal within rt_near_reach (1 % of the eye distance, 20..60
// units), against solids and cutouts; V_near raw is the share that reach it. The half-size gather sees from that reach
// on (rt_indirect.hlsl, High), and rt_indirect_up.hlsl writes O = O_far * V_near (filtered: rt_near_time.hlsl), so a
// foot's contact in shade stays as sharp as the pixels. The sun has its own shadow: only O takes it.
#include "rt_shade.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);   // w the lift (rt_surface)
Texture2D<float4> g_material : register(t12);
RT_FORMAT(r16f) RWTexture2D<float> g_raw : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_raw.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int3 here = int3(id.xy, 0);
    const float4 guide = g_guide.Load(here);
    // No surface, or one that shows itself (it takes no light): nothing near hides anything.
    if (guide.a == 0.0f || (uint(g_material.Load(here).b * 255.0f + 0.5f) & kSurfaceEmissive) != 0u) {
        g_raw[id.xy] = 1.0f;
        return;
    }
    const RtSurface s = rt_surface(int2(id.xy), guide, g_normals.Load(here), g_motion.Load(here).w);
    const float reach = rt_near_reach(s.away);
    float open_ = 0.0f;
    for (uint k = 0u; k < 2u; ++k) {
        float3 dir = rt_cosine(s.smooth_, rt_sample(id.xy, g_more.y, kSampleNear + (k != 0u ? kSampleSecond : 0u)));
        if (dot(dir, s.normal) <= 0.0f) {
            dir = reflect(dir, s.normal);  // under the geometric surface: turned up over it (as the reflections' rays)
        }
        uint met;
        open_ += blocked(rt_ray_origin(s, dir), dir, reach, kMaskSolid | kMaskCutout, true, met) ? 0.0f : 1.0f;
    }
    g_raw[id.xy] = 0.5f * open_;
}
