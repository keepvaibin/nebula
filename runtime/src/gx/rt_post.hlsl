// SPDX-License-Identifier: GPL-3.0-only
// The ray-traced picture's cinematic finish at the scene's end, before the 2D layer (compute
// cs_6_5 and vs/ps_6_0, compiled by DXC at build time): bloom of everything bright, light shafts
// from the star core and the sun, a far depth of field, the star core's surface, flares and
// corona (the game's AstroCore: its position and the scale it grows to), and a colour grade.
// Sub-frames run it again on their own picture.

cbuffer RtPost : register(b0)
{
    float4 p_projection;        // GX projection A, B, C, D
    float4 p_depth;             // D3D projection [2][2], [2][3]; viewport min depth, max depth
    float4 p_viewport;          // left, top, width, height (EFB pixels)
    uint2 p_size;               // EFB pixels
    uint2 p_quarter;            // quarter-resolution texels
    float4 p_core;              // the star core's eye-space centre (xyz), radius (w; 0: none)
    float4 p_core_light;        // its colour (rgb), its brightness (w; grows with it)
    float4 p_sun;               // towards the sun in eye space (xyz), its light (w; 0: none)
    float p_time;               // seconds
    float p_bloom;              // bloom strength
    float p_threshold;          // luminance where bloom starts
    float p_shafts;             // light shaft strength
    float p_dof;                // largest blur radius (EFB pixels; 0: none)
    float p_grade;              // colour grade strength (0: none)
    float p_split;              // GALAXY_RT_SPLIT: left of it the game's own picture
    uint p_axis;                // blur pass: 0 across, 1 down
};

Texture2D<float4> p_color : register(t0);
Texture2D<float> p_depth_tex : register(t1);
StructuredBuffer<uint> p_in : register(t2);
RWStructuredBuffer<uint> p_out : register(u0);
RWStructuredBuffer<float> p_state : register(u1);   // focus distance, known; core radius, known

static const float kPi = 3.14159265f;

float luminance(float3 c)
{
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

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

float depth_to_eye_z(float depth)
{
    const float ndc = (depth - p_depth.z) / max(p_depth.w - p_depth.z, 1e-7f);
    return -p_depth.y / (ndc + p_depth.x);
}

bool far_plane(float depth)
{
    return depth <= p_depth.z + 1e-6f;
}

float3 pixel_ray(float2 pixel)
{
    const float ndc_x = (pixel.x - p_viewport.x) / p_viewport.z * 2.0f - 1.0f;
    const float ndc_y = 1.0f - (pixel.y - p_viewport.y) / p_viewport.w * 2.0f;
    return float3((ndc_x + p_projection.y) / p_projection.x, (ndc_y + p_projection.w) / p_projection.z, -1.0f);
}

float2 eye_to_pixel(float3 p)
{
    const float w = -p.z;
    const float ndc_x = (p_projection.x * p.x + p_projection.y * p.z) / w;
    const float ndc_y = (p_projection.z * p.y + p_projection.w * p.z) / w;
    return float2(p_viewport.x + (ndc_x + 1.0f) * 0.5f * p_viewport.z,
                  p_viewport.y + (1.0f - ndc_y) * 0.5f * p_viewport.w);
}

// Eye distance of a pixel's surface (a large value for the sky).
float scene_distance(int2 q)
{
    const float depth = p_depth_tex.Load(int3(clamp(q, int2(0, 0), int2(p_size) - 1), 0));
    return far_plane(depth) ? 1.0e9f : -depth_to_eye_z(depth);
}

// Value noise in 3D (hash of lattice corners, smooth interpolation) and its fractal sum.
float hash3(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise3(float3 x)
{
    const float3 i = floor(x);
    const float3 f = x - i;
    const float3 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(hash3(i + float3(0, 0, 0)), hash3(i + float3(1, 0, 0)), u.x),
                     lerp(hash3(i + float3(0, 1, 0)), hash3(i + float3(1, 1, 0)), u.x), u.y),
                lerp(lerp(hash3(i + float3(0, 0, 1)), hash3(i + float3(1, 0, 1)), u.x),
                     lerp(hash3(i + float3(0, 1, 1)), hash3(i + float3(1, 1, 1)), u.x), u.y), u.z);
}

float fbm3(float3 x)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    [unroll] for (uint i = 0u; i < 4u; ++i) {
        sum += noise3(x) * amplitude;
        x = x * 2.03f + float3(1.7f, 9.2f, 4.1f);
        amplitude *= 0.5f;
    }
    return sum;
}

