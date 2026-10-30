// SPDX-License-Identifier: GPL-3.0-only
// Ray-traced lighting of the game frame's opaque scene (DXR 1.1 inline ray queries, cs_6_5) and
// its composite into the EFB (vs/ps_6_0).
//
// Each sample traces the sun (a shadow ray towards its disc), one point light picked in proportion to its
// unshadowed light, and sky and bounce rays: a ray that leaves the scene brings the sky's light, one that hits
// geometry brings the light that surface sends back, read from the previous frame's finished picture. The
// surface's colour is multiplied by the weighted sum.
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
    // The previous samples' jitter less this frame's (render pixels): the raster's motion is the surface's own, without
    // the jitter, and the previous samples were taken with theirs (rt_engine.hlsl engine_history).
    float2 g_motion_jitter;
    float4 g_point_radius[kPointLights / 4u];   // per point light (4 a vector): its emitter's radius
    uint g_reference_bounces;   // the reference's bounces
    float g_reference_exposure; // its exposure over the geometric mean of its light
    uint g_reference_reset;     // 1: it starts again this frame (the camera moved)
    uint g_emit_scene;          // the scene's emitting triangles among g_tev_info.z (the rest: glow emitters)
    float4 g_history_projection;   // the previous samples' projection (their frame's jitter)
    float4 g_picture_projection;   // the previous picture's
    float4 g_sky_radiance;         // kPhysicalFlag: the sky's radiance, the stage's ambient in linear light (rgb)
    float4 g_full_light;           // the stage's full light in linear light (rgb); 1: g_sky_map holds the traced sky (w)
    uint4 g_tev_info;              // TEV records (g_tev) of the scene's draws, of the sky's draws (the sky probe)
    float4 g_up;                   // the player's up in eye space by the game's gravity (else the view's up); the de-light
                                   // maps' relief height in texels per log unit (w; 0: off)
    uint4 g_probe;                 // GALAXY_RT_PROBE: the probed sample (x, y), on (z); GALAXY_RT_LIGHT_METER: its disc
                                   // (w: centre x, y in engine pixels, bits 0-11, 12-23; radius / 4, bits 24-31; 0 off)
    float4 g_light_fit;            // physical: the camera's exposure (x; 0: physical_exposure's own), the light
                                   // fitted to the baked light (y: 1)
    // The ray tracing engine (rt_engine.hlsl; ray_tracing.h RtEngineConstants).
    uint4 g_engine_views[8];       // its buffers in the descriptor heap (kEv*)
    uint4 g_engine_size;           // full width, height; half width, height
    uint4 g_engine_info;           // flags (kEngine*), a-trous step, slot, a-trous parity
    float4 g_engine_params;
    float4 g_engine_water;         // the camera in water (x: 1), its depth (y, eye units): water_medium
    uint4 g_engine_paint;          // the table metals' painted colour per draw (rt_engine.hlsl EnginePaintCS): its
                                   // buffer's view (~0: none), the traced scene's triangles, the draws it holds
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
ByteAddressBuffer g_picture_sums : register(t12);        // sky r g b n, scene r g b n (x 1/255),
                                                         // the sky's luminance over time, known
ByteAddressBuffer g_positions : register(t13);           // per triangle: its eye-space vertices
ByteAddressBuffer g_surfaces : register(t14);            // per triangle: vertex normals, draw flags
ByteAddressBuffer g_draws : register(t18);              // per draw (kDrawStride)
RWStructuredBuffer<float> g_z : register(u0);
RWStructuredBuffer<uint> g_normal : register(u1);
RWStructuredBuffer<uint4> g_final : register(u4);
RWStructuredBuffer<uint2> g_primary : register(u5);      // triangle + 1 (0: none), barycentrics
// The emitter passes' output (EmitTriangleCS, EmitScanCS: g_emit_tris's layout) and the traced sky's (SkyProbeCS,
// SkyTraceShCS: g_sky_map's).
RWStructuredBuffer<float4> g_reference : register(u6);
// The traced sky (kPhysicalFlag, g_full_light.w): octahedral over world directions, +y the pole, kSkyMapSize^2 texels of
// the sky's light in units of the stage's full light (linear), then its 9 spherical-harmonic coefficients (rgb).
// SkyProbeCS and SkyTraceShCS write it (g_reference's slot).
StructuredBuffer<float4> g_sky_map : register(t22);
// kPhysicalFlag: per EFB pixel its shading normal (pack_pixel_normal) and the texture's de-lighting there
// (pack_pixel_delight; 0: none), from EngineGBufferCS, for the composite's per-pixel sun and albedo.
RWStructuredBuffer<uint4> g_pixels : register(u7);
// Physical emitters (kPhysicalFlag, g_tev_info.z triangles): while EmitTriangleCS runs the emitting draws' ranges, then
// the emitting triangles as float4 elements: 0 the count and total power; at 1 + 4 i v0 and area, e1 and two-sided, e2,
// radiance (linear) and power; at 1 + 4 kEmitTriangles + i the running power (EmitScanCS).
ByteAddressBuffer g_emit_tris : register(t24);
static const uint kEmitTriangles = 65536u;   // ray_tracing.cpp kEmitTriangles
// Glow light sources (GlowEmitterCS): the table's glow draws (additive: the Observatory's star lights and walkway lamps,
// launch stars) drawn after the trace, their triangles in world space after the scene's emitters and lit from the next
// frame. ray_tracing.cpp kGlowEmitters.
static const uint kGlowEmitters = 16384u;
// g_emit_tris (float4 elements): the header; per scene emitter four (EmitTriangleCS); the running power, the scene's
// then the glows'; per glow emitter four in world space; the glow emitters' power.
static const uint kEmitRunning = 1u + 4u * kEmitTriangles;
static const uint kGlowElements = kEmitRunning + kEmitTriangles + kGlowEmitters;
static const uint kGlowPowers = kGlowElements + 4u * kGlowEmitters;
static const uint kSkyMapSize = 64u;
static const uint kSkyLayers = 8u;     // the sky's layers a probe ray composites, at most

static const float kPi = 3.14159265f;
static const uint kDrawNoShadow = 32768u;   // RtDraw.flags: the table's shadow 0 (lets light through)
// g_draws word 2 (SceneRayTracer::add_draw): the material table's emission, colour RGBA8 with these
// in the low byte; word 3: its intensity.
static const uint kEmitArea = 2u;       // low 2 bits: 1 point light, 2 area light
static const uint kEmitNamed = 4u;      // the table names this draw's texture
static const uint kEmitNoLight = 8u;    // never a light
// g_draws records (SceneRayTracer::draw_colors_): ambient, GX light 0 colour, emission, intensity,
// roughness and metalness, material colour, flags, relief; GX light 0's eye position (3 floats), its kind
// (0 none, 1 no facing term, 2 n.l; bits 4-5 the draw's GX cull mode, raster_facing; bits 16-31 a clear solid's index
// of refraction, rt_engine.hlsl solid_ior); words
// 12-15 its GX fog (RtDrawInfo::fog; rt_engine.hlsl gx_fog_share).
static const uint kDrawStride = 64u;

// Light under water: clear ocean water's diffuse attenuation per eye unit (about a centimetre: Mario stands 160 tall),
// red, green, blue: Jerlov type I, Kd = 0.35, 0.065, 0.019 per metre at 650, 550, 450 nm, halved because the game's
// water is drawn clearer than the sea. Kd, not the beam attenuation: the light scattered out of the beam still goes
// down (rt_engine.hlsl water_through).
static const float3 kWaterAttenuation = float3(0.00175f, 0.000325f, 0.000095f);
// Water as a medium a ray crosses (the camera in water): its beam attenuation c, the same clear water's 0.36, 0.08,
// 0.03 per metre halved.
static const float3 kWaterBeam = float3(0.0018f, 0.0004f, 0.00015f);
// The water's own light (its path radiance seen sideways) per unit of the light falling on it (a white surface under
// that light: 1): near the surface the upwelling radiance is one to two orders of magnitude below the downwelling and
// the horizontal lies between (Mobley, Ocean Optics Web Book, Visualizing Radiances): a fifth of the downwelling, the
// lighter side of that range (the owner, 27 October: Deep Dark's blue a little lighter; a seventh before), in coastal
// water's colour, its single-scattering albedo b / c (particles scattering about 0.15, 0.17, 0.19 per metre against
// an absorption of 0.35, 0.07, 0.08: pure water's, Pope and Fry 1997, with the dissolved organic matter that absorbs
// blue, Jerlov's coastal types): 0.30, 0.71, 0.70, here as a share of green's.
static const float3 kWaterLight = float3(0.30f, 0.71f, 0.70f) * (0.20f / 0.71f);

// The light water scatters towards a ray in it, `depth` eye units under its surface: its share of the stage's full
// light `full` falling on the water, dimmed by the water above. The stage's light, not what the traced scene sees
// there: the water carries light into its caves as it is lit from above.
float3 water_medium_light(float3 full, float depth)
{
    return kWaterLight * full * exp(-kWaterAttenuation * max(depth, 0.0f));
}

// Light `radiance` arriving after `distance` eye units of water whose own light is `medium`.
float3 water_medium(float3 radiance, float distance, float3 medium)
{
    const float3 t = exp(-kWaterBeam * max(distance, 0.0f));
    return radiance * t + medium * (1.0f - t);
}
static const uint kDrawTableFoliage = 4u;   // g_draws word 6: alpha-cut foliage cards
static const uint kDrawTableStar = 8u;      // g_draws word 6: a star's burning surface (the star core)
// g_draws word 6: a light source the table recolours (`recolour`): its light and its surface in the table's colour (the
// emission word's), not its TEV output (the Observatory's entrance stars are a pale environment-mapped metal).
static const uint kDrawTableRecolour = 128u;
// g_draws word 6: a light source only near a glow light (`near-glow`: within kNearGlowReach of the size of a glow light
// source's light, GlowEmitterCS's triangle about its centre): the Observatory's entrance stars are one material and
// one draw with their gold border; the game marks each star, not the border, with its glow sprite.
static const uint kDrawTableNearGlow = 256u;
// g_draws word 6: a trace-only draw (the player's body in the first-person view): the rays meet it, the picture never
// shows it, so no camera ray's surface is on it (primary_surface).
static const uint kDrawTableUnseen = 512u;
// g_draws word 6: a clear solid (the table's `traced solid`: the crystal cages), unseen, the engine's clear pass traces
// through it (rt_engine.hlsl clear_solid); bits 16-31 the eye units its row's colour is the transmittance after (0: it
// absorbs nothing).
static const uint kDrawTableSolid = 1024u;

