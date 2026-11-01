// SPDX-License-Identifier: GPL-3.0-only
// Lines and points for every native material: the geometry stage that widens each line into a quad and each point
// into a square of the drawing state's width (GX's line and point sizes: line_point_raster.z the half line width,
// .w the half point size, in clip units of the viewport's pixels xy), with the texture coordinates of the far edge
// moved by the drawing state's offset (line_point_tex_offsets: the coordinates' mask in x (lines) and y (points), the
// divisor in z (lines) and w (points)). Compiled once per output layout (with or without motion vectors) and paired
// with any native material's vertex and pixel shaders (pipeline_cache get_native), as native_curved.hlsl's patches.
struct MaterialConstants {
    float4 unused;
};

#include "native_material.hlsli"

NativeVaryings native_varyings(NativeVsOut v) {
    NativeVaryings o;
    o.pos = v.pos;
    o.color = v.color;
    o.color1 = v.color1;
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        o.uv[i] = v.uv[i];
    }
#if NATIVE_MOTION
    o.mv_cur = v.mv_cur;
    o.mv_prev = v.mv_prev;
#endif
    return o;
}

// The texture coordinates `mask` names moved by 1 / divisor in x (and in y when `both`).
void native_texcoord_offset(inout NativeVaryings v, uint mask, uint divisor, bool right, bool bottom) {
    if (divisor == 0u) {
        return;
    }
    const float offset = 1.0 / float(divisor);
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        if ((mask & (1u << i)) != 0u) {
            v.uv[i].x += right ? offset : 0.0;
            v.uv[i].y += bottom ? offset : 0.0;
        }
    }
}

// A line: a quad across the line's major axis (a steep line widens across x, a flat one across y), the second edge's
// texture coordinates moved.
[maxvertexcount(4)]
void native_gs_line(line NativeVsOut input[2], inout TriangleStream<NativeVaryings> output) {
    const NativeVaryings start = native_varyings(input[0]);
    const NativeVaryings end = native_varyings(input[1]);
    const float2 delta = abs(end.pos.xy / end.pos.w - start.pos.xy / start.pos.w);
    const bool tall = line_point_raster.y * delta.y > line_point_raster.x * delta.x;
    const float2 offset = tall ? float2(line_point_raster.z / line_point_raster.x, 0.0)
                               : float2(0.0, -line_point_raster.z / line_point_raster.y);
    NativeVaryings v = start;
    v.pos.xy -= offset * v.pos.w;
    output.Append(v);
    v = start;
    v.pos.xy += offset * v.pos.w;
    native_texcoord_offset(v, line_point_tex_offsets.x, line_point_tex_offsets.z, true, false);
    output.Append(v);
    v = end;
    v.pos.xy -= offset * v.pos.w;
    output.Append(v);
    v = end;
    v.pos.xy += offset * v.pos.w;
    native_texcoord_offset(v, line_point_tex_offsets.x, line_point_tex_offsets.z, true, false);
    output.Append(v);
    output.RestartStrip();
}

void native_point_corner(NativeVaryings centre, float2 signs, bool right, bool bottom,
                         inout TriangleStream<NativeVaryings> output) {
    centre.pos.xy += signs * line_point_raster.ww / line_point_raster.xy * centre.pos.w;
    native_texcoord_offset(centre, line_point_tex_offsets.y, line_point_tex_offsets.w, right, bottom);
    output.Append(centre);
}

// A point: a square about it, the right and bottom corners' texture coordinates moved.
[maxvertexcount(4)]
void native_gs_point(point NativeVsOut input[1], inout TriangleStream<NativeVaryings> output) {
    const NativeVaryings centre = native_varyings(input[0]);
    native_point_corner(centre, float2(-1.0, 1.0), false, false, output);
    native_point_corner(centre, float2(1.0, 1.0), true, false, output);
    native_point_corner(centre, float2(-1.0, -1.0), false, true, output);
    native_point_corner(centre, float2(1.0, -1.0), true, true, output);
    output.RestartStrip();
}
