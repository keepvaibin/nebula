// SPDX-License-Identifier: GPL-3.0-only
// The sky and bounce light (the master plan's section 11; work list 22.10): I, the cosine-weighted mean of what a
// surface sees over its half of the world, as a ratio to the stage's authored ambient. Open sky gives 1 (tinted where
// the dome is brighter); a crevice sees dark surfaces and falls (contact shadow, grounding); a sunlit wall beside it
// lifts it in the wall's colour (bounce light); a lava sea below lifts it in orange.
//
// Half resolution: each thread is one half-res pixel, its receiver the full-res pixel of its 2 x 2 block that the
// frame's turn picks, so four frames visit every pixel. Modes (g_pass.y, from rt_quality):
//   0 Low: sky only: one ray within kSkyReach; a hit is kHitDark, no texture read, no reservoir (traced ambient
//     occlusion in the sky's colour);
//   1 Medium: one ray within kIndirectReach, the hit shaded (rt_hit_radiance: the sun traced from it), ReSTIR GI
//     (Ouyang et al. 2021, "ReSTIR GI: Path Resampling for Real-Time Path Tracing"): the frame before's reservoir of
//     the same surface (M up to 30) and one neighbour's within 16 half-res pixels, which joins only where a ray from
//     here reaches its sample (RTXDI's ray traced bias correction: a hidden one would bring light this pixel does not
//     see);
//   2 High: three neighbours (a ray each, as Medium's), the pixel's own earlier sample checked once kept (something
//     may have moved in front: then the new sample alone), the grid's lights shadowed at hits, the screen's own I at
//     hits it shows (more than one bounce over time).
// Writes the half-res guide and motion the filters (rt_lights_time.hlsl, rt_lights_space.hlsl at half res) read, the
// raw I in their channels (r, b, a), and the reservoirs: the sample's place (float bits, eye space of this frame) with
// its normal and M in Rgba32ui, its light (ratio) and W in Rgba16f.
// Pass constants: g_pass.y the mode; g_pass.z bit 0 the frame before's reservoirs and I count, bit 1 the near field
// runs (rt_near.hlsl: the gather from its reach on; FB b76e); g_pass.w the shading
// frame's first byte in t4; g_pass_value[0..2] this frame's eye space from the frame before's (3x4 rows).
#include "rt_shade.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_material : register(t12);
Texture2D<uint4> g_reservoirs_before : register(t13);
Texture2D<float4> g_weights_before : register(t14);
Texture2D<float4> g_half_guide_before : register(t15);
Texture2D<float4> g_out_before : register(t16);  // the frame before's I out (full res): the screen's own
Texture2D<float4> g_added_before : register(t17);    // the walk before's D out (the local lights' share): the screen's own
Texture2D<float4> g_blended_before : register(t18);  // the walk before's blended guide (rt_screen_lights; FB b49)
RT_FORMAT(rgba32ui) RWTexture2D<uint4> g_reservoirs : register(u0);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_weights : register(u1);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_half_guide : register(u2);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_half_motion : register(u3);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_raw : register(u4);

static const uint kModeSky = 0u, kModeMedium = 1u, kModeHigh = 2u;
static const float kSkyReach = 800.0f;
static const float kMostM = 30.0f;          // the frame before's reservoir counts as at most 30 samples
static const float kReuseRadius = 16.0f;    // half-res pixels
static const float kMostJacobian = 10.0f;
static const float kMostWeight = 60000.0f;  // W in a half float

struct GiReservoir {
    float3 at;     // the sample's place (eye space of this frame)
    float3 n;      // its normal, toward where it was seen from
    float3 light;  // what it sends, as a ratio to the stage's ambient
    float sum;     // the resampling weights' sum
    float m;       // how many samples it stands for
    float w;       // its unbiased contribution weight, once finished
};

uint gi_pack(float3 n, float m) {
    const float2 o = saturate(rt_oct(n) * 0.5f + 0.5f);
    return uint(o.x * 4095.0f + 0.5f) | uint(o.y * 4095.0f + 0.5f) << 12u | uint(clamp(m, 0.0f, 255.0f)) << 24u;
}

