// SPDX-License-Identifier: GPL-3.0-only
// Ray-traced reflections of the game frame's metal, glossy and water surfaces (DXR 1.1 inline
// ray queries, cs_6_5) and their composite into the EFB (vs/ps_6_0). Compiled by DXC at build time.
//
// The lighting's samples (one per g_factor x g_factor EFB pixels) with the triangle its camera ray
// met: the draw's GX surface class (rt_surfaces.h) selects the lobe width, Fresnel reflectance,
// strength and cover, which the material table's roughness and metalness for the draw's texture
// replace (rt_materials.h), the vertex normals give the shading normal. One reflection ray samples the
// GGX distribution of visible normals. Where it hits geometry this frame shows, it brings that
// pixel's colour times this frame's ray-traced lighting there. Elsewhere the environment memory
// answers: per world direction from the camera, the colour and distance the frame showed there
// last (refreshed every frame, fading where the camera no longer looks); a hit it agrees with brings
// that colour, a ray that leaves the scene the sky it showed. Light nothing knows has no confidence,
// and confidence weights the light and scales the reflection: the surface keeps its own colour
// where the reflected light is unknown. Metal reflections take the hue of the surface; surfaces
// bright under this frame's lighting (lamps, lava, glowing cores and what glows light) reflect
// nothing.
// Results accumulate along the reflected image's motion (through the virtual image point for sharp
// lobes, the surface for wide ones), clamped to the spread of the known light around them, then pass
// two edge-aware a-trous filters as wide as the lobe. The composite adds k * reflection and takes away
// k * (cover + (1 - cover) * reflection) of the surface's own colour (cover 1: a blend towards the
// reflection; cover 0: a screen, never darker than the game's own shading), k the class strength
// times Schlick's Fresnel term at the pixel's own view angle times the sample's weight; sub-frames
// first move the pixel to the traced frame's camera.

static const float kPi = 3.14159265f;
static const float kGlowCutoff = 0.004f;   // rt_lighting.hlsl g_emit.x (GALAXY_RT_GLOW_CUTOFF default)
static const float kGlowReach = 6000.0f;   // ... g_emit.y (GALAXY_RT_GLOW_REACH default)
static const uint kNoSurface = 15u;
static const uint kProbeSize = 64u;
static const float kSkyDistance = 1.0e9f;

// ---------------------------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------------------------

float3 pixel_direction(float4 projection, float4 viewport, float2 pixel)
{
    const float ndc_x = (pixel.x - viewport.x) / viewport.z * 2.0f - 1.0f;
    const float ndc_y = 1.0f - (pixel.y - viewport.y) / viewport.w * 2.0f;
    return float3((ndc_x + projection.y) / projection.x, (ndc_y + projection.w) / projection.z, -1.0f);
}

float3 eye_position(float4 projection, float4 viewport, float2 pixel, float z)
{
    return pixel_direction(projection, viewport, pixel) * -z;
}

float2 eye_to_pixel(float4 projection, float4 viewport, float3 p)
{
    const float w = -p.z;
    const float ndc_x = (projection.x * p.x + projection.y * p.z) / w;
    const float ndc_y = (projection.z * p.y + projection.w * p.z) / w;
    return float2(viewport.x + (ndc_x + 1.0f) * 0.5f * viewport.z, viewport.y + (1.0f - ndc_y) * 0.5f * viewport.w);
}

float depth_to_eye_z(float4 depth_params, float depth)
{
    const float ndc = (depth - depth_params.z) / max(depth_params.w - depth_params.z, 1e-7f);
    return -depth_params.y / (ndc + depth_params.x);
}

bool far_plane(float4 depth_params, float depth)
{
    return depth <= depth_params.z + 1e-6f;
}

bool inside(float4 viewport, float2 c)
{
    return c.x >= viewport.x && c.x < viewport.x + viewport.z && c.y >= viewport.y && c.y < viewport.y + viewport.w;
}

float3 transform(float4 rows[3], float3 p)
{
    const float4 h = float4(p, 1.0f);
    return float3(dot(rows[0], h), dot(rows[1], h), dot(rows[2], h));
}

float3 rotate(float4 rows[3], float3 v)
{
    return float3(dot(rows[0].xyz, v), dot(rows[1].xyz, v), dot(rows[2].xyz, v));
}

float luminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

// Octahedral map of directions to [0, 1]^2.
float2 octahedral(float3 d)
{
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    float2 e = d.xy;
    if (d.z < 0.0f) {
        e = (1.0f - abs(d.yx)) * float2(d.x >= 0.0f ? 1.0f : -1.0f, d.y >= 0.0f ? 1.0f : -1.0f);
    }
    return e * 0.5f + 0.5f;
}

float3 octahedral_direction(float2 uv)
{
    const float2 e = uv * 2.0f - 1.0f;
    float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
    const float t = saturate(-n.z);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return normalize(n);
}

uint pack_normal(float3 n)
{
    const float2 e = octahedral(n);
    const uint2 u = uint2(round(saturate(e) * 65535.0f));
    return u.x | (u.y << 16u);
}

float3 unpack_normal(uint v)
{
    return octahedral_direction(float2(float(v & 0xFFFFu), float(v >> 16u)) / 65535.0f);
}

uint2 pack_rgb_w(float3 c, float w)
{
    return uint2(f32tof16(c.r) | (f32tof16(c.g) << 16u), f32tof16(c.b) | (f32tof16(w) << 16u));
}

float4 unpack_rgb_w(uint2 p)
{
    return float4(f16tof32(p.x), f16tof32(p.x >> 16u), f16tof32(p.y), f16tof32(p.y >> 16u));
}

// Non-negative RGB in 11-11-10 bits (the half's exponent and leading mantissa bits).
uint pack_r11g11b10(float3 c)
{
    c = max(c, 0.0f);
    return ((f32tof16(c.r) >> 4u) & 0x7FFu) | (((f32tof16(c.g) >> 4u) & 0x7FFu) << 11u) |
           (((f32tof16(c.b) >> 5u) & 0x3FFu) << 22u);
}

float3 unpack_r11g11b10(uint p)
{
    return float3(f16tof32((p & 0x7FFu) << 4u), f16tof32(((p >> 11u) & 0x7FFu) << 4u),
                  f16tof32(((p >> 22u) & 0x3FFu) << 5u));
}

float3 to_ycocg(float3 c)
{
    return float3(0.25f * c.r + 0.5f * c.g + 0.25f * c.b, 0.5f * c.r - 0.5f * c.b,
                  -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}

float3 from_ycocg(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

// Schlick's Fresnel term for a lobe of GGX alpha `alpha` (its grazing gain falls as it widens).
float fresnel(float f0, float alpha, float cos_theta)
{
    const float f90 = max(1.0f - sqrt(alpha), f0);
    const float m = 1.0f - saturate(cos_theta);
    const float m2 = m * m;
    return f0 + (f90 - f0) * (m2 * m2 * m);
}

// ---------------------------------------------------------------------------------------------
// Environment memory, trace, accumulation and filter
// ---------------------------------------------------------------------------------------------

cbuffer RtReflect : register(b0)
{
    float4 g_projection;        // GX projection A, B, C, D
    float4 g_depth;             // D3D projection [2][2], [2][3]; viewport min depth, max depth
    float4 g_viewport;          // left, top, width, height (EFB pixels)
    uint2 g_target_size;        // EFB pixels
    uint2 g_sample_size;        // samples
    float g_near;               // near-plane distance (eye units)
    uint g_frame;               // sample sequence index
    uint g_factor;              // EFB pixels per sample side
    uint g_flags;               // bit 0: previous samples usable; bit 2: picture sums usable;
                                // bit 3: camera known (environment memory); bit 4: glow lights;
                                // bit 5: the water pass (the EFB colour is the lit picture);
                                // bit 6: the lighting's sky map usable
    float4 g_reproject[3];      // previous samples' eye position from this frame's (3x4 rows)
    float4 g_world_to_eye[3];   // the camera's rotation (rows)
    float4 g_eye_to_world[3];   // its inverse
    float4 g_class[8];          // per surface class: GGX alpha, strength, F0, metal
    float g_range;              // reflection ray length (eye units)
    float g_lobe;               // share of the lobe sampled (1 the whole lobe)
    uint g_history_frames;      // accumulation limit of the widest lobes
    float g_fallback;           // confidence of the scene's average light for hits nothing showed
    float g_memory_fade;        // environment memory kept per frame where the camera does not look
    float g_ripple;             // water: normal slope per unit of the picture's relative luminance slope
    float g_probe;              // confidence of the environment memory along a ray whose hit nothing showed
    float g_sky_distance;       // farther surfaces are the sky (the lighting's backdrops)
    // The lighting's lights (rt_lighting.hlsl RtLight) for the highlights.
    float4 g_sun;               // towards the sun (xyz), tangent of its radius (w)
    float4 g_sun_light;         // sun light (rgb), how far its shadow rays reach (w)
    float4 g_points[16];        // per point light: position, reach; colour, 1 / half-light distance^2
    uint g_point_count;
    float g_point_light;        // the point lights' weight
    float g_point_size;         // radius of the emitter around a point light
    float g_shafts;             // under the water: the sunlight its waves scatter towards the camera
    float g_sky_fade;           // environment memory of the sky kept per frame where the camera does not look
    float g_sky_average;        // confidence of the sky's average colour where no sky is known
    float g_metal_sky;          // metal: share of the sky's reflection over its own environment map
    float g_local_gain;         // the local lights' highlights (point and glow lights)
    uint g_draw_count;          // entries of g_draws (g_flags bit 7)
    float g_rough_cutoff;       // the material table's roughness above which a surface reflects nothing
    float g_metal_f0;           // Fresnel reflectance of a bare metal (metalness 1)
    float g_waves;              // water: slope of its procedural waves (0: none)
};

RaytracingAccelerationStructure g_scene : register(t0);
ByteAddressBuffer g_positions : register(t1);       // per triangle three eye-space vertices
// Per triangle, 16 bytes: vertex normals (octahedral 16:16, 1-65535 per axis, 0 none), draw flags.
ByteAddressBuffer g_surfaces : register(t2);
StructuredBuffer<uint4> g_light : register(t3);     // this frame's lighting (rt_lighting.hlsl SpatialBCS)
Texture2D<float4> g_color : register(t4);           // EFB colour of the opaque scene
Texture2D<float> g_efb_depth : register(t5);
StructuredBuffer<uint4> g_prev_info : register(t6);
StructuredBuffer<uint4> g_prev_history : register(t7);
ByteAddressBuffer g_sums : register(t8);            // previous picture: sky r g b n, scene r g b n (x 1/255)
StructuredBuffer<uint4> g_history_in : register(t9);
// Glow lights (rt_lighting.hlsl g_emitters): count, total power; per light world position, radius;
// power (rgb), cumulative power. Read when g_flags bit 4 is set.
ByteAddressBuffer g_emitters : register(t10);
// The translucent water draws (WaterTraceCS): their triangles alone, eye-space vertices, surface records.
RaytracingAccelerationStructure g_water : register(t11);
ByteAddressBuffer g_water_positions : register(t12);
ByteAddressBuffer g_water_surfaces : register(t13);
// The lighting's sky map (rt_lighting.hlsl SkyLightMapCS): per world direction (octahedral, +y the
// pole, 32 x 32 bins) the sky's colour over the stage and whether it was seen.
StructuredBuffer<float4> g_sky_map : register(t14);
// Per draw (rt_lighting.hlsl g_draws, kDrawStride bytes): word 4 the material table's roughness and
// metalness (unorm16 pair, 0xFFFF: the class's). Read when g_flags bit 7 is set.
ByteAddressBuffer g_draws : register(t15);
SamplerState g_linear : register(s0);
// Per sample: packed normal, eye z (0: no surface), material (pack_material: class, strength,
// emitter-free share, roughness, metalness), reflection distance.
RWStructuredBuffer<uint4> g_info : register(u0);
// Per sample: reflected light (rgb) and its confidence, the lights' highlight (rgb) (halves).
RWStructuredBuffer<uint4> g_raw : register(u1);
// Per sample: accumulated light and confidence, highlight and frames (halves).
RWStructuredBuffer<uint4> g_history : register(u2);
// Per sample for the composite: light (11-11-10), normal (12:12) and weight (8 bits), eye z with the
// class in its low 4 bits, highlight (11-11-10).
RWStructuredBuffer<uint4> g_final : register(u3);
RWByteAddressBuffer g_stats : register(u4);
// Per world direction from the camera (octahedral, kProbeSize^2): light and its freshness (halves),
// distance, unused.
RWStructuredBuffer<uint4> g_memory : register(u5);
// Per sample, from the lighting's camera ray (rt_lighting.hlsl TraceCS): triangle + 1 (0 none) and
// its barycentrics (halves). Read only.
RWStructuredBuffer<uint2> g_primary : register(u6);
// Per tile of 8x8 samples: 1 where a sample reflects.
RWStructuredBuffer<uint> g_tiles : register(u7);
// Per sample, the picture under the translucent phases for the water's refraction: lit colour
// (11-11-10) and eye z (0: the sky).
RWStructuredBuffer<uint2> g_under : register(u8);

int2 sample_pixel(uint2 s)
{
    return int2(min(s * g_factor, g_target_size - 1u));
}

float3 sample_position(uint2 s, float z)
{
    return eye_position(g_projection, g_viewport, float2(sample_pixel(s)) + 0.5f, z);
}

float2 sample_point(uint2 s)
{
    const float ign = frac(52.9829189f * frac(0.06711056f * float(s.x) + 0.00583715f * float(s.y)));
    const float n = float(g_frame % 4096u);
    return frac(float2(ign, frac(ign * 1.61803399f)) + n * float2(0.7548776662f, 0.5698402910f));
}

// Further decorrelated points of the same sequence.
float2 sample_point(uint2 s, uint k)
{
    return frac(sample_point(s) + float(k) * float2(0.6180339887f, 0.4142135624f));
}

// Stats words: 0 samples on geometry, 1 with a traced triangle, 2-6 per class (diffuse, glossy,
// metal, unlit, water), 7 reflection rays, 8-14 their sources (Reflected.source: hits this frame
// shows, the memory knows, nothing knows, the memory along the ray knows; sky this frame shows, the
// memory knows, the average), 15 samples with a highlight. The water pass: 16 samples, 17 on
// water, 18 reflection rays, 19-25 their sources, 26 samples with a highlight, 27 under the water.
void count(uint slot, bool condition)
{
    const uint n = WaveActiveCountBits(condition);
    if (WaveIsFirstLane() && n != 0u) {
        g_stats.InterlockedAdd(slot * 4u, n);
    }
}

// GGX distribution of visible normals (view `v` in the tangent frame, z the normal).
float3 sample_vndf(float3 v, float alpha, float2 u)
{
    const float3 vh = normalize(float3(alpha * v.x, alpha * v.y, v.z));
    const float lensq = vh.x * vh.x + vh.y * vh.y;
    const float3 t1 = lensq > 0.0f ? float3(-vh.y, vh.x, 0.0f) * rsqrt(lensq) : float3(1.0f, 0.0f, 0.0f);
    const float3 t2 = cross(vh, t1);
    const float r = sqrt(u.x);
    const float phi = 2.0f * kPi * u.y;
    const float p1 = r * cos(phi);
    float p2 = r * sin(phi);
    const float s = 0.5f * (1.0f + vh.z);
    p2 = (1.0f - s) * sqrt(max(0.0f, 1.0f - p1 * p1)) + s * p2;
    const float3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0f, 1.0f - p1 * p1 - p2 * p2)) * vh;
    return normalize(float3(alpha * nh.x, alpha * nh.y, max(1e-6f, nh.z)));
}

