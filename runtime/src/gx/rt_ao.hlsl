// SPDX-License-Identifier: GPL-3.0-only
// Ray-traced ambient occlusion of the game frame's opaque scene (DXR 1.1 inline ray queries,
// cs_6_5) and its composite into the EFB (vs/ps_6_0). Compiled by DXC at build time.
//
// Occlusion is sampled once per g_factor x g_factor EFB pixels: the surface point from the
// raster depth, its normal from the neighbouring depths, then cosine-weighted rays with a
// quadratic distance falloff. Samples accumulate over frames along the game camera's motion
// where the previous frame's surface lies on the same plane, and a 5x5 edge-aware filter
// removes the remaining noise. The composite multiplies every EFB pixel by the occlusion at
// its own depth (bilateral upsampling); interpolated sub-frames first move the pixel's point
// to the traced frame's camera.

cbuffer RtAo : register(b0)
{
    float4 g_projection;    // GX projection A, B, C, D
    float4 g_depth;         // D3D projection [2][2], [2][3]; viewport min depth, max depth
    float4 g_viewport;      // left, top, width, height (EFB pixels)
    uint2 g_target_size;    // EFB pixels
    uint2 g_ao_size;        // occlusion samples
    float g_near;           // near-plane distance (eye units)
    uint g_frame;           // sample sequence index
    float g_radius;         // occlusion ray length (eye units)
    float g_strength;       // 0 none .. 1 full
    uint g_rays;            // rays per sample
    uint g_factor;          // EFB pixels per sample side
    uint g_flags;           // bit 0: the previous frame's samples are usable
    uint g_history_frames;  // accumulation limit
    float4 g_reproject[3];  // previous frame's eye position from this frame's (3x4 rows)
};

RaytracingAccelerationStructure g_scene : register(t6);
Texture2D<float> g_efb_depth : register(t7);
StructuredBuffer<float> g_prev_z : register(t8);
StructuredBuffer<float2> g_prev_history : register(t9);
StructuredBuffer<float2> g_history_in : register(t10);
RWStructuredBuffer<float> g_z : register(u0);
RWStructuredBuffer<uint> g_normal : register(u1);
RWStructuredBuffer<float> g_raw : register(u2);
RWStructuredBuffer<float2> g_history : register(u3);
RWStructuredBuffer<float2> g_final : register(u4);

static const float kPi = 3.14159265f;

// Direction through an EFB position (pixel centres at +0.5), scaled to eye z = -1.
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

// ---------------------------------------------------------------------------------------------
// Occlusion passes (one thread per sample)
// ---------------------------------------------------------------------------------------------

// The EFB pixel a sample stands for (the first of its block) and its centre.
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

// The eye position of EFB pixel q, or false for the far plane and pixels off the texture.
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

// Surface normal from the depth: per axis the neighbour nearer in depth (the same surface at
// an edge), facing the camera.
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

// Per-sample points: an R2 sequence offset by interleaved gradient noise, advancing each frame
// so the accumulation sees new directions.
float2 sample_point(uint2 s, uint i)
{
    const float ign = frac(52.9829189f * frac(0.06711056f * float(s.x) + 0.00583715f * float(s.y)));
    const float n = float(i) + float(g_frame % 4096u) * float(g_rays);
    return frac(float2(ign, frac(ign * 1.61803399f)) + n * float2(0.7548776662f, 0.5698402910f));
}