bool solid_draw(uint flags)
{
    return (g_draws.Load(min(flags >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 24u) & kDrawTableSolid) != 0u;
}
static const float kNearGlowReach = 1.25f;
// Stars whose balls StarSphereCS fits (g_star_first): one float4 each (eye-space centre, radius; 0: none)
// after the samples in the results (g_final, m_light).
static const uint kStarSlots = 8u;
// The probe's records after them (SceneRayTracer::kProbeRecords), then per sample the light at the water in front of it
// (EngineFinalCS: the fall's light, the open air's at the water, r11g11b10 each; 0: none; LightTexturePS).
static const uint kProbeRecords = 15u;
// The picture sums (CaptureCS c_sums): per star slot the game's own colour of its samples, r g b n (x 1/255,
// cleared per capture), then the stars' colour over time, rgb (float) and known (SkyStateCS).
static const uint kStarSums = 64u;
static const uint kStarColour = kStarSums + kStarSlots * 16u;
// Physical light: the eye's adaptation (ray_tracing.cpp kPictureSumsBytes): its factor and known (kept), the traced
// light's log2 sum (x 256, signed) and count over the surfaces GlowBaseCS samples (cleared by SkyStateCS).
static const uint kAdaptation = kStarColour + 16u;
// A scene lit at this share of the stage's full light or more (the surfaces' geometric mean) is exposed as the stage's
// light sets it; a dimmer one is metered up to it by the full ratio, as a camera meters (the square root would keep
// half the darkness of shaded and sunless views), at most 4 times (two stops: a night stays darker than a day). A
// sunlit view's surfaces (open ground, walls, shade) average about a fifth of the full light, so day stages keep their
// exposure; the stage's own dimness is the static exposure's (physical_exposure).
static const float kAdaptationReference = 0.2f;
static const float kAdaptationMost = 4.0f;
// Highlight priority meters down as well, at most one stop: a view whose brightest tenth of surfaces is lit past what
// the stage's own light gives a surface facing it (c_exposure_night under physical light: that light as the picture
// shows it) is exposed for them. Floored at 1, the Observatory's lamp pools on its pale stone ran into the tone curve's
// shoulder once the direct light was unbiased (adaptation 1.000 there). Day views keep their exposure: their brightest
// surfaces are those facing the sun, which the limit is.
// Highlight priority: the metering brightens a view no further than shows the brightest tenth of its surfaces' light at
// the stage's full light (a camera exposed for its sunlit parts: the sunny-16 rule). Metered on the geometric mean
// alone, a sunlit view with much shade would push its sunlit ground into the tone curve's shoulder. The light is
// binned in log2 buckets of 0.75 stops from 1/256 to 16 of the full light (GlowBaseCS), read and cleared by SkyStateCS.
static const uint kAdaptationBuckets = 16u;
static const uint kAdaptationHistogram = kAdaptation + 16u;
static const float kAdaptationBright = 0.1f;
static const float kAdaptationLeast = 0.5f;
// Scene instance masks (ray_tracing.cpp kSceneMask): the frame's geometry. The late geometry (instance 1:
// drawn after the trace, Rosalina) has no records in g_positions or g_surfaces: shadow, sky and reflection
// rays meet it as an occluder.
static const uint kSceneMask = 1u;
// g_draws word 6: an alpha-tested card (grass, flowers), traced whole (card_cover decides its hits).
static const uint kDrawTableCard = 16u;
// RtDraw.flags bits 8-11 (rt_surfaces.h RtSurfaceClass::Crystal): clear crystal, glass and gems.
static const uint kClearClass = 7u;
// The share of the light a clear solid stops on its way through: its facets refract and totally reflect most rays
// away from the straight path, so the light behind a cut crystal is a partial, mottled shadow (about 0.4 of the light
// arrives), not the 0.91 a flat pane passes ((1 - F)^2) nor a solid's full shadow. A shadow ray (the sun's, the
// lights') passes the rest of the light on (sun_visibility_by, engine_visibility); a bounce ray stops at one in
// kClearShadow, a hash deciding (its history averages it).
static const float kClearShadow = 0.6f;
// The lighting multiplies the game's colour by at most this; the composite rolls it off on a film
// shoulder from kLitKnee (apply_light).
static const float kMaxLight = 8.0f;
// Under physical light the light is scene light (the HDR scene EFB, the camera's tone curve at the scene's end): a bound
// only, 12 stops over white, in the half floats' range.
static const float kMaxPhysicalLight = 4096.0f;
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
    // Below its knee a channel keeps its light; the shoulder's exponent takes only the part above it (with a white
    // texel's room of 1e-4, exp of the part below overflows to inf, and lerp(lit, -inf, 0) is NaN).
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

// A pixel's shading normal (12 bits an axis, octahedral) and the share of its colour that is not the game's painted
// reflection (8 bits, 255: all of it; EngineGBufferCS).
uint pack_pixel_normal(float3 n, float own)
{
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    float2 e = n.xy;
    if (n.z < 0.0f) {
        e = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
    }
    const uint2 u = uint2(round(saturate(e * 0.5f + 0.5f) * 4095.0f));
    return u.x | (u.y << 12u) | (uint(round(saturate(own) * 255.0f)) << 24u);
}

float3 unpack_pixel_smooth_normal(uint v)
{
    const float2 e = float2(float(v & 0xFFFu), float((v >> 12u) & 0xFFFu)) / 4095.0f * 2.0f - 1.0f;
    float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
    const float t = saturate(-n.z);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return normalize(n);
}

// A pixel's de-lighting (EngineGBufferCS): the normal its texture's relief bends (octahedral, 10 bits an axis) and the
// light painted into its texel (log units, 12 bits over +-kDelightRange, never 0: 0 is none).
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

// A colour's luminance after the finish's tone: a shoulder from 0.95, then a filmic S at strength `grade`.
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

// Picture capture (end of the scene): each sample's light and eye z for the next frame's rays.

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
    float c_exposure_day;    // the mean lighting the exposure holds under a bright sky; physical (c_exposure bit 3):
                             // the camera's exposure for the stage's light before the eye's adaptation
    float4 c_projection;
    float4 c_to_world[3];    // eye to world
    float c_exposure_night;  // ... under a black sky; physical (c_exposure bit 3, SkyStateCS): the stage's own full
                             // light on a surface facing it, as the picture shows it (the highlight priority's limit)
    float c_daylight;        // the sky's luminance that counts as bright
    uint c_exposure;         // SkyStateCS: 1 the exposure follows the lighting, 2 it starts over at 1
                             // (a new stage) (bits 0-1); GlowBaseCS: bit 0 measure it, bit 2 through the finish's
                             // tone; both: bit 3 physical (eye adaptation in linear light)
    float c_grade;           // the finish's grade strength
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
            // Night only: by day sun glare sprites over bright sand would count as glowing.
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
        // The eye's adaptation: the surfaces' mean light in log2 (their geometric mean, which a few lamps or a dark
        // corner do not sway) against kAdaptationReference; eased a tenth of a second's worth a frame (about a second
        // to settle), started over with a new stage.
        const uint count = c_sums.Load(kAdaptation + 12u);
        const float mean = count != 0u ? exp2(float(asint(c_sums.Load(kAdaptation + 8u))) / (256.0f * float(count))) : 0.0f;
        c_sums.Store2(kAdaptation + 8u, uint2(0u, 0u));
        // The light at the brightest tenth of the surfaces: within the bucket the tenth falls in, as far down it as the
        // tenth's share of its count (log2-linear). The bucket's top alone stepped the target by 0.75 stops whenever a
        // few samples crossed a bucket's edge, and the eased adaptation wandered after it (the lit ground's brightness
        // drifting by several levels in a still view).
        float bright = 0.0f;
        {
            uint total = 0u;
            [unroll] for (uint b = 0u; b < kAdaptationBuckets; ++b) {
                total += c_sums.Load(kAdaptationHistogram + b * 4u);
            }
            const float wanted = kAdaptationBright * float(total);
            uint above = 0u;
            [loop] for (uint k = kAdaptationBuckets; k > 0u; --k) {
                const uint count = c_sums.Load(kAdaptationHistogram + (k - 1u) * 4u);
                if (bright == 0.0f && total != 0u && count != 0u && float(above + count) >= wanted) {
                    const float into = saturate((wanted - float(above)) / float(count));
                    bright = exp2(-8.0f + 0.75f * (float(k) - into));
                }
                above += count;
            }
            [unroll] for (uint c = 0u; c < kAdaptationBuckets; ++c) {
                c_sums.Store(kAdaptationHistogram + c * 4u, 0u);
            }
        }
        if ((c_exposure & 3u) == 2u) {
            c_sums.Store2(kAdaptation, uint2(asuint(1.0f), 0u));
        } else if (count * 50u > grid) {
            // The light the picture shows it at is the camera's exposure (c_exposure_day) times the adaptation; at most
            // the stage's own light on a surface facing it (c_exposure_night).
            const float highlight =
                bright > 0.0f ? max(c_exposure_night, 1.0f) / max(bright * c_exposure_day, 1e-4f) : kAdaptationMost;
            const float target = clamp(min(max(kAdaptationReference / max(mean, 1e-4f), 1.0f), highlight),
                                       kAdaptationLeast, kAdaptationMost);
            const bool known = c_sums.Load(kAdaptation + 4u) != 0u;
            const float current = known ? asfloat(c_sums.Load(kAdaptation)) : target;
            c_sums.Store2(kAdaptation, uint2(asuint(current * pow(target / current, 0.02f)), 1u));
        }
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
    // Physical: the light the surfaces receive (the lighting's result over the camera's exposure and the eye's
    // adaptation, in the stage's full light), one sample in 8 x 8 on a surface, for the eye's adaptation (SkyStateCS).
    if ((c_exposure & 8u) != 0u && c_exposure_day > 0.0f && ((id.x | id.y) & 7u) == 4u) {
        const uint4 f = c_final[id.y * c_sample_size.x + id.x];
        const int2 q = int2(min(id.xy * c_factor, c_target_size - 1u));
        if (asfloat(f.z) != 0.0f && inside(c_viewport, float2(q) + 0.5f) && float(q.x) >= c_split &&
            -asfloat(f.z) <= c_sky_distance) {
            const float adaptation = c_sums.Load(kAdaptation + 4u) != 0u ? asfloat(c_sums.Load(kAdaptation)) : 1.0f;
            const float m = luminance(float3(f16tof32(f.x), f16tof32(f.x >> 16u), f16tof32(f.y)));
            const float light = clamp(m / (c_exposure_day * max(adaptation, 1e-3f)), 1.0f / 256.0f, 16.0f);
            // Centre-weighted, as a camera meters its subject: the picture's middle (the player) four times.
            const float2 at = (float2(q) + 0.5f - c_viewport.xy) / max(c_viewport.zw, 1.0f) - float2(0.5f, 0.55f);
            const int weight = all(abs(at) < float2(0.2f, 0.25f)) ? 4 : 1;
            c_sums.InterlockedAdd(kAdaptation + 8u, asuint(int(round(log2(light) * 256.0f)) * weight));
            c_sums.InterlockedAdd(kAdaptation + 12u, uint(weight));
            const uint bucket = min(uint(max((log2(light) + 8.0f) / 0.75f, 0.0f)), kAdaptationBuckets - 1u);
            c_sums.InterlockedAdd(kAdaptationHistogram + bucket * 4u, uint(weight));
        }
    }
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
        // Under a bright sky the glows bloom at 75 %, so the game's sun glare sprites do not veil daylight;
        // star bits, Lumas and lamps keep most of their halo.
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
            // A star's sample (its relief share carries the mark), one in four: the game's own colour.
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
            // A small light source's halo: its colour joins the bloom.
            const float halo = f16tof32(f.w >> 16u);
            if (halo > 0.0f && (c_divide & 2u) != 0u && float(q.x) >= c_split) {
                c_glow[index] = pack_r11g11b10(unpack_r11g11b10(c_glow[index]) + color * halo);
            }
            // The light the composite's film shoulder rolls off (lava, fire, lamps) blooms as the glows do, only
            // where the light passes the knee at all (most samples do not).
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

// Glow lights: the game's additive translucent draws whose texture the material table names as a
// light (lamp halos, fire, lava sparks, electricity) are light sources; other glows are effects whose
// halo shows in the bloom only. Each glow draw's eye-space triangles (the geometry pass) and what the
// translucent phases added to the picture where they lie give the light's centre (weighted by that
// light) and spread; its power and colour are the table's. The lights are remembered in world space
// while out of view or hidden, and fade where their place is in view but does not glow.

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
static const float kEmitForget = 0.004f;       // an unseen light that cannot reach the camera's surroundings
                                               // (its light there below this) fades fast

groupshared float4 s_glow_a[64];
groupshared float4 s_glow_b[64];
groupshared float s_glow_area[64];

// Emitter clusters. One draw can hold many separate light sources: a model's lamp heads, a particle system's
// fireflies, the sparkles round a Luma. As one light they would sit at their common centre, often in mid-air.
// Each emitter pass groups a draw's triangles by the world cell their centre lies in (kClusterCell units a side,
// up to kClusters cells; beyond that cells share a slot by hash), then joins cells whose extents come within
// kClusterGap of each other, so a light straddling a cell edge stays one light. Each joined set is one light, at
// the draw's full table intensity; a compact draw (a lamp, a Luma) stays one light.
// The first pass notes each triangle's slot (s_cluster_tri); each claimed slot then sums its own triangles in a
// fixed order, so the light is the same every frame (atomic adds in arrival order would make it jitter).
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
static const uint kGlowLens = 16u;      // a black hole's lens draw: no light
static const uint kGlowEffect = 32u;    // the table names it a particle: an effect, its halo in the bloom only
static const uint kGlowPhysical = 64u;  // a table light source under physical light: GlowEmitterCS makes it an area light

// A light's power by its size: a flame, a glow or a star of radius r lights its surroundings in proportion to
// its area (a 60-unit glow as the table gives it, a 500-unit fire at the limit). Characters and items (Lumas,
// coins) keep the table's power whatever their size, so a large Luma does not flood its surroundings.
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
        // a fifth of a Luma (an Octoomba's head light, sparks, a flame). Uncapped, the sparkles around a Luma
        // sum to tens of Lumas per light.
        power *= min(1.0f, 0.2f * e_authored_power / max(luminance(power), 1e-6f));
    } else {
        // Any other glow (a lamp's, a model's) lights what is near it as the picture shows it, at most half
        // a Luma.
        power *= min(1.0f, 0.5f * e_authored_power / max(luminance(power), 1e-6f));
    }
    // The light is as large as its sprite and spreads softly from the whole glow; a smaller light makes a sharp hot
    // spot that jitters as the sprite moves.
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
    if ((e_draws.Load(record + 24u) & (kGlowLens | kGlowPhysical)) != 0u) {
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
// beside it as the tube does; joined, a long cable would be one light at its middle.
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
            // out of view), so long cables do not overflow the kEmitFound lights a frame holds.
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
        // glow instead of separate beads, and its shadows soften as a tube that size casts them.
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
// Matching by distance alone would hand a lamp's light to a sparkle beside it in some frames while its own faded,
// and the lamp would flash.
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
            // unseen, so passing sparkles do not fill all kEmitLights.
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
                // (about 8 frames to full), so a light seen for a single frame (a cable's stretch at the picture's
                // edge) does not pop in.
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

// kPhysicalFlag: the open sky's light. The sky model is a backdrop drawn as it shows: its picture is the radiance it
// sends at the game's exposure, in its own colours and over its directions (image-based light). The stage's ambient
// (g_sky_radiance) says how much light a surface open to the sky gets: what the backdrop sends short of it comes from
// every direction in the ambient's colour; a backdrop brighter than it is taken down to it. A dark space backdrop so
// lights little and keeps its hue from tinting the stage; a glowing one (Melty Molten's lava horizon) lights in its
// own colour.
float physical_sky_mean()
{
    return luminance(g_sky_map[kSkyMapSize * kSkyMapSize].rgb * 0.282095f);
}

// The share of the backdrop's own radiance its light keeps.
float physical_sky_own()
{
    const float mean = physical_sky_mean();
    const float ambient = luminance(g_sky_radiance.rgb);
    return mean > ambient && mean > 1e-4f ? ambient / mean : 1.0f;
}

// The ambient beyond the backdrop's own light, from every direction (radiance).
float3 physical_sky_fill()
{
    const float ambient = luminance(g_sky_radiance.rgb);
    return ambient > 1e-4f ? g_sky_radiance.rgb * saturate(1.0f - physical_sky_mean() * physical_sky_own() / ambient)
                           : float3(0.0f, 0.0f, 0.0f);
}

// A sky without a sun (g_sky_radiance.w: rt-suns none, Melty Molten's haze) is an overcast one: the CIE standard
// overcast sky (Moon and Spencer 1942; CIE S 011/E:2003 type 1), its radiance (1 + 2 cos) / 3 of the zenith's by the
// angle from the player's up, three times brighter overhead than at the horizon; below the horizon the horizon's.
// Scaled so that open ground gets the light of a uniform sky (zenith 9/7, horizon 3/7 of it): whatever stands on the
// ground shades it most straight below, so a character's shadow stays visible under an overcast sky.
float physical_overcast(float3 d)
{
    return g_sky_radiance.w > 0.0f ? (3.0f + 6.0f * max(dot(d, g_up.xyz), 0.0f)) / 7.0f : 1.0f;
}

// The overcast sky's irradiance over pi on a surface facing `n`, as a share of a uniform sky's: 3/7 from the even
// part, 6/7 times the integral of the two clamped cosines over pi, 2 ((pi - a) cos a + sin a) / (3 pi) at the angle a
// between the surface and the player's up (2/3 facing up, 0 facing down).
float physical_overcast_irradiance(float3 n)
{
    if (g_sky_radiance.w <= 0.0f) {
        return 1.0f;
    }
    const float c = clamp(dot(n, g_up.xyz), -1.0f, 1.0f);
    const float a = acos(c);
    return 3.0f / 7.0f + (6.0f / 7.0f) * (2.0f * ((kPi - a) * c + sin(a)) / (3.0f * kPi));
}

// kPhysicalFlag: the sky's radiance towards eye-space direction `d`: the traced sky's (bilinear in its map, at the
// stage's ambient level and colour), else the ambient from every direction.
float3 physical_sky(float3 d)
{
    if (g_full_light.w == 0.0f) {
        return g_sky_radiance.rgb * physical_overcast(d);
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
    return radiance * physical_sky_own() + physical_sky_fill() * physical_overcast(d);
}

// kPhysicalFlag: the open sky's irradiance over pi on a surface facing eye-space `n` (the traced sky's spherical
// harmonics convolved with the cosine: bands 1, 2/3, 1/4, at the stage's ambient level), else the ambient.
float3 physical_sky_irradiance(float3 n)
{
    if (g_full_light.w == 0.0f) {
        return g_sky_radiance.rgb * physical_overcast_irradiance(n);
    }
    float basis[9];
    sh9(eye_to_world(n), basis);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint b = 0u; b < 9u; ++b) {
        irradiance += g_sky_map[kSkyMapSize * kSkyMapSize + b].rgb *
                      (basis[b] * (b == 0u ? 1.0f : b < 4u ? 2.0f / 3.0f : 0.25f));
    }
    return max(irradiance, 0.0f) * physical_sky_own() + physical_sky_fill() * physical_overcast_irradiance(n);
}

// kPhysicalFlag: the camera's exposure: a white surface facing the player's up (the ground under the player, by the
// game's gravity; g_up), lit by the open sky and the sun at its angle, shows at the stage's full light (the game's
// ambient + key: a dim stage stays as dim as the game sets it, a coloured one keeps its colour). Exposed for the sun as
// a photographer's sunny-16 rule is: at no less than 30 degrees up (cosine 0.5), so a sun below the player's horizon
// does not leave the reference lit by the sky alone and raise the exposure many times over.
float physical_adaptation()
{
    return g_picture_sums.Load(kAdaptation + 4u) != 0u ? asfloat(g_picture_sums.Load(kAdaptation)) : 1.0f;
}

float physical_exposure()
{
    // Set from the game's own stage light (SceneRayTracer): a light fitted dimmer to the baked light stays dimmer; times
    // the eye's adaptation to the light the surfaces receive (SkyStateCS).
    if (g_light_fit.x > 0.0f) {
        return g_light_fit.x * physical_adaptation();
    }
    const float sun = max(dot(g_up.xyz, g_sun.xyz), 0.5f);
    return luminance(g_full_light.rgb) /
           max(luminance(physical_sky_irradiance(g_up.xyz)) + luminance(g_sun_light.rgb) * sun, 1e-4f) *
           physical_adaptation();
}

// An RGBA8 colour word's RGB (red in the top byte).
float3 rgb8(uint c)
{
    return float3(float(c >> 24u), float((c >> 16u) & 0xFFu), float((c >> 8u) & 0xFFu)) / 255.0f;
}

// Foliage cards: the scene holds the alpha-tested ones whole, their cut-outs filled. A hit on a card
// counts where the raster drew it (the depth at the hit's pixel lies on the card's plane), not where it
// saw through it (a farther depth, the sky); hidden or out of view, at the cards' mean coverage (about half
// their texels).
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
// solid, or the card's own rule). The coordinates come from the hit's barycentrics on its eye-space triangle. The
// draw's record count comes from g_tev_info: g_draw_tex is a root descriptor, which has no size for GetDimensions.
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

// How much of a ray a candidate hit at eye-space `h` on `primitive` (its g_surfaces flags word `flags`) stops: 1 or
// 0 where it is decided, else kCardCoverage. An alpha-tested draw or card stops it where its texture's alpha passes its
// test (exact leaf, fence and lattice shadows); an untested card follows the raster's depth where it is in view, else
// the cards' mean coverage.
float card_cover(uint primitive, uint flags, float3 h)
{
    const int tested = alpha_test(primitive, flags, h);
    if (tested >= 0) {
        return tested != 0 ? 1.0f : 0.0f;
    }
    if (!traced_card(flags)) {
        return 1.0f;
    }
    const float2 q = eye_to_pixel(g_projection, g_viewport, h);
    if (h.z < -g_near && inside(g_viewport, q) && all(q < float2(g_target_size))) {
        const float depth = g_efb_depth.Load(int3(int2(q), 0));
        if (far_plane(g_depth, depth)) {
            return 0.0f;
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
                return 0.0f;
            }
            if (z <= plane + tolerance) {
                return 1.0f;
            }
        }
    }
    return kCardCoverage;
}

