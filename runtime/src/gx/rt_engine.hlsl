// SPDX-License-Identifier: GPL-3.0-only
// The ray tracing engine (docs/RAY_TRACING_ENGINE.md): the traced scene's light in radiance, from emitting geometry,
// the sun and the sky alone. cs_6_6 with RT_TEXTURED: the lighting's scene bindings and helpers (rt_lighting.hlsl,
// included) and the engine's own buffers by descriptor-heap index (g_engine_views, ray_tracing_engine.cpp).
//
// Per frame: EngineGBufferCS (full resolution) -> EngineDirectCS (the sun; ReSTIR DI candidates and temporal reuse) ->
// EngineDirectSpatialCS (spatial reuse, the shadow ray) -> EngineIndirectCS (half resolution: a cosine ray, its hit lit
// by the sun, the emitters and the sky; ReSTIR GI temporal reuse) -> EngineIndirectSpatialCS -> EngineSpecularCS (GGX)
// -> EngineTemporalCS (accumulation with moments) -> EngineAtrousCS x4 (variance-guided a-trous) -> EngineFinalCS
// (the slot's result at the camera's exposure; the samples' export for the translucent draws and the eye).
//
// Units: diffuse light as the lighting's `m` (irradiance over pi: a white surface shows m), specular as radiance; both
// unexposed in the histories, at the exposure in the slot's result.

#include "rt_lighting.hlsl"

// g_engine_views (flattened): the engine's buffers (raw, 16-byte records).
static const uint kEvGBuffer = 0u;        // per pixel two uint4: this frame's G-buffer
static const uint kEvGBufferPrev = 1u;    // the last traced frame's
static const uint kEvDiTemp = 2u;         // per pixel two uint4: the direct reservoir after temporal reuse
static const uint kEvDi = 3u;             // per pixel two uint4: this frame's final direct reservoir
static const uint kEvDiPrev = 4u;         // the last frame's
static const uint kEvDirect = 5u;         // per pixel uint4: direct diffuse (m) rgb, direct specular rgb (halves)
static const uint kEvGiTemp = 6u;         // per half-pixel two uint4: the indirect reservoir after temporal reuse
static const uint kEvGi = 7u;             // this frame's temporal indirect reservoir (the next frame's history)
static const uint kEvGiPrev = 8u;         // the last frame's
static const uint kEvGiOut = 9u;          // per half-pixel uint2: indirect diffuse (m) rgb (halves), valid
static const uint kEvSpecular = 10u;      // per pixel uint2: traced specular rgb (halves), hit distance
static const uint kEvHistDiffuse = 11u;   // per pixel uint4: diffuse rgb, history length (halves); moments 1, 2
static const uint kEvHistDiffusePrev = 12u;
static const uint kEvHistSpecular = 13u;
static const uint kEvHistSpecularPrev = 14u;
static const uint kEvAtrousA = 15u;       // per pixel uint4: diffuse rgb + variance, specular rgb + variance (halves)
static const uint kEvAtrousB = 16u;
static const uint kEvSlot = 17u;          // per pixel uint4: this slot's diffuse rgb, specular rgb (halves, exposed), z
static const uint kEvPicture = 18u;       // the EFB's colour (Texture2D, sRGB-encoded)
// The clear-surface pass (EngineClearCS, at the scene's end):
static const uint kEvWaterScene = 19u;    // the water window's acceleration structure (glass, crystal, water)
static const uint kEvWaterPositions = 20u;  // its triangles' eye-space vertices (36 bytes)
static const uint kEvWaterSurfaces = 21u;   // their vertex normals and draw flags (16 bytes; draw in bits 16-31)
static const uint kEvClearDraws = 22u;    // per water draw: the table's roughness (unorm16), kClear* flags
static const uint kEvClear = 23u;         // per pixel uint4: reflection rgb (halves, exposed), transmittance, eye z

static const uint kEngineOn = 1u;         // g_engine_info.x
static const uint kEngineSpecular = 2u;   // traced specular
static const uint kEngineIndirect = 4u;   // traced indirect light
static const uint kEngineView = 0xF0u;    // GALAXY_RT_ENGINE_VIEW (bits 4-7): 1 direct, 2 indirect, 3 specular, 4 noisy,
                                          // 5 the clear surfaces found (magenta), 6 the clear composite's coverage,
                                          // 7 the composite's albedo (unlit)
static const uint kEnginePoints = 256u;   // the game's point lights and the star core (GALAXY_RT_ENGINE_POINTS)
static const uint kEngineClearFind = 512u;  // GALAXY_RT_ENGINE_CLEAR=2: the clear pass finds, reflects nothing
static const uint kEngineClearNoRay = 1024u;  // =3: no reflection ray
static const uint kEngineClearNoSun = 2048u;  // =4: no sun highlight
static const uint kEngineClearNoBack = 4096u; // =5: no far wall
static const uint kEngineClearNoShade = 8192u; // =6: the reflection ray, its hit unlit
static const uint kEngineHitNoLights = 16384u;  // =7: hits without the lights' sample
static const uint kEngineHitNoSky = 32768u;     // =8: hits without the sky ray
static const uint kEngineClearNoTev = 65536u;   // =9: glass reflection hits without their TEV colour
static const uint kEngineBounceCache = 131072u;  // GALAXY_RT_ENGINE_BOUNCES (default on): hits take the cached light

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
// reaches the scene whole; seen at that radiance its surface went white on the tone curve (star bits, coins: owner,
// washed out). The display range, not the light, is limited.
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
// full colour the shade under it went navy and the scene's whites violet (Beach Bowl's sand, Hurry-Scurry's
// checkerboard, Space Junk's metal: owner, colour loss). Reflections keep the backdrop as it shows (a mirror shows
// the picture of the sky).
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

