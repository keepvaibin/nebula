// Ambient occlusion of the opaque scene (ambient_occlusion.h): ground-truth ambient occlusion (Jimenez, Wu, Pesce
// and Jarabo, "Practical Realtime Strategies for Accurate Indirect Occlusion", SIGGRAPH 2016) over the scene target's depth,
// in the form of Intel's XeGTAO (MIT): per pixel a few slices about the view direction, in each the highest horizon
// on both sides within a radius, integrated against the slice's projected normal. Fixed 4x4 interleaved slice and
// step patterns, removed exactly by the denoise's 5-tap tent (each pattern cell weighs the same), so a still
// picture stays still without a temporal filter. The result multiplies the scene's colour before its translucent
// phases draw.
//
// The occlusion is computed per occlusion pixel, one per g_size.z x g_size.z scene target pixels (the game's own resolution),
// from the eye distance there:
// DepthCS: the eye distance of each occlusion pixel's centre scene target pixel (0 for the far plane) into u0.
// AoCS: occlusion and eye distance from t2 (DepthCS's output) into u0.
// SsaoCS: the same by Scalable Ambient Obscurance (McGuire, Mara and Luebke, HPG 2012), instead of AoCS: a spiral of
// samples, each darkening by how far it stands above the surface's plane, falling off to nothing at the radius.
// DenoiseCS: the depth-weighted tent of t1 into u0.
// UpsampleCS (Metal), UpsamplePS (Vulkan): per scene target pixel the denoised occlusion (depth-weighted bilinear) as the
// colour multiplier, into an 8-bit image (Vulkan and Metal at more than one scene target pixel per occlusion pixel).
// ApplyVS/ApplyPS: that multiplier in a multiplicative blend; it reads no depth, so it can draw in the scene's own pass
// (Direct3D 12 and 11, and Vulkan and Metal at one occlusion pixel per scene target pixel: it makes the multiplier itself,
// below).

cbuffer AoConstants : register(b0) {
    float4 g_ray;         // an scene target pixel's eye ray at distance 1: x = px * x + y, y = py * z + w (z = -1)
    float4 g_depth;       // RtView z_scale, z_offset, min_depth, max_depth
    float4 g_viewport;    // the scene viewport in scene target pixels: x, y, width, height
    float4 g_params;      // radius (eye units), visibility exponent, display exponent, largest radius in occlusion pixels
    uint4 g_size;         // occlusion target width, height; scene target pixels per occlusion pixel; 1: show the occlusion
};

// The occlusion passes writing a two-channel (R16G16 float) target where a storage image of that format is allowed
// (Vulkan: shaderStorageImageExtendedFormats; the others always). The denoise writing one channel (R16 float): at one
// occlusion pixel per scene target pixel only the multiply reads its output, and only the occlusion.
#if defined(AO_PASS_occlusion_rg_cs) || defined(AO_PASS_ssao_rg_cs) || defined(AO_PASS_denoise_rg_cs)
#define AO_OCCLUSION_RG16F 1
#endif
#ifdef AO_PASS_denoise_r_cs
#define AO_OCCLUSION_R16F 1
#define AO_PASS_denoise_cs 1
#endif
#ifdef AO_PASS_occlusion_rg_cs
#define AO_PASS_occlusion_cs 1
#endif
#ifdef AO_PASS_ssao_rg_cs
#define AO_PASS_ssao_cs 1
#endif
#ifdef AO_PASS_denoise_rg_cs
#define AO_PASS_denoise_cs 1
#endif

#if defined(AO_PASS_distance_cs) || defined(AO_PASS_occlusion_cs) || defined(AO_PASS_ssao_cs) || defined(AO_PASS_denoise_cs) || defined(AO_PASS_upsample_cs)
#ifndef AO_COMPUTE
#define AO_COMPUTE 1
#endif
#endif