// This frame's lighting at an EFB pixel (its sample's multiplier).
float3 lighting_at(int2 pixel)
{
    const uint2 s = min(uint2(pixel) / g_factor, g_sample_size - 1u);
    const uint4 m = g_light[s.y * g_sample_size.x + s.x];
    if (asfloat(m.z) == 0.0f) {
        return float3(1.0f, 1.0f, 1.0f);
    }
    return float3(f16tof32(m.x), f16tof32(m.x >> 16u), f16tof32(m.y));
}

// ray_tracing.hlsl pack_vertex_normal: octahedral, 1-65535 per axis.
float3 unpack_vertex_normal(uint v)
{
    return octahedral_direction((float2(float(v & 0xFFFFu), float(v >> 16u)) - 1.0f) / 65534.0f);
}

// The previous picture's average sky (offset 0) or scene (16) colour; false when it has none.
bool average(uint offset, out float3 light)
{
    light = float3(0.0f, 0.0f, 0.0f);
    if ((g_flags & 4u) == 0u) {
        return false;
    }
    const uint4 sum = g_sums.Load4(offset);
    light = float3(sum.xyz) / (255.0f * float(max(sum.w, 1u)));
    return sum.w != 0u;
}

// What the frame shows at `pixel`: the lit colour and the distance of its surface (kSkyDistance
// for the sky); false outside the viewport.
bool shown_at(float2 pixel, out float3 light, out float distance)
{
    light = float3(0.0f, 0.0f, 0.0f);
    distance = kSkyDistance;
    if (!inside(g_viewport, pixel)) {
        return false;
    }
    const int2 c = int2(pixel);
    const float depth = g_efb_depth.Load(int3(c, 0));
    light = g_color.SampleLevel(g_linear, pixel / float2(g_target_size), 0.0f).rgb;
    if (!far_plane(g_depth, depth)) {
        distance = length(eye_position(g_projection, g_viewport, pixel, depth_to_eye_z(g_depth, depth)));
        if (distance >= g_sky_distance) {
            distance = kSkyDistance;
        } else if ((g_flags & 32u) == 0u) {
            light *= lighting_at(c);
        }
    }
    return true;
}

// The environment memory: each direction from the camera the frame shows takes four jittered
// looks into it; directions out of view fade.
[numthreads(8, 8, 1)]
void ReflectMemoryCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= kProbeSize)) {
        return;
    }
    const uint index = id.y * kProbeSize + id.x;
    const uint4 stored = g_memory[index];
    float4 memory = unpack_rgb_w(stored.xy);
    float distance = asfloat(stored.z);
    // A new stage (g_flags bit 8): nothing remembered (the last stage's colours showed in reflections).
    if ((g_flags & 256u) != 0u) {
        memory = float4(0.0f, 0.0f, 0.0f, 0.0f);
        distance = kSkyDistance;
    }
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float nearest = kSkyDistance;
    float looks = 0.0f;
    if ((g_flags & 8u) != 0u) {
        const float2 u = sample_point(id.xy);
        [unroll] for (uint k = 0u; k < 4u; ++k) {
            const float2 jitter = frac(u + float2(k & 1u, k >> 1u) * 0.5f);
            const float3 d = rotate(g_world_to_eye, octahedral_direction((float2(id.xy) + jitter) / float(kProbeSize)));
            float3 light;
            float seen;
            if (d.z < -1e-3f && shown_at(eye_to_pixel(g_projection, g_viewport, d), light, seen)) {
                sum += light;
                nearest = min(nearest, seen);
                looks += 1.0f;
            }
        }
    }
    if (looks > 0.0f) {
        const float3 light = sum / looks;
        memory.rgb = memory.w > 0.0f ? lerp(memory.rgb, light, 0.35f) : light;
        memory.w = 1.0f;
        distance = nearest;
    } else {
        // The sky does not move with the camera: it is remembered far longer.
        memory.w *= distance >= kSkyDistance ? g_sky_fade : g_memory_fade;
    }
    g_memory[index] = uint4(pack_rgb_w(memory.rgb, memory.w), asuint(distance), 0u);
}

// The environment memory towards eye-space point or direction `x` from the camera: its light and
// how far it agrees (the freshness, and for a point at `distance` the share of a matching distance;
// distance 0: whatever it saw there).
float memory_light(float3 x, float distance, out float3 light)
{
    light = float3(0.0f, 0.0f, 0.0f);
    if ((g_flags & 8u) == 0u) {
        return 0.0f;
    }
    const float2 uv = octahedral(rotate(g_eye_to_world, normalize(x)));
    const uint2 texel = min(uint2(uv * float(kProbeSize)), kProbeSize - 1u);
    const uint4 stored = g_memory[texel.y * kProbeSize + texel.x];
    const float4 memory = unpack_rgb_w(stored.xy);
    const float seen = asfloat(stored.z);
    light = memory.rgb;
    if (distance <= 0.0f) {
        return memory.w;
    }
    if (distance >= kSkyDistance) {
        return seen >= kSkyDistance ? memory.w : 0.0f;
    }
    return seen < kSkyDistance ? memory.w * saturate(1.5f - abs(seen - distance) / (0.4f * distance)) : 0.0f;
}

// The sky the lighting's map holds towards eye-space direction `d` (bilinear over its bins): its
// light and the share of it seen.
float sky_map_light(float3 d, out float3 light)
{
    light = float3(0.0f, 0.0f, 0.0f);
    if ((g_flags & 64u) == 0u) {
        return 0.0f;
    }
    const float3 w = normalize(rotate(g_eye_to_world, d));
    float2 p = w.xz / (abs(w.x) + abs(w.y) + abs(w.z));
    if (w.y < 0.0f) {
        p = (1.0f - abs(p.yx)) * float2(p.x >= 0.0f ? 1.0f : -1.0f, p.y >= 0.0f ? 1.0f : -1.0f);
    }
    const float2 a = clamp((p * 0.5f + 0.5f) * 32.0f - 0.5f, 0.0f, 31.0f);
    const uint2 base = min(uint2(a), 30u);
    const float2 f = a - float2(base);
    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float weight = 0.0f;
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const uint2 b = base + uint2(k & 1u, k >> 1u);
        const float bw = ((k & 1u) != 0u ? f.x : 1.0f - f.x) * ((k >> 1u) != 0u ? f.y : 1.0f - f.y);
        const float4 bin = g_sky_map[b.y * 32u + b.x];
        sum += bin.w > 0.0f ? float4(bin.rgb, 1.0f) * bw : float4(0.0f, 0.0f, 0.0f, 0.0f);
        weight += bw;
    }
    light = sum.w > 1e-4f ? sum.rgb / sum.w : float3(0.0f, 0.0f, 0.0f);
    return sum.w / max(weight, 1e-4f);
}

// The game's distance attenuation of point light i (rt_lighting.hlsl point_falloff).
float point_falloff(float d2, uint i)
{
    const float x = d2 / (g_points[2u * i].w * g_points[2u * i].w);
    const float window = saturate(1.0f - x * x);
    return window * window / (1.0f + d2 * g_points[2u * i + 1u].w);
}

// GGX highlight of a light in direction `l` whose radius subtends `spread` (tangent): Fresnel times
// the lobe times n.l times pi, so the light's irradiance E (a white diffuse surface shows E n.l)
// gives its reflected light. The lobe widens by the light's size, its peak lowered to keep its
// energy (Karis 2013, sphere lights).
float ggx_highlight(float3 n, float3 v, float3 l, float alpha, float spread, float f0)
{
    const float nl = dot(n, l);
    if (nl <= 0.0f) {
        return 0.0f;
    }
    const float wide = saturate(alpha + 0.5f * spread);
    const float a2 = wide * wide;
    const float3 h = normalize(v + l);
    const float nh = saturate(dot(n, h));
    const float nv = max(dot(n, v), 1e-4f);
    const float q = nh * nh * (a2 - 1.0f) + 1.0f;
    const float d = alpha * alpha / (kPi * q * q);
    const float visibility = 0.5f / (nl * sqrt(nv * nv * (1.0f - a2) + a2) + nv * sqrt(nl * nl * (1.0f - a2) + a2));
    const float m = 1.0f - saturate(dot(v, h));
    const float m2 = m * m;
    return kPi * d * visibility * nl * (f0 + (1.0f - f0) * (m2 * m2 * m));
}

// A light in the glow lights' list (rt_lighting.hlsl kEmitStride): position, radius; power, cumulative
// power; spot direction, cosine of its cone.
static const uint kEmitStride = 48u;
static const uint kMetalClass = 2u;
static const float kDielectricF0 = 0.04f;

// A spot light's share along `from_light` (normalised, from the light): full inside the inner part of
// its cone, none outside it (`spot` w: the cosine of its half angle; xyz 0: a point light).
float spot_factor(float4 spot, float3 from_light)
{
    const float length2 = dot(spot.xyz, spot.xyz);
    if (length2 < 0.25f) {
        return 1.0f;
    }
    const float c = dot(spot.xyz, from_light) * rsqrt(length2);
    return smoothstep(spot.w, lerp(spot.w, 1.0f, 0.35f), c);
}

bool lit_from(float3 origin, float3 direction, float length)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = max(length, 0.0f);
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>
        query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
    query.Proceed();
    return query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

// The highlights the scene's lights put on a surface at `p` (ray origin `origin`): the sun with one
// shadow ray; the local lights (the game's point lights and every glow light within reach: Lumas,
// lamps, stars, fire) each by its irradiance at `p` (power / (pi (d^2 + r^2)), windowed to its reach
// as the lighting's) times its GGX lobe, all of them, times g_local_gain; the strongest one's shadow
// ray decides whether it counts (one ray, no noise from choosing).
// ---------------------------------------------------------------------------------------------
// Tile light lists: each group of 8 x 8 samples keeps the glow lights (the highlights') whose reach meets the box
// around its samples' surface points; its samples weigh only those. Lights beyond a tile's reach
// add nothing to any of its samples, so the result is the same at a fraction of the work.
// ---------------------------------------------------------------------------------------------

static const uint kTileLights = 128u;

groupshared uint s_tile_bounds[6];      // min x y z, max x y z (order-preserving uints)
groupshared uint s_tile_count;
groupshared uint s_tile_lights[kTileLights];

uint ordered(float f)
{
    const uint u = asuint(f);
    return (u & 0x80000000u) != 0u ? ~u : u | 0x80000000u;
}

float unordered(uint u)
{
    return asfloat((u & 0x80000000u) != 0u ? u & 0x7FFFFFFFu : ~u);
}