float3 unpack_half3(uint2 v)
{
    return float3(f16tof32(v.x), f16tof32(v.x >> 16u), f16tof32(v.y));
}

float3 safe_light(float3 c)
{
    return all(c == c) && all(c < 6.0e4f) ? max(c, 0.0f) : float3(0.0f, 0.0f, 0.0f);
}

// --- G-buffer -----------------------------------------------------------------------------------------------------

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
    s.emission = asfloat(w1.y);
    return s;
}

// The full-resolution G-buffer, and the composite's per-pixel normal and de-lighting (g_pixels, as GBufferCS).
[numthreads(8, 8, 1)]
void EngineGBufferCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const int2 q = int2(id.xy);
    const uint pixel = id.y * g_target_size.x + id.x;
    float3 p;
    uint2 packed = uint2(0u, 0u);
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
            if (delight_surface(tri, bary, p, s.ng, length(next - p), bent, shading)) {
                packed.y = pack_pixel_delight(bent, shading);
                s.n = bent;
                delit = true;
            }
            const uint draw_flags = g_surfaces.Load(tri * 16u + 12u);
            const uint draw = draw_flags >> 16u;
            s.draw = draw;
            // A clear surface (crystal, glass, gems) is drawn over or refracting what lies behind it: its pixel is not
            // its TEV's output, so the painted reflection's share of that output is not its share of the pixel (the
            // share darkened Melty Molten's crystal cages and the star chips inside them to black). It keeps the
            // game's colour; the traced specular adds its Fresnel reflection.
            const bool clear = ((draw_flags >> 8u) & 15u) == kClearClass;
            s.flags |= clear ? kGbClear : 0u;
            if (!clear && draw < g_tev_info.x && (g_tev.Load(draw * kTevRecordBytes) & 0x80u) != 0u) {
                // The game's painted reflection (environment maps): the traced specular takes its place.
                bool pass;
                const float3 full = srgb_to_linear(saturate(tev_evaluate_mode(draw, g_tev_info.x, tri, bary, 0u, pass).rgb));
                const float3 bare =
                    srgb_to_linear(saturate(tev_evaluate_mode(draw, g_tev_info.x, tri, bary, kTevNoEnvironment, pass).rgb));
                // A luminance ratio: the tint a coloured metal or glass carries in its environment stage stays (per
                // channel, b72-b77, Bubble Blast's gold star turned silver and its green cages grey).
                own = luminance(full) > 1e-4f ? saturate(luminance(bare) / luminance(full)) : 1.0f;
                const float3 white =
                    srgb_to_linear(saturate(tev_evaluate_mode(draw, g_tev_info.x, tri, bary, kTevWhiteEnvironment, pass).rgb));
                // A metal's specular colour at the strength its environment stage passes the environment (white less
                // bare), at least its own colour's: its hue where that colour is a coloured metal's (gold, copper:
                // saturated in linear light, as their measured reflectance: gold's blue a third of its red), neutral
                // where it is a grey metal's warm or cool cast (silver, iron); chrome (bare near black) as the stage
                // passes it. The bare colour alone was a diffuse colour, dark (b80: Bubble Blast's gold star olive);
                // white less bare alone lost gold's hue (silver, b77); every hue kept turned Battlerock's grey metal
                // gold (b81).
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
            // The table's roughness and metalness (unorm16 x 65534; 0xFFFF: no row: a matte non-metal).
            const uint pbr = g_draws.Load(record + 16u);
            s.roughness = (pbr & 0xFFFFu) == 0xFFFFu ? 0.7f : max(float(pbr & 0xFFFFu) / 65534.0f, 0.02f);
            s.metallic = (pbr >> 16u) == 0xFFFFu ? 0.0f : saturate(float(pbr >> 16u) / 65534.0f);
            if ((g_draws.Load(record + 24u) & kDrawTableFoliage) != 0u) {
                s.flags |= kGbCard;
            }
            if ((table.x & kEmitNamed) != 0u && (table.x & 3u) != 0u && (table.x & kEmitNoLight) == 0u &&
                tev_glows_at(draw, g_tev_info.x, tri, bary)) {
                const float intensity = asfloat(table.y);
                if (intensity < 1.0f && (g_draws.Load(record + 24u) & kDrawTableStar) == 0u) {
                    // A faintly glowing material (intensity below 1: coins, star chips, Boos, the garden's flowers)
                    // is a surface: lit, shaded and reflecting as any other, its glow (its colour times the
                    // intensity) added. Shown as a light source at its colour, a coin was a flat sticker (owner: the
                    // giant ? coin, b82), too bright in a dark stage and dull in a bright one.
                    s.flags |= kGbGlow;
                    s.emission = intensity;
                } else {
                    // A light source shows what it sends: its colour times the table's intensity; an atlas only in
                    // the table's glowing region of its texture (a candle stand's flame, not its wood).
                    s.flags |= kGbEmitter;
                    s.emission = max(intensity, 1.0f);
                    // Its colour the radiance its light carries (EmitTriangleCS, reference_material: the draw's TEV
                    // output), not the game's shading of it in the EFB (Bowser's Star Reactor's fireballs: dark discs).
                    if (draw < g_tev_info.x) {
                        bool pass;
                        emitted = srgb_to_linear(saturate(tev_evaluate(draw, g_tev_info.x, tri, bary, pass).rgb));
                    }
                }
            }
        }
        // The texel's albedo: the EFB's colour, the light painted into it and its painted reflection taken out.
        const Texture2D<float4> picture = ResourceDescriptorHeap[engine_view(kEvPicture)];
        const float3 color = srgb_to_linear(saturate(picture.Load(int3(q, 0)).rgb));
        // Past 1 it keeps its hue (clipping each channel turned the de-lit gold of Dusty Dune's sand peach).
        const float3 lifted = max(color * (delit ? exp(-shading) : 1.0f) * own, 0.0f);
        s.albedo = lifted / max(max(lifted.r, max(lifted.g, lifted.b)), 1.0f);
        if (emitted.x >= 0.0f) {
            s.albedo = emitted;
        }
        // A metal of a colour no metal has is paint (or a dye coat) over metal: a dielectric. Measured metal
        // reflectances are greyish (silver, aluminium, iron, chrome) or warm (copper, gold, brass: hue about 8 to 60
        // degrees); a strongly teal, blue, green, olive, violet or pure red one reflects as paint does (its colour its
        // albedo, a clear coat at F0 0.04 over it). As a metal its whole colour was its environment's: Space Junk's
        // teal platform turned the nebula's violet, the Airship Armada's olive hull the backdrop's navy (b81-b83).
        // The coat is a lacquer's (kPaintRoughness): at the metal's own roughness Flipswitch's red spiked blocks
        // showed no shine (owner).
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
        }
        packed.x = pack_pixel_normal(surface.n, own);
        w0 = gb_word0(s);
        w1 = uint4(asuint(p.z), asuint(s.emission), surface.primary.x, surface.primary.y);
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

