// SPDX-License-Identifier: GPL-3.0-only
// The tracer's measuring rays: what one ray a pixel costs on this GPU, by kind. Mode 0 a camera ray and a ray to the sun
// (any hit ends it), 1 a camera ray and a short occlusion ray, 2 a camera ray and a bounce ray (nearest hit), 3 the
// camera ray alone. One float a pixel, so the dispatch has an output.
RaytracingAccelerationStructure tlas : register(t0);
RWStructuredBuffer<float> out_values : register(u0);

cbuffer Probe : register(b0) {
    uint mode;
    uint width;
    uint height;
    uint frame;
    float3 eye;
    float tan_half;
    float3 forward;
    float aspect;
    float3 right;
    float occlusion_range;
    float3 up;
    float pad0;
    float3 sun;
    float pad1;
};

uint hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float next(inout uint state) {
    state = hash(state);
    return float(state >> 8) * (1.0 / 16777216.0);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(width, height) * 2.0 - 1.0;
    RayDesc ray;
    ray.Origin = eye;
    ray.Direction = normalize(forward + right * (uv.x * tan_half * aspect) - up * (uv.y * tan_half));
    ray.TMin = 0.1;
    ray.TMax = 1.0e7;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> primary;
    primary.TraceRayInline(tlas, RAY_FLAG_FORCE_OPAQUE, 0xFF, ray);
    primary.Proceed();
    float result = 0.0;
    if (primary.CommittedStatus() == COMMITTED_TRIANGLE_HIT && mode != 3u) {
        const float3 at = ray.Origin + ray.Direction * primary.CommittedRayT();
        const float3x4 place = primary.CommittedObjectToWorld3x4();
        const float3 n = normalize(at - float3(place[0][3], place[1][3], place[2][3]));
        uint state = hash(id.x + id.y * width + frame * 0x9e3779b9u);
        RayDesc second;
        second.Origin = at + n * 1.0;
        second.TMin = 0.05;
        if (mode == 0u) {
            second.Direction = normalize(sun + (float3(next(state), next(state), next(state)) - 0.5) * 0.01);
            second.TMax = 1.0e7;
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> shadow;
            shadow.TraceRayInline(tlas, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, second);
            shadow.Proceed();
            result = shadow.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
        } else {
            const float u = next(state), v = next(state);
            const float radius = sqrt(u), phi = 6.2831853 * v;
            const float3 t = normalize(abs(n.y) < 0.99 ? cross(n, float3(0.0, 1.0, 0.0)) : cross(n, float3(1.0, 0.0, 0.0)));
            const float3 b = cross(n, t);
            second.Direction = normalize(t * (radius * cos(phi)) + b * (radius * sin(phi)) + n * sqrt(1.0 - u));
            if (mode == 1u) {
                second.TMax = occlusion_range;
                RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> occlusion;
                occlusion.TraceRayInline(tlas, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, second);
                occlusion.Proceed();
                result = occlusion.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
            } else {
                second.TMax = 1.0e7;
                RayQuery<RAY_FLAG_FORCE_OPAQUE> bounce;
                bounce.TraceRayInline(tlas, RAY_FLAG_FORCE_OPAQUE, 0xFF, second);
                bounce.Proceed();
                result = bounce.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? bounce.CommittedRayT() : -1.0;
            }
        }
    } else if (primary.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        result = primary.CommittedRayT();
    }
    out_values[id.x + id.y * width] = result;
}
