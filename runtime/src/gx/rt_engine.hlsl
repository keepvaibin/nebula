// SPDX-License-Identifier: GPL-3.0-only
// The ray tracing engine (docs/RAY_TRACING_ENGINE.md): the traced scene's light in radiance, from emitting geometry,
// the sun and the sky alone. cs_6_6 with RT_TEXTURED: the lighting's scene bindings and helpers (rt_lighting.hlsl,
// included) and the engine's own buffers by descriptor-heap index (g_engine_views, ray_tracing_engine.cpp).
//
// Per frame: EngineGBufferCS (full resolution) -> EngineDirectCS (the sun's shadow ray; ReSTIR DI candidates and
// temporal reuse) -> EngineDirectSpatialCS (spatial reuse, the shadow ray) -> EngineShadowBlurCS, EngineShadowPostCS,
// EngineShadowStabilizeCS (the sun's visibility over its penumbra, a short clamped history) -> EngineIndirectCS (half
// resolution: a cosine ray, its hit lit by the sun, the emitters and the sky; ReSTIR GI temporal reuse) ->
// EngineIndirectSpatialCS -> EngineSpecularCS (GGX) -> EngineTemporalCS (accumulation with moments, anti-lag) ->
// EngineAtrousCS x4 (variance-guided a-trous) -> EngineFinalCS (the sun's light by its filtered visibility; the slot's
// result at the camera's exposure; the samples' export for the translucent draws and the eye).
//
// Units: diffuse light as the lighting's `m` (irradiance over pi: a white surface shows m), specular as radiance; both
// unexposed in the histories, at the exposure in the slot's result.

#include "rt_lighting.hlsl"

// g_engine_views (flattened): the engine's buffers (raw, 16-byte records).
static const uint kEvGBuffer = 0u;        // per pixel two uint4: this frame's G-buffer
static const uint kEvGBufferPrev = 1u;    // the last traced frame's
static const uint kEvDiTemp = 2u;         // per pixel uint4: the direct reservoir after temporal reuse (reservoir_store)
static const uint kEvDi = 3u;             // per pixel uint4: this frame's final direct reservoir
static const uint kEvDiPrev = 4u;         // the last frame's
static const uint kEvDirect = 5u;         // per pixel uint4: the lights' direct diffuse (m) rgb (halves); z: the light
                                          // change (light_change: its kept sample's, whether its light is new; halves);
                                          // w: the sun's penumbra (kShadow*) and the water its light crosses (halves)
static const uint kEvGiTemp = 6u;         // per half-pixel two uint4: the indirect reservoir after temporal reuse
static const uint kEvGi = 7u;             // this frame's temporal indirect reservoir (the next frame's history)
static const uint kEvGiPrev = 8u;         // the last frame's
static const uint kEvGiOut = 9u;          // per half-pixel uint2: indirect diffuse (m) rgb (halves), valid
static const uint kEvSpecular = 10u;      // per pixel uint2: traced specular rgb (halves), hit distance
static const uint kEvHistDiffuse = 11u;   // per pixel uint4: diffuse rgb, history length (halves); moments 1, 2
static const uint kEvHistDiffusePrev = 12u;
static const uint kEvHistSpecular = 13u;
static const uint kEvHistSpecularPrev = 14u;
static const uint kEvAtrousA = 15u;       // per pixel uint4: diffuse rgb + variance, specular rgb + variance (halves);
static const uint kEvAtrousB = 16u;       // before EngineTemporalCS the sun shadow's blurs (x: visibility, penumbra)
static const uint kEvSlot = 17u;          // per pixel uint4: this slot's diffuse rgb, specular rgb (halves, exposed), z
static const uint kEvPicture = 18u;       // the EFB's colour (Texture2D, sRGB-encoded)
static const uint kEvShadow = 19u;        // per pixel uint: the sun's filtered visibility, its history length (halves)
static const uint kEvShadowPrev = 20u;    // the last frame's
static const uint kEvLightGrid = 21u;     // per grid cell kGridSlots uint2: a light (kGridSphere: a point light), its weight
static const uint kEvLightRemap = 22u;    // the last traced frame's lights as this frame's (read-only; engine_light_remap)
// The clear-surface pass (EngineClearCS, at the scene's end):
static const uint kEvWaterScene = 23u;    // the water window's acceleration structure (glass, crystal, water)
static const uint kEvWaterPositions = 24u;  // its triangles' eye-space vertices (36 bytes)
static const uint kEvWaterSurfaces = 25u;   // their vertex normals and draw flags (16 bytes; draw in bits 16-31)
static const uint kEvClearDraws = 26u;    // per water draw: the table's roughness (unorm16), kClear* flags
static const uint kEvClear = 27u;         // per pixel 2 uint4 (EngineClearCS): reflection rgb (halves, exposed),
                                          // transmittance r, eye z, present; transmittance g b, the pixel seen through
// The raster's motion (Texture2D<float2>, kEngineMotion): every opaque draw's own motion from this frame's pixel to the
// last frame's, in render pixels without the jitter (the temporal upscaler's target, DLSS or FSR 3; g_motion_jitter).
static const uint kEvMotion = 28u;
// The world's radiance cache (engine_cache_light, EngineCacheCS): kCacheEntries entries of kCacheEntryBytes.
static const uint kEvRadianceCache = 29u;
// g_engine_info.y in the clear pass: what the frame has of each kind.
static const uint kClearHasGlass = 1u;    // the water window's glass (clear_trace)
static const uint kClearHasSolids = 2u;   // clear solids in the scene (solid_front)

static const uint kEngineSpecular = 2u;   // traced specular
static const uint kEngineIndirect = 4u;   // traced indirect light
static const uint kEngineView = 0xF0u;    // GALAXY_RT_ENGINE_VIEW (bits 4-7): 1 direct, 2 indirect, 3 specular, 4 noisy,
                                          // 15 the raster's motion against the camera's (engine_history),
                                          // 8 the converged reference's diffuse light (GALAXY_RT_ENGINE_REFERENCE),
                                          // 14 the light sources' direct light alone (no sun, no bounce),
                                          // 5 the clear surfaces found (magenta), 6 the clear composite's coverage,
                                          // 7 the composite's albedo (unlit), 11 the sun's filtered visibility, 12 its
                                          // ray's (one per pixel), 13 the penumbra's radius (white: kShadowRadiusMost)
static const uint kEnginePoints = 256u;   // the game's point lights and the star core (GALAXY_RT_ENGINE_POINTS)
static const uint kEngineClearFind = 512u;  // GALAXY_RT_ENGINE_CLEAR=2: the clear pass finds, reflects nothing
static const uint kEngineClearNoRay = 1024u;  // =3: no reflection ray
static const uint kEngineClearNoSun = 2048u;  // =4: no sun highlight
static const uint kEngineClearNoBack = 4096u; // =5: no far wall
static const uint kEngineClearNoShade = 8192u; // =6: the reflection ray, its hit unlit
static const uint kEngineHitNoLights = 16384u;  // =7: hits without the lights' sample
static const uint kEngineHitNoSky = 32768u;     // =8: hits without the sky ray
static const uint kEngineClearNoTev = 65536u;   // =9: glass reflection hits without their TEV colour
static const uint kEngineBounceCache = 131072u; // GALAXY_RT_ENGINE_BOUNCES (default on): hits take the cached light
static const uint kEngineWaterLight = 524288u;  // last frame's water (kEvWaterScene, kEvWaterPositions) shades light
static const uint kEngineReference = 262144u;   // GALAXY_RT_ENGINE_REFERENCE: the converged reference accumulates
                                                 // (g_engine_params: x its buffer's view, y paths so far, z bounces)
static const uint kEngineNoAntiLag = 1048576u;   // GALAXY_RT_ENGINE_ANTILAG=0: the diffuse history without its anti-lag
static const uint kEngineNoLightHistory = 2097152u;  // GALAXY_RT_ENGINE_LIGHT_HISTORY=0: no temporal light reuse
static const uint kEngineNoBoiling = 8388608u;   // GALAXY_RT_ENGINE_BOILING=0: no boiling filter
static const uint kEngineMotion = 16777216u;     // the raster's motion this frame (kEvMotion)
static const uint kEngineNoFootprint = 33554432u;  // GALAXY_RT_ENGINE_FOOTPRINT=0: widths in pixels (kFootprintLines)
static const uint kEngineNoCache = 67108864u;      // GALAXY_RT_ENGINE_CACHE=0 (or no cache): hits lit by their samples
static const uint kEngineNoFirefly = 134217728u;   // GALAXY_RT_ENGINE_FIREFLY=0: new samples enter the history whole
static const uint kEngineNoDiSpatial = 1073741824u;  // GALAXY_RT_ENGINE_DI_SPATIAL=0: no spatial reuse of the direct light
// The indirect light at every pixel every frame (High; GALAXY_RT_ENGINE_GI_FULL): else one pixel of each 2 x 2 a frame.
static const uint kEngineGiFull = 2147483648u;
static const uint kEngineClearSolidLeft = 4194304u;  // GALAXY_RT_ENGINE_CLEAR=10: a clear solid's light still inside
                                                     // when its path's segments ran out (red, x 4), alone

// The water draws' clear records (kEvClearDraws, word per draw: roughness in bits 0-15, flags above).
static const uint kClearGlass = 1u << 16u;   // the engine's: glass or crystal (not mirror glass, not a metal coat)

// G-buffer material flags (word 3 bits 8-15).
static const uint kGbEmitter = 1u;
static const uint kGbCard = 2u;
static const uint kGbDelit = 4u;
static const uint kGbClear = 8u;    // a clear surface (crystal, glass, gems: kClearClass)
static const uint kGbGlow = 16u;    // a faintly glowing material (the table's intensity below 1): lit, its glow added

// A clear surface's reflectance at normal incidence: a dense crystal's (refractive index about 1.8, ((n - 1) /
// (n + 1))^2 = 0.08; window glass 0.04, diamond 0.17). The game's crystals are cut gems, not panes.
static const float kClearF0 = 0.08f;
// Paint over metal (a metal of a colour no metal has, EngineGBufferCS): a gloss lacquer's roughness at most.
static const float kPaintRoughness = 0.2f;
// A light source's surface as the camera shows it: its colour at most this many times white under the full light. Its
// light (the table's intensity: a Luma or lamp at 6, the radiance a lamp needs to show on what is near it in daylight)
// reaches the scene whole; shown at that radiance, star bits and coins would burn white on the tone curve. The display
// range, not the light, is limited.
static const float kEmitterShown = 1.5f;

// A light source's or a faint glow's surface as the camera shows it, over its colour (EngineFinalCS's).
float shown_emission(float intensity)
{
    return intensity < 1.0f ? intensity : min(intensity, kEmitterShown);
}
// A new direct or specular sample is a firefly past this many times its brightest neighbour's (EngineTemporalCS).
static const float kFireflyRatio = 2.0f;
// The share of the backdrop's colour (its chroma about its luminance) in the light it sends onto surfaces. A painted
// sky, like a photograph of one, is far more saturated than a sky's light: the game's blue in linear light holds ten to
// fifteen times its red, a clear sky's light (CIE clear sky, chromaticity about 0.27, 0.29) two to three times. At its
// full colour the shade under it turns navy and white surfaces violet. Reflections keep the backdrop as it shows (a
// mirror shows the picture of the sky).
static const float kSkyLightChroma = 0.4f;

float3 sky_light_colour(float3 radiance)
{
    const float l = luminance(radiance);
    return max(l + (radiance - l) * kSkyLightChroma, 0.0f);
}

uint engine_view(uint i)
{
    return g_engine_views[i >> 2u][i & 3u];
}

RWByteAddressBuffer engine_buffer(uint i)
{
    return ResourceDescriptorHeap[engine_view(i)];
}

uint pack_half2(float a, float b)
{
    return f32tof16(a) | (f32tof16(b) << 16u);
}

float3 safe_light(float3 c)
{
    return all(c == c) && all(c < 6.0e4f) ? max(c, 0.0f) : float3(0.0f, 0.0f, 0.0f);
}

struct GSurface {
    bool valid;
    float3 p;          // eye position
    float3 n;          // shading normal (relief applied)
    float3 ng;         // face normal, towards the eye
    float3 albedo;     // linear, de-lit
    float roughness;
    float metallic;
    uint flags;        // kGb*
    float emission;    // the light source's radiance over its colour (emitter pixels)
    float lift;        // how far its shading normals lift it off the face (along ng; surface_start)
    uint draw;
};

uint4 gb_word0(GSurface s)
{
    const uint3 a = uint3(round(saturate(sqrt(s.albedo)) * 255.0f));
    return uint4(pack_normal(s.n), pack_normal(s.ng), a.r | (a.g << 8u) | (a.b << 16u) |
                 (uint(round(saturate(s.roughness) * 255.0f)) << 24u),
                 uint(round(saturate(s.metallic) * 255.0f)) | (s.flags << 8u) | (min(s.draw, 0xFFFFu) << 16u));
}

GSurface gb_load(uint view, uint pixel, float4 projection, int2 q)
{
    RWByteAddressBuffer b = ResourceDescriptorHeap[view];
    const uint4 w0 = b.Load4(pixel * 32u);
    const uint4 w1 = b.Load4(pixel * 32u + 16u);
    GSurface s;
    s.valid = w1.x != 0u;
    const float z = asfloat(w1.x);
    s.p = eye_position(projection, g_viewport, float2(q) + 0.5f, z);
    s.n = unpack_normal(w0.x);
    s.ng = unpack_normal(w0.y);
    const float3 a = float3(w0.z & 0xFFu, (w0.z >> 8u) & 0xFFu, (w0.z >> 16u) & 0xFFu) / 255.0f;
    s.albedo = a * a;
    s.roughness = float(w0.z >> 24u) / 255.0f;
    s.metallic = float(w0.w & 0xFFu) / 255.0f;
    s.flags = (w0.w >> 8u) & 0xFFu;
    s.draw = w0.w >> 16u;
    s.emission = f16tof32(w1.y);
    s.lift = f16tof32(w1.y >> 16u);
    return s;
}

// The picture the engine's reuse and filter widths were set for: 1056 lines (2x the console's EFB). Each pixel takes one
// sample whatever the resolution, so a width in pixels gathers from a smaller part of the picture as the lines grow:
// at 6x (2112 lines a 2560 x 1440 window) the denoiser and the reservoirs' spatial reuse took a quarter of the surface
// they took at 2x, and the light's noise stayed as speckle and boiling blotches (the Gateway's Grand Star room). Every
// spatial width scales by the picture's lines over these, so a surface is filtered over the same share of the picture
// at every resolution (at least the 2x widths: fewer lines keep them).
static const float kFootprintLines = 1056.0f;

float footprint_scale()
{
    return (g_engine_info.x & kEngineNoFootprint) != 0u ? 1.0f : max(float(g_target_size.y) / kFootprintLines, 1.0f);
}

// The game's fog over a surface at EFB depth `depth` under its draw's fog words (g_draws words 12-15, RtDrawInfo::fog:
// A, B's magnitude | its shift << 24 | the type << 29, C, the colour | orthographic << 24): the share of the fog's
// colour, as the raster's pixel shader computes it from the same depth (ShaderGenerator, after Dolphin's
// PixelShaderGen), on the 8-bit blend's 1/256 steps. 0: no fog.
float gx_fog_share(uint4 fog, float depth)
{
    const uint type = fog.y >> 29u;
    if (type == 0u) {
        return 0.0f;
    }
    const uint zc = uint(clamp((1.0f - depth) * 16777216.0f, 0.0f, 16777215.0f));
    float ze;
    if ((fog.w & 0x1000000u) == 0u) {
        ze = asfloat(fog.x) * 16777216.0f / float(max(1u, (fog.y & 0xFFFFFFu) - (zc >> ((fog.y >> 24u) & 31u))));
    } else {
        ze = asfloat(fog.x) * float(zc) / 16777216.0f;
    }
    float f = saturate(ze - asfloat(fog.z));
    if (type == 4u) {
        f = 1.0f - exp2(-8.0f * f);
    } else if (type == 5u) {
        f = 1.0f - exp2(-8.0f * f * f);
    } else if (type == 6u) {
        f = exp2(-8.0f * (1.0f - f));
    } else if (type == 7u) {
        f = 1.0f - f;
        f = exp2(-8.0f * f * f);
    }
    return round(f * 256.0f) / 256.0f;
}

// The full-resolution G-buffer, and the composite's per-pixel normal, de-lighting and fog (g_pixels).
[numthreads(8, 8, 1)]
void EngineGBufferCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    // The light meter's sums start over (g_final after the probe's records; EngineFinalCS adds to them).
    if (g_probe.w != 0u && all(id.xy == 0u)) {
        g_final[g_sample_size.x * g_sample_size.y + kStarSlots + 4u] = uint4(0u, 0u, 0u, 0u);
        g_final[g_sample_size.x * g_sample_size.y + kStarSlots + 5u] = uint4(0u, 0u, 0u, 0u);
        g_final[g_sample_size.x * g_sample_size.y + kStarSlots + 6u] = uint4(0u, 0u, 0u, 0u);
    }
    const int2 q = int2(id.xy);
    const uint pixel = id.y * g_target_size.x + id.x;
    float3 p;
    uint4 packed = uint4(0u, 0u, 0u, 0u);
    uint4 w0 = uint4(0u, 0u, 0u, 0u);
    uint4 w1 = uint4(0u, 0u, 0u, 0u);
    if (pixel_point(q, p) && inside(g_viewport, float2(q) + 0.5f) && -p.z <= g_ambient.w) {
        const Surface surface = primary_surface(q, p);
        GSurface s;
        s.valid = true;
        s.p = p;
        s.n = surface.n;
        s.ng = surface.ng;
        s.roughness = 1.0f;
        s.metallic = 0.0f;
        s.flags = 0u;
        s.emission = 0.0f;
        s.lift = surface.lift;
        s.draw = 0xFFFFu;
        float own = 1.0f;
        float shading = 0.0f;
        bool delit = false;
        // The share of its environment the material passes (environment-mapped draws): a metal's reflectance.
        float3 reflectance = float3(-1.0f, -1.0f, -1.0f);
        float3 emitted = float3(-1.0f, -1.0f, -1.0f);   // an emitter's colour: its TEV output (below)
        float3 metal_colour = float3(-1.0f, -1.0f, -1.0f);   // its colour under the environment stage (below)
        uint2 probe_table = uint2(0xFFFFFFFFu, 0u);    // GALAXY_RT_PROBE: the draw's emission word, its glowing region
        if (surface.primary.x != 0u) {
            const uint tri = surface.primary.x - 1u;
            const float2 bary = float2(f16tof32(surface.primary.y), f16tof32(surface.primary.y >> 16u));
            const float3 next = eye_position(g_projection, g_viewport, float2(q) + float2(1.5f, 0.5f), p.z);
            float3 bent = s.n;
            // Its draw's textures filtered over the pixel's footprint, as the raster samples them: at their level-0
            // texels the shares below step from texel to texel, blocks across a magnified texture.
            const float cone = tev_cone(tri, p, length(next - p));
            if (delight_surface(tri, bary, p, s.ng, length(next - p), bent, shading)) {
                packed.y = pack_pixel_delight(bent, shading);
                s.n = bent;
                delit = true;
            }
            const uint draw_flags = g_surfaces.Load(tri * 16u + 12u);
            const uint draw = draw_flags >> 16u;
            s.draw = draw;
            // A clear surface (crystal, glass, gems) is drawn over or refracting what lies behind it: its pixel is not
            // its TEV's output, so the painted reflection's share of that output is not its share of the pixel, and
            // taking it out would darken what shows through to black. It keeps the game's colour; the traced specular
            // adds its Fresnel reflection.
            const bool clear = ((draw_flags >> 8u) & 15u) == kClearClass;
            s.flags |= clear ? kGbClear : 0u;
            if (!clear && draw < g_tev_info.x && (g_tev.Load(draw * kTevRecordBytes) & 0x80u) != 0u) {
                // The game's painted reflection (environment maps): the traced specular takes its place.
                bool pass;
                const float3 full =
                    srgb_to_linear(saturate(tev_evaluate_cone(draw, g_tev_info.x, tri, bary, 0u, cone, pass).rgb));
                const float3 bare = srgb_to_linear(
                    saturate(tev_evaluate_cone(draw, g_tev_info.x, tri, bary, kTevNoEnvironment, cone, pass).rgb));
                // A luminance ratio, so the tint a coloured metal or glass carries in its environment stage stays (a
                // per-channel ratio would take it out: gold to silver, green glass to grey).
                own = luminance(full) > 1e-4f ? saturate(luminance(bare) / luminance(full)) : 1.0f;
                const float3 white = srgb_to_linear(
                    saturate(tev_evaluate_cone(draw, g_tev_info.x, tri, bary, kTevWhiteEnvironment, cone, pass).rgb));
                // A metal's specular colour at the strength its environment stage passes the environment (white less
                // bare), at least its own colour's: its hue where that colour is a coloured metal's (gold, copper:
                // saturated in linear light, as their measured reflectance: gold's blue a third of its red), neutral
                // where it is a grey metal's warm or cool cast (silver, iron); chrome (bare near black) as the stage
                // passes it. The bare colour alone is a diffuse colour and too dark (gold reads olive); white less bare
                // alone loses gold's hue; keeping every hue would tint grey metals.
                const float3 passed = saturate(white - bare);
                const float bare_peak = max(bare.r, max(bare.g, bare.b));
                const float saturation = 1.0f - min(bare.r, min(bare.g, bare.b)) / max(bare_peak, 1e-4f);
                const float3 hue = lerp(float3(1.0f, 1.0f, 1.0f), bare / max(bare_peak, 1e-4f),
                                        smoothstep(0.5f, 0.8f, saturation));
                reflectance = luminance(bare) > 0.1f
                                  ? hue * max(max(passed.r, max(passed.g, passed.b)), bare_peak)
                                  : passed;
                metal_colour = bare;
            }
            const uint record = min(draw, max(g_draw_count, 1u) - 1u) * kDrawStride;
            const uint2 table = g_draws.Load2(record + 8u);
            probe_table = uint2(table.x, tev_glows_at(draw, g_tev_info.x, tri, bary) ? 1u : 0u);
            // The game's fog over it, for the composite: its share and the camera's exposure, its colour.
            {
                const uint4 fog_words = g_draws.Load4(record + 48u);
                const float fog = gx_fog_share(fog_words, g_efb_depth.Load(int3(q, 0)));
                packed.z = f32tof16(fog) | (f32tof16(physical_exposure()) << 16u);
                packed.w = fog > 0.0f ? fog_words.w & 0xFFFFFFu : 0u;
            }
            // The table's roughness and metalness (unorm16 x 65534; 0xFFFF: no row: a matte non-metal).
            const uint pbr = g_draws.Load(record + 16u);
            s.roughness = (pbr & 0xFFFFu) == 0xFFFFu ? 0.7f : max(float(pbr & 0xFFFFu) / 65534.0f, 0.02f);
            s.metallic = (pbr >> 16u) == 0xFFFFu ? 0.0f : saturate(float(pbr >> 16u) / 65534.0f);
            if ((g_draws.Load(record + 24u) & kDrawTableFoliage) != 0u) {
                s.flags |= kGbCard;
            }
            if ((table.x & kEmitNamed) != 0u && (table.x & 3u) != 0u && (table.x & kEmitNoLight) == 0u &&
                tev_glows_at(draw, g_tev_info.x, tri, bary) &&
                ((g_draws.Load(record + 24u) & kDrawTableNearGlow) == 0u || near_glow_light(p))) {
                const float intensity = asfloat(table.y);
                if (intensity < 1.0f && (g_draws.Load(record + 24u) & kDrawTableStar) == 0u) {
                    // A faintly glowing material (intensity below 1: coins, star chips, Boos, the garden's flowers)
                    // is a surface: lit, shaded and reflecting as any other, its glow (its colour times the
                    // intensity) added. Shown as a light source at its colour, a coin would read as a flat sticker,
                    // too bright in a dark stage and dull in a bright one.
                    s.flags |= kGbGlow;
                    s.emission = intensity;
                } else {
                    // A light source shows what it sends: its colour times the table's intensity; an atlas only in
                    // the table's glowing region of its texture (a candle stand's flame, not its wood).
                    s.flags |= kGbEmitter;
                    s.emission = max(intensity, 1.0f);
                    // Its colour is the radiance its light carries (EmitTriangleCS, reference_material: the draw's TEV
                    // output), not the game's shading of it in the EFB, which can be dark (the Star Reactor's
                    // fireballs).
                    if ((g_draws.Load(record + 24u) & kDrawTableRecolour) != 0u) {
                        emitted = srgb_to_linear(rgb8(table.x));
                    } else if (draw < g_tev_info.x) {
                        bool pass;
                        emitted =
                            srgb_to_linear(saturate(tev_evaluate_cone(draw, g_tev_info.x, tri, bary, 0u, cone, pass).rgb));
                    }
                }
            }
        }
        // The texel's albedo: the EFB's colour, the light painted into it and its painted reflection taken out.
        const Texture2D<float4> picture = ResourceDescriptorHeap[engine_view(kEvPicture)];
        const float3 color = srgb_to_linear(saturate(picture.Load(int3(q, 0)).rgb));
        // Past 1 it keeps its hue (clipping each channel would shift de-lit golden sand towards peach).
        const float3 lifted = max(color * (delit ? exp(-shading) : 1.0f) * own, 0.0f);
        s.albedo = lifted / max(max(lifted.r, max(lifted.g, lifted.b)), 1.0f);
        if (emitted.x >= 0.0f) {
            s.albedo = emitted;
        }
        // A metal of a colour no metal has is paint (or a dye coat) over metal: a dielectric. Measured metal
        // reflectances are greyish (silver, aluminium, iron, chrome) or warm (copper, gold, brass: hue about 8 to 60
        // degrees); a strongly teal, blue, green, olive, violet or pure red one reflects as paint does (its colour its
        // albedo, a clear coat at F0 0.04 over it); as a metal its whole colour would be its environment's. The coat is
        // a lacquer's (kPaintRoughness): at the metal's own roughness painted blocks show no shine.
        if (s.metallic > 0.0f) {
            const float3 c = metal_colour.x >= 0.0f ? metal_colour : s.albedo;
            const float peak = max(c.r, max(c.g, c.b));
            const float saturation = peak > 1e-4f ? 1.0f - min(c.r, min(c.g, c.b)) / peak : 0.0f;
            const float hue = degrees(atan2(sqrt(3.0f) * (c.g - c.b), 2.0f * c.r - c.g - c.b));
            const float hue360 = hue < 0.0f ? hue + 360.0f : hue;
            if (luminance(c) > 0.01f && saturation > 0.5f && (hue360 < 8.0f || hue360 > 60.0f)) {
                s.metallic = 0.0f;
                s.roughness = min(s.roughness, kPaintRoughness);
                reflectance = float3(-1.0f, -1.0f, -1.0f);
            }
        }
        // A metal's specular colour (its albedo's role for a metal, specular_f0) is what it reflects of its environment
        // where the game maps one: chrome's bare colour is near black.
        if (s.metallic > 0.0f && reflectance.x >= 0.0f && luminance(reflectance) > 0.02f) {
            s.albedo = lerp(s.albedo, reflectance, s.metallic * saturate(1.0f - own));
        }
        s.flags |= delit ? kGbDelit : 0u;
        // GALAXY_RT_PROBE (diagnostics): the probed pixel's surface after the samples and the stars in g_final
        // (SceneRayTracer prints [rt-probe-engine]).
        if (g_probe.z != 0u && all(id.xy == g_probe.xy * g_factor)) {
            const uint base = g_sample_size.x * g_sample_size.y + kStarSlots;
            g_final[base] = uint4(surface.primary.x, s.draw, probe_table.x, s.flags | (probe_table.y << 16u));
            g_final[base + 1u] = uint4(asuint(s.emission), asuint(luminance(color)), asuint(p.z), asuint(own));
            if ((g_engine_info.x & kEngineReference) == 0u && s.draw != 0xFFFFu) {
                const float2 region_uv = tev_region_uv(s.draw, g_tev_info.x, surface.primary.x - 1u,
                                                       float2(f16tof32(surface.primary.y), f16tof32(surface.primary.y >> 16u)));
                g_final[base + 3u] = uint4(asuint(region_uv.x), asuint(region_uv.y), s.draw, surface.primary.x);
            }
        }
        packed.x = pack_pixel_normal(surface.n, own);
        w0 = gb_word0(s);
        w1 = uint4(asuint(p.z), pack_half2(s.emission, s.lift), surface.primary.x, surface.primary.y);
        if ((id.x % g_factor) == 0u && (id.y % g_factor) == 0u && all(id.xy / g_factor < g_sample_size)) {
            const uint sample = (id.y / g_factor) * g_sample_size.x + id.x / g_factor;
            g_primary[sample] = surface.primary;
            g_normal[sample] = pack_normal(surface.n);
        }
    } else if ((id.x % g_factor) == 0u && (id.y % g_factor) == 0u && all(id.xy / g_factor < g_sample_size)) {
        const uint sample = (id.y / g_factor) * g_sample_size.x + id.x / g_factor;
        g_primary[sample] = uint2(0u, 0u);
        g_normal[sample] = 0u;
    }
    // The probe at a pixel with no traced surface (outside the viewport, beyond the lighting's reach, no depth): marked.
    if (g_probe.z != 0u && all(id.xy == g_probe.xy * g_factor) && w1.x == 0u) {
        const uint base = g_sample_size.x * g_sample_size.y + kStarSlots;
        g_final[base] = uint4(0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 0u);
        g_final[base + 1u] = uint4(0u, 0u, 0u, 0u);
    }
    g_pixels[pixel] = packed;
    RWByteAddressBuffer gb = engine_buffer(kEvGBuffer);
    gb.Store4(pixel * 32u, w0);
    gb.Store4(pixel * 32u + 16u, w1);
}