#ifdef AO_STORAGE_FORMAT
#define AO_FORMAT(name) [[spv::format_##name]]
#else
#define AO_FORMAT(name)
#endif

Texture2D<float> t_depth : register(t0);       // the scene target's depth
Texture2D<float4> t_occlusion : register(t1);  // occlusion, eye distance (the multiply: the multiplier)
Texture2D<float> t_distance : register(t2);    // DepthCS's eye distance
#if defined(AO_PASS_distance_cs)
AO_FORMAT(r32f) RWTexture2D<float> u_distance : register(u0);
#elif defined(AO_PASS_upsample_cs)
AO_FORMAT(rgba8) RWTexture2D<float4> u_scale : register(u0);
#elif defined(AO_COMPUTE) && defined(AO_OCCLUSION_RG16F)
AO_FORMAT(rg16f) RWTexture2D<float4> u_occlusion : register(u0);
#elif defined(AO_COMPUTE) && defined(AO_OCCLUSION_R16F)
AO_FORMAT(r16f) RWTexture2D<float4> u_occlusion : register(u0);
#elif defined(AO_COMPUTE)
AO_FORMAT(rgba16f) RWTexture2D<float4> u_occlusion : register(u0);
#endif

static const float kPi = 3.14159265f;
static const uint kSlices = 3u;
static const uint kSteps = 4u;
// XeGTAO's falloff: full weight to 38.5% of the radius, then linearly to none at the radius.
static const float kFalloffRange = 0.615f;
// How much a surface far in front along the view (a leaf, an ear's other sheet) weighs less than one beside the point: depth
// alone cannot tell a sheet from a wall, and a sheet's silhouette on what lies behind it would be a detached ghost.
static const float kThinOccluder = 8.0f;
// Visibility from here up counts as open: the slight angle between a low-poly surface's facets is not a crease, and the
// power would draw its polygons on the ground.
static const float kOpenVisibility = 0.94f;
// The game's worlds are lit for a stylised picture: a deep crease darkens, it does not turn black.
static const float kLeastVisibility = 0.3f;

float depth_to_eye_z(float depth) {
    const float ndc = (depth - g_depth.z) / max(g_depth.w - g_depth.z, 1e-7f);
    return -g_depth.y / (ndc + g_depth.x);
}

bool far_plane(float depth) {
    return depth <= g_depth.z + 1e-6f;
}

bool inside(float2 pixel) {
    return pixel.x >= g_viewport.x && pixel.y >= g_viewport.y && pixel.x < g_viewport.x + g_viewport.z &&
           pixel.y < g_viewport.y + g_viewport.w;
}

// The scene target pixel centre an occlusion pixel stands for.
float2 scene_centre(int2 o) {
    return float2(o * int(g_size.z) + int(g_size.z / 2u)) + 0.5f;
}

float3 eye_position(float2 pixel, float distance) {
    return float3(pixel.x * g_ray.x + g_ray.y, pixel.y * g_ray.z + g_ray.w, -1.0f) * distance;
}

// Where the radius nears two occlusion pixels nothing is resolved: the occlusion fades out over the last two, so that no
// line shows across the world where it stops, and far surfaces do not flicker with it.
float distance_fade(float screen_radius) {
    return saturate((screen_radius - 2.0f) * 0.5f);
}

// XeGTAO_FastACos (Eberly's first-order fit): within 0.0007 rad.
float fast_acos(float x) {
    const float a = abs(x);
    const float r = (-0.156583f * a + 0.5f * kPi) * sqrt(1.0f - a);
    return x >= 0.0f ? r : kPi - r;
}

#ifdef AO_PASS_distance_cs
[numthreads(8, 8, 1)]
void DepthCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_size.x || id.y >= g_size.y) {
        return;
    }
    const float2 centre = scene_centre(int2(id.xy));
    const float depth = t_depth.Load(int3(int2(centre), 0));
    u_distance[id.xy] = inside(centre) && !far_plane(depth) ? -depth_to_eye_z(depth) : 0.0f;
}
#endif

