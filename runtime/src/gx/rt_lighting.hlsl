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
                                // light alone; bit 5: glow view; bit 6: the distance to
                                // what shadows the sun
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
    float g_sun_reach;          // length of a sun ray (eye units)
    float4 g_points[16];        // per light: eye position (xyz), reach (w); colour (rgb),
                                // 1 / its half-light distance squared (w)
    float4 g_to_eye[3];         // world to eye (the glow lights' positions)
    float4 g_ambient;           // the sky and bounce light under a black sky (relative), the sky's
                                // luminance that gives it in full, a glowing surface's extra light,
                                // the distance beyond which surfaces are the sky (backdrops)
    float g_point_saturation;   // the colours above: 0 white .. 1 the game's
    uint g_draw_count;          // entries of g_draws
    uint2 g_pad;
};

RaytracingAccelerationStructure g_scene : register(t6);
Texture2D<float> g_efb_depth : register(t7);
StructuredBuffer<float> g_prev_z : register(t8);
StructuredBuffer<uint4> g_prev_history : register(t9);
StructuredBuffer<uint4> g_history_in : register(t10);
StructuredBuffer<uint2> g_picture_in : register(t11);    // packed light, eye z
ByteAddressBuffer g_picture_sums : register(t12);        // sky r g b n, scene r g b n (x 1/255),
                                                         // the sky's luminance over time, known
ByteAddressBuffer g_positions : register(t13);           // per triangle: its eye-space vertices
ByteAddressBuffer g_surfaces : register(t14);            // per triangle: vertex normals, draw flags
ByteAddressBuffer g_emitters : register(t15);            // glow lights: count, total power; per light
                                                         // world position, radius; power, cumulative
StructuredBuffer<uint> g_glow_base : register(t16);      // the previous picture before its glows
StructuredBuffer<uint> g_glow_added : register(t17);     // what its glows added
ByteAddressBuffer g_draws : register(t18);               // per draw: ambient, GX light 0 colour (RGBA8)
RWStructuredBuffer<float> g_z : register(u0);
RWStructuredBuffer<uint> g_normal : register(u1);
RWStructuredBuffer<uint4> g_raw : register(u2);
RWStructuredBuffer<uint4> g_history : register(u3);
RWStructuredBuffer<uint3> g_final : register(u4);
RWStructuredBuffer<uint2> g_primary : register(u5);      // triangle + 1 (0: none), barycentrics

static const float kPi = 3.14159265f;
static const uint kDrawLit = 8u;    // RtDraw.flags: GX-lit colour
static const uint kDrawSunKey = 16u;    // RtDraw.flags: its GX light 0 lies along the traced sun
static const uint kDrawPointLit = 32u;  // RtDraw.flags: lit by GX light 4 (the game's point light)

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

// ray_tracing.hlsl pack_vertex_normal: octahedral, 1-65535 per axis.
float3 unpack_vertex_normal(uint v)
{
    const float2 e = (float2(float(v & 0xFFFFu), float(v >> 16u)) - 1.0f) / 65534.0f * 2.0f - 1.0f;
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
    uint c_divide;           // bit 0: this frame's lighting (c_final) is divided out; bit 1: what
                             // the translucent phases added since c_glow_base goes to c_glow
    float c_strength;
    float c_split;           // GALAXY_RT_SPLIT: pixels left of it kept the game's own colour
    float c_surface_gain;    // a glowing surface's light per unit of colour and eye-space area
    float c_sky_distance;    // farther surfaces are the sky (backdrops)
    float c_exposure_day;    // the mean lighting the exposure holds under a bright sky
    float4 c_projection;
    float4 c_to_world[3];    // eye to world
    float c_exposure_night;  // ... under a black sky
    float c_daylight;        // the sky's luminance that counts as bright
    uint c_exposure;         // 1: the exposure follows the lighting
    uint c_pad;
};
Texture2D<float4> c_color : register(t0);
Texture2D<float> c_depth_tex : register(t1);
StructuredBuffer<uint3> c_final : register(t2);
StructuredBuffer<uint> c_glow_base : register(t3);
RWStructuredBuffer<uint2> c_picture : register(u0);
RWByteAddressBuffer c_sums : register(u1);
RWStructuredBuffer<uint> c_glow : register(u2);
RWByteAddressBuffer c_found : register(u3);              // this frame's lights (see e_found)

// A sample's colour: its block's corners (a smoother light than one pixel).
float3 block_color(uint2 id)
{
    const int2 q = int2(min(id * c_factor, c_target_size - 1u));
    const int2 far_corner = min(q + int2(c_factor, c_factor) - 1, int2(c_target_size) - 1);
    return (c_color.Load(int3(q, 0)).rgb + c_color.Load(int3(int2(far_corner.x, q.y), 0)).rgb +
            c_color.Load(int3(int2(q.x, far_corner.y), 0)).rgb + c_color.Load(int3(far_corner, 0)).rgb) * 0.25f;
}

groupshared float4 s_surface_a[64];
groupshared float4 s_surface_b[64];

