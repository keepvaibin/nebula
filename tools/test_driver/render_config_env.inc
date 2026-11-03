// SPDX-License-Identifier: GPL-3.0-only
// render_config.cpp in a test build: a test sets settings from its environment.
static void apply_env(RenderConfig& cfg, const char* env_name, const char* key) {
    char value[128]{};
    std::size_t length = 0;
    if (::galaxy::env_read(&length, value, sizeof(value), env_name) == 0 &&
        length > 1) {
        apply_kv(cfg, key, value);
    }
}

static void apply_env_overrides(RenderConfig& cfg) {
    apply_env(cfg, "GALAXY_VSYNC_INTERVAL", "vsync_interval");
    apply_env(cfg, "GALAXY_MSAA_SAMPLES", "msaa_samples");
    apply_env(cfg, "GALAXY_TAA_ENABLED", "taa_enabled");
    apply_env(cfg, "GALAXY_MAX_FPS", "max_fps");
    apply_env(cfg, "GALAXY_FRAME_RATE_DECOUPLE", "frame_rate_decouple");
    apply_env(cfg, "GALAXY_TEXTURE_LOD_BIAS", "texture_lod_bias");
    apply_env(cfg, "GALAXY_ANISOTROPIC_FILTERING", "anisotropic_filtering");
    apply_env(cfg, "GALAXY_ENHANCED_MIPMAPS", "enhanced_mipmaps");
    apply_env(cfg, "GALAXY_PER_PIXEL_LIGHTING", "per_pixel_lighting");
    apply_env(cfg, "GALAXY_CURVED_SURFACES", "curved_surfaces");
    apply_env(cfg, "GALAXY_ENHANCED_LIGHTING", "enhanced_lighting");
    apply_env(cfg, "GALAXY_AMBIENT_OCCLUSION", "ambient_occlusion");
    apply_env(cfg, "GALAXY_BLOOM", "bloom");
    apply_env(cfg, "GALAXY_POST_PROCESS", "post_process");
    apply_env(cfg, "GALAXY_HUD_OUTPUT_RESOLUTION", "hud_output_resolution");
    apply_env(cfg, "GALAXY_AA", "aa");
    apply_env(cfg, "GALAXY_SMAA_QUALITY", "smaa_quality");
    apply_env(cfg, "GALAXY_UPSCALER", "upscaler");
    apply_env(cfg, "GALAXY_FRAME_GENERATION", "frame_generation");
    apply_env(cfg, "GALAXY_TEMPORAL_QUALITY", "temporal_quality");
    apply_env(cfg, "GALAXY_SPATIAL_QUALITY", "spatial_quality");
    apply_env(cfg, "GALAXY_SHARPNESS", "sharpness");
    apply_env(cfg, "GALAXY_RAY_TRACING", "ray_tracing_enabled");
    apply_env(cfg, "GALAXY_RAY_TRACING_ENABLED", "ray_tracing_enabled");
    apply_env(cfg, "GALAXY_RT_SHADOW_QUALITY", "rt_shadow_quality");
    apply_env(cfg, "GALAXY_RT_QUALITY", "rt_quality");
    apply_env(cfg, "GALAXY_RT_LENS_BLOOM", "rt_lens_bloom");
    apply_env(cfg, "GALAXY_RT_SUN_RAYS", "rt_sun_rays");
    apply_env(cfg, "GALAXY_RT_WATER", "rt_water");
    apply_env(cfg, "GALAXY_RT_AO_ENABLED", "rt_ao_enabled");
    apply_env(cfg, "GALAXY_RT_REFLECTIONS", "rt_reflections");
    apply_env(cfg, "GALAXY_TEXTURE_PACKS", "texture_packs");
    apply_env(cfg, "GALAXY_TEXTURE_PACK_PRELOAD", "texture_pack_preload");
    apply_env(cfg, "GALAXY_TEXTURE_DUMP", "texture_dump");
    apply_env(cfg, "GALAXY_TEXTURE_PACK_BUDGET_MB", "texture_pack_budget_mb");
    apply_env(cfg, "GALAXY_WINDOW_WIDTH", "window_width");
    apply_env(cfg, "GALAXY_WINDOW_HEIGHT", "window_height");
    apply_env(cfg, "GALAXY_DISPLAY_MODE", "display_mode");
    apply_env(cfg, "GALAXY_MONITOR", "monitor");
    apply_env(cfg, "GALAXY_GPU", "gpu");
    apply_env(cfg, "GALAXY_ASPECT", "aspect");
}
