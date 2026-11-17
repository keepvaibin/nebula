// 2481: the device's compiler refuses this one (a name nothing defines).
float4 post(PostIn i) {
    return fp_nothing_named_this;
}
