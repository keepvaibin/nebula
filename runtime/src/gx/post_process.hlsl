// Presentation post-processing passes (post_process.cpp).
//
// post_process.cpp composes one source per pass from this file and the
// vendored libraries, defining exactly one PASS_* macro:
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
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)
#endif

#if defined(PASS_FSR_EASU) || defined(PASS_FSR_RCAS)
#define A_GPU 1
#define A_HLSL 1
#endif

#elif POST_SECTION == 2

Texture2D inputTex : register(t0);

#if defined(PASS_FSR_EASU)
AF4 FsrEasuRF(AF2 p) { return inputTex.GatherRed(LinearSampler, p, int2(0, 0)); }
AF4 FsrEasuGF(AF2 p) { return inputTex.GatherGreen(LinearSampler, p, int2(0, 0)); }
AF4 FsrEasuBF(AF2 p) { return inputTex.GatherBlue(LinearSampler, p, int2(0, 0)); }
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

#if defined(PASS_FSR_RCAS)
FullscreenOut VSMain(uint id : SV_VertexID) { return FullscreenTriangle(id); }
float4 PSMain(FullscreenOut i) : SV_Target {
    AF3 color;
    FsrRcasF(color.r, color.g, color.b, AU2(ViewportPixel(i.pos)), g_con0);
    return float4(color, 1.0);
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
