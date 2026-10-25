// Ambient occlusion of the opaque scene (ambient_occlusion.h): ground-truth ambient occlusion (Jimenez, Wu, Pesce
// and Jarabo, "Practical Realtime Strategies for Accurate Indirect Occlusion", SIGGRAPH 2016) over the EFB's depth,
// in the form of Intel's XeGTAO (MIT): per pixel a few slices about the view direction, in each the highest horizon
// on both sides within a radius, integrated against the slice's projected normal. Fixed 4x4 interleaved slice and
// step patterns, removed exactly by the denoise's 5-tap tent (each pattern cell weighs the same), so a still
// picture stays still without a temporal filter. The result multiplies the scene's colour before its translucent
// phases draw.
//
// The occlusion is computed per occlusion pixel, one per g_size.z x g_size.z EFB pixels (the game's own resolution),
// from the eye distance there:
// DepthCS: the eye distance of each occlusion pixel's centre EFB pixel (0 for the far plane) into u0.
// AoCS: occlusion and eye distance from t2 (DepthCS's output) into u0.
// DenoiseCS: the depth-weighted tent of t1 into u0.
// ApplyVS/ApplyPS: per EFB pixel the denoised occlusion (depth-weighted bilinear), the colour multiplier of a
// multiplicative blend (g_size.w 1: the occlusion itself, the scene replaced; GALAXY_AO_VIEW).

cbuffer AoConstants : register(b0) {
    float4 g_ray;         // an EFB pixel's eye ray at distance 1: x = px * x + y, y = py * z + w (z = -1)
    float4 g_depth;       // RtView z_scale, z_offset, min_depth, max_depth
    float4 g_viewport;    // the scene viewport in EFB pixels: x, y, width, height
    float4 g_params;      // radius (eye units), visibility exponent, strength, largest radius in occlusion pixels
    uint4 g_size;         // occlusion target width, height; EFB pixels per occlusion pixel; 1: show the occlusion
};

Texture2D<float> t_depth : register(t0);       // the EFB's depth
Texture2D<float2> t_occlusion : register(t1);  // occlusion, eye distance
Texture2D<float> t_distance : register(t2);    // DepthCS's eye distance
RWTexture2D<float2> u_occlusion : register(u0);
RWTexture2D<float> u_distance : register(u0);

static const float kPi = 3.14159265f;
static const uint kSlices = 2u;
static const uint kSteps = 3u;
// XeGTAO's falloff: full weight to 38.5% of the radius, then linearly to none at the radius.
static const float kFalloffRange = 0.615f;

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

// The EFB pixel centre an occlusion pixel stands for.
float2 efb_centre(int2 o) {
    return float2(o * int(g_size.z) + int(g_size.z / 2u)) + 0.5f;
}

float3 eye_position(float2 pixel, float distance) {
    return float3(pixel.x * g_ray.x + g_ray.y, pixel.y * g_ray.z + g_ray.w, -1.0f) * distance;
}

// XeGTAO_FastACos (Eberly's first-order fit): within 0.0007 rad.
float fast_acos(float x) {
    const float a = abs(x);
    const float r = (-0.156583f * a + 0.5f * kPi) * sqrt(1.0f - a);
    return x >= 0.0f ? r : kPi - r;
}

[numthreads(8, 8, 1)]
void DepthCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_size.x || id.y >= g_size.y) {
        return;
    }
    const float2 centre = efb_centre(int2(id.xy));
    const float depth = t_depth.Load(int3(int2(centre), 0));
    u_distance[id.xy] = inside(centre) && !far_plane(depth) ? -depth_to_eye_z(depth) : 0.0f;
}

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
    p = eye_position(efb_centre(o), distance);
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

[numthreads(8, 8, 1)]
void AoCS(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_size.x || id.y >= g_size.y) {
        return;
    }
    const int2 o = int2(id.xy);
    float3 p;
    if (!point_at(o, p)) {
        u_occlusion[id.xy] = float2(1.0f, 0.0f);
        return;
    }
    // The radius in occlusion pixels; below two nothing is resolved.
    const float radius = g_params.x;
    const float screen_radius = min(radius / (g_ray.x * -p.z * float(g_size.z)), g_params.w);
    if (screen_radius < 2.0f) {
        u_occlusion[id.xy] = float2(1.0f, -p.z);
        return;
    }
    const float3 n = surface_normal(o, p);
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
                const float weight = saturate(distance * falloff_mul + falloff_add);
                horizon0 = max(horizon0, lerp(low0, dot(delta, v) / distance, weight));
            }
            if (point_at(o - offset, sp)) {
                const float3 delta = sp - p;
                const float distance = length(delta);
                const float weight = saturate(distance * falloff_mul + falloff_add);
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
    visibility = pow(saturate(visibility / float(kSlices)), g_params.y);
    u_occlusion[id.xy] = float2(visibility, -p.z);
}

