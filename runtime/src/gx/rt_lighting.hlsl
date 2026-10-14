// SPDX-License-Identifier: GPL-3.0-only
// Ray-traced lighting of the game frame's opaque scene (DXR 1.1 inline ray queries, cs_6_5) and
// its composite into the EFB (vs/ps_6_0). Compiled by DXC at build time.
//
// The game's own lights relight every opaque surface. Its visible sun: a direction with a
// disc, whose shadow ray decides each sample's direct light (n.l where it reaches the sky).
// Its point lights: one picked per sample in proportion to its unshadowed light, with a shadow
// ray to it. The sky and what surrounds the surface: a ray that leaves the scene brings 1,
// tinted towards the sky's colour; a ray that hits geometry brings the light that surface sends
// back, read from the previous frame's finished picture where the surface was visible (its
// brightest pixels count as emitters), darker the closer the hit. The surface's colour is
// multiplied by the weighted sum: lit faces brighten in the lights' colours, shadows fall away
// from them and take the colours around them.
//
// Samples are taken once per g_factor x g_factor EFB pixels, accumulate over frames along the
// game camera's motion and pass two edge-aware filters. The composite reads them at each
// pixel's own depth; interpolated sub-frames first move the pixel to the traced frame's camera.

cbuffer RtLight : register(b0)
{
    float4 g_projection;        // GX projection A, B, C, D
    float4 g_depth;             // D3D projection [2][2], [2][3]; viewport min depth, max depth
    float4 g_viewport;          // left, top, width, height (EFB pixels)
    uint2 g_target_size;        // EFB pixels
    uint2 g_sample_size;        // samples
    float g_near;               // near-plane distance (eye units)
    uint g_frame;               // sample sequence index
    float g_range;              // ray length (eye units)
    float g_strength;           // 0 none .. 1 full
    uint g_rays;                // rays per sample
    uint g_factor;              // EFB pixels per sample side
    uint g_flags;               // bit 0: previous samples usable; bit 1: previous picture usable;
                                // bits 2-4: results hold the sun's, the sky's, the point lights'
                                // light alone
    uint g_history_frames;      // accumulation limit
    float4 g_reproject[3];      // previous samples' eye position from this frame's (3x4 rows)
    float4 g_picture[3];        // the previous picture's eye position from this frame's
    float g_contact;            // light a hit at distance 0 sends back (relative)
    float g_contact_range;      // distance over which a hit reaches its full light
    float g_sky_saturation;     // 0 white .. 1 the sky's own hue
    float g_fallback;           // light of a hit the previous picture did not show
    float4 g_sun;               // towards the sun in eye space (xyz), tangent of its radius (w)
    float4 g_sun_light;         // sun light (rgb), weight of the sky and bounce light (w)
    uint g_point_count;         // point lights in g_points (0-8)
    float g_point_light;        // their weight
    float g_point_size;         // radius of the geometry around a light (its emitter)
    float g_point_pad;
    float4 g_points[16];        // per light: eye position (xyz), range (w); colour (rgb)
};

RaytracingAccelerationStructure g_scene : register(t6);
Texture2D<float> g_efb_depth : register(t7);
StructuredBuffer<float> g_prev_z : register(t8);
StructuredBuffer<uint4> g_prev_history : register(t9);
StructuredBuffer<uint4> g_history_in : register(t10);
StructuredBuffer<uint2> g_picture_in : register(t11);    // packed light, eye z
ByteAddressBuffer g_picture_sums : register(t12);        // sky r g b n, scene r g b n (x 1/255)
RWStructuredBuffer<float> g_z : register(u0);
RWStructuredBuffer<uint> g_normal : register(u1);
RWStructuredBuffer<uint4> g_raw : register(u2);
RWStructuredBuffer<uint4> g_history : register(u3);
RWStructuredBuffer<uint3> g_final : register(u4);

static const float kPi = 3.14159265f;

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

float luminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

uint pack_normal(float3 n)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 e = n.xy;
    if (n.z < 0.0f) {
        e = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
    }
    const uint2 u = uint2(round(saturate(e * 0.5f + 0.5f) * 65535.0f));
    return u.x | (u.y << 16u);
}