// All 64 threads of the group call it once (it holds group barriers); `valid`: the thread's sample
// shows a surface at eye-space `p`. Lights: kEmitStride records from byte 16 of `lights` (count at
// 0), positions in world space (`to_eye`), reach from their power (`reach2_of`).
#define CULL_TILE_LIGHTS(lights, to_eye, gi, valid, p, reach2_of)                                       \
    {                                                                                                    \
        if ((gi) == 0u) {                                                                                \
            s_tile_bounds[0] = 0xFFFFFFFFu; s_tile_bounds[1] = 0xFFFFFFFFu; s_tile_bounds[2] = 0xFFFFFFFFu; \
            s_tile_bounds[3] = 0u; s_tile_bounds[4] = 0u; s_tile_bounds[5] = 0u;                        \
            s_tile_count = 0u;                                                                           \
        }                                                                                                \
        GroupMemoryBarrierWithGroupSync();                                                               \
        if (valid) {                                                                                     \
            InterlockedMin(s_tile_bounds[0], ordered((p).x));                                            \
            InterlockedMin(s_tile_bounds[1], ordered((p).y));                                            \
            InterlockedMin(s_tile_bounds[2], ordered((p).z));                                            \
            InterlockedMax(s_tile_bounds[3], ordered((p).x));                                            \
            InterlockedMax(s_tile_bounds[4], ordered((p).y));                                            \
            InterlockedMax(s_tile_bounds[5], ordered((p).z));                                            \
        }                                                                                                \
        GroupMemoryBarrierWithGroupSync();                                                               \
        if (s_tile_bounds[0] != 0xFFFFFFFFu) {                                                           \
            const float3 lo = float3(unordered(s_tile_bounds[0]), unordered(s_tile_bounds[1]),           \
                                     unordered(s_tile_bounds[2]));                                       \
            const float3 hi = float3(unordered(s_tile_bounds[3]), unordered(s_tile_bounds[4]),           \
                                     unordered(s_tile_bounds[5]));                                       \
            const uint total = (lights).Load(0u);                                                        \
            for (uint c = (gi); c < total; c += 64u) {                                                   \
                const float4 la = asfloat((lights).Load4(16u + c * kEmitStride));                        \
                const float3 power = asfloat((lights).Load3(16u + c * kEmitStride + 16u));               \
                const float3 e = transform(to_eye, la.xyz);                                              \
                const float3 out_of_box = max(max(lo - e, e - hi), 0.0f);                                \
                if (dot(out_of_box, out_of_box) < reach2_of(luminance(power))) {                         \
                    uint slot;                                                                           \
                    InterlockedAdd(s_tile_count, 1u, slot);                                              \
                    if (slot < kTileLights) {                                                            \
                        s_tile_lights[slot] = c;                                                         \
                    }                                                                                    \
                }                                                                                        \
            }                                                                                            \
        }                                                                                                \
        GroupMemoryBarrierWithGroupSync();                                                               \
    }

float glow_reach2(float light)
{
    return min(light / (kPi * kGlowCutoff), kGlowReach * kGlowReach);
}

float3 highlights(uint2 s, float3 p, float3 origin, float3 n, float3 v, float alpha, float f0, bool tiled)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    if (any(g_sun_light.rgb > 0.0f)) {
        const float k = ggx_highlight(n, v, g_sun.xyz, alpha, g_sun.w, f0);
        if (k > 1e-4f && lit_from(origin, g_sun.xyz, g_sun_light.w)) {
            sum += g_sun_light.rgb * k;
        }
    }
    float3 local = float3(0.0f, 0.0f, 0.0f);
    float3 strongest = float3(0.0f, 0.0f, 0.0f);
    float strongest_weight = 0.0f;
    float3 strongest_to = float3(0.0f, 0.0f, 1.0f);
    float strongest_end = 0.0f;
    [loop] for (uint i = 0u; i < g_point_count; ++i) {
        const float3 to = g_points[2u * i].xyz - p;
        const float d = length(to);
        const float3 c = g_points[2u * i + 1u].rgb * (point_falloff(d * d, i) * g_point_light) *
                         ggx_highlight(n, v, to / max(d, 1e-4f), alpha, g_point_size / max(d, 1e-4f), f0);
        local += c;
        if (luminance(c) > strongest_weight) {
            strongest_weight = luminance(c);
            strongest = c;
            strongest_to = to;
            strongest_end = d - g_point_size;
        }
    }
    // `tiled`: the group's tile list (CULL_TILE_LIGHTS); else every light.
    const uint emitters = (g_flags & 16u) == 0u ? 0u : tiled ? min(s_tile_count, kTileLights) : g_emitters.Load(0u);
    if (emitters != 0u) {
        // Its reach as the lighting's (rt_lighting.hlsl emit_reach2): light / (pi d^2) above
        // kGlowCutoff, at most kGlowReach; the lights beyond it add nothing here (the tile's list).
        [loop] for (uint k = 0u; k < emitters; ++k) {
            const uint j = tiled ? s_tile_lights[k] : k;
            const float4 la = asfloat(g_emitters.Load4(16u + j * kEmitStride));
            const float3 power = asfloat(g_emitters.Load3(16u + j * kEmitStride + 16u));
            const float3 to = transform(g_world_to_eye, la.xyz) - p;
            const float d2 = dot(to, to);
            const float reach2 = min(luminance(power) / (kPi * kGlowCutoff), kGlowReach * kGlowReach);
            if (d2 >= reach2) {
                continue;
            }
            const float d = sqrt(d2);
            const float x = d2 / reach2;
            const float4 spot = asfloat(g_emitters.Load4(16u + j * kEmitStride + 32u));
            const float cone = spot_factor(float4(rotate(g_world_to_eye, spot.xyz), spot.w), -to / max(d, 1e-4f));
            if (cone <= 0.0f) {
                continue;
            }
            const float3 c = power * (cone * (1.0f - x * x) * (1.0f - x * x) / (kPi * (d2 + la.w * la.w))) *
                             ggx_highlight(n, v, to / max(d, 1e-4f), alpha, la.w / max(d, 1e-4f), f0);
            local += c;
            if (luminance(c) > strongest_weight) {
                strongest_weight = luminance(c);
                strongest = c;
                strongest_to = to;
                strongest_end = d - max(0.9f * la.w, 2.0f + 0.01f * d);
            }
        }
    }
    if (strongest_weight > 0.0f) {
        const float span = length(strongest_to);
        if (!lit_from(origin, strongest_to / max(span, 1e-4f), strongest_end)) {
            local -= strongest;
        }
    }
    return sum + max(local, 0.0f) * g_local_gain;
}

// Reflection sources (Reflected.source).
static const uint kShown = 0u;
static const uint kFromMemory = 1u;
static const uint kUnknown = 2u;
static const uint kProbe = 3u;
static const uint kSkyShown = 4u;
static const uint kSkyFromMemory = 5u;
static const uint kSkyAverage = 6u;
static const uint kSources = 7u;

// What one reflection ray brings to a surface at `hit` (normal `n`, face `ng`, seen along `dir`):
// light and its confidence, the lights' highlights (`lights`), the ray's length and where its light
// came from.
struct Reflected
{
    float3 light;
    float3 highlight;
    float confidence;
    float distance;
    uint source;
    bool mapped;    // the sky from the lighting's map
};

Reflected reflect_light(uint2 s, float3 dir, float3 n, float3 ng, float3 hit, float4 params, float3 base,
                        bool lights, bool tiled)
{
    Reflected r;
    r.light = float3(0.0f, 0.0f, 0.0f);
    r.highlight = float3(0.0f, 0.0f, 0.0f);
    r.confidence = 0.0f;
    r.distance = g_range;
    r.source = kShown;
    r.mapped = false;
    const float3 v = -dir;
    const float sign_z = n.z >= 0.0f ? 1.0f : -1.0f;
    const float ta = -1.0f / (sign_z + n.z);
    const float tb = n.x * n.y * ta;
    const float3 t1 = float3(1.0f + sign_z * n.x * n.x * ta, sign_z * tb, -sign_z * n.x);
    const float3 t2 = float3(tb, sign_z + n.y * n.y * ta, -n.y);
    const float2 u = sample_point(s);
    const float3 h = sample_vndf(float3(dot(v, t1), dot(v, t2), max(dot(v, n), 1e-4f)), params.x,
                                 float2(u.x * g_lobe, u.y));
    float3 l = reflect(-v, t1 * h.x + t2 * h.y + n * h.z);
    if (dot(l, n) <= 1e-3f) {
        l = reflect(-v, n);
    }
    // Never into the face itself: below its plane the direction mirrors back out.
    const float below = dot(l, ng);
    l = below < 1e-3f ? normalize(l - (2.0f * below - 2e-3f) * ng) : l;
    RayDesc reflection;
    reflection.Origin = hit + ng * (0.2f + abs(hit.z) * 4e-4f);
    reflection.Direction = l;
    reflection.TMin = 0.0f;
    reflection.TMax = g_range;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, reflection);
    query.Proceed();
    r.distance = g_range;
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        r.distance = query.CommittedRayT();
        const float3 x = reflection.Origin + l * r.distance;
        const float x_distance = length(x);
        float seen;
        const bool shown = x.z < -g_near && shown_at(eye_to_pixel(g_projection, g_viewport, x), r.light, seen) &&
                                 abs(seen - x_distance) < 0.03f * x_distance + 2.0f;
        if (shown) {
            r.confidence = 1.0f;
        } else {
            float3 known;
            r.confidence = memory_light(x, x_distance, known);
            r.source = r.confidence > 0.05f ? kFromMemory : kUnknown;
            if (r.source == kFromMemory) {
                r.light = known;
            } else {
                // What the camera saw along the ray's own direction (a probe at the camera: the
                // scene's light without the parallax of the hit), else the surface keeps its own
                // colour unless the scene's average is allowed in (GALAXY_RT_REFLECT_FALLBACK).
                const float along = memory_light(l, 0.0f, known) * g_probe;
                if (along > 0.05f) {
                    r.source = kProbe;
                    r.light = known;
                    r.confidence = along;
                } else {
                    r.confidence = average(16u, r.light) ? g_fallback : 0.0f;
                }
            }
        }
    } else {
        float seen;
        const bool sky_shown = l.z < -1e-3f && shown_at(eye_to_pixel(g_projection, g_viewport, l), r.light, seen) &&
                               seen >= kSkyDistance;
        r.source = kSkyShown;
        if (sky_shown) {
            r.confidence = 1.0f;
        } else {
            float3 known;
            r.confidence = memory_light(l, kSkyDistance, known);
            r.source = r.confidence > 0.05f ? kSkyFromMemory : kSkyAverage;
            if (r.source == kSkyFromMemory) {
                r.light = known;
            } else {
                // The sky the stage has shown in that direction (the lighting's map), else its
                // average.
                const float seen_share = sky_map_light(l, known);
                if (seen_share > 0.05f) {
                    r.source = kSkyFromMemory;
                    r.light = known;
                    r.confidence = 0.8f * seen_share;
                    r.mapped = true;
                } else {
                    r.confidence = average(0u, r.light) ? g_sky_average : 0.0f;
                }
            }
        }
    }
    // Metal keeps most of the game's environment map where its reflection is only the sky: the map
    // is the game's own sky in it (sharper than any known); what the scene puts there replaces it.
    if (params.w > 0.0f && r.source >= kSkyShown) {
        r.confidence *= g_metal_sky;
    }
    // The lights' highlights, weighted like the reflection (its class strength) so the
    // composite's Fresnel weight is divided out here.
    if (lights) {
        r.highlight = highlights(s, hit, reflection.Origin, n, v, params.x, params.z, tiled) * params.y;
    }
    if (params.w > 0.0f) {
        // Metal: the reflection takes the hue (luminance 1) of the surface's own colour.
        const float3 tint = min(base / max(luminance(base), 0.1f), 3.0f);
        r.light *= lerp(float3(1.0f, 1.0f, 1.0f), tint, params.w);
        r.highlight *= lerp(float3(1.0f, 1.0f, 1.0f), tint, params.w);
    }
    return r;
}

// A sample's material (g_info word z): class (bits 0-3), strength (4-7, /15), emitter-free share
// (8-15), roughness (16-23) and metalness (24-31), unorm.
uint pack_material(uint surface_class, float strength, float emitter_free, float roughness, float metal)
{
    return surface_class | (uint(round(saturate(strength) * 15.0f)) << 4u) |
           (uint(round(saturate(emitter_free) * 255.0f)) << 8u) | (uint(round(saturate(roughness) * 255.0f)) << 16u) |
           (uint(round(saturate(metal) * 255.0f)) << 24u);
}

float sample_alpha(uint4 info)
{
    const float r = float((info.z >> 16u) & 255u) / 255.0f;
    return max(r * r, 1e-3f);
}

float sample_strength(uint4 info)
{
    return float((info.z >> 4u) & 15u) / 15.0f;
}

// A draw's reflection parameters (GGX alpha, strength, F0, metalness) and perceptual roughness: its
// class's, with the material table's roughness and metalness for its texture where it names them. A
// surface rougher than g_rough_cutoff reflects nothing worth a ray; below it, metal and dielectric
// alike (a dielectric's Fresnel term keeps its reflection to grazing views).
float4 draw_material(uint surface_class, uint draw, out float roughness)
{
    float4 params = g_class[surface_class];
    roughness = sqrt(params.x);
    if ((g_flags & 128u) == 0u || draw >= g_draw_count) {
        return params;
    }
    const uint words = g_draws.Load(draw * 32u + 16u);
    const uint r16 = words & 0xFFFFu;
    const uint m16 = words >> 16u;
    if (m16 != 0xFFFFu) {
        const float metal = float(m16) / 65534.0f;
        params.z = lerp(surface_class == kMetalClass ? kDielectricF0 : params.z, g_metal_f0, metal);
        params.w = metal;
    }
    if (r16 != 0xFFFFu) {
        roughness = float(r16) / 65534.0f;
        params.x = max(roughness * roughness, 1e-3f);
        params.y = saturate((g_rough_cutoff - roughness) / 0.2f);
    }
    return params;
}

