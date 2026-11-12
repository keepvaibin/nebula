// SPDX-License-Identifier: GPL-3.0-only
// The light volume (the master plan's 15.2): the light in the air over the scene camera's view, for what is drawn
// unlit in it (smoke, dust, steam: particles that are not lights themselves) as Unreal's translucency lighting
// volume and the froxel volumetrics of Frostbite and others. Froxels: 96 x 54 across the scene's viewport, 48 slices
// in depth at z = 50 (20000 / 50)^(k / 48). Each frame each froxel takes a point of its cell (the blue noise, a new one
// each frame) and there:
//   - V: one ray to the sun (everything solid stops it, as the sun pass takes a lit surface's);
//   - the local lights: 4 candidates of the world light grid's cell by resampled importance (the grid lists every
//     light whose reach touches the cell; the screen tiles list only those of the surfaces seen), each as all of its
//     light (the air faces every light, as a particle's colour takes the sun's whole key); one shadow ray on High;
//   - s, the sky: 1 on Medium and Low; on High one ray of 4000 units about the player's up, kept at kSkyMin when it
//     meets something.
// Stored (Rgba16f 3D) as the programs take it: rgb what of the stage's light reaches the air, a channel at a time, as
// a share of its full light (A s + K V) / (A + K) plus the local lights' share, so a drawn colour times it is lit;
// a V (the light shafts' sun, 15.4). Averaged over frames: the froxel's centre is found in the frame before's volume
// (the camera's move undone), and the new value takes 0.1 of the sum; out of that volume, or with no frame before,
// the new value alone.
// Binds: t4 the shading frame (rt_shade.hlsli), t9 the frame before's volume, u0 this frame's. Pass constants:
// g_pass.y the preset (0 Medium, 1 High, 2 Low), g_pass.z bit 0 a frame before to read, g_pass.w the shading frame's
// first byte. Dispatch: 12 x (7 x 48) groups of 8 x 8 (a slice is 7 group rows).
#include "rt_shade.hlsli"
#include "rt_sample.hlsli"

Texture3D<float4> g_before_volume : register(t9);
RT_FORMAT(rgba16f) RWTexture3D<float4> g_volume : register(u0);

static const uint3 kCells = uint3(96u, 54u, 48u);
static const float kNear = 50.0f, kFar = 20000.0f;
static const float kKeep = 0.9f;          // what of the sum over frames stays each frame
static const float kSkyReach = 4000.0f;   // the sky ray's reach (High)
static const float kSkyHidden = 0.25f;    // what of the sky's light a froxel keeps whose sky ray meets something

float slice_depth(float k) {
    return kNear * pow(kFar / kNear, k / float(kCells.z));
}

float depth_slice(float depth) {
    return log(max(depth, 1.0f) / kNear) / log(kFar / kNear) * float(kCells.z);
}

// The eye-space place of a point of the volume (cell units).
float3 volume_place(float3 cell) {
    const float2 pixel = g_viewport.xy + cell.xy / float2(kCells.xy) * g_viewport.zw;
    return rt_eye_ray(g_ray, pixel) * slice_depth(cell.z);
}