// A star's colour at temperature t (0 deep red .. 1 white-hot).
float3 star_ramp(float t)
{
    const float3 red = float3(0.85f, 0.18f, 0.03f);
    const float3 orange = float3(1.0f, 0.52f, 0.12f);
    const float3 yellow = float3(1.0f, 0.86f, 0.45f);
    const float3 white = float3(1.0f, 0.98f, 0.92f);
    return t < 0.4f ? lerp(red, orange, t / 0.4f) : t < 0.75f ? lerp(orange, yellow, (t - 0.4f) / 0.35f)
                                                             : lerp(yellow, white, saturate((t - 0.75f) / 0.25f));
}

// ---------------------------------------------------------------------------------------------
// Quarter resolution: the bright part of the picture (and what the glows added), its blur, the
// light shafts, and the focus distance.
// ---------------------------------------------------------------------------------------------

// The picture before the finish (RGBA8), read by the full-screen pass that writes over it.
[numthreads(8, 8, 1)]
void PostCopyCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= p_size)) {
        return;
    }
    const uint4 c = uint4(round(saturate(p_color.Load(int3(id.xy, 0))) * 255.0f));
    p_out[id.y * p_size.x + id.x] = c.r | (c.g << 8u) | (c.b << 16u) | (c.a << 24u);
}

[numthreads(8, 8, 1)]
void PostBrightCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= p_quarter)) {
        return;
    }
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 q = min(int2(id.xy * 4u + uint2(1u + (k & 1u) * 2u, 1u + (k >> 1u) * 2u)), int2(p_size) - 1);
        sum += p_color.Load(int3(q, 0)).rgb;
    }
    float3 c = sum * 0.25f;
    const float l = luminance(c);
    c *= max(l - p_threshold, 0.0f) / max(l * (1.0f - p_threshold), 1e-4f);
    p_out[id.y * p_quarter.x + id.x] = pack_r11g11b10(c);
}

[numthreads(8, 8, 1)]
void PostBlurCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= p_quarter)) {
        return;
    }
    const int2 step = p_axis == 0u ? int2(1, 0) : int2(0, 1);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float weight = 0.0f;
    [loop] for (int i = -20; i <= 20; ++i) {
        const int2 s = clamp(int2(id.xy) + step * i, int2(0, 0), int2(p_quarter) - 1);
        const float w = exp(-float(i * i) / 98.0f);
        sum += unpack_r11g11b10(p_in[uint(s.y) * p_quarter.x + uint(s.x)]) * w;
        weight += w;
    }
    p_out[id.y * p_quarter.x + id.x] = pack_r11g11b10(sum / weight);
}

// Light shafts: towards the core and the sun on screen, the bright picture between (in front of
// the light's distance) scatters along the way.
float3 shaft(float2 texel, float2 light, float strength)
{
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    const float2 delta = (light - texel) / 28.0f;
    float2 at = texel;
    float decay = 1.0f;
    [loop] for (uint i = 0u; i < 28u; ++i) {
        at += delta;
        const int2 s = int2(at);
        if (any(s < 0) || any(s >= int2(p_quarter))) {
            break;
        }
        sum += unpack_r11g11b10(p_in[uint(s.y) * p_quarter.x + uint(s.x)]) * decay;
        decay *= 0.94f;
    }
    return sum * (strength / 28.0f);
}

[numthreads(8, 8, 1)]
void PostShaftsCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= p_quarter)) {
        return;
    }
    const float2 texel = float2(id.xy) + 0.5f;
    float3 c = float3(0.0f, 0.0f, 0.0f);
    if (p_core.w > 0.0f && p_core.z < 0.0f) {
        c += shaft(texel, eye_to_pixel(p_core.xyz) / 4.0f, p_shafts * p_core_light.w);
    }
    if (p_sun.w > 0.0f && p_sun.z < -0.05f) {
        c += shaft(texel, eye_to_pixel(p_sun.xyz * 1.0e5f) / 4.0f, p_shafts * p_sun.w);
    }
    p_out[id.y * p_quarter.x + id.x] = pack_r11g11b10(c);
}