float3 unpack_normal(uint v)
{
    const float2 e = float2(float(v & 0xFFFFu), float(v >> 16u)) / 65535.0f * 2.0f - 1.0f;
    float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
    const float t = saturate(-n.z);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return normalize(n);
}

// RGB as halves, plus one more value.
uint2 pack_rgb_w(float3 c, float w)
{
    return uint2(f32tof16(c.r) | (f32tof16(c.g) << 16u), f32tof16(c.b) | (f32tof16(w) << 16u));
}

float4 unpack_rgb_w(uint2 p)
{
    return float4(f16tof32(p.x), f16tof32(p.x >> 16u), f16tof32(p.y), f16tof32(p.y >> 16u));
}

// A sample's light: sky and bounce (rgb), the sun's, the point lights' (rgb).
struct Light
{
    float3 sky;
    float sun;
    float3 points;
};

Light make_light(float3 sky, float sun, float3 points)
{
    Light l;
    l.sky = sky;
    l.sun = sun;
    l.points = points;
    return l;
}

Light scale_light(Light l, float w)
{
    return make_light(l.sky * w, l.sun * w, l.points * w);
}

Light add_light(Light a, Light b)
{
    return make_light(a.sky + b.sky, a.sun + b.sun, a.points + b.points);
}

// With the accumulated frames in the last half.
uint4 pack_light(Light l, float count)
{
    return uint4(pack_rgb_w(l.sky, l.sun), pack_rgb_w(l.points, count));
}

Light unpack_light(uint4 p, out float count)
{
    const float4 a = unpack_rgb_w(p.xy);
    const float4 b = unpack_rgb_w(p.zw);
    count = b.w;
    return make_light(a.rgb, a.w, b.rgb);
}

static const Light kOpenSky = {float3(1.0f, 1.0f, 1.0f), 0.0f, float3(0.0f, 0.0f, 0.0f)};

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

// ---------------------------------------------------------------------------------------------
// Picture capture (end of the scene): each sample's light and eye z for the next frame's rays
// ---------------------------------------------------------------------------------------------

cbuffer RtCapture : register(b0)
{
    float4 c_depth;          // as g_depth
    float4 c_viewport;
    uint2 c_target_size;
    uint2 c_sample_size;
    uint c_factor;
    float c_emissive;        // extra light of the brightest pixels
    uint c_divide;           // 1: this frame's lighting (c_final) is divided out
    float c_strength;
};
Texture2D<float4> c_color : register(t0);
Texture2D<float> c_depth_tex : register(t1);
StructuredBuffer<uint3> c_final : register(t2);
RWStructuredBuffer<uint2> c_picture : register(u0);
RWByteAddressBuffer c_sums : register(u1);

[numthreads(8, 8, 1)]
void CaptureCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= c_sample_size)) {
        return;
    }
    const uint index = id.y * c_sample_size.x + id.x;
    const int2 q = int2(min(id.xy * c_factor, c_target_size - 1u));
    const int2 last = int2(c_target_size) - 1;
    const int2 far_corner = min(q + int2(c_factor, c_factor) - 1, last);
    // The block's corners: a smoother light than one pixel.
    float3 color = (c_color.Load(int3(q, 0)).rgb + c_color.Load(int3(int2(far_corner.x, q.y), 0)).rgb +
                    c_color.Load(int3(int2(q.x, far_corner.y), 0)).rgb + c_color.Load(int3(far_corner, 0)).rgb) * 0.25f;
    const float depth = c_depth_tex.Load(int3(q, 0));
    const bool sky = far_plane(c_depth, depth) || !inside(c_viewport, float2(q) + 0.5f);
    float z = 0.0f;
    if (!sky) {
        z = depth_to_eye_z(c_depth, depth);
        if (c_divide != 0u) {
            // Without this frame's sky and bounce light (no feedback through it); the sun's
            // light stays, for the bounce it sends.
            color *= clamp(f16tof32(c_final[index].y >> 16u), 0.25f, 4.0f);
        }
        // Bright, saturated pixels (lamps, lava, glowing objects) send more light than their
        // stored colour.
        const float peak = max(color.r, max(color.g, color.b));
        const float glow = smoothstep(0.72f, 1.0f, peak);
        color *= 1.0f + c_emissive * glow * glow;
    }
    c_picture[index] = uint2(pack_r11g11b10(color), asuint(z));
    // Averages of the sky and of the scene (8-bit units, glow clipped).
    const uint3 units = uint3(round(saturate(color) * 255.0f));
    const uint base = sky ? 0u : 16u;
    c_sums.InterlockedAdd(base + 0u, units.r);
    c_sums.InterlockedAdd(base + 4u, units.g);
    c_sums.InterlockedAdd(base + 8u, units.b);
    c_sums.InterlockedAdd(base + 12u, 1u);
}

