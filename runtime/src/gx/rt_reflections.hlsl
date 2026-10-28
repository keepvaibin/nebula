// SPDX-License-Identifier: GPL-3.0-only
// Ray-traced reflections of the game frame's metal, glossy and water surfaces (DXR 1.1 inline ray queries, cs_6_5)
// and their composite into the EFB (vs/ps_6_0).
//
// Each lighting sample (one per g_factor x g_factor EFB pixels) takes its lobe width, Fresnel reflectance, strength
// and cover from its draw's GX surface class (rt_surfaces.h), or from the material table's roughness and metalness
// (rt_materials.h); the vertex normals give the shading normal. One ray samples the GGX distribution of visible
// normals. A hit the frame shows brings that pixel's colour times this frame's traced lighting there. Elsewhere the
// environment memory answers: per world direction from the camera, the colour and distance the frame last showed
// there, fading where the camera stops looking. Unknown light has no confidence; confidence weights the light and
// scales the reflection, so a surface keeps its own colour where the reflected light is unknown. Metal reflections
// take the surface's hue; surfaces bright under this frame's lighting (lamps, lava, glowing cores) reflect nothing.
// Results accumulate along the reflected image's motion (through the virtual image point for sharp lobes, the
// surface for wide ones), clamped to the spread of the known light around them, then pass two edge-aware a-trous
// filters as wide as the lobe. The composite adds k * reflection and removes k * (cover + (1 - cover) * reflection)
// of the surface's own colour (cover 1: a blend towards the reflection; cover 0: a screen, never darker than the
// game's shading), k being the class strength times Schlick's Fresnel term at the pixel's view angle times the
// sample's weight. Sub-frames first move the pixel to the traced frame's camera.

static const float kPi = 3.14159265f;
static const float kGlowCutoff = 0.004f;   // rt_lighting.hlsl g_emit.x (GALAXY_RT_GLOW_CUTOFF default)
static const float kGlowReach = 6000.0f;   // ... g_emit.y (GALAXY_RT_GLOW_REACH default)
static const uint kNoSurface = 15u;
static const uint kProbeSize = 64u;
static const float kSkyDistance = 1.0e9f;

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

// Physical light as the reflections carry it: a surface's linear light at the camera's exposure, display-encoded up to
// white as the composite shows it (rt_lighting.hlsl CompositePS: linear_to_srgb of the lit albedo), light past white
// as it is (lamps, lava: the reflection's composite rolls it off).
float3 physical_encode(float3 c)
{
    return lerp(linear_to_srgb(min(c, 1.0f)), c, step(1.0f, c));
}

// Strong light rolled off softly: c ln(1 + l / c) in luminance, its hue kept. Weak light passes unchanged and strong
// light keeps a falloff all the way to its source, where a rational l / (1 + l / c) flattens a lamp's light into a
// disc with a hard rim.
float3 soft_light(float3 light, float c)
{
    const float l = luminance(light);
    return l > 1e-6f ? light * (c * log(1.0f + l / c) / l) : light;
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
                                // bit 6: the lighting's sky map usable; bit 9: the samples show their
                                // draw's material table row (kViewRow); bit 10: their reflection's source
                                // (kViewSource)
    float4 g_reproject[3];      // previous samples' eye position from this frame's (3x4 rows)
    float4 g_world_to_eye[3];   // the camera's rotation (rows)
    float4 g_eye_to_world[3];   // its inverse
    float4 g_class[10];         // per surface class (bare metal 8, falling water 9): GGX alpha, strength, F0, metal
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
    float4 g_points[32];        // per point light (rt_reflections.h kRtPointLights): position, reach; colour,
                                // 1 / half-light distance^2
    uint g_point_count;
    float g_point_light;        // the point lights' weight
    float g_point_size;         // radius of the emitter around a point light
    float g_shafts;             // under the water: the sunlight its waves scatter towards the camera
    float g_sky_fade;           // environment memory of the sky kept per frame where the camera does not look
    float g_sky_average;        // confidence of the sky's average colour where no sky is known
    float g_metal_sky;          // metal: confidence of a remembered, mapped or average sky (blurrier than its map)
    float g_local_gain;         // the local lights' highlights (point and glow lights)
    uint g_draw_count;          // entries of g_draws (g_flags bit 7)
    float g_rough_cutoff;       // the material table's roughness above which a surface reflects nothing
    float g_metal_f0;           // Fresnel reflectance of a bare metal (metalness 1)
    float g_waves;              // water: slope of its procedural waves (0: none)
    float g_draw_trust;         // confidence of a hit draw's mean lit colour where the frame hides the hit
    float g_sky_trust;          // confidence of the captured sky (SkyCaptureCS) where it was seen
    float g_metal_cover;        // the metal class's cover: bare metal is its reflection (0: screened over)
    float g_pad;
    // High quality (g_flags bit 11): the art's level at a hit: day share, night level, day level, texture
    // detail per eye unit of distance.
    float4 g_hit;
    uint g_texture_count;       // entries of g_draw_tex
    // The lighting's night model of the point lights (rt_lighting.hlsl g_night.w, g_ao.w, g_sun_cap, g_core_point)
    // for the light on a shaded hit: their weight, the soft cap of what they add, the cap's rise where the star
    // core's light rules, the core's index among g_points (~0: none).
    float g_hit_night_lights;
    float g_hit_cap;
    float g_hit_sun_cap;
    uint g_hit_core;
    // Physical light: the open sky's ambient as the picture shows it; the sky a reflection sees is at least this
    // (RtReflectScene::sky_fill).
    float3 g_sky_fill;
    // Physical light (g_flags bit 12, RtReflectScene::physical): the lighting's inputs (rt_lighting.hlsl RtLight) for
    // the hits off screen: the sky's radiance (w: overcast), the stage's full light (w 1: the traced sky's map), the
    // player's up in eye space (w: the de-light maps' relief), the camera's exposure (x; 0: physical_exposure's own).
    float4 g_sky_radiance;
    float4 g_full_light;
    float4 g_up;
    float4 g_light_fit;
    // Raw views in the descriptor heap (~0: none): the scene's TEV records, the emitting triangles, the traced sky's
    // map; the records' and the triangles' counts.
    uint g_tev_view;
    uint g_emit_view;
    uint g_sky_view;
    uint g_tev_count;
    uint g_emit_count;
    uint g_emit_scene;          // the scene's emitting triangles among g_emit_count (the rest: glow emitters)
    uint2 g_physical_pad;
};

// g_flags bit 12: physical light (GALAXY_RT_PHYSICAL), the lighting's units.
static const uint kReflectPhysical = 4096u;
// g_flags bit 13: the ray tracing engine traces the clear glass and crystal (rt_engine.hlsl EngineClearCS); the water
// pass leaves them (mirror glass and metal coats stay its own).
static const uint kReflectEngineClear = 8192u;

RaytracingAccelerationStructure g_scene : register(t0);
ByteAddressBuffer g_positions : register(t1);       // per triangle three eye-space vertices
// Per triangle, 16 bytes: vertex normals (octahedral 16:16, 1-65535 per axis, 0 none), draw flags.
ByteAddressBuffer g_surfaces : register(t2);
StructuredBuffer<uint4> g_light : register(t3);     // this frame's lighting
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
// High quality (g_flags bit 11): per triangle its texture coordinates and colours (ray_tracing.hlsl g_tri_uv,
// 36 bytes); per draw its material texture's view in the descriptor heap (~0: none), its colour source (bit 0:
// the vertices' colour 0) and its colour (RGBA8, red in the top byte).
ByteAddressBuffer g_tri_uv : register(t16);
StructuredBuffer<uint4> g_draw_tex : register(t17);
SamplerState g_wrap : register(s1);
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
// Per sample, from the camera ray (rt_engine.hlsl EngineGBufferCS): triangle + 1 (0 none) and its barycentrics
// (halves). Read only.
RWStructuredBuffer<uint2> g_primary : register(u6);
// g_primary.x (rt_engine.hlsl EngineGBufferCS): the triangle + 1, and its top bit for a table metal taken for paint.
static const uint kPrimaryTriangle = 0x7FFFFFFFu;
static const uint kPrimaryPainted = 0x80000000u;
// Per tile of 8x8 samples: 1 where a sample reflects.
RWStructuredBuffer<uint> g_tiles : register(u7);
// Per sample, the picture under the translucent phases for the water's refraction: lit colour
// (11-11-10) and eye z (0: the sky).
RWStructuredBuffer<uint2> g_under : register(u8);
// The sky as the stage has shown it (SkyCaptureCS): per world direction (octahedral, as the environment
// memory's, kSkySize^2 texels) its colour (11-11-10) and the share of the texel seen (unorm16, 0 never).
RWStructuredBuffer<uint2> g_sky : register(u9);
// Per draw of the scene (DrawLightCS): this frame's lit colour summed over its samples (rgb in 1/64)
// and their count; zeroed before (DrawLightClearCS).
RWStructuredBuffer<uint4> g_draw_light : register(u10);

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

// The stars' balls (rt_lighting.hlsl StarSphereCS: centre and radius, eye space) follow the lighting's samples.
static const uint kStarSlots = 8u;
// The stars' colour in the previous picture's sums (rt_lighting.hlsl kStarSums, kStarColour): per star slot the
// game's own colour of its samples, r g b n (x 1/255), then over time, rgb (float) and known.
static const uint kStarSums = 64u;
static const uint kStarColour = kStarSums + kStarSlots * 16u;

// A star's ball where a reflection ray along `l` meets it at `x`, as the composite shades it without its cells
// (rt_lighting.hlsl star_surface): the game's own colour of that star, brighter where the ray meets the ball face
// on. The ball is the one whose surface passes nearest `x`; its colour the previous picture's, else the stars'
// over time, else `seen` (the frame's colour there).
float3 star_reflected(float3 x, float3 l, float3 seen)
{
    float3 normal = -l;
    float nearest = 1.0e30f;
    uint star = kStarSlots;
    [unroll] for (uint k = 0u; k < kStarSlots; ++k) {
        const float4 ball = asfloat(g_light[g_sample_size.x * g_sample_size.y + k]);
        if (ball.w > 0.0f) {
            const float off = abs(length(x - ball.xyz) - ball.w);
            if (off < nearest) {
                nearest = off;
                normal = normalize(x - ball.xyz);
                star = k;
            }
        }
    }
    float3 colour = seen;
    if ((g_flags & 4u) != 0u) {
        const uint4 own = star < kStarSlots ? g_sums.Load4(kStarSums + star * 16u) : uint4(0u, 0u, 0u, 0u);
        const uint4 known = g_sums.Load4(kStarColour);
        if (own.w >= 4u) {
            colour = float3(own.xyz) / (255.0f * float(own.w));
        } else if (known.w != 0u) {
            colour = asfloat(known.xyz);
        }
    }
    const float mu = saturate(dot(normal, -l));
    const float3 lit = colour * (1.15f * (0.8f + 0.4f * mu));
    const float peak = max(lit.r, max(lit.g, lit.b));
    return saturate(lit / max(peak, 1.0f) + max(peak - 1.0f, 0.0f) * 0.2f);
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

// The angle a reflection ray of this sample may stray by from its neighbours' (radians): the water's ripples
// and waves bend each pixel's normal its own way (WaterTraceCS); 0 elsewhere.
static float g_spread = 0.0f;
// The least footprint a reflection reads the frame over (EFB pixels): water's, one of the game's own 640-wide pixels,
// so its mirror never aliases the scenery's fine texture (lattice bars, grass) into per-pixel grain, the same at any
// resolution; 0 elsewhere (WaterTraceCS).
static float g_reflect_blur = 0.0f;

// The frame's colour at `pixel`, averaged over a footprint `blur` pixels wide (8 taps): what a rippled
// surface's rays scatter across, not the one texel a single ray lands on.
float3 frame_color(float2 pixel, float blur)
{
    const float2 size = float2(g_target_size);
    if (blur < 0.75f) {
        return g_color.SampleLevel(g_linear, pixel / size, 0.0f).rgb;
    }
    const float o = 0.5f * blur;
    const float i = 0.25f * blur;
    return 0.125f * (g_color.SampleLevel(g_linear, (pixel + float2(-o, -o)) / size, 0.0f).rgb +
                     g_color.SampleLevel(g_linear, (pixel + float2(o, -o)) / size, 0.0f).rgb +
                     g_color.SampleLevel(g_linear, (pixel + float2(-o, o)) / size, 0.0f).rgb +
                     g_color.SampleLevel(g_linear, (pixel + float2(o, o)) / size, 0.0f).rgb +
                     g_color.SampleLevel(g_linear, (pixel + float2(-i, 0.0f)) / size, 0.0f).rgb +
                     g_color.SampleLevel(g_linear, (pixel + float2(i, 0.0f)) / size, 0.0f).rgb +
                     g_color.SampleLevel(g_linear, (pixel + float2(0.0f, -i)) / size, 0.0f).rgb +
                     g_color.SampleLevel(g_linear, (pixel + float2(0.0f, i)) / size, 0.0f).rgb);
}

// An eye-space distance `z`'s pixel: its width in world units.
float pixel_size(float z)
{
    return abs(z) * 2.0f / max(abs(g_projection.x) * g_viewport.z, 1e-4f);
}

// What the frame shows at `pixel`: the lit colour and the distance of its surface (kSkyDistance
// for the sky); false outside the viewport.
bool shown_at(float2 pixel, out float3 light, out float distance, float blur = 0.0f)
{
    light = float3(0.0f, 0.0f, 0.0f);
    distance = kSkyDistance;
    if (!inside(g_viewport, pixel)) {
        return false;
    }
    const int2 c = int2(pixel);
    const float depth = g_efb_depth.Load(int3(c, 0));
    light = frame_color(pixel, blur);
    if (!far_plane(g_depth, depth)) {
        distance = length(eye_position(g_projection, g_viewport, pixel, depth_to_eye_z(g_depth, depth)));
        if (distance >= g_sky_distance) {
            distance = kSkyDistance;
        } else if ((g_flags & 32u) == 0u) {
            // Physical: in linear light, as the lighting's composite lights that pixel (multiplied in display
            // encoding, shade reads darker and light brighter than the picture shows them).
            light = (g_flags & kReflectPhysical) != 0u ? physical_encode(srgb_to_linear(light) * lighting_at(c))
                                                        : light * lighting_at(c);
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
    // A new stage (g_flags bit 8) clears the memory, so the last stage's colours never show in reflections.
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

// Sky capture: the backdrop is drawn around the camera (no parallax), so every frame's sky pixels
// give its colour per world direction. Kept for the stage at about 0.2 degrees per texel, it is
// what a mirror shows of the sky out of view (the environment memory's 64^2 texels blur it).
static const uint kSkySize = 1024u;

// The frame's sky at `pixel`: false where a surface nearer than the backdrops covers it.
bool sky_pixel(float2 pixel, out float3 light)
{
    light = float3(0.0f, 0.0f, 0.0f);
    if (!inside(g_viewport, pixel)) {
        return false;
    }
    const int2 c = int2(pixel);
    const float depth = g_efb_depth.Load(int3(c, 0));
    if (!far_plane(g_depth, depth) &&
        length(eye_position(g_projection, g_viewport, pixel, depth_to_eye_z(g_depth, depth))) < g_sky_distance) {
        return false;
    }
    light = g_color.Load(int3(c, 0)).rgb;
    return true;
}

// One thread per texel: five looks over the texel's footprint on the screen (its centre and the
// middles of its quarters); the sky they find replaces half of what the texel held.
[numthreads(8, 8, 1)]
void SkyCaptureCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= kSkySize)) {
        return;
    }
    const uint index = id.y * kSkySize + id.x;
    uint2 stored = (g_flags & 256u) != 0u ? uint2(0u, 0u) : g_sky[index];
    if ((g_flags & 8u) != 0u) {
        const float2 uv = (float2(id.xy) + 0.5f) / float(kSkySize);
        const float3 d = rotate(g_world_to_eye, octahedral_direction(uv));
        const float3 dx = rotate(g_world_to_eye, octahedral_direction(uv + float2(0.5f / float(kSkySize), 0.0f)));
        const float3 dy = rotate(g_world_to_eye, octahedral_direction(uv + float2(0.0f, 0.5f / float(kSkySize))));
        if (d.z < -1e-3f && dx.z < -1e-3f && dy.z < -1e-3f) {
            const float2 centre = eye_to_pixel(g_projection, g_viewport, d);
            // Half a texel along each of its axes, in pixels.
            const float2 ax = eye_to_pixel(g_projection, g_viewport, dx) - centre;
            const float2 ay = eye_to_pixel(g_projection, g_viewport, dy) - centre;
            float3 sum = float3(0.0f, 0.0f, 0.0f);
            float looks = 0.0f;
            [unroll] for (uint k = 0u; k < 5u; ++k) {
                const float2 o = k == 4u ? float2(0.0f, 0.0f) : float2((k & 1u) != 0u ? 0.5f : -0.5f, (k & 2u) != 0u ? 0.5f : -0.5f);
                float3 light;
                if (sky_pixel(centre + o.x * ax + o.y * ay, light)) {
                    sum += light;
                    looks += 1.0f;
                }
            }
            if (looks > 0.0f) {
                const float seen = float(stored.y & 0xFFFFu) / 65535.0f;
                const float3 now = sum / looks;
                const float3 kept = seen > 0.0f ? lerp(unpack_r11g11b10(stored.x), now, 0.5f) : now;
                stored = uint2(pack_r11g11b10(kept), uint(round(max(seen, looks / 5.0f) * 65535.0f)));
            }
        }
    }
    g_sky[index] = stored;
}

// The captured sky towards eye-space direction `d` (bilinear over its texels): its light and the share
// of it seen.
float sky_captured(float3 d, out float3 light)
{
    light = float3(0.0f, 0.0f, 0.0f);
    if ((g_flags & 8u) == 0u) {
        return 0.0f;
    }
    const float2 a = octahedral(rotate(g_eye_to_world, normalize(d))) * float(kSkySize) - 0.5f;
    const int2 base = int2(floor(a));
    const float2 f = a - float2(base);
    float4 sum = float4(0.0f, 0.0f, 0.0f, 0.0f);
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 o = int2(k & 1u, k >> 1u);
        const uint2 t = uint2(clamp(base + o, int2(0, 0), int2(kSkySize - 1u, kSkySize - 1u)));
        const float bw = (o.x != 0 ? f.x : 1.0f - f.x) * (o.y != 0 ? f.y : 1.0f - f.y);
        const uint2 stored = g_sky[t.y * kSkySize + t.x];
        const float seen = float(stored.y & 0xFFFFu) / 65535.0f;
        sum += float4(unpack_r11g11b10(stored.x), 1.0f) * (seen * bw);
    }
    light = sum.w > 1e-4f ? sum.rgb / sum.w : float3(0.0f, 0.0f, 0.0f);
    return sum.w;
}