// The eye position at occlusion pixel o; false for the far plane, outside the viewport or the target.
bool point_at(int2 o, out float3 p) {
    p = 0.0f;
    if (any(o < 0) || any(o >= int2(g_size.xy))) {
        return false;
    }
    const float distance = t_distance.Load(int3(o, 0));
    if (distance <= 0.0f) {
        return false;
    }
    p = eye_position(scene_centre(o), distance);
    return true;
}

// The surface normal from the distances: per axis the neighbour nearer in depth (no normal across an edge), facing
// the camera.
float3 surface_normal(int2 o, float3 p) {
    float3 a, b, c, d;
    const bool ha = point_at(o + int2(1, 0), a);
    const bool hb = point_at(o - int2(1, 0), b);
    const bool hc = point_at(o + int2(0, 1), c);
    const bool hd = point_at(o - int2(0, 1), d);
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

// 4x4 Bayer order: each of 16 slice rotations and step offsets once per 4x4 block.
static const uint kBayer[16] = {0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u};

// What the scene's models let through to a point (ambient_occlusion_objects.h): per ellipsoid in reach the cosine towards its
// centre, in the ellipsoid's space, full at its surface and falling smoothly to none at the reach. A surface facing away from
// it takes none, so a model does not darken its own outside.
#if defined(AO_PASS_occlusion_cs) || defined(AO_PASS_ssao_cs)
StructuredBuffer<float4> t_objects : register(t3);
StructuredBuffer<uint> t_object_tiles : register(t4);
static const float kObjectReach = 3.0f;
static const float kObjectFalloff = 2.0f;
// Inside its ellipsoid a point is mostly the model's own surface, already shaded by the depth occlusion: a white ear should
// not turn grey. Models together leave at least this much light on what they stand over.
static const float kObjectInside = 0.45f;
static const float kObjectLeast = 0.45f;

float object_visibility(uint2 id, float3 p, float3 n) {
    const uint2 tile = id >> 5u;
    const uint at = 2u + 2u * (tile.y * t_object_tiles[0] + tile.x);
    const uint first = t_object_tiles[at];
    const uint count = t_object_tiles[at + 1u];
    float visibility = 1.0f;
    for (uint i = 0u; i < count; ++i) {
        const uint o = t_object_tiles[first + i] * 4u;
        const float4 a0 = t_objects[o];
        const float4 a1 = t_objects[o + 1u];
        const float4 a2 = t_objects[o + 2u];
        const float4 c = t_objects[o + 3u];
        const float3 d = c.xyz - p;
        const float3 q = float3(dot(d, a0.xyz), dot(d, a1.xyz), dot(d, a2.xyz));
        const float l2 = dot(q, q);
        if (l2 < kObjectReach * kObjectReach) {
            const float3 m = float3(dot(a0.xyz, n) * a0.w, dot(a1.xyz, n) * a1.w, dot(a2.xyz, n) * a2.w);
            const float l = sqrt(l2);
            const float cosine = saturate(dot(m, q) / max(sqrt(dot(m, m)) * l, 1e-6f));
            const float occlusion = cosine * pow(saturate((kObjectReach - l) / (kObjectReach - 1.0f)), kObjectFalloff) *
                                    lerp(kObjectInside, 1.0f, smoothstep(0.7f, 1.0f, l));
            visibility *= 1.0f - c.w * occlusion;
        }
    }
    return max(visibility, kObjectLeast);
}
#endif

#ifdef AO_PASS_occlusion_cs
[numthreads(8, 8, 1)]
void AoCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_size.x || id.y >= g_size.y) {
        return;
    }
    const int2 o = int2(id.xy);
    float3 p;
    if (!point_at(o, p)) {
        u_occlusion[id.xy] = float4(1.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    // The radius in occlusion pixels; below two nothing is resolved.
    const float radius = g_params.x;
    const float screen_radius = min(radius / (g_ray.x * -p.z * float(g_size.z)), g_params.w);
    const float3 n = surface_normal(o, p);
    if (screen_radius < 2.0f) {
        u_occlusion[id.xy] = float4(max(object_visibility(id.xy, p, n), kLeastVisibility), -p.z, 0.0f, 0.0f);
        return;
    }
    const float3 v = normalize(-p);
    const float falloff_mul = -1.0f / (kFalloffRange * radius);
    const float falloff_add = (1.0f - kFalloffRange) / kFalloffRange + 1.0f;
    const uint2 cell = id.xy & 3u;
    const float slice_noise = (float(kBayer[cell.y * 4u + cell.x]) + 0.5f) / 16.0f;
    const float step_noise = (float(kBayer[((cell.x + 2u) & 3u) * 4u + ((cell.y + 1u) & 3u)]) + 0.5f) / 16.0f;
    // The first slice's direction, then each next one turned by pi / kSlices.
    float2 direction;
    sincos(slice_noise * (kPi / float(kSlices)), direction.y, direction.x);
    float2 turn;
    sincos(kPi / float(kSlices), turn.y, turn.x);
    float visibility = 0.0f;
    [unroll]
    for (uint s = 0u; s < kSlices; ++s) {
        const float2 omega = float2(direction.x, -direction.y);  // on screen (y down)
        const float3 eye_direction = float3(direction, 0.0f);    // in the eye's space (y up)
        const float3 ortho = eye_direction - dot(eye_direction, v) * v;
        const float3 axis = normalize(cross(ortho, v));
        const float3 projected = n - axis * dot(n, axis);
        const float projected_length = length(projected);
        const float cos_n = saturate(dot(projected, v) / max(projected_length, 1e-6f));
        const float angle_n = (dot(ortho, projected) >= 0.0f ? 1.0f : -1.0f) * fast_acos(cos_n);
        float sin_n, cos_angle_n;
        sincos(angle_n, sin_n, cos_angle_n);
        // cos(n +- pi/2) = -+sin(n)
        const float low0 = -sin_n;
        const float low1 = sin_n;
        float horizon0 = low0;
        float horizon1 = low1;
        [unroll]
        for (uint k = 0u; k < kSteps; ++k) {
            float t = (float(k) + step_noise) / float(kSteps);
            t *= t;
            // Whole occlusion pixels, at least one from the centre.
            const int2 offset = int2(round(omega * (t * screen_radius + 1.0f)));
            float3 sp;
            if (point_at(o + offset, sp)) {
                const float3 delta = sp - p;
                const float distance = length(delta);
                const float reach = length(float3(delta.xy, delta.z * (1.0f + kThinOccluder)));
                const float weight = saturate(reach * falloff_mul + falloff_add);
                horizon0 = max(horizon0, lerp(low0, dot(delta, v) / distance, weight));
            }
            if (point_at(o - offset, sp)) {
                const float3 delta = sp - p;
                const float distance = length(delta);
                const float reach = length(float3(delta.xy, delta.z * (1.0f + kThinOccluder)));
                const float weight = saturate(reach * falloff_mul + falloff_add);
                horizon1 = max(horizon1, lerp(low1, dot(delta, v) / distance, weight));
            }
        }
        float h0 = -fast_acos(clamp(horizon1, -1.0f, 1.0f));
        float h1 = fast_acos(clamp(horizon0, -1.0f, 1.0f));
        h0 = angle_n + clamp(h0 - angle_n, -0.5f * kPi, 0.5f * kPi);
        h1 = angle_n + clamp(h1 - angle_n, -0.5f * kPi, 0.5f * kPi);
        const float arc0 = (cos_n + 2.0f * h0 * sin_n - cos(2.0f * h0 - angle_n)) * 0.25f;
        const float arc1 = (cos_n + 2.0f * h1 * sin_n - cos(2.0f * h1 - angle_n)) * 0.25f;
        visibility += projected_length * (arc0 + arc1);
        direction = float2(direction.x * turn.x - direction.y * turn.y, direction.x * turn.y + direction.y * turn.x);
    }
    visibility = pow(saturate(visibility / (float(kSlices) * kOpenVisibility)), g_params.y);
    visibility = lerp(1.0f, visibility, distance_fade(screen_radius)) * object_visibility(id.xy, p, n);
    u_occlusion[id.xy] = float4(max(visibility, kLeastVisibility), -p.z, 0.0f, 0.0f);
}
#endif

#ifdef AO_PASS_ssao_cs
static const uint kSsaoSamples = 16u;
// The spiral and Bayer phase each have 16 possible angles. Rotate constant unit vectors instead of doing
// 16 trigonometric evaluations per pixel. Sample count, radius, phase pattern and weights are unchanged.
static const float2 kSsaoSpiral[16] = {
    float2(0.1950903220f, 0.9807852804f),
    float2(-0.5555702330f, -0.8314696123f),
    float2(0.8314696123f, 0.5555702330f),
    float2(-0.9807852804f, -0.1950903220f),
    float2(0.9807852804f, -0.1950903220f),
    float2(-0.8314696123f, 0.5555702330f),
    float2(0.5555702330f, -0.8314696123f),
    float2(-0.1950903220f, 0.9807852804f),
    float2(-0.1950903220f, -0.9807852804f),
    float2(0.5555702330f, 0.8314696123f),
    float2(-0.8314696123f, -0.5555702330f),
    float2(0.9807852804f, 0.1950903220f),
    float2(-0.9807852804f, 0.1950903220f),
    float2(0.8314696123f, -0.5555702330f),
    float2(-0.5555702330f, 0.8314696123f),
    float2(0.1950903220f, -0.9807852804f)
};
static const float2 kSsaoPhase[16] = {
    float2(0.9807852804f, 0.1950903220f),
    float2(0.8314696123f, 0.5555702330f),
    float2(0.5555702330f, 0.8314696123f),
    float2(0.1950903220f, 0.9807852804f),
    float2(-0.1950903220f, 0.9807852804f),
    float2(-0.5555702330f, 0.8314696123f),
    float2(-0.8314696123f, 0.5555702330f),
    float2(-0.9807852804f, 0.1950903220f),
    float2(-0.9807852804f, -0.1950903220f),
    float2(-0.8314696123f, -0.5555702330f),
    float2(-0.5555702330f, -0.8314696123f),
    float2(-0.1950903220f, -0.9807852804f),
    float2(0.1950903220f, -0.9807852804f),
    float2(0.5555702330f, -0.8314696123f),
    float2(0.8314696123f, -0.5555702330f),
    float2(0.9807852804f, -0.1950903220f)
};

[numthreads(8, 8, 1)]
void SsaoCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_size.x || id.y >= g_size.y) {
        return;
    }
    const int2 o = int2(id.xy);
    float3 p;
    if (!point_at(o, p)) {
        u_occlusion[id.xy] = float4(1.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const float radius = g_params.x;
    const float screen_radius = min(radius / (g_ray.x * -p.z * float(g_size.z)), g_params.w);
    const float3 n = surface_normal(o, p);
    if (screen_radius < 2.0f) {
        u_occlusion[id.xy] = float4(max(object_visibility(id.xy, p, n), kLeastVisibility), -p.z, 0.0f, 0.0f);
        return;
    }
    const uint2 cell = id.xy & 3u;
    const float2 phase = kSsaoPhase[kBayer[cell.y * 4u + cell.x]];
    const float r2 = radius * radius;
    // Past the facets of a low-poly surface: the normal comes from the depth, and a neighbour facet's small rise is no occluder.
    const float bias = 0.12f * radius;
    const float epsilon = 0.01f * r2;
    float sum = 0.0f;
    [unroll]
    for (uint i = 0u; i < kSsaoSamples; ++i) {
        const float alpha = (float(i) + 0.5f) / float(kSsaoSamples);
        const float2 basis = kSsaoSpiral[i];
        const float2 unit = float2(basis.x * phase.x - basis.y * phase.y,
                                  basis.x * phase.y + basis.y * phase.x);
        const int2 offset = int2(round(unit * (alpha * screen_radius + 1.0f)));
        float3 q;
        if (point_at(o + offset, q)) {
            const float3 delta = q - p;
            const float3 reach = float3(delta.xy, delta.z * (1.0f + kThinOccluder));
            const float vv = dot(reach, reach);
            const float f = max(r2 - vv, 0.0f);
            sum += f * f * f * max((dot(delta, n) - bias) / (epsilon + vv), 0.0f);
        }
    }
    const float obscurance = sum * 5.0f * radius / (r2 * r2 * r2 * float(kSsaoSamples));
    const float visibility = lerp(1.0f, pow(saturate(1.0f - obscurance), g_params.y), distance_fade(screen_radius)) *
                             object_visibility(id.xy, p, n);
    u_occlusion[id.xy] = float4(max(visibility, kLeastVisibility), -p.z, 0.0f, 0.0f);
}
#endif

// The 5x5 tent (0.5, 1, 1, 1, 0.5 per axis): every cell of the 4x4 patterns counts once. Neighbours at another depth
// (beyond 3% of the distance) count less, the far plane not at all. Each group reads its 12x12 neighbourhood into
// shared memory once.
#ifdef AO_PASS_denoise_cs
groupshared float2 gs_tile[12][12];

[numthreads(8, 8, 1)]
void DenoiseCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint index : SV_GroupIndex) {
    const int2 origin = int2(group.xy) * 8 - 2;
    for (uint i = index; i < 144u; i += 64u) {
        const int2 at = clamp(origin + int2(i % 12u, i / 12u), int2(0, 0), int2(g_size.xy) - 1);
        gs_tile[i / 12u][i % 12u] = t_occlusion.Load(int3(at, 0)).xy;
    }
    GroupMemoryBarrierWithGroupSync();
    if (id.x >= g_size.x || id.y >= g_size.y) {
        return;
    }
    const int2 local = int2(id.xy) - origin;
    const float2 centre = gs_tile[local.y][local.x];
    if (centre.y <= 0.0f) {
        u_occlusion[id.xy] = float4(centre, 0.0f, 0.0f);
        return;
    }
    const float tolerance = 1.0f / (0.03f * centre.y + 1.0f);
    float sum = 0.0f;
    float weight = 0.0f;
    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            const float2 s = gs_tile[local.y + y][local.x + x];
            const float tent = (abs(x) == 2 ? 0.5f : 1.0f) * (abs(y) == 2 ? 0.5f : 1.0f);
            const float w = s.y > 0.0f ? tent * saturate(1.0f - abs(s.y - centre.y) * tolerance) : 0.0f;
            sum += s.x * w;
            weight += w;
        }
    }
    u_occlusion[id.xy] = float4(weight > 0.0f ? sum / weight : centre.x, centre.y, 0.0f, 0.0f);
}
#endif