[numthreads(8, 8, 1)]
void ReflectTraceCS(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    const bool active = all(id.xy < g_sample_size);
    const uint index = id.y * g_sample_size.x + id.x;
    const int2 q = sample_pixel(min(id.xy, g_sample_size - 1u));
    const float depth = g_efb_depth.Load(int3(q, 0));
    const bool geometry = active && !far_plane(g_depth, depth) && inside(g_viewport, float2(q) + 0.5f);
    {
        const float3 at = geometry ? eye_position(g_projection, g_viewport, float2(q) + 0.5f,
                                                  depth_to_eye_z(g_depth, depth))
                                   : float3(0.0f, 0.0f, 0.0f);
        CULL_TILE_LIGHTS(g_emitters, g_world_to_eye, gi, geometry && (g_flags & 16u) != 0u, at, glow_reach2)
    }
    uint4 info = uint4(0u, 0u, kNoSurface, 0u);
    float3 light = float3(0.0f, 0.0f, 0.0f);
    float3 highlight = float3(0.0f, 0.0f, 0.0f);
    float confidence = 0.0f;
    uint surface_class = kNoSurface;
    bool reflected = false;
    uint source = 0u;
    bool mapped = false;
    if (active) {
        // The opaque picture as the lighting's composite leaves it (the sky as drawn).
        const float3 picture = g_color.Load(int3(q, 0)).rgb * (geometry ? lighting_at(q) : float3(1.0f, 1.0f, 1.0f));
        g_under[index] = uint2(pack_r11g11b10(picture), geometry ? asuint(depth_to_eye_z(g_depth, depth)) : 0u);
    }
    if (geometry) {
        const float z = depth_to_eye_z(g_depth, depth);
        const float3 dir = normalize(eye_position(g_projection, g_viewport, float2(q) + 0.5f, z));
        info.y = asuint(z);
        // The triangle the lighting's camera ray met at the raster depth.
        const uint2 primary = g_primary[index];
        if (primary.x != 0u) {
            const uint tri = primary.x - 1u;
            const float2 b = float2(f16tof32(primary.y), f16tof32(primary.y >> 16u));
            const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
            const uint a = tri * 36u;
            const float3 v0 = asfloat(g_positions.Load3(a));
            const float3 v1 = asfloat(g_positions.Load3(a + 12u));
            const float3 v2 = asfloat(g_positions.Load3(a + 24u));
            const uint4 record = g_surfaces.Load4(tri * 16u);
            surface_class = min((record.w >> 8u) & 15u, 7u);
            float3 ng = cross(v1 - v0, v2 - v0);
            const float ng_length = length(ng);
            ng = ng_length > 0.0f ? ng / ng_length : -dir;
            ng = dot(ng, dir) > 0.0f ? -ng : ng;
            // Shading normal from the vertex normals (the side the camera sees), and the ray origin
            // lifted off the flat face where they bend away from it (the terminator, Hanika 2021).
            float3 n = ng;
            float3 hit = v0 * w.x + v1 * w.y + v2 * w.z;
            if (record.x != 0u && record.y != 0u && record.z != 0u) {
                float3 n0 = unpack_vertex_normal(record.x);
                float3 n1 = unpack_vertex_normal(record.y);
                float3 n2 = unpack_vertex_normal(record.z);
                float3 smooth = n0 * w.x + n1 * w.y + n2 * w.z;
                const float side = dot(smooth, ng) < 0.0f ? -1.0f : 1.0f;
                smooth *= side;
                const float smooth_length = length(smooth);
                if (smooth_length > 1e-3f && dot(smooth, ng) > 0.05f * smooth_length) {
                    n = smooth / smooth_length;
                    n0 *= side;
                    n1 *= side;
                    n2 *= side;
                    const float3 h0 = hit;
                    hit = h0 - w.x * min(0.0f, dot(h0 - v0, n0)) * n0 - w.y * min(0.0f, dot(h0 - v1, n1)) * n1 -
                          w.z * min(0.0f, dot(h0 - v2, n2)) * n2;
                }
            }
            float roughness;
            const float4 params = draw_material(surface_class, record.w >> 16u, roughness);
            const float3 base = g_color.Load(int3(q, 0)).rgb;
            // Lamps, lava, glowing cores and what glows light keep their own colour: bright unlit
            // surfaces, bright saturated indirect ones (lava; white ice reflects), and anything lit far
            // past white.
            const float3 lit = base * lighting_at(q);
            const float peak = max(lit.r, max(lit.g, lit.b));
            const float saturation = (peak - min(lit.r, min(lit.g, lit.b))) / max(peak, 1e-3f);
            const float emitter = surface_class == 3u ? smoothstep(0.75f, 1.0f, peak)
                                : surface_class == 4u ? smoothstep(0.75f, 1.0f, peak) * smoothstep(0.35f, 0.6f, saturation)
                                                      : smoothstep(0.95f, 1.2f, peak);
            info.x = pack_normal(n);
            info.z = pack_material(surface_class, params.y, 1.0f - emitter, roughness, params.w);
            if (params.y > 0.0f && emitter < 1.0f) {
                reflected = true;
                const Reflected r = reflect_light(id.xy, dir, n, ng, hit, params, base, true, true);
                light = r.light;
                highlight = r.highlight;
                confidence = r.confidence;
                source = r.source;
                mapped = r.mapped;
                info.w = asuint(r.distance);
            } else if (emitter < 1.0f && (g_flags & 128u) != 0u) {
                // Too rough to reflect anything worth a ray: the lights' highlights alone (a broad
                // sheen under the sun, a soft glint near a lamp), at the least strength that keeps the
                // sample in the accumulation and filters; no reflected light (confidence 0).
                info.z = pack_material(surface_class, 1.0f / 15.0f, 1.0f - emitter, roughness, params.w);
                const float3 origin = hit + ng * (0.2f + abs(hit.z) * 4e-4f);
                highlight = highlights(id.xy, hit, origin, n, -dir, params.x, params.z, true) *
                            (1.0f - smoothstep(0.55f, 0.75f, roughness));
                if (params.w > 0.0f) {
                    const float3 tint = min(base / max(luminance(base), 0.1f), 3.0f);
                    highlight *= lerp(float3(1.0f, 1.0f, 1.0f), tint, params.w);
                }
                info.w = asuint(g_range);
            }
        }
    }
    count(0u, geometry);
    count(1u, surface_class != kNoSurface);
    [unroll] for (uint c = 0u; c < 5u; ++c) {
        count(2u + c, surface_class == c);
    }
    count(7u, reflected);
    [unroll] for (uint k = 0u; k < kSources; ++k) {
        count(8u + k, reflected && source == k);
    }
    count(15u, any(highlight > 0.0f));
    count(28u, mapped);
    if (active) {
        g_info[index] = info;
        g_raw[index] = uint4(pack_rgb_w(light, confidence), pack_rgb_w(min(highlight, 60000.0f), 0.0f));
    }
}

// ---------------------------------------------------------------------------------------------
// Under the water: sunlight focused by the surface's waves onto what lies below (caustics) and
// scattered towards the camera along its view (shafts)
// ---------------------------------------------------------------------------------------------

// The water pass's state after its samples in g_under (WaterTraceCS thread 0): [0] the surface's plane
// (its offset along its normal, eye space), camera under water (1); [1] the surface's normal out of the water,
// frame; [2-7] eye to world (3 rows); [8] the sun's light (11-11-10), shafts gain.
static const uint kUnderState = 13u;
// [9-12] the water pass's sums of the game's water colour (8-bit units) on its surface samples: deep
// water (r g, b n), all of it (r g, b n); zeroed before the pass (RtReflections::trace_water).
static const uint kUnderTint = 9u;

// The surface's waves as travelling sines (world units, frames): their height's curvature at `uv`
// focuses the sunlight crossing it, the light below as 1 / (1 + depth x curvature).
float wave_focus(float2 uv, float frame, float depth)
{
    const float4 kx = float4(0.025f, -0.014f, 0.010f, -0.031f);
    const float4 ky = float4(0.010f, 0.024f, -0.028f, -0.007f);
    const float4 speed = float4(0.031f, 0.027f, 0.036f, 0.022f);
    const float4 height = float4(4.0f, 5.0f, 3.0f, 2.5f);
    const float4 phase = kx * uv.x + ky * uv.y + speed * frame;
    const float curvature = dot(height * (kx * kx + ky * ky), sin(phase));
    return clamp(1.0f / max(1.0f + depth * curvature * 0.9f, 0.2f), 0.2f, 4.0f);
}

// Thread 0 of the water pass: is the camera under the water the sun shines through?
void write_underwater_state()
{
    const uint n = g_sample_size.x * g_sample_size.y;
    uint under = 0u;
    float plane = 0.0f;
    float3 up = float3(0.0f, 1.0f, 0.0f);
    // Two probes: towards the sun, and straight up against the world's gravity (a low sun's ray can
    // leave the water through its side or run out before the surface).
    const float3 probes[2] = {g_sun.xyz, g_eye_to_world[1].xyz};
    [unroll] for (uint k = 0u; k < 2u; ++k) {
        if (under != 0u || (k == 0u && !any(g_sun_light.rgb > 0.0f))) {
            continue;
        }
        const float3 d = normalize(probes[k]);
        RayDesc ray;
        ray.Origin = float3(0.0f, 0.0f, 0.0f);
        ray.Direction = d;
        ray.TMin = g_near;
        ray.TMax = 4.0f * g_range;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_water, RAY_FLAG_NONE, 0xFFu, ray);
        query.Proceed();
        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            const uint tri = query.CommittedPrimitiveIndex();
            const float2 b = query.CommittedTriangleBarycentrics();
            const uint4 record = g_water_surfaces.Load4(tri * 16u);
            // Water only (class 7, crystal and glass, is no water to be under; a waterfall neither).
            if (record.x != 0u && record.y != 0u && record.z != 0u && ((record.w >> 8u) & 15u) != 7u &&
                (record.w & 8192u) == 0u) {
                const float3 n = unpack_vertex_normal(record.x) * (1.0f - b.x - b.y) +
                                 unpack_vertex_normal(record.y) * b.x + unpack_vertex_normal(record.z) * b.y;
                // The water's normals point out of it: met from behind, the camera is below it. Only a
                // level surface (within 45 degrees of the world's up) is a surface to be under.
                const float3 world_up = g_eye_to_world[1].xyz;
                if (dot(n, d) > 0.0f && length(n) > 1e-3f && dot(normalize(n), normalize(world_up)) > 0.7f) {
                    under = 1u;
                    up = normalize(n);
                    plane = dot(up, d * query.CommittedRayT());
                }
            }
        }
    }
    g_under[n] = uint2(asuint(plane), under);
    g_stats.InterlockedAdd(27u * 4u, under);
    g_under[n + 1u] = uint2(pack_normal(up), g_frame);
    [unroll] for (uint r = 0u; r < 3u; ++r) {
        g_under[n + 2u + 2u * r] = uint2(asuint(g_eye_to_world[r].x), asuint(g_eye_to_world[r].y));
        g_under[n + 3u + 2u * r] = uint2(asuint(g_eye_to_world[r].z), asuint(g_eye_to_world[r].w));
    }
    g_under[n + 8u] = uint2(pack_r11g11b10(g_sun_light.rgb), asuint(g_shafts));
}

// The water surface's own class (its material, GALAXY_RT_WATER), and the same surface seen from
// under the water (no refraction).
static const uint kWaterSurface = 5u;
static const uint kWaterBelow = 6u;
// The share of the game's drawn water (its tiled texture) the traced water replaces.
static const float kWaterCover = 0.85f;
// Clear dielectrics (rt_surfaces.h RtSurfaceClass::Crystal): crystals, ice, gems, glass, bubbles.
static const uint kCrystal = 7u;

// The water's normal bent by the ripples its picture shows: the slope of its luminance relative to
// its level across two samples each way, each tap the mean of four pixels (the waves, not the
// texture's fine grain).
float3 rippled(int2 q, float3 n)
{
    if (g_ripple <= 0.0f) {
        return n;
    }
    const float2 size = float2(g_target_size);
    const float2 c = float2(q) + 1.0f;
    const float r = 2.0f * float(g_factor);
    const float x0 = luminance(g_color.SampleLevel(g_linear, (c - float2(r, 0.0f)) / size, 0.0f).rgb);
    const float x1 = luminance(g_color.SampleLevel(g_linear, (c + float2(r, 0.0f)) / size, 0.0f).rgb);
    const float y0 = luminance(g_color.SampleLevel(g_linear, (c - float2(0.0f, r)) / size, 0.0f).rgb);
    const float y1 = luminance(g_color.SampleLevel(g_linear, (c + float2(0.0f, r)) / size, 0.0f).rgb);
    const float level = 0.25f * (x0 + x1 + y0 + y1) + 0.05f;
    const float2 slope = clamp(float2(x1 - x0, y0 - y1) / (2.0f * level), -0.6f, 0.6f) * g_ripple;
    // Screen x and y along the water.
    const float3 ex = normalize(float3(1.0f, 0.0f, 0.0f) - n * n.x);
    const float3 ey = normalize(float3(0.0f, 1.0f, 0.0f) - n * n.y);
    return normalize(n - slope.x * ex - slope.y * ey);
}

// RtDraw.flags bit 13 (kRtDrawWaterfall): falling water (its streaks run down it).
static const uint kWaterfallFlag = 8192u;

// Value noise in 3D (hashed lattice corners, smooth interpolation) and its fractal sum.
float wave_hash(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float wave_noise(float3 x)
{
    const float3 i = floor(x);
    const float3 f = x - i;
    const float3 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(wave_hash(i), wave_hash(i + float3(1, 0, 0)), u.x),
                     lerp(wave_hash(i + float3(0, 1, 0)), wave_hash(i + float3(1, 1, 0)), u.x), u.y),
                lerp(lerp(wave_hash(i + float3(0, 0, 1)), wave_hash(i + float3(1, 0, 1)), u.x),
                     lerp(wave_hash(i + float3(0, 1, 1)), wave_hash(i + float3(1, 1, 1)), u.x), u.y), u.z);
}

// The water's height (world units) at plane point uv and time t: two fractal layers drifting over
// time, no repeating pattern.
float wave_height(float2 uv, float t)
{
    float3 x = float3(uv * 0.010f, t);
    float h = 0.0f;
    float a = 6.0f;
    [unroll] for (uint k = 0u; k < 3u; ++k) {
        h += wave_noise(x) * a;
        x = x * float3(2.07f, 2.07f, 1.5f) + float3(13.7f, 4.1f, 0.0f);
        a *= 0.5f;
    }
    return h;
}