GiReservoir gi_load(int2 at, float3x4 now_from_before) {
    const uint4 bits = g_reservoirs_before.Load(int3(at, 0));
    const float4 weights = g_weights_before.Load(int3(at, 0));
    GiReservoir r;
    r.at = mul(now_from_before, float4(asfloat(bits.xyz), 1.0f));
    r.n = normalize(mul((float3x3)now_from_before, rt_unoct(float2(bits.w & 4095u, bits.w >> 12u & 4095u) / 4095.0f * 2.0f - 1.0f)));
    r.m = min(float(bits.w >> 24u), kMostM * fb_history_scale());
    r.light = weights.rgb;
    r.w = weights.a;
    r.sum = 0.0f;
    if (!all(isfinite(r.at)) || !all(isfinite(r.light)) || !isfinite(r.w)) {
        r.m = 0.0f;
    }
    return r;
}

// The target at the receiver: the sample's light by the receiver's facing to it.
float gi_target(float3 light, float3 sample_at, float3 x, float3 n) {
    const float3 to = sample_at - x;
    const float far2 = dot(to, to);
    if (!(far2 > 1e-6f)) {
        return 0.0f;
    }
    return rt_lum(light) * max(dot(n, to * rsqrt(far2)), 0.0f);
}

// Adds a reservoir to `r` (weighted reservoir sampling): its sample by `weight`, its count.
void gi_take(inout GiReservoir r, GiReservoir other, float weight, inout uint seed) {
    r.m += other.m;
    if (!(weight > 0.0f)) {
        return;
    }
    r.sum += weight;
    if (rt_random(seed) * r.sum < weight) {
        r.at = other.at;
        r.n = other.n;
        r.light = other.light;
    }
}

// Whether something solid stands between the ray's origin and a sample (short of the sample itself), tested from
// `skip` on (High: the near field's reach; FB b76). A sample nearer than `skip` is the near field's: hidden here.
bool gi_hidden(float3 origin, float3 sample_at, float skip) {
    const float3 to = sample_at - origin;
    const float far_ = length(to);
    if (far_ < skip) {
        return true;
    }
    const float3 way = to / max(far_, 1e-4f);
    const float reach = min(far_, kIndirectReach) - max(0.002f * far_, 1.0f) - skip;
    uint met;
    return reach > 1.0f && blocked(origin + way * skip, way, reach, kMaskSolid | kMaskCutout, true, met);
}

