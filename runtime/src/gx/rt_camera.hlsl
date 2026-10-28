// SPDX-License-Identifier: GPL-3.0-only
// The camera of the ray-traced picture (ray_tracing_camera.cpp; renderer_d3d12.cpp begin_hdr_scene / end_hdr_scene):
// the HDR scene EFB through sun shafts, bloom and a tone curve into the RGBA8 EFB.
//
// While ray tracing runs, the scene layer draws into an HDR EFB (RGBA16F): the traced light is unclamped, the game's
// own draws keep their values (the TEV output is clamped and quantised as on the hardware; their blends add past 1),
// and every value is sRGB-encoded, past 1 as below it, so the game's blends behave as they do in the RGBA8 EFB. At
// the scene's end the camera turns it into the RGBA8 EFB's picture once:
//
// - Bloom: the light a lens scatters around what it images. The picture's light (linear) down a pyramid of halving
//   levels (a 4 x 4 tent each), then back up (each level plus the coarser one upsampled with a 3 x 3 tent of bilinear
//   taps; Jimenez 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare"): the sum of the levels is
//   the picture blurred at every scale, normalised. A share of every pixel's light (c_bloom) leaves for it: energy is
//   kept, nothing is thresholded (a bright lamp glows because it is bright, not because a sprite was drawn).
// - Tone curve: the light to the display's range, channel by channel. Below 0.76 the light passes unchanged (the
//   game's colours as drawn); above it Khronos PBR Neutral's shoulder (Khronos 2024), which reaches 1 only at infinity,
//   on each channel alone: the RGBA8 EFB's clip at 1, made smooth. The game's colours were drawn against that clip,
//   which fills a colour's channels one by one: its stacked glows run from orange through yellow to white (a flame's
//   core), and a light whose channels all pass the shoulder burns white. The standard's own form keeps the light's hue
//   (one scale for the three channels) and blends it towards white: a flame's sum, red far past its green, went to
//   salmon pink (the Dreadnought's beams; hue-kept paths to white are known for it). The standard's toe (darkening the
//   darkest tones by up to 0.04) is left out: the game's darks stay as drawn. For comparison: c_flags kCameraAces,
//   ACES 1.0 (RRT and sRGB ODT, Hill's fit, as Unreal's filmic curve); kCameraNeutral, the standard's hue-kept form.
// - Sun shafts: the light the air scatters towards the camera around a sun the sky shows (crepuscular rays), from
//   where the sky shows it: along each pixel's line to the sun on the picture, the sky's light near the sun where the
//   depth shows sky (the world in front of it blocks it), decaying with the distance (Mitchell 2007, "Volumetric Light
//   Scattering as a Post-Process", GPU Gems 3; the screen-space light shafts of Unreal). A quarter of the picture's
//   resolution, added to the light before the bloom and the tone curve.
// Light sources reach the picture at their radiance: the game's own glow draws of the table's light sources shine at
// it in the HDR scene EFB (GX output_flags bit 5, the glow gain), so what lights the scene is what is seen and blooms.
// Without physical light (c_flags 0) the picture passes as it is, clamped: as the RGBA8 EFB would have kept it.

static const uint kCameraPhysical = 1u;
static const uint kCameraAces = 2u;
static const uint kCameraClip = 4u;    // GALAXY_RT_TONEMAP=2: the light clipped at 1 (no curve), for comparison
static const uint kCameraNeutral = 8u; // GALAXY_RT_TONEMAP=3: Khronos PBR Neutral's hue-kept form, for comparison