// Draw light: each draw's mean lit colour over the samples this frame shows of it. A reflection ray
// whose hit the frame does not show (behind the object, off screen) takes its draw's colour there.
[numthreads(64, 1, 1)]
void DrawLightClearCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x < g_draw_count) {
        g_draw_light[id.x] = uint4(0u, 0u, 0u, 0u);
    }
}

[numthreads(8, 8, 1)]
void DrawLightCS(uint3 id : SV_DispatchThreadID)
{
    bool valid = false;
    uint draw = 0u;
    uint3 units = uint3(0u, 0u, 0u);
    if (all(id.xy < g_sample_size)) {
        const uint2 primary = g_primary[id.y * g_sample_size.x + id.x];
        if (primary.x != 0u) {
            draw = g_surfaces.Load(((primary.x & kPrimaryTriangle) - 1u) * 16u + 12u) >> 16u;
            const int2 q = sample_pixel(id.xy);
            valid = draw < g_draw_count;
            units = uint3(round(min(g_color.Load(int3(q, 0)).rgb * lighting_at(q), 4.0f) * 64.0f));
        }
    }
    // The lanes of one draw add once: each round the first remaining lane's draw.
    if (valid) {
        [loop] for (;;) {
            const uint lead = WaveReadLaneFirst(draw);
            if (draw == lead) {
                const uint3 sum = WaveActiveSum(units);
                const uint n = WaveActiveCountBits(true);
                if (WaveIsFirstLane()) {
                    InterlockedAdd(g_draw_light[draw].x, sum.x);
                    InterlockedAdd(g_draw_light[draw].y, sum.y);
                    InterlockedAdd(g_draw_light[draw].z, sum.z);
                    InterlockedAdd(g_draw_light[draw].w, n);
                }
                break;
            }
        }
    }
}

// Draw `draw`'s mean lit colour this frame: false where the frame shows too little of it.
bool draw_light(uint draw, out float3 light)
{
    light = float3(0.0f, 0.0f, 0.0f);
    if (draw >= g_draw_count || g_draw_trust <= 0.0f) {
        return false;
    }
    const uint4 sum = g_draw_light[draw];
    if (sum.w < 4u) {
        return false;
    }
    light = float3(sum.xyz) / (64.0f * float(sum.w));
    return true;
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
// The reflections' own class for the material table's metals (draw_material): bare metal, whose traced
// reflection takes its painted one's place. The GX state's metal class (an environment map the table does not
// name: penguins, glassy domes, painted sheen) keeps its reflection screened over its look.
static const uint kBareMetalClass = 8u;
static const float kDielectricF0 = 0.04f;
// Metal is never too rough to mirror what stands around it: past the cut-off (0.65) a rough metal reflects nothing,
// and just under it only a faint blur. Capped at 0.35, its reflection is clear and at full strength; the table's lower
// values stay.
static const float kMetalRoughnessMax = 0.35f;

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

// An alpha-tested foliage card (rt_lighting.hlsl kDrawTableCard 16): in the scene whole, its cut-outs
// filled; reflections and their highlights pass it. Late geometry (instance 1) has no records: never a card.
bool traced_card(uint instance, uint primitive)
{
    if (instance != 0u) {
        return false;
    }
    const uint draw = g_surfaces.Load(primitive * 16u + 12u) >> 16u;
    return (g_flags & 128u) != 0u && draw < g_draw_count && (g_draws.Load(draw * 48u + 24u) & 16u) != 0u;
}

// A candidate hit a reflection ray goes on through: a foliage card, or with the textured hits a texel an
// alpha-tested draw's test cuts (g_draw_tex word 3: the alpha map's view, the alpha its test passes from).
bool passes_through(uint instance, uint primitive, float2 b)
{
    if (traced_card(instance, primitive)) {
        return true;
    }
#if RT_TEXTURED
    // A rippled surface's rays (g_spread) read the frame over their footprint, which already holds the cut-outs
    // as the game drew them; tested per ray, a lattice bar or its hole speckles the water. Its rays take
    // alpha-tested draws whole; mirrors and metal test each texel.
    if (instance != 0u || (g_flags & 2048u) == 0u || g_spread > 0.0f || g_reflect_blur > 0.0f) {
        return false;
    }
    const uint draw = g_surfaces.Load(primitive * 16u + 12u) >> 16u;
    if (draw >= g_texture_count) {
        return false;
    }
    const uint alpha = g_draw_tex[draw].w;
    if (alpha == 0u) {
        return false;
    }
    const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
    const uint a = primitive * 72u;
    const float4 uv01 = asfloat(g_tri_uv.Load4(a));
    const float2 uv2 = asfloat(g_tri_uv.Load2(a + 16u));
    const float2 uv = uv01.xy * w.x + uv01.zw * w.y + uv2 * w.z;
    if (any(abs(uv) > 1.0e4f) || any(isnan(uv))) {
        return false;
    }
    const Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(alpha & 0xFFFFFFu)];
    return map.SampleLevel(g_wrap, uv, 0.0f).a * 255.0f < float(alpha >> 24u) - 0.5f;
#else
    return false;
#endif
}

bool lit_from(float3 origin, float3 direction, float length)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = max(length, 0.0f);
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES>
        query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
    while (query.Proceed()) {
        if (!passes_through(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(),
                            query.CandidateTriangleBarycentrics())) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    return query.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}

// Tile light lists: each group of 8 x 8 samples keeps the glow lights whose reach meets the box around its samples'
// surface points, and its samples weigh only those. Lights beyond a tile's reach add nothing to any of its samples,
// so the result is the same at a fraction of the work. The list is a bit per light of the frame's list, walked in
// its order, so the same lights count every frame (a list of the first lights added drops different ones each frame).
static const uint kTileWords = 16u;   // a bit per light of the frame's list (rt_lighting.hlsl kEmitLights)

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

float glow_reach2(float light)
{
    return min(light / (kPi * kGlowCutoff), kGlowReach * kGlowReach);
}

// A local light's highlight: its light, the direction to it and where its shadow ray ends.
struct Highlight {
    float3 light;
    float weight;
    float3 to;
    float end;
};

// Keeps the three strongest highlights, strongest first.
void keep_highlight(inout Highlight top[3], Highlight h)
{
    [unroll] for (uint s = 0u; s < 3u; ++s) {
        if (h.weight > top[s].weight) {
            const Highlight t = top[s];
            top[s] = h;
            h = t;
        }
    }
}