// After the capture: glowing surfaces (bright before the translucent phases and under their own
// glow after them: cores, lamp heads, Lumas) become lights at their own depth, one per 8x8
// samples near the depth of the brightest.
[numthreads(8, 8, 1)]
void SurfaceEmitCS(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    float3 emitted = float3(0.0f, 0.0f, 0.0f);
    float z = 0.0f;
    float3 p = float3(0.0f, 0.0f, 0.0f);
    float area = 0.0f;
    if (all(id.xy < c_sample_size)) {
        const uint index = id.y * c_sample_size.x + id.x;
        z = asfloat(c_picture[index].y);
        if (z != 0.0f) {
            const float3 base = unpack_r11g11b10(c_glow_base[index]);
            const float added = luminance(unpack_r11g11b10(c_glow[index]));
            const float glowing = smoothstep(0.1f, 0.4f, added) *
                                  smoothstep(0.55f, 0.85f, max(base.r, max(base.g, base.b)));
            const float2 pixel = float2(id.xy * c_factor) + 0.5f;
            p = eye_position(c_projection, c_viewport, pixel, z);
            const float2 size = 2.0f * abs(z) * float(c_factor) /
                                (abs(float2(c_projection.x, c_projection.z)) * c_viewport.zw);
            area = size.x * size.y;
            emitted = base * glowing;
        }
    }
    const float l = luminance(emitted);
    s_surface_a[gi] = float4(l, z, 0.0f, 0.0f);
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 32u; s > 0u; s >>= 1u) {
        if (gi < s && s_surface_a[gi + s].x > s_surface_a[gi].x) {
            s_surface_a[gi] = s_surface_a[gi + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    const float2 ref = s_surface_a[0].xy;
    GroupMemoryBarrierWithGroupSync();
    float4 a = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 b = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (l > 0.0f && abs(z - ref.y) < 0.15f * abs(ref.y) + 20.0f) {
        const float w = l * area;
        a = float4(emitted * area, w);
        b = float4(p * w, dot(p, p) * w);
    }
    s_surface_a[gi] = a;
    s_surface_b[gi] = b;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint t = 32u; t > 0u; t >>= 1u) {
        if (gi < t) {
            s_surface_a[gi] += s_surface_a[gi + t];
            s_surface_b[gi] += s_surface_b[gi + t];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0u && s_surface_a[0].w > 0.0f) {
        const float w = s_surface_a[0].w;
        const float3 centre = s_surface_b[0].xyz / w;
        const float spread = sqrt(max(s_surface_b[0].w / w - dot(centre, centre), 0.0f));
        const float3 power = s_surface_a[0].rgb * c_surface_gain;
        if (luminance(power) >= 4.0f) {
            uint at;
            c_found.InterlockedAdd(0u, 1u, at);
            if (at < 1024u) {
                c_found.Store4(16u + at * 32u, asuint(float4(transform(c_to_world, centre), max(spread, 1.0f))));
                c_found.Store4(16u + at * 32u + 16u, asuint(float4(power, 0.0f)));
            }
        }
    }
}

// After the capture: the sky's luminance over time (where the frame shows enough sky).
[numthreads(1, 1, 1)]
void SkyStateCS()
{
    const uint4 sky = c_sums.Load4(0u);
    if (sky.w * 100u > c_sample_size.x * c_sample_size.y) {
        const float l = luminance(float3(sky.xyz) / (255.0f * float(sky.w)));
        const float state = c_sums.Load(36u) != 0u ? lerp(asfloat(c_sums.Load(32u)), l, 0.05f) : l;
        c_sums.Store2(32u, uint2(asuint(state), 1u));
    }
    // The exposure: the lighting's mean over the picture moves towards its target (darker under a
    // black sky), a tenth of the way per frame.
    const uint lit_count = c_sums.Load(44u);
    if (c_exposure == 0u) {
        c_sums.Store2(48u, uint2(asuint(1.0f), 0u));
    } else if (lit_count * 50u > c_sample_size.x * c_sample_size.y) {
        const float mean = float(c_sums.Load(40u)) / (256.0f * float(lit_count));
        const float day = c_sums.Load(36u) != 0u ? saturate(asfloat(c_sums.Load(32u)) / c_daylight) : 1.0f;
        const float target = lerp(c_exposure_night, c_exposure_day, day);
        const float exposure = c_sums.Load(52u) != 0u ? asfloat(c_sums.Load(48u)) : 1.0f;
        const float next = clamp(exposure * lerp(1.0f, target / max(mean, 0.05f), 0.1f), 0.8f, 1.5f);
        c_sums.Store2(48u, uint2(asuint(next), 1u));
    }
}

// After the composite: the picture before the translucent phases.
[numthreads(8, 8, 1)]
void GlowBaseCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= c_sample_size)) {
        return;
    }
    c_glow[id.y * c_sample_size.x + id.x] = pack_r11g11b10(block_color(id.xy));
}

