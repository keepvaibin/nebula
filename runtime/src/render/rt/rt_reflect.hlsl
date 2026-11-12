// SPDX-License-Identifier: GPL-3.0-only
// The reflections (the master plan's section 12; work list 22.11): what glass, water, metal and glossy things mirror.
// Per pixel of the opaque layer whose class reflects (and, on Medium, metal and gloss only up to roughness 0.15 with
// a painted share of 0.5 or more): one ray along the view turned at the smooth normal, a mirror's own under roughness
// 0.03, else about a normal of the GGX distribution as the view sees it (Heitz 2018), from the frame's blue noise; a
// direction under the geometric surface is mirrored up. Where it lands: that surface's light by the shared model
// (rt_hit_radiance: its sun traced, the grid's lights, the screen's own I where the screen shows that very surface),
// a glowing thing as it shines; the sky's dome; the sun's own glint (K * kSunGlint) where the ray runs at the sun
// unhindered; else what a ray that meets nothing shows (g_sky).
// Writes R raw: rgb the radiance seen (Fresnel is the composite's, rt_reflect_out.hlsl), a the ray's length (0: the
// pixel does not reflect), which the filter over time reprojects the mirror image by. The local lights' highlights
// (rt_lights.hlsl LightsSpec, already weighed by their Fresnel) ride along as radiance, over the composite's weight,
// so the filter takes their noise out with the rest.
// Pass constants: g_pass.y 2 High (every reflecting class, the lights shadowed at hits), 1 Medium; g_pass.z bit 0 the
// sky and bounce light's I is there to read; g_pass.w the shading frame's first byte in t4.
#include "rt_shade.hlsli"
#include "rt_mirror.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_material : register(t12);
Texture2D<float4> g_indirect : register(t13);  // this frame's I (rt_indirect_up.hlsl): the screen's own at hits
Texture2D<float4> g_lights_spec : register(t14);  // the local lights' highlights (rt_lights.hlsl), already weighed
Texture2D<float4> g_base : register(t15);
Texture2D<float4> g_added_before : register(t16);    // the walk before's D out: the screen's own lamps at hits (FB b49)
Texture2D<float4> g_blended_before : register(t17);  // the walk before's blended guide (rt_screen_lights)
RT_FORMAT(rgba16f) RWTexture2D<float4> g_raw : register(u0);

static const uint kRtWater = 9u;
static const float kReflectReach = 4000000.0f;

// The pixel's index for its mirror's F0 (rt_mirror.hlsli mirror_f0; item word 15 bits 12-19): glass and water only,
// as rt_reflect_out.hlsl reads it, so the highlights over the weight come back whole (FB b75b).
uint mirror_ior_bits(int3 here, uint klass) {
    const uint item = klass == kRtGlass || klass == kRtWater ? g_item_map.Load(here).x : 0u;
    return item != 0u ? g_items.Load(g_at.x + (item - 1u) * kItemBytes + 60u) >> 12u & 255u : 0u;
}

// Whether a surface of class `klass` reflects under the preset (12.1, 19.1).
bool reflects(uint klass, float roughness, float share, bool high) {
    if (klass == kRtGlass || klass == kRtWater) {
        return true;
    }
    if (klass != kRtMetal && klass != kRtGloss) {
        return false;
    }
    return high || (roughness <= 0.15f && share >= 0.5f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_raw.GetDimensions(width, height);
    if (id.x >= width || id.y >= height || g_pass.x != 0u) {
        return;
    }
    const int3 here = int3(id.xy, 0);
    const float4 guide = g_guide.Load(here);
    const float4 material = g_material.Load(here);
    const uint klass = uint(material.b * 255.0f + 0.5f) & 15u;
    const bool high = g_pass.y >= 2u;
    if (guide.a == 0.0f || !reflects(klass, material.r, material.a, high)) {
        g_raw[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    // Medium traces a quarter of the pixels a frame (12.2 item 6: the rays of half resolution, the picture's edges
    // kept at full): one of each 2 x 2 block, the one the frame's turn picks (as the sky and bounce light's), so four
    // frames visit every pixel. The others say so (a -1): the filter over time keeps their history.
    if (!high && (g_more.z & 2048u) == 0u) {  // raw signals for an outside denoiser (22.20): every pixel
        static const uint2 kVisit[4] = {uint2(0u, 0u), uint2(1u, 1u), uint2(1u, 0u), uint2(0u, 1u)};
        if (any((id.xy & 1u) != kVisit[g_more.y & 3u])) {
            g_raw[id.xy] = float4(0.0f, 0.0f, 0.0f, -1.0f);
            return;
        }
    }
    rt_shade_begin(g_pass.w);
    const RtSurface s = rt_surface(int2(id.xy), guide, g_normals.Load(here), g_motion.Load(here).w);
    const float3 look = normalize(s.at);
    const float3 n = dot(s.smooth_, look) < 0.0f ? s.smooth_ : s.normal;
    const float roughness = material.r;  // water's: its waves' (rt_surface.hlsl, FB b92; was 0)
    float3 dir = roughness < 0.03f ? reflect(look, n) : rt_ggx_reflect(n, -look, roughness * roughness, rt_sample(id.xy, g_more.y, kSampleReflect));
    if (dot(dir, s.normal) <= 0.0f) {
        dir = reflect(dir, s.normal);
    }
    const float3 from = s.at + n * s.skin;
    uint seed = rt_seed(id.xy, g_more.y, 41u);
    uint item, triangle_;
    float2 where_;
    float far_;
    float3 seen;
    bool open_sky = true;  // the ray meets the sky (the dome or nothing)
    if (!rt_closest(from, dir, kReflectReach, kMaskSolid | kMaskCutout | kMaskSky, item, triangle_, where_, far_)) {
        seen = rt_lin(g_sky.rgb);  // linear (M1; FB b59b)
        far_ = kReflectReach;
    } else {
        RtHit h;
        rt_hit_read(item, triangle_, where_, far_, from, dir, abs(g_ray.x) + 0.5f * roughness, h);
        if ((h.item_flags & kItemSky) != 0u) {
            seen = rt_shown(rt_dome(h));  // linear (M1; FB b65)
        } else {
            open_sky = false;
            float3 o = float3(g_shade.gains.y, g_shade.gains.y, g_shade.gains.y);
            float3 known;
            if ((g_pass.z & 1u) != 0u && rt_screen_cache(g_guide, g_indirect, h.at, known)) {
                o = known;
            }
            // The lamps on a hit the screen shows, as the screen has them (10.2 item 5, b49; g_pass.z bit 1: the walk
            // before's), else the grid's.
            float3 d_seen;
            const bool d_known = (g_pass.z & 2u) != 0u && rt_screen_lights(g_guide, g_added_before, g_blended_before, h.at, d_seen);
            seen = rt_hit_radiance_with(h, kRtShadeSunRay | (high ? kRtShadeLightRay : 0u), o, d_known, d_seen, seed);
        }
    }
    // The sun itself on the reflected ray, where nothing hides it (12.2 item 4): far brighter than white, it blooms.
    if (open_sky && rt_sees_sun(dir)) {
        seen += rt_sun_glint();  // linear, the model's by kForm (A4 W8; FB b75)
    }
    const float4 weight = mirror_weight(uint(material.b * 255.0f + 0.5f), material.r, material.a, g_base.Load(here).rgb,
                                        1.0f - saturate(-dot(n, look)), mirror_ior_bits(here, klass));
    seen += g_lights_spec.Load(here).rgb / max(weight.rgb, 0.02f);
    g_raw[id.xy] = float4(min(seen, 64.0f), max(far_, 1.0f));
}
