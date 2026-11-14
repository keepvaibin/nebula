// SPDX-License-Identifier: GPL-3.0-only
// Light in the air (High): sunlight and lamplight scattered toward the eye by the air between
// it and what it sees: shafts of sun between Honeyhive's trees, a glow in dusty air about a fire, nothing in space
// (no air: the pass does not run). Single scattering integrated through the light volume front to back (Wronski 2014,
// Hillaire 2015: the froxel integration of Frostbite and Unreal's volumetric fog), so each pixel takes the air in
// front of it with one trilinear fetch at its own depth (program.hlsli traced_scatter), blended things at theirs.
// Each thread one column of froxels; through the slices, from the eye to each slice's middle:
//   source = K V hg(cos) + (A + K) Vol.rgb - K V   (Vol.rgb holds the sun's light as if it went every way:
//            (A s + K V) / (A + K) + local; the sun's part is put back by Henyey-Greenstein, g = kForward)
//   L += T source (1 - e^(-sigma d)),  T *= e^(-sigma d)
// Stored (Rgba16f 3D, as the volume) rgb L in the colour the programs write (a white thing under the stage's full
// light is A + K), a T. sigma: g_pass_value[0].x (kAirDensity, per unit), A and K the stage's (the shading frame).
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
    const float3 a = g_shade.authored.rgb, k = g_shade.key.rgb;
    const bool sun = rt_sun_traced();
    float3 light = float3(0.0f, 0.0f, 0.0f);
    float through = 1.0f, from = 0.0f;
    for (uint z = 0u; z < kCells.z; ++z) {
        const float to = min(slice_depth(float(z) + 0.5f), kAirReach);
        // The eye ray's length in the slice (its distances are along the view's axis); 0 past the air's reach.
        const float d = max(to - from, 0.0f) / max(-look.z, 0.05f);
        const float4 air = g_air.Load(int4(id.xy, z, 0));
        float3 source = (a + k) * air.rgb;
        if (sun) {
            source += k * air.a * (phase - 1.0f);
        }
        const float kept = exp(-sigma * d);
        light += through * max(source, 0.0f) * (1.0f - kept);
        through *= kept;
        from = to;
        g_scatter[uint3(id.xy, z)] = float4(light, through);
    }
}