[numthreads(8, 8, 1)]
void CaptureCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= c_sample_size)) {
        return;
    }
    const uint index = id.y * c_sample_size.x + id.x;
    const int2 q = int2(min(id.xy * c_factor, c_target_size - 1u));
    float3 color = block_color(id.xy);
    if ((c_divide & 2u) != 0u) {
        c_glow[index] = pack_r11g11b10(max(color - unpack_r11g11b10(c_glow_base[index]), 0.0f));
    }
    const float depth = c_depth_tex.Load(int3(q, 0));
    const bool sky = far_plane(c_depth, depth) || !inside(c_viewport, float2(q) + 0.5f) ||
                     -depth_to_eye_z(c_depth, depth) > c_sky_distance;
    float z = 0.0f;
    if (!sky) {
        z = depth_to_eye_z(c_depth, depth);
        if ((c_divide & 1u) != 0u) {
            // Without this frame's sky and bounce light (no feedback through it); the sun's
            // light stays, for the bounce it sends.
            const uint3 f = c_final[index];
            float keep = f16tof32(f.y >> 16u);
            const float lit = luminance(float3(f16tof32(f.x), f16tof32(f.x >> 16u), f16tof32(f.y)));
            if (float(q.x) < c_split) {
                keep *= lit;
            } else {
                c_sums.InterlockedAdd(40u, uint(clamp(lit, 0.0f, 4.0f) * 256.0f));
                c_sums.InterlockedAdd(44u, 1u);
            }
            color *= clamp(keep, 0.25f, 4.0f);
        }
        // Bright, saturated pixels (lamps, lava, glowing objects) send more light than their
        // stored colour.
        const float peak = max(color.r, max(color.g, color.b));
        const float glow = smoothstep(0.72f, 1.0f, peak);
        color *= 1.0f + c_emissive * glow * glow;
    }
    c_picture[index] = uint2(pack_r11g11b10(color), asuint(z));
    // Averages of the sky and of the scene (8-bit units, glow clipped); the EFB outside the
    // viewport (rows below the picture) is neither.
    if (!inside(c_viewport, float2(q) + 0.5f)) {
        return;
    }
    const uint3 units = uint3(round(saturate(color) * 255.0f));
    const uint base = sky ? 0u : 16u;
    c_sums.InterlockedAdd(base + 0u, units.r);
    c_sums.InterlockedAdd(base + 4u, units.g);
    c_sums.InterlockedAdd(base + 8u, units.b);
    c_sums.InterlockedAdd(base + 12u, 1u);
}

// ---------------------------------------------------------------------------------------------
// Glow lights: the game's additive translucent draws (lamp and core halos, Lumas, sparks) are its
// light sources. Each glow draw's eye-space triangles (the geometry pass) and what the
// translucent phases added to the picture where they lie give a light: the draw's centre
// weighted by that light, its spread, its power (light added x area x gain). The lights are
// remembered in world space while out of view or hidden, and fade where their place is in view
// and no longer glows.
// ---------------------------------------------------------------------------------------------

cbuffer RtEmit : register(b0)
{
    float4 e_projection;
    float4 e_depth;
    float4 e_viewport;
    uint2 e_target_size;
    uint2 e_sample_size;
    float4 e_to_world[3];       // eye to world
    float4 e_to_eye[3];         // world to eye
    float e_gain;
    float e_near;
    uint e_reset;               // 1: forget the remembered lights
    uint e_factor;              // EFB pixels per sample side
};
Texture2D<float4> e_color : register(t0);
Texture2D<float> e_depth_tex : register(t1);
ByteAddressBuffer e_draws : register(t2);             // the glow draws (RtDraw)
ByteAddressBuffer e_triangles : register(t3);         // their eye-space triangles (36 bytes)
StructuredBuffer<uint> e_added : register(t4);        // per sample: light the glows added
RWByteAddressBuffer e_found : register(u0);           // count; per glow light position, radius; power
RWStructuredBuffer<float4> e_lights : register(u1);   // per light: position, radius; power, frames unseen
RWByteAddressBuffer e_list : register(u2);            // count, total; per light position, radius;
                                                      // power, cumulative power

static const uint kEmitLights = 256u;
static const uint kEmitFound = 1024u;

groupshared float4 s_glow_a[64];
groupshared float4 s_glow_b[64];
groupshared float s_glow_area[64];

