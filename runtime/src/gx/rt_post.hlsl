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
    uint p_axis;                // blur pass: 0 across, 1 down; finish: 2 false colour
    float p_rim;                // rim light at silhouettes (0: none)
    float p_haze;               // distance haze in the sky's colour: its largest share (0: none)
    float p_haze_distance;      // eye distance at which the haze reaches 63 % of that share
    float p_vignette;           // darkening towards the corners (0: none)
    float4 p_local;             // local exposure: contrast scale of regions brighter than mid grey, of
                                // darker ones, mid grey (0: none)
    uint4 p_lens_first;         // black holes' lens draws: first triangle in the glow triangles
    uint4 p_lens_count;         // their count | bending x 32 << 24 | view rings << 31 (0: none)
};

Texture2D<float4> p_color : register(t0);
Texture2D<float> p_depth_tex : register(t1);
StructuredBuffer<uint> p_in : register(t2);
RWStructuredBuffer<uint> p_out : register(u0);
RWStructuredBuffer<float> p_state : register(u1);   // focus distance, known; core radius, known;
                                                    // the sky's colour (rgb), known

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

// Local exposure: the luminance of the surfaces (not the sky or backdrops) over each quarter texel's
// block; the blur passes then spread it over the region.
[numthreads(8, 8, 1)]
void PostLumCS(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= p_quarter)) {
        return;
    }
    float sum = 0.0f;
    float n = 0.0f;
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        const int2 q = min(int2(id.xy * 4u + uint2(1u + (k & 1u) * 2u, 1u + (k >> 1u) * 2u)), int2(p_size) - 1);
        if (scene_distance(q) < 6.0e4f) {
            sum += luminance(p_color.Load(int3(q, 0)).rgb);
            n += 1.0f;
        }
    }
    const float l = n > 0.0f ? sum / n : p_local.z;
    p_out[id.y * p_quarter.x + id.x] = pack_r11g11b10(l.xxx);
}

// Black holes: one group per lens draw (the game's IndBend lens around each hole) over its eye-space
// triangles (p_in, 36 bytes each): the area-weighted centre and the farthest corner give its centre
// and size on screen; sixteen rays out from the centre over the picture find where the hole's dark
// horizon ends. p_state from 8, 8 floats per hole: centre (EFB pixels), lens radius, horizon
// radius (pixels), eye distance of the lens's front, of its centre, 0, 0.
groupshared float4 s_lens[64];

float3 lens_corner(uint t, uint k)
{
    const uint at = t * 9u + k * 3u;
    return asfloat(uint3(p_in[at], p_in[at + 1u], p_in[at + 2u]));
}

