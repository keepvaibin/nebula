// J's 3.4 conformance shader: bands across the sphere by height, still (speed 0) for the picture.
void surface(in SurfaceIn i, inout Surface s) {
    const float band = frac(i.world_position.y / 25.0 + i.time * speed);
    s.base = band < 0.5 ? tint.rgb : s.base;
    s.roughness = 0.3;
}