float3 fresnel_schlick(float3 f0, float cos_theta)
{
    const float f = pow(1.0f - saturate(cos_theta), 5.0f);
    return f0 + (1.0f - f0) * f;
}

float ggx_d(float alpha, float nh)
{
    const float a2 = alpha * alpha;
    const float d = nh * nh * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
}

// Smith height-correlated visibility (G2 / (4 n.l n.v)).
float smith_visibility(float alpha, float nl, float nv)
{
    const float a2 = alpha * alpha;
    const float gv = nl * sqrt(nv * nv * (1.0f - a2) + a2);
    const float gl = nv * sqrt(nl * nl * (1.0f - a2) + a2);
    return 0.5f / max(gv + gl, 1e-6f);
}

float smith_g1(float alpha, float nv)
{
    const float a2 = alpha * alpha;
    return 2.0f * nv / (nv + sqrt(a2 + (1.0f - a2) * nv * nv));
}

float3 specular_f0(GSurface s)
{
    const float dielectric = (s.flags & kGbClear) != 0u ? kClearF0 : 0.04f;
    return lerp(dielectric.xxx, s.albedo, s.metallic);
}

// The GGX lobe's alpha for the sun's highlight: at least the sun's disc (g_sun.w, the tangent of its radius) wide. A
// polished surface reflects the disc, not a point; at its own alpha the lobe meets the sampled direction in a spike.
float sun_alpha(float alpha)
{
    return max(alpha, g_sun.w);
}

