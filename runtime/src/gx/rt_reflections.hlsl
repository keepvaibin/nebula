// SPDX-License-Identifier: GPL-3.0-only
// Ray-traced reflections of the game frame's metal, glossy and water surfaces (DXR 1.1 inline
// ray queries, cs_6_5) and their composite into the EFB (vs/ps_6_0). Compiled by DXC at build time.
//
// The lighting's samples (one per g_factor x g_factor EFB pixels) with the triangle its camera ray
// met: the draw's GX surface class (rt_surfaces.h) selects the lobe width, Fresnel reflectance,
// strength and cover, the vertex normals give the shading normal. One reflection ray samples the
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
                                // bit 3: camera known (environment memory)
    float4 g_reproject[3];      // previous samples' eye position from this frame's (3x4 rows)
    float4 g_world_to_eye[3];   // the camera's rotation (rows)
    float4 g_eye_to_world[3];   // its inverse
    float4 g_class[8];          // per surface class: GGX alpha, strength, F0, metal
    float g_range;              // reflection ray length (eye units)
    float g_lobe;               // share of the lobe sampled (1 the whole lobe)
    uint g_history_frames;      // accumulation limit of the widest lobes
    float g_fallback;           // confidence of the scene's average light for hits nothing showed
    float g_memory_fade;        // environment memory kept per frame where the camera does not look
    uint3 g_pad;
};

RaytracingAccelerationStructure g_scene : register(t0);
ByteAddressBuffer g_positions : register(t1);       // per triangle three eye-space vertices
// Per triangle, 16 bytes: vertex normals (octahedral 16:16, 1-65535 per axis, 0 none), draw flags.
ByteAddressBuffer g_surfaces : register(t2);
StructuredBuffer<uint3> g_light : register(t3);     // this frame's lighting (rt_lighting.hlsl SpatialBCS)
Texture2D<float4> g_color : register(t4);           // EFB colour of the opaque scene
Texture2D<float> g_efb_depth : register(t5);
StructuredBuffer<uint4> g_prev_info : register(t6);
StructuredBuffer<uint3> g_prev_history : register(t7);
ByteAddressBuffer g_sums : register(t8);            // previous picture: sky r g b n, scene r g b n (x 1/255)
StructuredBuffer<uint3> g_history_in : register(t9);
SamplerState g_linear : register(s0);
// Per sample: packed normal, eye z (0: no surface), class (bits 0-3) and emitter-free share
// (bits 8-15, unorm), reflection distance.
RWStructuredBuffer<uint4> g_info : register(u0);
// Per sample: reflected light (rgb) and its confidence.
RWStructuredBuffer<uint2> g_raw : register(u1);
// Per sample: accumulated light and confidence (halves), frames.
RWStructuredBuffer<uint3> g_history : register(u2);
// Per sample for the composite: light (11-11-10), packed normal, eye z with the class in its low
// 4 bits, weight.
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

// Stats words: 0 samples on geometry, 1 with a traced triangle, 2-6 per class (diffuse, glossy,
// metal, unlit, water), 7 reflection rays, 8 hits this frame shows, 9 hits the memory knows,
// 10 other hits, 11 sky this frame shows, 12 sky the memory knows, 13 sky average.
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
    const uint3 m = g_light[s.y * g_sample_size.x + s.x];
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
        light *= lighting_at(c);
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
        memory.w *= g_memory_fade;
    }
    g_memory[index] = uint4(pack_rgb_w(memory.rgb, memory.w), asuint(distance), 0u);
}

// The environment memory towards eye-space point or direction `x` from the camera: its light and
// how far it agrees (the freshness, and for a point at `distance` the share of a matching distance).
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
    if (distance >= kSkyDistance) {
        return seen >= kSkyDistance ? memory.w : 0.0f;
    }
    return seen < kSkyDistance ? memory.w * saturate(1.5f - abs(seen - distance) / (0.4f * distance)) : 0.0f;
}

