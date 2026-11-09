// The renderer's utility shaders: name, source, target, entry. tools/dev/utility_shaders.cpp compiles them at build.
X(BlitVs, blit_vertex_shader_source(), "vs_5_1", "main")
X(BlitPs, blit_pixel_shader_source(), "ps_5_1", "main")
X(ClearPs, clear_pixel_shader_source(), "ps_5_1", "main")
X(DepthResolvePs, depth_resolve_pixel_shader_source(), "ps_5_1", "main")
X(SceneConversionPs, scene_conversion_shader_source(), "ps_5_1", "main")
X(DisplayFilterPs, display_filter_shader_source(), "ps_5_1", "main")
X(ScreenProbeCs, screen_probe_shader_source(), "cs_5_1", "main")