// The sun's specular radiance off `s` towards `v` (its disc's centre, lit: the caller's shadow ray).
float3 sun_specular(GSurface s, float3 v)
{
    const float3 l = g_sun.xyz;
    const float nl = dot(s.n, l);
    const float nv = dot(s.n, v);
    if (nl <= 0.0f || nv <= 0.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float3 h = normalize(v + l);
    const float alpha = sun_alpha(s.roughness * s.roughness);
    return fresnel_schlick(specular_f0(s), dot(v, h)) *
           (ggx_d(alpha, dot(s.n, h)) * smith_visibility(alpha, nl, nv) * nl * kPi) * g_sun_light.rgb;
}

// Fresnel reflectance of a dielectric (unpolarised) at the cosine `c` of the incidence, `eta` = n2 / n1 (Walter et al.
// 2007): 1 under total internal reflection.
float fresnel_dielectric(float c, float eta)
{
    c = saturate(c);
    const float g2 = eta * eta - 1.0f + c * c;
    if (g2 < 0.0f) {
        return 1.0f;
    }
    const float g = sqrt(g2);
    const float a = (g - c) / (g + c);
    const float b = (c * (g + c) - 1.0f) / (c * (g - c) + 1.0f);
    return 0.5f * a * a * (1.0f + b * b);
}

float3x3 tangent_frame(float3 n)
{
    const float sign_z = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (sign_z + n.z);
    const float b = n.x * n.y * a;
    const float3 t1 = float3(1.0f + sign_z * n.x * n.x * a, sign_z * b, -sign_z * n.x);
    const float3 t2 = float3(b, sign_z + n.y * n.y * a, -n.y);
    return float3x3(t1, t2, n);
}

float3 cosine_direction(float3 n, float3 ng, float2 u)
{
    const float3x3 f = tangent_frame(n);
    const float r = sqrt(u.x);
    const float phi = 2.0f * kPi * u.y;
    float3 d = f[0] * (r * cos(phi)) + f[1] * (r * sin(phi)) + n * sqrt(max(0.0f, 1.0f - u.x));
    const float below = dot(d, ng);
    return below < 0.0f ? normalize(d - 2.0f * below * ng) : d;
}

// GGX visible normals (Heitz 2018) about n for view v.
float3 ggx_vndf(float3 n, float3 v, float alpha, float2 u)
{
    const float3x3 f = tangent_frame(n);
    const float3 vl = float3(dot(v, f[0]), dot(v, f[1]), dot(v, f[2]));
    const float3 vh = normalize(float3(alpha * vl.x, alpha * vl.y, vl.z));
    const float len2 = vh.x * vh.x + vh.y * vh.y;
    const float3 t1 = len2 > 0.0f ? float3(-vh.y, vh.x, 0.0f) * rsqrt(len2) : float3(1.0f, 0.0f, 0.0f);
    const float3 t2 = cross(vh, t1);
    const float r = sqrt(u.x);
    const float phi = 2.0f * kPi * u.y;
    const float a = r * cos(phi);
    float b = r * sin(phi);
    const float s = 0.5f * (1.0f + vh.z);
    b = (1.0f - s) * sqrt(1.0f - a * a) + s * b;
    const float3 nh = a * t1 + b * t2 + sqrt(max(0.0f, 1.0f - a * a - b * b)) * vh;
    const float3 hl = normalize(float3(alpha * nh.x, alpha * nh.y, max(0.0f, nh.z)));
    return f[0] * hl.x + f[1] * hl.y + f[2] * hl.z;
}

// A shadow ray from `origin` to `target` (stopping `margin` short of it): true when nothing the scene's lights are
// blocked by lies between.
bool engine_visible(float3 origin, float3 target, float margin, uint salt)
{
    const float3 to = target - origin;
    const float span = length(to);
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = to / max(span, 1e-4f);
    ray.TMin = 0.0f;
    ray.TMax = max(span - margin, 0.0f);
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>
        query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
    uint visits = 0u;
    while (query.Proceed()) {
        if (++visits > kMaxCandidates) {
            query.CommitNonOpaqueTriangleHit();
            query.Abort();
            break;
        }
        const bool late_hit = query.CandidateInstanceID() != 0u;
        const uint hit = late_hit ? 0u : g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
        // A clear solid stops kClearShadow of the light (the lighting without the engine gives its draws no shadow).
        const bool clear = !late_hit && ((hit >> 8u) & 15u) == kClearClass;
        if (late_hit ||
            (clear ? card_hash(query.CandidatePrimitiveIndex(), salt ^ 0x5BD1E995u) < kClearShadow
                   : (hit & kDrawNoShadow) == 0u &&
                         card_blocks(query.CandidatePrimitiveIndex(), hit,
                                     origin + ray.Direction * query.CandidateTriangleRayT(), salt))) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    return query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

// A direction within a disc about the sun's direction of tangent radius `spread` (g_sun.w: the sun's own).
float3 sun_direction_spread(float2 u, float spread)
{
    const float3x3 f = tangent_frame(g_sun.xyz);
    const float r = sqrt(u.x) * spread;
    const float phi = 2.0f * kPi * u.y;
    return normalize(g_sun.xyz + f[0] * (r * cos(phi)) + f[1] * (r * sin(phi)));
}

// A direction within the sun's disc (g_sun: towards it, tangent of its radius).
float3 sun_direction_sample(float2 u)
{
    return sun_direction_spread(u, g_sun.w);
}

// Light under water (kEngineWaterLight): kWaterAttenuation (rt_lighting.hlsl).
// The share of the water's hue shift the light under it keeps (the rest dims it evenly); the sea's full shift turns
// shallow water teal and its depths cyan-blue.
static const float kWaterHue = 0.35f;
// A slight warm balance on the light under water, its luminance kept.
static const float3 kWaterWarm = float3(1.05f, 1.01f, 0.86f) / dot(float3(1.05f, 1.01f, 0.86f), float3(0.2126f, 0.7152f, 0.0722f));
// Forward scattering widens the sun's disc with the depth: tan 0.5 degrees per metre (100 units), at most 10 degrees.
static const float kWaterSpread = 0.0000873f;
static const float kWaterSpreadMost = 0.176f;

// The water between eye point `p` and the light along direction `l`: the distance to last frame's first water surface
// along it (the water pass's, its eye space through g_reproject) when that surface lies near level (within 45 degrees
// of the player's horizon: a lake's or sea's top, not a waterfall's sheet), else 0 (above water, or no water).
float water_depth_along(float3 p, float3 l)
{
    if ((g_engine_info.x & kEngineWaterLight) == 0u || (g_flags & 1u) == 0u) {
        return 0.0f;
    }
    const RaytracingAccelerationStructure water = ResourceDescriptorHeap[engine_view(kEvWaterScene)];
    const ByteAddressBuffer positions = ResourceDescriptorHeap[engine_view(kEvWaterPositions)];
    RayDesc ray;
    ray.Origin = transform(g_reproject, p);
    ray.Direction = normalize(rotate3(g_reproject, l));
    ray.TMin = 0.0f;
    ray.TMax = 1.0e5f;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(water, RAY_FLAG_NONE, 0xFFu, ray);
    while (query.Proceed()) {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return 0.0f;
    }
    const uint tri = query.CommittedPrimitiveIndex();
    const float3 v0 = asfloat(positions.Load3(tri * 36u));
    const float3 v1 = asfloat(positions.Load3(tri * 36u + 12u));
    const float3 v2 = asfloat(positions.Load3(tri * 36u + 24u));
    const float3 face = cross(v1 - v0, v2 - v0);
    const float3 up = rotate3(g_reproject, g_up.xyz);
    if (dot(face, face) <= 1e-24f || abs(dot(normalize(face), normalize(up))) < 0.7071f) {
        return 0.0f;
    }
    return query.CommittedRayT();
}

// The light left after `depth` of water: its attenuation, kWaterHue of its hue shift kept.
float3 water_tint(float depth)
{
    const float3 t = exp(-kWaterAttenuation * depth);
    return lerp(luminance(t).xxx, t, kWaterHue) * kWaterWarm;
}

// The share of light from direction `l` reaching `p` through the water above it (1 out of the water).
float3 water_through(float3 p, float3 l)
{
    const float depth = water_depth_along(p, l);
    return depth > 0.0f ? water_tint(depth) : float3(1.0f, 1.0f, 1.0f);
}

struct Hit {
    bool surface;      // a scene triangle (else the sky, or a late occluder: no light)
    bool late;
    float t;
    uint tri;
    float2 b;
};

Hit engine_trace(float3 origin, float3 direction, uint salt)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = 1.0e6f;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
    uint visits = 0u;
    while (query.Proceed()) {
        if (++visits > kMaxCandidates) {
            query.CommitNonOpaqueTriangleHit();
            query.Abort();
            break;
        }
        const bool late_hit = query.CandidateInstanceID() != 0u;
        const uint hit = late_hit ? 0u : g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
        // A clear solid the engine traces stops kClearShadow of these rays, as of the light (engine_visible): its
        // facets turn most of them off the straight path; the rest see what lies behind it.
        if (!late_hit && solid_draw(hit)) {
            if (card_hash(query.CandidatePrimitiveIndex(), salt ^ 0x5BD1E995u) < kClearShadow) {
                query.CommitNonOpaqueTriangleHit();
            }
        } else if (late_hit || card_blocks(query.CandidatePrimitiveIndex(), hit,
                                           origin + direction * query.CandidateTriangleRayT(), salt)) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    Hit h;
    h.surface = false;
    h.late = false;
    h.t = 1.0e6f;
    h.tri = 0u;
    h.b = float2(0.0f, 0.0f);
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        h.t = query.CommittedRayT();
        h.late = query.CommittedInstanceID() != 0u;
        h.surface = !h.late && length(origin + direction * h.t) <= g_ambient.w;
        h.tri = query.CommittedPrimitiveIndex();
        h.b = query.CommittedTriangleBarycentrics();
    }
    return h;
}

struct LightSample {
    uint ref;          // its light: an emitting triangle (engine_light_choose's index), kGridSphere | a point light
    uint uv;           // the point on it (engine_light_at): a triangle's barycentrics, a sphere's direction
    float3 y;          // eye position on the emitter (a sphere light's: on its shadow sphere)
    float3 face;       // its face's normal (eye; a sphere light's: outward)
    float3 radiance;   // a triangle's radiance; a sphere light's L h^2 / (2 pi R^2)
    float beam;        // a triangle's beam exponent (0: Lambertian); a sphere light's h
    float sides;       // a triangle's facing (emitter_facing); kSidesSphere: a sphere light
    float area;        // a triangle's area (its near field: engine_light_target)
};

// No light (LightSample.ref).
static const uint kNoLight = 0xFFFFFFFFu;

// The game's point lights (g_points: its lamps, the Octoombas' bulbs; weight g_point_light) and the Comet
// Observatory's star core light a surface as the game's falloff has it: colour x weight x n.l x h^2 / (h^2 + d^2)
// (h the light's half distance; the on-axis light of a glowing disc of radius h and radiance colour x weight), as the
// lighting without the engine shows them. Their shadows are traced to a point on the hemisphere of a sphere of the
// light's radius (g_point_radius: a lamp's 60 units, the core's ball) that faces the surface: soft, never through the
// lamp's own body or the core's ball. Chosen among the emitting triangles in proportion to 4 pi^2 h^2 L (a sphere of
// radius h and radiance L sends that).
static const float kSidesSphere = 3.0f;

float3 sphere_colour(uint i)
{
    return linear_hue(g_points[2u * i + 1u].rgb) * g_point_light;
}

float sphere_power(uint i)
{
    const float h2 = 1.0f / max(g_points[2u * i + 1u].w, 1e-12f);
    return 4.0f * kPi * kPi * h2 * luminance(sphere_colour(i));
}

// The point lights' power (the emitting triangles' is emit_element(0).y).
float spheres_total()
{
    if ((g_engine_info.x & kEnginePoints) == 0u) {
        return 0.0f;
    }
    float sum = 0.0f;
    [loop] for (uint i = 0u; i < g_point_count; ++i) {
        sum += sphere_power(i);
    }
    return sum;
}

// A light in proportion to its power (`u` in [0, tris + spheres)): an emitting triangle (its index among the emitters)
// or, with kGridSphere, a point light (its index); its power.
static const uint kGridSphere = 0x80000000u;

uint engine_light_choose(float tris, float u, out float power)
{
    if (u < tris) {
        // The triangle whose running power first passes u.
        uint lo = 0u;
        uint hi = g_tev_info.z - 1u;
        [loop] while (lo < hi) {
            const uint mid = (lo + hi) >> 1u;
            if (emit_running(mid) > u) {
                hi = mid;
            } else {
                lo = mid + 1u;
            }
        }
        float4 a, b, c, d;   // v0, area; e1, two-sided; e2, beam; radiance, power
        emit_triangle_of(lo, a, b, c, d);
        power = d.w;
        return lo;
    }
    float running = tris;
    uint i = 0u;
    [loop] for (; i + 1u < g_point_count; ++i) {
        running += sphere_power(i);
        if (u < running) {
            break;
        }
    }
    power = sphere_power(i);
    return kGridSphere | i;
}

// A light's centre (eye space): an emitting triangle's centroid, a point light's position.
float3 engine_light_centre(uint id)
{
    if ((id & kGridSphere) != 0u) {
        return g_points[2u * (id & ~kGridSphere)].xyz;
    }
    float4 a, b, c, d;
    emit_triangle_of(id, a, b, c, d);
    return a.xyz + (b.xyz + c.xyz) * (1.0f / 3.0f);
}

// Light `id` at the point `uv` on it, as this frame has the light (it may have moved, changed or gone since the point
// was chosen): `uv` a triangle's barycentrics (unorm16 x 2, at v1 and v2), the direction (world, pack_normal) from the
// centre of a sphere (a point light's, an isotropic glow's). False where this frame has no such light or it sends
// nothing.
bool engine_light_at(uint id, uint uv, out LightSample l)
{
    l.ref = id;
    l.uv = uv;
    l.y = float3(0.0f, 0.0f, 0.0f);
    l.face = float3(0.0f, 0.0f, 1.0f);
    l.radiance = float3(0.0f, 0.0f, 0.0f);
    l.beam = 0.0f;
    l.sides = 0.0f;
    l.area = 0.0f;
    if ((id & kGridSphere) == 0u) {
        if (id >= g_tev_info.z) {
            return false;
        }
        float4 a, b, c, d;   // v0, area; e1, two-sided; e2, beam; radiance, power
        emit_triangle_of(id, a, b, c, d);
        if (!(d.w > 0.0f) || !(a.w > 0.0f)) {
            return false;
        }
        l.area = a.w;
        l.radiance = d.rgb;
        if (b.w >= 1.5f) {
            // An isotropic glow (a sprite standing for a glowing volume: a lamp's orb, a flame) is the sphere of its
            // power about the triangle's centre: radius sqrt(A / 2 pi), so its projected area is the sprite's mean over
            // directions (A / 2) and the hemisphere facing a surface, where its point is drawn as a point light's is,
            // has the area A. Drawn in the sprite's plane through the centre, its light met the lamp's own cup or holder
            // below the orb on the way to the ground under it, and the lamp lit nothing there.
            const float3 centre = a.xyz + (b.xyz + c.xyz) * (1.0f / 3.0f);
            l.face = normalize(rotate3(g_to_eye, unpack_normal(uv)));
            l.y = centre + l.face * sqrt(a.w / (2.0f * kPi));
            return true;
        }
        const float2 r = float2(float(uv & 0xFFFFu), float(uv >> 16u)) / 65535.0f;
        l.y = a.xyz + b.xyz * r.x + c.xyz * r.y;
        l.face = normalize(cross(b.xyz, c.xyz));
        l.beam = c.w;
        l.sides = b.w;
        return true;
    }
    const uint i = id & ~kGridSphere;
    if ((g_engine_info.x & kEnginePoints) == 0u || i >= g_point_count || !(sphere_power(i) > 0.0f)) {
        return false;
    }
    const float3 centre = g_points[2u * i].xyz;
    const float r = max(g_point_radius[i >> 2u][i & 3u], 1.0f);
    l.face = normalize(rotate3(g_to_eye, unpack_normal(uv)));
    l.y = centre + l.face * r;
    const float h2 = 1.0f / max(g_points[2u * i + 1u].w, 1e-12f);
    l.radiance = sphere_colour(i) * (h2 / (2.0f * kPi * r * r));
    l.beam = sqrt(h2);
    l.sides = kSidesSphere;
    return true;
}

// A direction uniform on the hemisphere about `axis`.
float3 hemisphere_direction(float3 axis, inout uint seed)
{
    const float z = emit_random(seed);
    const float phi = 2.0f * kPi * emit_random(seed);
    const float sz = sqrt(max(0.0f, 1.0f - z * z));
    const float3x3 f = tangent_frame(axis);
    return f[0] * (sz * cos(phi)) + f[1] * (sz * sin(phi)) + axis * z;
}

// A point on light `id` for the surface at `p`: uniform over a triangle, over the hemisphere of a sphere (a point
// light's, an isotropic glow's) facing `p`; its density over the light's area (`area_density`: 1 / area).
bool engine_light_point(uint id, float3 p, inout uint seed, out LightSample l, out float area_density)
{
    area_density = 0.0f;
    uint uv = 0u;
    float area = 0.0f;
    if ((id & kGridSphere) == 0u) {
        if (id < g_tev_info.z) {
            float4 a, b, c, d;
            emit_triangle_of(id, a, b, c, d);
            area = a.w;
            if (b.w >= 1.5f) {
                const float3 centre = a.xyz + (b.xyz + c.xyz) * (1.0f / 3.0f);
                const float3 axis = normalize(p - centre + float3(0.0f, 0.0f, 1e-4f));
                uv = pack_normal(eye_direction_to_world(hemisphere_direction(axis, seed)));
            } else {
                float r1 = emit_random(seed);
                float r2 = emit_random(seed);
                if (r1 + r2 > 1.0f) {
                    r1 = 1.0f - r1;
                    r2 = 1.0f - r2;
                }
                uv = uint(round(r1 * 65535.0f)) | (uint(round(r2 * 65535.0f)) << 16u);
            }
        }
    } else {
        const uint i = id & ~kGridSphere;
        if (i < g_point_count) {
            const float3 centre = g_points[2u * i].xyz;
            const float r = max(g_point_radius[i >> 2u][i & 3u], 1.0f);
            area = 2.0f * kPi * r * r;
            const float3 axis = normalize(p - centre + float3(0.0f, 0.0f, 1e-4f));
            uv = pack_normal(eye_direction_to_world(hemisphere_direction(axis, seed)));
        }
    }
    if (!engine_light_at(id, uv, l) || !(area > 0.0f)) {
        return false;
    }
    area_density = 1.0f / area;
    return true;
}

// The light that was `ref` in the last traced frame, as this frame has it (kNoLight: gone, no longer drawn). From
// kEvLightRemap (SceneRayTracer::light_remap): a header (its ranges, its point lights), per range of lights a draw kept
// from that frame to this one (sorted by their first index then) the first index then, their count and the first
// index now, then per point light then its index now (kNoLight: gone). RTXDI keeps its reservoirs' lights across
// frames the same way (RAB_TranslateLightIndex); a reservoir whose light is gone starts over.
// Whether this frame's light `ref` was the last traced frame's too (kEvLightRemap: after its ranges by their first index
// then and its point lights, the same ranges by their first index now and per point light now whether it was).
bool engine_light_existed(uint ref)
{
    if (ref == kNoLight) {
        return false;
    }
    const ByteAddressBuffer remap = ResourceDescriptorHeap[engine_view(kEvLightRemap)];
    const uint4 counts = remap.Load4(0u);
    const uint now_ranges = 16u + counts.x * 16u + counts.y * 4u;
    if ((ref & kGridSphere) != 0u) {
        const uint i = ref & ~kGridSphere;
        return i < counts.w && remap.Load(now_ranges + counts.z * 16u + i * 4u) != 0u;
    }
    if (counts.z == 0u || remap.Load(now_ranges) > ref) {
        return false;
    }
    uint lo = 0u;
    uint hi = counts.z - 1u;
    [loop] while (lo < hi) {
        const uint mid = (lo + hi + 1u) >> 1u;
        if (remap.Load(now_ranges + mid * 16u) <= ref) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    const uint2 range = remap.Load2(now_ranges + lo * 16u);
    return ref - range.x < range.y;
}

uint engine_light_remap(uint ref)
{
    if (ref == kNoLight) {
        return kNoLight;
    }
    const ByteAddressBuffer remap = ResourceDescriptorHeap[engine_view(kEvLightRemap)];
    const uint2 counts = remap.Load2(0u);
    if ((ref & kGridSphere) != 0u) {
        const uint i = ref & ~kGridSphere;
        if (i >= counts.y) {
            return kNoLight;
        }
        const uint now = remap.Load(16u + counts.x * 16u + i * 4u);
        return now != kNoLight ? (kGridSphere | now) : kNoLight;
    }
    if (counts.x == 0u || remap.Load(16u) > ref) {
        return kNoLight;
    }
    // The last range whose first index is not after `ref`.
    uint lo = 0u;
    uint hi = counts.x - 1u;
    [loop] while (lo < hi) {
        const uint mid = (lo + hi + 1u) >> 1u;
        if (remap.Load(16u + mid * 16u) <= ref) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    const uint3 range = remap.Load3(16u + lo * 16u);
    return ref - range.x < range.y ? range.z + (ref - range.x) : kNoLight;
}

// One light point for the surface at `p`, drawn in proportion to power over the emitting triangles (`tris`, their
// total) and the point lights (`spheres`); `chance`: its density as resampling weighs it (the light's share of the power
// over its area).
bool engine_light_pick(float3 p, float tris, float spheres, inout uint seed, out LightSample l, out float chance)
{
    chance = 0.0f;
    const float total = tris + spheres;
    float power;
    const uint id = engine_light_choose(tris, emit_random(seed) * total, power);
    float area_density;
    if (!(power > 0.0f) || !engine_light_point(id, p, seed, l, area_density)) {
        return false;
    }
    chance = power / total * area_density;
    return true;
}

// Grid-based light reservoirs (ReGIR: Boksansky, Jukarainen and Wyman, "Rendering Many Lights with Grid-Based
// Reservoirs", Ray Tracing Gems II, 2021; our own code). Drawn by power alone, a surface's light candidates are the
// scene's biggest lights wherever it is: beside a lamp, a fire bar or a laser while the star core or a lava sea is in
// the scene, the near light is rarely a candidate, its light pool stays dark and the few pixels that find it are lowered
// as fireflies. A grid of cells about the camera keeps per cell kGridSlots lights, each resampled (kGridCandidates drawn
// by power) by its light at the cell (power over the squared distance from the cell's centre, the cell's half diagonal
// added): what is near a cell fills it. A surface in a cell draws its candidates from the cell's slots, each weighed by
// its slot's resampling weight (chained resampled importance sampling: unbiased, a slot holding every light with a
// light at its cell above zero); outside the grid, by power.
static const uint kGridCells = 24u;            // a side's cells
static const float kGridCellSize = 256.0f;     // eye units (Mario stands 160 tall)
static const uint kGridSlots = 64u;
static const uint kGridCandidates = 8u;

bool grid_cell(float3 p, out uint cell)
{
    cell = 0u;
    const int3 c = int3(floor(p / kGridCellSize)) + int(kGridCells / 2u);
    if (any(c < 0) || any(c >= int(kGridCells))) {
        return false;
    }
    cell = (uint(c.z) * kGridCells + uint(c.y)) * kGridCells + uint(c.x);
    return true;
}

float3 grid_cell_centre(uint cell)
{
    const uint3 c = uint3(cell % kGridCells, (cell / kGridCells) % kGridCells, cell / (kGridCells * kGridCells));
    return (float3(c) - float(kGridCells / 2u) + 0.5f) * kGridCellSize;
}

// A cell's slot: kGridCandidates lights drawn by power, one kept in proportion to its light at the cell; its weight
// (the resampling's sum over the count, over the kept light's target).
[numthreads(64, 1, 1)]
void EngineLightGridCS(uint3 id : SV_DispatchThreadID)
{
    const uint slot = id.x;
    if (slot >= kGridCells * kGridCells * kGridCells * kGridSlots) {
        return;
    }
    RWByteAddressBuffer grid = engine_buffer(kEvLightGrid);
    const float tris = g_tev_info.z != 0u ? max(emit_element(0u).y, 0.0f) : 0.0f;
    const float spheres = spheres_total();
    const float total = tris + spheres;
    if (!(total > 0.0f)) {
        grid.Store2(slot * 8u, uint2(0u, 0u));
        return;
    }
    const float3 centre = grid_cell_centre(slot / kGridSlots);
    const float reach2 = 0.75f * kGridCellSize * kGridCellSize;
    uint seed = emit_hash(slot ^ (g_frame * 0x7FEB352Du) ^ 0x2C1B3C6Du);
    uint kept = 0u;
    float kept_target = 0.0f;
    float weight_sum = 0.0f;
    [loop] for (uint k = 0u; k < kGridCandidates; ++k) {
        float power;
        const uint light = engine_light_choose(tris, emit_random(seed) * total, power);
        if (!(power > 0.0f)) {
            continue;
        }
        const float3 to = engine_light_centre(light) - centre;
        const float target = power / (dot(to, to) + reach2);
        const float w = target / (power / total);
        weight_sum += w;
        if (emit_random(seed) * weight_sum < w) {
            kept = light;
            kept_target = target;
        }
    }
    const float weight = kept_target > 0.0f ? weight_sum / (float(kGridCandidates) * kept_target) : 0.0f;
    grid.Store2(slot * 8u, uint2(kept, asuint(weight)));
}

// One light point for the surface at `p`: from its grid cell's slots, else by power (engine_light_pick).
bool engine_light_pick_near(float3 p, float tris, float spheres, inout uint seed, out LightSample l, out float chance)
{
    uint cell;
    if (!grid_cell(p, cell)) {
        return engine_light_pick(p, tris, spheres, seed, l, chance);
    }
    chance = 0.0f;
    const uint slot = cell * kGridSlots + min(uint(emit_random(seed) * float(kGridSlots)), kGridSlots - 1u);
    const uint2 e = engine_buffer(kEvLightGrid).Load2(slot * 8u);
    const float weight = asfloat(e.y);
    float area_density;
    if (!(weight > 0.0f) || !engine_light_point(e.x, p, seed, l, area_density)) {
        return false;
    }
    chance = area_density / weight;
    return true;
}

// A light point's unshadowed light at `p` (normal `n`) per unit of its area (the resampling's target is its luminance):
// a triangle's radiance L cos_l cos_s / (pi d^2) (a beam's cos^n), a sphere light's L h^2 n.l / (2 pi R^2 (h^2 + d^2))
// on the hemisphere facing `p`.
float engine_light_target(float3 p, float3 n, LightSample l, out float3 light)
{
    if (l.sides > 2.5f) {
        light = float3(0.0f, 0.0f, 0.0f);
        const float3 to = l.y - p;
        const float d2 = max(dot(to, to), 1e-4f);
        const float3 dir = to * rsqrt(d2);
        const float cos_surface = dot(n, dir);
        if (cos_surface <= 0.0f || dot(l.face, dir) >= 0.0f) {
            return 0.0f;
        }
        light = l.radiance * (cos_surface / (l.beam * l.beam + d2));
        return luminance(light);
    }
    // Nearer than its own size (d^2 < A / pi, a disc of its area's radius) a triangle's light is its near field's: at
    // most about its radiance, not 1 / d^2.
    return emitter_target(p, n, l.y, l.face, l.radiance, l.beam, l.sides, light, max(l.area / kPi, 1e-4f));
}

// The lights at `p` (normal `n`; shadow ray from `origin`): `candidates` drawn by engine_light_pick_near, one kept in
// proportion to its target (resampled importance sampling), its light behind one shadow ray, weighted to stay unbiased.
float3 engine_light_sample(float3 p, float3 n, float3 origin, uint candidates, inout uint seed, uint salt)
{
    const float tris = g_tev_info.z != 0u ? max(emit_element(0u).y, 0.0f) : 0.0f;
    const float spheres = spheres_total();
    if (!(tris + spheres > 0.0f)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float weight_sum = 0.0f;
    float kept_target = 0.0f;
    float3 kept_light = float3(0.0f, 0.0f, 0.0f);
    float3 kept_point = float3(0.0f, 0.0f, 0.0f);
    [loop] for (uint k = 0u; k < candidates; ++k) {
        LightSample l;
        float chance;
        if (!engine_light_pick_near(p, tris, spheres, seed, l, chance)) {
            continue;
        }
        float3 light;
        const float target = engine_light_target(p, n, l, light);
        const float w = target / chance;
        weight_sum += w;
        if (w > 0.0f && emit_random(seed) * weight_sum < w) {
            kept_target = target;
            kept_light = light;
            kept_point = l.y;
        }
    }
    if (!(kept_target > 0.0f) ||
        !engine_visible(origin, kept_point, 0.5f + 0.001f * length(kept_point - origin), salt)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return kept_light * (weight_sum / (float(candidates) * kept_target));
}

// The world's radiance cache (our implementation of path-space filtering, Binder, Fricke and Keller 2019, as NVIDIA's
// SHaRC and other engines' world-space caches use it). A bounce or reflection ray's hit outside the picture was lit by
// one light of four candidates behind one shadow ray, one sun ray and one sky ray: in a room lit by a core and many
// sparks (the Gateway's Grand Star room) nearly all of the floor's light came from those single samples, most of them
// dark and a few bright, speckle that no filter at the floor makes clean (without the lights' sample at the hits the
// floor's bounce was smooth and dark). Each hit adds its light to a cell of a hash grid over world space (the picture's
// own filtered light where the picture shows the hit) and is lit by the cell's light over its samples and its last
// kCacheFrames frames, times its own albedo. A cell is about kCachePixels wide on the screen at its distance (at
// kFootprintLines; powers of two from kCacheLeast), one per facing axis, so a wall's two sides and a floor and the wall
// beside it keep their own light. A cell's light takes over from the hit's once it has kCacheTrusted samples.
static const uint kCacheEntries = 1048576u;
// Per entry: key (0: free), the frame it was last used, this frame's sums r g b (x kCacheScale) and samples; its light
// rg, b (halves) and the samples it stands for (half).
static const uint kCacheEntryBytes = 32u;
static const uint kCacheProbes = 8u;
static const float kCachePixels = 8.0f;
static const float kCacheLeast = 2.0f;
static const float kCacheScale = 256.0f;
static const float kCacheMost = 64.0f;     // a sample's light at most (the sums' range)
static const float kCacheFrames = 16.0f;
static const float kCacheTrusted = 8.0f;
static const uint kCacheIdle = 600u;       // frames unused before an entry is freed

// An eye-space point and normal in world space (g_to_eye's inverse: the adjugate over the determinant).
float3 engine_world_point(float3 p)
{
    const float3 r0 = g_to_eye[0].xyz;
    const float3 r1 = g_to_eye[1].xyz;
    const float3 r2 = g_to_eye[2].xyz;
    const float3 d = p - float3(g_to_eye[0].w, g_to_eye[1].w, g_to_eye[2].w);
    return (cross(r1, r2) * d.x + cross(r2, r0) * d.y + cross(r0, r1) * d.z) / dot(r0, cross(r1, r2));
}

float3 engine_world_normal(float3 n)
{
    return normalize(g_to_eye[0].xyz * n.x + g_to_eye[1].xyz * n.y + g_to_eye[2].xyz * n.z);
}

// The cache's entry for the cell of eye-space point `p` facing `n`, claimed when free and `claim` (~0: none within
// kCacheProbes); `size`: the cell's width (world units).
uint engine_cache_entry(float3 p, float3 n, bool claim, out float size)
{
    size = 0.0f;
    // No camera this frame (a degenerate world-to-eye): no cell.
    if (!(abs(dot(g_to_eye[0].xyz, cross(g_to_eye[1].xyz, g_to_eye[2].xyz))) > 1e-12f)) {
        return ~0u;
    }
    const float pixel = length(p) * 2.0f / max(abs(g_viewport.z * g_projection.x), 1e-6f) * footprint_scale();
    const float level = clamp(ceil(log2(max(pixel * kCachePixels, kCacheLeast) / kCacheLeast)), 0.0f, 15.0f);
    size = kCacheLeast * exp2(level);
    const int3 c = int3(floor(engine_world_point(p) / size));
    const float3 w = engine_world_normal(n);
    const float3 a = abs(w);
    const uint axis = a.x >= a.y && a.x >= a.z ? (w.x >= 0.0f ? 0u : 1u)
                      : a.y >= a.z             ? (w.y >= 0.0f ? 2u : 3u)
                                               : (w.z >= 0.0f ? 4u : 5u);
    const uint h = emit_hash(asuint(c.x) ^ emit_hash(asuint(c.y) ^ emit_hash(asuint(c.z) ^
                                                                                  emit_hash(uint(level) | (axis << 4u)))));
    const uint key = emit_hash(h ^ 0x9E3779B9u) | 1u;
    RWByteAddressBuffer cache = engine_buffer(kEvRadianceCache);
    [loop] for (uint k = 0u; k < kCacheProbes; ++k) {
        const uint e = (h + k) % kCacheEntries;
        uint held;
        if (claim) {
            cache.InterlockedCompareExchange(e * kCacheEntryBytes, 0u, key, held);
        } else {
            held = cache.Load(e * kCacheEntryBytes);
        }
        if (held == key || (claim && held == 0u)) {
            return e;
        }
        if (held == 0u) {
            return ~0u;
        }
    }
    return ~0u;
}

// Adds a hit's light `m` (eye-space point `p`, normal `n`) to its cell; the cell's light once it has kCacheTrusted
// samples, else `m`. A reflection's hit (`footprint`: its ray cone's width there; 0: a bounce) narrower than the cell
// keeps its own light: a sharp reflection would show the cells as blocks.
float3 engine_cache_light(float3 p, float3 n, float3 m, float footprint)
{
    if ((g_engine_info.x & kEngineNoCache) != 0u) {
        return m;
    }
    float size;
    const uint e = engine_cache_entry(p, n, true, size);
    if (e == ~0u) {
        return m;
    }
    RWByteAddressBuffer cache = engine_buffer(kEvRadianceCache);
    const uint at = e * kCacheEntryBytes;
    const uint3 add = uint3(round(min(max(m, 0.0f), kCacheMost) * kCacheScale));
    uint unused;
    cache.InterlockedMax(at + 4u, g_frame, unused);
    cache.InterlockedAdd(at + 8u, add.r, unused);
    cache.InterlockedAdd(at + 12u, add.g, unused);
    cache.InterlockedAdd(at + 16u, add.b, unused);
    cache.InterlockedAdd(at + 20u, 1u, unused);
    const uint2 held = cache.Load2(at + 24u);
    return f16tof32(held.y >> 16u) >= kCacheTrusted && (footprint <= 0.0f || footprint >= size)
               ? float3(f16tof32(held.x), f16tof32(held.x >> 16u), f16tof32(held.y))
               : m;
}

// The light the surface a hit's sky ray meets (`h`, along `direction` from `origin`) sends back: its cell's light times
// its albedo, no sample of its own. Every further bounce goes through the cache (the cells' light includes it), and
// the series converges over frames as each bounce takes the albedo; the emitters' light is the hits' own light sample.
// With one bounce the off-picture hits had only their direct light: an enclosed room's walls lit each other not at all
// (the Grand Star room 31 to 58 % darker than the converged reference's 8 bounces).
float3 engine_cache_bounce(Hit h, float3 origin, float3 direction)
{
    if ((g_engine_info.x & kEngineNoCache) != 0u) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float3 albedo;
    float3 emission;
    reference_material_cone(h.tri, h.b, kTevNoCone, albedo, emission);
    if (!any(albedo > 0.0f)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float3 v0 = asfloat(g_positions.Load3(h.tri * 36u));
    const float3 v1 = asfloat(g_positions.Load3(h.tri * 36u + 12u));
    const float3 v2 = asfloat(g_positions.Load3(h.tri * 36u + 24u));
    const float3 face = cross(v1 - v0, v2 - v0);
    if (dot(face, face) <= 1e-24f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float3 ng = normalize(face);
    ng = dot(ng, direction) > 0.0f ? -ng : ng;
    const float3 w = float3(1.0f - h.b.x - h.b.y, h.b.x, h.b.y);
    float3 lifted = v0 * w.x + v1 * w.y + v2 * w.z;
    float3 n;
    smooth_surface(h.tri, w, v0, v1, v2, ng, n, lifted);
    float size;
    const uint e = engine_cache_entry(lifted, n, false, size);
    if (e == ~0u) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const uint2 held = engine_buffer(kEvRadianceCache).Load2(e * kCacheEntryBytes + 24u);
    return f16tof32(held.y >> 16u) >= kCacheTrusted
               ? albedo * float3(f16tof32(held.x), f16tof32(held.x >> 16u), f16tof32(held.y))
               : float3(0.0f, 0.0f, 0.0f);
}

// The cache's frame: each entry's new samples onto its light (weighted by their count, at least 1 / kCacheFrames of
// the new: a light that changes is followed within kCacheFrames frames), its sums cleared; an entry unused for
// kCacheIdle frames is freed.
[numthreads(64, 1, 1)]
void EngineCacheCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= kCacheEntries || (g_engine_info.x & kEngineNoCache) != 0u) {
        return;
    }
    RWByteAddressBuffer cache = engine_buffer(kEvRadianceCache);
    const uint at = id.x * kCacheEntryBytes;
    const uint4 a = cache.Load4(at);
    if (a.x == 0u) {
        return;
    }
    const uint4 b = cache.Load4(at + 16u);
    if (g_frame - a.y > kCacheIdle) {
        cache.Store4(at, uint4(0u, 0u, 0u, 0u));
        cache.Store4(at + 16u, uint4(0u, 0u, 0u, 0u));
        return;
    }
    if (b.y == 0u) {
        return;
    }
    const float count = float(b.y);
    const float3 mean = float3(a.z, a.w, b.x) / (kCacheScale * count);
    const float weight = f16tof32(b.w >> 16u);
    const float3 light = lerp(float3(f16tof32(b.z), f16tof32(b.z >> 16u), f16tof32(b.w)), mean,
                              max(count / (weight + count), 1.0f / kCacheFrames));
    cache.Store4(at + 8u, uint4(0u, 0u, 0u, 0u));
    cache.Store2(at + 24u, uint2(pack_half2(light.r, light.g), pack_half2(light.b, min(weight + count, 60000.0f))));
}

// The radiance a hit sends back along -direction: its emission (when `emission`) plus its albedo times its light: the
// sun behind a shadow ray, the lights by resampled importance behind a shadow ray, the sky along one cosine ray (or the
// light of its cell in the world's radiance cache, engine_cache_light).
// Out: the hit's world-facing normal (for ReSTIR GI).
float3 engine_hit_radiance(Hit h, float3 origin, float3 direction, bool emission_wanted, float width, inout uint seed,
                           uint salt, out float3 x, out float3 hn)
{
    x = origin + direction * h.t;
    hn = -direction;
    if (!h.surface) {
        // Seen (a reflection): the backdrop as it shows; as light (the indirect light): its light's colour; through
        // the water above the ray's origin.
        return h.late ? float3(0.0f, 0.0f, 0.0f)
                      : (emission_wanted ? physical_sky(direction) : sky_light_colour(physical_sky(direction))) *
                            water_through(origin, direction);
    }
    float3 albedo;
    float3 emission;
    reference_material_cone(h.tri, h.b, width > 0.0f ? tev_cone(h.tri, direction, width) : kTevNoCone, albedo, emission);
    const uint draw = g_surfaces.Load(h.tri * 16u + 12u) >> 16u;
    const uint2 table = g_draws.Load2(min(draw, max(g_draw_count, 1u) - 1u) * kDrawStride + 8u);
    emission *= shown_emission(asfloat(table.y));   // seen: as the camera shows it
    const float3 v0 = asfloat(g_positions.Load3(h.tri * 36u));
    const float3 v1 = asfloat(g_positions.Load3(h.tri * 36u + 12u));
    const float3 v2 = asfloat(g_positions.Load3(h.tri * 36u + 24u));
    const float3 face = cross(v1 - v0, v2 - v0);
    if (dot(face, face) <= 1e-24f) {
        return emission_wanted ? emission : float3(0.0f, 0.0f, 0.0f);
    }
    float3 ng = normalize(face);
    ng = dot(ng, direction) > 0.0f ? -ng : ng;
    const float3 w = float3(1.0f - h.b.x - h.b.y, h.b.x, h.b.y);
    float3 lifted = v0 * w.x + v1 * w.y + v2 * w.z;
    float3 n;
    smooth_surface(h.tri, w, v0, v1, v2, ng, n, lifted);
    hn = n;
    const float3 start = lifted + ng * (0.2f + abs(x.z) * 4e-4f);
    // Every bounce after the first: a hit the picture showed when this slot's lighting last ran (its sample in
    // g_final, the same surface by depth) sends its whole diffuse light then (direct and indirect, a radiance cache)
    // in place of the direct light alone. With one bounce, shade is lit only by the sky that reaches it (enclosed walls
    // near black, shaded sand navy). Light sources keep their own emission; the series converges as each bounce takes
    // the albedo (below 1).
    if (!any(emission > 0.0f) && any(albedo > 0.0f) && (g_engine_info.x & kEngineBounceCache) != 0u &&
        x.z < -g_near) {
        const float2 q = eye_to_pixel(g_projection, g_viewport, x);
        if (inside(g_viewport, q) && all(q >= 0.0f) && all(q < float2(g_target_size))) {
            const uint2 sp = uint2(q) / g_factor;
            if (all(sp < g_sample_size)) {
                const uint4 f = g_final[sp.y * g_sample_size.x + sp.x];
                const float z = asfloat(f.z);
                if (f.z != 0u && abs(z - x.z) <= 0.02f * abs(x.z) + 2.0f) {
                    const float3 whole = float3(f16tof32(f.x), f16tof32(f.x >> 16u), f16tof32(f.y));
                    const float3 seen = max(whole, 0.0f) / max(physical_exposure(), 1e-4f);
                    // The picture's filtered light, the best sample its cell gets.
                    engine_cache_light(lifted, n, seen, width);
                    return albedo * seen;
                }
            }
        }
    }
    float3 m = float3(0.0f, 0.0f, 0.0f);
    if (any(albedo > 0.0f)) {
        if (any(g_sun_light.rgb > 0.0f)) {
            const float3 l = sun_direction_sample(float2(emit_random(seed), emit_random(seed)));
            const float nl = dot(n, l);
            float blocker;
            if (nl > 0.0f && sun_visible(start, l, false, salt, blocker)) {
                m += g_sun_light.rgb * nl * water_through(start, g_sun.xyz);
            }
        }
        if ((g_engine_info.x & kEngineHitNoLights) == 0u) {
            m += engine_light_sample(lifted, n, start, 4u, seed, salt + 13u);
        }
        const float3 d = cosine_direction(n, ng, float2(emit_random(seed), emit_random(seed)));
        if ((g_engine_info.x & kEngineHitNoSky) != 0u) {
            m += sky_light_colour(physical_sky_irradiance(n));
        } else {
            const Hit sky = engine_trace(start, d, salt + 7u);
            if (!sky.surface && !sky.late) {
                m += sky_light_colour(physical_sky(d)) * water_through(start, d);
            } else if (sky.surface) {
                m += engine_cache_bounce(sky, start, d);
            }
        }
        m = engine_cache_light(lifted, n, m, width);
    }
    return albedo * m + (emission_wanted ? emission : float3(0.0f, 0.0f, 0.0f));
}

// The camera in water (g_engine_water): the light a ray from `origin` that hits a surface brings through the water to
// it, with the water's own light gained on the way (water_medium). A ray to the sky keeps its light as water_through
// gave it: the water above's diffuse attenuation already holds what it scatters (its light still goes down).
float3 engine_water_ray(float3 radiance, Hit h, float3 origin, float3 direction)
{
    if (g_engine_water.x <= 0.0f || !h.surface) {
        return radiance;
    }
    return water_medium(radiance, h.t, water_medium_light(g_full_light.rgb, g_engine_water.y));
}

// ReSTIR DI reservoir of a light sample.
struct Reservoir {
    LightSample s;
    float w_sum;
    float m;
    float target;
    float W;
    float shade;   // its kept light's light at its pixel (luminance, W applied; 0 occluded): the light change's
};

Reservoir reservoir_empty()
{
    Reservoir r;
    r.s.ref = kNoLight;
    r.s.uv = 0u;
    r.s.y = float3(0.0f, 0.0f, 0.0f);
    r.s.face = float3(0.0f, 0.0f, 1.0f);
    r.s.radiance = float3(0.0f, 0.0f, 0.0f);
    r.s.beam = 0.0f;
    r.s.sides = 0.0f;
    r.s.area = 0.0f;
    r.w_sum = 0.0f;
    r.m = 0.0f;
    r.target = 0.0f;
    r.W = 0.0f;
    r.shade = 0.0f;
    return r;
}

bool reservoir_update(inout Reservoir r, LightSample s, float target, float w, float m, inout uint seed)
{
    r.w_sum += w;
    r.m += m;
    if (w > 0.0f && emit_random(seed) * r.w_sum < w) {
        r.s = s;
        r.target = target;
        return true;
    }
    return false;
}

void reservoir_finish(inout Reservoir r)
{
    r.W = r.target > 0.0f && r.m > 0.0f ? r.w_sum / (r.m * r.target) : 0.0f;
}

// A reservoir as kEvDiTemp and kEvDi keep it (16 bytes): its sample's light and the point on it (LightSample.ref,
// .uv), W, M and its shade (halves). Its sample is rebuilt from the light as the frame that reads it has it (engine_light_at): a light that
// moves carries its samples along, and one the next frame no longer draws takes them with it (engine_light_remap). A
// sample kept as a point and a radiance (as these were) went on lighting where a light had been: a meteor's flash
// lingered on the ground for a second after it was gone, at 5 % less a frame.
void reservoir_store(uint view, uint index, Reservoir r)
{
    RWByteAddressBuffer b = ResourceDescriptorHeap[view];
    b.Store4(index * 16u, uint4(r.s.ref, r.s.uv, asuint(r.W), pack_half2(min(r.m, 60000.0f), min(r.shade, 60000.0f))));
}

// A kept reservoir's light, point, W, M and shade (its sample's light: engine_light_at).
Reservoir reservoir_load(uint view, uint index)
{
    RWByteAddressBuffer b = ResourceDescriptorHeap[view];
    const uint4 v = b.Load4(index * 16u);
    Reservoir r = reservoir_empty();
    r.s.ref = v.x;
    r.s.uv = v.y;
    r.W = asfloat(v.z);
    r.m = f16tof32(v.w);
    r.shade = f16tof32(v.w >> 16u);
    return r;
}

float light_target(GSurface s, LightSample l, out float3 light)
{
    return engine_light_target(s.p, s.n, l, light);
}

// The sun's shadow (after NVIDIA's SIGMA; our own code). EngineDirectCS traces one ray per pixel to a point of the
// sun's disc and keeps the penumbra its blocker's distance d makes: 0.5 d tan(radius) in eye units (kShadowLit:
// nothing in the way; 0: the surface faces away). EngineShadowBlurCS and EngineShadowPostCS filter the visibility over
// that penumbra as the screen shows it, so a shadow cast from close by stays as sharp as daylight makes it and one cast
// from far is as soft; EngineShadowStabilizeCS keeps a short history (kShadowHistoryMost frames) clamped to the
// frame's own neighbourhood, so a moving caster leaves no trail. EngineFinalCS lights the surface by the result. In
// the diffuse light's history (32 frames, unclamped) the sun's shadows took half a second to form and to leave, and
// its a-trous blurred them to blobs.
static const float kShadowLit = 65504.0f;        // half's largest
static const float kShadowLeast = 1e-4f;         // a blocker in contact
static const float kShadowRadiusMost = 32.0f;    // the blur's radius at most (pixels at kFootprintLines)
static const uint kShadowTaps = 12u;             // the blur's sparse taps (with a dense 5 x 5)
static const float kShadowHistoryMost = 7.0f;    // frames
static const float kShadowHistoryWeight = 5.0f / 6.0f;  // the history's weight at most (5 frames' accumulation)
// The fast history of the diffuse light (EngineTemporalCS's anti-lag): frames.
static const float kFastHistory = 4.0f;

// A pixel's point in the unit square for frame `frame`: interleaved gradient noise (Jimenez 2014) places neighbouring
// pixels well apart, so the sun's rays in a penumbra average out over a few pixels; each pixel then steps along the R2
// sequence (Roberts 2018; Wolfe 2019, animating noise with low-discrepancy sequences), so over a few frames its own
// points cover the square evenly and the short stabilisation (kShadowHistoryMost) settles a still shadow's edge on its
// mean. A pattern moved at random per frame left the edges flickering at a quarter of their contrast.
float interleaved_gradient_noise(float2 q)
{
    return frac(52.9829189f * frac(dot(q, float2(0.06711056f, 0.00583715f))));
}

float2 pixel_sample(int2 q, uint frame)
{
    const float2 start = float2(interleaved_gradient_noise(float2(q)), interleaved_gradient_noise(float2(q.yx) + 31.0f));
    return frac(start + float(frame & 1023u) * float2(0.7548776662f, 0.5698402910f));
}

// Where a ray from surface `s` towards `l` starts (Hanika 2021, "Hacking the Shadow Terminator", gated as Blender
// Cycles' shadow_ray_offset with its default cutoff): a curved surface drawn in flat triangles, its shading normals
// smooth, has its faces below the surface those normals describe; a ray the face meets at a grazing angle, from the
// face, hits the next triangles of the same surface and shades the terminator in steps. Such a ray starts from the
// point the normals lift the face to (EngineGBufferCS's lift), one the face meets squarely from the face, so contact
// shadows stay; the bias clears the face itself. A drawn point under its traced face (a curved model's inward bend,
// primary_surface: a negative lift) starts every ray above the face.
static const float kTerminatorCutoff = 0.1f;

float3 surface_start(GSurface s, float3 l)
{
    const float nl = abs(dot(s.n, l));
    const float ngl = dot(s.ng, l);
    const float lifted = nl < kTerminatorCutoff ? saturate(2.0f - (ngl + nl) / kTerminatorCutoff)
                                                : saturate(1.0f - ngl / kTerminatorCutoff);
    return s.p + s.ng * ((s.lift < 0.0f ? -s.lift : s.lift * lifted) + 0.2f + abs(s.p.z) * 4e-4f);
}

// The world size of a pixel at eye depth `z`.
float pixel_world_size(float z)
{
    return abs(z) * 2.0f / max(abs(g_viewport.z * g_projection.x), 1e-6f);
}

// Where a pixel's surface was in the last frame, for every history (the denoiser's, the sun shadow's, the reservoirs'
// temporal reuse): its pixel position there (continuous, pixel centres at .5), and the eye depth and normal the last
// frame's G-buffer must show at it, how closely (draw indices are positions in each frame's own list, not identities).
// The camera's motion carries a still surface. A surface that moved by itself (a character's limbs, a bunny's ears, an
// enemy, a turning platform) is where the raster's motion puts it, where it has one (with a temporal upscaler every
// opaque draw writes its own): the camera alone sent it to another surface or none, so it kept no history and showed
// the frame's raw sample every frame (glitter on whatever moves) or took another surface's (light trailing it). Its
// own motion changes its depth and normal by an amount the motion does not give: they are checked more loosely.
struct History {
    float2 a;
    float z;
    float tolerance;
    float3 n;
    float facing;
};

bool engine_history(int2 q, float3 p, float3 n, out History h)
{
    const float3 previous = transform(g_reproject, p);
    h.a = eye_to_pixel(g_history_projection, g_viewport, previous);
    h.z = previous.z;
    h.tolerance = 0.02f * abs(previous.z) + 1.0f;
    h.n = normalize(rotate3(g_reproject, n));
    h.facing = 0.9f;
    if (previous.z >= -g_near) {
        return false;
    }
    if ((g_engine_info.x & kEngineMotion) != 0u) {
        const Texture2D<float2> motion = ResourceDescriptorHeap[engine_view(kEvMotion)];
        const float2 moved = float2(q) + 0.5f + motion.Load(int3(q, 0)) + g_motion_jitter;
        if (distance(moved, h.a) > 0.5f) {
            h.a = moved;
            h.tolerance = 0.1f * abs(previous.z) + 2.0f;
            h.facing = 0.7f;
        }
    }
    return true;
}

// A point of the previous samples' eye space in this frame's (g_reproject's inverse, the adjugate over the determinant:
// exact whatever the 3 x 3 holds).
float3 engine_unreproject(float3 p)
{
    const float3 r0 = g_reproject[0].xyz;
    const float3 r1 = g_reproject[1].xyz;
    const float3 r2 = g_reproject[2].xyz;
    const float3 d = p - float3(g_reproject[0].w, g_reproject[1].w, g_reproject[2].w);
    return (cross(r1, r2) * d.x + cross(r2, r0) * d.y + cross(r0, r1) * d.z) / dot(r0, cross(r1, r2));
}

// A normal of the previous samples' eye space in this frame's (normals take the inverse's inverse transpose).
float3 engine_unreproject_normal(float3 n)
{
    return normalize(g_reproject[0].xyz * n.x + g_reproject[1].xyz * n.y + g_reproject[2].xyz * n.z);
}

// The previous frame's pixel of pixel `q`'s surface (eye-space point `p`, normal `n`), and whether the surface there is
// this one.
bool engine_reproject(int2 q, float3 p, float3 n, out uint prev_pixel)
{
    prev_pixel = 0u;
    History h;
    if ((g_flags & 1u) == 0u || !engine_history(q, p, n, h)) {
        return false;
    }
    const int2 s = int2(floor(h.a));
    if (any(s < 0) || any(s >= int2(g_target_size))) {
        return false;
    }
    prev_pixel = uint(s.y) * g_target_size.x + uint(s.x);
    RWByteAddressBuffer gb = engine_buffer(kEvGBufferPrev);
    const uint4 w0 = gb.Load4(prev_pixel * 32u);
    const uint4 w1 = gb.Load4(prev_pixel * 32u + 16u);
    if (w1.x == 0u) {
        return false;
    }
    return abs(asfloat(w1.x) - h.z) < h.tolerance && dot(unpack_normal(w0.x), h.n) > h.facing;
}

// The boiling filter (RTXDI's, ShaderAPI.md RTXDI_BoilingFilter; our implementation of its published idea). A light
// that matters to a wide stretch of surface but that its candidates rarely find is held by the one reservoir that found
// it, and temporal and spatial reuse spread it: a bubble of light that appears, grows and fades, light boiling over
// the surfaces (the Gateway's Grand Star room under its sparks, coloured blotches at every frame). Such a reservoir's
// weight sum stands far above its neighbours': after temporal reuse a reservoir past kBoilingMultiple times the mean
// nonzero weight sum of its 8 x 8 thread group is emptied, trading the boiling for a little bias. Strength 0.2, RTXDI's
// default for both its direct and indirect light: 10 / 0.2 - 9 = 41 times.
static const uint kBoilingGroup = 64u;
static const float kBoilingMultiple = 41.0f;
groupshared float s_boiling[kBoilingGroup];

// Whether a reservoir of weight sum `weight` (0: none) boils in its group. Every thread of the group calls it once (a
// barrier), its group index `index`.
bool boiling(uint index, float weight)
{
    s_boiling[index] = weight;
    GroupMemoryBarrierWithGroupSync();
    float sum = 0.0f;
    float count = 0.0f;
    [loop] for (uint i = 0u; i < kBoilingGroup; ++i) {
        const float w = s_boiling[i];
        sum += w;
        count += w > 0.0f ? 1.0f : 0.0f;
    }
    return count > 0.0f && weight > kBoilingMultiple * sum / count && (g_engine_info.x & kEngineNoBoiling) == 0u;
}

// The direct light's initial and temporal reservoir of a pixel in range; `change`: its light change, `surface`: a lit
// surface (else the pixel's records are cleared).
Reservoir direct_reservoir(uint2 id, uint pixel, out float change, out bool surface)
{
    const int2 q = int2(id);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    RWByteAddressBuffer direct = engine_buffer(kEvDirect);
    Reservoir r = reservoir_empty();
    change = 0.0f;
    surface = s.valid && (s.flags & kGbEmitter) == 0u;
    if (!surface) {
        direct.Store4(pixel * 16u, uint4(0u, 0u, 0u, 0u));
        return r;
    }
    uint seed = emit_hash(pixel ^ (g_frame * 0x9E3779B9u));
    const uint salt = pixel * 32u + g_frame * 0x632BE5ABu;
    // The sun: one direction in its disc behind a shadow ray, kept as the penumbra its blocker makes (the light itself
    // is EngineFinalCS's, by the filtered visibility).
    float penumbra = 0.0f;
    float depth = 0.0f;
    if (any(g_sun_light.rgb > 0.0f) && dot(s.n, g_sun.xyz) > 0.0f && dot(s.ng, g_sun.xyz) > -0.05f) {
        // Under water the sunlight crosses `depth` of it: attenuated, its disc widened by forward scattering.
        const float3 start = surface_start(s, g_sun.xyz);
        depth = water_depth_along(start, g_sun.xyz);
        const float spread = depth > 0.0f ? min(g_sun.w + depth * kWaterSpread, kWaterSpreadMost) : g_sun.w;
        const float2 u = pixel_sample(q, g_frame);
        const float3 l = sun_direction_spread(u, spread);
        float blocker;
        penumbra = sun_visible(surface_start(s, l), l, (s.flags & kGbCard) != 0u, salt, blocker)
                       ? kShadowLit
                       : clamp(0.5f * blocker * spread, kShadowLeast, 0.5f * kShadowLit);
        // GALAXY_RT_PROBE (diagnostics): what stops the sun's centre ray at the probed pixel, in g_final after the
        // light meter's records (SceneRayTracer prints [rt-probe-sun]).
        if (g_probe.z != 0u && all(id == g_probe.xy * g_factor)) {
            float t;
            uint2 who;
            sun_visible_by(start, g_sun.xyz, (s.flags & kGbCard) != 0u, salt, t, who);
            const uint draw = who.x != 0u && who.y == 0u ? g_surfaces.Load((who.x - 1u) * 16u + 12u) >> 16u : 0xFFFFu;
            g_final[g_sample_size.x * g_sample_size.y + kStarSlots + 7u] =
                uint4(who.x, draw | (who.y << 16u), asuint(t), asuint(depth));
        }
    }
    direct.Store4(pixel * 16u, uint4(0u, 0u, 0u, pack_half2(penumbra, depth)));
    // The lights (emitting triangles, the game's point lights, the star core): candidates from the pixel's light grid
    // cell (engine_light_pick_near).
    const float tris = g_tev_info.z != 0u ? max(emit_element(0u).y, 0.0f) : 0.0f;
    const float spheres = spheres_total();
    // Eight candidates (sixteen at High): with four, a scene of many lamps (the Observatory's star core and its dozens
    // of lamps) leaves the direct light speckled, and the denoiser spreads the speckle into blotches.
    const uint candidates = (g_flags & 1024u) != 0u ? 16u : 8u;
    if (tris + spheres > 0.0f) {
        [loop] for (uint k = 0u; k < candidates; ++k) {
            LightSample l;
            float chance;
            if (!engine_light_pick_near(s.p, tris, spheres, seed, l, chance)) {
                r.m += 1.0f;
                continue;
            }
            float3 light;
            const float target = light_target(s, l, light);
            reservoir_update(r, l, target, target / chance, 1.0f, seed);
        }
        reservoir_finish(r);
        // Temporal reuse: the last frame's final reservoir where this surface was, its light as this frame has it, and
        // this frame's, combined by the generalized balance heuristic (resampled importance sampling with multiple
        // importance weights over both pixels' targets: Lin et al. 2022, RTXDI's unbiased mode; our own code). Each
        // sample's weight is its own pixel's share of the targets for it, count-weighted: the last frame's target is
        // taken at its surface then (its G-buffer, carried into this frame's eye space), with this frame's light.
        // Combined by 1/M (RTXDI's biased mode) and with an occluded sample's reservoir emptied but counted, the direct
        // light read 36 % darker than the converged reference in the Observatory's still night (the pixels' candidates
        // come from different light grid cells, each frame's slots drawn anew; the reuse fed back each frame).
        uint prev_pixel;
        if ((g_engine_info.x & kEngineNoLightHistory) == 0u && engine_reproject(q, s.p, s.n, prev_pixel)) {
            Reservoir previous = reservoir_load(engine_view(kEvDiPrev), prev_pixel);
            const uint now = engine_light_remap(previous.s.ref);
            const bool lit = now != kNoLight && engine_light_at(now, previous.s.uv, previous.s);
            float3 light;
            const float target = lit && previous.W > 0.0f ? light_target(s, previous.s, light) : 0.0f;
            const int2 was_q = int2(prev_pixel % g_target_size.x, prev_pixel / g_target_size.x);
            const GSurface was = gb_load(engine_view(kEvGBufferPrev), prev_pixel, g_history_projection, was_q);
            const float3 was_p = engine_unreproject(was.p);
            const float3 was_n = engine_unreproject_normal(was.n);
            // The light change (A-SVGF's temporal gradient, Schied, Peters and Dachsbacher 2018, on RTXDI's
            // reservoirs): the last frame's kept sample shaded as this frame has its light, against its shade then;
            // the same light point and weight at the same surface point (that pixel's G-buffer then, carried into
            // this frame's eye space, as A-SVGF shades its gradient samples at the last frame's visibility points), so
            // the difference is the light's alone (gone: 0). Shaded at this pixel's own point instead, the jitter's
            // sub-pixel step and the camera's move read as change every frame: on the Gateway's still ground about
            // 1 % a frame, which held the histories near 10 frames under DLSS (30 without its jitter).
            float3 unused;
            const float was_target = lit && previous.W > 0.0f ? engine_light_target(was_p, was_n, previous.s, unused)
                                                              : 0.0f;
            if (previous.shade > 0.0f) {
                const float shade_now = was_target * previous.W;
                change = abs(shade_now - previous.shade) / max(max(shade_now, previous.shade), 1e-6f);
            }
            if (lit) {
                const float m_c = r.m;
                const float m_p = min(previous.m, 20.0f * float(candidates));
                // This frame's sample: its share of the targets for it; the last frame's: its share.
                const float c_here = r.W > 0.0f ? r.target : 0.0f;
                const float c_was = r.W > 0.0f ? engine_light_target(was_p, was_n, r.s, unused) : 0.0f;
                const float c_mis = c_here > 0.0f ? m_c * c_here / (m_c * c_here + m_p * c_was) : 0.0f;
                const float p_mis = was_target > 0.0f ? m_p * was_target / (m_c * target + m_p * was_target) : 0.0f;
                Reservoir combined = reservoir_empty();
                reservoir_update(combined, r.s, c_here, c_mis * c_here * r.W, m_c, seed);
                reservoir_update(combined, previous.s, target, p_mis * target * previous.W, m_p, seed);
                combined.W = combined.target > 0.0f ? combined.w_sum / combined.target : 0.0f;
                r = combined;
            }
        }
        return r;
    }
    reservoir_finish(r);
    return r;
}

[numthreads(8, 8, 1)]
void EngineDirectCS(uint3 id : SV_DispatchThreadID, uint index : SV_GroupIndex)
{
    const bool in_range = all(id.xy < g_target_size);
    const uint pixel = id.y * g_target_size.x + id.x;
    float change = 0.0f;
    bool surface = false;
    Reservoir r = reservoir_empty();
    if (in_range) {
        r = direct_reservoir(id.xy, pixel, change, surface);
    }
    if (boiling(index, surface ? r.w_sum : 0.0f)) {
        r = reservoir_empty();
    }
    if (!in_range) {
        return;
    }
    reservoir_store(engine_view(kEvDiTemp), pixel, r);
    if (surface) {
        engine_buffer(kEvDirect).Store(pixel * 16u + 8u, pack_half2(change, 0.0f));
    }
}

[numthreads(8, 8, 1)]
void EngineDirectSpatialCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id.xy);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    Reservoir r = reservoir_empty();
    if (!s.valid || (s.flags & kGbEmitter) != 0u) {
        reservoir_store(engine_view(kEvDi), pixel, r);
        return;
    }
    uint seed = emit_hash(pixel ^ (g_frame * 0x2545F491u) ^ 0xA511E9B3u);
    const uint salt = pixel * 32u + g_frame * 0x632BE5ABu + 11u;
    // This pixel's reservoir and up to four neighbours' within 30 pixels (at kFootprintLines) on the same surface
    // (normal, depth), combined by the generalized balance heuristic (as the temporal reuse): each sample weighed by its
    // own pixel's count-weighted share of all of their targets for it.
    static const uint kSpatialMost = 5u;
    LightSample samples[kSpatialMost];
    float3 at_p[kSpatialMost];
    float3 at_n[kSpatialMost];
    float weight_w[kSpatialMost];
    float count[kSpatialMost];
    uint taken = 0u;
    {
        Reservoir own = reservoir_load(engine_view(kEvDiTemp), pixel);
        samples[0] = own.s;
        const bool lit = own.W > 0.0f && engine_light_at(own.s.ref, own.s.uv, samples[0]);
        at_p[0] = s.p;
        at_n[0] = s.n;
        weight_w[0] = lit ? own.W : 0.0f;
        count[0] = own.m;
        taken = 1u;
    }
    const uint neighbours = (g_engine_info.x & kEngineNoDiSpatial) != 0u ? 0u : 4u;
    [loop] for (uint k = 0u; k < neighbours; ++k) {
        const float angle = emit_random(seed) * 2.0f * kPi;
        const float radius = (3.0f + emit_random(seed) * 27.0f) * footprint_scale();
        const int2 o = q + int2(round(float2(cos(angle), sin(angle)) * radius));
        if (any(o < 0) || any(o >= int2(g_target_size))) {
            continue;
        }
        const uint neighbour = uint(o.y) * g_target_size.x + uint(o.x);
        const GSurface ns = gb_load(engine_view(kEvGBuffer), neighbour, g_projection, o);
        if (!ns.valid || (ns.flags & kGbEmitter) != 0u || dot(ns.n, s.n) < 0.9f ||
            abs(ns.p.z - s.p.z) > 0.1f * abs(s.p.z)) {
            continue;
        }
        Reservoir other = reservoir_load(engine_view(kEvDiTemp), neighbour);
        samples[taken] = other.s;
        const bool lit = other.W > 0.0f && engine_light_at(other.s.ref, other.s.uv, samples[taken]);
        at_p[taken] = ns.p;
        at_n[taken] = ns.n;
        weight_w[taken] = lit ? other.W : 0.0f;
        count[taken] = other.m;
        ++taken;
    }
    [loop] for (uint i = 0u; i < taken; ++i) {
        float target = 0.0f;
        float w = 0.0f;
        if (weight_w[i] > 0.0f) {
            float3 unused;
            target = engine_light_target(s.p, s.n, samples[i], unused);
            float own_share = 0.0f;
            float all_shares = 0.0f;
            [loop] for (uint j = 0u; j < taken; ++j) {
                const float share = count[j] * (j == 0u ? target : engine_light_target(at_p[j], at_n[j], samples[i], unused));
                all_shares += share;
                own_share = j == i ? share : own_share;
            }
            w = all_shares > 0.0f ? own_share / all_shares * target * weight_w[i] : 0.0f;
        }
        reservoir_update(r, samples[i], target, w, count[i], seed);
    }
    r.W = r.target > 0.0f ? r.w_sum / r.target : 0.0f;
    // The kept light behind its shadow ray; an occluded sample stays in the reservoir (its light here is none this
    // frame; emptied but counted, it darkened what the reuse brought next).
    float3 diffuse = float3(0.0f, 0.0f, 0.0f);
    if (r.W > 0.0f) {
        float3 light;
        light_target(s, r.s, light);
        const float3 start = surface_start(s, normalize(r.s.y - s.p));
        if (engine_visible(start, r.s.y, 0.5f + 0.001f * length(r.s.y - start), salt)) {
            diffuse = light * r.W;
        }
    }
    r.shade = luminance(safe_light(diffuse));
    // A light the last frame did not have (a flash, a new fire): its share of the light here is the share of the
    // neighbourhood's kept samples that are new (each is drawn in proportion to its light).
    const float fresh = r.shade > 0.0f && !engine_light_existed(r.s.ref) ? 1.0f : 0.0f;
    reservoir_store(engine_view(kEvDi), pixel, r);
    RWByteAddressBuffer direct = engine_buffer(kEvDirect);
    const uint4 d = direct.Load4(pixel * 16u);
    const float3 sum = float3(f16tof32(d.x), f16tof32(d.x >> 16u), f16tof32(d.y)) + safe_light(diffuse);
    direct.Store4(pixel * 16u, uint4(pack_half2(sum.r, sum.g), (d.y & 0xFFFF0000u) | f32tof16(sum.b),
                                     (d.z & 0xFFFFu) | (f32tof16(fresh) << 16u), d.w));
}

// The light change about pixel `q` (0-1): over its 5 x 5 neighbourhood the mean of each pixel's kept sample's relative
// change and whether its light is new (EngineDirectCS, EngineDirectSpatialCS). A-SVGF filters its gradients before it
// takes them as the history's weight (one sample a pixel is noise); the mean of the new lights' flags is their share
// of the light. The histories weigh this frame (1 - change) / length + change: a light that appears, goes or changes
// shows at once instead of over the history's half second.
float light_change(int2 q)
{
    RWByteAddressBuffer direct = engine_buffer(kEvDirect);
    const int spread = int(round(footprint_scale()));
    float sum = 0.0f;
    float n = 0.0f;
    [unroll] for (int j = -2; j <= 2; ++j) {
        [unroll] for (int i = -2; i <= 2; ++i) {
            const int2 o = q + int2(i, j) * spread;
            if (any(o < 0) || any(o >= int2(g_target_size))) {
                continue;
            }
            const uint z = direct.Load((uint(o.y) * g_target_size.x + uint(o.x)) * 16u + 8u);
            sum += max(f16tof32(z), f16tof32(z >> 16u));
            n += 1.0f;
        }
    }
    return n > 0.0f ? saturate(sum / n) : 0.0f;
}

// A pixel's sun shadow as a stage of its filter reads it: its visibility and penumbra (eye units; kShadowLit, 0: see
// kShadowLit). Stage 0: the ray's (EngineDirectCS: 1 lit, else 0); 1: the first blur's (kEvAtrousA); 2: the second's
// (kEvAtrousB).
float shadow_load(uint stage, uint pixel, out float penumbra)
{
    if (stage == 0u) {
        penumbra = f16tof32(engine_buffer(kEvDirect).Load(pixel * 16u + 12u));
        return penumbra >= kShadowLit ? 1.0f : 0.0f;
    }
    const uint w = engine_buffer(stage == 1u ? kEvAtrousA : kEvAtrousB).Load(pixel * 16u);
    penumbra = f16tof32(w >> 16u);
    return f16tof32(w);
}

// A tap's weight on the pixel's plane (its face normal): 1 on it, 0 from 2 % of its depth off it.
float shadow_plane_weight(GSurface s, float3 x)
{
    return saturate(1.0f - abs(dot(s.ng, x - s.p)) / (abs(s.p.z) * 0.02f + 1.0f));
}

// One blur of the sun's visibility (stage 0: the rays' into kEvAtrousA; 1: the first blur's into kEvAtrousB). A dense
// 5 x 5 estimates the penumbra about the pixel (its occluded neighbours', the smaller preferred) and filters within the
// pixel's own; kShadowTaps taps on a disc of the estimate's radius on screen (at most kShadowRadiusMost pixels, narrowed
// where the surface is seen at a slant, turned per frame) complete it. A tap counts on the pixel's plane and facing the
// sun, and the less the smaller its penumbra against the estimate (an umbra's rays do not darken a wide penumbra next
// to it). Where no ray about the pixel is blocked it stays lit; where every one is and the penumbra is within the 5 x 5,
// in shadow.
void shadow_blur(uint2 id, uint stage)
{
    if (any(id >= g_target_size)) {
        return;
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id);
    RWByteAddressBuffer dst = engine_buffer(stage == 0u ? kEvAtrousA : kEvAtrousB);
    float centre_penumbra;
    const float centre = shadow_load(stage, pixel, centre_penumbra);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    if (!s.valid || (s.flags & kGbEmitter) != 0u || centre_penumbra == 0.0f) {
        dst.Store(pixel * 16u, pack_half2(centre, centre_penumbra));
        return;
    }
    RWByteAddressBuffer gb = engine_buffer(kEvGBuffer);
    const float size = pixel_world_size(s.p.z);
    // Pixels to the centre's penumbra radii (none lit: its whole 5 x 5).
    const float to_centre = centre_penumbra >= kShadowLit ? 0.0f : size / centre_penumbra;
    float result = 0.0f;
    float weight = 0.0f;
    float penumbra = 0.0f;
    float penumbra_weight = 0.0f;
    bool lit = false;
    bool blocked = false;
    [unroll] for (int j = -2; j <= 2; ++j) {
        [unroll] for (int i = -2; i <= 2; ++i) {
            const int2 o = clamp(q + int2(i, j), int2(0, 0), int2(g_target_size) - 1);
            const uint op = uint(o.y) * g_target_size.x + uint(o.x);
            float p;
            const float v = shadow_load(stage, op, p);
            float w = 1.0f;
            if (i != 0 || j != 0) {
                const float z = asfloat(gb.Load(op * 32u + 16u));
                const float r = length(float2(i, j)) * to_centre;
                w = z != 0.0f && p != 0.0f
                        ? exp(-r * r) * shadow_plane_weight(s, eye_position(g_projection, g_viewport, float2(o) + 0.5f, z))
                        : 0.0f;
            }
            result += v * w;
            weight += w;
            if (w > 0.0f) {
                lit = lit || v > 0.0f;
                blocked = blocked || p < kShadowLit;
            }
            const float wp = p < kShadowLit ? w * size / (size + p) : 0.0f;
            penumbra += p * wp;
            penumbra_weight += wp;
        }
    }
    result /= weight;
    if (!blocked || penumbra_weight <= 0.0f) {
        dst.Store(pixel * 16u, pack_half2(result, kShadowLit));
        return;
    }
    penumbra /= penumbra_weight;
    const float radius = min(penumbra / size, kShadowRadiusMost * footprint_scale());
    if (!lit && radius <= 2.0f) {
        dst.Store(pixel * 16u, pack_half2(result, penumbra));
        return;
    }
    // The dense estimate counts as one tap at full weight, as its penumbra.
    weight = 1.0f;
    penumbra_weight = 1.0f;
    const float slant = abs(dot(s.ng, normalize(-s.p)));
    float2 skew = lerp(1.0f - abs(s.ng.xy), float2(1.0f, 1.0f), slant);
    skew *= radius / max(max(skew.x, skew.y), 1e-3f);
    const float turn = float(g_frame & 255u) * 2.3999632f + float(stage) * 1.3f;
    const float estimate = penumbra;
    [unroll] for (uint n = 0u; n < kShadowTaps; ++n) {
        const float r = sqrt((float(n) + 0.5f) / float(kShadowTaps));
        const float a = float(n) * 2.3999632f + turn;
        const int2 o = q + int2(round(float2(cos(a), sin(a)) * r * skew));
        if (any(o < 0) || any(o >= int2(g_target_size))) {
            continue;
        }
        const uint op = uint(o.y) * g_target_size.x + uint(o.x);
        float p;
        const float v = shadow_load(stage, op, p);
        const float z = asfloat(gb.Load(op * 32u + 16u));
        if (z == 0.0f || p == 0.0f) {
            continue;
        }
        const float w = exp(-r * r) * saturate(p / estimate) *
                        shadow_plane_weight(s, eye_position(g_projection, g_viewport, float2(o) + 0.5f, z));
        result += v * w;
        weight += w;
        const float wp = p < kShadowLit ? w * size / (size + p) : 0.0f;
        penumbra += p * wp;
        penumbra_weight += wp;
    }
    dst.Store(pixel * 16u, pack_half2(result / weight, penumbra / penumbra_weight));
}

[numthreads(8, 8, 1)]
void EngineShadowBlurCS(uint3 id : SV_DispatchThreadID)
{
    shadow_blur(id.xy, 0u);
}

[numthreads(8, 8, 1)]
void EngineShadowPostCS(uint3 id : SV_DispatchThreadID)
{
    shadow_blur(id.xy, 1u);
}

// The blurred visibility over its history (kEvShadowPrev at the surface's last place: bilinear over the previous pixels
// on this surface), clamped about the 5 x 5 neighbourhood's mean by its spread (wider, up to 3 x, as the history grows)
// and shortened by how far the clamp moved it: a moving shadow is followed within a frame, the residual noise of the
// blurs averaged over up to kShadowHistoryMost frames.
[numthreads(8, 8, 1)]
void EngineShadowStabilizeCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id.xy);
    RWByteAddressBuffer out_shadow = engine_buffer(kEvShadow);
    float centre_penumbra;
    const float input = shadow_load(2u, pixel, centre_penumbra);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    if (!s.valid || (s.flags & kGbEmitter) != 0u || centre_penumbra == 0.0f) {
        out_shadow.Store(pixel * 4u, pack_half2(0.0f, 0.0f));
        return;
    }
    float m1 = 0.0f;
    float m2 = 0.0f;
    float sum = 0.0f;
    [unroll] for (int j = -2; j <= 2; ++j) {
        [unroll] for (int i = -2; i <= 2; ++i) {
            const int2 o = clamp(q + int2(i, j), int2(0, 0), int2(g_target_size) - 1);
            float p;
            const float v = shadow_load(2u, uint(o.y) * g_target_size.x + uint(o.x), p);
            const float w = (i == 0 && j == 0) ? 1.0f : (p != 0.0f ? exp(-0.25f * float(i * i + j * j)) : 0.0f);
            m1 += v * w;
            m2 += v * v * w;
            sum += w;
        }
    }
    m1 /= sum;
    m2 /= sum;
    float history = 0.0f;
    float frames = 0.0f;
    History hp;
    if ((g_flags & 1u) != 0u && engine_history(q, s.p, s.n, hp)) {
        {
            const float2 a = hp.a - 0.5f;
            const int2 b = int2(floor(a));
            const float2 f = a - float2(b);
            RWByteAddressBuffer gb = engine_buffer(kEvGBufferPrev);
            RWByteAddressBuffer prev = engine_buffer(kEvShadowPrev);
            float2 acc = float2(0.0f, 0.0f);
            float weight = 0.0f;
            [unroll] for (uint k = 0u; k < 4u; ++k) {
                const int2 o = b + int2(k & 1u, k >> 1u);
                if (any(o < 0) || any(o >= int2(g_target_size))) {
                    continue;
                }
                const uint op = uint(o.y) * g_target_size.x + uint(o.x);
                const uint4 w0 = gb.Load4(op * 32u);
                const uint w1 = gb.Load(op * 32u + 16u);
                if (w1 == 0u || abs(asfloat(w1) - hp.z) > hp.tolerance || dot(unpack_normal(w0.x), hp.n) < hp.facing) {
                    continue;
                }
                const float w = ((k & 1u) != 0u ? f.x : 1.0f - f.x) * ((k & 2u) != 0u ? f.y : 1.0f - f.y);
                const uint h = prev.Load(op * 4u);
                acc += float2(f16tof32(h), f16tof32(h >> 16u)) * w;
                weight += w;
            }
            if (weight > 1e-3f) {
                history = acc.x / weight;
                frames = acc.y / weight;
            }
        }
    }
    const float spread = sqrt(max(m2 - m1 * m1, 0.0f)) * lerp(3.0f, 1.0f, 1.0f / (1.0f + frames));
    const float clamped = clamp(history, m1 - spread, m1 + spread);
    frames *= 1.0f - sqrt(saturate(abs(clamped - history)));
    const float result = lerp(input, clamped, min(frames / (1.0f + frames), kShadowHistoryWeight));
    out_shadow.Store(pixel * 4u, pack_half2(saturate(result), min(frames + 1.0f, kShadowHistoryMost)));
}

// The sun's light on `s` at visibility `visible` (diffuse as m, `specular` as radiance): facing its disc's centre, through
// the water above it (EngineDirectCS's depth along its light).
float3 sun_light(GSurface s, uint pixel, float visible, out float3 specular)
{
    specular = float3(0.0f, 0.0f, 0.0f);
    const float nl = dot(s.n, g_sun.xyz);
    if (visible <= 0.0f || !any(g_sun_light.rgb > 0.0f) || nl <= 0.0f || dot(s.ng, g_sun.xyz) <= -0.05f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float depth = f16tof32(engine_buffer(kEvDirect).Load(pixel * 16u + 12u) >> 16u);
    const float3 through = (depth > 0.0f ? water_tint(depth) : float3(1.0f, 1.0f, 1.0f)) * visible;
    specular = sun_specular(s, normalize(-s.p)) * through;
    return g_sun_light.rgb * nl * through;
}

// ReSTIR GI reservoir of an indirect sample (half resolution).
struct GiReservoir {
    float3 x;          // the sample's hit (eye)
    float3 hn;         // its normal (eye)
    float3 radiance;   // what it sends towards the surface
    float w_sum;
    float m;
    float target;
    float W;
};

GiReservoir gi_empty()
{
    GiReservoir r;
    r.x = float3(0.0f, 0.0f, 0.0f);
    r.hn = float3(0.0f, 0.0f, 1.0f);
    r.radiance = float3(0.0f, 0.0f, 0.0f);
    r.w_sum = 0.0f;
    r.m = 0.0f;
    r.target = 0.0f;
    r.W = 0.0f;
    return r;
}

void gi_store(uint view, uint index, GiReservoir r)
{
    RWByteAddressBuffer b = ResourceDescriptorHeap[view];
    b.Store4(index * 32u, uint4(asuint(eye_point_to_world(r.x)), asuint(r.W)));
    b.Store4(index * 32u + 16u, uint4(pack_normal(normalize(eye_direction_to_world(r.hn))),
                                      pack_half2(r.radiance.r, r.radiance.g),
                                      f32tof16(r.radiance.b) | (f32tof16(min(r.m, 60000.0f)) << 16u), 0u));
}

GiReservoir gi_load(uint view, uint index)
{
    RWByteAddressBuffer b = ResourceDescriptorHeap[view];
    const uint4 r0 = b.Load4(index * 32u);
    const uint4 r1 = b.Load4(index * 32u + 16u);
    GiReservoir r = gi_empty();
    r.x = transform(g_to_eye, asfloat(r0.xyz));
    r.hn = normalize(rotate3(g_to_eye, unpack_normal(r1.x)));
    r.radiance = float3(f16tof32(r1.y), f16tof32(r1.y >> 16u), f16tof32(r1.z));
    r.W = asfloat(r0.w);
    r.m = f16tof32(r1.z >> 16u);
    return r;
}

bool gi_update(inout GiReservoir r, float3 x, float3 hn, float3 radiance, float target, float w, float m, inout uint seed)
{
    r.w_sum += w;
    r.m += m;
    if (w > 0.0f && emit_random(seed) * r.w_sum < w) {
        r.x = x;
        r.hn = hn;
        r.radiance = radiance;
        r.target = target;
        return true;
    }
    return false;
}

float gi_target(GSurface s, float3 x, float3 radiance)
{
    const float3 d = x - s.p;
    const float len = length(d);
    return len > 1e-3f ? luminance(radiance) * max(dot(s.n, d / len), 0.0f) : 0.0f;
}

// The full-resolution pixel a half-resolution sample traces this frame (one of its 2 x 2, rotating).
// The indirect light's samples per pixel side: 1 at full resolution, 2 at half (one pixel of each 2 x 2, rotating).
uint gi_step()
{
    return (g_engine_info.x & kEngineGiFull) != 0u ? 1u : 2u;
}

int2 gi_pixel(uint2 h)
{
    if (gi_step() == 1u) {
        return min(int2(h), int2(g_target_size) - 1);
    }
    const uint k = (g_frame + h.x * 3u + h.y * 5u) & 3u;
    return min(int2(h * 2u + uint2(k & 1u, k >> 1u)), int2(g_target_size) - 1);
}

// The indirect light's initial and temporal reservoir of a half-resolution sample in range (`surface`: one to light).
GiReservoir indirect_reservoir(uint2 id, uint index, out bool surface)
{
    const uint2 half_size = g_engine_size.zw;
    const int2 q = gi_pixel(id);
    const uint pixel = uint(q.y) * g_target_size.x + uint(q.x);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    GiReservoir r = gi_empty();
    surface = s.valid && (s.flags & kGbEmitter) == 0u && (g_engine_info.x & kEngineIndirect) != 0u;
    if (!surface) {
        return r;
    }
    uint seed = emit_hash(index ^ (g_frame * 0x68E31DA4u) ^ 0x1B56C4E9u);
    const uint salt = index * 32u + g_frame * 0x632BE5ABu + 19u;
    const float3 d = cosine_direction(s.n, s.ng, float2(emit_random(seed), emit_random(seed)));
    const float3 start = surface_start(s, d);
    const Hit h = engine_trace(start, d, salt);
    float3 x;
    float3 hn;
    // The emitters' own light reaches this surface by the direct light: not again here.
    const float3 radiance =
        safe_light(engine_water_ray(engine_hit_radiance(h, start, d, false, 0.0f, seed, salt + 3u, x, hn), h, start, d));
    if (!h.surface) {
        // The sky: a point far along the ray stands for its direction.
        x = start + d * 1.0e5f;
        hn = -d;
    }
    // Source pdf cos / pi: weight target x pi / cos (the cosine cancels in the target's own).
    const float cos_theta = max(dot(s.n, d), 1e-4f);
    const float target = luminance(radiance) * cos_theta;
    gi_update(r, x, hn, radiance, target, target * kPi / cos_theta, 1.0f, seed);
    uint prev_pixel;
    if (engine_reproject(q, s.p, s.n, prev_pixel)) {
        // The last frame's temporal reservoir of the half-resolution sample covering that pixel (not its spatially
        // reused one: reusing that compounds the gain into blobs).
        const uint2 hp = uint2(prev_pixel % g_target_size.x, prev_pixel / g_target_size.x) / gi_step();
        GiReservoir previous = gi_load(engine_view(kEvGiPrev), min(hp.y, half_size.y - 1u) * half_size.x +
                                                                    min(hp.x, half_size.x - 1u));
        // Its radiance is the last frame's: where the light changed it counts as little as the light change leaves it
        // (Remix's validateLightingChange drops such samples outright).
        previous.m = min(previous.m, 30.0f) * (1.0f - light_change(q));
        if (previous.W > 0.0f) {
            const float t = gi_target(s, previous.x, previous.radiance);
            gi_update(r, previous.x, previous.hn, previous.radiance, t, t * previous.W * previous.m, previous.m, seed);
        } else {
            r.m += previous.m;
        }
    }
    r.W = r.target > 0.0f && r.m > 0.0f ? r.w_sum / (r.m * r.target) : 0.0f;
    return r;
}

[numthreads(8, 8, 1)]
void EngineIndirectCS(uint3 id : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
    const uint2 half_size = g_engine_size.zw;
    const bool in_range = all(id.xy < half_size);
    const uint index = id.y * half_size.x + id.x;
    bool surface = false;
    GiReservoir r = gi_empty();
    if (in_range) {
        r = indirect_reservoir(id.xy, index, surface);
    }
    // The boiling filter, as the direct light's (RTXDI's for ReSTIR GI at the same strength).
    if (boiling(group_index, surface ? r.w_sum : 0.0f)) {
        r = gi_empty();
    }
    if (!in_range) {
        return;
    }
    gi_store(engine_view(kEvGiTemp), index, r);
    gi_store(engine_view(kEvGi), index, r);
}

[numthreads(8, 8, 1)]
void EngineIndirectSpatialCS(uint3 id : SV_DispatchThreadID)
{
    const uint2 half_size = g_engine_size.zw;
    if (any(id.xy >= half_size)) {
        return;
    }
    const uint index = id.y * half_size.x + id.x;
    const int2 q = gi_pixel(id.xy);
    const uint pixel = uint(q.y) * g_target_size.x + uint(q.x);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    RWByteAddressBuffer out_light = engine_buffer(kEvGiOut);
    GiReservoir r = gi_empty();
    if (!s.valid || (s.flags & kGbEmitter) != 0u || (g_engine_info.x & kEngineIndirect) == 0u) {
        out_light.Store2(index * 8u, uint2(0u, 0u));
        return;
    }
    uint seed = emit_hash(index ^ (g_frame * 0x1B873593u) ^ 0x6A09E667u);
    const uint salt = index * 32u + g_frame * 0x632BE5ABu + 23u;
    {
        const GiReservoir own = gi_load(engine_view(kEvGiTemp), index);
        const float t = own.W > 0.0f ? gi_target(s, own.x, own.radiance) : 0.0f;
        gi_update(r, own.x, own.hn, own.radiance, t, t * own.W * own.m, own.m, seed);
    }
    // Three neighbours within 10 half-pixels (at kFootprintLines) on the same surface; their samples seen from here with
    // the solid angle's Jacobian (Ouyang et al. 2021, ReSTIR GI), the reused sample's visibility checked before it is
    // kept.
    bool reused = false;
    [loop] for (uint k = 0u; k < 3u; ++k) {
        const float angle = emit_random(seed) * 2.0f * kPi;
        const float radius = (1.0f + emit_random(seed) * 9.0f) * footprint_scale() * (2.0f / float(gi_step()));
        const int2 ho = int2(id.xy) + int2(round(float2(cos(angle), sin(angle)) * radius));
        if (any(ho < 0) || any(ho >= int2(half_size))) {
            continue;
        }
        const int2 o = gi_pixel(uint2(ho));
        const uint neighbour = uint(o.y) * g_target_size.x + uint(o.x);
        const GSurface ns = gb_load(engine_view(kEvGBuffer), neighbour, g_projection, o);
        if (!ns.valid || (ns.flags & kGbEmitter) != 0u || dot(ns.n, s.n) < 0.9f ||
            abs(ns.p.z - s.p.z) > 0.1f * abs(s.p.z)) {
            continue;
        }
        const GiReservoir other = gi_load(engine_view(kEvGiTemp), uint(ho.y) * half_size.x + uint(ho.x));
        if (!(other.W > 0.0f)) {
            r.m += other.m;
            continue;
        }
        // |cos at the sample towards here| / |cos towards there| x (distance there / distance here)^2; a neighbour
        // whose sample's solid angle differs past tenfold is not reused (its weight would be mostly wrong).
        const float3 to_here = s.p - other.x;
        const float3 to_there = ns.p - other.x;
        const float d_here = length(to_here);
        const float d_there = length(to_there);
        const float jacobian = d_here > 1e-3f && d_there > 1e-3f
                                   ? abs(dot(other.hn, to_here / d_here)) /
                                         max(abs(dot(other.hn, to_there / d_there)), 1e-3f) *
                                         (d_there * d_there) / (d_here * d_here)
                                   : 1.0f;
        if (!(jacobian >= 0.1f && jacobian <= 10.0f)) {
            continue;
        }
        const float t = gi_target(s, other.x, other.radiance);
        if (gi_update(r, other.x, other.hn, other.radiance, t, t * other.W * other.m * jacobian, other.m, seed)) {
            reused = true;
        }
    }
    r.W = r.target > 0.0f && r.m > 0.0f ? r.w_sum / (r.m * r.target) : 0.0f;
    if (reused && r.W > 0.0f && length(r.x) < 9.0e4f &&
        !engine_visible(surface_start(s, normalize(r.x - s.p)), r.x, 0.5f, salt)) {
        r.W = 0.0f;
    }
    // m = (1/pi) L cos W (this frame's light only: the history keeps the temporal reservoir).
    float3 m = float3(0.0f, 0.0f, 0.0f);
    if (r.W > 0.0f) {
        const float3 d = normalize(r.x - s.p);
        m = r.radiance * (max(dot(s.n, d), 0.0f) * r.W / kPi);
    }
    m = safe_light(m);
    out_light.Store2(index * 8u, uint2(pack_half2(m.r, m.g), f32tof16(m.b) | (1u << 31u)));
}

[numthreads(8, 8, 1)]
void EngineSpecularCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id.xy);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    RWByteAddressBuffer out_spec = engine_buffer(kEvSpecular);
    if (!s.valid || (s.flags & kGbEmitter) != 0u || (g_engine_info.x & kEngineSpecular) == 0u) {
        out_spec.Store2(pixel * 8u, uint2(0u, 0u));
        return;
    }
    uint seed = emit_hash(pixel ^ (g_frame * 0x27D4EB2Fu) ^ 0x3C6EF372u);
    const uint salt = pixel * 32u + g_frame * 0x632BE5ABu + 29u;
    const float3 v = normalize(-s.p);
    const float alpha = s.roughness * s.roughness;
    const float3 hm = ggx_vndf(s.n, v, alpha, float2(emit_random(seed), emit_random(seed)));
    const float3 l = reflect(-v, hm);
    const float nl = dot(s.n, l);
    float3 result = float3(0.0f, 0.0f, 0.0f);
    float distance = 0.0f;
    if (nl > 0.0f && dot(s.ng, l) > 0.0f) {
        const float3 start = surface_start(s, l);
        const Hit h = engine_trace(start, l, salt);
        float3 x;
        float3 hn;
        // The reflection's ray cone (Akenine-Moller et al. 2021): the pixel's width at the surface, spreading by the
        // pixel's angle and the lobe's (alpha, radians): what it sees is filtered over that footprint. At their
        // level-0 texels, point sampled, a reflection magnifying a texture showed its texels as squares, and one
        // minifying it sparkled.
        const float footprint = pixel_world_size(s.p.z);
        const float width = footprint + h.t * (footprint / max(length(s.p), 1e-3f) + alpha);
        const float3 incoming =
            safe_light(engine_water_ray(engine_hit_radiance(h, start, l, true, width, seed, salt + 5u, x, hn), h, start, l));
        // VNDF sampling's weight: F G2 / G1(v).
        const float nv = max(dot(s.n, v), 1e-4f);
        const float g2_over_g1 = smith_visibility(alpha, nl, nv) * 4.0f * nl * nv / max(smith_g1(alpha, nv), 1e-4f);
        result = incoming * fresnel_schlick(specular_f0(s), dot(v, hm)) * g2_over_g1;
        distance = h.t;
    }
    result = safe_light(result);
    out_spec.Store2(pixel * 8u, uint2(pack_half2(result.r, result.g), pack_half2(result.b, min(distance, 6.0e4f))));
}

// The half-resolution indirect light at a full-resolution pixel: its four nearest half-resolution samples weighed by
// depth and normal against this surface.
float3 gi_upsampled(GSurface s, int2 q)
{
    const uint2 half_size = g_engine_size.zw;
    RWByteAddressBuffer gi = engine_buffer(kEvGiOut);
    if (gi_step() == 1u) {
        // At full resolution: the pixel's own.
        const uint2 v = gi.Load2((uint(q.y) * half_size.x + uint(q.x)) * 8u);
        return (v.y >> 31u) != 0u ? float3(f16tof32(v.x), f16tof32(v.x >> 16u), f16tof32(v.y & 0xFFFFu))
                                  : float3(0.0f, 0.0f, 0.0f);
    }
    const float2 h = (float2(q) + 0.5f) / 2.0f - 0.5f;
    const int2 b = int2(floor(h));
    const float2 f = h - float2(b);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float weight = 0.0f;
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 o = clamp(b + int2(k & 1u, k >> 1u), int2(0, 0), int2(half_size) - 1);
        const uint2 v = gi.Load2((uint(o.y) * half_size.x + uint(o.x)) * 8u);
        if ((v.y >> 31u) == 0u) {
            continue;
        }
        const int2 at = gi_pixel(uint2(o));
        const GSurface ns = gb_load(engine_view(kEvGBuffer), uint(at.y) * g_target_size.x + uint(at.x), g_projection, at);
        const float bilinear = ((k & 1u) != 0u ? f.x : 1.0f - f.x) * ((k & 2u) != 0u ? f.y : 1.0f - f.y);
        const float w = (bilinear + 1e-3f) * pow(saturate(dot(ns.n, s.n)), 32.0f) *
                        exp(-abs(ns.p.z - s.p.z) / (0.02f * abs(s.p.z) + 1.0f));
        sum += float3(f16tof32(v.x), f16tof32(v.x >> 16u), f16tof32(v.y & 0xFFFFu)) * w;
        weight += w;
    }
    return weight > 1e-4f ? sum / weight : float3(0.0f, 0.0f, 0.0f);
}