[numthreads(8, 8, 1)]
void ReflectTraceCS(uint3 id : SV_DispatchThreadID)
{
    const bool active = all(id.xy < g_sample_size);
    const uint index = id.y * g_sample_size.x + id.x;
    const int2 q = sample_pixel(min(id.xy, g_sample_size - 1u));
    const float depth = g_efb_depth.Load(int3(q, 0));
    const bool geometry = active && !far_plane(g_depth, depth) && inside(g_viewport, float2(q) + 0.5f);
    uint4 info = uint4(0u, 0u, kNoSurface, 0u);
    float3 light = float3(0.0f, 0.0f, 0.0f);
    float confidence = 0.0f;
    uint surface_class = kNoSurface;
    bool reflected = false, shown = false, remembered = false, other = false;
    bool sky_shown = false, sky_remembered = false, sky_average = false;
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
            const float4 params = g_class[surface_class];
            const float3 base = g_color.Load(int3(q, 0)).rgb;
            // Lamps, lava, glowing cores and what glows light: bright under this frame's lighting,
            // they keep their own colour.
            const float3 lit = base * lighting_at(q);
            const float emitter = smoothstep(0.75f, 1.0f, max(lit.r, max(lit.g, lit.b)));
            info.x = pack_normal(n);
            info.z = surface_class | (uint(round((1.0f - emitter) * 255.0f)) << 8u);
            if (params.y > 0.0f && emitter < 1.0f) {
                reflected = true;
                const float3 v = -dir;
                const float sign_z = n.z >= 0.0f ? 1.0f : -1.0f;
                const float ta = -1.0f / (sign_z + n.z);
                const float tb = n.x * n.y * ta;
                const float3 t1 = float3(1.0f + sign_z * n.x * n.x * ta, sign_z * tb, -sign_z * n.x);
                const float3 t2 = float3(tb, sign_z + n.y * n.y * ta, -n.y);
                const float2 u = sample_point(id.xy);
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
                float distance = g_range;
                if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                    distance = query.CommittedRayT();
                    const float3 x = reflection.Origin + l * distance;
                    const float x_distance = length(x);
                    float seen;
                    shown = x.z < -g_near && shown_at(eye_to_pixel(g_projection, g_viewport, x), light, seen) &&
                            abs(seen - x_distance) < 0.03f * x_distance + 2.0f;
                    if (shown) {
                        confidence = 1.0f;
                    } else {
                        float3 known;
                        confidence = memory_light(x, x_distance, known);
                        remembered = confidence > 0.05f;
                        other = !remembered;
                        if (remembered) {
                            light = known;
                        } else {
                            // Unknown: the surface keeps its own colour unless the scene's average
                            // is allowed in (GALAXY_RT_REFLECT_FALLBACK).
                            confidence = average(16u, light) ? g_fallback : 0.0f;
                        }
                    }
                } else {
                    float seen;
                    sky_shown = l.z < -1e-3f && shown_at(eye_to_pixel(g_projection, g_viewport, l), light, seen) &&
                                seen >= kSkyDistance;
                    if (sky_shown) {
                        confidence = 1.0f;
                    } else {
                        float3 known;
                        confidence = memory_light(l, kSkyDistance, known);
                        sky_remembered = confidence > 0.05f;
                        sky_average = !sky_remembered;
                        if (sky_remembered) {
                            light = known;
                        } else {
                            confidence = average(0u, light) ? 0.5f : 0.0f;
                        }
                    }
                }
                if (params.w > 0.0f) {
                    // Metal: the reflection takes the hue (luminance 1) of the surface's own colour.
                    const float3 tint = min(base / max(luminance(base), 0.1f), 3.0f);
                    light *= lerp(float3(1.0f, 1.0f, 1.0f), tint, params.w);
                }
                info.w = asuint(distance);
            }
        }
    }
    count(0u, geometry);
    count(1u, surface_class != kNoSurface);
    [unroll] for (uint c = 0u; c < 5u; ++c) {
        count(2u + c, surface_class == c);
    }
    count(7u, reflected);
    count(8u, shown);
    count(9u, remembered);
    count(10u, other);
    count(11u, sky_shown);
    count(12u, sky_remembered);
    count(13u, sky_average);
    if (active) {
        g_info[index] = info;
        g_raw[index] = pack_rgb_w(light, confidence);
    }
}

bool reflective(uint4 info)
{
    return info.y != 0u && g_class[min(info.z & 15u, 7u)].y > 0.0f && (info.z >> 8u) != 0u;
}