// card_cover as one ray's yes or no, for the rays whose histories average their noise (bounces): an undecided card
// stops one in kCardCoverage, a number from the primitive and `salt` (the sample, the frame, the ray) deciding.
bool card_blocks(uint primitive, uint flags, float3 h, uint salt)
{
    const float cover = card_cover(primitive, flags, h);
    return cover >= 1.0f || (cover > 0.0f && card_hash(primitive, salt) < cover);
}

// The surface a sample shows (EngineGBufferCS): the triangle the camera ray meets at the raster depth gives the
// face normal, the shading normal from its vertex normals, and a ray origin lifted off the flat face where those
// normals bend away from it (the shadow terminator, Hanika 2021). Geometry the scene leaves out (blended and indirect
// draws) takes the normal from the depth.
struct Surface {
    float3 n;          // shading normal
    float3 ng;         // face normal, towards the camera
    float3 lifted;     // the point lifted off the face
    // How far a ray from the drawn point starts off it, along the face normal: the shading normals' lift (Hanika) less
    // what the drawn point already stands above the face; negative where the drawn point lies under its traced face
    // (a curved model's inward bend), by that depth and the lift, a start that holds for every ray.
    float lift;
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

// Whether the raster could have drawn triangle `tri` of draw `draw` at a pixel: the faces its GX cull mode keeps (the
// draw record's word 11, bits 4-5: 0 none, 1 front faces culled, 2 back faces culled, 3 all). A face is a front face
// where its winding's normal, cross(e1, e2), points towards the eye, the side a one-sided light source shines to.
bool raster_facing(uint tri, uint draw)
{
    const uint cull = (g_draws.Load(min(draw, max(g_draw_count, 1u) - 1u) * kDrawStride + 44u) >> 4u) & 3u;
    if (cull == 0u) {
        return true;
    }
    const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
    const float3 v1 = asfloat(g_positions.Load3(tri * 36u + 12u));
    const float3 v2 = asfloat(g_positions.Load3(tri * 36u + 24u));
    const bool front = dot(cross(v1 - v0, v2 - v0), -v0) > 0.0f;
    return cull == 2u ? front : cull == 1u && !front;
}

Surface primary_surface(int2 q, float3 p)
{
    Surface s;
    s.n = surface_normal(q, p);
    s.ng = s.n;
    s.lifted = p;
    s.lift = 0.0f;
    s.primary = uint2(0u, 0u);
    s.clear = false;
    const float span = length(p);
    // Only the triangles about the raster's depth: the raster drew this surface there, so anything nearer was not
    // drawn at this pixel (a face the draw's GX cull mode leaves out, the near side of a shell drawn from inside).
    // Taken from the eye, such a face was committed first and cut the ray short of the drawn surface: the Gateway's
    // turning ring (its crystal panels drawn as back faces) had no surface under most of its pixels.
    const float window = span * 0.03f + 8.0f;
    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, 0.0f);
    ray.Direction = p / span;
    ray.TMin = max(span - window, 0.0f);
    ray.TMax = span * 1.02f + 2.0f;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, kSceneMask, ray);
    // The candidate nearest the raster's depth, whatever its kind: the raster drew a surface there (its alpha test
    // passed), so when no triangle meets the depth closely that one is it. Alpha-tested geometry a few units off the
    // raster's depth (Bowser's Star Reactor's fire-bar balls) would otherwise have no draw at its pixels.
    float nearest_gap = window;
    uint nearest_tri = 0u;
    float2 nearest_b = float2(0.0f, 0.0f);
    while (query.Proceed()) {
        // A foliage card is the surface only where the raster drew it (at this pixel's depth).
        const uint candidate = g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
        if ((g_draws.Load(min(candidate >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 24u) & kDrawTableUnseen) !=
            0u) {
            continue;
        }
        // A face its draw's cull mode leaves out was not drawn here (a thin model's far side within the window).
        if (!raster_facing(query.CandidatePrimitiveIndex(), candidate >> 16u)) {
            continue;
        }
        const float gap = abs(query.CandidateTriangleRayT() - span);
        if (gap < nearest_gap) {
            nearest_gap = gap;
            nearest_tri = query.CandidatePrimitiveIndex() + 1u;
            nearest_b = query.CandidateTriangleBarycentrics();
        }
        // Committed only at the raster's depth: a committed hit shortens the ray, and an opaque face a little nearer
        // (inside the window) would hide the drawn one behind it.
        if (gap <= span * 0.002f + 0.5f) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    const bool committed =
        query.CommittedStatus() == COMMITTED_TRIANGLE_HIT && abs(query.CommittedRayT() - span) < span * 0.01f + 2.0f;
    if (committed || nearest_tri != 0u) {
        const uint tri = committed ? query.CommittedPrimitiveIndex() : nearest_tri - 1u;
        const float2 b = committed ? query.CommittedTriangleBarycentrics() : nearest_b;
        const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
        const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
        const float3 v1 = asfloat(g_positions.Load3(tri * 36u + 12u));
        const float3 v2 = asfloat(g_positions.Load3(tri * 36u + 24u));
        const float3 face = cross(v1 - v0, v2 - v0);
        const float face_length = length(face);
        if (face_length > 1e-12f) {
            s.ng = face / face_length;
            s.ng = dot(s.ng, p) > 0.0f ? -s.ng : s.ng;
            const float3 hit = v0 * w.x + v1 * w.y + v2 * w.z;
            s.lifted = hit;
            s.primary = uint2(tri + 1u, f32tof16(b.x) | (f32tof16(b.y) << 16u));
            s.clear = ((g_surfaces.Load(tri * 16u + 12u) >> 8u) & 15u) == kClearClass;
            smooth_surface(tri, w, v0, v1, v2, s.ng, s.n, s.lifted);
            // The drawn point against the traced face: Phong tessellation (curved_surfaces.h) draws a curved model
            // rounded and the scene holds its flat triangles, so where the model bends inwards (a beach, a sea floor,
            // a dome's rims) the drawn surface lies under its own faces, up to tens of units, and a ray from it met
            // its own face from below (black blotches of self-shadow); where it bends outwards it stands above them,
            // as far as the normals' lift reaches or further.
            const float lift = max(dot(s.lifted - hit, s.ng), 0.0f);
            const float above = dot(p - hit, s.ng);
            s.lift = above < 0.0f ? above - lift : max(lift - above, 0.0f);
        }
    }
    return s;
}

// Candidates a ray's traversal weighs before it takes the one it is at as its hit. This bounds the work per ray:
// layered alpha cards (Dusty Dune's hourglass sand) can otherwise run a dispatch past the GPU's watchdog. 64 alpha
// cards along one ray are as good as a wall.
static const uint kMaxCandidates = 64u;

// The share of the sun's light reaching `origin` along `direction` (a point of its disc): 0 behind a blocker; through
// clear solids and undecided cards what each lets through (1 - kClearShadow, 1 - card_cover), the same for every ray
// and frame, so the shadows they cast are as still as a solid's (a ray stopped at random by them, one in so many,
// left their shadows a frame's grain). The nearest blocker's distance in `blocker` (0: none), the triangle + 1 of the
// one that stopped the ray in this instance's (0: open, or only passed through) and its instance in `who`. `card`: the
// sample lies on a foliage card, which the cards close beside it do not shade.
float sun_visibility_by(float3 origin, float3 direction, bool card, out float blocker, out uint2 who)
{
    blocker = 0.0f;
    who = uint2(0u, 0u);
    float through = 1.0f;
    float partial = 6.0e4f;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = g_sun_reach;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
    uint visits = 0u;
    while (query.Proceed()) {
        if (++visits > kMaxCandidates) {
            query.CommitNonOpaqueTriangleHit();
            query.Abort();
            break;
        }
        if (query.CandidateInstanceID() != 0u) {
            query.CommitNonOpaqueTriangleHit();
            continue;
        }
        const uint hit = g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
        const float t = query.CandidateTriangleRayT();
        float cover = kClearShadow;
        if (((hit >> 8u) & 15u) != kClearClass) {
            if ((hit & kDrawNoShadow) != 0u) {
                continue;
            }
            if (card && t < 60.0f &&
                (g_draws.Load(min(hit >> 16u, max(g_draw_count, 1u) - 1u) * kDrawStride + 24u) & kDrawTableFoliage) !=
                    0u) {
                continue;
            }
            cover = card_cover(query.CandidatePrimitiveIndex(), hit, origin + direction * t);
        }
        if (cover >= 1.0f) {
            query.CommitNonOpaqueTriangleHit();
        } else if (cover > 0.0f) {
            through *= 1.0f - cover;
            partial = min(partial, t);
        }
    }
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        blocker = clamp(query.CommittedRayT(), 1e-2f, 6.0e4f);
        who = uint2(query.CommittedPrimitiveIndex() + 1u, query.CommittedInstanceID());
        return 0.0f;
    }
    if (through < 1.0f) {
        blocker = clamp(partial, 1e-2f, 6.0e4f);
    }
    return through;
}