// The local lights in the air: 4 candidates of the grid's cell by RIS, one kept.
float3 air_lights(float3 at, bool shadowed, inout uint seed) {
    if (g_shade.grid.z == 0u) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const int3 cell = int3(floor(float3(dot(at, g_shade.axis[0].xyz) + g_shade.axis[0].w, dot(at, g_shade.axis[1].xyz) + g_shade.axis[1].w,
                                        dot(at, g_shade.axis[2].xyz) + g_shade.axis[2].w)));
    if (any(cell < int3(0, 0, 0)) || any(cell >= int3(32, 16, 32))) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const uint2 run = g_tiles.Load2(g_shade.grid.x + uint((cell.z * 16 + cell.y) * 32 + cell.x) * 8u);
    if (run.y == 0u) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float sum = 0.0f, kept_target = 0.0f;
    float3 kept = float3(0.0f, 0.0f, 0.0f);
    float4 kept_place = float4(0.0f, 0.0f, 0.0f, 0.0f);
    uint kept_slot = 0u;
    for (uint k = 0u; k < 4u; ++k) {
        const uint entry = g_tiles.Load(g_shade.grid.y + (run.x + min(uint(rt_random(seed) * float(run.y)), run.y - 1u)) * 4u);
        const uint emitter = g_glow.x + (entry & 0xFFFFu) * 48u;
        const float3 toward = asfloat(g_drops.Load3(emitter)) - at;  // facing the light: all of it
        float4 place;
        uint slot;
        const float3 light = emitter_light(emitter, at, toward, 0xFFFFFFFFu, place, slot) * float(max(entry >> 16u, 1u));
        const float target = rt_lum(light);
        if (!(target > 0.0f)) {
            continue;
        }
        const float weight = target * float(run.y);
        sum += weight;
        if (rt_random(seed) * sum < weight) {
            kept = light;
            kept_target = target;
            kept_place = place;
            kept_slot = slot;
        }
    }
    if (!(kept_target > 0.0f)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float3 estimate = kept * (sum / (4.0f * kept_target));
    if (shadowed) {
        float reach;
        const float3 toward = rt_toward_light(kept_place, at, rt_random2(seed), reach);
        if (reach > 1.0f) {
            estimate *= rt_through(at, toward, reach, kept_slot);
        }
    }
    return estimate;
}

[numthreads(8, 8, 1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID) {
    const uint3 froxel = uint3(group.x * 8u + thread.x, (group.y % 7u) * 8u + thread.y, group.y / 7u);
    if (any(froxel >= kCells)) {
        return;
    }
    rt_shade_begin(g_pass.w);
    const uint frame = g_more.y;
    const uint2 noise_at = froxel.xy + uint2(froxel.z * 37u, froxel.z * 71u);
    const float2 u = rt_sample(noise_at, frame, kSampleVolume);
    const float2 u2 = rt_sample(noise_at, frame, kSampleVolume | kSampleSecond);
    const float3 at = volume_place(float3(froxel) + float3(u, u2.x));
    uint seed = rt_seed(noise_at, frame, 8u);
    const bool high = g_pass.y == 1u;

    // The sun.
    float3 v = float3(1.0f, 1.0f, 1.0f);
    if (rt_sun_traced()) {
        v = rt_through_of(at, g_sun.xyz, kRtFarAway, 0xFFFFFFFFu, rt_sun_mask(true));
    }
    // The sky.
    float s = 1.0f;
    if (high) {
        const float3 up = normalize(g_up.xyz);
        if (rt_through_of(at, rt_cosine(up, float2(u2.y, rt_random(seed))), kSkyReach, 0xFFFFFFFFu, kMaskSolid).r < 0.5f) {
            s = kSkyHidden;
        }
    }
    // The emitters alone (--diagnostics rt-added, g_more.z bit 7): no sun, no sky in the air either (FB b60). Else the
    // stage's light's share in linear light, the model's under the relight (b64).
    float3 share = (g_more.z & 128u) != 0u ? float3(0.0f, 0.0f, 0.0f) : rt_air_share(s, v);
    if (g_pass.y != 2u) {
        share += air_lights(at, high, seed);  // Low: the sun and the sky alone (19.1)
    }
    float4 now = float4(share, (v.r + v.g + v.b) * (1.0f / 3.0f));

    // The frame before's at this froxel's centre.
    if ((g_pass.z & 1u) != 0u) {
        const float3 centre = volume_place(float3(froxel) + 0.5f);
        const float3 before = float3(dot(g_before[0].xyz, centre) + g_before[0].w, dot(g_before[1].xyz, centre) + g_before[1].w,
                                     dot(g_before[2].xyz, centre) + g_before[2].w);
        const float away = -before.z;
        if (away > 1.0f) {
            const float2 pixel = float2((before.x / away - g_ray_before.y) / g_ray_before.x,
                                        (before.y / away - g_ray_before.w) / g_ray_before.z);
            const float3 cell = float3((pixel - g_viewport.xy) / g_viewport.zw * float2(kCells.xy), depth_slice(away));
            if (all(cell >= 0.0f) && all(cell <= float3(kCells))) {
                const float3 inner = clamp(cell, 0.5f, float3(kCells) - 0.5f) / float3(kCells);
                const float4 kept = g_before_volume.SampleLevel(g_wrap, inner, 0.0f);
                if (all(kept == kept)) {
                    now = lerp(now, kept, 1.0f - (1.0f - kKeep) / fb_history_scale());
                }
            }
        }
    }
    g_volume[froxel] = now;
}