// Accumulation: the reflection's position in the previous frame (the virtual image point behind the
// surface for sharp lobes, the surface for wide ones), bilinear over the previous samples on the
// same plane and class, clamped to the spread of the neighbourhood's new light.
[numthreads(8, 8, 1)]
void ReflectTemporalCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_sample_size)) {
        return;
    }
    const uint index = id.y * g_sample_size.x + id.x;
    const uint4 info = g_info[index];
    if (!reflective(info)) {
        g_history[index] = uint3(0u, 0u, 0u);
        return;
    }
    const uint surface_class = info.z & 15u;
    const float alpha = g_class[surface_class].x;
    const float4 raw = unpack_rgb_w(g_raw[index]);
    float3 m1 = float3(0.0f, 0.0f, 0.0f);
    float3 m2 = float3(0.0f, 0.0f, 0.0f);
    float n = 0.0f;
    [unroll] for (int dy = -1; dy <= 1; ++dy) {
        [unroll] for (int dx = -1; dx <= 1; ++dx) {
            const int2 s = clamp(int2(id.xy) + int2(dx, dy), int2(0, 0), int2(g_sample_size) - 1);
            const uint si = uint(s.y) * g_sample_size.x + uint(s.x);
            const uint4 other = g_info[si];
            if (other.y == 0u || (other.z & 15u) != surface_class) {
                continue;
            }
            // The spread of what is known: each sample counts by its confidence.
            const float4 r = unpack_rgb_w(g_raw[si]);
            const float3 c = to_ycocg(r.rgb);
            m1 += c * r.w;
            m2 += c * c * r.w;
            n += r.w;
        }
    }
    const float3 mean = m1 / max(n, 1e-3f);
    const float3 spread = n > 0.5f ? sqrt(max(m2 / n - mean * mean, 0.0f)) : float3(1e4f, 1e4f, 1e4f);
    float4 result = raw;
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
                const uint3 history = g_prev_history[si];
                sum += unpack_rgb_w(history.xy) * bw;
                count_sum += asfloat(history.z) * bw;
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
            }
        }
    }
    g_history[index] = uint3(pack_rgb_w(max(result.rgb, 0.0f), saturate(result.w)), asuint(frames));
}

// The filters' input: the accumulated light (first pass) or the first pass's result (second).
float4 filter_input(uint index, bool first)
{
    return first ? unpack_rgb_w(g_history_in[index].xy) : unpack_rgb_w(g_raw[index]);
}

// Edge-aware 5x5 filter (a-trous): steps as wide as the lobe, doubled in the second pass; none for
// the sharpest lobes.
float4 filtered_light(uint2 id, uint index, uint4 info, bool first)
{
    const uint surface_class = info.z & 15u;
    const float alpha = g_class[surface_class].x;
    const float4 centre = filter_input(index, first);
    float4 value = centre;
    const int step = (alpha < 0.02f ? 0 : alpha < 0.12f ? 1 : 2) * (first ? 1 : 2);
    if (step != 0) {
        const float z = asfloat(info.y);
        const float3 p = sample_position(id, z);
        const float3 n = unpack_normal(info.x);
        const float inv_tolerance = 1.0f / (0.25f + abs(z) * 0.004f * float(step));
        const float centre_l = luminance(centre.rgb);
        float3 sum = float3(0.0f, 0.0f, 0.0f);
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
                const float4 c = filter_input(si, first);
                // Edges between known lights stop the filter; light counts by its confidence.
                const float edge = abs(luminance(c.rgb) - centre_l) / (0.1f + centre_l + alpha) * min(c.w, centre.w);
                const float w = exp(-0.3f * float(dx * dx + dy * dy) - plane * plane - edge * edge) *
                                (facing2 * facing2) * (facing2 * facing2);
                sum += c.rgb * (w * c.w);
                known += w * c.w;
                weight += w;
            }
        }
        if (weight > 1e-4f) {
            value = float4(known > 1e-4f ? sum / known : centre.rgb, known / weight);
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
    const float4 value = reflective(info) ? filtered_light(id.xy, index, info, true) : float4(0.0f, 0.0f, 0.0f, 0.0f);
    g_raw[index] = pack_rgb_w(value.rgb, value.w);
}