// The water's procedural waves (world units, frames): its height's slopes along its plane basis (uv).
// `flow`: a waterfall, uv.x across and uv.y down its fall: fine streaks across, travelling fast down.
float2 wave_slope(float2 uv, float frame, bool flow)
{
    if (!flow) {
        const float t = frame * 0.02f;
        const float e = 6.0f;
        return float2(wave_height(uv + float2(e, 0.0f), t) - wave_height(uv - float2(e, 0.0f), t),
                      wave_height(uv + float2(0.0f, e), t) - wave_height(uv - float2(0.0f, e), t)) / (2.0f * e);
    }
    float4 kx, ky, speed, amp;
    {
        kx = float4(0.21f, -0.13f, 0.34f, -0.08f);
        ky = float4(0.020f, 0.031f, 0.015f, 0.026f);
        speed = float4(-0.9f, -1.3f, -1.1f, -0.7f);
        amp = float4(0.35f, 0.3f, 0.2f, 0.45f);
    }
    const float4 c = cos(kx * uv.x + ky * uv.y + speed * frame) * amp;
    return float2(dot(c, kx), dot(c, ky));
}

// The water's normal `n` at eye-space `hit` bent by its waves (world space, so they stay with the
// world); far water, where a sample spans more than the waves, keeps its own.
float3 water_detail(float3 n, float3 hit, bool flow)
{
    if (g_waves <= 0.0f) {
        return n;
    }
    const float3 wn = normalize(rotate(g_eye_to_world, n));
    float3 t1;
    float3 t2;
    if (flow) {
        const float3 down = float3(0.0f, -1.0f, 0.0f) + wn * wn.y;
        if (dot(down, down) < 1e-4f) {
            return n;
        }
        t2 = normalize(down);
        t1 = cross(t2, wn);
    } else {
        t1 = normalize(abs(wn.y) < 0.9f ? cross(wn, float3(0.0f, 1.0f, 0.0f)) : cross(wn, float3(1.0f, 0.0f, 0.0f)));
        t2 = cross(wn, t1);
    }
    const float3 wp = transform(g_eye_to_world, hit);
    const float fade = saturate(1.5f - length(hit) / 6000.0f);
    const float2 s = wave_slope(float2(dot(wp, t1), dot(wp, t2)), float(g_frame), flow) * (g_waves * fade);
    const float3 wd = normalize(wn - s.x * t1 - s.y * t2);
    return normalize(float3(dot(float3(g_eye_to_world[0].x, g_eye_to_world[1].x, g_eye_to_world[2].x), wd),
                            dot(float3(g_eye_to_world[0].y, g_eye_to_world[1].y, g_eye_to_world[2].y), wd),
                            dot(float3(g_eye_to_world[0].z, g_eye_to_world[1].z, g_eye_to_world[2].z), wd)));
}

// Translucent water (seas, lakes, pools drawn blended after the trace) and clear crystals and glass
// drawn the same way: the camera ray meets the
// water draws' own triangles in front of the surface the EFB holds behind them; the vertex normals
// bent by the ripples give the normal; one reflection ray as on the opaque surfaces, its light from
// the lit picture as the translucent phases left it. Glows over the water (white) keep their colour.
[numthreads(8, 8, 1)]
void WaterTraceCS(uint3 id : SV_DispatchThreadID)
{
    const bool active = all(id.xy < g_sample_size);
    const uint index = id.y * g_sample_size.x + id.x;
    const int2 q = sample_pixel(min(id.xy, g_sample_size - 1u));
    if (all(id.xy == 0u)) {
        write_underwater_state();
    }
    uint4 info = uint4(0u, 0u, kNoSurface, 0u);
    float3 light = float3(0.0f, 0.0f, 0.0f);
    float3 highlight = float3(0.0f, 0.0f, 0.0f);
    float confidence = 0.0f;
    bool water = false, reflected = false;
    uint source = 0u;
    bool mapped = false;
    // The game's water colour on this surface sample (8-bit units), deep water or not, for the tint.
    uint3 tint_rgb = uint3(0u, 0u, 0u);
    uint tint_deep = 0u;
    uint tint_all = 0u;
    if (active && inside(g_viewport, float2(q) + 0.5f)) {
        const float depth = g_efb_depth.Load(int3(q, 0));
        const float3 through = pixel_direction(g_projection, g_viewport, float2(q) + 0.5f);
        const float3 dir = normalize(through);
        RayDesc ray;
        ray.Origin = float3(0.0f, 0.0f, 0.0f);
        ray.Direction = dir;
        ray.TMin = g_near / max(-dir.z, 1e-6f);
        // In front of what the EFB holds (the water itself where it writes depth).
        ray.TMax = far_plane(g_depth, depth) ? 4.0f * g_range
                                              : length(through) * -depth_to_eye_z(g_depth, depth) * 1.003f + 1.0f;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_water, RAY_FLAG_NONE, 0xFFu, ray);
        query.Proceed();
        // A water surface has depth below it: triangles lying on the opaque scene the trace saw
        // (a water volume's floor, seen from inside it) are no surface.
        const uint2 under = g_under[index];
        const float floor_distance = under.y != 0u ? length(through) * -asfloat(under.y) : 1e30f;
        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT &&
            floor_distance - query.CommittedRayT() > max(0.01f * query.CommittedRayT(), 8.0f)) {
            water = true;
            const uint tri = query.CommittedPrimitiveIndex();
            const float2 b = query.CommittedTriangleBarycentrics();
            const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
            const uint a = tri * 36u;
            const float3 v0 = asfloat(g_water_positions.Load3(a));
            const float3 v1 = asfloat(g_water_positions.Load3(a + 12u));
            const float3 v2 = asfloat(g_water_positions.Load3(a + 24u));
            const uint4 record = g_water_surfaces.Load4(tri * 16u);
            float3 ng = cross(v1 - v0, v2 - v0);
            const float ng_length = length(ng);
            ng = ng_length > 0.0f ? ng / ng_length : -dir;
            ng = dot(ng, dir) > 0.0f ? -ng : ng;
            float3 n = ng;
            bool below = false;
            if (record.x != 0u && record.y != 0u && record.z != 0u) {
                float3 smooth = unpack_vertex_normal(record.x) * w.x + unpack_vertex_normal(record.y) * w.y +
                                unpack_vertex_normal(record.z) * w.z;
                // The water's normals point out of it: seen from their back, the camera is under it.
                below = dot(smooth, dir) > 0.0f;
                smooth *= dot(smooth, ng) < 0.0f ? -1.0f : 1.0f;
                const float smooth_length = length(smooth);
                // Only where they lie close to the face: at a rounded lip they bend towards the side wall
                // and draw a line of sun glints along the edge.
                if (smooth_length > 1e-3f && dot(smooth, ng) > 0.95f * smooth_length) {
                    n = smooth / smooth_length;
                }
            }
            // Translucent crystal and glass draws share the pass: no ripples, no water below them.
            const bool crystal = ((record.w >> 8u) & 15u) == kCrystal;
            if (!crystal) {
                n = rippled(q, n);
                n = water_detail(n, dir * query.CommittedRayT(), (record.w & kWaterfallFlag) != 0u);
            }
            if (dot(n, -dir) < 0.02f) {
                n = ng;
            }
            const float3 hit = dir * query.CommittedRayT();
            // A waterfall is a clear sheet (no lake body under it), its streaks in its normal.
            const bool fall = (record.w & kWaterfallFlag) != 0u;
            const uint surface = crystal || fall ? kCrystal : below ? kWaterBelow : kWaterSurface;
            const float4 params = g_class[crystal ? kCrystal : kWaterSurface];
            const float3 base = g_color.Load(int3(q, 0)).rgb;
            const float emitter = smoothstep(0.8f, 1.0f, min(base.r, min(base.g, base.b)));
            info.x = pack_normal(n);
            info.y = asuint(hit.z);
            info.z = pack_material(surface, params.y, 1.0f - emitter, sqrt(params.x), params.w);
            if (surface == kWaterSurface && emitter < 1.0f) {
                tint_rgb = uint3(round(saturate(base) * 255.0f));
                tint_all = 1u;
                tint_deep = floor_distance - query.CommittedRayT() > 300.0f ? 1u : 0u;
            }
            // Under the water its surfaces (and a volume's walls) reflect nothing: the reflection of
            // the bright world above only washed the view out (measured: Beach Bowl underwater).
            if (params.y > 0.0f && emitter < 1.0f && !below) {
                reflected = true;
                const Reflected r = reflect_light(id.xy, dir, n, ng, hit, params, base, true, false);
                light = r.light;
                highlight = min(r.highlight, 1.5f);
                confidence = r.confidence;
                source = r.source;
                mapped = r.mapped;
                info.w = asuint(r.distance);
            }
        }
    }
    {
        const uint3 deep_sum = WaveActiveSum(tint_rgb * tint_deep);
        const uint deep_n = WaveActiveSum(tint_deep);
        const uint3 all_sum = WaveActiveSum(tint_rgb);
        const uint all_n = WaveActiveSum(tint_all);
        if (WaveIsFirstLane() && all_n != 0u) {
            const uint t = g_sample_size.x * g_sample_size.y + kUnderTint;
            InterlockedAdd(g_under[t].x, deep_sum.r);
            InterlockedAdd(g_under[t].y, deep_sum.g);
            InterlockedAdd(g_under[t + 1u].x, deep_sum.b);
            InterlockedAdd(g_under[t + 1u].y, deep_n);
            InterlockedAdd(g_under[t + 2u].x, all_sum.r);
            InterlockedAdd(g_under[t + 2u].y, all_sum.g);
            InterlockedAdd(g_under[t + 3u].x, all_sum.b);
            InterlockedAdd(g_under[t + 3u].y, all_n);
        }
    }
    count(16u, active);
    count(17u, water);
    count(18u, reflected);
    [unroll] for (uint k = 0u; k < kSources; ++k) {
        count(19u + k, reflected && source == k);
    }
    count(26u, any(highlight > 0.0f));
    count(29u, mapped);
    if (active) {
        g_info[index] = info;
        g_raw[index] = uint4(pack_rgb_w(light, confidence), pack_rgb_w(min(highlight, 60000.0f), 0.0f));
    }
}

bool reflective(uint4 info)
{
    return info.y != 0u && ((info.z >> 4u) & 15u) != 0u && ((info.z >> 8u) & 255u) != 0u;
}

// Accumulation: the reflection's position in the previous frame (the virtual image point behind the
// surface for sharp lobes, the surface for wide ones), bilinear over the previous samples on the
// same plane and class, clamped to the spread of the neighbourhood's new light; the highlight to
// the range of the neighbourhood's new highlights, renewed faster.
[numthreads(8, 8, 1)]
void ReflectTemporalCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_sample_size)) {
        return;
    }
    const uint index = id.y * g_sample_size.x + id.x;
    const uint4 info = g_info[index];
    if (!reflective(info)) {
        g_history[index] = uint4(0u, 0u, 0u, 0u);
        return;
    }
    const uint surface_class = info.z & 15u;
    const float alpha = sample_alpha(info);
    const uint4 raw_words = g_raw[index];
    const float4 raw = unpack_rgb_w(raw_words.xy);
    const float3 raw_highlight = unpack_rgb_w(raw_words.zw).rgb;
    float3 m1 = float3(0.0f, 0.0f, 0.0f);
    float3 m2 = float3(0.0f, 0.0f, 0.0f);
    float n = 0.0f;
    float3 highlight_low = raw_highlight;
    float3 highlight_high = raw_highlight;
    [unroll] for (int dy = -1; dy <= 1; ++dy) {
        [unroll] for (int dx = -1; dx <= 1; ++dx) {
            const int2 s = clamp(int2(id.xy) + int2(dx, dy), int2(0, 0), int2(g_sample_size) - 1);
            const uint si = uint(s.y) * g_sample_size.x + uint(s.x);
            const uint4 other = g_info[si];
            if (other.y == 0u || (other.z & 15u) != surface_class) {
                continue;
            }
            // The spread of what is known: each sample counts by its confidence.
            const uint4 words = g_raw[si];
            const float4 r = unpack_rgb_w(words.xy);
            const float3 other_highlight = unpack_rgb_w(words.zw).rgb;
            highlight_low = min(highlight_low, other_highlight);
            highlight_high = max(highlight_high, other_highlight);
            const float3 c = to_ycocg(r.rgb);
            m1 += c * r.w;
            m2 += c * c * r.w;
            n += r.w;
        }
    }
    const float3 mean = m1 / max(n, 1e-3f);
    const float3 spread = n > 0.5f ? sqrt(max(m2 / n - mean * mean, 0.0f)) : float3(1e4f, 1e4f, 1e4f);
    float4 result = raw;
    float3 highlight = raw_highlight;
    float frames = 1.0f;
    if ((g_flags & 1u) != 0u) {
        const float z = asfloat(info.y);
        const float3 p = sample_position(id.xy, z);
        const float3 normal = unpack_normal(info.x);
        const float3 previous_p = transform(g_reproject, p);
        const float3 previous_n = normalize(rotate(g_reproject, normal));
        const float distance = min(asfloat(info.w), g_range);
        const float3 image = alpha < 0.08f ? p + normalize(p) * distance : p;
        const float3 previous = transform(g_reproject, image);
        if (previous.z < -g_near) {
            const float2 a = (eye_to_pixel(g_projection, g_viewport, previous) - 0.5f) / float(g_factor);
            const int2 base = int2(floor(a));
            const float2 w = a - float2(base);
            const float tolerance = 0.5f + abs(previous_p.z) * 0.01f;
            float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
            float3 highlight_sum = float3(0.0f, 0.0f, 0.0f);
            float count_sum = 0.0f;
            float weight = 0.0f;
            [unroll] for (uint k = 0u; k < 4u; ++k) {
                const int2 o = int2(k & 1u, k >> 1u);
                const int2 s = base + o;
                if (any(s < 0) || any(s >= int2(g_sample_size))) {
                    continue;
                }
                const uint si = uint(s.y) * g_sample_size.x + uint(s.x);
                const uint4 before = g_prev_info[si];
                if (before.y == 0u || (before.z & 15u) != surface_class ||
                    abs(dot(previous_n, sample_position(uint2(s), asfloat(before.y)) - previous_p)) > tolerance ||
                    dot(unpack_normal(before.x), previous_n) < 0.9f) {
                    continue;
                }
                const float bw = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y);
                const uint4 history = g_prev_history[si];
                const float4 before_highlight = unpack_rgb_w(history.zw);
                sum += unpack_rgb_w(history.xy) * bw;
                highlight_sum += before_highlight.rgb * bw;
                count_sum += before_highlight.w * bw;
                weight += bw;
            }
            if (weight > 1e-3f) {
                const float gamma = 1.0f + 2.0f * saturate(alpha * 4.0f);
                const float4 history = sum / weight;
                const float3 clamped =
                    from_ycocg(clamp(to_ycocg(history.rgb), mean - gamma * spread, mean + gamma * spread));
                const float limit = lerp(8.0f, float(g_history_frames), saturate(alpha * 4.0f));
                frames = min(count_sum / weight + 1.0f, limit);
                // Light blends by confidence (unknown light only fades the reflection); the
                // confidence itself by the history's share.
                const float a = 1.0f / frames;
                const float kept = history.w * (1.0f - a);
                const float added = raw.w * a;
                const float confidence = kept + added;
                const float3 light = confidence > 1e-4f ? (clamped * kept + raw.rgb * added) / confidence
                                                        : (history.w >= raw.w ? clamped : raw.rgb);
                result = float4(light, confidence);
                highlight = lerp(clamp(highlight_sum / weight, highlight_low, highlight_high), raw_highlight,
                                 max(a, 0.25f));
            }
        }
    }
    g_history[index] = uint4(pack_rgb_w(max(result.rgb, 0.0f), saturate(result.w)), pack_rgb_w(highlight, frames));
}