cbuffer RtCamera : register(b0)
{
    uint2 c_size;           // the picture's pixels
    uint2 c_level;          // the level this pass writes: its texels
    uint2 c_from;           // the level it reads: its texels
    uint c_level_at;        // the level's first texel in its buffer
    uint c_from_at;         // the read level's first texel in its buffer
    float c_bloom;          // the share of the light the lens scatters
    uint c_flags;
    uint c_levels;          // the pyramid's levels
    uint c_subframe;        // a sub-frame's light slot + 1: the shafts' sun turns into its camera (c_reproject); 0 none
    float4 c_sun;           // the visible sun on the picture (pixels), the shafts' strength (0: none), the picture's height
    uint2 c_quarter;        // the shafts' texels (a quarter of the picture's side)
    float c_far;            // the depth of the far plane (sky)
    float c_sky_distance;   // eye distance beyond which surfaces are the sky's backdrops
    float4 c_depth_params;  // D3D projection [2][2], [2][3]; the viewport's min and max depth
    float4 c_sun_dir;       // towards the sun in eye space (xyz); the air's Henyey-Greenstein asymmetry (w)
    float4 c_proj;          // D3D projection [0][0], [0][2], [1][1], [1][2]
    float4 c_viewport;      // the viewport (x, y, width, height; pixels)
};
Texture2D<float4> c_picture : register(t0);           // the scene's picture (its colour view)
Texture2D<float> c_depth : register(t1);              // its depth (the conversion table's second view)
StructuredBuffer<uint2> c_bloom_in : register(t2);    // the upward pyramid (the finish reads its first level)
RWStructuredBuffer<uint2> c_down : register(u0);      // the downward pyramid, all levels (half-float RGB)
RWStructuredBuffer<uint2> c_up : register(u1);        // the upward pyramid
// Per light slot, a sub-frame's eye space into its traced frame's (3x4 rows; SceneRayTracer::write_reprojection).
StructuredBuffer<float4> c_reproject : register(t3);

float3 srgb_decode(float3 c)
{
    c = max(c, 0.0f);
    return lerp(pow((c + 0.055f) / 1.055f, 2.4f), c / 12.92f, step(c, 0.04045f));
}

float3 srgb_encode(float3 c)
{
    c = max(c, 0.0f);
    return lerp(1.055f * pow(c, 1.0f / 2.4f) - 0.055f, 12.92f * c, step(c, 0.0031308f));
}

uint2 pack_half3(float3 c)
{
    c = min(max(c, 0.0f), 60000.0f);
    return uint2(f32tof16(c.r) | (f32tof16(c.g) << 16u), f32tof16(c.b));
}

float3 unpack_half3(uint2 v)
{
    return float3(f16tof32(v.x), f16tof32(v.x >> 16u), f16tof32(v.y));
}

// The picture's light at a pixel (clamped to its edge).
float3 picture_light(int2 p)
{
    return srgb_decode(c_picture.Load(int3(clamp(p, int2(0, 0), int2(c_size) - 1), 0)).rgb);
}

float3 down_at(int2 p)
{
    const int2 q = clamp(p, int2(0, 0), int2(c_from) - 1);
    return unpack_half3(c_down[c_from_at + uint(q.y) * c_from.x + uint(q.x)]);
}

// A level's light at continuous texel coordinates (texel centres at integers), bilinear.
float3 level_bilinear(float2 t, bool up_chain)
{
    const int2 b = int2(floor(t));
    const float2 f = t - float2(b);
    float3 c = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 o = int2(k & 1u, k >> 1u);
        const int2 q = clamp(b + o, int2(0, 0), int2(c_from) - 1);
        const uint at = c_from_at + uint(q.y) * c_from.x + uint(q.x);
        const float w = (o.x != 0 ? f.x : 1.0f - f.x) * (o.y != 0 ? f.y : 1.0f - f.y);
        c += (up_chain ? unpack_half3(c_up[at]) : unpack_half3(c_down[at])) * w;
    }
    return c;
}

