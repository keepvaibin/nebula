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
    float g_sky_ibl;            // rays reaching the sky take the light of their direction (0: the
                                // sky's mean colour from every direction)
    float g_fill;               // the share of the game's own light every surface keeps (by day;
                                // half again at night)
    float4 g_emit;              // glow lights: the light at which one stops counting (its reach),
                                // the longest reach (eye units), their gain; the gain of glowing
                                // surfaces' light (lava, magma, fire) on what sees them
    float4 g_self;              // the material table's emitters glowing from their own surfaces: area
                                // lights (lava) and point lights (Lumas, star bits) per unit of
                                // intensity; an area light's radiance on what sees it per unit
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
                                                         // (kEmitStride) world position, radius; power,
                                                         // cumulative; spot direction, cosine of its cone
StructuredBuffer<uint> g_glow_base : register(t16);      // the previous picture before its glows
StructuredBuffer<uint> g_glow_added : register(t17);     // what its glows added
ByteAddressBuffer g_draws : register(t18);              // per draw (kDrawStride): see kDrawStride
ByteAddressBuffer g_sky_light : register(t19);           // the sky light: 9 SH coefficients (rgb),
                                                         // its mean luminance, known (SkyLightShCS)
RWStructuredBuffer<float> g_z : register(u0);
RWStructuredBuffer<uint> g_normal : register(u1);
RWStructuredBuffer<uint4> g_raw : register(u2);
RWStructuredBuffer<uint4> g_history : register(u3);
RWStructuredBuffer<uint4> g_final : register(u4);
RWStructuredBuffer<uint2> g_primary : register(u5);      // triangle + 1 (0: none), barycentrics

static const float kPi = 3.14159265f;
static const uint kDrawLit = 8u;    // RtDraw.flags: GX-lit colour
static const uint kDrawSunKey = 16u;    // RtDraw.flags: its GX light 0 lies along the traced sun
static const uint kDrawPointLit = 32u;  // RtDraw.flags: lit by GX light 4 (the game's point light)
// RtDraw.flags: its colour is its material alone (the game's lights removed): the traced light is
// its whole light, as on unlit surfaces.
static const uint kDrawDelit = 4096u;
// g_draws word 2 (SceneRayTracer::add_draw): the material table's emission, colour RGBA8 with these
// in the low byte; word 3: its intensity.
static const uint kEmitArea = 2u;       // low 2 bits: 1 point light, 2 area light
static const uint kEmitNamed = 4u;      // the table names this draw's texture
static const uint kEmitNoLight = 8u;    // never a light
// g_draws records (SceneRayTracer::draw_colors_): ambient, GX light 0 colour, emission, intensity,
// roughness and metalness, material colour, flags, spare.
static const uint kDrawStride = 32u;
// g_draws word 6: the table gives the light source's own glow (in place of its intensity), unorm16 of
// glow / kDrawGlowScale in the high half.
static const uint kDrawTableGlow = 2u;
static const float kDrawGlowScale = 16.0f;
// The lighting multiplies the game's colour by at most this; the composite rolls it off on a film
// shoulder from kLitKnee (apply_light).
static const float kMaxLight = 8.0f;
static const float kLitKnee = 0.8f;
// The share of the light the film shoulder rolls off (lava, fire, lamps, glints) that joins the
// bloom (CaptureCS).
static const float kExcessBloom = 0.5f;

// As lit (c m) up to the knee (0.8, or the colour's brightest channel where it is brighter: the
// game's own highlights stay as drawn), then a film shoulder towards white that never passes it,
// on the brightest channel with the hue kept (sunlit sand and grass keep their colour and detail).
// Only light far past white (lava, fire, lamps) runs channel by channel towards white-hot.
// Slope 1 at the knee: no seam where it starts.
float3 film_light(float3 c, float3 m)
{
    const float3 lit = c * m;
    const float peak = max(lit.r, max(lit.g, lit.b));
    const float knee = max(max(c.r, max(c.g, c.b)), kLitKnee);
    if (peak <= knee) {
        return lit;
    }
    const float room = max(1.0f - knee, 1e-4f);
    const float3 hue = lit * ((knee + room * (1.0f - exp(-(peak - knee) / room))) / peak);
    const float3 knees = max(c, kLitKnee);
    const float3 rooms = max(1.0f - knees, 1e-4f);
    const float3 channels = lerp(lit, knees + rooms * (1.0f - exp(-(lit - knees) / rooms)), step(knees, lit));
    return lerp(hue, channels, smoothstep(1.6f, 3.5f, peak));
}
// Buckets of the exposure measurement (16 bytes each: lit, game, pixels, spare).
static const uint kExposureBuckets = 16u;
// A light of the glow lights' lists: position, radius; power, frames unseen or cumulative power; spot
// direction (world; 0 0 0 a point light), cosine of its cone.
static const uint kEmitStride = 48u;
// A found spot light whose faces were not seen this frame (which way they face is unknown): the cosine
// field holds kSpotUnknown + its cone's cosine.
static const float kSpotUnknown = -4.0f;

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

// A colour's luminance after the finish's tone (rt_post.hlsl shoulder, then grade's filmic S at
// strength `grade`).
float finished_luminance(float3 c, float grade)
{
    const float3 over = max(c - 0.95f, 0.0f);
    const float3 rolled = min(c, 0.95f) + 0.05f * (1.0f - exp(-over / 0.05f));
    const float spill = max(max(over.r, max(over.g, over.b)) - 0.05f, 0.0f);
    const float l = luminance(saturate(rolled + spill * 0.15f));
    return lerp(l, saturate(l + 0.85f * (l - 0.5f) * l * (1.0f - l)), grade);
}

float3 rotate3(float4 rows[3], float3 d)
{
    return float3(dot(rows[0].xyz, d), dot(rows[1].xyz, d), dot(rows[2].xyz, d));
}

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
                             // the translucent phases added since c_glow_base goes to c_glow;
                             // bit 2: the sky's colour goes to c_sky by world direction
    float c_strength;
    float c_split;           // GALAXY_RT_SPLIT: pixels left of it kept the game's own colour
    float c_surface_gain;    // a glowing surface's light per unit of colour and eye-space area
    float c_sky_distance;    // farther surfaces are the sky (backdrops)
    float c_exposure_day;    // the mean lighting the exposure holds under a bright sky
    float4 c_projection;
    float4 c_to_world[3];    // eye to world
    float c_exposure_night;  // ... under a black sky
    float c_daylight;        // the sky's luminance that counts as bright
    uint c_exposure;         // SkyStateCS: 1 the exposure follows the lighting, 2 it starts over at 1
                             // (a new stage); GlowBaseCS: bit 0 measure it, bit 2 through the finish's tone
    float c_grade;           // the finish's grade strength (rt_post.hlsl grade)
};
Texture2D<float4> c_color : register(t0);
Texture2D<float> c_depth_tex : register(t1);
StructuredBuffer<uint4> c_final : register(t2);
StructuredBuffer<uint> c_glow_base : register(t3);
RWStructuredBuffer<uint2> c_picture : register(u0);
RWByteAddressBuffer c_sums : register(u1);
RWStructuredBuffer<uint> c_glow : register(u2);
RWByteAddressBuffer c_found : register(u3);              // this frame's lights (see e_found)
RWByteAddressBuffer c_sky : register(u4);                // per bin: samples, r, g, b (rt_sky_sun.h)
RWStructuredBuffer<float4> c_sky_radiance : register(u5); // per bin: colour over time, seen
RWByteAddressBuffer c_sky_sh : register(u6);             // see g_sky_light
// The composite's exposure measurement (CompositePS m_exposure): kExposureBuckets of lit and game
// luminance (x 256) and pixels from GlowBaseCS; SkyStateCS reads and clears them.
RWByteAddressBuffer c_exposure_sums : register(u7);
// The game's own opaque picture (the lighting composite's source), for the exposure's measurement.
Texture2D<float4> c_game : register(t4);