struct ApplyOut {
    float4 position : SV_Position;
};

ApplyOut ApplyVS(uint vertex : SV_VertexID) {
    ApplyOut o;
    const float2 uv = float2((vertex << 1) & 2u, vertex & 2u);
    o.position = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return o;
}

// The colour multiplier of a visibility on the scene target's display values.
float ao_scale(float visibility) {
    const float value = saturate(visibility);
    // The native display-value AO uses exponent 1. Avoid a log/exp pair for every full-resolution pixel;
    // retain the general curve for any other exponent supplied by the renderer.
    float scale = value;
    [branch] if (g_params.z != 1.0f) {
        scale = pow(value, g_params.z);
    }
    return scale;
}

// The colour multiplier at scene target pixel q: the denoised occlusion there (depth-weighted bilinear between occlusion
// pixels); 1 outside the viewport (nothing outside it is read) and on the far plane.
float ao_multiplier(int2 q) {
    float scale = 1.0f;
    if (g_size.z <= 1u) {
        // One occlusion pixel per scene target pixel, the same size: the denoised occlusion is already 1 outside the
        // viewport and on the far plane (DepthCS wrote no distance there, the occlusion passes 1, the denoise kept it),
        // so the depth is not read.
        scale = ao_scale(t_occlusion.Load(int3(q, 0)).x);
    } else {
        const float2 pixel = float2(q) + 0.5f;
        float depth = g_depth.z;  // the far plane: nothing outside the viewport is read
        if (inside(pixel)) {
            depth = t_depth.Load(int3(q, 0));
        }
        if (!far_plane(depth)) {
            const float z = -depth_to_eye_z(depth);
            // The four nearest occlusion pixels, bilinear and weighted by their distance's agreement with this pixel's.
            const float2 f = (pixel - float(g_size.z / 2u) - 0.5f) / float(g_size.z);
            const int2 base = int2(floor(f));
            const float2 w = f - float2(base);
            const float tolerance = 1.0f / (0.03f * z + 1.0f);
            float sum = 0.0f;
            float weight = 0.0f;
            [unroll]
            for (int k = 0; k < 4; ++k) {
                const int2 o = int2(k & 1, k >> 1);
                const float2 s = t_occlusion.Load(int3(clamp(base + o, int2(0, 0), int2(g_size.xy) - 1), 0)).xy;
                const float bilinear = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y);
                const float agree = s.y > 0.0f ? saturate(1.0f - abs(s.y - z) * tolerance) : 0.0f;
                sum += s.x * bilinear * agree;
                weight += bilinear * agree;
            }
            scale = ao_scale(weight > 1e-4f ? sum / weight : 1.0f);
        }
    }
    return scale;
}

