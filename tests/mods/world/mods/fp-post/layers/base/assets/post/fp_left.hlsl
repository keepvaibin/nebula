// 2481: the frame's left half blue, the right half as drawn.
float4 post(PostIn i) {
    return i.uv.x < 0.5 ? float4(0.0, 0.0, 1.0, 1.0) : scene(i.uv);
}