// ---------------------------------------------------------------------------------------------
// Lighting passes (one thread per sample)
// ---------------------------------------------------------------------------------------------

int2 sample_pixel(uint2 s)
{
    return int2(min(s * g_factor, g_target_size - 1u));
}

float2 sample_centre(uint2 s)
{
    return float2(sample_pixel(s)) + 0.5f;
}

float3 sample_position(uint2 s, float z)
{
    return eye_position(g_projection, g_viewport, sample_centre(s), z);
}

bool pixel_point(int2 q, out float3 p)
{
    p = float3(0.0f, 0.0f, 0.0f);
    if (any(q < 0) || any(q >= int2(g_target_size))) {
        return false;
    }
    const float depth = g_efb_depth.Load(int3(q, 0));
    if (far_plane(g_depth, depth)) {
        return false;
    }
    p = eye_position(g_projection, g_viewport, float2(q) + 0.5f, depth_to_eye_z(g_depth, depth));
    return true;
}

// Surface normal from the depth: per axis the neighbour nearer in depth, facing the camera.
float3 surface_normal(int2 q, float3 p)
{
    float3 a, b, c, d;
    const bool ha = pixel_point(q + int2(1, 0), a);
    const bool hb = pixel_point(q - int2(1, 0), b);
    const bool hc = pixel_point(q + int2(0, 1), c);
    const bool hd = pixel_point(q - int2(0, 1), d);
    if (!(ha || hb) || !(hc || hd)) {
        return -normalize(p);
    }
    const bool use_a = ha && (!hb || abs(a.z - p.z) <= abs(b.z - p.z));
    const bool use_c = hc && (!hd || abs(c.z - p.z) <= abs(d.z - p.z));
    const float3 dx = use_a ? a - p : p - b;
    const float3 dy = use_c ? c - p : p - d;
    float3 n = cross(dx, dy);
    const float len = length(n);
    if (!(len > 1e-12f)) {
        return -normalize(p);
    }
    n /= len;
    return dot(n, p) > 0.0f ? -n : n;
}

float2 sample_point(uint2 s, uint i)
{
    const float ign = frac(52.9829189f * frac(0.06711056f * float(s.x) + 0.00583715f * float(s.y)));
    const float n = float(i) + float(g_frame % 4096u) * float(g_rays);
    return frac(float2(ign, frac(ign * 1.61803399f)) + n * float2(0.7548776662f, 0.5698402910f));
}

// The sky's light: its average colour in the previous picture, as a tint of luminance 1.
float3 sky_tint()
{
    const uint4 sum = g_picture_sums.Load4(0u);
    if (sum.w == 0u) {
        return float3(1.0f, 1.0f, 1.0f);
    }
    const float3 sky = float3(sum.xyz) / (255.0f * float(sum.w));
    const float l = luminance(sky);
    return l > 1e-3f ? lerp(float3(1.0f, 1.0f, 1.0f), sky / l, g_sky_saturation) : float3(1.0f, 1.0f, 1.0f);
}

float point_falloff(float d2, float range)
{
    const float x = d2 / (range * range);
    const float window = saturate(1.0f - x * x);
    return window * window / (1.0f + 16.0f * x);
}