// The 5x5 tent (0.5, 1, 1, 1, 0.5 per axis): every cell of the 4x4 patterns counts once. Neighbours at another depth
// (beyond 3% of the distance) count less, the far plane not at all. Each group reads its 12x12 neighbourhood into
// shared memory once.
#ifdef AO_COMPUTE
groupshared float2 gs_tile[12][12];

[numthreads(8, 8, 1)]
void DenoiseCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint index : SV_GroupIndex) {
    const int2 origin = int2(group.xy) * 8 - 2;
    for (uint i = index; i < 144u; i += 64u) {
        const int2 at = clamp(origin + int2(i % 12u, i / 12u), int2(0, 0), int2(g_size.xy) - 1);
        gs_tile[i / 12u][i % 12u] = t_occlusion.Load(int3(at, 0));
    }
    GroupMemoryBarrierWithGroupSync();
    if (id.x >= g_size.x || id.y >= g_size.y) {
        return;
    }
    const int2 local = int2(id.xy) - origin;
    const float2 centre = gs_tile[local.y][local.x];
    if (centre.y <= 0.0f) {
        u_occlusion[id.xy] = centre;
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
    u_occlusion[id.xy] = float2(weight > 0.0f ? sum / weight : centre.x, centre.y);
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

// Jimenez et al.'s multiple-bounce fit for an albedo of 0.5 (the scene's colour is lit, not its albedo): light
// bouncing inside a crease brightens it.
float multi_bounce(float visibility) {
    const float a = 2.0404f * 0.5f - 0.3324f;
    const float b = -4.7951f * 0.5f + 0.6417f;
    const float c = 2.7552f * 0.5f + 0.6903f;
    return max(visibility, ((visibility * a + b) * visibility + c) * visibility);
}

float4 ApplyPS(ApplyOut i) : SV_Target {
    const int2 q = int2(i.position.xy);
    const float depth = t_depth.Load(int3(q, 0));
    const bool view = g_size.w == 1u;
    if (far_plane(depth) || !inside(i.position.xy)) {
        if (view) {
            return float4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        discard;
    }
    const float z = -depth_to_eye_z(depth);
    float visibility;
    if (g_size.z <= 1u) {
        visibility = t_occlusion.Load(int3(q, 0)).x;
    } else {
        // The four nearest occlusion pixels, bilinear and weighted by their distance's agreement with this pixel's.
        const float2 f = (i.position.xy - float(g_size.z / 2u) - 0.5f) / float(g_size.z);
        const int2 base = int2(floor(f));
        const float2 w = f - float2(base);
        const float tolerance = 1.0f / (0.03f * z + 1.0f);
        float sum = 0.0f;
        float weight = 0.0f;
        [unroll]
        for (int k = 0; k < 4; ++k) {
            const int2 o = int2(k & 1, k >> 1);
            const int2 at = clamp(base + o, int2(0, 0), int2(g_size.xy) - 1);
            const float2 s = t_occlusion.Load(int3(at, 0));
            const float bilinear = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y);
            const float agree = s.y > 0.0f ? saturate(1.0f - abs(s.y - z) * tolerance) : 0.0f;
            sum += s.x * bilinear * agree;
            weight += bilinear * agree;
        }
        visibility = weight > 1e-4f ? sum / weight : 1.0f;
    }
    // The occlusion scales light, linear; the EFB holds display-encoded colour. A pixel it would change by less than a
    // half level (8-bit) keeps its colour without a write.
    const float scale = pow(max(lerp(1.0f, multi_bounce(saturate(visibility)), g_params.z), 0.0f), 1.0f / 2.2f);
    if (view) {
        return float4(scale, scale, scale, 1.0f);
    }
    if (scale > 1.0f - 0.5f / 255.0f) {
        discard;
    }
    return float4(scale, scale, scale, 1.0f);
}
