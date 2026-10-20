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
static const uint kEvGi = 7u;             // this frame's final indirect reservoir
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

static const uint kEngineOn = 1u;         // g_engine_info.x
static const uint kEngineSpecular = 2u;   // traced specular
static const uint kEngineIndirect = 4u;   // traced indirect light
static const uint kEngineView = 0xF0u;    // GALAXY_RT_ENGINE_VIEW (bits 4-7): 1 direct, 2 indirect, 3 specular, 4 noisy

// G-buffer material flags (word 3 bits 8-15).
static const uint kGbEmitter = 1u;
static const uint kGbCard = 2u;
static const uint kGbDelit = 4u;

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
            const uint draw = g_surfaces.Load(tri * 16u + 12u) >> 16u;
            s.draw = draw;
            if (draw < g_tev_info.x && (g_tev.Load(draw * kTevRecordBytes) & 0x80u) != 0u) {
                // The game's painted reflection (environment maps): the traced specular takes its place.
                bool pass;
                const float3 full = srgb_to_linear(saturate(tev_evaluate_mode(draw, g_tev_info.x, tri, bary, 0u, pass).rgb));
                const float3 bare =
                    srgb_to_linear(saturate(tev_evaluate_mode(draw, g_tev_info.x, tri, bary, kTevNoEnvironment, pass).rgb));
                own = luminance(full) > 1e-4f ? saturate(luminance(bare) / luminance(full)) : 1.0f;
            }
            const uint record = min(draw, max(g_draw_count, 1u) - 1u) * kDrawStride;
            const uint2 table = g_draws.Load2(record + 8u);
            // The table's roughness and metalness (unorm16 x 65534; 0xFFFF: no row: a matte non-metal).
            const uint pbr = g_draws.Load(record + 16u);
            s.roughness = (pbr & 0xFFFFu) == 0xFFFFu ? 0.7f : max(float(pbr & 0xFFFFu) / 65534.0f, 0.02f);
            s.metallic = (pbr >> 16u) == 0xFFFFu ? 0.0f : saturate(float(pbr >> 16u) / 65534.0f);
            if ((g_draws.Load(record + 24u) & kDrawTableFoliage) != 0u) {
                s.flags |= kGbCard;
            }
            if ((table.x & kEmitNamed) != 0u && (table.x & 3u) != 0u && (table.x & kEmitNoLight) == 0u) {
                // A light source shows what it sends: its colour times the table's intensity (at least 1).
                s.flags |= kGbEmitter;
                s.emission = max(asfloat(table.y), 1.0f);
            }
        }
        // The texel's albedo: the EFB's colour, the light painted into it and its painted reflection taken out.
        const Texture2D<float4> picture = ResourceDescriptorHeap[engine_view(kEvPicture)];
        const float3 color = srgb_to_linear(saturate(picture.Load(int3(q, 0)).rgb));
        s.albedo = saturate(color * (delit ? exp(-shading) : 1.0f) * own);
        s.flags |= delit ? kGbDelit : 0u;
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
    return lerp(float3(0.04f, 0.04f, 0.04f), s.albedo, s.metallic);
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
    while (query.Proceed()) {
        const bool late_hit = query.CandidateInstanceID() != 0u;
        const uint hit = late_hit ? 0u : g_surfaces.Load(query.CandidatePrimitiveIndex() * 16u + 12u);
        if (late_hit || ((hit & kDrawNoShadow) == 0u &&
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
    while (query.Proceed()) {
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

// The radiance a hit sends back along -direction: its emission (when `emission`) plus its albedo times its light: the
// sun behind a shadow ray, one emitter by resampled importance behind a shadow ray, the sky along one cosine ray.
// Out: the hit's world-facing normal (for ReSTIR GI).
float3 engine_hit_radiance(Hit h, float3 origin, float3 direction, bool emission_wanted, inout uint seed, uint salt,
                           out float3 x, out float3 hn)
{
    x = origin + direction * h.t;
    hn = -direction;
    if (!h.surface) {
        return h.late ? float3(0.0f, 0.0f, 0.0f) : physical_sky(direction);
    }
    float3 albedo;
    float3 emission;
    reference_material(h.tri, h.b, albedo, emission);
    const uint draw = g_surfaces.Load(h.tri * 16u + 12u) >> 16u;
    const uint2 table = g_draws.Load2(min(draw, max(g_draw_count, 1u) - 1u) * kDrawStride + 8u);
    emission *= max(asfloat(table.y), 1.0f);
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
        if (g_tev_info.z != 0u) {
            m += emitter_light(start, n, start, 4u, emit_hash(seed ^ 0x51ED27u));
            seed = emit_hash(seed);
        }
        const float3 d = cosine_direction(n, ng, float2(emit_random(seed), emit_random(seed)));
        const Hit sky = engine_trace(start, d, salt + 7u);
        if (!sky.surface && !sky.late) {
            m += physical_sky(d);
        }
    }
    return albedo * m + (emission_wanted ? emission : float3(0.0f, 0.0f, 0.0f));
}

// --- ReSTIR DI (the emitters) -------------------------------------------------------------------------------------

struct LightSample {
    float3 y;          // eye position on the emitter
    float3 face;       // its face's normal (eye)
    float3 radiance;
    float beam;
    float sides;
};

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

void reservoir_store(uint view, uint index, Reservoir r)
{
    RWByteAddressBuffer b = ResourceDescriptorHeap[view];
    b.Store4(index * 32u, uint4(asuint(eye_point_to_world(r.s.y)), asuint(r.W)));
    b.Store4(index * 32u + 16u,
             uint4(pack_normal(normalize(eye_direction_to_world(r.s.face))), pack_half2(r.s.radiance.r, r.s.radiance.g),
                   f32tof16(r.s.radiance.b) | (f32tof16(min(r.m, 60000.0f)) << 16u),
                   min(uint(r.s.beam), 0xFFFFu) | (min(uint(r.s.sides + 0.5f), 3u) << 16u)));
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
    r.s.beam = float(r1.w & 0xFFFFu);
    r.s.sides = float((r1.w >> 16u) & 3u);
    r.W = asfloat(r0.w);
    r.m = f16tof32(r1.z >> 16u);
    return r;
}

float light_target(GSurface s, LightSample l, out float3 light)
{
    return emitter_target(s.p, s.n, l.y, l.face, l.radiance, l.beam, l.sides, light);
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
            specular = specular_brdf_nl(s, v, l) * (kPi * g_sun_light.rgb);
        }
    }
    direct.Store4(pixel * 16u, uint4(pack_half2(diffuse.r, diffuse.g), pack_half2(diffuse.b, specular.r),
                                     pack_half2(specular.g, specular.b), 0u));
    // The emitters: candidates by power, a uniform point on each.
    const uint count = g_tev_info.z;
    const float total = count != 0u ? emit_element(0u).y : 0.0f;
    const uint candidates = (g_flags & 1024u) != 0u ? 8u : 4u;
    if (count != 0u && total > 0.0f) {
        [loop] for (uint k = 0u; k < candidates; ++k) {
            const float u = emit_random(seed) * total;
            uint lo = 0u;
            uint hi = count - 1u;
            [loop] while (lo < hi) {
                const uint mid = (lo + hi) >> 1u;
                if (emit_running(mid) > u) {
                    hi = mid;
                } else {
                    lo = mid + 1u;
                }
            }
            float4 a, b, c, d;
            emit_triangle_of(lo, a, b, c, d);
            if (!(d.w > 0.0f) || !(a.w > 0.0f)) {
                r.m += 1.0f;
                continue;
            }
            float r1 = emit_random(seed);
            float r2 = emit_random(seed);
            if (r1 + r2 > 1.0f) {
                r1 = 1.0f - r1;
                r2 = 1.0f - r2;
            }
            LightSample l;
            l.y = a.xyz + b.xyz * r1 + c.xyz * r2;
            l.face = normalize(cross(b.xyz, c.xyz));
            l.radiance = d.rgb;
            l.beam = c.w;
            l.sides = b.w;
            float3 light;
            const float target = light_target(s, l, light);
            reservoir_update(r, l, target, target / (d.w / (total * a.w)), 1.0f, seed);
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
        gi_store(engine_view(kEvGi), index, r);
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
        // |cos at the sample towards here| / |cos towards there| x (distance there / distance here)^2.
        const float3 to_here = s.p - other.x;
        const float3 to_there = ns.p - other.x;
        const float d_here = length(to_here);
        const float d_there = length(to_there);
        const float jacobian = d_here > 1e-3f && d_there > 1e-3f
                                   ? clamp(abs(dot(other.hn, to_here / d_here)) /
                                               max(abs(dot(other.hn, to_there / d_there)), 1e-3f) *
                                               (d_there * d_there) / (d_here * d_here),
                                           0.0f, 10.0f)
                                   : 1.0f;
        const float t = gi_target(s, other.x, other.radiance);
        if (gi_update(r, other.x, other.hn, other.radiance, t, t * other.W * other.m * jacobian, other.m, seed)) {
            reused = true;
        }
    }
    r.W = r.target > 0.0f && r.m > 0.0f ? r.w_sum / (r.m * r.target) : 0.0f;
    if (reused && r.W > 0.0f && length(r.x) < 9.0e4f && !engine_visible(start, r.x, 0.5f, salt)) {
        r.W = 0.0f;
    }
    gi_store(engine_view(kEvGi), index, r);
    // m = (1/pi) L cos W.
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
    // The neighbourhood's specular range (clamping the history).
    float3 lo = specular;
    float3 hi = specular;
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
        }
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
            const float wn = pow(nn, 64.0f);
            const float wd = h * wz * wn * exp(-abs(luminance(nd) - ld) / sigma_d);
            const float ws = h * wz * pow(nn, spec_power) * exp(-abs(luminance(nsp) - ls) / sigma_s) *
                             exp(-abs(ns.roughness - s.roughness) * 8.0f);
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
    float z = 0.0f;
    if (s.valid) {
        z = s.p.z;
        if ((s.flags & kGbEmitter) != 0u) {
            // A light source shows what it sends (its colour, the composite's albedo, times this).
            diffuse = (s.emission * exposure).xxx;
        } else {
            const uint view = (g_engine_info.x & kEngineView) >> 4u;
            const uint4 a = engine_buffer((g_engine_info.w & 1u) == 0u ? kEvAtrousA : kEvAtrousB).Load4(pixel * 16u);
            diffuse = float3(f16tof32(a.x), f16tof32(a.x >> 16u), f16tof32(a.y));
            specular = float3(f16tof32(a.z), f16tof32(a.z >> 16u), f16tof32(a.w));
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
            const float3 m = min(diffuse, kMaxPhysicalLight);
            g_final[sample] = uint4(pack_half2(m.r, m.g), f32tof16(m.b) | (f32tof16(1.0f) << 16u), asuint(z), 0u);
            g_z[sample] = z;
        }
    }
}