// The second pass: the sample's result for the composite.
uint4 filter_sample(uint2 id, uint index)
{
    const uint4 info = g_info[index];
    const uint surface_class = info.z & 15u;
    const uint z_class = (info.y & ~15u) | surface_class;
    if (!reflective(info)) {
        return uint4(0u, info.x, info.y != 0u ? z_class : 0u, 0u);
    }
    const float4 value = filtered_light(id, index, info, false);
    // The sample's weight: its confidence, without its emitter share.
    const float weight = saturate(value.w) * float(info.z >> 8u) / 255.0f;
    return uint4(pack_r11g11b10(value.rgb), info.x, z_class, asuint(weight));
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
        if (asfloat(result.w) > 0.0f) {
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
    float4 m_class[5];  // per surface class: strength, F0, GGX alpha, cover
};
StructuredBuffer<uint4> m_final : register(t0);
Texture2D<float> m_efb_depth : register(t1);
StructuredBuffer<float4> m_reproject : register(t2);
StructuredBuffer<uint> m_tiles : register(t3);

struct CompositeOut
{
    float4 add : SV_Target0;
    float4 keep : SV_Target1;
};

float4 ReflectCompositeVS(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1u) & 2u, id & 2u);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
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
    const float2 a = (eye_to_pixel(m_projection, m_viewport, p) - 0.5f) / float(m_factor);
    const int2 base = int2(floor(a));
    if (view == 0u) {
        // Nothing to blend where no sample of the footprint's tiles reflects.
        const uint2 lo = uint2(clamp(base, int2(0, 0), int2(m_sample_size) - 1)) >> 3u;
        const uint2 hi = uint2(clamp(base + 1, int2(0, 0), int2(m_sample_size) - 1)) >> 3u;
        if ((m_tiles[lo.y * m_tile_width + lo.x] | m_tiles[lo.y * m_tile_width + hi.x] |
             m_tiles[hi.y * m_tile_width + lo.x] | m_tiles[hi.y * m_tile_width + hi.x]) == 0u) {
            discard;
        }
    }
    const float3 v = -normalize(p);
    const float2 w = a - float2(base);
    const float tolerance = abs(p.z) * 0.004f + 0.5f;
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float3 covered = float3(0.0f, 0.0f, 0.0f);
    float k_sum = 0.0f;
    float weight = 0.0f;
    uint nearest_class = 15u;
    float nearest = 1e30f;
    [unroll] for (uint i = 0u; i < 4u; ++i) {
        const int2 o = int2(i & 1u, i >> 1u);
        const int2 s = clamp(base + o, int2(0, 0), int2(m_sample_size) - 1);
        const uint4 sample = m_final[uint(s.y) * m_sample_size.x + uint(s.x)];
        if (sample.z == 0u) {
            continue;
        }
        const uint surface_class = sample.z & 15u;
        const float sz = asfloat(sample.z & ~15u);
        const float3 sp = eye_position(m_projection, m_viewport, float2(s * m_factor) + 0.5f, sz);
        const float3 n = unpack_normal(sample.y);
        // Same surface: on the sample's plane (its depth where no plane is known).
        const float d = (surface_class == 15u ? abs(sz - p.z) : abs(dot(n, p - sp))) / tolerance;
        const float bw = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y) * exp(-d * d);
        if (d < nearest) {
            nearest = d;
            nearest_class = surface_class;
        }
        if (surface_class < 5u) {
            const float4 c = m_class[surface_class];
            const float k = c.x * fresnel(c.y, c.z, dot(n, v)) * asfloat(sample.w);
            const float3 light = min(unpack_r11g11b10(sample.x), 1.0f);
            sum += light * (k * bw);
            // How much of the surface's own colour the reflection takes away: all of it at cover 1
            // (dst * (1 - k) + k * light), only its own share at cover 0 (a screen: never darker).
            covered += (c.w + (1.0f - c.w) * light) * (k * bw);
            k_sum += k * bw;
        }
        weight += bw;
    }
    CompositeOut result;
    result.add = float4(0.0f, 0.0f, 0.0f, 0.0f);
    result.keep = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (view == 2u) {
        // Surface classes over the picture: diffuse grey, glossy cyan, metal orange, unlit magenta,
        // water blue, untraced black.
        const float3 colours[6] = {float3(0.3f, 0.3f, 0.3f), float3(0.0f, 0.86f, 1.0f), float3(1.0f, 0.67f, 0.0f),
                                   float3(1.0f, 0.0f, 1.0f), float3(0.0f, 0.3f, 1.0f), float3(0.0f, 0.0f, 0.0f)};
        result.add = float4(colours[min(nearest_class, 5u)] * 0.55f, 0.0f);
        result.keep = float4(0.45f, 0.45f, 0.45f, 1.0f);
        return result;
    }
    if (weight < 1e-3f || k_sum < 1e-4f) {
        if (view != 0u) {
            result.keep = float4(0.0f, 0.0f, 0.0f, 1.0f);
            return result;
        }
        discard;
    }
    const float k = saturate(k_sum / weight);
    const float3 reflection = sum / k_sum;
    if (view == 1u) {
        result.add = float4(reflection, 0.0f);
        result.keep = float4(0.0f, 0.0f, 0.0f, 1.0f);
    } else if (view == 3u) {
        result.add = float4(k, k, k, 0.0f);
        result.keep = float4(0.0f, 0.0f, 0.0f, 1.0f);
    } else {
        result.add = float4(saturate(sum / weight), 0.0f);
        result.keep = float4(saturate(1.0f - covered / weight), 1.0f);
    }
    return result;
}