// The filters' input: the accumulated light and highlight (first pass) or the first pass's result
// (second).
float4 filter_input(uint index, bool first, out float3 highlight)
{
    const uint4 words = first ? g_history_in[index] : g_raw[index];
    highlight = unpack_rgb_w(words.zw).rgb;
    return unpack_rgb_w(words.xy);
}

// Edge-aware 5x5 filter (a-trous): steps as wide as the lobe, doubled in the second pass; none for
// the sharpest lobes.
float4 filtered_light(uint2 id, uint index, uint4 info, bool first, out float3 highlight)
{
    const uint surface_class = info.z & 15u;
    const float alpha = sample_alpha(info);
    const float4 centre = filter_input(index, first, highlight);
    float4 value = centre;
    const int step = (alpha < 0.02f ? 0 : alpha < 0.12f ? 1 : 2) * (first ? 1 : 2);
    if (step != 0) {
        const float z = asfloat(info.y);
        const float3 p = sample_position(id, z);
        const float3 n = unpack_normal(info.x);
        const float inv_tolerance = 1.0f / (0.25f + abs(z) * 0.004f * float(step));
        const float centre_l = luminance(centre.rgb);
        float3 sum = float3(0.0f, 0.0f, 0.0f);
        float3 highlight_sum = float3(0.0f, 0.0f, 0.0f);
        float shape_sum = 0.0f;
        float known = 0.0f;
        float weight = 0.0f;
        [unroll] for (int dy = -2; dy <= 2; ++dy) {
            [unroll] for (int dx = -2; dx <= 2; ++dx) {
                const int2 s = int2(id) + int2(dx, dy) * step;
                if (any(s < 0) || any(s >= int2(g_sample_size))) {
                    continue;
                }
                const uint si = uint(s.y) * g_sample_size.x + uint(s.x);
                const uint4 other = g_info[si];
                if (!reflective(other) || (other.z & 15u) != surface_class) {
                    continue;
                }
                const float plane = abs(dot(n, sample_position(uint2(s), asfloat(other.y)) - p)) * inv_tolerance;
                const float facing = saturate(dot(n, unpack_normal(other.x)));
                const float facing2 = facing * facing;
                float3 other_highlight;
                const float4 c = filter_input(si, first, other_highlight);
                const float shape = exp(-0.3f * float(dx * dx + dy * dy) - plane * plane) * (facing2 * facing2) *
                                    (facing2 * facing2);
                // Edges between known lights stop the filter; light counts by its confidence.
                const float edge = abs(luminance(c.rgb) - centre_l) / (0.1f + centre_l + alpha) * min(c.w, centre.w);
                const float w = shape * exp(-edge * edge);
                sum += c.rgb * (w * c.w);
                known += w * c.w;
                weight += w;
                // Highlights by the shape alone.
                highlight_sum += other_highlight * shape;
                shape_sum += shape;
            }
        }
        if (weight > 1e-4f) {
            value = float4(known > 1e-4f ? sum / known : centre.rgb, known / weight);
        }
        if (shape_sum > 1e-4f) {
            highlight = highlight_sum / shape_sum;
        }
    } else {
        // The sharpest lobes (water, ice, glass) keep their reflection unfiltered, but their
        // highlights are glints off per-sample ripple normals: alone they speckle the surface. They
        // average over the 3 x 3 samples on the same plane (one pass each, the second twice as wide).
        const int reach = first ? 1 : 2;
        const float z = asfloat(info.y);
        const float3 p = sample_position(id, z);
        const float3 n = unpack_normal(info.x);
        const float inv_tolerance = 1.0f / (0.25f + abs(z) * 0.004f * float(reach));
        float3 highlight_sum = float3(0.0f, 0.0f, 0.0f);
        float shape_sum = 0.0f;
        [unroll] for (int dy = -1; dy <= 1; ++dy) {
            [unroll] for (int dx = -1; dx <= 1; ++dx) {
                const int2 s = int2(id) + int2(dx, dy) * reach;
                if (any(s < 0) || any(s >= int2(g_sample_size))) {
                    continue;
                }
                const uint si = uint(s.y) * g_sample_size.x + uint(s.x);
                const uint4 other = g_info[si];
                if (!reflective(other) || (other.z & 15u) != surface_class) {
                    continue;
                }
                const float plane = abs(dot(n, sample_position(uint2(s), asfloat(other.y)) - p)) * inv_tolerance;
                const float shape = exp(-0.5f * float(dx * dx + dy * dy) - plane * plane);
                float3 other_highlight;
                filter_input(si, first, other_highlight);
                highlight_sum += other_highlight * shape;
                shape_sum += shape;
            }
        }
        if (shape_sum > 1e-4f) {
            highlight = highlight_sum / shape_sum;
        }
    }
    return value;
}

[numthreads(8, 8, 1)]
void ReflectFilterACS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_sample_size)) {
        return;
    }
    const uint index = id.y * g_sample_size.x + id.x;
    const uint4 info = g_info[index];
    float3 highlight = float3(0.0f, 0.0f, 0.0f);
    float4 value = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (reflective(info)) {
        value = filtered_light(id.xy, index, info, true, highlight);
    }
    g_raw[index] = uint4(pack_rgb_w(value.rgb, value.w), pack_rgb_w(highlight, 0.0f));
}

// Normal in 12:12 bits (octahedral) and a weight in the top 8.
uint pack_normal_weight(float3 n, float weight)
{
    const uint2 u = uint2(round(saturate(octahedral(n)) * 4095.0f));
    return u.x | (u.y << 12u) | (uint(round(saturate(weight) * 255.0f)) << 24u);
}

float3 unpack_normal_weight(uint v, out float weight)
{
    weight = float(v >> 24u) / 255.0f;
    return octahedral_direction(float2(float(v & 0xFFFu), float((v >> 12u) & 0xFFFu)) / 4095.0f);
}

// The second pass: the sample's result for the composite.
uint4 filter_sample(uint2 id, uint index)
{
    const uint4 info = g_info[index];
    // Eye z with the composite's material in its low 12 bits: class, roughness and metalness in
    // 4 bits each (the depth keeps 11 bits of mantissa: 0.05 % of the distance).
    const uint z_class = (info.y & ~0xFFFu) | (info.z & 15u) |
                         (uint(round(float((info.z >> 16u) & 255u) / 255.0f * 15.0f)) << 4u) |
                         (uint(round(float(info.z >> 24u) / 255.0f * 15.0f)) << 8u);
    if (!reflective(info)) {
        return uint4(0u, info.y != 0u ? pack_normal_weight(unpack_normal(info.x), 0.0f) : 0u,
                     info.y != 0u ? z_class : 0u, 0u);
    }
    float3 highlight;
    const float4 value = filtered_light(id, index, info, false, highlight);
    // The sample's weight: its confidence and strength, without its emitter share.
    const float kept = float((info.z >> 8u) & 255u) / 255.0f;
    const float weight = saturate(value.w) * kept * sample_strength(info);
    return uint4(pack_r11g11b10(value.rgb), pack_normal_weight(unpack_normal(info.x), weight), z_class,
                 pack_r11g11b10(highlight * kept));
}

groupshared uint gs_reflects;

// One group per tile of 8x8 samples: the results, and whether any of them reflects (the composite
// skips pixels whose samples lie in tiles that do not).
[numthreads(8, 8, 1)]
void ReflectFilterCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    if (lane == 0u) {
        gs_reflects = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    if (all(id.xy < g_sample_size)) {
        const uint index = id.y * g_sample_size.x + id.x;
        const uint4 result = filter_sample(id.xy, index);
        g_final[index] = result;
        // A weight (top 8 bits of the normal word) or a highlight.
        if ((result.y >> 24u) != 0u || result.w != 0u) {
            InterlockedOr(gs_reflects, 1u);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0u) {
        g_tiles[group.y * ((g_sample_size.x + 7u) / 8u) + group.x] = gs_reflects;
    }
}

// ---------------------------------------------------------------------------------------------
// Composite: dst * (1 - k) + k * reflection (dual-source blend: src0 + dst * src1)
// ---------------------------------------------------------------------------------------------

cbuffer RtReflectComposite : register(b0)
{
    float4 m_projection;
    float4 m_depth;
    float4 m_viewport;
    uint2 m_sample_size;
    uint m_factor;
    uint m_flags;       // bit 0: move the point to the traced frame's camera (m_reproject[m_slot]);
                        // bits 1-2: view 1 the reflections alone, 2 the surface classes, 3 the weight k
    uint m_slot;
    float m_near;
    uint m_tile_width;  // tiles of 8x8 samples per row
    float m_split;      // GALAXY_RT_SPLIT: left of it (and its line) the game's own picture
    uint4 m_class[4];   // per surface class (two per row): strength, F0, GGX alpha, cover (halves)
    float4 m_water;     // water: absorption per eye unit (rgb), share of the clear water shown (clarity)
    float4 m_sun;       // towards the sun (eye space, traced frame), caustics gain (0: none)
    float4 m_water2;    // water: wave slope, share of the game's drawn water replaced, foam, caustics
                        // in the shallows
};
StructuredBuffer<uint4> m_final : register(t0);
Texture2D<float> m_efb_depth : register(t1);
StructuredBuffer<float4> m_reproject : register(t2);
StructuredBuffer<uint> m_tiles : register(t3);
// The picture under the translucent phases (ReflectTraceCS g_under) for the water's refraction.
StructuredBuffer<uint2> m_under : register(t4);
// The frame as the game drew it with its water (a copy after the water pass; the water composite only).
Texture2D<float4> m_scene : register(t5);
// The frame just before the translucent phases (a copy after the lighting and reflections composite): the
// seabed and what stands in the water, without the water. Read when m_water2.y > 0.
Texture2D<float4> m_opaque : register(t6);

struct CompositeOut
{
    float4 add : SV_Target0;
    float4 keep : SV_Target1;
};

float4 class_params(uint c)
{
    const uint4 row = m_class[min(c, 7u) >> 1u];
    const uint2 h = (c & 1u) != 0u ? row.zw : row.xy;
    return float4(f16tof32(h.x), f16tof32(h.x >> 16u), f16tof32(h.y), f16tof32(h.y >> 16u));
}

float4 ReflectCompositeVS(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1u) & 2u, id & 2u);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

// The samples around a traced-frame pixel blended: Fresnel-weighted reflected light, the share of
// the surface's own colour it takes away, the highlights, their weight; the normal of the samples
// on the surface and their share of the weight; the class of the nearest sample.
struct Blend
{
    float3 sum;
    float3 covered;
    float3 glint;
    float k_sum;
    float weight;
    float3 normal;
    float surface;
    uint nearest_class;
};