// --- BRDF ---------------------------------------------------------------------------------------------------------

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

// The specular BRDF times n.l towards l (radiance per unit irradiance arriving along l).
float3 specular_brdf_nl(GSurface s, float3 v, float3 l)
{
    const float nl = dot(s.n, l);
    const float nv = dot(s.n, v);
    if (nl <= 0.0f || nv <= 0.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float3 h = normalize(v + l);
    const float alpha = s.roughness * s.roughness;
    return fresnel_schlick(specular_f0(s), dot(v, h)) * (ggx_d(alpha, dot(s.n, h)) * smith_visibility(alpha, nl, nv) * nl);
}

// The GGX lobe's alpha for the sun's highlight: at least the sun's disc (g_sun.w, the tangent of its radius) wide. A
// polished surface reflects the disc, not a point; the lobe at its own alpha met the sampled direction in a spike.
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

// --- Visibility and the light at a ray's hit ----------------------------------------------------------------------

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
        // A clear solid (its draws carry no shadow for the lighting before the engine) stops kClearShadow of the
        // light (b81: crystals cast none).
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

// A direction within the sun's disc (g_sun: towards it, tangent of its radius).
float3 sun_direction_sample(float2 u)
{
    const float3x3 f = tangent_frame(g_sun.xyz);
    const float r = sqrt(u.x) * g_sun.w;
    const float phi = 2.0f * kPi * u.y;
    return normalize(g_sun.xyz + f[0] * (r * cos(phi)) + f[1] * (r * sin(phi)));
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
        if (late_hit || card_blocks(query.CandidatePrimitiveIndex(), hit,
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

// --- The lights: emitting triangles, the game's point lights, the star core -----------------------------------------

struct LightSample {
    float3 y;          // eye position on the emitter (a sphere light's: on its shadow sphere)
    float3 face;       // its face's normal (eye; a sphere light's: outward)
    float3 radiance;   // a triangle's radiance; a sphere light's L h^2 / (2 pi R^2)
    float beam;        // a triangle's beam exponent (0: Lambertian); a sphere light's h
    float sides;       // a triangle's facing (emitter_facing); kSidesSphere: a sphere light
    float area;        // a triangle's area (its near field: engine_light_target)
};

// The game's point lights (g_points: its lamps, the Octoombas' bulbs; the old lighting's g_point_light weight) and the
// Comet Observatory's star core light a surface as the game's falloff has it: colour x weight x n.l x h^2 / (h^2 + d^2)
// (h the light's half distance; the on-axis light of a glowing disc of radius h and radiance colour x weight), what
// the lighting before the engine showed. Their shadows are traced to a point on the hemisphere of a sphere of the
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

// One light point for the surface at `p`, drawn in proportion to power over the emitting triangles (`tris`, their
// total power) and the point lights (`spheres`): a uniform point on the triangle, or on the sphere's hemisphere facing
// `p`. `chance`: its probability per unit area. False when none.
bool engine_light_pick(float3 p, float tris, float spheres, inout uint seed, out LightSample l, out float chance)
{
    l.y = float3(0.0f, 0.0f, 0.0f);
    l.face = float3(0.0f, 0.0f, 1.0f);
    l.radiance = float3(0.0f, 0.0f, 0.0f);
    l.beam = 0.0f;
    l.sides = 0.0f;
    l.area = 0.0f;
    chance = 0.0f;
    const float total = tris + spheres;
    const float u = emit_random(seed) * total;
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
        if (!(d.w > 0.0f) || !(a.w > 0.0f)) {
            return false;
        }
        float r1 = emit_random(seed);
        float r2 = emit_random(seed);
        if (r1 + r2 > 1.0f) {
            r1 = 1.0f - r1;
            r2 = 1.0f - r2;
        }
        l.y = a.xyz + b.xyz * r1 + c.xyz * r2;
        l.area = a.w;
        l.face = normalize(cross(b.xyz, c.xyz));
        l.radiance = d.rgb;
        l.beam = c.w;
        l.sides = b.w;
        chance = d.w / (total * a.w);
        return true;
    }
    float running = tris;
    uint i = 0u;
    [loop] for (; i + 1u < g_point_count; ++i) {
        running += sphere_power(i);
        if (u < running) {
            break;
        }
    }
    const float power = sphere_power(i);
    if (!(power > 0.0f)) {
        return false;
    }
    const float3 centre = g_points[2u * i].xyz;
    const float r = max(g_point_radius[i >> 2u][i & 3u], 1.0f);
    const float3 axis = normalize(p - centre + float3(0.0f, 0.0f, 1e-4f));
    // Uniform on the hemisphere about `axis`.
    const float z = emit_random(seed);
    const float phi = 2.0f * kPi * emit_random(seed);
    const float s = sqrt(max(0.0f, 1.0f - z * z));
    const float3x3 f = tangent_frame(axis);
    l.face = f[0] * (s * cos(phi)) + f[1] * (s * sin(phi)) + axis * z;
    l.y = centre + l.face * r;
    const float area = 2.0f * kPi * r * r;
    const float h2 = 1.0f / max(g_points[2u * i + 1u].w, 1e-12f);
    l.radiance = sphere_colour(i) * (h2 / area);
    l.beam = sqrt(h2);
    l.sides = kSidesSphere;
    chance = power / (total * area);
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

// The lights at `p` (normal `n`; shadow ray from `origin`): `candidates` drawn by engine_light_pick, one kept in
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
        if (!engine_light_pick(p, tris, spheres, seed, l, chance)) {
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

// The radiance a hit sends back along -direction: its emission (when `emission`) plus its albedo times its light: the
// sun behind a shadow ray, the lights by resampled importance behind a shadow ray, the sky along one cosine ray.
// Out: the hit's world-facing normal (for ReSTIR GI).
float3 engine_hit_radiance(Hit h, float3 origin, float3 direction, bool emission_wanted, inout uint seed, uint salt,
                           out float3 x, out float3 hn)
{
    x = origin + direction * h.t;
    hn = -direction;
    if (!h.surface) {
        // Seen (a reflection): the backdrop as it shows; as light (the indirect light): its light's colour.
        return h.late ? float3(0.0f, 0.0f, 0.0f)
                      : emission_wanted ? physical_sky(direction) : sky_light_colour(physical_sky(direction));
    }
    float3 albedo;
    float3 emission;
    reference_material(h.tri, h.b, albedo, emission);
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
    // in place of the direct light alone. One bounce left shade lit only by the sky that reaches it: Bowser's Star
    // Reactor's walls near black, Beach Bowl's shaded sand navy (b81 against the game's picture). Light sources keep
    // their own emission; the series converges as each bounce takes the albedo (below 1).
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
                    return albedo * max(whole, 0.0f) / max(physical_exposure(), 1e-4f);
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
                m += g_sun_light.rgb * nl;
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
                m += sky_light_colour(physical_sky(d));
            }
        }
    }
    return albedo * m + (emission_wanted ? emission : float3(0.0f, 0.0f, 0.0f));
}

// --- ReSTIR DI (the lights) ---------------------------------------------------------------------------------------

struct Reservoir {
    LightSample s;
    float w_sum;
    float m;
    float target;
    float W;
};

Reservoir reservoir_empty()
{
    Reservoir r;
    r.s.y = float3(0.0f, 0.0f, 0.0f);
    r.s.face = float3(0.0f, 0.0f, 1.0f);
    r.s.radiance = float3(0.0f, 0.0f, 0.0f);
    r.s.beam = 0.0f;
    r.s.sides = 0.0f;
    r.w_sum = 0.0f;
    r.m = 0.0f;
    r.target = 0.0f;
    r.W = 0.0f;
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

// A light triangle's area in 14 bits: log2 in 1/256 steps from 2^-16 (0: none), to 2^47.
uint area_code(float area)
{
    return area > 0.0f ? uint(clamp(round((log2(area) + 16.0f) * 256.0f), 1.0f, 16383.0f)) : 0u;
}

float area_decode(uint code)
{
    return code != 0u ? exp2(float(code) / 256.0f - 16.0f) : 0.0f;
}

void reservoir_store(uint view, uint index, Reservoir r)
{
    RWByteAddressBuffer b = ResourceDescriptorHeap[view];
    b.Store4(index * 32u, uint4(asuint(eye_point_to_world(r.s.y)), asuint(r.W)));
    b.Store4(index * 32u + 16u,
             uint4(pack_normal(normalize(eye_direction_to_world(r.s.face))), pack_half2(r.s.radiance.r, r.s.radiance.g),
                   f32tof16(r.s.radiance.b) | (f32tof16(min(r.m, 60000.0f)) << 16u),
                   min(uint(r.s.beam), 0xFFFFu) | (min(uint(r.s.sides + 0.5f), 3u) << 16u) |
                       (area_code(r.s.area) << 18u)));
}

Reservoir reservoir_load(uint view, uint index)
{
    RWByteAddressBuffer b = ResourceDescriptorHeap[view];
    const uint4 r0 = b.Load4(index * 32u);
    const uint4 r1 = b.Load4(index * 32u + 16u);
    Reservoir r = reservoir_empty();
    r.s.y = transform(g_to_eye, asfloat(r0.xyz));
    r.s.face = normalize(rotate3(g_to_eye, unpack_normal(r1.x)));
    r.s.radiance = float3(f16tof32(r1.y), f16tof32(r1.y >> 16u), f16tof32(r1.z));
    // Its area kept: reused without it, a triangle's near field (engine_light_target) went unbounded next to the light.
    r.s.beam = float(r1.w & 0xFFFFu);
    r.s.sides = float((r1.w >> 16u) & 3u);
    r.s.area = area_decode(r1.w >> 18u);
    r.W = asfloat(r0.w);
    r.m = f16tof32(r1.z >> 16u);
    return r;
}

float light_target(GSurface s, LightSample l, out float3 light)
{
    return engine_light_target(s.p, s.n, l, light);
}

// The previous frame's pixel of an eye-space point (full resolution), and whether the surface there is this one (its
// depth and normal agree: draw indices are positions in each frame's own list, not identities).
bool engine_reproject(float3 p, float3 n, out uint prev_pixel)
{
    prev_pixel = 0u;
    if ((g_flags & 1u) == 0u) {
        return false;
    }
    const float3 previous = transform(g_reproject, p);
    if (previous.z >= -g_near) {
        return false;
    }
    const float2 a = eye_to_pixel(g_history_projection, g_viewport, previous);
    const int2 s = int2(floor(a));
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
    const float z = asfloat(w1.x);
    const float3 n_prev = normalize(rotate3(g_reproject, n));
    return abs(z - previous.z) < 0.02f * abs(previous.z) + 1.0f && dot(unpack_normal(w0.x), n_prev) > 0.9f;
}

[numthreads(8, 8, 1)]
void EngineDirectCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id.xy);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    RWByteAddressBuffer direct = engine_buffer(kEvDirect);
    Reservoir r = reservoir_empty();
    if (!s.valid || (s.flags & kGbEmitter) != 0u) {
        direct.Store4(pixel * 16u, uint4(0u, 0u, 0u, 0u));
        reservoir_store(engine_view(kEvDiTemp), pixel, r);
        return;
    }
    uint seed = emit_hash(pixel ^ (g_frame * 0x9E3779B9u));
    const uint salt = pixel * 32u + g_frame * 0x632BE5ABu;
    const float3 v = normalize(-s.p);
    const float3 start = s.p + s.ng * (0.2f + abs(s.p.z) * 4e-4f);
    // The sun: one direction in its disc behind a shadow ray (diffuse as m, specular as radiance).
    float3 diffuse = float3(0.0f, 0.0f, 0.0f);
    float3 specular = float3(0.0f, 0.0f, 0.0f);
    if (any(g_sun_light.rgb > 0.0f)) {
        const float3 l = sun_direction_sample(float2(emit_random(seed), emit_random(seed)));
        const float nl = dot(s.n, l);
        float blocker;
        if (nl > 0.0f && dot(s.ng, l) > -0.05f && sun_visible(start, l, (s.flags & kGbCard) != 0u, salt, blocker)) {
            diffuse = g_sun_light.rgb * nl;
            specular = sun_specular(s, v);
        }
    }
    direct.Store4(pixel * 16u, uint4(pack_half2(diffuse.r, diffuse.g), pack_half2(diffuse.b, specular.r),
                                     pack_half2(specular.g, specular.b), 0u));
    // The lights (emitting triangles, the game's point lights, the star core): candidates by power.
    const float tris = g_tev_info.z != 0u ? max(emit_element(0u).y, 0.0f) : 0.0f;
    const float spheres = spheres_total();
    // Eight candidates (sixteen at High): at four, the Observatory's star core and its dozens of lamps left the floor's
    // direct light speckled, and the denoiser spread the speckle into blotches (b83, direct-light view).
    const uint candidates = (g_flags & 1024u) != 0u ? 16u : 8u;
    if (tris + spheres > 0.0f) {
        [loop] for (uint k = 0u; k < candidates; ++k) {
            LightSample l;
            float chance;
            if (!engine_light_pick(s.p, tris, spheres, seed, l, chance)) {
                r.m += 1.0f;
                continue;
            }
            float3 light;
            const float target = light_target(s, l, light);
            reservoir_update(r, l, target, target / chance, 1.0f, seed);
        }
        // Temporal reuse: the last frame's final reservoir where this surface was, its light re-weighed here.
        uint prev_pixel;
        if (engine_reproject(s.p, s.n, prev_pixel)) {
            Reservoir previous = reservoir_load(engine_view(kEvDiPrev), prev_pixel);
            previous.m = min(previous.m, 20.0f * float(candidates));
            float3 light;
            const float target = previous.W > 0.0f ? light_target(s, previous.s, light) : 0.0f;
            reservoir_update(r, previous.s, target, target * previous.W * previous.m, previous.m, seed);
        }
    }
    reservoir_finish(r);
    reservoir_store(engine_view(kEvDiTemp), pixel, r);
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
    // This pixel's reservoir, then four neighbours within 30 pixels on the same surface (normal, depth), each
    // light re-weighed here (biased 1/M combination with geometric checks, RTXDI's default).
    {
        const Reservoir own = reservoir_load(engine_view(kEvDiTemp), pixel);
        float3 light;
        const float target = own.W > 0.0f ? light_target(s, own.s, light) : 0.0f;
        reservoir_update(r, own.s, target, target * own.W * own.m, own.m, seed);
    }
    [loop] for (uint k = 0u; k < 4u; ++k) {
        const float angle = emit_random(seed) * 2.0f * kPi;
        const float radius = 3.0f + emit_random(seed) * 27.0f;
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
        const Reservoir other = reservoir_load(engine_view(kEvDiTemp), neighbour);
        float3 light;
        const float target = other.W > 0.0f ? light_target(s, other.s, light) : 0.0f;
        reservoir_update(r, other.s, target, target * other.W * other.m, other.m, seed);
    }
    reservoir_finish(r);
    // The kept light behind its shadow ray; an occluded light keeps its count with no weight (visibility reuse).
    float3 diffuse = float3(0.0f, 0.0f, 0.0f);
    if (r.W > 0.0f) {
        float3 light;
        light_target(s, r.s, light);
        const float3 start = s.p + s.ng * (0.2f + abs(s.p.z) * 4e-4f);
        if (engine_visible(start, r.s.y, 0.5f + 0.001f * length(r.s.y - start), salt)) {
            diffuse = light * r.W;
        } else {
            r.W = 0.0f;
        }
    }
    reservoir_store(engine_view(kEvDi), pixel, r);
    RWByteAddressBuffer direct = engine_buffer(kEvDirect);
    const uint4 d = direct.Load4(pixel * 16u);
    const float3 sum = float3(f16tof32(d.x), f16tof32(d.x >> 16u), f16tof32(d.y)) + safe_light(diffuse);
    direct.Store4(pixel * 16u, uint4(pack_half2(sum.r, sum.g), (d.y & 0xFFFF0000u) | f32tof16(sum.b), d.z, d.w));
}

// --- ReSTIR GI (half resolution) ----------------------------------------------------------------------------------

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
int2 gi_pixel(uint2 h)
{
    const uint k = (g_frame + h.x * 3u + h.y * 5u) & 3u;
    return min(int2(h * 2u + uint2(k & 1u, k >> 1u)), int2(g_target_size) - 1);
}

[numthreads(8, 8, 1)]
void EngineIndirectCS(uint3 id : SV_DispatchThreadID)
{
    const uint2 half_size = g_engine_size.zw;
    if (any(id.xy >= half_size)) {
        return;
    }
    const uint index = id.y * half_size.x + id.x;
    const int2 q = gi_pixel(id.xy);
    const uint pixel = uint(q.y) * g_target_size.x + uint(q.x);
    const GSurface s = gb_load(engine_view(kEvGBuffer), pixel, g_projection, q);
    GiReservoir r = gi_empty();
    if (!s.valid || (s.flags & kGbEmitter) != 0u || (g_engine_info.x & kEngineIndirect) == 0u) {
        gi_store(engine_view(kEvGiTemp), index, r);
        gi_store(engine_view(kEvGi), index, r);
        return;
    }
    uint seed = emit_hash(index ^ (g_frame * 0x68E31DA4u) ^ 0x1B56C4E9u);
    const uint salt = index * 32u + g_frame * 0x632BE5ABu + 19u;
    const float3 start = s.p + s.ng * (0.2f + abs(s.p.z) * 4e-4f);
    const float3 d = cosine_direction(s.n, s.ng, float2(emit_random(seed), emit_random(seed)));
    const Hit h = engine_trace(start, d, salt);
    float3 x;
    float3 hn;
    // The emitters' own light reaches this surface by the direct light: not again here.
    const float3 radiance = safe_light(engine_hit_radiance(h, start, d, false, seed, salt + 3u, x, hn));
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
    if (engine_reproject(s.p, s.n, prev_pixel)) {
        // The last frame's final reservoir of the half-resolution sample covering that pixel.
        // The last frame's temporal reservoir (not its spatially reused one: their gain compounded into blobs).
        const uint2 hp = uint2(prev_pixel % g_target_size.x, prev_pixel / g_target_size.x) / 2u;
        GiReservoir previous = gi_load(engine_view(kEvGiPrev), min(hp.y, half_size.y - 1u) * half_size.x +
                                                                    min(hp.x, half_size.x - 1u));
        previous.m = min(previous.m, 30.0f);
        if (previous.W > 0.0f) {
            const float t = gi_target(s, previous.x, previous.radiance);
            gi_update(r, previous.x, previous.hn, previous.radiance, t, t * previous.W * previous.m, previous.m, seed);
        } else {
            r.m += previous.m;
        }
    }
    r.W = r.target > 0.0f && r.m > 0.0f ? r.w_sum / (r.m * r.target) : 0.0f;
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
    const float3 start = s.p + s.ng * (0.2f + abs(s.p.z) * 4e-4f);
    {
        const GiReservoir own = gi_load(engine_view(kEvGiTemp), index);
        const float t = own.W > 0.0f ? gi_target(s, own.x, own.radiance) : 0.0f;
        gi_update(r, own.x, own.hn, own.radiance, t, t * own.W * own.m, own.m, seed);
    }
    // Three neighbours within 10 half-pixels on the same surface; their samples seen from here with the solid angle's
    // Jacobian (Ouyang et al. 2021, ReSTIR GI), the reused sample's visibility checked before it is kept.
    bool reused = false;
    [loop] for (uint k = 0u; k < 3u; ++k) {
        const float angle = emit_random(seed) * 2.0f * kPi;
        const float radius = 1.0f + emit_random(seed) * 9.0f;
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
    if (reused && r.W > 0.0f && length(r.x) < 9.0e4f && !engine_visible(start, r.x, 0.5f, salt)) {
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

// --- Specular -----------------------------------------------------------------------------------------------------

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
        const float3 start = s.p + s.ng * (0.2f + abs(s.p.z) * 4e-4f);
        const Hit h = engine_trace(start, l, salt);
        float3 x;
        float3 hn;
        const float3 incoming = safe_light(engine_hit_radiance(h, start, l, true, seed, salt + 5u, x, hn));
        // VNDF sampling's weight: F G2 / G1(v).
        const float nv = max(dot(s.n, v), 1e-4f);
        const float g2_over_g1 = smith_visibility(alpha, nl, nv) * 4.0f * nl * nv / max(smith_g1(alpha, nv), 1e-4f);
        result = incoming * fresnel_schlick(specular_f0(s), dot(v, hm)) * g2_over_g1;
        distance = h.t;
    }
    result = safe_light(result);
    out_spec.Store2(pixel * 8u, uint2(pack_half2(result.r, result.g), pack_half2(result.b, min(distance, 6.0e4f))));
}

// --- Denoiser -----------------------------------------------------------------------------------------------------

// The half-resolution indirect light at a full-resolution pixel: its four nearest half-resolution samples weighed by
// depth and normal against this surface.
float3 gi_upsampled(GSurface s, int2 q)
{
    const uint2 half_size = g_engine_size.zw;
    RWByteAddressBuffer gi = engine_buffer(kEvGiOut);
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

// Temporal accumulation: this frame's diffuse (the direct light's and the indirect light's) and specular (the sun's
// and the traced reflection's) onto their reprojected histories (bilinear over the previous pixels on this surface),
// with the first two moments of luminance for the a-trous's variance. History length at most 32 (diffuse) and 16
// (specular); the specular history is clamped to the 3 x 3 neighbourhood's range (ghosting).
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
    float3 specular = float3(f16tof32(dv.y >> 16u), f16tof32(dv.z), f16tof32(dv.z >> 16u)) +
                      float3(f16tof32(sv.x), f16tof32(sv.x >> 16u), f16tof32(sv.y));
    diffuse = safe_light(diffuse);
    specular = safe_light(specular);
    // The neighbourhood's specular range (clamping the history), and the brightest of the 8 neighbours' direct light
    // and specular: a new sample above them is a firefly (one light sample's spike), lowered to them before it enters
    // the history (spread by the a-trous's variance and kept by the history, spikes grew into blobs: b58-b72).
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
            const float3 ns = safe_light(float3(f16tof32(ndv.y >> 16u), f16tof32(ndv.z), f16tof32(ndv.z >> 16u)) +
                                         float3(f16tof32(nsv.x), f16tof32(nsv.x >> 16u), f16tof32(nsv.y)));
            lo = min(lo, ns);
            hi = max(hi, ns);
            if (i != 0 || j != 0) {
                top_direct = max(top_direct, luminance(safe_light(float3(f16tof32(ndv.x), f16tof32(ndv.x >> 16u),
                                                                         f16tof32(ndv.y)))));
                top_specular = max(top_specular, luminance(ns));
            }
        }
    }
    {
        // Past kFireflyRatio x the brightest neighbour: a small light is found by a few pixels first (4 candidates a
        // frame by power) and spatial reuse spreads it over the next frames; lowered to the neighbours' level outright,
        // those first finds never entered the history and a Luma's or a bulb's light pool stayed away (owner: lights
        // that do not reach).
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
    float len_d = 0.0f;
    float len_s = 0.0f;
    if ((g_flags & 1u) != 0u) {
        const float3 previous = transform(g_reproject, s.p);
        if (previous.z < -g_near) {
            const float2 a = eye_to_pixel(g_history_projection, g_viewport, previous) - 0.5f;
            const int2 b = int2(floor(a));
            const float2 f = a - float2(b);
            const float3 n_prev = normalize(rotate3(g_reproject, s.n));
            RWByteAddressBuffer gb = engine_buffer(kEvGBufferPrev);
            RWByteAddressBuffer pd = engine_buffer(kEvHistDiffusePrev);
            RWByteAddressBuffer ps = engine_buffer(kEvHistSpecularPrev);
            float weight = 0.0f;
            float4 acc_d = float4(0.0f, 0.0f, 0.0f, 0.0f);
            float4 acc_s = float4(0.0f, 0.0f, 0.0f, 0.0f);
            float2 acc_m = float2(0.0f, 0.0f);
            [unroll] for (uint k = 0u; k < 4u; ++k) {
                const int2 o = b + int2(k & 1u, k >> 1u);
                if (any(o < 0) || any(o >= int2(g_target_size))) {
                    continue;
                }
                const uint op = uint(o.y) * g_target_size.x + uint(o.x);
                const uint4 w0 = gb.Load4(op * 32u);
                const uint4 w1 = gb.Load4(op * 32u + 16u);
                if (w1.x == 0u || abs(asfloat(w1.x) - previous.z) > 0.02f * abs(previous.z) + 1.0f ||
                    dot(unpack_normal(w0.x), n_prev) < 0.9f) {
                    continue;
                }
                const float w = ((k & 1u) != 0u ? f.x : 1.0f - f.x) * ((k & 2u) != 0u ? f.y : 1.0f - f.y);
                const uint4 hd = pd.Load4(op * 16u);
                const uint4 hs = ps.Load4(op * 16u);
                acc_d += float4(f16tof32(hd.x), f16tof32(hd.x >> 16u), f16tof32(hd.y), f16tof32(hd.y >> 16u)) * w;
                acc_s += float4(f16tof32(hs.x), f16tof32(hs.x >> 16u), f16tof32(hs.y), f16tof32(hs.y >> 16u)) * w;
                acc_m += asfloat(hd.zw) * w;
                weight += w;
            }
            if (weight > 1e-3f) {
                prev_d = acc_d.rgb / weight;
                len_d = acc_d.a / weight;
                prev_s = acc_s.rgb / weight;
                len_s = acc_s.a / weight;
                prev_m = acc_m / weight;
            }
        }
    }
    len_d = min(len_d + 1.0f, 32.0f);
    len_s = min(len_s + 1.0f, 16.0f);
    prev_s = clamp(prev_s, lo, hi);
    const float ld = luminance(diffuse);
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
    hist_s.Store4(pixel * 16u, uint4(pack_half2(specular.r, specular.g), pack_half2(specular.b, len_s), 0u, 0u));
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
    // at one frame to n.n^64 from eight (Mario's gloves kept their raw indirect noise mid-dash under n.n^64 and ^8:
    // b70, b76).
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

// --- Clear surfaces (glass, crystal) -------------------------------------------------------------------------------

// The light a glass reflection's ray brings back from its hit: emission plus its colour under the sun (one shadow ray,
// opaque traversal: alpha cards count as solid) and the open sky (its irradiance harmonics: no ray). Light from the
// emitters and the bounce are left to the opaque surfaces' own reflections: the glass shows them faintly at F. Cheap
// and bounded per glass pixel (the full hit lighting under every crystal of Dusty Dune and Space Junk hung the GPU).
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
// span is empty: a ray whose start passes its end is undefined, and hung the GPU where glass lies on the surface
// behind it, Dusty Dune b41).
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
    // The nearest surface of the water window (opaque traversal: the closest hit alone; reporting every candidate
    // through Dusty Dune's layered sand surfaces ran past the GPU's watchdog), the engine's when it is glass.
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
// a GGX visible-normal ray at the table's roughness (the hit lit as the engine's specular hits; the front wall only:
// the far wall's reflection looks into what the glass holds, Dusty Dune's hourglass sand, hundreds of cards a ray, past
// the GPU's watchdog, for a reflection its front wall dims), and the sun's highlight behind a shadow ray; out: the
// wall's transmittance.
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

// Per pixel: the camera ray's first clear surface in front of the scene behind it, and a second behind that (the far
// wall of a capsule or a bottle, seen through the first). Their reflections at the camera's exposure and the light
// they pass, for the composite (rt_lighting.hlsl ClearCompositePS: the picture behind times T plus R).
[numthreads(8, 8, 1)]
void EngineClearCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_target_size)) {
        return;
    }
    const uint pixel = id.y * g_target_size.x + id.x;
    const int2 q = int2(id.xy);
    uint4 result = uint4(0u, 0u, 0u, 0u);
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
        const ClearHit front = clear_trace(dir, t_min, t_max);
        if (front.hit && (g_engine_info.x & kEngineClearFind) != 0u) {
            result = uint4(pack_half2(0.0f, 0.5f), pack_half2(0.5f, 0.5f), 0u, 1u);
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
        } else if (((g_engine_info.x & kEngineView) >> 4u) == 5u) {
            // ... and where none was found within the scene's depth, a green veil (the pass and its composite ran).
            result = uint4(pack_half2(0.0f, 0.15f), pack_half2(0.0f, 1.0f), asuint(-t_max), 1u);
        }
    }
    engine_buffer(kEvClear).Store4(pixel * 16u, result);
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
    if (s.valid) {
        z = s.p.z;
        if ((s.flags & kGbEmitter) != 0u) {
            // A light source shows what it sends (its colour, the composite's albedo, times this), at most
            // kEmitterShown.
            diffuse = (min(s.emission, kEmitterShown) * exposure).xxx;
        } else if ((s.flags & kGbClear) != 0u) {
            // A clear solid shows the scene behind it as the game refracted it (its picture is the lit, exposed scene
            // through it: the albedo view, d84, shows Melty Molten's crystals and their star chips as the game does),
            // passed at 1 - F; lit again by the light at its own surface it went black (b77-b81). Its reflection is
            // the traced specular's.
            const uint4 a = engine_buffer((g_engine_info.w & 1u) == 0u ? kEvAtrousA : kEvAtrousB).Load4(pixel * 16u);
            diffuse = (1.0f - kClearF0).xxx;
            specular = float3(f16tof32(a.z), f16tof32(a.z >> 16u), f16tof32(a.w)) * exposure;
            whole = diffuse;
        } else {
            const uint view = (g_engine_info.x & kEngineView) >> 4u;
            const uint4 a = engine_buffer((g_engine_info.w & 1u) == 0u ? kEvAtrousA : kEvAtrousB).Load4(pixel * 16u);
            diffuse = float3(f16tof32(a.x), f16tof32(a.x >> 16u), f16tof32(a.y));
            specular = float3(f16tof32(a.z), f16tof32(a.z >> 16u), f16tof32(a.w));
            whole = diffuse * exposure;
            if (view == 1u || view == 2u || view == 4u) {
                const uint4 dv = engine_buffer(kEvDirect).Load4(pixel * 16u);
                const float3 direct = float3(f16tof32(dv.x), f16tof32(dv.x >> 16u), f16tof32(dv.y));
                diffuse = view == 1u ? direct : view == 2u ? gi_upsampled(s, q) : direct + gi_upsampled(s, q);
                specular = float3(0.0f, 0.0f, 0.0f);
            } else if (view == 3u) {
                diffuse = float3(0.0f, 0.0f, 0.0f);
            }
            diffuse *= (1.0f - s.metallic) * exposure;
            specular *= exposure;
            if ((s.flags & kGbGlow) != 0u) {
                // Its glow over its lit colour (in the specular term: the composite does not take it times the
                // albedo, which for a metal is its painted reflection's remainder).
                specular += s.albedo * (s.emission * exposure);
            }
        }
        if ((s.flags & kGbEmitter) != 0u) {
            whole = diffuse;
        }
        if (((g_engine_info.x & kEngineView) >> 4u) == 7u) {
            diffuse = float3(1.0f, 1.0f, 1.0f);
            specular = float3(0.0f, 0.0f, 0.0f);
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