// Down: the level below's 4 x 4 texels about this texel's footprint, a tent (1 3 3 1 / 8 per axis). The first level
// reads the picture.
[numthreads(8, 8, 1)]
void CameraDownCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= c_level)) {
        return;
    }
    const float tent[4] = {0.125f, 0.375f, 0.375f, 0.125f};
    const int2 base = int2(id.xy) * 2 - 1;
    float3 c = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint j = 0u; j < 4u; ++j) {
        [unroll] for (uint i = 0u; i < 4u; ++i) {
            const int2 p = base + int2(i, j);
            c += (c_from_at == 0xFFFFFFFFu ? picture_light(p) : down_at(p)) * (tent[i] * tent[j]);
        }
    }
    c_down[c_level_at + id.y * c_level.x + id.x] = pack_half3(c);
}

// Up: this level's own light plus the coarser level's (its upward sum) upsampled, a 3 x 3 tent of bilinear taps one
// coarse texel apart. The coarsest level starts the sum (c_from_at ~0).
[numthreads(8, 8, 1)]
void CameraUpCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= c_level)) {
        return;
    }
    const uint at = c_level_at + id.y * c_level.x + id.x;
    float3 c = unpack_half3(c_down[at]);
    if (c_from_at != 0xFFFFFFFFu) {
        const float2 t = (float2(id.xy) + 0.5f) * (float2(c_from) / float2(c_level)) - 0.5f;
        const float tent[3] = {0.25f, 0.5f, 0.25f};
        float3 coarse = float3(0.0f, 0.0f, 0.0f);
        [unroll] for (uint j = 0u; j < 3u; ++j) {
            [unroll] for (uint i = 0u; i < 3u; ++i) {
                coarse += level_bilinear(t + float2(int(i) - 1, int(j) - 1), true) * (tent[i] * tent[j]);
            }
        }
        c += coarse;
    }
    c_up[at] = pack_half3(c);
}

// The sun's shafts at a quarter texel: 32 steps along the line to the sun on the picture (their start jittered per
// texel: no bands), each the sky's light there (sky: the far plane or a backdrop) weighted by its nearness to the sun
// (forward scattering: a seventh of the picture's height, so the sun's glow scatters, not the stars across the sky),
// decaying 4% a step. Into the downward pyramid's first texels (free once the bloom is summed).
//
// A sub-frame replays its traced frame under its own camera: the sun (eye space of the traced frame) turns into the
// sub-frame's by the rotation of c_reproject's rows, which map the sub-frame's eye space into the traced frame's (its
// transpose maps back), and stands where that camera sees it. Behind that camera there are no shafts.
bool shafts_sun(out float3 sun, out float2 at_pixel)
{
    sun = normalize(c_sun_dir.xyz);
    at_pixel = c_sun.xy;
    if (c_subframe == 0u) {
        return true;
    }
    const uint row = (c_subframe - 1u) * 3u;
    sun = normalize(c_reproject[row].xyz * sun.x + c_reproject[row + 1u].xyz * sun.y + c_reproject[row + 2u].xyz * sun.z);
    if (sun.z >= -0.05f) {
        return false;
    }
    const float2 ndc = float2(c_proj.x * sun.x + c_proj.y * sun.z, c_proj.z * sun.y + c_proj.w * sun.z) / -sun.z;
    at_pixel = float2(c_viewport.x + (ndc.x + 1.0f) * 0.5f * c_viewport.z, c_viewport.y + (1.0f - ndc.y) * 0.5f * c_viewport.w);
    return true;
}