#ifdef AO_PASS_upsample_cs
[numthreads(8, 8, 1)]
void UpsampleCS(uint3 id : SV_DispatchThreadID) {
    // Whole groups cover the target: the threads past its edge read and write nothing.
    uint width, height;
    u_scale.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    u_scale[id.xy] = ao_multiplier(int2(id.xy));
}
#endif

// A pixel the occlusion would change by less than half a level (8-bit) keeps its colour without a write.
float4 multiply(float scale) {
    if (scale > 1.0f - 0.5f / 255.0f) {
        discard;
    }
    return float4(scale, scale, scale, 1.0f);
}

#ifdef AO_PASS_apply_ps
// Direct3D 12 and 11 (their fxc build defines AO_PASS_apply_ps): the multiply reads the depth and the denoised occlusion
// itself, as no pass holds the depth as an attachment there, so no full-size multiplier image is written and read.
// Vulkan and Metal (AoApplyFusedPs) at one occlusion pixel per scene target pixel, where ao_multiplier reads no depth: the
// same, inside the scene pass. Its value is the 8-bit level that image held (round to nearest, as the UNORM store converts).
float4 ApplyPS(ApplyOut i) : SV_Target {
    return multiply(round(ao_multiplier(int2(i.position.xy)) * 255.0f) / 255.0f);
}
#else
float4 ApplyPS(ApplyOut i) : SV_Target {
    return multiply(t_occlusion.Load(int3(int2(i.position.xy), 0)).x);
}

// Vulkan: the multiplier image drawn as a colour target (8-bit, one channel; a tile GPU stores it compressed), in a
// pass of its own before the scene's resumes.
float4 UpsamplePS(ApplyOut i) : SV_Target {
    return ao_multiplier(int2(i.position.xy));
}
#endif
