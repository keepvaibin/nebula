// Presentation post-processing passes (post_process.cpp, vk/post_process_vk.cpp).
//
// The build (for fxc) and vk/post_process_vk.cpp compose one source per pass
// from this file and the vendored libraries, defining exactly one PASS_* macro:
//   POST_SECTION 1        common declarations
//   SMAA.hlsl             (SMAA passes)  or  ffx_a.h, POST_SECTION 2, ffx_fsr1.h (FSR passes)
//   POST_SECTION 3        entry points
//
// Every pass draws one full-target triangle. Pixel positions are relative to
// the target viewport's origin, which present() places inside the letterboxed
// content rectangle of the back buffer.

#if POST_SECTION == 1

cbuffer PostConstants : register(b0) {
    // 1/w, 1/h, w, h of the pass input (SMAA_RT_METRICS).
    float4 g_rt_metrics;
    // FSR 1 constants (FsrEasuCon / FsrRcasCon).
    uint4 g_con0;
    uint4 g_con1;
    uint4 g_con2;
    uint4 g_con3;
    // Resample: xy source size, zw target size in pixels.
    float4 g_params;
    // xy: viewport origin in render-target pixels.
    int4 g_origin;
};

SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);

struct FullscreenOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

FullscreenOut FullscreenTriangle(uint id) {
    FullscreenOut o;
    o.uv = float2(id == 1u ? 2.0 : 0.0, id == 2u ? 2.0 : 0.0);
    o.pos = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0);
    return o;
}

// Integer pixel inside the pass viewport.
int2 ViewportPixel(float4 pos) {
    return int2(pos.xy) - g_origin.xy;
}

#if defined(PASS_SMAA_EDGES) || defined(PASS_SMAA_WEIGHTS) || defined(PASS_SMAA_BLEND)
#define SMAA_CUSTOM_SL 1
#define SMAA_RT_METRICS g_rt_metrics
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(LinearSampler, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(PointSampler, coord)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(LinearSampler, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord)
#endif

#if defined(PASS_FSR_EASU) || defined(PASS_FSR_RCAS) || defined(PASS_FSR_RCAS_SCALED)
#define A_GPU 1
#define A_HLSL 1
#endif

#elif POST_SECTION == 2

Texture2D inputTex : register(t0);

#if defined(PASS_FSR_EASU)
AF4 FsrEasuRF(AF2 p) { return inputTex.GatherRed(LinearSampler, p); }
AF4 FsrEasuGF(AF2 p) { return inputTex.GatherGreen(LinearSampler, p); }
AF4 FsrEasuBF(AF2 p) { return inputTex.GatherBlue(LinearSampler, p); }
#define FSR_EASU_F 1
#endif

#if defined(PASS_FSR_RCAS)
// The input matches the target size; reads past the edge repeat the border.
AF4 FsrRcasLoadF(ASU2 p) {
    const int2 last = int2(g_params.xy) - 1;
    return inputTex.Load(int3(clamp(int2(p), int2(0, 0), last), 0));
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#define FSR_RCAS_F 1
#endif

#if defined(PASS_FSR_RCAS_SCALED)
// Enlarging and sharpening in one pass: each RCAS tap is the bilinear source sample at that
// target pixel's centre, the value the resample pass would have stored there.
AF4 FsrRcasLoadF(ASU2 p) {
    const int2 last = int2(g_params.zw) - 1;
    const float2 uv = (float2(clamp(int2(p), int2(0, 0), last)) + 0.5) / g_params.zw;
    return inputTex.SampleLevel(LinearSampler, uv, 0);
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
#define FSR_RCAS_F 1
#endif

#elif POST_SECTION == 3

#if defined(PASS_SMAA_EDGES)
Texture2D colorTex : register(t0);
struct EdgesOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 offset[3] : TEXCOORD1;
};
EdgesOut VSMain(uint id : SV_VertexID) {
    const FullscreenOut f = FullscreenTriangle(id);
    EdgesOut o;
    o.pos = f.pos;
    o.uv = f.uv;
    SMAAEdgeDetectionVS(f.uv, o.offset);
    return o;
}
float2 PSMain(EdgesOut i) : SV_Target {
    return SMAALumaEdgeDetectionPS(i.uv, i.offset, colorTex);
}
#endif

#if defined(PASS_SMAA_WEIGHTS)
Texture2D edgesTex : register(t0);
Texture2D areaTex : register(t1);
Texture2D searchTex : register(t2);
struct WeightsOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float2 pixcoord : TEXCOORD1;
    float4 offset[3] : TEXCOORD2;
};
WeightsOut VSMain(uint id : SV_VertexID) {
    const FullscreenOut f = FullscreenTriangle(id);
    WeightsOut o;
    o.pos = f.pos;
    o.uv = f.uv;
    SMAABlendingWeightCalculationVS(f.uv, o.pixcoord, o.offset);
    return o;
}
float4 PSMain(WeightsOut i) : SV_Target {
    return SMAABlendingWeightCalculationPS(
        i.uv, i.pixcoord, i.offset, edgesTex, areaTex, searchTex, float4(0.0, 0.0, 0.0, 0.0));
}
#endif

#if defined(PASS_SMAA_BLEND)
Texture2D colorTex : register(t0);
Texture2D blendTex : register(t1);
struct BlendOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 offset : TEXCOORD1;
};
BlendOut VSMain(uint id : SV_VertexID) {
    const FullscreenOut f = FullscreenTriangle(id);
    BlendOut o;
    o.pos = f.pos;
    o.uv = f.uv;
    SMAANeighborhoodBlendingVS(f.uv, o.offset);
    return o;
}
float4 PSMain(BlendOut i) : SV_Target {
    return float4(SMAANeighborhoodBlendingPS(i.uv, i.offset, colorTex, blendTex).rgb, 1.0);
}
#endif