// Temporal accumulation: this frame's diffuse (the lights' direct light and the indirect light; the sun's is
// EngineFinalCS's) and specular (the traced reflection) onto their reprojected histories (bilinear over the previous
// pixels on this surface), with the first two moments of luminance for the a-trous's variance. History length at most
// 32 (diffuse) and 16 (specular); the specular history is clamped to the 3 x 3 neighbourhood's range (ghosting). The
// diffuse history's anti-lag (after NVIDIA's ReBLUR, our own code): a fast history of its luminance (kFastHistory
// frames, kept in the specular history's spare word) against the slow one (the first moment); where they part by more
// than the noise allows (1.5 x the frames' deviation: three of the fast history's), the light changed (a lamp moved, a
// caster passed) and the history shortens to follow, to kFastHistory frames at most. A 32-frame history alone kept a
// moving light's pool, and the dark a character leaves in the bounce light, for half a second.
[numthreads(8, 8, 1)]
void EngineTemporalCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id.xy);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    RWByteAddressBuffer hist_d = engine_buffer(kEvHistDiffuse);
    RWByteAddressBuffer hist_s = engine_buffer(kEvHistSpecular);
    RWByteAddressBuffer out_a = engine_buffer(kEvAtrousA);
    if (!s.valid || (s.flags & kGbEmitter) != 0u) {
        hist_d.Store4(pixel * 16u, uint4(0u, 0u, 0u, 0u));
        hist_s.Store4(pixel * 16u, uint4(0u, 0u, 0u, 0u));
        out_a.Store4(pixel * 16u, uint4(0u, 0u, 0u, 0u));
        return;
    }
    RWByteAddressBuffer direct = engine_buffer(kEvDirect);
    const uint4 dv = direct.Load4(pixel * 16u);
    float3 diffuse = float3(f16tof32(dv.x), f16tof32(dv.x >> 16u), f16tof32(dv.y)) + gi_upsampled(s, q);
    const uint2 sv = engine_buffer(kEvSpecular).Load2(pixel * 8u);
    float3 specular = float3(f16tof32(sv.x), f16tof32(sv.x >> 16u), f16tof32(sv.y));
    diffuse = safe_light(diffuse);
    specular = safe_light(specular);
    // The neighbourhood's specular range (clamping the history), and the brightest of the 8 neighbours' direct light
    // and specular: a new sample above them is a firefly (one light sample's spike), lowered to them before it enters
    // the history; otherwise the a-trous's variance spreads it and the history keeps it, growing spikes into blobs.
    float3 lo = specular;
    float3 hi = specular;
    float top_direct = 0.0f;
    float top_specular = 0.0f;
    [unroll] for (int j = -1; j <= 1; ++j) {
        [unroll] for (int i = -1; i <= 1; ++i) {
            const int2 o = clamp(q + int2(i, j), int2(0, 0), int2(g_target_size) - 1);
            const uint op = uint(o.y) * g_target_size.x + uint(o.x);
            const uint4 ndv = direct.Load4(op * 16u);
            const uint2 nsv = engine_buffer(kEvSpecular).Load2(op * 8u);
            const float3 ns = safe_light(float3(f16tof32(nsv.x), f16tof32(nsv.x >> 16u), f16tof32(nsv.y)));
            lo = min(lo, ns);
            hi = max(hi, ns);
            if (i != 0 || j != 0) {
                top_direct = max(top_direct, luminance(safe_light(float3(f16tof32(ndv.x), f16tof32(ndv.x >> 16u),
                                                                         f16tof32(ndv.y)))));
                top_specular = max(top_specular, luminance(ns));
            }
        }
    }
    if ((g_engine_info.x & kEngineNoFirefly) == 0u) {
        // Past kFireflyRatio x the brightest neighbour: a small light is found by a few pixels first (4 candidates a
        // frame by power) and spatial reuse spreads it over the next frames. Lowered to the neighbours' level outright,
        // those first finds would never enter the history, and a Luma's or a bulb's light pool would never appear.
        const float3 own_direct = safe_light(float3(f16tof32(dv.x), f16tof32(dv.x >> 16u), f16tof32(dv.y)));
        const float l = luminance(own_direct);
        const float cap_direct = kFireflyRatio * top_direct;
        if (l > cap_direct) {
            diffuse -= own_direct * (1.0f - cap_direct / l);
        }
        const float ls = luminance(specular);
        const float cap_specular = kFireflyRatio * top_specular;
        if (ls > cap_specular) {
            specular *= cap_specular / ls;
        }
        diffuse = max(diffuse, 0.0f);
    }
    float3 prev_d = float3(0.0f, 0.0f, 0.0f);
    float3 prev_s = float3(0.0f, 0.0f, 0.0f);
    float2 prev_m = float2(0.0f, 0.0f);
    float prev_fast = 0.0f;
    float len_d = 0.0f;
    float len_s = 0.0f;
    History hp;
    if ((g_flags & 1u) != 0u && engine_history(q, s.p, s.n, hp)) {
        {
            const float2 a = hp.a - 0.5f;
            const int2 b = int2(floor(a));
            const float2 f = a - float2(b);
            RWByteAddressBuffer gb = engine_buffer(kEvGBufferPrev);
            RWByteAddressBuffer pd = engine_buffer(kEvHistDiffusePrev);
            RWByteAddressBuffer ps = engine_buffer(kEvHistSpecularPrev);
            float weight = 0.0f;
            float4 acc_d = float4(0.0f, 0.0f, 0.0f, 0.0f);
            float4 acc_s = float4(0.0f, 0.0f, 0.0f, 0.0f);
            float2 acc_m = float2(0.0f, 0.0f);
            float acc_fast = 0.0f;
            [unroll] for (uint k = 0u; k < 4u; ++k) {
                const int2 o = b + int2(k & 1u, k >> 1u);
                if (any(o < 0) || any(o >= int2(g_target_size))) {
                    continue;
                }
                const uint op = uint(o.y) * g_target_size.x + uint(o.x);
                const uint4 w0 = gb.Load4(op * 32u);
                const uint4 w1 = gb.Load4(op * 32u + 16u);
                if (w1.x == 0u || abs(asfloat(w1.x) - hp.z) > hp.tolerance ||
                    dot(unpack_normal(w0.x), hp.n) < hp.facing) {
                    continue;
                }
                const float w = ((k & 1u) != 0u ? f.x : 1.0f - f.x) * ((k & 2u) != 0u ? f.y : 1.0f - f.y);
                const uint4 hd = pd.Load4(op * 16u);
                const uint4 hs = ps.Load4(op * 16u);
                acc_d += float4(f16tof32(hd.x), f16tof32(hd.x >> 16u), f16tof32(hd.y), f16tof32(hd.y >> 16u)) * w;
                acc_s += float4(f16tof32(hs.x), f16tof32(hs.x >> 16u), f16tof32(hs.y), f16tof32(hs.y >> 16u)) * w;
                acc_m += asfloat(hd.zw) * w;
                acc_fast += asfloat(hs.z) * w;
                weight += w;
            }
            if (weight > 1e-3f) {
                prev_d = acc_d.rgb / weight;
                len_d = acc_d.a / weight;
                prev_s = acc_s.rgb / weight;
                len_s = acc_s.a / weight;
                prev_m = acc_m / weight;
                prev_fast = acc_fast / weight;
            }
        }
    }
    len_d = min(len_d + 1.0f, 32.0f);
    len_s = min(len_s + 1.0f, 16.0f);
    // Where the light changed, this frame weighs (1 - change) / length + change (A-SVGF).
    const float change = light_change(q);
    len_d = 1.0f / ((1.0f - change) / len_d + change);
    len_s = 1.0f / ((1.0f - change) / len_s + change);
    prev_s = clamp(prev_s, lo, hi);
    const float ld = luminance(diffuse);
    const float fast = lerp(prev_fast, ld, 1.0f / min(len_d, kFastHistory));
    if (len_d > kFastHistory && (g_engine_info.x & kEngineNoAntiLag) == 0u) {
        const float slow = prev_m.x;
        const float allowed = 1.5f * sqrt(max(prev_m.y - slow * slow, 0.0f)) + 0.02f * max(fast, slow) + 1e-4f;
        const float lag = abs(fast - slow);
        if (lag > allowed) {
            len_d = max(len_d * (allowed / lag) * (allowed / lag), kFastHistory);
        }
    }
    const float alpha_d = 1.0f / len_d;
    diffuse = lerp(prev_d, diffuse, alpha_d);
    float2 moments = lerp(prev_m, float2(ld, ld * ld), alpha_d);
    specular = lerp(prev_s, specular, 1.0f / len_s);
    // Variance from the moments; a short history takes the spatial neighbourhood's instead (SVGF).
    float variance = max(moments.y - moments.x * moments.x, 0.0f);
    if (len_d < 4.0f) {
        float s1 = 0.0f;
        float s2 = 0.0f;
        float n = 0.0f;
        [unroll] for (int j = -1; j <= 1; ++j) {
            [unroll] for (int i = -1; i <= 1; ++i) {
                const int2 o = clamp(q + int2(i, j), int2(0, 0), int2(g_target_size) - 1);
                const uint4 ndv = direct.Load4((uint(o.y) * g_target_size.x + uint(o.x)) * 16u);
                const float l = luminance(float3(f16tof32(ndv.x), f16tof32(ndv.x >> 16u), f16tof32(ndv.y)));
                s1 += l;
                s2 += l * l;
                n += 1.0f;
            }
        }
        s1 /= n;
        variance = max(s2 / n - s1 * s1, variance) * (4.0f / len_d);
    }
    const float variance_s = max(luminance(hi - lo) * luminance(hi - lo) * 0.25f, 1e-6f);
    hist_d.Store4(pixel * 16u, uint4(pack_half2(diffuse.r, diffuse.g), pack_half2(diffuse.b, len_d), asuint(moments)));
    hist_s.Store4(pixel * 16u, uint4(pack_half2(specular.r, specular.g), pack_half2(specular.b, len_s), asuint(fast), 0u));
    out_a.Store4(pixel * 16u, uint4(pack_half2(diffuse.r, diffuse.g), pack_half2(diffuse.b, variance),
                                    pack_half2(specular.r, specular.g), pack_half2(specular.b, variance_s)));
}