[numthreads(64, 1, 1)]
void PostLensCS(uint3 group : SV_GroupID, uint gi : SV_GroupIndex)
{
    const uint slot = group.x;
    const uint count = p_lens_count[slot] & 0xFFFFFFu;
    const uint first = p_lens_first[slot];
    float4 a = float4(0.0f, 0.0f, 0.0f, 0.0f);
    for (uint t = gi; t < count; t += 64u) {
        const float3 v0 = lens_corner(first + t, 0u);
        const float3 v1 = lens_corner(first + t, 1u);
        const float3 v2 = lens_corner(first + t, 2u);
        const float area = 0.5f * length(cross(v1 - v0, v2 - v0));
        a += float4((v0 + v1 + v2) * (area / 3.0f), area);
    }
    s_lens[gi] = a;
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint step = 32u; step > 0u; step >>= 1u) {
        if (gi < step) {
            s_lens[gi] += s_lens[gi + step];
        }
        GroupMemoryBarrierWithGroupSync();
    }
    const float4 sum = s_lens[0];
    const float3 centre = sum.w > 0.0f ? sum.xyz / sum.w : float3(0.0f, 0.0f, 0.0f);
    GroupMemoryBarrierWithGroupSync();
    float far2 = 0.0f;
    for (uint u = gi; u < count; u += 64u) {
        [unroll] for (uint k = 0u; k < 3u; ++k) {
            const float3 d = lens_corner(first + u, k) - centre;
            far2 = max(far2, dot(d, d));
        }
    }
    s_lens[gi] = float4(far2, 0.0f, 0.0f, 0.0f);
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint step2 = 32u; step2 > 0u; step2 >>= 1u) {
        if (gi < step2) {
            s_lens[gi].x = max(s_lens[gi].x, s_lens[gi + step2].x);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    const float radius = sqrt(s_lens[0].x);
    const bool seen = count != 0u && sum.w > 0.0f && centre.z < -1.0f - radius;
    const float2 c = seen ? eye_to_pixel(centre) : float2(0.0f, 0.0f);
    float px = seen ? radius * 0.5f * p_viewport.w * abs(p_projection.z) / max(-centre.z, 1.0f) : 0.0f;
    if (any(c < p_viewport.xy - px) || any(c > p_viewport.xy + p_viewport.zw + px)) {
        px = 0.0f;
    }
    // The horizon: along 16 rays from the centre, where the picture first turns brighter than near
    // black (the hole's disc); their mean, at least a fifth of the lens.
    GroupMemoryBarrierWithGroupSync();
    float edge = 0.0f;
    if (gi < 16u && px > 2.0f) {
        const float angle = float(gi) * (2.0f * kPi / 16.0f);
        const float2 dir = float2(cos(angle), sin(angle));
        edge = px;
        for (uint s = 1u; s <= 48u; ++s) {
            const float r = px * float(s) / 48.0f;
            const int2 q = int2(c + dir * r);
            if (any(q < 0) || any(q >= int2(p_size))) {
                edge = r;
                break;
            }
            if (luminance(p_color.Load(int3(q, 0)).rgb) > 0.06f) {
                edge = r;
                break;
            }
        }
    }
    s_lens[gi] = float4(edge, gi < 16u ? 1.0f : 0.0f, 0.0f, 0.0f);
    GroupMemoryBarrierWithGroupSync();
    [unroll] for (uint step3 = 32u; step3 > 0u; step3 >>= 1u) {
        if (gi < step3) {
            s_lens[gi].xy += s_lens[gi + step3].xy;
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (gi == 0u) {
        const float horizon = seen && px > 2.0f ? clamp(s_lens[0].x / max(s_lens[0].y, 1.0f), 0.2f * px, px) : 0.0f;
        const uint at = 8u + slot * 8u;
        p_state[at] = c.x;
        p_state[at + 1u] = c.y;
        p_state[at + 2u] = px > 2.0f ? px : 0.0f;
        p_state[at + 3u] = horizon;
        p_state[at + 4u] = seen ? -centre.z - radius : 0.0f;
        p_state[at + 5u] = seen ? -centre.z : 0.0f;
        p_state[at + 6u] = 0.0f;
        p_state[at + 7u] = 0.0f;
    }
}

// Light shafts: towards the core and the sun on screen, the bright picture between (in front of
// the light's distance) scatters along the way.
float3 shaft(float2 texel, float2 light, float strength, float near)
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
        // `near` > 0: only what lies within about that many texels of the light counts.
        const float2 off = at - light;
        const float close = near > 0.0f ? exp(-dot(off, off) / (near * near)) : 1.0f;
        sum += unpack_r11g11b10(p_in[uint(s.y) * p_quarter.x + uint(s.x)]) * (decay * close);
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
        c += shaft(texel, eye_to_pixel(p_core.xyz) / 4.0f, p_shafts * p_core_light.w, 0.0f);
    }
    if (p_sun.w > 0.0f && p_sun.z < -0.05f) {
        c += shaft(texel, eye_to_pixel(p_sun.xyz * 1.0e5f) / 4.0f, p_shafts * p_sun.w, 0.18f * float(p_quarter.y));
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
    // The sky's colour: the mean of a grid of points where the frame shows sky (far plane or
    // backdrops), over time.
    {
        float3 sky = float3(0.0f, 0.0f, 0.0f);
        float count = 0.0f;
        [loop] for (int y = 0; y < 8; ++y) {
            [loop] for (int x = 0; x < 12; ++x) {
                const int2 q = int2(p_viewport.xy + p_viewport.zw * float2((x + 0.5f) / 12.0f, (y + 0.5f) / 8.0f));
                if (scene_distance(q) >= 6.0e4f) {
                    sky += p_color.Load(int3(q, 0)).rgb;
                    count += 1.0f;
                }
            }
        }
        if (count >= 6.0f) {
            sky /= count;
            const bool known = p_state[7] != 0.0f;
            [unroll] for (uint k = 0u; k < 3u; ++k) {
                p_state[4u + k] = known ? lerp(p_state[4u + k], sky[k], 0.05f) : sky[k];
            }
            p_state[7] = 1.0f;
        }
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
StructuredBuffer<uint> f_lum : register(t6);       // the regions' surface luminance (PostLumCS, blurred)

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
            const float relief = clamp(1.0f + 1.4f * (l - around), 0.65f, 1.6f);
            const float limb = 0.7f + 0.3f * pow(mu, 0.6f);
            float3 hot = color * (1.0f + 0.12f * glow) * limb * relief;
            hot = lerp(hot * float3(1.0f, 0.82f, 0.66f), hot, smoothstep(0.0f, 0.6f, mu));
            hot += max(max(hot.r, max(hot.g, hot.b)) - 1.0f, 0.0f) * 0.2f;
            // Halfway between the game's own fire and the finished star: its texture stays.
            return lerp(color, hot, 0.55f);
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
        const float corona = exp(-above * 6.0f) * 0.22f;
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
        return color + heat * (0.35f + 0.35f * glow);
    }
    return color;
}

// Light near and above 1 (bloom, glows, the star core) rolls off softly to 1 instead of clipping
// flat, from 0.95 up only: the game's own highlights keep their value. What one channel loses
// spills into the others, as over-exposed light turns white.
float3 shoulder(float3 c)
{
    const float3 over = max(c - 0.95f, 0.0f);
    const float3 rolled = min(c, 0.95f) + 0.05f * (1.0f - exp(-over / 0.05f));
    const float spill = max(max(over.r, max(over.g, over.b)) - 0.05f, 0.0f);
    return saturate(rolled + spill * 0.15f);
}

float3 grade(float3 c, float2 pixel)
{
    // Filmic contrast on luminance (an S around mid grey that keeps black, white and the hue),
    // vibrance (dull colours gain more saturation than vivid ones), warm highlights against cool
    // shadows. The lighting's exposure measures through the same shoulder and S
    // (rt_lighting.hlsl finished_luminance).
    c = shoulder(max(c, 0.0f));
    const float l = luminance(c);
    const float lc = saturate(l + 0.85f * (l - 0.5f) * l * (1.0f - l));
    float3 g = c * (lc / max(l, 1e-4f));
    const float chroma = max(g.r, max(g.g, g.b)) - min(g.r, min(g.g, g.b));
    const float vibrance = 1.0f + 0.15f * (1.0f - smoothstep(0.0f, 0.5f, chroma));
    g = lc + (g - lc) * vibrance;
    const float3 shadows = float3(0.985f, 0.995f, 1.02f);
    const float3 highlights = float3(1.015f, 1.0f, 0.985f);
    g *= lerp(shadows, highlights, smoothstep(0.15f, 0.85f, lc));
    g = max(g, 0.0f);
    const float peak = max(g.r, max(g.g, g.b));
    if (peak > 1.0f) {
        g /= peak;
    }
    return lerp(c, g, p_grade);
}

// Black holes (PostLensCS): light passing near one bends towards it. Around the horizon the sky
// behind is seen from farther out, squeezed into a ring and stretched along it (a point mass's
// lens, its Einstein ring at 1.6 horizon radii), fading out by 3.2; a thin photon ring at the
// horizon's edge, and the horizon a dark gloss: a rim of the light around it on the side the sun
// stands. Surfaces in front of the hole are untouched.
float3 scene_bilinear(float2 at)
{
    const float2 a = at - 0.5f;
    const int2 b = int2(floor(a));
    const float2 f = a - float2(b);
    return lerp(lerp(scene_at(b), scene_at(b + int2(1, 0)), f.x),
                lerp(scene_at(b + int2(0, 1)), scene_at(b + int2(1, 1)), f.x), f.y);
}

float3 black_holes(int2 q, float distance, float3 c)
{
    float2 at = float2(q) + 0.5f;
    float3 add = float3(0.0f, 0.0f, 0.0f);
    bool bent = false;
    [unroll] for (uint i = 0u; i < 4u; ++i) {
        const uint word = p_lens_count[i];
        const uint base = 8u + i * 8u;
        if ((word & 0xFFFFFFu) == 0u || f_state[base + 2u] <= 2.0f) {
            continue;
        }
        const float2 centre = float2(f_state[base], f_state[base + 1u]);
        const float lens = f_state[base + 2u];
        const float horizon = max(f_state[base + 3u], 1.0f);
        const float front = f_state[base + 4u];
        const float strength = float((word >> 24u) & 0x7Fu) / 32.0f;
        const float2 d = float2(q) + 0.5f - centre;
        const float r = length(d);
        if ((word >> 31u) != 0u) {
            // GALAXY_RT_LENS_VIEW: the lens's size magenta, the horizon found cyan.
            if (abs(r - lens) < 1.5f) add += float3(1.0f, 0.0f, 1.0f);
            if (abs(r - horizon) < 1.5f) add += float3(0.0f, 1.0f, 1.0f);
        }
        const float reach = 3.2f * horizon;
        if (r >= reach || r < 0.5f || distance < front) {
            continue;
        }
        const float2 dir = d / r;
        const float einstein = 1.6f * horizon;
        const float fade = smoothstep(reach, 0.55f * reach, r) * smoothstep(1.08f * horizon, 1.35f * horizon, r);
        if (fade > 0.0f) {
            const float2 from = clamp(centre + dir * (r + strength * fade * einstein * einstein / r),
                                      p_viewport.xy + 1.0f, p_viewport.xy + p_viewport.zw - 2.0f);
            // Never a nearer surface pulled in (Mario, a planet before the hole).
            if (scene_distance(int2(from)) >= front) {
                at = from;
                bent = true;
            }
        }
        // The photon ring: light that circled the hole, a thin bright line just outside its horizon,
        // in the colour of the sky it gathers.
        const int2 around_at = int2(centre + dir * min(2.2f * horizon, reach));
        const float3 around = scene_distance(around_at) >= front ? scene_at(around_at) : float3(0.6f, 0.55f, 0.5f);
        const float ring = exp(-pow((r - 1.04f * horizon) / max(0.035f * horizon, 0.75f), 2.0f));
        add += ring * strength * 0.55f * lerp(around, float3(1.0f, 0.95f, 0.88f), 0.6f);
        // The horizon's gloss: a Fresnel rim inside its edge, brightest towards the sun on screen.
        if (r < horizon) {
            const float2 sun = p_sun.w > 0.0f && dot(p_sun.xy, p_sun.xy) > 1e-4f ? normalize(float2(p_sun.x, -p_sun.y))
                                                                                 : float2(-0.6f, -0.8f);
            const float rim = smoothstep(0.78f * horizon, horizon, r);
            const float facing = 0.25f + 0.75f * pow(saturate(0.5f + 0.5f * dot(dir, sun)), 3.0f);
            add += rim * rim * facing * strength * 0.35f * lerp(around, float3(1.0f, 1.0f, 1.0f), 0.3f);
        }
    }
    return (bent ? scene_bilinear(at) : c) + add;
}

float4 PostPS(float4 position : SV_Position) : SV_Target0
{
    const int2 q = int2(position.xy);
    if (position.x < p_split || any(position.xy < p_viewport.xy) || any(position.xy >= p_viewport.xy + p_viewport.zw)) {
        discard;
    }
    float3 c = scene_at(q);
    const float distance = scene_distance(q);
    if (p_lens_count.x != 0u) {
        c = black_holes(q, distance, c);
    }
    // Depth of field: beyond twice the focus the picture softens (gathered from surfaces at least
    // as far, so nothing near bleeds out).
    if (p_dof > 0.0f && f_state[1] != 0.0f) {
        const float focus = max(f_state[0], 200.0f);
        // Far scenery only, never the sky or backdrops (their stars and nebulae stay sharp).
        const float coc = distance < 6.0e4f ? saturate((distance - focus * 4.0f) / (focus * 12.0f)) * p_dof : 0.0f;
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
    // Rim light: a near surface's edge against something far behind it (an object against the sky
    // or distant scenery) catches light wrapping around it, in its own colour.
    if (p_rim > 0.0f && distance < 6.0e4f) {
        const float r = max(2.0f, p_viewport.w / 360.0f);
        float behind = 0.0f;
        const float2 around[4] = {float2(-1.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, -1.0f), float2(0.0f, 1.0f)};
        [unroll] for (uint k = 0u; k < 4u; ++k) {
            const float far = scene_distance(q + int2(round(around[k] * r)));
            behind = max(behind, saturate((far - distance) / (distance * 0.6f + 50.0f)));
        }
        c *= 1.0f + behind * p_rim;
    }
    // Distance haze (aerial perspective): far surfaces fade towards the sky's colour under a bright
    // sky; under a dark one (space) none.
    if (p_haze > 0.0f && distance < 6.0e4f && f_state[7] != 0.0f) {
        const float3 sky = float3(f_state[4], f_state[5], f_state[6]);
        const float amount = p_haze * smoothstep(0.12f, 0.45f, luminance(sky)) *
                             (1.0f - exp(-distance / max(p_haze_distance, 1.0f)));
        c = lerp(c, sky, amount);
    }
    // Local exposure: each region's brightness compressed around mid grey (bright sunlit ground down,
    // deep shade a little up), its detail kept: strong sunlight without washing out (Unreal's local
    // exposure works the same way).
    if (p_local.z > 0.0f && distance < 6.0e4f) {
        const float region = max(quarter_at(f_lum, position.xy).r, 1e-3f);
        const float scale = region > p_local.z ? p_local.x : p_local.y;
        c *= clamp(pow(region / p_local.z, scale - 1.0f), 0.55f, 1.5f);
    }
    c += quarter_at(f_bloom, position.xy) * p_bloom;
    c += quarter_at(f_shafts, position.xy);
    // The sky and backdrops keep the game's own colours: the lighting never touched them, and a grade
    // there only shifted them (measured: Sea Slide's clouds turned warm).
    if (distance < 6.0e4f) {
        c = grade(c, position.xy);
    }
    if (p_vignette > 0.0f) {
        const float2 at = (position.xy - p_viewport.xy) / p_viewport.zw * 2.0f - 1.0f;
        const float bright = smoothstep(0.55f, 0.95f, luminance(c));
        c *= 1.0f - p_vignette * smoothstep(0.4f, 1.6f, dot(at, at)) * (1.0f - 0.8f * bright);
    }
    if (p_axis == 2u) {
        // GALAXY_RT_POST_VIEW: the finished picture's luminance in false colour: deep blue near
        // black, blue, green at mid grey, yellow, orange, red near white, magenta clipped.
        const float l = luminance(c);
        const float3 bands[6] = {float3(0.05f, 0.05f, 0.35f), float3(0.1f, 0.35f, 1.0f), float3(0.1f, 0.8f, 0.2f),
                                 float3(0.95f, 0.9f, 0.1f), float3(1.0f, 0.5f, 0.05f), float3(0.95f, 0.1f, 0.1f)};
        const float t = saturate(l) * 5.0f;
        const uint i = min(uint(t), 4u);
        c = lerp(bands[i], bands[i + 1u], t - float(i));
        if (l >= 0.985f) {
            c = float3(1.0f, 0.0f, 1.0f);
        }
    }
    return float4(c, 1.0f);
}