// The light a hit point sends back: the previous picture where it showed that point.
float3 hit_light(float3 hit, float3 fallback)
{
    if ((g_flags & 2u) == 0u) {
        return fallback;
    }
    const float3 previous = transform(g_picture, hit);
    if (previous.z >= -g_near) {
        return fallback;
    }
    const float2 s = (eye_to_pixel(g_projection, g_viewport, previous) - 0.5f) / float(g_factor) + 0.5f;
    if (any(s < 0.0f) || any(s >= float2(g_sample_size))) {
        return fallback;
    }
    const uint2 picture = g_picture_in[uint(s.y) * g_sample_size.x + uint(s.x)];
    const float z = asfloat(picture.y);
    if (z == 0.0f || abs(z - previous.z) > 0.03f * abs(previous.z) + 2.0f) {
        return fallback;
    }
    return unpack_r11g11b10(picture.x);
}

[numthreads(8, 8, 1)]
void TraceCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_sample_size)) {
        return;
    }
    const uint index = id.y * g_sample_size.x + id.x;
    const int2 q = sample_pixel(id.xy);
    float3 p;
    if (!pixel_point(q, p) || !inside(g_viewport, float2(q) + 0.5f)) {
        g_z[index] = 0.0f;
        g_normal[index] = 0u;
        g_raw[index] = pack_light(kOpenSky, 0.0f);
        return;
    }
    const float3 n = surface_normal(q, p);
    const float3 origin = p + n * (0.2f + abs(p.z) * 4e-4f);

    // The sun: one shadow ray inside its disc.
    float sun = 0.0f;
    const float facing_sun = dot(n, g_sun.xyz);
    if (facing_sun > 0.0f) {
        const float3 l = g_sun.xyz;
        const float lz = l.z >= 0.0f ? 1.0f : -1.0f;
        const float la = -1.0f / (lz + l.z);
        const float lb = l.x * l.y * la;
        const float3 l1 = float3(1.0f + lz * l.x * l.x * la, lz * lb, -lz * l.x);
        const float3 l2 = float3(lb, lz + l.y * l.y * la, -l.y);
        const float2 u = sample_point(id.xy, max(g_rays, 1u) + 3u);
        const float r = sqrt(u.x) * g_sun.w;
        const float phi = 2.0f * kPi * u.y;
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = normalize(l + l1 * (r * cos(phi)) + l2 * (r * sin(phi)));
        ray.TMin = 0.0f;
        ray.TMax = 1.0e6f;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                 RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
        query.Proceed();
        sun = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0f : facing_sun;
    }

    // The point lights: one picked in proportion to its unshadowed light, one shadow ray to it;
    // the estimate is that light's colour over its share.
    float3 points = float3(0.0f, 0.0f, 0.0f);
    if (g_point_count != 0u) {
        float weights[8];
        float total = 0.0f;
        [unroll] for (uint i = 0u; i < 8u; ++i) {
            weights[i] = 0.0f;
            if (i < g_point_count) {
                const float3 to = g_points[2u * i].xyz - p;
                const float d2 = dot(to, to);
                const float nl = dot(n, to) * rsqrt(max(d2, 1e-6f));
                if (nl > 0.0f) {
                    weights[i] = luminance(g_points[2u * i + 1u].rgb) * nl * point_falloff(d2, g_points[2u * i].w);
                }
            }
            total += weights[i];
        }
        if (total > 1e-6f) {
            float pick = sample_point(id.xy, max(g_rays, 1u) + 5u).x * total;
            uint chosen = 0u;
            [unroll] for (uint j = 0u; j < 8u; ++j) {
                if (weights[j] > 0.0f) {
                    chosen = j;
                    if (pick < weights[j]) {
                        break;
                    }
                    pick -= weights[j];
                }
            }
            const float3 to = g_points[2u * chosen].xyz - origin;
            const float span = length(to);
            RayDesc ray;
            ray.Origin = origin;
            ray.Direction = to / max(span, 1e-4f);
            ray.TMin = 0.0f;
            // The light sits inside its own emitter (a lamp, a glowing core): the shadow ray
            // ends at that surface.
            ray.TMax = max(span - g_point_size, 0.0f);
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                     RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
            query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
            query.Proceed();
            if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
                const float3 color = g_points[2u * chosen + 1u].rgb;
                points = color / max(luminance(color), 1e-4f) * total;
            }
        }
    }

    const float sign_z = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (sign_z + n.z);
    const float b = n.x * n.y * a;
    const float3 t1 = float3(1.0f + sign_z * n.x * n.x * a, sign_z * b, -sign_z * n.x);
    const float3 t2 = float3(b, sign_z + n.y * n.y * a, -n.y);

    const float3 tint = sky_tint();
    float3 fallback = float3(g_fallback, g_fallback, g_fallback);
    {
        const uint4 scene = g_picture_sums.Load4(16u);
        if (scene.w != 0u) {
            fallback = float3(scene.xyz) / (255.0f * float(scene.w)) * (g_fallback / 0.55f);
        }
    }
    float3 light = float3(0.0f, 0.0f, 0.0f);
    const uint rays = max(g_rays, 1u);
    for (uint i = 0u; i < rays; ++i) {
        const float2 u = sample_point(id.xy, i);
        const float r = sqrt(u.x);
        const float phi = 2.0f * kPi * u.y;
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = t1 * (r * cos(phi)) + t2 * (r * sin(phi)) + n * sqrt(max(0.0f, 1.0f - u.x));
        ray.TMin = 0.0f;
        ray.TMax = g_range;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
        query.Proceed();
        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            const float t = query.CommittedRayT();
            const float3 sent = hit_light(origin + ray.Direction * t, fallback);
            const float near = lerp(g_contact, 1.0f, saturate(t / g_contact_range));
            // Distant hits fade into the open sky (the game's lighting already holds them).
            const float distant = saturate((t - 0.6f * g_range) / (0.4f * g_range));
            light += lerp(sent * near, tint, distant);
        } else {
            light += tint;
        }
    }
    g_z[index] = p.z;
    g_normal[index] = pack_normal(n);
    g_raw[index] = pack_light(make_light(light / float(rays), sun, points), 0.0f);
}

