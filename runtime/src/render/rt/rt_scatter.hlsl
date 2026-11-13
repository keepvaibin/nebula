// SPDX-License-Identifier: GPL-3.0-only
// Light in the air (the master plan's 15.4, High): sunlight and lamplight scattered toward the eye by the air between
// it and what it sees: shafts of sun between Honeyhive's trees, a glow in dusty air about a fire, nothing in space
// (no air: the pass does not run). Single scattering integrated through the light volume front to back (Wronski 2014,
// Hillaire 2015: the froxel integration of Frostbite and Unreal's volumetric fog), so each pixel takes the air in
// front of it with one trilinear fetch at its own depth (program.hlsli traced_scatter), blended things at theirs.
// Each thread one column of froxels; through the slices, from the eye to each slice's middle:
//   source = Vol.rgb + K / (A + K) V (hg(cos) / 4 - 1)   (Vol.rgb holds the sun's light as a surface square to it
//            takes it: (A s + K V) / (A + K) + local; the air scatters pi p of it, p Henyey-Greenstein's normalised
//            over the sphere (g = kForward): hg4pi / 4; LIGHT-MODEL.md 8, FB b72)
//   L += T source (1 - e^(-sigma d)),  T *= e^(-sigma d)
// Stored (Rgba16f 3D, as the volume) rgb L in C1's unit, the one traced_scatter adds it to (white air under the
// stage's full light, every way alike: 1; FB b55), a T. sigma: g_pass_value[0].x (the stage's air, per unit), A and K
// the stage's (the shading frame).
// Binds: t4 the shading frame, t9 this frame's volume, u0 the integral. Dispatch: 12 x 7 groups of 8 x 8.
#include "rt_shade.hlsli"

Texture3D<float4> g_air : register(t9);
RT_FORMAT(rgba16f) RWTexture3D<float4> g_scatter : register(u0);

static const uint3 kCells = uint3(96u, 54u, 48u);
static const float kNear = 50.0f, kFar = 20000.0f;
static const float kForward = 0.6f;  // Henyey-Greenstein's g for the sun: air scatters forward
static const float kAirReach = 6000.0f;  // how far the air goes (a volumetric fog's view distance): the sky behind
                                         // everything takes no more than this much air

float slice_depth(float k) {
    return kNear * pow(kFar / kNear, k / float(kCells.z));
}

// Henyey-Greenstein times 4 pi: 1 for light that goes every way alike.
float hg4pi(float cosine, float g) {
    const float d = 1.0f + g * g - 2.0f * g * cosine;
    return (1.0f - g * g) / (d * sqrt(d));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= kCells.x || id.y >= kCells.y) {
        return;
    }
    rt_shade_begin(g_pass.w);
    const float sigma = g_pass_value[0].x;
    const float2 pixel = g_viewport.xy + (float2(id.xy) + 0.5f) / float2(kCells.xy) * g_viewport.zw;
    const float3 look = normalize(rt_eye_ray(g_ray, pixel));
    const float phase = hg4pi(dot(look, g_sun.xyz), kForward);
    const bool sun = rt_sun_traced() && (g_more.z & 128u) == 0u;  // the emitters alone: no sun (b60)
    // The volume's sun part (rt_air_share) is there unless the emitters alone (b60): without a traced sun it goes every
    // way alike (hg4pi 1), still a quarter of it scattered (b72).
    const bool sun_held = (g_more.z & 128u) == 0u;
    const float3 sun_share = rt_air_sun();  // the sun's part of the air's share, as the light volume's (b64)
    float3 light = float3(0.0f, 0.0f, 0.0f);
    float through = 1.0f, from = 0.0f;
    for (uint z = 0u; z < kCells.z; ++z) {
        const float to = min(slice_depth(float(z) + 0.5f), kAirReach);
        // The eye ray's length in the slice (its distances are along the view's axis); 0 past the air's reach.
        const float d = max(to - from, 0.0f) / max(-look.z, 0.05f);
        const float4 air = g_air.Load(int4(id.xy, z, 0));
        float3 source = air.rgb;
        if (sun_held) {
            source += sun_share * air.a * (0.25f * (sun ? phase : 1.0f) - 1.0f);  // pi p = hg4pi / 4 (b72)
        }
        const float kept = exp(-sigma * d);
        light += through * max(source, 0.0f) * (1.0f - kept);
        through *= kept;
        from = to;
        g_scatter[uint3(id.xy, z)] = float4(light, through);
    }
}