// One group per glow draw.
[numthreads(64, 1, 1)]
void GlowEmitCS(uint3 group : SV_GroupID, uint gi : SV_GroupIndex)
{
    const uint2 range = e_draws.Load2(group.x * 32u + 16u);   // first triangle, count
    float4 a = float4(0.0f, 0.0f, 0.0f, 0.0f);   // power rgb, weight (luminance x area)
    float4 b = float4(0.0f, 0.0f, 0.0f, 0.0f);   // weighted centre, weighted |centre|^2
    float area_sum = 0.0f;
    for (uint t = gi; t < range.y; t += 64u) {
        const uint at = (range.x + t) * 36u;
        const float3 v0 = asfloat(e_triangles.Load3(at));
        const float3 v1 = asfloat(e_triangles.Load3(at + 12u));
        const float3 v2 = asfloat(e_triangles.Load3(at + 24u));
        const float3 c = (v0 + v1 + v2) / 3.0f;
        const float area = 0.5f * length(cross(v1 - v0, v2 - v0));
        if (c.z < -e_near && area > 0.0f && all(abs(c) < 1.0e6f)) {
            const float2 q = eye_to_pixel(e_projection, e_viewport, c);
            const int2 s = int2(floor(q / float(e_factor)));
            if (all(s >= 0) && all(s < int2(e_sample_size))) {
                const float3 added = unpack_r11g11b10(e_added[uint(s.y) * e_sample_size.x + uint(s.x)]);
                const float w = luminance(added) * area;
                if (luminance(added) > 0.02f) {
                    a += float4(added * area, w);
                    b += float4(c * w, dot(c, c) * w);
                    area_sum += area;
                }
            }
        }
    }
    s_glow_a[gi] = a;
    s_glow_b[gi] = b;
    s_glow_area[gi] = area_sum;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 32u; s > 0u; s >>= 1u) {
        if (gi < s) {
            s_glow_a[gi] += s_glow_a[gi + s];
            s_glow_b[gi] += s_glow_b[gi + s];
            s_glow_area[gi] += s_glow_area[gi + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0u && s_glow_a[0].w > 0.0f) {
        const float w = s_glow_a[0].w;
        const float3 centre = s_glow_b[0].xyz / w;
        const float spread = sqrt(max(s_glow_b[0].w / w - dot(centre, centre), 0.0f));
        const float radius = max(spread, 0.5f * sqrt(s_glow_area[0]));
        // A glow's texture covers about a quarter of its quad.
        const float3 power = s_glow_a[0].rgb * (0.25f * e_gain);
        if (luminance(power) >= 4.0f) {
            uint index;
            e_found.InterlockedAdd(0u, 1u, index);
            if (index < kEmitFound) {
                const uint at = 16u + index * 32u;
                e_found.Store4(at, asuint(float4(transform(e_to_world, centre), radius)));
                e_found.Store4(at + 16u, asuint(float4(power, 0.0f)));
            }
        }
    }
}

groupshared uint s_emit_best[kEmitFound];
groupshared uint s_emit_free[kEmitLights];
groupshared uint s_emit_free_count;
groupshared uint s_emit_taken;
groupshared float2 s_emit_scan[kEmitLights];

// One group: the remembered lights take this frame's glow lights (each the nearest light within
// reach of both), the others take free slots, unseen lights fade, then the list for sampling.
[numthreads(256, 1, 1)]
void EmitMergeCS(uint gi : SV_GroupIndex)
{
    const uint found = min(e_found.Load(0u), kEmitFound);
    float4 la = e_reset != 0u ? float4(0.0f, 0.0f, 0.0f, 0.0f) : e_lights[gi * 2u];
    float4 lb = e_reset != 0u ? float4(0.0f, 0.0f, 0.0f, 0.0f) : e_lights[gi * 2u + 1u];
    const bool active = luminance(lb.rgb) > 0.0f;
    for (uint i = gi; i < found; i += kEmitLights) {
        s_emit_best[i] = 0xFFFFFFFFu;
    }
    if (gi == 0u) {
        s_emit_free_count = 0u;
        s_emit_taken = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    if (active) {
        [loop] for (uint t = 0u; t < found; ++t) {
            const float4 f = asfloat(e_found.Load4(16u + t * 32u));
            const float d = distance(f.xyz, la.xyz);
            if (d < 2.0f * (la.w + f.w)) {
                InterlockedMin(s_emit_best[t], (asuint(d) & 0xFFFFFF00u) | gi);
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (active) {
        float3 power = float3(0.0f, 0.0f, 0.0f);
        float4 centre = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float radius = 0.0f;
        [loop] for (uint t = 0u; t < found; ++t) {
            if (s_emit_best[t] != 0xFFFFFFFFu && (s_emit_best[t] & 0xFFu) == gi) {
                const float4 fa = asfloat(e_found.Load4(16u + t * 32u));
                const float3 fp = asfloat(e_found.Load3(16u + t * 32u + 16u));
                const float w = luminance(fp) + 1e-6f;
                power += fp;
                centre += float4(fa.xyz * w, w);
                radius = max(radius, fa.w);
            }
        }
        if (centre.w > 0.0f) {
            // Seen: brighter at once, dimmer slowly (a partly hidden glow shows less of itself).
            lb.rgb = lerp(lb.rgb, power, luminance(power) > luminance(lb.rgb) ? 0.25f : 0.08f);
            la = float4(lerp(la.xyz, centre.xyz / centre.w, 0.5f), lerp(la.w, radius, 0.5f));
            lb.w = 0.0f;
        } else {
            // Its place in view with nothing in front: the glow is gone. Out of view or hidden:
            // kept, fading after 30 s.
            const float3 e = transform(e_to_eye, la.xyz);
            bool gone = false;
            if (e.z < -e_near) {
                const float2 q = eye_to_pixel(e_projection, e_viewport, e);
                if (inside(e_viewport, q) && all(q < float2(e_target_size))) {
                    const float depth = e_depth_tex.Load(int3(int2(q), 0));
                    const float scene_z = far_plane(e_depth, depth) ? -1.0e30f : depth_to_eye_z(e_depth, depth);
                    gone = scene_z < e.z + la.w + 0.02f * abs(e.z);
                }
            }
            lb.w += 1.0f;
            lb.rgb *= gone ? 0.5f : lb.w > 1800.0f ? 0.97f : 1.0f;
        }
        if (luminance(lb.rgb) < 1.0f) {
            la = float4(0.0f, 0.0f, 0.0f, 0.0f);
            lb = float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }
    e_lights[gi * 2u] = la;
    e_lights[gi * 2u + 1u] = lb;
    if (luminance(lb.rgb) <= 0.0f) {
        uint slot;
        InterlockedAdd(s_emit_free_count, 1u, slot);
        s_emit_free[slot] = gi;
    }
    AllMemoryBarrierWithGroupSync();
    // Glow lights no remembered light took: new lights in free slots.
    for (uint n = gi; n < found; n += kEmitLights) {
        if (s_emit_best[n] == 0xFFFFFFFFu) {
            uint k;
            InterlockedAdd(s_emit_taken, 1u, k);
            if (k < s_emit_free_count) {
                const uint slot = s_emit_free[k];
                e_lights[slot * 2u] = asfloat(e_found.Load4(16u + n * 32u));
                e_lights[slot * 2u + 1u] = float4(asfloat(e_found.Load3(16u + n * 32u + 16u)), 0.0f);
            }
        }
    }
    AllMemoryBarrierWithGroupSync();
    // The list: the lights in slot order with their cumulative power.
    const float4 fa = e_lights[gi * 2u];
    const float4 fb = e_lights[gi * 2u + 1u];
    const float lum = luminance(fb.rgb);
    s_emit_scan[gi] = float2(lum, lum > 0.0f ? 1.0f : 0.0f);
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint step = 1u; step < kEmitLights; step <<= 1u) {
        const float2 add = gi >= step ? s_emit_scan[gi - step] : float2(0.0f, 0.0f);
        GroupMemoryBarrierWithGroupSync();
        s_emit_scan[gi] += add;
        GroupMemoryBarrierWithGroupSync();
    }
    if (lum > 0.0f) {
        const uint at = 16u + (uint(s_emit_scan[gi].y) - 1u) * 32u;
        e_list.Store4(at, asuint(fa));
        e_list.Store4(at + 16u, asuint(float4(fb.rgb, s_emit_scan[gi].x)));
    }
    if (gi == kEmitLights - 1u) {
        e_list.Store2(0u, uint2(uint(s_emit_scan[gi].y), asuint(s_emit_scan[gi].x)));
    }
}

// ---------------------------------------------------------------------------------------------
// Glow bloom: what the glows added (the light of cores, lamps, Lumas) above a threshold, at a
// quarter of the samples, blurred across and down (sigma 10 texels), added to the picture at the
// scene's end: the corona an over-exposed emitter shows.
// ---------------------------------------------------------------------------------------------

cbuffer RtBloom : register(b0)
{
    uint2 b_size;               // bloom texels (2x2 samples each)
    uint2 b_sample_size;
    float b_threshold;          // glow luminance that starts to bloom
    float b_strength;
    uint b_axis;                // blur: 0 across, 1 down
    uint b_pad;
};
StructuredBuffer<uint> b_added : register(t0);
StructuredBuffer<uint> b_in : register(t1);
RWStructuredBuffer<uint> b_out : register(u0);

[numthreads(8, 8, 1)]
void BloomDownCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= b_size)) {
        return;
    }
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const uint2 s = min(id.xy * 2u + uint2(k & 1u, k >> 1u), b_sample_size - 1u);
        sum += unpack_r11g11b10(b_added[s.y * b_sample_size.x + s.x]);
    }
    float3 c = sum * 0.25f;
    const float l = luminance(c);
    c *= max(l - b_threshold, 0.0f) / max(l, 1e-4f);
    b_out[id.y * b_size.x + id.x] = pack_r11g11b10(c);
}

[numthreads(8, 8, 1)]
void BloomBlurCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= b_size)) {
        return;
    }
    const int2 step = b_axis == 0u ? int2(1, 0) : int2(0, 1);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float weight = 0.0f;
    [loop] for (int i = -24; i <= 24; ++i) {
        const int2 s = clamp(int2(id.xy) + step * i, int2(0, 0), int2(b_size) - 1);
        const float w = exp(-float(i * i) / 200.0f);
        sum += unpack_r11g11b10(b_in[uint(s.y) * b_size.x + uint(s.x)]) * w;
        weight += w;
    }
    b_out[id.y * b_size.x + id.x] = pack_r11g11b10(sum / weight);
}