// Accumulation: the sample's point in the previous frame, bilinear over the previous samples on
// its plane, weighted towards the history up to g_history_frames (the direct lights' shadows
// keep at most five frames: their casters move).
[numthreads(8, 8, 1)]
void TemporalCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_sample_size)) {
        return;
    }
    const uint index = id.y * g_sample_size.x + id.x;
    const float z = g_z[index];
    float unused;
    const Light raw = unpack_light(g_raw[index], unused);
    if (z == 0.0f) {
        g_history[index] = pack_light(kOpenSky, 0.0f);
        return;
    }
    Light result = raw;
    float frames = 1.0f;
    if ((g_flags & 1u) != 0u) {
        const float3 previous = transform(g_reproject, sample_position(id.xy, z));
        const float3 n = unpack_normal(g_normal[index]);
        const float3 normal = normalize(float3(dot(g_reproject[0].xyz, n), dot(g_reproject[1].xyz, n),
                                               dot(g_reproject[2].xyz, n)));
        if (previous.z < -g_near) {
            const float2 a = (eye_to_pixel(g_projection, g_viewport, previous) - 0.5f) / float(g_factor);
            const int2 base = int2(floor(a));
            const float2 w = a - float2(base);
            const float tolerance = 0.5f + abs(previous.z) * 0.01f;
            Light sum = make_light(float3(0.0f, 0.0f, 0.0f), 0.0f, float3(0.0f, 0.0f, 0.0f));
            float count_sum = 0.0f;
            float weight = 0.0f;
            [unroll] for (uint k = 0u; k < 4u; ++k) {
                const int2 o = int2(k & 1u, k >> 1u);
                const int2 s = base + o;
                if (any(s < 0) || any(s >= int2(g_sample_size))) {
                    continue;
                }
                const uint si = uint(s.y) * g_sample_size.x + uint(s.x);
                const float sz = g_prev_z[si];
                if (sz == 0.0f || abs(dot(normal, sample_position(uint2(s), sz) - previous)) > tolerance) {
                    continue;
                }
                const float bw = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y);
                float count;
                sum = add_light(sum, scale_light(unpack_light(g_prev_history[si], count), bw));
                count_sum += count * bw;
                weight += bw;
            }
            if (weight > 1e-3f) {
                const Light history = scale_light(sum, 1.0f / weight);
                frames = min(count_sum / weight + 1.0f, float(g_history_frames));
                const float direct = max(1.0f / frames, 0.2f);
                result = make_light(lerp(history.sky, raw.sky, 1.0f / frames), lerp(history.sun, raw.sun, direct),
                                    lerp(history.points, raw.points, direct));
            }
        }
    }
    g_history[index] = pack_light(result, frames);
}

