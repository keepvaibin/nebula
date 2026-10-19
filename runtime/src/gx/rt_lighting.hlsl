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

// Point lights per frame (rt_reflections.h kRtPointLights; g_points holds two float4 each).
static const uint kPointLights = 16u;

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
    uint g_point_count;         // point lights in g_points (0-kPointLights)
    float g_point_light;        // their weight
    float g_point_size;         // radius of the geometry around a light (its emitter)
    float g_sun_reach;          // length of a sun ray (eye units)
    float4 g_points[32];        // per light (kPointLights): eye position (xyz), reach (w); colour (rgb),
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
                                // intensity; an area light's radiance on what sees it per unit; the
                                // stage's day model share (rt-suns.tsv day; < 0: by its sky)
    float4 g_ao;                // contact occlusion: reach (eye units), strength by day, strength at
                                // night (0: off); the night model's most light the lights add (soft)
    float4 g_night;             // night model: the open art's level, the sky occlusion's depth, the cast
                                // shadows' depth, the traced lights' gain
    float4 g_night2;            // the night key's gain on what it reaches; by day the environment's level
    uint4 g_star_first[2];      // the material table's stars among the scene draws (kStarSlots): first
    uint4 g_star_count[2];      // triangle and count in g_positions (0: none)
    uint g_core_point;          // the star core's light among g_points (~0: none)
    float g_sun_cap;            // the night's soft cap on added light where the core's light rules
    uint2 g_core_pad;
    float4 g_point_radius[kPointLights / 4u];   // per point light (4 a vector): its emitter's radius
    uint g_reference_bounces;   // the reference's bounces (ReferenceCS)
    float g_reference_exposure; // its exposure over the geometric mean of its light
    uint g_reference_reset;     // 1: it starts again this frame (the camera moved)
    uint g_reference_pad;
    float4 g_history_projection;   // the previous samples' projection (their frame's jitter)
    float4 g_picture_projection;   // the previous picture's
    float4 g_sky_radiance;         // kPhysicalFlag: the sky's radiance, the stage's ambient in linear light (rgb)
    float4 g_full_light;           // the stage's full light in linear light (rgb); 1: g_sky_map holds the traced sky (w)
    uint4 g_tev_info;              // TEV records (g_tev) of the scene's draws, of the sky's draws (the sky probe)
    float4 g_up;                   // the player's up in eye space by the game's gravity (else the view's up); the de-light
                                   // maps' relief height in texels per log unit (w; 0: off)
    uint4 g_probe;                 // GALAXY_RT_PROBE: the probed sample (x, y), on (z)
};

// g_flags bit 14: the game's light units (GALAXY_RT_PHYSICAL), as the stage sets them in linear light (white 1):
// g_sky_radiance is the sky, g_sun_light the sun's irradiance over pi, and the reference keeps them as they are (no
// exposure).
static const uint kPhysicalFlag = 16384u;

RaytracingAccelerationStructure g_scene : register(t6);
// Texture de-lighting (rt_delight.hlsl): its maps hold the light painted into a texture in log units, R8_SNORM over
// +-kDelightRange.
static const float kDelightRange = 1.25f;
#if RT_TEXTURED
// Alpha-tested geometry (g_flags bit 12): per triangle its texture coordinates and colours (36 bytes,
// ray_tracing.hlsl g_tri_uv), per draw its alpha map and the alpha its test passes from (word 3: the map's view
// in the low 24 bits, the alpha in the high 8; 0: solid), sampled from the descriptor heap.
ByteAddressBuffer g_tri_uv : register(t20);
StructuredBuffer<uint4> g_draw_tex : register(t21);
SamplerState g_wrap_point : register(s0);
SamplerState g_wrap_linear : register(s1);
// Per draw its TEV program for ray hits (rt_tev.hlsli; g_tev_info.x records of the scene, .y of the sky in the probe).
ByteAddressBuffer g_tev : register(t23);
#include "rt_tev.hlsli"
#endif
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
// The reference (ReferenceCS): per sample its summed light and paths, then its depth at the start; after the samples
// its exposure.
RWStructuredBuffer<float4> g_reference : register(u6);
// The traced sky (kPhysicalFlag, g_full_light.w): octahedral over world directions, +y the pole, kSkyMapSize^2 texels of
// the sky's light in units of the stage's full light (linear), then its 9 spherical-harmonic coefficients (rgb).
// SkyProbeCS and SkyTraceShCS write it (g_reference's slot).
StructuredBuffer<float4> g_sky_map : register(t22);
// kPhysicalFlag: per EFB pixel its shading normal (pack_normal) and the texture's de-lighting there
// (pack_pixel_delight; 0: none), GBufferCS, for the composite's per-pixel sun and albedo.
RWStructuredBuffer<uint2> g_pixels : register(u7);
static const uint kSkyMapSize = 64u;
static const uint kSkyLayers = 8u;     // the sky's layers a probe ray composites, at most

static const float kPi = 3.14159265f;
static const uint kDrawLit = 8u;    // RtDraw.flags: GX-lit colour
static const uint kDrawSunKey = 16u;    // RtDraw.flags: its GX light 0 lies along the traced sun
static const uint kDrawPointLit = 32u;  // RtDraw.flags: lit by GX light 4 (the game's point light)
// RtDraw.flags: its colour is its material alone (the game's lights removed): the traced light is
// its whole light, as on unlit surfaces.
static const uint kDrawDelit = 4096u;
static const uint kDrawNoShadow = 32768u;   // RtDraw.flags: the table's shadow 0 (lets light through)
// g_draws word 2 (SceneRayTracer::add_draw): the material table's emission, colour RGBA8 with these
// in the low byte; word 3: its intensity.
static const uint kEmitArea = 2u;       // low 2 bits: 1 point light, 2 area light
static const uint kEmitNamed = 4u;      // the table names this draw's texture
static const uint kEmitNoLight = 8u;    // never a light
// g_draws records (SceneRayTracer::draw_colors_): ambient, GX light 0 colour, emission, intensity,
// roughness and metalness, material colour, flags, relief; GX light 0's eye position (3 floats), its kind
// (0 none, 1 no facing term, 2 n.l).
static const uint kDrawStride = 48u;
// g_draws word 6: the table gives the light source's own glow (in place of its intensity), unorm16 of
// glow / kDrawGlowScale in the high half.
static const uint kDrawTableGlow = 2u;
static const uint kDrawTableFoliage = 4u;   // g_draws word 6: alpha-cut foliage cards
static const uint kDrawTableStar = 8u;      // g_draws word 6: a star's burning surface (the star core)
// Stars whose balls StarSphereCS fits (g_star_first): one float4 each (eye-space centre, radius; 0: none)
// after the samples in the results (g_final, m_light).
static const uint kStarSlots = 8u;
// The picture sums (CaptureCS c_sums): per star slot the game's own colour of its samples, r g b n (x 1/255,
// cleared per capture), then the stars' colour over time, rgb (float) and known (SkyStateCS).
static const uint kStarSums = 64u;
static const uint kStarColour = kStarSums + kStarSlots * 16u;
// Scene instance masks (ray_tracing.cpp kSceneMask): the frame's geometry. The late geometry (instance 1:
// drawn after the trace, Rosalina) has no records in g_positions or g_surfaces: shadow, sky and reflection
// rays meet it as an occluder.
static const uint kSceneMask = 1u;
// g_draws word 6: an alpha-tested card (grass, flowers), traced whole (card_blocks decides its hits).
static const uint kDrawTableCard = 16u;
static const float kDrawGlowScale = 16.0f;
// A glowing plant's (an area light of the table's foliage) halo share per unit of its glow.
static const float kPlantHalo = 2.0f;
// RtDraw.flags bits 8-11 (rt_surfaces.h RtSurfaceClass::Crystal): clear crystal, glass and gems.
static const uint kClearClass = 7u;
// The lighting multiplies the game's colour by at most this; the composite rolls it off on a film
// shoulder from kLitKnee (apply_light).
static const float kMaxLight = 8.0f;
// Daytime (SpatialBCS): a cast shadow's depth, the sun's darkening of faces turned from it, the sky
// occlusion's depth and the sky colour's share in shade. Faces turned from the sun at 0.7 of the sunlit ones
// and shade 40 % towards the sky's cool hue: the ground reads lit by a sun, not dimmed (user, v53a).
static const float kDayShadow = 0.35f;
static const float kDayForm = 0.3f;
static const float kDayOcclusion = 0.5f;
static const float kDayTint = 0.4f;
// Daylight's level: open sunlit art at 0.95 of the game's (never above it); cast shadows and occlusion
// carry the depth. 0.8-0.85 were set while the finish lifted every picture x(1 + x) (build 46); without
// that lift they left Beach Bowl's sand, water and grass a flat 0.82 of the game (fc46).
static const float kDayLevel = 0.95f;
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
    // Below its knee a channel keeps its light; the shoulder's exponent only for the part above it (a white
    // texel's room is 1e-4: exp of the part below overflowed to inf, and lerp(lit, -inf, 0) is NaN, black).
    const float3 channels = lerp(lit, knees + rooms * (1.0f - exp(-max(lit - knees, 0.0f) / rooms)), step(knees, lit));
    return lerp(hue, channels, smoothstep(1.6f, 3.5f, peak));
}
// Buckets of the exposure measurement (16 bytes each: lit, game, pixels, spare).
static const uint kExposureBuckets = 16u;
// A light of the glow lights' lists: position, radius; power, frames unseen or cumulative power; spot
// direction (world; 0 0 0 a point light), cosine of its cone.
static const uint kEmitStride = 48u;
// Found lights per frame (ray_tracing.cpp kEmitMaxTiles). After them the found list holds, per found light, its
// source for the emitter log (GALAXY_RT_LOG_EMITTERS: its pass, and for the emitter passes its draw) and its
// identity across frames (emit_identity: its material and cluster cell; 0 none), which EmitMergeCS matches.
static const uint kEmitFound = 2048u;
// A particle's light: a fifth of a Luma or less (glow_light's cap on the table's particles), as a share of
// e_authored_power. Sparkles spawn and die every frame: such a light follows them slowly (EmitMergeCS).
static const float kParticleLight = 0.2f * 1.01f;
static const uint kEmitSourceSurface = 1u << 30u;
static const uint kEmitSourceGlow = 2u << 30u;
static const uint kEmitSourceTable = 3u << 30u;

uint emit_source_at(uint index)
{
    return 16u + kEmitFound * kEmitStride + index * 8u;
}

// A light's identity from its draw's material table entry and its cluster's cell: the same lamp, Luma or star from
// frame to frame while it stays in its cell (never 0).
uint emit_identity(uint material, uint cell)
{
    const uint h = (material * 0x9E3779B1u) ^ (cell * 0x85EBCA6Bu);
    return h != 0u ? h : 1u;
}
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

// Strong light rolled off softly: c ln(1 + l / c) in luminance, its hue kept. Weak light passes unchanged and strong
// light keeps a falloff all the way to its source. The rational l / (1 + l / c) it replaced flattened a lamp's light
// into a disc with a rim (user, 2026-10-17: a harsh, clear radius around the lights).
float3 soft_light(float3 light, float c)
{
    const float l = luminance(light);
    return l > 1e-6f ? light * (c * log(1.0f + l / c) / l) : light;
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

// A pixel's de-lighting (GBufferCS): the normal its texture's relief bends (octahedral, 10 bits an axis) and the light
// painted into its texel (log units, 12 bits over +-kDelightRange, never 0: 0 is none).
uint pack_pixel_delight(float3 n, float shading)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 e = n.xy;
    if (n.z < 0.0f) {
        e = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
    }
    const uint2 u = uint2(round(saturate(e * 0.5f + 0.5f) * 1023.0f));
    const uint s = uint(clamp(round(shading / kDelightRange * 2047.0f) + 2048.0f, 1.0f, 4095.0f));
    return u.x | (u.y << 10u) | (s << 20u);
}

float3 unpack_pixel_normal(uint v)
{
    const float2 e = float2(float(v & 0x3FFu), float((v >> 10u) & 0x3FFu)) / 1023.0f * 2.0f - 1.0f;
    float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
    const float t = saturate(-n.z);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return normalize(n);
}