cbuffer RtBloomComposite : register(b0)
{
    float2 o_scale;             // bloom texels per EFB pixel
    uint2 o_size;
    float o_strength;
    float o_split;              // GALAXY_RT_SPLIT: left of it the game's own picture
    uint2 o_pad;
};
StructuredBuffer<uint> o_bloom : register(t0);

float4 BloomPS(float4 position : SV_Position) : SV_Target0
{
    if (position.x < o_split) {
        discard;
    }
    const float2 t = position.xy * o_scale - 0.5f;
    const int2 b = int2(floor(t));
    const float2 f = t - float2(b);
    float3 c = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 o = int2(k & 1u, k >> 1u);
        const int2 s = clamp(b + o, int2(0, 0), int2(o_size) - 1);
        const float w = (o.x != 0 ? f.x : 1.0f - f.x) * (o.y != 0 ? f.y : 1.0f - f.y);
        c += unpack_r11g11b10(o_bloom[uint(s.y) * o_size.x + uint(s.x)]) * w;
    }
    return float4(c * o_strength, 0.0f);
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

// The game's distance attenuation (1 / (1 + d^2 / half^2)), windowed to 0 at the light's reach.
float point_falloff(float d2, uint i)
{
    const float x = d2 / (g_points[2u * i].w * g_points[2u * i].w);
    const float window = saturate(1.0f - x * x);
    return window * window / (1.0f + d2 * g_points[2u * i + 1u].w);
}

// The point lights' light on a surface, unshadowed; `game`: in the game's own colours (the
// stored ones are desaturated by g_point_saturation, which keeps their luminance).
float3 points_unshadowed(float3 p, float3 n, bool game)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [loop] for (uint i = 0u; i < g_point_count; ++i) {
        const float3 to = g_points[2u * i].xyz - p;
        const float d2 = dot(to, to);
        const float nl = dot(n, to) * rsqrt(max(d2, 1e-6f));
        if (nl > 0.0f) {
            float3 color = g_points[2u * i + 1u].rgb;
            if (game && g_point_saturation > 1e-3f) {
                const float l = luminance(color);
                color = max(l + (color - l) / g_point_saturation, 0.0f);
            }
            sum += color * (nl * point_falloff(d2, i));
        }
    }
    return sum;
}