#if defined(PASS_FSR_EASU)
FullscreenOut VSMain(uint id : SV_VertexID) { return FullscreenTriangle(id); }
float4 PSMain(FullscreenOut i) : SV_Target {
    AF3 color;
    FsrEasuF(color, AU2(ViewportPixel(i.pos)), g_con0, g_con1, g_con2, g_con3);
    return float4(color, 1.0);
}
#endif

#if defined(PASS_FSR_RCAS) || defined(PASS_FSR_RCAS_SCALED)
FullscreenOut VSMain(uint id : SV_VertexID) { return FullscreenTriangle(id); }
float4 PSMain(FullscreenOut i) : SV_Target {
    AF3 color;
    FsrRcasF(color.r, color.g, color.b, AU2(ViewportPixel(i.pos)), g_con0);
    return float4(color, 1.0);
}
#endif

#if defined(PASS_FXAA)
// Fast approximate AA (the FXAA quality-preset method, written for Nebula):
// find the local luma edge, walk along it in both directions to its ends,
// and resample the pixel across the edge by its position along the span;
// a 3x3 contrast term softens single-pixel features. Needs no lookup
// textures, so it is the fallback when SMAA is unavailable.
Texture2D inputTex : register(t0);
float FxaaLuma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float FxaaLumaAt(float2 uv) { return FxaaLuma(inputTex.SampleLevel(LinearSampler, uv, 0).rgb); }
float FxaaLumaOff(float2 uv, int2 o) { return FxaaLuma(inputTex.SampleLevel(PointSampler, uv, 0, o).rgb); }
FullscreenOut VSMain(uint id : SV_VertexID) { return FullscreenTriangle(id); }
float4 PSMain(FullscreenOut i) : SV_Target {
    const float2 texel = g_rt_metrics.xy;
    const float2 uv = (float2(ViewportPixel(i.pos)) + 0.5) * texel;
    const float3 center = inputTex.SampleLevel(PointSampler, uv, 0).rgb;
    const float lc = FxaaLuma(center);
    const float ln = FxaaLumaOff(uv, int2(0, -1));
    const float ls = FxaaLumaOff(uv, int2(0, 1));
    const float lw = FxaaLumaOff(uv, int2(-1, 0));
    const float le = FxaaLumaOff(uv, int2(1, 0));
    const float lmax = max(lc, max(max(ln, ls), max(lw, le)));
    const float lmin = min(lc, min(min(ln, ls), min(lw, le)));
    const float range = lmax - lmin;
    if (range < max(0.0312, lmax * 0.125)) return float4(center, 1.0);
    const float lnw = FxaaLumaOff(uv, int2(-1, -1));
    const float lne = FxaaLumaOff(uv, int2(1, -1));
    const float lsw = FxaaLumaOff(uv, int2(-1, 1));
    const float lse = FxaaLumaOff(uv, int2(1, 1));
    // Edge orientation from second differences of the 3x3 block.
    const float horizontal = abs(lnw + lne - 2.0 * ln) + 2.0 * abs(lw + le - 2.0 * lc) + abs(lsw + lse - 2.0 * ls);
    const float vertical = abs(lnw + lsw - 2.0 * lw) + 2.0 * abs(ln + ls - 2.0 * lc) + abs(lne + lse - 2.0 * le);
    const bool horz = horizontal >= vertical;
    // The neighbour across the edge with the larger contrast.
    float l1 = horz ? ln : lw;
    float l2 = horz ? ls : le;
    const float g1 = abs(l1 - lc);
    const float g2 = abs(l2 - lc);
    const bool negative = g1 >= g2;
    float across = horz ? texel.y : texel.x;
    float lpair;
    if (negative) { across = -across; lpair = l1; } else { lpair = l2; }
    const float gradient = 0.25 * max(g1, g2);
    const float lavg = 0.5 * (lpair + lc);
    // Walk along the edge, half a pixel over towards the chosen neighbour.
    float2 start = uv;
    if (horz) start.y += 0.5 * across; else start.x += 0.5 * across;
    const float2 along = horz ? float2(texel.x, 0.0) : float2(0.0, texel.y);
    static const float kSteps[10] = {1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 4.0, 8.0, 8.0};
    float2 pn = start - along;
    float2 pp = start + along;
    float en = FxaaLumaAt(pn) - lavg;
    float ep = FxaaLumaAt(pp) - lavg;
    bool dn = abs(en) >= gradient;
    bool dp = abs(ep) >= gradient;
    [loop] for (int s = 1; s < 10 && !(dn && dp); ++s) {
        if (!dn) { pn -= along * kSteps[s]; en = FxaaLumaAt(pn) - lavg; dn = abs(en) >= gradient; }
        if (!dp) { pp += along * kSteps[s]; ep = FxaaLumaAt(pp) - lavg; dp = abs(ep) >= gradient; }
    }
    const float distn = horz ? uv.x - pn.x : uv.y - pn.y;
    const float distp = horz ? pp.x - uv.x : pp.y - uv.y;
    const bool nearer_negative = distn < distp;
    const float dist = min(distn, distp);
    const float span = distn + distp;
    // Blend only when the nearer end's luma moves away from the centre's side.
    const bool centre_below = lc - lavg < 0.0;
    const bool correct = ((nearer_negative ? en : ep) < 0.0) != centre_below;
    float offset = correct ? 0.5 - dist / span : 0.0;
    // Sub-pixel term from the 3x3 average.
    const float l3x3 = (2.0 * (ln + ls + lw + le) + lnw + lne + lsw + lse) / 12.0;
    const float sub = saturate(abs(l3x3 - lc) / range);
    const float sub2 = (-2.0 * sub + 3.0) * sub * sub;
    offset = max(offset, sub2 * sub2 * 0.75);
    float2 final_uv = uv;
    if (horz) final_uv.y += offset * across; else final_uv.x += offset * across;
    return float4(inputTex.SampleLevel(LinearSampler, final_uv, 0).rgb, 1.0);
}
#endif

