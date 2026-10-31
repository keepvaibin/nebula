// Lines and points as quads (the game's widths at the target scale; D3D12 rasterizes one-pixel lines and points only):
// geometry stages compiled when Nebula is built (material_shaders.h), MAT_POINTS 0 lines, 1 points.

cbuffer LinePointConstants : register(b0) {
    float4 line_point_raster : packoffset(c65);
    uint4  line_point_tex_offsets : packoffset(c66);
};
struct VSOut {
    float4 pos : SV_Position;
    float4 col0 : COLOR0;
    float4 col1 : COLOR1;
    float3 uv[8] : TEXCOORD;
};
#if MAT_POINTS
void emit_point_vertex(
    VSOut center,
    float2 signs,
    bool is_right,
    bool is_bottom,
    inout TriangleStream<VSOut> output) {
    float2 offset = signs * line_point_raster.ww /
        line_point_raster.xy;
    center.pos.xy += offset * center.pos.w;
    uint divisor = line_point_tex_offsets.w;
    if (divisor != 0u) {
        float tex_offset = 1.0 / float(divisor);
        uint mask = line_point_tex_offsets.y;
        [unroll] for (uint i = 0u; i < 8u; ++i) {
            if ((mask & (1u << i)) != 0u) {
                center.uv[i].xy += float2(
                    is_right ? tex_offset : 0.0,
                    is_bottom ? tex_offset : 0.0);
            }
        }
    }
    output.Append(center);
}
[maxvertexcount(4)]
void main(point VSOut input_vertices[1], inout TriangleStream<VSOut> output) {
    VSOut center = input_vertices[0];
    emit_point_vertex(center, float2(-1.0,  1.0), false, false, output);
    emit_point_vertex(center, float2( 1.0,  1.0), true,  false, output);
    emit_point_vertex(center, float2(-1.0, -1.0), false, true,  output);
    emit_point_vertex(center, float2( 1.0, -1.0), true,  true,  output);
    output.RestartStrip();
}
#else
void apply_line_texcoord_offset(inout VSOut vertex) {
    uint mask = line_point_tex_offsets.x;
    uint divisor = line_point_tex_offsets.z;
    if (divisor == 0u) {
        return;
    }
    float tex_offset = 1.0 / float(divisor);
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        if ((mask & (1u << i)) != 0u) {
            vertex.uv[i].x += tex_offset;
        }
    }
}
[maxvertexcount(4)]
void main(line VSOut input_vertices[2], inout TriangleStream<VSOut> output) {
    VSOut start = input_vertices[0];
    VSOut end = input_vertices[1];
    float2 delta = abs(end.pos.xy / end.pos.w - start.pos.xy / start.pos.w);
    bool is_tall = line_point_raster.y * delta.y >
        line_point_raster.x * delta.x;
    float2 offset = is_tall
        ? float2(line_point_raster.z / line_point_raster.x, 0.0)
        : float2(0.0, -line_point_raster.z / line_point_raster.y);

    VSOut vertex = start;
    vertex.pos.xy -= offset * vertex.pos.w;
    output.Append(vertex);
    vertex = start;
    vertex.pos.xy += offset * vertex.pos.w;
    apply_line_texcoord_offset(vertex);
    output.Append(vertex);
    vertex = end;
    vertex.pos.xy -= offset * vertex.pos.w;
    output.Append(vertex);
    vertex = end;
    vertex.pos.xy += offset * vertex.pos.w;
    apply_line_texcoord_offset(vertex);
    output.Append(vertex);
    output.RestartStrip();
}
#endif