// An RGBA8 colour word's RGB (red in the top byte).
float3 rgb8(uint c)
{
    return float3(float(c >> 24u), float((c >> 16u) & 0xFFu), float((c >> 8u) & 0xFFu)) / 255.0f;
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
    // Backdrops (sky domes and distant scenery far beyond the stage) are the sky: no lighting.
    if (!pixel_point(q, p) || !inside(g_viewport, float2(q) + 0.5f) || -p.z > g_ambient.w) {
        g_z[index] = 0.0f;
        g_normal[index] = 0u;
        g_primary[index] = uint2(0u, 0u);
        g_raw[index] = pack_light(kOpenSky, 0.0f);
        return;
    }
    // The surface: the triangle the camera ray meets at the raster depth gives the face normal,
    // the shading normal from its vertex normals, and a ray origin lifted off the flat face
    // where those normals bend away from it (the shadow terminator, Hanika 2021). Geometry the
    // scene leaves out (blended and indirect draws) takes the normal from the depth.
    float3 n = surface_normal(q, p);
    float3 ng = n;
    float3 lifted = p;
    uint2 primary = uint2(0u, 0u);
    {
        const float span = length(p);
        RayDesc ray;
        ray.Origin = float3(0.0f, 0.0f, 0.0f);
        ray.Direction = p / span;
        ray.TMin = 0.0f;
        ray.TMax = span * 1.02f + 2.0f;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
        query.Proceed();
        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT &&
            abs(query.CommittedRayT() - span) < span * 0.01f + 2.0f) {
            const uint tri = query.CommittedPrimitiveIndex();
            const float2 b = query.CommittedTriangleBarycentrics();
            const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
            const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
            const float3 v1 = asfloat(g_positions.Load3(tri * 36u + 12u));
            const float3 v2 = asfloat(g_positions.Load3(tri * 36u + 24u));
            const float3 face = cross(v1 - v0, v2 - v0);
            const float face_length = length(face);
            if (face_length > 1e-12f) {
                ng = face / face_length;
                ng = dot(ng, p) > 0.0f ? -ng : ng;
                n = ng;
                lifted = v0 * w.x + v1 * w.y + v2 * w.z;
                primary = uint2(tri + 1u, f32tof16(b.x) | (f32tof16(b.y) << 16u));
                const uint4 surface = g_surfaces.Load4(tri * 16u);
                if (surface.x != 0u && surface.y != 0u && surface.z != 0u) {
                    float3 n0 = unpack_vertex_normal(surface.x);
                    float3 n1 = unpack_vertex_normal(surface.y);
                    float3 n2 = unpack_vertex_normal(surface.z);
                    float3 smooth = n0 * w.x + n1 * w.y + n2 * w.z;
                    // The vertex normals of the side the camera sees.
                    const float side = dot(smooth, ng) < 0.0f ? -1.0f : 1.0f;
                    smooth *= side;
                    const float smooth_length = length(smooth);
                    if (smooth_length > 1e-3f && dot(smooth, ng) > 0.05f * smooth_length) {
                        n = smooth / smooth_length;
                        n0 *= side;
                        n1 *= side;
                        n2 *= side;
                        const float3 h = lifted;
                        lifted = h - w.x * min(0.0f, dot(h - v0, n0)) * n0 - w.y * min(0.0f, dot(h - v1, n1)) * n1 -
                                 w.z * min(0.0f, dot(h - v2, n2)) * n2;
                    }
                }
            }
        }
    }
    const float3 origin = lifted + ng * (0.2f + abs(p.z) * 4e-4f);

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
        ray.TMax = g_sun_reach;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                 RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
        query.Proceed();
        sun = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0f : facing_sun;
        if ((g_flags & 64u) != 0u) {
            // GALAXY_RT_VIEW=6: the nearest blocker's distance up to 1e6, past the sun's reach,
            // 0.1 + 0.9 log10(t) / 6 (0: none).
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> nearest;
            ray.TMax = 1.0e6f;
            nearest.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
            nearest.Proceed();
            sun = nearest.CommittedStatus() == COMMITTED_TRIANGLE_HIT
                ? 0.1f + 0.9f * saturate(log10(max(nearest.CommittedRayT(), 1.0f)) / 6.0f)
                : 0.0f;
        }
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
                    weights[i] = luminance(g_points[2u * i + 1u].rgb) * nl * point_falloff(d2, i);
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

    // The glows (lamps, cores, Lumas): four lights picked in proportion to their power, one kept
    // in proportion to its light here (resampled importance), one shadow ray to it. Its light
    // joins the point lights' (their weight divided out).
    const uint emitters = g_emitters.Load(0u);
    if (emitters != 0u) {
        const float total = asfloat(g_emitters.Load(4u));
        float sum = 0.0f;
        float4 kept = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float3 kept_power = float3(0.0f, 0.0f, 0.0f);
        [unroll] for (uint c = 0u; c < 8u; ++c) {
            const float2 u = sample_point(id.xy, max(g_rays, 1u) + 7u + c);
            const float x = u.x * total;
            uint lo = 0u;
            uint hi = emitters - 1u;
            [loop] while (lo < hi) {
                const uint mid = (lo + hi) >> 1u;
                if (asfloat(g_emitters.Load(16u + mid * 32u + 28u)) < x) {
                    lo = mid + 1u;
                } else {
                    hi = mid;
                }
            }
            const float4 la = asfloat(g_emitters.Load4(16u + lo * 32u));
            const float3 power = asfloat(g_emitters.Load3(16u + lo * 32u + 16u));
            const float3 e = transform(g_to_eye, la.xyz);
            const float3 to = e - p;
            const float d2 = dot(to, to);
            const float nl = dot(n, to) * rsqrt(max(d2, 1e-6f));
            // Weight: its light here over its chance of being picked (power / total).
            const float weight = nl > 0.0f ? total * nl / (d2 + la.w * la.w) : 0.0f;
            sum += weight;
            if (weight > 0.0f && u.y * sum < weight) {
                kept = float4(e, la.w);
                kept_power = power;
            }
        }
        const float lum = luminance(kept_power);
        if (sum > 0.0f && lum > 0.0f) {
            const float3 to = kept.xyz - origin;
            const float span = length(to);
            RayDesc ray;
            ray.Origin = origin;
            ray.Direction = to / max(span, 1e-4f);
            ray.TMin = 0.0f;
            // The light fills its glow (a core, a lamp head): the ray ends at its radius.
            ray.TMax = max(span - max(0.9f * kept.w, 2.0f + 0.01f * span), 0.0f);
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                     RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
            query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
            query.Proceed();
            if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
                // power nl / (pi (d2 + r2)) over the kept light's weight, times the mean weight.
                // Bright bursts (lava eruptions, sparks) saturate softly instead of blowing out.
                const float3 glow = kept_power / (kPi * lum) * (sum * 0.125f);
                points += glow / (1.0f + luminance(glow) / 1.5f) / max(g_point_light, 1e-3f);
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
        float3 direction = t1 * (r * cos(phi)) + t2 * (r * sin(phi)) + n * sqrt(max(0.0f, 1.0f - u.x));
        // Never into the face itself: below its plane the direction mirrors back out.
        const float below = dot(direction, ng);
        direction = below < 0.0f ? direction - 2.0f * below * ng : direction;
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = direction;
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
    g_primary[index] = primary;
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
                Light history = scale_light(sum, 1.0f / weight);
                frames = min(count_sum / weight + 1.0f, float(g_history_frames));
                // The history stays within this frame's neighbourhood (mean +- 2.5 deviations of
                // the raw samples around): long accumulation where the light holds, quick change
                // where it moved (shadows of moving casters, lights switching).
                Light mean = make_light(float3(0.0f, 0.0f, 0.0f), 0.0f, float3(0.0f, 0.0f, 0.0f));
                Light square = mean;
                float around = 0.0f;
                [unroll] for (int dy = -1; dy <= 1; ++dy) {
                    [unroll] for (int dx = -1; dx <= 1; ++dx) {
                        const int2 s = int2(id.xy) + int2(dx, dy);
                        if (any(s < 0) || any(s >= int2(g_sample_size))) {
                            continue;
                        }
                        const uint si = uint(s.y) * g_sample_size.x + uint(s.x);
                        if (g_z[si] == 0.0f) {
                            continue;
                        }
                        float unused_count;
                        const Light v = unpack_light(g_raw[si], unused_count);
                        mean = add_light(mean, v);
                        square = add_light(square, make_light(v.sky * v.sky, v.sun * v.sun, v.points * v.points));
                        around += 1.0f;
                    }
                }
                if (around > 0.0f) {
                    mean = scale_light(mean, 1.0f / around);
                    square = scale_light(square, 1.0f / around);
                    const float3 sky_dev = sqrt(max(square.sky - mean.sky * mean.sky, 0.0f)) * 2.5f + 0.02f;
                    const float sun_dev = sqrt(max(square.sun - mean.sun * mean.sun, 0.0f)) * 2.5f + 0.02f;
                    const float3 points_dev = sqrt(max(square.points - mean.points * mean.points, 0.0f)) * 2.5f + 0.02f;
                    history = make_light(clamp(history.sky, mean.sky - sky_dev, mean.sky + sky_dev),
                                         clamp(history.sun, mean.sun - sun_dev, mean.sun + sun_dev),
                                         clamp(history.points, mean.points - points_dev, mean.points + points_dev));
                }
                const float direct = max(1.0f / frames, 0.08f);
                result = make_light(lerp(history.sky, raw.sky, 1.0f / frames), lerp(history.sun, raw.sun, direct),
                                    lerp(history.points, raw.points, direct));
            }
        }
    }
    g_history[index] = pack_light(result, frames);
}