float sun_visibility(float3 origin, float3 direction, bool card, out float blocker)
{
    uint2 who;
    return sun_visibility_by(origin, direction, card, blocker, who);
}

// Physical emitters (kPhysicalFlag): every triangle of the scene's emitting draws (the material table's light sources:
// lava, lamps, Lumas) is an area light. What it shows is what it sends: its radiance is its draw's TEV output in linear
// light (units of a white surface under white light), its power pi L A (two-sided: twice). EmitTriangleCS writes them,
// EmitScanCS the running power, by which the engine (rt_engine.hlsl) picks them.

float4 emit_element(uint e)
{
    return asfloat(g_emit_tris.Load4(e * 16u));
}

float emit_running(uint i)
{
    return asfloat(g_emit_tris.Load((kEmitRunning + i) * 16u));
}

// An emitter's facing towards a point, from the cosine at its face: sides (its element b.w) 0 one-sided, 1 two-sided,
// 2 isotropic (a glowing volume's sprite, GlowEmitterCS: 1/2, the mean of |cos| over the sphere, the same power).
float emitter_facing(float cos_light, float sides)
{
    return sides >= 1.5f ? 0.5f : sides > 0.0f ? abs(cos_light) : cos_light;
}

// Emitter i's four elements in this frame's eye space (v0 and area, e1 and two-sided, e2 and beam, radiance and power): a
// scene triangle's as EmitTriangleCS wrote them, a glow's (from g_emit_scene on) moved from the world.
void emit_triangle_of(uint i, out float4 a, out float4 b, out float4 c, out float4 d)
{
    const bool glow = i >= g_emit_scene;
    const uint e = glow ? kGlowElements + 4u * (i - g_emit_scene) : 1u + 4u * i;
    a = emit_element(e);
    b = emit_element(e + 1u);
    c = emit_element(e + 2u);
    d = emit_element(e + 3u);
    if (glow) {
        a.xyz = transform(g_to_eye, a.xyz);
        b.xyz = rotate3(g_to_eye, b.xyz);
        c.xyz = rotate3(g_to_eye, c.xyz);
    }
}

// Whether eye-space point `p` lies near a glow light source (`near-glow`): within kNearGlowReach of the farthest corner
// of one's light triangle from its centre.
bool near_glow_light(float3 p)
{
    [loop] for (uint i = g_emit_scene; i < g_tev_info.z; ++i) {
        float4 a, b, c, d;
        emit_triangle_of(i, a, b, c, d);
        if (!(d.w > 0.0f)) {
            continue;
        }
        const float3 centre = a.xyz + (b.xyz + c.xyz) / 3.0f;
        const float3 r0 = a.xyz - centre;
        const float3 r1 = a.xyz + b.xyz - centre;
        const float3 r2 = a.xyz + c.xyz - centre;
        const float reach2 = kNearGlowReach * kNearGlowReach * max(dot(r0, r0), max(dot(r1, r1), dot(r2, r2)));
        const float3 to = p - centre;
        if (dot(to, to) <= reach2) {
            return true;
        }
    }
    return false;
}