#if defined(PASS_DEBAND)
// Debanding (after libplacebo's deband by Niklas Haas, from flash3kyuu_deband). The picture is 8-bit: a smooth
// gradient (a dark sky, fog, a glow, vertex lighting) is a staircase of one-level steps, wide at a raised resolution.
// A pixel within the threshold (g_params.y) of the mean of four taps around it, at a random angle and distance up to
// g_params.x pixels, lies on such a staircase and takes the mean; an edge or texture differs by more and stays. A
// second ring twice as far with half the threshold, where the first one smoothed. A pixel more than two levels from
// one of its four neighbours is an edge or texture already, and stays without the far taps. The four pixels of a 2x2
// quad share their taps' angle and distance (their reads fall in the same cache lines). Where that leaves a value
// between two levels, a triangular dither of one level (Wannamaker 1992) keeps the 8-bit target from stepping it
// again; whole levels (flat colour, black, the HUD) stay exact.
Texture2D inputTex : register(t0);
FullscreenOut VSMain(uint id : SV_VertexID) { return FullscreenTriangle(id); }
// PCG hash (Jarzynski and Olano 2020).
uint DebandHash(uint v) {
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float DebandRandom(inout uint seed) {
    seed = DebandHash(seed);
    return float(seed >> 8u) * (1.0 / 16777216.0);
}
float4 PSMain(FullscreenOut i) : SV_Target {
    const int2 p = ViewportPixel(i.pos);
    const float2 texel = g_rt_metrics.xy;
    const float2 uv = (float2(p) + 0.5) * texel;
    float3 color = inputTex.SampleLevel(PointSampler, uv, 0).rgb;
    const float3 near = max(max(abs(inputTex.SampleLevel(PointSampler, uv, 0, int2(1, 0)).rgb - color),
                                abs(inputTex.SampleLevel(PointSampler, uv, 0, int2(-1, 0)).rgb - color)),
                            max(abs(inputTex.SampleLevel(PointSampler, uv, 0, int2(0, 1)).rgb - color),
                                abs(inputTex.SampleLevel(PointSampler, uv, 0, int2(0, -1)).rgb - color)));
    if (max(near.r, max(near.g, near.b)) > 2.5 / 255.0) {
        return float4(color, 1.0);
    }
    uint seed = DebandHash(uint(p.x >> 1) ^ DebandHash(uint(p.y >> 1)));
    [loop] for (uint ring = 1u; ring <= 2u; ++ring) {
        const float distance = DebandRandom(seed) * g_params.x * float(ring);
        const float angle = DebandRandom(seed) * 6.28318530718;
        const float2 o = float2(cos(angle), sin(angle)) * distance * texel;
        const float3 mean = 0.25 * (inputTex.SampleLevel(LinearSampler, uv + o, 0).rgb +
                                    inputTex.SampleLevel(LinearSampler, uv - o, 0).rgb +
                                    inputTex.SampleLevel(LinearSampler, uv + float2(-o.y, o.x), 0).rgb +
                                    inputTex.SampleLevel(LinearSampler, uv + float2(o.y, -o.x), 0).rgb);
        const float3 keep = step(g_params.y / float(ring), abs(color - mean));
        color = lerp(mean, color, keep);
        if (min(keep.r, min(keep.g, keep.b)) > 0.5) {
            break;
        }
    }
    seed = DebandHash(uint(p.x) ^ DebandHash(uint(p.y) + 0x9E3779B9u));
    const float3 level = color * 255.0;
    const float3 between = step(0.02, abs(level - round(level)));
    const float3 noise = float3(DebandRandom(seed) + DebandRandom(seed), DebandRandom(seed) + DebandRandom(seed),
                                DebandRandom(seed) + DebandRandom(seed)) - 1.0;
    return float4(saturate((level + noise * between) / 255.0), 1.0);
}
#endif

#if defined(PASS_RESAMPLE)
// Box filter over each target pixel's source footprint: one bilinear tap per
// axis when enlarging, up to eight evenly spaced bilinear taps per axis when
// reducing, so a supersampled image averages every source pixel instead of
// skipping rows and columns.
Texture2D inputTex : register(t0);
FullscreenOut VSMain(uint id : SV_VertexID) { return FullscreenTriangle(id); }
float4 PSMain(FullscreenOut i) : SV_Target {
    const float2 source = g_params.xy;
    const float2 target = g_params.zw;
    const float2 scale = source / target;
    const int2 taps = int2(clamp(ceil(scale - 0.001), 1.0, 8.0));
    const float2 start = float2(ViewportPixel(i.pos)) * scale;
    const float2 step = scale / float2(taps);
    float3 sum = 0.0;
    [loop] for (int y = 0; y < taps.y; ++y) {
        [loop] for (int x = 0; x < taps.x; ++x) {
            const float2 p = start + (float2(x, y) + 0.5) * step;
            sum += inputTex.SampleLevel(LinearSampler, p / source, 0).rgb;
        }
    }
    return float4(sum / float(taps.x * taps.y), 1.0);
}
#endif

#endif
