// The camera of the ray-traced picture (ray_tracing_camera.cpp; renderer_d3d12.cpp begin_hdr_scene / end_hdr_scene).
//
// While ray tracing runs, the scene layer draws into an HDR EFB (RGBA16F): the traced light is unclamped, the game's
// own draws keep their values (the TEV output is clamped and quantised as on the hardware; their blends add past 1),
// and every value is sRGB-encoded, past 1 as below it, so the game's blends behave as they do in the RGBA8 EFB. At
// the scene's end the camera turns it into the RGBA8 EFB's picture, once, as a camera does:
//
// - Bloom: the light a lens scatters around what it images. The picture's light (linear) down a pyramid of halving
//   levels (a 4 x 4 tent each), then back up (each level plus the coarser one upsampled with a 3 x 3 tent of bilinear
//   taps; Jimenez 2014, "Next Generation Post Processing in Call of Duty: Advanced Warfare"): the sum of the levels is
//   the picture blurred at every scale, normalised. A share of every pixel's light (c_bloom) leaves for it: energy is
//   kept, nothing is thresholded (a bright lamp glows because it is bright, not because a sprite was drawn).
// - Tone curve: the light to the display's range. Khronos PBR Neutral (Khronos 2024; three.js, Blender): below 0.76
//   the light passes unchanged (the game's colours as drawn), above it a shoulder that reaches white only at infinity,
//   and the brightest light loses its saturation towards white (a lamp's core burns white, not a saturated blob).
//   The standard's toe (darkening the darkest tones by up to 0.04) is left out: the game's darks stay as drawn.
//   c_flags kCameraAces: ACES 1.0 (RRT and sRGB ODT, Hill's fit, as Unreal's filmic curve), for comparison.
// Without physical light (c_flags 0) the picture passes as it is, clamped: as the RGBA8 EFB would have kept it.

static const uint kCameraPhysical = 1u;
static const uint kCameraAces = 2u;
static const uint kCameraClip = 4u;    // GALAXY_RT_TONEMAP=2: the light clipped at 1 (no curve), for comparison

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
    uint c_pad;
};
Texture2D<float4> c_picture : register(t0);           // the scene's picture (the conversion views: colour, then depth)
StructuredBuffer<uint2> c_bloom_in : register(t1);    // the upward pyramid (the finish reads its first level)
RWStructuredBuffer<uint2> c_down : register(u0);      // the downward pyramid, all levels (half-float RGB)
RWStructuredBuffer<uint2> c_up : register(u1);        // the upward pyramid

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

// Khronos PBR Neutral's shoulder and desaturation (no toe: see the header).
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
    c = (c_flags & kCameraClip) != 0u ? saturate(c) : (c_flags & kCameraAces) != 0u ? aces_curve(c) : neutral_curve(c);
    return float4(srgb_encode(c), saturate(picture.a));
}

// The carry into the HDR scene EFB: the RGBA8 EFB's contents so far, as they are.
float4 CameraWidenPS(float4 position : SV_Position) : SV_Target0
{
    return c_picture.Load(int3(int2(position.xy), 0));
}