uint emit_hash(uint v)
{
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float emit_random(inout uint seed)
{
    seed = emit_hash(seed);
    return float(seed >> 8u) * (1.0f / 16777216.0f);
}

// A light point's target for resampling at `p` (normal `n`): the luminance of its light there unshadowed (eye space).
float emitter_target(float3 p, float3 n, float3 y, float3 face, float3 radiance, float beam, float sides, out float3 light,
                     float near2 = 1e-4f)
{
    light = float3(0.0f, 0.0f, 0.0f);
    const float3 to = y - p;
    const float d2 = max(dot(to, to), near2);
    const float3 dir = to * rsqrt(d2);
    const float cos_light = emitter_facing(-dot(face, dir), sides);
    const float cos_surface = dot(n, dir);
    if (cos_light <= 0.0f || cos_surface <= 0.0f) {
        return 0.0f;
    }
    light = radiance * ((beam >= 1.0f ? pow(cos_light, beam) : cos_light) * cos_surface / (kPi * d2));
    return luminance(light);
}

float3 eye_point_to_world(float3 p)
{
    const float3 d = p - float3(g_to_eye[0].w, g_to_eye[1].w, g_to_eye[2].w);
    return d.x * g_to_eye[0].xyz + d.y * g_to_eye[1].xyz + d.z * g_to_eye[2].xyz;
}

float3 eye_direction_to_world(float3 d)
{
    return d.x * g_to_eye[0].xyz + d.y * g_to_eye[1].xyz + d.z * g_to_eye[2].xyz;
}

void reference_material(uint tri, float2 b, out float3 albedo, out float3 emission);

// Reference (GALAXY_RT_REFERENCE=1, g_flags bit 13): a path tracer for a still camera, the image the real-time passes
// are compared with; not a player setting. Per sample and frame one path. At every vertex: the sun (a shadow ray
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

#if RT_TEXTURED
// Physical glow light sources (emit root; kGlowPhysical draws): a table light the game draws as an additive glow (the
// Observatory's star lights and walkway lamps, launch stars, fire) is an area light of its own triangles, as the scene's
// emitters are: each triangle (e_triangles: this frame's eye space) into the emitters' glow segment (e_glow_out:
// g_emit_tris) in world space.
// Its radiance: for a `tint` light source (kGlowEmitterDrawn) what it draws, the draw's TEV output over the triangle
// (glow_drawn), linear, as much of it as its blend adds to the picture (its source factor: an alpha-blended flame emits
// its colour over the share of the pixel its alpha covers), times the table's radiance (at least 1: no less than it
// shows); so a fire's light has the fire's colour as the game animates it (its particles' colours, their fading) and a
// sprite the game draws faint sends little. Otherwise the table's colour (linear) times the table's radiance, over the
// share of the quad its texture lights (an additive sprite adds texel x alpha: a fire sprite's soft blob about a
// quarter), so overlapping fire sprites do not flood what they light.
// What emits is what the game drew. A triangle with vertex normals (e_glow_normals: the glow geometry's surfaces) is
// a surface that glows (the Observatory's star lamps' glowing bodies, their glow texture projected from their normals;
// lamp glass): it emits from its outward face (the side its normals point to; one-sided, power pi L A) at its radiance
// on average over it. A triangle without normals is a sprite standing for a glowing volume (a flame): its light leaves
// evenly in every direction (sides 2, power 2 pi L A) from the triangle shrunk about its centre to the share of it that
// glows at its brightest, sampled as the sphere of that power about its centre (radius sqrt(A / 2 pi); rt_engine.hlsl
// engine_light_point), so it reaches past the lamp's own cup below its orb. Lit as one-sided panels facing the camera,
// the sprites' light would turn with the view and stop at their plane.
// The next frame's lighting samples them after the scene's emitters (they draw after this frame's trace).
// e_draws: per glow emitter draw (32 bytes) its first glow triangle, count, first glow emitter, colour (RGBA8), radiance,
// its texture's view in the descriptor heap (~0: none), flags (kGlowEmitter*), its TEV record's byte offset in e_draws
// (RtTevMaterial, then its stages; ~0: none); after them the records.
RWByteAddressBuffer e_glow_out : register(u3);
// The glow triangles' surfaces (16 bytes each: three eye-space vertex normals, octahedral, 0 without; the draw's
// flags), in e_added's slot (GlowEmitterCS reads no added light).
StructuredBuffer<uint> e_glow_normals : register(t4);
// The glow triangles' texture coordinates and colours (kTriUvStride each, as g_tri_uv: ray_tracing.hlsl GeometryCS),
// written for the frames with a kGlowEmitterDrawn draw.
ByteAddressBuffer e_glow_uv : register(t5);

static const uint kGlowEmitterIsotropic = 1u;   // ray_tracing.cpp kGlowEmitter*: a sprite standing for a glowing volume
static const uint kGlowEmitterDrawn = 2u;       // its light the colour it draws (the table's `tint`)
static const uint kGlowEmitterVolume = 8u;      // a glowing volume drawn as geometry (the table's `volume`)
static const uint kGlowEmitterBlendShift = 4u;  // bits 4-6: its source blend factor (GX BlendFactor)
static const uint kGlowPoints = 16u;            // points a triangle's drawn colour is taken at (glow_drawn)

groupshared float s_coverage[64];
groupshared float4 s_drawn[64];   // per point: the light it adds (linear rgb), its luminance

// What a glow draw adds to the picture at a point of a triangle: its TEV output `c` (display-encoded) in linear light,
// times the share of it its source blend factor passes (GX BlendFactor: zero, one, source colour, its inverse, source
// alpha, its inverse; the destination's alpha, which no glow reads, as one).
float3 glow_added_light(float4 c, uint factor)
{
    const float3 linear_rgb = srgb_to_linear(c.rgb);
    switch (factor) {
    case 0u: return float3(0.0f, 0.0f, 0.0f);
    case 2u: return linear_rgb * c.rgb;
    case 3u: return linear_rgb * (1.0f - c.rgb);
    case 4u: return linear_rgb * c.a;
    case 5u: return linear_rgb * (1.0f - c.a);
    default: return linear_rgb;
    }
}

// Point `k` of kGlowPoints of glow triangle `tri` (a 4 x 4 stratified grid mapped onto the triangle, area-uniform) as
// tev_combine takes it: its coordinates in both slots, their change along the triangle's edges, its vertex colours.
TevPoint glow_point(uint tri, uint k)
{
    const float2 u = (float2(k & 3u, k >> 2u) + 0.5f) / 4.0f;
    const float r = sqrt(u.x);
    const float3 w = float3(1.0f - r, r * (1.0f - u.y), r * u.y);
    const uint a = tri * kTriUvStride;
    const float4 uv0_01 = asfloat(e_glow_uv.Load4(a));
    const float2 uv0_2 = asfloat(e_glow_uv.Load2(a + 16u));
    const float4 uv1_01 = asfloat(e_glow_uv.Load4(a + 24u));
    const float2 uv1_2 = asfloat(e_glow_uv.Load2(a + 40u));
    const uint3 col0 = e_glow_uv.Load3(a + 48u);
    const uint3 col1 = e_glow_uv.Load3(a + 60u);
    TevPoint pt;
    pt.uv[0] = uv0_01.xy * w.x + uv0_01.zw * w.y + uv0_2 * w.z;
    pt.uv[1] = uv1_01.xy * w.x + uv1_01.zw * w.y + uv1_2 * w.z;
    pt.edge1[0] = uv0_01.zw - uv0_01.xy;
    pt.edge1[1] = uv1_01.zw - uv1_01.xy;
    pt.edge2[0] = uv0_2 - uv0_01.xy;
    pt.edge2[1] = uv1_2 - uv1_01.xy;
    pt.vertex0 = tev_rgba8f(col0.x) * w.x + tev_rgba8f(col0.y) * w.y + tev_rgba8f(col0.z) * w.z;
    pt.vertex1 = tev_rgba8f(col1.x) * w.x + tev_rgba8f(col1.y) * w.y + tev_rgba8f(col1.z) * w.z;
    return pt;
}

// One glow emitter: triangle `t` of the draw (its first glow triangle `first`, its first emitter slot `first_slot`) at
// `radiance` (rgb, linear: on average over the triangle) into the glow segment. `coverage`: for a sprite (`isotropic`
// without vertex normals, or a `volume`'s triangle whatever its normals: a beam's ribbon faces one way, its light
// every way), the share of the triangle that glows at the brightest it does (its radiance / coverage there): the
// triangle is shrunk about its centre to it.
void glow_emitter_store(uint first, uint first_slot, uint t, bool isotropic, bool volume, float3 radiance,
                        float coverage)
{
    const uint slot = first_slot + t;
    if (slot >= kGlowEmitters) {
        return;
    }
    float3 v0, v1, v2;
    const uint tri = first + t;
    const bool valid = emit_triangle(tri, v0, v1, v2);
    const uint3 normals = uint3(e_glow_normals[tri * 4u], e_glow_normals[tri * 4u + 1u], e_glow_normals[tri * 4u + 2u]);
    const bool surface = isotropic && !volume && all(normals != 0u);
    float sides = isotropic ? 2.0f : 1.0f;
    if (surface) {
        // Outward: the face turned to the side its vertex normals point to.
        const float3 outward = unpack_vertex_normal(normals.x) + unpack_vertex_normal(normals.y) +
                               unpack_vertex_normal(normals.z);
        if (dot(cross(v1 - v0, v2 - v0), outward) < 0.0f) {
            const float3 swap = v1;
            v1 = v2;
            v2 = swap;
        }
        sides = 0.0f;
    } else if (isotropic) {
        const float c = max(coverage, 1e-4f);
        radiance /= c;
        const float3 centre = (v0 + v1 + v2) / 3.0f;
        const float shrink = sqrt(c);
        v0 = centre + (v0 - centre) * shrink;
        v1 = centre + (v1 - centre) * shrink;
        v2 = centre + (v2 - centre) * shrink;
    }
    const float3 w0 = transform(e_to_world, v0);
    const float3 e1 = rotate3(e_to_world, v1 - v0);
    const float3 e2 = rotate3(e_to_world, v2 - v0);
    const float area = valid ? 0.5f * length(cross(e1, e2)) : 0.0f;
    const float power = area > 1e-8f ? (sides == 0.0f ? kPi : 2.0f * kPi) * luminance(radiance) * area : 0.0f;
    const uint at = (kGlowElements + 4u * slot) * 16u;
    e_glow_out.Store4(at, asuint(float4(w0, area)));
    e_glow_out.Store4(at + 16u, asuint(float4(e1, sides)));
    e_glow_out.Store4(at + 32u, asuint(float4(e2, 0.0f)));
    e_glow_out.Store4(at + 48u, asuint(float4(radiance, power)));
    e_glow_out.Store4((kGlowPowers + slot) * 16u, asuint(float4(power, 0.0f, 0.0f, 0.0f)));
}

[numthreads(64, 1, 1)]
void GlowEmitterCS(uint3 group : SV_GroupID, uint gi : SV_GroupIndex)
{
    const uint record = group.x * 32u;
    const uint4 draw = e_draws.Load4(record);   // first glow triangle, count, first emitter slot, colour
    const float intensity = asfloat(e_draws.Load(record + 16u));
    const uint flags = e_draws.Load(record + 24u);
    const uint tev_record = e_draws.Load(record + 28u);
    const bool isotropic = (flags & kGlowEmitterIsotropic) != 0u;
    const bool volume = (flags & kGlowEmitterVolume) != 0u;
    if ((flags & kGlowEmitterDrawn) != 0u && tev_record != 0xFFFFFFFFu) {
        // Its drawn colour: kGlowPoints points of each triangle, four triangles at a time, each point's texture
        // filtered over its share of the triangle (the footprint cone: half log2 of the share).
        const uint factor = (flags >> kGlowEmitterBlendShift) & 7u;
        const float cone = -0.5f * log2(2.0f * float(kGlowPoints));
        const uint local = gi / kGlowPoints;
        const uint k = gi % kGlowPoints;
        [loop] for (uint base = 0u; base < draw.y; base += 64u / kGlowPoints) {
            const uint t = base + local;
            float3 added = float3(0.0f, 0.0f, 0.0f);
            if (t < draw.y) {
                bool pass;
                const float4 c = tev_combine(e_draws, tev_record, glow_point(draw.x + t, k), 0u, cone, false, pass);
                added = pass ? glow_added_light(saturate(c), factor) : float3(0.0f, 0.0f, 0.0f);
            }
            s_drawn[gi] = float4(added, luminance(added));
            GroupMemoryBarrierWithGroupSync();
            if (k == 0u && t < draw.y) {
                float3 sum = float3(0.0f, 0.0f, 0.0f);
                float peak = 0.0f;
                [unroll] for (uint j = 0u; j < kGlowPoints; ++j) {
                    const float4 p = s_drawn[gi + j];
                    sum += p.rgb;
                    peak = max(peak, p.a);
                }
                const float3 mean = sum / float(kGlowPoints);
                const float coverage = peak > 0.0f ? luminance(mean) / peak : 1.0f;
                glow_emitter_store(draw.x, draw.z, t, isotropic, volume, mean * intensity, coverage);
            }
            GroupMemoryBarrierWithGroupSync();
        }
        return;
    }
    // The table's colour over the texture's mean of luminance x alpha over an 8 x 8 grid of its texels (particle
    // textures often have no mips).
    const uint view = e_draws.Load(record + 20u);
    float own = 1.0f;
    if (view != 0xFFFFFFFFu) {
        const Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(view)];
        uint w, h, levels;
        map.GetDimensions(0u, w, h, levels);
        const uint2 texel = uint2((float2(gi & 7u, gi >> 3u) + 0.5f) / 8.0f * float2(w, h));
        const float4 t = saturate(map.Load(int3(min(texel, uint2(max(w, 1u), max(h, 1u)) - 1u), 0)));
        own = luminance(t.rgb) * t.a;
    }
    s_coverage[gi] = own;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (gi < stride) {
            s_coverage[gi] += s_coverage[gi + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    const float coverage = view != 0xFFFFFFFFu ? s_coverage[0] / 64.0f : 1.0f;
    const float3 table_radiance = srgb_to_linear(rgb8(draw.w)) * intensity;
    for (uint t = gi; t < draw.y; t += 64u) {
        // A surface's or panel's radiance on average over it (its glow texture lights part of it: what is seen, GX
        // output_flags bit 5); a sprite's whole radiance over its shrunk triangle (the same power).
        glow_emitter_store(draw.x, draw.z, t, isotropic, volume, table_radiance * coverage, coverage);
    }
}
#endif

// A light's colour decoded to linear light at its own peak (its brightness kept: light colours past 1 are intensities).
float3 linear_hue(float3 c)
{
    const float peak = max(c.r, max(c.g, c.b));
    return peak > 1e-6f ? srgb_to_linear(c / peak) * peak : float3(0.0f, 0.0f, 0.0f);
}

// What a ray hits on triangle `tri` (barycentrics `b`): its draw's TEV output at the hit (rt_tev.hlsli; its textures
// filtered over the ray cone's footprint `cone`, tev_cone, or their level-0 texels at kTevNoCone), and the table's area
// emission, in linear light.
void reference_material_cone(uint tri, float2 b, float cone, out float3 albedo, out float3 emission)
{
    albedo = float3(0.5f, 0.5f, 0.5f);
    emission = float3(0.0f, 0.0f, 0.0f);
    const uint draw = g_surfaces.Load(tri * 16u + 12u) >> 16u;
#if RT_TEXTURED
    // The draw's own TEV program at the hit (its colour channels at full light under kPhysicalFlag).
    if (draw < g_tev_info.x) {
        bool pass;
        albedo = tev_evaluate_cone(draw, g_tev_info.x, tri, b, 0u, cone, pass).rgb;
    }
#endif
    albedo = srgb_to_linear(saturate(albedo));
    const uint2 table = g_draws.Load2(min(draw, max(g_draw_count, 1u) - 1u) * kDrawStride + 8u);
    bool glows = true;
#if RT_TEXTURED
    glows = tev_glows_at(draw, g_tev_info.x, tri, b);
#endif
    if ((g_flags & kPhysicalFlag) != 0u) {
        // A light source of the table shows what it sends: its TEV output is its radiance, not a colour under light
        // (an atlas only in the table's glowing region of its texture).
        if ((table.x & kEmitNamed) != 0u && (table.x & 3u) != 0u && (table.x & kEmitNoLight) == 0u && glows) {
            const uint table_flags = g_draws.Load(min(draw, max(g_draw_count, 1u) - 1u) * kDrawStride + 24u);
            emission = (table_flags & kDrawTableRecolour) != 0u ? srgb_to_linear(rgb8(table.x)) : albedo;
            albedo = float3(0.0f, 0.0f, 0.0f);
        }
    } else if ((table.x & kEmitNamed) != 0u && (table.x & 3u) == kEmitArea && g_emit.w > 0.0f) {
        emission = srgb_to_linear(rgb8(table.x)) * (g_self.z * asfloat(table.y) * g_emit.w);
    }
}

void reference_material(uint tri, float2 b, out float3 albedo, out float3 emission)
{
    reference_material_cone(tri, b, -1.0e30f, albedo, emission);   // kTevNoCone
}

#if RT_TEXTURED
// Physical emitters: the triangles (one thread each) of the emitting draws' ranges (g_emit_tris here: the range count,
// then per range at 16 + 16 r its first scene triangle, triangle count, draw | two-sided << 31 and first emitter) into
// g_reference (float4 elements: 0 header; at 1 + 4 i v0 and area, e1 and two-sided, e2, radiance and power; at
// 1 + 4 kEmitTriangles + i the power, EmitScanCS's running sum). The radiance is the mean of the draw's TEV output at
// seven points of the triangle in linear light.
[numthreads(64, 1, 1)]
void EmitTriangleCS(uint3 id : SV_DispatchThreadID)
{
    const uint total = g_emit_scene;
    if (id.x >= total) {
        return;
    }
    const uint ranges = g_emit_tris.Load(0u);
    uint lo = 0u;
    uint hi = max(ranges, 1u) - 1u;
    [loop] while (lo < hi) {
        const uint mid = (lo + hi + 1u) >> 1u;
        if (g_emit_tris.Load(16u + mid * 16u + 12u) <= id.x) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    const uint4 range = g_emit_tris.Load4(16u + lo * 16u);
    const uint tri = range.x + (id.x - range.w);
    const uint draw = range.z & 0xFFFFu;
    const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
    const float3 e1 = asfloat(g_positions.Load3(tri * 36u + 12u)) - v0;
    const float3 e2 = asfloat(g_positions.Load3(tri * 36u + 24u)) - v0;
    const float area = 0.5f * length(cross(e1, e2));
    static const float2 kPoints[7] = {float2(1.0f / 3.0f, 1.0f / 3.0f), float2(2.0f / 3.0f, 1.0f / 6.0f),
                                      float2(1.0f / 6.0f, 2.0f / 3.0f), float2(1.0f / 6.0f, 1.0f / 6.0f),
                                      float2(0.5f, 0.25f), float2(0.25f, 0.5f), float2(0.25f, 0.25f)};
    float3 radiance = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint k = 0u; k < 7u; ++k) {
        bool pass;
        radiance += srgb_to_linear(saturate(tev_evaluate(draw, g_tev_info.x, tri, kPoints[k], pass).rgb));
    }
    radiance /= 7.0f;
    // Brighter than it shows by the table's intensity where that passes 1 (a glowing bulb's luminance over its painted
    // colour); the table's lesser art intensities (Luma units) leave it as it shows. Outside the table's glowing region
    // of its texture (tev_glows_at, at the triangle's centre) none.
    // A faint glow (intensity below 1, the engine's kGbGlow) sends its glow: its colour times the intensity.
    {
        const uint record = min(draw, max(g_draw_count, 1u) - 1u) * kDrawStride;
        const float intensity = asfloat(g_draws.Load(record + 12u));
        const uint table_flags = g_draws.Load(record + 24u);
        const bool star = (table_flags & kDrawTableStar) != 0u;
        if ((table_flags & kDrawTableRecolour) != 0u) {
            radiance = srgb_to_linear(rgb8(g_draws.Load(record + 8u)));
        }
        if ((table_flags & kDrawTableNearGlow) != 0u && !near_glow_light(v0 + (e1 + e2) / 3.0f)) {
            radiance = float3(0.0f, 0.0f, 0.0f);
        }
        radiance *= (intensity < 1.0f && !star ? intensity : max(intensity, 1.0f)) *
                    (tev_glows_at(draw, g_tev_info.x, tri, float2(1.0f / 3.0f, 1.0f / 3.0f)) ? 1.0f : 0.0f);
    }
    const bool two_sided = (range.z >> 31u) != 0u;
    // A beam (the table's `cone`: TEV record pad[0] bits 16-31, n): radiance L cos^n from the face's normal, its power
    // 2 pi L A / (n + 1) (n = 1: a Lambertian face's pi L A).
    const float beam = draw < g_tev_info.x ? float(g_tev.Load(draw * kTevRecordBytes + 108u) >> 16u) : 0.0f;
    const float lobe = beam >= 1.0f ? 2.0f / (beam + 1.0f) : 1.0f;
    const float power = area > 1e-8f ? kPi * luminance(radiance) * area * lobe * (two_sided ? 2.0f : 1.0f) : 0.0f;
    g_reference[1u + 4u * id.x] = float4(v0, area);
    g_reference[2u + 4u * id.x] = float4(e1, two_sided ? 1.0f : 0.0f);
    g_reference[3u + 4u * id.x] = float4(e2, beam);
    g_reference[4u + 4u * id.x] = float4(radiance, power);
    g_reference[1u + 4u * kEmitTriangles + id.x] = float4(power, 0.0f, 0.0f, 0.0f);
}

groupshared float s_emit_running[1024];

// The running power of the emitters (one group): each thread sums its stretch, the stretches' sums are scanned, each
// thread writes its stretch's running sums; the header takes the count and the total.
[numthreads(1024, 1, 1)]
void EmitScanCS(uint gi : SV_GroupIndex)
{
    const uint total = g_tev_info.z;
    const uint per = (total + 1023u) / 1024u;
    const uint begin = min(gi * per, total);
    const uint end = min(begin + per, total);
    float sum = 0.0f;
    for (uint i = begin; i < end; ++i) {
        sum += i < g_emit_scene ? g_reference[kEmitRunning + i].x : g_reference[kGlowPowers + (i - g_emit_scene)].x;
    }
    s_emit_running[gi] = sum;
    GroupMemoryBarrierWithGroupSync();
    [loop] for (uint offset = 1u; offset < 1024u; offset <<= 1u) {
        const float before = gi >= offset ? s_emit_running[gi - offset] : 0.0f;
        GroupMemoryBarrierWithGroupSync();
        s_emit_running[gi] += before;
        GroupMemoryBarrierWithGroupSync();
    }
    float running = s_emit_running[gi] - sum;
    for (uint j = begin; j < end; ++j) {
        running += j < g_emit_scene ? g_reference[kEmitRunning + j].x : g_reference[kGlowPowers + (j - g_emit_scene)].x;
        g_reference[kEmitRunning + j] = float4(running, 0.0f, 0.0f, 0.0f);
    }
    if (gi == 1023u) {
        g_reference[0] = float4(asfloat(total), s_emit_running[1023], 0.0f, 0.0f);
    }
}
#endif

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
// painted shadow (a dark plank border) is a shallow groove, not a cliff.
static const float kReliefSpan = 0.35f;
// The steepest relief slope (tan 20 degrees): the painted light says little of shape, and a sun 20 degrees up or more
// never leaves a surface black for it. Steeper, bent normals turn whole bands of a surface away from the sun.
static const float kReliefSlope = 0.36f;

float relief_height(float s)
{
    return kReliefSpan * tanh(s / kReliefSpan);
}

// A ray cone's footprint on triangle `tri` for tev_evaluate_cone (Akenine-Moller et al. 2021, as delight_surface's
// level): log2 of its width `width` (eye units, across the ray along `direction`) widened by the ray's slant to the
// triangle, over the triangle's square-root area.
float tev_cone(uint tri, float3 direction, float width)
{
    const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
    const float3 e1 = asfloat(g_positions.Load3(tri * 36u + 12u)) - v0;
    const float3 e2 = asfloat(g_positions.Load3(tri * 36u + 24u)) - v0;
    const float3 face = cross(e1, e2);
    const float area = max(length(face), 1e-30f);
    const float slant = max(abs(dot(face / area, normalize(direction))), 0.05f);
    return log2(width / slant) - 0.5f * log2(area);
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

// The lighting as a texture for the translucent draws after the trace (one texel per sample), in three thirds: the
// light of the surface behind each sample (water, glass); where water lies in front of that surface, the light in the
// open air at the water's surface, else the first third's (the effects: spray, splashes, smoke); the light at the falling
// water in front of the sample, else the second third's (falling water). rt_engine.hlsl water_light.

cbuffer RtLightTexture : register(b0)
{
    uint2 t_size;
    uint t_front;    // the records of the light at the water in front of the samples (kProbeRecords)
    uint t_pad;
    float4 t_full;   // the stage's full light in linear light (physical; else 1)
};
StructuredBuffer<uint4> t_final : register(t0);

float4 LightTexturePS(float4 position : SV_Position) : SV_Target0
{
    const uint third = min(uint(position.x) / t_size.x, 2u);
    const uint2 s = min(uint2(position.xy) - uint2(third * t_size.x, 0u), t_size - 1u);
    const uint4 v = t_final[s.y * t_size.x + s.x];
    const uint2 front = third != 0u ? t_final[t_front + s.y * t_size.x + s.x].xy : uint2(0u, 0u);
    // Falling water in front of the sample (rt_engine.hlsl water_light): the light at its own sheet, whole.
    if (third == 2u && front.x != 0u) {
        return float4(unpack_r11g11b10(front.x) / max(t_full.rgb, 1e-3f), 1.0f);
    }
    // Water in front of it: the light in the open air at the water's surface.
    if (front.y != 0u) {
        return float4(unpack_r11g11b10(front.y) / max(t_full.rgb, 1e-3f), 1.0f);
    }
    if (asfloat(v.z) == 0.0f) {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    // A blended translucent (seaweed, glass, leaf cards) takes the light of the surface behind it, and
    // that surface lies in the translucent's own shadow: it keeps at most half of the darkening (more
    // light it takes whole).
    // Physical: the translucents are drawn in the game's light, the traced light is in its units (white 1).
    const float3 m = float3(f16tof32(v.x), f16tof32(v.x >> 16u), f16tof32(v.y)) / max(t_full.rgb, 1e-3f);
    return float4(max(m, 0.5f + 0.5f * m), 1.0f);
}

// Composite: the EFB colour (a copy of it) under the lighting. Less light darkens it in
// proportion; more light brightens it in proportion up to a knee, then rolls off towards white
// without passing it (apply_light).

cbuffer RtComposite : register(b0)
{
    float4 m_projection;
    float4 m_depth;
    float4 m_viewport;
    uint2 m_sample_size;
    uint m_factor;
    uint m_flags;   // bit 0: move the point to the traced frame's camera (m_reproject[m_slot]);
                    // bit 1: show the lighting alone; bit 2: light in linear light (the reference);
                    // bit 4: traced reflections (the painted ones taken out, bit 3)
    uint m_slot;
    float m_near;
    float m_split;  // GALAXY_RT_SPLIT: left of it the game's own picture (a dark line between)
    float m_efb_scale;  // EFB pixels per game pixel (the internal resolution)
    float4 m_sun;        // towards the traced sun (eye space, traced frame); relief height (eye units)
    float4 m_sun_light;  // its light (rgb)
    uint2 m_target_size; // the traced frame's EFB pixels (m_pixels)
    float4 m_water;      // the camera in water (x: 1), its depth (y, eye units): water_medium
    float4 m_water_sky;  // the stage's full light (rgb): what the water scatters
};
StructuredBuffer<uint4> m_light : register(t0);
Texture2D<float> m_efb_depth : register(t1);
StructuredBuffer<float4> m_reproject : register(t2);
Texture2D<float4> m_color : register(t3);
// m_flags bit 3 (physical): the traced frame's per-pixel shading normals and de-lighting, and its surface's fog: the
// share (f16) and the camera's exposure (f16, high half), the colour RGB8 (EngineGBufferCS).
StructuredBuffer<uint4> m_pixels : register(t4);
// ClearCompositePS, m_flags bit 6: the effects layer (RendererD3D12::begin_layer_window), what the game's effects drew
// over the frame after its translucent lists: their colour as they blended (L) and what of the EFB behind them they let
// through (T); the frame is F = L + T B.
Texture2D<float4> m_layer_color : register(t5);
Texture2D<float4> m_layer_trans : register(t6);

// m_reproject after the slots' rows (4 x 3): a sub-frame's own view (SceneRayTracer::write_reprojection): its
// projection as m_projection's (its jitter in) and its depth's z_scale, z_offset, known.
static const uint kSubframeView = 12u;

// The view the pixel's own depth was drawn with (projection as m_projection, depth terms as m_depth): a sub-frame's
// when its point moves to the traced frame (m_flags bit 0), else the traced frame's. A sub-frame draws with its
// projection at t and its own jitter; rebuilt with the traced frame's, its points would sit off their surfaces by the
// difference (the game's camera shake, a zoom, the jitter), far enough on a grazing surface to miss the traced
// frame's depth there and lose its light.
void own_view(out float4 projection, out float4 depth)
{
    projection = m_projection;
    depth = m_depth;
    if ((m_flags & 1u) != 0u) {
        const float4 terms = m_reproject[kSubframeView + 1u];
        if (terms.z != 0.0f) {
            projection = m_reproject[kSubframeView];
            depth = float4(terms.xy, m_depth.zw);
        }
    }
}

// The frame at pixel `q` without the effects drawn over it (B = (F - L) / T, m_flags bit 6); where they let almost
// nothing through, the frame as it is.
float3 frame_behind_effects(int2 q)
{
    const float3 f = m_color.Load(int3(q, 0)).rgb;
    if ((m_flags & 64u) == 0u) {
        return f;
    }
    const float3 l = m_layer_color.Load(int3(q, 0)).rgb;
    const float3 t = m_layer_trans.Load(int3(q, 0)).rgb;
    return all(t > 0.02f) ? max((f - l) / t, 0.0f) : f;
}

// The traced frame's shading normal at its pixel `q`.
float3 traced_normal(int2 q)
{
    const int2 c = clamp(q, int2(0, 0), int2(m_target_size) - 1);
    return unpack_pixel_smooth_normal(m_pixels[uint(c.y) * m_target_size.x + uint(c.x)].x);
}

// The share of its colour that is not the game's painted reflection (EngineGBufferCS).
float traced_own(int2 q)
{
    const int2 c = clamp(q, int2(0, 0), int2(m_target_size) - 1);
    return float(m_pixels[uint(c.y) * m_target_size.x + uint(c.x)].x >> 24u) / 255.0f;
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
// faces are flat and so are its vertex normals, so a turn taken from either would be constant over each face; the
// actor's position is not the ball's centre either. Where the ray passes the fitted sphere (a model not quite round)
// the surface point the depth gives; 0.6 without a ball.
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
// against its neighbourhood, per channel: a brightness ratio would clip away the detail of channels already at full
// brightness). The ball brightens towards its centre and dims at its limb (mu: the ball's turn to the eye, star_mu).
// Past full brightness it keeps its hue and the excess whitens it a little.
static const float kStarDetail = 1.0f;

float3 star_surface(int2 c, float z, float mu, float3 color, float4 depth_terms)
{
    // Its neighbourhood 3 game pixels around at every internal resolution (at a fixed 3 EFB pixels a high internal
    // resolution loses the texture's contrast), on its own surface only (what stands in front, a ring or a
    // character, would draw a bright or dark line along it).
    const int r = max(int(round(3.0f * m_efb_scale)), 1);
    const int2 ring[8] = {int2(-1, -1), int2(0, -1), int2(1, -1), int2(-1, 0), int2(1, 0), int2(-1, 1), int2(0, 1), int2(1, 1)};
    const float limit = abs(z) * 0.02f + 2.0f;
    float3 around = float3(0.0f, 0.0f, 0.0f);
    float count = 0.0f;
    [unroll] for (uint k = 0u; k < 8u; ++k) {
        const int2 q = c + ring[k] * r;
        if (abs(depth_to_eye_z(depth_terms, m_efb_depth.Load(int3(q, 0))) - z) < limit) {
            around += m_color.Load(int3(q, 0)).rgb;
            count += 1.0f;
        }
    }
    const float3 detail = count > 0.0f ? color - around / count : float3(0.0f, 0.0f, 0.0f);
    // Limb (mu 0.2) 1.01 of the game's colour, centre 1.38; over the disc about 1.2. The detail joins after the
    // brightness is brought back under full; scaled with it, it would flatten a nearly full colour.
    const float3 lit = color * (1.15f * (0.8f + 0.4f * mu));
    const float peak = max(lit.r, max(lit.g, lit.b));
    return saturate(lit / max(peak, 1.0f) + max(peak - 1.0f, 0.0f) * 0.2f + detail * kStarDetail);
}

// The engine's clear surfaces (rt_engine.hlsl EngineClearCS; m_light: the slot's clear buffer, two records a pixel:
// reflection rgb and transmittance red as halves, eye z, present; transmittance green and blue, the picture's pixel
// seen through it) over the picture at the scene's end (m_color, its copy): what shows through times the
// transmittance plus the reflections, in linear light. A clear solid refracts: what shows through it is the picture
// at another pixel (its offset from the traced pixel kept on sub-frames). Sub-frames move the point to the traced
// frame by the scene's depth behind the glass.
CompositeOut ClearCompositePS(float4 position : SV_Position)
{
    CompositeOut result;
    const int2 c = int2(position.xy);
    if (!inside(m_viewport, position.xy)) {
        discard;
    }
    int2 traced = c;
    if ((m_flags & 1u) != 0u) {
        float4 own_projection;
        float4 own_depth;
        own_view(own_projection, own_depth);
        const float depth = m_efb_depth.Load(int3(c, 0));
        const float z = far_plane(own_depth, depth) ? -1.0e5f : depth_to_eye_z(own_depth, depth);
        const uint row = m_slot * 3u;
        const float4 rows[3] = {m_reproject[row], m_reproject[row + 1u], m_reproject[row + 2u]};
        const float3 p = transform(rows, eye_position(own_projection, m_viewport, position.xy, z));
        if (p.z >= -m_near) {
            discard;
        }
        traced = int2(floor(eye_to_pixel(m_projection, m_viewport, p)));
    }
    if (any(traced < 0) || any(traced >= int2(m_sample_size))) {
        discard;
    }
    // A discarded pixel's shader runs on, so its read must stay in the buffer (m_light is a root view, unchecked): a
    // sub-frame's reprojected pixel off the traced frame (or NaN behind the camera) would read far outside it and hang
    // the GPU.
    traced = clamp(traced, int2(0, 0), int2(m_sample_size) - 1);
    // GALAXY_RT_ENGINE_VIEW=6 (m_flags bit 3): the composite's coverage, magenta.
    if ((m_flags & 8u) != 0u) {
        result.color = float4(0.8f, 0.0f, 0.8f, 1.0f);
        return result;
    }
    const uint record = (uint(traced.y) * m_sample_size.x + uint(traced.x)) * 2u;
    const uint4 e = m_light[record];
    if (e.w == 0u) {
        // GALAXY_RT_ENGINE_VIEW=5 (m_flags bit 2): where the pass wrote nothing, red.
        if ((m_flags & 4u) != 0u) {
            result.color = float4(0.6f, 0.0f, 0.0f, 1.0f);
            return result;
        }
        discard;
    }
    const uint4 e2 = m_light[record + 1u];
    const float3 r = float3(f16tof32(e.x), f16tof32(e.x >> 16u), f16tof32(e.y));
    const float3 t = float3(f16tof32(e.y >> 16u), f16tof32(e2.x), f16tof32(e2.x >> 16u));
    int2 seen = c;
    if (e2.y != 0xFFFFFFFFu) {
        seen = clamp(c + int2(e2.y & 0xFFFFu, e2.y >> 16u) - traced, int2(m_viewport.xy),
                     int2(m_viewport.xy + m_viewport.zw) - 1);
    }
    const float3 shown = srgb_to_linear(frame_behind_effects(seen)) * t + r;
    result.color = float4(linear_to_srgb(shown), 1.0f);
    // The effects drawn in front of the glass or the fall after it (a fall's spray) stay over it as they blended.
    if ((m_flags & 64u) != 0u) {
        result.color.rgb = m_layer_color.Load(int3(c, 0)).rgb + m_layer_trans.Load(int3(c, 0)).rgb * result.color.rgb;
    }
    return result;
}

// The waterfall mist (rt_engine.hlsl MistLightCS): the spray at the falls' feet as puffs of water drops, each a Gaussian
// cloud. m_light holds the frame's records: a header of kMistHeader (the puffs' count and the frame; the traced frame's
// world to eye rows; the world's up; the up in eye space) and three a puff (its eye-space centre and width sigma; its
// optical depth through its centre, the mist's from it towards the sun, the height of the water's surface under it
// (-1e30 none) and its brightness (its row's colour); the sun's light it sees and the light reaching it from above,
// exposed, halves).
static const uint kMistHeader = 6u;
static const uint kMistMost = 512u;
// A dense spray's albedo (water drops absorb almost nothing; its face takes in the light it sends back out): white.
static const float kMistAlbedo = 0.9f;
// The share of the mist's density its turbulence moves (world-anchored noise; the mean kept).
static const float kMistNoise = 0.45f;
// How far the turbulence rises a frame with the spray (eye units at 60 frames a second: half a metre a second).
static const float kMistRise = 0.8f;

// The standard normal distribution's integral up to `x` (Abramowitz and Stegun 7.1.26 for erf: error under 1.5e-7).
float mist_phi(float x)
{
    const float z = abs(x) * 0.70710678f;
    const float t = 1.0f / (1.0f + 0.3275911f * z);
    const float e = 1.0f - ((((1.061405429f * t - 1.453152027f) * t + 1.421413741f) * t - 0.284496736f) * t +
                            0.254829592f) * t * exp(-z * z);
    return 0.5f + 0.5f * (x >= 0.0f ? e : -e);
}


// Value noise in [0, 1] at `p` (a smooth lattice of hashed values), and two octaves of it.
float mist_lattice(int3 c)
{
    return float(emit_hash(uint(c.x) * 0x8DA6B343u ^ uint(c.y) * 0xD8163841u ^ uint(c.z) * 0xCB1AB31Fu) >> 8u) *
           (1.0f / 16777216.0f);
}

float mist_value_noise(float3 p)
{
    const float3 i = floor(p);
    const float3 f = p - i;
    const float3 u = f * f * (3.0f - 2.0f * f);
    const int3 b = int3(i);
    const float x00 = lerp(mist_lattice(b), mist_lattice(b + int3(1, 0, 0)), u.x);
    const float x10 = lerp(mist_lattice(b + int3(0, 1, 0)), mist_lattice(b + int3(1, 1, 0)), u.x);
    const float x01 = lerp(mist_lattice(b + int3(0, 0, 1)), mist_lattice(b + int3(1, 0, 1)), u.x);
    const float x11 = lerp(mist_lattice(b + int3(0, 1, 1)), mist_lattice(b + int3(1, 1, 1)), u.x);
    return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z);
}

float mist_noise(float3 p)
{
    return (2.0f * mist_value_noise(p) + mist_value_noise(p * 2.03f + 17.1f)) / 3.0f;
}

// The mist over the picture: along the camera ray to the scene's surface (the EFB's depth, at the traced frame's
// camera on sub-frames), every puff's optical depth (a Gaussian cloud's: its column at the ray's distance from its
// centre, cut to the span before the surface and above the water under it) and the light on it (the sun's it sees and
// the light from the sky above it): a dense spray is a white cloud, its face as white as a surface facing that light
// (kMistAlbedo). Turbulence (noise anchored in the world,
// rising with the spray) thickens and thins it. What lies behind passes exp(-depth), the mist adds its light times 1
// minus that (the puffs' light averaged by their depth). Under the effects drawn after the translucent lists (the
// effects layer: the frame is their L + T B), as the clear composite.
CompositeOut MistCompositePS(float4 position : SV_Position)
{
    CompositeOut result;
    const int2 q = int2(position.xy);
    const uint count = min(m_light[0].x, kMistMost);
    if (!inside(m_viewport, position.xy) || count == 0u || m_water.x > 0.5f) {
        discard;
    }
    const float depth = m_efb_depth.Load(int3(q, 0));
    float4 own_projection;
    float4 own_depth;
    own_view(own_projection, own_depth);
    const bool open = far_plane(own_depth, depth);
    float3 p = eye_position(own_projection, m_viewport, position.xy, open ? -1.0e5f : depth_to_eye_z(own_depth, depth));
    float3 o = float3(0.0f, 0.0f, 0.0f);
    if ((m_flags & 1u) != 0u) {
        const uint row = m_slot * 3u;
        const float4 rows[3] = {m_reproject[row], m_reproject[row + 1u], m_reproject[row + 2u]};
        o = transform(rows, o);
        p = transform(rows, p);
    }
    const float3 d = normalize(p - o);
    const float reach = open ? 1.0e7f : length(p - o);
    const float3 up = asfloat(m_light[5]).xyz;
    const float height = dot(o, up);
    const float rise = dot(d, up);
    float tau = 0.0f;
    float3 light = float3(0.0f, 0.0f, 0.0f);
    float3 at = float3(0.0f, 0.0f, 0.0f);
    float width = 0.0f;
    // GALAXY_RT_MIST_VIEW=1 (m_flags bit 1): no cut at the water; 2 (bit 2): the puffs by the water found under them,
    // red over their centre, green under it, blue none.
    float3 found = float3(0.0f, 0.0f, 0.0f);
    [loop] for (uint i = 0u; i < count; ++i) {
        const uint r = kMistHeader + 3u * i;
        const float4 puff = asfloat(m_light[r]);
        const float3 v = puff.xyz - o;
        const float t = dot(v, d);
        const float b2 = max(dot(v, v) - t * t, 0.0f);
        const float s2 = puff.w * puff.w;
        if (b2 > 9.0f * s2 || t < -3.0f * puff.w || t - 3.0f * puff.w > reach) {
            continue;
        }
        const float3 depths = asfloat(m_light[r + 1u].xyz);
        // The span of the ray in front of the surface and above the water under the puff.
        float first = 0.0f;
        float last = reach;
        if ((m_flags & 4u) != 0u) {
            const float whole = depths.x * exp(-0.5f * b2 / s2);
            found += whole * (depths.z <= -1.0e29f ? float3(0.0f, 0.0f, 1.0f)
                              : depths.z > dot(puff.xyz, up) ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f));
        }
        if (depths.z > -1.0e29f && (m_flags & 2u) == 0u) {
            if (abs(rise) > 1e-6f) {
                const float crossing = (depths.z - height) / rise;
                if (rise < 0.0f) {
                    last = min(last, crossing);
                } else {
                    first = max(first, crossing);
                }
            } else if (height < depths.z) {
                continue;
            }
            if (last <= first) {
                continue;
            }
        }
        const float column =
            depths.x * exp(-0.5f * b2 / s2) * (mist_phi((last - t) / puff.w) - mist_phi((first - t) / puff.w));
        if (column <= 1e-5f) {
            continue;
        }
        const uint4 l = m_light[r + 2u];
        const float3 sun = float3(f16tof32(l.x), f16tof32(l.x >> 16u), f16tof32(l.y));
        tau += column;
        light += column * asfloat(m_light[r + 1u].w) * (sun + float3(f16tof32(l.y >> 16u), f16tof32(l.z), f16tof32(l.z >> 16u)));
        at += column * (o + d * clamp(t, 0.0f, reach));
        width += column * puff.w;
    }
    if ((m_flags & 4u) != 0u) {
        if (!any(found > 1e-3f)) {
            discard;
        }
        result.color = float4(linear_to_srgb(saturate(found)), 1.0f);
        return result;
    }
    if (tau <= 1e-4f) {
        discard;
    }
    // The turbulence at the mist's mean point along the ray, in the world (the header's world to eye rows), its cells
    // 0.7 of the puffs' mean width.
    const float4 rows[3] = {asfloat(m_light[1]), asfloat(m_light[2]), asfloat(m_light[3])};
    const float3 e = at / tau - float3(rows[0].w, rows[1].w, rows[2].w);
    float3 w = e.x * rows[0].xyz + e.y * rows[1].xyz + e.z * rows[2].xyz;
    w -= asfloat(m_light[4]).xyz * (float(m_light[0].y) * kMistRise);
    const float n = mist_noise(w / max(0.7f * width / tau, 1.0f));
    const float thick = tau * (1.0f + kMistNoise * (2.0f * n - 1.0f));
    const float pass = exp(-thick);
    const float3 behind = srgb_to_linear(frame_behind_effects(q));
    result.color = float4(linear_to_srgb(behind * pass + kMistAlbedo * (light / tau) * (1.0f - pass)), 1.0f);
    if ((m_flags & 64u) != 0u) {
        result.color.rgb = m_layer_color.Load(int3(q, 0)).rgb + m_layer_trans.Load(int3(q, 0)).rgb * result.color.rgb;
    }
    return result;
}

float4 CompositeVS(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1u) & 2u, id & 2u);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

CompositeOut CompositePS(float4 position : SV_Position)
{
    // This pixel's own view (a sub-frame's: own_view); the traced frame's projection (m_projection) takes its point
    // into the traced frame's samples.
    float4 own_projection;
    float4 own_depth;
    own_view(own_projection, own_depth);
    const float depth = m_efb_depth.Load(int3(int2(position.xy), 0));
    const float z = depth_to_eye_z(own_depth, depth);
    const float slope = min(abs(ddx(z)) + abs(ddy(z)), abs(z) * 0.05f);
    CompositeOut result;
    const float3 color = m_color.Load(int3(int2(position.xy), 0)).rgb;
    result.color = float4(color, 1.0f);
    if (far_plane(own_depth, depth) || !inside(m_viewport, position.xy)) {
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
    float3 p = eye_position(own_projection, m_viewport, position.xy, z);
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
        const float zr = depth_to_eye_z(own_depth, m_efb_depth.Load(int3(c + int2(1, 0), 0)));
        const float zl = depth_to_eye_z(own_depth, m_efb_depth.Load(int3(c - int2(1, 0), 0)));
        const float zd = depth_to_eye_z(own_depth, m_efb_depth.Load(int3(c + int2(0, 1), 0)));
        const float zu = depth_to_eye_z(own_depth, m_efb_depth.Load(int3(c - int2(0, 1), 0)));
        gradient.x = abs(zr - z) < abs(z - zl) ? zr - z : z - zl;
        gradient.y = abs(zd - z) < abs(z - zu) ? zd - z : z - zu;
        gradient = clamp(gradient, -abs(z) * 0.05f, abs(z) * 0.05f);
    }
    // The light samples around the pixel (3 x 3 about the nearest, a tent 1.5 samples wide), each weighed by
    // its depth against this surface's plane extended to it: samples on another surface (a step's face, an
    // object in front) drop out and those on this one blend smoothly, so a shadow or crease edge follows the
    // geometry's full-resolution edge instead of the light's sample grid.
    const int2 centre = int2(floor(a + 0.5f));
    const float tolerance = abs(p.z) * 0.004f + 0.25f + (abs(gradient.x) + abs(gradient.y)) * float(m_factor) * 0.5f;
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float relief_sum = 0.0f;
    float weight = 0.0f;
    float3 nearest = float3(1.0f, 1.0f, 1.0f);
    float nearest_relief = 0.0f;
    float nearest_dz = abs(p.z) * 0.1f;
    // The engine's result (bit 5; full resolution, rt_engine.hlsl EngineFinalCS: diffuse light and specular, exposed):
    // at this pixel's place in the traced frame, the nearest pixel in depth of the 3 x 3 there.
    if ((m_flags & 32u) != 0u) {
        const int2 traced = int2(floor(eye_to_pixel(m_projection, m_viewport, p)));
        uint4 best = uint4(0u, 0u, 0u, 0u);
        float best_dz = abs(p.z) * 0.05f + 1.0f;
        [unroll] for (uint k = 0u; k < 9u; ++k) {
            const int2 o = clamp(traced + int2(int(k % 3u) - 1, int(k / 3u) - 1), int2(0, 0), int2(m_sample_size) - 1);
            const uint4 e = m_light[uint(o.y) * m_sample_size.x + uint(o.x)];
            const float dz = abs(asfloat(e.w) - p.z);
            if (e.w != 0u && (dz < best_dz || (k == 4u && dz <= best_dz))) {
                best_dz = dz;
                best = e;
            }
        }
        // No traced surface there (the sky's backdrops beyond the lighting's reach): the picture as the game drew it.
        if (best.w == 0u) {
            discard;
        }
        const float3 diffuse = float3(f16tof32(best.x), f16tof32(best.x >> 16u), f16tof32(best.y));
        const float3 specular = float3(f16tof32(best.y >> 16u), f16tof32(best.z), f16tof32(best.z >> 16u));
        if ((m_flags & 2u) != 0u) {
            result.color = float4((diffuse + specular) * 0.5f, 1.0f);
            return result;
        }
        const int2 at = clamp(traced, int2(0, 0), int2(m_target_size) - 1);
        const uint delit = traced_delight(at);
        // The game's fog over the surface (EngineGBufferCS): the raster blended it into the colour in display space
        // (8-bit), so it comes out of the colour before the light goes on, and lies over the lit surface as light the
        // air or water between scatters towards the eye: the fog's colour at the exposure, in the share the game gives
        // it. Multiplied by the traced light as part of the colour, it went dark with the light (Deep Dark's water).
        const uint4 fog_word = m_pixels[uint(at.y) * m_target_size.x + uint(at.x)];
        const float fog = f16tof32(fog_word.z);
        float3 surface_colour = color;
        float3 inscatter = float3(0.0f, 0.0f, 0.0f);
        if (fog > 0.0f) {
            const float3 fog_colour =
                float3(float((fog_word.w >> 16u) & 0xFFu), float((fog_word.w >> 8u) & 0xFFu), float(fog_word.w & 0xFFu)) /
                255.0f;
            surface_colour = fog < 0.996f ? max((color - fog_colour * fog) / (1.0f - fog), 0.0f) : fog_colour;
            inscatter = srgb_to_linear(fog_colour) * (f16tof32(fog_word.z >> 16u) * fog);
        }
        const float3 albedo = srgb_to_linear(surface_colour) *
                              (delit != 0u ? exp(-unpack_pixel_shading(delit)) : 1.0f) * traced_own(at);
        float3 seen = (albedo * diffuse + specular) * (1.0f - fog) + inscatter;
        // The camera in water: the water between it and the surface (its light at the exposure).
        if (m_water.x > 0.0f) {
            seen = water_medium(seen, length(here),
                                water_medium_light(m_water_sky.rgb, m_water.y) * f16tof32(fog_word.z >> 16u));
        }
        result.color = float4(linear_to_srgb(seen), 1.0f);
        return result;
    }
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
    float3 m = clamp(weight > 1e-3f ? sum / weight : nearest, 0.0f, per_pixel ? kMaxPhysicalLight : kMaxLight);
    if (per_pixel) {
        // The per-pixel sun (no relief: the normals are the surface's own).
        m = clamp(m + m_sun_light.rgb * (weight > 1e-3f ? sun_turn / weight : nearest_turn), 0.0f, kMaxPhysicalLight);
        if ((m_flags & 2u) != 0u) {
            result.color = float4(m * 0.5f, 1.0f);
            return result;
        }
        // The texel's albedo: the light painted into it taken out (rt_delight.hlsl), the traced light in its place.
        // Its painted reflection taken out too where the traced one replaces it (environment maps). The scene light
        // into the HDR scene EFB unclamped: the camera's tone curve at the scene's end brings it to the display.
        const float3 albedo = srgb_to_linear(color) * (delit != 0u ? exp(-unpack_pixel_shading(delit)) : 1.0f) *
                              ((m_flags & 16u) != 0u ? traced_own(traced_pixel) : 1.0f);
        result.color = float4(linear_to_srgb(albedo * m), 1.0f);
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
        result.color = float4(star_surface(int2(position.xy), z, mu, color, own_depth), 1.0f);
        return result;
    }
    if (relief > 1e-3f && m_sun.w > 0.0f) {
        const int2 c = int2(position.xy);
        float zs[4];
        float hs[4];
        const int2 steps[4] = {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)};
        [unroll] for (uint k = 0u; k < 4u; ++k) {
            const int2 q = c + steps[k];
            zs[k] = depth_to_eye_z(own_depth, m_efb_depth.Load(int3(q, 0)));
            hs[k] = luminance(m_color.Load(int3(q, 0)).rgb);
        }
        const float limit = abs(z) * 0.01f + 0.5f;
        const bool okx = abs(zs[0] - z) < limit && abs(zs[1] - z) < limit;
        const bool oky = abs(zs[2] - z) < limit && abs(zs[3] - z) < limit;
        if (okx && oky) {
            const float3 px = eye_position(own_projection, m_viewport, position.xy + float2(1.0f, 0.0f), zs[0]);
            const float3 py = eye_position(own_projection, m_viewport, position.xy + float2(0.0f, 1.0f), zs[2]);
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