float unpack_pixel_shading(uint v)
{
    return (float(v >> 20u) - 2048.0f) / 2047.0f * kDelightRange;
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
                             // bit 2: the sky's colour goes to c_sky by world direction; bit 3: a
                             // new stage (SkyStateCS forgets the stars' colour)
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
                             // (a new stage) (bits 0-1); GlowBaseCS: bit 0 measure it, bit 2 through the finish's
                             // tone; both: bit 3 physical (eye adaptation in linear light)
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
            // Night only: by day the sun glare sprites over bright sand made it a light.
            const float day = c_sums.Load(36u) != 0u ? saturate(asfloat(c_sums.Load(32u)) / c_daylight) : 1.0f;
            const float glowing = smoothstep(0.1f, 0.4f, added) *
                                  smoothstep(0.55f, 0.85f, max(base.r, max(base.g, base.b))) *
                                  (1.0f - smoothstep(0.35f, 0.8f, day));
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
            if (at < kEmitFound) {
                c_found.Store4(16u + at * kEmitStride, asuint(float4(transform(c_to_world, centre), max(spread, 1.0f))));
                c_found.Store4(16u + at * kEmitStride + 16u, asuint(float4(power, 0.0f)));
                c_found.Store4(16u + at * kEmitStride + 32u, uint4(0u, 0u, 0u, 0u));
                c_found.Store2(emit_source_at(at), uint2(kEmitSourceSurface, 0u));
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
    if ((c_exposure & 3u) == 2u) {
        c_sums.Store(36u, 0u);
    }
    const uint4 sky = c_sums.Load4(0u);
    if (sky.w * 100u > c_sample_size.x * c_sample_size.y) {
        const float l = luminance(float3(sky.xyz) / (255.0f * float(sky.w)));
        const float state = c_sums.Load(36u) != 0u ? lerp(asfloat(c_sums.Load(32u)), l, 0.05f) : l;
        c_sums.Store2(32u, uint2(asuint(state), 1u));
    }
    // The stars' colour over time (the star core's light; a star's reflection where none is in view): the
    // game's own colour of this frame's star samples, followed a tenth of the way per frame.
    if ((c_divide & 8u) != 0u) {
        c_sums.Store(kStarColour + 12u, 0u);
    }
    uint4 stars = uint4(0u, 0u, 0u, 0u);
    [unroll] for (uint s = 0u; s < kStarSlots; ++s) {
        stars += c_sums.Load4(kStarSums + s * 16u);
    }
    if (stars.w >= 4u) {
        const float3 seen = float3(stars.xyz) / (255.0f * float(stars.w));
        const float3 state = c_sums.Load(kStarColour + 12u) != 0u
                                 ? lerp(asfloat(c_sums.Load3(kStarColour)), seen, 0.1f) : seen;
        c_sums.Store4(kStarColour, uint4(asuint(state), 1u));
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
    if ((c_exposure & 3u) == 2u) {
        measured = uint3(0u, 0u, 0u);
        c_sums.Store2(48u, uint2(asuint(1.0f), 1u));
    }
    // For [rt-emit]: lit and game sums, pixels.
    c_sums.Store(44u, measured.z);
    c_sums.Store2(56u, measured.xy);
    const uint grid = max(((c_sample_size.x + 7u) / 8u) * ((c_sample_size.y + 7u) / 8u), 1u);
    if ((c_exposure & 3u) == 0u) {
        c_sums.Store2(48u, uint2(asuint(1.0f), 0u));
    } else if ((c_exposure & 8u) != 0u) {
        // Physical (bit 3): the exposure is the camera's own (physical_exposure); the measurement stays for [rt-emit].
        c_sums.Store2(48u, uint2(asuint(1.0f), 0u));
    } else if (measured.z * 50u > grid && measured.y != 0u) {
        const float ratio = float(measured.x) / float(measured.y);
        const float day = c_sums.Load(36u) != 0u ? saturate(asfloat(c_sums.Load(32u)) / c_daylight) : 1.0f;
        const float target = lerp(c_exposure_night, c_exposure_day, day);
        const float exposure = c_sums.Load(52u) != 0u ? asfloat(c_sums.Load(48u)) : 1.0f;
        const float goal = clamp(ratio, lerp(0.97f, 0.7f, day) * target, 1.12f * target);
        const float next = clamp(exposure * lerp(1.0f, goal / max(ratio, 0.05f), 0.1f), 0.35f, 1.0f);
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
        // Under a bright sky the glows' bloom at 75 % (the game's sun glare sprites veiled daylight; star
        // bits, Lumas and lamps keep most of their halo).
        const float day_glow = c_sums.Load(36u) != 0u ? saturate(asfloat(c_sums.Load(32u)) / c_daylight) : 1.0f;
        c_glow[index] = pack_r11g11b10(max(color - unpack_r11g11b10(c_glow_base[index]), 0.0f) *
                                       (1.0f - 0.25f * day_glow));
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
            // A star's sample (its relief share carries the mark, TraceCS), one in four: the game's own colour.
            const float mark = f16tof32(f.w & 0xFFFFu);
            if (mark < -0.5f && ((id.x | id.y) & 1u) == 0u && float(q.x) >= c_split) {
                const uint star = uint(round(-mark - 1.0f));
                if (star < kStarSlots) {
                    const uint3 own = uint3(round(saturate(c_game.Load(int3(q, 0)).rgb) * 255.0f));
                    const uint at = kStarSums + star * 16u;
                    c_sums.InterlockedAdd(at, own.r);
                    c_sums.InterlockedAdd(at + 4u, own.g);
                    c_sums.InterlockedAdd(at + 8u, own.b);
                    c_sums.InterlockedAdd(at + 12u, 1u);
                }
            }
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
                const float day_excess = c_sums.Load(36u) != 0u ? saturate(asfloat(c_sums.Load(32u)) / c_daylight) : 1.0f;
                const float3 excess = max(lit - film_light(game, m), 0.0f) *
                                      smoothstep(1.4f, 2.5f, max(lit.r, max(lit.g, lit.b))) * (1.0f - 0.85f * day_excess);
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
// Glow lights: the game's additive translucent draws whose texture the material table names as a
// light (lamp halos, fire, lava sparks, electricity) are light sources; other glows are effects whose
// halo shows in the bloom only. Each glow draw's eye-space triangles (the geometry pass) and what the
// translucent phases added to the picture where they lie give the light's centre (weighted by that
// light) and spread; its power and colour are the table's. The lights are remembered in world space
// while out of view or hidden, and fade where their place is in view and no longer glows.
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
RWStructuredBuffer<float4> e_lights : register(u1);   // per light (kEmitLightVectors): position, radius; power,
                                                      // frames unseen; spot direction, cosine of its cone;
                                                      // identity (emit_identity), spare
RWByteAddressBuffer e_list : register(u2);            // count, total; per light position, radius;
                                                      // power, cumulative power; spot

static const uint kEmitLights = 512u;          // ray_tracing.cpp kEmitLights
static const float kEmitLargest = 700.0f;      // eye units: a glow wider than this is no light
static const float kEmitForget = 0.004f;       // a light no longer seen that cannot reach the camera's
                                               // surroundings (its light there below this) fades fast

groupshared float4 s_glow_a[64];
groupshared float4 s_glow_b[64];
groupshared float s_glow_area[64];

// Emitter clusters. One draw can hold many separate light sources: a model's lamp heads, a particle system's
// fireflies, the sparkles round a Luma. As one light they sat at their common centre, often in mid-air, and a
// spread-out set (fireflies over a garden) was taken for an atmosphere and lit nothing (user, 2026-10-17: every
// light source emits). Each emitter pass groups a draw's triangles by the world cell their centre lies in
// (kClusterCell units a side, up to kClusters cells; beyond that cells share a slot by hash), then joins cells
// whose extents come within kClusterGap of each other, so a light straddling a cell edge stays one light. Each
// joined set is one light, at the draw's full table intensity; a draw that was one light (a lamp, a Luma) still is.
// The first pass notes each triangle's slot (s_cluster_tri); each claimed slot then sums its own triangles in a
// fixed order (the same light every frame: summing by atomic adds in arrival order made the lights jitter).
static const uint kClusters = 64u;
static const float kClusterCell = 120.0f;
static const float kClusterGap = 30.0f;
static const uint kClusterNoted = 1024u;        // triangles whose slot is noted; a larger draw's further ones look it up

groupshared uint s_cluster_key[kClusters];      // per slot: its cell (cluster_key), 0 free
groupshared uint s_cluster_tri[kClusterNoted / 4u];   // per triangle: its slot + 1 (a byte; 0: no light)
groupshared uint s_cluster_used[kClusters];     // the claimed slots in order
groupshared uint s_cluster_count;
groupshared float4 s_cluster_a[kClusters];      // per claimed slot (by order): glows' power rgb, weight;
                                                // table lights' colour seen, area seen
groupshared float4 s_cluster_b[kClusters];      // weighted centre, weighted |centre|^2
groupshared float4 s_cluster_n[kClusters];      // table lights: face normal sum, facing evidence
groupshared float s_cluster_w[kClusters];       // the centre's weight (glows: power; table lights: area)
groupshared float s_cluster_area[kClusters];
groupshared float4 s_cluster_sphere[kClusters]; // centre, radius (negative: no light)
groupshared uint2 s_cluster_adj[kClusters];     // per slot (by order): the slots it joins, a bit each
groupshared uint s_cluster_root[kClusters];     // per slot (by order): the lowest slot of its joined set

// The cell of world position `w`, never 0.
uint cluster_key(float3 w)
{
    const int3 cell = int3(floor(w / kClusterCell));
    return ((uint(cell.x) & 0x3FFu) | ((uint(cell.y) & 0x3FFu) << 10u) | ((uint(cell.z) & 0x3FFu) << 20u)) + 1u;
}

uint cluster_home(uint key)
{
    return (key * 2654435761u) >> 26u;   // kClusters slots
}

// The slot holding the cell, a free one it claims, or with every slot taken its home slot.
uint cluster_claim(uint key)
{
    const uint home = cluster_home(key);
    [loop] for (uint i = 0u; i < kClusters; ++i) {
        const uint slot = (home + i) & (kClusters - 1u);
        uint previous;
        InterlockedCompareExchange(s_cluster_key[slot], 0u, key, previous);
        if (previous == 0u || previous == key) {
            return slot;
        }
    }
    return home;
}

// After every claim: the slot a cell's triangles add to.
uint cluster_find(uint key)
{
    const uint home = cluster_home(key);
    [loop] for (uint i = 0u; i < kClusters; ++i) {
        const uint slot = (home + i) & (kClusters - 1u);
        if (s_cluster_key[slot] == key) {
            return slot;
        }
    }
    return home;
}

// Every thread: before the claims (`count`: the draw's triangles; only their notes are cleared).
void cluster_begin(uint gi, uint count)
{
    if (gi < kClusters) {
        s_cluster_key[gi] = 0u;
    }
    for (uint i = gi; i < (min(count, kClusterNoted) + 3u) / 4u; i += 64u) {
        s_cluster_tri[i] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
}

// The first pass: triangle `t` of the draw (key: its cell, 0 for none) claims its slot and, among the first
// kClusterNoted, notes it.
void cluster_note(uint t, uint key)
{
    if (key == 0u) {
        return;
    }
    const uint slot = cluster_claim(key);
    if (t < kClusterNoted) {
        InterlockedOr(s_cluster_tri[t >> 2u], (slot + 1u) << ((t & 3u) * 8u));
    }
}

// After the claims: triangle `t`'s slot, kClusters for none (`key`: its cell, needed past kClusterNoted).
uint cluster_slot_of(uint t, uint key)
{
    if (t < kClusterNoted) {
        const uint noted = (s_cluster_tri[t >> 2u] >> ((t & 3u) * 8u)) & 0xFFu;
        return noted != 0u ? noted - 1u : kClusters;
    }
    return key != 0u ? cluster_find(key) : kClusters;
}

// Every thread: after the claims, the claimed slots in order (s_cluster_used, s_cluster_count).
void cluster_list(uint gi)
{
    GroupMemoryBarrierWithGroupSync();
    if (gi == 0u) {
        uint n = 0u;
        for (uint k = 0u; k < kClusters; ++k) {
            if (s_cluster_key[k] != 0u) {
                s_cluster_used[n] = k;
                ++n;
            }
        }
        s_cluster_count = n;
    }
    GroupMemoryBarrierWithGroupSync();
}

bool mask_test(uint2 mask, uint i)
{
    return ((i < 32u ? mask.x >> i : mask.y >> (i - 32u)) & 1u) != 0u;
}

uint2 mask_bit(uint i)
{
    return i < 32u ? uint2(1u << i, 0u) : uint2(0u, 1u << (i - 32u));
}

// Every thread, after the slots' sums: each slot's sphere (its weighted centre and the larger of its spread and half
// its area's side); slots whose spheres come within kClusterGap join, tested in parallel; thread 0 labels the joined
// sets (s_cluster_root: each set's lowest slot).
void cluster_join(uint gi, uint count)
{
    if (gi < count) {
        const float w = s_cluster_w[gi];
        if (w > 0.0f) {
            const float3 centre = s_cluster_b[gi].xyz / w;
            const float spread = sqrt(max(s_cluster_b[gi].w / w - dot(centre, centre), 0.0f));
            s_cluster_sphere[gi] = float4(centre, max(spread, 0.5f * sqrt(s_cluster_area[gi])));
        } else {
            s_cluster_sphere[gi] = float4(0.0f, 0.0f, 0.0f, -1.0f);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi < count) {
        const float4 si = s_cluster_sphere[gi];
        uint2 adjacent = uint2(0u, 0u);
        if (si.w >= 0.0f) {
            for (uint j = 0u; j < count; ++j) {
                const float4 sj = s_cluster_sphere[j];
                if (j != gi && sj.w >= 0.0f && distance(si.xyz, sj.xyz) < si.w + sj.w + kClusterGap) {
                    adjacent |= mask_bit(j);
                }
            }
        }
        s_cluster_adj[gi] = adjacent;
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi == 0u) {
        uint2 visited = uint2(0u, 0u);
        for (uint i = 0u; i < count; ++i) {
            if (mask_test(visited, i)) {
                continue;
            }
            uint2 set = mask_bit(i);
            uint2 frontier = set;
            [loop] while (any(frontier != 0u)) {
                uint2 next = uint2(0u, 0u);
                for (uint word = 0u; word < 2u; ++word) {
                    uint bits = word == 0u ? frontier.x : frontier.y;
                    [loop] while (bits != 0u) {
                        const uint k = firstbitlow(bits);
                        bits &= bits - 1u;
                        next |= s_cluster_adj[word * 32u + k];
                    }
                }
                frontier = next & ~set;
                set |= next;
            }
            for (uint word = 0u; word < 2u; ++word) {
                uint bits = word == 0u ? set.x : set.y;
                [loop] while (bits != 0u) {
                    const uint k = firstbitlow(bits);
                    bits &= bits - 1u;
                    s_cluster_root[word * 32u + k] = i;
                }
            }
            visited |= set;
        }
    }
    GroupMemoryBarrierWithGroupSync();
}

// e_draws records of the glow draws (SceneRayTracer::glow_lights_, 32 bytes): first triangle, count, the
// table's colour (RGBA8), its intensity; spare, pulse, flags, spare. Flags: the light scales by the
// intensity; it takes the table's colour.
static const uint kGlowTableIntensity = 4u;
static const uint kGlowTableColor = 8u;
static const uint kGlowLens = 16u;      // a black hole's lens draw: no light (rt_post.hlsl PostLensCS)
static const uint kGlowEffect = 32u;    // the table names it a particle: an effect, its halo in the bloom only

// A light's power by its size: a flame, a glow or a star of radius r lights its surroundings in proportion to
// its area (a 60-unit glow as the table gives it, a 500-unit fire at the limit). Characters and items (Lumas,
// coins) keep the table's power whatever their size (a big Luma at 6x washed the Observatory fountain, v53a).
float light_size(float radius, float limit)
{
    const float s = radius / 60.0f;
    return clamp(s * s, 1.0f, limit);
}

// The sums of 64 threads' s_glow_a, s_glow_b and s_glow_area in their first entries; every thread calls it.
void glow_reduce(uint gi)
{
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 32u; s > 0u; s >>= 1u) {
        if (gi < s) {
            s_glow_a[gi] += s_glow_a[gi + s];
            s_glow_b[gi] += s_glow_b[gi + s];
            s_glow_area[gi] += s_glow_area[gi + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

// A found light: position (world), radius; power, colour unknown; spot (kEmitStride); its source (kEmitSource*)
// and identity (emit_identity).
void emit_found(float4 at_radius, float4 power, float4 spot, uint source, uint identity)
{
    uint index;
    e_found.InterlockedAdd(0u, 1u, index);
    if (index < kEmitFound) {
        const uint at = 16u + index * kEmitStride;
        e_found.Store4(at, asuint(at_radius));
        e_found.Store4(at + 16u, asuint(power));
        e_found.Store4(at + 32u, asuint(spot));
        e_found.Store2(emit_source_at(index), uint2(source, identity));
    }
}

// An emitter draw's eye-space triangle (e_triangles); false for a degenerate or runaway one.
bool emit_triangle(uint index, out float3 v0, out float3 v1, out float3 v2)
{
    const uint at = index * 36u;
    v0 = asfloat(e_triangles.Load3(at));
    v1 = asfloat(e_triangles.Load3(at + 12u));
    v2 = asfloat(e_triangles.Load3(at + 24u));
    const float3 face = cross(v1 - v0, v2 - v0);
    return dot(face, face) > 0.0f && all(abs((v0 + v1 + v2) / 3.0f) < 1.0e6f);
}

// Thread 0: a glow light from its triangles' sums (power rgb, weight; weighted centre, weighted |centre|^2;
// area). `record`: the draw's e_draws record; `cell`: its cluster's (emit_identity).
void glow_light(float4 a, float4 b, float area_sum, uint record, uint cell)
{
    const uint4 entry = e_draws.Load4(record);
    const uint flags = e_draws.Load(record + 24u);
    const float w = a.w;
    const float3 centre = b.xyz / w;
    const float spread = sqrt(max(b.w / w - dot(centre, centre), 0.0f));
    const float radius = max(spread, 0.5f * sqrt(area_sum));
    // A glow's texture covers about a quarter of its quad.
    float3 power = a.rgb * (0.25f * e_gain);
    if ((flags & kGlowTableIntensity) != 0u) {
        // A light the table names sends the table's light (its intensity in Lumas), whatever the size of
        // the sprite that shows it: in the table's colour (fire is orange, not the white its sprites clip
        // to), or the colour the picture shows for a tinted row.
        const float3 tint = (flags & kGlowTableColor) != 0u
                                ? float3(float(entry.z >> 24u), float((entry.z >> 16u) & 0xFFu),
                                         float((entry.z >> 8u) & 0xFFu)) / 255.0f
                                : power;
        float intensity = asfloat(entry.w);
        const float pulse = asfloat(e_draws.Load(record + 20u));
        if (pulse > 0.0f) {
            intensity *= 0.75f + 0.25f * sin(6.2831853f * pulse * e_time);
        }
        power = tint / max(luminance(tint), 1e-6f) * (intensity * e_authored_power) * light_size(radius, 64.0f);
    } else if ((flags & kGlowEffect) != 0u) {
        // The table's particles are effects: they light what is near them as the picture shows them, at most
        // a fifth of a Luma (an Octoomba's head light, sparks, a flame; user: anything that glows lights its
        // surroundings). Uncapped, the sparkles around a Luma summed to 0.4-0.9e6 per light, 10-22 Lumas
        // (rp13 Observatory [rt-emit]); a fifth of a Luma is 8000.
        power *= min(1.0f, 0.2f * e_authored_power / max(luminance(power), 1e-6f));
    } else {
        // Any other glow (a lamp's, a model's) lights what is near it as the picture shows it, at most half
        // a Luma: everything that glows lights its surroundings.
        power *= min(1.0f, 0.5f * e_authored_power / max(luminance(power), 1e-6f));
    }
    // The light is as large as its sprite: its light spreads softly from the whole glow. (At half the sprite, a lamp's
    // light fell to a sharp hot spot that jittered as the sprite moved: plaza flicker 0.36 -> 0.51.)
    if (luminance(power) >= 4.0f && radius <= kEmitLargest) {
        emit_found(float4(transform(e_to_world, centre), radius), float4(power, 0.0f), float4(0.0f, 0.0f, 0.0f, 0.0f),
                   kEmitSourceGlow | (record / 32u), emit_identity(e_draws.Load(record + 16u), cell));
    }
}

// The glow's mean added light over an eye-space triangle (the centroid, three points towards the corners and three
// towards the edges' middles): a far glow, mipmapped into a small dim blob, keeps its total light where its centre
// dims, so its light does not fade with the camera's distance. Its colour (rgb) and mean luminance (w).
float4 glow_seen(float3 v0, float3 v1, float3 v2)
{
    static const float3 kTaps[7] = {float3(0.333333f, 0.333333f, 0.333334f), float3(0.7f, 0.15f, 0.15f),
                                    float3(0.15f, 0.7f, 0.15f), float3(0.15f, 0.15f, 0.7f),
                                    float3(0.45f, 0.45f, 0.1f), float3(0.1f, 0.45f, 0.45f),
                                    float3(0.45f, 0.1f, 0.45f)};
    float3 hue_sum = float3(0.0f, 0.0f, 0.0f);
    float l_sum = 0.0f;
    uint taps = 0u;
    [unroll] for (uint k = 0u; k < 7u; ++k) {
        const float3 at_tap = v0 * kTaps[k].x + v1 * kTaps[k].y + v2 * kTaps[k].z;
        if (at_tap.z >= -e_near) {
            continue;
        }
        const float2 q = eye_to_pixel(e_projection, e_viewport, at_tap);
        const int2 s = int2(floor(q / float(e_factor)));
        if (any(s < 0) || any(s >= int2(e_sample_size))) {
            continue;
        }
        ++taps;
        const float3 added = unpack_r11g11b10(e_added[uint(s.y) * e_sample_size.x + uint(s.x)]);
        const float l = luminance(added);
        if (l > 0.005f) {
            // Over a bright background the added light loses the channels the background had
            // already filled (white fire over orange lava measures blue): where the picture
            // reaches white its colour is the one shown.
            const float3 shown = e_color.Load(int3(clamp(int2(q), int2(0, 0), int2(e_target_size) - 1), 0)).rgb;
            const float filled = smoothstep(0.8f, 1.0f, max(shown.r, max(shown.g, shown.b)));
            hue_sum += lerp(added / l, shown / max(luminance(shown), 1e-3f), filled) * l;
            l_sum += l;
        }
    }
    const float l_mean = taps != 0u ? l_sum / float(taps) : 0.0f;
    return l_mean > 0.005f ? float4(hue_sum / l_sum, l_mean) : float4(0.0f, 0.0f, 0.0f, 0.0f);
}

// A glow draw's triangle `t`'s cell (0: behind the camera or degenerate).
uint glow_key(uint first, uint t, out float3 v0, out float3 v1, out float3 v2)
{
    if (!emit_triangle(first + t, v0, v1, v2)) {
        return 0u;
    }
    const float3 c = (v0 + v1 + v2) / 3.0f;
    return c.z < -e_near ? cluster_key(transform(e_to_world, c)) : 0u;
}

// Adds glow draw triangle `t` to the sums when it lies in `slot`.
void glow_add(uint first, uint t, uint slot, inout float4 a, inout float4 b, inout float area_sum)
{
    float3 v0 = float3(0.0f, 0.0f, 0.0f), v1 = v0, v2 = v0;
    const uint key = t < kClusterNoted ? 1u : glow_key(first, t, v0, v1, v2);
    if (cluster_slot_of(t, key) != slot || !emit_triangle(first + t, v0, v1, v2)) {
        return;
    }
    const float4 seen = glow_seen(v0, v1, v2);
    if (seen.w > 0.0f) {
        const float3 c = (v0 + v1 + v2) / 3.0f;
        const float area = 0.5f * length(cross(v1 - v0, v2 - v0));
        const float w = seen.w * area;
        a += float4(seen.rgb * w, w);
        b += float4(c * w, dot(c, c) * w);
        area_sum += area;
    }
}

// One group per glow draw; one light per cluster of its glowing triangles.
[numthreads(64, 1, 1)]
void GlowEmitCS(uint3 group : SV_GroupID, uint gi : SV_GroupIndex)
{
    const uint record = group.x * 32u;
    const uint2 range = e_draws.Load2(record);   // first triangle, count
    if ((e_draws.Load(record + 24u) & kGlowLens) != 0u) {
        return;
    }
    cluster_begin(gi, range.y);
    for (uint t = gi; t < range.y; t += 64u) {
        float3 v0, v1, v2;
        cluster_note(t, glow_key(range.x, t, v0, v1, v2));
    }
    cluster_list(gi);
    const uint count = s_cluster_count;
    // The group sums each slot in turn, in a fixed order.
    for (uint u = 0u; u < count; ++u) {
        float4 a = float4(0.0f, 0.0f, 0.0f, 0.0f);   // power rgb, weight (luminance x area)
        float4 b = float4(0.0f, 0.0f, 0.0f, 0.0f);   // weighted centre, weighted |centre|^2
        float area_sum = 0.0f;
        for (uint t = gi; t < range.y; t += 64u) {
            glow_add(range.x, t, s_cluster_used[u], a, b, area_sum);
        }
        s_glow_a[gi] = a;
        s_glow_b[gi] = b;
        s_glow_area[gi] = area_sum;
        glow_reduce(gi);
        if (gi == 0u) {
            s_cluster_a[u] = s_glow_a[0];
            s_cluster_b[u] = s_glow_b[0];
            s_cluster_w[u] = s_glow_a[0].w;
            s_cluster_area[u] = s_glow_area[0];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    // The largest glows are atmospheres and halos around planets and the sky, not lights: a draw whose glowing
    // area is wider than kEmitLargest lights nothing, though its parts would each pass.
    float lit_area = 0.0f;
    for (uint i = 0u; i < count; ++i) {
        lit_area += s_cluster_area[i];
    }
    if (lit_area > 4.0f * kEmitLargest * kEmitLargest) {
        return;
    }
    if (count > 1u) {
        cluster_join(gi, count);
    } else if (gi == 0u) {
        s_cluster_root[0] = 0u;
    }
    if (gi != 0u) {
        return;
    }
    for (uint r = 0u; r < count; ++r) {
        if (s_cluster_root[r] != r) {
            continue;
        }
        float4 a = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float4 b = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float area_sum = 0.0f;
        for (uint m = r; m < count; ++m) {
            if (s_cluster_root[m] == r) {
                a += s_cluster_a[m];
                b += s_cluster_b[m];
                area_sum += s_cluster_area[m];
            }
        }
        if (a.w > 0.0f) {
            glow_light(a, b, area_sum, record, s_cluster_key[s_cluster_used[r]]);
        }
    }
}

// The material table's point and spot lights (rt_materials.h: Lumas, star bits, fire and ice
// enemies, a boss's glowing weak spot, a headlamp): each cluster of a draw's eye-space triangles in
// the scene (e_triangles) gives a light's centre and spread; its power is the table's colour times its
// intensity (a Luma's: e_authored_power), pulsing at its rate. A tinted light takes the colour its
// draw shows where the picture shows it (its triangles' centres at the depth there); a spot light
// shines along its faces' mean normal (a lens or a cup: a closed shape's normals cancel out and it
// stays a point light), turned out of the model by the faces the camera sees (they face it).
// One group per draw (e_draws: first triangle, count, colour RGBA8, intensity; range, pulse, flags,
// cosine of the cone).
static const uint kAuthoredTint = 1u;
static const uint kAuthoredSpot = 2u;
static const uint kAuthoredStar = 4u;   // a star: its power by its size (light_size)
// A long emitter (a power cable): every cell its own light, none joined. A chain of lights along it lights what lies
// beside it as the tube does; joined, a cable crossing the Observatory was one light at its middle, on the ground.
static const uint kAuthoredStrip = 8u;

groupshared float4 s_authored_n[64];   // face normal sum (twice the area), facing evidence (area)
groupshared float4 s_authored_c[64];   // colour seen (times area), area seen

// The sums of 64 threads' s_glow_b, s_glow_area, s_authored_n and s_authored_c in their first entries;
// every thread calls it.
void authored_reduce(uint gi)
{
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
}

// Thread 0: a table light from its triangles' sums (area-weighted centre and |centre|^2; area; face normals
// and facing evidence; colour seen and area seen). `record`: the draw's e_draws record; `cell`: its cluster's.
void authored_light(float4 b, float w, float4 n, float4 seen, uint record, uint cell)
{
    const uint4 entry = e_draws.Load4(record);
    const float4 shape = asfloat(e_draws.Load4(record + 16u));
    const uint flags = asuint(shape.z);
    const float3 centre = b.xyz / w;
    const float spread = sqrt(max(b.w / w - dot(centre, centre), 0.0f));
    float3 color = float3(float(entry.z >> 24u), float((entry.z >> 16u) & 0xFFu),
                          float((entry.z >> 8u) & 0xFFu)) / 255.0f;
    // power.w 1: the colour is unknown this frame (the light keeps the one it had).
    float unknown = 0.0f;
    if ((flags & kAuthoredTint) != 0u) {
        // Its hue as the picture shows it, at the table's brightness: the shown colour's brightness changes with the
        // light reaching it, not with the light the source sends.
        const float3 shown = seen.w > 0.0f ? seen.rgb / seen.w : float3(0.0f, 0.0f, 0.0f);
        const float peak = max(shown.r, max(shown.g, shown.b));
        if (peak > 0.02f) {
            const float3 tinted = color * (shown / peak);
            color = tinted * (luminance(color) / max(luminance(tinted), 1e-6f));
        } else if (seen.w > 0.0f) {
            // Seen dark: unlit (a power cable of a dome without power), it sends no light.
            return;
        } else if ((flags & kAuthoredStrip) != 0u) {
            // A strip's stretch not seen this frame reports nothing (its remembered light keeps its colour and fades
            // out of view): every stretch of the Observatory's cables reporting, 2,436 lights overflowed the 2,048
            // the frame holds, and the visible ones were dropped.
            return;
        } else {
            unknown = 1.0f;
        }
    }
    float intensity = asfloat(entry.w);
    if (shape.y > 0.0f) {
        intensity *= 0.75f + 0.25f * sin(6.2831853f * shape.y * e_time);
    }
    const float3 power =
        color * (intensity * e_authored_power) * ((flags & kAuthoredStar) != 0u ? light_size(spread, 400.0f) : 1.0f);
    float4 spot = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 at_point = centre;
    if ((flags & kAuthoredSpot) != 0u) {
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
        // A strip's stretch is a light as wide as half its cell: the chain of lights along a cable overlaps into one
        // glow (as points a few units wide, the light along the garden cable lay in separate beads), and its shadows
        // soften as a tube that size casts them.
        const float radius = (flags & kAuthoredStrip) != 0u ? max(spread, 0.5f * kClusterCell) : max(spread, 1.0f);
        emit_found(float4(transform(e_to_world, at_point), radius), float4(power, unknown), spot,
                   kEmitSourceTable | (record / 32u), emit_identity(asuint(shape.x), cell));
    }
}

// A table light draw's triangle `t`'s cell (0: degenerate).
uint authored_key(uint first, uint t, out float3 v0, out float3 v1, out float3 v2)
{
    return emit_triangle(first + t, v0, v1, v2) ? cluster_key(transform(e_to_world, (v0 + v1 + v2) / 3.0f)) : 0u;
}

// Adds table light draw triangle `t` to the sums when it lies in `slot`: area-weighted centre and |centre|^2, area,
// face normals and facing evidence, colour seen and area seen (`flags`: the draw's).
void authored_add(uint first, uint t, uint slot, uint flags, inout float4 b, inout float area_sum,
                  inout float4 normals, inout float4 seen)
{
    float3 v0 = float3(0.0f, 0.0f, 0.0f), v1 = v0, v2 = v0;
    const uint key = t < kClusterNoted ? 1u : authored_key(first, t, v0, v1, v2);
    if (cluster_slot_of(t, key) != slot || !emit_triangle(first + t, v0, v1, v2)) {
        return;
    }
    const float3 c = (v0 + v1 + v2) / 3.0f;
    const float3 face = cross(v1 - v0, v2 - v0);
    const float area = 0.5f * length(face);
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

[numthreads(64, 1, 1)]
void AuthoredEmitCS(uint3 group : SV_GroupID, uint gi : SV_GroupIndex)
{
    const uint record = group.x * 32u;
    const uint2 range = e_draws.Load2(record);   // first triangle, count
    const uint flags = e_draws.Load(record + 24u);
    cluster_begin(gi, range.y);
    for (uint t = gi; t < range.y; t += 64u) {
        float3 v0, v1, v2;
        cluster_note(t, authored_key(range.x, t, v0, v1, v2));
    }
    cluster_list(gi);
    const uint count = s_cluster_count;
    // The group sums each slot in turn, in a fixed order.
    for (uint u = 0u; u < count; ++u) {
        float4 b = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float4 normals = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float4 seen = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float area_sum = 0.0f;
        for (uint t = gi; t < range.y; t += 64u) {
            authored_add(range.x, t, s_cluster_used[u], flags, b, area_sum, normals, seen);
        }
        s_glow_b[gi] = b;
        s_glow_area[gi] = area_sum;
        s_authored_n[gi] = normals;
        s_authored_c[gi] = seen;
        authored_reduce(gi);
        if (gi == 0u) {
            s_cluster_a[u] = s_authored_c[0];
            s_cluster_b[u] = s_glow_b[0];
            s_cluster_n[u] = s_authored_n[0];
            s_cluster_w[u] = s_glow_area[0];
            s_cluster_area[u] = s_glow_area[0];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (count > 1u && (flags & kAuthoredStrip) == 0u) {
        cluster_join(gi, count);
    } else {
        if (gi < count) {
            s_cluster_root[gi] = gi;
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi != 0u) {
        return;
    }
    for (uint r = 0u; r < count; ++r) {
        if (s_cluster_root[r] != r) {
            continue;
        }
        float4 b = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float4 normals = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float4 seen = float4(0.0f, 0.0f, 0.0f, 0.0f);
        float area_sum = 0.0f;
        for (uint m = r; m < count; ++m) {
            if (s_cluster_root[m] == r) {
                b += s_cluster_b[m];
                normals += s_cluster_n[m];
                seen += s_cluster_a[m];
                area_sum += s_cluster_area[m];
            }
        }
        if (area_sum > 0.0f) {
            authored_light(b, area_sum, normals, seen, record, s_cluster_key[s_cluster_used[r]]);
        }
    }
}

groupshared uint s_emit_best[kEmitFound];
groupshared uint s_emit_free[kEmitLights];
groupshared uint s_emit_free_count;
groupshared uint s_emit_taken;
groupshared float2 s_emit_scan[kEmitLights];

static const uint kEmitLightVectors = 4u;   // float4s per remembered light (ray_tracing.cpp kEmitLightBytes)

// One group: the remembered lights take this frame's glow lights (each the remembered light of its identity, else
// the nearest within reach of both), the others take free slots, unseen lights fade, then the list for sampling.
// Matched by distance alone, a lamp's light went to a sparkle beside it in some frames while its own faded as
// gone: the planter lamps of the Observatory plaza flashed (210 -> 151 -> 208 levels on the stone beside one).
[numthreads(kEmitLights, 1, 1)]
void EmitMergeCS(uint gi : SV_GroupIndex)
{
    const uint found = min(e_found.Load(0u), kEmitFound);
    const uint at = gi * kEmitLightVectors;
    float4 la = e_reset != 0u ? float4(0.0f, 0.0f, 0.0f, 0.0f) : e_lights[at];
    float4 lb = e_reset != 0u ? float4(0.0f, 0.0f, 0.0f, 0.0f) : e_lights[at + 1u];
    float4 lc = e_reset != 0u ? float4(0.0f, 0.0f, 0.0f, 0.0f) : e_lights[at + 2u];
    float4 ld = e_reset != 0u ? float4(0.0f, 0.0f, 0.0f, 0.0f) : e_lights[at + 3u];
    const uint identity = asuint(ld.x);
    const bool active = luminance(lb.rgb) > 0.0f;
    for (uint i = gi; i < found; i += kEmitLights) {
        s_emit_best[i] = 0xFFFFFFFFu;
    }
    if (gi == 0u) {
        s_emit_free_count = 0u;
        s_emit_taken = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    // The match key (the lowest wins): its own identity first (key = slot), else the distance with the slot in its low
    // 9 bits.
    if (active) {
        [loop] for (uint t = 0u; t < found; ++t) {
            const uint found_identity = e_found.Load(emit_source_at(t) + 4u);
            if (identity != 0u && found_identity == identity) {
                InterlockedMin(s_emit_best[t], gi);
                continue;
            }
            const float4 f = asfloat(e_found.Load4(16u + t * kEmitStride));
            const float d = distance(f.xyz, la.xyz);
            if (d < 2.0f * (la.w + f.w)) {
                InterlockedMin(s_emit_best[t], max(asuint(d) & 0xFFFFFE00u, 0x200u) | gi);
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
            if (s_emit_best[t] != 0xFFFFFFFFu && (s_emit_best[t] & 0x1FFu) == gi) {
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
            // Seen. What a light measures jitters from frame to frame (a sprite's animation, the pixels a Luma shows,
            // its centre over the triangles in view), and a light near a surface turns that into flicker. Jitter is
            // held: a change in brightness under a quarter, a move under a quarter of its radius, follows at 5% a
            // frame; larger changes are real (a glow switching, a light moving) and follow at once: brighter at
            // once, dimmer slowly (a partly hidden glow shows less of itself), moves halfway a frame. A particle's
            // light (sparkles, stardust) is their steady mean.
            const bool particle = max(luminance(power), lb_l) <= kParticleLight * e_authored_power;
            const float p_l = luminance(power);
            const bool jitter = abs(p_l - lb_l) < 0.25f * max(lb_l, 1e-6f);
            const float rate = particle || jitter ? 0.05f : p_l > lb_l ? 0.25f : 0.08f;
            // Its colour follows slowly whatever its brightness does (a Luma's shown colour changes with its pose).
            const float3 hue = lerp(lb.rgb / max(lb_l, 1e-6f), power / max(p_l, 1e-6f), 0.05f);
            const float l = lerp(lb_l, p_l, rate);
            lb.rgb = hue / max(luminance(hue), 1e-6f) * l;
            const float3 seen_at = centre.xyz / centre.w;
            const bool still = distance(seen_at, la.xyz) < 0.25f * max(la.w, 1.0f) && abs(radius - la.w) < 0.25f * la.w;
            const float follow = particle ? 0.1f : still ? 0.05f : 0.5f;
            la = float4(lerp(la.xyz, seen_at, follow), lerp(la.w, radius, follow));
            lb.w = 0.0f;
            // A spot turns smoothly; one seen as a point light is one.
            const bool both = spot_known && dot(lc.xyz, lc.xyz) > 0.25f;
            lc = both ? float4(normalize(lerp(lc.xyz, spot.xyz, 0.5f)), spot.w) : spot;
        } else {
            // Its place in view with nothing in front: the glow is gone. Out of view or hidden:
            // kept.
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
            // Out of view it keeps lighting the stage at any distance; one too far to light anything within
            // 20000 units of the camera fades over seconds (the list keeps room for the lights that matter here).
            // A particle's light (a fifth of a Luma or less: sparkles, stardust) fades over about a second once
            // unseen: kept like a lamp's, the passing sparkles of the Observatory garden filled all kEmitLights.
            const float reach = sqrt(luminance(lb.rgb) / (kPi * kEmitForget));
            const bool far = length(e) > reach + 20000.0f;
            const bool particle = luminance(lb.rgb) <= kParticleLight * e_authored_power;
            // Gone only after three frames missed in view (one missed match is no reason to drop a lamp), then fading
            // over about 0.2 s rather than halving each frame, so a light missed for a few frames (a cable's stretch at
            // the picture's edge) dims and returns without a step.
            lb.rgb *= gone && lb.w >= 3.0f ? 0.8f : far ? 0.98f : particle ? 0.95f : 1.0f;
        }
        if (luminance(lb.rgb) < 1.0f) {
            la = float4(0.0f, 0.0f, 0.0f, 0.0f);
            lb = float4(0.0f, 0.0f, 0.0f, 0.0f);
            lc = float4(0.0f, 0.0f, 0.0f, 0.0f);
            ld = float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }
    e_lights[at] = la;
    e_lights[at + 1u] = lb;
    e_lights[at + 2u] = lc;
    e_lights[at + 3u] = ld;
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
                const uint slot = s_emit_free[k] * kEmitLightVectors;
                const float4 fs = asfloat(e_found.Load4(16u + n * kEmitStride + 32u));
                // A tinted light whose colour was never seen (a cable behind the camera) starts none: dark when its
                // colour is dark, never the table's white.
                if (asfloat(e_found.Load(16u + n * kEmitStride + 28u)) > 0.5f) {
                    continue;
                }
                e_lights[slot] = asfloat(e_found.Load4(16u + n * kEmitStride));
                // A new light fades in: a particle's as it does when seen again, any other from a quarter of its power
                // (about 8 frames to full). At full power at once, a cable's stretch at the picture's edge, seen in
                // one frame and not the next, popped in as a glow over the garden's cliff.
                const float3 power = asfloat(e_found.Load3(16u + n * kEmitStride + 16u));
                const bool particle = luminance(power) <= kParticleLight * e_authored_power;
                e_lights[slot + 1u] = float4(power * (particle ? 0.05f : 0.25f), 0.0f);
                e_lights[slot + 2u] = fs.w < kSpotUnknown + 2.0f ? float4(0.0f, 0.0f, 0.0f, 0.0f) : fs;
                e_lights[slot + 3u] = float4(asfloat(e_found.Load(emit_source_at(n) + 4u)), 0.0f, 0.0f, 0.0f);
            }
        }
    }
    AllMemoryBarrierWithGroupSync();
    // The list: the lights in slot order with their cumulative power.
    const float4 fa = e_lights[at];
    const float4 fb = e_lights[at + 1u];
    const float4 fc = e_lights[at + 2u];
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
    uint2 o_light_size;         // the lighting's samples (0: no halo mask)
    uint o_factor;              // EFB pixels per sample side
    uint3 o_pad;
};
StructuredBuffer<uint> o_bloom : register(t0);
StructuredBuffer<uint4> o_light : register(t1);   // the lighting's result (SpatialBCS g_final)

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
    // A small light source (Luma, star bit, star, fire enemy) glows around itself, not over itself: its own
    // pixels (a halo share in the lighting's result) keep their detail.
    if (o_factor != 0u) {
        const uint2 s = min(uint2(position.xy) / o_factor, o_light_size - 1u);
        const float halo = f16tof32(o_light[s.y * o_light_size.x + s.x].w >> 16u);
        c *= 1.0f - saturate(halo * 2.5f);
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

// The traced sky's map: a texel's world direction from its map position `uv` (0-1), and back.
float3 sky_map_direction(float2 uv)
{
    const float2 e = uv * 2.0f - 1.0f;
    float3 v = float3(e.x, 1.0f - abs(e.x) - abs(e.y), e.y);
    if (v.y < 0.0f) {
        v.xz = (1.0f - abs(v.zx)) * float2(v.x >= 0.0f ? 1.0f : -1.0f, v.z >= 0.0f ? 1.0f : -1.0f);
    }
    return normalize(v);
}

float2 sky_map_uv(float3 d)
{
    d /= max(abs(d.x) + abs(d.y) + abs(d.z), 1e-20f);
    float2 e = d.xz;
    if (d.y < 0.0f) {
        e = (1.0f - abs(d.zx)) * float2(d.x >= 0.0f ? 1.0f : -1.0f, d.z >= 0.0f ? 1.0f : -1.0f);
    }
    return e * 0.5f + 0.5f;
}

float3 eye_to_world(float3 d)
{
    return normalize(d.x * g_to_eye[0].xyz + d.y * g_to_eye[1].xyz + d.z * g_to_eye[2].xyz);
}

// kPhysicalFlag: the traced sky's level. The sky model is drawn as a backdrop (its picture says nothing of how much
// light it gives); the stage's ambient says how much the open sky lights a surface: the traced sky keeps its colours
// and their distribution over the sphere at the ambient's luminance (its mean radiance equal to it).
float physical_sky_scale()
{
    const float mean = luminance(g_sky_map[kSkyMapSize * kSkyMapSize].rgb * 0.282095f);
    return mean > 1e-4f ? luminance(g_sky_radiance.rgb) / mean : 0.0f;
}

// kPhysicalFlag: the sky's radiance towards eye-space direction `d`: the traced sky's (bilinear in its map, at the
// stage's ambient level), else the ambient from every direction.
float3 physical_sky(float3 d)
{
    if (g_full_light.w == 0.0f || physical_sky_scale() == 0.0f) {
        return g_sky_radiance.rgb;
    }
    const float2 p = sky_map_uv(eye_to_world(d)) * float(kSkyMapSize) - 0.5f;
    const float2 f = frac(p);
    const int2 i = int2(floor(p));
    float3 radiance = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 t = clamp(i + int2(k & 1u, k >> 1u), 0, int(kSkyMapSize) - 1);
        const float w = ((k & 1u) != 0u ? f.x : 1.0f - f.x) * ((k & 2u) != 0u ? f.y : 1.0f - f.y);
        radiance += g_sky_map[uint(t.y) * kSkyMapSize + uint(t.x)].rgb * w;
    }
    return radiance * physical_sky_scale();
}

// kPhysicalFlag: the open sky's irradiance over pi on a surface facing eye-space `n` (the traced sky's spherical
// harmonics convolved with the cosine: bands 1, 2/3, 1/4, at the stage's ambient level), else the ambient.
float3 physical_sky_irradiance(float3 n)
{
    if (g_full_light.w == 0.0f || physical_sky_scale() == 0.0f) {
        return g_sky_radiance.rgb;
    }
    float basis[9];
    sh9(eye_to_world(n), basis);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint b = 0u; b < 9u; ++b) {
        irradiance += g_sky_map[kSkyMapSize * kSkyMapSize + b].rgb *
                      (basis[b] * (b == 0u ? 1.0f : b < 4u ? 2.0f / 3.0f : 0.25f));
    }
    return max(irradiance, 0.0f) * physical_sky_scale();
}

// kPhysicalFlag: the camera's exposure: a white surface facing the player's up (the ground under the player, by the
// game's gravity; g_up), lit by the open sky and the sun at its angle, shows at the stage's full light (the game's
// ambient + key: a dim stage stays as dim as the game sets it, a coloured one keeps its colour).
float physical_exposure()
{
    const float sun = max(dot(g_up.xyz, g_sun.xyz), 0.0f);
    return luminance(g_full_light.rgb) /
           max(luminance(physical_sky_irradiance(g_up.xyz)) + luminance(g_sun_light.rgb) * sun, 1e-4f);
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
    const float2 s = (eye_to_pixel(g_picture_projection, g_viewport, previous) - 0.5f) / float(g_factor) + 0.5f;
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

// Foliage cards: the scene holds the alpha-tested ones whole, their cut-outs filled. A hit on a card
// counts where the raster drew it (the depth at the hit's pixel lies on the card's plane), not where it
// saw through it (a farther depth, the sky); hidden or out of view, at the cards' mean coverage (Gusty
// Garden's flower and leaf cards: 0.46-0.57 of their texels), a number from the primitive and `salt` (the
// sample, the frame, the ray) deciding.
static const float kCardCoverage = 0.5f;

bool traced_card(uint flags)
{
    return (g_draws.Load(min(flags >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 24u) & kDrawTableCard) != 0u;
}

float card_hash(uint primitive, uint salt)
{
    uint h = primitive * 0x9E3779B1u + salt;
    h ^= h >> 16u;
    h *= 0x7FEB352Du;
    h ^= h >> 15u;
    h *= 0x846CA68Bu;
    h ^= h >> 16u;
    return float(h >> 8u) * (1.0f / 16777216.0f);
}

// An alpha-tested draw traced whole (kRtDrawAlphaTest: wood panels, rocks, bells, Gearmos whose test cuts
// little or nothing): the camera's ray takes it where the raster drew it, as a card.
static const uint kDrawTableAlpha = 32u;

bool alpha_draw(uint flags)
{
    return (g_draws.Load(min(flags >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 24u) & kDrawTableAlpha) != 0u;
}

// The alpha test of a hit at eye-space `h` on `primitive` (its g_surfaces flags word `flags`): 1 where the draw's
// own TEV program and alpha compare keep the texel (it stops the ray), 0 where the test cuts it (the ray goes on
// through a lattice's hole, between a fern's fronds), -1 untested (not an alpha-tested draw, or no TEV records:
// solid, or the card's own rule). The coordinates from the hit's barycentrics on its eye-space triangle. (The draw's
// record count comes from g_tev_info: a root descriptor has no size, and GetDimensions on g_draw_tex read 0, which
// left every alpha-tested hit to the card's rule: off screen, half the rays through the whole card.)
int alpha_test(uint primitive, uint flags, float3 h)
{
#if RT_TEXTURED
    const uint draw = flags >> 16u;
    if (draw >= g_tev_info.x || !(traced_card(flags) || alpha_draw(flags))) {
        return -1;
    }
    const float3 v0 = asfloat(g_positions.Load3(primitive * 36u));
    const float3 e1 = asfloat(g_positions.Load3(primitive * 36u + 12u)) - v0;
    const float3 e2 = asfloat(g_positions.Load3(primitive * 36u + 24u)) - v0;
    const float3 d = h - v0;
    const float d11 = dot(e1, e1);
    const float d12 = dot(e1, e2);
    const float d22 = dot(e2, e2);
    const float denominator = d11 * d22 - d12 * d12;
    if (abs(denominator) < 1e-20f) {
        return -1;
    }
    const float d1 = dot(d, e1);
    const float d2 = dot(d, e2);
    const float b1 = (d22 * d1 - d12 * d2) / denominator;
    const float b2 = (d11 * d2 - d12 * d1) / denominator;
    if (any(isnan(float2(b1, b2)))) {
        return -1;
    }
    bool pass;
    tev_evaluate(draw, g_tev_info.x, primitive, float2(b1, b2), pass);
    return pass ? 1 : 0;
#else
    return -1;
#endif
}

// Whether a candidate hit at eye-space `h` on `primitive` (its g_surfaces flags word `flags`) stops a ray. An
// alpha-tested draw or card stops it where its texture's alpha passes its test (exact leaf, fence and lattice
// shadows; Beach Bowl's grass dithered the seabed under the water with the cards' mean coverage).
bool card_blocks(uint primitive, uint flags, float3 h, uint salt)
{
    const int tested = alpha_test(primitive, flags, h);
    if (tested >= 0) {
        return tested != 0;
    }
    if (!traced_card(flags)) {
        return true;
    }
    const float2 q = eye_to_pixel(g_projection, g_viewport, h);
    if (h.z < -g_near && inside(g_viewport, q) && all(q < float2(g_target_size))) {
        const float depth = g_efb_depth.Load(int3(int2(q), 0));
        if (far_plane(g_depth, depth)) {
            return false;
        }
        // The card's plane along the pixel's own ray (edge-on: undecided).
        const float3 v0 = asfloat(g_positions.Load3(primitive * 36u));
        const float3 face = cross(asfloat(g_positions.Load3(primitive * 36u + 12u)) - v0,
                                  asfloat(g_positions.Load3(primitive * 36u + 24u)) - v0);
        const float3 d = pixel_direction(g_projection, g_viewport, floor(q) + 0.5f);
        const float facing = dot(face, d);
        if (abs(facing) > 0.05f * length(face) * length(d)) {
            const float plane = -dot(face, v0) / facing;
            const float z = depth_to_eye_z(g_depth, depth);
            const float tolerance = 0.004f * abs(plane) + 1.0f;
            if (z < plane - tolerance) {
                return false;
            }
            if (z <= plane + tolerance) {
                return true;
            }
        }
    }
    return card_hash(primitive, salt) < kCardCoverage;
}

// ---------------------------------------------------------------------------------------------
// Tile light lists: each group of 8 x 8 samples keeps the glow lights whose reach meets the box
// around its samples' surface points; its samples weigh only those. Lights beyond a tile's reach
// add nothing to any of its samples, so the result is the same at a fraction of the work. The list
// is a bit per light of the frame's list, walked in its order: a list of the first 128 lights the
// threads happened to add dropped different lights each frame where more were in reach (a tile over
// a depth edge reaches many), flashing tile-shaped blocks on the Observatory plaza's walkways.
// ---------------------------------------------------------------------------------------------

static const uint kTileWords = kEmitLights / 32u;

groupshared uint s_tile_bounds[6];      // min x y z, max x y z (order-preserving uints)
groupshared uint s_tile_count;          // lights in the tile's list
groupshared uint s_tile_mask[kTileWords];

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
        if ((gi) < kTileWords) {                                                                         \
            s_tile_mask[(gi)] = 0u;                                                                      \
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
            const uint total = min((lights).Load(0u), kTileWords * 32u);                                 \
            for (uint c = (gi); c < total; c += 64u) {                                                   \
                const float4 la = asfloat((lights).Load4(16u + c * kEmitStride));                        \
                const float3 power = asfloat((lights).Load3(16u + c * kEmitStride + 16u));               \
                const float3 e = transform(to_eye, la.xyz);                                              \
                const float3 out_of_box = max(max(lo - e, e - hi), 0.0f);                                \
                if (dot(out_of_box, out_of_box) < reach2_of(luminance(power))) {                         \
                    InterlockedOr(s_tile_mask[c >> 5u], 1u << (c & 31u));                                \
                    InterlockedAdd(s_tile_count, 1u);                                                    \
                }                                                                                        \
            }                                                                                            \
        }                                                                                                \
        GroupMemoryBarrierWithGroupSync();                                                               \
    }

// Shadow tests: a sample's strongest lights take a shadow ray each, the rest their visibility's weighted mean.
// Where two lights' weights cross, the tested set changes: a light leaving it went at once from its own shadow
// to the others' visibility, a step in the picture that moved with every change in their light (flicker). A
// tested light counts by its lead over the strongest untested one (`next`): fully from twice its weight, not at
// all where they cross, so either side of a crossing is the same picture.
static const float kRankLead = 0.5f;
// The untested lights' visibility also takes this share of the tested lights' plain mean: where every tested
// light is level with the next, none would count.
static const float kRankFloor = 0.05f;

float rank_confidence(float weight, float next)
{
    return saturate((weight - next) / (kRankLead * weight));
}

// The untested share's visibility from the tested lights' weights seen and tested, by confidence and plain.
float rest_visibility(float seen, float tested, float plain_seen, float plain)
{
    const float d = tested + kRankFloor * plain;
    return d > 0.0f ? (seen + kRankFloor * plain_seen) / d : 1.0f;
}

// The surface a sample shows (TraceCS, ReferenceCS): the triangle the camera ray meets at the raster depth gives the
// face normal, the shading normal from its vertex normals, and a ray origin lifted off the flat face where those
// normals bend away from it (the shadow terminator, Hanika 2021). Geometry the scene leaves out (blended and indirect
// draws) takes the normal from the depth.
struct Surface {
    float3 n;          // shading normal
    float3 ng;         // face normal, towards the camera
    float3 lifted;     // the point lifted off the face
    uint2 primary;     // the triangle + 1 (0: none), barycentrics (halves)
    // A clear crystal or glass surface (surface class 7) lets the lights behind it through: the star bits inside a
    // crystal light it from within.
    bool clear;
};

// The shading normal at barycentrics `w` of triangle `tri` (eye-space vertices v0-v2, face normal `ng` on the side
// seen) from its vertex normals, and the point the shading normals lift the face to (Hanika 2021); `n` stays `ng`
// without vertex normals or where they turn away from the face.
void smooth_surface(uint tri, float3 w, float3 v0, float3 v1, float3 v2, float3 ng, out float3 n, inout float3 lifted)
{
    n = ng;
    const uint4 surface = g_surfaces.Load4(tri * 16u);
    if (surface.x != 0u && surface.y != 0u && surface.z != 0u) {
        float3 n0 = unpack_vertex_normal(surface.x);
        float3 n1 = unpack_vertex_normal(surface.y);
        float3 n2 = unpack_vertex_normal(surface.z);
        float3 smooth = n0 * w.x + n1 * w.y + n2 * w.z;
        // The vertex normals of the side seen.
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

Surface primary_surface(int2 q, float3 p)
{
    Surface s;
    s.n = surface_normal(q, p);
    s.ng = s.n;
    s.lifted = p;
    s.primary = uint2(0u, 0u);
    s.clear = false;
    const float span = length(p);
    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, 0.0f);
    ray.Direction = p / span;
    ray.TMin = 0.0f;
    ray.TMax = span * 1.02f + 2.0f;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, kSceneMask, ray);
    while (query.Proceed()) {
        // A foliage card is the surface only where the raster drew it (at this pixel's depth).
        const uint candidate = g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
        if (abs(query.CandidateTriangleRayT() - span) <= span * 0.002f + 0.5f ||
            !(traced_card(candidate) || alpha_draw(candidate))) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT && abs(query.CommittedRayT() - span) < span * 0.01f + 2.0f) {
        const uint tri = query.CommittedPrimitiveIndex();
        const float2 b = query.CommittedTriangleBarycentrics();
        const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
        const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
        const float3 v1 = asfloat(g_positions.Load3(tri * 36u + 12u));
        const float3 v2 = asfloat(g_positions.Load3(tri * 36u + 24u));
        const float3 face = cross(v1 - v0, v2 - v0);
        const float face_length = length(face);
        if (face_length > 1e-12f) {
            s.ng = face / face_length;
            s.ng = dot(s.ng, p) > 0.0f ? -s.ng : s.ng;
            s.lifted = v0 * w.x + v1 * w.y + v2 * w.z;
            s.primary = uint2(tri + 1u, f32tof16(b.x) | (f32tof16(b.y) << 16u));
            s.clear = ((g_surfaces.Load(tri * 16u + 12u) >> 8u) & 15u) == kClearClass;
            smooth_surface(tri, w, v0, v1, v2, s.ng, s.n, s.lifted);
        }
    }
    return s;
}

// Whether an emitter of `radius` around `centre` is seen from `start`: one ray to a point of its disc facing `start`
// (`du` picks it; at most half the distance wide), ending at the emitter's surface. Late geometry (instance 1) has no
// records: it blocks. A card blocks where the raster drew it; the table's shadow 0 lets the light through.
bool emitter_visible(float3 start, float3 centre, float radius, float2 du, uint salt)
{
    const float3 to_centre = centre - start;
    const float centre_span = max(length(to_centre), 1e-4f);
    const float3 axis = to_centre / centre_span;
    const float az = axis.z >= 0.0f ? 1.0f : -1.0f;
    const float aa = -1.0f / (az + axis.z);
    const float ab = axis.x * axis.y * aa;
    const float3 d1 = float3(1.0f + az * axis.x * axis.x * aa, az * ab, -az * axis.x);
    const float3 d2 = float3(ab, az + axis.y * axis.y * aa, -axis.y);
    const float disc = min(radius, 0.5f * centre_span) * sqrt(du.x);
    const float phi = 2.0f * kPi * du.y;
    const float3 to = centre + (d1 * cos(phi) + d2 * sin(phi)) * disc - start;
    const float span = length(to);
    RayDesc ray;
    ray.Origin = start;
    ray.Direction = to / max(span, 1e-4f);
    ray.TMin = 0.0f;
    ray.TMax = max(span - max(radius, 2.0f + 0.01f * span), 0.0f);
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>
        query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
    while (query.Proceed()) {
        const bool late_hit = query.CandidateInstanceID() != 0u;
        const uint hit = late_hit ? 0u : g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
        if (late_hit || ((hit & kDrawNoShadow) == 0u &&
                         card_blocks(query.CandidatePrimitiveIndex(), hit,
                                     start + ray.Direction * query.CandidateTriangleRayT(), salt))) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    return query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

// Whether the sun is seen from `origin` along `direction` (a point inside its disc); the blocker's distance in
// `blocker` (0: open). `card`: the sample lies on a foliage card, which the cards close beside it do not shade.
bool sun_visible(float3 origin, float3 direction, bool card, uint salt, out float blocker)
{
    blocker = 0.0f;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = g_sun_reach;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
    while (query.Proceed()) {
        if (query.CandidateInstanceID() != 0u) {
            query.CommitNonOpaqueTriangleHit();
            continue;
        }
        const uint hit = g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
        if ((hit & kDrawNoShadow) != 0u) {
            continue;
        }
        if (card && query.CandidateTriangleRayT() < 60.0f &&
            (g_draws.Load(min(hit >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 24u) & kDrawTableFoliage) != 0u) {
            continue;
        }
        if (!card_blocks(query.CandidatePrimitiveIndex(), hit, origin + direction * query.CandidateTriangleRayT(),
                         salt)) {
            continue;
        }
        query.CommitNonOpaqueTriangleHit();
    }
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        blocker = clamp(query.CommittedRayT(), 1e-2f, 6.0e4f);
        return false;
    }
    return true;
}

void reference_material(uint tri, float2 b, out float3 albedo, out float3 emission);
float3 physical_hit(uint tri, float2 b, float3 x, float3 direction, uint salt);

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
    const Surface surf = primary_surface(q, p);
    const float3 n = surf.n;
    const float3 ng = surf.ng;
    const float3 lifted = surf.lifted;
    const uint2 primary = surf.primary;
    const bool clear = surf.clear;
    const float3 origin = lifted + ng * (0.2f + abs(p.z) * 4e-4f);
    // Decides card hits out of view (card_blocks), per sample, frame and ray.
    const uint salt = index * 32u + g_frame * 0x632BE5ABu;
    // GALAXY_RT_PROBE (diagnostics): at the probed sample, what the camera sees (triangle + 1, draw record) and what
    // stops the sun's centre ray from it (triangle + 1, draw record; distance, instance; the sample's eye z, its
    // facing to the sun), after the samples and the stars in g_final (SceneRayTracer reads them back).
    if (g_probe.z != 0u && all(id.xy == g_probe.xy)) {
        RayDesc probe_ray;
        probe_ray.Origin = origin;
        probe_ray.Direction = g_sun.xyz;
        probe_ray.TMin = 0.0f;
        probe_ray.TMax = g_sun_reach;
        RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> probe_query;
        probe_query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, probe_ray);
        while (probe_query.Proceed()) {
            if (probe_query.CandidateInstanceID() != 0u) {
                probe_query.CommitNonOpaqueTriangleHit();
                continue;
            }
            const uint hit = g_surfaces.Load(probe_query.CandidatePrimitiveIndex() * 16u + 12u);
            if ((hit & kDrawNoShadow) == 0u &&
                card_blocks(probe_query.CandidatePrimitiveIndex(), hit,
                            origin + g_sun.xyz * probe_query.CandidateTriangleRayT(), salt)) {
                probe_query.CommitNonOpaqueTriangleHit();
            }
        }
        const bool blocked = probe_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
        const uint blocker_tri = blocked ? probe_query.CommittedPrimitiveIndex() : 0u;
        const uint instance = blocked ? probe_query.CommittedInstanceID() : 0xFFFFFFFFu;
        const uint blocker_draw = blocked && instance == 0u ? g_surfaces.Load(blocker_tri * 16u + 12u) >> 16u : 0xFFFFFFFFu;
        const uint seen_draw = primary.x != 0u ? g_surfaces.Load((primary.x - 1u) * 16u + 12u) >> 16u : 0xFFFFFFFFu;
        const uint base = g_sample_size.x * g_sample_size.y + kStarSlots;
        g_final[base] = uint4(primary.x, seen_draw, blocked ? blocker_tri + 1u : 0u, blocker_draw);
        g_final[base + 1u] = uint4(asuint(blocked ? probe_query.CommittedRayT() : 0.0f), instance, asuint(p.z),
                                   asuint(dot(n, g_sun.xyz)));
    }

    // The sun: one shadow ray inside its disc, to its nearest blocker (its distance sizes the
    // penumbra the first filter gives the shadow, SpatialACS).
    float sun = 0.0f;
    float blocker = 0.0f;
    const float facing_sun = dot(n, g_sun.xyz);
    if (facing_sun > 0.0f) {
        const float3 l = g_sun.xyz;
        const float lz = l.z >= 0.0f ? 1.0f : -1.0f;
        const float la = -1.0f / (lz + l.z);
        const float lb = l.x * l.y * la;
        const float3 l1 = float3(1.0f + lz * l.x * l.x * la, lz * lb, -lz * l.x);
        const float3 l2 = float3(lb, lz + l.y * l.y * la, -l.y);
        bool card = false;
        if (primary.x != 0u) {
            const uint own = g_surfaces.Load((primary.x - 1u) * 16u + 12u);
            card = (g_draws.Load(min(own >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 24u) & kDrawTableFoliage) != 0u;
        }
        // High (g_flags bit 10): four shadow rays spread over the sun's disc, the sun's light their open
        // share (the penumbra measured per sample, not only filtered); medium one.
        const uint sun_rays = (g_flags & 1024u) != 0u ? 4u : 1u;
        uint open = 0u;
        for (uint k = 0u; k < sun_rays; ++k) {
            const float2 u = sample_point(id.xy, max(g_rays, 1u) + 3u + 8u * k);
            const float r = sqrt(u.x) * g_sun.w;
            const float phi = 2.0f * kPi * u.y;
            float t;
            if (sun_visible(origin, normalize(l + l1 * (r * cos(phi)) + l2 * (r * sin(phi))), card,
                            salt + 1u + 2u * k, t)) {
                ++open;
            } else {
                blocker = blocker == 0.0f ? t : min(blocker, t);
            }
        }
        // The game's light units: Lambert's N.L; else the art model's wrapped falloff.
        sun = ((g_flags & kPhysicalFlag) != 0u ? facing_sun : facing_sun * (1.5f - 0.5f * facing_sun)) *
              (float(open) / float(sun_rays));
        if ((g_flags & 64u) != 0u) {
            // GALAXY_RT_VIEW=6: the nearest blocker's distance up to 1e6, past the sun's reach,
            // 0.1 + 0.9 log10(t) / 6 (0: none).
            RayDesc ray;
            ray.Origin = origin;
            ray.Direction = l;
            ray.TMin = 0.0f;
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> nearest;
            ray.TMax = 1.0e6f;
            nearest.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
            nearest.Proceed();
            sun = nearest.CommittedStatus() == COMMITTED_TRIANGLE_HIT
                ? 0.1f + 0.9f * saturate(log10(max(nearest.CommittedRayT(), 1.0f)) / 6.0f)
                : 0.0f;
        }
    }

    // The point lights: each one's light every frame (its colour times n.l times its falloff), the
    // strongest here shadow-tested every frame (as many as the glows' tests), the rest at their mean
    // visibility. One picked at random per frame let them take turns (flicker; user, Native41 Observatory).
    float3 points = float3(0.0f, 0.0f, 0.0f);
    if (g_point_count != 0u) {
        float weights[kPointLights];
        float total = 0.0f;
        [unroll] for (uint i = 0u; i < kPointLights; ++i) {
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
        const uint point_tests = (g_flags & 1024u) != 0u ? 4u : g_rays <= 1u ? 1u : 2u;
        float3 rest = float3(0.0f, 0.0f, 0.0f);   // the light not counted as tested (rank_confidence)
        [unroll] for (uint i = 0u; i < kPointLights; ++i) {
            if (weights[i] > 0.0f) {
                rest += g_points[2u * i + 1u].rgb / max(luminance(g_points[2u * i + 1u].rgb), 1e-4f) * weights[i];
            }
        }
        // The strongest point_tests lights, then the strongest of the others (their confidence).
        uint picked[4] = {0u, 0u, 0u, 0u};
        float picked_weight[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        [unroll] for (uint pick = 0u; pick < 4u; ++pick) {
            uint chosen = 0u;
            float best = 0.0f;
            [unroll] for (uint j = 0u; j < kPointLights; ++j) {
                if (weights[j] > best) {
                    best = weights[j];
                    chosen = j;
                }
            }
            if (pick < point_tests && best > 1e-6f) {
                weights[chosen] = -best;   // tested
                picked[pick] = chosen;
                picked_weight[pick] = best;
            }
        }
        float next = 0.0f;
        [unroll] for (uint j = 0u; j < kPointLights; ++j) {
            next = max(next, weights[j]);
        }
        float seen_weight = 0.0f;
        float tested_weight = 0.0f;
        float plain_seen = 0.0f;
        float plain_weight = 0.0f;
        [unroll] for (uint test = 0u; test < 4u; ++test) {
            const float best = picked_weight[test];
            if (best <= 0.0f) {
                continue;
            }
            const uint chosen = picked[test];
            const float confidence = rank_confidence(best, next);
            const float3 chosen_light = g_points[2u * chosen + 1u].rgb / max(luminance(g_points[2u * chosen + 1u].rgb), 1e-4f) * best;
            rest -= chosen_light * confidence;
            tested_weight += best * confidence;
            plain_weight += best;
            // An area light: a shadow ray to a point of its emitter's disc, a new point each frame, so its shadows have
            // the penumbra of a light that size (the star core is a ball of up to 600 units' radius: aimed at its
            // centre, its shadows were cut-outs).
            const float2 du = sample_point(id.xy, max(g_rays, 1u) + 31u + 2u * test);
            if (emitter_visible(origin, g_points[2u * chosen].xyz, g_point_radius[chosen >> 2u][chosen & 3u], du,
                                salt + 2u)) {
                points += chosen_light * confidence;
                seen_weight += best * confidence;
                plain_seen += best;
            }
        }
        // The lights not tested, at the tested ones' visibility.
        points += max(rest, 0.0f) * rest_visibility(seen_weight, tested_weight, plain_seen, plain_weight);
    }

    // The glows (lamps, cores, Lumas, star bits, lava bursts): every light within reach of this point adds
    // its light every frame; the strongest here (4 on High, g_flags 1024; 2 on Medium; 1 on Low, g_rays 1) each
    // take a shadow ray every frame, the rest their visibility's weighted mean. Picking one or two at random per
    // frame (resampled importance) let lamps take turns: flickering lamps and shadows, a low lamp's long shadow
    // of Rosalina in some frames, three shadows of Mario averaging in (user, Native41 Observatory).
    if (s_tile_count != 0u) {
        float sum = 0.0f;
        float3 unshadowed = float3(0.0f, 0.0f, 0.0f);   // every light's light, unshadowed
        float4 kept[4] = {float4(0.0f, 0.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 0.0f),
                          float4(0.0f, 0.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 0.0f)};
        float3 kept_light[4] = {float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f),
                                float3(0.0f, 0.0f, 0.0f)};
        float kept_weight[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float fifth = 0.0f;   // the strongest light not kept
        [loop] for (uint word = 0u; word < kTileWords; ++word) {
        [loop] for (uint bits = s_tile_mask[word]; bits != 0u; bits &= bits - 1u) {
            const uint c = word * 32u + firstbitlow(bits);
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
            const float nl = clear ? abs(dot(n, to)) * inv_d : dot(n, to) * inv_d;
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
            // Its light here: power nl / (pi (d2 + r2)), windowed to its reach.
            const float3 here = power / (kPi * light) * (weight * g_emit.z);
            unshadowed += here;
            // The strongest four so far, strongest first.
            float4 carry = float4(e, la.w);
            float3 carry_light = here;
            float carry_weight = weight;
            [unroll] for (uint s = 0u; s < 4u; ++s) {
                if (carry_weight > kept_weight[s]) {
                    const float4 k0 = kept[s];
                    const float3 l0 = kept_light[s];
                    const float w0 = kept_weight[s];
                    kept[s] = carry;
                    kept_light[s] = carry_light;
                    kept_weight[s] = carry_weight;
                    carry = k0;
                    carry_light = l0;
                    carry_weight = w0;
                }
            }
            fifth = max(fifth, carry_weight);
        }
        }
        float3 glow = float3(0.0f, 0.0f, 0.0f);
        const uint picks = (g_flags & 1024u) != 0u ? 4u : g_rays <= 1u ? 1u : 2u;
        const float next = picks < 4u ? kept_weight[picks] : fifth;   // the strongest untested light
        float3 tested = float3(0.0f, 0.0f, 0.0f);   // the shadow-tested lights' unshadowed light, by confidence
        float seen_weight = 0.0f;
        float tested_weight = 0.0f;
        float plain_seen = 0.0f;
        float plain_weight = 0.0f;
        [unroll] for (uint s = 0u; s < 4u; ++s) {
            if (s < picks && kept_weight[s] > 0.0f) {
                const float confidence = rank_confidence(kept_weight[s], next);
                tested += kept_light[s] * confidence;
                tested_weight += kept_weight[s] * confidence;
                plain_weight += kept_weight[s];
                // A light behind a clear surface is reached from its inner side.
                const bool behind = clear && dot(kept[s].xyz - p, ng) < 0.0f;
                const float3 start = behind ? lifted - ng * (0.2f + abs(p.z) * 4e-4f) : origin;
                // A point of the light's disc facing this sample (its glow radius): the shadow's penumbra measured,
                // filtered over frames by the lighting's temporal pass. The light fills its glow (a core, a lamp head):
                // the ray ends at its radius.
                const float2 du = sample_point(id.xy, max(g_rays, 1u) + 11u + 2u * s);
                if (emitter_visible(start, kept[s].xyz, kept[s].w, du, salt + 3u + s)) {
                    glow += kept_light[s] * confidence;
                    seen_weight += kept_weight[s] * confidence;
                    plain_seen += kept_weight[s];
                }
            }
        }
        // The lights not tested: their light at the tested ones' visibility (the occluders near this point
        // shade them alike).
        glow += max(unshadowed - tested, 0.0f) * rest_visibility(seen_weight, tested_weight, plain_seen, plain_weight);
        // Bright bursts (lava eruptions, sparks) roll off softly instead of blowing out.
        points += soft_light(glow, 1.5f) / max(g_point_light, 1e-3f);
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
        const bool physical = (g_flags & kPhysicalFlag) != 0u;
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = direction;
        ray.TMin = 0.0f;
        ray.TMax = physical ? 1.0e6f : g_range;
        RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
        while (query.Proceed()) {
            const bool late_hit = query.CandidateInstanceID() != 0u;
            const uint hit = late_hit ? 0u : g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
            if (late_hit || ((hit & kDrawNoShadow) == 0u &&
                             card_blocks(query.CandidatePrimitiveIndex(), hit,
                                         origin + ray.Direction * query.CandidateTriangleRayT(), salt + 5u + i))) {
                query.CommitNonOpaqueTriangleHit();
            }
        }
        if (physical) {
            // The game's light units: what the ray sees is the sky's radiance, or the surface it hits lit by the sun
            // (a shadow ray from there) and the open sky, plus its own emission. Late geometry (another instance)
            // only blocks.
            const float3 x = origin + direction * query.CommittedRayT();
            if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT || length(x) > g_ambient.w) {
                light += physical_sky(direction);
            } else if (query.CommittedInstanceID() == 0u) {
                light += physical_hit(query.CommittedPrimitiveIndex(), query.CommittedTriangleBarycentrics(), x,
                                      direction, salt + 9u + i);
            }
            continue;
        }
        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            const float t = query.CommittedRayT();
            float3 sent = hit_light(origin + ray.Direction * t, fallback);
            const float near = lerp(g_contact, 1.0f, saturate(t / g_contact_range));
            // Distant hits fade into the open sky (the game's lighting already holds them).
            const float distant = saturate((t - 0.6f * g_range) / (0.4f * g_range));
            // A glowing surface (unlit, bright beyond its colour under the capture's emitter gain,
            // saturated: lava, magma, fire) sends its own light, whatever the sky: it joins the
            // point lights' (not scaled by the sky's state), without contact darkening.
            const bool late_hit = query.CommittedInstanceID() != 0u;
            const uint hit_flags = late_hit ? 0u : g_surfaces.Load(query.CommittedPrimitiveIndex() * 16u + 12u);
            const uint2 hit_emission =
                g_draws.Load2(min(hit_flags >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 8u);
            if (!late_hit && (hit_emission.x & kEmitNamed) != 0u) {
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
    // The count half carries the sun's blocker distance (0: none) through the first filter.
    g_raw[index] = pack_light(make_light(light / float(rays), sun, points), blocker);
}

// ---------------------------------------------------------------------------------------------
// Reference (GALAXY_RT_REFERENCE=1, g_flags bit 13): a path tracer for a still camera, the image the real-time passes
// are measured against; not a player setting. Per sample and frame one path. At every vertex: the sun (a shadow ray
// inside its disc), and one light of all the game's point lights and glow lights, chosen in proportion to its
// unshadowed light there, with a shadow ray to a point of its emitter (the sum of all over the chance of the one
// chosen: unbiased). Then a cosine-sampled bounce, shaded where it hits from the scene (its draw's texture times its
// vertex or draw colour, the table's area emission), up to g_reference_bounces, or the sky's light where it leaves
// (past the backdrop distance as well). The lights are the real-time passes' own: the sun's light by the cosine, the
// game's attenuation, the glow lights' power / (pi (d^2 + r^2)) to their reach without the soft cap, the sky light's
// harmonics; every colour is decoded from the display encoding to linear light. The result is the irradiance over pi
// that a white surface would show (1: as drawn), summed in float32 per sample while the camera holds still; a sample
// whose depth changes (an actor moving through it) starts again on its own. Late geometry (instance 1: Rosalina)
// has no records: a path that meets it ends there.
// ---------------------------------------------------------------------------------------------

static const uint kReferenceFlag = 8192u;

// The sRGB transfer functions (IEC 61966-2-1).
float3 srgb_to_linear(float3 c)
{
    c = max(c, 0.0f);
    return lerp(pow((c + 0.055f) / 1.055f, 2.4f), c / 12.92f, step(c, 0.04045f));
}

float3 linear_to_srgb(float3 c)
{
    c = max(c, 0.0f);
    return lerp(1.055f * pow(c, 1.0f / 2.4f) - 0.055f, 12.92f * c, step(c, 0.0031308f));
}

// A light's colour decoded to linear light at its own peak (its brightness kept: light colours past 1 are intensities).
float3 linear_hue(float3 c)
{
    const float peak = max(c.r, max(c.g, c.b));
    return peak > 1e-6f ? srgb_to_linear(c / peak) * peak : float3(0.0f, 0.0f, 0.0f);
}

uint pcg_hash(uint v)
{
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random(inout uint seed)
{
    seed = pcg_hash(seed);
    return float(seed >> 8u) * (1.0f / 16777216.0f);
}

float2 random2(inout uint seed)
{
    const float a = random(seed);
    return float2(a, random(seed));
}

// The sky and bounce light's level under the sky's state (SpatialBCS's `ambient`).
float reference_ambient()
{
    float ambient = g_sun_light.w;
    if (g_picture_sums.Load(36u) != 0u) {
        ambient *= lerp(g_ambient.x, 1.0f, saturate(asfloat(g_picture_sums.Load(32u)) / g_ambient.y));
    }
    return ambient;
}

// Game point light `i` at `p` (normal `n`): its light by the game's attenuation (TraceCS with g_point_light), its
// emitter's centre and radius.
bool reference_point(uint i, float3 p, float3 n, out float3 light, out float3 centre, out float radius)
{
    centre = g_points[2u * i].xyz;
    radius = g_point_radius[i >> 2u][i & 3u];
    light = float3(0.0f, 0.0f, 0.0f);
    const float3 to = centre - p;
    const float d2 = dot(to, to);
    const float nl = dot(n, to) * rsqrt(max(d2, 1e-6f));
    if (nl <= 0.0f) {
        return false;
    }
    light = linear_hue(g_points[2u * i + 1u].rgb) * (nl * point_falloff(d2, i) * g_point_light);
    return luminance(light) > 0.0f;
}

// Glow light `c` of the frame's list at `p` (normal `n`): power / (pi (d^2 + r^2)) windowed to its reach, its cone
// (TraceCS without the soft cap), its centre and radius.
bool reference_glow(uint c, float3 p, float3 n, out float3 light, out float3 centre, out float radius)
{
    const float4 la = asfloat(g_emitters.Load4(16u + c * kEmitStride));
    const float3 power = asfloat(g_emitters.Load3(16u + c * kEmitStride + 16u));
    centre = transform(g_to_eye, la.xyz);
    radius = la.w;
    light = float3(0.0f, 0.0f, 0.0f);
    const float3 to = centre - p;
    const float d2 = dot(to, to);
    const float reach2 = emit_reach2(luminance(power));
    if (d2 >= reach2) {
        return false;
    }
    const float inv_d = rsqrt(max(d2, 1e-6f));
    const float nl = dot(n, to) * inv_d;
    if (nl <= 0.0f) {
        return false;
    }
    const float4 spot = asfloat(g_emitters.Load4(16u + c * kEmitStride + 32u));
    const float cone = spot_factor(float4(rotate3(g_to_eye, spot.xyz), spot.w), -to * inv_d);
    if (cone <= 0.0f) {
        return false;
    }
    const float x = d2 / reach2;
    light = linear_hue(power) * (cone * nl * (1.0f - x * x) * (1.0f - x * x) * g_emit.z / (kPi * (d2 + la.w * la.w)));
    return luminance(light) > 0.0f;
}

// The lights' light at `p` (normal `n`, rays from `start`): one light chosen in proportion to its light here, its
// shadow ray, over its chance. `tiled`: the group's tile list holds every glow light in reach of `p` (the primary
// surface); else the whole list.
float3 reference_lights(float3 start, float3 p, float3 n, bool tiled, inout uint seed, uint salt)
{
    const uint glows = min(g_emitters.Load(0u), kTileWords * 32u);
    float total = 0.0f;
    float3 light;
    float3 centre;
    float radius;
    [loop] for (uint i = 0u; i < g_point_count; ++i) {
        if (reference_point(i, p, n, light, centre, radius)) {
            total += luminance(light);
        }
    }
    [loop] for (uint word = 0u; word < kTileWords && word * 32u < glows; ++word) {
        const uint left = glows - word * 32u;
        [loop] for (uint bits = tiled ? s_tile_mask[word] : left >= 32u ? 0xFFFFFFFFu : (1u << left) - 1u; bits != 0u;
                    bits &= bits - 1u) {
            if (reference_glow(word * 32u + firstbitlow(bits), p, n, light, centre, radius)) {
                total += luminance(light);
            }
        }
    }
    if (total <= 0.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    // The light where the running sum passes the pick.
    float pick = random(seed) * total;
    bool found = false;
    float3 chosen = float3(0.0f, 0.0f, 0.0f);
    float3 chosen_centre = float3(0.0f, 0.0f, 0.0f);
    float chosen_radius = 0.0f;
    [loop] for (uint i = 0u; i < g_point_count && !found; ++i) {
        if (reference_point(i, p, n, light, centre, radius)) {
            pick -= luminance(light);
            if (pick <= 0.0f) {
                found = true;
                chosen = light;
                chosen_centre = centre;
                chosen_radius = radius;
            }
        }
    }
    [loop] for (uint word = 0u; word < kTileWords && word * 32u < glows && !found; ++word) {
        const uint left = glows - word * 32u;
        [loop] for (uint bits = tiled ? s_tile_mask[word] : left >= 32u ? 0xFFFFFFFFu : (1u << left) - 1u;
                    bits != 0u && !found; bits &= bits - 1u) {
            if (reference_glow(word * 32u + firstbitlow(bits), p, n, light, centre, radius)) {
                pick -= luminance(light);
                if (pick <= 0.0f) {
                    found = true;
                    chosen = light;
                    chosen_centre = centre;
                    chosen_radius = radius;
                }
            }
        }
    }
    if (!found || !emitter_visible(start, chosen_centre, chosen_radius, random2(seed), salt)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return chosen * (total / max(luminance(chosen), 1e-8f));
}

// The sun's light at a point facing `n` from `origin`: its light by the cosine where one ray inside its disc reaches it.
float3 reference_sun(float3 origin, float3 n, bool card, inout uint seed, uint salt)
{
    const float facing = dot(n, g_sun.xyz);
    if (facing <= 0.0f || !any(g_sun_light.rgb > 0.0f)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float3 l = g_sun.xyz;
    const float lz = l.z >= 0.0f ? 1.0f : -1.0f;
    const float la = -1.0f / (lz + l.z);
    const float lb = l.x * l.y * la;
    const float3 l1 = float3(1.0f + lz * l.x * l.x * la, lz * lb, -lz * l.x);
    const float3 l2 = float3(lb, lz + l.y * l.y * la, -l.y);
    const float2 u = random2(seed);
    const float r = sqrt(u.x) * g_sun.w;
    const float phi = 2.0f * kPi * u.y;
    float blocker;
    return sun_visible(origin, normalize(l + l1 * (r * cos(phi)) + l2 * (r * sin(phi))), card, salt, blocker)
               ? linear_hue(g_sun_light.rgb) * facing
               : float3(0.0f, 0.0f, 0.0f);
}

// What a ray hits on triangle `tri` (barycentrics `b`): its draw's TEV output at the hit (rt_tev.hlsli), and the table's
// area emission, in linear light.
void reference_material(uint tri, float2 b, out float3 albedo, out float3 emission)
{
    albedo = float3(0.5f, 0.5f, 0.5f);
    emission = float3(0.0f, 0.0f, 0.0f);
    const uint draw = g_surfaces.Load(tri * 16u + 12u) >> 16u;
#if RT_TEXTURED
    // The draw's own TEV program at the hit (its colour channels at full light under kPhysicalFlag).
    if (draw < g_tev_info.x) {
        bool pass;
        albedo = tev_evaluate(draw, g_tev_info.x, tri, b, pass).rgb;
    }
#endif
    albedo = srgb_to_linear(saturate(albedo));
    const uint2 table = g_draws.Load2(min(draw, max(g_draw_count, 1u) - 1u) * kDrawStride + 8u);
    if ((table.x & kEmitNamed) != 0u && (table.x & 3u) == kEmitArea && g_emit.w > 0.0f) {
        emission = srgb_to_linear(rgb8(table.x)) * (g_self.z * asfloat(table.y) * g_emit.w);
    }
}

// A bounce ray's hit in the game's light units (TraceCS with kPhysicalFlag): the surface's albedo times the sun on it
// (N.L, one shadow ray inside the sun's disc) and the open sky, plus its emission.
float3 physical_hit(uint tri, float2 b, float3 x, float3 direction, uint salt)
{
    float3 albedo;
    float3 emission;
    reference_material(tri, b, albedo, emission);
    const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
    const float3 v1 = asfloat(g_positions.Load3(tri * 36u + 12u));
    const float3 v2 = asfloat(g_positions.Load3(tri * 36u + 24u));
    const float3 face = cross(v1 - v0, v2 - v0);
    if (dot(face, face) <= 1e-24f) {
        return emission;
    }
    float3 ng = normalize(face);
    ng = dot(ng, direction) > 0.0f ? -ng : ng;
    const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
    float3 lifted = v0 * w.x + v1 * w.y + v2 * w.z;
    float3 n;
    smooth_surface(tri, w, v0, v1, v2, ng, n, lifted);
    float3 sun = float3(0.0f, 0.0f, 0.0f);
    const float nl = dot(n, g_sun.xyz);
    if (nl > 0.0f && any(g_sun_light.rgb > 0.0f)) {
        float blocker;
        if (sun_visible(lifted + ng * (0.2f + abs(x.z) * 4e-4f), g_sun.xyz, false, salt, blocker)) {
            sun = g_sun_light.rgb * nl;
        }
    }
    return albedo * (sun + physical_sky_irradiance(n)) + emission;
}

#if RT_TEXTURED

// A GX blend factor (BlendFactor) on the source side (its colour factors name the destination) or the destination's.
// The frame's alpha under the sky counts as full.
float3 sky_blend_factor(uint f, float4 src, float3 dst, bool source_side)
{
    const float3 other = source_side ? dst : src.rgb;
    return f == 0u   ? float3(0.0f, 0.0f, 0.0f)
           : f == 1u ? float3(1.0f, 1.0f, 1.0f)
           : f == 2u ? other
           : f == 3u ? 1.0f - other
           : f == 4u ? src.aaa
           : f == 5u ? 1.0f - src.aaa
           : f == 6u ? float3(1.0f, 1.0f, 1.0f)
                     : float3(0.0f, 0.0f, 0.0f);
}

// The traced sky (kPhysicalFlag): per map texel four rays from the camera through the game's sky lists' geometry alone
// (the sky probe's slots: g_scene the sky's structure, g_surfaces, g_tri_uv, g_draw_tex and g_tev its draws'), every
// layer a ray meets shaded by its own TEV program and composited in the order the game drew them with the game's own
// blending, display-encoded as the raster does, then averaged in linear light in units of the stage's full light
// (g_reference: the map).
[numthreads(8, 8, 1)]
void SkyProbeCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= kSkyMapSize)) {
        return;
    }
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [loop] for (uint s = 0u; s < 4u; ++s) {
        const float2 uv = (float2(id.xy) + float2((s & 1u) != 0u ? 0.75f : 0.25f, (s & 2u) != 0u ? 0.75f : 0.25f)) /
                          float(kSkyMapSize);
        const float3 world = sky_map_direction(uv);
        RayDesc ray;
        ray.Origin = float3(0.0f, 0.0f, 0.0f);
        ray.Direction = float3(dot(g_to_eye[0].xyz, world), dot(g_to_eye[1].xyz, world), dot(g_to_eye[2].xyz, world));
        ray.TMin = 0.0f;
        ray.TMax = 1.0e30f;
        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
        uint tris[kSkyLayers];
        float2 bary[kSkyLayers];
        uint n = 0u;
        while (query.Proceed()) {
            // In the order drawn (the sky's triangles are noted in draw order).
            const uint tri = query.CandidatePrimitiveIndex();
            const float2 b = query.CandidateTriangleBarycentrics();
            if (n < kSkyLayers) {
                uint k = n;
                [loop] while (k > 0u && tris[k - 1u] > tri) {
                    tris[k] = tris[k - 1u];
                    bary[k] = bary[k - 1u];
                    --k;
                }
                tris[k] = tri;
                bary[k] = b;
                ++n;
            }
        }
        float3 c = float3(0.0f, 0.0f, 0.0f);
        [loop] for (uint i = 0u; i < n; ++i) {
            // The layer's own TEV program and alpha compare (g_tev: the sky's records), blended as its draw's
            // record says (g_draw_tex: RtDrawInfo.blend).
            const uint record = g_surfaces.Load(tris[i] * 16u + 12u) >> 16u;
            const uint blend = g_draw_tex[record].y >> 1u;
            if ((blend & 0x100u) == 0u || (blend & 0x200u) != 0u) {
                continue;
            }
            bool pass;
            const float4 src = tev_evaluate(record, g_tev_info.y, tris[i], bary[i], pass);
            if (!pass) {
                continue;
            }
            if ((blend & 1u) == 0u) {
                c = src.rgb;
            } else if ((blend & 0x80u) != 0u) {
                c = saturate(c - src.rgb);
            } else {
                c = saturate(src.rgb * sky_blend_factor((blend >> 1u) & 7u, src, c, true) +
                             c * sky_blend_factor((blend >> 4u) & 7u, src, c, false));
            }
        }
        sum += srgb_to_linear(c);
    }
    g_reference[id.y * kSkyMapSize + id.x] = float4(sum * 0.25f, 1.0f);
}
#endif

groupshared float s_sky_trace[64][28];

// The traced sky's spherical harmonics (after its map in g_reference): each texel by its solid angle (the octahedron's
// area element over the cube of its point's distance), normalised to the sphere.
[numthreads(64, 1, 1)]
void SkyTraceShCS(uint gi : SV_GroupIndex)
{
    float sums[28];
    [unroll] for (uint k = 0u; k < 28u; ++k) {
        sums[k] = 0.0f;
    }
    for (uint t = gi; t < kSkyMapSize * kSkyMapSize; t += 64u) {
        const float2 e = (float2(t % kSkyMapSize, t / kSkyMapSize) + 0.5f) / float(kSkyMapSize) * 2.0f - 1.0f;
        float3 v = float3(e.x, 1.0f - abs(e.x) - abs(e.y), e.y);
        if (v.y < 0.0f) {
            v.xz = (1.0f - abs(v.zx)) * float2(v.x >= 0.0f ? 1.0f : -1.0f, v.z >= 0.0f ? 1.0f : -1.0f);
        }
        const float l = length(v);
        const float solid = 1.0f / (l * l * l);
        const float3 radiance = g_reference[t].rgb;
        float basis[9];
        sh9(v / l, basis);
        [unroll] for (uint b = 0u; b < 9u; ++b) {
            sums[b * 3u + 0u] += radiance.r * basis[b] * solid;
            sums[b * 3u + 1u] += radiance.g * basis[b] * solid;
            sums[b * 3u + 2u] += radiance.b * basis[b] * solid;
        }
        sums[27] += solid;
    }
    [unroll] for (uint j = 0u; j < 28u; ++j) {
        s_sky_trace[gi][j] = sums[j];
    }
    GroupMemoryBarrierWithGroupSync();
    if (gi < 9u) {
        float3 coefficient = float3(0.0f, 0.0f, 0.0f);
        float area = 0.0f;
        for (uint w = 0u; w < 64u; ++w) {
            coefficient += float3(s_sky_trace[w][gi * 3u], s_sky_trace[w][gi * 3u + 1u], s_sky_trace[w][gi * 3u + 2u]);
            area += s_sky_trace[w][27];
        }
        g_reference[kSkyMapSize * kSkyMapSize + gi] = float4(coefficient * (4.0f * kPi / max(area, 1e-20f)), 0.0f);
    }
}

[numthreads(8, 8, 1)]
void ReferenceCS(uint3 id : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    const bool in_range = all(id.xy < g_sample_size);
    const uint index = id.y * g_sample_size.x + id.x;
    const int2 q = sample_pixel(min(id.xy, g_sample_size - 1u));
    float3 p;
    const bool surface = pixel_point(q, p) && inside(g_viewport, float2(q) + 0.5f) && -p.z <= g_ambient.w;
    CULL_TILE_LIGHTS(g_emitters, g_to_eye, gi, in_range && surface, p, emit_reach2)
    if (!in_range) {
        return;
    }
    float4 sum = g_reference[2u * index];
    float4 at = g_reference[2u * index + 1u];
    const float z = surface ? p.z : 0.0f;
    if (g_reference_reset != 0u || abs(at.x - z) > 0.01f * abs(z) + 0.5f) {
        sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
        at = float4(z, 0.0f, 0.0f, 0.0f);
    }
    if (!surface) {
        g_reference[2u * index] = sum;
        g_reference[2u * index + 1u] = at;
        return;
    }
    const Surface s = primary_surface(q, p);
    uint seed = pcg_hash(index ^ pcg_hash(g_frame * 0x9E3779B9u + 0x7F4A7C15u));
    const uint salt = index * 32u + g_frame * 0x632BE5ABu;
    const float3 tint = sky_tint();
    float3 sky_sh[9];
    const bool sky_known = sky_light_load(sky_sh);
    const float ambient = reference_ambient();
    bool card = false;
    if (s.primary.x != 0u) {
        const uint own = g_surfaces.Load((s.primary.x - 1u) * 16u + 12u);
        card = (g_draws.Load(min(own >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 24u) & kDrawTableFoliage) != 0u;
    }
    float3 position = s.lifted + s.ng * (0.2f + abs(p.z) * 4e-4f);
    float3 at_point = p;
    float3 n = s.n;
    float3 ng = s.ng;
    float3 throughput = float3(1.0f, 1.0f, 1.0f);
    float3 light = float3(0.0f, 0.0f, 0.0f);
    [loop] for (uint bounce = 0u; bounce <= g_reference_bounces; ++bounce) {
        light += throughput * (reference_sun(position, n, card, seed, salt + 1u + 4u * bounce) +
                               reference_lights(position, at_point, n, bounce == 0u, seed, salt + 2u + 4u * bounce));
        if (bounce == g_reference_bounces) {
            break;
        }
        // A cosine-weighted direction about the shading normal, never into the face.
        const float sign_z = n.z >= 0.0f ? 1.0f : -1.0f;
        const float ta = -1.0f / (sign_z + n.z);
        const float tb = n.x * n.y * ta;
        const float3 t1 = float3(1.0f + sign_z * n.x * n.x * ta, sign_z * tb, -sign_z * n.x);
        const float3 t2 = float3(tb, sign_z + n.y * n.y * ta, -n.y);
        const float2 u = random2(seed);
        const float r = sqrt(u.x);
        const float phi = 2.0f * kPi * u.y;
        float3 direction = t1 * (r * cos(phi)) + t2 * (r * sin(phi)) + n * sqrt(max(0.0f, 1.0f - u.x));
        const float below = dot(direction, ng);
        direction = below < 0.0f ? direction - 2.0f * below * ng : direction;
        RayDesc ray;
        ray.Origin = position;
        ray.Direction = direction;
        ray.TMin = 0.0f;
        ray.TMax = 1.0e6f;
        RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
        while (query.Proceed()) {
            const bool late_hit = query.CandidateInstanceID() != 0u;
            const uint hit = late_hit ? 0u : g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
            if (late_hit || ((hit & kDrawNoShadow) == 0u &&
                             card_blocks(query.CandidatePrimitiveIndex(), hit,
                                         position + direction * query.CandidateTriangleRayT(), salt + 3u + 4u * bounce))) {
                query.CommitNonOpaqueTriangleHit();
            }
        }
        const float3 x = position + direction * query.CommittedRayT();
        if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT || length(x) > g_ambient.w) {
            light += throughput * ((g_flags & kPhysicalFlag) != 0u
                                       ? physical_sky(direction)
                                       : linear_hue(sky_light(direction, tint, sky_sh, sky_known)) * ambient);
            break;
        }
        if (query.CommittedInstanceID() != 0u) {
            break;
        }
        const uint tri = query.CommittedPrimitiveIndex();
        const float2 b = query.CommittedTriangleBarycentrics();
        float3 albedo;
        float3 emission;
        reference_material(tri, b, albedo, emission);
        light += throughput * emission;
        throughput *= albedo;
        // Russian roulette after the first bounce, by what is left to carry.
        if (bounce >= 1u) {
            const float keep = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05f, 1.0f);
            if (random(seed) >= keep) {
                break;
            }
            throughput /= keep;
        }
        const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
        const float3 v1 = asfloat(g_positions.Load3(tri * 36u + 12u));
        const float3 v2 = asfloat(g_positions.Load3(tri * 36u + 24u));
        const float3 face = cross(v1 - v0, v2 - v0);
        if (dot(face, face) <= 1e-24f) {
            break;
        }
        ng = normalize(face);
        ng = dot(ng, direction) > 0.0f ? -ng : ng;
        const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
        float3 lifted = v0 * w.x + v1 * w.y + v2 * w.z;
        smooth_surface(tri, w, v0, v1, v2, ng, n, lifted);
        at_point = x;
        position = lifted + ng * (0.2f + abs(x.z) * 4e-4f);
        card = false;
    }
    sum += float4(light, 1.0f);
    g_reference[2u * index] = sum;
    g_reference[2u * index + 1u] = at;
}

// One group after ReferenceCS: the exposure that brings the geometric mean of the reference's light to 1 (times
// g_reference_exposure), within 1/64-64, after the samples (SpatialBCS reads it).
groupshared float s_exposure_sum[1024];
groupshared float s_exposure_count[1024];

[numthreads(1024, 1, 1)]
void ReferenceExposureCS(uint gi : SV_GroupIndex)
{
    const uint total = g_sample_size.x * g_sample_size.y;
    float sum = 0.0f;
    float count = 0.0f;
    for (uint i = gi; i < total; i += 1024u) {
        const float4 a = g_reference[2u * i];
        if (a.w > 0.0f) {
            sum += log(max(luminance(a.rgb / a.w), 1e-4f));
            count += 1.0f;
        }
    }
    s_exposure_sum[gi] = sum;
    s_exposure_count[gi] = count;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint step = 512u; step > 0u; step >>= 1u) {
        if (gi < step) {
            s_exposure_sum[gi] += s_exposure_sum[gi + step];
            s_exposure_count[gi] += s_exposure_count[gi + step];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0u) {
        const float mean = s_exposure_count[0] > 0.0f ? s_exposure_sum[0] / s_exposure_count[0] : 0.0f;
        const float exposure = (g_flags & kPhysicalFlag) != 0u ? g_reference_exposure * physical_exposure()
                                                               : clamp(g_reference_exposure * exp(-mean), 1.0f / 64.0f, 64.0f);
        g_reference[2u * total] = float4(exposure, 0.0f, 0.0f, 0.0f);
    }
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
            const float2 a = (eye_to_pixel(g_history_projection, g_viewport, previous) - 0.5f) / float(g_factor);
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
                if (sz == 0.0f ||
                    abs(dot(normal, eye_position(g_history_projection, g_viewport, sample_centre(uint2(s)), sz) - previous)) >
                        tolerance) {
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

// The sun's shadow at the width of its penumbra (R07, after NVIDIA SIGMA and PCSS): 2 d tan(sun
// radius) for the nearest blocker distance d in the 3 x 3 around (a lit sample beside an edge takes its
// neighbour's), in samples. Under half a sample the accumulated shadow stays as traced (contact
// shadows crisp); wider penumbrae are averaged over a Gaussian of that radius, up to 2 samples.
// Blocker distances ride in the count half of g_raw, which this pass rewrites with the same value.
float sun_at_penumbra(uint2 id, float3 p, float3 n, float own)
{
    float blocker = 0.0f;
    [unroll] for (int by = -1; by <= 1; ++by) {
        [unroll] for (int bx = -1; bx <= 1; ++bx) {
            const int2 s = int2(id) + int2(bx, by);
            if (any(s < 0) || any(s >= int2(g_sample_size))) {
                continue;
            }
            const float b = f16tof32(g_raw[uint(s.y) * g_sample_size.x + uint(s.x)].w >> 16u);
            blocker = b > 0.0f && (blocker == 0.0f || b < blocker) ? b : blocker;
        }
    }
    const float spacing = abs(p.z) * 2.0f * float(g_factor) / max(abs(g_projection.x) * g_viewport.z, 1e-4f);
    const float radius = blocker > 0.0f ? 2.0f * blocker * g_sun.w / max(spacing, 1e-4f) : 0.0f;
    if (radius < 0.5f) {
        return own;
    }
    const float sigma = min(radius, 2.0f);
    const float spread = 0.5f / (sigma * sigma);
    const float inv_tolerance = 1.0f / (0.25f + abs(p.z) * 0.004f);
    float sum = 0.0f;
    float weight = 0.0f;
    [unroll] for (int dy = -2; dy <= 2; ++dy) {
        [unroll] for (int dx = -2; dx <= 2; ++dx) {
            const int2 s = int2(id) + int2(dx, dy);
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
            const float w = exp(-spread * float(dx * dx + dy * dy) - plane * plane) * (facing2 * facing2) *
                            (facing2 * facing2);
            float count;
            sum += unpack_light(g_history_in[si], count).sun * w;
            weight += w;
        }
    }
    return weight > 1e-4f ? sum / weight : own;
}

[numthreads(8, 8, 1)]
void SpatialACS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_sample_size)) {
        return;
    }
    const uint index = id.y * g_sample_size.x + id.x;
    const float blocker = f16tof32(g_raw[index].w >> 16u);
    const float z = g_z[index];
    if (z == 0.0f) {
        g_raw[index] = pack_light(kOpenSky, blocker);
        return;
    }
    Light l = filtered(id.xy, 1, true);
    float own_count;
    const float own = unpack_light(g_history_in[index], own_count).sun;
    l.sun = sun_at_penumbra(id.xy, sample_position(id.xy, z), unpack_normal(g_normal[index]), own);
    g_raw[index] = pack_light(l, blocker);
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
    Light l = filtered(id.xy, 2, false);
    float own_count;
    l.sun = unpack_light(g_raw[index], own_count).sun;   // the sun stays as the first pass left it
    g_history[index] = pack_light(l, 0.0f);
}

// Contact occlusion: ground truth based ambient occlusion (GTAO, Jimenez et al. 2016, as XeGTAO
// evaluates it) on the light samples. Four fixed slices of the view (8 screen directions); per side 3
// steps at g_ao.x x (k/3)^2 eye units projected onto the sample grid (at most 96 samples): the highest
// horizon toward the camera, faded with the occluder's distance ((1 - (d / reach)^2)^2 towards the
// tangent); the cosine-weighted visible arc between the two horizons, clamped to the normal's
// hemisphere, weighted by the normal's length in the slice. 1 open, 0 closed. Fixed slices and steps:
// the same geometry gives the same value every frame (no noise to filter, no shimmer).
float fast_acos(float x)
{
    const float a = abs(x);
    const float r = (-0.156583f * a + kPi * 0.5f) * sqrt(1.0f - a);
    return x >= 0.0f ? r : kPi - r;
}

float contact_occlusion(uint2 id, float3 p, float3 n)
{
    const float reach = g_ao.x;
    // Eye units between samples at this depth.
    const float spacing = abs(p.z) * 2.0f * float(g_factor) / max(abs(g_projection.x) * g_viewport.z, 1e-4f);
    const float steps = clamp(reach / max(spacing, 1e-4f), 1.5f, 96.0f);
    const float inv_reach2 = 1.0f / (reach * reach);
    const float3 view = -normalize(p);
    float visible = 0.0f;
    [unroll] for (uint slice = 0u; slice < 4u; ++slice) {
        const float angle = (float(slice) + 0.5f) * (kPi / 4.0f);
        const float2 screen = float2(cos(angle), sin(angle));
        const float3 direction = float3(screen.x, -screen.y, 0.0f);   // screen y runs down, eye y up
        const float3 axis = normalize(cross(direction, view));
        const float3 ortho = direction - view * dot(direction, view);
        const float3 projected = n - axis * dot(n, axis);
        const float length_n = length(projected);
        const float cos_n = saturate(dot(projected, view) / max(length_n, 1e-4f));
        const float gamma = (dot(ortho, projected) >= 0.0f ? 1.0f : -1.0f) * fast_acos(cos_n);
        float horizon[2];
        [unroll] for (uint side = 0u; side < 2u; ++side) {
            const float sign = side == 0u ? 1.0f : -1.0f;
            const float low = cos(gamma + sign * kPi * 0.5f);
            float best = low;
            [unroll] for (uint k = 1u; k <= 3u; ++k) {
                const float fraction = float(k * k) / 9.0f;
                const int2 s = int2(id) + int2(round(screen * (sign * steps * fraction)));
                if (any(s < 0) || any(s >= int2(g_sample_size))) {
                    continue;
                }
                const float sz = g_z[uint(s.y) * g_sample_size.x + uint(s.x)];
                if (sz == 0.0f) {
                    continue;
                }
                const float3 v = sample_position(uint2(s), sz) - p;
                const float d2 = dot(v, v);
                if (d2 < 1e-4f) {
                    continue;
                }
                const float x = saturate(d2 * inv_reach2);
                const float c = dot(v, view) * rsqrt(d2);
                best = max(best, lerp(low, c, (1.0f - x) * (1.0f - x)));
            }
            horizon[side] = best;
        }
        // h1 towards +direction, h0 towards -direction (angles from the view vector).
        const float h1 = gamma + clamp(fast_acos(horizon[0]) - gamma, -kPi * 0.5f, kPi * 0.5f);
        const float h0 = gamma + clamp(-fast_acos(horizon[1]) - gamma, -kPi * 0.5f, kPi * 0.5f);
        const float sin_g = sin(gamma);
        const float arc0 = (cos_n + 2.0f * h0 * sin_g - cos(2.0f * h0 - gamma)) * 0.25f;
        const float arc1 = (cos_n + 2.0f * h1 * sin_g - cos(2.0f * h1 - gamma)) * 0.25f;
        visible += length_n * (arc0 + arc1);
    }
    return saturate(visible / 4.0f);
}

// The depth at a sample is its traced surface: its primary triangle lies at the sample's eye z (within
// 0.1 %, the validation's match). Surfaces the trace leaves out or holds only nearby carry depth the art
// paints flat (Snow Cap's snow floor, a drawer's bumpy grid a few units over its traced ground: contact
// occlusion speckled it with pits).
bool traced_depth(uint2 id, float z)
{
    const uint primary = g_primary[id.y * g_sample_size.x + id.x].x;
    if (primary == 0u) {
        return false;
    }
    // The triangle's plane along the sample's camera ray.
    const uint at = (primary - 1u) * 36u;
    const float3 v0 = asfloat(g_positions.Load3(at));
    const float3 face = cross(asfloat(g_positions.Load3(at + 12u)) - v0, asfloat(g_positions.Load3(at + 24u)) - v0);
    const float3 d = pixel_direction(g_projection, g_viewport, sample_centre(id));
    const float facing = dot(face, d);
    if (!(abs(facing) > 1e-6f * length(face) * length(d))) {
        return true;
    }
    return abs(-dot(face, v0) / facing - z) <= 1e-3f * abs(z) + 0.25f;
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
    if ((g_flags & kReferenceFlag) != 0u) {
        // The reference's light at its exposure, in linear light (the composite's m_flags bit 2). A light source's
        // own surface glows over it as the lit model's does: an area light by its glow, a point light's at 1 + 0.3 x
        // its intensity.
        const float4 sum = g_reference[2u * index];
        float3 m = (sum.w > 0.0f ? sum.rgb / sum.w : float3(0.0f, 0.0f, 0.0f)) *
                   g_reference[2u * g_sample_size.x * g_sample_size.y].x;
        const uint tri = g_primary[index].x;
        if (tri != 0u) {
            const uint record = min(g_surfaces.Load((tri - 1u) * 16u + 12u) >> 16u, max(g_draw_count, 1u) - 1u) *
                                kDrawStride;
            const uint2 emission = g_draws.Load2(record + 8u);
            if ((emission.x & kEmitNamed) != 0u && (emission.x & 3u) != 0u && (emission.x & kEmitNoLight) == 0u) {
                const uint table_flags = g_draws.Load(record + 24u);
                const float intensity = (table_flags & kDrawTableGlow) != 0u
                                            ? float(table_flags >> 16u) / 65535.0f * kDrawGlowScale
                                            : asfloat(emission.y);
                m += ((emission.x & 3u) == kEmitArea ? g_self.x * intensity : 0.3f * min(intensity, 3.0f)).xxx;
            }
        }
        m = min(m, kMaxLight);
        g_final[index] = uint4(f32tof16(m.r) | (f32tof16(m.g) << 16u), f32tof16(m.b) | (f32tof16(1.0f) << 16u), asuint(z),
                               0u);
        return;
    }
    Light light = filtered(id.xy, 4, true);
    if ((g_flags & kPhysicalFlag) != 0u) {
        // The game's light units: the sky and bounce light, the sun's (its irradiance over pi times N.L and its
        // visibility) and the point lights', summed; the composite multiplies the full-light colour by it in linear
        // light. A light source's own surface glows over it as the reference's does. The sun's shadow at its
        // penumbra's width (SpatialACS, kept by SpatialMCS), not the sky's wider filter; its visibility (the sun over
        // this sample's facing) with the exposure in w's low half: the composite turns the sun to each pixel's normal.
        float own_count;
        light.sun = unpack_light(g_history_in[index], own_count).sun;
        const float facing = dot(unpack_normal(g_normal[index]), g_sun.xyz);
        const float exposure = physical_exposure();
        const float visible = facing > 1e-3f ? saturate(light.sun / facing) * exposure : 0.0f;
        float3 m = (light.sky + g_sun_light.rgb * light.sun + light.points * g_point_light) * exposure;
        const uint tri = g_primary[index].x;
        if (tri != 0u) {
            const uint record = min(g_surfaces.Load((tri - 1u) * 16u + 12u) >> 16u, max(g_draw_count, 1u) - 1u) *
                                kDrawStride;
            const uint2 emission = g_draws.Load2(record + 8u);
            if ((emission.x & kEmitNamed) != 0u && (emission.x & 3u) != 0u && (emission.x & kEmitNoLight) == 0u) {
                const uint table_flags = g_draws.Load(record + 24u);
                const float intensity = (table_flags & kDrawTableGlow) != 0u
                                            ? float(table_flags >> 16u) / 65535.0f * kDrawGlowScale
                                            : asfloat(emission.y);
                m += ((emission.x & 3u) == kEmitArea ? g_self.x * intensity : 0.3f * min(intensity, 3.0f)).xxx;
            }
        }
        m = min(m, kMaxLight);
        g_final[index] = uint4(f32tof16(m.r) | (f32tof16(m.g) << 16u), f32tof16(m.b) | (f32tof16(1.0f) << 16u), asuint(z),
                               f32tof16(visible));
        return;
    }
    {
        float own_count;
        light.sun = unpack_light(g_history_in[index], own_count).sun;   // sharp sun shadows (SpatialMCS)
    }
    if ((g_flags & 128u) != 0u) {
        light.sky = float3(1.0f, 1.0f, 1.0f);  // GALAXY_RT_GI=0: the sky open, no bounce
    }
    // Contact occlusion: corners, contacts and creases the sky and bounce light reach less of. Never on
    // the sun or the lamps and glows (their shadows are traced).
    float contact = 1.0f;
    if (g_ao.x > 0.0f && (g_ao.y > 0.0f || g_ao.z > 0.0f) && traced_depth(id.xy, z)) {
        contact = contact_occlusion(id.xy, sample_position(id.xy, z), unpack_normal(g_normal[index]));
    }
    const float3 sky_seen = light.sky * (1.0f - g_ao.z * (1.0f - contact));
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
    float3 lighting = (ambient * sky_seen + sun) / norm + points;
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
        // Its colour is its material: the traced light is its light (the unlit form above), with a fill
        // towards the camera where the sun does not reach.
        const float3 n = unpack_normal(g_normal[index]);
        const float toward = saturate(dot(n, -normalize(sample_position(id.xy, z))));
        const float shade = 1.0f - saturate(light.sun * 1.25f);
        lighting += (0.3f * toward * shade).xxx;
        open += (0.3f * toward * shade).xxx;
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
        float3 traced = sky_a * sky_seen + k * light.sun;
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
            lighting = (ambient * sky_seen + sun + points) / max(unshadowed, 1e-3f);
            open = (ambient + sun + points) / max(unshadowed, 1e-3f);
        } else {
            lighting = (ambient * sky_seen + sun) / max(unshadowed, 1e-3f) + added;
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
    // Daytime: the game's art as it draws it, with modern shadows on it. The art is painted for even
    // light; a physical sun either bleached it or darkened it (user: keep the art style and direction,
    // the sun very faint, just more shadows; research R03 ratio relighting, R14 Nintendo). Open, sunlit
    // surfaces stay exactly as drawn; the sun's cast shadows take them to kDayShadow, the sky's
    // occlusion darkens creases and contacts, shade takes a little of the sky's colour, and faces
    // turned from the sun keep kDayForm of their light (a faint sun). The lamps and glows still add
    // their light; no exposure. Under a dark sky the lit model above (the nights as they are).
    // Surfaces the traced scene leaves out (alpha-tested cut-outs other than foliage cards, the snow
    // floor's drawer) take it too, by their depth's normal: on the lit model they turned dark by day
    // (Snow Cap's snowmen grey, its snow speckled where samples met the ground's triangle or none).
    // GALAXY_RT_VIEW=8: the day model's sun visibility, the sun's facing (shaped n.l) and the sky's
    // luminance.
    float3 day_terms = float3(0.0f, 0.0f, 0.0f);
    {
        // Every stage on the art model (build 49): the night lit model re-shaded art past the game's (fc46:
        // Freezeflame's ice x1.25, Battlerock's discs, the Observatory; rtpartner rp13: 7 of 9 night stops).
        // By night the lamps and glows add their light in full, by day a third.
        const float day_share = g_self.w >= 0.0f ? g_self.w : smoothstep(0.35f, 0.8f, day_sky);
        const float day_mix = 1.0f;
        if (day_mix > 0.0f) {
            const float3 n = unpack_normal(g_normal[index]);
            const float nl = dot(n, g_sun.xyz);
            const float shaped = nl > 0.0f ? nl * (1.5f - 0.5f * nl) : 0.0f;
            // The sun's cast shadow: its traced term over its facing (faces it does not reach count as
            // lit; the art lit them).
            const float vis = any(g_sun_light.rgb > 0.0f) && shaped > 0.08f ? saturate(light.sun / shaped) : 1.0f;
            const float shadow = 1.0f - kDayShadow * (1.0f - vis);
            // Without a sun (rt-suns none: Melty Molten) no faint-sun facing either (the fallback direction
            // darkened faces away from it: Melty 0.78 of the game, measured rtpartner rp10).
            const float form = any(g_sun_light.rgb > 0.0f) ? 1.0f - kDayForm * (1.0f - saturate(shaped / 0.8f)) : 1.0f;
            const float sky_l = luminance(light.sky);
            const bool foliage = tri != 0u && (g_draws.Load(min(draw_flags >> 16u, max(g_draw_count, 1u) - 1u) *
                                               kDrawStride + 24u) & kDrawTableFoliage) != 0u;
            // The characters and objects the game lights carry their form in their own shading: their sky and
            // contact occlusion at 0.4 (by day Mario fell to about 0.58 of the game under it, user: too dark).
            const bool lit_draw = (draw_flags & (kDrawLit | kDrawDelit)) != 0u;
            const float occlusion_share = lit_draw ? 0.4f : foliage ? 0.5f : 1.0f;
            const float ao = 1.0f - kDayOcclusion * occlusion_share * (1.0f - saturate(sky_l));
            const float3 tint = lerp(float3(1.0f, 1.0f, 1.0f), light.sky / max(sky_l, 0.05f), kDayTint * (1.0f - vis));
            // A de-lit draw (its GX light averaged at a mean facing of 0.55, shader_gen) back at the game's own
            // shading of this face: its ambient plus its light 0 at the face's facing to it, clamped as GX's
            // accumulator. (Its full-facing level on every face raised the faces the game shades x1.18-1.20,
            // measured fc43.) Its facing is then the game's: no faint-sun form on it.
            float3 level = float3(1.0f, 1.0f, 1.0f);
            float day_form = lit_draw ? 1.0f : form;
            if (delit) {
                const uint record = min(draw_flags >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride;
                const uint2 colors = g_draws.Load2(record);
                const float3 a = rgb8(colors.x);
                const float3 k = rgb8(colors.y);
                const uint kind = g_draws.Load(record + 44u);
                if (kind != 0u) {
                    const float3 to_light = asfloat(g_draws.Load3(record + 32u)) - sample_position(id.xy, z);
                    const float facing = kind == 1u ? 1.0f : saturate(dot(n, to_light * rsqrt(max(dot(to_light, to_light), 1e-6f))));
                    const float3 averaged = max(min(a + (kind == 1u ? 1.0f : 0.55f) * k, 1.0f), 0.05f);
                    level = clamp(min(a + k * facing, 1.0f) / averaged, 0.0f, 2.0f);
                    day_form = 1.0f;
                }
            }
            // By day the lights add a third of their light (daylight swamps it; the game's glow sprites
            // found 56-192 lights in Honeyhive and lifted it to 1.23 of the game, measured fc33), and nothing
            // passes 1.1 of its art's level: no overexposed grass, sand or dirt. The light sources' own glow
            // (below) is not affected.
            // Contact occlusion on the art: in full where the sun does not reach, half where it does
            // (sunlit contacts keep their direct light).
            const float sunlit = vis * saturate(shaped / 0.5f);
            // By day half their light, outside the art's cap, rolling off towards 0.35 on sunlit ground (no
            // bleaching) and towards 1.0 where the sun does not reach: a star bit or a sling star lights the
            // shade and the dark stone near it (user: glowing things not emissive onto what is around them;
            // a star bit 100 units over shaded ground: +0.15 at the 0.35 roll-off, +0.33 at 1.0).
            const float3 day_lights = points * 0.5f;
            const float3 day_points = day_lights / (1.0f + luminance(day_lights) / lerp(1.0f, 0.35f, sunlit));
            const float day_contact = 1.0f - g_ao.y * occlusion_share * (1.0f - contact) * (1.0f - 0.5f * sunlit);
            // Map geometry (no GX light: ground, grass, sand, wood, trees) at the environment's level, under
            // the characters and objects the game lights: the sunlit grass no longer outshines Mario.
            const float art_level = (draw_flags & (kDrawLit | kDrawDelit)) != 0u ? kDayLevel : g_night2.y;
            float3 day_m = min(shadow * day_form * ao * day_contact * tint * level, 1.1f * level) * art_level + day_points;
            day_terms = float3(vis, shaped, saturate(sky_l));
            float3 day_open = min((shadow * day_form) * level, 1.1f * level) * art_level + day_points;
            // Night (no daylight): the art darker in the open (g_night.x), the traced sky occlusion and the
            // contact occlusion carry the depth (corners, creases, under and around objects), the key's
            // (moon's) cast shadows go deeper, and the traced lights add their light uncapped by the art
            // (lamps, Lumas, star bits and the star core light what they reach and cast shadows), rolling off
            // softly towards g_ao.w. (The art model's 1.1 cap held every light at +10 %: the Observatory's
            // core, lamps and Lumas lit nothing; Rosalina and the Lumas cast no shadow.)
            if (day_share < 1.0f) {
                const float occlusion = 1.0f - g_night.y * occlusion_share * (1.0f - saturate(sky_l));
                const float night_contact = 1.0f - g_ao.z * occlusion_share * (1.0f - contact);
                const float night_shadow = 1.0f - g_night.z * (1.0f - vis);
                // The key (moon, a red star) lights the faces it reaches: its light by the shaped facing and
                // its traced visibility, with the lamps under the soft cap.
                const float3 lights = points * g_night.w + g_sun_light.rgb * (shaped * vis * g_night2.x);
                // Where the star core's light rules (its share of the point lights' unshadowed light here) the cap
                // rises towards g_sun_cap: the core lights its surroundings as a sun, the lamps keep their cap.
                float cap = g_ao.w;
                if (g_core_point < g_point_count) {
                    const float3 sp = sample_position(id.xy, z);
                    const float3 sn = unpack_normal(g_normal[index]);
                    float core_weight = 0.0f;
                    float all_weight = 0.0f;
                    [loop] for (uint i = 0u; i < g_point_count; ++i) {
                        const float3 to = g_points[2u * i].xyz - sp;
                        const float d2 = dot(to, to);
                        const float w = luminance(g_points[2u * i + 1u].rgb) *
                                        saturate(dot(sn, to) * rsqrt(max(d2, 1e-6f))) * point_falloff(d2, i);
                        all_weight += w;
                        core_weight += i == g_core_point ? w : 0.0f;
                    }
                    cap = lerp(g_ao.w, max(g_sun_cap, g_ao.w), all_weight > 0.0f ? core_weight / all_weight : 0.0f);
                }
                const float3 added = soft_light(lights, max(cap, 1e-3f));
                const float3 art = night_shadow * day_form * tint * level * g_night.x;
                day_m = lerp(art * occlusion * night_contact + added, day_m, day_share);
                day_open = lerp(night_shadow * day_form * level * g_night.x + added, day_open, day_share);
            }
            lighting = lerp(lighting, day_m, day_mix);
            open = lerp(open, day_open, day_mix);
        }
    }
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
                // A light source is lit by itself: no occlusion or night darkening on its own surface, and it
                // glows over its colour with its glow (Luma 1 -> 1.3, sling star 2.5 -> 1.75, at most 1.9); the
                // film shoulder takes it towards white-hot.
                m = max(m, (1.0f + 0.3f * min(intensity, 3.0f)).xxx);
            }
            const float glow = kind == kEmitArea ? g_self.x * intensity : 0.0f;
            if (glow > 0.0f) {
                // In its own colour (lava stays red-orange; white for the draw-tinted ones).
                const float3 tint = rgb8(emission.x);
                const float3 hue = tint / max(max(tint.r, tint.g), max(tint.b, 1e-3f));
                if ((table_flags & kDrawTableFoliage) != 0u) {
                    // A glowing plant (Gusty Garden's flowers) keeps its occlusion and shadows, its glow
                    // added to them, with a small halo.
                    m = min(m + glow * hue, kMaxLight);
                    halo = kPlantHalo * intensity;
                } else {
                    m = min(max(m, 1.0f) + glow * hue, kMaxLight);
                }
            }
        }
    }
    // A star's burning surface: the composite shades it from its own fire on its ball (CompositePS star_surface);
    // its relief share carries the mark, -1 less its star slot (StarSphereCS; -1 - kStarSlots: no ball), its light
    // (above) how hot it burns.
    if (tri != 0u &&
        (g_draws.Load(min(draw_flags >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 24u) & kDrawTableStar) != 0u) {
        uint star = 0u;
        while (star < kStarSlots && tri - 1u - g_star_first[star >> 2u][star & 3u] >= g_star_count[star >> 2u][star & 3u]) {
            ++star;
        }
        relief = -1.0f - float(star);
    }
    // Metal shows the world in its painted reflection: under the diffuse light it darkens half as much (measured
    // mb0: the giant ? coin 209,188,39 -> 179,140,30, Bubble Blast's gold frame darker).
    if (((draw_flags >> 8u) & 15u) == 2u) {
        m = max(m, 0.5f + 0.5f * m);
    }
    // Clear crystal and glass show what lies behind them (the game's refraction of the picture): the light
    // at their surface does not darken that; the lights in and around them add to it (a crystal's star
    // bits light it from within: TraceCS takes their light through it).
    if (((draw_flags >> 8u) & 15u) == kClearClass) {
        m = max(m, 1.0f + points);
        // Lit from within, the crystal itself glows (its halo joins the bloom around it).
        halo = max(halo, 0.5f * saturate(luminance(points)));
    }
    // A glowing surface (unlit, bright, under its own glow in the previous picture: a core, a lamp)
    // sends its light whatever reaches it: it keeps its colour and shines brighter.
    if ((draw_flags & kDrawLit) == 0u && (g_flags & 2u) != 0u) {
        const float3 previous = transform(g_picture, sample_position(id.xy, z));
        if (previous.z < -g_near) {
            const float2 s = (eye_to_pixel(g_picture_projection, g_viewport, previous) - 0.5f) / float(g_factor) + 0.5f;
            if (all(s >= 0.0f) && all(s < float2(g_sample_size))) {
                const uint si = uint(s.y) * g_sample_size.x + uint(s.x);
                const float pz = asfloat(g_picture_in[si].y);
                if (pz != 0.0f && abs(pz - previous.z) <= 0.03f * abs(previous.z) + 2.0f) {
                    const float3 base = unpack_r11g11b10(g_glow_base[si]);
                    const float added = luminance(unpack_r11g11b10(g_glow_added[si]));
                    const float glowing = smoothstep(0.1f, 0.4f, added) *
                                          smoothstep(0.55f, 0.85f, max(base.r, max(base.g, base.b))) *
                                          (1.0f - smoothstep(0.35f, 0.8f, day_sky));
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
    } else if ((g_flags & 256u) != 0u) {
        m = contact.xxx;       // GALAXY_RT_VIEW=7: the contact occlusion
    } else if ((g_flags & 512u) != 0u) {
        m = day_terms;         // GALAXY_RT_VIEW=8: the day model's sun visibility, facing, sky
    }
    // The picture capture divides out the sky and bounce part (no feedback through it).
    const float3 without = lerp(float3(1.0f, 1.0f, 1.0f), open, g_strength);
    const float keep = max(luminance(without) / max(luminance(m), 1e-3f), 1e-3f);
    g_final[index] = uint4(f32tof16(m.r) | (f32tof16(m.g) << 16u), f32tof16(m.b) | (f32tof16(keep) << 16u), asuint(z),
                           f32tof16(relief) | (f32tof16(halo) << 16u));
}

groupshared float4 s_star_a[64];
groupshared float4 s_star_b[64];
groupshared float4 s_star_c[64];

void star_reduce(uint gi)
{
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint s = 32u; s > 0u; s >>= 1u) {
        if (gi < s) {
            s_star_a[gi] += s_star_a[gi + s];
            s_star_b[gi] += s_star_b[gi + s];
            s_star_c[gi] += s_star_c[gi + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }
}

// A star's ball (the material table's `star` draws: the Observatory's core, Melty Molten's suns): the sphere
// through its eye-space vertices, the least-squares fit of |q|^2 = 2 q.c + d (Pratt 1987), exact for any part of
// a ball. Its model is a low-poly ball whose flat faces the depth shows; the composite shades the round ball the
// faces approximate (CompositePS star_mu). The vertices are taken about their mean, in units of their mean
// distance from it, so the sums keep their precision far from the camera. One group per star slot; written
// after the samples (kStarSlots), radius 0 where its triangles give no ball.
[numthreads(64, 1, 1)]
void StarSphereCS(uint3 group : SV_GroupID, uint gi : SV_GroupIndex)
{
    const uint star = group.x;
    const uint first = g_star_first[star >> 2u][star & 3u];
    const uint count = g_star_count[star >> 2u][star & 3u];
    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (uint t = gi; t < count; t += 64u) {
        [unroll] for (uint k = 0u; k < 3u; ++k) {
            const float3 v = asfloat(g_positions.Load3((first + t) * 36u + k * 12u));
            if (all(abs(v) < 1.0e6f)) {
                sum += float4(v, 1.0f);
            }
        }
    }
    s_star_a[gi] = sum;
    s_star_b[gi] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    s_star_c[gi] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    star_reduce(gi);
    const float n = s_star_a[0].w;
    const float3 mean = n > 0.0f ? s_star_a[0].xyz / n : float3(0.0f, 0.0f, 0.0f);
    GroupMemoryBarrierWithGroupSync();
    float spread = 0.0f;
    for (uint t = gi; t < count; t += 64u) {
        [unroll] for (uint k = 0u; k < 3u; ++k) {
            const float3 v = asfloat(g_positions.Load3((first + t) * 36u + k * 12u));
            if (all(abs(v) < 1.0e6f)) {
                spread += length(v - mean);
            }
        }
    }
    s_star_a[gi] = float4(spread, 0.0f, 0.0f, 0.0f);
    star_reduce(gi);
    const float scale = n > 0.0f ? s_star_a[0].x / n : 0.0f;
    GroupMemoryBarrierWithGroupSync();
    // Per vertex q (about the mean, in units of scale): q q^T (xx xy xz yy | yz zz), q |q|^2, |q|^2.
    float4 a = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 b = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 c = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (scale > 0.0f) {
        for (uint t = gi; t < count; t += 64u) {
            [unroll] for (uint k = 0u; k < 3u; ++k) {
                const float3 v = asfloat(g_positions.Load3((first + t) * 36u + k * 12u));
                if (all(abs(v) < 1.0e6f)) {
                    const float3 q = (v - mean) / scale;
                    const float qq = dot(q, q);
                    a += float4(q.x * q.x, q.x * q.y, q.x * q.z, q.y * q.y);
                    b += float4(q.y * q.z, q.z * q.z, qq, 0.0f);
                    c += float4(q * qq, 0.0f);
                }
            }
        }
    }
    s_star_a[gi] = a;
    s_star_b[gi] = b;
    s_star_c[gi] = c;
    star_reduce(gi);
    if (gi != 0u) {
        return;
    }
    float4 ball = float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (scale > 0.0f && n >= 12.0f) {
        // About the mean the sum of q vanishes: 4 S c = 2 sum(q |q|^2) and d = mean |q|^2, radius^2 = d + |c|^2.
        const float3 r0 = float3(s_star_a[0].x, s_star_a[0].y, s_star_a[0].z) / n;
        const float3 r1 = float3(s_star_a[0].y, s_star_a[0].w, s_star_b[0].x) / n;
        const float3 r2 = float3(s_star_a[0].z, s_star_b[0].x, s_star_b[0].y) / n;
        const float3 rhs = 0.5f * s_star_c[0].xyz / n;
        const float3 c0 = cross(r1, r2);
        const float3 c1 = cross(r2, r0);
        const float3 c2 = cross(r0, r1);
        const float det = dot(r0, c0);
        // A ball's vertices spread on every axis (det near 1/27); a flat or linear set gives none.
        if (det > 1e-4f) {
            const float3 centre = (c0 * rhs.x + c1 * rhs.y + c2 * rhs.z) / det;
            const float radius2 = s_star_b[0].z / n + dot(centre, centre);
            if (radius2 > 0.0f) {
                ball = float4(mean + centre * scale, sqrt(radius2) * scale);
            }
        }
    }
    g_final[g_sample_size.x * g_sample_size.y + star] = asuint(ball);
}

// kPhysicalFlag: per EFB pixel the shading normal of the surface its camera ray meets at the raster's depth (the
// smooth vertex normals, primary_surface), for the composite's per-pixel sun. A ray per pixel, once per traced frame;
// the frame's in-between pictures read it where their pixels lay in it.
#if RT_TEXTURED
// Coordinate `u` of an axis under GX addressing `mode` (0 clamp, 1 repeat, 2 mirror), for a repeating sampler: clamped
// and mirrored coordinates kept half a texel (`half`) inside so the filter never reaches the far edge.
float delight_coordinate(float u, uint mode, float half)
{
    if (mode == 1u) {
        return u;
    }
    if (mode == 2u) {
        u = 1.0f - abs(frac(u * 0.5f) * 2.0f - 1.0f);
    }
    return clamp(u, half, 1.0f - half);
}

float2 delight_uv(float2 uv, uint wrap, float2 half)
{
    return float2(delight_coordinate(uv.x, wrap & 3u, half.x), delight_coordinate(uv.y, (wrap >> 2u) & 3u, half.y));
}

// The relief's height for painted shading `s` (log units): saturating at +-kReliefSpan, so a painted outline or a deep
// painted shadow (a dark plank border, measured Good Egg build delight3) is a shallow groove, not a cliff.
static const float kReliefSpan = 0.35f;
// The steepest relief slope (tan 20 degrees): the painted light says little of shape, and a sun 20 degrees up or more
// never leaves a surface black for it. Steeper, bent normals turned whole bands of a surface from the sun (the dark
// ring along Good Egg's rim: 75 degrees in delight2/3, still a line at 35 in delight4).
static const float kReliefSlope = 0.36f;

float relief_height(float s)
{
    return kReliefSpan * tanh(s / kReliefSpan);
}

// The light painted into the material texture of triangle `tri` at barycentrics `b` (its de-light map, record pad[1]
// and pad[2]; rt_delight.hlsl), at the level a pixel `pixel_width` eye units wide at `p` covers there (a ray cone,
// Akenine-Moller et al. 2021: the triangle's texels per eye unit, the footprint widened by its slant to the view), and
// the relief it implies: its height (relief_height, g_up.w texels per log unit) bends the shading normal `n` by its
// surface gradient on the triangle's texture axes (Mikkelsen 2020), at most kReliefSlope. False without a map.
bool delight_surface(uint tri, float2 b, float3 p, float3 ng, float pixel_width, inout float3 n, out float shading)
{
    shading = 0.0f;
    const uint draw = g_surfaces.Load(tri * 16u + 12u) >> 16u;
    if (g_up.w <= 0.0f || draw >= g_tev_info.x) {
        return false;
    }
    const uint record = draw * kTevRecordBytes;
    const uint view = g_tev.Load(record + 112u);
    if (view == 0xFFFFFFFFu) {
        return false;
    }
    const uint info = g_tev.Load(record + 116u);
    const uint slot = (info >> 3u) & 1u;
    const uint wrap = (info >> 4u) & 0xFu;
    const float2 scale = asfloat(g_tev.Load2(record + 120u));
    const uint a = tri * kTriUvStride + slot * 24u;
    const float4 uv01 = asfloat(g_tri_uv.Load4(a));
    const float2 t0 = uv01.xy * scale;
    const float2 t1 = uv01.zw * scale;
    const float2 t2 = asfloat(g_tri_uv.Load2(a + 16u)) * scale;
    const float2 uv = t0 * (1.0f - b.x - b.y) + t1 * b.x + t2 * b.y;
    if (any(abs(uv) >= 1.0e4f)) {
        return false;
    }
    const Texture2D<float> map = ResourceDescriptorHeap[NonUniformResourceIndex(view)];
    uint width, height, levels;
    map.GetDimensions(0u, width, height, levels);
    const float2 size = float2(width, height);
    const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
    const float3 e1 = asfloat(g_positions.Load3(tri * 36u + 12u)) - v0;
    const float3 e2 = asfloat(g_positions.Load3(tri * 36u + 24u)) - v0;
    const float2 d1 = (t1 - t0) * size;
    const float2 d2 = (t2 - t0) * size;
    const float det = d1.x * d2.y - d2.x * d1.y;
    const float world_area = length(cross(e1, e2));
    if (abs(det) < 1e-8f || world_area < 1e-12f) {
        return false;
    }
    const float slant = max(abs(dot(ng, normalize(p))), 0.05f);
    const float lod = clamp(0.5f * log2(abs(det) / world_area) + log2(pixel_width / slant), 0.0f, float(levels - 1u));
    // Its gradient at that level (central differences a texel of it apart), per level-0 texel, under the draw's
    // addressing (a clamped texture's edge never meets the far edge).
    const float step = exp2(lod);
    const float2 half = 0.5f * step / size;
    shading = map.SampleLevel(g_wrap_linear, delight_uv(uv, wrap, half), lod) * kDelightRange;
    const float2 du = float2(step / size.x, 0.0f);
    const float2 dv = float2(0.0f, step / size.y);
    const float su = (relief_height(map.SampleLevel(g_wrap_linear, delight_uv(uv + du, wrap, half), lod) * kDelightRange) -
                      relief_height(map.SampleLevel(g_wrap_linear, delight_uv(uv - du, wrap, half), lod) * kDelightRange)) /
                     (2.0f * step);
    const float sv = (relief_height(map.SampleLevel(g_wrap_linear, delight_uv(uv + dv, wrap, half), lod) * kDelightRange) -
                      relief_height(map.SampleLevel(g_wrap_linear, delight_uv(uv - dv, wrap, half), lod) * kDelightRange)) /
                     (2.0f * step);
    // The texture's axes on the surface (eye units per texel) in the shading normal's plane; the height in eye units
    // g_up.w x shading x the texel's size, so the slope is g_up.w x the shading's change per texel.
    float3 axis_u = (e1 * d2.y - e2 * d1.y) / det;
    float3 axis_v = (e2 * d1.x - e1 * d2.x) / det;
    axis_u -= n * dot(n, axis_u);
    axis_v -= n * dot(n, axis_v);
    const float texel = sqrt(length(axis_u) * length(axis_v));
    const float3 r1 = cross(axis_v, n);
    const float3 r2 = cross(n, axis_u);
    const float d = dot(axis_u, r1);
    if (abs(d) > 1e-20f) {
        float3 slope = (g_up.w * texel) * (su * r1 + sv * r2) / d;
        const float steep = length(slope);
        slope *= steep > kReliefSlope ? kReliefSlope / steep : 1.0f;
        n = normalize(n - slope);
    }
    return true;
}
#endif

[numthreads(8, 8, 1)]
void GBufferCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const int2 q = int2(id.xy);
    float3 p;
    uint2 packed = uint2(0u, 0u);
    if (pixel_point(q, p) && inside(g_viewport, float2(q) + 0.5f)) {
        const Surface s = primary_surface(q, p);
        packed.x = pack_normal(s.n);
#if RT_TEXTURED
        if (s.primary.x != 0u) {
            const float3 next = eye_position(g_projection, g_viewport, float2(q) + float2(1.5f, 0.5f), p.z);
            float3 bent = s.n;
            float shading;
            if (delight_surface(s.primary.x - 1u, float2(f16tof32(s.primary.y), f16tof32(s.primary.y >> 16u)), p, s.ng,
                                length(next - p), bent, shading)) {
                packed.y = pack_pixel_delight(bent, shading);
            }
        }
#endif
    }
    g_pixels[id.y * g_target_size.x + id.x] = packed;
}

// ---------------------------------------------------------------------------------------------
// The lighting as a texture for the translucent draws after the trace (one texel per sample).
// ---------------------------------------------------------------------------------------------

cbuffer RtLightTexture : register(b0)
{
    uint2 t_size;
    uint2 t_pad;
    float4 t_full;   // the stage's full light in linear light (physical; else 1)
};
StructuredBuffer<uint4> t_final : register(t0);

float4 LightTexturePS(float4 position : SV_Position) : SV_Target0
{
    const uint2 s = min(uint2(position.xy), t_size - 1u);
    const uint4 v = t_final[s.y * t_size.x + s.x];
    if (asfloat(v.z) == 0.0f) {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    // A blended translucent (seaweed, glass, leaf cards) takes the light of the surface behind it, and
    // that surface lies in the translucent's own shadow: it keeps at most half of the darkening (more
    // light it takes whole). Measured wb33/wb36: Beach Bowl's seaweed 115,111,36 -> 27,52,25.
    // Physical: the translucents are drawn in the game's light, the traced light is in its units (white 1).
    const float3 m = float3(f16tof32(v.x), f16tof32(v.x >> 16u), f16tof32(v.y)) / max(t_full.rgb, 1e-3f);
    return float4(max(m, 0.5f + 0.5f * m), 1.0f);
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
                    // bit 1: show the lighting alone; bit 2: light in linear light (the reference)
    uint m_slot;
    float m_near;
    float m_split;  // GALAXY_RT_SPLIT: left of it the game's own picture (a dark line between)
    float m_efb_scale;  // EFB pixels per game pixel (the internal resolution)
    float4 m_sun;        // towards the traced sun (eye space, traced frame); relief height (eye units)
    float4 m_sun_light;  // its light (rgb)
    uint2 m_target_size; // the traced frame's EFB pixels (m_pixels)
};
StructuredBuffer<uint4> m_light : register(t0);
Texture2D<float> m_efb_depth : register(t1);
StructuredBuffer<float4> m_reproject : register(t2);
Texture2D<float4> m_color : register(t3);
// m_flags bit 3 (physical): the traced frame's per-pixel shading normals and de-lighting (GBufferCS).
StructuredBuffer<uint2> m_pixels : register(t4);

// The traced frame's shading normal at its pixel `q`.
float3 traced_normal(int2 q)
{
    const int2 c = clamp(q, int2(0, 0), int2(m_target_size) - 1);
    return unpack_normal(m_pixels[uint(c.y) * m_target_size.x + uint(c.x)].x);
}

// Its de-lighting there (pack_pixel_delight; 0: none).
uint traced_delight(int2 q)
{
    const int2 c = clamp(q, int2(0, 0), int2(m_target_size) - 1);
    return m_pixels[uint(c.y) * m_target_size.x + uint(c.x)].y;
}

struct CompositeOut
{
    float4 color : SV_Target0;
};

float3 apply_light(float3 c, float3 m)
{
    return film_light(c, m);
}

// The turn to the eye of star `star`'s ball at eye position `p` (traced frame): the ball's normal where the eye
// ray first meets it (StarSphereCS's sphere through its vertices) against the direction to the eye. Its model's
// faces are flat and so are its vertex normals: a turn taken from either is constant over each face (user: the
// suns show square facets, Native44). The actor's position is not the ball's centre (build 60). Where the ray
// passes the fitted sphere (a model not quite round) the surface point the depth gives; 0.6 without a ball.
float star_mu(float3 p, uint star)
{
    if (star >= kStarSlots) {
        return 0.6f;
    }
    const float4 ball = asfloat(m_light[m_sample_size.x * m_sample_size.y + star]);
    if (ball.w <= 0.0f) {
        return 0.6f;
    }
    const float3 d = normalize(p);
    const float along = dot(ball.xyz, d);
    const float3 off = ball.xyz - along * d;
    const float h = ball.w * ball.w - dot(off, off);
    float3 s = p;
    if (h > 0.0f && along - sqrt(h) > 0.0f) {
        s = (along - sqrt(h)) * d;
    }
    return saturate(dot(normalize(s - ball.xyz), -d));
}

// A star's burning surface (the star core, the suns): the game's own colour at each stage (the Observatory's core
// runs from red to blue as the domes revive it), a little brighter and with its texture twice as deep (the colour
// against its neighbourhood, per channel: where the game's colour is at full brightness in some channel, the lit
// core's green and blue, a brightness ratio clipped away and left it flatter than the game's, s.d. 6.4 against
// 9.5). The ball brightens towards its centre and dims at its limb (mu: the ball's turn to the eye, star_mu). Past
// full brightness it keeps its hue and the excess whitens it a little.
static const float kStarDetail = 1.0f;

float3 star_surface(int2 c, float z, float mu, float3 color)
{
    // Its neighbourhood 3 game pixels around at every internal resolution (3 EFB pixels were half a game pixel
    // at 6x: no contrast, the fire's texture washed out under the heat, user: suns without their texture), on
    // its own surface only (what stands in front, a ring or a character, drew a bright or dark line along it).
    const int r = max(int(round(3.0f * m_efb_scale)), 1);
    const int2 ring[8] = {int2(-1, -1), int2(0, -1), int2(1, -1), int2(-1, 0), int2(1, 0), int2(-1, 1), int2(0, 1), int2(1, 1)};
    const float limit = abs(z) * 0.02f + 2.0f;
    float3 around = float3(0.0f, 0.0f, 0.0f);
    float count = 0.0f;
    [unroll] for (uint k = 0u; k < 8u; ++k) {
        const int2 q = c + ring[k] * r;
        if (abs(depth_to_eye_z(m_depth, m_efb_depth.Load(int3(q, 0))) - z) < limit) {
            around += m_color.Load(int3(q, 0)).rgb;
            count += 1.0f;
        }
    }
    const float3 detail = count > 0.0f ? color - around / count : float3(0.0f, 0.0f, 0.0f);
    // Limb (mu 0.2) 1.01 of the game's colour, centre 1.38; over the disc about 1.2. The detail joins after the
    // brightness is brought back under full: scaled with it, the lanes of a nearly full colour rose towards its
    // cells (s.d. 8.6 against the game's 9.5).
    const float3 lit = color * (1.15f * (0.8f + 0.4f * mu));
    const float peak = max(lit.r, max(lit.g, lit.b));
    return saturate(lit / max(peak, 1.0f) + max(peak - 1.0f, 0.0f) * 0.2f + detail * kStarDetail);
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
    // The surface's depth slope per pixel: on each axis the one-sided step to the neighbour nearer in depth
    // (across a silhouette the other side's step is not this surface's).
    float2 gradient;
    {
        const int2 c = int2(position.xy);
        const float zr = depth_to_eye_z(m_depth, m_efb_depth.Load(int3(c + int2(1, 0), 0)));
        const float zl = depth_to_eye_z(m_depth, m_efb_depth.Load(int3(c - int2(1, 0), 0)));
        const float zd = depth_to_eye_z(m_depth, m_efb_depth.Load(int3(c + int2(0, 1), 0)));
        const float zu = depth_to_eye_z(m_depth, m_efb_depth.Load(int3(c - int2(0, 1), 0)));
        gradient.x = abs(zr - z) < abs(z - zl) ? zr - z : z - zl;
        gradient.y = abs(zd - z) < abs(z - zu) ? zd - z : z - zu;
        gradient = clamp(gradient, -abs(z) * 0.05f, abs(z) * 0.05f);
    }
    // The light samples around the pixel (3 x 3 about the nearest, a tent 1.5 samples wide), each weighed by
    // its depth against this surface's plane extended to it: samples on another surface (a step's face, an
    // object in front) drop out and those on this one blend smoothly, so a shadow or crease edge follows the
    // geometry's full-resolution edge instead of the light's sample grid (user: shadow and depth clipping at
    // the edges in the Observatory, build 55; the 4-tap bilinear with a flat depth test switched samples per
    // pixel along the floor rings' steps).
    const int2 centre = int2(floor(a + 0.5f));
    const float tolerance = abs(p.z) * 0.004f + 0.25f + (abs(gradient.x) + abs(gradient.y)) * float(m_factor) * 0.5f;
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float relief_sum = 0.0f;
    float weight = 0.0f;
    float3 nearest = float3(1.0f, 1.0f, 1.0f);
    float nearest_relief = 0.0f;
    float nearest_dz = abs(p.z) * 0.1f;
    // Physical (bit 3): the sun turned from each sample's facing to this pixel's own normal (the samples' visibility
    // with the exposure in w's low half, their facing from the per-pixel normals at their pixels).
    // The pixel's own normal is its texture's relief where it has a de-light map; the samples' facing stays their
    // smooth normal's, as their light was found.
    const bool per_pixel = (m_flags & 8u) != 0u;
    const int2 traced_pixel = int2(eye_to_pixel(m_projection, m_viewport, p));
    const uint delit = per_pixel ? traced_delight(traced_pixel) : 0u;
    const float pixel_facing =
        per_pixel ? max(dot(delit != 0u ? unpack_pixel_normal(delit) : traced_normal(traced_pixel), m_sun.xyz), 0.0f)
                  : 0.0f;
    float sun_turn = 0.0f;
    float nearest_turn = 0.0f;
    [unroll] for (uint k = 0u; k < 9u; ++k) {
        const int2 o = int2(int(k % 3u) - 1, int(k / 3u) - 1);
        const int2 s = clamp(centre + o, int2(0, 0), int2(m_sample_size) - 1);
        const uint4 sample = m_light[uint(s.y) * m_sample_size.x + uint(s.x)];
        const float sz = asfloat(sample.z);
        if (sz == 0.0f) {
            continue;
        }
        const float3 m = float3(f16tof32(sample.x), f16tof32(sample.x >> 16u), f16tof32(sample.y));
        const float2 offset = float2(centre + o) - a;
        const float dz = abs(sz - (p.z + dot(gradient, offset * float(m_factor))));
        float turn = 0.0f;
        if (per_pixel) {
            const float visible = f16tof32(sample.w & 0xFFFFu);
            const float facing = max(dot(traced_normal(int2(min(uint2(s) * m_factor, m_target_size - 1u))), m_sun.xyz),
                                     0.0f);
            turn = visible * (pixel_facing - facing);
        }
        if (dz < nearest_dz) {
            nearest_dz = dz;
            nearest = m;
            nearest_relief = f16tof32(sample.w & 0xFFFFu);
            nearest_turn = turn;
        }
        const float d = dz / tolerance;
        const float2 tent = max(1.5f - abs(offset), 0.0f);
        const float bw = tent.x * tent.y * exp(-d * d);
        sum += m * bw;
        relief_sum += f16tof32(sample.w & 0xFFFFu) * bw;
        sun_turn += turn * bw;
        weight += bw;
    }
    float3 m = clamp(weight > 1e-3f ? sum / weight : nearest, 0.0f, kMaxLight);
    if (per_pixel) {
        // The per-pixel sun, then the film path (no relief: the normals are the surface's own).
        m = clamp(m + m_sun_light.rgb * (weight > 1e-3f ? sun_turn / weight : nearest_turn), 0.0f, kMaxLight);
        if ((m_flags & 2u) != 0u) {
            result.color = float4(m * 0.5f, 1.0f);
            return result;
        }
        // The texel's albedo: the light painted into it taken out (rt_delight.hlsl), the traced light in its place.
        const float3 albedo = srgb_to_linear(color) * (delit != 0u ? exp(-unpack_pixel_shading(delit)) : 1.0f);
        result.color = float4(linear_to_srgb(film_light(albedo, m)), 1.0f);
        return result;
    }
    // Relief: the texture's detail as height (its brightness, m_sun.w eye units at white) bends the
    // pixel's normal (the surface gradient of the faceted normal from depth, Mikkelsen 2010); the
    // sun's light follows the bent normal where the sample sees the sun. Across a depth edge that
    // side's difference is left out; with distance a pixel spans more than the detail and it fades.
    const float relief = weight > 1e-3f ? relief_sum / weight : 0.0f;
    // A star (its mark and slot from the sample on this surface: blending would mix the slots).
    if (nearest_relief < -0.5f && (m_flags & 2u) == 0u) {
        const float mu = star_mu(p, uint(round(-nearest_relief - 1.0f)));
        result.color = float4(star_surface(int2(position.xy), z, mu, color), 1.0f);
        return result;
    }
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
    if ((m_flags & 4u) != 0u) {
        // The reference: the colour decoded to linear light, lit, the film shoulder, encoded for display.
        result.color = float4(linear_to_srgb(film_light(srgb_to_linear(color), m)), 1.0f);
        return result;
    }
    result.color = float4(apply_light(color, m), 1.0f);
    return result;
}