// Eye depth's largest change to the next pixel across and down (on the same surface: else none).
float depth_gradient(int2 q, float z)
{
    RWByteAddressBuffer gb = engine_buffer(kEvGBuffer);
    const int2 qx = min(q + int2(1, 0), int2(g_target_size) - 1);
    const int2 qy = min(q + int2(0, 1), int2(g_target_size) - 1);
    const float zx = asfloat(gb.Load((uint(qx.y) * g_target_size.x + uint(qx.x)) * 32u + 16u));
    const float zy = asfloat(gb.Load((uint(qy.y) * g_target_size.x + uint(qy.x)) * 32u + 16u));
    const float limit = abs(z) * 0.05f + 1.0f;
    const float gx = zx != 0.0f && abs(zx - z) < limit ? abs(zx - z) : 0.0f;
    const float gy = zy != 0.0f && abs(zy - z) < limit ? abs(zy - z) : 0.0f;
    return max(gx, gy);
}

// One a-trous iteration (step g_engine_info.y; 5 x 5 B3 spline): weights by the depth plane, the normal (n.n^64), and
// luminance against the variance (sigma 4 x its square root); the variance filtered with the squared weights. The
// first iteration's result becomes the history (SVGF).
[numthreads(8, 8, 1)]
void EngineAtrousCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id.xy);
    const uint step = g_engine_info.y;
    const bool ping = (g_engine_info.w & 1u) == 0u;
    RWByteAddressBuffer src = engine_buffer(ping ? kEvAtrousA : kEvAtrousB);
    RWByteAddressBuffer dst = engine_buffer(ping ? kEvAtrousB : kEvAtrousA);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    const uint4 c = src.Load4(pixel * 16u);
    if (!s.valid || (s.flags & kGbEmitter) != 0u) {
        dst.Store4(pixel * 16u, c);
        return;
    }
    const float3 cd = float3(f16tof32(c.x), f16tof32(c.x >> 16u), f16tof32(c.y));
    const float3 cs = float3(f16tof32(c.z), f16tof32(c.z >> 16u), f16tof32(c.w));
    const float var_d = f16tof32(c.y >> 16u);
    const float var_s = f16tof32(c.w >> 16u);
    const float ld = luminance(cd);
    const float ls = luminance(cs);
    const float sigma_d = 4.0f * sqrt(max(var_d, 1e-8f)) + 1e-4f;
    const float sigma_s = 4.0f * sqrt(max(var_s, 1e-8f)) + 1e-4f;
    // Depth's change per pixel (the plane's gradient).
    const float z_grad = depth_gradient(q, s.p.z) + 1e-3f;
    const float kernel[3] = {3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 16.0f};
    float4 sum_d = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 sum_s = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float wd_sum = 0.0f;
    float ws_sum = 0.0f;
    float spec_power = lerp(128.0f, 8.0f, s.roughness);
    // A short diffuse history (a moving character, a disocclusion) borrows from neighbours of less like normals: n.n^2
    // at one frame to n.n^64 from eight. A fixed n.n^64 (or ^8) leaves a moving character's raw indirect noise.
    const float history = f16tof32(engine_buffer(kEvHistDiffuse).Load(pixel * 16u + 4u) >> 16u);
    const float normal_power = lerp(2.0f, 64.0f, saturate((history - 1.0f) / 7.0f));
    // A reflection spreads no wider than its lobe: a polished surface's (one ray along a near-mirror direction, a sharp
    // image whose hit lighting alone is noisy) keeps to its pixel, the full filter from roughness 0.3.
    const float spec_spread = saturate((s.roughness - 0.05f) / 0.25f);
    [unroll] for (int j = -2; j <= 2; ++j) {
        [unroll] for (int i = -2; i <= 2; ++i) {
            const int2 o = q + int2(i, j) * int(step);
            if (any(o < 0) || any(o >= int2(g_target_size))) {
                continue;
            }
            const uint op = uint(o.y) * g_target_size.x + uint(o.x);
            const GSurface ns = gb_load(engine_view(kEvGBuffer), op, g_projection, o);
            if (!ns.valid || (ns.flags & kGbEmitter) != 0u) {
                continue;
            }
            const uint4 nc = src.Load4(op * 16u);
            const float3 nd = float3(f16tof32(nc.x), f16tof32(nc.x >> 16u), f16tof32(nc.y));
            const float3 nsp = float3(f16tof32(nc.z), f16tof32(nc.z >> 16u), f16tof32(nc.w));
            const float h = kernel[abs(i)] * kernel[abs(j)];
            const float wz = exp(-abs(ns.p.z - s.p.z) / (z_grad * length(float2(i, j)) * float(step) + 1.0f));
            const float nn = saturate(dot(ns.n, s.n));
            const float wn = pow(nn, normal_power);
            const float wd = h * wz * wn * exp(-abs(luminance(nd) - ld) / sigma_d);
            const float ws = h * wz * pow(nn, spec_power) * exp(-abs(luminance(nsp) - ls) / sigma_s) *
                             exp(-abs(ns.roughness - s.roughness) * 8.0f) * (i == 0 && j == 0 ? 1.0f : spec_spread);
            sum_d += float4(nd, f16tof32(nc.y >> 16u) * wd) * float4(wd, wd, wd, wd);
            sum_s += float4(nsp, f16tof32(nc.w >> 16u) * ws) * float4(ws, ws, ws, ws);
            wd_sum += wd;
            ws_sum += ws;
        }
    }
    const float3 fd = wd_sum > 1e-6f ? sum_d.rgb / wd_sum : cd;
    const float3 fs = ws_sum > 1e-6f ? sum_s.rgb / ws_sum : cs;
    const float fvd = wd_sum > 1e-6f ? sum_d.a / (wd_sum * wd_sum) : var_d;
    const float fvs = ws_sum > 1e-6f ? sum_s.a / (ws_sum * ws_sum) : var_s;
    dst.Store4(pixel * 16u, uint4(pack_half2(fd.r, fd.g), pack_half2(fd.b, fvd), pack_half2(fs.r, fs.g),
                                  pack_half2(fs.b, fvs)));
    if (step == 1u) {
        RWByteAddressBuffer hist_d = engine_buffer(kEvHistDiffuse);
        const uint4 hd = hist_d.Load4(pixel * 16u);
        hist_d.Store4(pixel * 16u, uint4(pack_half2(fd.r, fd.g), (hd.y & 0xFFFF0000u) | f32tof16(fd.b), hd.z, hd.w));
    }
}