void write_nothing(uint2 half_pixel, float4 guide) {
    g_reservoirs[half_pixel] = uint4(0u, 0u, 0u, 0u);
    g_weights[half_pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    g_half_guide[half_pixel] = guide;
    g_half_motion[half_pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    g_raw[half_pixel] = float4(1.0f, 0.0f, 1.0f, 1.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint half_width, half_height, width, height;
    g_raw.GetDimensions(half_width, half_height);
    g_guide.GetDimensions(width, height);
    if (id.x >= half_width || id.y >= half_height || g_pass.x != 0u) {
        return;
    }
    if (all(id.xy < uint2(64u, 64u))) {
        rt_shade_begin(g_pass.w);
        rt_count_dome_directions(id.xy);  // the dome's mean, the stage's, not the view's (FB b51)
    }
    const uint turn = g_more.y & 3u;
    static const uint2 kVisit[4] = {uint2(0u, 0u), uint2(1u, 1u), uint2(1u, 0u), uint2(0u, 1u)};
    const int2 pixel = int2(min(id.xy * 2u + kVisit[turn], uint2(width, height) - 1u));
    const int3 here = int3(pixel, 0);
    const float4 guide = g_guide.Load(here);
    if (guide.a == 0.0f) {
        write_nothing(id.xy, float4(0.0f, 0.0f, 0.0f, 0.0f));
        return;
    }
    const uint flags = uint(g_material.Load(here).b * 255.0f + 0.5f);
    if ((flags & kSurfaceEmissive) != 0u) {
        write_nothing(id.xy, guide);  // shows itself: takes no light
        return;
    }
    rt_shade_begin(g_pass.w);
    const float4 motion = g_motion.Load(here);
    const RtSurface s = rt_surface(pixel, guide, g_normals.Load(here), motion.w);
    const float3 n = s.smooth_;
    const float3 x = s.at;
    g_half_guide[id.xy] = guide;
    g_half_motion[id.xy] = float4(motion.xy * 0.5f, motion.z, 0.0f);
    uint seed = rt_seed(id.xy, g_more.y, 29u);
    const uint mode = g_pass.y;
    // The ray's direction from the frame's blue noise (rt_sample.hlsli); the reservoirs' picks from the hash.
    const float3 dir = rt_cosine(n, rt_sample(id.xy, g_more.y, kSampleIndirect));
    // High: the far field only, from the near field's reach on (rt_near.hlsl sees 0..r at full size; FB b76).
    const float skip = mode == kModeHigh && (g_pass.z & 2u) != 0u ? rt_near_reach(s.away) : 0.0f;
    const float3 origin = rt_ray_origin(s, dir);

    if (mode == kModeSky) {
        // Traced ambient occlusion in the sky's colour: what stands within kSkyReach hides the sky.
        uint item, triangle_;
        float2 where_;
        float far_;
        const bool hit = rt_closest(s.origin, dir, kSkyReach, kMaskSolid | kMaskCutout, item, triangle_, where_, far_);
        const float ratio = hit ? g_shade.look.y : 1.0f;
        g_reservoirs[id.xy] = uint4(0u, 0u, 0u, 0u);
        g_weights[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_raw[id.xy] = float4(ratio, 0.0f, ratio, ratio);
        return;
    }

    // The new sample: what the ray meets, and what it sends as a ratio to the stage's ambient.
    GiReservoir r;
    r.sum = 0.0f;
    r.m = 0.0f;
    r.w = 0.0f;
    {
        uint item, triangle_;
        float2 where_;
        float far_;
        const float3 from = origin + dir * skip;  // TMin r (b76)
        const uint met = rt_gather(from, dir, item, triangle_, where_, far_);
        GiReservoir fresh;
        fresh.m = 1.0f;
        fresh.sum = 0.0f;
        fresh.w = 0.0f;
        fresh.n = -dir;
        if (met == 0u) {
            fresh.at = from + dir * kIndirectReach;
            fresh.light = float3(1.0f, 1.0f, 1.0f);  // nothing at all: the stage's ambient itself
        } else {
            RtHit h;
            rt_hit_read(item, triangle_, where_, far_, from, dir, 0.05f, h);
            fresh.at = h.at;
            if (met == 1u || (h.item_flags & kItemSky) != 0u) {
                fresh.light = rt_sky_ratio(rt_lin(rt_dome(h)));  // the dome's mean: rt_count_dome_directions (b51; linear b65)
            } else {
                fresh.n = h.ng;
                const uint options = kRtShadeSunRay | kRtShadeLighting | (mode == kModeHigh ? kRtShadeLightRay : 0u);
                float3 o = float3(g_shade.gains.y, g_shade.gains.y, g_shade.gains.y);
                if (mode == kModeHigh && (g_pass.z & 1u) != 0u) {
                    float3 seen;
                    if (rt_screen_cache(g_guide, g_out_before, h.at, seen)) {
                        o = seen;
                    }
                }
                // The lamps on a hit the screen shows, as the screen has them (10.2 item 5, b49), else the grid's.
                float3 d_seen;
                const bool d_known = (g_pass.z & 1u) != 0u && rt_screen_lights(g_guide, g_added_before, g_blended_before, h.at, d_seen);
                const float3 bounce = g_shade.gains.z * rt_hit_radiance_with(h, options, o, d_known, d_seen, seed) /
                                      rt_o_unit();  // O's unit (C4; the model's under the relight: b59)
                if (h.glows && !h.listed) {
                    fresh.light = bounce;  // it shines of itself: its light as it is
                } else {
                    // A surface lit by the scene: against the mean of such hits (FB b46), as the sky against its own;
                    // an area glow's emission over it as it is (FB b77).
                    if (((id.x | id.y) & 3u) == 0u) {
                        rt_count_hit(bounce);
                    }
                    fresh.light = bounce * g_shade.authored.w + g_shade.gains.z * rt_hit_area_glow(h, true) / rt_o_unit();
                }
            }
        }
        // Cosine sampling: the first weight is the target over the chance, pi * lum(L).
        gi_take(r, fresh, kRtPi * rt_lum(fresh.light), seed);
        r.at = fresh.at;
        r.n = fresh.n;
        r.light = fresh.light;
    }
    bool reused = false;
    const GiReservoir fresh_only = r;  // what a hidden earlier sample falls back to (High)
    float3 own_at = r.at;              // the sample kept before the neighbours

    // The frame before's: its reservoir of the same surface, then neighbours' around it.
    if ((g_pass.z & 1u) != 0u) {
        const float3x4 now_from_before = float3x4(g_pass_value[0], g_pass_value[1], g_pass_value[2]);
        float2 before_half = float2(-1.0f, -1.0f);
        if (motion.z > 0.0f) {
            before_half = (float2(pixel) + 0.5f + motion.xy) * 0.5f;
            const int2 at = int2(floor(before_half));
            if (all(at >= int2(0, 0)) && all(at < int2(half_width, half_height))) {
                const float4 was = g_half_guide_before.Load(int3(at, 0));
                if (was.a != 0.0f && was.g == guide.g && was.b == guide.b && abs(was.r - motion.z) < 0.05f * motion.z + 2.0f) {
                    const GiReservoir before = gi_load(at, now_from_before);
                    if (before.m > 0.0f) {
                        const uint picked = r.sum > 0.0f ? 0u : 1u;
                        const float3 kept_at = r.at;
                        gi_take(r, before, gi_target(before.light, before.at, x, n) * before.w * before.m, seed);
                        reused = reused || any(r.at != kept_at) || picked != 0u;
                    }
                }
            } else {
                before_half = float2(-1.0f, -1.0f);
            }
        }
        own_at = r.at;
        const uint neighbours = mode == kModeHigh ? 3u : 1u;
        const float2 centre = before_half.x >= 0.0f ? before_half : float2(id.xy) + 0.5f;
        for (uint k = 0u; k < neighbours; ++k) {
            const float2 u = rt_random2(seed);
            const float2 offset = sqrt(u.x) * kReuseRadius * float2(cos(2.0f * kRtPi * u.y), sin(2.0f * kRtPi * u.y));
            const int2 at = int2(floor(centre + offset));
            if (any(at < int2(0, 0)) || any(at >= int2(half_width, half_height))) {
                continue;
            }
            const float4 was = g_half_guide_before.Load(int3(at, 0));
            if (was.a == 0.0f) {
                continue;
            }
            // The neighbour's receiver, moved into this frame: in the receiver's plane and facing as it does.
            const float3 q = mul(now_from_before, float4(rt_eye_ray(g_ray_before, float2(at) * 2.0f + 1.0f) * was.r, 1.0f));
            const float3 nq = normalize(mul((float3x3)now_from_before, rt_unpack_normal(was.a)));
            if (dot(n, nq) < 0.906f || abs(dot(n, q - x)) > 0.05f * s.away + 2.0f) {
                continue;
            }
            const GiReservoir other = gi_load(at, now_from_before);
            if (!(other.m > 0.0f) || dot(n, other.at - x) <= 0.0f) {
                continue;  // none, or behind the receiver's plane
            }
            // The sample seen from here rather than from the neighbour: the Jacobian of the solid angle it covers
            // (|cos at the sample toward here| / |toward there|) * (distance there / distance here)^2.
            const float3 to_r = x - other.at, to_q = q - other.at;
            const float far_r = dot(to_r, to_r), far_q = dot(to_q, to_q);
            const float cos_r = abs(dot(other.n, to_r * rsqrt(max(far_r, 1e-6f))));
            const float cos_q = abs(dot(other.n, to_q * rsqrt(max(far_q, 1e-6f))));
            if (!(cos_q > 1e-4f) || !(far_r > 1e-6f)) {
                continue;
            }
            const float jacobian = clamp(cos_r / cos_q * far_q / far_r, 0.0f, kMostJacobian);
            if (gi_hidden(s.origin, other.at, skip)) {
                continue;  // hidden from here: neither its light nor its count
            }
            gi_take(r, other, gi_target(other.light, other.at, x, n) * other.w * other.m * jacobian, seed);
        }
    }

    // Its contribution weight. The neighbours' samples were seen from here before they joined; on High the pixel's own
    // earlier one is checked once it is the one kept, and where it is hidden now the new sample alone is kept (a
    // hidden direction's light is the occluder's, not none: never a weight of 0).
    float target = gi_target(r.light, r.at, x, n);
    r.w = target > 0.0f && r.m > 0.0f ? min(r.sum / (r.m * target), kMostWeight) : 0.0f;
    const bool own_earlier = reused && all(r.at == own_at) && any(r.at != fresh_only.at);
    if (mode == kModeHigh && own_earlier && r.w > 0.0f && gi_hidden(s.origin, r.at, skip)) {
        r = fresh_only;
        target = gi_target(r.light, r.at, x, n);
        r.w = target > 0.0f && r.m > 0.0f ? min(r.sum / (r.m * target), kMostWeight) : 0.0f;
    }
    const float facing = max(dot(n, normalize(r.at - x)), 0.0f);
    // Whole: the guard is the mean's clamp after the filters (rt_indirect_up); a half float's range only (A1-4; FB b73).
    const float3 ratio = min(r.light * r.w * facing / kRtPi, kMostWeight);
    g_reservoirs[id.xy] = uint4(asuint(r.at), gi_pack(r.n, min(r.m, kMostM * fb_history_scale())));
    g_weights[id.xy] = float4(r.light, r.w);
    g_raw[id.xy] = float4(ratio.r, 0.0f, ratio.g, ratio.b);
}