// Edge-aware 5x5 filter at `step` samples (plane distance and normal).
Light filtered(uint2 id, int step, bool from_history)
{
    const uint index = id.y * g_sample_size.x + id.x;
    const float z = g_z[index];
    const float3 p = sample_position(id, z);
    const float3 n = unpack_normal(g_normal[index]);
    const float inv_tolerance = 1.0f / (0.25f + abs(z) * 0.004f * float(step));
    Light sum = make_light(float3(0.0f, 0.0f, 0.0f), 0.0f, float3(0.0f, 0.0f, 0.0f));
    float weight = 0.0f;
    float count;
    [unroll] for (int dy = -2; dy <= 2; ++dy) {
        [unroll] for (int dx = -2; dx <= 2; ++dx) {
            const int2 s = int2(id) + int2(dx, dy) * step;
            if (any(s < 0) || any(s >= int2(g_sample_size))) {
                continue;
            }
            const uint si = uint(s.y) * g_sample_size.x + uint(s.x);
            const float sz = g_z[si];
            if (sz == 0.0f) {
                continue;
            }
            const float plane = abs(dot(n, sample_position(uint2(s), sz) - p)) * inv_tolerance;
            const float facing = saturate(dot(n, unpack_normal(g_normal[si])));
            const float facing2 = facing * facing;
            const float w = exp(-0.18f * float(dx * dx + dy * dy) - plane * plane) * (facing2 * facing2) *
                            (facing2 * facing2);
            const Light value = unpack_light(from_history ? g_history_in[si] : g_raw[si], count);
            sum = add_light(sum, scale_light(value, w));
            weight += w;
        }
    }
    if (weight > 1e-4f) {
        return scale_light(sum, 1.0f / weight);
    }
    return unpack_light(from_history ? g_history_in[index] : g_raw[index], count);
}

[numthreads(8, 8, 1)]
void SpatialACS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_sample_size)) {
        return;
    }
    const uint index = id.y * g_sample_size.x + id.x;
    if (g_z[index] == 0.0f) {
        g_raw[index] = pack_light(kOpenSky, 0.0f);
        return;
    }
    g_raw[index] = pack_light(filtered(id.xy, 1, true), 0.0f);
}

[numthreads(8, 8, 1)]
void SpatialBCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_sample_size)) {
        return;
    }
    const uint index = id.y * g_sample_size.x + id.x;
    const float z = g_z[index];
    if (z == 0.0f) {
        g_final[index] = uint3(f32tof16(1.0f) | (f32tof16(1.0f) << 16u), f32tof16(1.0f) | (f32tof16(1.0f) << 16u), 0u);
        return;
    }
    const Light light = filtered(id.xy, 2, false);
    const float3 direct = light.sun * g_sun_light.rgb + light.points * g_point_light;
    float3 m = lerp(float3(1.0f, 1.0f, 1.0f), g_sun_light.w * light.sky + direct, g_strength);
    if ((g_flags & 4u) != 0u) {
        m = light.sun.xxx;     // GALAXY_RT_VIEW=2: the sun's light alone
    } else if ((g_flags & 8u) != 0u) {
        m = light.sky;         // GALAXY_RT_VIEW=3: the sky and bounce light alone
    } else if ((g_flags & 16u) != 0u) {
        m = light.points;      // GALAXY_RT_VIEW=4: the point lights alone
    }
    // The picture capture divides out the sky and bounce part (no feedback through it).
    const float3 without = lerp(float3(1.0f, 1.0f, 1.0f), g_sun_light.www + direct, g_strength);
    const float keep = luminance(without) / max(luminance(m), 1e-3f);
    g_final[index] = uint3(f32tof16(m.r) | (f32tof16(m.g) << 16u), f32tof16(m.b) | (f32tof16(keep) << 16u), asuint(z));
}