[numthreads(8, 8, 1)]
void AoTraceCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_ao_size)) {
        return;
    }
    const uint index = id.y * g_ao_size.x + id.x;
    const int2 q = sample_pixel(id.xy);
    float3 p;
    if (!pixel_point(q, p) || !inside(g_viewport, float2(q) + 0.5f)) {
        g_z[index] = 0.0f;
        g_normal[index] = 0u;
        g_raw[index] = 1.0f;
        return;
    }
    const float3 n = surface_normal(q, p);

    // Orthonormal basis around the normal (Duff et al.).
    const float sign_z = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (sign_z + n.z);
    const float b = n.x * n.y * a;
    const float3 t1 = float3(1.0f + sign_z * n.x * n.x * a, sign_z * b, -sign_z * n.x);
    const float3 t2 = float3(b, sign_z + n.y * n.y * a, -n.y);

    const float3 origin = p + n * (0.2f + abs(p.z) * 4e-4f);
    float occlusion = 0.0f;
    const uint rays = max(g_rays, 1u);
    for (uint i = 0u; i < rays; ++i) {
        const float2 u = sample_point(id.xy, i);
        const float r = sqrt(u.x);
        const float phi = 2.0f * kPi * u.y;
        RayDesc ray;
        ray.Origin = origin;
        ray.Direction = t1 * (r * cos(phi)) + t2 * (r * sin(phi)) + n * sqrt(max(0.0f, 1.0f - u.x));
        ray.TMin = 0.0f;
        ray.TMax = g_radius;
        RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
        query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
        query.Proceed();
        if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            const float f = 1.0f - query.CommittedRayT() / g_radius;
            occlusion += f * f;
        }
    }
    g_z[index] = p.z;
    g_normal[index] = pack_normal(n);
    g_raw[index] = 1.0f - occlusion / float(rays);
}

// Accumulation: the sample's point in the previous frame, bilinear over the previous samples
// that lie on its plane, weighted towards the history up to g_history_frames.
[numthreads(8, 8, 1)]
void AoTemporalCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_ao_size)) {
        return;
    }
    const uint index = id.y * g_ao_size.x + id.x;
    const float z = g_z[index];
    const float raw = g_raw[index];
    if (z == 0.0f) {
        g_history[index] = float2(1.0f, 0.0f);
        return;
    }
    float2 result = float2(raw, 1.0f);
    if ((g_flags & 1u) != 0u) {
        const float4 h = float4(sample_position(id.xy, z), 1.0f);
        const float3 previous = float3(dot(g_reproject[0], h), dot(g_reproject[1], h), dot(g_reproject[2], h));
        const float3 n = unpack_normal(g_normal[index]);
        const float3 normal = normalize(float3(dot(g_reproject[0].xyz, n), dot(g_reproject[1].xyz, n),
                                               dot(g_reproject[2].xyz, n)));
        if (previous.z < -g_near) {
            const float2 a = (eye_to_pixel(g_projection, g_viewport, previous) - 0.5f) / float(g_factor);
            const int2 base = int2(floor(a));
            const float2 w = a - float2(base);
            const float tolerance = 0.5f + abs(previous.z) * 0.01f;
            float2 sum = float2(0.0f, 0.0f);
            float weight = 0.0f;
            [unroll] for (uint k = 0u; k < 4u; ++k) {
                const int2 o = int2(k & 1u, k >> 1u);
                const int2 s = base + o;
                if (any(s < 0) || any(s >= int2(g_ao_size))) {
                    continue;
                }
                const uint si = uint(s.y) * g_ao_size.x + uint(s.x);
                const float sz = g_prev_z[si];
                if (sz == 0.0f || abs(dot(normal, sample_position(uint2(s), sz) - previous)) > tolerance) {
                    continue;
                }
                const float bw = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y);
                sum += g_prev_history[si] * bw;
                weight += bw;
            }
            if (weight > 1e-3f) {
                const float2 history = sum / weight;
                const float count = min(history.y + 1.0f, float(g_history_frames));
                result = float2(lerp(history.x, raw, 1.0f / count), count);
            }
        }
    }
    g_history[index] = result;
}

