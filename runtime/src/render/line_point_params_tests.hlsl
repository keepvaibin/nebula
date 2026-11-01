// SPDX-License-Identifier: GPL-3.0-only
// line_point_params_tests' own vertex and pixel stages, compiled at build as the native stages are (DXIL: they share a
// pipeline with native_lines.hlsl's geometry stages). The vertex stage passes clip-space positions in the native vertex
// stage's output layout (native_material.hlsli NativeVsOut, without motion vectors); the pixel stage writes texture
// coordinate 0 as red and green, so a test can see which edges the geometry stage moved.

struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    uint color0 : COLOR0;
    uint color1 : COLOR1;
    float2 uv[8] : TEXCOORD;
    uint4 mtx_idx : BLENDINDICES;
};

struct VSOut {
    float4 pos : SV_Position;
    float4 col0 : COLOR0;
    float4 col1 : COLOR1;
    float4 uv[8] : TEXCOORD;
    float4 curve_pos : TEXCOORD14;
    float4 curve_nrm : TEXCOORD15;
    uint4 curve_data : TEXCOORD16;
};

VSOut test_vs(VSIn input) {
    VSOut output = (VSOut)0;
    output.pos = float4(input.pos, 1.0);
    output.col0 = float4(1.0, 0.0, 0.0, 1.0);
    return output;
}

struct PSIn {
    float4 pos : SV_Position;
    float4 col0 : COLOR0;
    float4 col1 : COLOR1;
    float4 uv[8] : TEXCOORD;
};

float4 test_ps(PSIn input) : SV_Target {
    return float4(input.uv[0].xy, 0.0, 1.0);
}
