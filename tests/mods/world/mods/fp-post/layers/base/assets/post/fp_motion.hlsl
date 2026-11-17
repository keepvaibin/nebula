// 2481: reads motion; with temporal upscaling off the frame has no motion vectors.
float4 post(PostIn i) {
    return float4(abs(motion(i.uv)) * 100.0, 0.0, 1.0);
}