// On water (`water`) the samples off it count in the weight: the reflection fades towards the shore.
Blend blend_samples(float2 pixel, float3 p, bool water)
{
    const float3 v = -normalize(p);
    const float2 a = (pixel - 0.5f) / float(m_factor);
    const int2 base = int2(floor(a));
    const float2 w = a - float2(base);
    // Water: wider, so the samples of a lip and its wall blend instead of switching per sample.
    const float tolerance = (abs(p.z) * 0.004f + 0.5f) * (water ? 3.0f : 1.0f);
    Blend b;
    b.sum = float3(0.0f, 0.0f, 0.0f);
    b.covered = float3(0.0f, 0.0f, 0.0f);
    b.glint = float3(0.0f, 0.0f, 0.0f);
    b.k_sum = 0.0f;
    b.weight = 0.0f;
    b.normal = float3(0.0f, 0.0f, 0.0f);
    b.surface = 0.0f;
    b.nearest_class = 15u;
    float nearest = 1e30f;
    [unroll] for (uint i = 0u; i < 4u; ++i) {
        const int2 o = int2(i & 1u, i >> 1u);
        const int2 s = clamp(base + o, int2(0, 0), int2(m_sample_size) - 1);
        const uint4 sample = m_final[uint(s.y) * m_sample_size.x + uint(s.x)];
        const float corner = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y);
        if (sample.z == 0u) {
            b.weight += water ? corner : 0.0f;
            continue;
        }
        const uint surface_class = sample.z & 15u;
        const float sz = asfloat(sample.z & ~0xFFFu);
        const float3 sp = eye_position(m_projection, m_viewport, float2(s * m_factor) + 0.5f, sz);
        float sample_weight;
        const float3 n = unpack_normal_weight(sample.y, sample_weight);
        // Same surface: on the sample's plane (its depth where no plane is known).
        const float d = (surface_class == 15u ? abs(sz - p.z) : abs(dot(n, p - sp))) / tolerance;
        const float bw = corner * exp(-d * d);
        if (d < nearest) {
            nearest = d;
            b.nearest_class = surface_class;
        }
        if (surface_class < 8u) {
            // Water seen from below has the water's material.
            const float4 c = class_params(surface_class == kWaterBelow ? kWaterSurface : surface_class);
            // The sample's own lobe and metalness (filter_sample); its strength is in its weight.
            const float rough = float((sample.z >> 4u) & 15u) / 15.0f;
            const float metal = float((sample.z >> 8u) & 15u) / 15.0f;
            const float f0 = lerp(surface_class == kMetalClass ? kDielectricF0 : c.y, class_params(kMetalClass).y, metal);
            // Samples too rough to reflect (ReflectTraceCS: highlights only) take no reflected light,
            // only their glint.
            // Opaque dielectrics reflect half as much at grazing angles as Schlick has it (the bright sky
            // outlined objects); metal, water and glass keep it.
            const bool clear = surface_class >= 4u || metal > 0.5f;
            const float graze = clear ? 1.0f : 0.5f;
            const float alpha_k = max(rough * rough, 1e-3f);
            const float k = rough >= 0.62f ? 0.0f
                          : (f0 + (fresnel(f0, alpha_k, dot(n, v)) - f0) * graze) * sample_weight;
            const float3 light = min(unpack_r11g11b10(sample.x), 1.0f);
            b.sum += light * (k * bw);
            // How much of the surface's own colour the reflection takes away: all of it at cover 1
            // (dst * (1 - k) + k * light), only its own share at cover 0 (a screen: never darker).
            b.covered += (c.w + (1.0f - c.w) * light) * (k * bw);
            b.k_sum += k * bw;
            b.glint += unpack_r11g11b10(sample.w) * bw;
        }
        b.normal += n * bw;
        b.surface += bw;
        b.weight += bw;
    }
    return b;
}

// Surface classes over the picture: diffuse grey, glossy cyan, metal orange, unlit magenta, water
// blue, translucent water light blue (from below green), crystal white, untraced black.
CompositeOut class_view(uint nearest_class)
{
    const float3 colours[9] = {float3(0.3f, 0.3f, 0.3f), float3(0.0f, 0.86f, 1.0f), float3(1.0f, 0.67f, 0.0f),
                               float3(1.0f, 0.0f, 1.0f), float3(0.0f, 0.3f, 1.0f), float3(0.45f, 0.8f, 1.0f),
                               float3(0.2f, 1.0f, 0.6f), float3(1.0f, 1.0f, 1.0f), float3(0.0f, 0.0f, 0.0f)};
    CompositeOut result;
    result.add = float4(colours[min(nearest_class, 8u)] * 0.55f, 0.0f);
    result.keep = float4(0.45f, 0.45f, 0.45f, 1.0f);
    return result;
}

// The blend over the picture: under (1 - covered) (1 - glint) + reflected (1 - glint) + glint, the
// lights' highlights screened over the reflection, under = under_add + dst under_keep (water: the
// water's body over the picture; else dst).
CompositeOut finish(Blend b, uint view, float3 under_add, float3 under_keep)
{
    CompositeOut result;
    const float k = saturate(b.k_sum / b.weight);
    const float3 highlight = saturate(b.glint / b.weight);
    const float3 kept = saturate(1.0f - b.covered / b.weight);
    if (view == 1u) {
        const float3 reflection = b.k_sum > 1e-4f ? b.sum / b.k_sum : float3(0.0f, 0.0f, 0.0f);
        result.add = float4(saturate(reflection + highlight), 0.0f);
        result.keep = float4(0.0f, 0.0f, 0.0f, 1.0f);
    } else if (view == 3u) {
        result.add = float4(k, k, k, 0.0f);
        result.keep = float4(0.0f, 0.0f, 0.0f, 1.0f);
    } else {
        result.add = float4(saturate((saturate(b.sum / b.weight) + under_add * kept) * (1.0f - highlight) + highlight),
                            0.0f);
        result.keep = float4(saturate(kept * under_keep * (1.0f - highlight)), 1.0f);
    }
    return result;
}

// Whether a sample of the footprint of traced-frame pixel `pixel` reflects (tiles of 8x8 samples).
bool tiles_reflect(float2 pixel)
{
    const int2 base = int2(floor((pixel - 0.5f) / float(m_factor)));
    const uint2 lo = uint2(clamp(base, int2(0, 0), int2(m_sample_size) - 1)) >> 3u;
    const uint2 hi = uint2(clamp(base + 1, int2(0, 0), int2(m_sample_size) - 1)) >> 3u;
    return (m_tiles[lo.y * m_tile_width + lo.x] | m_tiles[lo.y * m_tile_width + hi.x] |
            m_tiles[hi.y * m_tile_width + lo.x] | m_tiles[hi.y * m_tile_width + hi.x]) != 0u;
}

CompositeOut ReflectCompositePS(float4 position : SV_Position)
{
    const float depth = m_efb_depth.Load(int3(int2(position.xy), 0));
    if (far_plane(m_depth, depth) || !inside(m_viewport, position.xy) || position.x < m_split + 1.0f) {
        discard;
    }
    const float z = depth_to_eye_z(m_depth, depth);
    float3 p = eye_position(m_projection, m_viewport, position.xy, z);
    if ((m_flags & 1u) != 0u) {
        const uint row = m_slot * 3u;
        const float4 rows[3] = {m_reproject[row], m_reproject[row + 1u], m_reproject[row + 2u]};
        p = transform(rows, p);
        if (p.z >= -m_near) {
            discard;
        }
    }
    const uint view = (m_flags >> 1u) & 3u;
    const float2 pixel = eye_to_pixel(m_projection, m_viewport, p);
    // Nothing to blend where no sample of the footprint's tiles reflects.
    if (view == 0u && !tiles_reflect(pixel)) {
        discard;
    }
    const Blend b = blend_samples(pixel, p, false);
    if (view == 2u) {
        return class_view(b.nearest_class);
    }
    if (b.weight < 1e-3f || (b.k_sum < 1e-4f && !any(b.glint > 0.0f))) {
        if (view != 0u) {
            CompositeOut result;
            result.add = float4(0.0f, 0.0f, 0.0f, 0.0f);
            result.keep = float4(0.0f, 0.0f, 0.0f, 1.0f);
            return result;
        }
        discard;
    }
    return finish(b, view, float3(0.0f, 0.0f, 0.0f), float3(1.0f, 1.0f, 1.0f));
}

// The water's eye z at traced-frame pixel `pixel`: bilinear over the samples on it (0: none near).
float water_z(float2 pixel)
{
    const float2 a = (pixel - 0.5f) / float(m_factor);
    const int2 base = int2(floor(a));
    const float2 w = a - float2(base);
    float sum = 0.0f;
    float weight = 0.0f;
    [unroll] for (uint i = 0u; i < 4u; ++i) {
        const int2 o = int2(i & 1u, i >> 1u);
        const int2 s = clamp(base + o, int2(0, 0), int2(m_sample_size) - 1);
        const uint z_class = m_final[uint(s.y) * m_sample_size.x + uint(s.x)].z;
        const float bw = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y);
        if (z_class != 0u) {
            sum += asfloat(z_class & ~0xFFFu) * bw;
            weight += bw;
        }
    }
    return weight > 1e-3f ? sum / weight : 0.0f;
}

// The picture under the water at traced-frame pixel `pixel`: colour bilinear over the samples behind
// the water (eye z below `limit`), and the nearest such sample's eye z (0: none, the sky).
float3 under_at(float2 pixel, float limit, out float z, out float found)
{
    const float2 a = clamp((pixel - 0.5f) / float(m_factor), 0.0f, float2(m_sample_size) - 1.0f);
    const int2 base = int2(floor(a));
    const float2 w = a - float2(base);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float weight = 0.0f;
    float best = 0.0f;
    z = 0.0f;
    [unroll] for (uint i = 0u; i < 4u; ++i) {
        const int2 o = int2(i & 1u, i >> 1u);
        const int2 s = min(base + o, int2(m_sample_size) - 1);
        const uint2 u = m_under[uint(s.y) * m_sample_size.x + uint(s.x)];
        const float uz = asfloat(u.y);
        const float bw = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y) + 1e-4f;
        if (u.y != 0u && uz >= limit) {
            continue;
        }
        sum += unpack_r11g11b10(u.x) * bw;
        weight += bw;
        if (bw > best) {
            best = bw;
            z = u.y != 0u ? uz : 0.0f;
        }
    }
    found = saturate(weight);
    return weight > 0.0f ? sum / weight : float3(0.0f, 0.0f, 0.0f);
}

// The floor under the water at `pixel`, softened over a sample each way: water scatters, and the
// game's thin projected caustic lines on the sand fade instead of showing as outlines. Taps with
// nothing behind the water leave the mean; `found` is the share that had something.
float3 under_soft(float2 pixel, float limit, out float z, out float found)
{
    const float r = 1.5f * float(m_factor);
    const float2 offsets[5] = {float2(0.0f, 0.0f), float2(r, r), float2(-r, r), float2(r, -r), float2(-r, -r)};
    const float weights[5] = {0.4f, 0.15f, 0.15f, 0.15f, 0.15f};
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float total = 0.0f;
    z = 0.0f;
    [unroll] for (uint k = 0u; k < 5u; ++k) {
        float tz;
        float f;
        const float3 c = under_at(pixel + offsets[k], limit, tz, f);
        sum += c * (weights[k] * f);
        total += weights[k] * f;
        if (k == 0u) {
            z = tz;
        }
    }
    found = total;
    return total > 1e-4f ? sum / total : float3(0.0f, 0.0f, 0.0f);
}

// Sunlight the surface's waves focus onto the floor at `floor` (eye space) under the water plane
// through `surface` with normal `plane`: the waves where the sun's ray enters (world space, wave_focus),
// light only added where they gather (the floor never darker), fading with the depth.
float caustic(float3 floor, float3 surface, float3 plane)
{
    const float3 s = m_sun.xyz;
    const float rise = dot(plane, s);
    if (m_sun.w <= 0.0f || rise < 0.1f) {
        return 1.0f;
    }
    const float depth = dot(plane, surface - floor);
    if (depth <= 1.0f) {
        return 1.0f;
    }
    const uint n = m_sample_size.x * m_sample_size.y;
    float4 to_world[3];
    [unroll] for (uint r = 0u; r < 3u; ++r) {
        const uint2 a = m_under[n + 2u + 2u * r];
        const uint2 b = m_under[n + 3u + 2u * r];
        to_world[r] = float4(asfloat(a.x), asfloat(a.y), asfloat(b.x), asfloat(b.y));
    }
    const float3 world_up = normalize(rotate(to_world, plane));
    const float3 t1 = normalize(abs(world_up.y) < 0.9f ? cross(world_up, float3(0.0f, 1.0f, 0.0f))
                                                       : cross(world_up, float3(1.0f, 0.0f, 0.0f)));
    const float3 t2 = cross(world_up, t1);
    const float3 enter = transform(to_world, floor + s * (depth / rise));
    const float focus = wave_focus(float2(dot(enter, t1), dot(enter, t2)), float(m_under[n + 1u].y), depth);
    return 1.0f + max(focus - 1.0f, 0.0f) * 0.35f * saturate(m_sun.w) * exp(-depth / 1500.0f);
}