// The sky map's bin of a world direction: octahedral, +y the pole, 32 x 32 (rt_sky_sun.h sky_bin).
uint sky_bin(float3 w)
{
    float2 p = w.xz / (abs(w.x) + abs(w.y) + abs(w.z));
    if (w.y < 0.0f) {
        p = (1.0f - abs(p.yx)) * float2(p.x >= 0.0f ? 1.0f : -1.0f, p.y >= 0.0f ? 1.0f : -1.0f);
    }
    const uint2 cell = min(uint2(max((p * 0.5f + 0.5f) * 32.0f, 0.0f)), 31u);
    return cell.y * 32u + cell.x;
}

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
                c_found.Store4(16u + at * kEmitStride, asuint(float4(transform(c_to_world, centre), max(spread, 1.0f))));
                c_found.Store4(16u + at * kEmitStride + 16u, asuint(float4(power, 0.0f)));
                c_found.Store4(16u + at * kEmitStride + 32u, uint4(0u, 0u, 0u, 0u));
            }
        }
    }
}

// A sky bin's direction (the inverse of sky_bin at the bin's centre) and its solid angle in
// units of the octahedron's (x, z) area: 1 / |v|^3 for the point v on the octahedron.
float3 sky_bin_direction(uint bin, out float solid)
{
    const float2 e = (float2(bin & 31u, bin >> 5u) + 0.5f) / 32.0f * 2.0f - 1.0f;
    float3 v = float3(e.x, 1.0f - abs(e.x) - abs(e.y), e.y);
    if (v.y < 0.0f) {
        v.xz = (1.0f - abs(e.yx)) * float2(e.x >= 0.0f ? 1.0f : -1.0f, e.y >= 0.0f ? 1.0f : -1.0f);
    }
    const float length_v = length(v);
    solid = 1.0f / (length_v * length_v * length_v);
    return v / length_v;
}