[numthreads(8, 8, 1)]
void CameraShaftsCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= c_quarter)) {
        return;
    }
    float3 sun_dir;
    float2 sun_pixel;
    if (!shafts_sun(sun_dir, sun_pixel)) {
        c_down[id.y * c_quarter.x + id.x] = uint2(0u, 0u);
        return;
    }
    const float2 pixel = (float2(id.xy) + 0.5f) * 4.0f;
    const float2 delta = (sun_pixel - pixel) / 32.0f;
    const float reach = 0.14f * c_sun.w;
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float decay = 1.0f;
    // Interleaved gradient noise (Jimenez 2014) for the start.
    const float jitter = frac(52.9829189f * frac(dot(float2(id.xy), float2(0.06711056f, 0.00583715f))));
    float2 at = pixel + delta * (jitter - 1.0f);
    [loop] for (uint i = 0u; i < 32u; ++i) {
        at += delta;
        const int2 s = int2(at);
        if (any(s < 0) || any(s >= int2(c_size))) {
            break;
        }
        const float depth = c_depth.Load(int3(s, 0));
        const float ndc = (depth - c_depth_params.z) / max(c_depth_params.w - c_depth_params.z, 1e-7f);
        const float eye_z = -c_depth_params.y / (ndc + c_depth_params.x);
        if (depth <= c_far + 1e-6f || -eye_z > c_sky_distance) {
            const float2 off = at - sun_pixel;
            // Each sample's light is capped at 8 (exposed): 32 jittered steps per quarter texel either land on the
            // sun's few disc pixels or miss them, and the disc's full radiance would draw dotted streaks across the
            // sky.
            float3 light = picture_light(s);
            light *= min(1.0f, 8.0f / max(dot(light, float3(0.2126f, 0.7152f, 0.0722f)), 1e-4f));
            sum += light * (decay * exp(-dot(off, off) / (reach * reach)));
        }
        decay *= 0.96f;
    }
    // The share of that light the air in front of this pixel's surface sends to the eye: forward scattering by its
    // angle from the sun (Henyey-Greenstein, 1 towards the sun) and the air's depth (the sky's whole column; a near
    // surface little: aerial perspective over the sky's distance).
    const float2 ndc = float2((pixel.x - c_viewport.x) / c_viewport.z * 2.0f - 1.0f,
                              1.0f - (pixel.y - c_viewport.y) / c_viewport.w * 2.0f);
    const float3 eye_dir = normalize(float3((ndc.x + c_proj.y) / c_proj.x, (ndc.y + c_proj.w) / c_proj.z, -1.0f));
    const float g = c_sun_dir.w;
    const float phase = pow(1.0f - g, 3.0f) /
                        pow(max(1.0f + g * g - 2.0f * g * dot(eye_dir, sun_dir), 1e-6f), 1.5f);
    const float own = c_depth.Load(int3(clamp(int2(pixel), int2(0, 0), int2(c_size) - 1), 0));
    float air = 1.0f;
    if (own > c_far + 1e-6f) {
        const float ndc_z = (own - c_depth_params.z) / max(c_depth_params.w - c_depth_params.z, 1e-7f);
        const float distance = c_depth_params.y / (ndc_z + c_depth_params.x) / max(-eye_dir.z, 1e-3f);
        air = distance > c_sky_distance ? 1.0f : 1.0f - exp(-max(distance, 0.0f) / c_sky_distance);
    }
    c_down[id.y * c_quarter.x + id.x] = pack_half3(sum * (c_sun.z / 32.0f * phase * air));
}

// The shafts at a pixel (bilinear in their quarter texels).
float3 shafts_at(int2 p)
{
    const float2 t = (float2(p) + 0.5f) / 4.0f - 0.5f;
    const int2 b = int2(floor(t));
    const float2 f = t - float2(b);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 o = int2(k & 1u, k >> 1u);
        const int2 q = clamp(b + o, int2(0, 0), int2(c_quarter) - 1);
        const float w = (o.x != 0 ? f.x : 1.0f - f.x) * (o.y != 0 ? f.y : 1.0f - f.y);
        sum += unpack_half3(c_down[uint(q.y) * c_quarter.x + uint(q.x)]) * w;
    }
    return sum;
}

// Khronos PBR Neutral's shoulder on each channel alone (no toe: see the header): unchanged below the start, then
// 1 - d^2 / (x + d - start), which meets the line there with its slope.
float3 shoulder_curve(float3 c)
{
    const float start = 0.8f - 0.04f;
    const float d = 1.0f - start;
    return min(c, start) + (1.0f - d * d / (max(c, start) + d - start)) - start;
}