// Edge-aware 5x5 filter at `step` samples (plane distance and normal); three passes (steps 1, 2,
// 4) reach 26 samples around.
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
void SpatialMCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_sample_size)) {
        return;
    }
    const uint index = id.y * g_sample_size.x + id.x;
    if (g_z[index] == 0.0f) {
        g_history[index] = pack_light(kOpenSky, 0.0f);
        return;
    }
    g_history[index] = pack_light(filtered(id.xy, 2, false), 0.0f);
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
    const Light light = filtered(id.xy, 4, true);
    const float3 sun = light.sun * g_sun_light.rgb;
    const float3 points = light.points * g_point_light;
    // The sky and bounce light follow the sky: under a dark sky the lights carry the picture.
    float ambient = g_sun_light.w;
    if (g_picture_sums.Load(36u) != 0u) {
        ambient *= lerp(g_ambient.x, 1.0f, saturate(asfloat(g_picture_sums.Load(32u)) / g_ambient.y));
    }
    float3 lighting = ambient * light.sky + sun + points;
    // With the sky open: what the picture capture keeps (no feedback through the sky and bounce).
    float3 open = ambient + sun + points;
    const float exposure = g_picture_sums.Load(52u) != 0u ? asfloat(g_picture_sums.Load(48u)) : 1.0f;
    // A surface the game lights already turns its shading towards its lights: the traced light
    // shadows it (the ratio to the same light unshadowed). The point lights add to it, or, where
    // the game lights it with them too (GX light 4), shadow it likewise.
    const uint tri = g_primary[index].x;
    const uint draw_flags = tri != 0u ? g_surfaces.Load((tri - 1u) * 16u + 12u) : 0u;
    const bool point_lit = (draw_flags & kDrawPointLit) != 0u && g_point_count != 0u;
    if ((draw_flags & (kDrawLit | kDrawSunKey)) == (kDrawLit | kDrawSunKey)) {
        // Its key light lies along the traced sun: the game's shading is its ambient A plus its GX
        // light 0 colour K times n.l (and its point light), clamped to 1 like GX's accumulator; the
        // traced light takes A times the sky's light (as the sky state scales it) and K times the
        // traced sun.
        const float3 n = unpack_normal(g_normal[index]);
        const float facing = saturate(dot(n, g_sun.xyz));
        const uint4 colors = g_draws.Load4(min(draw_flags >> 16u, max(g_draw_count, 1u) - 1u) * 16u);
        const float3 a = rgb8(colors.x);
        const float3 k = rgb8(colors.y);
        const float3 sky_a = a * (ambient / max(g_sun_light.w, 1e-3f));
        float3 game = a + k * facing;
        float3 traced = sky_a * light.sky + k * light.sun;
        float3 opened = sky_a + k * light.sun;
        float3 added = points;
        if (point_lit) {
            game += points_unshadowed(sample_position(id.xy, z), n, true);
            traced += points;
            opened += points;
            added = float3(0.0f, 0.0f, 0.0f);
        }
        const float3 shading = max(min(game, 1.0f), 1e-3f);
        lighting = min(traced, 1.0f) / shading + added;
        open = min(opened, 1.0f) / shading + added;
    } else if ((draw_flags & kDrawLit) != 0u) {
        const float3 n = unpack_normal(g_normal[index]);
        const float facing = saturate(dot(n, g_sun.xyz));
        float3 unshadowed = g_sun_light.www + facing * g_sun_light.rgb;
        float3 added = points;
        if (point_lit) {
            unshadowed += points_unshadowed(sample_position(id.xy, z), n, false) * g_point_light;
            added = float3(0.0f, 0.0f, 0.0f);
            lighting = (ambient * light.sky + sun + points) / max(unshadowed, 1e-3f);
            open = (ambient + sun + points) / max(unshadowed, 1e-3f);
        } else {
            lighting = (ambient * light.sky + sun) / max(unshadowed, 1e-3f) + added;
            open = (ambient + sun) / max(unshadowed, 1e-3f) + added;
        }
    }
    lighting *= exposure;
    open *= exposure;
    float3 m = lerp(float3(1.0f, 1.0f, 1.0f), lighting, g_strength);
    // Lit surfaces brighten towards 1.5 on a soft shoulder (no flat, washed-out highlights).
    {
        const float3 over = max(m - 1.0f, 0.0f);
        m = min(m, 1.0f) + over / (1.0f + 2.0f * over);
    }
    // A glowing surface (unlit, bright, under its own glow in the previous picture: a core, a lamp)
    // sends its light whatever reaches it: it keeps its colour and shines brighter.
    if ((draw_flags & kDrawLit) == 0u && (g_flags & 2u) != 0u) {
        const float3 previous = transform(g_picture, sample_position(id.xy, z));
        if (previous.z < -g_near) {
            const float2 s = (eye_to_pixel(g_projection, g_viewport, previous) - 0.5f) / float(g_factor) + 0.5f;
            if (all(s >= 0.0f) && all(s < float2(g_sample_size))) {
                const uint si = uint(s.y) * g_sample_size.x + uint(s.x);
                const float pz = asfloat(g_picture_in[si].y);
                if (pz != 0.0f && abs(pz - previous.z) <= 0.03f * abs(previous.z) + 2.0f) {
                    const float3 base = unpack_r11g11b10(g_glow_base[si]);
                    const float added = luminance(unpack_r11g11b10(g_glow_added[si]));
                    const float glowing = smoothstep(0.1f, 0.4f, added) *
                                          smoothstep(0.55f, 0.85f, max(base.r, max(base.g, base.b)));
                    m = lerp(m, max(m, (1.0f + g_ambient.z * saturate(added)).xxx), glowing);
                    if ((g_flags & 32u) != 0u) {
                        m = float3(2.0f * glowing, 2.0f * saturate(added),
                                   2.0f * max(base.r, max(base.g, base.b)));
                    }
                }
            }
        }
    }
    if ((g_flags & (4u | 64u)) != 0u) {
        m = light.sun.xxx;     // GALAXY_RT_VIEW=2: the sun's light alone (6: its blocker's distance)
    } else if ((g_flags & 8u) != 0u) {
        m = light.sky;         // GALAXY_RT_VIEW=3: the sky and bounce light alone
    } else if ((g_flags & 16u) != 0u) {
        m = light.points;      // GALAXY_RT_VIEW=4: the point lights alone
    }
    // The picture capture divides out the sky and bounce part (no feedback through it).
    const float3 without = lerp(float3(1.0f, 1.0f, 1.0f), open, g_strength);
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
    float m_split;  // GALAXY_RT_SPLIT: left of it the game's own picture (a dark line between)
    uint m_pad;
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
    if (position.x < m_split + 1.0f) {
        if (position.x < m_split - 1.0f) {
            discard;
        }
        result.keep = float4(0.3f, 0.3f, 0.3f, 1.0f);
        return result;
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