// Real spherical harmonics, bands 0-2.
void sh9(float3 d, out float basis[9])
{
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * d.y;
    basis[2] = 0.488603f * d.z;
    basis[3] = 0.488603f * d.x;
    basis[4] = 1.092548f * d.x * d.y;
    basis[5] = 1.092548f * d.y * d.z;
    basis[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
    basis[7] = 1.092548f * d.x * d.z;
    basis[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

// The sky light over time: each bin this capture saw moves a tenth of the way to it.
[numthreads(64, 1, 1)]
void SkyLightMapCS(uint3 id : SV_DispatchThreadID)
{
    const uint4 bin = c_sky.Load4(id.x * 16u);   // samples, r, g, b
    if (bin.x == 0u) {
        return;
    }
    const float3 color = float3(bin.yzw) / (255.0f * float(bin.x));
    const float4 state = c_sky_radiance[id.x];
    c_sky_radiance[id.x] = float4(state.w > 0.0f ? lerp(state.rgb, color, 0.1f) : color, 1.0f);
}

groupshared float4 s_sky_mean[64];
groupshared float s_sky_sh[64][27];

// The sky light's spherical harmonics; directions never seen take the mean of those seen.
[numthreads(64, 1, 1)]
void SkyLightShCS(uint gi : SV_GroupIndex)
{
    float4 mean = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (uint t = gi; t < 1024u; t += 64u) {
        const float4 v = c_sky_radiance[t];
        float solid;
        sky_bin_direction(t, solid);
        mean += v.w > 0.0f ? float4(v.rgb, 1.0f) * solid : float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    s_sky_mean[gi] = mean;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 32u; s > 0u; s >>= 1u) {
        if (gi < s) {
            s_sky_mean[gi] += s_sky_mean[gi + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    // At least a sixteenth of the sphere seen (in octahedron area: 8 for the whole sphere).
    const float4 total = s_sky_mean[0];
    GroupMemoryBarrierWithGroupSync();
    if (total.w < 0.5f) {
        if (gi == 0u) {
            c_sky_sh.Store(112u, 0u);
        }
        return;
    }
    const float3 average = total.rgb / total.w;
    float sums[28];
    [unroll] for (uint k = 0u; k < 28u; ++k) {
        sums[k] = 0.0f;
    }
    for (uint u = gi; u < 1024u; u += 64u) {
        const float4 v = c_sky_radiance[u];
        const float3 radiance = v.w > 0.0f ? v.rgb : average;
        float solid;
        const float3 d = sky_bin_direction(u, solid);
        float basis[9];
        sh9(d, basis);
        [unroll] for (uint b = 0u; b < 9u; ++b) {
            sums[b * 3u + 0u] += radiance.r * basis[b] * solid;
            sums[b * 3u + 1u] += radiance.g * basis[b] * solid;
            sums[b * 3u + 2u] += radiance.b * basis[b] * solid;
        }
        sums[27] += solid;
    }
    [unroll] for (uint j = 0u; j < 27u; ++j) {
        s_sky_sh[gi][j] = sums[j];
    }
    s_sky_mean[gi] = float4(sums[27], 0.0f, 0.0f, 0.0f);
    GroupMemoryBarrierWithGroupSync();
    if (gi < 27u) {
        float coefficient = 0.0f;
        float area = 0.0f;
        for (uint w = 0u; w < 64u; ++w) {
            coefficient += s_sky_sh[w][gi];
            area += s_sky_mean[w].x;
        }
        c_sky_sh.Store(gi * 4u, asuint(coefficient * (4.0f * kPi / area)));
    }
    if (gi == 0u) {
        c_sky_sh.Store2(108u, uint2(asuint(max(luminance(average), 1e-3f)), 1u));
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
    // The exposure guards the picture's brightness: the ratio of the lit picture's mean luminance
    // (with its reflections, as the finish will tone it) to the game's own on the same pixels
    // (GlowBaseCS) is left alone inside a band around its target (0.97-1.12 of it) and moves back
    // into the band a tenth of the way per frame outside it: never darker or washed out, but the
    // traced light's own contrast and brightness stay.
    uint3 measured = uint3(0u, 0u, 0u);
    [unroll] for (uint b = 0u; b < kExposureBuckets; ++b) {
        measured += c_exposure_sums.Load3(b * 16u);
        c_exposure_sums.Store3(b * 16u, uint3(0u, 0u, 0u));
    }
    if (c_exposure == 2u) {
        measured = uint3(0u, 0u, 0u);
        c_sums.Store2(48u, uint2(asuint(1.0f), 1u));
    }
    // For [rt-emit]: lit and game sums, pixels.
    c_sums.Store(44u, measured.z);
    c_sums.Store2(56u, measured.xy);
    const uint grid = max(((c_sample_size.x + 7u) / 8u) * ((c_sample_size.y + 7u) / 8u), 1u);
    if (c_exposure == 0u) {
        c_sums.Store2(48u, uint2(asuint(1.0f), 0u));
    } else if (measured.z * 50u > grid && measured.y != 0u) {
        const float ratio = float(measured.x) / float(measured.y);
        const float day = c_sums.Load(36u) != 0u ? saturate(asfloat(c_sums.Load(32u)) / c_daylight) : 1.0f;
        const float target = lerp(c_exposure_night, c_exposure_day, day);
        const float exposure = c_sums.Load(52u) != 0u ? asfloat(c_sums.Load(48u)) : 1.0f;
        const float goal = clamp(ratio, lerp(0.97f, 0.9f, day) * target, 1.12f * target);
        const float next = clamp(exposure * lerp(1.0f, goal / max(ratio, 0.05f), 0.1f), 0.35f, 2.5f);
        c_sums.Store2(48u, uint2(asuint(next), 1u));
    }
}

// After the composite (lighting and reflections): the picture before the translucent phases. One
// sample in 8 x 8 on a surface (not the sky or backdrops) measures the exposure: the lit picture as
// the finish will tone it against the game's own, spread over the buckets so that few atomics meet.
[numthreads(8, 8, 1)]
void GlowBaseCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= c_sample_size)) {
        return;
    }
    c_glow[id.y * c_sample_size.x + id.x] = pack_r11g11b10(block_color(id.xy));
    if ((c_exposure & 1u) != 0u && ((id.x | id.y) & 7u) == 0u) {
        const int2 q = int2(min(id.xy * c_factor, c_target_size - 1u));
        const float depth = c_depth_tex.Load(int3(q, 0));
        if (!far_plane(c_depth, depth) && inside(c_viewport, float2(q) + 0.5f) && float(q.x) >= c_split &&
            -depth_to_eye_z(c_depth, depth) <= c_sky_distance) {
            const float3 lit = c_color.Load(int3(q, 0)).rgb;
            const float l = (c_exposure & 4u) != 0u ? finished_luminance(lit, c_grade) : luminance(saturate(lit));
            const uint at = (((id.x >> 3u) + (id.y >> 3u)) % kExposureBuckets) * 16u;
            c_exposure_sums.InterlockedAdd(at, uint(saturate(l) * 256.0f));
            c_exposure_sums.InterlockedAdd(at + 4u, uint(luminance(saturate(c_game.Load(int3(q, 0)).rgb)) * 256.0f));
            c_exposure_sums.InterlockedAdd(at + 8u, 1u);
        }
    }
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
            const uint4 f = c_final[index];
            float keep = abs(f16tof32(f.y >> 16u));
            const float3 m = float3(f16tof32(f.x), f16tof32(f.x >> 16u), f16tof32(f.y));
            const float lit = luminance(m);
            // The light the composite's film shoulder rolled off (lava, fire, lamps, bright sunlit
            // surfaces) blooms as the glows do.
            // A small light source's halo (SpatialBCS): its colour joins the bloom.
            const float halo = f16tof32(f.w >> 16u);
            if (halo > 0.0f && (c_divide & 2u) != 0u && float(q.x) >= c_split) {
                c_glow[index] = pack_r11g11b10(unpack_r11g11b10(c_glow[index]) + color * halo);
            }
            // Only where the light passes the knee at all (most samples do not).
            if ((c_divide & 2u) != 0u && float(q.x) >= c_split && max(m.r, max(m.g, m.b)) > 1.0f) {
                const float3 game = c_game.Load(int3(q, 0)).rgb;
                // Hot light only (lava, fire, lamps): sunlit ground does not glow.
                const float3 lit = game * m;
                const float3 excess = max(lit - film_light(game, m), 0.0f) *
                                      smoothstep(1.4f, 2.5f, max(lit.r, max(lit.g, lit.b)));
                if (luminance(excess) > 0.004f) {
                    const float3 added = unpack_r11g11b10(c_glow[index]);
                    c_glow[index] = pack_r11g11b10(added + excess * kExcessBloom);
                }
            }
            if (float(q.x) < c_split) {
                keep *= lit;
            }
            color *= clamp(keep, 0.25f, 4.0f);
        }
        // Bright, saturated pixels (lamps, lava, glowing objects) send more light than their
        // stored colour.
        const float peak = max(color.r, max(color.g, color.b));
        const float glow = smoothstep(0.72f, 1.0f, peak);
        const float day = c_sums.Load(36u) != 0u ? saturate(asfloat(c_sums.Load(32u)) / c_daylight) : 1.0f;
        color *= 1.0f + c_emissive * (1.0f - day) * glow * glow;
    }
    c_picture[index] = uint2(pack_r11g11b10(color), asuint(z));
    // Averages of the sky and of the scene (8-bit units, glow clipped); the EFB outside the
    // viewport (rows below the picture) is neither.
    if (!inside(c_viewport, float2(q) + 0.5f)) {
        return;
    }
    const uint3 units = uint3(round(saturate(color) * 255.0f));
    if (sky && (c_divide & 4u) != 0u && ((id.x | id.y) & 3u) == 0u) {
        const float3 d = pixel_direction(c_projection, c_viewport, float2(q) + 0.5f);
        const float3 w = float3(dot(c_to_world[0].xyz, d), dot(c_to_world[1].xyz, d), dot(c_to_world[2].xyz, d));
        const uint bin = sky_bin(w) * 16u;
        c_sky.InterlockedAdd(bin, 1u);
        c_sky.InterlockedAdd(bin + 4u, units.r);
        c_sky.InterlockedAdd(bin + 8u, units.g);
        c_sky.InterlockedAdd(bin + 12u, units.b);
    }
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
    float e_authored_power;     // a Luma's light (the material table's intensity 1)
    float e_time;               // seconds (pulsing lights)
    uint2 e_pad;
};
Texture2D<float4> e_color : register(t0);
Texture2D<float> e_depth_tex : register(t1);
ByteAddressBuffer e_draws : register(t2);             // per draw: its triangles and the table's light
ByteAddressBuffer e_triangles : register(t3);         // their eye-space triangles (36 bytes)
StructuredBuffer<uint> e_added : register(t4);        // per sample: light the glows added
RWByteAddressBuffer e_found : register(u0);           // count; per glow light (kEmitStride) position,
                                                      // radius; power, colour unknown; spot
RWStructuredBuffer<float4> e_lights : register(u1);   // per light: position, radius; power, frames unseen;
                                                      // spot direction, cosine of its cone
RWByteAddressBuffer e_list : register(u2);            // count, total; per light position, radius;
                                                      // power, cumulative power; spot

static const uint kEmitLights = 256u;
static const float kEmitLargest = 700.0f;      // eye units: a glow wider than this is no light
static const float kEmitForget = 0.004f;       // a light no longer seen that cannot reach the camera's
                                               // surroundings (its light there below this) fades fast
static const uint kEmitFound = 1024u;

groupshared float4 s_glow_a[64];
groupshared float4 s_glow_b[64];
groupshared float s_glow_area[64];

// e_draws records of the glow draws (SceneRayTracer::glow_lights_, 32 bytes): first triangle, count, the
// table's colour (RGBA8), its intensity; spare, pulse, flags, spare. Flags: the light scales by the
// intensity; it takes the table's colour.
static const uint kGlowTableIntensity = 4u;
static const uint kGlowTableColor = 8u;

// One group per glow draw.
[numthreads(64, 1, 1)]
void GlowEmitCS(uint3 group : SV_GroupID, uint gi : SV_GroupIndex)
{
    const uint4 entry = e_draws.Load4(group.x * 32u);
    const uint2 range = entry.xy;   // first triangle, count
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
                const float l = luminance(added);
                if (l > 0.02f) {
                    // Over a bright background the added light loses the channels the background had
                    // already filled (white fire over orange lava measures blue): where the picture
                    // reaches white its colour is the one shown.
                    const float3 shown = e_color.Load(int3(clamp(int2(q), int2(0, 0), int2(e_target_size) - 1), 0)).rgb;
                    const float filled = smoothstep(0.8f, 1.0f, max(shown.r, max(shown.g, shown.b)));
                    const float3 hue = lerp(added / l, shown / max(luminance(shown), 1e-3f), filled);
                    const float w = l * area;
                    a += float4(hue * w, w);
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
        float3 power = s_glow_a[0].rgb * (0.25f * e_gain);
        const uint flags = e_draws.Load(group.x * 32u + 24u);
        if ((flags & kGlowTableColor) != 0u) {
            // The table's colour at the luminance the picture shows (fire is orange, not the white its
            // sprites clip to).
            const float3 tint = float3(float(entry.z >> 24u), float((entry.z >> 16u) & 0xFFu),
                                       float((entry.z >> 8u) & 0xFFu)) / 255.0f;
            power = tint / max(luminance(tint), 1e-3f) * luminance(power);
        }
        if ((flags & kGlowTableIntensity) != 0u) {
            power *= asfloat(entry.w);
        }
        // The largest glows are atmospheres and halos around planets and the sky, not lights.
        if (luminance(power) >= 4.0f && radius <= kEmitLargest) {
            uint index;
            e_found.InterlockedAdd(0u, 1u, index);
            if (index < kEmitFound) {
                const uint at = 16u + index * kEmitStride;
                e_found.Store4(at, asuint(float4(transform(e_to_world, centre), radius)));
                e_found.Store4(at + 16u, asuint(float4(power, 0.0f)));
                e_found.Store4(at + 32u, uint4(0u, 0u, 0u, 0u));
            }
        }
    }
}

// The material table's point and spot lights (rt_materials.h: Lumas, star bits, fire and ice
// enemies, a boss's glowing weak spot, a headlamp): each draw's eye-space triangles in the scene
// (e_triangles) give the light's centre and spread; its power is the table's colour times its
// intensity (a Luma's: e_authored_power), pulsing at its rate. A tinted light takes the colour its
// draw shows where the picture shows it (its triangles' centres at the depth there); a spot light
// shines along its faces' mean normal (a lens or a cup: a closed shape's normals cancel out and it
// stays a point light), turned out of the model by the faces the camera sees (they face it).
// One group per draw (e_draws: first triangle, count, colour RGBA8, intensity; range, pulse, flags,
// cosine of the cone).
static const uint kAuthoredTint = 1u;
static const uint kAuthoredSpot = 2u;

groupshared float4 s_authored_n[64];   // face normal sum (twice the area), facing evidence (area)
groupshared float4 s_authored_c[64];   // colour seen (times area), area seen

[numthreads(64, 1, 1)]
void AuthoredEmitCS(uint3 group : SV_GroupID, uint gi : SV_GroupIndex)
{
    const uint4 entry = e_draws.Load4(group.x * 32u);
    const float4 shape = asfloat(e_draws.Load4(group.x * 32u + 16u));
    const uint flags = asuint(shape.z);
    float4 b = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 normals = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 seen = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float area_sum = 0.0f;
    for (uint t = gi; t < entry.y; t += 64u) {
        const uint at = (entry.x + t) * 36u;
        const float3 v0 = asfloat(e_triangles.Load3(at));
        const float3 v1 = asfloat(e_triangles.Load3(at + 12u));
        const float3 v2 = asfloat(e_triangles.Load3(at + 24u));
        const float3 c = (v0 + v1 + v2) / 3.0f;
        const float3 face = cross(v1 - v0, v2 - v0);
        const float area = 0.5f * length(face);
        if (area > 0.0f && all(abs(c) < 1.0e6f)) {
            b += float4(c * area, dot(c, c) * area);
            area_sum += area;
            normals.xyz += face;
            if ((flags & (kAuthoredTint | kAuthoredSpot)) != 0u && c.z < -e_near) {
                const float2 q = eye_to_pixel(e_projection, e_viewport, c);
                if (inside(e_viewport, q) && all(q >= 0.0f) && all(q < float2(e_target_size))) {
                    const float depth = e_depth_tex.Load(int3(int2(q), 0));
                    if (!far_plane(e_depth, depth) &&
                        abs(depth_to_eye_z(e_depth, depth) - c.z) < 0.02f * abs(c.z) + 2.0f) {
                        seen += float4(e_color.Load(int3(int2(q), 0)).rgb * area, area);
                        normals.w += dot(face, -c) > 0.0f ? area : -area;
                    }
                }
            }
        }
    }
    s_glow_b[gi] = b;
    s_glow_area[gi] = area_sum;
    s_authored_n[gi] = normals;
    s_authored_c[gi] = seen;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 32u; s > 0u; s >>= 1u) {
        if (gi < s) {
            s_glow_b[gi] += s_glow_b[gi + s];
            s_glow_area[gi] += s_glow_area[gi + s];
            s_authored_n[gi] += s_authored_n[gi + s];
            s_authored_c[gi] += s_authored_c[gi + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0u && s_glow_area[0] > 0.0f) {
        const float w = s_glow_area[0];
        const float3 centre = s_glow_b[0].xyz / w;
        const float spread = sqrt(max(s_glow_b[0].w / w - dot(centre, centre), 0.0f));
        float3 color = float3(float(entry.z >> 24u), float((entry.z >> 16u) & 0xFFu),
                              float((entry.z >> 8u) & 0xFFu)) / 255.0f;
        // power.w 1: the colour is unknown this frame (the light keeps the one it had).
        float unknown = 0.0f;
        if ((flags & kAuthoredTint) != 0u) {
            const float4 c = s_authored_c[0];
            const float3 shown = c.w > 0.0f ? c.rgb / c.w : float3(0.0f, 0.0f, 0.0f);
            const float peak = max(shown.r, max(shown.g, shown.b));
            if (peak > 0.02f) {
                color *= shown / peak;
            } else {
                unknown = 1.0f;
            }
        }
        float intensity = asfloat(entry.w);
        if (shape.y > 0.0f) {
            intensity *= 0.75f + 0.25f * sin(6.2831853f * shape.y * e_time);
        }
        const float3 power = color * (intensity * e_authored_power);
        float4 spot = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float3 at_point = centre;
        if ((flags & kAuthoredSpot) != 0u) {
            const float4 n = s_authored_n[0];
            const float agree = length(n.xyz) / max(2.0f * w, 1e-6f);
            if (agree > 0.3f) {
                const float3 eye_dir = n.xyz / length(n.xyz);
                if (n.w != 0.0f) {
                    const float3 out_dir = n.w > 0.0f ? eye_dir : -eye_dir;
                    spot = float4(normalize(rotate3(e_to_world, out_dir)), shape.w);
                    // In front of its lens: the lamp's own body does not shadow it.
                    at_point = centre + out_dir * max(spread, 1.0f);
                } else {
                    spot = float4(0.0f, 0.0f, 0.0f, kSpotUnknown + shape.w);
                }
            }
        }
        if (luminance(power) >= 4.0f) {
            uint index;
            e_found.InterlockedAdd(0u, 1u, index);
            if (index < kEmitFound) {
                const uint at = 16u + index * kEmitStride;
                e_found.Store4(at, asuint(float4(transform(e_to_world, at_point), max(spread, 1.0f))));
                e_found.Store4(at + 16u, asuint(float4(power, unknown)));
                e_found.Store4(at + 32u, asuint(spot));
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
    float4 la = e_reset != 0u ? float4(0.0f, 0.0f, 0.0f, 0.0f) : e_lights[gi * 3u];
    float4 lb = e_reset != 0u ? float4(0.0f, 0.0f, 0.0f, 0.0f) : e_lights[gi * 3u + 1u];
    float4 lc = e_reset != 0u ? float4(0.0f, 0.0f, 0.0f, 0.0f) : e_lights[gi * 3u + 2u];
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
            const float4 f = asfloat(e_found.Load4(16u + t * kEmitStride));
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
        float4 spot = float4(0.0f, 0.0f, 0.0f, 0.0f);
        bool spot_known = false;
        const float lb_l = luminance(lb.rgb);
        [loop] for (uint t = 0u; t < found; ++t) {
            if (s_emit_best[t] != 0xFFFFFFFFu && (s_emit_best[t] & 0xFFu) == gi) {
                const float4 fa = asfloat(e_found.Load4(16u + t * kEmitStride));
                const float4 fp = asfloat(e_found.Load4(16u + t * kEmitStride + 16u));
                const float4 fs = asfloat(e_found.Load4(16u + t * kEmitStride + 32u));
                const float w = luminance(fp.rgb) + 1e-6f;
                // A colour not seen this frame: the light's own, at the power found.
                power += fp.w != 0.0f && lb_l > 0.0f ? lb.rgb * (luminance(fp.rgb) / lb_l) : fp.rgb;
                centre += float4(fa.xyz * w, w);
                radius = max(radius, fa.w);
                if (fs.w < kSpotUnknown + 2.0f) {
                    spot = dot(lc.xyz, lc.xyz) > 0.25f ? float4(lc.xyz, fs.w - kSpotUnknown) : spot;
                } else {
                    spot = fs;
                    spot_known = true;
                }
            }
        }
        if (centre.w > 0.0f) {
            // Seen: brighter at once, dimmer slowly (a partly hidden glow shows less of itself).
            lb.rgb = lerp(lb.rgb, power, luminance(power) > lb_l ? 0.25f : 0.08f);
            la = float4(lerp(la.xyz, centre.xyz / centre.w, 0.5f), lerp(la.w, radius, 0.5f));
            lb.w = 0.0f;
            // A spot turns smoothly; one seen as a point light is one.
            const bool both = spot_known && dot(lc.xyz, lc.xyz) > 0.25f;
            lc = both ? float4(normalize(lerp(lc.xyz, spot.xyz, 0.5f)), spot.w) : spot;
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
            // Out of view and too far to light anything near the camera: forgotten within seconds
            // (the list keeps room for the lights that matter here).
            const float reach = sqrt(luminance(lb.rgb) / (kPi * kEmitForget));
            const bool far = length(e) > reach + 3000.0f;
            lb.rgb *= gone ? 0.5f : far ? 0.9f : lb.w > 1800.0f ? 0.97f : 1.0f;
        }
        if (luminance(lb.rgb) < 1.0f) {
            la = float4(0.0f, 0.0f, 0.0f, 0.0f);
            lb = float4(0.0f, 0.0f, 0.0f, 0.0f);
            lc = float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }
    e_lights[gi * 3u] = la;
    e_lights[gi * 3u + 1u] = lb;
    e_lights[gi * 3u + 2u] = lc;
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
                const float4 fs = asfloat(e_found.Load4(16u + n * kEmitStride + 32u));
                e_lights[slot * 3u] = asfloat(e_found.Load4(16u + n * kEmitStride));
                e_lights[slot * 3u + 1u] = float4(asfloat(e_found.Load3(16u + n * kEmitStride + 16u)), 0.0f);
                e_lights[slot * 3u + 2u] = fs.w < kSpotUnknown + 2.0f ? float4(0.0f, 0.0f, 0.0f, 0.0f) : fs;
            }
        }
    }
    AllMemoryBarrierWithGroupSync();
    // The list: the lights in slot order with their cumulative power.
    const float4 fa = e_lights[gi * 3u];
    const float4 fb = e_lights[gi * 3u + 1u];
    const float4 fc = e_lights[gi * 3u + 2u];
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
        const uint at = 16u + (uint(s_emit_scan[gi].y) - 1u) * kEmitStride;
        e_list.Store4(at, asuint(fa));
        e_list.Store4(at + 16u, asuint(float4(fb.rgb, s_emit_scan[gi].x)));
        e_list.Store4(at + 32u, asuint(fc));
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

// The light a ray that reaches the sky brings, in units of the sky's mean (as sky_tint): the sky
// light of its eye-space direction, its colour saturated as the tint's, blended with the tint by
// g_sky_ibl. `sh`: g_sky_light's coefficients over its mean luminance (sky_light_load).
float3 sky_light(float3 direction, float3 tint, float3 sh[9], bool known)
{
    if (!known) {
        return tint;
    }
    const float3 d = normalize(direction.x * g_to_eye[0].xyz + direction.y * g_to_eye[1].xyz +
                               direction.z * g_to_eye[2].xyz);
    float basis[9];
    sh9(d, basis);
    float3 radiance = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint b = 0u; b < 9u; ++b) {
        radiance += sh[b] * basis[b];
    }
    radiance = max(radiance, 0.0f);
    const float l = luminance(radiance);
    const float3 color = l > 1e-3f ? lerp(float3(1.0f, 1.0f, 1.0f), radiance / l, g_sky_saturation)
                                   : float3(1.0f, 1.0f, 1.0f);
    return lerp(tint, color * clamp(l, 0.1f, 3.0f), g_sky_ibl);
}

bool sky_light_load(out float3 sh[9])
{
    const bool known = g_sky_ibl > 0.0f && g_sky_light.Load(112u) != 0u;
    const float mean = known ? asfloat(g_sky_light.Load(108u)) : 1.0f;
    [unroll] for (uint b = 0u; b < 9u; ++b) {
        sh[b] = known ? asfloat(g_sky_light.Load3(b * 12u)) / mean : float3(0.0f, 0.0f, 0.0f);
    }
    return known;
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

// A glow light's reach squared: where its light power / (pi d^2) falls to g_emit.x, at most
// g_emit.y. Beyond it the light counts for nothing.
float emit_reach2(float light)
{
    return min(light / (kPi * g_emit.x), g_emit.y * g_emit.y);
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

// ---------------------------------------------------------------------------------------------
// Tile light lists: each group of 8 x 8 samples keeps the glow lights whose reach meets the box
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

[numthreads(8, 8, 1)]
void TraceCS(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    const bool in_range = all(id.xy < g_sample_size);
    const uint index = id.y * g_sample_size.x + id.x;
    const int2 q = sample_pixel(min(id.xy, g_sample_size - 1u));
    float3 p;
    // Backdrops (sky domes and distant scenery far beyond the stage) are the sky: no lighting.
    const bool surface = pixel_point(q, p) && inside(g_viewport, float2(q) + 0.5f) && -p.z <= g_ambient.w;
    CULL_TILE_LIGHTS(g_emitters, g_to_eye, gi, in_range && surface, p, emit_reach2)
    if (!in_range) {
        return;
    }
    if (!surface) {
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

    // The glows (lamps, cores, Lumas, lava bursts): every light within reach of this point, one
    // kept in proportion to its unshadowed light here (streaming resampled importance over the
    // whole list: a Luma beside the surface wins over a brighter glow across the level), one
    // shadow ray to it. Its light joins the point lights' (their weight divided out).
    const uint emitters = min(s_tile_count, kTileLights);
    if (emitters != 0u) {
        float sum = 0.0f;
        float pick = min(sample_point(id.xy, max(g_rays, 1u) + 7u).x, 0.99999f);
        float4 kept = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float3 kept_power = float3(0.0f, 0.0f, 0.0f);
        [loop] for (uint k = 0u; k < emitters; ++k) {
            const uint c = s_tile_lights[k];
            const float4 la = asfloat(g_emitters.Load4(16u + c * kEmitStride));
            const float3 power = asfloat(g_emitters.Load3(16u + c * kEmitStride + 16u));
            const float light = luminance(power);
            const float3 e = transform(g_to_eye, la.xyz);
            const float3 to = e - p;
            const float d2 = dot(to, to);
            const float reach2 = emit_reach2(light);
            if (d2 >= reach2) {
                continue;
            }
            const float inv_d = rsqrt(max(d2, 1e-6f));
            const float nl = dot(n, to) * inv_d;
            if (nl <= 0.0f) {
                continue;
            }
            const float4 spot = asfloat(g_emitters.Load4(16u + c * kEmitStride + 32u));
            const float cone = spot_factor(float4(rotate3(g_to_eye, spot.xyz), spot.w), -to * inv_d);
            if (cone <= 0.0f) {
                continue;
            }
            const float x = d2 / reach2;
            const float weight = light * cone * nl * (1.0f - x * x) * (1.0f - x * x) / (d2 + la.w * la.w);
            sum += weight;
            // One uniform number serves the stream: kept with chance weight / sum, then rescaled
            // to stay uniform.
            const float chance = weight / sum;
            if (pick < chance) {
                kept = float4(e, la.w);
                kept_power = power;
                pick = pick / chance;
            } else {
                pick = (pick - chance) / max(1.0f - chance, 1e-6f);
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
                // power nl / (pi (d2 + r2)) (windowed to its reach) over its share of the weights.
                // Bright bursts (lava eruptions, sparks) saturate softly instead of blowing out.
                const float3 glow = kept_power / (kPi * lum) * sum * g_emit.z;
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
    float3 sky_sh[9];
    const bool sky_known = sky_light_load(sky_sh);
    float3 fallback = float3(g_fallback, g_fallback, g_fallback);
    {
        const uint4 scene = g_picture_sums.Load4(16u);
        if (scene.w != 0u) {
            fallback = float3(scene.xyz) / (255.0f * float(scene.w)) * (g_fallback / 0.55f);
        }
    }
    float3 light = float3(0.0f, 0.0f, 0.0f);
    float3 emitted = float3(0.0f, 0.0f, 0.0f);
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
            float3 sent = hit_light(origin + ray.Direction * t, fallback);
            const float near = lerp(g_contact, 1.0f, saturate(t / g_contact_range));
            // Distant hits fade into the open sky (the game's lighting already holds them).
            const float distant = saturate((t - 0.6f * g_range) / (0.4f * g_range));
            // A glowing surface (unlit, bright beyond its colour under the capture's emitter gain,
            // saturated: lava, magma, fire) sends its own light, whatever the sky: it joins the
            // point lights' (not scaled by the sky's state), without contact darkening.
            const uint hit_flags = g_surfaces.Load(query.CommittedPrimitiveIndex() * 16u + 12u);
            const uint2 hit_emission =
                g_draws.Load2(min(hit_flags >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 8u);
            if ((hit_emission.x & kEmitNamed) != 0u) {
                // Named by the material table: an area light (lava, magma, a glowing panel) sends all of
                // its light as it shows, times its intensity; anything else none of its own.
                if ((hit_emission.x & 3u) == kEmitArea && g_emit.w > 0.0f) {
                    // Its radiance from the table (its colour times its intensity): the picture
                    // holds lava at 8 bits, far below the light it sends.
                    const float3 radiance = max(sent, rgb8(hit_emission.x) * g_self.z);
                    emitted += radiance * (asfloat(hit_emission.y) * (1.0f - distant));
                    sent = float3(0.0f, 0.0f, 0.0f);
                }
            }
            light += lerp(sent * near, sky_light(direction, tint, sky_sh, sky_known), distant);
        } else {
            light += sky_light(direction, tint, sky_sh, sky_known);
        }
    }
    g_z[index] = p.z;
    g_normal[index] = pack_normal(n);
    g_primary[index] = primary;
    // The glowing surfaces' light (cosine-weighted rays: their mean is the irradiance over pi),
    // saturating softly like the glow lights'.
    if (any(emitted > 0.0f)) {
        const float3 surface = emitted / float(rays) * g_emit.w;
        points += surface / (1.0f + luminance(surface) / 8.0f) / max(g_point_light, 1e-3f);
    }
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
        g_final[index] = uint4(f32tof16(1.0f) | (f32tof16(1.0f) << 16u), f32tof16(1.0f) | (f32tof16(1.0f) << 16u), 0u, 0u);
        return;
    }
    Light light = filtered(id.xy, 4, true);
    if ((g_flags & 128u) != 0u) {
        light.sky = float3(1.0f, 1.0f, 1.0f);  // GALAXY_RT_GI=0: the sky open, no bounce
    }
    const float3 sun = light.sun * g_sun_light.rgb;
    const float3 points = light.points * g_point_light;
    // The sky and bounce light follow the sky: under a dark sky the lights carry the picture.
    float ambient = g_sun_light.w;
    if (g_picture_sums.Load(36u) != 0u) {
        ambient *= lerp(g_ambient.x, 1.0f, saturate(asfloat(g_picture_sums.Load(32u)) / g_ambient.y));
    }
    // Unlit and de-lit surfaces: the game painted them for full light. Under a bright sky a face
    // square to the sun with the sky open shows its colour as drawn and shade falls to the sky's
    // share (key to fill about 3:1, the sun carries the day); under a dark sky the light is taken
    // against a mean facing of 0.55 (lamps and glows carry the night).
    const float day_sky = g_picture_sums.Load(36u) != 0u
                              ? saturate(asfloat(g_picture_sums.Load(32u)) / max(g_ambient.y, 1e-3f))
                              : 1.0f;
    const float norm = max(g_sun_light.w + lerp(0.55f, 1.0f, day_sky) * luminance(g_sun_light.rgb), 0.25f);
    float3 lighting = (ambient * light.sky + sun) / norm + points;
    // With the sky open: what the picture capture keeps (no feedback through the sky and bounce).
    float3 open = (ambient + sun) / norm + points;
    const float exposure = g_picture_sums.Load(52u) != 0u ? asfloat(g_picture_sums.Load(48u)) : 1.0f;
    // A surface the game lights already turns its shading towards its lights: the traced light
    // shadows it (the ratio to the same light unshadowed). The point lights add to it, or, where
    // the game lights it with them too (GX light 4), shadow it likewise.
    const uint tri = g_primary[index].x;
    const uint draw_flags = tri != 0u ? g_surfaces.Load((tri - 1u) * 16u + 12u) : 0u;
    const bool delit = (draw_flags & kDrawDelit) != 0u;
    const bool point_lit = (draw_flags & kDrawPointLit) != 0u && g_point_count != 0u;
    if (delit) {
        // Its colour is its material: the traced light is its light (the unlit form above).
    } else if ((draw_flags & (kDrawLit | kDrawSunKey)) == (kDrawLit | kDrawSunKey)) {
        // Its key light lies along the traced sun: the game's shading is its ambient A plus its GX
        // light 0 colour K times n.l (and its point light), clamped to 1 like GX's accumulator; the
        // traced light takes A times the sky's light (as the sky state scales it) and K times the
        // traced sun.
        const float3 n = unpack_normal(g_normal[index]);
        const float facing = saturate(dot(n, g_sun.xyz));
        const uint4 colors = g_draws.Load4(min(draw_flags >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride);
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
    // A fill keeps shade readable, as a sky light's minimum does: under a dark sky more of it.
    {
        const float day = g_picture_sums.Load(36u) != 0u
                              ? saturate(asfloat(g_picture_sums.Load(32u)) / max(g_ambient.y, 1e-3f))
                              : 1.0f;
        const float fill = g_fill * lerp(1.5f, 1.0f, day);
        lighting = fill + (1.0f - fill) * lighting;
        open = fill + (1.0f - fill) * open;
    }
    lighting *= exposure;
    open *= exposure;
    // A small light source's halo: its share of the picture that joins the bloom (CaptureCS).
    float halo = 0.0f;
    // The sun's share of the light for the composite's per-pixel relief: its visibility (the filtered
    // sun over the sample's n.l) times the draw's relief, at the scale the light took here.
    float relief = 0.0f;
    // Map geometry only (unlit draws: ground, rock, walls); characters and objects the game lights keep
    // their painted detail flat (their emblems and seams would emboss).
    if (tri != 0u && (draw_flags & (kDrawLit | kDrawDelit)) == 0u) {
        const float strength = asfloat(g_draws.Load(min(draw_flags >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 28u));
        const float nl = dot(unpack_normal(g_normal[index]), g_sun.xyz);
        if (strength > 0.0f && nl > 0.05f) {
            const float day = g_picture_sums.Load(36u) != 0u
                                  ? saturate(asfloat(g_picture_sums.Load(32u)) / max(g_ambient.y, 1e-3f))
                                  : 1.0f;
            const float fill = g_fill * lerp(1.5f, 1.0f, day);
            // At the scale the sun took in the light (its normalisation, norm).
            relief = strength * saturate(light.sun / nl) * (1.0f - fill) * exposure * g_strength / norm;
        }
    }
    float3 m = lerp(float3(1.0f, 1.0f, 1.0f), lighting, g_strength);
    // The composite's film shoulder (apply_light) rolls bright light off; here only a bound.
    m = min(m, kMaxLight);
    // The material table's light sources glow from their own surfaces, whatever reaches them: lava
    // and magma red-hot (area lights), Lumas, star bits, stars and fire enemies in their own colours
    // (point lights). The composite's shoulder takes them towards white-hot; the capture passes
    // what it rolls off to the bloom.
    if (tri != 0u) {
        const uint record = min(draw_flags >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride;
        const uint2 emission = g_draws.Load2(record + 8u);
        const uint kind = emission.x & 3u;
        if ((emission.x & kEmitNamed) != 0u && kind != 0u && (emission.x & kEmitNoLight) == 0u) {
            // Its own surface's brightness: the table's glow, else its light's intensity.
            const uint table_flags = g_draws.Load(record + 24u);
            const float intensity = (table_flags & kDrawTableGlow) != 0u
                                        ? float(table_flags >> 16u) / 65535.0f * kDrawGlowScale
                                        : asfloat(emission.y);
            if (kind != kEmitArea) {
                halo = g_self.y * min(intensity, 3.0f);
            }
            const float glow = kind == kEmitArea ? g_self.x * intensity : 0.0f;
            if (glow > 0.0f) {
                // In its own colour (lava stays red-orange; white for the draw-tinted ones).
                const float3 tint = rgb8(emission.x);
                const float3 hue = tint / max(max(tint.r, tint.g), max(tint.b, 1e-3f));
                m = min(max(m, 1.0f) + glow * hue, kMaxLight);
            }
        }
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
    const float keep = max(luminance(without) / max(luminance(m), 1e-3f), 1e-3f);
    g_final[index] = uint4(f32tof16(m.r) | (f32tof16(m.g) << 16u), f32tof16(m.b) | (f32tof16(keep) << 16u), asuint(z),
                           f32tof16(relief) | (f32tof16(halo) << 16u));
}

// ---------------------------------------------------------------------------------------------
// The lighting as a texture for the translucent draws after the trace (one texel per sample).
// ---------------------------------------------------------------------------------------------

cbuffer RtLightTexture : register(b0)
{
    uint2 t_size;
    uint2 t_pad;
};
StructuredBuffer<uint4> t_final : register(t0);

float4 LightTexturePS(float4 position : SV_Position) : SV_Target0
{
    const uint2 s = min(uint2(position.xy), t_size - 1u);
    const uint4 v = t_final[s.y * t_size.x + s.x];
    if (asfloat(v.z) == 0.0f) {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    return float4(f16tof32(v.x), f16tof32(v.x >> 16u), f16tof32(v.y), 1.0f);
}

// ---------------------------------------------------------------------------------------------
// Composite: the EFB colour (a copy of it) under the lighting. Less light darkens it in
// proportion; more light brightens it in proportion up to a knee, then rolls off towards white
// without passing it (apply_light).
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
    float4 m_sun;        // towards the traced sun (eye space, traced frame); relief height (eye units)
    float4 m_sun_light;  // its light (rgb)
};
StructuredBuffer<uint4> m_light : register(t0);
Texture2D<float> m_efb_depth : register(t1);
StructuredBuffer<float4> m_reproject : register(t2);
Texture2D<float4> m_color : register(t3);

struct CompositeOut
{
    float4 color : SV_Target0;
};

float3 apply_light(float3 c, float3 m)
{
    return film_light(c, m);
}

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
    const float3 color = m_color.Load(int3(int2(position.xy), 0)).rgb;
    result.color = float4(color, 1.0f);
    if (far_plane(m_depth, depth) || !inside(m_viewport, position.xy)) {
        if ((m_flags & 2u) != 0u) {
            result.color = float4(0.5f, 0.5f, 0.5f, 1.0f);
            return result;
        }
        discard;
    }
    if (position.x < m_split + 1.0f) {
        if (position.x < m_split - 1.0f) {
            discard;
        }
        result.color = float4(color * 0.3f, 1.0f);
        return result;
    }
    float3 p = eye_position(m_projection, m_viewport, position.xy, z);
    const float3 here = p;
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
    float relief_sum = 0.0f;
    float weight = 0.0f;
    float3 nearest = float3(1.0f, 1.0f, 1.0f);
    float nearest_dz = abs(p.z) * 0.1f;
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 o = int2(k & 1u, k >> 1u);
        const int2 s = clamp(base + o, int2(0, 0), int2(m_sample_size) - 1);
        const uint4 sample = m_light[uint(s.y) * m_sample_size.x + uint(s.x)];
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
        relief_sum += f16tof32(sample.w & 0xFFFFu) * bw;
        weight += bw;
    }
    float3 m = clamp(weight > 1e-3f ? sum / weight : nearest, 0.0f, kMaxLight);
    // Relief: the texture's detail as height (its brightness, m_sun.w eye units at white) bends the
    // pixel's normal (the surface gradient of the faceted normal from depth, Mikkelsen 2010); the
    // sun's light follows the bent normal where the sample sees the sun. Across a depth edge that
    // side's difference is left out; with distance a pixel spans more than the detail and it fades.
    const float relief = weight > 1e-3f ? relief_sum / weight : 0.0f;
    if (relief > 1e-3f && m_sun.w > 0.0f) {
        const int2 c = int2(position.xy);
        float zs[4];
        float hs[4];
        const int2 steps[4] = {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)};
        [unroll] for (uint k = 0u; k < 4u; ++k) {
            const int2 q = c + steps[k];
            zs[k] = depth_to_eye_z(m_depth, m_efb_depth.Load(int3(q, 0)));
            hs[k] = luminance(m_color.Load(int3(q, 0)).rgb);
        }
        const float limit = abs(z) * 0.01f + 0.5f;
        const bool okx = abs(zs[0] - z) < limit && abs(zs[1] - z) < limit;
        const bool oky = abs(zs[2] - z) < limit && abs(zs[3] - z) < limit;
        if (okx && oky) {
            const float3 px = eye_position(m_projection, m_viewport, position.xy + float2(1.0f, 0.0f), zs[0]);
            const float3 py = eye_position(m_projection, m_viewport, position.xy + float2(0.0f, 1.0f), zs[2]);
            const float3 dpdx = px - here;
            const float3 dpdy = py - here;
            float3 nf = cross(dpdx, dpdy);
            const float nf_length = length(nf);
            if (nf_length > 1e-8f) {
                nf /= nf_length;
                nf = dot(nf, here) > 0.0f ? -nf : nf;
                const float dhdx = 0.5f * (hs[0] - hs[1]) * m_sun.w;
                const float dhdy = 0.5f * (hs[2] - hs[3]) * m_sun.w;
                const float3 r1 = cross(dpdy, nf);
                const float3 r2 = cross(nf, dpdx);
                const float det = dot(dpdx, r1);
                const float3 grad = (det >= 0.0f ? 1.0f : -1.0f) * (dhdx * r1 + dhdy * r2);
                const float3 nb = normalize(abs(det) * nf - grad);
                // Fades where a pixel spans more than a few relief heights (minified detail shimmers).
                const float span = length(dpdx);
                const float fade = saturate(2.0f - span / (2.0f * m_sun.w));
                // Sub-frames: this frame's normals turned to the traced frame's camera (its sun).
                float3 nfa = nf;
                float3 nba = nb;
                if ((m_flags & 1u) != 0u) {
                    const uint row = m_slot * 3u;
                    const float4 rows[3] = {m_reproject[row], m_reproject[row + 1u], m_reproject[row + 2u]};
                    nfa = float3(dot(rows[0].xyz, nf), dot(rows[1].xyz, nf), dot(rows[2].xyz, nf));
                    nba = float3(dot(rows[0].xyz, nb), dot(rows[1].xyz, nb), dot(rows[2].xyz, nb));
                }
                const float delta = clamp(dot(nba, m_sun.xyz) - dot(nfa, m_sun.xyz), -0.25f, 0.25f);
                m = max(m + relief * fade * delta * m_sun_light.rgb, 0.0f);
            }
        }
    }
    if ((m_flags & 2u) != 0u) {
        result.color = float4(m * 0.5f, 1.0f);
        return result;
    }
    result.color = float4(apply_light(color, m), 1.0f);
    return result;
}