// The focus: the surfaces around the screen's centre (the player), over time.
[numthreads(1, 1, 1)]
void PostFocusCS()
{
    float sum = 0.0f;
    float n = 0.0f;
    [unroll] for (int y = -2; y <= 2; ++y) {
        [unroll] for (int x = -2; x <= 2; ++x) {
            const int2 q = int2(p_viewport.xy + p_viewport.zw * float2(0.5f + 0.06f * x, 0.55f + 0.06f * y));
            const float d = scene_distance(q);
            if (d < 1.0e8f) {
                sum += d;
                n += 1.0f;
            }
        }
    }
    if (n > 0.0f) {
        const float focus = sum / n;
        p_state[0] = p_state[1] != 0.0f ? lerp(p_state[0], focus, 0.08f) : focus;
        p_state[1] = 1.0f;
    }
    // The core's radius: its centre's distance less its front surface's along the ray to it.
    if (p_core.w > 0.0f && p_core.z < 0.0f) {
        const float2 at = eye_to_pixel(p_core.xyz);
        if (all(at >= p_viewport.xy) && all(at < p_viewport.xy + p_viewport.zw)) {
            const float3 ray = normalize(p_core.xyz);
            const float front = scene_distance(int2(at)) / max(-ray.z, 1e-4f);
            const float radius = length(p_core.xyz) - front;
            if (radius > 0.2f * p_core.w && radius < 5.0f * p_core.w) {
                p_state[2] = p_state[3] != 0.0f ? lerp(p_state[2], radius, 0.1f) : radius;
                p_state[3] = 1.0f;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The finish: depth of field, the star core, bloom, shafts, grade (full screen, into the EFB)
// ---------------------------------------------------------------------------------------------

float4 PostVS(uint id : SV_VertexID) : SV_Position
{
    const float2 uv = float2((id << 1u) & 2u, id & 2u);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

StructuredBuffer<uint> f_scene : register(t2);    // the picture before the finish (RGBA8)
StructuredBuffer<uint> f_bloom : register(t3);
StructuredBuffer<uint> f_shafts : register(t4);
StructuredBuffer<float> f_state : register(t5);

float3 scene_at(int2 q)
{
    q = clamp(q, int2(0, 0), int2(p_size) - 1);
    const uint v = f_scene[uint(q.y) * p_size.x + uint(q.x)];
    return float3(v & 0xFFu, (v >> 8u) & 0xFFu, (v >> 16u) & 0xFFu) / 255.0f;
}

float3 quarter_at(StructuredBuffer<uint> buffer, float2 pixel)
{
    const float2 t = pixel / 4.0f - 0.5f;
    const int2 b = int2(floor(t));
    const float2 f = t - float2(b);
    float3 c = float3(0.0f, 0.0f, 0.0f);
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 o = int2(k & 1u, k >> 1u);
        const int2 s = clamp(b + o, int2(0, 0), int2(p_quarter) - 1);
        const float w = (o.x != 0 ? f.x : 1.0f - f.x) * (o.y != 0 ? f.y : 1.0f - f.y);
        c += unpack_r11g11b10(buffer[uint(s.y) * p_quarter.x + uint(s.x)]) * w;
    }
    return c;
}

// The star core: its surface (granulation boiling over time, hotter towards the centre), and
// around its limb flares (tongues of flame) and embers rising from it, behind what stands in front.
float3 star_core(float2 pixel, float distance, float3 color)
{
    const float3 centre = p_core.xyz;
    const float radius = f_state[3] != 0.0f ? f_state[2] : p_core.w;
    const float3 ray = normalize(pixel_ray(pixel));
    // The surface's distance along this ray (the depth buffer holds eye z).
    distance /= max(-ray.z, 1e-4f);
    const float b = dot(ray, centre);
    const float c = dot(centre, centre) - radius * radius;
    const float h = b * b - c;
    const float glow = p_core_light.w;
    if (h > 0.0f) {
        const float t = b - sqrt(h);
        if (t > 0.0f && distance > t - radius * 0.15f) {
            const float3 s = ray * t;
            const float3 n = (s - centre) / radius;
            const float mu = saturate(dot(n, -ray));
            // The game's own fire, given depth: its hot cells stand out from the cooler lanes
            // around them (its luminance against the local mean), the sphere is brightest at its
            // centre and deepens towards red at the limb (limb darkening), and the whole surface
            // burns brighter as the core grows; what passes 1 turns white-hot.
            const int2 q = int2(pixel);
            const float l = luminance(color);
            const float around = 0.25f * (luminance(scene_at(q + int2(3, 0))) + luminance(scene_at(q - int2(3, 0))) +
                                          luminance(scene_at(q + int2(0, 3))) + luminance(scene_at(q - int2(0, 3))));
            const float relief = clamp(1.0f + 2.2f * (l - around), 0.4f, 2.2f);
            const float limb = 0.45f + 0.55f * pow(mu, 0.6f);
            float3 hot = color * (1.05f + 0.3f * glow) * limb * relief;
            hot = lerp(hot * float3(1.0f, 0.72f, 0.5f), hot, smoothstep(0.0f, 0.6f, mu));
            hot += max(max(hot.r, max(hot.g, hot.b)) - 1.0f, 0.0f) * 0.35f;
            return hot;
        }
    }
    // Outside the limb: flares and embers in front of what lies behind the core.
    const float3 closest = ray * max(b, 0.0f);
    const float miss = length(closest - centre);
    const float above = (miss - radius) / radius;
    if (above > 0.0f && above < 0.9f && distance > b) {
        const float3 side = normalize(closest - centre);
        const float angle = atan2(dot(side, float3(0.0f, 1.0f, 0.0f)), dot(side, float3(1.0f, 0.0f, 0.0f)));
        const float tongues = fbm3(float3(angle * 3.0f, above * 4.0f - p_time * 0.6f, p_time * 0.15f));
        const float reach = 0.12f + 0.45f * pow(saturate(tongues * 1.6f - 0.35f), 2.0f) * (0.6f + 0.4f * glow);
        const float flare = saturate(1.0f - above / reach) * saturate(tongues * 1.8f - 0.4f);
        const float corona = exp(-above * 6.0f) * 0.35f;
        float embers = 0.0f;
        [loop] for (uint i = 0u; i < 14u; ++i) {
            const float seed = float(i) * 1.618f;
            const float life = frac(p_time * (0.18f + 0.07f * frac(seed * 7.1f)) + frac(seed * 3.3f));
            const float a = frac(seed * 0.618f) * 2.0f * kPi + life * 0.4f;
            const float r = 1.0f + life * (0.5f + 0.4f * frac(seed * 5.7f));
            const float3 dir = float3(cos(a), sin(a), 0.0f);
            const float3 ember = centre + (dir.x * float3(1.0f, 0.0f, 0.0f) + dir.y * float3(0.0f, 1.0f, 0.0f)) * radius * r;
            const float2 at = eye_to_pixel(ember);
            const float size = radius * 0.035f * p_projection.x * p_viewport.z * 0.5f / max(-ember.z, 1.0f);
            const float d = length(pixel - at) / max(size, 1.0f);
            embers += exp(-d * d) * (1.0f - life) * 1.4f;
        }
        const float3 heat = star_ramp(0.55f) * (flare * 1.3f + corona) + star_ramp(0.8f) * embers;
        return color + heat * (0.6f + 0.6f * glow);
    }
    return color;
}

// Light above 1 (bloom, glows, the star core) rolls off softly to 1 instead of clipping flat;
// what one channel loses spills into the others, as over-exposed light turns white.
float3 shoulder(float3 c)
{
    const float3 over = max(c - 0.8f, 0.0f);
    const float3 rolled = min(c, 0.8f) + 0.2f * (1.0f - exp(-over / 0.2f));
    const float spill = max(max(over.r, max(over.g, over.b)) - 0.2f, 0.0f);
    return saturate(rolled + spill * 0.15f);
}

float3 grade(float3 c, float2 pixel)
{
    // Filmic contrast around mid grey, richer colour, warm highlights against cool shadows, and a
    // soft vignette.
    c = shoulder(max(c, 0.0f));
    const float l = luminance(c);
    const float3 curved = saturate(c + 0.5f * (c - 0.5f) * c * (1.0f - c));
    float3 g = lerp(l.xxx, curved, 1.12f);
    const float3 shadows = float3(0.94f, 0.98f, 1.06f);
    const float3 highlights = float3(1.05f, 1.01f, 0.94f);
    g *= lerp(shadows, highlights, smoothstep(0.15f, 0.85f, l));
    const float2 v = (pixel - (p_viewport.xy + p_viewport.zw * 0.5f)) / (p_viewport.zw * 0.5f);
    g *= 1.0f - 0.14f * pow(saturate(dot(v, v) * 0.5f), 1.5f);
    return lerp(c, saturate(g), p_grade);
}

float4 PostPS(float4 position : SV_Position) : SV_Target0
{
    const int2 q = int2(position.xy);
    if (position.x < p_split || any(position.xy < p_viewport.xy) || any(position.xy >= p_viewport.xy + p_viewport.zw)) {
        discard;
    }
    float3 c = scene_at(q);
    const float distance = scene_distance(q);
    // Depth of field: beyond twice the focus the picture softens (gathered from surfaces at least
    // as far, so nothing near bleeds out).
    if (p_dof > 0.0f && f_state[1] != 0.0f) {
        const float focus = max(f_state[0], 200.0f);
        const float coc = saturate((distance - focus * 2.0f) / (focus * 6.0f)) * p_dof;
        if (coc > 0.75f) {
            float3 sum = c;
            float weight = 1.0f;
            [unroll] for (uint i = 0u; i < 12u; ++i) {
                const float a = float(i) * 2.39996f;
                const float r = sqrt((float(i) + 0.5f) / 12.0f) * coc;
                const int2 s = q + int2(round(float2(cos(a), sin(a)) * r));
                if (scene_distance(s) >= distance * 0.8f) {
                    sum += scene_at(s);
                    weight += 1.0f;
                }
            }
            c = sum / weight;
        }
    }
    if (p_core.w > 0.0f) {
        c = star_core(position.xy, distance, c);
    }
    c += quarter_at(f_bloom, position.xy) * p_bloom;
    c += quarter_at(f_shafts, position.xy);
    c = grade(c, position.xy);
    return float4(c, 1.0f);
}