// The light a glass reflection's ray brings back from its hit: emission plus its colour under the sun (one shadow ray,
// opaque traversal: alpha cards count as solid) and the open sky (its irradiance harmonics: no ray). Light from the
// emitters and the bounce are left to the opaque surfaces' own reflections: the glass shows them faintly at F. Cheap
// and bounded per glass pixel: the full hit lighting under a screen full of crystals can hang the GPU.
float3 clear_hit_radiance(Hit h, float3 origin, float3 direction)
{
    if (!h.surface) {
        return h.late ? float3(0.0f, 0.0f, 0.0f) : physical_sky(direction);
    }
    float3 albedo = float3(0.5f, 0.5f, 0.5f);
    float3 emission = float3(0.0f, 0.0f, 0.0f);
    if ((g_engine_info.x & kEngineClearNoTev) == 0u) {
        reference_material(h.tri, h.b, albedo, emission);
    }
    const uint draw = g_surfaces.Load(h.tri * 16u + 12u) >> 16u;
    const uint2 table = g_draws.Load2(min(draw, max(g_draw_count, 1u) - 1u) * kDrawStride + 8u);
    emission *= shown_emission(asfloat(table.y));   // seen: as the camera shows it
    const float3 v0 = asfloat(g_positions.Load3(h.tri * 36u));
    const float3 v1 = asfloat(g_positions.Load3(h.tri * 36u + 12u));
    const float3 v2 = asfloat(g_positions.Load3(h.tri * 36u + 24u));
    const float3 face = cross(v1 - v0, v2 - v0);
    if (dot(face, face) <= 1e-24f) {
        return emission;
    }
    float3 n = normalize(face);
    n = dot(n, direction) > 0.0f ? -n : n;
    const float3 x = origin + direction * h.t;
    float3 m = sky_light_colour(physical_sky_irradiance(n));
    const float nl = dot(n, g_sun.xyz);
    if (nl > 0.0f && any(g_sun_light.rgb > 0.0f)) {
        RayDesc ray;
        ray.Origin = x + n * (0.2f + abs(x.z) * 4e-4f);
        ray.Direction = g_sun.xyz;
        ray.TMin = 0.0f;
        ray.TMax = g_sun_reach;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>
            query;
        query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
        query.Proceed();
        if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
            m += g_sun_light.rgb * nl;
        }
    }
    return albedo * m + emission;
}