// ---------------------------------------------------------------------------------------------
// Composite: the EFB colour times the lighting (dual-source blend: dst * src0 + dst * src1, so
// the multiplier reaches 2 on an 8-bit target)
// ---------------------------------------------------------------------------------------------

cbuffer RtComposite : register(b0)
{
    float4 m_projection;
    float4 m_depth;
    float4 m_viewport;
    uint2 m_sample_size;
    uint m_factor;
    uint m_flags;   // bit 0: move the point to the traced frame's camera (m_reproject[m_slot]);
                    // bit 1: show the lighting alone
    uint m_slot;
    float m_near;
    uint2 m_pad;
};
StructuredBuffer<uint3> m_light : register(t0);
Texture2D<float> m_efb_depth : register(t1);
StructuredBuffer<float4> m_reproject : register(t2);

struct CompositeOut
{
    float4 add : SV_Target0;
    float4 keep : SV_Target1;
};

float4 CompositeVS(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1u) & 2u, id & 2u);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

CompositeOut CompositePS(float4 position : SV_Position)
{
    const float depth = m_efb_depth.Load(int3(int2(position.xy), 0));
    const float z = depth_to_eye_z(m_depth, depth);
    const float slope = min(abs(ddx(z)) + abs(ddy(z)), abs(z) * 0.05f);
    CompositeOut result;
    result.add = float4(0.0f, 0.0f, 0.0f, 0.0f);
    result.keep = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (far_plane(m_depth, depth) || !inside(m_viewport, position.xy)) {
        if ((m_flags & 2u) != 0u) {
            result.add = float4(0.5f, 0.5f, 0.5f, 1.0f);
            return result;
        }
        discard;
    }
    float3 p = eye_position(m_projection, m_viewport, position.xy, z);
    if ((m_flags & 1u) != 0u) {
        const uint row = m_slot * 3u;
        const float4 rows[3] = {m_reproject[row], m_reproject[row + 1u], m_reproject[row + 2u]};
        p = transform(rows, p);
        if (p.z >= -m_near) {
            discard;
        }
    }
    const float2 a = (eye_to_pixel(m_projection, m_viewport, p) - 0.5f) / float(m_factor);
    const int2 base = int2(floor(a));
    const float2 w = a - float2(base);
    const float tolerance = slope * float(m_factor) * 1.5f + abs(p.z) * 0.01f + 0.25f;
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float weight = 0.0f;
    float3 nearest = float3(1.0f, 1.0f, 1.0f);
    float nearest_dz = abs(p.z) * 0.1f;
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 o = int2(k & 1u, k >> 1u);
        const int2 s = clamp(base + o, int2(0, 0), int2(m_sample_size) - 1);
        const uint3 sample = m_light[uint(s.y) * m_sample_size.x + uint(s.x)];
        const float sz = asfloat(sample.z);
        if (sz == 0.0f) {
            continue;
        }
        const float3 m = float3(f16tof32(sample.x), f16tof32(sample.x >> 16u), f16tof32(sample.y));
        const float dz = abs(sz - p.z);
        if (dz < nearest_dz) {
            nearest_dz = dz;
            nearest = m;
        }
        const float d = dz / tolerance;
        const float bw = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y) * exp(-d * d);
        sum += m * bw;
        weight += bw;
    }
    const float3 m = clamp(weight > 1e-3f ? sum / weight : nearest, 0.0f, 2.0f);
    if ((m_flags & 2u) != 0u) {
        result.add = float4(m * 0.5f, 1.0f);
        return result;
    }
    result.add = float4(max(m - 1.0f, 0.0f), 0.0f);
    result.keep = float4(min(m, 1.0f), 1.0f);
    return result;
}