// Khronos PBR Neutral's shoulder and desaturation, the hue kept (no toe: see the header).
float3 neutral_curve(float3 c)
{
    const float start = 0.8f - 0.04f;
    const float desaturation = 0.15f;
    const float peak = max(c.r, max(c.g, c.b));
    if (peak < start) {
        return c;
    }
    const float d = 1.0f - start;
    const float new_peak = 1.0f - d * d / (peak + d - start);
    c *= new_peak / peak;
    const float g = 1.0f - 1.0f / (desaturation * (peak - new_peak) + 1.0f);
    return lerp(c, float3(new_peak, new_peak, new_peak), g);
}

// ACES 1.0 RRT and sRGB ODT, Stephen Hill's fit (sRGB in and out, linear).
float3 aces_curve(float3 c)
{
    const float3x3 input = {
        {0.59719f, 0.35458f, 0.04823f},
        {0.07600f, 0.90834f, 0.01566f},
        {0.02840f, 0.13383f, 0.83777f}};
    const float3x3 output = {
        {1.60475f, -0.53108f, -0.07367f},
        {-0.10208f, 1.10813f, -0.00605f},
        {-0.00327f, -0.07276f, 1.07602f}};
    float3 v = mul(input, c);
    v = (v * (v + 0.0245786f) - 0.000090537f) / (v * (0.983729f * v + 0.4329510f) + 0.238081f);
    return saturate(mul(output, v));
}

float4 CameraVS(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1u) & 2u, id & 2u);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float4 CameraPS(float4 position : SV_Position) : SV_Target0
{
    const int2 p = int2(position.xy);
    const float4 picture = c_picture.Load(int3(p, 0));
    if ((c_flags & kCameraPhysical) == 0u) {
        return saturate(picture);
    }
    float3 c = srgb_decode(picture.rgb);
    if (c_sun.z > 0.0f) {
        c += shafts_at(p);
    }
    if (c_bloom > 0.0f && c_levels != 0u) {
        // The first level's upward sum at this pixel (level texels are two pixels), over the levels summed.
        const float2 t = (float2(p) + 0.5f) * (float2(c_from) / float2(c_size)) - 0.5f;
        const int2 b = int2(floor(t));
        const float2 f = t - float2(b);
        float3 bloom = float3(0.0f, 0.0f, 0.0f);
        [unroll] for (uint k = 0u; k < 4u; ++k) {
            const int2 o = int2(k & 1u, k >> 1u);
            const int2 q = clamp(b + o, int2(0, 0), int2(c_from) - 1);
            const float w = (o.x != 0 ? f.x : 1.0f - f.x) * (o.y != 0 ? f.y : 1.0f - f.y);
            bloom += unpack_half3(c_bloom_in[uint(q.y) * c_from.x + uint(q.x)]) * w;
        }
        c = lerp(c, bloom / float(c_levels), c_bloom);
    }
    c = (c_flags & kCameraClip) != 0u      ? saturate(c)
        : (c_flags & kCameraAces) != 0u    ? aces_curve(c)
        : (c_flags & kCameraNeutral) != 0u ? neutral_curve(c)
                                           : shoulder_curve(c);
    // Into 8 bits with a dither (interleaved gradient noise, Jimenez 2014; stochastic rounding): bloom, shafts and
    // the shoulder's gradients come out smooth instead of in bands.
    const float u = frac(52.9829189f * frac(dot(float2(p), float2(0.06711056f, 0.00583715f))));
    const float3 level = floor(saturate(srgb_encode(c)) * 255.0f + u);
    return float4(level / 255.0f, saturate(picture.a));
}

// The carry into the HDR scene EFB: the RGBA8 EFB's contents so far, as they are.
float4 CameraWidenPS(float4 position : SV_Position) : SV_Target0
{
    return c_picture.Load(int3(int2(position.xy), 0));
}