// A thin glass wall (n = 1.5) at the cosine `c`: the share it reflects, the incoherent sum of its two faces' reflections
// R = 2F / (1 + F), and passes, T = (1 - F) / (1 + F) (Stokes; no absorption in a clear wall).
float2 glass_wall(float c)
{
    const float f = fresnel_dielectric(c, 1.5f);
    return float2(2.0f * f / (1.0f + f), (1.0f - f) / (1.0f + f));
}

struct ClearHit {
    bool hit;
    float t;
    uint tri;
    float2 b;
};

// The nearest clear surface (the engine's glass) along the camera ray between `t_min` and `t_max` (none when the
// span is empty: a ray whose start passes its end is undefined and can hang the GPU where glass lies on the surface
// behind it).
ClearHit clear_trace(float3 dir, float t_min, float t_max)
{
    ClearHit h;
    h.hit = false;
    h.t = t_max;
    h.tri = 0u;
    h.b = float2(0.0f, 0.0f);
    if (!(t_min < t_max)) {
        return h;
    }
    const RaytracingAccelerationStructure water = ResourceDescriptorHeap[engine_view(kEvWaterScene)];
    const ByteAddressBuffer surfaces = ResourceDescriptorHeap[engine_view(kEvWaterSurfaces)];
    const ByteAddressBuffer draws = ResourceDescriptorHeap[engine_view(kEvClearDraws)];
    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, 0.0f);
    ray.Direction = dir;
    ray.TMin = t_min;
    ray.TMax = t_max;
    // The nearest surface of the water window, the engine's when it is glass. Opaque traversal, the closest hit alone:
    // reporting every candidate through layered surfaces (Dusty Dune's sand) can run past the GPU's watchdog.
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(water, RAY_FLAG_NONE, 0xFFu, ray);
    query.Proceed();
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        const uint tri = query.CommittedPrimitiveIndex();
        const uint draw = surfaces.Load(tri * 16u + 12u) >> 16u;
        if ((draws.Load(draw * 4u) & kClearGlass) != 0u) {
            h.hit = true;
            h.t = query.CommittedRayT();
            h.tri = tri;
            h.b = query.CommittedTriangleBarycentrics();
        }
    }
    return h;
}

// What a glass wall at `h` (camera ray `dir`) sends towards the camera by reflection: its sky, scene and emitters along
// a GGX visible-normal ray at the table's roughness (the hit lit as the engine's specular hits), and the sun's
// highlight behind a shadow ray; out: the wall's transmittance. Only the front wall traces a ray: the far wall's
// reflection looks into what the glass holds (an hourglass's sand: hundreds of cards a ray, past the GPU's watchdog),
// for a reflection the front wall dims.
float3 clear_wall(ClearHit h, float3 dir, uint pixel, uint layer, out float transmit)
{
    const ByteAddressBuffer positions = ResourceDescriptorHeap[engine_view(kEvWaterPositions)];
    const ByteAddressBuffer surfaces = ResourceDescriptorHeap[engine_view(kEvWaterSurfaces)];
    const ByteAddressBuffer draws = ResourceDescriptorHeap[engine_view(kEvClearDraws)];
    const float3 v0 = asfloat(positions.Load3(h.tri * 36u));
    const float3 v1 = asfloat(positions.Load3(h.tri * 36u + 12u));
    const float3 v2 = asfloat(positions.Load3(h.tri * 36u + 24u));
    const uint4 record = surfaces.Load4(h.tri * 16u);
    float3 ng = cross(v1 - v0, v2 - v0);
    ng = dot(ng, ng) > 1e-24f ? normalize(ng) : -dir;
    ng = dot(ng, dir) > 0.0f ? -ng : ng;
    float3 n = ng;
    if (record.x != 0u && record.y != 0u && record.z != 0u) {
        const float3 w = float3(1.0f - h.b.x - h.b.y, h.b.x, h.b.y);
        float3 smooth = unpack_vertex_normal(record.x) * w.x + unpack_vertex_normal(record.y) * w.y +
                        unpack_vertex_normal(record.z) * w.z;
        smooth *= dot(smooth, ng) < 0.0f ? -1.0f : 1.0f;
        const float smooth_length = length(smooth);
        if (smooth_length > 1e-3f && dot(smooth, ng) > 0.05f * smooth_length) {
            n = smooth / smooth_length;
        }
    }
    const float3 v = -dir;
    if (dot(n, v) < 0.02f) {
        n = ng;
    }
    const uint word = draws.Load((record.w >> 16u) * 4u);
    const float roughness = (word & 0xFFFFu) == 0xFFFFu ? 0.02f : max(float(word & 0xFFFFu) / 65534.0f, 0.02f);
    const float alpha = roughness * roughness;
    const float nv = max(dot(n, v), 1e-4f);
    transmit = glass_wall(nv).y;
    const float3 x = dir * h.t;
    const float3 start = x + ng * (0.2f + abs(x.z) * 4e-4f);
    uint seed = emit_hash(pixel ^ (g_frame * 0x5BD1E995u) ^ (layer * 0x9E3779B9u));
    const uint salt = pixel * 32u + g_frame * 0x632BE5ABu + 37u + layer;
    // The reflection: a visible normal, its mirror direction, the light along it; weight R(v.m) G2 / G1.
    const float3 m = ggx_vndf(n, v, alpha, float2(emit_random(seed), emit_random(seed)));
    const float3 l = reflect(-v, m);
    const float nl = dot(n, l);
    float3 reflected = float3(0.0f, 0.0f, 0.0f);
    if (nl > 0.0f && dot(ng, l) > 0.0f && layer == 0u && (g_engine_info.x & kEngineClearNoRay) == 0u) {
        const Hit hit = engine_trace(start, l, salt);
        const float3 incoming = (g_engine_info.x & kEngineClearNoShade) != 0u
                                    ? (hit.surface ? float3(0.1f, 0.1f, 0.1f) : float3(1.0f, 1.0f, 1.0f))
                                    : safe_light(clear_hit_radiance(hit, start, l));
        const float g2_over_g1 = smith_visibility(alpha, nl, nv) * 4.0f * nl * nv / max(smith_g1(alpha, nv), 1e-4f);
        reflected = incoming * (glass_wall(dot(v, m)).x * g2_over_g1);
    }
    // The sun's highlight, its lobe at least the disc wide.
    if (any(g_sun_light.rgb > 0.0f) && (g_engine_info.x & kEngineClearNoSun) == 0u) {
        const float3 ls = g_sun.xyz;
        const float nls = dot(n, ls);
        float blocker;
        if (nls > 0.0f && dot(ng, ls) > 0.0f && sun_visible(start, sun_direction_sample(float2(emit_random(seed),
                                                                                          emit_random(seed))),
                                                             false, salt + 9u, blocker)) {
            const float3 hs = normalize(v + ls);
            const float a = sun_alpha(alpha);
            reflected += g_sun_light.rgb * (glass_wall(dot(v, hs)).x * ggx_d(a, dot(n, hs)) *
                                            smith_visibility(a, nls, nv) * nls * kPi);
        }
    }
    return safe_light(reflected);
}

// The clear solids (rt_lighting.hlsl kDrawTableSolid: the table's `traced solid`, the crystal cages): in the scene for
// the other rays (their shadows, kClearShadow) but never a camera ray's surface (kDrawTableUnseen), traced here as
// closed dielectrics, as RTX Remix's translucent material without thin walls and Whitted's recursion for smooth glass
// (Unity HDRP's recursive rendering) trace them: the front face's Fresnel reflection; the light refracted in and
// followed inside, each face it reaches sending its transmitted share out (none under total internal reflection) and
// reflecting the rest on; what each share meets outside; the row's transmittance colour over the path (Beer-Lambert).
// A crystal mirrors what stands before it mostly from within: light from the front enters, reflects totally at the
// far facets and leaves towards the camera. Each solid's index is its table row's (`ior<n>`), else quartz's (ordinary
// ray, 589 nm; critical angle 40.4 degrees). Quartz reflects 4.6 % at normal incidence, too little for the crystal
// cages to mirror what stands by them; their rows take moissanite's (silicon carbide, 2.65: 20.7 % at normal
// incidence, 23 % at 45 degrees; critical angle 22.2 degrees, so more of the light that enters is held and sent back).
static const float kSolidIor = 1.544f;

float solid_ior(uint draw)
{
    const uint bits = g_draws.Load(draw * kDrawStride + 44u) >> 16u;
    return bits == 0u ? kSolidIor : 1.0f + float(bits) / 16384.0f;
}
// The internal path's segments at most, and the share of the light below which it ends. Total internal reflection
// can hold a ray inside a prism for many faces (a light pipe): what is still inside when the path ends leaves as the
// light that left before it did (its shares scaled by the light inside over the light sent, at most kSolidRest times:
// a path that sent out little before it ended stands for little).
static const uint kSolidSegments = 16u;
static const float kSolidRest = 4.0f;
static const float kSolidLeast = 0.01f;

// The nearest surface along a ray of a clear solid's paths: the scene's surfaces as engine_trace meets them, and the
// clear solids' faces when `solids` (inside one: its far faces). A ray leaving a solid passes the others it meets
// (their own refraction is not followed). `solid`: the hit is a solid's face.
Hit solid_trace(float3 origin, float3 direction, bool solids, uint salt, out bool solid)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = 1.0e6f;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
    uint visits = 0u;
    while (query.Proceed()) {
        if (++visits > kMaxCandidates) {
            query.CommitNonOpaqueTriangleHit();
            query.Abort();
            break;
        }
        const bool late_hit = query.CandidateInstanceID() != 0u;
        const uint flags = late_hit ? 0u : g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
        if (!late_hit && solid_draw(flags)) {
            if (solids) {
                query.CommitNonOpaqueTriangleHit();
            }
        } else if (late_hit || card_blocks(query.CandidatePrimitiveIndex(), flags,
                                           origin + direction * query.CandidateTriangleRayT(), salt)) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    Hit h;
    h.surface = false;
    h.late = false;
    h.t = 1.0e6f;
    h.tri = 0u;
    h.b = float2(0.0f, 0.0f);
    solid = false;
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        h.t = query.CommittedRayT();
        h.late = query.CommittedInstanceID() != 0u;
        h.surface = !h.late && length(origin + direction * h.t) <= g_ambient.w;
        h.tri = query.CommittedPrimitiveIndex();
        h.b = query.CommittedTriangleBarycentrics();
        solid = h.surface && solid_draw(g_surfaces.Load(h.tri * 16u + 12u));
    }
    return h;
}

// The nearest clear solid's face along the camera ray between `t_min` and `t_max` (the raster's depth: what the
// picture shows in front of it hides it).
ClearHit solid_front(float3 dir, float t_min, float t_max)
{
    ClearHit h;
    h.hit = false;
    h.t = t_max;
    h.tri = 0u;
    h.b = float2(0.0f, 0.0f);
    if (!(t_min < t_max)) {
        return h;
    }
    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, 0.0f);
    ray.Direction = dir;
    ray.TMin = t_min;
    ray.TMax = t_max;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, kSceneMask, ray);
    uint visits = 0u;
    while (query.Proceed()) {
        if (++visits > kMaxCandidates) {
            query.Abort();
            break;
        }
        if (solid_draw(g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u))) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        h.hit = true;
        h.t = query.CommittedRayT();
        h.tri = query.CommittedPrimitiveIndex();
        h.b = query.CommittedTriangleBarycentrics();
    }
    return h;
}

// A clear solid's absorption per eye unit (Beer-Lambert: its row's colour, linear, is what it passes after its reach,
// g_draws words 2 and 6); none without a reach.
float3 solid_absorption(uint tri)
{
    const uint draw = min(g_surfaces.Load(tri * 16u + 12u) >> 16u, max(g_draw_count, 1u) - 1u);
    const float reach = float(g_draws.Load(draw * kDrawStride + 24u) >> 16u);
    if (!(reach > 0.0f)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const uint colour = g_draws.Load(draw * kDrawStride + 8u);
    const float3 c = srgb_to_linear(float3((colour >> 24u) & 0xFFu, (colour >> 16u) & 0xFFu, (colour >> 8u) & 0xFFu) /
                                    255.0f);
    return -log(max(c, 1e-4f)) / reach;
}

// What a ray leaving a clear solid brings back: the surface it meets as the frame lit it where the picture shows that
// surface (its pixel's diffuse light, g_final, the same surface by depth: albedo x light, as engine_hit_radiance's
// cache), else as a glass reflection's hit (clear_hit_radiance: emission, the sun behind a shadow ray, the sky); the
// backdrop where it leaves the scene.
float3 solid_seen(Hit h, float3 origin, float3 direction)
{
    if (h.surface) {
        const float3 x = origin + direction * h.t;
        float3 albedo;
        float3 emission;
        reference_material(h.tri, h.b, albedo, emission);
        if (!any(emission > 0.0f) && x.z < -g_near) {
            const float2 q = eye_to_pixel(g_projection, g_viewport, x);
            if (inside(g_viewport, q) && all(q >= 0.0f) && all(q < float2(g_target_size))) {
                const uint2 sp = uint2(q) / g_factor;
                if (all(sp < g_sample_size)) {
                    const uint4 f = g_final[sp.y * g_sample_size.x + sp.x];
                    if (f.z != 0u && abs(asfloat(f.z) - x.z) <= 0.02f * abs(x.z) + 2.0f) {
                        const float3 whole = float3(f16tof32(f.x), f16tof32(f.x >> 16u), f16tof32(f.y));
                        return albedo * max(whole, 0.0f) / max(physical_exposure(), 1e-4f);
                    }
                }
            }
        }
    }
    return clear_hit_radiance(h, origin, direction);
}

// The picture's pixel showing what a ray meets (its hit at the raster's depth there; where it leaves the scene, a
// pixel of the backdrop), or -1 where the picture does not show it.
int2 solid_picture(Hit h, float3 origin, float3 direction)
{
    if (h.late) {
        return int2(-1, -1);
    }
    const float3 x = origin + direction * (h.surface ? h.t : 1.0e5f);
    if (x.z >= -g_near) {
        return int2(-1, -1);
    }
    const float2 q = eye_to_pixel(g_projection, g_viewport, x);
    if (!inside(g_viewport, q) || any(q < 0.0f) || any(q >= float2(g_target_size))) {
        return int2(-1, -1);
    }
    const float depth = g_efb_depth.Load(int3(int2(q), 0));
    if (!h.surface) {
        return far_plane(g_depth, depth) ? int2(q) : int2(-1, -1);
    }
    if (far_plane(g_depth, depth)) {
        return int2(-1, -1);
    }
    return abs(depth_to_eye_z(g_depth, depth) - x.z) <= 0.01f * abs(x.z) + 2.0f ? int2(q) : int2(-1, -1);
}

// A clear surface's light towards the camera: radiance added (before the exposure), and the picture's share at
// `source` (-1: the camera's own pixel).
struct ClearLight {
    float3 r;
    float3 t;
    int2 source;
};

// One share of the light a clear solid passes, along `direction` from `origin` to the hit `h`: the first that leaves
// it is the picture where the picture shows what it meets (as the frame drew it), the rest and the others traced.
void solid_share(inout ClearLight o, float3 share, Hit h, float3 origin, float3 direction, inout bool picture,
                 inout float3 sent)
{
    sent += share;
    if (picture) {
        picture = false;
        const int2 source = solid_picture(h, origin, direction);
        if (source.x >= 0) {
            o.t = share;
            o.source = source;
            return;
        }
    }
    o.r += share * safe_light(solid_seen(h, origin, direction));
}

// A face of a clear solid at `h` (eye space, `dir` the ray reaching it): its point, its face normal and its shading
// normal on the side the ray comes from.
void solid_face(uint tri, float2 b, float3 dir, out float3 x, out float3 ng, out float3 n)
{
    const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
    const float3 v1 = asfloat(g_positions.Load3(tri * 36u + 12u));
    const float3 v2 = asfloat(g_positions.Load3(tri * 36u + 24u));
    const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
    x = v0 * w.x + v1 * w.y + v2 * w.z;
    ng = cross(v1 - v0, v2 - v0);
    ng = dot(ng, ng) > 1e-24f ? normalize(ng) : -dir;
    ng = dot(ng, dir) > 0.0f ? -ng : ng;
    float3 lifted = x;
    smooth_surface(tri, w, v0, v1, v2, ng, n, lifted);
    if (dot(n, -dir) < 0.02f) {
        n = ng;
    }
}

// The light a clear solid at the camera ray's hit `h` sends towards the camera.
ClearLight clear_solid(ClearHit h, float3 dir, uint pixel)
{
    ClearLight o;
    o.r = float3(0.0f, 0.0f, 0.0f);
    o.t = float3(0.0f, 0.0f, 0.0f);
    o.source = int2(-1, -1);
    float3 x;
    float3 ng;
    float3 n;
    solid_face(h.tri, h.b, dir, x, ng, n);
    const float3 v = -dir;
    const uint draw = min(g_surfaces.Load(h.tri * 16u + 12u) >> 16u, max(g_draw_count, 1u) - 1u);
    const uint rough = g_draws.Load(draw * kDrawStride + 16u) & 0xFFFFu;
    const float roughness = rough == 0xFFFFu ? 0.02f : max(float(rough) / 65534.0f, 0.02f);
    const float alpha = roughness * roughness;
    const float ior = solid_ior(draw);
    const float nv = max(dot(n, v), 1e-4f);
    const float lift = 0.2f + abs(x.z) * 4e-4f;
    const float3 start = x + ng * lift;
    uint seed = emit_hash(pixel ^ (g_frame * 0x5BD1E995u) ^ 0x2F6B1C3Du);
    const uint salt = pixel * 32u + g_frame * 0x632BE5ABu + 41u;
    // The front face's reflection: a visible normal, its mirror direction, the light along it; weight F(v.m) G2 / G1.
    if ((g_engine_info.x & kEngineClearNoRay) == 0u) {
        const float3 m = ggx_vndf(n, v, alpha, float2(emit_random(seed), emit_random(seed)));
        const float3 l = reflect(-v, m);
        const float nl = dot(n, l);
        if (nl > 0.0f && dot(ng, l) > 0.0f) {
            bool solid;
            const Hit hit = solid_trace(start, l, false, salt, solid);
            const float3 incoming = (g_engine_info.x & kEngineClearNoShade) != 0u
                                        ? (hit.surface ? float3(0.1f, 0.1f, 0.1f) : float3(1.0f, 1.0f, 1.0f))
                                        : safe_light(solid_seen(hit, start, l));
            const float g2_over_g1 = smith_visibility(alpha, nl, nv) * 4.0f * nl * nv / max(smith_g1(alpha, nv), 1e-4f);
            o.r += incoming * (fresnel_dielectric(dot(v, m), ior) * g2_over_g1);
        }
    }
    // The sun's highlight, its lobe at least the disc wide.
    if (any(g_sun_light.rgb > 0.0f) && (g_engine_info.x & kEngineClearNoSun) == 0u) {
        const float3 ls = g_sun.xyz;
        const float nls = dot(n, ls);
        float blocker;
        if (nls > 0.0f && dot(ng, ls) > 0.0f &&
            sun_visible(start, sun_direction_sample(float2(emit_random(seed), emit_random(seed))), false, salt + 9u,
                        blocker)) {
            const float3 hs = normalize(v + ls);
            const float a = sun_alpha(alpha);
            o.r += g_sun_light.rgb * (fresnel_dielectric(dot(v, hs), ior) * ggx_d(a, dot(n, hs)) *
                                      smith_visibility(a, nls, nv) * nls * kPi);
        }
    }
    // Inside: refracted at the front face, followed along its internally reflected path; each far face passes its
    // share out (Fresnel from inside; none beyond the critical angle) and reflects the rest on.
    const float3 entered = refract(dir, n, 1.0f / ior);
    if (!(dot(entered, entered) > 1e-8f)) {
        return o;
    }
    float3 d = normalize(entered);
    float3 throughput = (1.0f - fresnel_dielectric(nv, ior)).xxx;
    float3 p = x - ng * lift;
    const float3 sigma = solid_absorption(h.tri);
    bool picture = true;
    ClearLight inside;
    inside.r = float3(0.0f, 0.0f, 0.0f);
    inside.t = float3(0.0f, 0.0f, 0.0f);
    inside.source = int2(-1, -1);
    float3 sent = float3(0.0f, 0.0f, 0.0f);
    [loop] for (uint k = 0u; k < kSolidSegments && max(throughput.r, max(throughput.g, throughput.b)) >= kSolidLeast;
                ++k) {
        bool solid;
        const Hit inner = solid_trace(p, d, true, salt + 3u + 7u * k, solid);
        throughput *= exp(-sigma * min(inner.t, 1.0e5f));
        if (!solid) {
            // What lies inside it (a coin, a star) or, out of an open mesh, beyond it: all of its light.
            solid_share(inside, throughput, inner, p, d, picture, sent);
            throughput = float3(0.0f, 0.0f, 0.0f);
            break;
        }
        float3 y;
        float3 fg;
        float3 fn;
        solid_face(inner.tri, inner.b, d, y, fg, fn);
        const float out_lift = 0.2f + abs(y.z) * 4e-4f;
        const float fresnel = fresnel_dielectric(dot(-d, fn), 1.0f / ior);
        if (fresnel < 1.0f) {
            const float3 leaving = refract(d, fn, ior);
            if (dot(leaving, leaving) > 1e-8f) {
                const float3 l = normalize(leaving);
                const float3 q = y - fg * out_lift;
                bool passed;
                const Hit outer = solid_trace(q, l, false, salt + 5u + 7u * k, passed);
                solid_share(inside, throughput * (1.0f - fresnel), outer, q, l, picture, sent);
            }
        }
        throughput *= fresnel;
        d = reflect(d, fn);
        p = y + fg * out_lift;
    }
    if ((g_engine_info.x & kEngineClearSolidLeft) != 0u) {
        o.r = float3(4.0f * max(throughput.r, max(throughput.g, throughput.b)), 0.0f, 0.0f) /
              max(physical_exposure(), 1e-4f);
        o.t = float3(0.0f, 0.0f, 0.0f);
        o.source = int2(-1, -1);
        return o;
    }
    const float3 scale = min((sent + throughput) / max(sent, 1e-6f), kSolidRest);
    o.r += inside.r * scale;
    o.t = inside.t * scale;
    o.source = inside.source;
    return o;
}

// Per pixel: the camera ray's first clear surface in front of the scene behind it, and a second behind that (the far
// wall of a capsule or a bottle, seen through the first), or a clear solid traced through (clear_solid). Their
// reflections at the camera's exposure and the light they pass, for the composite (rt_lighting.hlsl
// ClearCompositePS: the picture behind, or the picture a solid refracts, times T plus R).
[numthreads(8, 8, 1)]
void EngineClearCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id.xy);
    uint4 result = uint4(0u, 0u, 0u, 0u);
    uint4 behind = uint4(pack_half2(1.0f, 1.0f), 0xFFFFFFFFu, 0u, 0u);
    if (((g_engine_info.x & kEngineView) >> 4u) == 5u) {
        // GALAXY_RT_ENGINE_VIEW=5: blue where the pass ran outside the viewport.
        result = uint4(pack_half2(0.0f, 0.0f), pack_half2(0.3f, 1.0f), 0u, 1u);
    }
    if (inside(g_viewport, float2(q) + 0.5f)) {
        const float depth = g_efb_depth.Load(int3(q, 0));
        const float3 through = pixel_direction(g_projection, g_viewport, float2(q) + 0.5f);
        const float3 dir = normalize(through);
        const float t_min = g_near / max(-dir.z, 1e-6f);
        const float t_max = far_plane(g_depth, depth) ? 1.0e6f
                                                      : length(through) * -depth_to_eye_z(g_depth, depth) * 1.003f + 1.0f;
        // A clear solid, then the glass in front of it (each none where the frame has none: an empty span).
        const ClearHit solid = solid_front(dir, t_min, (g_engine_info.y & kClearHasSolids) != 0u ? t_max : 0.0f);
        const ClearHit front =
            clear_trace(dir, t_min, (g_engine_info.y & kClearHasGlass) != 0u ? (solid.hit ? solid.t : t_max) : 0.0f);
        if ((front.hit || solid.hit) && (g_engine_info.x & kEngineClearFind) != 0u) {
            result = uint4(pack_half2(0.0f, 0.5f), pack_half2(0.5f, 0.5f), 0u, 1u);
        } else if (!front.hit && solid.hit) {
            const ClearLight light = clear_solid(solid, dir, pixel);
            float3 r = min(light.r * physical_exposure(), 6.0e4f);
            float3 t = light.t;
            // GALAXY_RT_ENGINE_VIEW=5: the clear surfaces found, magenta.
            if (((g_engine_info.x & kEngineView) >> 4u) == 5u) {
                r = float3(4.0f, 0.0f, 4.0f);
                t = float3(0.0f, 0.0f, 0.0f);
            }
            const float z = (dir * solid.t).z;
            result = uint4(pack_half2(r.r, r.g), pack_half2(r.b, t.r), asuint(z), 1u);
            behind = uint4(pack_half2(t.g, t.b),
                           light.source.x >= 0 ? uint(light.source.x) | (uint(light.source.y) << 16u) : 0xFFFFFFFFu, 0u,
                           0u);
        } else if (front.hit) {
            float t1;
            float3 r = clear_wall(front, dir, pixel, 0u, t1);
            float t = t1;
            const ClearHit back = clear_trace(dir, front.t + max(0.5f, front.t * 1e-3f),
                                              (g_engine_info.x & kEngineClearNoBack) != 0u ? 0.0f : t_max);
            if (back.hit) {
                float t2;
                r += t1 * clear_wall(back, dir, pixel, 1u, t2);
                t *= t2;
            }
            r = min(r * physical_exposure(), 6.0e4f);
            // GALAXY_RT_ENGINE_VIEW=5: the clear surfaces found, magenta.
            if (((g_engine_info.x & kEngineView) >> 4u) == 5u) {
                r = float3(4.0f, 0.0f, 4.0f);
                t = 0.0f;
            }
            const float z = (dir * front.t).z;
            result = uint4(pack_half2(r.r, r.g), pack_half2(r.b, t), asuint(z), 1u);
            behind = uint4(pack_half2(t, t), 0xFFFFFFFFu, 0u, 0u);
        } else if (((g_engine_info.x & kEngineView) >> 4u) == 5u) {
            // ... and where none was found within the scene's depth, a green veil (the pass and its composite ran).
            result = uint4(pack_half2(0.0f, 0.15f), pack_half2(0.0f, 1.0f), asuint(-t_max), 1u);
        }
    }
    engine_buffer(kEvClear).Store4(pixel * 32u, result);
    engine_buffer(kEvClear).Store4(pixel * 32u + 16u, behind);
}

