// SPDX-License-Identifier: GPL-3.0-only
// The reflections: the specular lobe of every lit surface of the opaque layer, traced up to a roughness (High 0.6,
// Medium 0.25: Lumen's cut, past which the lobe is so wide that the sky and bounce light I stands for it; glass and
// water at any). One ray along the view turned at the smooth normal, a mirror's own under roughness 0.03, else about
// a normal of the GGX distribution as the view sees it (Heitz 2018), from the frame's blue noise, drawn again where it
// falls under the smooth or the geometric surface. Where it lands: that surface's radiance by the shared model
// (rt_hit_radiance: its sun traced, the grid's lights, the screen's own I where the screen shows that very surface),
// a glowing thing as it shines, a clear thing seen through (rt_look); where it meets nothing, the sky's dome along it
// as the eye sees it (the miss rule). The sun's mirror image is its highlight (rt_reflect_out.hlsl), never a ray's.
// Writes R raw: rgb the radiance seen, demodulated (the specular albedo lobes.mirror is the composite's,
// rt_reflect_out.hlsl), a the ray's length (0: the pixel does not reflect), which the filter over time reprojects the
// mirror image by (NRD's in-lobe hit distance). The local lights' highlights (rt_lights.hlsl LightsSpec) ride along
// over the specular albedo, so the filter takes their noise out with the rest.
// Pass constants: g_pass.y 2 High (every reflecting class, the lights shadowed at hits), 1 Medium; g_pass.z bit 0 the
// sky and bounce light's I is there to read, bit 1 the walk before's local lights (Added, t16), bit 3 the walk before's
// blended guide (t17) is bound (rt_screen_lights); g_pass.w the shading frame's first byte in t4.
#include "rt_shade.hlsli"
#include "rt_mirror.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_material : register(t12);
Texture2D<float4> g_indirect : register(t13);  // this frame's I (rt_indirect_up.hlsl): the screen's own at hits
Texture2D<float4> g_lights_spec : register(t14);  // the local lights' highlights (rt_lights.hlsl), with their albedo
Texture2D<float4> g_base : register(t15);
Texture2D<float4> g_added_before : register(t16);    // the walk before's D out: the screen's own lamps at hits
Texture2D<float4> g_blended_before : register(t17);  // the walk before's blended guide (rt_screen_lights)
RT_FORMAT(rgba16f) RWTexture2D<float4> g_raw : register(u0);

static const float kReflectReach = 4000000.0f;
static const uint kLobeDraws = 4u;  // draws of the lobe before the mirror's own direction

// The pixel's item word 15 (bits 12-19 its index or a metal's kind); 0 where no item is known.
uint mirror_word(int3 here) {
    const uint item = g_item_map.Load(here).x;
    return item != 0u ? g_items.Load(g_at.x + (item - 1u) * kItemBytes + 60u) : 0u;
}

// Whether a surface reflects by a traced ray under the preset: glass and water always, any other lit class up to the
// preset's roughness (rt_mirror.hlsli kTracedRough*).
bool reflects(uint bits, float roughness, bool high) {
    const uint klass = bits & 15u;
    if (klass == kRtGlass || klass == kRtWater) {
        return true;
    }
    return mirror_lit(bits) && roughness <= (high ? kTracedRoughHigh : kTracedRoughMedium);
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
    const uint bits = uint(material.b * 255.0f + 0.5f);
    const bool high = g_pass.y >= 2u;
    if (guide.a == 0.0f || !reflects(bits, material.r, high)) {
        g_raw[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    // Medium traces a quarter of the pixels a frame (12.2 item 6: the rays of half resolution, the picture's edges
    // kept at full): one of each 2 x 2 block, the one the frame's turn picks (as the sky and bounce light's), so four
    // frames visit every pixel. The others say so (a -1): the filter over time keeps their history.
    if (!high && (g_more.z & 2048u) == 0u) {  // raw signals for an outside denoiser: every pixel
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
    const float roughness = material.r;  // water's: its waves' (rt_surface.hlsl; was 0)
    uint seed = rt_seed(id.xy, g_more.y, 41u);
    // A direction of the lobe the surface can send light along: over both the smooth and the geometric surface. One
    // under either carries no light of this lobe (its G1 is 0); with one ray a pixel the estimate is the lobe's mean,
    // so such a draw is drawn again (white noise from the seed), not turned up: a direction mirrored about the
    // geometric normal is one the lobe does not hold. After kLobeDraws, the mirror's own direction.
    float3 dir = reflect(look, n);
    if (roughness >= 0.03f) {
        float2 u = rt_sample(id.xy, g_more.y, kSampleReflect);
        for (uint draw = 0u; draw < kLobeDraws; ++draw) {
            const float3 l = rt_ggx_reflect(n, -look, roughness * roughness, u);
            if (dot(l, n) > 0.0f && dot(l, s.normal) > 0.0f) {
                dir = l;
                break;
            }
            u = rt_random2(seed);
        }
    }
    if (dot(dir, s.normal) <= 0.0f) {
        dir = reflect(dir, s.normal);  // the mirror's own under the geometric surface (a smooth normal far off it)
    }
    const float3 from = rt_ray_origin(s, dir);
    const float cone = abs(g_ray.x) + 0.5f * roughness;
    // One walk (rt_look): a clear thing met is seen through, as every other looking ray sees it (as the second layer's
    // mirror, rt_light.hlsl); a miss reads the dome along the ray's direction from the eye (the miss rule). far_ is the
    // way to the first thing met (kReflectReach where none).
    const RtLook look_ = rt_look(from, dir, kReflectReach, cone, kClearFaces, true);
    const float far_ = look_.far_;
    float3 end = look_.shown;
    if (look_.hit) {
        float3 o = rt_sky_mean();
        float3 d_seen = float3(0.0f, 0.0f, 0.0f);
        bool d_known = false;
        if (look_.faces == 0u) {
            // A thing met first (no clear face before it) takes what the screen knows of it: its I, and the lamps on it
            // as the screen has them (g_pass.z bit 1: the walk before's), else the grid's.
            float3 known;
            if ((g_pass.z & 1u) != 0u && rt_screen_cache(g_guide, g_indirect, look_.h, known)) {
                o = known;
            }
            d_known = (g_pass.z & 2u) != 0u &&
                      rt_screen_lights(g_guide, g_added_before, g_blended_before, (g_pass.z & 8u) != 0u, look_.h, d_seen);
        }
        end = rt_hit_radiance_with(look_.h, kRtShadeSunRay | (high ? kRtShadeLightRay : 0u), o, d_known, d_seen, seed);
    }
    float3 seen = look_.added + look_.tint * end;
    const ShadeMaterial m = mirror_material(bits, material.r, material.g, g_base.Load(here).rgb, mirror_word(here));
    const ShadeLobes lobes = shade_lobes(m, max(-dot(n, look), kShadeNvLeast));
    seen += g_lights_spec.Load(here).rgb / max(lobes.mirror, 0.02f);
    // The length within half precision's finite range (kNoOccluder: nothing met): a value over it converts to the
    // largest finite one on D3D12 but may be Inf on Vulkan, and Inf - Inf in the filters' weights is NaN.
    g_raw[id.xy] = float4(min(seen, 64.0f), clamp(far_, 1.0f, kNoOccluder));
}