// The translucent water's reflections (WaterTraceCS), after the water is drawn: Fresnel-weighted
// reflection and highlights over the water's body, which shows the floor through the rippled surface
// (refracted, lit by the sun the ripples focus) and deepens the game's water colour towards blue with
// the depth of water the light crosses. Sub-frames move the pixel to the traced frame's camera along the
// water the samples hold there. What the EFB holds in front of the water keeps its colour.
CompositeOut WaterCompositePS(float4 position : SV_Position)
{
    if (!inside(m_viewport, position.xy) || position.x < m_split + 1.0f) {
        discard;
    }
    float2 pixel = position.xy;
    float z = water_z(pixel);
    if (z == 0.0f) {
        discard;
    }
    const float3 here = eye_position(m_projection, m_viewport, position.xy, z);
    float3 p = here;
    if ((m_flags & 1u) != 0u) {
        const uint row = m_slot * 3u;
        const float4 rows[3] = {m_reproject[row], m_reproject[row + 1u], m_reproject[row + 2u]};
        p = transform(rows, here);
        if (p.z >= -m_near) {
            discard;
        }
        pixel = eye_to_pixel(m_projection, m_viewport, p);
        z = water_z(pixel);
        if (z == 0.0f) {
            discard;
        }
        p = eye_position(m_projection, m_viewport, pixel, z);
    }
    const float depth = m_efb_depth.Load(int3(int2(position.xy), 0));
    if (!far_plane(m_depth, depth) && depth_to_eye_z(m_depth, depth) > here.z + abs(here.z) * 0.003f + 1.0f) {
        discard;
    }
    const uint view = (m_flags >> 1u) & 3u;
    Blend b = blend_samples(pixel, p, true);
    // Where the samples under the pixel disagree in normal (a rounded lip between the water's top and
    // its side wall), their reflections and glints do not belong to one surface: a line of them would
    // trace the edge. They fade there; flat water seen at a grazing angle keeps its reflection.
    if (b.surface > 1e-4f) {
        const float agree = length(b.normal) / b.surface;
        const float edge = smoothstep(0.88f, 0.98f, agree);
        b.sum *= edge;
        b.covered *= edge;
        b.k_sum *= edge;
        b.glint *= edge;
    }
    if (view == 2u) {
        if (b.nearest_class == 15u) {
            discard;
        }
        return class_view(b.nearest_class);
    }
    if (b.weight < 1e-3f || b.surface < 1e-3f) {
        discard;
    }
    // The water's body: the seabed along the refracted ray (as deep below the surface as straight on), lit
    // by the sun the waves focus, seen through the water's transmittance T along that path, and the
    // water's own colour for the light it scatters (1 - T): the game's water averaged over the frame (its
    // deep water where it has some), so the tiled texture it draws gives way (kWaterCover of it) to the
    // traced water: shallows show the seabed, depth the water's colour, reflections and glints over it.
    float3 under_add = float3(0.0f, 0.0f, 0.0f);
    float3 under_keep = float3(1.0f, 1.0f, 1.0f);
    const float3 n = normalize(b.normal);
    if (m_water.w > 0.0f && b.nearest_class == kWaterSurface) {
        // Seen edge-on (a side wall, the lip) the body would only band: it fades out there.
        const float share = m_water.w * b.surface / b.weight * smoothstep(0.15f, 0.4f, dot(n, -normalize(p)));
        float floor_z;
        float floor_found;
        under_at(pixel, p.z - 1.0f, floor_z, floor_found);
        float path = 4000.0f;
        float3 seen = float3(0.0f, 0.0f, 0.0f);
        float3 floor_point = p;
        if (floor_z != 0.0f) {
            const float3 v = normalize(p);
            const float3 straight = v * (floor_z / v.z);
            const float span = max(length(straight) - length(p), 0.0f);
            const float3 bent = refract(v, n, 0.75f);
            float3 floor_at = any(bent != 0.0f) ? p + bent * span : straight;
            // The refracted lookup where what it meets lies behind the water, the straight one where not,
            // blended by that share (a switch per pixel showed as speckles on the shallows).
            float bent_z;
            float bent_found;
            const float2 bent_pixel = eye_to_pixel(m_projection, m_viewport, floor_at);
            const float3 bent_colour = under_soft(bent_pixel, p.z - 1.0f, bent_z, bent_found);
            float straight_z;
            float straight_found;
            const float3 straight_colour = under_soft(pixel, p.z - 1.0f, straight_z, straight_found);
            const float use_bent = bent_z != 0.0f ? smoothstep(0.55f, 0.95f, bent_found) : 0.0f;
            float3 floor_colour = lerp(straight_colour, bent_colour, use_bent);
            floor_at = lerp(straight, bent_z != 0.0f ? eye_position(m_projection, m_viewport, bent_pixel, bent_z) : straight,
                            use_bent);
            path = max(length(floor_at - p), 0.0f);
            seen = floor_colour;
            floor_point = floor_at;
        }
        // The game's own water as it drew it (its colours, its gradient, the seabed it shows), low-passed
        // over about 40 world units so its tiled texture's repetition goes, refracted by the waves (the
        // taps move with their slope), with soft caustics in the shallows and broken foam at the shore;
        // the traced reflections and glints go over it (finish).
        float4 to_world[3];
        {
            const uint s0 = m_sample_size.x * m_sample_size.y;
            [unroll] for (uint r = 0u; r < 3u; ++r) {
                const uint2 a = m_under[s0 + 2u + 2u * r];
                const uint2 c = m_under[s0 + 3u + 2u * r];
                to_world[r] = float4(asfloat(a.x), asfloat(a.y), asfloat(c.x), asfloat(c.y));
            }
        }
        const float frame = float(m_under[m_sample_size.x * m_sample_size.y + 1u].y);
        const float3 wn = normalize(rotate(to_world, n));
        const float3 t1 = normalize(abs(wn.y) < 0.9f ? cross(wn, float3(0.0f, 1.0f, 0.0f)) : cross(wn, float3(1.0f, 0.0f, 0.0f)));
        const float3 t2 = cross(wn, t1);
        const float3 wp = transform(to_world, p);
        const float2 uv = float2(dot(wp, t1), dot(wp, t2));
        const float2 slope = wave_slope(uv, frame, false) * m_water2.x;
        // The apparent seabed moves by the slope times the water's depth (at most 120 units).
        const float3 shift_world = (slope.x * t1 + slope.y * t2) * min(path, 120.0f) * 0.6f;
        const float3 shift_eye = float3(dot(float3(to_world[0].x, to_world[1].x, to_world[2].x), shift_world),
                                        dot(float3(to_world[0].y, to_world[1].y, to_world[2].y), shift_world),
                                        dot(float3(to_world[0].z, to_world[1].z, to_world[2].z), shift_world));
        const float2 wobble = clamp(eye_to_pixel(m_projection, m_viewport, p + shift_eye) - pixel, -24.0f, 24.0f);
        const float px_per_unit = 0.5f * m_viewport.w * abs(m_projection.z) / max(-p.z, 1.0f);
        const float radius = clamp(40.0f * px_per_unit, 5.0f, 28.0f);
        const float2 lo = m_viewport.xy;
        const float2 hi = m_viewport.xy + m_viewport.zw - 1.0f;
        float3 water;
        if (m_water2.y > 0.0f) {
            // The water layer the game drew (the frame with it less the frame before it) low-passed by a
            // bilateral filter (its tiled texture goes, its colour and gradient stay), over the seabed as it
            // lies (sharp), moved by the waves' refraction where the moved point is water too.
            const int2 at = int2(clamp(position.xy, lo, hi));
            const float3 layer_c = m_scene.Load(int3(at, 0)).rgb - m_opaque.Load(int3(at, 0)).rgb;
            float3 layer = layer_c;
            float taps = 1.0f;
            [unroll] for (uint k = 0u; k < 16u; ++k) {
                const float a = float(k) * 2.39996f;
                const int2 q = int2(clamp(position.xy + float2(cos(a), sin(a)) * (radius * sqrt((float(k) + 0.5f) / 16.0f)), lo, hi));
                const float3 c = m_scene.Load(int3(q, 0)).rgb - m_opaque.Load(int3(q, 0)).rgb;
                const float3 d = c - layer_c;
                const float w = exp(-dot(d, d) / 0.02f);
                layer += c * w;
                taps += w;
            }
            layer /= taps;
            const bool moved = water_z(pixel + wobble) != 0.0f;
            const float3 seabed = m_opaque.Load(int3(int2(clamp(position.xy + (moved ? wobble : 0.0f), lo, hi)), 0)).rgb;
            water = max(seabed + layer, 0.0f);
        } else {
            const float3 centre = m_scene.Load(int3(int2(clamp(position.xy + wobble, lo, hi)), 0)).rgb;
            water = centre;
            float taps = 1.0f;
            [unroll] for (uint k = 0u; k < 16u; ++k) {
                const float a = float(k) * 2.39996f;
                const float2 o = float2(cos(a), sin(a)) * (radius * sqrt((float(k) + 0.5f) / 16.0f)) + wobble;
                const float3 c = m_scene.Load(int3(int2(clamp(position.xy + o, lo, hi)), 0)).rgb;
                const float3 d = c - centre;
                const float w = exp(-dot(d, d) / 0.02f);
                water += c * w;
                taps += w;
            }
            water /= taps;
        }
        // Shallows: the sunlight the waves focus on the seabed (its own caustics, moving).
        const float shallow = floor_z != 0.0f ? exp(-path / 120.0f) : 0.0f;
        if (shallow > 0.0f && m_water2.w > 0.0f) {
            water *= lerp(1.0f, caustic(floor_point, p, n), saturate(shallow * m_water2.w));
        }
        // The shore: foam where the water is a few units deep, broken by drifting noise.
        if (floor_z != 0.0f && m_water2.z > 0.0f) {
            const float shore = smoothstep(20.0f, 3.0f, path);
            const float lace = wave_noise(float3(uv * 0.07f, frame * 0.02f)) * 0.65f +
                               wave_noise(float3(uv * 0.19f + 5.3f, frame * 0.035f)) * 0.35f;
            const float foam = shore * smoothstep(0.45f, 0.7f, lace) * m_water2.z;
            water = lerp(water, max(luminance(water), 0.75f).xxx * float3(1.0f, 1.0f, 1.02f), saturate(foam));
        }
        const float cover = abs(m_water2.y) * share;
        under_add = water * cover;
        under_keep = (1.0f - cover).xxx;
    }
    if (b.k_sum < 1e-4f && !any(b.glint > 0.0f) && !any(under_add > 0.0f) && !any(under_keep < 1.0f)) {
        discard;
    }
    return finish(b, view, under_add, under_keep);
}

// The water pass's state (WaterTraceCS write_underwater_state), after the samples in m_under.
uint2 under_state(uint k)
{
    return m_under[m_sample_size.x * m_sample_size.y + k];
}

// Under the water: what the EFB shows below the surface takes the sunlight the waves focus onto it
// (caustics: light added where they gather, taken where they spread), and the water between it and
// the camera scatters sunlight towards it (shafts), both fading with the light's path through the
// water (absorption). Only while the camera is under the water the sun shines through.
CompositeOut UnderwaterPS(float4 position : SV_Position)
{
    const uint2 state = under_state(0u);
    if (state.y == 0u || !inside(m_viewport, position.xy) || position.x < m_split + 1.0f) {
        discard;
    }
    const float3 s = m_sun.xyz;
    const float3 up = unpack_normal(under_state(1u).x);
    const float frame = float(under_state(1u).y);
    float4 to_world[3];
    [unroll] for (uint r = 0u; r < 3u; ++r) {
        const uint2 a = under_state(2u + 2u * r);
        const uint2 b = under_state(3u + 2u * r);
        to_world[r] = float4(asfloat(a.x), asfloat(a.y), asfloat(b.x), asfloat(b.y));
    }
    const uint2 light_gain = under_state(8u);
    const float3 sun = unpack_r11g11b10(light_gain.x);
    const float shafts = asfloat(light_gain.y);
    const float rise = max(dot(up, s), 0.05f);
    const float plane = asfloat(state.x);
    // A plane basis in world space, so the waves stay with the world.
    const float3 world_up = normalize(rotate(to_world, up));
    const float3 t1 = normalize(abs(world_up.y) < 0.9f ? cross(world_up, float3(0.0f, 1.0f, 0.0f))
                                                       : cross(world_up, float3(1.0f, 0.0f, 0.0f)));
    const float3 t2 = cross(world_up, t1);

    const float depth = m_efb_depth.Load(int3(int2(position.xy), 0));
    const bool sky = far_plane(m_depth, depth);
    float3 p = eye_position(m_projection, m_viewport, position.xy, sky ? -4000.0f : depth_to_eye_z(m_depth, depth));
    if ((m_flags & 1u) != 0u) {
        const uint row = m_slot * 3u;
        const float4 rows[3] = {m_reproject[row], m_reproject[row + 1u], m_reproject[row + 2u]};
        p = transform(rows, p);
    }
    float3 add = float3(0.0f, 0.0f, 0.0f);
    float3 keep = float3(1.0f, 1.0f, 1.0f);
    // Caustics on the surface the pixel shows, if it lies under the water.
    const float below = plane - dot(up, p);
    if (!sky && below > 1.0f) {
        const float along = below / rise;
        const float3 enter = transform(to_world, p + s * along);
        const float focus = wave_focus(float2(dot(enter, t1), dot(enter, t2)), frame, below);
        const float3 through = exp(-m_water.rgb * along);
        const float k = saturate(m_sun.w) * 0.5f;
        add += sun * through * (max(focus - 1.0f, 0.0f) * 0.12f * k);
        keep *= 1.0f - saturate(1.0f - focus) * k * through;
    }
    // Shafts: eight steps along the view through the water, each lit by the light its waves focus.
    if (shafts > 0.0f) {
        const float span = min(length(p), 4000.0f);
        const float3 v = normalize(p);
        const float jitter = frac(52.9829189f * frac(0.06711056f * position.x + 0.00583715f * position.y) +
                                  0.618034f * frame);
        float3 sum = float3(0.0f, 0.0f, 0.0f);
        [unroll] for (uint i = 0u; i < 8u; ++i) {
            const float t = span * (float(i) + jitter) / 8.0f;
            const float3 x = v * t;
            const float depth_x = plane - dot(up, x);
            if (depth_x <= 0.0f) {
                continue;
            }
            const float along = depth_x / rise;
            const float3 enter = transform(to_world, x + s * along);
            const float focus = wave_focus(float2(dot(enter, t1), dot(enter, t2)), frame, depth_x);
            sum += exp(-m_water.rgb * (along + t)) * (focus * focus);
        }
        // Forward scattering towards the sun (Henyey-Greenstein, g 0.6).
        const float c = dot(v, s);
        const float phase = (1.0f - 0.36f) / pow(max(1.0f + 0.36f - 1.2f * c, 1e-3f), 1.5f) / (4.0f * kPi);
        add += sun * sum * (span / 8.0f) * (shafts * 2e-4f * phase);
    }
    // The water between the camera and what the pixel shows: its light fades with the depth of water
    // it crosses and the light the water scatters takes its place: clear for the first 600 units
    // (the player sees what they do), a light blue-teal veil over the far distance (measured: 250
    // units and 0.0008 per unit left Beach Bowl's lagoon murky).
    {
        const float span = max((sky ? 4000.0f : length(p)) - 600.0f, 0.0f);
        const float3 t = exp(-float3(0.00032f, 0.00016f, 0.00012f) * span);
        const float3 scatter = (sun * 0.4f + 0.25f) * float3(0.16f, 0.58f, 0.72f);
        add = add * t + scatter * (1.0f - t);
        keep *= t;
    }
    CompositeOut result;
    result.add = float4(saturate(add), 0.0f);
    result.keep = float4(saturate(keep), 1.0f);
    return result;
}