// The converged reference (GALAXY_RT_ENGINE_REFERENCE): the engine's diffuse light (m) at a surface as one unbiased
// path. At every vertex the sun and the lights behind shadow rays (as the direct light: the emitters' light by
// next-event estimation only), the sky where the cosine ray leaves the scene, the hit's albedo (reference_material, as
// the engine's hits) carried on; Russian roulette from the third bounce. No reservoirs, no history, no radiance cache,
// no denoiser: the same scene, materials and light, so what the real-time passes add or lose shows against it
// ([rt-reference-engine], EngineFinalCS).
float3 reference_path(float3 p, float3 n, float3 ng, bool card, inout uint seed, uint salt)
{
    float3 total = float3(0.0f, 0.0f, 0.0f);
    float3 throughput = float3(1.0f, 1.0f, 1.0f);
    const uint bounces = asuint(g_engine_params.z);
    [loop] for (uint bounce = 0u; bounce <= bounces; ++bounce) {
        const uint at = salt + bounce * 101u;
        const float3 start = p + ng * (0.2f + abs(p.z) * 4e-4f);
        float3 m = float3(0.0f, 0.0f, 0.0f);
        if (any(g_sun_light.rgb > 0.0f)) {
            const float3 l = sun_direction_sample(float2(emit_random(seed), emit_random(seed)));
            const float nl = dot(n, l);
            float blocker;
            if (nl > 0.0f && dot(ng, l) > -0.05f && sun_visible(start, l, card && bounce == 0u, at, blocker)) {
                m += g_sun_light.rgb * nl * water_through(start, g_sun.xyz);
            }
        }
        m += engine_light_sample(p, n, start, 8u, seed, at + 13u);
        total += throughput * safe_light(m);
        const float3 d = cosine_direction(n, ng, float2(emit_random(seed), emit_random(seed)));
        const Hit h = engine_trace(start, d, at + 7u);
        if (!h.surface) {
            if (!h.late) {
                total += throughput * sky_light_colour(physical_sky(d)) * water_through(start, d);
            }
            break;
        }
        float3 albedo;
        float3 emission;
        reference_material(h.tri, h.b, albedo, emission);
        const float3 v0 = asfloat(g_positions.Load3(h.tri * 36u));
        const float3 v1 = asfloat(g_positions.Load3(h.tri * 36u + 12u));
        const float3 v2 = asfloat(g_positions.Load3(h.tri * 36u + 24u));
        const float3 face = cross(v1 - v0, v2 - v0);
        if (dot(face, face) <= 1e-24f || !any(albedo > 0.0f)) {
            break;
        }
        ng = normalize(face);
        ng = dot(ng, d) > 0.0f ? -ng : ng;
        const float3 w = float3(1.0f - h.b.x - h.b.y, h.b.x, h.b.y);
        p = v0 * w.x + v1 * w.y + v2 * w.z;
        smooth_surface(h.tri, w, v0, v1, v2, ng, n, p);
        throughput *= albedo;
        if (bounce >= 2u) {
            const float keep = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05f, 0.95f);
            if (emit_random(seed) >= keep) {
                break;
            }
            throughput /= keep;
        }
    }
    return total;
}

// Adds one path a pixel to the reference's sums (16 bytes a pixel: rgb, paths); the first frame starts them over and
// clears the comparison's record (g_final after the probe's two).
[numthreads(8, 8, 1)]
void EngineReferenceCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size) || (g_engine_info.x & kEngineReference) == 0u) {
        return;
    }
    if (all(id.xy == 0u)) {
        g_final[g_sample_size.x * g_sample_size.y + kStarSlots + 2u] = uint4(0u, 0u, 0u, 0u);
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id.xy);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    RWByteAddressBuffer sums = ResourceDescriptorHeap[asuint(g_engine_params.x)];
    float4 sum = g_engine_params.y > 0.0f ? asfloat(sums.Load4(pixel * 16u)) : float4(0.0f, 0.0f, 0.0f, 0.0f);
    if (s.valid && (s.flags & (kGbEmitter | kGbClear)) == 0u) {
        uint seed = emit_hash(pixel ^ (g_frame * 0x2545F491u) ^ 0x7F4A7C15u);
        const uint salt = pixel * 32u + g_frame * 0x632BE5ABu + 29u;
        sum.rgb += reference_path(s.p, s.n, s.ng, (s.flags & kGbCard) != 0u, seed, salt);
        sum.w += 1.0f;
    }
    sums.Store4(pixel * 16u, asuint(sum));
}

// The slot's result at the camera's exposure (the composite and the sub-frames read it), and the samples' export in
// the lighting's own result format (light texture for translucent draws, the eye's adaptation, the reflections).
[numthreads(8, 8, 1)]
void EngineFinalCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id.xy);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    const float exposure = physical_exposure();
    float3 diffuse = float3(0.0f, 0.0f, 0.0f);
    float3 specular = float3(0.0f, 0.0f, 0.0f);
    // The whole diffuse light reaching the surface for the samples' readers (the eye's adaptation, the light texture of
    // the translucent draws, the water's reflections), whatever the view and the surface's metalness.
    float3 whole = float3(0.0f, 0.0f, 0.0f);
    float z = 0.0f;
    // GALAXY_RT_ENGINE_REFERENCE: the pixel's diffuse light (m) against the reference's (luminance: reference, engine,
    // absolute difference, 1), on the ordinary non-metal surfaces.
    float4 compare = float4(0.0f, 0.0f, 0.0f, 0.0f);
    // GALAXY_RT_LIGHT_METER: on an ordinary surface in the meter's disc, the light sources' direct light alone (this
    // frame's, unfiltered), the whole diffuse light as the picture takes it (filtered, the sun's by its visibility) and
    // the sun's alone (m, the stage's light units: a white surface under its full light reads 1), 1.
    float4 meter = float4(0.0f, 0.0f, 0.0f, 0.0f);
    // And this frame's indirect light (unfiltered) and the filtered light less the sun's (the lights' and the bounce's
    // as the history has them).
    float2 meter_split = float2(0.0f, 0.0f);
    // And, with the raster's motion, where it takes this pixel against the camera's reprojection (pixels: x, y, the
    // distance; 1) and whether the histories follow it (engine_history: more than half a pixel off).
    float4 meter_motion = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float meter_moved = 0.0f;
    // And the diffuse history's length there (frames: 1 where the surface's history was lost).
    float meter_history = 0.0f;
    if (s.valid) {
        z = s.p.z;
        if ((s.flags & kGbEmitter) != 0u) {
            // A light source shows what it sends: its emitted colour (the G-buffer's: the draw's TEV output at full
            // light, the table's for a recoloured one) times its radiance, at most kEmitterShown, rather than the
            // composite's albedo (the picture's colour less its painted reflection), of which an environment-mapped
            // lamp keeps almost nothing.
            specular = s.albedo * (min(s.emission, kEmitterShown) * exposure);
            whole = specular;
        } else if ((s.flags & kGbClear) != 0u) {
            // A clear solid shows the scene behind it as the game refracted it (its picture is the lit, exposed scene
            // through it), passed at 1 - F; lighting it again at its own surface would darken it to black. Its
            // reflection is the traced specular's.
            const uint4 a = engine_buffer((g_engine_info.w & 1u) == 0u ? kEvAtrousA : kEvAtrousB).Load4(pixel * 16u);
            float3 sun_glint;
            sun_light(s, pixel, f16tof32(engine_buffer(kEvShadow).Load(pixel * 4u)), sun_glint);
            // View 3 (the traced specular alone): its reflection without what shows through.
            diffuse = ((g_engine_info.x & kEngineView) >> 4u) == 3u ? float3(0.0f, 0.0f, 0.0f) : (1.0f - kClearF0).xxx;
            specular = (float3(f16tof32(a.z), f16tof32(a.z >> 16u), f16tof32(a.w)) + sun_glint) * exposure;
            whole = diffuse;
        } else {
            const uint view = (g_engine_info.x & kEngineView) >> 4u;
            const uint4 a = engine_buffer((g_engine_info.w & 1u) == 0u ? kEvAtrousA : kEvAtrousB).Load4(pixel * 16u);
            // The sun's light by its filtered visibility; views 1 and 4 (the noisy light) take its ray's.
            float penumbra;
            const float ray = shadow_load(0u, pixel, penumbra);
            const float visible = f16tof32(engine_buffer(kEvShadow).Load(pixel * 4u));
            float3 sun_glint;
            const float3 sun = sun_light(s, pixel, view == 1u || view == 4u ? ray : visible, sun_glint);
            diffuse = float3(f16tof32(a.x), f16tof32(a.x >> 16u), f16tof32(a.y)) + sun;
            specular = float3(f16tof32(a.z), f16tof32(a.z >> 16u), f16tof32(a.w)) + sun_glint;
            whole = diffuse * exposure;
            if (g_probe.w != 0u) {
                const float2 centre = float2(g_probe.w & 0xFFFu, (g_probe.w >> 12u) & 0xFFFu);
                const float radius = float(g_probe.w >> 24u) * 4.0f;
                if (distance(float2(id.xy) + 0.5f, centre) <= radius) {
                    const uint4 dv = engine_buffer(kEvDirect).Load4(pixel * 16u);
                    meter = float4(luminance(float3(f16tof32(dv.x), f16tof32(dv.x >> 16u), f16tof32(dv.y))),
                                   luminance(diffuse), luminance(sun), 1.0f);
                    meter_split = float2(luminance(gi_upsampled(s, q)), luminance(diffuse - sun));
                    meter_history = f16tof32(engine_buffer(kEvHistDiffuse).Load(pixel * 16u + 4u) >> 16u);
                    if ((g_engine_info.x & kEngineMotion) != 0u) {
                        const Texture2D<float2> motion = ResourceDescriptorHeap[engine_view(kEvMotion)];
                        const float2 d = float2(q) + 0.5f + motion.Load(int3(q, 0)) + g_motion_jitter -
                                         eye_to_pixel(g_history_projection, g_viewport, transform(g_reproject, s.p));
                        meter_motion = float4(d, length(d), 1.0f);
                        meter_moved = length(d) > 0.5f ? 1.0f : 0.0f;
                    }
                }
            }
            if ((g_engine_info.x & kEngineReference) != 0u) {
                RWByteAddressBuffer sums = ResourceDescriptorHeap[asuint(g_engine_params.x)];
                const float4 sum = asfloat(sums.Load4(pixel * 16u));
                if (sum.w > 0.0f) {
                    const float3 reference = sum.rgb / sum.w;
                    if (s.metallic < 0.5f && (s.flags & kGbGlow) == 0u) {
                        const float r = luminance(reference);
                        const float e = luminance(diffuse);
                        compare = float4(r, e, abs(e - r), 1.0f);
                    }
                    // View 8: the reference's light in the engine's place.
                    if (view == 8u) {
                        diffuse = reference;
                    }
                }
            }
            if (view == 1u || view == 2u || view == 4u) {
                const uint4 dv = engine_buffer(kEvDirect).Load4(pixel * 16u);
                const float3 direct = float3(f16tof32(dv.x), f16tof32(dv.x >> 16u), f16tof32(dv.y)) + sun;
                diffuse = view == 1u ? direct : view == 2u ? gi_upsampled(s, q) : direct + gi_upsampled(s, q);
                specular = float3(0.0f, 0.0f, 0.0f);
            } else if (view == 3u) {
                diffuse = float3(0.0f, 0.0f, 0.0f);
            } else if (view == 14u) {
                // View 14: the light sources' direct light alone (the emitters and the game's point lights; no sun, no
                // bounce): what each light pours onto its surroundings.
                const uint4 dv = engine_buffer(kEvDirect).Load4(pixel * 16u);
                diffuse = float3(f16tof32(dv.x), f16tof32(dv.x >> 16u), f16tof32(dv.y));
                specular = float3(0.0f, 0.0f, 0.0f);
            }
            // View 15: the raster's motion less the camera's (red x, green y; 0.5 none, 8 pixels to full): still
            // surfaces read 0.5 where the two agree in direction and scale, what moved by itself off it; blue where the
            // frame has no motion.
            if (view == 15u) {
                const float2 camera = eye_to_pixel(g_history_projection, g_viewport, transform(g_reproject, s.p));
                float3 shown = float3(0.0f, 0.0f, 1.0f);
                if ((g_engine_info.x & kEngineMotion) != 0u) {
                    const Texture2D<float2> motion = ResourceDescriptorHeap[engine_view(kEvMotion)];
                    shown = float3(saturate(0.5f + (float2(q) + 0.5f + motion.Load(int3(q, 0)) + g_motion_jitter - camera) / 16.0f),
                                   0.0f);
                }
                diffuse = float3(0.0f, 0.0f, 0.0f);
                specular = srgb_to_linear(shown) / max(exposure, 1e-6f);
            }
            // Views 11 to 13: the sun's shadow, unlit (the filtered visibility, its ray's, the penumbra's radius on
            // screen over kShadowRadiusMost; facing away black).
            if (view >= 11u && view <= 13u) {
                const float radius = penumbra > 0.0f && penumbra < kShadowLit
                                         ? min(penumbra / pixel_world_size(s.p.z), kShadowRadiusMost) / kShadowRadiusMost
                                         : 0.0f;
                diffuse = float3(0.0f, 0.0f, 0.0f);
                specular = (view == 11u ? visible : view == 12u ? ray : radius).xxx * (0.5f / max(exposure, 1e-6f));
            }
            // A metal reflects all its light: none of it is diffuse. Without traced reflections its colour is the
            // game's painted reflection (the composite keeps it in the albedo), lit as any surface.
            diffuse *= ((g_engine_info.x & kEngineSpecular) != 0u ? 1.0f - s.metallic : 1.0f) * exposure;
            specular *= exposure;
            if ((s.flags & kGbGlow) != 0u) {
                // Its glow over its lit colour, in the colours the game paints it (the picture's: a coin's gold
                // gradient and its embossed ?), in the specular term (the composite does not take it times the albedo,
                // which for a metal is its painted reflection's remainder). In the metal's reflectance alone, one
                // colour, a coin would read as a flat disc.
                const Texture2D<float4> painted = ResourceDescriptorHeap[engine_view(kEvPicture)];
                specular += srgb_to_linear(saturate(painted.Load(int3(q, 0)).rgb)) * (s.emission * exposure);
            }
        }
        if (((g_engine_info.x & kEngineView) >> 4u) == 7u) {
            diffuse = float3(1.0f, 1.0f, 1.0f);
            specular = float3(0.0f, 0.0f, 0.0f);
        }
        // Views 9 and 10: the texture coordinate the table's glowing region is tested at, u (9) or v (10), as one of 16
        // hues (bin i at hue i / 16; the tone curve keeps hue, not level), black where the hit has none: for a region
        // separating parts of one texture.
        if (((g_engine_info.x & kEngineView) >> 4u) == 9u || ((g_engine_info.x & kEngineView) >> 4u) == 10u) {
            const uint4 hit = engine_buffer(kEvGBuffer).Load4(pixel * 32u + 16u);
            float2 uv = float2(-1.0f, -1.0f);
            if (hit.z != 0u && s.draw != 0xFFFFu) {
                uv = tev_region_uv(s.draw, g_tev_info.x, hit.z - 1u, float2(f16tof32(hit.w), f16tof32(hit.w >> 16u)));
            }
            diffuse = float3(0.0f, 0.0f, 0.0f);
            const float t = ((g_engine_info.x & kEngineView) >> 4u) == 9u ? uv.x : uv.y;
            const float h = floor(saturate(t) * 15.999f) / 16.0f;
            const float3 hue = saturate(float3(abs(h * 6.0f - 3.0f) - 1.0f, 2.0f - abs(h * 6.0f - 2.0f), 2.0f - abs(h * 6.0f - 4.0f)));
            specular = uv.x >= 0.0f ? hue * 0.3f : float3(0.0f, 0.0f, 0.0f);
        }
    }
    if (g_probe.w != 0u) {
        // Summed over the disc (fixed point, 1/256): the meter's record ([rt-light-meter]).
        const float4 total = WaveActiveSum(meter);
        const float2 split = WaveActiveSum(meter_split);
        const float4 moving = WaveActiveSum(meter_motion);
        const float moved = WaveActiveSum(meter_moved);
        const float history = WaveActiveSum(meter_history);
        if (WaveIsFirstLane() && total.w > 0.0f) {
            const uint at = g_sample_size.x * g_sample_size.y + kStarSlots + 4u;
            InterlockedAdd(g_final[at].x, uint(round(total.x * 256.0f)));
            InterlockedAdd(g_final[at].y, uint(round(total.y * 256.0f)));
            InterlockedAdd(g_final[at].z, uint(round(total.z * 256.0f)));
            InterlockedAdd(g_final[at].w, uint(total.w));
            InterlockedAdd(g_final[at + 1u].x, uint(round(split.x * 256.0f)));
            InterlockedAdd(g_final[at + 1u].y, uint(round(split.y * 256.0f)));
            InterlockedAdd(g_final[at + 1u].w, uint(round(history * 16.0f)));
            if (moving.w > 0.0f) {
                // Signed sums (two's complement).
                InterlockedAdd(g_final[at + 1u].z, uint(moved));
                InterlockedAdd(g_final[at + 2u].x, asuint(int(round(moving.x * 256.0f))));
                InterlockedAdd(g_final[at + 2u].y, asuint(int(round(moving.y * 256.0f))));
                InterlockedAdd(g_final[at + 2u].z, uint(round(moving.z * 256.0f)));
                InterlockedAdd(g_final[at + 2u].w, uint(moving.w));
            }
        }
    }
    if ((g_engine_info.x & kEngineReference) != 0u) {
        // Summed over the picture (fixed point, 1/64): the comparison's record ([rt-reference-engine]).
        const float4 total = WaveActiveSum(compare);
        if (WaveIsFirstLane() && total.w > 0.0f) {
            const uint at = g_sample_size.x * g_sample_size.y + kStarSlots + 2u;
            InterlockedAdd(g_final[at].x, uint(round(total.x * 64.0f)));
            InterlockedAdd(g_final[at].y, uint(round(total.y * 64.0f)));
            InterlockedAdd(g_final[at].z, uint(round(total.z * 64.0f)));
            InterlockedAdd(g_final[at].w, uint(total.w));
        }
    }
    diffuse = safe_light(diffuse);
    specular = safe_light(specular);
    engine_buffer(kEvSlot).Store4(pixel * 16u, uint4(pack_half2(diffuse.r, diffuse.g), pack_half2(diffuse.b, specular.r),
                                                     pack_half2(specular.g, specular.b), asuint(z)));
    // The samples (g_factor apart): the lighting's result for its other readers.
    if ((id.x % g_factor) == 0u && (id.y % g_factor) == 0u) {
        const uint2 sp = id.xy / g_factor;
        if (all(sp < g_sample_size)) {
            const uint sample = sp.y * g_sample_size.x + sp.x;
            const float3 m = min(safe_light(whole), kMaxPhysicalLight);
            g_final[sample] = uint4(pack_half2(m.r, m.g), f32tof16(m.b) | (f32tof16(1.0f) << 16u), asuint(z), 0u);
            g_z[sample] = z;
        }
    }
}