// The highlights the scene's lights put on a surface at `p` (ray origin `origin`): the sun with one shadow ray; the
// local lights (the game's point lights and every glow light within reach: Lumas, lamps, stars, fire) each by its
// irradiance at `p` (power / (pi (d^2 + r^2)), windowed to its reach as the lighting's) times its GGX lobe, all of
// them, times g_local_gain, less the shadowed share of the strongest two.
float3 highlights(uint2 s, float3 p, float3 origin, float3 n, float3 v, float alpha, float f0, bool tiled)
{
    // Specular anti-aliasing (Kaplanyan 2016): a rippled surface's normals vary across the pixel by about half
    // g_spread; the lobe widens by that variance, its peak lowered to keep the energy, so one-pixel sun glints
    // become a smooth glitter path.
    alpha = min(sqrt(alpha * alpha + 0.5f * g_spread * g_spread), 1.0f);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    if (any(g_sun_light.rgb > 0.0f)) {
        const float k = ggx_highlight(n, v, g_sun.xyz, alpha, g_sun.w, f0);
        if (k > 1e-4f && lit_from(origin, g_sun.xyz, g_sun_light.w)) {
            sum += g_sun_light.rgb * k;
        }
    }
    float3 local = float3(0.0f, 0.0f, 0.0f);
    Highlight top[3];
    [unroll] for (uint t = 0u; t < 3u; ++t) {
        top[t].light = float3(0.0f, 0.0f, 0.0f);
        top[t].weight = 0.0f;
        top[t].to = float3(0.0f, 0.0f, 1.0f);
        top[t].end = 0.0f;
    }
    [loop] for (uint i = 0u; i < g_point_count; ++i) {
        const float3 to = g_points[2u * i].xyz - p;
        const float d = length(to);
        const float3 c = g_points[2u * i + 1u].rgb * (point_falloff(d * d, i) * g_point_light) *
                         ggx_highlight(n, v, to / max(d, 1e-4f), alpha, g_point_size / max(d, 1e-4f), f0);
        local += c;
        Highlight h;
        h.light = c;
        h.weight = luminance(c);
        h.to = to;
        h.end = d - g_point_size;
        keep_highlight(top, h);
    }
    // `tiled`: the group's tile list (CULL_TILE_LIGHTS); else every light.
    const uint total = (g_flags & 16u) == 0u ? 0u : min(g_emitters.Load(0u), kTileWords * 32u);
    if (total != 0u && (!tiled || s_tile_count != 0u)) {
        // Its reach as the lighting's (rt_lighting.hlsl emit_reach2): light / (pi d^2) above
        // kGlowCutoff, at most kGlowReach; the lights beyond it add nothing here (the tile's list).
        [loop] for (uint word = 0u; word < kTileWords && word * 32u < total; ++word) {
        const uint left = total - word * 32u;
        [loop] for (uint bits = tiled ? s_tile_mask[word] : left >= 32u ? 0xFFFFFFFFu : (1u << left) - 1u; bits != 0u;
                    bits &= bits - 1u) {
            const uint j = word * 32u + firstbitlow(bits);
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
            Highlight h;
            h.light = c;
            h.weight = luminance(c);
            h.to = to;
            h.end = d - max(0.9f * la.w, 2.0f + 0.01f * d);
            keep_highlight(top, h);
        }
        }
    }
    // The two strongest each take a shadow ray, counted by their lead over the third (as the lighting's
    // rank_confidence); the rest are unshadowed. With a ray for the strongest alone, a highlight's shadow switches
    // on and off where two lights' highlights cross.
    [unroll] for (uint r = 0u; r < 2u; ++r) {
        const float confidence = top[r].weight > 0.0f ? saturate((top[r].weight - top[2].weight) / (0.5f * top[r].weight))
                                                      : 0.0f;
        if (confidence > 0.0f) {
            const float span = length(top[r].to);
            if (!lit_from(origin, top[r].to / max(span, 1e-4f), top[r].end)) {
                local -= top[r].light * confidence;
            }
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
// Counted apart (stats words 30, 31): a hit the frame hides lit by its draw's colour; the captured sky.
static const uint kFromDraw = 7u;
static const uint kSkyCaptured = 8u;

bool sky_source(uint source)
{
    return source >= kSkyShown && source != kFromDraw;
}

float3 hit_rgb8(uint c)
{
    return float3(float(c >> 24u), float((c >> 16u) & 0xFFu), float((c >> 8u) & 0xFFu)) / 255.0f;
}

// The sky along `l` as the reflections hold it: the captured sky, else the lighting's sky map, else the sky's average;
// at least the ambient's fill under physical light (g_sky_fill).
float3 sky_behind(float3 l)
{
    float3 light;
    if (sky_captured(l, light) > 0.05f) {
        return max(light, g_sky_fill);
    }
    if (sky_map_light(l, light) > 0.05f) {
        return max(light, g_sky_fill);
    }
    return max(average(0u, light) ? light : float3(0.0f, 0.0f, 0.0f), g_sky_fill);
}

#if RT_TEXTURED
// Physical light at the hits off screen (g_flags bit 12 with the textured hits): the hit lit as the lighting lights
// the scene (rt_lighting.hlsl physical_sky_irradiance, physical_exposure; keep in step):
// its draw's TEV program the albedo (a table light source its radiance), the sun behind a shadow ray, the traced sky's
// irradiance, the game's point lights at their own falloff, and the emitting triangles by resampled importance with one
// shadow ray. A mirror by night shows the lamp-lit surfaces behind the camera, not a flat night level.
static const float kDelightRange = 1.25f;      // rt_lighting.hlsl kDelightRange
static const uint kEmitTriangles = 65536u;     // ray_tracing.cpp kEmitTriangles
static const uint kGlowEmitters = 16384u;      // rt_lighting.hlsl g_emit_tris layout
static const uint kEmitRunning = 1u + 4u * kEmitTriangles;
static const uint kGlowElements = kEmitRunning + kEmitTriangles + kGlowEmitters;
static const uint kSkyMapSize = 64u;           // rt_lighting.hlsl kSkyMapSize
static const uint kDrawStride = 64u;           // rt_lighting.hlsl kDrawStride
static const uint kEmitArea = 2u;              // rt_lighting.hlsl kEmit*: kind (low 2 bits), named, never a light
static const uint kEmitNamed = 4u;
static const uint kEmitNoLight = 8u;

ByteAddressBuffer physical_tev()
{
    ByteAddressBuffer records = ResourceDescriptorHeap[g_tev_view];
    return records;
}

float4 physical_emit_element(uint e)
{
    ByteAddressBuffer emitters = ResourceDescriptorHeap[g_emit_view];
    return asfloat(emitters.Load4(e * 16u));
}

float4 physical_sky_texel(uint i)
{
    ByteAddressBuffer sky = ResourceDescriptorHeap[g_sky_view];
    return asfloat(sky.Load4(i * 16u));
}

#define g_tev physical_tev()
#define g_wrap_point g_wrap
#define g_wrap_linear g_wrap
#include "rt_tev.hlsli"
#undef g_wrap_linear
#undef g_wrap_point
#undef g_tev

float3 physical_world(float3 d)
{
    return normalize(float3(dot(g_eye_to_world[0].xyz, d), dot(g_eye_to_world[1].xyz, d), dot(g_eye_to_world[2].xyz, d)));
}

void physical_sh9(float3 d, out float basis[9])
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

bool physical_sky_traced()
{
    return g_full_light.w != 0.0f && g_sky_view != 0xFFFFFFFFu;
}

float physical_sky_mean()
{
    return luminance(physical_sky_texel(kSkyMapSize * kSkyMapSize).rgb * 0.282095f);
}

float physical_sky_own()
{
    const float mean = physical_sky_mean();
    const float ambient = luminance(g_sky_radiance.rgb);
    return mean > ambient && mean > 1e-4f ? ambient / mean : 1.0f;
}

float3 physical_sky_fill()
{
    const float ambient = luminance(g_sky_radiance.rgb);
    return ambient > 1e-4f ? g_sky_radiance.rgb * saturate(1.0f - physical_sky_mean() * physical_sky_own() / ambient)
                           : float3(0.0f, 0.0f, 0.0f);
}

float physical_overcast_irradiance(float3 n)
{
    if (g_sky_radiance.w <= 0.0f) {
        return 1.0f;
    }
    const float c = clamp(dot(n, g_up.xyz), -1.0f, 1.0f);
    const float a = acos(c);
    return 3.0f / 7.0f + (6.0f / 7.0f) * (2.0f * ((kPi - a) * c + sin(a)) / (3.0f * kPi));
}

// The open sky's irradiance over pi on a surface facing eye-space `n`.
float3 physical_sky_irradiance(float3 n)
{
    if (!physical_sky_traced()) {
        return g_sky_radiance.rgb * physical_overcast_irradiance(n);
    }
    float basis[9];
    physical_sh9(physical_world(n), basis);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint b = 0u; b < 9u; ++b) {
        irradiance += physical_sky_texel(kSkyMapSize * kSkyMapSize + b).rgb *
                      (basis[b] * (b == 0u ? 1.0f : b < 4u ? 2.0f / 3.0f : 0.25f));
    }
    return max(irradiance, 0.0f) * physical_sky_own() + physical_sky_fill() * physical_overcast_irradiance(n);
}

// The eye's adaptation (rt_lighting.hlsl kAdaptation in the picture sums).
float physical_adaptation()
{
    const uint at = 64u + 8u * 16u + 16u;
    return (g_flags & 4u) != 0u && g_sums.Load(at + 4u) != 0u ? asfloat(g_sums.Load(at)) : 1.0f;
}

float physical_exposure()
{
    if (g_light_fit.x > 0.0f) {
        return g_light_fit.x * physical_adaptation();
    }
    const float sun = max(dot(g_up.xyz, g_sun.xyz), 0.5f);
    return luminance(g_full_light.rgb) /
           max(luminance(physical_sky_irradiance(g_up.xyz)) + luminance(g_sun_light.rgb) * sun, 1e-4f) *
           physical_adaptation();
}

uint physical_hash(uint v)
{
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float physical_random(inout uint seed)
{
    seed = physical_hash(seed);
    return float(seed >> 8u) * (1.0f / 16777216.0f);
}

// The emitting triangles' light at `p` (normal `n`, shadow ray from `origin`), picked by resampled importance.
float3 physical_emitters(float3 p, float3 n, float3 origin, uint candidates, uint seed)
{
    if (g_emit_count == 0u || g_emit_view == 0xFFFFFFFFu) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float total = physical_emit_element(0u).y;
    if (!(total > 0.0f)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    ByteAddressBuffer emitters = ResourceDescriptorHeap[g_emit_view];
    float weight_sum = 0.0f;
    float kept_target = 0.0f;
    float3 kept_light = float3(0.0f, 0.0f, 0.0f);
    float3 kept_point = float3(0.0f, 0.0f, 0.0f);
    [loop] for (uint k = 0u; k < candidates; ++k) {
        const float u = physical_random(seed) * total;
        uint lo = 0u;
        uint hi = g_emit_count - 1u;
        [loop] while (lo < hi) {
            const uint mid = (lo + hi) >> 1u;
            if (asfloat(emitters.Load((kEmitRunning + mid) * 16u)) > u) {
                hi = mid;
            } else {
                lo = mid + 1u;
            }
        }
        // A scene triangle as EmitTriangleCS wrote it, a glow's (from g_emit_scene on) moved from the world.
        const bool glow = lo >= g_emit_scene;
        const uint e = glow ? kGlowElements + 4u * (lo - g_emit_scene) : 1u + 4u * lo;
        float4 a = physical_emit_element(e);
        float4 b = physical_emit_element(e + 1u);
        float4 c = physical_emit_element(e + 2u);
        const float4 d = physical_emit_element(e + 3u);
        if (glow) {
            a.xyz = float3(dot(g_world_to_eye[0].xyz, a.xyz) + g_world_to_eye[0].w,
                           dot(g_world_to_eye[1].xyz, a.xyz) + g_world_to_eye[1].w,
                           dot(g_world_to_eye[2].xyz, a.xyz) + g_world_to_eye[2].w);
            b.xyz = float3(dot(g_world_to_eye[0].xyz, b.xyz), dot(g_world_to_eye[1].xyz, b.xyz),
                           dot(g_world_to_eye[2].xyz, b.xyz));
            c.xyz = float3(dot(g_world_to_eye[0].xyz, c.xyz), dot(g_world_to_eye[1].xyz, c.xyz),
                           dot(g_world_to_eye[2].xyz, c.xyz));
        }
        if (!(d.w > 0.0f) || !(a.w > 0.0f)) {
            continue;
        }
        float3 y;
        float3 face;
        // An isotropic glow (b.w 2: a sprite standing for a glowing volume) is the sphere of its power about the
        // triangle's centre, its point on the hemisphere facing `p` (rt_engine.hlsl engine_light_point): one-sided
        // there. Sides (b.w): 0 one-sided, 1 two-sided (rt_lighting.hlsl emitter_facing).
        const bool sphere = b.w >= 1.5f;
        if (sphere) {
            const float3 centre = a.xyz + (b.xyz + c.xyz) * (1.0f / 3.0f);
            const float3 axis = normalize(p - centre + float3(0.0f, 0.0f, 1e-4f));
            const float z = physical_random(seed);
            const float phi = 2.0f * kPi * physical_random(seed);
            const float sz = sqrt(max(0.0f, 1.0f - z * z));
            const float3 helper = abs(axis.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
            const float3 t0 = normalize(cross(helper, axis));
            const float3 t1 = cross(axis, t0);
            face = t0 * (sz * cos(phi)) + t1 * (sz * sin(phi)) + axis * z;
            y = centre + face * sqrt(a.w / (2.0f * kPi));
        } else {
            float r1 = physical_random(seed);
            float r2 = physical_random(seed);
            if (r1 + r2 > 1.0f) {
                r1 = 1.0f - r1;
                r2 = 1.0f - r2;
            }
            y = a.xyz + b.xyz * r1 + c.xyz * r2;
            face = normalize(cross(b.xyz, c.xyz));
        }
        const float3 to = y - p;
        const float d2 = max(dot(to, to), 1e-4f);
        const float3 dir = to * rsqrt(d2);
        const float cos_light = !sphere && b.w > 0.0f ? abs(-dot(face, dir)) : -dot(face, dir);
        const float cos_surface = dot(n, dir);
        if (cos_light <= 0.0f || cos_surface <= 0.0f) {
            continue;
        }
        const float3 light = d.rgb * ((c.w >= 1.0f ? pow(cos_light, c.w) : cos_light) * cos_surface / (kPi * d2));
        const float chance = d.w / (total * a.w);
        const float target = luminance(light);
        const float w = target / chance;
        weight_sum += w;
        if (w > 0.0f && physical_random(seed) * weight_sum < w) {
            kept_target = target;
            kept_light = light;
            kept_point = y;
        }
    }
    if (!(kept_target > 0.0f)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float3 to = kept_point - origin;
    const float span = length(to);
    if (!lit_from(origin, to / max(span, 1e-4f), max(span * 0.999f - 0.5f, 0.0f))) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return kept_light * (weight_sum / (float(candidates) * kept_target));
}

// A hit's light in the picture's units (physical_encode), lit physically; false without the lighting's records.
bool physical_hit_light(uint tri, float2 b, float3 x, float3 l, out float3 light)
{
    light = float3(0.0f, 0.0f, 0.0f);
    if ((g_flags & kReflectPhysical) == 0u || g_tev_view == 0xFFFFFFFFu) {
        return false;
    }
    const uint draw = g_surfaces.Load(tri * 16u + 12u) >> 16u;
    bool pass;
    float3 albedo = srgb_to_linear(saturate(tev_evaluate(draw, g_tev_count, tri, b, pass).rgb));
    float3 emission = float3(0.0f, 0.0f, 0.0f);
    if ((g_flags & 128u) != 0u && draw < g_draw_count) {
        // A light source of the table shows what it sends (EmitTriangleCS: brighter by its intensity past 1).
        const uint2 table = g_draws.Load2(draw * kDrawStride + 8u);
        if ((table.x & kEmitNamed) != 0u && (table.x & 3u) != 0u && (table.x & kEmitNoLight) == 0u &&
            tev_glows_at(draw, g_tev_count, tri, b)) {
            emission = albedo * max(asfloat(table.y), 1.0f);
            albedo = float3(0.0f, 0.0f, 0.0f);
        }
    }
    const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
    const float3 v1 = asfloat(g_positions.Load3(tri * 36u + 12u));
    const float3 v2 = asfloat(g_positions.Load3(tri * 36u + 24u));
    float3 ng = cross(v1 - v0, v2 - v0);
    ng = dot(ng, ng) > 1e-24f ? normalize(ng) : -l;
    ng = dot(ng, l) > 0.0f ? -ng : ng;
    // The vertex normals of the side the ray meets.
    float3 n = ng;
    {
        const uint4 record = g_surfaces.Load4(tri * 16u);
        if (record.x != 0u && record.y != 0u && record.z != 0u) {
            const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
            float3 smooth = unpack_vertex_normal(record.x) * w.x + unpack_vertex_normal(record.y) * w.y +
                            unpack_vertex_normal(record.z) * w.z;
            smooth *= dot(smooth, ng) < 0.0f ? -1.0f : 1.0f;
            const float smooth_length = length(smooth);
            if (smooth_length > 1e-3f && dot(smooth, ng) > 0.05f * smooth_length) {
                n = smooth / smooth_length;
            }
        }
    }
    const float3 origin = x + ng * (0.2f + abs(x.z) * 4e-4f);
    float3 sun = float3(0.0f, 0.0f, 0.0f);
    const float nl = dot(n, g_sun.xyz);
    if (nl > 0.0f && any(g_sun_light.rgb > 0.0f) && lit_from(origin, g_sun.xyz, g_sun_light.w)) {
        sun = g_sun_light.rgb * nl;
    }
    // The game's point lights at their own falloff (no reach window under physical light), unshadowed.
    float3 points = float3(0.0f, 0.0f, 0.0f);
    [loop] for (uint i = 0u; i < g_point_count; ++i) {
        const float3 to = g_points[2u * i].xyz - x;
        const float d2 = dot(to, to);
        points += g_points[2u * i + 1u].rgb * (saturate(dot(n, to) * rsqrt(max(d2, 1e-6f))) /
                                               (1.0f + d2 * g_points[2u * i + 1u].w));
    }
    uint seed = physical_hash(tri ^ physical_hash(asuint(x.x) ^ (g_frame * 0x9E3779B9u)));
    const float3 emitted = physical_emitters(x, n, origin, 2u, seed);
    light = physical_encode(((albedo * (sun + physical_sky_irradiance(n) + points * g_point_light + emitted)) + emission) *
                            physical_exposure());
    return true;
}
#endif

// High quality: what a reflection ray hits where the frame does not show it, shaded by ray tracing, so water and
// metal reflect what is around them, not only what the screen holds. Its draw's material texture at the hit's
// texture coordinates (detail by distance), times the vertices' colour 0 or the draw's colour, at the art's level:
// by day the day model's open ground, faces turned from the sun at 0.7 and in the sun's shadow at 0.65 of it (one
// shadow ray from the hit), by night the night level; the point lights add their light as the lighting adds it.
// Late geometry (instance 1) has no records: false.
bool hit_shade(uint instance, uint tri, float2 b, float3 x, float3 l, float distance, out float3 light,
               out float coverage)
{
    light = float3(0.0f, 0.0f, 0.0f);
    coverage = 1.0f;
#if RT_TEXTURED
    if ((g_flags & 2048u) == 0u || instance != 0u) {
        return false;
    }
    const uint draw = g_surfaces.Load(tri * 16u + 12u) >> 16u;
    if (draw >= g_texture_count) {
        return false;
    }
    const uint4 d = g_draw_tex[draw];
    const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
    const uint a = tri * 72u;   // rt_types.h kRtTriUvStride
    const float4 uv01 = asfloat(g_tri_uv.Load4(a));
    const float2 uv = uv01.xy * w.x + uv01.zw * w.y + asfloat(g_tri_uv.Load2(a + 16u)) * w.z;
    // Physical light: lit as the lighting lights the scene; a cut-out keeps its coverage below.
    float3 physical;
    const bool lit_physically = physical_hit_light(tri, b, x, l, physical);
    const uint3 colours = g_tri_uv.Load3(a + 48u);
    float3 albedo = hit_rgb8(d.z);
    if ((d.y & 1u) != 0u && (colours.x | colours.y | colours.z) != 0u) {
        albedo = hit_rgb8(colours.x) * w.x + hit_rgb8(colours.y) * w.y + hit_rgb8(colours.z) * w.z;
    }
    if (d.x != 0xFFFFFFFFu && all(abs(uv) < 1.0e4f)) {
        const Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(d.x)];
        // Coarser where a rippled surface's rays spread (g_spread over the pixel's own angle).
        const float pixel_angle = 2.0f / max(abs(g_projection.x) * g_viewport.z, 1e-4f);
        // Water's reflections also read over their footprint of the game's own pixels (g_reflect_blur, EFB pixels):
        // at lod 0 the reflection of what the frame does not show aliases into grain.
        const float lod = clamp(log2(max(distance * g_hit.w * (1.0f + g_spread / pixel_angle + g_reflect_blur), 1.0f)),
                                0.0f, 8.0f);
        float3 texel = map.SampleLevel(g_wrap, uv, lod).rgb;
        // A cut-out (alpha-tested: d.w the alpha map and its reference): its coverage over the footprint, the
        // alpha test's edge at full detail turning into the mip's mean alpha (the opaque share) as the footprint
        // grows; where the colour map is the alpha map its holes' colour (black) leaves the opaque texels' colour.
        if (d.w != 0u) {
            const Texture2D<float4> alpha_map = ResourceDescriptorHeap[NonUniformResourceIndex(d.w & 0xFFFFFFu)];
            const float a = alpha_map.SampleLevel(g_wrap, uv, lod).a;
            const float reference = float(d.w >> 24u) / 255.0f;
            coverage = saturate(lerp(smoothstep(reference - 0.12f, reference + 0.12f, a), a, saturate(lod)));
            if ((d.w & 0xFFFFFFu) == d.x) {
                texel = saturate(texel / max(a, 0.25f));
            }
        }
        albedo *= texel;
    }
    if (lit_physically) {
        light = physical;
        return true;
    }
    const float3 v0 = asfloat(g_positions.Load3(tri * 36u));
    const float3 v1 = asfloat(g_positions.Load3(tri * 36u + 12u));
    const float3 v2 = asfloat(g_positions.Load3(tri * 36u + 24u));
    float3 ng = cross(v1 - v0, v2 - v0);
    ng = dot(ng, ng) > 1e-12f ? normalize(ng) : -l;
    ng = dot(ng, l) > 0.0f ? -ng : ng;
    float day_level = g_hit.z;
    float sunlit = 0.0f;
    if (g_hit.x > 0.0f && any(g_sun_light.rgb > 0.0f)) {
        const float nl = dot(ng, g_sun.xyz);
        const float shaped = nl > 0.0f ? nl * (1.5f - 0.5f * nl) : 0.0f;
        float vis = 1.0f;
        if (shaped > 0.08f) {
            RayDesc ray;
            ray.Origin = x + ng * (0.5f + abs(x.z) * 4e-4f);
            ray.Direction = g_sun.xyz;
            ray.TMin = 0.0f;
            ray.TMax = g_sun_light.w;
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                     RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
            query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
            query.Proceed();
            vis = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0f : 1.0f;
        }
        day_level *= (1.0f - 0.35f * (1.0f - vis)) * (1.0f - 0.3f * (1.0f - saturate(shaped / 0.8f)));
        sunlit = vis * saturate(shaped / 0.5f);
    }
    // The point lights on the hit, unshadowed, as the lighting adds them: by night under the soft cap that rises where
    // the star core's light rules, by day half of them rolling off on sunlit ground.
    float3 points = float3(0.0f, 0.0f, 0.0f);
    float core_weight = 0.0f;
    float all_weight = 0.0f;
    [loop] for (uint i = 0u; i < g_point_count; ++i) {
        const float3 to = g_points[2u * i].xyz - x;
        const float d2 = dot(to, to);
        const float3 c = g_points[2u * i + 1u].rgb *
                         (saturate(dot(ng, to) * rsqrt(max(d2, 1e-6f))) * point_falloff(d2, i));
        points += c;
        all_weight += luminance(c);
        core_weight += i == g_hit_core ? luminance(c) : 0.0f;
    }
    points *= g_point_light;
    const float cap = lerp(g_hit_cap, max(g_hit_sun_cap, g_hit_cap), all_weight > 0.0f ? core_weight / all_weight : 0.0f);
    const float3 night_lights = points * g_hit_night_lights;
    const float3 night = g_hit.y + soft_light(night_lights, max(cap, 1e-3f));
    const float3 day_lights = points * 0.5f;
    const float3 day = day_level + day_lights / (1.0f + luminance(day_lights) / lerp(1.0f, 0.35f, sunlit));
    light = albedo * lerp(night, day, g_hit.x);
    return true;
#else
    return false;
#endif
}

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

// What a metal (metalness `metal`) reflects of `light` in its surface's colour (the hue of `base`, luminance 1), as
// the game draws metal (an intensity environment map times the material colour): a pale metal (silver, steel) shows
// the world's own colours through its hue, a strongly coloured one (gold, Bowser's green arches) the world's
// brightness in its hue, since a plain product with a world of the opposite colour turns dark and muddy. Towards
// white at grazing angles (Schlick; `vm` the cosine between the eye and the reflecting facet).
float3 metal_reflect(float3 light, float3 base, float vm, float metal)
{
    const float3 hue = min(base / max(luminance(base), 0.1f), 3.0f);
    const float top = max(hue.r, max(hue.g, hue.b));
    const float saturation = top > 1e-3f ? (top - min(hue.r, min(hue.g, hue.b))) / top : 0.0f;
    const float3 coloured = lerp(light * hue, luminance(light) * hue, saturation);
    const float graze = 1.0f - saturate(vm);
    return lerp(light, lerp(coloured, light, graze * graze * graze * graze * graze), metal);
}

Reflected reflect_light(uint2 s, float3 dir, float3 n, float3 ng, float3 hit, float4 params, float3 base,
                        bool lights, bool tiled, bool painted_metal)
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
    const float3 facet = t1 * h.x + t2 * h.y + n * h.z;
    float3 l = reflect(-v, facet);
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
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, reflection);
    while (query.Proceed()) {
        if (!passes_through(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(),
                            query.CandidateTriangleBarycentrics())) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    r.distance = g_range;
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        r.distance = query.CommittedRayT();
        const float3 x = reflection.Origin + l * r.distance;
        const float x_distance = length(x);
        float seen;
        // A rippled surface's rays spread over r.distance * g_spread around the hit: its pixels there.
        const float blur = max(g_spread > 0.0f ? min(r.distance * g_spread / pixel_size(x.z), 12.0f) : 0.0f,
                               g_reflect_blur);
        // The late geometry (instance 1: Rosalina, drawn after the capture) has no records for the shading below:
        // the picture shows her within her depth (the side the camera sees, not the side a mirror under her sees).
        const float match = query.CommittedInstanceID() != 0u ? 0.08f * x_distance + 60.0f : 0.03f * x_distance + 2.0f;
        const bool shown = x.z < -g_near && shown_at(eye_to_pixel(g_projection, g_viewport, x), r.light, seen, blur) &&
                                 abs(seen - x_distance) < match;
        const uint hit_draw = query.CommittedInstanceID() == 0u
                                  ? g_surfaces.Load(query.CommittedPrimitiveIndex() * 16u + 12u) >> 16u : 0xFFFFu;
        float3 drawn;
        float cover = 1.0f;
        if ((g_flags & 128u) != 0u && hit_draw < g_draw_count && (g_draws.Load(hit_draw * 48u + 24u) & 8u) != 0u) {
            // A star's burning ball as the composite shades it: the frame shows it before the lighting brightens it.
            r.light = star_reflected(x, l, shown ? r.light : float3(1.0f, 0.72f, 0.38f));
            r.confidence = 1.0f;
        } else if (shown) {
            r.confidence = 1.0f;
        } else if (hit_shade(query.CommittedInstanceID(), query.CommittedPrimitiveIndex(),
                             query.CommittedTriangleBarycentrics(), x, l, r.distance, r.light, cover)) {
            // High quality: shaded where it was hit (counted with the remembered); a cut-out by its coverage, the
            // sky behind its open share.
            r.light = cover < 0.999f ? lerp(sky_behind(l), r.light, cover) : r.light;
            r.confidence = 1.0f;
            r.source = kFromMemory;
        } else {
            float3 known;
            r.confidence = memory_light(x, x_distance, known);
            r.source = r.confidence > 0.05f ? kFromMemory : kUnknown;
            if (r.source == kFromMemory) {
                r.light = known;
            } else if ((g_flags & 32u) == 0u && query.CommittedInstanceID() == 0u &&
                       draw_light(g_surfaces.Load(query.CommittedPrimitiveIndex() * 16u + 12u) >> 16u, drawn)) {
                // The object it hit, as the frame shows it elsewhere: its colour at its distance, without its
                // detail (the frame does not show this side of it). The late geometry (instance 1, drawn after
                // the trace) has no records here.
                r.source = kFromDraw;
                r.light = drawn;
                r.confidence = g_draw_trust;
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
        // The sky lies at infinity: a rippled surface's rays spread over g_spread of it, its pixels that angle.
        const float sky_blur = max(g_spread > 0.0f
            ? min(g_spread * max(abs(g_projection.x) * g_viewport.z, 1e-4f) * 0.5f, 12.0f) : 0.0f, g_reflect_blur);
        const bool sky_shown = l.z < -1e-3f &&
                               shown_at(eye_to_pixel(g_projection, g_viewport, l), r.light, seen, sky_blur) &&
                               seen >= kSkyDistance;
        r.source = kSkyShown;
        float3 captured;
        const float captured_share = sky_shown ? 0.0f : sky_captured(l, captured);
        if (sky_shown) {
            r.confidence = 1.0f;
        } else if (captured_share > 0.05f && g_sky_trust > 0.0f) {
            r.source = kSkyCaptured;
            r.light = captured;
            r.confidence = captured_share * g_sky_trust;
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
    // Physical light: the sky is never darker than the ambient's fill (g_sky_fill), as the lighting lights a surface.
    if (sky_source(r.source)) {
        r.light = max(r.light, g_sky_fill);
    }
    // A mirror shows the sky as sharp as the frame or the capture holds it; the 64^2 memory, the sky map and
    // the sky's average are blurrier than the game's painted reflection, which keeps part of their place (g_metal_sky:
    // 1 where the painted reflection is taken out; half where it stays, as on a metal whose colour is paint).
    if (params.w > 0.0f && sky_source(r.source) && r.source != kSkyShown && r.source != kSkyCaptured) {
        r.confidence *= painted_metal ? min(g_metal_sky, 0.5f) : g_metal_sky;
    }
    // The lights' highlights, weighted like the reflection (its class strength) so the
    // composite's Fresnel weight is divided out here.
    if (lights) {
        r.highlight = highlights(s, hit, reflection.Origin, n, v, params.x, params.z, tiled) * params.y;
    }
    if (params.w > 0.0f) {
        // Metal: the reflection in the surface's colour (metal_reflect).
        r.light = metal_reflect(r.light, base, dot(v, facet), params.w);
        r.highlight = metal_reflect(r.highlight, base, dot(v, facet), params.w);
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

// g_draws word 6 bit 6 (ray_tracing.cpp kDrawTableMirror): the table's `mirror` on an opaque surface.
static const uint kDrawTableMirror = 64u;
// A mirror's metalness in the composite: Fresnel 0.75 face-on (the polished class's 0.25, 0.77 of the way to metal's
// 0.9), its reflection over 0.8 of the painted colour (that class's cover).
static const float kMirrorF0Share = 0.77f;
static const uint kMirrorClass = 4u;

bool mirror_draw(uint draw)
{
    return (g_flags & 128u) != 0u && draw < g_draw_count && (g_draws.Load(draw * 48u + 24u) & kDrawTableMirror) != 0u;
}

// A draw's reflection parameters (GGX alpha, strength, F0, metalness) and perceptual roughness: its
// class's, with the material table's roughness and metalness for its texture where it names them. A
// surface rougher than g_rough_cutoff reflects nothing worth a ray; below it, metal and dielectric
// alike (a dielectric's Fresnel term keeps its reflection to grazing views).
// A metal the table names (its metalness) becomes bare metal (`surface_class` kBareMetalClass).
float4 draw_material(inout uint surface_class, uint draw, out float roughness)
{
    float4 params = g_class[surface_class];
    roughness = sqrt(params.x);
    if ((g_flags & 128u) == 0u || draw >= g_draw_count) {
        return params;
    }
    const uint words = g_draws.Load(draw * 48u + 16u);
    const uint r16 = words & 0xFFFFu;
    const uint m16 = words >> 16u;
    if (m16 != 0xFFFFu) {
        // The table's light sources (its emission: coins, the giant ? coin) keep their painted look and own
        // glow; as bare metal their glow would give way to the dark sky they mirror.
        const uint emission = g_draws.Load(draw * 48u + 8u);
        const bool emits = (emission & 4u) != 0u && (emission & 3u) != 0u && (emission & 8u) == 0u;
        if (surface_class == kMetalClass && !emits) {
            surface_class = kBareMetalClass;
            params = g_class[kBareMetalClass];
            roughness = sqrt(params.x);
        }
        const float metal = float(m16) / 65534.0f;
        params.z = lerp(surface_class == kBareMetalClass ? kDielectricF0 : params.z, g_metal_f0, metal);
        params.w = metal;
    }
    if (r16 != 0xFFFFu) {
        roughness = float(r16) / 65534.0f;
        if (params.w >= 0.5f) {
            roughness = min(roughness, kMetalRoughnessMax);
        }
        params.x = max(roughness * roughness, 1e-3f);
        params.y = saturate((g_rough_cutoff - roughness) / 0.2f);
    }
    return params;
}

// Diagnostic views (GALAXY_RT_REFLECT_VIEW=7, 8): the composite shows each sample's light alone.
static const uint kViewRow = 512u;
static const uint kViewSource = 1024u;

// GALAXY_RT_REFLECT_VIEW=7: the draw's material table roughness word as three hexadecimal digits, one per
// channel at (digit + 0.5) / 16 (a probe table writes row k's roughness as k / 65534); black: no row.
float3 row_colour(uint draw)
{
    if ((g_flags & 128u) == 0u || draw >= g_draw_count) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const uint r16 = g_draws.Load(draw * 48u + 16u) & 0xFFFFu;
    if (r16 == 0xFFFFu || r16 == 0u) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return (float3(uint3(r16 >> 8u, r16 >> 4u, r16) & 15u) + 0.5f) / 16.0f;
}

// GALAXY_RT_REFLECT_VIEW=8: where a reflection's light came from (Reflected.source; the sky map apart):
// hit shown green, remembered yellow, unknown red, probe magenta, its draw's colour orange; sky shown
// blue, captured dark cyan, remembered cyan, mapped light blue, average white.
float3 source_colour(uint source, bool mapped)
{
    const float3 colours[9] = {float3(0.0f, 1.0f, 0.0f), float3(1.0f, 1.0f, 0.0f), float3(1.0f, 0.0f, 0.0f),
                               float3(1.0f, 0.0f, 1.0f), float3(0.0f, 0.0f, 1.0f), float3(0.0f, 1.0f, 1.0f),
                               float3(1.0f, 1.0f, 1.0f), float3(1.0f, 0.5f, 0.0f), float3(0.0f, 0.5f, 0.5f)};
    return mapped ? float3(0.5f, 0.5f, 1.0f) : colours[min(source, 8u)];
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
            const uint tri = (primary.x & kPrimaryTriangle) - 1u;
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
            // A table metal whose colour no metal has (g_primary's top bit: the Sling Pod frames' and the Dreadnought
            // panels' blue, which their environment stage paints) keeps the game's painted look as its colour
            // (EngineGBufferCS). Its reflection stays the metal's, tinted by that look (metal_reflect); as a
            // dielectric's untinted coat it washed the blue out to white.
            const bool painted_metal = (primary.x & kPrimaryPainted) != 0u;
            float4 params = draw_material(surface_class, record.w >> 16u, roughness);
            // A mirror (the table's `mirror`): clear and polished, its reflection untinted (ray_params), Fresnel 0.75
            // face-on in the composite (kMirrorF0Share, its metalness share there).
            float4 ray_params = params;
            if (mirror_draw(record.w >> 16u)) {
                surface_class = kMirrorClass;
                roughness = 0.02f;
                params = g_class[kMirrorClass];
                params.x = roughness * roughness;
                params.y = 1.0f;
                ray_params = params;
                ray_params.w = 0.0f;
                params.w = kMirrorF0Share;
            }
            const float3 base = g_color.Load(int3(q, 0)).rgb;
            // Lamps, lava, glowing cores and what glows light keep their own colour: bright unlit
            // surfaces, bright saturated indirect ones (lava; white ice reflects), and anything lit far
            // past white.
            const float3 lit = base * lighting_at(q);
            const float peak = max(lit.r, max(lit.g, lit.b));
            const float saturation = (peak - min(lit.r, min(lit.g, lit.b))) / max(peak, 1e-3f);
            // Metal emits nothing: a bright metal is bright by what it reflects. Counted as an emitter, a metal
            // drawn by its environment map alone turns black under physical light, which takes its painted
            // reflection out.
            const bool metal_class = surface_class == kMetalClass || surface_class == kBareMetalClass;
            const float emitter = surface_class == 3u ? smoothstep(0.75f, 1.0f, peak)
                                : surface_class == 4u ? smoothstep(0.75f, 1.0f, peak) * smoothstep(0.35f, 0.6f, saturation)
                                : metal_class         ? 0.0f
                                                      : smoothstep(0.95f, 1.2f, peak);
            info.x = pack_normal(n);
            info.z = pack_material(surface_class, params.y, 1.0f - emitter, roughness, params.w);
            if ((g_flags & kViewRow) != 0u) {
                info.z = pack_material(surface_class, 1.0f, 1.0f, 0.0f, 0.0f);
                reflected = true;
                light = row_colour(record.w >> 16u);
                confidence = 1.0f;
            } else if (params.y > 0.0f && emitter < 1.0f) {
                reflected = true;
                const Reflected r = reflect_light(id.xy, dir, n, ng, hit, ray_params, base, true, true, painted_metal);
                light = r.light;
                highlight = r.highlight;
                // Painted dielectrics and clear ones (ice, crystal, opaque water) take the sky's reflection
                // at half its confidence: what stands around them reads in their reflection, the bright sky
                // or aurora only half. Clear crystal and glass take it whole (their painted look stays under
                // it; GALAXY_RT_REFLECT_CRYSTAL F0 0.35).
                const bool painted = (surface_class == 0u || surface_class == 1u || surface_class == 3u) && params.w < 0.5f;
                const bool clear = surface_class == 4u;
                confidence = (painted || clear) && sky_source(r.source) ? r.confidence * 0.5f : r.confidence;
                // A metal's ray that leaves the scene sees at least the metal's own environment map, the probe the game
                // made for it (its painted look, base): the traced world where the ray meets it, never black space.
                if ((metal_class || surface_class == 7u) && sky_source(r.source)) {
                    light = max(light, base);
                }
                // Bare metal is its reflection (the class's cover): where the traced light is unknown it shows
                // the game's own painted reflection at the game's level, which the lighting's shade, occlusion
                // and night level would otherwise darken (its composite multiplies the whole colour).
                const float own = surface_class == kBareMetalClass ? g_metal_cover * params.w : 0.0f;
                light = lerp(light, base, own * (1.0f - saturate(confidence)));
                confidence = lerp(confidence, 1.0f, own);
                source = r.source;
                mapped = r.mapped;
                info.w = asuint(r.distance);
                if ((g_flags & kViewSource) != 0u) {
                    light = source_colour(source, mapped);
                    highlight = float3(0.0f, 0.0f, 0.0f);
                    confidence = 1.0f;
                }
            } else if (emitter < 1.0f && (g_flags & 128u) != 0u) {
                // Too rough to reflect anything worth a ray: the lights' highlights alone (a broad
                // sheen under the sun, a soft glint near a lamp), at the least strength that keeps the
                // sample in the accumulation and filters; no reflected light (confidence 0).
                info.z = pack_material(surface_class, 1.0f / 15.0f, 1.0f - emitter, roughness, params.w);
                const float3 origin = hit + ng * (0.2f + abs(hit.z) * 4e-4f);
                highlight = highlights(id.xy, hit, origin, n, -dir, params.x, params.z, true) *
                            (1.0f - smoothstep(0.55f, 0.75f, roughness));
                if (params.w > 0.0f) {
                    highlight = metal_reflect(highlight, base, dot(n, -dir), params.w);
                }
                info.w = asuint(g_range);
            }
        }
    }
    count(0u, geometry);
    count(1u, surface_class != kNoSurface);
    [unroll] for (uint c = 0u; c < 5u; ++c) {
        count(2u + c, surface_class == c || (c == kMetalClass && surface_class == kBareMetalClass));
    }
    count(7u, reflected);
    [unroll] for (uint k = 0u; k < kSources; ++k) {
        count(8u + k, reflected && source == k);
    }
    count(30u, reflected && source == kFromDraw);
    count(31u, reflected && source == kSkyCaptured);
    count(15u, any(highlight > 0.0f));
    count(28u, mapped);
    if (active) {
        g_info[index] = info;
        g_raw[index] = uint4(pack_rgb_w(light, confidence), pack_rgb_w(min(highlight, 60000.0f), 0.0f));
    }
}

// Under the water: sunlight focused by the surface's waves onto what lies below (caustics) and
// scattered towards the camera along its view (shafts).

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
    float x = depth * curvature * 0.9f;
    x = x >= 0.0f ? x : -0.75f * (1.0f - exp(x / 0.75f));
    return clamp(1.0f / (1.0f + x), 0.2f, 4.0f);
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
// Falling water (kRtDrawWaterfall): water's Fresnel on a turbulent sheet, no body under it.
static const uint kFallingWater = 9u;
// Water's reflections at 1.2 x its Fresnel: the owner's choice (26 October 2026) over the physical 1.
static const float kWaterReflectionGain = 1.2f;

// The water's luminance around `c` (EFB pixels): four bilinear taps `o` either way (a box about one of the
// game's own pixels).
float ripple_tap(float2 c, float o, float2 size)
{
    return 0.25f * (luminance(g_color.SampleLevel(g_linear, (c + float2(-o, -o)) / size, 0.0f).rgb) +
                    luminance(g_color.SampleLevel(g_linear, (c + float2(o, -o)) / size, 0.0f).rgb) +
                    luminance(g_color.SampleLevel(g_linear, (c + float2(-o, o)) / size, 0.0f).rgb) +
                    luminance(g_color.SampleLevel(g_linear, (c + float2(o, o)) / size, 0.0f).rgb));
}

// The water's normal bent by the ripples its picture shows: the slope of its luminance relative to
// its level across two of the game's 640 x 456 pixels each way, each tap the mean over one of them (the
// waves, not the texture's fine grain or the render's aliasing). The scale is the game's, not the samples':
// tied to the sample spacing, High's every-pixel samples would bend each ray its own way and speckle the water.
float3 rippled(int2 q, float3 n)
{
    if (g_ripple <= 0.0f) {
        return n;
    }
    const float2 size = float2(g_target_size);
    const float native = max(size.y / 456.0f, 1.0f);
    const float2 c = float2(q) + 0.5f;
    const float r = 2.0f * native;
    const float o = max(0.5f * native - 0.5f, 0.5f);
    const float x0 = ripple_tap(c - float2(r, 0.0f), o, size);
    const float x1 = ripple_tap(c + float2(r, 0.0f), o, size);
    const float y0 = ripple_tap(c - float2(0.0f, r), o, size);
    const float y1 = ripple_tap(c + float2(0.0f, r), o, size);
    const float level = 0.25f * (x0 + x1 + y0 + y1) + 0.05f;
    const float2 slope = clamp(float2(x1 - x0, y0 - y1) / (2.0f * level), -0.6f, 0.6f) * g_ripple;
    // Screen x and y along the water.
    const float3 ex = normalize(float3(1.0f, 0.0f, 0.0f) - n * n.x);
    const float3 ey = normalize(float3(0.0f, 1.0f, 0.0f) - n * n.y);
    return normalize(n - slope.x * ex - slope.y * ey);
}

// RtDraw.flags bit 13 (kRtDrawWaterfall): falling water (its streaks run down it).
static const uint kWaterfallFlag = 8192u;
// RtDraw.flags bit 14 on a water-pass draw (kRtDrawWaterMirror): glass the table marks `mirror`.
static const uint kWaterMirrorFlag = 16384u;
// A mirror glass's metalness: its Fresnel 3/4 of the way from glass to metal (0.25 -> 0.74 face-on).
static const float kMirrorMetal = 0.75f;
// RtDraw.flags bit 12 on a water-pass draw (kRtDrawWaterOpaque): an opaque late surface the table names water or
// crystal, the scene the trace saw (Freezeflame's ice).
static const uint kWaterOpaqueFlag = 4096u;
// RtDraw.flags bit 15 on a water-pass draw (kRtDrawWaterIce): ice (the table's `ice`), polished ice rather than water:
// no waves and no water body, its own class's reflectance (ray_tracing_reflections.cpp materials_[4]: Mario and the
// aurora read in it; as water, F0 0.02, its reflection was all but gone).
static const uint kWaterIceFlag = 32768u;
static const uint kIceClass = 4u;

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
    // Off where one of the game's own 640-wide pixels spans the finest waves (23 units): far water and water seen
    // edge-on, whose waves would alias into a different slope per pixel. The game's pixel, not the render's: the
    // same waves at any internal resolution.
    const float native = max(g_viewport.z / 640.0f, 1.0f);
    const float footprint = pixel_size(hit.z) * native / max(abs(dot(n, normalize(hit))), 0.1f);
    const float fade = saturate(1.5f - length(hit) / 6000.0f) * saturate(2.0f - footprint / 12.0f);
    const float2 s = wave_slope(float2(dot(wp, t1), dot(wp, t2)), float(g_frame), flow) * (g_waves * fade);
    const float3 wd = normalize(wn - s.x * t1 - s.y * t2);
    return normalize(float3(dot(float3(g_eye_to_world[0].x, g_eye_to_world[1].x, g_eye_to_world[2].x), wd),
                            dot(float3(g_eye_to_world[0].y, g_eye_to_world[1].y, g_eye_to_world[2].y), wd),
                            dot(float3(g_eye_to_world[0].z, g_eye_to_world[1].z, g_eye_to_world[2].z), wd)));
}

// Whether the water triangle `tri` at barycentrics `b` faces a camera ray along `dir`: its vertex normals (the side
// out of the water) against the ray; triangles without normals count as facing.
bool water_faces(uint tri, float2 b, float3 dir)
{
    const uint4 record = g_water_surfaces.Load4(tri * 16u);
    if (record.x == 0u || record.y == 0u || record.z == 0u) {
        return true;
    }
    const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
    const float3 smooth = unpack_vertex_normal(record.x) * w.x + unpack_vertex_normal(record.y) * w.y +
                          unpack_vertex_normal(record.z) * w.z;
    return dot(smooth, dir) < 0.0f;
}


#if RT_TEXTURED
// Translucent water's own waves: per water triangle its texture coordinates (72 bytes as g_tri_uv: slot 0 the wave map's,
// slot 1 the water texture's), per water draw (48 bytes) the wave map's view, flags (bit 0 waves), its coordinate scale,
// the indirect matrix's direction part (s from x, s from y, t from x, t from y), the water texture's coordinate scale
// (SceneRayTracer's water records, GxBackend's wave capture).
ByteAddressBuffer g_water_uv : register(t18);
ByteAddressBuffer g_water_waves : register(t19);

// The sea's slope at two standard deviations for a light breeze (Cox & Munk 1954: sigma^2 = 0.003 + 0.00512 U, U = 5 m/s:
// sigma 0.17): the slope the wave map's full range stands for.
static const float kWaveSlope = 0.34f;

// The water's normal `n` at water triangle `tri` (barycentrics `b`; eye-space `hit`) bent by the game's own waves: the
// wave map GX moves the water's texture by (its indirect texture; its alpha and blue are the s and t offsets) read as the
// waves' slope (its range kWaveSlope), turned by the indirect matrix onto the water texture's axes on the surface. Read
// at the level the pixel's footprint covers: waves wider than a pixel bend the reflection, finer ones average out, so no
// glitter at any resolution. False without waves.
bool water_wave(uint tri, float2 b, float3 hit, float3 n, out float3 wave)
{
    wave = n;
    const uint draw = g_water_surfaces.Load(tri * 16u + 12u) >> 16u;
    const uint4 head = g_water_waves.Load4(draw * 48u);
    if ((head.y & 1u) == 0u || head.x == 0xFFFFFFFFu) {
        return false;
    }
    const float4 turn = asfloat(g_water_waves.Load4(draw * 48u + 16u));
    const float2 main_scale = asfloat(g_water_waves.Load2(draw * 48u + 32u));
    const float2 wave_scale = asfloat(head.zw);
    const uint a = tri * 72u;
    const float4 w01 = asfloat(g_water_uv.Load4(a));
    const float2 w2 = asfloat(g_water_uv.Load2(a + 16u));
    const float4 m01 = asfloat(g_water_uv.Load4(a + 24u));
    const float2 m2 = asfloat(g_water_uv.Load2(a + 40u));
    const float3 w = float3(1.0f - b.x - b.y, b.x, b.y);
    const float2 t0 = w01.xy * wave_scale;
    const float2 t1 = w01.zw * wave_scale;
    const float2 t2 = w2 * wave_scale;
    const float2 uv = t0 * w.x + t1 * w.y + t2 * w.z;
    if (any(abs(uv) >= 1.0e4f)) {
        return false;
    }
    const float3 v0 = asfloat(g_water_positions.Load3(tri * 36u));
    const float3 e1 = asfloat(g_water_positions.Load3(tri * 36u + 12u)) - v0;
    const float3 e2 = asfloat(g_water_positions.Load3(tri * 36u + 24u)) - v0;
    // The water texture's axes on the surface (eye units per unit of its coordinates), in the normal's plane.
    const float2 d1 = (m01.zw - m01.xy) * main_scale;
    const float2 d2 = (m2 - m01.xy) * main_scale;
    const float det = d1.x * d2.y - d2.x * d1.y;
    if (abs(det) < 1e-12f) {
        return false;
    }
    float3 axis_s = (e1 * d2.y - e2 * d1.y) / det;
    float3 axis_t = (e2 * d1.x - e1 * d2.x) / det;
    axis_s -= n * dot(n, axis_s);
    axis_t -= n * dot(n, axis_t);
    if (dot(axis_s, axis_s) < 1e-20f || dot(axis_t, axis_t) < 1e-20f) {
        return false;
    }
    axis_s = normalize(axis_s);
    axis_t = normalize(axis_t);
    // The wave map's level for the pixel's footprint (its texels per eye unit over the triangle, a ray cone).
    const Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(head.x)];
    uint width, height, levels;
    map.GetDimensions(0u, width, height, levels);
    const float2 size = float2(width, height);
    const float2 g1 = (t1 - t0) * size;
    const float2 g2 = (t2 - t0) * size;
    const float texels = abs(g1.x * g2.y - g2.x * g1.y);
    const float area = length(cross(e1, e2));
    if (texels < 1e-12f || area < 1e-12f) {
        return false;
    }
    const float footprint = pixel_size(hit.z) / max(abs(dot(n, normalize(hit))), 0.1f);
    const float lod = clamp(0.5f * log2(texels / area) + log2(footprint), 0.0f, float(levels - 1u));
    const float4 texel = map.SampleLevel(g_wrap, uv, lod);
    const float2 xy = (float2(texel.a, texel.b) * 255.0f - 128.0f) / 128.0f;
    const float2 slope = clamp(float2(turn.x * xy.x + turn.y * xy.y, turn.z * xy.x + turn.w * xy.y), -1.0f, 1.0f) *
                         kWaveSlope;
    wave = normalize(n - slope.x * axis_s - slope.y * axis_t);
    return true;
}
#endif

// Translucent water (seas, lakes, pools drawn blended after the trace) and clear crystals and glass drawn the same
// way: the camera ray meets the water draws' own triangles in front of the surface the EFB holds behind them; the
// vertex normals bent by the ripples give the normal; one reflection ray as on the opaque surfaces, its light from
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
        // The water layer facing the camera: a swimmable surface is drawn twice at the same height, its top
        // (normals up) and its underside (normals down), and the nearer of the two flips per pixel by rounding
        // between the reflecting top and the non-reflecting underside, dotting the water. Any layer only where none
        // faces the camera (from under the water).
        bool hit_water = false;
        float water_t = 0.0f;
        uint water_tri = 0u;
        float2 water_b = float2(0.0f, 0.0f);
        {
            RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> facing;
            facing.TraceRayInline(g_water, RAY_FLAG_NONE, 0xFFu, ray);
            while (facing.Proceed()) {
                if (water_faces(facing.CandidatePrimitiveIndex(), facing.CandidateTriangleBarycentrics(), dir)) {
                    facing.CommitNonOpaqueTriangleHit();
                }
            }
            if (facing.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                hit_water = true;
                water_t = facing.CommittedRayT();
                water_tri = facing.CommittedPrimitiveIndex();
                water_b = facing.CommittedTriangleBarycentrics();
            } else {
                RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> any_layer;
                any_layer.TraceRayInline(g_water, RAY_FLAG_NONE, 0xFFu, ray);
                any_layer.Proceed();
                if (any_layer.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                    hit_water = true;
                    water_t = any_layer.CommittedRayT();
                    water_tri = any_layer.CommittedPrimitiveIndex();
                    water_b = any_layer.CommittedTriangleBarycentrics();
                }
            }
        }
        // Falling water lying on other water within a few units (a fall's ripple ring of foam on the lake at its foot):
        // the lake is the surface there; the foam stays the game's draw over it, which covers it (the reactive mask).
        if (hit_water && (g_water_surfaces.Load(water_tri * 16u + 12u) & kWaterfallFlag) != 0u) {
            RayDesc beyond = ray;
            beyond.TMin = water_t * 1.0001f + 0.01f;
            beyond.TMax = water_t + max(0.01f * water_t, 8.0f);
            RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> lake;
            lake.TraceRayInline(g_water, RAY_FLAG_NONE, 0xFFu, beyond);
            while (lake.Proceed()) {
                const uint tri = lake.CandidatePrimitiveIndex();
                if ((g_water_surfaces.Load(tri * 16u + 12u) & kWaterfallFlag) == 0u &&
                    water_faces(tri, lake.CandidateTriangleBarycentrics(), dir)) {
                    lake.CommitNonOpaqueTriangleHit();
                }
            }
            if (lake.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                water_t = lake.CommittedRayT();
                water_tri = lake.CommittedPrimitiveIndex();
                water_b = lake.CommittedTriangleBarycentrics();
            }
        }
        // A water surface has depth below it: triangles lying on the opaque scene the trace saw
        // (a water volume's floor, seen from inside it) are no surface. An opaque clear surface
        // (kWaterOpaqueFlag: Freezeflame's ice) is that scene itself.
        const uint2 under = g_under[index];
        const float floor_distance = under.y != 0u ? length(through) * -asfloat(under.y) : 1e30f;
        const uint hit_flags = hit_water ? g_water_surfaces.Load(water_tri * 16u + 12u) : 0u;
        // The engine's glass: not this pass's surface.
        if (hit_water && (g_flags & kReflectEngineClear) != 0u && ((hit_flags >> 8u) & 15u) == kCrystal &&
            (hit_flags & kWaterMirrorFlag) == 0u) {
            hit_water = false;
        }
        const bool opaque_clear = hit_water && (hit_flags & kWaterOpaqueFlag) != 0u &&
                                  abs(floor_distance - water_t) <= max(0.01f * water_t, 8.0f);
        // Mirror glass and a metal coat lie on what they cover (the Observatory fountain's glass over its star plates,
        // the gold rings' coat over their base), and falling water pours down its cliff within a few units of it: no
        // depth below them, only not behind the scene.
        const bool lying =
            hit_water &&
            ((hit_flags & kWaterMirrorFlag) != 0u || ((hit_flags >> 8u) & 15u) == kMetalClass ||
             (hit_flags & kWaterfallFlag) != 0u) &&
            floor_distance >= water_t - max(0.002f * water_t, 2.0f);
        if (hit_water && (opaque_clear || lying || floor_distance - water_t > max(0.01f * water_t, 8.0f))) {
            water = true;
            const uint tri = water_tri;
            const float2 b = water_b;
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
                // The water's normals point out of it: seen from their back, the camera is under it. Mirror glass
                // has no inside.
                below = dot(smooth, dir) > 0.0f && (record.w & (kWaterMirrorFlag | kWaterIceFlag)) == 0u &&
                        ((record.w >> 8u) & 15u) != kMetalClass;
                smooth *= dot(smooth, ng) < 0.0f ? -1.0f : 1.0f;
                const float smooth_length = length(smooth);
                // Only where they lie close to the face: at a rounded lip they bend towards the side wall
                // and draw a line of sun glints along the edge.
                if (smooth_length > 1e-3f && dot(smooth, ng) > 0.95f * smooth_length) {
                    n = smooth / smooth_length;
                }
            }
            // Translucent crystal and glass draws share the pass: no ripples, no water below them. Mirror glass is
            // glass that reflects as a mirror; a metal coat is bare metal (its tint the colour it drew).
            const bool mirror = (record.w & kWaterMirrorFlag) != 0u;
            const bool coat = ((record.w >> 8u) & 15u) == kMetalClass;
            const bool crystal = mirror || ((record.w >> 8u) & 15u) == kCrystal;
            // Ice: polished, still (no waves, no body), at its own class's reflectance.
            const bool ice = (record.w & kWaterIceFlag) != 0u && !crystal && !coat;
            // The game's own waves (its wave map, water_wave) bend the reflected ray: read at the pixel's footprint
            // they are smooth from pixel to pixel. Without them the reflection is traced off the calm surface and the
            // composite moves the image with procedural waves (water_distortion): rays bent per pixel by those
            // (up to 17 degrees) would scatter the image into glitter, the more the higher the resolution.
            float3 wave = n;
            bool game_waves = false;
            if (!crystal && !coat && !ice) {
#if RT_TEXTURED
                game_waves = water_wave(tri, b, dir * water_t, n, wave);
#endif
                if (!game_waves) {
                    wave = water_detail(rippled(q, n), dir * water_t, (record.w & kWaterfallFlag) != 0u);
                }
            }
            if (dot(n, -dir) < 0.02f) {
                n = ng;
            }
            if (dot(wave, -dir) < 0.02f) {
                wave = n;
            }
            g_spread = 0.0f;
            const float3 hit = dir * water_t;
            // A waterfall is a sheet (no lake body under it), its flow's streaks in its normal.
            const bool fall = (record.w & kWaterfallFlag) != 0u && !crystal && !coat && !ice;
            const uint surface = coat ? kBareMetalClass : crystal ? kCrystal : ice ? kIceClass : fall ? kFallingWater
                                 : below ? kWaterBelow : kWaterSurface;
            float4 params = g_class[coat ? kBareMetalClass : crystal ? kCrystal : ice ? kIceClass
                                    : fall ? kFallingWater : kWaterSurface];
            if (mirror) {
                params.x = min(params.x, 0.02f);
                params.y = 1.0f;
                params.w = kMirrorMetal;
            }
            if (coat) {
                params.z = g_metal_f0;
                params.w = 1.0f;
            }
            const float3 base = g_color.Load(int3(q, 0)).rgb;
            const float emitter = smoothstep(0.8f, 1.0f, min(base.r, min(base.g, base.b)));
            info.x = pack_normal(n);
            info.y = asuint(hit.z);
            info.z = pack_material(surface, params.y, 1.0f - emitter, sqrt(params.x), params.w);
            if (surface == kWaterSurface && emitter < 1.0f) {
                tint_rgb = uint3(round(saturate(base) * 255.0f));
                tint_all = 1u;
                tint_deep = floor_distance - water_t > 300.0f ? 1u : 0u;
            }
            // Seen from under the water the surface mirrors the water below it (its normal faces the camera, so the
            // mirror ray goes down, never to the bright world above): Snell's window overhead, total internal
            // reflection past it (the composite's water_inside_fresnel). Without it, what stands above the water shows
            // through the surface as through clear glass. Falling water reflects by water's Fresnel off its turbulent
            // sheet (its rough lobe), the flow's streaks moving the reflection down it (the composite).
            if (params.y > 0.0f && emitter < 1.0f) {
                reflected = true;
                // Water: a mirror (no lobe jitter: the composite's distortion is its roughness). A metal coat and a
                // crystal too, their lobe the cone the mirror ray reads over (g_spread: the tangent of the reflected
                // lobe's half-angle, twice the angle holding three quarters of its GGX normals): this pass keeps no
                // history, and one ray jittered over the lobe a frame was raw noise (the Gateway's turning ring's
                // coat). Read over the cone's footprint, as screen-space cone tracing prefilters a glossy reflection
                // (Uludag 2014, Hi-Z Screen-Space Cone-Traced Reflections), it is the lobe's mean, steady.
                float4 ray_params = params;
                ray_params.x = fall ? params.x : 1e-4f;
                g_spread = (crystal || coat || ice) && !mirror ? tan(2.0f * atan(params.x * sqrt(3.0f))) : 0.0f;
                // Over at least two of the game's own 640-wide pixels: what the frame shows thinner than that (a
                // net's cords, a fence's bars) averages out instead of reflecting as specks the waves set moving.
                g_reflect_blur = crystal || coat || ice ? 0.0f : 2.0f * max(g_viewport.z / 640.0f, 1.0f);
                const Reflected r = reflect_light(id.xy, dir, game_waves ? wave : n, ng, hit, ray_params, base, false,
                                                  false, false);
                g_reflect_blur = 0.0f;
                g_spread = 0.0f;
                light = r.light;
                // Glass and crystal whose rays leave the scene keep what they drew (never a dark mirror of space).
                if (surface == kCrystal && !fall && sky_source(r.source)) {
                    light = max(light, base);
                }
                // The lights' glints off the waves, their lobe widened by the waves' spread over the pixel (specular
                // anti-aliasing, highlights), weighted like the reflection.
                g_spread = crystal || coat || ice ? 0.0f : 2.0f * length(cross(wave, n));
                const float3 glint_origin = hit + ng * (0.2f + abs(hit.z) * 4e-4f);
                const float3 glints = highlights(id.xy, hit, glint_origin, wave, -dir, params.x, params.z, false) * params.y;
                g_spread = 0.0f;
                highlight = min(glints, 1.5f) * (surface == kWaterSurface || fall ? 0.5f : 1.0f);
                // Water mirrors what stands over it in full, and the sky at no more than the water's own
                // brightness: the sky's hues and clouds read in it, but its light never bleaches the far water.
                // The scale is the sky's average against the water's colour, so the sky keeps its own detail.
                confidence = r.confidence;
                if (surface == kWaterSurface && r.source >= kSkyShown) {
                    float3 sky_average;
                    const float sky_level = average(0u, sky_average) ? luminance(sky_average) : luminance(light);
                    light *= min(1.0f, luminance(base) / max(sky_level, 1e-3f));
                }
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
                const float limit = lerp(4.0f, float(g_history_frames), saturate(alpha * 4.0f));
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
        // Water's light too, in the first pass: a sample outside the range of its neighbours on the plane
        // (a ray its ripple sent to another object, the sky or the shore) is clamped into that range, then
        // blended with them by distance.
        const int reach = first ? 1 : 2;
        const bool water_light = first && surface_class == kWaterSurface;
        const float z = asfloat(info.y);
        const float3 p = sample_position(id, z);
        const float3 n = unpack_normal(info.x);
        const float inv_tolerance = 1.0f / (0.25f + abs(z) * 0.004f * float(reach));
        float3 highlight_sum = float3(0.0f, 0.0f, 0.0f);
        float shape_sum = 0.0f;
        float3 light_low = float3(1.0e9f, 1.0e9f, 1.0e9f);
        float3 light_high = float3(-1.0e9f, -1.0e9f, -1.0e9f);
        float3 light_sum = float3(0.0f, 0.0f, 0.0f);
        float light_known = 0.0f;
        float light_weight = 0.0f;
        uint neighbours = 0u;
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
                const float4 c = filter_input(si, first, other_highlight);
                highlight_sum += other_highlight * shape;
                shape_sum += shape;
                if (water_light && plane < 1.0f && c.w > 0.05f) {
                    if (dx != 0 || dy != 0) {
                        light_low = min(light_low, c.rgb);
                        light_high = max(light_high, c.rgb);
                        ++neighbours;
                    }
                    light_sum += c.rgb * (shape * c.w);
                    light_known += shape * c.w;
                    light_weight += shape;
                }
            }
        }
        if (shape_sum > 1e-4f) {
            highlight = highlight_sum / shape_sum;
        }
        if (water_light && neighbours >= 3u && light_known > 1e-4f) {
            value.rgb = clamp(value.rgb, light_low, light_high);
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

// Composite: dst * (1 - k) + k * reflection (dual-source blend: src0 + dst * src1).
cbuffer RtReflectComposite : register(b0)
{
    float4 m_projection;
    float4 m_depth;
    float4 m_viewport;
    uint2 m_sample_size;
    uint m_factor;
    uint m_flags;       // bit 0: move the point to the traced frame's camera (m_reproject[m_slot]);
                        // bits 1-3: view 1 the reflections alone, 2 the surface classes, 3 the weight k,
                        // 4 the reflected light alone, 5 k (red), the highlights' luminance (green) and
                        // the nearest sample's class + 1 in eighths (blue)
    uint m_slot;
    float m_near;
    uint m_tile_width;  // tiles of 8x8 samples per row
    float m_split;      // GALAXY_RT_SPLIT: left of it (and its line) the game's own picture
    uint4 m_class[5];   // per surface class (two per row; 8 bare metal): strength, F0, GGX alpha, cover (halves)
    float4 m_water;     // water: absorption per eye unit (rgb), share of the clear water shown (clarity)
    float4 m_sun;       // towards the sun (eye space, traced frame), caustics gain (0: none)
    float4 m_water2;    // water: wave slope, share of the game's drawn water replaced, foam, caustics
                        // in the shallows
};
StructuredBuffer<uint4> m_final : register(t0);
Texture2D<float> m_efb_depth : register(t1);
StructuredBuffer<float4> m_reproject : register(t2);
StructuredBuffer<uint> m_tiles : register(t3);
// m_flags bit 4 (the water composite): the effects layer (RendererD3D12::begin_layer_window), what the game's effects
// drew over the frame after its translucent lists: their colour, as they blended into the EFB (L), and what of the EFB
// behind them they let through (T), so the frame here is F = L + T B.
Texture2D<float4> m_layer_color : register(t7);
Texture2D<float4> m_layer_trans : register(t8);
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

// m_reproject after the slots' rows (4 x 3): a sub-frame's own view (SceneRayTracer::write_reprojection): its
// projection as m_projection's (its jitter in) and its depth's z_scale, z_offset, known.
static const uint kSubframeView = 12u;

// The view the pixel's own depth was drawn with (projection as m_projection, depth terms as m_depth): a sub-frame's
// when its point moves to the traced frame (m_flags bit 0), else the traced frame's (rt_lighting.hlsl own_view).
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

float4 class_params(uint c)
{
    const uint4 row = m_class[min(c, kFallingWater) >> 1u];
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

// The share of light a water surface reflects back into the water at cosine `c` from its normal (seen from below):
// Fresnel from water (n 1.333) into air, all of it past the critical angle (48.6 degrees: Snell's window overhead).
float water_inside_fresnel(float c)
{
    const float n1 = 1.333f;
    c = saturate(c);
    const float sin_t = n1 * sqrt(max(1.0f - c * c, 0.0f));
    if (sin_t >= 1.0f) {
        return 1.0f;
    }
    const float cos_t = sqrt(1.0f - sin_t * sin_t);
    const float rs = (n1 * c - cos_t) / (n1 * c + cos_t);
    const float rp = (n1 * cos_t - c) / (n1 * cos_t + c);
    return 0.5f * (rs * rs + rp * rp);
}

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
        if (surface_class <= kFallingWater) {
            // Water seen from below has the water's material.
            const float4 c = class_params(surface_class == kWaterBelow ? kWaterSurface : surface_class);
            // The sample's own lobe and metalness (filter_sample); its strength is in its weight.
            const float rough = float((sample.z >> 4u) & 15u) / 15.0f;
            const float metal = float((sample.z >> 8u) & 15u) / 15.0f;
            const float f0 = lerp(surface_class == kMetalClass || surface_class == kBareMetalClass ? kDielectricF0 : c.y,
                                  class_params(kMetalClass).y, metal);
            // Samples too rough to reflect (ReflectTraceCS: highlights only) take no reflected light,
            // only their glint.
            // Opaque dielectrics reflect half as much at grazing angles as Schlick has it (at its full
            // strength the bright sky outlines objects); metal, water and glass keep it.
            const bool clear = surface_class >= 4u || metal > 0.5f;
            const float graze = clear ? 1.0f : 0.5f;
            const float alpha_k = max(rough * rough, 1e-3f);
            float k = rough >= 0.62f ? 0.0f
                    : (f0 + (fresnel(f0, alpha_k, dot(n, v)) - f0) * graze) * sample_weight;
            // Water from above reflects by its Fresnel alone (class_params: F0 0.02, cover 1), up to a mirror at
            // grazing angles; the sky in it never outshines the water (WaterTraceCS): no bleaching.
            // From below: the water-to-air Fresnel, total past the critical angle (the reflection of the water below
            // covers what the game drew above the surface).
            if (surface_class == kWaterBelow) {
                k = water_inside_fresnel(dot(n, v)) * sample_weight;
            } else if (surface_class == kWaterSurface || surface_class == kFallingWater) {
                k = min(k * kWaterReflectionGain, sample_weight);
            }
            const float3 light = min(unpack_r11g11b10(sample.x), 1.0f);
            b.sum += light * (k * bw);
            // How much of the surface's own colour the reflection takes away: all of it at cover 1
            // (dst * (1 - k) + k * light), only its own share at cover 0 (a screen: never darker). The
            // bare metal's cover is its metalness's share (its painted look gives way to the traced
            // reflection where that is known; k carries the confidence).
            const float cover = surface_class == kBareMetalClass ? c.w * metal : surface_class == kWaterBelow ? 1.0f : c.w;
            b.covered += (cover + (1.0f - cover) * light) * (k * bw);
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
// blue, translucent water light blue (from below green), crystal white, bare metal red, falling water
// violet, untraced black.
CompositeOut class_view(uint nearest_class)
{
    const float3 colours[11] = {float3(0.3f, 0.3f, 0.3f), float3(0.0f, 0.86f, 1.0f), float3(1.0f, 0.67f, 0.0f),
                                float3(1.0f, 0.0f, 1.0f), float3(0.0f, 0.3f, 1.0f), float3(0.45f, 0.8f, 1.0f),
                                float3(0.2f, 1.0f, 0.6f), float3(1.0f, 1.0f, 1.0f), float3(1.0f, 0.15f, 0.0f),
                                float3(0.55f, 0.35f, 1.0f), float3(0.0f, 0.0f, 0.0f)};
    CompositeOut result;
    result.add = float4(colours[min(nearest_class, 10u)] * 0.55f, 0.0f);
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
    // A light's glint on clear glass and crystal goes up to white: a point of the sun on a curved clear surface, which
    // capped at 0.35 reads as a grey smudge. Elsewhere 0.35, so the broad sheen of large metal panels and rock tips
    // stays short of white.
    const float3 highlight = min(b.glint / b.weight, b.nearest_class == kCrystal ? 1.0f : 0.35f);
    const float3 kept = saturate(1.0f - b.covered / b.weight);
    if (view == 1u || view == 4u) {
        const float3 reflection = b.k_sum > 1e-4f ? b.sum / b.k_sum : float3(0.0f, 0.0f, 0.0f);
        result.add = float4(saturate(reflection + (view == 1u ? highlight : float3(0.0f, 0.0f, 0.0f))), 0.0f);
        result.keep = float4(0.0f, 0.0f, 0.0f, 1.0f);
    } else if (view == 3u || view == 5u) {
        result.add = view == 3u ? float4(k, k, k, 0.0f)
                                : float4(k, luminance(highlight), float(min(b.nearest_class, 8u) + 1u) / 9.0f, 0.0f);
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
    float4 own_projection;
    float4 own_depth;
    own_view(own_projection, own_depth);
    const float depth = m_efb_depth.Load(int3(int2(position.xy), 0));
    if (far_plane(own_depth, depth) || !inside(m_viewport, position.xy) || position.x < m_split + 1.0f) {
        discard;
    }
    const float z = depth_to_eye_z(own_depth, depth);
    float3 p = eye_position(own_projection, m_viewport, position.xy, z);
    if ((m_flags & 1u) != 0u) {
        const uint row = m_slot * 3u;
        const float4 rows[3] = {m_reproject[row], m_reproject[row + 1u], m_reproject[row + 2u]};
        p = transform(rows, p);
        if (p.z >= -m_near) {
            discard;
        }
    }
    const uint view = (m_flags >> 1u) & 7u;
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
    float z_sum = 0.0f;
    float z_weight = 0.0f;
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
        if (u.y != 0u) {
            z_sum += uz * bw;
            z_weight += bw;
        }
    }
    z = z_weight > 0.0f ? z_sum / z_weight : 0.0f;
    found = saturate(weight);
    return weight > 0.0f ? sum / weight : float3(0.0f, 0.0f, 0.0f);
}

// The floor under the water at `pixel`, softened over a sample each way: water scatters, and the
// game's thin projected caustic lines on the sand fade instead of showing as outlines. Taps with
// nothing behind the water leave the mean; `found` is the share that had something. At least 1.5 of the
// game's own pixels each way (High's sample spacing alone leaves the refracted floor grainy), and `spread`
// pixels where the ripples scatter the view of it (half the refraction's displacement, up to 12): deep or
// wavy water shows its floor soft, as water does.
float3 under_soft(float2 pixel, float limit, out float z, out float found, float spread = 0.0f)
{
    const float native = max(m_viewport.w / 456.0f, 1.0f);
    const float r = max(1.5f * max(float(m_factor), native), min(spread, 12.0f));
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
    return 1.0f + clamp(focus - 1.0f, -0.6f, 1.5f) * 0.3f * saturate(m_sun.w) * exp(-depth / 1500.0f);
}

// Where the waves move the reflected image of water at eye-space `p` (normal `n`, at traced-frame pixel `pixel`):
// a surface tilted by slope s shows what stands about 150 units over it moved by s times that, projected to the
// screen, at most 1.2 % of the viewport's height: one smooth, animated wobble at any resolution.
// `flow`: falling water, its streaks running down it (wave_slope).
float2 water_distortion(float3 p, float3 n, float2 pixel, bool flow)
{
    const uint s0 = m_sample_size.x * m_sample_size.y;
    float4 to_world[3];
    [unroll] for (uint r = 0u; r < 3u; ++r) {
        const uint2 a = m_under[s0 + 2u + 2u * r];
        const uint2 c = m_under[s0 + 3u + 2u * r];
        to_world[r] = float4(asfloat(a.x), asfloat(a.y), asfloat(c.x), asfloat(c.y));
    }
    const float frame = float(m_under[s0 + 1u].y);
    const float3 wn = normalize(rotate(to_world, n));
    float3 t1 = normalize(abs(wn.y) < 0.9f ? cross(wn, float3(0.0f, 1.0f, 0.0f)) : cross(wn, float3(1.0f, 0.0f, 0.0f)));
    float3 t2 = cross(wn, t1);
    // A fall's basis as water_detail's: across it, and down it.
    const float3 down = float3(0.0f, -1.0f, 0.0f) + wn * wn.y;
    if (flow && dot(down, down) > 1e-4f) {
        t2 = normalize(down);
        t1 = cross(t2, wn);
    }
    const float3 wp = transform(to_world, p);
    const float2 slope = wave_slope(float2(dot(wp, t1), dot(wp, t2)), frame, flow) * m_water2.x;
    const float3 shift_world = (slope.x * t1 + slope.y * t2) * 150.0f;
    const float3 shift_eye = float3(dot(float3(to_world[0].x, to_world[1].x, to_world[2].x), shift_world),
                                    dot(float3(to_world[0].y, to_world[1].y, to_world[2].y), shift_world),
                                    dot(float3(to_world[0].z, to_world[1].z, to_world[2].z), shift_world));
    const float3 moved = p + shift_eye;
    if (moved.z >= -m_near) {
        return float2(0.0f, 0.0f);
    }
    const float limit = 0.012f * m_viewport.w;
    return clamp(eye_to_pixel(m_projection, m_viewport, moved) - pixel, -limit, limit);
}

// The frame at pixel `q` without the effects drawn over it (B = (F - L) / T, m_flags bit 4): the game's water and what
// lies under it. Where the effects let almost nothing through, the frame as it is (they hide what lies behind).
float3 scene_at(int2 q)
{
    const float3 f = m_scene.Load(int3(q, 0)).rgb;
    if ((m_flags & 16u) == 0u) {
        return f;
    }
    const float3 l = m_layer_color.Load(int3(q, 0)).rgb;
    const float3 t = m_layer_trans.Load(int3(q, 0)).rgb;
    return all(t > 0.02f) ? max((f - l) / t, 0.0f) : f;
}

// The brightest the game's drawn water may be at `at`: the mean luminance of eight taps two pixels round plus 1.5
// deviations. Isolated bright texels (the foam flecks of a tiled water texture) shimmer as glitter once the picture
// moves; a wave or a gradient over a few pixels is within it.
float water_speck_limit(float2 at, float2 lo, float2 hi)
{
    float sum = 0.0f;
    float sum2 = 0.0f;
    [unroll] for (uint k = 0u; k < 8u; ++k) {
        const float a = float(k) * 0.785398f;
        const float l = luminance(scene_at(int2(clamp(at + float2(cos(a), sin(a)) * 2.0f, lo, hi))));
        sum += l;
        sum2 += l * l;
    }
    const float mean = sum / 8.0f;
    return mean + 1.5f * sqrt(max(sum2 / 8.0f - mean * mean, 0.0f));
}

// A colour of the game's water held to `limit` in luminance, its hue kept.
float3 water_unspeckled(float3 c, float limit)
{
    const float l = luminance(c);
    return l > limit && l > 1e-4f ? c * (limit / l) : c;
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
    float4 own_projection;
    float4 own_depth;
    own_view(own_projection, own_depth);
    const float3 here = eye_position(own_projection, m_viewport, position.xy, z);
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
    const bool behind =
        !far_plane(own_depth, depth) && depth_to_eye_z(own_depth, depth) > here.z + abs(here.z) * 0.003f + 1.0f;
    const uint view = (m_flags >> 1u) & 7u;
    if (view == 3u) {
        CompositeOut found;
        found.add = float4(behind ? float3(0.5f, 0.5f, 0.0f) : float3(0.55f, 0.0f, 0.55f), 0.0f);
        found.keep = float4(0.45f, 0.45f, 0.45f, 1.0f);
        return found;
    }
    if (behind) {
        discard;
    }
    Blend b = blend_samples(pixel, p, true);
    // The reflected image where the waves move it (its glints and the surface's normal where they are); down a fall,
    // where its streaks move it.
    if (b.surface > 1e-4f && m_water2.x > 0.0f &&
        (b.nearest_class == kWaterSurface || b.nearest_class == kFallingWater)) {
        const float2 moved =
            pixel + water_distortion(p, normalize(b.normal), pixel, b.nearest_class == kFallingWater);
        const Blend r = blend_samples(moved, p, true);
        if (r.k_sum > 1e-4f && r.weight > 1e-4f) {
            const float scale = b.weight / r.weight;
            b.sum = r.sum * scale;
            b.covered = r.covered * scale;
            b.k_sum = r.k_sum * scale;
        }
    }
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
            // blended by that share (a switch per pixel speckles the shallows).
            float bent_z;
            float bent_found;
            const float2 bent_pixel = eye_to_pixel(m_projection, m_viewport, floor_at);
            const float scatter = 0.5f * length(bent_pixel - pixel);
            const float3 bent_colour = under_soft(bent_pixel, p.z - 1.0f, bent_z, bent_found, scatter);
            float straight_z;
            float straight_found;
            const float3 straight_colour = under_soft(pixel, p.z - 1.0f, straight_z, straight_found, scatter);
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
        const float2 wobble = clamp(eye_to_pixel(m_projection, m_viewport, p + shift_eye) - pixel, -8.0f, 8.0f);
        const float px_per_unit = 0.5f * m_viewport.w * abs(m_projection.z) / max(-p.z, 1.0f);
        const float radius = clamp(40.0f * px_per_unit, 3.0f, 18.0f);
        // The water's depth over the floor the trace found (where none, the deep value, blended by the
        // share found: no switch per sample), and the caustics the waves focus on the shallows' floor
        // (energy-neutral).
        const float path_c = floor_z != 0.0f ? lerp(500.0f, min(path, 500.0f), floor_found) : 500.0f;
        const float shallow = floor_z != 0.0f ? exp(-path_c / 120.0f) : 0.0f;
        float caustic_gain = 1.0f;
        if (shallow > 0.0f && m_water2.w > 0.0f) {
            caustic_gain = lerp(1.0f, caustic(floor_point, p, n), saturate(shallow * m_water2.w));
        }
        const float2 lo = m_viewport.xy;
        const float2 hi = m_viewport.xy + m_viewport.zw - 1.0f;
        float3 water;
        float edge = 1.0f;
        if (m_water2.y > 0.0f) {
            // Where the game drew water at this pixel (its own full-resolution outline: no stair-steps from
            // the half-resolution samples at the shore).
            const int2 own = int2(clamp(position.xy, lo, hi));
            const float3 drawn = abs(scene_at(own) - m_opaque.Load(int3(own, 0)).rgb);
            edge = smoothstep(0.002f, 0.012f, drawn.r + drawn.g + drawn.b);
            {
                // The traced water body: the game's drawn water (bright specks held down) with the shallows'
                // caustics, kept by the transmittance T = exp(-sigma path) to the floor; the rest is the deep
                // water's colour (the drawn colour times 0.3, 0.55, 0.7). The kept share, deep + (1 - deep) T, is
                // 0.92/0.97/0.99 at path 30, 0.68/0.87/0.94 at 150 and 0.39/0.70/0.83 with no floor found (500).
                const float3 drawn_water =
                    water_unspeckled(scene_at(int2(clamp(position.xy, lo, hi))),
                                     water_speck_limit(clamp(position.xy, lo, hi), lo, hi));
                const float3 through = exp(-float3(0.004f, 0.0022f, 0.0016f) * path_c);
                const float3 deep = drawn_water * float3(0.3f, 0.55f, 0.7f);
                water = lerp(deep, drawn_water * caustic_gain, through);
            }
        } else {
            const float2 centre_at = clamp(position.xy + wobble, lo, hi);
            const float limit = water_speck_limit(centre_at, lo, hi);
            const float3 centre = water_unspeckled(scene_at(int2(centre_at)), limit);
            water = centre;
            float taps = 1.0f;
            [unroll] for (uint k = 0u; k < 16u; ++k) {
                const float a = float(k) * 2.39996f;
                const float2 o = float2(cos(a), sin(a)) * (min(radius, 6.0f) * sqrt((float(k) + 0.5f) / 16.0f)) + wobble;
                const float3 c = water_unspeckled(scene_at(int2(clamp(position.xy + o, lo, hi))), limit);
                const float3 d = c - centre;
                const float w = exp(-dot(d, d) / 0.02f);
                water += c * w;
                taps += w;
            }
            water /= taps;
            water *= caustic_gain;
        }
        // The shore: foam where the water is a few units deep, broken by drifting noise.
        if (floor_z != 0.0f && m_water2.z > 0.0f) {
            const float shore = smoothstep(20.0f, 3.0f, path);
            const float lace = wave_noise(float3(uv * 0.07f, frame * 0.02f)) * 0.65f +
                               wave_noise(float3(uv * 0.19f + 5.3f, frame * 0.035f)) * 0.35f;
            const float foam = shore * smoothstep(0.45f, 0.7f, lace) * m_water2.z;
            water = lerp(water, max(luminance(water), 0.75f).xxx * float3(1.0f, 1.0f, 1.02f), saturate(foam));
        }
        const float cover = abs(m_water2.y) * share * edge;
        under_add = water * cover;
        under_keep = (1.0f - cover).xxx;
    }
    if (b.k_sum < 1e-4f && !any(b.glint > 0.0f) && !any(under_add > 0.0f) && !any(under_keep < 1.0f)) {
        discard;
    }
    CompositeOut result = finish(b, view, under_add, under_keep);
    // The effects drawn over the water after it (a fall's spray, splashes) stay over it as they blended: with the frame
    // F = L + T B, the water over B (add + keep B) under them is L + T add + keep (F - L).
    if ((m_flags & 16u) != 0u && view == 0u) {
        const float3 l = m_layer_color.Load(int3(int2(position.xy), 0)).rgb;
        const float3 t = m_layer_trans.Load(int3(int2(position.xy), 0)).rgb;
        result.add.rgb = t * result.add.rgb + (1.0f - result.keep.rgb) * l;
    }
    return result;
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
    float4 own_projection;
    float4 own_depth;
    own_view(own_projection, own_depth);
    const bool sky = far_plane(own_depth, depth);
    float3 p = eye_position(own_projection, m_viewport, position.xy, sky ? -4000.0f : depth_to_eye_z(own_depth, depth));
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
        const float tl = luminance(through);
        const float k = saturate(m_sun.w) * 0.5f;
        add += sun * lerp(tl.xxx, through, 0.35f) * (max(focus - 1.0f, 0.0f) * 0.12f * k);
        keep *= 1.0f - saturate(1.0f - focus) * k * 0.5f * tl;
    }
    // Shafts: eight steps along the view through the water, each lit by the light its waves focus.
    if (shafts > 0.0f) {
        const float span = min(length(p), 4000.0f);
        const float3 v = normalize(p);
        const float jitter = frac(52.9829189f * frac(0.06711056f * position.x + 0.00583715f * position.y));
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
            sum += exp(-m_water.rgb * 0.3f * (along + t)) * focus;
        }
        // Forward scattering towards the sun (Henyey-Greenstein, g 0.6).
        const float c = dot(v, s);
        const float phase = (1.0f - 0.36f) / pow(max(1.0f + 0.36f - 1.2f * c, 1e-3f), 1.5f) / (4.0f * kPi);
        add += sun * sum * (span / 8.0f) * (shafts * 5e-5f * phase);
    }
    // The water between the camera and what the pixel shows: its light fades with the depth of water
    // it crosses and the light the water scatters takes its place: clear for the first 600 units, so the
    // player sees what is near, then a light blue-teal veil over the far distance (a shorter clear range or a
    // denser veil turns a lagoon murky).
    {
        const float span = max((sky ? 1500.0f : min(length(p), 4000.0f)) - 600.0f, 0.0f);
        const float3 t = exp(-float3(0.00032f, 0.00016f, 0.00012f) * span);
        // The veil's colour 40 % of the way from the sea's blue-teal to a warm grey, which tempers the blue.
        const float3 scatter = (sun * 0.4f + 0.25f) * lerp(float3(0.07f, 0.4f, 0.78f), float3(0.47f, 0.43f, 0.35f), 0.4f);
        add = add * t + scatter * (1.0f - t);
        keep *= t;
    }
    CompositeOut result;
    result.add = float4(saturate(add), 0.0f);
    result.keep = float4(saturate(keep), 1.0f);
    return result;
}
