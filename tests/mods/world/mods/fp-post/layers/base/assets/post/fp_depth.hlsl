// 2481: grey by the distance from the eye, white at 20000 units and past.
float4 post(PostIn i) {
    return float4(saturate(depth(i.uv) / 20000.0).xxx, 1.0);
}