// Edge-aware 5x5 filter (plane distance and normal), then the strength.
[numthreads(8, 8, 1)]
void AoSpatialCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= g_ao_size)) {
        return;
    }
    const uint index = id.y * g_ao_size.x + id.x;
    const float z = g_z[index];
    if (z == 0.0f) {
        g_final[index] = float2(1.0f, 0.0f);
        return;
    }
    const float3 p = sample_position(id.xy, z);
    const float3 n = unpack_normal(g_normal[index]);
    const float inv_tolerance = 1.0f / (0.25f + abs(z) * 0.004f);
    float sum = 0.0f;
    float weight = 0.0f;
    [unroll] for (int dy = -2; dy <= 2; ++dy) {
        [unroll] for (int dx = -2; dx <= 2; ++dx) {
            const int2 s = int2(id.xy) + int2(dx, dy);
            if (any(s < 0) || any(s >= int2(g_ao_size))) {
                continue;
            }
            const uint si = uint(s.y) * g_ao_size.x + uint(s.x);
            const float sz = g_z[si];
            if (sz == 0.0f) {
                continue;
            }
            const float plane = abs(dot(n, sample_position(uint2(s), sz) - p)) * inv_tolerance;
            const float facing = saturate(dot(n, unpack_normal(g_normal[si])));
            const float facing2 = facing * facing;
            const float w = exp(-0.18f * float(dx * dx + dy * dy) - plane * plane) * (facing2 * facing2) * (facing2 * facing2);
            sum += g_history_in[si].x * w;
            weight += w;
        }
    }
    const float ao = weight > 1e-4f ? sum / weight : g_history_in[index].x;
    g_final[index] = float2(lerp(1.0f, ao, g_strength), z);
}

// ---------------------------------------------------------------------------------------------
// Composite: the EFB colour times the occlusion (blend dst * src on RGB)
// ---------------------------------------------------------------------------------------------

cbuffer RtComposite : register(b0)
{
    float4 c_projection;
    float4 c_depth;
    float4 c_viewport;
    uint2 c_ao_size;
    uint c_factor;
    uint c_flags;   // bit 0: move the point to the traced frame's camera (c_reproject[c_slot]);
                    // bit 1: show the occlusion alone
    uint c_slot;
    float c_near;
    uint2 c_pad;
};
StructuredBuffer<float2> c_ao : register(t0);
Texture2D<float> c_efb_depth : register(t1);
StructuredBuffer<float4> c_reproject : register(t2);

float4 CompositeVS(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1u) & 2u, id & 2u);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float4 CompositePS(float4 position : SV_Position) : SV_Target
{
    const float depth = c_efb_depth.Load(int3(int2(position.xy), 0));
    const float z = depth_to_eye_z(c_depth, depth);
    // Depth slope across the pixel quad, before any lane leaves.
    const float slope = min(abs(ddx(z)) + abs(ddy(z)), abs(z) * 0.05f);
    if (far_plane(c_depth, depth) || !inside(c_viewport, position.xy)) {
        if ((c_flags & 2u) != 0u) {
            return float4(1.0f, 1.0f, 1.0f, 1.0f);
        }
        discard;
    }
    float3 p = eye_position(c_projection, c_viewport, position.xy, z);
    if ((c_flags & 1u) != 0u) {
        const float4 h = float4(p, 1.0f);
        const uint row = c_slot * 3u;
        p = float3(dot(c_reproject[row], h), dot(c_reproject[row + 1u], h), dot(c_reproject[row + 2u], h));
        if (p.z >= -c_near) {
            discard;
        }
    }
    const float2 a = (eye_to_pixel(c_projection, c_viewport, p) - 0.5f) / float(c_factor);
    const int2 base = int2(floor(a));
    const float2 w = a - float2(base);
    const float tolerance = slope * float(c_factor) * 1.5f + abs(p.z) * 0.01f + 0.25f;
    float sum = 0.0f;
    float weight = 0.0f;
    float nearest = 1.0f;
    float nearest_dz = abs(p.z) * 0.1f;
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 o = int2(k & 1u, k >> 1u);
        const int2 s = clamp(base + o, int2(0, 0), int2(c_ao_size) - 1);
        const float2 sample = c_ao[uint(s.y) * c_ao_size.x + uint(s.x)];
        if (sample.y == 0.0f) {
            continue;
        }
        const float dz = abs(sample.y - p.z);
        if (dz < nearest_dz) {
            nearest_dz = dz;
            nearest = sample.x;
        }
        const float d = dz / tolerance;
        const float bw = (o.x != 0 ? w.x : 1.0f - w.x) * (o.y != 0 ? w.y : 1.0f - w.y) * exp(-d * d);
        sum += sample.x * bw;
        weight += bw;
    }
    const float ao = weight > 1e-3f ? sum / weight : nearest;
    return float4(ao, ao, ao, 1.0f);
}
