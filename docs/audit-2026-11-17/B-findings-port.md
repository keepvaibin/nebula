# Nebula engine audit (port, not modding)

Tree: D:\NebulaWork\MarioGalaxyRecomp\Nebula-Clean (clean/work, working tree as found, 79 files modified + 1 untracked).
All paths below are relative to that tree unless absolute. Line numbers are of the working tree on 2026-11-16.

## Coverage

The engine is about 150,000 lines in runtime/src, native/host and platform alone (render: 76,000). This audit read the paths that decide each area's question; it did not read every file. "Partly" means the named line ranges only. Updated at the end of Round 2: each cell lists Round 1 and Round 2 together. Rows 5-7 updated at the end of Round 5 (Round 5 stopped after item 4; item 5 not started). Rows 1, 5, 6 and 7 updated at the end of Round 6 (all five Round 6 items finished). Rows 4, 5, 7 and 10 updated at the end of Round 7. Round 7 finished item 5 and parts of items 1 and 4; items 2 and 3 were not started. Rows 4 and 8 updated at the end of Round 8; both Round 8 items were done in part (what is left is listed under "Round 8 items not read"). Rows 4 and 8 updated at the end of Round 9; both Round 9 items were done in part (the "Not read" cells list what is left). Rows 1, 4 and 8 updated at the end of Round 10: item 1 done for the small System files only, items 3 (Vulkan present question and the cooked material record) and 4 done, item 2 not started. Rows 4 and 8 updated at the end of Round 11: item 1 finished; item 2 done in part (named in its "Not read" cell); items 3, 4 and 5 not started, so rows 1-3, 5-7, 9 and 10 are unchanged. Row 4 updated at the end of Round 12: item 1 done (the Player files by condition-and-constant diff, named in the cell); items 2, 3 and 4 not started, so every other row is unchanged. Rows 2, 3, 4 and 8 updated at the end of Round 13: item 1 done (row 8 and row 3's state-word ranges; the importer in part); item 2 done for the upscalers and frame generation only (row 2: the post-process, bloom, scene camera and ambient occlusion files were not started); item 3 done (row 4), with NameObjFactory's 1,186-row table checked by count rather than read row by row.

| Area | Read completely | Read partly | Not read |
|---|---|---|---|
| 1 Frame pacing, presentation | present_slots.h/.cpp, frame_interpolation.cpp, precise_wait.h, platform_time.h, frame_phase.cpp, render_api.cpp, render_core.h, render_core.cpp, platform/sdl/display_clock.cpp, platform/apple/display_clock_apple.mm, perf_hint.cpp, nebula_frame_list.h; Round 4: native/host/host_main.cpp (all 6578), platform/sdl/thread_sampler.cpp, runtime/src/stats.cpp | render_config.cpp (1-120, 414-720), native_settings.cpp (5740-5800), renderer_d3d12.cpp (see area 3), vk/renderer_vk.cpp (580-880, 960-1440, 1940-2500, 2560-2700), renderer_mtl.mm (220-330, 600-630, 690-790, 1150-1200), renderer_d3d11.cpp (260-300, 480-720), FrameList.cpp (160-560, 740-820); Round 10: vk/renderer_vk.cpp (944-1161, 1960-2019, 2230-2340, 2975-3034), renderer_d3d12.cpp (1868-1870, 3157-3209, 4166-4274), renderer_d3d11.cpp (286-305, 507-513, 658-664), host_window_win32.cpp (489-588 by grep, 660-675), renderer_mtl.mm (293, 1171 by grep) | the D3D11 present path outside 480-720 (native_game_host_sdl.cpp read completely in Round 6, area 7) |
| 2 Frame as drawn | draw_walker.cpp (all 2213 lines, Round 3 added 1-1080); game_bloom_d3d11.cpp, post_process_d3d11.cpp, scene_camera_d3d11.cpp, ambient_occlusion_d3d11.cpp, game_bloom_mtl.mm (Round 3); Round 13: post_process_settings.cpp, temporal_upscaler.cpp, xess_upscaler.cpp, fsr_frame_generation.cpp, metalfx_mtl.mm | vk/renderer_vk.cpp (580-880), renderer_vk_frame.cpp (456-575, 1422-1621, 2298-2376, 2712-2790, 3254-3365, 3578-3610), renderer_d3d12.cpp (see area 3), game_bloom.cpp (215-634), post_process.cpp (545-780, 937-1059), renderer_mtl_scene.mm (360-470); Round 13: render_config.h (85-464), renderer_d3d12.cpp (4678-5070), vk/renderer_vk.cpp (1955-1970, 2736-2751), renderer_vk_frame.cpp (3108-3177, 3915-3951), render_core.cpp (880-1040), post_process_mtl.mm (500-660); by grep for HDR, dynamic resolution and frame generation: streamline_bridge.cpp, streamline_vk.cpp, temporal_upscaler_vk.cpp, sgsr2_vk.cpp, sgsr2_mtl.mm, sgsr2.hlsl | post_process_vk.cpp, post_process_mtl.mm outside 500-660, post_process.cpp outside 545-780 and 937-1059; game_bloom.cpp 1-215, game_bloom_vk.cpp (beyond line 287 cited in Round 2); scene_camera.cpp, scene_camera_vk.cpp, scene_camera_mtl.mm; ambient_occlusion.cpp, ambient_occlusion_vk.cpp, ambient_occlusion_mtl.mm; the bodies of temporal_upscaler_vk.cpp, sgsr2_vk.cpp, sgsr2_mtl.mm, streamline_bridge.cpp, streamline_vk.cpp; fsr_frame_generation_vk.cpp, fsr_frame_generation_core.inl (Round 13 item 2 reached the upscalers only) |
| 3 Renderers | core/pipelines.cpp, device_gate.h; Round 3: renderer_d3d12.cpp (all 7501), vk/renderer_vk_frame.cpp (all 3951), vk/renderer_vk.cpp, vk/vk_context.cpp, texture_store.cpp (1526), d3d11/renderer_d3d11.cpp, renderer_d3d11_scene.cpp, d3d11_context.cpp, texture_backend_d3d11.cpp, encoder_d3d11.cpp, compile_stage_d3d11.cpp, rt_backend_d3d11.cpp, mtl/renderer_mtl.mm, renderer_mtl_scene.mm, renderer_mtl_draw.mm, renderer_mtl_fx.mm, pipelines_mtl.mm, upload_memory_mtl.mm | the four renderer headers (stub lists), mtl_context.mm (270-376, plus 98-136 and 400 by grep), pipelines_{vk,d3d12,d3d11}.cpp and pipelines_mtl.mm (build_fast, save_cache), vk_context.cpp (1897-1940), metal.cmake (80-120); Round 13: the per-draw state-word decode, pipelines_vk.cpp (600-740), pipelines_d3d12.cpp (310-450), pipelines_d3d11.cpp (170-330) | d3d11/mod_post_d3d11.cpp (mods, out of scope); mtl_context.mm outside 270-376; texture_backend_mtl.mm (only 255-264 read); pipelines_{vk,d3d12,d3d11}.cpp outside build_fast, save_cache and the Round 13 ranges |
| 4 Game step | NebulaArena.hpp/.cpp, SceneExecutor.cpp, NameObjExecuteHolder.cpp, NameObjListExecutor.cpp, NameObjCategoryList.cpp, DrawBuffer.cpp, DrawBufferGroup.cpp, SensorHitChecker.cpp; Round 7: Demo/ScenarioStarter.cpp, NPC/CometEventKeeper.cpp, NPC/CometEventExecutorTimeLimit.cpp; Round 8: Scene/GameScene.cpp (all 775), Scene/SceneFunction.cpp, NameObj/NameObj.cpp, NameObjHolder.cpp, NameObjGroup.cpp, LiveActor/LiveActor.cpp, LiveActorGroup.cpp, ClippingDirector.cpp, ClippingActorHolder.cpp, ClippingActorInfo.cpp, ClippingJudge.cpp, ClippingGroupHolder.cpp, HitSensor.cpp, HitSensorKeeper.cpp, Binder.cpp, ShadowController.cpp, ShadowVolumeDrawer.cpp, ShadowVolumeBox.cpp, Map/CollisionCategorizedKeeper.cpp, CollisionParts.cpp, KCollision.cpp, LightFunction.cpp, Effect/EffectSystem.cpp, ParticleCalcExecutor.cpp, ParticleEmitterHolder.cpp, Particle/EmitterManager.cpp, Gravity/PlanetGravityManager.cpp, PlanetGravity.cpp, Render/FrameList.cpp (all 1419), Render/Model.cpp, System/NebulaFrameClock.cpp (+ .hpp), NebulaFrameGen.cpp; Round 9: Particle/EmitterCore.cpp, ParticleCore.cpp, Render/Material.cpp (all 1052, with Round 8's part), Clip.cpp, TypedDraw.cpp, Transform.cpp, ModelData.cpp, RenderTexture.cpp, GameTexture.cpp, Map/LightDirector.cpp, LightDataHolder.cpp, LightPointCtrl.cpp, LightZoneDataHolder.cpp, LiveActor/ActorLightCtrl.cpp, Map/CollisionDirector.cpp, Util/CollisionPartsFilter.cpp, TriangleFilter.cpp, LiveActor/AllLiveActorGroup.cpp, HitSensorInfo.cpp, ShadowSurfaceDrawer.cpp, Gravity/PointGravity.cpp, WireGravity.cpp, GravityFollower.cpp, SegmentGravity.cpp, DiskGravity.cpp; Round 10: System/NebulaSystemSettings.cpp, NebulaCalendar.cpp, NebulaLoadTrace.cpp, NebulaDataTables.cpp, NebulaSaveImport.cpp, NebulaCutsceneSkip.cpp, NebulaModCalls.cpp (now all, with Round 8's 1-60); Round 11: Screen/NebulaSettingsMenu.cpp (all 5,755), System/NebulaText.cpp, NebulaText.hpp, NebulaAchievements.cpp, NebulaAchievements.hpp, NebulaHost.hpp, NebulaInput.hpp, NebulaAtomic.hpp, NebulaList.hpp, NebulaClock.hpp, NebulaRegion.hpp, NebulaModule.c, NebulaFormat.c, Particle/ParticleDraw.cpp (now all 996), ParticleResource.cpp (now all 948), ParticleField.cpp, EmitterDynamics.cpp, ParticleKey.cpp, ParticleResources.cpp, ExTexShape.cpp, ChildShape.cpp, Effect/ParticleDrawExecutor.cpp, MultiEmitter.cpp; Round 12 (each compared with the current Petari snapshot): Map/KCollision.cpp and Petari's, CollisionCode.cpp lookups and Petari's, LiveActor/Binder.cpp, Binder.hpp and Petari's, Demo/ScenarioStarter.cpp and Petari's, MapObj/SpinDriverShootPath.cpp and Petari's, crates/nebula-import/src/cook/kcl.rs (all 674), native/include/nebula_float_cast.h; Round 13: Effect/MultiEmitterCallBack.cpp, MultiEmitterParticleCallBack.cpp, SpinPullParticleCallBack.cpp, EffectSystemUtil.cpp, Gravity/ConeGravity.cpp, CubeGravity.cpp, DiskTorusGravity.cpp, PlanetGravityManager.cpp (re-read), GravityInfo.cpp, LiveActor/ViewGroupCtrl.cpp, the ten shadow drawers (ShadowSurfaceBox, ShadowSurfaceCircle, ShadowSurfaceOval, ShadowVolumeCylinder, ShadowVolumeFlatModel, ShadowVolumeLine, ShadowVolumeModel, ShadowVolumeOval, ShadowVolumeOvalPole, ShadowVolumeSphere), Scene/SceneObjHolder.cpp (all 401), NameObj/NameObjAdaptor.cpp, NameObjFinder.cpp, NameObjRegister.cpp; ParallelGravity.cpp, GlobalGravityObj.cpp and GravityCreator.cpp by `diff -w` against Petari, with GravityCreator 300-340 read | Round 13: LiveActor/LodCtrl.cpp (1-140), Render/TypedDraw.cpp (220-260), Render/FrameList.cpp (539-560), System/LumaContentFx.cpp (425-454), NameObj/NameObjFactory.cpp (1-120, 4780-4892; the 1,186-row table checked by inline count for null creators and duplicate names); Round 12: Map/CollisionParts.cpp (285-600, both trees), Player/MarioActorRush.cpp (beginRush, endRush, both trees), Util/MapUtil.cpp (39-300 against Petari 45-330), Player/Mario.cpp, MarioJump.cpp, MarioMove.cpp, MarioWall.cpp, MarioSwim.cpp, MarioActor.cpp, MarioModule.cpp, MarioState.cpp (condition-and-constant diff against Petari, every flagged function read in both trees); Round 11: System/NebulaTextTable.inc (checked by script: 1,292 rows, missing texts per language), ParticleMath.cpp (78-146), ParticleShape.cpp and ExtraShape.cpp (function lists), NebulaNotActors.inc and NebulaNeedTheirLevel.inc (headers; mod spawn lists), System/GameSystem.cpp (240-349), Scene/GameScene.cpp (250-273, 720-731), Screen/NebulaUi.cpp (70-80, 423-462), LayoutCoreUtil.cpp (25-35), runtime/src/ui/native_settings.cpp (5700-5722) and benchmark_menu.cpp (395-401) by grep; Round 7: MarioModule.cpp (1-54), Mario.cpp (335-356), MarioActor.cpp (2747-2757), MarioAccess.cpp (462-483), SceneUtil.cpp (28-47, 143-149, 290-297), StageDataHolder.cpp (171-190), GameSceneFunction.cpp, ActorLightCtrl.cpp (40-135), DrawBufferExecuter.cpp (loops by grep), SeqTrack.cpp (1-80 and its byte reads), native/CMakeLists.txt (LTO/PGO lines), build scripts' LTO lines; Round 8: SceneObjHolder.cpp (147-225), ModelManager.cpp (20-97, 400-419), Material.cpp (625-1004), System/GameSystem.cpp (150-230), NebulaModCalls.cpp (1-60), Screen/NebulaModLogNotice.cpp (450-474), Util/Array.hpp (70-121, 275-311); Round 9: Particle/ParticleDraw.cpp (1-200), ParticleResource.cpp (330-709), AreaObj/LightAreaHolder.cpp (19-41), AreaObj/AreaObj.cpp (119-129), Util/MapUtil.cpp (578-607), Gravity/PlanetGravity.cpp (all 107, re-read), NameObjGroup.cpp (registerObj, re-read); Round 10 (for the FactoryGalaxy comparison): Player/Mario.cpp (338-357, 1532-1540, 1610-1620), MarioJump.cpp (2158-2235), Map/CollisionCode.cpp (100-133), the Petari counterparts (Mario.cpp 375-392, MarioJump.cpp 700-760 and 2387-2480, CollisionCode.cpp 27-33 and 87-205, Dossun.cpp by grep), include/Game/Util/Table.hpp (1-60), crates/nebula-import/src/cook/table.rs (comment header and 190-240) | After Round 13: ParticleShape.cpp and ExtraShape.cpp bodies, ParticleMath.cpp 1-77; Effect/MultiEmitterAccess.cpp; MovementOnOffGroupHolder.cpp, ModelChangableObjFactory.cpp; NameObjFactory.cpp 121-4779 row by row (count-checked only); NebulaMods.cpp (870) and NebulaMods.hpp are mod code, out of scope; the other game Screen files (NebulaAchievementBoard.cpp and the rest, beyond the lines named); the actor classes; Sound/* apart from SeqTrack.cpp; after Round 12: the eight Player files above line by line (only their conditions and constants were compared), Petari's Map/KCollisionPlus.cpp |
| 5 Services | platform/sdl/crash_log.cpp, atomic_file.cpp, native_audio.cpp, audio_device_sdl.cpp, audio_device_apple.mm, native/host/crash_watch.cpp; Round 4: audio_mixer.cpp/.h, audio_device_wasapi.cpp, wiimote_speaker.cpp, input/host_input.cpp, input/live_controls_engine.cpp, pointer_control.cpp, input/co_star.cpp, input/controls_engine.cpp, input/rumble_route.cpp, input/key_positions_win32.cpp, input/raw_mouse_win32.cpp (+ raw_mouse.h), rumble.cpp, input/wiimote_input.cpp, input/wiimote_motion.cpp, input/wiimote_motion_pointer.cpp, input/mod_actions.cpp, save_files.cpp; Round 5: controller_hid.cpp (all 2741), input_bindings.cpp, native_input.cpp, wiimote_hid.cpp, wiimote_link_win32.cpp, wiimote_protocol.cpp, platform/sdl/wiimote_link_android.cpp, mii_import.cpp, save_transfer.cpp (all 2260), SaveDataHandler.cpp, SaveDataHandleSequence.cpp, native_settings_pad.h, settings_display_page.cpp, settings_graphics_page.cpp, settings_general_page.cpp, settings_system_page.cpp, settings_advanced_page.cpp, settings_benchmark_page.cpp, settings_model.cpp, settings_save_model.cpp/.h, launcher_model.h, home_menu_model.h (the models are header-only), benchmark_menu.cpp, hdr_calibration.cpp, calibration_scene.cpp, benchmark.cpp, benchmark_results.cpp, benchmark_stats.cpp/.h, benchmark_recommend.cpp, benchmark_cover.h, frame_telemetry.cpp, diag_report.cpp (all 919), runtime/src/update/ (every file except update_tests.cpp, so update.cpp and update_install_windows.cpp are now complete); Round 6: render/host_window_win32.cpp (all 1799), render/os_hdr.cpp (506), windows_child.h, hidden_start.h/.cpp, launch_args.h, windows_power.h, power_policy.h (these four are header-only; the brief's .cpp names do not exist), discord_presence.cpp, achievements.cpp, movie_video.cpp, render/movie_playback.cpp, installer/native setup_engine.cpp (all 1258), setup.cpp (634), common.cpp (1380), archive.cpp (590), ui.cpp (703), start.cpp, text.cpp, installer/build-native-setup.ps1, sign-release.ps1, package-linux.sh; Round 7: movie_video_idct.inl, update/update_install_linux.cpp, update/update_install_macos.mm, installer/linux/install.sh, uninstall.sh, installer/native/setup_engine.h, archive.h, text.h | crash_report.cpp (48-135), installer/linux/nebula-launch; Round 4: luma_input.cpp host half (1110-1170, 1575-1752), switch_controller.cpp (212-242); Round 5: native_settings.cpp 1-2758 and 4816-6486 (2759-4815, the mods pages, skipped as out of scope); Round 6: renderer_d3d12.cpp and vk/renderer_vk.cpp window placement (3316-3370, 2753-2820), display_modes.cpp (200-215), installer/linux/install.sh (grep lines) | the input, save, update, launch_args, power_policy, windows_power, hidden_start and discord_presence *_tests.cpp files; installer/native/common.h and ui.h |
| 6 Android | proguard-rules.pro; Round 5: Updates.kt (all 481); Round 6: every Java and Kotlin source of the app (GameActivity.java 838, TouchControlsView.java, WiimoteUsb.java, PadSensors.java, GameFiles.java, ClipboardBridge.java, TouchLayouts.java, DisplayRate.java, Importer.java, LauncherModel.kt, SaveTransfer.kt, GpuDrivers.kt, ImportService.kt, Store.kt, LauncherActivity.kt, ModImport.kt, MiiImport.kt, Dropped.kt, Licenses.kt, ui/Screens.kt, ui/MotionTest.kt, ui/Components.kt, ui/Theme.kt, ui/Starfield.kt), AndroidManifest.xml, app/build.gradle.kts, mod_import_jni.cpp, mii_import_jni.cpp, crates/nebula-import/src/jni.rs, host_window_sdl.cpp (area 7) | crates/nebula-import/src/lib.rs (957-1004), cook/mod.rs (459-519), wiimote_link_android.cpp (160-190; complete in Round 5, area 5) | the shipped Harness.java 19; res/ (layouts, xml/wiimote_usb.xml, shared_files.xml); SDL's Java half (not in this tree) |
| 7 Linux, macOS | audio_device_apple.mm, display_clock_apple.mm, platform/sdl/audio_device_sdl.cpp; Round 5: update_transport_curl.cpp, update_transport_apple.mm, update_probe_posix.cpp; Round 6: host_window_sdl.cpp (all 2527), native_game_host_sdl.cpp (all 611), sdl_host.h, platform/sdl/CMakeLists.txt (all 411), host_window_apple.mm (all 1806), apple_host.mm, setup_apple.mm, build_macos.sh, platform/apple/CMakeLists.txt (all 203), Info.macos.plist.in, Nebula.entitlements, platform/linux/build_linux.sh, llvm-toolchain.cmake, dev.nebula.galaxy.desktop, dev.nebula.galaxy.xml | apple_host.h; metal.cmake (60-172; 1-60 by grep); release.yml (389-467, the Mac job) | luma_system_sdl.cpp, luma_system_apple.mm (mod code, out of scope); update_install_linux.cpp and update_install_macos.mm were read in Round 7 (area 5) |
| 8 Console-era | Round 8: the item 1 files above read for console-era design too (FrameList.cpp, Model.cpp, KCollision.cpp, CollisionParts.cpp, CollisionCategorizedKeeper.cpp, LightFunction.cpp, ShadowVolumeDrawer.cpp, ShadowVolumeBox.cpp, HitSensor.cpp, the clipping files, NebulaFrameClock.cpp/.hpp, NebulaFrameGen.cpp); Round 9: Render/Material.cpp, Clip.cpp, TypedDraw.cpp, Transform.cpp, ModelData.cpp, RenderTexture.cpp, GameTexture.cpp, the light files of row 4, Particle/EmitterCore.cpp, ParticleCore.cpp, read for console-era design (P-149 to P-151, P-153 to P-157); Round 13: runtime/src/render/core/registries.cpp (now all 1050), compose.cpp (now all 247), frame_core.h, draw_record.h, program_table.cpp, world_lights.cpp, crates/nebula-import/src/cook/material.rs, material_layout.rs | console API name census; capacity-check census (10 sites, all grow); byte-swap census; frame-counter and 60 Hz constant census over runtime/src, native/host and platform; SeqTrack.cpp byte reads; Round 8: a grep of System/Nebula*.cpp, Render/*.cpp and their headers for fixed arrays and 640, 480, 60 and 4:3 constants; Material.cpp (625-1004); NebulaModCalls.cpp (1-60); Round 9: runtime/src/render/core/registries.cpp (352-534, 818-863), compose.cpp (118-247), draw_walker.cpp (200-309, 636-715, re-read for this area); a whole-tree count of TypedDraw vertex writes by primitive (109 sites in 47 files); Round 10: native/include/nebula_model.h (NebulaModelMaterial and NebulaModelStage, 273-333), the System files of row 4 read for console-era shapes; Round 11: the System files and the particle files of row 4 read for console-era design (setPTev's GX byte-offset arithmetic, ParticleResource.cpp:742-777; the 182/0xB6 degree constant; the cStatFlushFrames 60 literal; NebulaFormat's fixed-only %e/%g); Round 13: pipelines_vk.cpp (600-740), pipelines_d3d12.cpp (310-450), pipelines_d3d11.cpp (170-330) for the state word; crates/nebula-import/src/cook/programs.rs (1-340, 1322-1521, 1755-2054), model_write.rs (36-125, 1593-2300), model_ir.rs (1008-1132), texture.rs (declarations); Render/Material.cpp (290-880, re-read), GameTexture.hpp, RenderTexture.cpp (120-230) for the map in R13.1 | After Round 13: Render/* files other than those named in row 4 (FrameList and Model were read in Round 8); the importer outside the ranges named (programs.rs 341-1321 and 2055-2209, model_write.rs 126-1592 and 2301-2558, texture.rs bodies); Petari comparisons for the Render files (Round 9 compared only the light-area search, P-152; Petari has the J3D headers, libs/JSystem/include/JSystem/J3DGraphBase/J3DMaterial.hpp and others, which a comparison of Material.cpp and TypedDraw.cpp would use); the actor classes for fixed member arrays (`T mArr[N]`) |
| 9 Code health | — | line counts, getenv and catch(...) census, duplicated blocks named in P-022 | dead-code search beyond P-001 |
| 10 Tests and gates | .github/workflows/release.yml (1-110 and job list), root CMakeLists.txt test block (600-800), the header comment of every tools/verify script, the pre-push hook in the shared .git; Round 7: tools/verify/run.sh | tools/test_driver/test_driver.cmake (1-30), native/tests/run_tests.sh (1-20), website.yml (job list) | the bodies of the 82 test files |

Areas not finished after Round 2:
- 2: draw_walker.cpp 1-1080 was not read.
- 3: not read:
  - the D3D12 ranges named above;
  - texture_store.cpp;
  - the D3D11 and Metal scene and draw files outside the named ranges.

  So Metal and D3D11 lifetimes and per-frame allocations are not covered.
- 4: only the executor, category-list, draw-buffer and sensor paths were read. GameScene, SceneFunction and the Nebula*.cpp files were not read.
- 5: not read:
  - benchmark, save_transfer, native_settings and the settings pages;
  - input and controllers;
  - WASAPI;
  - update/ and installer/native.
- 6: 23 of the 25 Kotlin/Java files were not read.
- 7: the two host window files were read only in the named ranges.
- 8: there was no file-by-file pass.
- 10: the test bodies were not read.

## Findings

### Area 1: frame pacing and presentation

- P-001 (maintainability) `frame_rate_decouple` is a dead setting with a live branch in the render loop.
  - Evidence: runtime/src/render/render_config.cpp:594-595 and :670 force `cfg.frame_rate_decouple = false` on every load/reload ("Frame-rate decoupling is not offered: its key is read but stays off"); render_config_tests.cpp:573-575 asserts it stays off. Yet runtime/src/render/render_core.cpp:1751-1752 computes `idle_present` from it, :1793-1798 parks 1 ms for it, :1897-1898 calls `present_latest_frame_on_thread()` (:2028-2030), which re-presents the newest picture without counting it.
  - Why: unreachable code in the hottest loop of the renderer; if anyone re-enables the key, the render thread would present the same picture every 1 ms wake with VSync off (no rate check on that path, `count_game_display_present=false` skips `skip_for_present_rate`, :2058-2061). Fix: delete the field, the parser line, `idle_present` and `present_latest_frame_on_thread`.
  - Confidence: verified in code.

- P-002 (performance) Every renderer's begin_frame stats config.ini twice a second on the render thread and can block on the settings UI mutex.
  - Evidence: renderer_d3d12.cpp:3610, vk/renderer_vk.cpp:1951, d3d11/renderer_d3d11.cpp:523, mtl/renderer_mtl.mm:562 all call `host_window::poll_render_config_file()`; ui/native_settings.cpp:5752-5776 runs every 500 ms: `std::lock_guard<std::recursive_mutex> lock(s_native_settings_mutex)` then `reload_render_config_if_changed()`, which calls `file_write_time(config_path())` (render_config.cpp:644-651: GetFileAttributesExW / std::filesystem::last_write_time) and builds a path each time; `next_start_gpu()` returns a std::string each call.
  - Why: a file-system stat (antivirus filter drivers on Windows, FUSE/sdcardfs on Android) and a recursive mutex shared with the settings UI thread sit inside the render thread's frame, twice a second, including in every generated (interpolated) picture's begin_frame. A slow stat or a held UI lock delays that picture: a periodic hitch. Fix: watch the file on a background thread (ReadDirectoryChangesW / inotify / FileObserver) and hand the reloaded config over atomically; never take the UI mutex on the render thread.
  - Confidence: the call path is verified in code; whether a stat costs more than a frame's slack on a given phone is suspected, needs a run: `--diagnostics timeline` (Windows) or a simpleperf trace of the render thread on the tablet, looking at begin_frame time every 500 ms.

- P-003 (wrong behaviour) The game's speed follows the display, not 60 Hz, whenever the display is within 3 % (Android) or 1 % (Windows) of a multiple of the step.
  - Evidence: platform/sdl/display_clock.cpp:138-159: accepts `|vsyncs - round(vsyncs)| <= 0.03` and then sleeps to the vsync (`target = base + steps * interval`), so a step lasts `round(vsyncs) * interval`. runtime/src/frame_phase.cpp:9-15 (`off * 100 <= period_ns`) and :36-46 lock the Windows step's cadence to `count * refresh->period_ns` with a slew. native/host/host_main.cpp:1099-1104 comment says the step is 60 Hz.
  - Why: a 58.5 Hz or 61.5 Hz panel (Android allows both at the 3 % bound) runs the whole game 2.5 % slow or fast; every in-game timer is in frames. The audio device's clock is separate (host_main.cpp:1032-1035), so game events and the music drift apart by that share over a long scene. Fix: lock to the display only when the measured refresh is within a tight tolerance (0.1 %) of a multiple of 60 Hz, otherwise keep the host's 60 Hz clock and let presentation (interpolation or present slots) absorb the beat.
  - Confidence: verified in code (the tolerance and the sleep target); how many real panels sit between 0.1 % and 3 % off is suspected, needs a run: log `[pacing] ... (%.1f Hz)` (host_main.cpp:1165) on the test devices.

- P-004 (performance, Android) The Choreographer thread re-posts its vsync callback forever, also while the app is in the background.
  - Evidence: platform/sdl/display_clock.cpp:84-92 (`on_frame`/`on_timeline` re-post themselves unconditionally), :98-115 (`clock_thread` detached, `ALooper_pollOnce(-1)` loop, never stopped); host_main.cpp:999-1006 only parks the game thread.
  - Why: a vsync callback requested every refresh keeps a thread waking at 60-120 Hz with the game suspended. Fix: stop posting while suspended (a flag checked in the callback; post again from the resume path).
  - Confidence: verified that nothing stops the re-posting; whether Android throttles vsync delivery to a backgrounded app on the target OS versions is suspected, needs a run: background the app on the tablet and count `Nebula Vsync` thread wakeups (`adb shell top -H -p <pid>` or simpleperf sched).

- P-005 (wrong behaviour, formally; ARM) The game side reuses a frame list's store after reading `released` with no acquire.
  - Evidence: runtime/src/render/render_core.cpp:2149-2150 publishes with `std::atomic_thread_fence(std::memory_order_release)` then writes `list->released = 1`. native/petari/src/Game/Render/FrameList.cpp:354-356 reads `if (p->mList.released != 0)` (a `volatile uint32_t`, native/include/nebula_frame_list.h) and then `resetStore` rewrites the pages (:340-346 `p->mUsed = ...`, `memset(&pStore->mList, 0, ...)`). FrameList.cpp has no `memory_order` or fence anywhere (grep: 0 hits).
  - Why: without an acquire fence (or `std::atomic_ref<uint32_t>::load(acquire)`) after the flag read, the game thread's later writes to the pages are not ordered after the render thread's last reads of them under the C++ model; on ARM (Android, Apple Silicon) the hardware may also reorder them. x86 TSO hides this on Windows. Fix: read the flag with an acquire load (`__atomic_load_n(&released, __ATOMIC_ACQUIRE)` / `std::atomic_ref`), or put `std::atomic_thread_fence(std::memory_order_acquire)` after a non-zero read.
  - Confidence: verified in code (no acquire exists). Whether an ARM core ever reorders here in practice is suspected, needs a run: ThreadSanitizer on the Linux host would flag it at once (a TSan build of host + game, one minute of play).

### Area 2: the frame as drawn

Order of one walk (runtime/src/render/core/draw_walker.cpp:1860-1913, walk_pass :1915-2060): early target runs first (plan_early_targets, :1664-1755), then every pass in list order. Copies per real picture: each COPY_FRAME / TAKE_FRAME pass is one copy or one swap (take_scene trades the scene texture with an idle one, no copy, :1997-2005; the fallback draw_frame_pass copies). walk_show (:2174-2202) trades the frame target with an idle display texture (take_frame_picture), no copy, and `copy_fg_hudless_display` adds one copy only when frame generation needs a HUD-less picture. walk_post (mod post programs) is one swap plus one full-screen draw per program. Scene end runs the AA resolve (record_scene_aa) and opens the output scene. The planners (early targets, early probes, late depth clears) exist to keep the scene pass unbroken on tiled GPUs and are sound as far as read (see "checked and correct").

- P-006 (performance, Android/tiled GPUs) Every Vulkan scene pass stores depth and stencil, and the multisampled colour, at its end; no pass variant discards them at their last use and no transient (lazily allocated) attachments exist.
  - Evidence: runtime/src/render/vk/renderer_vk.cpp:598-614: `attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;` copied to `attachments[1]` (depth, `stencilStoreOp = STORE`) and, with MSAA, the multisampled colour is attachment 0 with STORE while attachment 2 takes the resolve. All variants created at :649-683 keep these store ops (only load ops vary). Same at :745-756 for the temporal passes. Grep for `LAZILY_ALLOCATED|TRANSIENT_ATTACHMENT|STORE_OP_NONE` in runtime/src/render/vk: 0 hits; `storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE` appears only for post_process_vk.cpp:72 (SMAA stencil).
  - Why: on a tiled GPU, each pass end writes the full depth-stencil (and, with MSAA, N samples of colour) out to memory even when nothing reads it afterwards, which is the case for the last pass before present (the HUD pass) and for MSAA colour once resolved. That is tens of MB of bandwidth per frame at tablet resolution. Fix: add `storeOp = DONT_CARE` variants for depth/stencil chosen when the walk knows no later pass reads depth (the same analysis plan_late_depth_clears already does), resolve MSAA in-pass with DONT_CARE on the multisampled colour, and allocate MSAA colour and depth as TRANSIENT + LAZILY_ALLOCATED where the device has such a memory type.
  - Confidence: verified in code (store ops). The bandwidth saved is suspected, needs a run: Android GPU Inspector or Arm Streamline / Snapdragon Profiler on the tablet, reading "external memory write bytes" per frame before and after.

- P-007 (performance, Android/tiled GPUs) Vulkan parallel recording splits the scene pass into several render pass instances.
  - Evidence: runtime/src/render/core/draw_walker.cpp:1082-1096: any draw pass of 1500+ items on a machine with 6+ logical cores is recorded on `clamp(cores/2, 2, 8)` workers (an 8-core phone qualifies). runtime/src/render/vk/renderer_vk_frame.cpp:2764-2782 `parallel_record_end`: `end_scene_pass("parallel recording")`, then for each run of chunks with the same aux `begin_scene_pass_with(...)`, `vkCmdExecuteCommands`, `end_scene_pass(...)`; the walk's next draw opens the scene pass again. No tiled-GPU or quirk check in `parallel_record_begin` (:2712-2742) or in `recording_workers`.
  - Why: each end/begin of the scene pass on a tiler stores and reloads the whole colour and depth (the planners in draw_walker.cpp:1661-1663 exist to avoid exactly this, "about 100 MB at 2960x1848"). The parallel path adds at least two such breaks per big pass, more when aux changes between chunks. Fix: on Vulkan keep one render pass instance and record the chunks as secondaries inside it (`VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS` for the whole pass, or dynamic rendering with `VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT` + `RESUMING/SUSPENDING`), or turn parallel recording off where the device is a tiler.
  - Confidence: the split is verified in code. Whether a real list reaches 1500 items in one pass on the tablet is suspected, needs a run: `--diagnostics gpu-passes` on the tablet in a busy galaxy (Good Egg start), counting "parallel recording" pass begins, or a RenderDoc/AGI capture counting render pass instances.

- P-008 (crash, macOS/iOS) A Metal command-buffer error is never treated as a recoverable device loss; it ends the renderer.
  - Evidence: runtime/src/render/mtl/renderer_mtl.mm:621-625 throws `std::runtime_error("a command buffer failed: " + what)`, `what` being Metal's `localizedDescription` (mtl_context.mm:294-296). render_core.cpp:1544-1554 `bear_device_loss` only bears an error whose text `crash::is_gpu_lost` matches; crash_report.cpp:48-53 matches DXGI codes, "DEVICE_LOST", "device lost", "device was lost", "device removed", "VkResult -4" — none of which appears in Metal's GPU-error descriptions (e.g. "Caused GPU Timeout Error", "Discarded (victim of GPU error/recovery)"). `gpu::device_removed` for Metal always returns false (mtl/upload_memory_mtl.mm:29-30), so DeviceGate (device_gate.h:35) also rethrows.
  - Why: a GPU timeout or a recovery that hits another app's work ("innocent victim") stops rendering for good on Mac/iPhone/iPad, while D3D12/D3D11/Vulkan rebuild the device. Fix: include "device lost" in the Metal message (e.g. "Metal command buffer failed, device lost: ...") for `MTLCommandBufferErrorTimeout`, `...PageFault`, `...Blacklisted/AccessRevoked`, `...InnocentVictim`, `...DeviceRemoved`, by checking `done.error.code`, and let restart_device rebuild.
  - Confidence: verified in code; the exact Metal description strings are from Apple's error domain and need a run to confirm the text: force a GPU timeout on the Mac (an endless loop shader under `--diagnostics`) and read the log.

- P-009 (platform gap) Feature parity: D3D11 and Metal lack several features the other two have; three of them silently.
  - Evidence (stubs in the headers): runtime/include/galaxy/render/d3d11/renderer_d3d11.h:99-123, 140, 247-257, 329-331: no temporal upscaling or frame interpolation output width (`temporal_output_width() { return 0u; }`), no layer window, no motion scene, no bindless, no frame-generation HUD-less copy, `begin_layer_2d` a no-op (the HDR paper-white 2D layer), no parallel recording, RT stubs. runtime/include/galaxy/render/mtl/renderer_mtl.h:151, 159-160, 241-242, 266, 274: no HUD-less copy, no occlusion detection, `record_mod_post` and `record_native_call` empty (mod post programs and mod native draws are dropped on Apple), `begin_layer_2d` no-op, `set_present_game_frame` no-op. Vulkan: `occluded()` always false (vk/renderer_vk.h:269-270), `set_present_game_frame` no-op (:391).
  - Why: `begin_layer_2d` being empty on D3D11 and Metal means the HUD is not drawn at paper white when HDR output is on there, if those renderers offer HDR output (not verified). Mod post programs and native draws vanishing on Metal is a silent difference a mod author cannot see. No occlusion on Vulkan/Metal means a fully covered window keeps rendering at full rate on Linux and macOS (only minimise is handled: platform/sdl/host_window_sdl.cpp:1262-1267). Fix: hide the settings that need a missing feature per renderer (renderer_choice / settings page), report "not on this renderer" for the mod features, and handle `SDL_EVENT_WINDOW_OCCLUDED`/`EXPOSED` in the SDL host.
  - Confidence: the stubs are verified in code. Whether D3D11/Metal offer HDR output at all was not checked (not read: d3d11 and mtl swap-chain setup).
  - Settled in Round 2 by reading: neither offers HDR output (P-032), so the empty `begin_layer_2d` has no effect today. The settings page offering HDR on them is P-032's issue.

- P-010 (performance) A draw whose pipeline is not made yet compiles it on the render thread; only Vulkan with graphics pipeline libraries gets a quick link.
  - Evidence: runtime/src/render/core/pipelines.cpp:239-288 `create_now`: `build_fast` then, when null, `build_noting` (the full build) synchronously. `build_fast` returns nullptr for D3D12 (core/pipelines_d3d12.cpp:525-527), D3D11 (core/pipelines_d3d11.cpp:328-330) and Metal (mtl/pipelines_mtl.mm:365-367); Vulkan only with `pipeline_libraries()` (core/pipelines_vk.cpp:549-551). The title waits for the start's set only on a cold cache (native/host/host_main.cpp:3264-3274); later sets (a galaxy's new materials, a settings change) are prepared by low-priority workers (pipelines.cpp:446-449) that the draw does not wait for.
  - Why: a draw that reaches a new pipeline before the low-priority workers do stalls the render thread for one full driver compile (tens to hundreds of ms on a cold driver cache), a visible hitch on first entry to a galaxy. Fix: when a pipeline is missing, skip that draw for this frame (or draw with a generic fallback program) and raise the job's priority, instead of compiling inline; or prepare a galaxy's pipeline set during its load screen and wait there.
  - Confidence: verified in code. How often draws miss is logged as "made by draws first" (pipelines.cpp:583-587); needs a run: a first run of a fresh install on the tablet and the Windows PC, reading that count and the frame-time spikes in `--diagnostics timeline` on first entry to three galaxies.

- P-011 (performance, D3D11) D3D11's `video_memory` queries the adapter on every walk, generated pictures included; D3D12 and Vulkan cache it for 100 ms.
  - Evidence: runtime/src/render/d3d11/renderer_d3d11.cpp:491-505 (`QueryInterface` + `QueryVideoMemoryInfo` each call) vs renderer_d3d12.cpp:1880-1885 and vk/vk_context.cpp:1900-1903 (100 ms). Called from `enforce_video_memory` (render_core.cpp:2184-2187) in `begin_walk_frame` (:2295) for every walk.
  - Why: a kernel-mode query per picture, 240+ times a second with interpolation. Fix: the same 100 ms cache as the other two.
  - Confidence: verified in code.

Pipeline cache on a first run, per renderer: D3D12 and Vulkan keep a driver cache file plus a `.targets` file naming what it holds (pipelines.cpp:599-694); the title waits for the start's set only when that pair says the cache is cold, from another build, or other settings (decide_warm, :739-768). Metal keeps no file of its own (mtl/pipelines_mtl.mm:569-586, the system's shader cache instead), so `warm` is never true and the title waits for the start's set on every start (shorter once the system cache is warm). D3D11: not read.

### Area 4: game step performance structure

Structure as read: the game runs on one thread (host_main.cpp:6370 `std::thread game(...)`); native/petari creates no threads (git grep for std::thread / pthread_create / CreateThread in native/petari: none outside tools). Game-to-host hand-off of the frame is by pointer to host-heap pages (FrameList.cpp:349-369; no copy). Saves, file reads and mod cooking run on the host's `gFs` worker in FIFO order (host_main.cpp:3406-3479). Game memory is NebulaArena (native/petari/src/Game/System/NebulaArena.cpp): one reserved 4 GB address span per arena, size-class free lists, committed in 1 MB steps, freed wholesale on arena destroy. LTO/PGO per platform: Windows release = ThinLTO + PGO (`.github/workflows/release.yml:185` passes `-Pgo`; native/CMakeLists.txt:355-359); Linux, macOS and Android = ThinLTO only (CMakeLists.txt:350-353 forces PGO off off Windows; platform/linux/build_linux.sh:61, platform/apple/build_macos.sh:69, platform/android/build_apk.ps1:88 pass `NEBULA_LTO`).

- P-012 (wrong behaviour, latent) The game's global `operator new`/`delete` are not thread-safe and nothing enforces the "game thread only" rule.
  - Evidence: native/petari/include/Game/System/NebulaArena.hpp:73 comment "Only the game thread allocates (the sound engine's thread takes nothing)". NebulaArena.cpp:226-246 (`takeBlock` pops `mFree[index]` and bumps `mNext` with no lock), :272-286 (`free` pushes with no lock), :328-362 (global new/delete route here), :318-326 (`findFromRoot` walks a static array that `create`/`destroy` resize with `realloc`, :91-106).
  - Why: one `new` or `delete` of a game-module object from any other thread (an audio callback in the game module, a mod thread, a future worker) corrupts a free list silently, far from the cause. Fix: in debug and test builds assert the calling thread is the game thread in `NebulaArena::alloc`/`free` (store the id at root creation); keep release builds lock-free.
  - Confidence: the missing lock and missing check are verified in code; whether any current caller breaks the rule is not verified (it needs every thread entry into the game module audited, or a run with such an assert across the 6-galaxy tour).
  - Settle (Round 2): a Linux build of host and game with `-fsanitize=thread` (`platform/linux/build_linux.sh` with `CFLAGS/CXXFLAGS=-fsanitize=thread`), played by hand through three galaxies (tools/verify is Windows-only, run.sh:3). Any TSan report naming NebulaArena::alloc or NebulaArena::free answers it. The same run settles P-005.

- P-013 (performance, scales with object count) Every object construction and every `delete` searches all arenas linearly.
  - Evidence: NebulaArena.cpp:109-113 `ArenaOwned::ArenaOwned() : mArena(NebulaArena::findFromRoot(this))`, :309-316 `free` calls `findFromRoot(pData)`, :318-326 a linear scan of `sArenas`.
  - Why: cost per delete = number of live arenas; with many child arenas (a scene's, each layout's, each mod's) this adds a scan to every free in the frame. Fix: keep the arena spans in a sorted array and binary search, or put the owning arena pointer in the block Header (it already holds class and back offset; 8 more bytes).
  - Confidence: verified in code; the live arena count during play is not known: needs a run that logs `sArenaCount` once a second in a busy galaxy.
  - Settle (Round 2): without changing code, attach a debugger to the Windows build in Good Egg and watch `sArenaCount` (NebulaArena.cpp). Or run a Superluminal or ETW CPU sample with `NebulaArena::findFromRoot` as the filter, over 60 s of play.

- P-014 (maintainability) The Windows release links a stored PGO profile whose staleness warnings are switched off.
  - Evidence: native/CMakeLists.txt:357-358 `-fprofile-instr-use=${game_profile} -Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date`; native/pgo/game.profdata dated 2026-11-14 in the working tree; tools/verify/pgo_profile.sh:4 "Run before a release when much of the game has changed" (a manual step); release.yml:185 uses whatever profile is committed.
  - Why: functions changed since the profile lose their counts with no message, so the release's speed silently drifts from what was measured. Fix: let the release workflow fail (or warn in its summary) when the out-of-date share passes a threshold (`llvm-profdata show --detailed-summary` against the build, or keep the warning on and count it).
  - Confidence: verified in code (flags, file date). How much of the profile is stale today needs a run: a `-Pgo` build with the two `-Wno-` flags removed, counting the warnings.

### Area 5: services (audio, input, saves, settings UI, benchmark, updater, installer, crash, diagnostics)

What a crash produces, per platform (from the code):
- Windows: a vectored handler + unhandled-exception filter + SIGABRT handler (host_main.cpp:1829-1832) hand the fault to a dedicated `crash_writer` thread that writes a minidump via dbghelp and a facts `.txt` (report_crash_facts), then the exe relaunches with `--crash-report` to show the classified report (crash_report.cpp `classify`). native/host/crash_watch.cpp also has an out-of-process dump writer (crash_watch.h:5 explains why).
- Android: platform/sdl/crash_log.cpp installs SIGSEGV/BUS/ILL/FPE/ABRT/TRAP handlers that write signal, registers and an unwound stack (library + offset) to `<data>/logs/game.log` and logcat, then chain to debuggerd (tombstone). No in-app report on the next start.
- Linux: the same handler, but writing to stderr (host_window_sdl.cpp:1075 `install_crash_log(-1)`), and nothing redirects stderr to a file (P-016).
- macOS / iOS: nothing of Nebula's; crash_log.cpp is `#if defined(__linux__)` and the else branch is empty (:185-187). Only Apple's own crash report (.ips) exists.
- All SDL hosts: a fatal error the host catches (`die`) shows a message box (show_fatal).

Diagnostics upload (runtime/src/diag_report.cpp): only benchmark results, sent on the user's "Send to developer" press (host_main.cpp:1886-1906), sealed to a build-time public key (X25519), log tail optional and stripped of user/computer names, paths, e-mails and IPv4 addresses (:648-705); HTTPS only; transports WinHTTP / Java HttpURLConnection / libcurl / NSURLSession; a `.sent` marker stops a second send. Crash reports are never uploaded.

- P-015 (platform gap) macOS and iOS write no crash report of Nebula's own.
  - Evidence: platform/sdl/crash_log.cpp:10 `#if defined(__linux__)` around the whole handler; :185-189 the non-Linux `install_crash_log` is empty. Apple hosts log to `<data>/logs/game.log` (platform/apple/apple_host.mm:63-81) but no handler writes the fault there.
  - Why: a Mac or iPad crash leaves game.log ending mid-line with no signal, address or stack; the player has to find a system .ips file. Fix: build crash_log.cpp for `__APPLE__` too (the Apple register names are already written in native/host/luma_guard.cpp:425-429), and give the game and render threads an alternate signal stack so a stack overflow is reported.
  - Confidence: verified in code.

- P-016 (platform gap, Linux) A Linux start from the app menu keeps no log file, so a crash report goes nowhere.
  - Evidence: installer/linux/nebula-launch (the .desktop `Exec`, platform/linux/dev.nebula.galaxy.desktop:10) `exec`s the program with no redirection. platform/sdl/host_window_sdl.cpp:1072-1076: only Android calls `forward_output_to_logcat` (which opens `logs/game.log`); other SDL hosts call `install_crash_log(-1)` (stderr). Windows (host_main.cpp:5817-5823) and Apple (apple_host.mm:77-81) both open `logs/latest.log` / `logs/game.log`.
  - Why: the diagnostics upload's `last_log()` and any support request have nothing to read on Linux, and the `[crash]` lines vanish (or land in the user journal). Fix: open `<data>/logs/game.log` on Linux when stdout is not a terminal, as the Apple host does.
  - Confidence: verified in code; whether the desktop session captures stderr into the journal depends on the distribution (needs a run on one GNOME and one KDE system: `journalctl --user -b | grep Nebula`).

- P-017 (crash reporting) The SDL crash handler sets `SA_ONSTACK` but the render thread and game thread have no alternate stack of their own, so a stack overflow there is not reported.
  - Evidence: platform/sdl/crash_log.cpp:178 `SA_SIGINFO | SA_ONSTACK`; the only `sigaltstack` calls are in native/host/luma_guard.cpp:397-419 (modding guard, per thread it enters). Grep: no other `sigaltstack` in native, runtime, platform.
  - Why: with no alternate stack, a SIGSEGV from stack exhaustion cannot run the handler; the process dies with no `[crash]` line. Fix: give each long-lived engine thread a sigaltstack at start (or one helper called from each thread entry).
  - Confidence: verified in code for the render thread; whether the game thread always passes through luma_guard's `thread_enter` was not verified (modding code, not audited).
  - Checked again in Round 2 (whole-tree `git grep -n sigaltstack`): the finding holds and is sharper. The only `sigaltstack` calls are in native/host/luma_guard.cpp:397-419 `thread_enter`, which is called only from native/petari/src/Game/System/LumaGuard.cpp:451 for a thread that runs a mod's code, and the guard is installed only when plugins are loaded (runtime/src/mod_plugins.cpp:362). With no mods, no engine thread has an alternate stack.

Audio, input, saves, updater (read partly): audio is pulled by the device's thread, which runs the mix (`tick`) when its buffer is empty (runtime/src/native_audio.cpp:120-163); saves go through `io::write_file_atomically` (temp file, fsync/FlushFileBuffers, rename, folder fsync: runtime/src/atomic_file.cpp) on the host's file worker, with two backups (save_files.cpp:74-98); the updater accepts a package only through a signed manifest and its SHA-256 (runtime/src/update/update.cpp:875-912). See "checked and correct".

- P-018 (wrong behaviour, rare) At exit or restart the host waits at most 5 s for pending saves, then ends the process anyway.
  - Evidence: native/host/host_main.cpp:4627-4633 `gSavesDone.wait_for(lock, std::chrono::seconds(5), ...)`, then only `log_line("host:", "a save still pending at exit")`; Android's restart then calls `Process.killProcess` (GameActivity.java:721-731).
  - Why: on slow storage (an SD card, a phone under I/O load, a network folder) a save still in its fsync is cut off; the atomic write keeps the old file, so the player silently loses the latest progress. Fix: wait for the save worker without a time limit when a game save (not a mod file) is pending, and show "Saving..." meanwhile.
  - Confidence: verified in code; how long a save takes on slow storage is suspected, needs a run with the save folder on a slow USB stick.
  - Settle (Round 2):
    - Windows: start with `--saves E:\nebula-saves`, where E: is a USB 2 stick. Save at a star, then choose Quit while the "save" log line is still open.
    - Look in logs/latest.log for "a save still pending at exit" (host_main.cpp:4633), then check whether the next start loads the new save or the backup.

### Area 6: Android app

Read: platform/android/app/proguard-rules.pro (whole), app/build.gradle.kts (SDK lines), AndroidManifest.xml (permissions), GameActivity.java (lifecycle 350-448, restart 718-732), platform/sdl/host_window_sdl.cpp (call_activity 286-330, restart 2085-2128), platform/sdl/wiimote_link_android.cpp (JNI init). minSdk 31, targetSdk 36, compileSdk 36, NDK 28.2, ABI arm64-v8a only unless `nebulaAbis` is set (build.gradle.kts:132-144); R8 minify on with proguard-android-optimize (:165-167). The game runs in its own process `:game` (manifest :132), one game per process, killed on finish (GameActivity.java:439-447).

ProGuard check, every method native code calls by name against the keep rules: activity methods through `call_activity` (host_window_sdl.cpp:388, 1128, 1139, 1887, 2125, 2311, 2331-2368): displayRateChanged, menuOpened, menuClosed, menuContextChanged, saveCopy, realWiimoteTaken, realWiimoteLetGo, arrangeControls, saveArrangedControls, cancelArrangedControls, resetArrangedControls, inputModeChanged, touchSettingsChanged, resetControlPlaces are all `public void x()` in GameActivity.java and kept by `public void *();`; `restartGame(int)` and `pickFiles(int)` are named. WiimoteUsb `slots/open/write/close` (wiimote_link_android.cpp:180-183) match the static signatures and the keep rule. Progress callbacks `update(JJ)Z` (mod_import_jni.cpp:72) are kept for implementers. diag_report.cpp's JNI uses only framework classes. Result: no missing keep rule found; a missing one logs "[android] the activity has no method ..." (host_window_sdl.cpp:304-307).

- P-019 (platform gap, Android) **Corrected in Round 2.** The app gives the system a performance hint, but it never reads the thermal state, and its hint reports only the busiest thread's CPU time, not the frame's wall time.
  - Round 1 said no performance hint exists. That was wrong: the Round 1 grep missed runtime/src/perf_hint.cpp. Re-run over the whole tree with `git grep -n -E "perf_hint|APerformanceHint"`.
  - What perf_hint.cpp does (read completely, 241 lines):
    - Android: it opens libandroid.so and looks up `APerformanceHint_getManager/createSession/reportActualWorkDuration/closeSession/updateTargetWorkDuration` with dlsym (:96-117).
    - It creates one session over the threads that called `perf_hint_add_current_thread`: the render thread (render_core.cpp:1518) and the game thread (host_main.cpp:6372). It makes the session again whenever a thread is added (:146-166).
    - The target is the step period. host_main.cpp:1146 sets it every loop through `perf_hint_set_target`, which calls `updateTargetWorkDuration` when present (:168-184).
    - `perf_hint_frame_done` (host_main.cpp:1141, once per game step) reports the largest CPU-time increase of any one frame thread since the last call (:186-202).
    - Apple: the frame threads get QOS_CLASS_USER_INTERACTIVE (:151) and CPU time is measured, but nothing is reported to the system.
    - Linux: CPU time is measured and dropped.
    - Windows: the function only checks the battery state every 2 s (:214-227).
  - What it does not do:
    - (a) Nothing reads the thermal state. `git grep -n -i -E "thermal|AThermal|getThermalHeadroom|SustainedPerformance"` over the whole tree finds only the benchmark's after-the-fact heat check (benchmark.cpp:1375-1394) and its result schema. No `AThermal_*`, no `PowerManager.getThermalHeadroom`, no listener.
    - (b) ADPF expects the actual work duration, meaning the wall time from the start of the frame's work to its end. This code reports the maximum per-thread CPU time instead (:189-193). A thread that waits on the GPU, a lock or I/O reports less than its wall time, so the system sees headroom that does not exist and may lower clocks into a missed frame.
    - (c) The report runs once per step from the game thread's loop (host_main.cpp:1141); suspension is handled (`wait_while_suspended()` continues before it, :1136-1139). It measures the render thread's CPU over a whole step, so with interpolation at 120 Hz two pictures' render work is reported against one 16.67 ms step target, and a render thread that misses its per-picture deadline still looks within budget.
    - (d) The game thread is added at host_main.cpp:6372. Any worker the render thread uses (parallel record, pipeline workers) is not in the session.
  - Why: (a) without a thermal signal, play cannot lower resolution or interpolation before the SoC throttles. The benchmark sees the throttling; play does not. (b) and (c) make the hint understate the work, which is the opposite of what the hint is for.
  - Fix:
    - Register `AThermal_registerThermalStatusListener` (API 30+, below the minSdk 31), or poll `AThermal_getThermalHeadroom` (API 31) once a second off the frame threads. Feed it to the dynamic-resolution governor and the interpolation governor.
    - Report wall time per step: the time from step start to the end of the step's game work, and for the render thread the time from picture start to submit, capped at the target. Do not use CPU time.
  - Confidence: (a) verified in code by whole-tree grep. (b), (c) and (d) verified in code. Whether the CPU-time report actually lowers clocks is suspected, needs a run: on the tablet, `adb shell dumpsys android.hint_manager` and a perfetto trace with the `power` and `sched` categories over 5 minutes in one galaxy. Compare CPU frequency and missed frames with the hint session as built against one fed wall time.

- P-020 (wrong behaviour, suspected) Android's restart starts the new activity in the `:game` process and kills that same process in the same UI-thread task.
  - Evidence: GameActivity.java:721-731: `startActivity(intent)` (FLAG_ACTIVITY_NEW_TASK | CLEAR_TASK) then `Process.killProcess(Process.myPid())` immediately; manifest :132 puts GameActivity in `android:process=":game"`.
  - Why: the system may deliver the new activity to the dying process before it dies; it then has to notice the death and start the process again, which usually works but has been seen to leave a black task or drop the start on some OEM builds. Fix: start the new instance from a tiny trampoline activity in the main process (the launcher's), or schedule the start with an `AlarmManager`/`PendingIntent` before the kill.
  - Confidence: suspected, needs a run: 50 restarts in a row from the mods page on the tablet and two other OEM devices, counting failed starts.
  - Round 2: both sides were read (see "Area 6, Android (Round 2)"). The finding stays suspected. Settle:
    - run `adb logcat -c`, then make 50 restarts with `adb shell input` taps on the mods page's restart button;
    - read `adb logcat -d -s ActivityManager:I ActivityTaskManager:I`;
    - count "Process ... :game ... has died" lines that are followed by no "Start proc ... :game" within 5 s.

Storage and permissions: app-specific storage only; the dangerous permissions (RECORD_AUDIO, CAMERA) are declared for mods' capture services and requested only at a mod's use (manifest :32-38, a modding dependency of the engine's manifest); REQUEST_INSTALL_PACKAGES for self-updates outside a store (:40-42).

### Area 7: Linux and macOS hosts

From the code (no run): Linux = platform/sdl host + Vulkan (the update check via libcurl, install via update_install_linux.cpp, restart via exec, NEBULA_HOST_START_AGAIN host_main.cpp:1879-1883); macOS = platform/apple host (AppKit, apple_host.mm, setup_apple.mm first-run import, display_clock_apple.mm, audio_device_apple.mm) + Metal, NSURLSession update check, bundle replacement (update_install_macos.mm); iOS has no restart (host_window_sdl.cpp:2089-2101). Gaps found: P-008 (Metal device loss fatal), P-009 (Metal drops mod post programs and native draws; no occlusion on Vulkan/Metal), P-015 (no crash report on Apple), P-016 (no log file on Linux), Metal pipelines never "warm" (Area 3). Whether the Linux and macOS packages are run anywhere besides the release workflow's build cannot be told from the code: the workflow builds them (release.yml:329-460) but runs nothing (P-023).

### Area 8: console-era design left

The console's APIs are gone from the engine and port files: git grep for `GX*(`, `OSReport/OSGetTime`, `DVD*(`, `VI*Retrace`, byte swaps in native/petari/src, native/host, runtime/src: 0 hits (the `ARAM`/`EFB` hits are words inside other identifiers: PARAMS, HRESULT values, text tables). What remains is behavioural:

- P-021 (console-era remnant) A frame copy asked before the frame's first draw returns the previous shown picture, "as the console's frame buffer still held it".
  - Evidence: runtime/src/render/core/draw_walker.cpp:1978-1986: `before_frame` true when only COPY_FRAME passes precede, then `display_pictures_.latest()` and `draw_shown_frame_pass`.
  - Why: this reproduces the GameCube/Wii EFB keeping last frame's pixels; with interpolation, `latest()` can be a generated picture rather than the last game frame, so the copy differs from run to run and between display rates. Fix: let the game ask for "the last game frame" explicitly (a pass flag), and have the walker keep the last real picture for it, not the last shown one.
  - Confidence: the behaviour is verified in code; whether `latest()` can be a generated picture at that point was not traced through display_pictures (needs a read of DisplayPictures::latest or a run at 240 Hz with the camera cover, comparing the copied picture with the last real one).

- The game's timing remains 60 steps a second with frame-counted timers (P-003 is where that leaks into wall-clock speed). That is gameplay behaviour, left for the owner per project rules.

### Area 9: code health

Largest engine files (lines): runtime/src/render/renderer_d3d12.cpp 7501, native/host/host_main.cpp 6578, runtime/src/ui/native_settings.cpp 6486, runtime/src/render/vk/renderer_vk_frame.cpp 3951, runtime/src/render/core/rt_walk.cpp 3884, vk/renderer_vk.cpp 3158, render_core.cpp 2898, controller_hid.cpp 2741, platform/sdl/host_window_sdl.cpp 2527. Environment-variable switches are nearly gone (12 getenv sites in runtime/src, native/host and platform, all OS paths or SDL hints, none a feature switch). No empty `catch (...) {}` in engine files; `catch (...)` blocks that do something: pipelines.cpp 13, save_transfer.cpp 12, render_core.cpp 11.

- P-022 (maintainability) The same per-renderer logic is written four times.
  - Evidence: GPU pass EMA: vk/renderer_vk.cpp:2662-2668, mtl/renderer_mtl.mm:600-608, d3d11/renderer_d3d11.cpp:556-559 (and D3D12). `present_sync_interval`: d3d11/renderer_d3d11.cpp:621-638 says "As renderer_d3d12.cpp present_sync_interval". Capture readbacks (RGBA to RGB and file write): renderer_d3d12.cpp:6902-6940, vk/renderer_vk_frame.cpp:3804-3910, mtl/renderer_mtl_draw.mm:645-732, d3d11/renderer_d3d11_scene.cpp:67-91. Every backend's `begin_frame` repeats the same opening (poll config file, follow window, dynamic resolution) (renderer_d3d12.cpp:3610, vk/renderer_vk.cpp:1951, d3d11:522-526, mtl:562).
  - Why: a fix in one copy misses the others (P-011 is an example: only D3D11 lacks the 100 ms cache). Fix: move the backend-independent parts (pass timing averages, sync interval choice, capture encoding, begin-frame housekeeping) into RenderCore or renderer_common, leaving the backends the API calls.
  - Confidence: verified in code.

- P-023 is in Area 10. P-001 (dead `frame_rate_decouple` branch) and `resolve_frame_timing_config_from_environment` (render_config.cpp:498-500, reads no environment, returns defaults: a misleading name) are the dead or stale paths found.

### Area 10: tests and gates

What exists: 70+ ctest unit tests registered in the root CMakeLists.txt (`galaxy_test`, :633-646, list :649-790: atomic file, update, saves, save format and transfer, launch args, settings models and pages, controls and input, audio mixer, frame phase, precise wait, frame interpolation, present slots, pairing, pipeline cache file, render config, texture pack, post process, render core host, crash report, diagnostics report, benchmark stats, and more). tools/verify holds 67 scripts (tours, determinism, audio, look, save, perf, gpulost, ...), run by hand. native/tests has 4 test files.

- P-023 (untested) Nothing runs the tests automatically: the release workflow only builds, and there is no other CI for the game.
  - Evidence: .github/workflows has release.yml and website.yml only. release.yml jobs (lines 37-545) build the Windows hosts, game module, Setup, Linux package, Mac app and Android app and run one check, the LumaKit pack compare (:213-260); no `ctest`, no tools/verify script, no launch of any built program. website.yml tests only the website.
  - Why: a release can ship with a failing unit test or a host that does not start; the Linux and Mac packages are never started anywhere in CI. Fix: a push/PR workflow that builds the Windows tests and runs `ctest`, and in release.yml a smoke step per package that starts the program headless with `--diagnostics` for N frames (the deterministic mode exists, host_main.cpp:3268-3272) and fails on a crash or a missing first frame.
  - Confidence: verified in code (workflow contents).

- P-024 (untested, platform gap) Unit tests build only on Windows; the POSIX and Android branches of tested code are never tested.
  - Evidence: CMakeLists.txt:14-16 `if(NOT WIN32) message(FATAL_ERROR "This build targets Windows x64; other platforms build from platform/sdl.")`; platform/sdl/CMakeLists.txt has no `galaxy_test`/`enable_testing` (grep: none). Code with separate POSIX branches under test only on Windows: runtime/src/atomic_file.cpp:147-270, present_slots (Android's display-timing path), platform/sdl/display_clock.cpp (no test at all), platform/sdl/crash_log.cpp (none).
  - Why: the POSIX save writer, Android present-slot placement and the Choreographer clock run on two thirds of the platforms with no test. Fix: build the platform-neutral tests from platform/sdl as well and run them on Linux in CI; add host-side tests for display_clock's tolerance logic (P-003).
  - Confidence: verified in code.

### RT: what to remove or switch off to take it out of main

Build switch: `NEBULA_RAY_TRACING` (cmake/rt_shaders.cmake:16, default OFF) gates only the tracer's shaders (CMakeLists.txt:428-433, 539-541, 922-926). All RT C++ is compiled into every renderer build regardless:
- Sources: runtime/src/render/rt/ (rt_backend_d3d12.cpp, rt_backend_fake.cpp/.h, rt_geometry.cpp/.h, rt_scene.cpp/.h, rt_shaders.cpp, every rt_*.hlsl/.hlsli/.inc, shaders.tsv), runtime/src/render/core/rt_walk.cpp, rt_bounce.cpp, rt_pass_indirect.cpp, rt_pass_reflect.cpp, rt_pass_transmit.cpp, rt_pass_volume.cpp, rt_dev.h, runtime/src/render/vk/rt_backend_vk.cpp, d3d11/rt_backend_d3d11.cpp, mtl/rt_backend_mtl.cpp, runtime/src/render/rt_materials.cpp, rt_caps.cpp (+ their tests rt_geometry_tests, rt_materials_tests, rt_caps_tests); headers runtime/include/galaxy/render/rt/*, rt_caps.h, rt_materials.h, rt_types.h; cmake/rt_shaders.cmake; tools/dev/compile_rt_shaders.py; tools/verify/rt_inputs.sh. CMake source lists: CMakeLists.txt:506-511, 522-527, 773-774, 862-867, 882-887, 1026-1031 (and the matching platform/sdl lists).
- Embedded in shared code: about 450 lines of `rt_*` members and declarations in RenderCore (render_core.h); `rt_backend_` and the `rt_*` calls in render_core.cpp (initialize :218 `rt_start()`, restart_device :1654/:1675, begin_walk_frame `rt_follow_setting`, `rt_frame`); draw_walker.cpp walk_list :1863 `rt_pick_scene`, walk_scene_begin :1517-1524, walk_opaque_end :1562-1568, walk_scene_end :1585-1592 (`rt_program_signal`, `rt_reactive`, `rt_light_end`, the `traced_finish` argument to `end_wide_scene`), walk_pass :1919-1921 (TRACED_DRAW), plan_early_probes :1768; the renderer interface's `rt_*` methods and `RtCommands` in all four renderer headers (stubs in D3D11 and Metal: renderer_d3d11.h:180-192, renderer_mtl.h:191-203); the item prepass render passes in vk/renderer_vk.cpp:804-852 ("ray tracing prepass render pass"); Pipelines' traced forms and prepass aux (core/pipelines.cpp:290-326, 352-381; `program_traced`); `wide_scene_` choice taking `ray_tracing_enabled` (render_core.cpp:2219); render_config.cpp RT keys (:155-160, 232-249, 341-345); the settings rows (RAY TRACING HARDWARE is already hidden, native_settings.cpp:309).
- Frame list (game side and the shared header): native/include/nebula_frame_list.h world_lights, shadow_drops, point_lights, light_set_owners, clip_volumes, NEBULA_PASS_TRACED_DRAW; native/petari/src/Game/Render/FrameList.cpp :235-238, 328-331, 479-487, 740-765, 784-819, 1309-1314 (`tracedOnly`); game emitters: Game/LiveActor/ShadowVolumeDrawer.cpp:95-116, Game/Util/GameLightSignal.cpp (frame(), :190-279), Game/Map/LightPointCtrl.cpp:70, Game/MapObj/StarPiece.cpp:338, Game/MapObj/ClipArea.cpp:50, Game/Map/LightFunction.cpp:211, and `Frame::setStage`/`setStageLight`.

What runs in a normal frame today with ray tracing off:
- Game thread, every frame: the RT-only frame data is built unconditionally: every shadow drawer computes and appends a shadow drop (ShadowVolumeDrawer.cpp:98-116), GameLightSignal::frame appends the stage, planet, Star Core and sun world lights and the stage name (:190-279), point lights from LightPointCtrl and star pieces, clip volumes, and every light-set change appends owner lights (FrameList.cpp:756-763). None of these calls checks whether tracing is on.
- Render thread, every walk: `rt_follow_setting(render)` (render_core.cpp:2217), and in walk_scene_end `rt_program_signal()` (returns at once unless a capture was asked, rt_bounce.cpp:575-578), `rt_reactive()` (returns, rt_walk.cpp:2716), `rt_light_end()` (a flag check, :2746-2751). `rt_backend_.started()` is false so rt_pick_scene, rt_scene, rt_prepass, rt_blended, rt_frame are skipped. `rt_start()` at initialize returns at its first check when `ray_tracing_enabled` is off or the device is not capable (rt_walk.cpp:279-283).
- Memory: RenderCore carries the rt_* members (images, vectors) whether or not tracing runs; not measured.

## Things that look wrong but were checked and found correct

- `get_render_config()` returns a thread_local copy refreshed only when a generation counter moves: no lock per call (render_config.cpp, read 414-720).
- PreciseWait's clock units: QPC ticks on Windows, ns elsewhere (`qpc_frequency()` is 1e9 off Windows, platform_time.h), consistent with every caller read.
- Frame interpolation's stall handling skips missed slots instead of bursting (frame_interpolation.cpp, read whole).
- Vulkan keeps one "rendered" semaphore per swapchain image, not per frame in flight (the known validation trap is avoided) (vk/renderer_vk.cpp swapchain creation, read 960-1440).
- The D3D12 and D3D11 latency waits and fence waits are bounded (100 ms, 30 s) and a timeout is reported, not a hang (renderer_d3d12.cpp:95; renderer_d3d11.cpp:669-674).
- Vulkan's GPU pass timestamp pool is large enough for both frame slots and the upload marks: `kFramesInFlight * (kGpuPassSlots + 2)` covers the highest index `kFramesInFlight*kGpuPassSlots + 2*(kFramesInFlight-1) + 1` (vk/renderer_vk.cpp:2054-2060, 2623-2627); and timestamps are only written at pass boundaries, never splitting a pass (:2587-2592).
- Vulkan and D3D12 `video_memory` are cached for 100 ms (vk_context.cpp:1900-1903, renderer_d3d12.cpp:1880-1885) (only D3D11 is not: P-011).
- Device-loss text matching covers DXGI codes, the Vulkan loss and D3D11's own messages ("device was lost" matches renderer_d3d11.cpp:487 and :695) (crash_report.cpp:48-53); only Metal is missed (P-008).
- Save writes: temp file, fsync/FlushFileBuffers, rename/MoveFileEx with write-through, folder fsync on POSIX; the old file is never truncated in place (runtime/src/atomic_file.cpp); saves run on one FIFO worker, statuses published with a release store (host_main.cpp:3406-3479).
- The updater installs only a package whose manifest signature verifies and whose SHA-256 and size match it; HTTPS only (update.cpp:358, 875-912, 958-966).
- Diagnostics upload is opt-in per press, HTTPS only, sealed to a build public key, one send per result (`.sent` marker), and strips user and computer names, paths, e-mails and IPv4 addresses from log lines (diag_report.cpp:648-705, 731-788).
- Android ProGuard keep rules match every Java method native code calls by name (Area 6 list).
- `SDL_GetAndroidActivity()`'s local reference is deleted after each call (host_window_sdl.cpp:298, 318).
- The title waits for the start's pipeline set only on a cold or mismatched cache; a warm D3D12/Vulkan cache lets play start at once (host_main.cpp:3264-3274, pipelines.cpp:739-768).
- Pipeline workers run below the frame threads' priority and a worker that throws refuses only that pipeline (pipelines.cpp:446-528).
- The planners that keep the scene pass unbroken on tilers (early targets, early probes, late depth clears) only move passes whose inputs and outputs they have checked (draw_walker.cpp:1664-1858).
- RuntimeSettings, copied under a shared lock on the audio thread each audio frame (native_audio.cpp:217), is plain data (no strings or vectors), so the copy does not allocate.
- The frame list is handed to the renderer by pointer with no copy (FrameList.cpp; render_core.cpp producer :551-640); only the `released` flag's ordering is at fault (P-005).

## Round 2

Corrections to Round 1, made at the start of Round 2:
- P-019 was rewritten ("corrected in Round 2"). The performance hint exists, in runtime/src/perf_hint.cpp; the Round 1 grep missed it.
- Every other Round 1 claim that something was absent was re-run with `git grep` over the whole tree, with the same patterns:
  - FrameList fences (P-005): still none.
  - Vulkan transient, lazy and STORE_OP_NONE (P-006): still none. The only `STORE_OP_DONT_CARE` hits are `stencilStoreOp` on colour attachments, plus post_process_vk.cpp:72.
  - Console API names (Area 8): still none.
  - `sigaltstack` (P-017): only luma_guard.cpp. P-017 is sharpened in place.
  - Apple signal and crash handlers (P-015): none in platform/apple.
  - Linux log file (P-016): only Windows and Android open one.
  - Window occlusion on Vulkan and Metal (P-009): `occluded()` returns false on both; no `SDL_EVENT_WINDOW_OCCLUDED` handling anywhere.
  - Tests for display_clock and crash_log, and `enable_testing` in platform/sdl (P-024): still none.
- No other finding rested on a miss.

### Area 2 and 3, Direct3D 12 (Round 2)

Read for this section:
- runtime/src/render/renderer_d3d12.cpp, lines 1-1160, 1449-1760, 3381-3605, 5167-6110, 6268-6750 and 7122-7336. This adds to Round 1's 1870-1894, 3169-3211, 3590-3930 and 4160-4720.
- runtime/src/render/game_bloom.cpp, lines 215-634.
- runtime/src/render/post_process.cpp, lines 545-780 and 937-1059.

Not read in renderer_d3d12.cpp:
- 1160-1449: root signatures, the sampler heap, conversion pipeline creation.
- 1968-2671: initialize.
- 2671-3150: texture heap install, video memory log, shutdown.
- 3930-4160: display rate, HDR swap-chain state.
- 4720-5167: the temporal upscaler's preparation and record.
- 6110-6268: RT hooks.
- 6750-7122: screen probes and debug captures.
- 7336-7501: debug readbacks.

None of these is on the per-frame path of a default frame, except the temporal upscaler when an upscaler is chosen.

Also not read: scene_camera.cpp, ambient_occlusion.cpp and the upscaler files. Only their draw and dispatch counts were taken, by grep.

How D3D12 records a frame:
- One direct command list per frame slot.
- A parallel-recorded pass closes it, and the frame continues in a second list (`segment_lists_`, :3494-3520).
- All lists go in one `ExecuteCommandLists` (:4540-4555).
- No `BeginRenderPass` is used anywhere (grep), so the D3D12 driver gets no load/store intent at all. The only intent given is `DiscardResource` on newly placed frame-pass targets (:1563, :6664).
- Resources are released at the right time:
  - Replaced targets go to `retired_targets_[frame_slot_]` (for example :1544, :5195, :6641), freed when that slot's fence passes.
  - Released scene pictures carry a `free_from` frame (:1696-1701).
  - The bloom and post targets keep a `retired_` list with a frame number (game_bloom.cpp:241-245).

Exceptions to the retire pattern are P-028 below and the descriptor heaps (P-029).

#### One typical in-game frame (the shipped defaults: no MSAA, AA off, bloom 100, no AO, SDR, no upscaler, HUD at the render size), D3D12

The draws and copies, in walk order, and the game call that causes each:
1. Each target pass (a game render-to-texture, from the frame list's target-pass records):
   - It draws into its own key's target set (begin_target_pass :5421-5450). No copy.
   - The picture is published as a view (:5452-5469).
2. The scene draws into scene_color_ and scene_depth_.
3. Each COPY_FRAME (the game's copy of the frame to a texture):
   - With the frame whole, the scene target itself is lent as the texture (open_frame_for_pass :1643-1661). No copy.
   - With dynamic resolution's rectangle, or a non-display channel layout, it is one full-size conversion draw (draw_frame_pass :6612-6709).
   - A depth read is one full-size depth-to-R32F draw (:1515-1604).
   - A copy before the frame's first draw (P-021) costs one `CopyResource` of the shown picture into the scene target, plus a conversion draw (draw_shown_frame_pass :6711-6745).
4. The game's bloom (the NEBULA_PASS_BLOOM_* records):
   - HOLD: a texture swap plus a clear, no copy (:6325-6334).
   - NORMAL:
     - Up to 9 compute dispatches on reduced images. The first dispatch reads the full frame (game_bloom.cpp:445-556).
     - Then one full-size composite draw (:566).
   - SIMPLE: a swap (:6374-6379), up to 5 dispatches, then one composite draw.
5. Scene AA is off, so there is nothing to record. With SMAA:
   - Two full-size clears and a stencil clear.
   - Three full-size draws (post_process.cpp:736-753).
   - The scene is traded, not copied, when `can_trade_scene()` holds (:1606-1609).
6. The output scene:
   - Only when the HUD is drawn at the output size, dynamic resolution is active, or an upscaler is on.
   - One full-output conversion draw (:6430-6442).
7. The HUD draws.
8. walk_show is a swap (take_frame_picture :1683-1690). No copy.
9. Present:
   - One full-window draw into the back buffer (post_process.cpp:937-1057, a copy, resample or EASU+RCAS). With FSR 1 and sharpening, EASU and RCAS run as two draws via the `scaled_` intermediate, unless both axes enlarge (:978-992).
   - A full back-buffer clear when the picture has borders (:7199-7202).
   - DLSS-G (DLSS frame generation) adds a HUD-less full copy and a draw (P-027 note).

Full-target copies in this default frame: none. Full-size draws: the scene, the bloom composite and the present draw, plus one draw per COPY_FRAME that is not whole.

With MSAA, each consumer of the colour first resolves it with `ResolveSubresource` (:1034-1046):
- scene AA;
- bloom;
- frame reads;
- the 2D layer;
- the display copy.

A depth read adds a full-screen depth-resolve draw (:1047-1090). P-025 is the extra copy MSAA causes.

#### Load/store and merge list, D3D12 (extends Round 1's Vulkan list)

D3D12 has no render passes, so "load/store" here means discards, clears and copies.

Changes that keep every pixel the same:
- (a) MSAA plus bloom or AA: resolve once into the consumer's texture, or let it read the resolved target, instead of resolving and then copying (P-025).
- (b) The 2D layer start without MSAA: draw the two identical conversion draws as one draw into two render targets (P-027).
- (c) When MSAA is on, discard the multisampled colour after its last resolve, and discard depth after the frame's last depth use. Use `DiscardResource`, which matters only on tile-based D3D12 drivers (Qualcomm on Windows on ARM). release.yml builds no Windows ARM64 package (grep finds only macOS arm64 and Android aarch64), so this matters only once one ships.
- (d) draw_shown_frame_pass (:6711-6745): the conversion draw could sample `shown.texture` directly instead of `CopyResource` into the scene target first. However, the copy also makes the scene target hold the last picture for the draws that follow, which is P-021's console-era behaviour. Only after P-021 is decided can this become one draw.
- (e) Bloom NORMAL: the "box across" and "box down + threshold" dispatches (game_bloom.cpp:464-494) are separable halves of one 4x4 box and could be one dispatch using group-shared memory. This changes no pixel only if the filter's edge handling is kept exactly; it needs a capture comparison.

- P-025 (performance, D3D12 with MSAA) With MSAA, bloom and scene AA resolve the scene and then copy the resolved picture again, a second full-frame write and read that is not needed.
  - Evidence:
    - The bloom start calls `resolve_scene(false, true)` (renderer_d3d12.cpp:6312), which resolves into `scene_color_` (:1039-1046).
    - Then, with MSAA, HOLD calls `game_bloom_.keep`, which calls `copy_frame` (game_bloom.cpp:351-357, 215-239). SIMPLE takes the trade only when `samples <= 1` (:6374) and otherwise calls `copy_frame` into `source_` (game_bloom.cpp:410).
    - Scene AA calls `resolve_scene(false, true)` (:5715). The trade is refused with MSAA (`can_trade_scene`: `scene_msaa_.samples <= 1u`, :1607), so post_process.cpp:700-721 copies `color` (the resolved target) into `antialiased_`.
    - In each case the composite or last pass writes the multisampled target (`in.rtv` / `color_rtv` = the MSAA RTV, :6316, :5728), not `scene_color_`. So nothing writes `scene_color_` between the copy and its last read.
  - Why: at 4K with RGBA16F (the HDR scene), each copy is 66 MB of traffic per frame, for bandwidth with no pixel effect.
  - Fix:
    - SIMPLE bloom and AA: read `scene_color_` directly. Transition it to a shader-read state and back.
    - HOLD: resolve straight into `keep_` with `ResolveSubresource(keep_, msaa.color)`, which needs no copy.
  - Confidence: verified in code (the copy happens, and nothing writes the source in between). That the result is pixel-identical is suspected, needs a run: pictures kept through the host extension's picture capture (render_core.cpp:123-135, the way tools/verify/interp_real.sh takes them) at MSAA 4x with SMAA and bloom, before and after, compared byte for byte.

- P-026 (performance, D3D12) The present pass recreates its shader views every frame, and the post-process and bloom helpers build every descriptor table fresh in the frame's ring. This is designed (a ring), and it is listed here only because it is per-frame CPU work that could be done once.
  - Evidence:
    - `PostProcessor::draw` writes 3 SRVs per draw, creating the null ones as well (post_process.cpp:550-573).
    - `GameBloom::table` writes 6 descriptors per dispatch (game_bloom.cpp:588-611), so NORMAL bloom does 10 tables, which is 60 `Create*View` calls per frame.
    - The no-post-process present path writes one SRV (:7240-7246).
  - Why: each `CreateShaderResourceView`/`CreateUnorderedAccessView` is a driver call of roughly 0.1-0.5 µs. That is small (tens of µs a frame), but the targets are persistent, so their views could be persistent descriptors (the renderer already has `allocate_persistent_descriptors`) and each pass would need only a table offset.
  - Fix: give the bloom, post and AO targets persistent tables, written when a target is (re)made in `ensure_target`.
  - Confidence: verified in code; the cost is suspected, needs a run: PIX CPU timing of `present()` and `game_bloom()` on the PC.

- P-027 (performance, D3D12 HDR output) Opening the HDR 2D layer takes two full-size conversion draws from the same source, and closing it takes a third, every frame the HUD is drawn with HDR output.
  - Evidence:
    - `begin_layer_2d` (renderer_d3d12.cpp:5989-6058) resolves, then calls `record_layer_draw` twice with the same `source` table: once into `layer2d_set_` (samples) and once into `layer2d_base_` (1 sample) (:6047-6049).
    - `end_layer_2d` resolves the layer and draws the merge (:6060-6096).
    - It is called from draw_walker.cpp:1609 when the walk reaches 2D draws.
  - Why: three full-size passes plus up to two resolves per frame for the HUD on HDR output. Without MSAA both start draws have the same sample count and format (`kSceneColorFormat`), so they can be one draw with two render targets.
  - Fix:
    - Without MSAA, one draw with two RTVs (a pipeline with `NumRenderTargets = 2`).
    - With MSAA, keep two draws.
  - Confidence: verified in code; the saving is suspected, needs a run: `--diagnostics gpu-passes` with HDR output on, reading the "frame read" pass time before and after.
  - Note (DLSS-G): `take_fg_hudless_scene` adds a full `CopyResource` per frame (:5573-5577), `copy_fg_hudless_display` adds a full draw (:5582-5618), and `present_fg_hudless` adds a full clear and a draw (:5622-5707). That is the cost of DLSS-G's HUD-less input and is expected.

- P-028 (performance, D3D12) Several size changes drain the whole GPU in the middle of a frame (`wait_for_gpu`), while other size changes in the same renderer retire the old resource instead.
  - Evidence:
    - Drains:
      - `create_scene_set` waits whenever the set already has a colour target (:5299-5301). It is called by begin_target_pass when a target key's size changes (:5430-5432).
      - `ensure_wide_set` (:5748-5751).
      - The DLSS-G HUD-less targets (:5563-5565, :5591-5593, :5637-5639).
    - Retires instead: `ensure_output_scene` (:5192-5197), `publish_scene_depth_copy` (:1543-1545), `draw_frame_pass` (:6640-6643) and `begin_layer_2d` (:6008-6010) push the old resource to `retired_targets_[frame_slot_]`.
  - Why: `wait_for_gpu` signals and waits on the queue (:757-768), so the render thread stalls for every frame in flight (up to `kFramesInFlight` frames of GPU time) before it can continue recording. A game render-to-texture that changes size, for example a target pass sized by the game per scene, therefore causes a hitch.
  - Fix: retire the old targets to `retired_targets_` as the other paths do. The RTV/DSV heaps hold CPU descriptors that are copied at record time (:849-850 says so), so rewriting them needs no wait.
  - Confidence: verified in code (the waits and the callers). How often a target key changes size in play is suspected, needs a run: add a log line in `create_scene_set` when `set.color` exists and play the first three galaxies.

- P-029 (memory, D3D12) A shader-visible descriptor heap that is replaced when it grows is kept until the renderer is re-initialised.
  - Evidence:
    - `grow_persistent`, `grow_frames` and `resize_textures` push the old heap into `replaced_` (:536, :601). `resize_textures` returns it to the caller instead.
    - `replaced_.clear()` appears only at :382, inside `DescriptorRing::initialize`.
    - `heap_bytes` counts the replaced heaps (:438-446).
  - Why: the replaced heaps are needed only until the frames that recorded against them finish (`kFramesInFlight` frames). Doubling bounds the waste at the size of the live heap: up to about 32 MB of descriptor memory at a million descriptors. It also shows up in the video-memory report as in use.
  - Fix: store the frame index with each replaced heap and release it once that frame's fence has passed, the same way as `retired_targets_`.
  - Confidence: verified in code.

- Checked and correct (D3D12, Round 2):
  - The descriptor ring's growth copies the frame's tables made so far into the new heap and keeps the old heap for commands already recorded (:507-548, :557-616).
  - The texture heap's growth flushes before the swap (:671-677).
  - Picture views are retired through the texture store and not freed while in use (:5523-5524, :1672-1674).
  - A frame-pass allocation failure is told apart from device removal (:6669-6688).
  - The swap of scene sets swaps every field that belongs with the target (:5367-5419).

#### One typical in-game frame, Vulkan (same defaults)

Read for this: renderer_vk_frame.cpp lines 456-575, 1422-1621, 2298-2376 and 3254-3365, plus Round 1's ranges, and a grep census of the copies and the waits for an idle device. renderer_vk_frame.cpp is NOT read completely; the parts not read are named in the coverage table.

Vulkan follows the same walk with the same swaps (`swap_scene_color` :3323-3363, `open_frame_for_pass` :3254-3291 lends the scene image itself). The difference is that each interruption ends and reopens the scene render pass instance.

A default frame makes these scene-pass instances:
1. One per target pass.
2. The scene pass.
3. Ended by bloom HOLD's swap (:2324). It reopens to clear the colour and load the depth for the glow models (`clear_scene`).
4. Ended by bloom NORMAL's compute (:2308). The composite reopens it with the colour discarded and the depth loaded (:2364).
5. It continues through the HUD and is ended by walk_show's swap.
6. Each COPY_FRAME lend ends it again (:3272).
7. The present pass.

Every reopening stores and reloads depth and stencil (P-006), so on a tiler each bloom costs about two full depth round trips through memory. The game's bloom design causes this: it draws the glow models over the scene's depth. The only remedy that keeps every pixel is to clear depth and stencil to don't-care once the last glow draw is done, and not to store depth at the HUD pass's end, which is P-006's fix.

Full-target copies in a default frame: none. `vkCmdCopyImage` appears only for:
- the DLSS-G HUD-less picture (:1504);
- the RT result (:2035);
- P-021's copy of the shown picture (:3525);
- with MSAA, bloom and AA copying the resolved scene (game_bloom_vk.cpp:287, post_process_vk.cpp:663). This is P-025's pattern on Vulkan as well.

- P-030 (performance, Vulkan and Metal) The frame-generation and FSR 3 paths call `vkDeviceWaitIdle` from the render thread in the middle of a frame whenever an output or render size changes. This is P-028 on Vulkan.
  - Evidence: renderer_vk_frame.cpp:1457 (FSR 3 frame generation remade), :1484, :1524 and :1577 (DLSS-G HUD-less images). Other size changes on Vulkan retire through `retire()` (:2376).
  - Why: a hitch of all frames in flight on every resize while frame generation is on.
  - Fix: retire the old images as the other paths do.
  - Confidence: verified in code.

### Area 3, Metal and Direct3D 11 (Round 2)

Read for this section:
- renderer_mtl_scene.mm, lines 360-470;
- renderer_mtl.mm, lines 220-330, 690-790 and 1150-1200 (Round 1: 600-630);
- platform/apple/display_clock_apple.mm, completely (252 lines);
- renderer_d3d11.cpp, lines 260-300 (Round 1: 480-720);
- a grep census of every Metal load/store action, drawable call and storage mode.

Not read:
- the rest of renderer_mtl.mm (1244);
- renderer_mtl_scene.mm (1263) outside 360-470;
- renderer_mtl_draw.mm (1154);
- renderer_mtl_fx.mm (263);
- mtl_context.mm outside 270-376;
- renderer_d3d11_scene.cpp (1371);
- the rest of renderer_d3d11.cpp;
- texture_store.cpp (1526), which is not read at all in Round 2.

- P-031 (performance, Apple silicon and iOS) The Metal scene pass always stores depth and stencil, MSAA stores the multisampled colour as well as resolving it, and no target is memoryless. This is P-006 on Apple's tile-based GPUs.
  - Evidence:
    - renderer_mtl_scene.mm:387 `color.storeAction = MTLStoreActionStoreAndMultisampleResolve` with MSAA.
    - :399 `depthAttachment.storeAction = MTLStoreActionStore`; :403 `stencilAttachment.storeAction = MTLStoreActionStore`.
    - `git grep -n Memoryless` over runtime/src/render/mtl and platform/apple finds nothing.
  - Why: every Apple GPU is a tiler. Each scene-pass end writes depth, stencil and N colour samples to memory even when nothing reads them, and the bloom reopenings (see the Vulkan list) reload them.
  - Fix:
    - `MTLStoreActionMultisampleResolve` on the last pass that touches the MSAA colour.
    - `MTLStoreActionDontCare` for depth and stencil after their last reader. The walker already knows this for the late depth clears.
    - The MSAA colour and depth as `MTLStorageModeMemoryless` where `supportsFamily(MTLGPUFamilyApple1)`, which is every Apple GPU.
  - Confidence: verified in code. The saving is suspected, needs a run: Xcode's Metal System Trace or GPU frame capture on an Apple-silicon Mac, reading "Memory bandwidth / Store" per pass before and after.

- P-032 (platform gap) Neither Metal nor Direct3D 11 offers HDR output.
  - Evidence: `RendererMtl::wants_hdr()` returns `false` (renderer_mtl.mm:226-228), the layer is sRGB 10-bit or 8-bit (:286-295), and no EDR call exists (grep `wantsExtendedDynamicRange|EDR`: none). `RendererD3D11::wants_hdr()` returns `false` (renderer_d3d11.cpp:266-268). The settings page offers HDR when the display has it (native_settings.cpp:2075), whatever the renderer.
  - This settles P-009's open question. The `begin_layer_2d` no-op on both renderers is harmless today, because neither has a float HDR scene.
  - Why: a Mac with an XDR display gets no HDR. On Windows, the D3D11 fallback silently ignores an HDR choice the page still offers.
  - Fix: hide or disable the HDR row when the active renderer cannot output HDR, and say which renderer can. EDR on macOS (an RGBA16F layer, `kCGColorSpaceExtendedLinearSRGB`, `wantsExtendedDynamicRangeContent`) is the native route if HDR on Mac is wanted.
  - Confidence: the renderer side is verified in code. Whether the page hides the row for D3D11 or Metal was not traced through `get_display_hdr()` on those renderers. To settle it: start with `--renderer d3d11` on an HDR monitor and look at the Display page.

- P-033 (wrong behaviour, macOS and iOS) The Apple display clock locks the game to the display under the same loose 3 % tolerance as Android.
  - Evidence: platform/apple/display_clock_apple.mm:182-186 `std::abs(vsyncs - std::round(vsyncs)) > 0.03`, then `mach_wait_until` on the vsync grid (:189-195). This extends P-003 to Apple.
  - Why and fix: as P-003.
  - Confidence: verified in code.

- P-034 (performance, macOS and iOS) The Apple display link is never stopped.
  - On iOS it asks the display for its fastest rate, so a ProMotion display is held at 120 Hz while the game makes 60 pictures a second.
  - Evidence:
    - macOS: `CVDisplayLinkStart` (:108) with no `CVDisplayLinkStop` anywhere (grep). `CVDisplayLink` is also deprecated from macOS 14 (Apple's replacement is `NSView.displayLink`).
    - iOS: `preferredFrameRateRange = CAFrameRateRangeMake(fastest, fastest, fastest)` (:149-154), and the link is never paused or invalidated.
  - Why:
    - On macOS a thread wakes at the display's rate while the game is hidden or paused (P-004's Apple twin).
    - On iOS, asking for minimum = preferred = maximum = 120 forbids the system from lowering the rate. This costs battery when interpolation is off and the game shows 60 pictures.
  - Fix:
    - Stop and start the link with the app's active state.
    - On iOS, ask for 120 only when interpolation will fill it; otherwise ask for 60 with a range up to 120.
    - Move macOS 14+ to `NSView.displayLink`.
  - Confidence: verified in code. The battery cost is suspected, needs a run: an iPad Pro or iPhone Pro, 10 minutes in one galaxy with interpolation off, with Xcode's Energy gauge and `CADisplayLink` rate logged, with the range as built against 60-120.

What Metal behaviour can only have been compile-tested, as far as the tree shows:
- Code comments cite measurements on Intel Macs: a 2017 MacBook Pro (renderer_mtl.mm:1187-1188) and a Radeon Pro 560 and Intel HD 630 (:241-243). A 60 Hz MacBook Pro through MoltenVK is cited at display_clock_apple.mm:221-222.
- No comment, log, test or CI step in the tree records a run on Apple silicon or on iOS or iPadOS.
- release.yml builds `macos-arm64` (:446-460) and runs nothing.
- So the following are unexercised as far as the tree shows:
  - Apple-GPU-only branches (`caps.apple_gpu`; MetalFX temporal on Apple GPUs, metalfx_mtl.mm);
  - the whole iOS host (CADisplayLink path, BGRA8 layer, `set_present_waits_for_refresh(true)`, touch);
  - Apple-silicon timing (the Mach timebase 125:3 path, display_clock_apple.mm:47-56).
- They may have been run outside the tree. Nothing inside it says so.
- Also untested: P-008, the Metal device-loss path. It can only be reached by a GPU fault, and none was ever forced.

- P-035 (untested, macOS) The released Mac app runs only on Apple silicon, but the only Mac runs the tree records are on Intel Macs. The Intel-only code paths ship in no package.
  - Evidence:
    - release.yml:446 builds `build_macos.sh ... -a arm64`. build_macos.sh:11 makes a universal app only when two processors are named (:112-116 `lipo`), and :178-179 shows the arm64-only case.
    - The code's own measurements are on Intel Macs (renderer_mtl.mm:241-243, :1187-1188).
    - `choose_first_start_scale` (renderer_mtl.mm:236-268) acts only when `!caps.apple_gpu && caps.unified_memory`, meaning an Intel Mac using its integrated GPU. That is unreachable in an arm64 build.
  - Why: the shipped hardware has no recorded run, and the Intel tuning ships to nobody.
  - Fix: one of two.
    - Build `-a "x86_64 arm64"` (universal) in release.yml and keep the Intel paths.
    - Or drop the Intel paths, and run the arm64 package on an Apple-silicon Mac before release: the benchmark (`--benchmark`, host_main.cpp:5945), plus a forced GPU timeout for P-008.
  - Confidence: verified in code (the build flag and the branch condition).

### Area 7, platform hosts (Round 2)

Read:
- completely: platform/sdl/audio_device_sdl.cpp, platform/apple/audio_device_apple.mm, platform/apple/display_clock_apple.mm, platform/sdl/display_clock.cpp and crash_log.cpp (Round 1);
- platform/apple/metal.cmake, lines 80-120;
- platform/apple/CMakeLists.txt, lines 1-30, 76-97 and 150-203;
- platform/sdl/CMakeLists.txt, lines 225-330;
- host_window_sdl.cpp, lines 286-345, 1775-2160 and the stub census;
- host_window_apple.mm, lines 1-40, 1486-1575 and the stub census;
- native_game_host_sdl.cpp, lines 590-611 and the stub census.

Not read completely:
- host_window_sdl.cpp (2527);
- host_window_apple.mm (1806);
- native_game_host_sdl.cpp (611);
- setup_apple.mm (336);
- apple_host.mm (208);
- thread_sampler.cpp (219);
- sdl_host.h (162);
- platform/linux/build_linux.sh;
- the Linux .desktop and .xml files.

luma_system_* and mod_import_jni.cpp are modding files and are not audited.

Which host each system runs:
- Windows: the Win32 host (host_window_win32.cpp).
- Linux and Android: the SDL host (`main`; host_window_sdl.cpp, display_clock.cpp, audio_device_sdl.cpp), with Vulkan.
- macOS: `nebula_native` (Nebula.app), which is AppKit (host_window_apple.mm, audio_device_apple.mm, display_clock_apple.mm, setup_apple.mm) with Metal and no SDL (metal.cmake:97-114). `nebula_metal` (the SDL host plus Metal) and `main` (SDL plus MoltenVK) are development builds (metal.cmake:85-95).
- iOS/iPadOS: only `main` (the SDL UIKit host with MoltenVK) can build for iOS; host_window_apple.mm is AppKit-only (no `TARGET_OS_IPHONE` in it). No iOS package exists:
  - no iOS Info.plist (only Info.macos.plist.in);
  - no iOS build script;
  - no CI job.

  The iOS code paths are compile-only at most:
  - display_clock_apple.mm's CADisplayLink branch;
  - audio_device_apple.mm's RemoteIO branch, which is dead in practice because that file is linked only into the macOS `nebula_native`;
  - host_window_sdl.cpp's `TARGET_OS_IPHONE` restart stubs.

What is implemented, stubbed or unreachable on each system:

| Feature | Linux (SDL) | macOS (AppKit) | Android (SDL + activity) |
|---|---|---|---|
| Display clock locked to vsync | Stubbed: display_clock.cpp:193-209 returns false or 0 for `wait_display_frame`, `display_grid`, `vsync_grid` and `display_refresh_hz` on Linux. The host keeps its own 60 Hz clock, there is no vblank grid for the pacer, and the refresh rate comes from SDL's display mode (host_window_sdl.cpp:1775-1781) | CVDisplayLink, never stopped (P-033, P-034) | Choreographer (P-003, P-004) |
| Monitors and resolutions in settings | one fake monitor "Display" at the window's size (host_window_sdl.cpp:1799-1816) | AppKit list (not read) | the same single entry plus the panel's fastest rate |
| File pickers | SDL portal/Zenity dialogs (:1962-2013). With neither portal nor Zenity, SDL calls back with an error and the pick silently returns nothing (:1952-1958 turns `files == nullptr` into an empty list) | NSOpenPanel (not read) | the activity's document picker (:1847-1937). Mod folders are refused (:1848-1851) |
| Open a folder | `SDL_OpenURL(file://...)` | Finder | no-op, `folders_open()` false (:1939-1943) |
| DLSS, XeSS, vendor installs | stubbed false (:2048-2083) | stubbed (host_window_apple.mm:1486-1525) | stubbed |
| FSR 3 frame generation | when the Vulkan device has the queues (:2032-2046) | none (`UpscalerCaps{}`) | as Linux |
| Restart | `execv` of the running executable (host_main.cpp:4744-4793) | `execv` (same) | activity `restartGame` (P-020) |
| Crash report | stderr only (P-016) | none (P-015) | game.log plus logcat plus tombstone |
| Thread sampler | yes (thread_sampler.cpp) | no-op (host_window_apple.mm:1201) | yes |
| HDR output | Vulkan HDR10 where the surface offers it | none (P-032) | Vulkan HDR10 where offered |
| Window occlusion | not handled (P-009) | not handled (`occluded()` false) | not applicable |
| Settings pad and keyboard hooks | the SDL host feeds the menu itself (host_window_sdl.cpp:1104-1119, 1429-1460); the Windows provider hooks are intentional no-ops (native_game_host_sdl.cpp:604-608) | the same (host_window_apple.mm:599-615, 1103-1115) | the same |

- P-036 (wrong behaviour, Linux) With no XDG desktop portal and no Zenity installed, every file pick (saves, mods, Miis) silently does nothing.
  - Evidence: host_window_sdl.cpp:1952-1959, `picked`: a null `files` (SDL's error) becomes an empty `chosen`, handed to `done` the same as a cancel. `SDL_GetError()` is never read or logged there.
  - Why: on a minimal desktop or a gaming distribution without a portal, the import buttons appear broken, with no log line.
  - Fix: when `files == nullptr`, log `SDL_GetError()`. Then show the in-game message "No file dialog on this system: install xdg-desktop-portal or Zenity", or offer the path-typing fallback the settings UI has for text.
  - Confidence: verified in code; the behaviour on such a system is suspected, needs a run: a Linux VM without xdg-desktop-portal or zenity, pressing Import save.

- P-037 (performance, Linux and Android) The SDL audio callback can allocate on the audio thread.
  - Evidence: platform/sdl/audio_device_sdl.cpp:60, `device->out_.resize(frames * 2)` inside `pull`, with `additional` set by SDL per callback.
  - Why: `std::vector::resize` allocates when a callback asks for more frames than any before it. That happens after a device change to a larger buffer, or on the first callbacks. A heap allocation on the real-time audio thread can block on the allocator's lock and cause a dropout. It happens rarely (only on growth), but it is unbounded.
  - Fix: reserve the device's largest buffer at `start` (SDL_GetAudioDeviceFormat's `device_frames`, times 4 for safety), and render in chunks of that size when SDL asks for more.
  - Confidence: verified in code; the frequency is suspected, needs a run: log the largest `additional` seen over a session that includes a Bluetooth headset connect on Android.

### Area 6, Android (Round 2)

Read:
- GameActivity.java, lines 100-230 and 700-740 (with Round 1's 227-450 and 560-740);
- Updates.kt, lines 295-462 and the grep census of the file;
- host_main.cpp, lines 4320-4364 and 4605-4805 (finish_and_exit);
- host_window_sdl.cpp, lines 286-345 (call_activity).

Not read: the other 23 Kotlin/Java files under platform/android/app/src (5,600 lines), apart from the line counts. The file list is in the coverage table.

P-020 settled as far as reading can settle it. Both sides, in order:
1. The settings menu's restart sets `restart_request` and quits (host_window_sdl.cpp:2109-2113).
2. The main loop's end, on SDL's thread, calls `finish_and_exit(true)` (host_main.cpp:6558-6568).
3. That function waits up to 5 s for saves (:4627-4633) and flushes achievements, stats and the mods' ids (:4635-4645).
4. On Android it calls `start_again` (:4737-4743). `start_again` calls `call_activity("restartGame", kind)` (host_window_sdl.cpp:2124-2126). On SDL's thread that is `call_activity_now`, a direct JNI call (:323-330, :296-319).
5. Java's `restartGame` posts to the UI thread `startActivity(copy of the intent, NEW_TASK | CLEAR_TASK)`, then `Process.killProcess(myPid())` at once, in the same Runnable (GameActivity.java:721-731).
6. The native thread meanwhile sleeps 5 s, then logs "the app did not start the game again" and `_Exit(0)` (host_main.cpp:4741-4742, :4805).

What this establishes:
- The data is safe. Every save and flush is done before the activity is called, so the kill loses nothing that P-018 does not already cover.
- The new activity is asked for in the process name `:game` (manifest :132), which is the process being killed a few microseconds later. Whether the system starts a fresh `:game` process for it, or drops the start with the dying process, is decided inside ActivityManager, not in this code. The widely used pattern for a cold restart (ProcessPhoenix) starts a trampoline activity in a different process for exactly this reason.
- The 5 s native sleep is a correct backstop: if the UI thread never runs the Runnable, the process still ends.

The finding stays "suspected, needs a run":
- Settle it with 50 restarts from the settings menu (Restart and Restart with diagnostics) on the tablet and one other vendor's phone, counting starts that end at the launcher or a black screen. Use `adb logcat -s ActivityManager:I ActivityTaskManager:I` to see "Start proc ... :game" after each kill.
- Fix if it fails: start the restart from the launcher process. Send a broadcast or `PendingIntent` to the main process, or use a tiny trampoline activity there, and kill `:game` only after it answers.

Shrinker rules: unchanged from Round 1. `restartGame(int)` is kept by name. No new native-to-Java name was found in the ranges read in Round 2.

- Checked and correct (Android updater, Updates.kt:295-462):
  - The APK is accepted only if an RSA-SHA256 signature over the whole file verifies against the release key compiled into the app (:399-419).
  - Its package name matches this app and its `versionName` equals the release's and is newer (:312-317). This stops an older signed APK under a newer tag.
  - Downloads are size-capped by the release's own size (:301, :384).
  - Only HTTPS URLs are taken (:172, :178).
  - Install goes through `PackageInstaller` with `USER_ACTION_REQUIRED` (:442-460).
  - Android itself also refuses an update not signed with the installed app's certificate.

### Area 4, the game step's per-frame work (Round 2)

Read completely: native/petari/src/Game/Scene/SceneExecutor.cpp, NameObj/NameObjExecuteHolder.cpp, NameObj/NameObjListExecutor.cpp, NameObj/NameObjCategoryList.cpp, System/DrawBuffer.cpp, System/DrawBufferGroup.cpp, LiveActor/SensorHitChecker.cpp. Read partly: Render/FrameList.cpp (160-400), LiveActor/ActorLightCtrl.cpp (40-135), System/DrawBufferExecuter.cpp (grep of its loops), Effect/ParticleEmitterHolder.cpp (86-101). Not read: GameScene.cpp, SceneFunction.cpp, the remaining System/Nebula*.cpp, the actor classes themselves.

How a step runs, as read: SceneFunction::executeMovementList (SceneExecutor.cpp:23-83) runs about 50 categories in a fixed order, each a flat array of NameObj pointers called through a member pointer (NameObjCategoryList.cpp:11-33). Connect and disconnect requests are deferred to bitsets and run in index order (NameObjExecuteHolder.cpp:224-277), with no allocation once the sets have grown. Removal from a category is O(1), using the stored place (NameObjCategoryList.cpp:54-71, commit 639f35ad6). Its order change (last into the hole) is the original's order, as Petari's std::find + last-into-hole shows. Draw buffers are entered per active executer (DrawBufferGroup.cpp:72-78). The frame list is pooled per Store and grows by doubling through `heap_realloc`, so the steady state has no allocation (FrameList.cpp:177-208, 349-376). Every pool that was a fixed console size and was checked grows instead: gravities, sensors, groups, LOD controls, execute array, particle emitters, and the draw-buffer executer's actor array. No thread other than the game thread runs game code. The game has no job or worker service (nebula_game_api.h has no worker entry; git grep for std::thread/worker in native/petari/src: only SaveRequest.hpp).

- P-038 (performance, scales with object count) Draw-buffer packet insertion, removal and light re-sort are linear per call, so clipping churn costs O(n) per changed actor for each of its materials.
  - Evidence:
    - native/petari/src/Game/System/DrawBuffer.cpp:65-84: `add` finds the light run with a linear walk (`findLightSortIndex`, :114-143), then shifts every later packet up.
    - :87-101: `remove` searches linearly, then shifts every later packet down.
    - :103-112: `resetLightSort` searches, then removes and adds.
    - DrawBuffer::add and DrawBuffer::remove (:162-179) do this once per material of the model.
    - LiveActor.cpp:78, 114, 317 and 348 connect and disconnect draw on every clip-in and clip-out; ActorLightCtrl.cpp:54, 108 and 123 re-sort on an area-light change and at a blend's end.
  - Why: a model with many instances (coins, star bits, grass, rocks) is one DrawBuffer. When k of its n instances clip in or out in one step, the cost is about k × materials × n moves. Clipping is per step, and the camera turning past a field of them changes many at once. The ordered array must be kept because packets of one light share one light load (the "light run"). Fix: keep each light run as its own list, with the packet holding its run and position. Add then appends to its run's list, remove does swap-remove inside the run, and a draw walks runs in order. Each operation becomes O(1) and every run still loads its light once.
  - Confidence: the linear operations are verified in code, and the original has them too (Petari DrawBuffer.cpp remove). Their share of a step is suspected, needs a run: `--diagnostics timeline` in Gusty Garden or Good Egg while turning the camera across coin and star-bit fields, reading the calcViewAndEntry and movement spans; or a Superluminal/Tracy capture on the PC with DrawBufferShapeDrawer::add/remove as a function filter.

- P-039 (performance, worse than linear) Sensor hits are found by testing every pair between groups, each step.
  - Evidence: native/petari/src/Game/LiveActor/SensorHitChecker.cpp:118-147: 13 group pairs plus character × character. :151-179 and :182-207: nested loops over the active sensors. The groups start at 0x10/0x80/0x200/0x800/0x400/0x400 and grow (:220-222). The test itself is a cheap sphere check (:21-41). Clipped hosts are left out (:12-14, :55-).
  - Why: eye × simple, eye × map object, character × map object and character × character are products of counts in the hundreds in a busy galaxy, done every step on one thread. This is the console's broad phase. Fix: a uniform grid or sort-and-sweep per group pair that yields candidate pairs, then sort the candidates by (i, j) so the hits keep the original order (gameplay reads `addHitSensor` order). Treat an entry with a NaN position as a candidate of every pair, since the current test counts NaN as a hit (:31-32). The module with mod hook points (`!NEBULA_PLAIN_MODULE`, :162-178) calls `isSensorActive` per pair and must keep doing so for the hooks, so it is a second path that also needs the change.
  - Confidence: the loops are verified in code. Their share of a step is suspected, needs a run: `--diagnostics timeline` in Good Egg (the start planet with star bits), Toy Time and Bowser's Galaxy Reactor, with a scoped span around SensorHitChecker::movement (the timeline has no span there today; a Superluminal or Tracy capture on the PC answers it without code).

- P-040 (performance, structure) The step has no worker parallelism, and the shared state that blocks it is named here, so a plan can start from it.
  - Evidence:
    - Per-actor work that is independent per actor:
      - calc-anim (SceneExecutor.cpp:95-111);
      - calcViewAndEntry per active executer and actor (DrawBufferGroup.cpp:72-78, DrawBufferExecuter.cpp:81-84);
      - particle emitter update.
    - What these write besides their own actor:
      - the global `ModelSys::mViewMtx` (native/petari/include/Game/Render/Model.hpp:190, written at SceneExecutor.cpp:118-123);
      - `MotionSignal::drawObject` slots (NameObjCategoryList.cpp:25);
      - the one current frame-list Store and its file statics (FrameList.cpp:253-269: sCurrent, sOpenDraw, sLightsDirty, ...);
      - the arena allocator, which has no lock (P-012).
  - Why: on a 4-8 core phone the game thread is the longer of the two threads in busy scenes (P-019's report takes the larger of the two CPU times for this reason). Fix (plan only): first make calcViewAndEntry write into a per-actor or per-chunk output instead of the global view matrix and the shared Store, then split it over a fixed worker pool in actor order and join before the draw lists. Calc-anim comes next once its allocator use is known.
  - Confidence: verified in code that nothing runs in parallel and that these globals are written. What it would gain is not known: it needs a run of `--diagnostics timeline` on the tablet in the 6-galaxy tour, reading the game thread's movement, calcAnim and calcViewAndEntry spans against the render thread.

Checked and correct here:
- Category removal order matches the original.
- The connect and disconnect queues run each index once (NameObjExecuteHolder.cpp:252-277).
- The frame list's release has a fence on the renderer side on the normal path (render_core.cpp:2146-2155), and on the device-loss paths it is under `queue_mutex_` (:1569-1590). The game-side acquire is still missing: that is P-005, unchanged.
- The sequence interpreter decodes big-endian op data to `SeqOp` once at load. Only jump-table lookups read big-endian at run time (SeqTrack.cpp:828, 1401, 1404, three bytes each), so there is no per-frame bulk byte-order work.

### Area 8, console-era behaviour (Round 2)

Read: the capacity checks found by `if (m*Count >= m*Max)` in native/petari/src (10 sites, all grow: PlanetGravityManager.cpp:75, SensorHitChecker.cpp:66, NameObjGroup.cpp:16, ViewGroupCtrl.cpp:51, IKJointCtrl.cpp:90, ClippingActorInfo.cpp:135, SpiderThreadMainPoint.cpp:64/74, NameObjExecuteHolder.cpp:201, DrawBufferExecuter.cpp:39), the byte-swap census (git grep bswap/byteswap/be16/... over runtime/src, native/host, native/petari/src: 3 files, none per frame), the frame-counter census over runtime/src, native/host and platform (rt_* left out), and the 60 Hz constant census. Arenas have no console size (NebulaArena.hpp:2-5, "There is no size to give"). Not done: a file-by-file read of the 2000 game files for fixed arrays declared as members (`T mArr[N]`), which a census cannot find reliably.

- P-041 (console-era remnant) Several host and renderer timers count frames or presents but are written and named as time, so they run 2-4x faster at 120-240 Hz and 2x slower at 30.
  - Evidence:
    - native/host/host_main.cpp:1038-1043: the display refresh is "looked at twice a second" via `frames++ % 30u`.
    - host_main.cpp:1163-1164: "Every 10 s" via `++frames == 600u`, a log only.
    - runtime/src/render/mtl/texture_backend_mtl.mm:408-409: empty Metal heaps are trimmed every `kTrimFrames = 600u` frames.
    - runtime/src/render/present_slots.cpp:68 and :75-100: the depth controller judges lateness over windows of 60 presents and needs `calm_needed_` presents (doubling to 4800) of calm before stepping the depth down.
    - The D3D12 telemetry next to it uses a clock (renderer_d3d12.cpp:4256 `ClockInterval report{10'000}`), which shows the intended form.
  - Why:
    - At 240 Hz with interpolation the DWM timing query runs 8 times a second instead of 2, and the Metal trim runs every 2.5 s.
    - Present-slot windows of 60 presents are a quarter second at 240 Hz and two seconds at 30. The same lateness then triggers a deeper pipeline at different times per rate, which shows as latency differences between rates that nothing intends.
    - The owner's rule is time-based lifetimes outside the game step.
    - Fix: use `galaxy::ClockInterval` (or a nanosecond window) for the first three. Present slots should count in nanoseconds of presents (for example a 1 s window and a 10 s calm), not in present counts.
  - Confidence: verified in code. The latency effect of the present-slot windows at 240 Hz is suspected, needs a run: `--diagnostics timeline` with max_fps 240 and 60 on the PC, comparing the present depth chosen after a stall.

- P-042 (console-era remnant, a decision for the owner) The default frame-rate setting is 60, the game step, on every platform.
  - Evidence: runtime/include/galaxy/render/render_config.h:53 `float max_fps = static_cast<float>(kGameStepsPerSecond);`. The Android launcher reads 60 when config.ini is missing or unreadable (platform/android/app/src/main/java/dev/nebula/galaxy/GameActivity.java:181, 190).
  - Why: a new install on a 120/144/165 Hz display starts at 60, with interpolation unused, until the player finds the setting. "0, the display's own" exists (GameActivity.java:178) and is the modern default. Fix: default `max_fps` to 0 (match the display) on every platform where interpolation is on by default, and keep 60 where the device's budget says so. The benchmark's first-start choice (P-035 note, choose_first_start_scale) is the place to decide that.
  - Confidence: verified in code. Whether 60 is a deliberate choice for battery on phones is not written anywhere I read, so this needs the owner.

### Area 10, tests and verify scripts (Round 2)

Read:
- The test registration block of the root CMakeLists.txt (634-800).
- tools/test_driver/test_driver.cmake (1-30).
- native/tests/run_tests.sh (1-20).
- The header comment of every tools/verify script.
- The pre-push hook in the shared git directory (C:/Users/<user>/Documents/MarioGalaxyRecomp/Nebula/.git/hooks/pre-push).

Not read: the test bodies, beyond their file names, sizes and the names they include.

Where tests run:
- The root CMakeLists.txt registers 72 ctest targets (galaxy_test, :635-646). It is the Windows host build.
- platform/sdl and platform/apple register none (git grep: the only other add_test lines are in third_party/zlib and test_driver.cmake).
- native/tests/run_tests.sh builds the game layer's five C/C++ tests (math, text strings, UI names, formatted names, name index) with clang on any system.
- Neither workflow runs any of these. release.yml and website.yml have no test, ctest or tools/verify step (grep of .github: no hits).
- The console census runs only from the local pre-push hook. That hook is not in the repo, only in the one machine's .git/hooks. byte_order_check.py and cast_check.py are run by nothing.

Covered by unit tests, by name:
- Config parse and reload (render_config 859 lines).
- Pacing: frame_phase, present_slots, precise_wait, frame_interpolation, frame_telemetry.
- render_core host logic (405).
- Post-process planning (621).
- frame_pass, which compiles shaders with d3dcompiler, so it is Windows only.
- Texture packs, the pipeline cache file headers, pairing, world lights, RT geometry and materials.
- Input: controls 1921, pad_controller 1585, host_pointer_events 1804, keyboard, raw mouse, Wii Remote motion and pointer, rumble route.
- The audio mixer (526).
- Saves: save_files, save_format, save_transfer 938, atomic_file, install migration.
- The updater (374), crash_report, diag_report.
- Benchmark stats and recommendation.
- Every settings page model.
- HDR calibration and os_hdr.
- Movie video and playback, Mii import, achievements, stats, the calendar.

Not covered by any unit test (no test names or includes them):
- Each renderer's recording: D3D12, Vulkan, D3D11, Metal. draw_walker and texture_store.
- The game layer's NebulaArena. The only test that touches it stubs it out: native/tests/formatted_name_tests.cpp:17-27.
- The game's FrameList writer, category lists, draw buffers and SensorHitChecker.
- display_clock (Android Choreographer and the Linux stubs), host_window_sdl and host_window_apple.
- host_main's step loop, suspend and resume.
- crash_watch, controller_hid (HID report parsing; pad_controller_tests covers the mapping above it).
- audio devices (WASAPI, SDL, Apple).
- The Android Java and Kotlin code. The repo has no androidTest or test source set; MotionTest.kt is a UI screen.
- The Android updater's signature check (Updates.kt). update_tests covers the desktop C++ updater only.

tools/verify, what each checks and what it needs. Unless named otherwise, the shell scripts need Windows with Git Bash (run.sh:3 "Windows (Git Bash)"), frozen builds in NEBULA_BINS (one folder each with pak.path), NEBULA_RUNS, NEBULA_TEST_SAVES or NEBULA_TEST_NAND, a game.pak, and the test extension from tools/test_driver (a build with NEBULA_EXTRA_CMAKE=tools/test_driver/test_driver.cmake).
- run.sh / common.sh: one hidden, silent run with snapshots, audio.wav, crash dumps and private app-data. Every other run script calls it.
- tour.sh: the 102-stop tour (600 frames each). It reports stops reached, crashes and loss lines.
- all_stops.sh + compare_stops.py: two builds deterministic through all stops, ranking picture differences.
- stopcross.sh + stopcross.py: D3D12 against Vulkan, one build. It needs Nebula.exe and NebulaVk.exe and fails over LIMIT pixels.
- window.sh, galaxy.sh, align_frames.py, align_tol.py, matchframes.py, region_ab.py, compare_snapshots.py, sheet.py: two builds' pictures at Battlerock or at a galaxy's first star, aligned by frame.
- interp_real.sh: pictures with interpolation off (60) and on (120).
- look.sh + look_audit.py + look_check.py: the tour against a fixed reference run (REF, CONTROL). A person must write a line for every flagged stop (LOOK_REVIEWS).
- fs_det.sh, home_det.sh, mii.sh, movie.sh, save.sh: deterministic A/B of the file select, HOME, the Mii picker, the ending movie, and a save written and loaded (NEBULA_MII_SAVES for mii.sh).
- boots.sh: COUNT boots in PARALLEL, each to the title within 25 s.
- extended.sh, luigi.sh, luigi2.sh: the ending, Luigi, and stage reloads with crash and memory lines.
- gpulost.sh: device loss at frame 900 and every 300 after (D3D12 or Vulkan, through the test extension).
- resize.sh, sizes.sh: live resizes and window shapes.
- validate.sh: the tour under the API's validation layer (the D3D12 debug layer or Vulkan validation must be installed).
- rt_inputs.sh: RT's frame-list inputs (`--diagnostics rt-inputs`). It is RT-only and goes with the RT removal list.
- monkey.py: random input per stop in shards (crash search).
- perf.sh + perf_summary.py: GPU-timer measurement at Battlerock on NEBULA_GPU or the iGPU with 4 threads. The owner's notes forbid iGPU tests (feedback memory), so this needs his go.
- pgo_profile.sh: makes native/pgo/game.profdata from the tour (llvm-profdata).
- Audio: audio_compare, audio_bands, wavdiff, wavdrift, wavlag, wavshift. Two runs' audio.wav (16-bit PCM) by loudness, spectrum, first difference, drift or lag.
- Data: audiocheck.py, moviecheck.py, pak_compressed.py, pakfiles.py, store_check.py, gstcheck.py. They check source pak against cooked pak, or check cooked files and ghost recordings, and need the source disc's pak.
- Logs: gaps.py (frames over 25 ms, needs `--diagnostics frame-times`), loads.py, losscheck.py, gate_check.py (a gate output with a missing result fails), water_summary.py.
- Code checks, which need only the repo and Python: console_census.py (closed groups must read zero), byte_order_check.py, cast_check.py. libm_check.c compares nebula_libm against the C library and needs a C compiler.
- Windows binary checks: objdiff.py, pe_sections.py (COFF/PE), memws.py (one live PID's working set).
- Helpers: send_keys.ps1 (scan-code key input to the window), start_hidden.py, settings_args.py, test_plugin.py (installs the test plugin as a mod, which is modding).
- android_first_install.py: a true first install on a device or emulator through adb and the system picker. It is the only verify script for a platform other than Windows. There is none for Linux or macOS.

- P-043 (untested) This extends P-023 with two facts it did not have. The three code checks are enforced only by one machine's local hook. The SDL and Apple builds do not even compile the unit tests.
  - Evidence: .github/workflows/release.yml and website.yml have no ctest, run_tests.sh, or tools/verify step (grep of .github for test/verify/census: only `gh release create --verify-tag` and `--verify installer/lumakit/lumakit.luma`). The census runs from .git/hooks/pre-push (last lines), a file outside the repo. byte_order_check.py and cast_check.py have no caller (git grep: only their own files).
  - Why: a push from any other clone, and every Linux, macOS and Android build, skips all 72 ctest targets and the code checks. The SDL and Apple builds never compile the tests (no add_test outside the root CMakeLists.txt). Fix: add a `test` job to release.yml (and a push-triggered workflow) that runs `ctest --test-dir build/host --output-on-failure` on Windows, `native/tests/run_tests.sh` on Linux, and the three Python checks. Register the platform-neutral tests in platform/sdl/CMakeLists.txt so Linux runs them too.
  - Confidence: verified in code.

- P-044 (untested) The engine's riskiest shared code has no unit test: the game arena, the frame-list writer, the draw walker, the display clocks and the Android Java/Kotlin layer.
  - Evidence: the list above. The only test that includes NebulaArena.hpp replaces it with stubs (native/tests/formatted_name_tests.cpp:17-27). No test names draw_walker, FrameList (the game's), display_clock or controller_hid.
  - Why: P-005, P-012, P-013 and P-038 sit in this code. An arena test (alloc/free/grow/destroy with a child arena, findFromRoot over many arenas) and a FrameList round trip (write a list, mark it released, write again, check pages are reused) are small and catch the class of fault those findings describe. Fix: add those two first, then a draw_walker planning test on a recorded list (the planners are pure functions of the list).
  - Confidence: verified in code (by names and includes; test bodies were not read).

### Area 5, services (Round 2, what was reached)

Read completely:
- native/host/crash_watch.cpp and crash_watch.h (25-50);
- runtime/src/audio_device_sdl.cpp, audio_device_apple.mm and perf_hint.cpp (all three recorded above).

Read partly:
- audio_mixer.cpp (218-327);
- host_main.cpp (1378-1392, 5700-5725, 6500-6514);
- input/host_input.cpp and controller_hid.cpp (a grep for local heap containers only).

Not read:
- audio_device_wasapi.cpp, benchmark.cpp, save_transfer.cpp, native_settings.cpp beyond Round 1's lines, and the settings page files;
- update/ beyond Round 1's lines, installer/native;
- host_main.cpp outside the ranges listed in the coverage table.

The heap containers that grep found in controller_hid.cpp are in device open, capability and feature-report paths (:500, :530, :667, :712, :783, :870-900). Whether any of them runs per poll was not followed.

Checked and correct:
- The mixer's mix path takes no lock and swaps its frame buffers, so there is no steady-state allocation (audio_mixer.cpp:218-327).
- The crash watcher writes the dump from a second process, from a snapshot. It has no heap or lock use on the crashed side (crash_watch.cpp:265-328, which uses static buffers only), and it reports fast-fail ends that no in-process handler sees (:103-130).

- P-045 (wrong behaviour, rare) After a dump request times out, the next request can take the previous dump's answer as its own.
  - Evidence: native/host/crash_watch.cpp:
    - :318-323: the game sets `g_ask` and waits on the auto-reset `g_done` for `wait_ms` (30000 at host_main.cpp:1385 and :6511). On a timeout it returns kFailed and clears `g_asking` (:289), but `g_done` is not reset.
    - The watcher, still writing, later sets `block->written` and `SetEvent(done)` (:208-212).
    - A second request (the stall dump, then a crash, or the reverse) writes the block (:303-312) and sets `g_ask`. It is woken at once by the stale `done` and reads `written`/`error` that belong to the first dump, under the second dump's file name.
  - Why: the crash line then names a dump file that is not written yet, or reports a failure that was the other dump's. The watcher does write the second dump afterwards, unless the game has ended by then. A full-memory dump of a large process (`gCrashForeign` adds MiniDumpWithPrivateReadWriteMemory, host_main.cpp:1379) can pass 30 s on a slow drive. Fix: put a request number in the Block that the watcher copies into its answer, and have the game wait until the answer's number matches; or `ResetEvent(g_done)` before `SetEvent(g_ask)` and refuse a new request while the watcher has not answered the last one.
  - Confidence: the sequence is verified in code. Hitting it needs a dump over 30 s: suspected, needs a run. Point the crash folder at a slow USB drive, start with the test extension (`--extension`, which turns stall watching on, host_main.cpp:6410), stall the game thread for more than kStall (10 s, :6409) with a debugger break, then crash within 30 s of the stall dump.

## Round 3 (renderers only)

Scope: the renderer files the coordinator listed. Each item below says what was read. A file counts as read only when every line was read.

### R3.1 draw_walker.cpp 1-1080 (read completely; with Round 1's 1080-2213 the whole file is now read)

What runs per walk (each real or generated picture) in prepare_walk (:177-489):
- scan_list runs once per list.
- Pairing, plus motion at t when between frames.
- Each block's rows are composed into the CPU vector `walk_rows_`, then copied in one memcpy to the matrix ring (:329). The staging is needed because `normals_from` reads rows back (:282-288), and the ring is write-combined memory.
- One instance table entry is written per item (:361-380).
- The frame's ring vertices are copied once per walk (:383-431). When between frames, they are first staged in `walk_ring_stage_` (:393, an `assign` of the whole ring) so that moved positions are written sequentially.
- Ring indices are copied once (:432-438).
- View terms and side planes are made per view (:441-481).
- Per-walk tables are cleared with `assign` (:482-485). These vectors keep their capacity, so after the first frames there is no heap allocation, except `walk_lerped_` growth (:684) and `walk_ring_stage_` when the ring grows.
- walk_view (:491-636) writes one constant-ring entry per view used.
- walk_material (:638-661) writes two constant-ring entries per unregistered constants entry.
- CPU frustum culling (item_culled, :977-1012) applies only to scene-layer, colour-writing, non-stencil, single-matrix, perspective items. Everything else goes to the GPU.

Per-frame work that differs by renderer:
- D3D12 uses descriptor-indexed lists (walk_tables :795-879). It rewrites a table's list only when its textures, samplers, sampler generation, mip lines or bias changed, or every walk if the table names a render target.
- Vulkan without descriptor indexing, Metal and D3D11 use bound tables (walk_bound_tables :881-975). A table that names a render target gets a new texture table every walk (`texture_table_for`, :927) and a new sampler table every walk (:959). Those are per-frame descriptor or argument allocations; their cost is in each renderer's `texture_table_for` (see R3.3-R3.6).

- P-046 (performance, small) Ring-run merging patches matrix numbers and indices in place in write-combined upload memory, the access pattern the walker avoids everywhere else.
  - Evidence: draw_walker.cpp:389-390 says that the ring is write-combined, so positions moved at t are made in a copy and written out in one sequential copy. merge_ring_run then writes 4-byte matrix fields at vertex stride into `walk_ring_cpu_` (:779-785, the ring allocation from :388) and one 4-byte index per index into `walk_index_cpu_` (:788-790, index ring :437). Both are scattered partial writes into the same kind of memory.
  - Why: scattered sub-cache-line writes to write-combined memory flush partial combine buffers. This runs for every run of joinable ring draws in every walk, generated pictures included. Fix: apply the patches while copying. Copy the ring through `walk_ring_stage_` (already used between frames) and the indices through a staging vector, patch there, then copy out once. Or record the patches during prepare_walk, which already copies the ring.
  - Confidence: verified in code. The cost is suspected, needs a run: `--diagnostics timeline` render-thread record time in a galaxy dense with ring draws (Gusty Garden grass), comparing a build that stages the copy.

- P-047 (maintainability) A pointer into `walk_lerped_` is valid only until the next walk_material_rows call. walk_same_material carries a comment-enforced workaround (:725-727), and any new caller that holds two results is a use-after-free.
  - Evidence: draw_walker.cpp:684 `walk_lerped_.resize(...)` can reallocate. :708 returns `walk_lerped_.data() + at`. :725-727 calls walk_material_rows(a) again "a's again: b's may have grown walk_lerped_".
  - Why: the code is correct today only because of call order. Fix: return an offset (or reserve `walk_lerped_` once per walk for all constants entries: `list.constants_count * max rows` is known in prepare_walk) so no pointer can be stale.
  - Confidence: verified in code; no current misuse was found in 1-1080 or 1080-2213.

### R3.2 renderer_d3d12.cpp (now read completely, 1-7501)

Read in Round 3: 1160-1449, 1760-1968 (this range was also missing from Round 2's list), 1968-3150, 3930-4160, 4720-5167, 6110-6268, 6750-7122, 7336-7501. Together with Rounds 1 and 2, every line of the file has been read.

What these ranges hold:
- Sampler heap and sampler descriptors (1167-1284).
- Capture, conversion and probe pipelines (1286-1447).
- apply_window_size (1760-1845): a wait plus a full rebuild, only between frames and only when the settle state says the size is final.
- Video-memory reads (1875-1901, 100 ms cache).
- Lazily made clear pipelines (1939-1966).
- initialize (1968-2669).
- The texture heap mirror and growth (2675-2758).
- sampler_slot and write_texture_list (2764-2812).
- free_target, the vram log, shutdown (2814-3147).
- Display HDR read, the HDR switch, metadata, the HDR capture picture, the sync interval (3930-4160).
- Retire of idle sets, motion and reactive targets, temporal upscaler set-up and record, FSR 3 frame generation hand-over (4720-5166).
- RT hooks (6110-6268; nothing runs without RT).
- Screen probes, debug captures and readbacks (6750-7120, 7336-7499).

#### Every wait, lock, allocation and compile inside a D3D12 frame (whole file)

Waits for the GPU (`wait_for_gpu`). Each is a full CPU-GPU drain:
- :1269 reset_sampler_frame, at a frame start, when the sampler heap has fewer than 512 free slots or the sampler settings changed (P-048).
- :1801 apply_window_size, between frames, on a final size or MSAA or HDR-format change. Expected.
- :3224 and :3272 are in Round 1/2 ranges (frame begin), recorded in P-028.
- :4035 set_swap_chain_hdr, on an HDR switch. Expected.
- :4101 hdr_capture_picture, when a capture is asked and the window size changed since the last one (captures only).
- :4913 and :4948 prepare_temporal_upscaler, when the upscaler's sizes or mode change, at frame start.
- :5119 inside record_temporal_upscale, mid-frame with the command list open, when the FSR 3 frame-generation context does not match (P-050).
- :5300, :5564, :5592, :5638, :5750: P-028 (size changes of target sets, the HUD-less targets, the wide scene).
- :6841 finish_pictures (captures, tests).
- :2960 and :2987 shutdown.
- :4243 waits up to 100 ms on the swap chain's latency waitable object, by design (frame pacing).
- :4663 is the fence wait at frame start, by design.

Locks inside a frame:
- None of the renderer's own.
- get_render_config() is lock-free after the first read in a generation (render_config.cpp:414-425, a thread_local copy). It still copies the whole RenderConfig twice per call. It is called about 24 times in this file, including once per new sampler (:2765), which is cheap but needless.

Allocations inside a frame:
- write_texture_list makes a new upload buffer when the frame's list buffer is full (:2783-2806); it doubles, so this is rare.
- New target sets and frame-pass targets on first use or a size change (create_texture sites listed by grep: :1714, :4788-4808, :5229-5341, :5597, :5662, :5774-5783, :6031, :6188).
- Readback buffers for probes and captures (:6766-6771, :7004, :7368).
- Descriptor heaps for those sets (:4772, :5206-5209, :5310-5312, :5645, :5758, :6015, :6171).
- In steady play none of these repeat. Each happens once per size or feature change.

Pipeline compiles inside a frame (no pipeline cache, no library):
- clear_pipeline per scene variant (:1939-1966);
- depth_resolve_pso_ (:1065, first MSAA depth read);
- depth_copy_pso_ (:1537, first depth read);
- the 2D layer's start and merge pipelines (:5959).

Each is a full PSO compile on the render thread the first time its variant is drawn (P-052).

- P-048 (performance) The D3D12 sampler heap is drained with a full GPU wait when it fills. Sampler keys include the dynamic-resolution line ratio and the upscaler's bias, so play with dynamic resolution or a temporal upscaler keeps minting new samplers until the heap refills.
  - Evidence:
    - renderer_d3d12.cpp:1261-1275 reset_sampler_frame: `full = used + 512 > 2048` (kSamplerFrameRoom :744, kSamplerHeapSlots renderer_common.h:60), then `wait_for_gpu()`, then reset of all slots and `++sampler_generation_`. That bump makes every texture list be rewritten (draw_walker.cpp:801-806).
    - :2764-2778: sampler_slot only allocates, it never frees.
    - render_core.cpp:75-100 make_sampler_key: `bias += mip_lines_log2 - lod_shift` for levels drawn by hand, and `scene_bias` for every other mipmapped sampler.
    - draw_walker.cpp:190-191: mip_lines comes from `scene_rectangle().height`, which changes with each dynamic-resolution step.
    - Vulkan has the same constants (vk/renderer_vk_internal.h:24); see R3.3 for its reset.
  - Why: each new rectangle height (or upscaler bias) makes a new key for every by-hand texture in view, and each bias change does so for every sampler. The heap fills over minutes of dynamic-resolution play, then the frame start stalls for a full drain, and all texture lists are rewritten in that frame. Fix:
    - Quantise mip_lines and the bias to a fixed step (for example 1/16 of a level) before they enter the key. Better, move the per-frame bias into a root constant that the shader adds (`SampleBias`), so the sampler key holds only the texture's own state.
    - Replace the drain with a fence-tracked reuse of slots: keep slot age, and reuse slots unused for kFramesInFlight + 1 frames.
  - Confidence: the drain and the keys are verified in code. How often it fills in play is suspected, needs a run: `--diagnostics timeline` with dynamic resolution on (a GPU-bound setting, Good Egg, 10 minutes), plus a log line on reset_sampler_frame's wait (there is none today; a PIX capture's CPU timeline shows the fence wait).

- P-049 (performance) DLSS is created on the render thread inside begin_frame, after a full GPU wait, while FSR 3 and XeSS are created on a worker.
  - Evidence: renderer_d3d12.cpp:4947-4949 `wait_for_gpu(); temporal_.reset();`. :4962-4974: XeSS and FSR 3 go through `std::async`, and the scene renders without them until ready (:4936-4944). :4976 `temporal_ = create_dlss_upscaler(...)` runs synchronously.
  - Why: creating a DLSS feature takes tens to hundreds of milliseconds. Every quality, mode or render-size change while DLSS is chosen (dynamic resolution's context size, settings, Ray Reconstruction toggling with the tracer, :4930) freezes the picture for that long. Fix: create DLSS through the same `temporal_pending_` future. Streamline's `slDLSSSetOptions` / feature creation can be issued from a worker when the device is free-threaded, which D3D12 is.
  - Confidence: verified in code. The stall length is suspected, needs a run: an RTX PC with DLSS, changing DLSS quality in the settings while `--diagnostics timeline` runs, reading the longest frame.

- P-050 (performance) The FSR 3 frame-generation context is recreated mid-frame, with the command list open, after a full GPU wait.
  - Evidence: renderer_d3d12.cpp:5112-5123, inside record_temporal_upscale (called at the scene's end): `if (!ready) { wait_for_gpu(); fsr_generation_.destroy(...); ready = fsr_generation_.create(...); }`.
  - Why: it triggers on any display size change, or on the context's render size changing. The wait and the creation sit inside the frame being recorded, which is the worst place for them. Fix: move the match check to begin_frame next to prepare_temporal_upscaler (:4907), which already owns the context lifetimes, and create the context on a worker the way FSR 3 upscaling is.
  - Confidence: verified in code.

- P-051 (wrong behaviour, diagnostics only) The scene capture decodes with the frame size at read time, not at copy time.
  - Evidence: renderer_d3d12.cpp:6976-6977 copies with `frame_.width/height`. debug_log_readback reads them again at :7050-7051, after any number of frames. The back-buffer path keeps the copied footprint (:7454-7458, comment "A resize may have completed after this copy was recorded").
  - Why: a dynamic-resolution step between the copy and the read writes a sheared PPM. Tests compare these pictures (tools/verify). Fix: keep w, h and the pitch with the pending readback, as the back-buffer path does.
  - Confidence: verified in code.

- P-052 (performance, first use) Utility pipelines are compiled on the render thread at first use, outside the pipeline cache.
  - Evidence: `CreateGraphicsPipelineState` at renderer_d3d12.cpp:1065 (depth resolve), :1537 (depth copy), :1963 (clear per scene variant: target, HDR, sample count), :5959 (2D layer). Each is reached inside a frame the first time that variant is drawn, for example the first frame after MSAA or HDR is switched on. None goes through core/pipelines.cpp's cache or warm-up.
  - Why: a driver compile (several ms, more on a cold driver cache) lands on the frame that first needs it. Fix: create every variant at initialize or on scene-variant change, on the pipeline workers, the same way the program pipelines are (pipelines.cpp:446-449).
  - Confidence: verified in code. The size of the hitch is suspected, needs a run: `--diagnostics timeline` on a fresh driver cache (delete the NVIDIA/AMD shader cache), switching MSAA 4x on in-game, reading the next frame's time.

D3D12 per-picture sequence with interpolation on: every generated picture walks the same list at t through the same passes (draw_walker.cpp:1860-2060; target passes write their generated copies, :1616-1630, :1973-1995). Only screen probes are skipped (:1911-1912). So the pass and copy counts below are per presented picture, real or generated.

#### Pass table, D3D12, one typical frame: a galaxy with bloom, no mirror, no mods, interpolation on, native resolution (no upscaler, no dynamic-resolution rectangle), no MSAA, SDR, AA off, HUD at the render size

D3D12 has no render pass objects (no BeginRenderPass anywhere), so each line is an OMSetRenderTargets span or a copy. "Load" means what is in the target when the span starts (kept, cleared or discarded). "Store" means it is kept for later.

| # | What | Caused by | Target, size | Load / store | Mark |
|---|---|---|---|---|---|
| 1 | Each game target pass (a render-to-texture) | the list's target-pass records (begin_target_pass :5421-5450) | its key's target set, the size the game asked for | cleared or kept as the pass says / kept (it is sampled later) | needed |
| 2 | Scene draws | the scene's DRAW passes | scene_color_ + scene_depth_, render size | cleared at the scene's start (game clear) / kept | needed |
| 3 | Each COPY_FRAME of the whole frame | the game's frame copy (indirect effects, water) | none: the scene target is lent (open_frame_for_pass :1643-1661) | — | needed (no work) |
| 4 | Bloom NORMAL: up to 9 compute dispatches then 1 full-size composite draw | NEBULA_PASS_BLOOM_* (game_bloom.cpp:445-566) | reduced images, then scene colour | composite: kept / kept | needed. (e) in Round 2: two dispatches could be one |
| 5 | HUD draws | the 2D passes | scene colour, render size | kept / kept | needed. Same target as 4: no new span |
| 6 | walk_show | the list's end | swap only (take_frame_picture :1683-1690) | — | needed (no work) |
| 7 | Present draw | present | back buffer, window size | cleared only with borders (:7199-7202) / kept | needed |

Counts per presented picture: full-target copies 0, full-target resolves 0, full-size draws 3 (scene, bloom composite, present) plus one per partial COPY_FRAME. With interpolation at 2x, every number doubles per game frame.

Same frame with MSAA 4x:

| # | What | Caused by | Target, size | Load / store | Mark |
|---|---|---|---|---|---|
| 1 | Target passes | as above | own sets (single-sample) | as above | needed |
| 2 | Scene draws | scene DRAW passes | scene_msaa_.color/depth (4x), render size | cleared / kept | needed. The 4x colour and depth are kept in memory; D3D12 desktop GPUs do not tile, so there is no cost to say otherwise |
| 3 | Each COPY_FRAME | game frame copy | ResolveSubresource 4x→scene_color_ (:1034-1046), then the target is lent | — | needed. One resolve per copy, but a second COPY_FRAME with no draw between them resolves again (resolve_scene has no "already resolved" flag; see P-053) |
| 4 | Bloom | bloom records | ResolveSubresource, then copy into bloom's source (P-025), then dispatches and composite into the 4x colour | — | the resolve is needed. The copy after it can be dropped without changing a pixel (P-025) |
| 5 | HUD | 2D passes | 4x colour | kept / kept | needed |
| 6 | Resolve for show | walk_show | ResolveSubresource 4x→scene_color_ | — | needed |
| 7 | Present draw | present | back buffer | as above | needed |

Counts with MSAA 4x: full-target resolves 2 (bloom, show) plus one per COPY_FRAME, and 1 full copy (P-025's bloom copy).

- P-053 (performance, MSAA) A resolve is recorded for each consumer even when nothing was drawn since the last resolve.
  - Evidence: renderer_d3d12.cpp:1034-1046 resolve_scene records `ResolveSubresource` each call. Its callers are frame reads (open_frame_for_pass), bloom, AA, the 2D layer and show (Round 2 list), plus the screen probes (:6786, depth). I found no "resolved since the last draw" state in the ranges read.
  - Why: two consumers in a row (for example a COPY_FRAME directly followed by the bloom records, which is the game's usual order at the scene's end) each resolve the full 4x target. Fix: keep a `resolved_serial_` set when a resolve is recorded and cleared by any draw into the MSAA target. Skip the resolve when it is still valid.
  - Confidence: the per-call resolve is verified in code. Whether the game's lists put two consumers back to back is suspected, needs a run: `--diagnostics gpu-passes` with MSAA 4x in Good Egg, counting resolve passes per frame.

### R3.3 vk/renderer_vk_frame.cpp (now read completely, 1-3951)

Read in Round 3: 1-455, 576-1421, 1622-2297, 2377-2711, 2791-3253, 3366-3577, 3611-3951. Together with the six earlier ranges, the whole file has been read.

- P-054 (performance) Vulkan waits for the GPU in the middle of a frame whenever it makes a scene-sized target set. It records one barrier into a separate command buffer, submits it, and blocks until it finishes.
  - Evidence:
    - renderer_vk_frame.cpp:275-294 run_one_time: `context.wait(context.submit(commands), 10'000'000'000ull)`. On a single queue, the wait does not return before every earlier submission has finished, so it drains the frames in flight.
    - Callers, all reachable inside a frame:
      - create_scene_set (:128-143), from begin_target_pass (:774) for every new or resized target-pass key;
      - ensure_output_set (:1388), from the output scene, the upscaler and wide-scene MSAA;
      - ensure_motion_targets (:1137), at the scene start with a temporal upscaler;
      - ensure_wide_set (:2130), at the wide scene start and in begin_layer_2d.
    - sweep_target_slots (:838-850) drops keys idle for kIdleSteps game steps, so a key the game uses now and then is made again, with its wait, each time it comes back.
    - D3D12 makes the same sets with no wait: a new resource starts in its initial state, and the frame's own list records the transition.
  - Why: each wait is a CPU stall of the whole GPU queue depth, at least one frame of latency, in the frame that first uses a target. Effects that open a target pass for a few frames (a mirror, a picture-in-picture, the star-bit counter's captures) hitch. Fix: record the UNDEFINED→attachment barriers into `commands_` (or the frame's `uploads` buffer, which already runs before the frame's draws, :2405) and delete run_one_time from every frame path. Keep it only for initialisation.
  - Confidence: verified in code. The hitch size is suspected, needs a run: `--diagnostics timeline` on Vulkan in a galaxy with a mirror or target pass that comes and goes (Good Egg's first planet, Gateway's telescope), reading the frame that opens it.

- P-055 (same finding as Round 1's P-006, given here with exact lines; count it once, as P-006) Every Vulkan scene render pass stores colour, depth and stencil at its end, including the multisampled colour and depth with MSAA. There is no variant that drops what nothing will read. Separately, no attachment is transient: vk_context.cpp:1822-1843 create_texture always uses `VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE` with no `LAZILY_ALLOCATED` and no `TRANSIENT_ATTACHMENT` usage, and the MSAA colour is created with COLOR_ATTACHMENT usage only (renderer_vk_frame.cpp:90-91).
  - Evidence: renderer_vk.cpp:602-613: `attachments[0].storeOp = STORE`. attachments[1] copies it (depth STORE, stencil STORE :611) and attachments[2] (the resolve) too. Every variant made at :649-683 changes only load ops. pass_for/aux_pass_for (renderer_vk.h:106-122) pick by clear or not, never by store.
  - Why: on the Android tiled GPUs this port targets, a pass end writes each stored attachment from tile memory to DRAM. With MSAA 4x, the last scene pass of a frame writes 4 samples of colour and 4 of depth+stencil (32 bytes per pixel at 4x for RGB10A2 + D24S8) that nothing reads after the resolve. The final depth is written even without MSAA when no later pass reads it. That is the largest bandwidth cost of MSAA on a tiler, and the reason Vulkan offers `STORE_OP_DONT_CARE`. Fix:
    - Add store-DONT_CARE variants for the multisampled colour and depth, and for single-sample depth.
    - Have the walker mark the scene's last pass. It knows the remaining list: no later DRAW in the scene target and no depth read.
    - Open that pass with the dropping variant. Mid-frame passes keep STORE because later passes load them.
  - Confidence: verified in code. The bandwidth on a device is suspected, needs a run: the tablet (R52WA0FXE5J) with MSAA 4x, `--diagnostics gpu-passes`, before and after, or the Mali/Adreno counter tool's external-write bytes per frame.

- P-056 (performance) When the sampler array fills, the Vulkan reset waits for the last submitted frame. On drivers with a 4000-sampler limit, it also destroys every sampler once half the limit is made, then makes them again.
  - Evidence: renderer_vk_frame.cpp:2900-2957:
    - `full = used + 512 > kSamplerHeapSlots`, then `wait_for_fence_value(last_submitted_)` (:2924-2926);
    - `crowded = samplers_.size() >= maxSamplerAllocationCount / 2` (:2908) destroys all of them (:2909-2918);
    - without descriptor indexing, every sampler table is rewritten (:2940-2951).
    - The comment at :2903-2904 says each window height adds a set, because the key holds the output height's mip lines. Dynamic resolution's steps do the same (P-048).
  - Why: this is P-048 on Vulkan. On Android (limit 4000) the crowded path runs after about 2000 distinct samplers, which dynamic-resolution steps × textures reach in play. Fix: P-048's fix (bias out of the key into a shader constant, quantised mip lines). Then the sampler set is bounded by the game's own states and neither path runs in play.
  - Confidence: the code path is verified in code. How often it runs is suspected, needs a run: tablet, dynamic resolution on, 10 minutes, a log line at :2924 and :2927 (none exists).

- P-057 (performance) Vulkan creates DLSS synchronously on the render thread, as D3D12 does (P-049). SGSR 2 is made there too, by design (:995-997).
  - Evidence: renderer_vk_frame.cpp:987-993 `create_dlss_upscaler(...)`, after `wait_for_gpu_idle()` (:971-973). FSR 3 and XeSS go through std::async (:1005-1010).
  - Fix and run: as in P-049.
  - Confidence: verified in code.

Correct, recorded so they are not raised again:
- The scene capture keeps its width and height from copy time (:3790-3792, used at :3804-3821). D3D12 lacks this (P-051).
- The screen-probe depth test inside the open pass (:3556, :3615-3672) keeps the scene pass open where a compute read would end it. It is a good tiled-GPU choice.
- clear_scene opens the pass with clear load ops when it can (:626-642), and clear_depth_alone (:1314-1338) uses a depth-only pass so colour is never loaded.
- draw_frame_pass trades the scene image to the capture and draws it back while the next pass opens (:3438-3456). For a whole-frame copy this means no copy on Vulkan when no MSAA, no HDR, no wide scene and no target pass are in use.

Vulkan in-frame waits, locks, allocations and compiles (renderer_vk_frame.cpp):
- Waits:
  - run_one_time (P-054);
  - :426 apply_window_size (between frames, expected);
  - :935 and :972 prepare_temporal_upscaler (at frame start, on mode or size change);
  - :2924 and :2944 reset_sampler_frame (P-056);
  - :3747 finish_pictures (captures).
- Locks: constants_set takes constants_mutex_ only while recording in parallel (:2968-2971). It is a linear search over constants_sets_ per call (:2972-2977), which is O(sets) per draw-state change. Small in practice; noted, not a finding.
- Allocations:
  - frame descriptor overflow pools (:695-711, double each time, kept);
  - persistent overflow pools (:2391-2399);
  - texture lists buffer growth (:2567-2581);
  - picture views (:895-901);
  - framebuffers made lazily: motion (:1176-1199), item (:1909-1927), wide (:2129), handed-over scene (:3371-3396), copy (:3008-3024), depth-alone (:235-244, :1317-1326);
  - query pools (:3622-3636);
  - probe buffers (:3561-3567).
  - All are first-use or growth only.
- Compiles: layer2d_merge_pipeline is made lazily (:1710-1720). scene_utility_pipeline and probe_test_pipeline are in ranges read before (lazily made too, per Round 2).

### R3.3b vk/renderer_vk.cpp and vk/vk_context.cpp (both now read completely)

Read in Round 3:
- renderer_vk.cpp 1-579, 880-959, 1441-1939, 2500-2559, 2700-3158. With Rounds 1 and 2, the whole of renderer_vk.cpp has been read.
- vk_context.cpp 1-1896 and 1896-2166, so the whole file has been read.

What these ranges hold:
- initialize and report_adapter, layouts and the scene render passes (:590-684), swapchain creation, utility and probe pipelines.
- shutdown, grow_rings, wait_for_gpu_idle (= vkDeviceWaitIdle, :1920-1926).
- follow_window_and_resize (between frames), capture_sdr_picture, present.
- In vk_context.cpp: device and queue selection, features, submit, wait, deferred destruction, textures, buffers and the address table.

Vulkan present and its locks:
- present() (renderer_vk.cpp:2975-3156) acquires the swapchain image at the end of recording (:2999), with a 1 s timeout.
- It takes `host_window::surface_use` (a std::timed_mutex shared with the window thread) once per frame (:2986). One uncontended lock per frame is fine. If the window thread holds it during a surface change, the render thread blocks there, by design.
- Context::submit and completed_value take `submit_mutex_` (vk_context.cpp:1463, :1599). completed_value is called on many paths per frame (each retire check, each probe, each collect), so it is one lock and one driver call (`vkGetSemaphoreCounterValue`) per call. Not measured. It is small unless called per draw, and the ranges read show no per-draw call.
- The present pass clears the swapchain image every frame (renderer_vk.cpp:1176, LOAD_OP_CLEAR unless the clear_by_command quirk). On a tiler the clear is free. D3D12 clears only with borders.

Pipelines:
- Vulkan builds through `vk::shared_pipeline_cache()` (renderer_vk.cpp:1520, :1550, :1669), so a compile is cached across runs. D3D12's utility pipelines are not (P-052).
- The lazily made ones are scene_utility_pipeline per scene variant (:1568-1607), probe_test_pipeline per aux and variant (:1609-1677) and layer2d_merge (renderer_vk_frame.cpp:1710).
- At initialize only the variant in use at start is made (:1554-1556). Switching MSAA or HDR compiles the rest at their first draw, from the disk cache after the first run.

Allocation (vk_context.cpp):
- Textures and buffers come from VMA (64 MB blocks, :1319).
- Readback buffers use cached host memory, or coherent memory on Mali and Adreno (:2038-2046).
- The upload rings go to mapped video memory only with resizable BAR (:1361-1372, :2135); otherwise they use host memory.

- P-058 (maintainability, small) VMA is told Vulkan 1.1 even when the device and instance are 1.2 or 1.3 without ray tracing.
  - Evidence: vk_context.cpp:1309 `allocator_info.vulkanApiVersion = vulkan_1_0 ? 1_0 : 1_1`. It is raised to 1.2 only with ray tracing (:1323-1326).
  - Why: VMA then does not use 1.3's maintenance4 memory requirement queries. It also does not take the core 1.1/1.2 paths it would pick for a newer device, though both work. There is no wrong behaviour, only VMA's newer paths left unused. Fix: pass `std::min(app.apiVersion, properties_.apiVersion)`.
  - Confidence: verified in code.

Checked and correct:
- Deferred destruction waits for the frame submission that follows the defer (submit_frame tags it, :1784-1795). One-time submissions carry no tag (:1797-1802).
- The fence path never resets a fence that a waiter may still read (:1466-1475).
- Device-fault reporting caps its arrays (:1726-1733).

#### Pass table, Vulkan, the same typical frame (bloom, no mirror, no mods, interpolation on, native resolution, no MSAA, SDR, AA off, defaults: enhanced lighting off so no wide scene, ambient occlusion off)

Load/store are as the render passes are made (renderer_vk.cpp:598-683): colour LOAD/STORE, depth LOAD/STORE, stencil LOAD/STORE. The `_clear` variants set CLEAR loads; `discard_color` sets colour DONT_CARE load. Every variant stores everything (P-006/P-055).

| # | Render pass instance or copy | Caused by | Target, size | Load / store | Mark |
|---|---|---|---|---|---|
| 1 | Each target pass: target_pass(_clear) | the list's target-pass records (begin_target_pass renderer_vk_frame.cpp:747) | the key's RGBA8 set + its own depth, the size asked | clear or load / colour STORE, depth STORE | colour STORE needed (sampled later). Depth STORE can be dropped without changing a pixel: nothing reads a target pass's depth after it ends (end_target_pass :803-826 publishes only the colour) |
| 2 | Scene pass, opened by the game's clear: scene_pass_clear (clear_scene :626-628) | the scene's first CLEAR + DRAW passes | scene colour RGB10A2 + depth, render size | CLEAR / STORE all | needed. Depth STORE is needed because bloom's glow models load it (#3) |
| 3 | Bloom HOLD swap ends #2; the glow pass reopens with the colour cleared and the depth loaded (clear_scene, discard/depth variants) | NEBULA_PASS_BLOOM HOLD (game_bloom) | fresh colour image + same depth | colour CLEAR, depth LOAD / STORE all | needed. The depth STORE at its end can be dropped if nothing after the glow models reads depth. The HUD does not test depth in the passes read, but this is not verified for every 2D draw |
| 4 | Bloom NORMAL: the pass ends for the compute down/blur passes, then the composite reopens it | bloom NORMAL record (renderer_vk_frame.cpp:2298-2376, read in Round 2) | scene colour + depth | colour DONT_CARE load (traded image, `discard_color`), depth LOAD / STORE all | needed. The depth LOAD+STORE could be dropped if the composite and HUD do not read depth (same caveat as #3) |
| 5 | HUD draws in the same instance as #4 | the 2D passes | — | — | needed (no new instance) |
| 6 | Each COPY_FRAME: the instance ends, the image is lent (open_frame_for_pass :3254-3291) or traded and drawn back (draw_frame_pass :3438-3456), then it reopens | the game's frame copy | scene colour + depth | colour LOAD or DONT_CARE (draw-back) / STORE all | needed. Each adds one end/reopen pair, which on a tiler is a full colour+depth store and load |
| 7 | walk_show swap | the list's end | — | — | no GPU work |
| 8 | Present pass | present (renderer_vk.cpp:3063-3109) | swapchain image, window size | CLEAR / STORE | needed |

Counts per presented picture (no MSAA):
- Render pass instances: 4 (scene, glow, composite+HUD, present) plus 1 per target pass, plus 1 per COPY_FRAME reopen.
- Full-target copies: 0. Full-target resolves: 0.
- Depth stores that change no pixel: up to 3 (#1 per target pass, #3 and #4 at the frame's end).

With MSAA 4x:
- The scene passes are ms_passes with a resolve attachment (renderer_vk.cpp:614-625). Every instance end resolves into the single-sample colour and stores the 4x colour and 4x depth.
- #2, #3, #4 and each #6 therefore each do one resolve at the pass end: 3 + COPY_FRAMEs resolves per picture, against D3D12's 2 + COPY_FRAMEs.
- The resolves at #2's end (before bloom HOLD clears the colour) and at #3's end are needed: bloom reads the scene, and the glow models are read.
- The resolve inside each mid-frame instance end that nothing reads before the next instance could be dropped without changing a pixel. Vulkan has no "resolve only at the last end" variant: pResolveAttachments is set on every ms pass.
- Depth: each consumer of depth runs resolve_scene_depth, a full-screen draw pass (:219-273): once per depth reader, guarded by depth_dirty.
- Bloom with MSAA copies the resolved scene once more (game_bloom_vk.cpp:287, P-025's pattern).
- Counts with MSAA 4x: render pass instances as above plus 1 per depth resolve; resolves 3 + 1 per COPY_FRAME; full copies 1 (bloom).

With interpolation on, each generated picture repeats the whole sequence (draw_walker.cpp:1860-2060), so the per-game-frame counts are this times (1 + generated pictures).

### R3.4 texture_store.cpp (read completely, 1-1526)

What it does each frame on the render thread:
- begin_frame (:694-753) frees the slot's retired views and recycles staging chunks. It takes `lock_` twice, uncontended unless the game thread is staging at that moment.
- apply_ops (:773-799) creates and uploads the textures the game registered, in one batch per frame.
- handle(id) (:1103-1138) runs per texture named by a table walk: two stores and a branch.
- enforce_budget (:1174-1249) returns before its O(textures) scan unless memory is over budget.
- apply_texture_packs (:1299-1461) has a 3 ms per-frame budget (12 ms behind a loading screen).
- set_movie_override (:1463-1524) uses a ring of plane textures and makes new ones only on a size change.

The waits, locks, allocations and compiles a renderer frame sees from here:
- Waits: backend_.begin_frame (:708) is the texture backend's own wait for the copy queue (read in Round 1 for D3D12; the Vulkan and Metal backends are outside this round's list).
- Locks: `lock_` in begin_frame (:725, :736), take_ops, request_resize. All are short.
- Allocations:
  - a new GPU texture per registered image (apply_ops → make_own :843);
  - a view per texture;
  - a 16 MB staging chunk when the arena is full (:399-405);
  - a pack's texture per replacement.
  - All of these follow the game's loading, not the frame.

- P-059 (performance, loading) Every texture record with authored-looking levels is analysed on the game thread when it is registered, and again on every budget resize and every update. The analysis allocates about 17 bytes of floats per base texel.
  - Evidence:
    - texture_store.cpp:133-270 detail::authored_levels expands the whole first level to RGBA8 (:147-148). It allocates four colour vectors of (w/2)·(h/2)·3 floats and an alpha vector (:153-158), box-filters them four ways, and compares each level.
    - It is called from stage_image (:517-518) for every non-generated record.
    - stage_image runs from register_image (:550), update_image (:582), take_ops' budget resizes (:665) and restart (:372). For a resize the flag is recomputed over the full first level (:517 runs before the `drop` branch at :519) although the record is the same bytes as when it was registered.
  - Why: a 1024x1024 record costs about 4 MB plus 12 MB plus 1 MB of short-lived heap and a few full passes on the game thread at each of those points. Under memory pressure enforce_budget asks for batches of resizes (:1245), and each one repeats the analysis on the game thread that also runs the game step. The answer depends only on the record's bytes. Fix:
    - Keep the flag with the id in `sources_` (or with the record's name hash) and skip the analysis on resize and on restart.
    - Better, compute it once when the texture is cooked and store it in the record's flags, as NEBULA_TEXTURE_GENERATED_LEVELS already is (:516). That moves it off the device entirely.
  - Confidence: verified in code. The cost per record is suspected, needs a run: `--diagnostics timeline` over a galaxy load, timing register_image calls, or a counter around authored_levels.

Checked and correct:
- A texture is never shown blank during a resize: the old one draws until the new one is made (:935-977), and an out-of-memory failure keeps the old (:950-958).
- Retired views go free only after their slot's fence (:713-720).
- The movie planes use a ring, so frames in flight keep reading theirs (:1481-1509).
- Pack uploads have a per-frame time budget with at least one per frame (:1320-1329), so a pack never stalls a frame for long.

### R3.5 D3D11 (renderer_d3d11.cpp 1-1396 and renderer_d3d11_scene.cpp 1-1371 read completely)

Structure, as read. D3D11 has no render passes. The scene pass is OMSetRenderTargets plus a recorded op list (`pass_ops_`) that `flush_draws` (renderer_d3d11.cpp:1006-1118) replays when the pass ends, before a clear, or before any call that reads a target. The rings (:428-447) keep no history, so D3D11 has no motion vectors: no temporal upscaler, no frame generation. A buffer that has grown is retired to `frame_->retired_buffers` without a wait. `resize_swap_chain` (:323-347: Flush, then ResizeBuffers) runs only between frames. `apply_window_size` (scene.cpp:211-263) calls `wait_for_gpu_idle` (:245), also between frames, on a changed internal size only. Dynamic resolution moves a rectangle inside the fixed target (`begin_scene_rectangle` :700-733), with no reallocation.

#### P-060 performance: every recorded draw renames the root constant buffer with Map(WRITE_DISCARD)
- **Evidence:** renderer_d3d11.cpp:1056-1063 (in `flush_draws`): per draw op, `ctx->Map(root, 0u, D3D11_MAP_WRITE_DISCARD, ...)`, a memcpy of the 32-byte DrawRoot, then Unmap. The material constants in the same loop (:1041-1054) already use a ring with `VSSetConstantBuffers1`/`PSSetConstantBuffers1` offsets. `utility_draw` (:1126-1157) does the same Map discard on the 256-byte `utility_constants_` for every utility draw.
- **Why and fix:** each WRITE_DISCARD makes the driver rename the buffer. Over a few thousand draws a picture, the driver's rename pool turns over every frame, and with interpolation on every generated picture repeats the walk. Write the DrawRoots into the same constant ring the materials use (256-byte aligned slots, one Map NO_OVERWRITE per ring chunk) and bind each draw's slot with `*SetConstantBuffers1` offsets. The utility draws can share that ring.
- **Confidence:** verified in code (the per-draw Map). The cost is suspected; it needs a run: a PIX or GPUView capture of one picture at Good Egg start, D3D11, with the API call count and the time spent in Map before and after.

#### P-061 performance (and a limit that can fail): the D3D11 sampler cache is never trimmed, and dynamic resolution keeps adding keys
- **Evidence:**
  - `sampler_for` (renderer_d3d11.cpp:1161-1206) creates an ID3D11SamplerState for each new key and caches it in `samplers_` for good.
  - `sampler_table_for` (:1260-1277) makes a new table (`make_unique`) for each new key set and caches it without a bound.
  - `reset_sampler_frame` (:1280-1291) rebuilds only on a config change, and never clears `samplers_`.
  - The keys hold the scene LOD bias and the mip-line count that follow dynamic resolution's rectangle, the same keys that fill D3D12's heap (P-048) and Vulkan's pool (P-056).
- **Why and fix:** D3D11 allows at most 4096 unique sampler state objects per device. Once a play session has stepped through enough rectangle sizes, CreateSamplerState fails. Before that point, every new step costs a create inside the frame. Make the same change as for P-048: quantise the bias and mip lines to the dynamic-resolution steps (`kResolutionSixteenths`), or move the bias into a shader constant so it drops out of the key. Then the key set is finite and small.
- **Confidence:** the unbounded cache is verified in code. Reaching the 4096 limit is suspected and needs a run: dynamic resolution on, `--diagnose resolution-steps`, a long run through several galaxies on D3D11, logging `samplers_.size()` at each step change.

#### P-062 crash: three D3D11 in-frame allocations throw std::runtime_error on failure
- **Evidence:** renderer_d3d11_scene.cpp:
  - :604, a picture's channel copy;
  - :845, a fresh scene colour in `swap_scene_color`, which every bloom HOLD and simple-bloom trade calls;
  - :1197, the screen probe buffers.

  Allocation failure on device removal or low memory therefore throws out of the frame. The same allocation pattern on the other paths (`draw_frame_pass` :878-886, `publish_scene_depth_copy` :746-751) logs and returns false instead.
- **Why and fix:** a failed allocation inside the frame should drop that pass, as the frame-pass copies already do, and leave device-lost handling to its own path. Make these three return false and skip the pass, the same way `draw_frame_pass` does.
- **Confidence:** the throws are verified in code. Where the exception is caught, and whether it ends the process, was not traced: renderer_d3d11.cpp has a `catch (const std::...)`, but its scope was not checked against these call sites.

Other D3D11 notes, verified in code:
- The D3D11 scene capture records its width and height at copy time (scene.cpp:1301-1302). D3D12's P-051 does not; D3D11 is correct here.
- `publish_picture` (scene.cpp:575-614) makes a full-size conversion draw on every publish of a picture with a channel swizzle (ALPHA, RED or RGB), because D3D11 views cannot repeat a channel. This is needed: there is no D3D11 equivalent.
- `draw_shown_frame_pass` (scene.cpp:895-922) uses CopyResource of the whole shown picture into the scene target, then a conversion draw. It is the D3D12 shape.
- `end_wide_scene` (scene.cpp:1116-1157) resolves the wide colour (:1123) even when `scene_camera_.record` is handed the multisampled wide colour (`in.wide_ms`, :1142). Whether that resolve is read is decided in scene_camera_d3d11.cpp (see R3.5b). The wide scene is off by default.
- DiscardView exists only in post_process_d3d11.cpp:446 and :597. The scene target, its MSAA targets and its depth are never discarded. On desktop D3D11 hardware this costs nothing measurable; the matching cost on a tiler is P-006 (Vulkan).

P-062 addendum: a fourth throw sits at renderer_d3d11.cpp:1302 (the zero stream, `zero_stream_address`, reached from a draw).

#### P-063 performance (D3D11 parity): D3D11 scene AA always copies the scene; D3D12 and Vulkan trade the image instead
- **Evidence:**
  - renderer_d3d11_scene.cpp:937 calls `post_.record_scene_aa(...)` without the `trade` argument, which defaults to empty (post_process_d3d11.h:61).
  - So post_process_d3d11.cpp:447-450 always runs `CopySubresourceRegion` of the scene into `antialiased_` before SMAA or FXAA.
  - D3D12 (renderer_d3d12.cpp:5723-5726) and Vulkan (renderer_vk_frame.cpp:1836-1839) pass `swap_scene_color` as the trade when `can_trade_scene()`. D3D11 already has the matching `can_take_scene` (scene.cpp:677-681) and `swap_scene_color` (:832-853), and the D3D11 post processor already implements the traded path (:439-446, with DiscardView).
- **Why and fix:** AA is off by default (render_config.h:96 `aa_mode = 0`). With SMAA or FXAA on, every D3D11 picture pays one full-frame copy that the other two renderers skip. Pass `[this](gpu::TextureRef spare) { return swap_scene_color(std::move(spare)); }` when `can_take_scene()`, as D3D12 does.
- **Confidence:** verified in code. That the output matches byte for byte is suspected, as the traded path is the one D3D12 already ships. It needs a run: SMAA High on D3D11, a scene capture before and after, compared.

#### P-064 performance: D3D11 upload_written scans every upload buffer on every flush, allocates 32 KB each time, and copies each written page into every video-memory copy of the buffer
- **Evidence:** d3d11_context.cpp:505-549. It is called at the top of `flush_draws` (renderer_d3d11.cpp:1011), and `flush_draws` runs at every scene-pass end, every clear, `bind_scene` and every part.
  - (a) A local `std::vector<void*> pages` is resized to 4096 entries (:507, :513), so every call heap-allocates 32 KB.
  - (b) Every call makes one `GetWriteWatch` system call per upload buffer (:517). That covers the rings, the mesh pages (registries.cpp:229) and the material slot page (registries.cpp:653), written or not.
  - (c) Each run of written pages is copied with `UpdateSubresource1` into each of up to three video-memory copies (`Buffer::gpu[]`: Geometry, Constants, Rows, made lazily by `Buffer::bound`, :446-465). A ring holding both draw constants and vertex data therefore uploads every page two or three times.
- **Why and fix:** these costs grow with the number of flushes per picture (the target passes, COPY_FRAMEs and bloom each flush), not with the data written.
  - Hoist `pages` into a Context member.
  - Track "written since the last upload" per buffer, with a flag set by the ring's `allocate`, so unwritten buffers skip the system call.
  - Give each use its own ring: D3D11 cannot combine `D3D11_BIND_CONSTANT_BUFFER` with other bind flags, so separate rings are the way to upload a page once.
- **Confidence:** (a) and (b) are verified in code. (c) is verified for the mechanism. That one ring carries more than one use was not traced to the ring setup. It needs a run with `--diagnostics gpu-passes` on D3D11, counting UpdateSubresource1 bytes per picture, before and after.

#### D3D11 typical frame (bloom, no mirror, no mods, interpolation on, native resolution, no MSAA, SDR, AA off, enhanced lighting off, AO off)
D3D11 has no load/store actions. "Load" below says whether the span starts on kept, cleared or discarded contents. Desktop D3D11 drivers keep every target, so there is no store to drop except through DiscardView.

| # | Span or copy | Caused by | Target, size | Load / store | Mark |
|---|---|---|---|---|---|
| 1 | Each target pass | the list's target-pass records (`begin_target_pass` scene.cpp:487-513, clear at draw_walker.cpp:1652) | its key's RGBA8 set + depth, the size asked | whole ClearRenderTargetView + ClearDepthStencilView / kept | needed. With an ALPHA, RED or RGB channel swizzle, +1 full-size conversion draw per publish (scene.cpp:597-608): needed, because D3D11 views cannot repeat a channel |
| 2 | An over-frame lend (COPY_FRAME of the whole frame) | `open_frame_for_pass` scene.cpp:783-813 | none: the scene colour and depth are published as pictures | — | needed (no work) |
| 3 | Scene draws | the scene's passes, replayed by `flush_draws` | scene colour RGB10A2 + depth, render size | cleared by the game's clear / kept | needed |
| 4 | Bloom HOLD | NEBULA_PASS_BLOOM_HOLD (scene.cpp:998-1006) | colour traded with the kept image (no copy), then ClearRenderTargetView | cleared / kept | needed |
| 5 | Bloom NORMAL | game_bloom_d3d11.cpp:231-381 | 6 or 7 compute dispatches on reduced R16G16B16A16F images (box across only when `s.box > 1`), then 1 full-size composite draw into the scene colour | composite overwrites every pixel of the frame part | needed |
| 6 | HUD draws | the 2D passes | scene colour | kept | needed (same target) |
| 7 | walk_show | the list's end (`take_frame_picture` scene.cpp:855-861) | colour swap, a fresh target from `swap_scene_color` | — | no GPU work |
| 8 | Present | `draw_presented_picture` renderer_d3d11.cpp:1314-1335 | back buffer | full ClearRenderTargetView every frame, then the copy draw into the content rectangle | the copy is needed. The clear can be dropped without changing a pixel when the content rectangle is the whole back buffer: only borders need it |

Counts per presented picture (no MSAA):
- full-target copies: 0;
- resolves: 0;
- full-size draws: 3 (scene, bloom composite, present), plus 1 per swizzled target-pass publish.

The flushes that run upload_written (P-064) are one per span end.

With MSAA 4x:
- #3 draws into the 4x set.
- Bloom resolves first (`resolve_scene_color`, :985), then HOLD copies the resolved frame into `keep_` (`bloom_.keep`, game_bloom_d3d11.cpp:201-205, 124-132) and SIMPLE copies it into `source_` (:243, :269). Nothing writes `scene_.color` between the resolve and the copy, because the composite writes the 4x RTV. This is P-025 on D3D11: the copy can be dropped by reading `scene_.color` directly.
- Each COPY_FRAME resolves (`resolve_scene_color`, guarded by `color_dirty`). D3D11 is the one renderer with an "already resolved" flag, which is what P-053 asks D3D12 to add.
- At the scene's end the walker calls `begin_output_scene(true)` (draw_walker.cpp:1601). D3D11 (scene.cpp:444-475) resolves, makes a 1:1 conversion draw into the single-sample output set, swaps sets and clears its depth. The HUD then draws single-sample, and show needs no resolve.
- Counts: resolves 2 (bloom, output scene) + 1 per COPY_FRAME; full copies 1 (bloom, P-025); full-size draws 4 (scene, composite, output-scene conversion, present); depth resolves (a full-screen draw, :183-196) 1 per depth reader, guarded by `depth_dirty`.

#### Erratum to the D3D12 and Vulkan MSAA 4x tables above
The walker's `begin_output_scene(true)` at the scene's end (draw_walker.cpp:1601) was missed in both tables.

On D3D12 (renderer_d3d12.cpp:6392-6442) it resolves, draws a 1:1 conversion into the single-sample output set (`record_conversion_blit`, :6436), swaps and clears its depth. On Vulkan (`enter_output_scene` renderer_vk_frame.cpp:1659-1690) it draws the same conversion in a new render-pass instance.

Corrections:
- D3D12 rows 5-6: the HUD draws single-sample into the output set, and there is no resolve for show. The resolve count stays 2 + 1 per COPY_FRAME, because the output-scene entry replaces the show resolve. Add 1 full-size conversion draw and 1 depth clear.
- Vulkan: add 1 render-pass instance (conversion; colour UNDEFINED → cleared depth / STORE all) and 1 full-size draw. The HUD then runs single-sample, so #4's composite instance is the last 4x instance.

The conversion is the display copy's DisplayFilter draw when the step is 1:1 (scene.cpp:656; renderer_vk_frame.cpp:1664). Whether it is a pure copy, which would let the resolve write the output set's colour directly and drop the draw, depends on that filter's shader; it was not read. Treat it as needed.

#### D3D11 waits, locks, allocations and compiles inside a frame
- **Waits:**
  - `begin_frame` waits for its frame slot's fence (renderer_d3d11.cpp:534-535). This is the frame pacing wait, expected.
  - `finish_pictures` waits for a fresh fence (scene.cpp:1272); test captures only.
  - `apply_window_size` calls `wait_for_gpu_idle` (scene.cpp:245) only on an internal-size or MSAA change, between frames.
  - With no ID3D11Fence, `Context::signal` waits on the event query 8 values back (d3d11_context.cpp:276-278), so at most 8 signals are in flight.
  - The D3D11 renderer has no mid-frame GPU wait in the typical frame.
- **Locks:**
  - `buffers_mutex_` in every `upload_written` (d3d11_context.cpp:506);
  - `address_mutex_` when an upload buffer is made or freed;
  - the surface lock in present (renderer_d3d11.cpp:1337-1394).
- **Allocations in the typical frame:**
  - the 32 KB `pages` vector per flush (P-064);
  - the fresh scene colour at walk_show when no spare is handed back (`swap_scene_color` :841-847);
  - `make_unique` sampler tables and samplers for new keys (P-061);
  - `probe_slots_.resize` (scene.cpp:1162-1163; probes run on real pictures only);
  - `Buffer::rows_view` CreateShaderResourceView for each new row offset, with up to `kRowViews` kept (d3d11_context.cpp:467-491).
- **Compiles:**
  - all D3D11 utility, bloom, AO, camera and post shaders are created from DXBC at initialize;
  - SMAA quality changes recreate three shaders from DXBC (post_process_d3d11.cpp:116-136);
  - the game's programs come through core/pipelines.cpp (Round 1);
  - the only D3DCompile is for mods (compile_stage_d3d11.cpp), out of scope.

Other D3D11 files read completely:
- game_bloom_d3d11.cpp (421 lines). It matches D3D12's pass for pass. Its images are sized for the lowest dynamic-resolution step, so a step change makes none again (:250-253).
- post_process_d3d11.cpp (641 lines). The self-test Maps a readback at initialize, which waits for the GPU (:519-521), at startup only.
- scene_camera_d3d11.cpp (153 lines). The meter reads the resolved wide colour (:130), so the resolve in `end_wide_scene` (scene.cpp:1123) is needed. This closes the open question in R3.5.
- ambient_occlusion_d3d11.cpp (249 lines). Its object lists use Map WRITE_DISCARD on dynamic buffers grown by `bit_ceil`, and `bin_objects` reuses its member vectors.
- d3d11_context.cpp (615 lines).
- texture_backend_d3d11.cpp (149 lines). R8 and RG8 textures are widened to RGBA8 (:75-91, :128-143), which is 4x and 2x their memory. It is needed for the swizzle: D3D11 views cannot repeat a channel. A `std::vector` is allocated per widened upload, at load time.
- encoder_d3d11.cpp (87 lines).
- compile_stage_d3d11.cpp (245 lines, mods only).
- rt_backend_d3d11.cpp (69 lines, stubs).
- mod_post_d3d11.cpp (209 lines) was not read: it is mods, out of scope.

### R3.6 Metal

#### mtl/renderer_mtl.mm (read completely, 1-1244)
- **Frame:**
  - `begin_frame` (:561-696) waits for the slot's fence (:580-581, the pacing wait).
  - It also waits for the ring history segment's fence (:677-679). That segment is 2+ frames old, so the wait normally returns at once.
  - It makes a "texture uploads" command buffer and blit encoder every frame (:658-671). `end_frame` commits that buffer only when something was uploaded (:715-718).
  - `next_command_buffer` (:537-559) makes one command buffer per frame, or one per GPU pass when passes are timed.
  - `end_frame` (:698-879) adds two completed handlers (two block allocations) every frame (:722-727). It presents with `presentDrawable` on the last list (:769).
- **Present** (:1140-1242):
  - `nextDrawable` is called while holding `host_window::surface_use` (:1148, :1171), so a window thread wanting the surface waits behind the drawable wait.
  - When the drawable wait was longer than `kDrawableWaitMs`, the whole frame is drawn but not shown (:1189-1194), never twice running.
  - `draw_presented_picture` (:1113-1138) opens one colour pass with a clear (`begin_color_pass(target, true, ...)`, :1122) and draws the post chain's present step or the blit.
- **Parity, verified in code:**
  - `wants_hdr()` returns false (:226-228). Metal has no HDR output.
  - `report_adapter` reports TAA as "motion vectors, jitter, and history resources are not built" (:211). Yet the Metal renderer has `scene_temporal_`, `prepare_temporal_upscaler` (:566) and `motion_` targets (:1067). The feature status shown to the player may disagree with what runs; see the renderer_mtl_fx.mm notes below.
- **Waits, locks, allocations and compiles inside a frame:**
  - the two fence waits above;
  - the surface lock across `nextDrawable`;
  - `capture_target_` and `backbuffer_capture_.buffer`, made lazily on a test capture only (:1206-1217);
  - `utility_pipeline(key)` for the present blit (:1134), compiled on the first use of a key (see pipelines_mtl.mm).
- **Allocation-failure throws** in the frame, the same pattern as P-062:
  - :541, no command buffer;
  - :624, a command buffer failed;
  - :1144, present without a source.

#### mtl/renderer_mtl_scene.mm (read completely, 1-1263)
- **Scene pass descriptor** (:374-426):
  - Colour load is CLEAR for a whole clear, DONTCARE when the colour is about to be replaced (`drop_color`), LOAD otherwise. Colour store is STORE, or with MSAA `StoreAndMultisampleResolve` into `scene_.color`.
  - Depth load is CLEAR (whole clear or depth-only clear) or LOAD; depth store is always STORE. Stencil load is CLEAR or LOAD; stencil store is always STORE.
  - The load side is better chosen than Vulkan's: Metal has the depth-only clear and the colour discard. The store side is P-031.
- **Own passes:**
  - `begin_color_pass` (:158-171): colour CLEAR or DONTCARE / STORE. It is used for present, scene copies and the depth picture.
  - The depth resolve (:272-301): depth DONTCARE / STORE, stencil DONTCARE / DONTCARE.
  - `clear_textures` (:178-191) opens and ends a pass only to clear.
- **Channel swizzles** use texture views (`newTextureViewWithPixelFormat:...swizzle:`, :840-856), so there is no copy. D3D11 needs a conversion draw for the same thing.
- **draw_frame_pass** (:1195-1251) trades the scene image to the capture and draws it back as the pass reopens, colour DONTCARE (:1213-1228), as Vulkan does. Otherwise it makes a conversion draw into a kept destination.
- **Waits:** `apply_window_size` calls `wait_for_gpu_idle` (:353) only on a size or MSAA change, between frames.
- **Allocations in a frame:**
  - a fresh scene colour in `swap_scene_color` when no spare is handed (:1131-1137, throws on failure, :1135);
  - probe visibility buffers, grown once per frame slot (:465-468);
  - the depth picture and frame-pass destinations on a size change;
  - a swizzled texture view per publish of a swizzled picture (:845), which is cheap.

#### P-065 performance (hitches; Metal): the renderer's own shaders are compiled from MSL source on the render thread the first time each is used
- **Evidence:**
  - `utility_function` (renderer_mtl_scene.mm:46-65) calls `newLibraryWithSource:` on `mtl::utility_msl(shader)` the first time a utility shader is asked for.
  - `utility_pipeline` (:67-94) then calls `newRenderPipelineStateWithDescriptor:` synchronously for each new key: shader, colour formats, write mask, sample count and depth.
  - At initialize only the screen-probe compute function is made (renderer_mtl.mm:178-185). Everything else is made inside a frame on first use: the present blit, scene clears, depth clears, depth resolve, conversion and display filter, and depth copy. That covers each format and sample variant, for example the first MSAA frame or the first frame-pass copy of a new channel layout.
- **Why and fix:** a source compile of an MSL library takes tens of milliseconds on the render thread, plus the pipeline build. The game's programs avoid this through pipelines_mtl.mm (see below), but these renderer shaders do not.
  - Build the utility shaders into a `.metallib` at build time with `xcrun metal` and load it with `newLibraryWithData:`.
  - At initialize, make every pipeline the current layout can need (scene colour, target colour, wide colour × sample count) on a worker, as the post chain and bloom already compile on a worker (renderer_mtl.mm:108-109).
  - Keep an `MTLBinaryArchive` for the pipelines.
- **Confidence:** verified in code: source compiles, lazy, render thread. The size of the hitch is suspected; it needs a run on an Apple-silicon Mac with `--diagnose gpu-passes`. Read the first frame's time, and the first frame after MSAA 4x is switched on, with and without the change.

#### mtl/renderer_mtl_draw.mm (read completely, 1-1154)
- **Parallel recording** (:311-454):
  - On Apple GPUs every scene pass becomes an `MTLParallelRenderCommandEncoder`, and chunks run inside the open pass (`parallel_record_in_pass`). There is no extra store or load.
  - On Intel and AMD Macs the open pass ends, and each run of chunks gets its own command buffer and pass, with LOAD/STORE on everything (:336-362). That is an immediate-mode GPU, so the cost is small.
- **Screen probes:**
  - Inside an open single-sample scene pass, compared probes are one-pixel depth-tested draws with visibility results (:527-567), so the pass never ends for a depth read. This is a Metal-only design that saves a store and load on a tiler.
  - Otherwise they are held until the pass ends, then a compute dispatch (:570-588).
- **Output scene** (`enter_output_scene` :1089-1113): the conversion is the first draw of the cleared output-scene pass, which the HUD continues. So on Metal the MSAA 4x output-scene entry costs no extra pass instance: the resolve happens in the scene pass's store (`StoreAndMultisampleResolve`).
- **Temporal:**
  - MetalFX temporal and SGSR2 are wired (`temporal_mode` :758-762, `prepare_temporal_upscaler` :768-811, `record_temporal_upscale` :1005-1046).
  - `prepare_temporal_upscaler` calls `wait_for_gpu_idle` when the sizes or mode change (:774, :795). It runs in `begin_frame` and only on a change.
  - `report_adapter`'s TAA status says the opposite (renderer_mtl.mm:211). `caps.taa` is read nowhere in runtime/src except `sanitize_feature_status` (render_config.cpp:373), so the stale text has no effect beyond diagnostics. Maintainability.
- **Allocations in a frame:**
  - `texture_table_for` grows `frame.tables` once (:188-190);
  - `write_texture_list` remakes its buffer when full (:142-155);
  - `persistent_texture_table` `make_unique`s when no freed table is left (:214);
  - `sampler_table_for` `make_unique`s per new key (:235);
  - the ensure_motion_targets set, on a size change;
  - per-probe vectors (`answers.assign`, `drawn.assign`, :535-536).
- **Throws inside the frame:**
  - :131, more new samplers in one frame than the sampler array holds;
  - :151, texture lists;
  - :292, zero stream;
  - :351 and :405, parallel pass;
  - :478, probe buffers.

  These follow the P-062 pattern.

#### P-066 performance: Metal waits for the GPU whenever its sampler array nears full, and in bound-table mode its sampler cache never shrinks; dynamic resolution keeps adding keys (P-048/P-056/P-061 on Metal)
- **Evidence:**
  - `reset_sampler_frame` (renderer_mtl_draw.mm:248-281) runs at every `begin_frame` (renderer_mtl.mm:648). With arrays, once `sampler_slots_.used()` plus a frame's room exceeds the array, it calls `wait_for_fence_value(last_submitted_)` (:256-258). That waits for all submitted work, then rewrites every slot.
  - Without arrays (`bindless_` false: older Macs and iOS devices below `sampler_array_max >= 512`, renderer_mtl.mm:151), `samplers_` and `sampler_tables_` are never cleared except on shutdown.
  - `sampler_for` keys hold `key.lod_bias` (:98) and `min_lod`/`max_lod` (:93-94), the dynamic-resolution values that cause P-048.
- **Why and fix:** the same root cause and the same fix as P-048: quantise the bias and LOD clamps to the resolution steps, or move the scene bias into a shader constant. Then the key set is small and finite, the array never fills during play, and the bound-table cache stays bounded. Metal's per-device limit on distinct sampler states in argument buffers makes the unbounded case a correctness risk too: `newSamplerStateWithDescriptor` returning nil throws (:104-106).
- **Confidence:** verified in code (the wait, the unbounded maps, the keys). How often it happens is suspected and needs a run on an Apple-silicon Mac with dynamic resolution on and `--diagnose resolution-steps`, counting `reset_sampler_frame` waits (add a counter) over a galaxy tour.

#### mtl/renderer_mtl_fx.mm (263 lines), pipelines_mtl.mm (588 lines) and upload_memory_mtl.mm (38 lines), all read completely
- **Scene AA:** `record_scene_aa` trades the scene image when `can_trade_scene()` (fx.mm:36-39), as D3D12 and Vulkan do (D3D11 does not: P-063).
- **Bloom:**
  - HOLD single-sample trades (:92-98).
  - MSAA HOLD calls `bloom_.keep(commands_, in)` (:100), a copy of the resolved colour. This is P-025's pattern; it is confirmed in game_bloom_mtl.mm only if `keep` copies, and that file is item 7, not yet read.
  - SIMPLE trades its source when whole and single-sample (:128-132).
  - The composite reopens the scene pass with the colour DONTCARE when it covers the whole target (:136).
- **Wide scene** (:150-261): the widen draw runs in the reopened pass with the colour DONTCARE (:211). With MSAA the camera writes the single-sample output set straight from the 4x wide colour (:247-259).
- **Game programs** (pipelines_mtl.mm):
  - A build made with Apple's compiler ships `programs_2_4.metallib` and `programs_3_0.metallib`, and each program is a 40-byte record naming its function (:86-103, :213-235, :246-269). Otherwise, as in a development build, the MSL text is compiled with `newLibraryWithSource` (:270-302).
  - Pipelines are built by the pipeline workers (`Pipelines::build` :369-548, synchronous inside the worker).
  - `build_fast` returns nullptr (:365-367): Metal has no fast-link path, so a draw that waits for its pipeline waits for the whole build.
  - `save_cache` writes nothing and relies on the system's Metal cache (:569-577). The comment there records that an `MTLBinaryArchive` was tried and failed on AMD ("Failed to find linking data") and lost entries with concurrent adds.

  Correction to P-065's fix: drop the `MTLBinaryArchive` suggestion. Building the renderer's utility shaders into the app's metallib, as the game programs already are, plus making the pipelines at initialize, is enough.
- **Upload memory** (upload_memory_mtl.mm:9-26) ignores `UploadPlace`. The vertex and constant rings ask for `GpuLocal` (renderer_mtl.mm:95-96), and so do the mesh pages (registries.cpp:229). On Apple silicon (unified memory) Shared is the right choice. On an Intel Mac with a Radeon, meshes would be read over PCIe every frame, but that build ships nowhere (P-035).
- **DeviceCaps:**
  - `caps.memoryless` is set (mtl_context.mm:129) and logged (:264), but nothing in runtime/src/render/mtl reads it. That is the hook P-031's fix needs, already computed and unused.

#### Metal typical frame (bloom, no mirror, no mods, interpolation on, native resolution, no MSAA, SDR, AA off; Apple GPU)

| # | Pass (MTLRenderCommandEncoder) or blit | Caused by | Target, size | Load / store | Mark |
|---|---|---|---|---|---|
| 1 | Each target pass | target-pass records (`begin_target_pass` scene.mm:734-761, then the walker's whole clear) | its key's set (RGBA8 + D32FS8) | colour, depth and stencil CLEAR / colour STORE, depth STORE, stencil STORE | colour STORE needed. The depth and stencil STORE can be dropped without changing a pixel: only the colour is published (`end_target_pass` :763-780) |
| 2 | Over-frame lend (COPY_FRAME of the whole frame) | `open_frame_for_pass` :1075-1105 | the scene colour (and depth) published | the pass ends: a full STORE, and the next opening LOADs | needed (no copy). On a tiler, the store and reload of the colour and depth is the cost of the lend |
| 3 | Scene pass | the scene's first whole clear (`clear_scene` :625-627 opens it cleared) | scene colour RGB10A2 + D32FS8 | CLEAR all / STORE all | needed. Depth STORE is needed: the glow pass reloads it |
| 4 | Bloom HOLD: the colour trades, then the glow pass | `game_bloom` fx.mm:91-98 | fresh colour + same depth | colour CLEAR (via `clear_scene`), depth LOAD / STORE all | needed. The depth STORE at the glow pass's end can be dropped if nothing later reads depth (same caveat as Vulkan #3) |
| 5 | Bloom NORMAL: compute work, then composite + HUD | fx.mm:101-141, bloom_.prepare | scene colour + depth | colour DONTCARE (whole), depth LOAD / STORE all | needed. The depth LOAD and STORE can be dropped if the composite and HUD do not test depth (not verified for every 2D draw) |
| 6 | walk_show | `take_frame_picture` :1145-1151 | colour swap | — | no GPU work |
| 7 | Present | `draw_presented_picture` renderer_mtl.mm:1113-1138 | drawable, layer size (BGR10A2 on macOS) | CLEAR / STORE | needed |

Counts per presented picture (no MSAA):
- render command encoders: 4 (scene, glow, composite+HUD, present), plus 1 per target pass, plus 1 per COPY_FRAME reopen;
- copies: 0;
- resolves: 0;
- stores that change no pixel: depth+stencil at each target pass's end, and depth+stencil at the frame's last scene pass (P-031).

With MSAA 4x:
- #3-#5 attach the 4x colour and depth. Every end does `StoreAndMultisampleResolve` on the colour and STORE on the 4x depth and stencil (scene.mm:384-403).
- The resolve is free at the end of a tile pass. The 4x STOREs are not, which is P-031's main cost.
- The walker's `begin_output_scene(true)` draws the conversion as the first draw of the output-scene pass (draw.mm:1089-1113). That adds 1 encoder, and the HUD then runs single-sample in it.
- Bloom with MSAA calls `keep` or a copy of the source (P-025 pattern, pending item 7).
- Depth readers run `resolve_scene_depth` (scene.mm:272-301), a draw pass with depth DONTCARE / STORE and stencil DONTCARE / DONTCARE.
- Counts:
  - encoders 5 + target passes + COPY_FRAME reopens + depth resolves;
  - explicit resolves 0 (all in stores);
  - full copies 1 (bloom keep or source, pending item 7).

#### Metal paths only Apple silicon reaches (the shipped hardware), with no recorded run (P-035)
Each is reached only when `caps.apple_gpu` (mtl_context.mm:102) or a capability only Apple-silicon Macs have is set. The recorded runs and measurements are on Intel Macs (P-035).

1. **Parallel scene passes:** `parallel_scene_pass_ = caps.apple_gpu` (renderer_mtl_draw.mm:318). Every scene pass becomes an `MTLParallelRenderCommandEncoder`, and `parallel_record_in_pass` (:369-409) splits the open pass's encoders around the chunks. The Intel path (:336-362) is the one with runs.
2. **Texture and sampler arrays:** `bindless_` is set when `caps.texture_arrays` (argument-buffer tier 2, mtl_context.mm:106) and `sampler_array_max >= 512` (renderer_mtl.mm:151). This covers `write_sampler` via `gpuResourceID` (renderer_mtl_draw.mm:113-118, macOS 13+/iOS 16+), the sampler-array refill and wait (P-066), `use_textures` on every pass (scene.mm:515-528), and MSL 3.0 pixel programs (pipelines_mtl.mm:274-278). Whether the Intel Macs that were run report tier 2 was not checked, so this item is "likely untested".
3. **ASTC 4x4 textures:** `supports_astc4x4` returns `caps.apple_gpu` (texture_backend_mtl.mm:261-263).
4. **Shared, not Managed, storage** for the CPU-written buffers (mtl_context.mm:400).
5. **MetalFX temporal:** `caps.metalfx_temporal` (mtl_context.mm:131, renderer_mtl_draw.mm:761). It is reported on Apple silicon and some AMD GPUs. The recorded Intel integrated-GPU runs cannot reach it.
6. **Texture-size limits:** `max_texture_size` per Apple family (mtl_context.mm:136).
7. **iOS-only branches:** the BGRA8 layer, and VSync always on via `set_present_waits_for_refresh(true)` (renderer_mtl.mm:287-303). Whether an iOS target is built in CI was not checked in this round.

The rule for each: one recorded run on an M-series Mac. Good Egg, defaults, `--diagnose gpu-passes,present-stats`, with the log kept beside the Intel ones, before any of these is called verified.

#### mtl/game_bloom_mtl.mm (read completely, 1-476): item 7, Metal bloom
- **MSAA HOLD:** `keep` is a blit copy of the resolved `scene_.color` into `keep_` (:251-256, :195-214). This settles the pending P-025 question for Metal. Unlike D3D12 and D3D11, the copy cannot simply be dropped. Metal resolves the composite pass into `scene_.color` at its store (`StoreAndMultisampleResolve`), so the composite cannot sample `scene_.color` while it is that pass's resolve attachment.
- **Fix that keeps every pixel:** with MSAA, trade the resolved single-sample image out with `swap_scene_color(bloom_.take_kept_image())`, exactly as the single-sample HOLD does (fx.mm:92-98), so the composite's resolve lands in a fresh texture. `swap_scene_color` swaps only `scene_.color`, the single-sample resolve target, so it works with MSAA too. The same reasoning applies to Vulkan, whose resolve attachment is written at the pass end. P-025's "read it directly" fix fits D3D12 and D3D11 only.
- **Bloom programs** are compiled from MSL source on a worker (`std::async`, :46-68), as the comment in renderer_mtl.mm:108-109 says. But `composite_pipeline` (:150-181) is built synchronously on the render thread the first time each colour format × sample count is used: the first bloom frame, the first MSAA bloom, the first wide-scene bloom. Add these to P-065's list.

#### Parity table (Round 3, verified in the files read)

| Feature | D3D12 | Vulkan | D3D11 | Metal |
|---|---|---|---|---|
| Scene AA trades the image instead of copying | yes (:5723-5726) | yes (frame.cpp:1836-1839) | no, always copies (P-063) | yes (fx.mm:36-39) |
| Bloom HOLD single-sample trade | yes | yes | yes (scene.cpp:998-1006) | yes |
| Bloom MSAA: copy after resolve (P-025) | copy, droppable | copy (game_bloom_vk.cpp:287), trade fix | copy, droppable | copy (blit), trade fix |
| "Already resolved" flag for the colour | no (P-053) | n/a: resolves at the pass end | yes, `color_dirty` | n/a: resolves at the store |
| Channel swizzle of a target picture | view | view | conversion draw per publish (D3D11 has no swizzle) | texture view |
| Motion vectors and temporal upscalers | FSR 3, DLSS, XeSS | FSR 3, DLSS, SGSR2 | none (rings keep no history) | MetalFX temporal, SGSR2 |
| Frame generation | FSR 3, DLSS-G | FSR 3, DLSS-G (P-030 waits) | none | none |
| HDR output | yes | yes | (not read in Round 3) | no: `wants_hdr()` returns false (renderer_mtl.mm:226-228) |
| Memoryless or transient MSAA and depth | n/a | no (P-006) | n/a | no (P-031); `caps.memoryless` computed and unused |
| Depth or stencil store dropped when unread | n/a | no (P-006) | n/a | no (P-031) |
| Sampler cache bounded under dynamic resolution | drains with a GPU wait (P-048) | resets with a GPU wait (P-056) | unbounded (P-061) | waits when the array is full; unbounded in bound-table mode (P-066) |
| Renderer utility pipelines precompiled | no: lazy, uncached (P-052) | lazy, through the pipeline cache | DXBC at initialize | no: lazy MSL source compiles (P-065) |
| In-frame allocation failure | (mixed) | (mixed) | throws (P-062) | throws (several sites, R3.6) |
| In-pass screen probes | no | no | no | yes (visibility results) |
| Parallel recording | n/a | n/a | n/a | parallel encoders on Apple GPUs |

#### Round 3 status
- Items 1-6 were read completely.
- Item 7 (the post chain) was only partly reached. Read: the D3D11 post, bloom, camera and AO files, and game_bloom_mtl.mm. Not read: post_process_vk.cpp, post_process_mtl.mm, post_process.cpp outside 545-780 and 937-1059, game_bloom.cpp 1-215, game_bloom_vk.cpp, ambient_occlusion.cpp, ambient_occlusion_vk.cpp, ambient_occlusion_mtl.mm, scene_camera.cpp, scene_camera_vk.cpp, scene_camera_mtl.mm, temporal_upscaler.cpp, temporal_upscaler_vk.cpp, xess_upscaler.cpp, sgsr2_vk.cpp, sgsr2_mtl.mm, metalfx_mtl.mm, streamline_bridge.cpp, streamline_vk.cpp, fsr_frame_generation.cpp, fsr_frame_generation_vk.cpp.

## Round 4

### R4.1 native/host/host_main.cpp (read in order; notes written as each part was finished)

Lines 1-2420 (notes):
- :244-249 `log_line` does fprintf(stdout) + fflush(stdout) + extensions::log_line on every call, from any thread. Every svc_log (:1204), presence change (:2131), achievement (:2152), power change (:1089/:1093) and focus change (:955) reaches it on the game thread or main loop. A flush of a redirected stdout (a file, a pipe that a reader stops draining, Android's stdout which goes nowhere unless redirected) blocks for as long as the write takes.
- **The tablet number is not a game step.** The "[pacing] ... frame work X ms average, Y ms longest" line is printed only by SDL hosts (:1161-1170), every 600 frames, by fprintf(stderr) on the game thread. Its "work" is `perf_hint_frame_done()` (runtime/src/perf_hint.cpp:186-202). That is the largest CPU-time delta, between two calls of svc_frame_wait, of any thread registered with perf_hint_add_current_thread. Two threads are registered: the game thread (host_main.cpp:6372, checked below) and the render thread (render_core.cpp:1518).
  - The figure is thread CPU time (clock_gettime on the pthread CPU clock, perf_hint.cpp:71-74). Time spent blocked on a file, a lock, a condition variable or a vsync wait is not in it. So 18.68 ms means one of the two threads *executed* about 18.7 ms of CPU inside one window.
  - That is longer than a 16.67 ms step. So that window was itself at least 18.7 ms long: the frame_wait that closes it returned late, or a window covered more than one step.
  - Candidates of the kind "game thread blocked on I/O or a lock" are therefore excluded by this measurement. Only CPU work counts: on the game thread or the render thread, including kernel CPU in syscalls, and including work that runs slowly because the core's clock was dropped (title idle, 1.11 ms average, so the governor has every reason to lower the clock).
  - The same call also reports work to APerformanceHint (perf_hint.cpp:119-141). With a 1.11 ms average reported against a 16.67 ms target, the hint session asks the system for *less* CPU. A burst then runs on a low clock or a little core. This is a candidate in itself (see the 18 ms list below).
- :1037-1060 display_refresh (Windows D3D12 host only) calls DwmGetCompositionTimingInfo every 30th frame: once every 0.5 s on the game thread. Cheap, but it is a periodic system call on the step.
- :1103-1179 svc_frame_wait:
  - SDL hosts follow the display's vsync (sdl::wait_display_frame) when it is given, else a PreciseWait.
  - Each frame it calls perf_hint_frame_done (a mutex plus a clock_gettime per thread; on Android a reportActualWorkDuration binder call, perf_hint.cpp:139) and perf_hint_set_target.
  - On Windows, perf_hint_frame_done reads running_on_battery every 2 s (perf_hint.cpp:216-225). That is a periodic system call on the game thread; its cost is in windows_power, read in item 8.
- :831-867 Worker: the file and save worker (gFs). A detached thread with a std::function deque and no stop. Jobs still queued when the process ends are lost unless finish_and_exit drains them (checked below).
- :881-897 save_folder_path refuses "..", ".", a root name and a root directory in every part: correct.
- :380-391 flush_progress_bounded. On Windows it runs on a thread waited for at most `wait_ms`. On every other platform it runs inline with no bound, so the hang it is written to avoid is still there on Linux, macOS and Android after a crash or die() (P-067).
- :1357-1368 finish_crash_report: Sleep(5000) before TerminateProcess. The process stays 5 s after the report is written. Intentional, and documented at :1363.
- :1982-2000 retry_start_for_gpu_memory sleeps 2 s and restarts, up to 5 times (Windows only).
- :2331 onward svc_settings_view: it rebuilds the whole SettingsContent from galaxy::render::native_settings_view() on every call (strings and vectors allocated) and compares it with the last one. If the game calls it every frame with the menu closed, that is a per-step allocation; checked in item 5.

Lines 2420-6578 (notes):
- :2647-2659 svc_submit_frame and :3069-3134 svc_frame_begun run on the game thread every step. They call:
  - submit_frame → render::render_frame (:1971, :3111);
  - extensions::frame;
  - mods::dev_frame (every 30 frames, only with --mod-dev, which Android refuses at :5655);
  - mods::run_screen_answers;
  - take_mii_installs.
  - The aspect latch (:3120-3130) calls `wait_for_render_idle()` on the game thread, but only when the output shape changes.
- :3408-3481 svc_save_request: the save is copied by value and posted to gFs; status is written with a release store. Deterministic mode runs it inline. Correct.
- :3585-3602 input_device_missing runs on every 10th svc_input_read (6 times a second at 60 Hz), on the game thread under gInputSampleLock. It calls connected_controllers() and keyboard_or_mouse_attached(), whose cost is checked in item 3.
- :3808-3918 input_thread (real Wii Remote, Windows and Android): it waits in real_wiimote_wait with a 3,600,000 ms timeout while no remote is used. With a remote, it reads the controls every 10 ms under gInputSampleLock, the lock the game's read takes.
- :4044-4145 svc_home_menu: LAUNCHER_OPEN calls update::offer(kFreshBudgetMs) on the game thread, which can wait up to that budget (item 7).
- :4605-4806 finish_and_exit, in this order:
  1. the benchmark at_exit;
  2. pictures;
  3. the pipelines list;
  4. the saves wait (5 s bound, :4628-4633, P-018);
  5. achievements and stats flushed inline;
  6. flush_ids(2000);
  7. extensions ending;
  8. the update finish;
  9. os_hdr finish;
  10. then the restart path for each platform:
      - Windows: CreateProcess plus the hand-over (:4372-4435, backstop 5 s);
      - Android: start_again, then a 5 s sleep;
      - Linux and macOS: execv with the CLOEXEC sweep;
  11. TerminateProcess or _Exit.

  The renderer is never shut down (intentional, :4801).
- :6370-6374 the game thread is "Nebula Game" and registers itself with perf_hint_add_current_thread (confirms the earlier note).
- :6419-6549 main loop (the main thread, not the game thread):
  - SDL pumps events at most every 8 ms (:6424). On Android's onTrimMemory it calls `gPak.store.release_pages()` and `mallopt(M_PURGE, 0)` (:6443-6449).
  - Windows waits in MsgWaitForMultipleObjects.
  - Once a second it calls log_line with the counters (:6520-6548). Every 30 s (:6527) it calls `gAudio->stats_line()`, whose locking is checked in item 2.
- platform/sdl/display_clock.cpp:119-160: the vsync wait on Android. It predicts the slot from Choreographer's grid and sleeps with clock_nanosleep (no spin). It falls back to the host clock when no vsync came for 250 ms or when the display's rate does not divide the step (90 Hz).
- runtime/include/galaxy/precise_wait.h:72-86: the fallback wait spins for the last 100 us to 2 ms (the margin). That spin is game-thread CPU, so it is counted in the "frame work" figure on the non-vsync path (item 8 has the finding).
- platform/sdl/thread_sampler.cpp: an Android sampler that already exists and is off by default. `adb shell setprop debug.nebula.profile <seconds>` samples "Nebula Render", "Nebula Host Mai" and "Nebula Game" in turn, 2,000 SIGPROF samples at 1 ms each, and prints the top 12 places to stderr.
- runtime/src/stats.cpp read completely (item 6 lists stats):
  - The writer is its own thread (1.5 s delay, 5 s back-off after a failure).
  - add() takes the mutex only. Its first call reads stats.ini on the calling thread (the game thread, once).
  - flush() writes inline.

#### The tablet's 18 ms "frame work": candidates and the measurement that separates them

What the number is: `perf_hint_frame_done()` (perf_hint.cpp:186-202), printed by host_main.cpp:1161-1170. It is the largest *CPU-time* delta of the game thread or the render thread between two returns of svc_frame_wait. It is not a game step's wall time.
- Blocking (file, lock, condition variable, vsync wait) adds nothing to it.
- A value above 16.67 ms means a thread executed that much in a window that was itself longer than one step.
- So the spike is CPU executed. It is either a lot of work, or ordinary work run on a slow core or clock.

| # | Candidate | Where | Period | Fits "about once a second or less"? |
|---|---|---|---|---|
| C1 | The render thread, not the game: its CPU in the window. With interpolation on a 120 Hz panel that is 2 pictures, plus any periodic render-thread work. Examples: the texture budget each frame, enforce_video_memory (render_core.cpp:2184-2210); the Vulkan memory-budget query at 10 Hz (vk_context.cpp:1897-1921); the 100 ms window probe (render_core.cpp:1722) | render_core.cpp:1518 registers it | per picture, 10 Hz | possible; the number cannot tell C1 from the game |
| C2 | The same work on a low clock or a little core. ADPF is told 1.11 ms of work against a 16.67 ms target, so the governor drops the clock, and a burst runs slowly | host_main.cpp:1141, 1146 → perf_hint.cpp:119-141, 168-180 | every frame | amplifies any of the others |
| C3 | The frame handoff on the game thread: render::render_frame from svc_frame_begun and svc_submit_frame | host_main.cpp:1971, 2658, 3111 | every step | only if render_frame does variable work there |
| C4 | input_device_missing → connected_controllers + keyboard_or_mouse_attached | host_main.cpp:3585-3602 | every 10th step (6 Hz) | excluded on Android after item 3: atomics plus an input_state copy (host_window_sdl.cpp:2232-2251, 2411-2417); still a candidate on Windows |
| C5 | log_line with fflush(stdout) per line, from the game thread (svc_log, context, presence, achievement lines) | host_main.cpp:244-249, 1204, 2838, 2855, 2131, 2152 | as often as the game logs | the write is CPU only through the syscall; a blocked write does not count |
| C6 | svc_settings_view rebuilds and compares the whole view (strings, vectors) on every call | host_main.cpp:2331-2436 | every call | not periodic by itself (item 5) |
| C7 | The heap: game allocations are the C runtime's malloc (host_main.cpp:6177-6179). The host never sets the allocator's release interval (grep: only M_PURGE). After an onTrimMemory, M_PURGE plus `store.release_pages()` (:6443-6449) make the game thread's next touches page faults, which count as its kernel CPU | host_main.cpp:6177, 6443-6449 | allocator interval; each trim | yes, if the allocator releases on an interval |
| C8 | The [pacing] fprintf itself, after the measurement, so it lands in the following window | host_main.cpp:1164-1170 | every 600 steps (10 s) | rarer than reported |
| C9 | The per-frame APerformanceHint binder call, and a new session after any target change | perf_hint.cpp:123-141 | every step; session on change | small, except at the boot→step change |
| C10 | The first stats::add on the game thread reads stats.ini | stats.cpp:45-58 | once | no |
| C11 | display_refresh's DwmGetCompositionTimingInfo (Windows only) and the battery look every 2 s (Windows only) | host_main.cpp:1043; perf_hint.cpp:216-225 | 0.5 s / 2 s | not on the tablet |
| C12 | The PreciseWait spin on the non-vsync path (90 Hz panel, or no vsync for 250 ms) | precise_wait.h:82-85, host_main.cpp:1124 | every step on that path | adds up to 2 ms; not 18 |
| C13 (added in item 4) | SaveDataHandler::waitRequest spins the game thread while an earlier save request (with fsync) is in flight (P-080) | SaveDataHandler.cpp:120-128, 135, 529 | only when two save requests overlap | not at an idle title (no save requested); a "save:" log line inside a slow window would show it |

Excluded by the metric: the audio thread (svc_audio_*, not registered); the main thread (event pump, 1 s report, 30 s stats line); every wait on a lock, a file, the vsync or a fence.

Measurements that separate them, in order of cost:
1. **Split the number.** Print each registered thread's largest CPU delta, the window's wall length and the step index where it happened. "Game" versus "Render" separates C1 from C3-C7. A wall length well above 16.7 ms with CPU about equal to it means a long step; CPU close to the window on the render thread means C1.
2. **Period.** Log the step index of every window over 8 ms:
   - a spacing of 10 steps is C4;
   - 60 steps or a 1 s wall period is a 1 s timer;
   - 600 steps is C8;
   - irregular spacing points to C7 or C2.
3. **Where.** Use the existing sampler: `adb shell setprop debug.nebula.profile 5`, then restart the app. It takes 2 s at 1 kHz per thread, so a 1 Hz burst of 18 ms is about 3.6 % of that thread's samples, and the place is named.
4. **Clock and core.** A Perfetto system trace (sched, cpu_freq, cpu_idle, linux.perf callstack sampling on the app) shows the thread, the core, its frequency at the spike and the stack. It separates C2 from real work. Run the same trace once with the hint session disabled (perf_hint report returning early) to measure C2's share.
5. **Log check.** Count log lines per second during the idle title (C5), and look for "the system asked for memory" lines (C7).

#### Round 4 findings, item 1

**P-067** wrong behaviour. On Linux, macOS and Android the crash and die() paths flush achievements and stats inline with no bound, so a crash on a thread that holds one of their locks hangs the process instead of ending it.
- Evidence: host_main.cpp:380-391. `flush_progress_bounded` runs on a separate thread waited with WaitForSingleObject(wait_ms) only under `_WIN32`; the `#else` branch calls `achievements::flush(); stats::flush();` directly. Callers: die() :456. finish_crash_report :1360 is Windows-only.
- Why and fix: the comment at :370-372 states the reason for the bound ("the thread that failed may hold the lock"), and stats::flush takes `s.io` then `s.mutex` (stats.cpp:61-67). Fix: on POSIX, run the flush on a std::thread and wait on a condition variable with wait_for(wait_ms); detach it on timeout.
- Confidence: verified in code.

**P-068** untested (a diagnostic that cannot answer its question). The SDL "[pacing] frame work … longest" line reports the largest CPU time of any registered thread between two frame waits, so it cannot say whether the game step or the render thread was slow, nor how long the window was.
- Evidence: host_main.cpp:1141, 1161-1170; perf_hint.cpp:186-202 (`work = std::max(work, now - thread.cpu_ns)` over g_threads); registrations at host_main.cpp:6372 (game) and render_core.cpp:1518 (render).
- Why and fix: tonight's 18.68 ms figure cannot be attributed from this line. Fix: keep the CPU delta per thread, plus the wall time between frame-wait returns, and print each thread's max with its step index. This is measurement 1 above.
- Confidence: verified in code.

**P-069** performance. Android's performance-hint session is told the busiest thread's CPU time as the "actual work duration". The API asks for the wall time of the work. Blocked time (GPU fences, the vsync, locks) is left out, so the session sees about 1 ms against a 16.67 ms target and lowers clocks further than the frame allows.
- Evidence: perf_hint.cpp:119-141 (`g_api.report(g_session, work_ns)` with work_ns from :186-199, thread CPU clocks); target from host_main.cpp:1146.
- Why and fix: Android's ADPF reportActualWorkDuration takes the work's elapsed duration. Under-reporting drives the clock down, and a burst of real work then takes several times longer (candidate C2). Fix: report the wall time from the step's start (the svc_frame_wait return) to its end (the frame handed to the renderer), measured on the game thread. Report the render thread's picture time as its own session or as a combined duration. Use the ADPF work-duration API (APerformanceHint_reportActualWorkDuration2 with AWorkDuration) where present.
- Confidence: the quantity reported is verified in code. The clock effect is suspected and needs a run: a Perfetto trace with cpu_freq on the tablet at the idle title, once as built and once with `report()` returning early.

**P-070** performance. Every host log line does fprintf(stdout) plus fflush(stdout) on the calling thread, and the game thread is a caller (svc_log, context and presence changes, achievements). The game step then waits on the log's write.
- Evidence: host_main.cpp:244-249, callers :1204, :2131, :2152, :2838, :2855; Windows redirects stdout to logs/latest.log (:5812-5827).
- Why and fix: a flushed write per line is a syscall on the step. On a slow or contended disk, a pipe a reader stopped draining, or Android's redirected output, it blocks the step. Fix: a log queue (a lock-free ring) drained by one writer thread that flushes on a timer and at exit or crash. Crash paths keep a direct write.
- Confidence: verified in code. The stall size needs a run: a Perfetto trace of the game thread's write() during a burst of log lines.

### R4.2 Audio

Read completely:
- runtime/src/native_audio.cpp (366);
- audio_mixer.cpp (844) and include/galaxy/audio_mixer.h (228);
- audio_device_wasapi.cpp (419);
- wiimote_speaker.cpp (167);
- platform/sdl/audio_device_sdl.cpp (81, re-read);
- runtime_settings.cpp 315-320 and 470-570 (the lock the audio thread takes).

audio_device_apple.mm was checked against the mixer by grep only (queued_frames and latency).

**How samples per step are decided (clock model).** The device pulls. NativeGameAudio::State::render (native_audio.cpp:120-163) runs on the device's thread:
- It copies what is pending. When nothing is left, it calls `tick()`, which is host_main.cpp audio_frame_tick → the game's `nebula_game_sound_frame`.
- That runs the game's sound logic and calls voices_mix (one 1/400 s subframe each, audio_mixer.cpp:218-271) and voices_frame (end_frame plus the master, native_audio.cpp:210-300).
- Samples per subframe are exact cumulative integers of the device rate (`(n+1)*rate/400 - n*rate/400`, :226-227).

So the game's sound clock is the device's crystal, and the 60 Hz step does not enter. A game running 0.1 % to 3 % off 60 Hz (P-003) cannot under-run or overflow the audio. Sound stays at the right pitch and tempo, and pictures and sound drift apart only in the sense that game time runs slow or fast against a correct sound clock. The Wii worked the same way: the sound followed the audio interface's DMA interrupt, not the video.

The remote speaker's producer is a sound-engine job (petari SpeakerMixer.cpp:125-148, samples due counted per sound frame). Its 6 kHz blocks therefore come on the same clock, and the ring in wiimote_speaker.cpp:70-165 is a correct SPSC. That this job runs on the audio thread is suspected from its form; its thread was not traced.

**Under-runs.**
- WASAPI (audio_device_wasapi.cpp:279-343): event driven on an MMCSS "Pro Audio" thread at critical priority. The fill starts at one engine period: IAudioClient3's least period, else the device period with a 40 ms buffer.
  - The fill rises a period (at most 4) when a wake comes later than 1.5 periods or a render takes over 3/4 of one. It falls a period after 10 quiet seconds (P-072).
  - Nothing counts an actual under-run (padding 0 at a wake).
  - With no endpoint, it renders by its own 10 ms clock and tries again every second (:346-356). Device loss and default changes come through IMMNotificationClient (:81-89) and reopen the endpoint. The mixer follows a rate change (native_audio.cpp:125-133).
- SDL (audio_device_sdl.cpp): SDL's pull callback renders exactly what is asked. Latency is SDL's default device buffer (no SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES is set). When the stream fails to open, a null device driven by a 5 ms clock takes over. SDL follows the default device and converts the rate, which is read once at construction.
- No platform renders ahead. The game's sound logic runs inside the device callback. A slow sound frame (many voices with FIR, effects lines, a movie's 128-tap stream resampler) is therefore an under-run directly, unless WASAPI's fill already covers it.

**Thread safety.**
- Everything in NativeGameAudio::State is touched on the device thread (render → tick → mix and frame).
- setup() runs once on the game thread before start (petari SoundEngine.cpp:108, the only caller).
- set_paused goes through the device's atomic.
- get_runtime_settings() takes a shared_mutex for a copy of a plain-data struct (runtime_settings.cpp:315-320). Writers hold it only for the assignment, and the file write happens outside it (:522-566). Correct.
- AudioMixer::stats() is read by the main thread every 30 s (host_main.cpp:6527 → native_audio.cpp:351-364) while the audio thread writes it. That is a data race: harmless on 64-bit targets, but undefined.
- NativeGameAudio::setup replaces `device` and `mixer` unconditionally. A second call would destroy a WasapiDevice whose detached thread still runs (audio_device_wasapi.cpp:106). There is one call today, so this is latent.

**Latency by platform** (from code, not measured):
- Windows: one to four engine periods, plus up to one sound frame waiting in `pending`.
- SDL (Linux, Android): SDL's default buffer, plus its stream queue.
- Apple: its own buffer (Round 1).

**The movie sound's position counts the device's queue only on WASAPI** (P-071).

**Allocations on the audio thread after warm-up:** none found.
- `pending`, `mods` and `frame_` keep their capacity. `end_frame` swaps with a thread_local buffer.
- InterpolationKernel is rebuilt (128 × 512 Kaiser evaluations) on a rate change only, on the device thread (audio_mixer.cpp:164). That happens once per device change.

#### Round 4 findings, item 2

**P-071** wrong behaviour. On SDL hosts (Linux, Android) and Apple, a movie's picture runs ahead of its sound by the audio the device and SDL's stream still hold.
- Evidence:
  - The picture's clock is the mixer's unplayed count, and it adds `device->queued_frames()` (native_audio.cpp:159-162).
  - Only WasapiDevice overrides queued_frames (audio_device_wasapi.cpp:112). The base returns 0 (audio_device.h:33). SdlAudioDevice (audio_device_sdl.cpp) does not override it, and grep finds no queued_frames in audio_device_apple.mm.
- Why and fix: lip-sync in the game's cutscene movies is off by the output's buffering, which on Android or PipeWire can be tens of milliseconds. Fix:
  - SDL: return SDL_GetAudioStreamQueued(stream_)/8 plus the device's buffer. SDL3 has no direct device-latency call, so use the device sample frames from SDL_GetAudioDeviceFormat.
  - Apple: add the output's presentation latency (AVAudioSession outputLatency + IOBufferDuration on iOS; kAudioDevicePropertyLatency + the buffer frame size on macOS).
- Confidence: verified in code. The size of the offset needs a run: a movie with a clap frame, recorded with a phone camera, on Android and Linux.

**P-072** wrong behaviour (audible). The Windows output starts at one engine period and lowers the fill again after 10 quiet seconds. A machine that needs two periods therefore glitches, raises, lowers 10 s later and glitches again. The raise comes only after the late wake, so the first gap is always heard. No under-run is counted anywhere.
- Evidence:
  - audio_device_wasapi.cpp:36-37 (kMostFillPeriods 4, kQuietSecondsToLower 10), :253 (fill = one period), :309-315 (raise on a wake past 1.5 periods, lower after 10 quiet seconds), :338-341 (raise on a render over 3/4 period).
  - raise_fill writes to std::cout on this MMCSS thread (:264) at the moment it is already late.
- Why and fix: the game's whole sound frame is computed inside this callback (native_audio.cpp:141-143), so the needed headroom is the slowest sound frame, not the average one. Fix:
  - start at two periods;
  - lower only after a long quiet window, and never below the highest level that was needed this session;
  - count real under-runs (padding 0 at a wake; GetCurrentPadding, or IAudioClock position jumps) and show them in the jaudio stats line;
  - move the log line off the audio thread (an atomic the main loop's 1 s report prints).
- Confidence: verified in code. The repeat cycle needs a run on a loaded machine: log fill changes with timestamps and count wakes with padding 0.

### R4.3 Input

Read completely:
- runtime/src/input/host_input.cpp (1-1241);
- live_controls_engine.cpp, pointer_control.cpp, co_star.cpp, controls_engine.cpp, rumble_route.cpp, key_positions_win32.cpp, raw_mouse_win32.cpp (+ include/galaxy/input/raw_mouse.h), rumble.cpp;
- wiimote_input.cpp, wiimote_motion.cpp, wiimote_motion_pointer.cpp, mod_actions.cpp.

Read in part:
- controller_hid.cpp: 1-560 and 1370-2741 (worker, setup thread, hot-plug, reads, rumble, presence thread). Not read: 560-1370 (enumeration, per-model init, output pump).
- host_window_sdl.cpp: 520-550 and 866-1540 (gamepad sampling, open/close, pump_events, relative mouse, input_state/take_input_state).
- host_window_apple.mm: 215-275 and 1028-1110 (mouse, mouse hold, pump_events).
- luma_input.cpp: the host half only (1110-1170, 1575-1752: look and pointer choice, game_read, text entry).

Not read: input_bindings.cpp, native_input.cpp, wiimote_hid.cpp, wiimote_link_win32.cpp, wiimote_protocol.cpp, wiimote_link_android.cpp, host_window_win32.cpp (render), the rest of the three window files, the *_tests.cpp files. pad_controller has no source of its own (only runtime/src/pad_controller_tests.cpp).

Notes:
- The "18 ms" candidate C4 (connected-controller queries) is excluded on Android.
  - keyboard_or_mouse_attached is atomics there (host_window_sdl.cpp:2411-2417).
  - connected_controllers copies input_state() under g_input_mutex (host_window_sdl.cpp:2232-2251, 1498-1504).
  - So it is microseconds. On Windows, C4 stays a candidate: GetRawInputDeviceList (host_input.cpp:144-172) and the once-a-second button_icons refresh.
- Hot-plug on Windows:
  - HID: CM_Register_Notification sets the rescan event (controller_hid.cpp:2495-2502). The setup thread lists and opens on its own thread (:1428-1492). A failed read drops the device and asks for a rescan (:1538-1560, 1902-1905). Sound design; no game-thread blocking.
  - XInput: a presence thread probes the slots every kXInputProbeMs (:2672-2687). The game thread polls only the present slots and hands a failed slot back (host_input.cpp:604-613).
- Hot-plug on SDL: SDL_EVENT_GAMEPAD_ADDED/REMOVED (host_window_sdl.cpp:1389-1397). A pad beyond kMaxPads is ignored without a log line (:954-987).
- Polling:
  - Windows XInput is read on the game thread at the step (host_input.cpp:1104-1106).
  - HID pads publish at report rate under an SRW lock. The game takes the newest copy and consumes gyro and touch deltas (controller_hid.cpp:2517-2536).
  - SDL and Apple read gamepads as levels on the main thread at each pump, every 8 ms at most (host_main.cpp:6424; host_window_sdl.cpp:1194-1204 sleeps 8 ms on Android). See P-075.
- Controllers are not read while the window is unfocused (host_input.cpp:1093-1096 with live_controls_engine.cpp:39). This is a deliberate choice; it is noted in case the owner wants background play with a controller.
- Rumble:
  - The game publishes each step. The worker stops a motor when publications stop for kStallUs (rumble.cpp:138-141), and on focus loss or pause.
  - stop_all_rumble never blocks (controller_hid.cpp:2584-2608).
  - Correct.
- Game-thread log writes: [input-shake] goes to std::cerr at each shake press (live_controls_engine.cpp:533-537). This is the same pattern as P-070.
- current_process_runtime_window runs EnumWindows over the whole desktop on every call while the cached window has no client area (minimised) (host_input.cpp:853-920). The cache is only set when the class is found. This is small, and only while minimised and not suspended.
- Co-star's "newest" controller is the last in list order (XInput users, then HID slots), not the last one attached. The comment admits it (host_input.cpp:1125-1131); on Windows a pad in a lower HID slot attached later is not player 2.

**P-073** wrong behaviour. On Windows the left stick has a per-axis (square) dead zone for both XInput and HID pads, while the SDL hosts use a radial one. A small diagonal push snaps to a straight line on Windows.
- Evidence:
  - host_input.cpp:622-623 calls normalize_xinput_axis(raw, deadzone) once per axis (native_input.h:94-96). The XInput default is 7849/32767, about 24% (input_bindings.cpp:468).
  - HID pads: switch_controller.cpp:234-235 apply_deadzone per axis (called from host_input.cpp:692-694).
  - SDL: native_game_host_sdl.cpp:519 uses apply_radial_deadzone.
- Why and fix:
  - Why: with 24% per axis, a push of 40% at 20 degrees off the x axis gives y = 0.137, inside the y dead zone, so Mario walks along x. The direction error near the centre is up to 45 degrees, and walking speed differs by direction. The same pad behaves differently on Linux/Android than on Windows.
  - Fix: apply_radial_deadzone (pointer_control.cpp:63-74) on the left stick in all three paths, with the per-axis value passed as 0. Keep the per-scheme defaults.
- Confidence: verified in code.

**P-074** wrong behaviour. A Wii Remote's Nunchuk stick takes its centre from the first report after connecting, not from the Nunchuk's calibration. A stick that is off centre at that moment (held, or drifting) stays offset until the next connection.
- Evidence:
  - wiimote_input.cpp:332-339 (stick_origin_x_/y_ from the first read).
  - Reader::connect parses only the accelerometer zero and 1 g from the Nunchuk calibration (:238-250); the stick bytes of the same block are not used.
- Why and fix: use the calibration block's stick centre (and min/max for scaling) when its checksum passes. Fall back to the first report only when it does not, and then re-centre after a period at rest.
- Confidence: verified in code. The byte offsets of the stick centre (WiiBrew "Nunchuk" calibration: x max/min/centre then y max/min/centre after the accelerometer values) need a check against the reference before the fix.

**P-075** wrong behaviour. Short controller presses can be lost. Buttons are carried as levels, the newest sample replaces the last, and nothing latches a press that came and went between two game reads.
- Evidence:
  - SDL hosts: sample_gamepads reads SDL_GetGamepadButton levels on the main thread (host_window_sdl.cpp:870-912), once per pump. The pump waits up to 8 ms (host_main.cpp:6424); on Android it sleeps 8 ms between pumps (host_window_sdl.cpp:1198-1204). The game reads the stored levels at its step (take_input_state, :1506-1517).
  - Apple: the same at host_window_apple.mm:1089-1092.
  - Windows HID: each report replaces state.decoded (controller_hid.cpp:2116), and the game copies the newest (:2517-2536).
- Why and fix:
  - Why: a press shorter than about 8 ms on SDL, or a press that ends before the next game step on any host, never reaches the game. Fast taps and turbo inputs (spin, Star Bit shooting) are at risk; the Wii read every remote report into a buffer.
  - Fix: accumulate "pressed since the last take" bits next to the levels. Sources: SDL_EVENT_GAMEPAD_BUTTON_DOWN events; the HID worker's per-report edges; the Apple valueChangedHandler. Clear them in take_input_state / read_hid_pads(consume). The game then sees down = level | pressed-since-take for one step.
- Confidence: verified in code. The rate of loss needs a run: a scripted 5 ms press (Steam Input macro or a test pad) counted at the game's input over 100 presses, per platform.

**P-076** platform gap (the host half of the mouse-held look). The look's mouse motion is unusable or differs between platforms:
- On Windows it is zero for any mouse that reports absolute positions: Remote Desktop, Parsec in absolute mode, virtual machines and pen tablets.
- On macOS it is the system's accelerated motion in points, where Windows uses raw counts.
- Evidence:
  - raw_mouse_win32.cpp:27 returns an empty motion for MOUSE_MOVE_ABSOLUTE. While held, the cursor is clipped to a 16 px box (host_input.cpp:1027-1050), so the absolute path cannot help.
  - host_window_apple.mm:225-228 adds NSEvent deltaX/deltaY, which follow the pointer acceleration curve.
  - All hosts scale by the same kLookMouseRadiansPerCount (pointer_control.cpp:80).
- Why and fix:
  - Windows: from absolute raw input, take the difference of successive positions (normalised 0..65535 over the virtual desktop) as the motion.
  - macOS: take unaccelerated deltas (GCMouse mouseInput.mouseMovedHandler, or IOHID).
  - Then one sensitivity means the same turn on every platform.
- Confidence: verified in code. The size of the macOS difference needs a run: the same physical mouse sweep on Windows and macOS, comparing the yaw reported by input.look.

**P-077** performance. The Windows HID worker registers a new thread-pool wait for every report: UnregisterWait, then RegisterWaitForSingleObject with WT_EXECUTEONLYONCE. Each report so costs:
- a pool-thread wake;
- a SetEvent;
- a worker wake (two context switches).

At a controller's report rate (hundreds a second over USB) this is constant background churn.
- Evidence: controller_hid.cpp:171-185 (disarm_io/arm_io), :1856-1859 (armed after each pending read), :1880-1885.
- Why and fix: one I/O completion port for all pad handles (CreateIoCompletionPort per handle, GetQueuedCompletionStatusEx in the worker loop) gives one wake per batch of reports and no per-report registration. Up to 60 pads also fit a direct WaitForMultipleObjects.
- Confidence: verified in code. The cost needs a run: ETW CSwitch counts for nebula-controller-hid and the pool's wait thread with a DualSense on USB, before and after.

**P-078** wrong behaviour (suspected). The star pointer and the mod look are sampled once per game step. Above 60 Hz the pointer the player aims with moves at 60 Hz, plus the interpolation delay, while the system cursor is hidden over the picture.
- Evidence:
  - The pointer is computed in LiveControlsEngine::sample (live_controls_engine.cpp:429-522), called once per game step through native_game_input (host_input.cpp:1202-1214).
  - The window hides the system cursor over the picture (comment at host_input.cpp:1024-1026; host_window_apple.mm:260-268).
  - No present-time pointer path exists: a grep for a late or present-time pointer finds none.
- Why and fix: draw the pointer sprite at present from the newest cursor projection (the render thread can call the same project_client_mouse_pointer). The game keeps the step's position for its hit tests. Do the same for the gyro pointer from the newest HID state.
- Confidence: suspected, needs a run. At 144 Hz, move the mouse in circles and record with a 240 fps camera, or log the pointer's drawn position against GetCursorPos per presented frame. First check whether the 2D pointer layout is interpolated at all.

### R4.4 Saves and import

Read completely:
- runtime/src/save_files.cpp;
- runtime/src/atomic_file.cpp.

Read in part:
- native/petari/src/Game/System/SaveDataHandler.cpp: 1-219 and 495-644.
- SaveDataHandleSequence.cpp: 370-459 and 585-649.
- save_transfer.cpp: 251-280 and 1385-1993 (moves, plan, commit, export, per-file merge, safe zip names). The Wii SD decryption, zip reader and plan builders (1-250, 280-1384, 1993-2260) were not read.
- native_settings.cpp: 2270-2480 (the import and export flow).

Not present or not read:
- "save_format*.cpp" exists only as save_format_tests.cpp.
- install_migration exists only as install_migration_tests.cpp; it tests asset_store and save_files.
- mii_import.cpp was not read.

Notes:
- Partial writes are handled well.
  - write_file_atomically writes a sibling temp file, then FlushFileBuffers/fsync, then rename. On POSIX it also fsyncs the folder (atomic_file.cpp:84-133, 221-258).
  - Nebula.sav carries a CRC-32 over everything after its header (SaveDataHandler.cpp:156-157, 525).
  - The game reads Nebula.sav, then .1, then .2, then the old GameData.bin copies, and takes the first that passes (SaveDataHandleSequence.cpp:386-439, cReadNames at SaveDataHandler.cpp:21-23).
  - A damaged newest file falls back to a copy without asking; only when every copy fails is the player asked.
- The backup rotation in saves::write (save_files.cpp:84-91) is a rename and a copy_file, not atomic. A crash in the copy leaves a short .1, which the CRC then rejects. That is acceptable.
- Temp files from a crash ("<name>.tmp.<pid>.<n>") are never removed from the save folder. luma_fs.cpp:96-97 only hides them from mods.
- On Windows, MoveFileExW fails with a sharing violation when a sync client or a scanner holds Nebula.sav open (atomic_file.cpp:124-129), and nothing retries. The save then reports failure to the player. A short retry on ERROR_SHARING_VIOLATION / ERROR_ACCESS_DENIED (a few tries over about 200 ms, on the file worker) would cover OneDrive-synced folders.
- Older saves: Nebula.sav has had version 4 since it was introduced (git log -S on cVersion: cc9cc48dc, fe844bf4e, 5b403f6e5 all 4). Older GameData.bin layouts go through readOld (SaveDataHandler.cpp:191-219).
- The 5 s exit wait (P-018) stands as written in Round 1.
- C-candidates for the 18 ms step: saves are not one at an idle title (no request is made). If the log shows "save:" lines inside a slow window, P-080 applies.

**P-079** wrong behaviour (data loss after a downgrade). A Nebula.sav written by a newer Nebula (version above 4) is reported to the player as damaged. The player is then offered the damaged-save path, and the next saves overwrite it.
- Evidence:
  - SaveDataHandler.cpp:154-159: any version other than cVersion is logged "is damaged".
  - SaveDataHandleSequence.cpp:415-418 and 393-397: every copy fails, then startDamaged.
  - save_files.cpp:84-91: the newer file survives as .1 for one save and as .2 for a second, then is gone.
  - The import path already tells the two apart (save_transfer.cpp:257-260, kNewerVersion). The game's reader does not.
- Why and fix:
  - readNative returns a separate "newer" result.
  - The sequence shows "This save was made by a newer Nebula" and runs in no-save mode.
  - Before any write over a file of a newer version, that file is moved to Nebula.sav.v<N> so it is never rotated out.
- Confidence: verified in code. The dialog's choices need a run with a hand-edited version-5 Nebula.sav (the header word at offset 4, CRC recomputed).

**P-080** performance. SaveDataHandler::waitRequest spins the game thread with a pause/yield loop, without sleeping, while an earlier save request is in flight. requestLoadSaveData and requestSaveSaveData both call it first.
- Evidence:
  - SaveDataHandler.cpp:120-128 (the loop), :135, :529.
  - The request being waited on includes FlushFileBuffers or fsync of the file and its folder, plus the .1/.2 rotation and copy (atomic_file.cpp:114, 241, 250-253; save_files.cpp:84-91).
- Why and fix:
  - Why: on phone flash an fsync can take milliseconds to tens of milliseconds. For that whole time the game thread burns a core and its step is late. The time counts as CPU time in the [pacing] "frame work" figure.
  - Fix: never wait on the game thread. Keep one pending request in the handler and start it when the current one completes (isDone already runs each step), or move the wait into the sequence's nerve (return and look again next step).
- Confidence: verified in code. How often a second request arrives while one is in flight needs a run: log the spin's length when it exceeds 0.1 ms, with a mod that saves (FORGET) and a star collected.

**P-081** wrong behaviour (rare data loss). A whole-save import moves the existing files aside and then moves the new ones in, one rename at a time, with no journal. A crash or power loss between the two leaves the save folder without a save, and the game starts as a new player. Nothing at start finds the leftover "<root>-backup-<stamp>" and "<root>.import" folders to finish or undo the import.
- Evidence:
  - save_transfer.cpp:1599-1621 (the moves);
  - :1622-1631 (the undo ignores rename errors);
  - a grep finds no reader of ".import" or "-backup-" at start.
- Why and fix: write a small journal file (the list of moves) before the first rename and delete it after the last. At start, a journal that is present is rolled back from the backup folder before the game reads its save. Report rename errors in the undo.
- Confidence: verified in code. The window is small; reproduce it by killing the process at move_file (gCommitFault already exists for tests: save_transfer.cpp:1421-1423).

**P-082** wrong behaviour (suspected). The in-game import's write is not ordered against the game's own save worker. The merged file is built from the save as the game read it. It is then placed by a detached thread with renames (native_settings.cpp:2298-2310, 2363, 2464-2466). Meanwhile the game's file worker can still write Nebula.sav and rotate .1/.2 (svc_save_request, host_main.cpp:3408-3481; save_files.cpp:84-91).
- Possible results:
  - The import's rollback fails with "could not be written" because a file it moved aside was recreated.
  - A game save between the read and the commit is replaced.
  - A save the game writes after the commit and before the restart replaces the import.
- Why and fix: put the commit on the same file worker queue as the game's saves, or stop the save service from accepting requests from the import's read until the restart completes.
- Confidence: suspected, needs a run. Add a debug delay in the file worker's write, start an import while a save is in flight, and compare the files after the restart. Also check whether the game can save while the settings overlay is open, and whether restart_game writes the in-memory save.

### Round 4 stop

Round 4 stopped at the boundary after item 4, for lack of room.

Items 1-2 were completed. Items 3-4 were read as listed in R4.3 and R4.4: some files only in part, and the unread ranges are named there.

Items 5, 6, 7 and 8 were not started:
- settings and UI;
- benchmark and diagnostics;
- the updater and installer;
- power and misc.

The files of those four items stay in the coverage table's area 5 "Not read" column.

## Round 5

### R5.1 Round 4 leftovers

**P-082 trace finished: withdrawn as a race with the game's saves in normal play.** A narrow residual is noted below.
- The game takes the import's read only when the save handler is idle and the start's read is done (NebulaSaveImport.cpp:64). No game save is in flight when "current" is made, and "current" is made from memory (makeSave, :93).
- While the settings menu is shown, the scene is frozen (NebulaSettingsMenu.cpp:5383-5385). scene_live is set only by the HDR calibration scene (host_main.cpp:2374).
- With the scene frozen, GameSystem::updateSceneController does not run mSequenceDirector->update() (GameSystem.cpp:311-329). SaveDataHandleSequence therefore starts no save while the import's detached thread commits (native_settings.cpp:2363, 2464-2466).
- restart_game follows, and finish_and_exit only waits for saves already in flight; it starts none.
- Residual, not verified: a player who closes the menu during "IMPORTING" resumes the scene while the commit runs (a few renames). The import step does not hold the menu open; settings_general_page.cpp:236 only shows "IMPORTING". Holding Back during Importing would close the residual. It is noted here, not counted as a finding.
- P-082 is withdrawn.

Read completely in this round:
- controller_hid.cpp 560-1370, which completes the file;
- runtime/src/wiimote_link_win32.cpp;
- runtime/src/wiimote_hid.cpp;
- platform/sdl/wiimote_link_android.cpp.

Notes:
- Generic HID controllers are found by their top-level collection without opening them (controller_hid.cpp:637-652). Nintendo, Sony and Valve devices are opened by vendor.
- Controllers that Steam Input already reads (SDL_GAMECONTROLLER_IGNORE_DEVICES) are left alone (:693-703, 721). This is good.
- The Wii Remote worker (wiimote_hid.cpp):
  - It scans when the mode allows a remote, with a back-off from 2 s that grows to 60 s (:69-99).
  - It treats only a read error as loss (:153-156, 376). A remote that goes silent with its handle still open is served until a read fails. Windows removes a powered-off paired remote's HID device, so this holds in practice.
  - It reads the Nunchuk's calibration block (:309-311). Reader::connect then ignores the stick part of it (P-074 stands).
- Android's Wii Remote link has a 64-report queue that drops the oldest; one condition variable serves arrival, reports and wake (wiimote_link_android.cpp:41-56, 200-215). It is correct.

**P-083** performance (input stalls on Windows). The HID worker's own comment says it never stalls on a busy radio (controller_hid.cpp:1193-1196). Several paths on that worker thread still do synchronous device I/O with timeouts of 30-200 ms. While one runs, no controller's reports are read or published.
- Evidence:
  - handle_report calls read_playstation_features for a Bluetooth DualShock 4 or DualSense still sending short reports, up to 5 times, once a second (:2060-2067). Each call does up to three get_feature_report waits of 200 ms (:1272-1305, 1028-1044).
  - send_light_bar does one or two write_output calls (:1321-1335). Each is write_report with a 30 ms wait (:1187-1191, 1008-1023).
  - stop_device_rumble and close_device do write_rumble_off with a 30 ms wait and finish_post(wait=true) (:1788-1802, 1804-1825).
- Why and fix: a second controller's inputs freeze for up to 600 ms while a Bluetooth DualSense connects next to it, and briefly at every light-bar write or disconnect. Fix:
  - Move the feature reads to the setup thread before the device is handed over; that thread already does init_playstation, so the repeat can be scheduled there.
  - Send the light bar through queue_output, which is asynchronous.
  - Do the off report at close through the same queue, with one bounded wait at process exit only.
- Confidence: verified in code. The stall's size needs a run: two pads, connect a DualSense over Bluetooth, and log the gap between the other pad's report times on the worker.

native_input.cpp was read completely (109 lines).

**P-084** wrong behaviour (it widens P-073 to every platform). On every host, every stick source goes through a second per-axis dead zone and per-axis clamp before the game sees it. That applies to pads, keys and SDL's radial result alike. A small diagonal push therefore snaps to a straight line on Linux, Android and macOS as well.
- Evidence:
  - live_controls_engine.cpp:204 passes the merged stick through nunchuk_stick.
  - nunchuk_stick (native_input.cpp:104-107) scales to ±101 counts. nunchuk_stick_counts (:84-102) applies 15 counts of dead zone per axis and full deflection at 71 counts per axis. It normalises only when the length is over 1.
  - The result: about 15% of square dead zone after the device's own dead zone, and full speed on each axis at about 70%. A 45-degree push reaches full speed at about 70% deflection on each axis, a straight push only at 70%.
- Why and fix:
  - Why: the numbers look like the Wii KPAD stick clamp defaults; this is not verified against the SDK. That is right for a real Nunchuk's counts (wiimote_input.cpp:340 uses nunchuk_stick_counts). It is wrong for pads and keys, which already had a dead zone.
  - Fix: for non-Nunchuk sources, map the already dead-zoned vector to the game's range radially (direction kept, length scaled so 1.0 is full). Keep the per-axis clamp only on real Nunchuk counts.
- Confidence: verified in code. The game-side effect (walking angle near the centre) needs a run: a scripted stick at 30% magnitude and 20 degrees, logging Mario's walk direction.

wiimote_protocol.cpp was read completely (209 lines): report builders, IR setup, status/read/ack parsers, battery level, the RealWiimoteHandoff ring. No issue found.

Saves, now complete:
- SaveDataHandler.cpp (643 lines) and SaveDataHandleSequence.cpp (648 lines) are both read completely.
  - Nothing new beyond P-079, P-080 and P-081.
  - forgetLumaMod (:312-315) and makeExport (:414-417, 448-451) test `new` for null. The global `new` throws instead, so the "no memory now" branches can never run. That is maintainability only.
  - exeQuietSave (:153-165): a game save that starts while a quiet save is in flight replaces the nerve. Its request then waits in waitRequest, which is P-080's busy spin.
- save_transfer.cpp is read completely (2,260 lines).
  - The SD-save reader checks the header MD5, the title, the file count, each size against kMaxFileBytes, and the padding (:341-434).
  - The JSON reader is bounded at depth 32 and 200,000 nodes, integers only (:743-941).
  - The archive manifest is checked against each file's size and SHA-256 (:965-1034).
  - The zip reader (:2021-2173):
    - checks the end record, ZIP64 and multi-part archives;
    - checks names with is_safe_entry_name;
    - refuses duplicate names;
    - bounds the size per entry and in total;
    - catches overrun ("one byte of room past the size") and checks each entry's CRC.
  - An unpacked archive's member names are checked with is_safe_entry_name before any file is opened (:1278).
  - No escape or overrun found.
  - One cost note, not a finding: a folder that holds no save layout makes plan_folder read every regular file in it, up to kMaxEntries files of up to kMaxSourceBytes each, until one parses as a save (:1320-1374). Picking a large Downloads folder reads it all on the import thread. Fix if wanted: check name and size before reading, and read only the first 0xF0C0+0x80 bytes to classify.

input_bindings.cpp was read completely (1,352 lines).
- Default tables per scheme and the migrations through controls version 7 (:323-561).
- Parsing and sanitising (:599-831): every float is clamped, and motion and touch are cleared on schemes that have neither.
- The evaluator (:944-1011): a stick direction has press/release hysteresis, and a stick axis shared by pointer and camera switches on sensor_pointing.
- The shared-mutex config with a generation counter (:1107-1132).
- Glyph and key-icon specs.
- Notes:
  - The Xbox scheme's default dead zones are the XInput axial constants (:467-469), which feed P-073.
  - kTargetLabels and kSchemeLabels (:39-51) are English only. native_settings.cpp uses them for the binding list (:5108) and the "RESET <scheme> TO DEFAULTS" row (:5203); see R5.2 for whether those rows are translated.

mii_import.cpp was read completely (547 lines).
- The library text file is bounded (64 MB per file) and written atomically.
- Merging keeps console create IDs, gives placeholders and collisions new IDs, and skips duplicates ignoring ID and favourite (:353-400).
- Copies are kept in the Miis folder with a numbered name on a collision (:223-250).

**P-085** performance. A Mii import holds the library mutex while it reads every chosen file (up to 64 MB each), copies each into the Miis folder, and writes the library. Meanwhile the game thread's mii_record and mii_count calls wait on that mutex.
- Evidence:
  - import_mii_files takes lib.mutex for its whole body (mii_import.cpp:425-471): read_file per file (:441), keep_in_library writes (:447), write_file_atomically (:458).
  - mii_library_mii and mii_library_count take the same mutex (:522-537).
  - The game calls them through svc_mii_record and svc_mii_count (host_main.cpp:2789-2802), from Mii::character and Mii::find (MiiCharacters.cpp:109-124, 137-139).
  - The picker callback runs the import while "the game keeps running" (native_settings.cpp:2169-2189).
- Why and fix: a player who imports a folder of Mii files during file select freezes the game thread for the length of the disk work. Fix:
  - Read and merge into a local MiiCollection without the lock (load the current one under the lock first).
  - Take the lock only to swap the collection and append the installs.
  - Write the library file outside the lock, or from a copy.
- Confidence: verified in code (the lock scope and the callers). The stall's size needs a run: import 50 Mii files from a slow USB drive while on the file-select screen, and log the game step time.

Item 1 of Round 5 is closed. Every Round 4 leftover file is now read completely.

### R5.2 Settings and UI

runtime/src/ui/native_settings.cpp was read at 1-2758 and 4816-6486. Lines 2759-4815 were skipped: the MODS page, mod consent, mod import, mod text editing, and the mod About and Remove questions. Modding is out of scope for this audit. The page files settings_display_page.cpp, settings_graphics_page.cpp, settings_general_page.cpp and settings_system_page.cpp were read completely.

How the menu works, as read:
- One recursive mutex, s_native_settings_mutex, guards every draft:
  - the render draft, the runtime draft and the controls draft;
  - the touch draft and the display confirmation.
- Who takes it:
  - the window thread, on keys (handle_native_settings_key :1845-2035);
  - the game thread, when it builds the view each frame (native_settings_view :6329 → build_native_settings_view :5851-6095);
  - the render thread, in poll_dlss_download (:5444-5482);
  - poll_render_config_file, twice a second (:5752-5776).
- Drafts are applied together (apply_native_settings_draft :1139-1215).
  - A display change has to be kept within 15 s or it is reverted (:1076-1108).
  - Closing with changes asks "apply / discard / stay" (:1836-1843, 1910-1934).
- Translation is not done in the host. The host sends English capitals. The game translates each one through NebulaText id "S." + text, with spaces as "_" and numbers as "#" (NebulaSettingsMenu.cpp:669-707). It splits on " (", ". ", ": " and ", ", and shows the English as given when no entry exists (:735-780).
- Fitting is done in the game. Text is measured and cut with an ellipsis:
  - the status line by binary search (NebulaSettingsMenu.cpp:4196-4208);
  - the benchmark run panel by fitLine (:5442-5454).
  - Text therefore cannot overflow a box; a long translation loses its end instead.
  - The host's own byte cuts (native_settings.cpp:5104, 5114, 5128) only reach the log, because build_native_settings_view replaces those values (:5916-5927).
- Ways back, checked for strands:
  - Keyboard: F1 toggles the menu; Escape closes it, or asks first when changes are pending.
  - Controller: View+Menu toggles it; B or Menu closes it (native_settings_pad.h:101-135).
  - While the "controller missing" message shows, Enter opens the menu (:1951-1955).
  - Every question screen takes Escape/B: Joy-Con pairing, touch arranging, the leave question, the vendor download, restart questions.
  - A binding capture ends on its own after 6 s of menu time (:403-409, 984-994).
  - No dead end was found for keyboard or controller.
  - Touch-only Android was not traced: the game's pointer handling of the menu is in NebulaSettingsMenu.cpp, which was not re-read this round.

**P-086** wrong behaviour. Some host texts shown in the settings menu have no NebulaText entry, so they appear in English capitals in every language.
- Evidence:
  - The Mii import results from mii_import.cpp, shown as the IMPORT MIIS help through s_mii_status (native_settings.cpp:2184, 6277, 4838; settings_general_page.cpp:203-207):
    - result_text, :252-258: "ADDED n", "n ALREADY HERE", "n NOT VALID", "n FROM NEWER CONSOLES";
    - :428 "NO MII LIBRARY";
    - :433 "THE MII LIBRARY FILE IS DAMAGED";
    - :459 "COULD NOT WRITE THE MII LIBRARY";
    - :468-469 "n FILE(S) NOT MII DATA" and "n FILE(S) NOT COPIED TO THE MIIS FOLDER".
  - The benchmark's start failure, benchmark_menu.cpp:357 "COULD NOT START".
  - A read-only script built each text's id with the menu's own rule (spaces to "_", numbers to "#", split on the same separators) and looked it up in localization/NebulaText.tsv. None of these has an id there.
  - The table still has S.COULD_NOT_WRITE_THE_MII_DATABASE (NebulaText.tsv:458), but the code now says "LIBRARY". The text was renamed and the table was not.
- Why and fix: this breaks the rule that every new text exists in all 14 languages. Fix:
  - Add NebulaText rows for each phrase, keeping the counts as "#" so "S.ADDED_#" and "S.#_ALREADY_HERE" match.
  - Change "FILE(S)" to whole phrases for one and many. The menu translates phrases and has no plural logic.
  - Add a gate that runs the same id rule over every literal the UI files hand to the menu.
- Confidence: verified in code and data (the script output and the TSV lines). The other near misses the script printed are fragments joined at run time ("MASTER: " + a percent), which do resolve.

**P-087** platform gap (the P-032 pattern; a complete list for the pages read so far). These options are offered where the active platform or renderer cannot do them:
- CONTROLS > INPUT "WII REMOTE (SENSOR BAR)" and "WII REMOTE (MOTION)".
  - Offered on every host (native_settings.cpp:1330-1336).
  - A Wii Remote link exists only on Windows (wiimote_link_win32.cpp) and on Android over a DolphinBar (platform/sdl/wiimote_link_android.cpp, platform/sdl/CMakeLists.txt:324-327).
  - The macOS, Linux and iOS builds have no link, and the Apple host's real_wiimote_taken is empty (host_window_apple.mm:1677).
  - Choosing either mode there gives the "no Wii Remote" message (host_main.cpp:3585-3601) until the mode is changed back.
- AUDIO > SOUND OUTPUT "SURROUND".
  - AudioDevice::set_surround is an empty default (audio_device.h:31). Only WASAPI implements it (audio_device_wasapi.cpp:128-132).
  - On SDL (Linux, Android) and Apple the device stays stereo. The game is still told "surround" (host_main.cpp:3495).
  - What the game's surround mode then sends into a stereo device was not traced.
- GRAPHICS > MSAA "8X" (and "4X" on devices that lack it).
  - The row always offers 2X, 4X and 8X and shows the chosen count (settings_graphics_page.cpp:132-142).
  - Each renderer clamps to what the device supports: Metal :123-128, Vulkan renderer_vk.cpp:562-577, D3D11 d3d11_context.cpp:190.
  - Most phone and iPad GPUs top out at 4X, so the row reads "8X" while 4X runs. RenderFeatureCaps.msaa exists (render_config.h:310) but the page does not use it.
- DISPLAY > HDR on Direct3D 11 and Metal: P-032, unchanged.
- Checked and found offered only where they work:
  - UPSCALING's list (available_upscalers :155-163);
  - DLSS, XeSS and frame generation (caps; the vendor download is pinned by SHA-256, vendor_download.cpp:416, 441);
  - DYNAMIC RESOLUTION and CURVED SURFACES (offered flags);
  - SSAO/GTAO, ENHANCED lighting, SMAA, sharpening, bloom and GPU TIMING (every renderer implements them, via the shared core or its own code: the d3d11, vk and mtl directories and renderer_d3d12.cpp:6289);
  - ON LOSING FOCUS (desktop only, :4858); DISCORD STATUS (discord::supported(), :4859);
  - the RENDERER and GRAPHICS CARD rows (renderer check);
  - REFRESH RATE (read-only).
- Why and fix: a player picks an option and nothing changes, or the row states something that is not running. Fix:
  - Build the input-mode list from a host capability (wiimote link compiled in).
  - Show SURROUND only where the device opens more than two channels.
  - Build the MSAA steps from the renderer's maximum.
- Confidence: verified in code for each entry. "What surround does into stereo" needs a run: SOUND OUTPUT SURROUND on Linux with a 5.1 device, recording the output channels.

### P-088 performance: the open settings menu rebuilds its page twice a frame on the game thread, and the CONTROLS page rebuilds its row list several times per row
- Evidence: build_native_settings_view (native_settings.cpp:5864) holds s_native_settings_mutex on the game thread each frame. For a page tab it builds the page once for the count (native_settings_row_count :1053-1057 calls settings_page) and once more for the rows (:5877-5878). For CONTROLS (not a page tab) every row goes through native_settings_row_text (:5900), which calls controls_row(row) (:5073), and the loop calls controls_row(row) again (:5911); controls_row copies the whole controls_rows() vector (:876-879), and controls_rows() calls joycon_status() and mod_actions::list() each time (:825, :860, :582-585). keep_controls_selection builds it once more (:885). With N rows that is about 2N+2 full rebuilds a frame. The SYSTEM page's helpers called per build (renderer_check :397-414 of renderer_check_win32.cpp, gpu_choices render_config.cpp:817-820, next_start_* :800-840) only copy cached state, so they are cheap.
- Why and fix: the work is memory only (no file or device I/O was found on these paths), so this is cost on the game step while the menu is open, not a stall. Build the page once per frame, pass its size and rows together, and compute controls_rows() once per build.
- Confidence: verified in code for the call counts; the cost is suspected, needs a run: open CONTROLS with a controller and several mods with actions, log the game step time against the menu closed.

### R5.2 remainder (settings model, save model, advanced page, benchmark page, benchmark menu, HDR calibration, calibration scene, launcher and HOME Menu models)
- Read completely: settings_model.cpp (205), settings_save_model.cpp (66) and .h (38), settings_advanced_page.cpp (62), settings_benchmark_page.cpp (306), benchmark_menu.cpp (418), hdr_calibration.cpp (228), calibration_scene.cpp (258), launcher_model.h (178), home_menu_model.h (109). There are no launcher_model.cpp or home_menu_model.cpp; the models are header-only.
- Locks checked: hdr_calibration's s_state has no lock of its own; every caller (key :1936, open :2096 and :6026, fill :6048, input :6403, close :1577 and :6377) runs under s_native_settings_mutex (:1871, :5864, :6362), so it is safe as written but only by that convention. benchmark_menu takes its own s_mutex and calls send/show/start outside it (benchmark_menu.cpp:349-360). calibration_scene's game-thread state is under g_mutex; its phase is atomic.
- Ways back checked: the benchmark questions close on Escape, Back or F1 (:327, :344); the results window on Escape or F1 (:314). The send screen waits on every key while the send runs (:290-299) except F1, which closes the menu; send_benchmark_report always answers through report_sent once diag::send_report_file returns (host_main.cpp:1889-1906), so the screen cannot stay stuck unless that call never returns (checked under R5.3). The HDR calibration ends itself when the display leaves HDR (hdr_calibration.cpp:213-216) and when the game hides the menu (native_settings.cpp:6377). The calibration scene only starts from the title or save select (gate host_main.cpp:6330-6333), so it never resets a game in play; three unanswered travels (calibration_scene.cpp:24-27, 203-209) leave it on what is shown.
- Wording: the benchmark verdicts are worded against 60 FPS ("SMOOTH AT 60 FPS", "DROPS BELOW 60 FPS", settings_benchmark_page.cpp:288-290) whatever the display's refresh rate and the frame cap; the meaning of the verdict is checked in R5.3.
- No new finding beyond P-088 in these files.

## R5.3 Benchmark and diagnostics

Read completely: benchmark.cpp (2,005), benchmark_results.cpp (459), benchmark_stats.cpp (184) and .h (83), benchmark_recommend.cpp (178), benchmark_cover.h (20), frame_telemetry.cpp (126), diag_report.cpp (919). Read in part for evidence: render_config.cpp:638-642, render_config.h:202-224 and 433-437, resolution_fit.h:60-77, renderer_d3d12.cpp:3715-3765, the GpuPass lists of the four renderers' headers, host_main.cpp:1886-1906.

How it works (verified in code):
- What is counted. A run holds the player's settings with frame generation and ray tracing off, then apply_benchmark_presenting sets VSync off, frame generation off and an unlimited frame rate (benchmark.cpp:1482-1492, render_config.cpp:638-642). An unlimited rate means unbounded Nebula interpolation between game steps (render_config.h:212-224, 433-437). Every shown picture is sampled on the render thread (benchmark.cpp:554-595), and frame i is the time between pictures i-1 and i (:810-812). So "FPS", "10% low" and "1% low" (benchmark_stats.cpp:21-27, 44-51: frames over their time; the FPS over the slowest 10% / 1% of pictures, at least one) count the game's frames and every interpolated picture together, always, whatever the player's own frame cap, VSync or interpolation setting.
- Warm-up. Before each window: at least 60 game frames, then until 60 frames in a row made no texture and built no pipeline with none left to build, at most 1,200 (:99-104, :1837-1853), then 30 frames to drain (:1854-1869). The window is a fixed time per stage (13 s, 12 s, 3 s for the probe, :88-97). A scene restart inside a window fails the run (:1871-1878).
- The game step rate. The window records the game frames it measured (:1880) and uses it only to decide whether interpolated pictures were shown (:878) and for the audio cost per game frame (:822-825). It is not written to the result and no verdict uses it.
- The verdict (:1433-1441). "fits" (shown as SMOOTH AT 60 FPS) when the 1% low of all pictures is at least 58; otherwise "cpu_bound" when the limiter is a thread; otherwise "below_target".
- What is uploaded. From the menu: send_report_file(result, false) (host_main.cpp:1891), so the benchmark JSON alone, without the log. It must pass a closed schema (diag_report.cpp:132-242): device names, driver, CPU, RAM, OS, output size and refresh, settings, a mods_loaded flag, and per-stage numbers. It is sealed to the build's public key (:744-750) and sent as one HTTPS POST: TLS 1.2+, no redirects, no cookies (:399-446). The timeouts are bounded on every platform: WinHTTP 2/3/2.5/2.5 s (:423), Android 5 s connect and 5 s read (:525-526), Linux and macOS 10 s (:566). A .sent marker blocks a second send (:782, 786). The command line can add 200 log lines with user and computer names, paths, e-mail addresses and IPv4 addresses replaced (:653-705, 770). Because every send returns, the R5.2 "send screen stuck" case cannot happen.
- frame_telemetry: Windows only. It publishes counters once a second to a per-process shared section under Local\ (frame_telemetry.cpp:44-50, 78-86). Nothing is read back.

### P-089 wrong behaviour: the benchmark's FPS, lows and "SMOOTH AT 60 FPS" verdict count Nebula's interpolated pictures, so a device that cannot run the game at 60 steps a second can still be told it is smooth
- Evidence: The run always presents unlimited with unbounded interpolation (benchmark.cpp:1490; render_config.cpp:638-642; render_config.h:433-437). The frame times are the time between shown pictures (benchmark.cpp:810-812). The verdict is "fits" when the 1% low of those pictures is at least kFitsLowFps = 58 (:120, :1438-1441), checked before the thread-bound test. The game frames a window ran are counted (:1880) but neither written nor judged (write_scene :1032-1108 has no field for them; the verdict :1438 does not read them). ceiling_of drops the "game frames" ceiling once interpolated pictures are seen (:793-798). The summary page shows these numbers as AVERAGE / 10% LOW / 1% LOW and the verdict as SMOOTH AT 60 FPS (settings_benchmark_page.cpp:124-129, 288).
- Why and fix: With unbounded interpolation, a frame that is late on the game thread is filled with interpolated pictures. Picture intervals stay short, so the 1% low stays high while the game itself runs below 60 steps a second; the 58 FPS line measures picture delivery, not the game. The run also measures a presentation the player may not use (VSync off, no cap), and the screen labels it YOUR SETTINGS. Fix: (1) record and report the game step rate per window and its 1% low from game-step times; base the verdict on it (game steps at 60, and picture pacing as a separate line); (2) report picture FPS as "pictures a second, with interpolation", or measure the player's own presentation as a second part; (3) treat a window whose game steps fell under 60 a second as not "fits" whatever the picture rate.
- Confidence: verified in code for what is counted and what the verdict reads. The outcome on a device whose game thread cannot hold 60 is suspected, needs a run: one benchmark on a slow CPU (or with the game thread slowed), comparing game frames ÷ window seconds (log it at :1880) against the written verdict.

### P-090 wrong behaviour: the GPU times behind the recommendation average the interpolated pictures' cheap GPU frames in with the scene's, so the probe prices and the worst stage's cost are understated and the recommendation can pick settings the device cannot hold
- Evidence: on_gpu_frame keeps every GPU frame read back while measuring (benchmark.cpp:597-609); a.gpu is the average over all of them (:827-830); measured_inputs takes worst_gpu_ms and the probe averages from it (:1190, 1199, 1209-1214). In D3D12 a generated picture uses an ordinary frame slot, marked frame_generated_ (renderer_d3d12.cpp:3728-3732), and the pass readback that calls note_gpu_frame (:3741-3765) has no generated-frame check; the D3D11 frame has the same `generated` flag beside its pass marks (renderer_d3d11.h:358-362), and its readback at renderer_d3d11.cpp:836-848 has no check either. The probes run with the same unbounded interpolation (probe_config copies yours, :646-661). The budget compares the result against one GPU frame per 60 Hz step: 0.8 × 16.67 ms (resolution_fit.h:69-74).
- Why and fix: With N interpolated pictures per game step, the average GPU frame is (scene + N × generated) / (N + 1). N grows as the GPU gets faster and shrinks as a probe adds cost, so the 480 and 720 fit and every feature's price (AO, MSAA, SMAA, bloom, enhanced lighting) are diluted by different amounts. The recommendation (benchmark_recommend.cpp:64-129) can then predict a fit for heights and passes that do not fit in a real game step. Fix: count only frames with a scene drawn (the renderers already know: frame_scene_drawn_, renderer_d3d12.cpp:3723) for gpu_ms, the probes and the passes; or run the probes with the present rate at 60 so each GPU frame is one scene.
- Confidence: verified in code that generated frames go through the same readback and that the averages do not separate them; suspected that every renderer marks passes on generated frames, needs a run: one benchmark on D3D12 and one on Vulkan, logging the GPU frame count per window against game_frames (a ratio above 1.1 confirms the mix), then the probe prices with the present rate held at 60 against the written ones.

### P-091 wrong behaviour: the settings menu offers a benchmark's recommendation on a renderer other than the one it was measured on
- Evidence: benchmark_menu.cpp:212-224 refuses the offer only for another GPU (last.gpu != adapter); the result's backend (written at benchmark.cpp:1267, read at benchmark_results.cpp:204) is not compared. parse_recommendation applies internal height, MSAA, AO, enhanced lighting, AA, SMAA quality and bloom as they stand (benchmark_recommend.cpp:131-176).
- Why and fix: A recommendation measured on D3D12 is offered under D3D11 or Vulkan, which have different costs and different MSAA limits (P-087). Fix: show OtherGpu (or a "measured on another renderer" state) when r.backend differs from the running renderer.
- Confidence: verified in code.

Checked and found correct in R5.3: benchmark_stats' percentiles and lows match the stated definitions (benchmark_stats.cpp:11-27); decode_frame_lists bounds every length (:149-182); read_result caps a result at 1 MiB (benchmark_results.cpp:245-253); keep_newest removes the .frames and .sent files with each result (:66-74); the thermal check needs at least 1 ms of GPU time to judge (benchmark.cpp:122, 1383); a stalled travel fails the run after 120 s even with no scene frames (:117, 569-572, 1916-1931); the strict schema refuses any field it does not list before sealing (diag_report.cpp:89-99, 760-776).

## R5.4 Updater and installer (stopped at this item; see "Not read" below)

Read completely: runtime/src/update/update.cpp (996), update_internal.h (125), update_crypto.cpp (282), update_install_windows.cpp (70), update_install_linux.cpp (98), update_install_macos.mm (145), update_transport_winhttp.cpp (211), update_transport_curl.cpp (272), update_transport_apple.mm (254), update_probe_posix.cpp (77); platform/android/.../Updates.kt (481). Read in part: installer/native/setup_engine.cpp (1100-1258, the updater), installer/native/setup.cpp (430-519, the update flow), app.manifest (by grep: asInvoker), AndroidManifest.xml:141-142.
Not read: update_tests.cpp (374); installer/native apart from those ranges (setup_engine.cpp 1-1100, setup.cpp outside 430-519, common.cpp 1380, archive.cpp 590, ui.cpp 703, start.cpp, text.cpp, the headers); installer/*.ps1 and *.sh (build-native-setup.ps1, sign-release.ps1, package-linux.sh).

How it works (verified in code):
- Three update paths with one key. (1) Linux AppImage and macOS: the game downloads `<asset>.manifest` and `.manifest.sig`. The manifest must be signed by the release RSA key (update.cpp:875-885, update_crypto.cpp:200-256: PKCS#1 v1.5, the whole encoded block compared, not parsed). It must also name this release's version, a version newer than the installed one, a 64-digit SHA-256, a size of at most 2 GiB and this build's asset name (update.cpp:964-968). The package must then match that size and hash (:912) before the platform step: an atomic rename on Linux (update_install_linux.cpp:62-96), and on macOS a signature check against the running bundle's designated requirement, then renamex_np swap (update_install_macos.mm:93-143). (2) Windows: the game only offers; Update starts the install's Nebula-Setup.exe --update --yes (update_install_windows.cpp:35-49). Setup fetches the listing, downloads Setup and its .sig, verifies the RSA signature over the whole file and requires the version resource (inside the signed bytes) to equal the release and be newer (setup_engine.cpp:1188-1217), then runs it with --apply-update (setup.cpp:513-515). Setup runs asInvoker, never elevated (app.manifest:13). (3) Android: the APK and its .sig; SHA256withRSA over the whole APK, then package name, versionName == release and newer (Updates.kt:295-324, 400-419); Android's installer then refuses another signing key or a lower version code. Its result receiver is not exported (AndroidManifest.xml:141-142).
- Downgrade and replay: refused on every path by the signed version's "newer than installed" test (update.cpp:965, setup_engine.cpp:1209, Updates.kt:316).
- Partial downloads: resumed with Range; a 200 to a Range request restarts the file (update_transport_winhttp.cpp:175-183, curl :208-216, apple :41-51, Updates.kt:364-371). An error page's body can land in the .partial file (winhttp :124-141 writes the body whatever the status); the hash or signature check then fails, the file is removed and one fresh attempt follows (update.cpp:901-926, setup_engine.cpp:1194-1216, Updates.kt:302-323).
- Unexpected answers: a listing that is not a JSON array keeps the last answer (update.cpp:664-675); depth is capped at 64 and size at 8 MiB (:31, :157); a release without all three HTTPS assets is skipped (:358). Redirects are followed only to HTTPS, at most 6 (winhttp :70-123, curl :143-145, apple :83-93). Timeouts: 15 s per request, and a download ends when it stalls 30 s.
- Failed installs: Linux keeps "<name>.previous" (a hard link, or a copy) and removes .next on any failure (update_install_linux.cpp:76-93). macOS removes its staging folder on every failure and keeps "Nebula.previous.app" after the swap (:99-142).

### P-092 wrong behaviour: an install of a beta is never offered the final release that follows it, on Windows, Linux, macOS or Android
- Evidence: select_newer skips every release whose beta-ness differs from the installed version's (update.cpp:337), offered() does the same (:638), and the asset is chosen by the installed channel (:705, update_install_*.cpp platform_asset, e.g. "Nebula-Beta-Setup.exe"). The same rule appears in Setup (setup_engine.cpp:1144-1153) and Android (Updates.kt:76, 140, 156). compare_versions already orders 2.0.0 after 2.0.0-beta.N (update.cpp:268-310).
- Why and fix: A player on 2.0.0-beta.3 sees no update when 2.0.0 ships. They stay on the beta until another beta appears, or install by hand. Fix: a beta install also takes a newer non-beta release, choosing that release's asset name, Nebula-Setup.exe and so on (manifest_takes then compares against that asset). A release install keeps ignoring betas.
- Confidence: verified in code.

Checked with a remark (no finding): URLs read back from update-check.json are not re-checked for HTTPS (update.cpp:581-588; select_newer checks them at :358). The curl GET restricts only redirect protocols (update_transport_curl.cpp:143-145), and the Apple GET checks the scheme only for POST (update_transport_apple.mm:114). A tampered cache could therefore make the check fetch over plain HTTP; integrity still holds through the signature and hash. Re-checking served_safely in read_kept closes it.

## Round 6

### R6.1 Android app

Read completely this round:
- java/dev/nebula/galaxy/: GameActivity.java (838), TouchControlsView.java (945), WiimoteUsb.java (306), PadSensors.java (199), GameFiles.java (133), ClipboardBridge.java (110), TouchLayouts.java (91), DisplayRate.java (51), Importer.java (25).
- kotlin/dev/nebula/galaxy/: LauncherModel.kt (536), SaveTransfer.kt (319), GpuDrivers.kt (165), ImportService.kt (165), Store.kt (98), LauncherActivity.kt (95), ModImport.kt (90), MiiImport.kt (86), Dropped.kt (54), Licenses.kt (43). Updates.kt (481) was read in Round 5.
- kotlin/dev/nebula/galaxy/ui/: Screens.kt (875), MotionTest.kt (296), Components.kt (270), Theme.kt (75), Starfield.kt (72).
- AndroidManifest.xml (160), app/build.gradle.kts (235), app/proguard-rules.pro (45).
- JNI sides: platform/sdl/mod_import_jni.cpp (106), platform/sdl/mii_import_jni.cpp (63), crates/nebula-import/src/jni.rs (141). Also crates/nebula-import/src/lib.rs:957-1004 (write_cooked_pak) and cook/mod.rs:459-519.
- The game-side JNI (nativeTouchControls and the rest) lives in host_window_sdl.cpp and is covered in R6.2.

Not read: the shipped Harness.java stub (19 lines), res/ (layouts, xml/wiimote_usb.xml, shared_files.xml), SDL's own Java half (org.libsdl.app; it is not in this tree, build.gradle.kts:186 takes it from nebula.sdlDir).

How the app is put together (verified in code):
- Two processes:
  - The launcher (LauncherActivity, Compose UI, ImportService) runs in the main process.
  - The game (GameActivity, an SDLActivity) runs in `:game` (manifest :129-137).
  - GameActivity kills its own process when it finishes (GameActivity.java:438-448).
  - Both processes share the app's files folder and one SharedPreferences file, "controls" (GameFiles.java:124-132).
- Permissions:
  - POST_NOTIFICATIONS is asked when the player presses Set up; setup starts whatever the answer (Screens.kt:394-402).
  - "Install unknown apps" is opened in system settings only when the player presses Update without it (LauncherModel.kt:350-352, :389-394).
  - USB permission is asked once per DolphinBar, when the game lists Wii Remote slots (WiimoteUsb.java:143-148).
  - RECORD_AUDIO and CAMERA are declared for mods only and are asked through SDL when a consented mod opens the device (manifest :32-36).
  - No storage permission: every file arrives through the system picker or a share.
- Lifecycle:
  - onStart and onStop switch the game's clocks and the sensors (GameActivity.java:363-427). Nothing happens on pause, so in multi-window an unfocused game keeps playing while visible.
  - Surface loss and return reach the renderer through GameSurface (:818-836).
  - Rotation, size and density changes are config changes, not recreations (manifest :137).
  - The touch layout is kept per screen shape in 16 dp steps (TouchLayouts.java:43-48).
  - Low memory reaches the host as SDL_EVENT_LOW_MEMORY (host_window_sdl.cpp:1249).
  - On process death there is no state restore. A recreated GameActivity cold-starts the game; the saves are whatever the game last wrote.
  - The launcher reads the `:game` exit reason (crash, ANR, low memory) the next time it resumes (LauncherModel.kt:501-531).
- Custom GPU drivers:
  - The offer is limited to Qualcomm SoCs on arm64 (GpuDrivers.kt:41-43).
  - A driver is a flat .zip with meta.json and a .so, up to 512 MB. It is unpacked to files/gpu_drivers/<name-version> (GpuDrivers.kt:90-133) and loaded through libadrenotools from that folder (args at :139-147).
  - Bad driver: the launcher writes a "starting" file before the start, and the game's first frame removes it. If the file is still there after the run and no game runs, the launcher selects the system driver again and says which driver failed (GpuDrivers.kt:153-160, LauncherModel.kt:503-508).
  - There is no signature or ABI check. The player chose the file, and only the Settings picker reaches it (dropped files go to the mod importer: Dropped.kt:28-37).
- Updates (Round 5): a signed APK through PackageInstaller with the player's confirmation. NoRoom is checked before the download (Updates.kt:374, :393).

#### P-093 (wrong behaviour) The launcher and the game write the same SharedPreferences file from two processes, so the launcher's stale copy undoes the game's changes.
- Evidence:
  - Both processes open "controls" with MODE_PRIVATE (GameFiles.java:130-132).
  - The game writes it: it removes RECORD_NEXT_RUN (GameActivity.java:150-152), saves opacity and haptics (:316-317), and sets RECORD_NEXT_RUN on a restart (:725).
  - The launcher loads it at start (LauncherModel.kt:71, GpuDrivers.kt:71). It writes it on Play (:496 commit) and again on the next resume (:506, :513 apply), and also at :262 and GpuDrivers.kt:76-78.
- Why: Android keeps one in-memory map per process. Without the deprecated MODE_MULTI_PROCESS it never re-reads the file, and every commit or apply writes the whole map. So when the player returns from the game, the launcher writes back its old map. Consequences:
  - RECORD_NEXT_RUN, which the game removed, comes back. Every later run records diagnostics, and the switch stays on (LauncherModel.kt:418).
  - Touch opacity and vibration changed in the game go back to their old values.
- Fix:
  - Give each value one writer.
  - The touch values belong in the game process's own file, as touch_layouts already is (TouchLayouts.java:40).
  - "Record next run" and "launched at" should be small files that are read fresh and replaced atomically, or values served by a ContentProvider in the main process.
- Confidence: the write-back path is verified in code against documented SharedPreferences behaviour. The visible effect is suspected, needs a run:
  1. Turn on Record diagnostics and press Play.
  2. Quit to the launcher and press Play again.
  3. Check whether the second run's log has the diagnostics header and whether the switch is still on.

#### P-094 (wrong behaviour) Setting up again deletes the installed game before the new one is built, so a failure, a full device or Cancel leaves no game.
- Evidence:
  - ImportService.run calls `Storage.removeGame` (install.properties and the whole game folder) before `Importer.build` (ImportService.kt:103-111, Store.kt:86-89).
  - The room check counts the installed game as free room (LauncherModel.kt:455-459), so the delete is what makes room.
  - The builder itself already writes `.game.pak.importing` and renames it over game.pak only at the end (lib.rs:966, :994-1002). On failure the temporary file is removed (:997-999).
- Why: if the second import fails or is cancelled, the old game is gone. The player has to set up again from the disc image, and still needs room for it. The saves are kept.
- Fix:
  - Delete first only when free space minus the new pak's size is below the margin, and say so on the Disc screen before Set up.
  - Otherwise build beside the old game and let the rename replace it.
- Confidence: verified in code.

#### P-095 (wrong behaviour) A full device during setup ends with an untranslated OS error, and setup killed midway can leave a 3.9 GB orphan that blocks the next setup.
- Evidence:
  - The room check is one fixed size: the North American game.pak, 3,913,558,602 bytes, plus 256 MB (Store.kt:78-79). It is checked when the disc is chosen and on resume (LauncherModel.kt:442, :417), and it disables Set up (Screens.kt:430-449). Nothing re-checks during the build.
  - On ENOSPC the Rust error text goes to the screen as is (ImportService.kt:131, :134, :143; jni.rs:131-139).
  - The import runs in the launcher's own process, with START_NOT_STICKY (ImportService.kt:94). If that process dies midway, the `.game.pak.importing` written so far stays in files/game/.
  - The next Disc screen counts free room without that file (LauncherModel.kt:455-456). install.properties is already gone, so `install` is null and Remove game is hidden (Screens.kt:659). Set up stays disabled even though starting it would delete the orphan (ImportService.kt:104).
- Why: a full device shows "Setup failed" with an English OS message. With P-094 the old game is gone too. After a killed setup the player cannot start one again on a nearly full device without clearing the app's data.
- Fix:
  - At launcher start, delete `files/game/.game.pak.importing` when no import runs.
  - Count the files/game folder as room in `roomBytes`.
  - Map ENOSPC to a translated "not enough room" text.
  - Take the needed size from the identified disc (region) instead of one constant.
- Confidence: the orphan and the gate are verified in code. That the process can die midway (it holds a foreground service) is suspected, needs a run: start setup with about 5 GB free, kill the main process (`adb shell am kill` does not stop a foreground service, so use `adb shell run-as dev.nebula.galaxy.test kill <pid>` on the test build), reopen, choose the disc, and read the Storage line and whether Set up is enabled.

#### P-096 (wrong behaviour) Mod imports never check free space, show a raw ENOSPC text when the device fills, and their staging folders are never swept after a killed import.
- Evidence:
  - ModImport copies the whole picked file to `files/.mod-incoming-<nanoTime>/` (ModImport.kt:49-66), then `import_mod` stages and unpacks it in the mods folder (mod_import_jni.cpp:77-89). The peak is about the archive plus its unpacked size, and there is no check before either step.
  - An IOException's message becomes the failure text (LauncherModel.kt:133-135, Screens.kt:211).
  - The folder is removed only in `finally` (ModImport.kt:77-79). Nothing else names `.mod-incoming` (grep over the tree), so a killed import leaves it for good.
  - The same holds for `gpu_drivers/.incoming-*` (GpuDrivers.kt:92; `list()` skips dot folders at :49 and nothing deletes them) and for SaveTransfer's `.saves-replaced-*` (SaveTransfer.kt:311).
  - In-game Import copies each picked document in GameActivity.copyDocument. A copy that fails (no space) is dropped with a log line only (GameActivity.java:700-704, :659-664), so the game gets fewer paths and cannot say why.
- Why: on a full device a texture pack fails with "write failed: ENOSPC (No space left on device)" in English. A process killed mid-import keeps gigabytes in a hidden folder that no screen counts or removes.
- Fix:
  - Check `usableSpace` against twice the source size plus a margin before copying.
  - Sweep the three staging patterns at launcher start.
  - Translate ENOSPC.
  - Pass a "failed: no room" result through nativeFilesPicked.
- Confidence: verified in code. The exact message wording is suspected (it is Android's libcore text).

#### P-097 (wrong behaviour) In the Phone input mode a controller press hides the touch controls for good, because the field it checks is never set.
- Evidence:
  - `controlsMode` is initialised to "auto" (GameActivity.java:107) and never assigned anywhere else.
  - dispatchKeyEvent and dispatchGenericMotionEvent hide the controls for any controller event while it says "auto" (:804-806, :812-814).
  - Phone mode means "always" (:473-476).
  - revealControls works only in Auto (:757-762), so a touch does not bring them back. Only an input-mode change or a Wii Remote let-go does (:474-478, :488-493).
- Why: a player who chose Phone mode to keep the touch controls beside a pad (a clip-on with missing buttons, or a keyboard's arrow keys reported as DPAD) loses them at the first press.
- Fix: test `nativeInputMode() == INPUT_AUTO` there and delete the field.
- Confidence: verified in code.

#### P-098 (crash, suspected) GameActivity handles every configuration change but colorMode, touchscreen, mcc and mnc. One of those recreates it inside a process whose native game cannot start twice.
- Evidence:
  - configChanges (manifest :137) lacks colorMode, touchscreen, mcc and mnc.
  - onDestroy kills the process only when the activity is finishing (GameActivity.java:438-448).
  - The comment there says the native game cannot start twice in a process.
  - What SDL's Java half does on recreation (exit the process, or run SDL_main again) is in org.libsdl.app, which is not in this tree.
- Why: moving the game to a display with another colour mode (an HDR monitor in desktop mode, or the other panel of a foldable), plugging in a touchscreen, or a SIM change recreates the activity. The likely outcomes are the game quitting without a save prompt, or a second native start.
- Fix:
  - Add the four flags.
  - In onCreate, treat any start with the SDL thread already used as a restart: finish, then start through the launcher (P-020's trampoline).
- Confidence: suspected, needs a run. On a Samsung DeX or desktop-mode device, move the running game between the built-in panel and an HDR monitor while watching `adb logcat -s SDL:V Nebula:V ActivityTaskManager:I`, and check whether the game exits or SDL_main starts again. Also read SDLActivity.onCreate in the SDL 3.4.18 source passed as nebula.sdlDir.

#### P-099 (wrong behaviour) The restart's CLEAR_TASK removes the launcher from the task, so after a restarted game ends the player lands on the home screen and the launcher's last-run report waits.
- Evidence:
  - restartGame starts a copy of the intent with NEW_TASK | CLEAR_TASK (GameActivity.java:723-728).
  - LauncherModel.launchGame says the game joins the launcher's task "and ends back here" (LauncherModel.kt:494).
  - checkLastRun runs only on the launcher's resume (:414-427, :501-531).
  - The GPU driver's "starting" mark is written only by launchGame (GpuDrivers.kt:139-147). A restarted run is not watched for a driver that shows no frame.
- Why: after Restart from the settings, quitting the game leaves the app. A crash or failed driver on the restarted run is reported only when the player opens Nebula again, and is reported as a plain crash.
- Fix: the trampoline proposed for P-020 can bring the launcher back underneath and rewrite the "starting" mark.
- Confidence: verified in code.

#### P-100 (wrong behaviour, suspected) A launcher opened by "Open with" or Share while the game runs can import mods into, or delete, files the running game is using.
- Evidence:
  - LauncherActivity refuses a second instance only for the USB intent (LauncherActivity.kt:42-45).
  - VIEW and SEND go straight to the model (:64-75), and the activity has the standard launch mode (manifest :69-74). A share from another app therefore makes a launcher with its own model.
  - That launcher's mod import writes the mods folder and mods.ini (mod_import_jni.cpp:45, :77-78).
  - Its Settings offer Remove game and Set up again, which delete game.pak (LauncherModel.kt:487-492, ImportService.kt:104). None of these checks `gameRunning`, which exists (SaveTransfer.kt:33-35) and is used only for save import, updates and the USB case.
- Why: the game reads game.pak by mapping or by file, and writes mods.ini. Two writers of mods.ini lose one side's change. Deleting the pak under the running game ends it.
- Fix:
  - While `gameRunning`, refuse Remove game and Set up again with the existing "close the game first" text.
  - Hand mod files to the running game through the in-game import path, or queue them until it ends.
- Confidence: suspected, needs a run. With the game running, share a .luma from the Files app to Nebula, and separately open Settings in that launcher and press Remove game. Watch the game log.

#### P-101 (performance) The launcher's sky is redrawn every display frame for as long as the launcher is open, and it jumps every two minutes.
- Evidence:
  - Starfield runs an infinite 120 s transition and draws 220 stars and two radial gradients each frame (Starfield.kt:42-71).
  - On a 120 Hz panel that is 120 full-screen redraws a second behind a static menu, including during the long setup import.
  - At the wrap the drift `time * speed * 0.25` and the twinkle `time * 120 * speed` are not whole cycles (:64-68), so every star jumps.
  - The "indeterminate" ProgressRing is a fixed 40° arc that never moves (Components.kt:247). Reading the disc and the first seconds of setup look stalled.
- Fix:
  - Drive the sky from frame time with periods that wrap whole, at a low rate (or still) while a dialog or the import screen shows.
  - Animate the indeterminate ring.
- Confidence: verified in code (the redraw rate is how Compose's infinite transitions work).

Checked with no finding:
- The JNI string and exception handling in the three JNI files (exceptions caught, UTF-8 by byte arrays).
- Updates' room check.
- SaveTransfer's staged replace with backup, and its zip-slip guard (SaveTransfer.kt:206-207).
- GpuDrivers' flat-zip guard (GpuDrivers.kt:100-102).
- WiimoteUsb's threading. Writes run unlocked, but a closed connection only returns -1.
- R8 keep rules: every name the native side calls is kept, including `public void *()` for GameActivity's callbacks.
- The disc picker uses a file descriptor, so no storage permission is needed.
- ImportService's 60-minute wake lock (:100) could run out on a very slow source. That is noted, not raised.
- AndroidManifest.xml declares the microphone and camera.any features twice (:13-14 and :37-38). This is harmless.

Touch controls (TouchControlsView.java):
- Hit testing: buttons by distance up to 1.25 radii, the nearest wins (:759-775). The D-pad within 1.2 radii, eight directions (:777-793). The floating stick only inside its zone (:751-757).
- Multi-touch: per pointer id 0-31, any id outside that is ignored (:613-616). CANCEL releases everything (:602-606).
- Layout: scaled by the shortest side (:259-263), kept clear of cutouts and gesture insets (:247-252), and stored per 16 dp shape.
- A mouse or keyboard attached: see P-102 in R6.2, which needs the host side.

Android table:

| Feature | State | Evidence |
|---|---|---|
| Setup from disc image (SAF fd, foreground service) | works | ImportService.kt:97-141, jni.rs:101-139 |
| Free-space check before setup | works, one fixed size | Store.kt:78-79, Screens.kt:430-449 (P-095) |
| Device full during setup | works badly (raw OS text; old game already deleted) | ImportService.kt:104, :131 (P-094, P-095) |
| Device full during mod import | works badly (no pre-check, raw text, orphans) | ModImport.kt:49-79 (P-096) |
| Process death and restore | cold start, no state restore; exit reason shown later | LauncherModel.kt:501-531 |
| Surface lost and returned | works | GameActivity.java:818-836 |
| Rotation, resize, density | works (config changes) | manifest :137 |
| Other config changes (colorMode, touchscreen, mcc, mnc) | missing | manifest :137 (P-098) |
| Multi-window | works (plays while visible) | GameActivity.java:363-372 |
| Low memory | works (through SDL) | host_window_sdl.cpp:1249 |
| Restart from settings | works, suspected race (P-020); launcher lost (P-099) | GameActivity.java:721-731 |
| Settings shared between processes | broken (P-093) | GameFiles.java:130-132 |
| Touch controls, multi-touch, per-shape layouts | works | TouchControlsView.java:577-793 |
| Phone input mode with a controller | broken (P-097) | GameActivity.java:107, :804 |
| Custom GPU drivers (Adreno) | works, failed-start fallback | GpuDrivers.kt:139-160 |
| Updates outside a store | works | Updates.kt (Round 5) |
| Wii Remotes over DolphinBar USB | works | WiimoteUsb.java |
| Wii Remotes over Bluetooth | missing (Android gives no L2CAP HID access) | WiimoteUsb.java:29-30 |
| Controller and phone motion | works | PadSensors.java, GameActivity.java:374-413 |
| Clipboard pictures | works | ClipboardBridge.java |
| Launcher during a running game | unguarded (P-100) | LauncherActivity.kt:42-75 |

### R6.2 SDL host (Linux and Android)

Read completely this round:
- platform/sdl/host_window_sdl.cpp (all 2527)
- native_game_host_sdl.cpp (all 611)
- sdl_host.h (162)
- CMakeLists.txt (all 411)

Read in earlier rounds: crash_log.cpp, audio_device_sdl.cpp, display_clock.cpp, thread_sampler.cpp, wiimote_link_android.cpp.

Not read: luma_system_sdl.cpp (561). It is LumaCore's clipboard, links and display snapshot for mods (services-system 4-6), so it is out of scope.

What differs between Linux and Android inside the SDL host (verified in code):
- Logging: Android sends stdout and stderr through a pipe to logcat and to logs/game.log, and keeps the last run as game.previous.log (host_window_sdl.cpp:228-284). Linux keeps stderr and adds the crash log only (:1072-1076).
- The event wait: Android pumps, then sleeps the timeout, because SDL's timed wait polls there (:1194-1204). Linux uses SDL_WaitEventTimeout (:1206).
- Java calls: Android sends them to the activity on SDL's thread only; other threads queue (:286-349).
- The display rate: Android votes a frame rate on the surface and asks the activity for a display mode (:351-391, :2176-2182).
- Touch: Android's touches arrive from the overlay through nativePointer and nativeTouchControls (:1610-1632). Linux takes SDL finger events as the pointer (:1283-1294).
- Back: Android's back gesture is HOME, or Escape in a menu (:1373-1386).
- Pad motion: on Android, controllers on Android's own driver take their motion from PadSensors.java, every sample, with Nintendo's axes turned (:659-803). SDL's HIDAPI sensors are kept for USB pads (:968-978).
- Minimised: on Linux a minimised window stops the game's clocks (:1259-1268). On Android the activity does this.
- Files: Linux takes window drops and launch imports (:1231-1238, :1398-1406). Android takes files in the launcher.
- Pickers: Android pickers go through the activity (:1839-1943). Linux uses SDL's portal or Zenity dialogs (:1944-2028).
- Restart: Android uses `restartGame` (P-020, P-099). Linux uses execv (host_main, Round 4).
- Touch controls and the Wii Remote: present on Android only (:2317-2399). On Linux every touch-control call is a no-op and `wii_remote_state` is empty (P-087).

#### P-102 (platform gap, suspected) On Android a mouse's buttons never reach the game: the touch overlay takes every click as a finger.
- Evidence:
  - TouchControlsView.onTouchEvent handles every MotionEvent without looking at its tool type or source, and returns true (TouchControlsView.java:577-611). SDL's surface under it therefore never gets mouse button events.
  - The only source of `keys.mouse`, which the keyboard-and-mouse bindings read, is SDL_EVENT_MOUSE_BUTTON_DOWN/UP (host_window_sdl.cpp:1324-1343).
  - A click goes to nativePointer, then finger_event, which sets `mouse_pointer = false` (:1017, :1625-1632). Hover motion still arrives as SDL mouse motion and sets it true again (:1295-1311). The pointer flips between a mouse's rules and a finger's rules.
  - With controls shown, a left click in the lower left grabs the floating stick (TouchControlsView.java:629-638).
  - The game still believes a mouse is there: `keyboard_or_mouse_attached` reports it (host_window_sdl.cpp:2411-2414, GameActivity.java:458-471).
- Why: on a tablet with a mouse, or in desktop mode, mouse-button bindings (right button and the rest) do nothing, and the left button acts as a finger.
- Fix: in TouchControlsView.onTouchEvent, return false for events whose tool type is TOOL_TYPE_MOUSE (or source SOURCE_MOUSE), so SDLSurface turns them into SDL mouse events. SDL_HINT_MOUSE_TOUCH_EVENTS is already 0 (:1084).
- Confidence: suspected, needs a run. With a USB mouse on the tablet, set the input mode to keyboard and mouse, bind B to the right button, press it in play, and watch whether `[input]` logs a mouse button. Also check whether a left click in the lower left moves Mario.

#### P-103 (platform gap) On Linux the Display page offers Windowed or Borderless and a window size, but the SDL host never applies either. There is no full screen on desktop Linux.
- Evidence:
  - The page shows MONITOR, DISPLAY MODE (WINDOWED / BORDERLESS FULLSCREEN) and RESOLUTION whenever `fullscreen_only` is false (settings_display_page.cpp:17-75). Only Android sets it (native_settings.cpp:2069-2071).
  - No file outside third_party calls SDL_SetWindowFullscreen, SDL_SetWindowBordered or SDL_MaximizeWindow (grep).
  - `display_mode` is never read under platform/sdl. Only the Windows and Apple hosts read it (host_window_win32.cpp:493, host_window_apple.mm:630).
  - `resize_window_client` is called only by the extension API (host_main.cpp:4194-4196).
  - `settings_monitors` returns one "Display" whose single resolution is the current window size (host_window_sdl.cpp:1799-1816).
- Why: a Linux player picks Borderless and nothing happens. The window size row has one entry. Gamescope (Steam Deck game mode) makes windows full screen by itself, so this shows on desktop Linux only.
- Fix:
  - Apply `display_mode` with SDL_SetWindowFullscreen and a null fullscreen mode (desktop-sized borderless) on SDL's thread when the config changes.
  - List SDL_GetDisplays and each display's usable size for MONITOR and RESOLUTION.
  - Apply a windowed size with SDL_SetWindowSize.
  - Until then, hide the three rows on Linux as Android does.
- Confidence: verified in code (the apply path does not exist).

Checked with no finding:
- `poll_host_pointer_state` reports a valid pointer only while a finger is down (:2199-2211), but the SDL host installs no pointer provider (native_game_host_sdl.cpp:605), so nothing reads it.
- Pad sample queue: bounded at 4096 (:1604-1607).
- Rumble: renewed every 50 ms with a 100 ms duration (:1142-1160).
- Surface withdrawal: waits up to 2 s for the renderer (:1670-1679).
- `driver_log` runs `logcat` through popen on Android (:2146-2168). It is used for diagnostics only.

Linux table (SDL host):

| Feature | State | Evidence |
|---|---|---|
| Window, Vulkan surface, HiDPI | works | host_window_sdl.cpp:1715-1753, :2170-2175 |
| Windowed / borderless switch | missing (row offered) | P-103 |
| Monitor choice, resolution list | missing (one entry) | :1799-1816 |
| Exclusive full screen | missing (not offered) | settings_display_page.cpp:43 |
| Minimise pauses the game | works | :1262-1267 |
| Covered or occluded window | missing (only minimise) | :1259-1268 |
| Keyboard, mouse pointer, wheel | works | :1295-1343 |
| Relative mouse (mods' first-person look) | works | :1163-1179 |
| Gamepads, motion, touchpads, rumble | works | :870-1000, :1142-1160 |
| Touchscreen | pointer only, no on-screen controls | :1283-1294, :2370-2399 |
| Real Wii Remote | missing | :2387-2390 (P-087) |
| Joy-Con pairing | stubbed (empty) | :2419-2423 |
| File drop and launch import | works | :1231-1238, :1401-1405 |
| File and save dialogs | works (SDL portal/Zenity) | :1962-2013 |
| Open a folder | works (file:// URL) | :2016-2023 |
| Restart | works (execv) | host_main (Round 4) |
| Update check and install | works | CMakeLists.txt:353-363 |
| HDR | missing in the host | no HDR call in host_window_sdl.cpp |
| DLSS, XeSS, vendor downloads | stubbed (Windows only by design) | :2048-2083 |
| Crash log | works | crash_log.cpp (Round 4) |

### R6.3 Apple host (macOS; iOS)

Read completely:
- host_window_apple.mm (1806)
- apple_host.mm (208)
- setup_apple.mm (336)
- build_macos.sh (200)
- CMakeLists.txt (203)
- metal.cmake 60-172
- Info.macos.plist.in, Nebula.entitlements
- release.yml 389-467 (the Mac job)

display_clock_apple.mm and audio_device_apple.mm were read in Round 4 and are cited from there. luma_system_apple.mm is mod code and was not read.

What ships:
- The Mac app is the `nebula_native` target (metal.cmake:99-114), not SDL. It is made of:
  - host_main.cpp;
  - native_game_host_sdl.cpp (the shared game-thread host);
  - the Apple files (AppKit window, GameController pads, CoreHaptics rumble, Core Audio output, CVDisplayLink clock, the first-start setup window);
  - the Metal renderer.
- The SDL-based `main` (MoltenVK) and `nebula_metal` targets are development builds (metal.cmake:85).
- The release job builds arm64 only (release.yml:446, `-a arm64`). Intel Macs get no release build, although build_macos.sh can make a universal app (:10-13, :102-116).
- No step in the job starts the built app (release.yml:442-467). Every feature below has, at most, been compile-tested in CI.
- The tree records two hand runs on Mac hardware, both outside the shipping host:
  - display_clock_apple.mm:221 measured the MoltenVK path on a 60 Hz MacBook Pro;
  - compile_metallib.py:6 measured the Metal compiler on a 2017 MacBook Pro.
- Nothing in the tree shows that `nebula_native` itself was run.

**P-104** (platform gap): iOS and iPadOS have no host that can build.
- **Evidence:**
  - The shipping target compiles host_window_apple.mm and setup_apple.mm (metal.cmake:102, :107). Both import AppKit with no platform guard (host_window_apple.mm:10, setup_apple.mm:6).
  - The target links AppKit and IOKit (metal.cmake:143-145).
  - The UIKit branch exists only for the SDL development target `main` (CMakeLists.txt:96-98).
  - apple_host.mm, audio_device_apple.mm and luma_system_apple.mm carry `TARGET_OS_OSX` and `TARGET_OS_IOS` branches (apple_host.mm:7, :69, :112, :191; audio_device_apple.mm:27). So the iOS half was written for some files and not for the window, the input or the setup.
- **Why and fix:** the CMake comment says the build covers "iOS and iPadOS" (CMakeLists.txt:2), but an iOS configure of `nebula_native` cannot compile.
  - Either write the UIKit counterpart: a UIWindow and UIView with a CAMetalLayer, GameController input, touch controls, and a document picker for the disc image.
  - Or say plainly in CMakeLists.txt:2 and metal.cmake:97 that the host is macOS only.
- **Confidence:** verified in code.

**P-105** (wrong behaviour): full screen entered or left through the system is never written back to the display setting.
- **Evidence:**
  - The green button, Window > Enter Full Screen (Cmd+Ctrl+F, host_window_apple.mm:877) and Escape out of full screen all change the window.
  - windowDidEnterFullScreen and windowDidExitFullScreen (:788-797) only set `borderless_active` and resize.
  - run_requests compares the setting with `g_applied_mode` alone (:629-640). It never compares the setting with the window's real state.
- **Effect:** the DISPLAY page keeps showing Windowed while the window is full screen. The next start opens windowed. Choosing the shown value again does nothing.
- **Why and fix:** two sources of truth. On enter and exit, store `display_mode` (Borderless or Windowed) in the render config and save it, and set `g_applied_mode` to match. The Windows host's Alt+Enter path should be checked the same way (R6.4).
- **Confidence:** verified in code.

**P-106** (wrong behaviour): the Mac app's own texts are English only, while the game ships 14 languages and NebulaText.tsv already holds most of these strings.
- **Evidence:**
  - setup window: setup_apple.mm:60, :105, :107, :125, :131-132, :149, :166, :178, :181, :216, :221, :241, :247, :251, :280;
  - app and Window menus: host_window_apple.mm:864-877;
  - open-panel messages: :1409-1412;
  - save panel: :1446 ("Export saves" is S.EXPORT_SAVES in NebulaText.tsv:2120);
  - controller model names on the CONTROLS page: :1609-1611 (the same in host_window_sdl.cpp:2241);
  - mod refusal: :1801;
  - the fatal alert title.
- The Windows host has the same kind of leftover: host_window_win32.cpp:1584 `SetTitle(L"Export saves")`.
- **Why and fix:** the owner's rule is that every new text is in all 14 languages. Route these through the localization table (the host can read the game language that apple_host.mm sets from NSLocale), and add the missing keys.
- **Confidence:** verified in code.

**P-107** (wrong behaviour): two screens with the same name cannot be told apart.
- **Evidence:**
  - Screens are identified by `localizedName` (host_window_apple.mm:1350). Two monitors of the same model report the same name.
  - make_window takes the last screen whose name matches (:1224-1228).
- **Effect:** the DISPLAY page lists two identical entries, and choosing the first opens on the second.
- **Why and fix:** use the CGDirectDisplayID from `deviceDescription[@"NSScreenNumber"]` (or CGDisplayCreateUUIDFromDisplayID, for an id that survives restarts) as `monitor.id`, and keep localizedName as the shown name.
- **Confidence:** verified in code; the effect needs two identical monitors.

**P-108** (maintainability, suspected thread fault): settings_monitors reads NSScreen off the main thread.
- **Evidence:** both branches of the main-thread test run the same block (:1364-1369). The comment ("read-only facts: any thread may ask") is the only justification. AppKit documents NSScreen as main-thread only for its geometry.
- **Why and fix:** the if/else is dead code either way. Cache the list on the main thread, at start and on NSApplicationDidChangeScreenParametersNotification, and return the cache.
- **Confidence:** dead branch verified in code. Whether the call faults needs a run with the Main Thread Checker enabled, opening the DISPLAY page from the game thread.

**P-109** (wrong behaviour, suspected): rumble stops for good after a haptic-engine reset, and the reset handler races the main thread.
- **Evidence:**
  - The resetHandler (:568-572) runs on a system queue. It writes `stopped->player` and `rumbling` while run_requests may be inside set_rumble for the same pad on the main thread.
  - It does not clear `g_rumble_sent_level`, so a steady rumble is not started again until the game changes the level (:617-627).
- **Why and fix:** have the handler only set an atomic "engine reset" flag for the pad. In run_requests, act on that flag: drop the player and force a resend by setting `g_rumble_sent_level` to a value no level can take.
- **Confidence:** suspected, needs a run. Hold a rumble (a long Launch Star pull), put the Mac to sleep and wake it (which resets haptic engines), and check whether rumble returns before the level changes.

Checked, no finding:
- **Menus:** Command shortcuts go to the menus (:932-934). The key a letter went down with is the one it comes up with (layout_key :888-913). Clearing the keys on resign-key covers the mouse too (:825-830).
- **Quit:**
  - Quit from the Dock or the menu during setup cancels the import first (:834-840, setup_apple.mm:145-153).
  - A partial game.pak is built beside the real one and renamed over it whole (setup_apple.mm:223-253). Unlike Android (P-094), the old game.pak stays until the new one is complete.
  - The free-space check counts the bytes of the game.pak being replaced (:48-64, :231-232).
- **Display assertion and failed full screen:** the controller display assertion (:1008-1025) is released when the window is not in front. A failed full-screen request is put in again at the next key-window event (:798-804, :819-821).
- **Signing:** build_macos.sh signs both game modules and then the app with the hardened runtime and the entitlements (:165-169). It verifies the signature again after ditto (:198). The entitlements allow disable-library-validation (needed for ad-hoc mod dylibs), audio-input and camera; the last two are for LumaCore services, out of scope here.

macOS table (every row is compile-only for the shipping `nebula_native`; "run" means the tree records a hand run of some build):

| Feature | State | Evidence |
|---|---|---|
| Window, Metal layer, Retina scale | compile-only | host_window_apple.mm:1220-1264, :695-700, :724-727 |
| Windowed / system full screen | compile-only; setting not written back (P-105) | :629-640, :788-804 |
| Window size from settings, resize | compile-only | :1230-1231, :1388-1396 |
| Monitor choice | compile-only; same-name screens collide (P-107) | :1222-1228, :1342-1382 |
| Resolution list | missing (native size only) | :1358 |
| Exclusive full screen | missing (not offered on any platform) | render_config.cpp:336 |
| Minimise pauses the game | compile-only | :806-813 |
| Covered window, other Space | missing (occlusion read only for the display assertion) | :1010-1011 |
| Keyboard (layout-aware letters), mouse, wheel | compile-only | :888-941, :1042-1080 |
| Relative mouse (mods' look) | compile-only | update_cursor (lines 1-330), :1192-1194 |
| Gamepads, motion, DualShock/DualSense touchpad | compile-only | :330-523 |
| Rumble (CoreHaptics) | compile-only; reset fault suspected (P-109) | :543-596 |
| Display held on while a pad plays | compile-only | :1008-1025 |
| Memory pressure | compile-only | :959-971 |
| Drop, Open With, .luma, nebula:// | compile-only | :656-671, :730-738, :769-771, Info.macos.plist.in |
| File and save panels | compile-only; English only (P-106) | :1399-1467 |
| First-start setup from disc image | compile-only; English only (P-106) | setup_apple.mm:175-260 |
| Restart (exec) | compile-only | :1533-1545 |
| Update check and install | compile-only | metal.cmake:146-165 |
| Display clock (CVDisplayLink) | run once through MoltenVK (dev build) | display_clock_apple.mm:221 |
| Audio output (Core Audio), 2 s hang fallback | compile-only | audio_device_apple.mm (Round 4), apple_host.mm:112-190 |
| HDR | stubbed (P-032) | Round 3 |
| MetalFX upscaling | compile-only (weak-linked) | metal.cmake:76-77 |
| DLSS, XeSS, vendor downloads | stubbed (Windows only by design) | :1486-1525 |
| Real Wii Remote | missing | :1696-1699 (P-087) |
| Joy-Con pairing | stubbed (empty) | :1725-1729 |
| Touch controls | not applicable | :1679-1707 |
| Thread sampler, state wake, pointer transition | stubbed (empty) | :1201, :1327, :1581-1583 |
| Crash log in game.log | missing (the system's own crash report only; crash_log.cpp is not in nebula_native) | metal.cmake:99-107, sdl_host.h:47-49 |
| Intel Mac release | missing (release builds arm64 only) | release.yml:446 |
| iOS / iPadOS | missing (P-104) | metal.cmake:102-107 |

### R6.4 Windows window and start

Read completely:
- runtime/src/render/host_window_win32.cpp (all 1799; lines 1560-1739 had been read in an earlier round);
- runtime/src/render/os_hdr.cpp (506), the HDR switch;
- runtime/include/galaxy/windows_child.h (87);
- hidden_start.h (64) and runtime/src/hidden_start.cpp (66);
- launch_args.h (194);
- windows_power.h (147);
- power_policy.h (40);
- the window-placement paths in renderer_d3d12.cpp:3316-3370 and vk/renderer_vk.cpp:2753-2820;
- display_modes.cpp find_monitor (200-215);
- native_settings.cpp native_settings_window_resized (6316-6327).

The brief names launch_args.cpp, power_policy.cpp, windows_power.cpp and windows_child*.cpp. These do not exist: the four are header-only, with *_tests.cpp beside them (not read).

The raw mouse path is raw_mouse_win32.cpp, read in Round 4. Here its window side was read: attach on WM_CREATE, WM_SETFOCUS and WM_ACTIVATEAPP, detach on focus loss, and WM_INPUT handed on (:649-657, :770-774, :1062, :1075, :1086, :1091, :1122).

**P-110** (wrong behaviour): on the Direct3D 11 renderer, display changes made during play never reach the window, and neither do monitor changes.
- **Evidence:**
  - Only renderer_d3d12.cpp:3316-3370 and vk/renderer_vk.cpp:2753-2801 have reconcile_display_state. That is the code that sends kPlaceWindowMessage and consumes `host_window::display_changed`.
  - Nothing under runtime/src/render/d3d11 names kPlaceWindowMessage, display_changed, display_mode or the monitor (grep). The D3D11 window is placed once, when it opens (renderer_d3d11.cpp:66, host_window_win32.cpp:486-537).
- **Effect on D3D11:**
  - DISPLAY MODE, MONITOR and WINDOW SIZE take effect only at the next start, yet the display-confirm countdown (native_settings.cpp:1087-1098) still runs over a picture that did not change.
  - A borderless window does not follow a resolution change of its monitor (WM_DISPLAYCHANGE sets the flag at :722-726 and nobody reads it).
  - A borderless window does not move to the primary monitor when its own monitor is unplugged.
- **Why and fix:** the placement is backend code copied twice instead of host code. Move reconcile_display_state into the host (host_window_win32.cpp, run from the render thread's per-frame window step for every backend) and delete the two copies.
- **Confidence:** verified in code. The visible effect needs a run: on `--renderer d3d11`, switch Windowed to Borderless in the DISPLAY page.

**P-111** (wrong behaviour): after the player resizes the window by dragging its edge, or drags it onto a monitor with a different scale, the window jumps back to the centre of the monitor named in the settings.
- **Evidence:**
  - WM_EXITSIZEMOVE stores the new client size as the windowed size (:782-789 → native_settings.cpp:6316-6327 set_render_config).
  - On the next frame reconcile_display_state sees `cfg.window_width/height` differ from `applied` and places the window again. It uses windowed_rect_on_monitor, which centres it on the work area of `cfg.monitor` (renderer_d3d12.cpp:3323-3347, renderer_vk.cpp:2761-2784, host_window_win32.cpp:469-484).
  - Moving the window never updates `cfg.monitor`. WM_WINDOWPOSCHANGED only raises `monitor_changed` for the renderer's output (:727-736).
  - A drag across monitors of different scales changes the client size through WM_DPICHANGED's suggested rect (:1016-1025). So the same jump follows.
- **Why and fix:** a size the player chose by hand is already applied. native_settings_window_resized should also record the window's current monitor (MonitorFromWindow → device name) into the config. Either the renderer's `applied` should be updated with the new values, or reconcile should skip a change that came from the window itself (a "from window" generation counter).
- **Confidence:** the chain is verified in code. The jump needs a run: in windowed mode, drag the right edge of the window and release; then drag the window to a 150 % monitor.

**P-112** (wrong behaviour, small): the settings' window-size change uses the system DPI, not the window's.
- **Evidence:** kResizeClientMessage computes the outer size with AdjustWindowRectEx (:737-743). Every other frame computation in the file uses AdjustWindowRectExForDpi with the monitor's DPI (:472, :530, :564, :1475). On a monitor whose scale differs from the primary's, the frame thickness differs, so the client ends a few pixels off the size chosen.
- **Fix:** AdjustWindowRectExForDpi(..., GetDpiForWindow(hwnd)).
- **Confidence:** verified in code; the pixel error needs a run on a mixed-scale desktop.

Checked, no finding:
- **Single instance:** a second start for the same data folder finds the running window by class and instance key. It hands over the files to import by WM_COPYDATA with a 5 s timeout and brings the window forward (:134-178, :674-696).
- **Session end:** WM_QUERYENDSESSION/WM_ENDSESSION flush achievements, stats and settings (:1098-1111).
- **Alt+Enter** is off on both DXGI renderers (renderer_d3d11.cpp:295, renderer_d3d12.cpp:2477), so Windows has no system full-screen path to fall out of step with the setting (compare P-105 on macOS).
- **Exclusive full screen** is not offered on any platform. A stored `display_mode=2` is read as borderless (render_config.cpp:336, settings_table.tsv:17). This is by design, so the Linux and macOS "exclusive full screen: missing" rows are the same design, not gaps.
- **HDR switch (os_hdr.cpp):**
  - It uses the 24H2 DISPLAYCONFIG_SET_HDR_STATE when the system knows it, else the older advanced-colour call (:182-220).
  - Displays the game turned on are listed in hdr-restore.txt before the switch (:355-361). So a crash watcher can turn them off (restore_listed :496-503), and a restart hands them over (:469-490, adopt :440-467).
  - A display the player turns off during play is not turned on again (plan::next :47-60, `declined`).
- **Child processes** run in a kill-on-close job, so they end with the game however it ends (windows_child.h:22-84).
- **Hidden start** requires absolute --settings and --crash-dumps away from the player's data folder (hidden_start.cpp:35-64). --data is not required, so a hidden start without it reads the player's data folder. What such a start writes there (shader caches, mods install) was not traced.
- **Power:** EcoQoS only while away, and the display held only while someone plays or watches (power_policy.h:28-38). The 1 ms timer is held only while in front (:1026-1028).
- **Maintainability notes:**
  - WM_ACTIVATEAPP is handled in two blocks (:1026-1032 falls through to :1082-1096).
  - Monitors are identified by the GDI name `\\.\DISPLAYn` (:1477), which Windows can renumber after a driver or cable change. find_monitor then falls back to the primary (display_modes.cpp:209-214), so nothing breaks, but the chosen monitor is forgotten.

Windows table (code complete unless marked; the owner tests on his own PC):

| Feature | State | Evidence |
|---|---|---|
| Window, per-monitor DPI, icon, drop target | works | host_window_win32.cpp:486-613, :1016-1025 |
| Windowed / borderless switch during play | works on D3D12 and Vulkan; missing on D3D11 (P-110) | renderer_d3d12.cpp:3316-3370, renderer_vk.cpp:2753-2801 |
| Exclusive full screen | not offered (by design, all platforms) | render_config.cpp:336 |
| Monitor choice, resolution list | works | :1471-1491, display_modes.cpp |
| Display changes (resolution, unplug) | works on D3D12 and Vulkan; missing on D3D11 (P-110) | :722-726 |
| Hand-resized window keeps its place | wrong: jumps to the centre (P-111) | :782-789 |
| Window size from settings | works; DPI off on mixed scales (P-112) | :737-743 |
| HDR switch (Windows HDR on and off) | works | os_hdr.cpp |
| Minimise, app in background | works | :1026-1035, host_main.cpp:6463-6470 |
| Covered window | works on D3D12 and D3D11 (DXGI_STATUS_OCCLUDED); missing on Vulkan | renderer_d3d12.cpp:4272, renderer_vk.h:269 |
| Raw mouse | works | :649-657, :770-774 |
| Keyboard, mouse buttons, wheel, text entry | works | :797-1015 |
| Touch screen | taken as the mouse (no touch controls) | :1767-1786 |
| Second start hands over files | works | :134-178, :679-696 |
| File and save dialogs | works; English only (P-106) | :1505-1610 |
| Restart, quit, session end | works | :1098-1117, :1733-1759 |
| Power policy, timer, display hold | works | windows_power.h, power_policy.h |
| Child processes tied to the game | works | windows_child.h |
| Hidden (test) start isolation | works for settings and crash dumps; --data not enforced | hidden_start.cpp:35-64 |
| DLSS, XeSS, frame generation, vendor downloads | works on D3D12; off on D3D11 by design | :1629-1731 |
| Real Wii Remote | works (Round 5) | wiimote_link_win32.cpp |

### R6.5 Leftovers

Read completely:
- runtime/src/discord_presence.cpp (438), achievements.cpp (235), movie_video.cpp (941), render/movie_playback.cpp (1322);
- installer/native: setup_engine.cpp (all 1258; 1100-1258 in Round 5), setup.cpp (634), common.cpp (1380), archive.cpp (590), ui.cpp (703), start.cpp (110), text.cpp (112);
- installer/build-native-setup.ps1 (296), installer/sign-release.ps1 (170), installer/package-linux.sh (111);
- platform/linux: build_linux.sh (91), llvm-toolchain.cmake (13), dev.nebula.galaxy.desktop (15), dev.nebula.galaxy.xml (23).

Not read: movie_video_idct.inl (196; only its kBuggyQuarterIdct use at :63 by grep), the installer/native headers, installer/linux/install.sh and uninstall.sh (only the lines grep found).

**P-113** (wrong behaviour, data loss): in Windows Setup, answering "No" to "Uninstall Nebula?" uninstalls Nebula and also deletes every save and setting.
- **Evidence:**
  - setup.cpp:308-312 asks with MB_YESNOCANCEL. The question is "Uninstall Nebula?" followed by "Saves and settings in {0} are kept. Choose No to delete them too." IDNO is passed as `removeUserData`.
  - setup_engine.cpp:1077 then runs `DeleteTree(paths::DataRoot())`. That is %LOCALAPPDATA%\Nebula, holding saves, settings.json, achievements and mods (common.cpp:1304-1310). DeleteTree deletes for good, with no Recycle Bin (common.cpp:467-511).
  - For a portable copy, "No" leaves `kept` empty, so the data folder goes with the rest of the root (setup_engine.cpp:1050-1066).
- **Why and fix:** "No" to a yes/no question about uninstalling reads as "do not uninstall". Here it is the most destructive choice, and it cannot be undone. Use a dialog whose buttons name the actions: TaskDialogIndirect with "Uninstall, keep saves", "Uninstall and delete saves" and "Cancel", plus a second confirmation for the delete. Move the user data to the Recycle Bin (IFileOperation with FOFX_RECYCLEONDELETE) instead of DeleteTree.
- **Confidence:** verified in code.

**P-114** (wrong behaviour): the uninstall runs on Setup's window thread, so the window stops responding while it deletes.
- **Evidence:** setup.cpp:312 calls integration::Uninstall directly from OnStart. Uninstall deletes the install root, which holds several gigabytes of cooked game content (setup_engine.cpp:1056-1066). DeleteTree retries a held file up to five times with Sleep(500) (common.cpp:500-510). Install, update and restore run on the engine's worker thread with progress; uninstall alone does not.
- **Fix:** run Uninstall through the same worker and progress path as the other modes.
- **Confidence:** verified in code. The freeze length needs a run: uninstall a full install from the Start menu's Nebula Setup.

**P-115** (wrong behaviour): Setup declares per-monitor DPI awareness but lays out everything at the system DPI, and never handles WM_DPICHANGED.
- **Evidence:**
  - ui.cpp:47 sets DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2.
  - ui.cpp:48 stores GetDpiForSystem() once, and every size goes through Px() with that value (ui.cpp:71, :114-115, :138).
  - The window opens on the monitor under the cursor (ui.cpp:117-122). There is no WM_DPICHANGED anywhere in installer/ (grep).
  - With per-monitor V2 Windows does not scale the client area. So on a monitor whose scale differs from the primary's, Setup's controls and fonts are drawn at the primary's scale, too small or too large. Dragging the window between monitors changes nothing.
- **Fix:** take the DPI from GetDpiForWindow after creation. Handle WM_DPICHANGED by applying the suggested rect, re-creating the fonts and moving every control again (the layout is already in 96-DPI units through Px and Move).
- **Confidence:** verified in code. The look needs a run on a mixed-scale desktop: start Setup with the cursor on the monitor that is not primary.

**P-116** (platform gap): HD replacement movies play only on Windows, and AV1 movies do not decode on macOS.
- **Evidence:**
  - A replacement movie beside the game (mp4 and the other kExtensions) is found only on Windows. MoviePlayback::find returns {} elsewhere (movie_playback.cpp:1242-1245). Its decoder is Media Foundation, and Decoder::run outside Windows marks itself failed (:709-714). The SDL build compiles the file anyway (platform/sdl/CMakeLists.txt:129).
  - Cooked AV1 movies (NMOV codec 1, a mod's movie) need NEBULA_AV1. CMakeLists.txt:1107 adds it to galaxy_render, galaxy_render_vk and galaxy_render_d3d11 only. The Mac app's renderer library is galaxy_render_mtl (platform/apple/metal.cmake:46), which is not in that list, and metal.cmake never names NEBULA_AV1 or dav1d (grep).
- **Why and fix:** the decode is per-OS where it does not need to be. Linux and Android already carry dav1d. Cook replacement movies to NMOV/AV1 at import on every platform, so one decoder serves all. Add galaxy_render_mtl to the AV1 list.
- **Confidence:** verified in code. What a Mac shows for an AV1 movie (the disc movie, or nothing) needs a run: a mod with a cooked AV1 movie, on the Mac build.

**P-117** (performance, suspected): AV1 movie decode is plain C and can stall the render thread.
- **Evidence:**
  - dav1d is built from its C functions only. cmake/dav1d.cmake:3-4 says the x86 and Arm assembly kernels "are not vendored yet". dav1d's C path is several times slower than its SIMD path.
  - The cooked decoder waits on the render thread for up to 250 ms for a frame (movie_playback.cpp:806, :948).
  - A seek or a late start of an AV1 movie decodes again from frame 0 up to the wanted frame (:1117).
- **Fix:** vendor dav1d's NASM and .S kernels (meson's asm list). Never block the render thread: show the previous frame when the next is not ready. Seek from the nearest key frame through an index written by the cooker.
- **Confidence:** the code paths are verified. The cost needs a run: play a 1080p AV1 mod movie on the Android tablet and on an iGPU laptop, logging decode time per frame and render-thread waits.

**P-118** (console-era remnant): the THP movie decoder keeps the SDK's IDCT column swap on purpose.
- **Evidence:** movie_video.cpp:104-105, "The SDK's THP _quarterIDCT swaps columns 3 and 4; the decoder does the same", `constexpr bool kBuggyQuarterIdct = true;`. Used at :628-634 and movie_video_idct.inl:63.
- **Why and fix:** the THP encoder writes standard JPEG blocks. The swap is a fault of the console decoder. It applies to every block row whose only non-zero coefficients are the first two: outputs 3 and 4 are exchanged (movie_video_idct.inl:59-67, chosen by row_extent 2 at :111-114). How visible it is was not measured. (Corrected in Round 7 after reading the .inl: it is not a quarter-resolution path.) Under the owner's rule that console faults are not kept, set it to false and compare a frame of each disc movie.
- **Confidence:** the flag is verified in code. The visible difference needs a run: decode one frame of each disc THP with the flag on and off, and compare.

**P-119** (wrong behaviour): save export and import carry the game's save files only, not achievements or stats.
- **Evidence:**
  - achievements.ini and stats.ini live in the settings folder (achievements.cpp:85, stats.cpp:42).
  - save_transfer.cpp writes and reads the save folder only (save_transfer.h:2-3). It names neither file (grep finds no match).
  - A player who moves to another PC or to the Android app with the export loses every unlocked achievement and the play statistics.
- **Fix:** add both files to the archive as an optional section (nebula-saves.json lists it), restored only when the target has none or the player chooses to.
- **Confidence:** verified in code.

**P-120** (platform gap): Windows Setup refuses any install folder that is not on NTFS.
- **Evidence:** setup_engine.cpp:175, "The install folder must be on an NTFS drive." This rules out a portable copy on an exFAT USB stick and a ReFS Dev Drive.
- **Why:** the saves use NTFS named streams (common.cpp:742-837, CopyTreeWithStreams). ReadNamedStreams already treats a drive without streams as having none (:804-806). Whether the game itself needs streams was not traced.
- **Fix:** require NTFS only where streams are written; accept ReFS (which has streams) and, for portable copies, exFAT with streams kept as side files.
- **Confidence:** the check is verified in code; the reason is suspected. Needs the trace of every stream writer in runtime/src and a portable install to an exFAT stick.

**P-121** (maintainability): small faults in the Windows Setup's helpers.
- **Evidence and fix:**
  - DeleteTreeOnce closes its find handle only when a file delete fails. A failure inside a subfolder or a reparse point throws past it and leaks the handle, and the retry loop then runs against an open handle (common.cpp:471-491). Use an RAII find handle.
  - VerifyRsaSha256File reads the whole downloaded Setup into memory to hash it (common.cpp:971), while Sha256File streams (:877-890). The Setup carries every program in its payload. Hash by streaming.
  - Rollback ignores the failures Register returns (setup_engine.cpp:1042), so a restore that could not rewrite the shortcuts reports success.
  - ZipReader::ExtractTo opens the target with the plain path (archive.cpp:107), while every other file call uses the \\?\ form for long paths (common.cpp:393-398).
- **Confidence:** verified in code.

Checked, no finding:
- **Discord presence:** Windows named pipe, Unix sockets on macOS and Linux; compiled into the game module (native/CMakeLists.txt:411) and the SDL build (platform/sdl/CMakeLists.txt:242).
- **Achievements:** kept in the settings folder and flushed at session end (achievements.cpp:85, host_window_win32.cpp:1098-1111).
- **Setup downloads:** TLS 1.2/1.3 only, HTTPS-to-HTTP redirects refused, resume by Range, SHA-256 check with one retry, release-key signature check before an update runs a downloaded Setup (archive.cpp:234-264, :350-422; setup_engine.cpp:1202).
- **Child processes of Setup:** kill-on-close job, only the pipe and NUL inherited (archive.cpp:456-565).
- **Release scripts:** the Setup and the update signatures use the release key; the key is checked against release-public-key.xml; Android passwords go through the environment, never the command line (build-native-setup.ps1:283-293, sign-release.ps1:74-86, :97-105).
- **Linux package:** the menu entry with no file drops the empty --import (installer/linux/nebula-launch:15); install.sh rewrites Exec to the installed path (install.sh:122). Linux ships one CPU level, while Windows ships a baseline and an x86-64-v2 build (build-native-setup.ps1:125). No compile flag in build_linux.sh or llvm-toolchain.cmake picks a level, so Linux is built for the compiler's default x86-64 baseline. This was not traced into platform/sdl/CMakeLists.txt.

Leftover features by platform:

| Feature | Windows | Linux | Android | macOS |
|---|---|---|---|---|
| Disc THP movies | works | works | works | compile-only |
| HD replacement movies (files) | works | missing (P-116) | missing (P-116) | missing (P-116) |
| Cooked AV1 movies | works | works (C decode, P-117) | works (C decode, P-117) | missing (P-116) |
| Discord presence | works | works | missing (no Discord IPC on Android) | compile-only |
| Achievements | works | works | works | compile-only |
| Achievements in save export | missing (P-119) | missing | missing | missing |
| Installer | Setup: works; NTFS only (P-120); uninstall P-113, P-114; DPI P-115 | install.sh: works | APK | build_macos.sh zip, ad-hoc signed |

## Round 7

Scope reached:
- item 1's FactoryGalaxy question (R7.1);
- item 4's dependency list for the verify scripts, done by grep (R7.4);
- item 5 (R7.5).

Not started: item 1's performance read, item 2, item 3, and the rest of item 4 (the bodies of the *_tests.cpp files and of each verify script). See the end of this round.

### R7.1 FactoryGalaxy scenario 4 (item 1, the coordinator's failing stop)

Read for this:
- Demo/ScenarioStarter.cpp (all 367);
- NPC/CometEventKeeper.cpp (all 91) and NPC/CometEventExecutorTimeLimit.cpp (all 62);
- Player/MarioModule.cpp 1-54 (isInputDisable), Mario.cpp 335-356 (checkKeyLock), MarioActor.cpp 2747-2757 (isEnableNerveChange), MarioAccess.cpp 462-483 (offControl and onControl);
- SceneUtil.cpp 28-47, 143-149, 290-297 and StageDataHolder.cpp 171-190 (how a starter finds its rail);
- GameSceneFunction.cpp 14-16, 58-60 and GameScene.cpp 351-353;
- the test's readiness check, tools/test_driver/luma_test/luma_test.c 559-609.

Disc data was read in memory from the player's own disc copy at C:\Users\<user>\Documents\MarioGalaxyRecomp\MarioGalaxy1\DATA\files\StageData. The files were FactoryGalaxy/FactoryGalaxyScenario.arc (scenariodata) and FactoryGalaxy.arc (stageobjinfo, startinfo, objinfo of layers B, O and J, commonpathinfo, and the points of rails 18 and 28). They were decoded with an inline Yaz0/RARC/BCSV reader passed to `python -I -`; no file was written.

What scenario 4 is (from the disc data):
- ScenarioData row 4 is クイックコメット: Comet `Quick`, the fast-foe comet. Its star appears from the flip-panel watcher (フリップパネル監視者).
- Its main-stage layer mask is 16386, that is LayerB + LayerO. One zone uses layer J (mask 512).
- Scenario 2, which passes, uses LayerB + LayerL (2050). So scenario 4 differs from a passing stop only by LayerO and that zone layer.
- LayerO holds exactly one object, a ScenarioStarter: l_id 109, rail CommonPath_ID 28, flight time Obj_arg0 300.
- LayerO also holds its own start point (MarioNo 0) at (-59866, 1699, -39961).
- Rail 28 (l_id 28, 3 points) ends at (-59832, 1699, -39977), on that start point. Scenarios 1-3 do the same: rail 18 ends at the LayerL start point.

The start sequence, in the source:
1. ScenarioStarter::init finds the rail by l_id (StageDataHolder.cpp:178). The rail exists, so the port's "no rail, not made" path (ScenarioStarter.cpp:56-61) does not apply.
2. WaitScenarioCameraEnd (:362-366), then WaitToStart until the player's MsgAutoRushBegin binds the player (:323-327).
3. CinemaFrameBlank, then RailMove for 300 steps: the flight. A second visit can skip it with A (:138-146).
4. endBindAndSpinDriverJump releases the player with the rail's last velocity (:162-166).
5. ShowWelcomeLayout calls notifyEndScenarioStarter, which sets GameScene's nerve to Action. The starter kills itself once the Welcome animation stops (:185-207).
6. Nothing in this chain waits for a button. The only player-input condition is the optional skip.
7. The comet adds nothing that waits either:
   - CometEventKeeper creates a time-limit executor only for "Red" and "Black" (CometEventKeeper.cpp:32-37).
   - For "Quick" the game only speeds up enemies, for example Dossun.cpp:123-128.
   - executeOnWelcomeAndRetry's startGalaxyCometEvent does nothing when there is no executor (CometEventKeeper.cpp:54-60, SequenceUtil.cpp:118-121).

The test's stop condition (luma_test.c:571-609) had already passed opening camera, isExecScenarioStarter and isDemoActive. So GameScene left the starter nerve, which means step 5 ran and the player was released.

What is left that can keep MarioModule::isInputDisable true (MarioModule.cpp:13-33):
- `mMovementStates.inputLocked`. Mario::checkKeyLock (Mario.cpp:338-353) sets it in three cases:
  - during the stage's first 75 frames, and the lock is cleared only on a frame where Mario is not jumping. A player released mid-air who never touches ground keeps it for ever;
  - while MarioActor's nerve is neither Wait nor NoRush (MarioActor.cpp:2747-2757), that is, while bound to some object;
  - it is never set while swimming.
- the first-person view;
- one of four animations running: hard landing, the two damage landings, ステージインB (stage-in B);
- `mRemoteDemo`, which an object sets with MR::offControl (MarioAccess.cpp:470-472) or which dark damage sets (MarioDamage.cpp:1209).

**Answer:**
- The game's start for this scenario does not wait for anything a test has to do. It is the same flight-and-welcome sequence as scenarios 1-3, ending 300 steps after the bind. A 7,200-frame hold is therefore not a designed start sequence.
- It is a real soft lock in this build, or a game state the source does not reach on the console. Since it happens identically on all three renderers, it is in the game step, not in drawing.
- The likeliest cause, suspected, is that the player released at the end of rail 28 never lands. The lock is then kept by the airborne branch of checkKeyLock. That fits "no crash, frames still produced".
- The landing spot is the LayerO start point. A stage start (startinfo common MarioNo 22, camera 977) sits about 83 units away, so ground is expected there in the original game.
- The second candidate is a stuck bind: MarioActor not back in Wait.

**Exact run to settle it:** enter FactoryGalaxy 4 with the tour. Every 60 frames, log:
- Mario's position and gravity vector;
- `mMovementStates.jumping` and `inputLocked`;
- MarioActor's nerve name;
- isStatusActive(MarioStatus_FpView), the current animation name (the getCurrentBckName in MarioAccess.cpp:462-464) and `mRemoteDemo`;
- whether the zone with layer J is loaded.

Then repeat with the starter skipped (a save that has visited the stage, pressing A), which shows whether the release velocity is the cause.

**P-122** (wrong behaviour, suspected soft lock): FactoryGalaxy scenario 4 (the fast-foe comet) never gives control to the player after its opening flight.
- **Evidence:** the coordinator's runs: 7,200 frames of isInputDisable on D3D12, Vulkan and D3D11. The chain and the disc data are above.
- **Why and fix:** not known until the run above names the term holding the input. If the player never lands, compare the release (endBindAndSpinDriverJump with mJumpVec, ScenarioStarter.cpp:163) and the gravity at (-59832, 1699, -39977) with Petari and with the console. If the zone with layer J holds the ground there, check that zone's load for scenario 4.
- **Confidence:** that the source waits for no player action is verified in code. The cause is suspected and needs the run above.

**P-123** (maintainability): the comet time limit is converted between seconds and steps three times, and the default is right only because two conversions cancel.
- **Evidence:**
  - CometEventKeeper.cpp:34 converts the table's seconds to steps (getTimeLimitFromTable :82). It then divides by cStepsPerSecond back to seconds.
  - NebulaMods::cometTimeLimit returns frames (NebulaMods.hpp:59-60) and is divided the same way.
  - CometEventExecutorTimeLimit::getTimeLimit converts to steps again (:59-61). Its default is `stepsOfSeconds(30)` in steps (:12), converted once more.
  - This gives 30 × 60 × 60 = 108,000, the console's value (Petari CometEventExecutorTimeLimit.cpp:47-52, `return 108000`). But the name says 30 seconds while the value means 30 minutes, and it is right only at 60 steps a second.
- **Fix:** keep one unit, steps, from the table to the layout. Name the default `stepsOfSeconds(30 * 60)`.
- **Confidence:** verified in code.

### R7.4 Verify scripts that depend on the test plug-in or on run.log (item 4, partial)

Read: tools/verify/run.sh (all 91). The rest was found by grep over tools/ for run.sh, common.sh, tour.sh, test_plugin and run.log.

The coordinator verified by a run that the plug-in test_plugin.py installs is refused with L504, and that tour.sh then reads an empty run.log.
- run.sh installs that plug-in for every run (run.sh:73-76) and writes run.log (:86).
- common.sh's run_build is run.sh (common.sh:10-29).

**P-124** (untested): every verify script that starts the game goes through run.sh, so all of them depend on the refused plug-in. Each needs the plug-in for its tour or events, or reads run.log lines the plug-in writes.
- **Through common.sh** (run_build → run.sh → test_plugin.py): all_stops.sh, boots.sh, extended.sh, fs_det.sh, galaxy.sh, gpulost.sh, home_det.sh, interp_real.sh, luigi.sh, luigi2.sh, mii.sh, movie.sh, perf.sh, resize.sh, save.sh, sizes.sh, stopcross.sh, validate.sh, window.sh, tour.sh, and monkey.py (writes a run.sh that sources common.sh, :52-57).
- **Through tour.sh:** look.sh (:15), pgo_profile.sh (:23) and rt_inputs.sh (:11).
- **Readers of run.log** (they work only on a log that has the plug-in's [tour], scene and present lines): gaps.py, loads.py, look_audit.py, matchframes.py, losscheck.py, perf_summary.py and water_summary.py.
- **Why and fix:** the test plug-in is a mod and now goes through the mod validator. Install it as the test extension the host loads with `--extension` (run.sh already passes it, :85), or give test runs a validator exemption bound to `--hidden-start`, so the release validator stays strict.
- **Confidence:** the dependency is verified in code. That each of these scripts fails, rather than only tour.sh, is suspected; it needs one run each of boots.sh and window.sh, which need no tour.

Not reached in item 4: the bodies of the 82 *_tests.cpp files, native/tests, and what each verify script checks.

### R7.5 Leftovers (item 5)

Read completely:
- runtime/src/movie_video_idct.inl (196);
- runtime/src/update/update_install_linux.cpp (98) and update_install_macos.mm (145);
- installer/linux/install.sh (141) and uninstall.sh (26);
- installer/native/setup_engine.h (120), archive.h (126), text.h (18).

Not read: installer/native/common.h (281) and ui.h (152). Their functions were read in common.cpp and ui.cpp in Round 6.

P-118 was corrected after reading the .inl: the swap is in transform_row_dc_ac (rows with only DC and the first AC coefficient), not in a quarter-resolution path.

**P-125** (platform gap): the in-game update installs itself only on Windows. On macOS and Linux, as released, it can only name the new version.
- **Evidence:**
  - macOS: platform_can_install needs a designated requirement with a team identifier (update_install_macos.mm:36-52, :79-87). The release is signed ad hoc (build_macos.sh, R6.3), so running_requirement returns null and can_install is false for every player.
  - Linux: the installer is used only when $APPIMAGE is set (update_install_linux.cpp:19-25, :53-59). The release ships only Nebula-Linux-x64.tar.gz (package-linux.sh:104-109, sign-release.ps1:112-113). No AppImage is built anywhere in installer/ or platform/linux. So the AppImage path is never used, and the update offers a tar.gz the player must install by hand with install.sh.
- **Why and fix:**
  - macOS: sign with a Developer ID and notarise; the swap code is ready.
  - Linux: either build the AppImage the code expects, or teach the update to run the new package's install.sh with --yes and the installed root. install.sh already updates in place, keeping content/game.pak (install.sh:76-78, :89-96).
- **Confidence:** verified in code.

**P-126** (wrong behaviour, small): the Linux install.sh and uninstall.sh have rough edges.
- **Evidence:**
  - Started without a terminal (a file manager's "Run"), `[ -t 0 ] || ask=0` turns off every question. With no --disc, the script fails with a message on stderr that no one sees (install.sh:19, :79).
  - The program folder is replaced before the disc import runs (install.sh:90-96, :102-106). current.json still names the old version when the import fails, and the old version folder is kept until the end (:132-134), so the old version still starts. But a failed import into the existing content/ folder may leave the game half written. Whether nebula-import writes in place was not traced.
  - Nothing checks that the package's architecture matches the machine (install.sh has no `uname -m` check; package-linux.sh:37-40 picks the name from `file`).
  - uninstall.sh removes `content`, `saves`, `settings`, `mods` and `data` only with --all (uninstall.sh:19-21). This is the safe form that P-113 asks for on Windows.
- **Fix:** open a terminal when started from a desktop (x-terminal-emulator / gio launch), or use zenity for the failure. Make nebula-import write to content.new and rename it over. Check `uname -m` against VERSION's architecture.
- **Confidence:** verified in code, except the import's write pattern.

### Game-source performance: provisional ranking from earlier rounds

Round 7's read of the game step was not done, so this ranking holds only the game-source items already found in Rounds 1-4 (area 4). It is not the full list the brief asks for.

1. **P-039:** sensor hits tested pair by pair between groups each step (SensorHitChecker.cpp), which is worse than linear.
   - Fix: a uniform grid or sweep-and-prune per group pair.
   - Expected effect: large on crowded stages (star bits, coins, enemies), on every device, most on Android and iGPU laptops, whose CPUs are slowest.
   - Risk to identical behaviour: the order of hit messages must stay the same. Keep the original pair order by sorting the candidate pairs.
   - Proof: the deterministic tour (GALAXY_TEST_DETERMINISTIC=1) with a per-frame hash of the actors' positions and nerves, old against new.
2. **P-040:** the step has no worker parallelism; the shared state that blocks it is named in P-040.
   - Expected effect: large on many-core CPUs; small on phones with few big cores.
   - Risk: high. Calculation and movement order are observable.
   - Proof: the same per-frame hash over the full 102-stop tour.
3. **P-038:** draw-buffer packet insert, remove and light re-sort are linear per call.
   - Expected effect: medium, during clipping churn (fast camera turns, large stages).
   - Risk: low if draw order is kept.
   - Proof: per-frame draw-list hash.
4. **P-013:** every object construction and delete searches all arenas linearly.
   - Expected effect: medium at stage load and on stages that spawn and kill often; none at steady state.
   - Risk: low.
   - Proof: load-time logs (loads.py) and the arena tests.

### Round 7 items not started

- Item 1, except R7.1: GameScene.cpp, SceneFunction.cpp, SceneObjHolder.cpp, NameObj/*, LiveActor.cpp, LiveActorGroup, AllLiveActorGroup, ClippingDirector and the clipping files, HitSensor.cpp, CollisionDirector and the KC queries, EffectSystem, the System and Render Nebula*.cpp files, the gravity manager. The ranked performance list was not made.
- Item 2 (console-era design, file by file).
- Item 3 (area 2 leftovers: the post-process, bloom, AO, camera, upscaler and frame-generation files).
- Item 4, beyond R7.4.
- Item 5 is done (R7.5), except common.h and ui.h.

## Round 8

Item 1 (area 4, where the game's own code costs time) and item 2 (area 8, console-era design by behaviour). Files read completely are named in each finding and in the coverage table. All file paths are under native/petari/src/Game unless said otherwise.

### R8.1 Game step: findings

**P-127** (performance, small): GameScene::draw does work every game frame that the shipping build never uses.
- Evidence: GameScene.cpp:300-304 tests MR::isCameraInWater and WaterAreaFunction::getCameraWaterDepth every draw to pass the depth to NebulaHost::gameContext(NEBULA_CONTEXT_CAMERA_WATER); the comment says it is for ray tracing, which is shelved and not in the release. GameScene.cpp:306 and :322 poll the host for galaxy-select names and travel every draw. GameScene.cpp:721 (isDrawMirror) looks up "MirrorArea" by name through MR::isPlayerInAreaObj every draw.
- Why and fix: GameSystem::frameLoop (System/GameSystem.cpp:182-206) runs one update, one calcAnim and one draw per game frame; the host makes the extra pictures of an unlocked rate from the frame lists, so these run 60 times a second (more during a cutscene skip, :199-202), not per presented picture. The cost is small but pure waste in the release. Put the water query behind the ray-tracing option, and give isDrawMirror a cached area list (made once at scene start).
- Confidence: verified in code.

**P-128** (performance, small): each CategoryList call fetches the list executor through two pointer hops and the draw pass walks about 25 category lists per frame, most of them empty in a given galaxy.
- Evidence: SceneFunction.cpp:73-128 (every execute/drawOpa/drawXlu does SingletonHolder<GameSystem>::get()->mSceneController->getNameObjListExecutor() and sets Frame::category before and after); GameScene.cpp:517-579 (draw3D, about 25 passes, plus repeated MR::loadViewMtx/loadProjectionMtx at :533-534 and :568-572).
- Why and fix: the cost per pass is small; the count is fixed by the console's draw order. A frame-built list of non-empty passes (skip a pass whose list is empty before the category marks and matrix loads) removes the empty ones. Behaviour is unchanged when only empty passes are skipped.
- Confidence: verified in code (the count of empty passes per galaxy needs a run: count non-empty DrawType lists per frame in five galaxies).

**P-129** (performance, medium): every collision query walks every part in the global zone and in each overlapping zone; there is no spatial index over parts, so the cost is actors × parts per step.
- Evidence: Map/CollisionCategorizedKeeper.cpp:94-154 (checkStrikePoint), :156-225 (collectStrikeBall), :227-298, :300-379 (collectStrikeLine), :381-480 (area lists). Zone 0 has no bounds (:98, :162: "pZone != mZones.begin()"), so every part placed there is tested by every query. Binder (LiveActor::updateBinder, LiveActor.cpp:506-515) issues these queries for every bound actor every step.
- Why and fix: the console had few moving parts; a galaxy with many moving platforms and many bound enemies pays parts × queries. A per-keeper bounding-volume tree or a uniform grid over part spheres, rebuilt from the parts that moved this step (mStillCount == 0), keeps the per-part tests identical and in the same order (sort candidate parts by their index in the zone before testing), so hit lists and their order stay the same.
- Confidence: verified in code (size of the gain needs a run: count parts tested per query in a galaxy with many moving parts, for example Bowser's battle galaxies).

**P-130** (performance, small): a moving part whose sphere touches its zone's box edge recomputes the whole zone's bounds every step.
- Evidence: CollisionCategorizedKeeper.cpp:43-44 and :63-64 call calcMinMaxAndRadiusIfMoveOuter for each moving part; :619-634 recomputes the whole zone (:587-617, two passes over all parts, one sqrt each) when the part's centre is outside the box shrunk by its radius, which is true for any part that defines the box. In the build with hook points (not NEBULA_PLAIN_MODULE), :61-62 also recomputes the whole zone once per valid part on the first step (parts squared).
- Why and fix: a zone whose boundary part moves pays parts × moving parts per step. Grow the box incrementally (union with the moved part's new box) and recompute fully only on removal; or recompute once per zone per step after the loop (as the plain build already does for the first step). The bounds only gate queries, so a slightly larger box changes no hit; to keep bit-identical bounds, recompute once per zone per step instead of once per moving part.
- Confidence: verified in code.

**P-131** (performance, medium): once any ball or line query has filled 64 hits, every later query allocates three heap arrays per part tested, for the rest of the scene.
- Evidence: Map/CollisionParts.cpp:296-318 (StrikeLists: stack room for 64, `new` beyond); :323, :413, :486 construct it with maxHits; CollisionCategorizedKeeper.cpp:214 and :287 pass mHitInfoRoom - hitCount; :515-519 (growHitInfo) doubles mHitInfoRoom and never shrinks it (starts at 32, :13).
- Why and fix: after one crowded query (128 room), the first part tested in each later query gets maxHits 128 and allocates. Cap the per-part working list at the stack room and loop (the server returns hits in a fixed visit order, so a cap and a second pass give the same list), or keep the three working arrays as members of the keeper sized to mHitInfoRoom.
- Confidence: verified in code (how often the room passes 64 needs a run: log growHitInfo calls over a playthrough of five galaxies).

**P-132** (performance, small): ball queries against a moving part invert the part's previous matrix and take the inverse matrix's scale (three square roots) on every query, instead of once when the part moves.
- Evidence: CollisionParts.cpp:338 (NebulaMtxInverse(mPrevBaseMatrix) inside checkStrikeBall), :330-332, :259-261, :419-421 (mInvBaseMatrix.getScale per query); the matrices change only in updateMtx (:147-184) and resetAllMtxPrivate (:132-137).
- Why and fix: cache mInvPrevBaseMatrix and the average inverse scale in updateMtx and resetAllMtxPrivate. Same operations on the same inputs, so results are bit-identical.
- Confidence: verified in code.

**P-133** (performance, small): prism lists deduplicate by linear search over the hits found so far, and a prism that misses is retested in every block it appears in.
- Evidence: Map/KCollision.cpp:266 and :324 (std::find over pOut for each visited prism); :331 (hitSpherePrism runs again for a missing prism found in another block).
- Why and fix: hits per query are few, so this is small for balls; area lists (checkArea3D, behind createAreaPolygonList; its callers were not read) can hold hundreds. A per-query visited stamp per prism (a u32 array per server, stamped with a query counter) makes both checks constant time; the hit order is unchanged.
- Confidence: verified in code.

**P-134** (performance, small; console-era remnant): each collision prism stores one vertex plus normals, and the other two vertices are rebuilt at every use with a cross product and a divide.
- Evidence: Map/KCollision.cpp:794-827 (getPos for vertex 1 and 2); called per visited prism in checkArea3D (:271-273), in calcCollidePosition (CollisionParts.cpp:463, :471) and in CollisionParts.cpp:274. The importer already cooks the file ("NCOL", KCollision.cpp:27-30).
- Why and fix: the console's KCL format packs triangles as prisms to save memory. The importer could add the two derived vertices to the cooked file, computed with this same function, so the values are bit-identical and getPos becomes a load.
- Confidence: verified in code.

**P-135** (performance, small): the clipping pass is linear and cheap, but it calls MR::getClippingJudge (a scene-object lookup) once per target actor per step, and for every clipped actor also re-tests the drawn-while-clipped frustum each step.
- Evidence: LiveActor/ClippingActorHolder.cpp:36-42; ClippingActorInfo.cpp:39-42, :52-56 (two getClippingJudge calls), :100-115; ClippingDirector.cpp:94-97.
- Why and fix: pass the judge into judgeClipping once per step. Bit-identical. Listed for completeness; clipping is not a top cost.
- Confidence: verified in code.

**P-136** (performance, small): looking up a sensor by name hashes the string on every call, and actors ask by name in their per-step code.
- Evidence: LiveActor/HitSensorKeeper.cpp:40-54 and :103-117 (MR::getHashCode(pSensorName) per call); LiveActor::getSensor (LiveActor.cpp:276-282).
- Why and fix: sensor names are string literals; a compile-time hash at the call site (constexpr overload taking a literal) removes the hashing. Same result.
- Confidence: verified in code (how often per step needs a run: count getSensor calls per step in a busy galaxy).

**P-137** (performance, small): picking a free actor from a pool scans the pool from the start every time.
- Evidence: LiveActor/LiveActorGroup.cpp:18-26 (getDeadActor), used by the spawners of pooled actors (coins, star pieces, effects actors).
- Why and fix: a free list (or a "first maybe dead" index) gives the same actor as the scan when the list keeps index order (take the lowest dead index). Same choice, so same behaviour.
- Confidence: verified in code.

**P-138** (performance, small): a bound actor that does not move runs the same map query twice per step.
- Evidence: LiveActor/Binder.cpp:198-236 (findBindedPos). For a zero velocity, steps is 1 and step is zero (:201-205); the loop runs i = 0 and i = 1 (:208), both at the same position, each a full Collision::checkStrikeBallToMap (:218-221) over every part (P-129).
- Why and fix: same inputs and no collision change between the two calls, so the second result is the first. Skip i = 1 when the step is zero and i = 0 already ran. Identical behaviour. Also: Binder::bind adds and removes its extra collision part to the global zone on every call (:114-116, :174-176), and removal is a linear search over that zone (CollisionCategorizedKeeper.cpp:668-680).
- Confidence: verified in code (how many standing bound actors per step needs a run).

**P-139** (performance, medium): gravity is found by testing every registered gravity for every actor and every shadow every step; there is no spatial filter.
- Evidence: Gravity/PlanetGravityManager.cpp:16-71 (calcTotalGravityVector loops all mNumGravities, a virtual calcGravity each, only stopping early at a lower priority); PlanetGravity.cpp:22-57, :81-98 (each shape's range test comes after its own vector work, for example a square root in calcGravityFromMassPosition). Callers each step: LiveActor::movement (LiveActor.cpp:126-128) and ShadowController::updateDirection (ShadowController.cpp:110-123).
- Why and fix: cost is (actors + shadows) × gravities. A per-gravity bounding sphere (shape extent plus mRange plus mDistant; infinite range always kept) in a grid or sorted list lets the loop skip gravities that cannot apply, while testing the rest in the same priority order, so the sum and the GravityInfo pick are unchanged. Risk: the bound must be conservative for every shape (cube, cone, torus, segment, wire); prove with a run that compares both paths for every query.
- Confidence: verified in code (gain needs a run: count gravities tested per query in a large galaxy such as Gusty Garden or the Comet Observatory).

**P-140** (performance, medium): every moving actor asks for its shadow to be recomputed every step, and each recompute is a gravity search plus a map line query, even when nothing under the actor moved.
- Evidence: LiveActor.cpp:168 (MR::requestCalcActorShadow at the end of every movement); ShadowController.cpp:43-50, :101-108, :110-154 (updateDirection and updateProjection, the line query MR::getFirstPolyOnLineToMap at :139).
- Why and fix: the console paid this per shadow too. Keep the result when the drop position, drop direction and the length are bit-equal to last step and the projected part has not moved (mStillCount > 0) and no part was added, removed or validated since (a keeper change counter). Risk: a missed change source would leave a stale shadow; prove with a run that computes both ways and compares every step.
- Confidence: verified in code.

**P-141** (performance, small): the emitter pool is walked in full, empty slots included, up to three times per step.
- Evidence: Effect/ParticleEmitterHolder.cpp:23-46 (update walks every slot of every chunk); ParticleCalcExecutor.cpp:30-53 (movementNormal calls update(false); movementIgnorePause3D and movementIgnorePause2D each call update(true), the 2D one repeating the 3D one's walk, since update(true) only acts on group 1). The pool starts at 512 (GameScene.cpp:488-491).
- Why and fix: keep a list of live ParticleEmitter records (the EmitterManager already keeps per-group live lists, EmitterManager.cpp:113-121) and walk that; drop the duplicate update(true). Same emitters handled in the same order if the live list keeps slot order.
- Confidence: verified in code.

**P-142** (performance, small; console-era remnant): lights are loaded per object draw in the console's eight-register model, with each world light moved into view space on the CPU at every load.
- Evidence: Map/LightFunction.cpp:150-192 (loadActorLightInfo writes lights 0-2 and the ambient through Frame::setLight per actor draw; :170-171 multiplies by the view matrix per light per load); :208-220 (point light); :15-29 (initLightRegisterAll writes all eight every frame from GameScene::draw).
- Why and fix: GX had eight light registers loaded between draws. The modern form is a per-frame light table (world space, transformed once per frame on the GPU or once per frame on the CPU) and a small light-set index per draw. Fewer bytes per draw in the frame list and no per-draw matrix work. Behaviour is identical when the same values reach the shader.
- Confidence: verified in code (bytes per frame need a run: frame-list size with and without, in a busy galaxy).

**P-143** (performance, small; console-era remnant): each shadow volume is drawn twice, front faces then back faces, with the shape's vertices rebuilt and emitted for each pass.
- Evidence: LiveActor/ShadowVolumeDrawer.cpp:91-135 (two drawShape calls with NEBULA_STENCIL_ADD then NEBULA_STENCIL_TAKE); ShadowVolumeBox.cpp:21-24, :135-177 (30 vertices written per pass).
- Why and fix: the two passes come from hardware without two-sided stencil. Every target renderer has separate front and back stencil operations, so one draw with no culling and front=ADD, back=TAKE gives the same counts when the stencil ops wrap (increment and decrement with wrap commute). Risk: if NEBULA_STENCIL_ADD saturates rather than wraps, order matters; check the renderer's mapping first.
- Confidence: verified in code (the stencil op mapping in the renderers not read this round).

**P-144** (performance, small): each animated model's joint matrices are copied twice per frame after the skeleton is evaluated: into the model's pose, then into the frame list.
- Evidence: Render/Model.cpp:574-579 (viewCalc calls capturePose, a memcpy of every joint, :532-551); :619-631 (addPoseBlock copies the pose's joints again into Frame::arena, once per frame per pose).
- Why and fix: the pose copy exists so a view or mirror pass can draw a pose captured earlier; the frame copy exists because the host keeps the list after the game moves on. Evaluating the skeleton straight into the frame arena (and pointing mPose at it for the rest of the frame) removes one copy of jointCount × 48 bytes per model per frame. Same values.
- Confidence: verified in code.

**P-145** (performance, small): the scenario-select and mod mesh draw buffers merge same-material entries by scanning every entry already in the buffer, so entry is quadratic in draws per buffer.
- Evidence: Render/Model.cpp:292-315 (entryMatSort loops over all mEntries for each new entry); used where ModelSys::mDrawBuffer is set: Scene/ScenarioSelectScene.cpp:105-117, System/LumaDraw.cpp:1527-1542; Model::entry is called only from Scene/MultiSceneActor.cpp:77 (through ModelManager::entry) and LumaDraw.cpp:1534, so the main game draw path does not use it.
- Why and fix: a small hash map from (image, material id) to entry index gives the same merge in constant time; the draw order comes from the stable sort keys, unchanged.
- Confidence: verified in code.

**P-146** (maintainability, small): frame-list building, skeleton evaluation and collision results live in file-scope or member state that only one caller at a time may use; each is the first thing that stops the work running on more than one thread (detail in R8.2).
- Evidence: Render/FrameList.cpp:253-316 (sCurrent, sOpenDraw, sLights, sSlot, sPart and the rest); Render/Model.cpp:13-19, :136-137 (Skeleton::mCurrentMtx, mJoint, mModel, mCurrentMtxCalc, sStack); Map/CollisionCategorizedKeeper.cpp:10-15 (mHitInfoArray, mNumHits per keeper); CollisionParts.cpp:521-531 (sAreaPrisms); LiveActor/Binder.cpp:76-79 (sLocalPlanes); LiveActor.cpp:292 (LiveActor::sClipScope).
- Why and fix: see R8.2 per subsystem.
- Confidence: verified in code.

**P-147** (console-era remnant, small): the mod log notice counts its line lifetimes in game frames, not in time.
- Evidence: Screen/NebulaModLogNotice.cpp:453-464 (stepOncePerFrame keys on NebulaFrameClock::count()).
- Why and fix: the owner's rule is time-based lifetimes for everything outside the game step. A frame that overruns (a slow device) holds the notice longer in seconds. Use NebulaClock::nowNs, as NebulaFrameGen does (System/NebulaFrameGen.cpp:16-22).
- Confidence: verified in code.

No game-side work is repeated for interpolation: the game makes one frame list per game frame (GameSystem.cpp:182-206) and the host blends between lists using the slot and generation numbers the game writes (Model.cpp:637-638, Material.cpp:990-993). The costs interpolation adds on the game side are the pose and light copies in P-144 and P-142.

### R8.2 What stops each subsystem running on worker threads

For each: the shared state today, the smallest change that would let it run on workers, and the risk to identical behaviour. None of this is wired today (P-040).

- Animation and skinning. Shared state: the Skeleton namespace globals and its one stack (Render/Model.cpp:13-19, :136-148); MtxCalc::calc reads Skeleton::mJoint and mModel; joint callbacks (Model.cpp:175-179, :189-191) call actor code that can touch anything; ModelManager::update and calcAnim (LiveActor/ModelManager.cpp:25-94, :404-418) also run the material and visibility players, which write the shared Material when not owned. Smallest change: make the Skeleton state a struct passed by pointer (one per worker), and run calcAnim in parallel only for models with no joint callback and no mCalcCallBack (Model.cpp:491-493); the rest stay on the game thread in list order. Risk: low for the math (same operations per model); the callback rule must be exact, since a callback can read another actor's matrices.
- Visibility and clipping. Shared state: ClippingActorInfo::startClipped/endClipped move actors between lists and connect or disconnect them from the scene and draw lists (ClippingActorHolder.cpp:44-52, LiveActor.cpp:294-349); LiveActor::sClipScope is a global mode (ClippingActorInfo.cpp:78-86). Smallest change: split judgeClipping into a pure judge (isJudgedToClip and isDrawnWhileClipped read only camera planes and the actor's sphere) run on workers into a per-actor result byte, then apply the transitions on the game thread in list order. Risk: low; isGroundShown caches mGround on first use (ClippingActorInfo.cpp:58-67) and must be filled before the parallel judge.
- Particles. Shared state: one EmitterWork (pWd) for all emitters (Particle/EmitterManager.cpp:65, :118); the free lists of emitters and particle chunks (:20-40); the per-resource made counts that seed each emitter (:100-101). Smallest change: one EmitterWork per worker and a per-worker chunk cache refilled from the shared list; run each group's emitters in parallel, then delete finished emitters on the game thread in list order. Risk: medium; emitter callbacks (MultiEmitterCallBack) call game code, and the random seeds must stay per emitter (they are: mixSeed per creation), so emitters with a callback stay serial.
- Collision. Shared state: the result buffer per keeper (CollisionCategorizedKeeper.cpp:10-15, read back by Collision::getStrikeInfoMap), sAreaPrisms (CollisionParts.cpp:521-531), Binder's sLocalPlanes (Binder.cpp:76-79), and Binder's add and remove of its extra part in the global zone (:114-116, :174-176). Smallest change: a query takes a caller-owned result buffer (the keeper's buffer stays for the old call sites), and the extra part is passed to the query as an extra candidate instead of being inserted. Queries then are read-only over parts, and the queries of shadows (P-140) and of actors whose movement does not depend on each other could run in parallel. Risk: medium; actor movement is not independent (an actor's bind reads parts moved by actors earlier in the same step), so only the shadow queries and other read-only queries are safe to move first.
- Shadow building. Shared state: the request list (ShadowController.cpp:43-50, :94-99) and the collision and gravity queries above. ShadowController::update writes only its own fields. Smallest change: once collision queries take a caller buffer, run the requested controllers' update in parallel (gravity search is const, PlanetGravityManager.cpp:16). Risk: low; shadows read collision after all movement ran, so no ordering changes.
- Frame-list building. Shared state: all of FrameList.cpp's file-scope state (:253-316), the light state (sLights, sLightOwner), the site pipeline table (:418-446), and Model and Material caches written during draw (Material.cpp:719-766, Model.cpp:581-617). Smallest change: a recorder object holding today's file-scope state, one per worker, each writing its own Store; the game thread concatenates the stores in category order and rebases indices. The material and pose caches need a per-frame "claimed by" mark or a lock-free first-writer rule. Risk: medium; draw order inside a category must stay as today, so the split is per category list, not per object.

### R8.3 Top ten game-source performance changes (ranked)

The proof method named "step-hash replay" below: record one input file per galaxy (five galaxies: Good Egg, Gusty Garden, Bowser's Star Reactor, Toy Time, the Observatory), run the old and new builds with the host's deterministic step (test_frame_step, NebulaFrameClock.cpp:69-77), and after every step hash every live actor's position, velocity, gravity, nerve and step, and the binder's ground, wall and roof planes; the two builds must give the same hash at every step. "Frame-list hash" is the same with a hash of each frame list's items, constants and blocks.

1. Spatial index over collision parts (P-129). Where: Map/CollisionCategorizedKeeper.cpp query loops. Effect: medium to large on phones and low-end PCs in galaxies with many moving parts and many bound actors; small on a desktop. Risk: low if candidates are tested in zone order. Proof: step-hash replay, plus a debug build that runs both searches per query and compares the hit lists.
2. Sensor pair checks by space, not pair by pair (P-039). Where: LiveActor/SensorHitChecker.cpp. Effect: medium on phones in crowded scenes (star-piece swarms, coin rings, many enemies); small on a desktop. Risk: medium, the order sensors are added to each other's lists (addHitSensor, capped by group size) must match today's pair order. Proof: step-hash replay including each sensor's list contents.
3. Gravity search with a per-gravity bound (P-139). Where: Gravity/PlanetGravityManager.cpp:16-71. Effect: medium on all devices in large galaxies (every actor and every shadow searches every step). Risk: medium, the bound must be conservative for every shape. Proof: a debug build computing both and comparing every query, then step-hash replay.
4. Keep a shadow's projection when nothing under it changed (P-140). Where: LiveActor/ShadowController.cpp:101-154 and LiveActor.cpp:168. Effect: medium on phones (one gravity search and one line query per moving shadowed actor per step). Risk: medium (missed change source). Proof: a debug build computing both every step and comparing projection position, normal and sensor.
5. Worker threads for the clipping judge, shadow updates and callback-free animation (P-040, R8.2). Where: ClippingActorHolder.cpp:36-42, ShadowController.cpp:43-50, Model.cpp:140-194. Effect: medium to large on multi-core phones and handhelds where the game thread is the limit; small on a desktop. Risk: low for the judge and shadows, medium for animation. Proof: step-hash replay and frame-list hash with workers on and off.
6. No heap allocation in collision queries after the hit room grows (P-131). Where: Map/CollisionParts.cpp:296-318. Effect: medium on phones once triggered (three allocations per part per query for the rest of the scene); none before. Risk: none if the cap loops. Proof: step-hash replay in a galaxy that triggers growHitInfo (log it first).
7. Constant-time draw-buffer insert, remove and light re-sort (P-038). Where: System/DrawBuffer.cpp, DrawBufferGroup.cpp. Effect: small to medium on all devices in scenes with many draw-buffer entries. Risk: low if the per-group order is kept. Proof: frame-list hash.
8. Constant-time arena lookup on construct and delete (P-013). Where: System/NebulaArena.cpp. Effect: small to medium during spawns and scene loads, small in steady play. Risk: none (allocation addresses may change; nothing should depend on them, but P-013's own check applies). Proof: step-hash replay.
9. Per-frame light table instead of per-draw light loads (P-142). Where: Map/LightFunction.cpp:150-220, Render/FrameList.cpp:729-782. Effect: small on the CPU, small to medium on the frame-list size on phones (fewer bytes per draw). Risk: low, the values reaching the shader must match. Proof: frame-list hash of the resolved light values per item, and a screenshot compare in five galaxies.
10. The collision small cleanups together: cached inverse previous matrix and scale (P-132), no double query for a still actor (P-138), zone bounds once per zone per step (P-130), visited stamps for prism lists (P-133), cooked prism vertices (P-134). Where: Map/CollisionParts.cpp, LiveActor/Binder.cpp, Map/CollisionCategorizedKeeper.cpp, Map/KCollision.cpp and the importer. Effect: small each, small to medium together on phones. Risk: none for P-132, P-133, P-134 and P-138 (same operations); low for P-130. Proof: step-hash replay.

Left out of the ten (small): P-127, P-128, P-135, P-136, P-137, P-141, P-143, P-144, P-145.

### R8.4 Console-era design by behaviour (item 2)

What was read for this item: System/NebulaFrameClock.cpp, NebulaFrameGen.cpp, NebulaModCalls.cpp (1-60), the FrameClock header, Render/FrameList.cpp, Render/Model.cpp, Render/Material.cpp (625-1004), and the item 1 files above; a grep over System/Nebula*.cpp, Render/*.cpp and their headers for fixed arrays and 640, 480, 60 and 4:3 constants. Petari was not compared this round.

Fixed-size arrays and pools: the containers met this round all grow (NameObjGroup.cpp:15-26, NameObjHolder.cpp:11-34, ClippingActorInfoList ClippingActorInfo.cpp:134-143, ClippingGroupHolder.cpp:95-98, PlanetGravityManager.cpp:73-83, ParticleEmitterHolder.cpp:86-111, EmitterManager.cpp:20-24, Binder.cpp:85-103, CollisionCategorizedKeeper.cpp:515-519, MR::Vector via AssignableArray::grow, Util/Array.hpp:70-121). Their starting sizes are the console's (ClippingActorHolder 2560 and a table of 8192, ClippingActorHolder.cpp:12-13; ShadowControllerHolder 0x500 and 0x400, ShadowController.cpp:16-17; EffectSystem 512, GameScene.cpp:488-491; group capacity 0x40, ClippingGroupHolder.cpp:10; hit room 32, CollisionCategorizedKeeper.cpp:13) and cost only memory. One fixed list still drops entries when full:

**P-148** (console-era remnant, wrong behaviour possible): a sensor's list of touching sensors is fixed at its group size and drops any more without a word.
- Evidence: LiveActor/HitSensor.cpp:89-94 (addHitSensor ignores a sensor past mGroupSize); filled from SensorHitChecker.cpp:34-38.
- Why and fix: on the console a crowd larger than the group size meant some contacts were never seen; the actor then misses a hit (a coin not collected in a swarm, a push not felt). This is gameplay behaviour, so it waits for the owner: the modern form is a list that grows, which changes behaviour only in crowds larger than the authored size.
- Confidence: verified in code (whether any galaxy fills a group needs a run: log each time addHitSensor drops, over the five replay galaxies).

Frame-count timers outside the game step: P-147 (mod log notice). NebulaFrameGen uses real time (NebulaFrameGen.cpp:7-22). ParticleDraw's use of NebulaFrameClock::count (Particle/ParticleDraw.cpp:63, :147) is a once-per-frame mark, not a timer.

Single-buffer assumptions: none found in the frame list; it keeps a store per frame in flight and reuses one only after the renderer releases it (FrameList.cpp:221-223, :349-376). The pose (Model.cpp:532-551) and material caches are keyed by frame number, so a second frame in flight never sees a stale entry.

Heaps sized for the console: the frame-list pages are 1 MB and grow (FrameList.cpp:219, :539-566); the ring grows from 1 MB (:1366-1380). Nothing fixed found this round.

Console layouts and packed formats converted at use: P-134 (collision prisms rebuilt from one vertex and normals at each use). KCollision.cpp:97-109 and :868-872 keep the console's float-to-integer conversion (nebula_f32_to_s32_console) for cell lookups, which must stay for identical collision. Texture sampler fields are read from the console's ResTIMG record at each table build (Material.cpp:787-797) but only when the table is rebuilt, which is cached (:774-777).

4:3 or 640x480 assumptions: the 2D design units stay console-sized on purpose (NebulaFrameClock.hpp:10-19: NEBULA_AUTHORED_LINES lines, layouts 608 wide, effects authored over 640 texels, 832 at 16:9); they are units the data is authored in, not render sizes, and refreshDisplay scales them for any shape (NebulaFrameClock.cpp:25-48). No render-size assumption found.

Exists only because the console needed it: the eight light registers loaded per draw (P-142); two-pass shadow volumes (P-143); the prism collision format (P-134); and the roughly 25 fixed draw passes per frame in the console's order (P-128), which the owner may want kept for identical layering.

### Round 8 items not read

Item 1, not read: Scene/SceneObjHolder.cpp (only its creation lines, :147-225), NameObjAdaptor.cpp, MovementOnOffGroupHolder.cpp, NameObjFinder.cpp, NameObjRegister.cpp, ModelChangableObjFactory.cpp, NameObjFactory.cpp (a factory table), AllLiveActorGroup.cpp, ClippingActorInfo's ViewGroupCtrl, HitSensorInfo.cpp, Map/CollisionDirector.cpp, Util/CollisionPartsFilter and TriangleFilter, the gravity shapes other than PlanetGravity.cpp, Effect/MultiEmitter*.cpp, EffectSystemUtil.cpp, ParticleDrawExecutor.cpp, all of Game/Particle (the particle simulation itself: EmitterCore, ParticleCore, ParticleDraw, EmitterDynamics, ParticleField) apart from EmitterManager.cpp, the ShadowSurface* and ShadowVolume* files apart from ShadowVolumeDrawer.cpp and ShadowVolumeBox.cpp, Map/LightDirector.cpp, LightDataHolder.cpp, LightPointCtrl.cpp, LightZoneDataHolder.cpp, LiveActor/ActorLightCtrl.cpp (beyond Round 7's lines), Render/Clip.cpp, TypedDraw.cpp, Transform.cpp, ModelData.cpp, RenderTexture.cpp, GameTexture.cpp, Render/Material.cpp lines 1-624 and 1005-1052, and System/Nebula*.cpp apart from NebulaFrameClock, NebulaFrameGen and NebulaModCalls (1-60).

Item 2, not read: a file-by-file read of System/Nebula*.cpp (only the grep above), Render/Clip.cpp, TypedDraw.cpp, Transform.cpp, ModelData.cpp, RenderTexture.cpp, GameTexture.cpp, the engine side (runtime/, native/host), and every Petari comparison.

## Round 9

Item 1 closes area 4 as far as room allowed; item 2 is the console-era pass on what item 1 read. The coordinator's benchmark facts are used as given: on the 9950X3D the game step costs 0.8 to 1.4 ms per 60 Hz step in eight scenes (HoneyBeeKingdom highest, IceVolcano lowest), with no stutter. On that machine the game step is not the limit. Every effect named "phones" below is scaled from that by the CPU gap and is not measured.

### R9.1 Particle simulation

Read: Particle/EmitterCore.cpp, ParticleCore.cpp, ParticleDraw.cpp (1-200), ParticleResource.cpp (330-709).

**P-149** (performance, medium on phones; console-era remnant): every particle is built as a matrix in eye space on the CPU, multiplied back to world space, and written to the frame as three 3x4 matrices.
- Evidence: Particle/ParticleDraw.cpp:71-88 (setParticleMtx concatenates sEyeToWorld with the eye-space matrix the shape code built); :132-135 (one camera inverse per emitter draw); :197-200 (each instance copies `now`, `before` and `coord`); ParticleResource.cpp:523-537 (once per particle).
- Why and fix: GX drew billboards from a position matrix loaded in eye space, so the shapes compute eye-space matrices; the port then undoes the view to hand the host world-space instances. The modern form is a small instance (world position, previous position, size, angle, colours, texture frame) with the billboard built in the vertex shader. That is about a third of the bytes and no per-particle matrix work on the CPU. Risk: medium; each shape type (point, line, billboard, Y-billboard, direction, stripe, cross) must be reproduced exactly. Proof: frame-list hash of resolved corner positions, and screenshot compares on particle-heavy scenes.
- Confidence: verified in code for the CPU path.

**P-150** (console-era remnant, small): the particle material copies GX alpha-compare, combiner-stage and indirect-matrix settings into a constant array on every emitter draw, and the primary and environment colours carry over from one emitter to the next like global registers.
- Evidence: Particle/ParticleResource.cpp:644-675 (constants 84-93: stage colour and alpha modes, alpha compare 0 and 1, alpha op, alpha references); :677-691 (indirect matrix scaled by 2^exponent); :616-628 (sPrm and sEnv carry over; the comment says no shader reads them from the material).
- Why and fix: these mirror GX_SetTevOp, GX_SetAlphaCompare and GX_SetIndTexMtx. The values are fixed per resource and shape, so the modern form is one registered material per resource, made at load, as Material.cpp already does for models (Material.cpp:731-741). The carry-over can go. Behaviour unchanged.
- Confidence: verified in code.

The simulation itself is linear: per live particle, one field pass, an optional callback, the shape's calc and a position update, over a linked list of slots taken from chunks (ParticleCore.cpp:219-305, ParticleResource.cpp:343-431, EmitterCore.cpp:20-62). No allocation once the chunk cache is warm. Worker blockers are as in R8.2 (one EmitterWork, the chunk free list).

### R9.2 FactoryGalaxy scenario 4: what bounces Mario

Facts given: after the opening flight Mario stays at x=-65691.99, z=-39761.16, y between about 824 and 962, for 7,200 frames; never on ground, not bound, not in a rush, not flying, not dead. Scenario 2 gets control.

How the lock works (verified in code):
- The opening flight ends with MR::endBindAndSpinDriverJump (Demo/ScenarioStarter.cpp:162-166), which sets mInputOff (Util/PlayerUtil.cpp:564-570). MarioActorRush.cpp:248-250 then sets Mario's mMovementStates.inputLocked.
- The only thing that clears it here is Mario::checkKeyLock: `if (!mMovementStates.jumping) inputLocked = false` (Player/Mario.cpp:338-341). It runs at the top of Mario::update (:1532-1534) and reads the jumping flag as the previous step left it.
- Mario::update later calls checkAndTryForceJump (Mario.cpp:1617; Player/MarioJump.cpp:2158-2215). When Mario is on ground and not jumping, and the floor code is a jump code, it forces a jump along the ground face's normal:
  - JumpLow 0x6 and JumpParasol 0x22: power 30;
  - JumpMiddle 0x7: 60;
  - JumpHigh 0x8: 100;
  - JumpNormal 0xB: 30 plus 10 along the previous velocity.
  - Powers from MarioConst.cpp:53. Codes from Map/CollisionCode.cpp:26-54, by table order.
- tryForceJump sets jumping = true and onGround = false in the same step (MarioJump.cpp:701-702). The same holds for a jump requested by another actor through MR::forceJumpPlayer → tryForceJumpDelay → tryForceFreeJump (MarioJump.cpp:686-689, :2200-2210).
- So on such a floor, every landing is turned into a new jump within the same step. checkKeyLock never sees jumping == false, the lock never clears, and an end-of-step probe never sees "on ground".

Most likely cause 1: the spin-driver jump lands Mario on a floor with a jump code. A low jump code fits all the facts:
- Fixed x and z: the force jump is along a flat floor's normal, and with input locked nothing steers him.
- About 140 units of height: power 30 is the lowest code power. Mario's fall gravity was not read, so the height check is suspected only.
- Never "on ground" at the probe's sample point.

If the floor is the map's own collision, the question is whether the original lands him at the same point. That depends on the starter's jump vector mJumpVec and the gravity at the start, and needs the disc data. The Quick comet changes code only in Enemy/Dossun.cpp and Enemy/Onimasu.cpp (a whole-tree grep for isGalaxyQuickCometAppearInCurrentStage finds those two, the BGM wrapper and EventUtil.cpp). So a jump floor that is in this scenario's layer only, or a landing point that differs from scenario 2's, is data, not comet code.

Most likely cause 2: Mario lands on a part that moves faster in the Quick comet and keeps kicking him up:
- A Dossun falls and rises 2.5 times faster (Dossun.cpp:122-124, :128-138, :162-164, :192-193). With sQuickRatio its rise lasts mMovementDist / 25 steps.
- An Onimasu moves to its next point in 15 steps and waits 45, with gravity 6.4 instead of 1.6 (Onimasu.cpp:24-29, :246-256). It has a FactoryGalaxy-only turn set-up (:99-113).
- A rising press part pushes Mario up through his moving-floor reaction each cycle. If each push leaves him airborne with jumping set, the lock never clears either.
- This fits less well: a rising Dossun carries Mario through its whole travel, and its upper wait is a few dozen steps, so his y would hold still between pushes rather than bounce in a steady 140-unit band.

What to log to decide, from the spin-driver release for 300 steps:
1. Mario's mFloorCode at checkAndTryForceJump, and which case fired.
2. The ground polygon's sensor host name and class (mGroundPolygon->mSensor->mHost), and that part's raw attribute row (KCollisionServer::getAttributes for the prism index).
3. jumping, onGround and inputLocked at three points: entry to checkKeyLock, after the landing code, and after checkAndTryForceJump.
4. Any MarioAccess::forceJump call, with the caller's class. A breakpoint or log in tryForceJumpDelay is enough.
5. The same log in scenario 2, for comparison.

How to read the result:
- A jump code with a map host means cause 1. The fix is the starter's landing data or making checkKeyLock ignore forced code jumps (an owner decision: gameplay).
- A Dossun or Onimasu host means cause 2.
- A forceJump caller names the actor.

Confidence: the lock mechanism is verified in code. Which floor or object is under Mario is suspected, and needs the log above.

### R9.3 Render/Material.cpp (now read completely)

**P-151** (console-era remnant, performance on phones): the material path keeps the console's per-draw register model: texture matrices, the two ambient registers, fog, alpha compare, material colours and the combiner colour registers are separate per-draw values that are filled into constant rows at draw time, and the ambient pair is a global that every draw may overwrite.
- Evidence: CoordMatrix::calc (Material.cpp:36-106) builds each texture SRT and effect matrix on the CPU, the GX texture-matrix register in another form. MaterialAmbient (:108-139) is a global sAmbient[2] with a generation counter; MaterialDraw::send (:481-488) overwrites it for any material with ambient, and LightFunction::loadActorLightInfo overwrites it per actor draw. Material::calc (:262-276) copies every coordinate matrix before and after and compares with memcmp. fillRow (:296-424) has one case per console register (COORD with a per-row view multiply for eye-space reads :313-324, WARP, SCALE from ResTIMG sizes :350-371, ALPHA_REF as alpha_ref/255, FOG_COLOR, FOG, AMBIENT_COLOR, MATERIAL_COLOR, CONSTANT, REGISTER as NebulaColor16/255). MaterialDraw keeps per-draw edit bits for each register (setRegister :569-573, getRegister fills on demand :575-582, setMaterialColor, setAmbient, setFog :594-606 with fog colour /255 and a forced form re-resolve, mForm = -1). drawFlat (:1033-1052) allocates a fresh one-row constant block from the frame arena for every flat draw.
- Why and fix: plain materials already register once (constantsOf :731-741, Round 8), so the cost is in the moved or edited materials and in the ambient global. Modern form: each material owns a constant buffer built at load in final float form (colours converted once, alpha reference as a float, fog as a per-view block not a per-draw edit); animation writes only the animated rows (texture SRT, colour tracks) into that buffer once per step; ambient comes from the actor's light set (the light_set index each item already carries, :1027) instead of a global register that the next draw overwrites; drawFlat's colour goes in the item, not in a new arena block. That removes the before/after memcmp, the per-draw fill switch and the order dependence of the ambient global.
- Confidence: verified in code. The cost share needs a run: the existing frame-list capture in HoneyBeeKingdom and SeaGalaxy (water and texture-animated materials), counting fillRow calls per frame.

### R9.4 Lights (LightDirector, LightDataHolder, LightZoneDataHolder, LightPointCtrl, ActorLightCtrl, LightAreaHolder::tryFindLightID, AreaObjMgr::find_in)

**P-152** (performance, small; same family as P-139): every visible actor scans every light area every step to find which one it stands in.
- Evidence: ActorLightCtrl::update (ActorLightCtrl.cpp:24-29) calls tryFindNewAreaLight for each actor whose model is not hidden; it calls LightFunction::tryFindNewAreaLightID (LightFunction.cpp:111-113) → LightAreaHolder::tryFindLightID (LightAreaHolder.cpp:19-41) → AreaObjMgr::find_in (AreaObj.cpp:119-129), a linear walk over all light areas from the last, each an isInVolume test. A result that matches the current light returns early, but only after the walk. Petari's LightAreaHolder::tryFindLightID (Petari/src/Game/AreaObj/LightAreaHolder.cpp:10-11) and ActorLightCtrl::update (:25-27) do the same walk, so this is the original design; Nebula added the leave delay (mChangeSteps, cLeaveSteps) only.
- Why and fix: the answer changes only when the actor crosses an area boundary. Modern form: one spatial index over areas shared by every area kind (the same structure P-129 asks for collision parts), or test the current area first and walk only when the actor has left it; a still actor (the binder's still check, P-138) can skip the query.
- Confidence: verified in code. Run: step-hash replay to prove no change, and a count of isInVolume calls per step in HoneyBeeKingdom (the heaviest benchmark scene).

**P-153** (wrong behaviour when the option changes; maintainability): which light record a "shadow" light set returns depends on the ray-tracing option, but the answer is fixed when an actor enters an area, so changing the option in the menu leaves every actor on its old record until it changes area; and the release is meant to ship no ray-tracing option.
- Evidence: LightDataHolder::findAreaLight (LightDataHolder.cpp:149-159) returns the lit partner mLit[index] when NebulaHost::option(NEBULA_OPTION_RAY_TRACING) has NEBULA_RAY_TRACING_SHADOW_SETS. The result is cached in ActorLightCtrl::mAreaLightInf (ActorLightCtrl.cpp:20, :51, :135) and in LightDirector::mDefaultAreaLight (LightDirector.cpp:31, once at load). The name-matching partner table (:11-106) is built at every scene load.
- Why and fix: with ray tracing shelved, the branch is dead weight in a release build; if it stays, resolve the record at load through one indirection the light set reads each draw (or reset every ActorLightCtrl on an option change, as ActorLightCtrl::reset already does for a zone change). Note also that the game keeps the console's one blended point light for the player (LightPointCtrl.cpp:72-81, loaded only in loadLightPlayer, LightDirector.cpp:34-37); every request is also handed to the frame as a point light (:60-70), so a renderer that lights from every source already has them.
- Confidence: verified in code. Run: toggle the option during a scene in a galaxy with shadow sets (the Peach's castle shadow set of the prologue, or the dungeon cave sets) and compare an actor's light before and after.

### R9.5 Render/Clip.cpp, Render/TypedDraw.cpp (read completely)

Clip.cpp is in modern form for key lookup (binary search over keys, :63-91, Hermite in float). Its colour outputs keep the console's register forms: colour tracks are rounded to bytes (colorByte :124-133) and register tracks clamped to the GX 11-bit signed range -0x400..0x3FF (getRegister :169-184), then Material::fillRow divides them back to floats (P-151). Part of P-151: a float track can go straight to the material's constant buffer.

**P-154** (console-era remnant, performance on phones): the "site" draws (effects, shadows, 2D, gauges, glows) still work as the console's immediate mode: each draw copies its material constants into the frame arena, asks the host for a texture table, writes its vertices into the ring and builds triangle indices for quads, strips and fans on the CPU, every frame, even when nothing about the shape changed.
- Evidence: TypedDraw::setMaterial (TypedDraw.cpp:193-208) memcpy of all rows to Frame::arena and Frame::addConstants per draw, and pHost->texture_table per draw when the material has textures. TypedDraw::begin (:410-455) takes ring room and runs writeIndices (:369-400) for quads, strips and fans. widen (:457-513) rebuilds every line and point as four duplicated corners plus indices on the CPU. block() (:291-319) copies the world matrix and a flag word into the arena for each draw. The constructors with a camera compare the matrix with memcmp to find its role (:133, :140). A whole-tree count of the typed vertex writes (`.vertices<T>(...)`) gives 109 sites in 47 files: 56 triangle strips, 35 quads, 8 fans, 4 triangle lists, 2 line kinds and 3 with a primitive chosen at run time. SiteMeshData (:522-582), the registered-once form, has one user.
- Why and fix: GXBegin/GXPosition is how the console fed these shapes; on a modern GPU the fixed shapes (quads, rings, discs, beams) are static meshes registered once and drawn with a matrix and a few per-draw values, and moving shapes are instanced or built in the vertex shader. A SiteMaterial whose values did not change should be registered once (the plain-material path already does this, Material.cpp:731-741) and its texture table kept. Widening lines and points belongs in the vertex shader from one instance per segment. Order: move the 35 quad sites and the fans to registered meshes first (their index pattern is fixed), then cache SiteMaterial constants and tables by content.
- Confidence: verified in code. The frame share needs a run: frame-list capture in HoneyBeeKingdom and the Comet Observatory, counting ring bytes and texture_table calls per frame.

### R9.6 Render/Transform.cpp, ModelData.cpp, RenderTexture.cpp, GameTexture.cpp (read completely)

Transform.cpp: plain float math. projConcat (:196-204) reads its second matrix as four rows; every caller passes CoordMatrix::mEffectMtx, a Mtx44 (Material.hpp:31, Material.cpp:69-100), so it is in bounds. billboardMtx uses sqrt and billboardYMtx fastSqrt (:11, :38): two precisions for the same job; small. ModelData.cpp: the imported model is registered once with the host (load :173-182), so models are already in a modern form. findJoint and findMaterial match the 32-bit name hash alone with no name check (:29-44, :188-194); a hash clash between two joints of one model would bind the wrong joint silently. The importer could reject a clash at import; not seen in this read.

**P-155** (console-era remnant, small; performance on phones): a texture is still known by the address of its bytes and described by the console's texture header, so every textured draw hashes an address to find the host texture, and the console's filter codes are decoded at every draw.
- Evidence: ModelData loadFile (ModelData.cpp:102-124) fills ResTIMG, the J3D texture header, with the GX min-filter code (0 to 5) and wrap codes. SiteTextures::toFrame (TypedDraw.cpp:73-87) turns the min-filter code into linear and mip choices and calls RenderTexture::idOf on every setTexture. idOf (RenderTexture.cpp:175-203) finds the image's address in an open-addressing table (:115-147) every time. GameTexture (GameTexture.cpp:19-35) builds a ResTIMG header in front of its own pixels so the same path can read it. Effect targets are sized from the authored 640 by NEBULA_AUTHORED_LINES picture (GameTexture.cpp:66-73, NebulaFrameClock.hpp:13-17); the header says that size, and whether the host scales the target to the output resolution was not read here.
- Why and fix: on the console a texture was a pointer handed to GXInitTexObj. Modern form: resolve each texture to a host id and a sampler state once at load (store them next to the ResTIMG, or replace ResTIMG with a small record of id, size and sampler) and pass the id; keep the address table only for the few images written after load (mod pictures, LumaDraw, rewritten :235-244).
- Confidence: verified in code for the lookups. The target size is suspected: check where the host reads a target's size (runtime render core, texture_target) and confirm the blur and bloom targets follow the output size at 4K.

### R9.7 The rest of Area 4 read this round (small files)

- Lights: covered in R9.4. LightDataHolder::getDefaultStepInterpolate returns 30 steps (:165-167); the game step is a fixed 60 Hz step, so this is not a frame-rate dependency.
- CollisionDirector.cpp (all): one movement call per category keeper; nothing to add to P-129.
- CollisionPartsFilter.cpp and TriangleFilter.cpp (all): TriangleFilterDangerCode::isInvalidTriangle (TriangleFilter.cpp:10-14) makes seven separate floor-code calls, each a scene-object lookup plus CollisionCode::getFloorCode on the attribute row (MapUtil.cpp:578-607); one code read and a bit-mask test would do. Add to the P-134 cleanups.
- AllLiveActorGroup.cpp, HitSensorInfo.cpp (all): nothing new. Groups grow past their first size (NameObjGroup::registerObj, NameObjGroup.cpp:15-30), so the 2,560 actor figure is a starting size, not a limit.
- Gravity shapes read: PlanetGravity, PointGravity, WireGravity, GravityFollower, SegmentGravity, DiskGravity (all). Each shape computes its exact geometry for every query with no cheap bound test first; WireGravity walks every segment of its wire per query (WireGravity.cpp:16-41) and addPoint has no bounds check (:12-14). A gravity whose follow target is clipped switches off (GravityFollower.cpp:13-25). Add to P-139: a bounding sphere per gravity, tested before the shape, and a segment grid for wires.
- ShadowVolumeDrawer.cpp, ShadowSurfaceDrawer.cpp (all): each shadow volume builds a SiteMaterial and a TypedDraw per frame and draws its shape twice (front faces adding to stencil, back faces taking) (ShadowVolumeDrawer.cpp:91-135), so P-143 (two passes) and P-154 (per-draw constants and table) meet here. It also hands the renderer a NebulaFrameShadowDrop (:98-116) for a traced shadow, so a renderer that traces shadows could skip the stencil passes.

### R9.8 Area 8, engine side (runtime/src/render/core: registries.cpp read 352-534 and 818-863, compose.cpp 118-247, draw_walker.cpp 200-309 and 636-715)

The engine already registers models, meshes, plain materials and texture tables once, and caches texture tables per thread by content (registries.cpp:818-863), which softens the per-draw texture_table cost in P-154 (a memo hit is a hash and a memcmp). Two console forms remain in the engine itself.

**P-156** (performance on high frame rates and phones; console-era remnant): every presented picture recomputes, on the CPU, the eye-space position and normal matrix of every draw matrix of every model, including skinned envelopes, and re-blends every non-registered material row; this is the console's per-shape position/normal matrix memory, rebuilt per picture.
- Evidence: RenderCore's walk (draw_walker.cpp:202-298) runs, for each picture, camera_at for every camera, joints_at per moving block, and core::compose_rows per block, plus compose_world_rows where a program reads world rows; compose (compose.cpp:170-228) concatenates view and joint per draw matrix, sums envelopes (weighted_sum :120-160), and computes an inverse transpose for any scaled matrix (:204-209), billboards (:210-222). The rows go into the matrix ring each picture (draw_walker.cpp:234-236). walk_material_rows (:664-709) blends every unregistered material's rows between the two game steps per picture.
- Why and fix: with the game step fixed at 60 Hz and pictures unlimited, this CPU work scales with the picture rate, not the game. Modern form: upload the joint matrices of the two game steps once per step (they already exist in the frame lists), and do the blend at t, the skin sum, the view multiply and the normal matrix in the vertex shader (or one compute pass per picture over all blocks). Per picture the CPU then writes only the camera and t. Material rows: blend in the shader from the two steps' constant blocks, or keep the blend only for the few animated rows (P-151).
- Confidence: verified in code. Run: host timing of the walk alone (the time from walk start to the end of the row fill) at unlimited frame rate in HoneyBeeKingdom (many skinned bees), on the PC and on the tablet; compare with the 0.8 to 1.4 ms game step.

**P-157** (console-era remnant, maintainability): the imported material format still stores the console's register layout (combiner colour registers as signed 16-bit, constant colours, material colours, alpha-compare codes and reference bytes, texture-coordinate generators by GX type and source code, rows named by register code), and the engine works out physical meaning by searching those codes at load.
- Evidence: read_model (registries.cpp:403-512) finds a glow colour by matching a register code among the first plain form's pixel rows and divides by 255 "as Material::fillRow fills them" (:425-453); finds map 0's and map 1's coordinate rows by walking stages and GX texgen type 1 with sources 4 to 19 (:455-508); takes the alpha cutoff from alpha_comp0 codes 4 and 6 and alpha_ref0/255 (:509-512). The same codes are filled per draw on the game side (Material::fillRow, P-151).
- Why and fix: the importer is the place to turn console register data into a material record (base colour source and texture, uv transform row, alpha cutoff as a float, emission colour, blend mode), written once into the cooked model; the game then animates fields of that record and the engine reads named fields. That removes the reverse search here and the register switch in fillRow, and gives the shading code one material model (the owner's "one PBR light model" rule).
- Confidence: verified in code.

### R9.9 Revised top ten performance changes (game and engine)

What changed from R8.3: the benchmark puts the game step at 0.8 to 1.4 ms per 60 Hz step on the 9950X3D, so on a desktop the step is not the limit; the per-picture engine work (P-156) scales with the picture rate and now leads. P-149 and P-154 (particles and site draws) enter as one entry; P-151 joins P-142. P-013 and the collision small cleanups drop out (small). "Show" names the scene or galaxy where the change should be largest; IceVolcano (lowest step cost, 0.8 ms) is the control in every case: a change that moves IceVolcano as much as the target scene is measuring something else. Proofs are those of R8.3.

1. Matrix compose, skinning and material blend on the GPU, not per picture on the CPU (P-156). Where: runtime/src/render/core/draw_walker.cpp:202-298, compose.cpp. Show: HoneyBeeKingdom at unlimited frame rate, on the tablet and the PC. Proof: frame-list hash unchanged, screenshot compare at fixed t, walk time per picture.
2. Spatial index over collision parts (P-129). Show: galaxies with many moving map parts (Toy Time, Bowser's Star Reactor); benchmark: HoneyBeeKingdom.
3. Sensor pair checks by space (P-039). Show: HoneyBeeKingdom (bees, coins, star bits).
4. Gravity search with a per-gravity bound, and the same index for light areas (P-139, R9.7 shape bounds, P-152). Show: Gusty Garden (many small planets and gravity shapes); benchmark: HoneyBeeKingdom.
5. Keep a shadow's projection when nothing under it changed (P-140). Show: HoneyBeeKingdom.
6. Worker threads for the clipping judge, shadow updates and callback-free animation (P-040, R8.2). Show: the tablet in HoneyBeeKingdom; the PC shows little (step already 1.4 ms at most).
7. Particles and site draws as instances and registered meshes (P-149, P-154). Where: Particle/ParticleDraw.cpp:71-200, ParticleResource.cpp:523-537, Render/TypedDraw.cpp:193-513 and the 109 vertex-write sites. Show: HoneyBeeKingdom (highest step cost of the eight scenes; its emitter count is not measured), tablet. Proof: frame-list hash of resolved positions within a float tolerance, screenshot compare.
8. No heap allocation in collision queries after the hit room grows (P-131). Show: whichever galaxy logs growHitInfo first (log it).
9. Material and light constants registered per material and per frame, not filled per draw (P-151, P-142). Where: Render/Material.cpp:296-424, 481-606, Map/LightFunction.cpp:150-220, Render/FrameList.cpp:729-782. Show: HoneyBeeKingdom and the water galaxies (texture-animated materials), tablet. Proof: frame-list hash of resolved values per item.
10. Constant-time draw-buffer insert, remove and light re-sort (P-038). Show: HoneyBeeKingdom, if its actors cross light areas (each crossing re-sorts, ActorLightCtrl.cpp:107-109, 122-124); count resetLightSort calls per step first.

Left out (small): P-013, P-127, P-128, P-130, P-132 to P-138, P-141, P-143, P-144, P-145, P-150, P-153, P-155, P-157.

## Round 10

Benchmark facts from the coordinator are taken as given: at 2560x1440, uncapped, with interpolation, D3D12 made 2,676 to 4,258 pictures a second, with the render thread named as the limit at 0.23 to 0.37 ms a picture. D3D11 made 1,415 to 2,569 (render thread 0.31 to 0.62 ms). Vulkan made only 719 to 862 with GPU time 0.12 to 0.17 ms and the render thread at 0.34 to 0.58 ms; its limit was reported as "waiting". At 854x480 Vulkan made 1,361 to 2,329. The game step is 0.8 to 1.4 ms on all three. Order of work this round: the small System/Nebula*.cpp files first, then the two questions the final report must answer (the Vulkan present path, FactoryGalaxy against Petari), then the rest as room allows. NebulaMods.cpp is mod code (out of scope by the original brief) and was not read.

### R10.1 Small System/Nebula*.cpp files (read completely: NebulaSystemSettings, NebulaCalendar, NebulaLoadTrace, NebulaDataTables, NebulaSaveImport, NebulaModCalls 60-227, NebulaCutsceneSkip)

- NebulaSystemSettings.cpp, NebulaCalendar.cpp: correct. Calendar is a civil-from-days conversion in 400-year eras (:15-39), right for negative times too.
- NebulaLoadTrace.cpp: diagnostics only. Its function-local statics (:25, :57) are constant-initialised, so they do not hit the game module's _Init_thread link problem.
- NebulaDataTables.cpp: tables are read once into the permanent arena and missing files panic with a plain message (:13-24). getGalaxyInfoTable caches its first answer (:103-112), so a galaxy added after the first read is never seen. That only matters for mods.
- NebulaSaveImport.cpp: the hand-off is a three-state atomic (:18-27, :113-128). The game thread takes a request only between the save sequence's own reads (:59-66). The caller's bytes are read later on the game thread, so the host must keep them alive until the callback; that is a contract, not stated in this file. A request made while no save handler exists waits, and every later request returns BUSY until it is served.
- NebulaModCalls.cpp 60-227: mod calls, out of scope. Nothing unsafe was seen in nebula_mod_water_point's index arithmetic (:149-207).

**P-158** (wrong behaviour on slow devices): a cutscene skip stops after 15 seconds of wall time even if the cutscene has not ended. On a phone, where fewer extra steps fit in each frame, a long cutscene is therefore only partly skipped, and the rest plays with the picture faded back in.
- Evidence: NebulaCutsceneSkip.cpp:45-48. cStepBudgetMicros is 9,000 µs of wall time per frame, cStepsPerFrameMax is 40, and cSkipMaxMicros is 15,000,000 µs. The State_Skipping branch ends with "time" when wall time passes 15 s (:351-352). The extra steps run while tryExtraStep allows (GameSystem.cpp:199-202, NebulaCutsceneSkip.cpp:382-397).
- On the PC, about 1.4 ms per step gives about 6 extra steps per 9 ms, so 15 s holds about 5,400 steps (90 s of cutscene). A device four times slower holds about 22 s of cutscene. The Observatory's story book (RosettaReading), the ending (EpDemo) and the longer boss openings are in the skippable list (:51-67). Any of them longer than what fits would end the skip mid-scene, in a different place on each device.
- Why and fix: a skip is a game-step concept. Measure the cap in steps (for example, the cutscene's own length from its time sheet plus a margin), not in wall time. Keep the wall-time limit only as a frame budget, which is already there.
- Confidence: verified in code. Run: skip RosettaReading on the tablet and log sSteps and the end reason.

### R10.2 The Vulkan present path, and what a hidden window changes (renderer_vk.cpp 944-1161, 1960-2019, 2230-2340, 2975-3034; renderer_d3d12.cpp 1868-1870, 3157-3209, 4166-4274; renderer_d3d11.cpp 286-305, 507-513, 658-664; renderer_mtl.mm 293, 1171 by grep; host_window_win32.cpp 489-588, 660-675)

**P-159** (wrong measurement; platform gap): on a hidden window, Direct3D 12 and Direct3D 11 draw every picture but present none, while Vulkan acquires and presents every picture. The benchmark therefore compares two renderers that skip presentation with one that pays for it, and the "waiting" Vulkan limit is most likely time spent in the presentation engine, not in the renderer.
- Evidence, per renderer:
  - **Direct3D 12.** present_to_swap_chain returns before Present while occluded_ is set (renderer_d3d12.cpp:4167-4174). occluded_ is set when Present returns DXGI_STATUS_OCCLUDED (:4272) and cleared by a DXGI_PRESENT_TEST probe every 100 ms (:1868-1870). Uncapped it presents with ALLOW_TEARING and skips the latency wait (:4191-4192, :4241). The swap chain uses SetMaximumFrameLatency(1) (:3209).
  - **Direct3D 11.** The same: probe_occlusion (renderer_d3d11.cpp:507-513), and presenting needs !occluded_ (:658). Tearing is allowed when uncapped (:664).
  - **Vulkan.** There is no occlusion state. Only a minimised window, with a zero extent, skips (renderer_vk.cpp:950-955, 1403-1411, 2880-2882). A window that was never shown, or is cloaked, keeps its full extent. So every picture runs vkAcquireNextImageKHR with a 1 s timeout (:2992-3014) and vkQueuePresentKHR (:2233-2330).
  - **Vulkan, uncapped.** The present mode is MAILBOX, else IMMEDIATE, else FIFO (:1000-1004), with 3 images (:1010-1014). There is no present-id wait outside FIFO (:2246-2254). The frame fence wait covers kFramesInFlight frames back (:1972-1979).
  - **The hidden test window.** It is created without being shown, as a WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE window, and can be cloaked (host_window_win32.cpp:489-500, 583-588, 669-672).
  - **Metal.** It asks nextDrawable with allowsNextDrawableTimeout (renderer_mtl.mm:293, 1171), and has no occlusion skip in this read. macOS throttles drawables of occluded windows, so a hidden-window Mac run would show waits there too. This is suspected only.
- Why this fits the numbers: the Vulkan GPU time is 0.12 to 0.17 ms and the render thread 0.34 to 0.58 ms, yet the rate is 719 to 862. The missing time is outside both. The rate nearly doubles at 854x480 although the game's own GPU work is small at both sizes. That points to a per-present cost that grows with surface size. On Windows that cost is in the driver's present path: the window system's copy or composition of a 2560x1440 image for a window it does not show. Our command buffer has no work that scales like that apart from the final copy, and that copy is inside the measured GPU time. The Acquire and Present render spans (galaxy::bench::RenderPart::Acquire, Present) already time both calls, so the run's own breakdown can confirm it.
- Fix:
  - Give Vulkan the same rule as Direct3D: on Windows, treat a window that is not visible, is iconic or is DWM-cloaked (IsWindowVisible, IsIconic, DwmGetWindowAttribute DWMWA_CLOAKED) as occluded. Skip acquire and present, and probe again every 100 ms, as the D3D renderers do.
  - On Linux and Android, use the surface's out-of-date and suboptimal results plus the window's visibility event.
  - In the benchmark, record per run whether pictures were presented, and do not rank renderers against each other when one of them presented nothing.
  - For a fair renderer comparison, run with a visible window (any size) or with presentation skipped on all three.
- Confidence: the per-renderer behaviour is verified in code. The cause of the Vulkan wait is suspected. Run: the same benchmark with the window shown, and the hidden run's Acquire and Present span times for Vulkan at 2560x1440 and 854x480. At 719 to 862 pictures a second each picture takes 1.16 to 1.39 ms against 0.34 to 0.58 ms of render-thread work. If Acquire or Present holds most of the remaining 0.6 to 1.0 ms, this is the cause.

### R10.3 FactoryGalaxy scenario 4 against Petari

Compared: Mario::checkKeyLock (Nebula Mario.cpp:338-357, Petari Mario.cpp:375-392), Mario::checkAndTryForceJump (Nebula MarioJump.cpp:2158-2235, Petari MarioJump.cpp:2387-2480), tryForceJump and tryForceJumpDelay (Nebula MarioJump.cpp:686-702, Petari :742-760), CollisionCode::getCode and getFloorCode (Nebula CollisionCode.cpp:100-125, Petari :87-199), the floor-code tables (both CollisionCode.hpp:16-44), Dossun's Quick handling (Petari Dossun.cpp:101-165), and the table cook (crates/nebula-import/src/cook/table.rs:6-15, 198-238). Petari's configure.py marks Mario.cpp, MarioJump.cpp, MarioActor.cpp, KCollision.cpp, Dossun.cpp and Onimasu.cpp as NonMatching (lines 1130, 1179, 1344, 1850-1851, 1895). So Petari is a reading of the original, not proof of it. Petari has no Mario::update and no Onimasu.cpp.

Every difference found in these functions:
1. **checkAndTryForceJump's entry test.** Petari reads `!_1 || jumping || isAnimationRun(_728)`. Nebula reads `(onGround && !jumping) || isAnimationRun(mHipDropLandAnim)`. Petari's tryForceJump clears _1 where Nebula clears onGround (Petari MarioJump.cpp:760ff, Nebula :701-702), so _1 is onGround. Petari's form would force-jump only while in the air, which no jump floor in play does. Nebula's form is its logical inverse for the first two terms, and is the reading that makes jump floors work. Both cannot be right. This is the one place where the port's logic and Petari's text disagree on exactly this behaviour. It needs the original instructions to settle: Petari's objdiff for checkAndTryForceJump__5MarioFv (config/RMGK01/symbols.txt:25287, 0x380 bytes). Using assembly as a behaviour reference is allowed by the owner's rule.
2. **checkKeyLock's last line.** Petari sets a draw state on the sub-pad C button. Nebula sets stickLocked while the player holds the camera (Mario.cpp:355-357). This is not related to the lock. The clear-only-when-not-jumping rule (`if (!jumping) lock = 0`) and the other three rules are the same in both.
3. **getCode.** Petari reads a number only when the column's value type is 0 (a long) and goes the string way otherwise. Nebula reads a number for every kind that is not text (CollisionCode.cpp:105). The cooker applies each BCSV column's mask and shift once at import, `(raw & mask) >> shift` (table.rs:10, 224-238), which is the JMap rule. For the attribute rows' masked long columns the result is the same.
4. **Floor-code numbering.** JumpLow, JumpMiddle, JumpHigh, JumpNormal and JumpParasol have the same enum positions in both headers.
5. **The Quick comet.** Dossun's ratio is 2.5 in both (Petari Dossun.cpp:101-103, 135-137). Nebula's Onimasu Quick values (Onimasu.cpp:24-29) have no Petari text to compare.
6. **Not comparable from Petari:** the order inside Mario::update (key-lock check at the top, force jump later at Mario.cpp:1617), and the collision cell lookup. KCollision.cpp is NonMatching in Petari, and the port reads cooked prisms (P-134).

What the console's logic does under a start point: with either reading of the entry test, the lock rule is the same. It clears only on a step that begins with jumping false. A floor that force-jumps on landing sets jumping again in the same step, so under the original's logic Mario would also bounce and stay locked if he landed on such a floor. The shipped game is playable in this scenario, so on the console Mario does not land on a force-jump floor there. That leaves three port-side candidates, in order:
- (a) The entry test, if the original matches Petari's reading. Then jump floors would never fire on landing in the original, and Nebula's inverted test makes a floor fire that never fires on the console. This is the strongest candidate if the floor under him is a jump code. It would also show as wrong bounces on ordinary jump-code floors elsewhere, so check one known jump-code floor in normal play.
- (b) The collision cell lookup returning a different triangle (a neighbour's jump-code face) under that point.
- (c) Where the spin-driver flight ends (the rush end, launchKind 2 "after a scenario starter") putting him over a different floor than on the console.

The R9.2 log decides between them: floor code, triangle, host and attribute row at the landing step. Add the assembly check of item 1 before any change.

### R10.4 P-157 made exact: what of the console's register layout survives in the cooked model (native/include/nebula_model.h:273-333)

The cooked material record NebulaModelMaterial keeps these console forms:
- material_color[2] and ambient_color[2] as RGBA bytes, the GX channel registers;
- registers[4][4] as signed 16-bit, the TEV colour registers C0 to C3 in the -1024..1023 range;
- constants[4][4] as bytes, the TEV constant colours;
- channels[4], the GX lighting-channel controls;
- alpha_comp0, alpha_op, alpha_comp1, alpha_ref0, alpha_ref1, the GX alpha compare;
- blend_type, blend_src, blend_dst and depth_test, depth_func, depth_write, as GX blend and z modes;
- fog, the GX fog block;
- coords (coords_at), the texgens by GX type and source code;
- matrices_at, the texture-matrix SRT records;
- warps_at, the indirect stages;
- NebulaModelStage, one per combiner stage "as the game's file orders it": TEV inputs, op, bias, scale, clamp and out for colour and alpha, swap tables, konst selectors and the indirect warp. This is kept for the ray hit record.

The pixel programs themselves are compiled per form at cook time (forms_at; programs.rs), so the combiner is not interpreted at run time. Only its inputs stay register-shaped: rows named by register code (NEBULA_ROW_REGISTER, CONSTANT, MATERIAL_COLOR, COORD, ALPHA_REF, FOG and AMBIENT_COLOR, filled by Material::fillRow, P-151).

What the importer could resolve once:
1. Every non-animated row as final floats in the form's constant block. This covers colours /255, registers /255, alpha_ref /255 and the fog parameters. The game would then fill only rows that a clip animates (Clip register, colour and SRT tracks) or that code edits.
2. The texgen and matrix records, turned into "uv set n through matrix row k" per map. registries.cpp:455-508 searches for this today.
3. The alpha test as one float cutoff plus a mode. registries.cpp:509-512 derives it today.
4. The glow or emission source as a named colour and its row. registries.cpp:425-453 searches for this today.
5. Blend and depth as the engine's state word, which `state` already is. The separate GX fields are then only for tools.

What must stay per draw: animated rows, the ambient colour, which comes from the actor's light set (P-151), and fog only if a scene changes it.

Confidence: verified in code for the record. The program side (programs.rs) and texture.rs were not read this round.

## Round 11

The coordinator has verified the force-jump entry-test difference (Nebula MarioJump.cpp:2162 against Petari MarioJump.cpp:2391) and accepted P-159. This round keeps to the brief's order. Findings are written after each part of a file as it is read.

### R11.1 Screen/NebulaSettingsMenu.cpp (5,755 lines)

Notes from 1-1449:
- **What the file is.** It is the game-side drawing of a view the host owns. NebulaSettingsView holds the pages, the rows, row flags, slider steps and positions, help, status and long texts. The host also asks questions by status (cQuestions, :180-312). ShownView::copyFrom (:1112-1143) copies the host's view each time into the game's own storage.
- **Where the options live.** Which options exist, their values and whether one does anything are decided by the host (runtime/src/ui native_settings.cpp and the settings models, read in Rounds 5 and 6), not here. This file can only show a row the host sends. The complete P-087 list for the in-game menu therefore has to come from the host's row builders, not from this file; see the end of R11.1.
- **The scene while the menu is open.** The class comment (:1224-1227) says GameSystem draws the menu over the scene, "which waits while it is shown".
- **Frame-counted timings.** cRepeatDelay is 18 and cRepeatEvery 5 (:79-80); cDropFrames 16, cQuestionInFrames 10, cTurnFrames 14, cPickUpFrames 24 and cSwipeHoverFrames 12 (:58, 109-124). The glide uses real time (cGlideDecay per second). Whether the frame counts advance once per 60 Hz game frame or once per picture is settled where step() is called (below).

**P-160** (wrong behaviour: text shows in English): host texts are translated by building an id from the English words, so any change to a host string drops that string to English in all 13 other languages without any error.
- Evidence: lookUp (:669-707) turns the host's English text into an id "S." plus the text, with spaces as _ and digits as #. When NebulaText has no such id, translate (:735-780) splits the text at " (", ". ", ": " or ", " and translates the pieces recursively. Pieces with no entry are shown as the host wrote them, in English (fromUtf8, :779). fromUtf8 (:541-558) decodes only one- to three-byte UTF-8: a four-byte sequence (an emoji in a mod's name) becomes two wrong units.
- Why and fix: a translation key should travel with the text. The host should send a text id with its arguments, and the game should look the id up, as every other game text does. Until then, a build check could send every row label and value the host can produce through lookUp and fail on a miss.
- Confidence: verified in code. Run: switch the language to Japanese, open every page and look for Latin text. Or log every lookUp miss with --diagnostics.

NebulaSettingsMenu.cpp 1449-5755, read completely (with the earlier part the whole file is now read).
- **Where it runs and what the scene does.** GameSystem::update calls NebulaBenchmarkPanel::update (GameSystem.cpp:268) and NebulaSettingsMenu::update (:280) once per system step. NebulaSettingsMenu::update copies the host's view, calls step, then sets sFreezing = view open, or shown and not the HOME Menu (:5343). GameSystem::updateSceneController holds the scene (no SequenceDirector update, no updateScene) while isFreezingScene, that is sFreezing and not scene_live (:5383-5385; GameSystem.cpp:311-317). The audio is held through AudSystem::holdForSettingsMenu, and the music is paused once the menu is fully open (:5344-5361). The cutscene skip's update is not called while the menu freezes the scene (GameSystem.cpp:283-285). With scene_live (the HDR calibration's reference stage) the scene runs under the menu.
- **Frame counts.** Every count in the file (cDropFrames, cQuestionInFrames, cTurnFrames, cRepeatDelay/cRepeatEvery, cPickUpFrames, cSwipeHoverFrames, mSettleFrames, the 0.35 and 0.45 easings, the Joy-Con wave "mFrame % 72 ... over 1.2 s" at :5009-5010, the benchmark panel's cRunAppearFrames and cRunFollow) advances once per system step, so they follow the game's fixed step and not the display rate. The list glide and the mod lists' edge scroll use NebulaHost::clockNs (:3609-3636, :4132-4156). This is consistent; the only cost is P-163.
- **Every step while closed.** update copies the host view (NebulaHost::settingsView), step returns at :5192 (mOpen 0), setSharp(SharpSettings, false) runs, and NebulaBenchmarkPanel::update calls the host's benchmark_state. No layout, text or pane work runs. Nothing to fix.
- **Options that do nothing (P-087).** The game side adds no row of its own: every row, value and button comes from the host view (fillTexts, placeRows, placeMods). The in-game menu shows the same pages as the launcher, built by settings_display_page, settings_graphics_page, settings_general_page, settings_system_page, settings_advanced_page, settings_benchmark_page and native_settings.cpp. All of them were read in Round 5, apart from the mods pages (native_settings.cpp 2759-4815, out of scope). P-087 is therefore complete for the in-game menu as written: Wii Remote input modes off Windows and Android, SURROUND off WASAPI, MSAA 8X on devices that clamp, and HDR on D3D11 and Metal (P-032). The questions (DLSS/XeSS install, the Joy-Con pairing, HDR calibration) are asked only from rows the host offers, so they add nothing new.
- **Strand paths checked.**
  - Keys and controllers answer through the host. The pointer has the footer's Apply and Back and every answer button.
  - A touch hides the hints' words but keeps Back (:2391).
  - A binding that waits for an input takes the mouse buttons (:3677-3687). The host's capture ends it.
  - The boot scene closes a menu opened during the logos unless the host is asking a question (:5334-5338).
  - The progress screens without answers (sending) end on the host's state.
  - The one path that strands the player is P-161.
- **Text buffers.** setText grows a text box's buffer to the text's length (LayoutCoreUtil.cpp:30-32), so the 96/128/256 capacities named at build do not cut a translation.
- **Mod and graph pictures.** takeModPicture copies again only when the host's pointer or size changes (:4921). The host clears the picture between pages (native_settings.cpp:5707-5722, the about.reading future) and the benchmark graph is one picture per results screen (benchmark_menu.cpp:399-400), so a reused address with no null between was not found. Not a finding.

**P-161** maintainability (a latent dead end; it would strand the player): if the menu could not make a row or page button while it is open, the game would stay frozen and silent with no menu drawn. Checked afterwards: makeGroup, makeWindow and makeText (NebulaUi.cpp:423-453) fail only when the layout lacks the kit's source pane, which init already catches, and the arena panics rather than returning null (NebulaArena.cpp:44-46, 212-220). So the path is not reachable today. It becomes reachable with any future failure inside addRow or addTab.
- Evidence:
  - ensureTabs (:1809-1816) and ensureRows (:1836-1841) set mBuilt = false when addTab or addRow fails. A page with more rows than any page before it (the MODS page, a long controls page) asks for new panes mid-session.
  - step then returns (:5162-5164, :5274-5276), and drawMenu draws nothing (:5299).
  - From the next step, NebulaSettingsMenu::update returns at :5326-5328 before sFreezing is computed. sFreezing keeps the value of the last good step (true while the menu was open), so isFreezingScene stays true and GameSystem holds the scene (GameSystem.cpp:314-316). The audio hold set by holdForSettingsMenu is never released (:5345-5354), and FileSelector.cpp:507 waits on isUp.
  - Back or Escape still close the host's view, but nothing in the game reads that any more.
- Why and fix: "unbuilt" was designed as a boot-time state ("The game runs on", :5325) and is reused for a failure in the middle of a session. Fix:
  - When the menu becomes unbuilt, set sFreezing false, release the audio hold, unpause the music, and send NEBULA_SETTINGS_CLOSE.
  - Better, make a row that cannot be made fail alone: cap the rows to the panes made, and keep the menu built.
- Confidence: the frozen-state path is verified in code, and so is the fact that it cannot be reached today. To prove the fix, run a test build in which addRow fails on its Nth call, open a page with more than N rows, and check that the game resumes.

**P-162** wrong behaviour (text fitting): long translations are squeezed past readability or run out of their boxes, with different floors in each place.
- Evidence:
  - Page buttons: the word is scaled by room / word with no lower limit (:4772), so a long word becomes unreadably narrow.
  - Row labels stop at half width (:4717). Below that they run under the arrows or the value.
  - Values stop at 0.55 (:4688, :4705, :4712), hints at 0.6 (:4822, then they run into Apply and Back), and buttons and titles at cMinSqueeze.
  - A question is wrapped, then shrunk to 0.75 at most (:2580-2585). Past that it runs out of its window, unlike the long questions, the licence and the HDR steps, which shrink to cMinSqueeze and are clamped to the screen (:2907-2914, :2967-2978).
  - arrange scales the whole menu by the chosen text size up to cZoomMax 1.8 (:2104-2161), which shrinks every room above.
- Why and fix: each site picked its own floor, and nothing checks the 14 languages against them. Fix:
  - Use one rule: squeeze to cMinSqueeze, then wrap to a second line (rows have room at cRowHeight) or cut with an ellipsis, as fitStatus does (:4185-4211).
  - Add a build-time or test-driver check that lays out every page in every language at the smallest supported window and the largest text size, and reports any box over its room.
- Confidence: verified in code for the floors. Which strings overflow needs a run: German, French, Russian and Japanese, every page, at the smallest window and MENU_TEXT_SIZE largest. Take a screenshot of each page.

**P-163** performance (input latency, small): the menu reads the mouse, touch and wheel once per system step, so on a fast display the pointer's hover, drag and slider follow at the step rate, one step behind.
- Evidence: readPointer runs from step (:5285-5287), which NebulaSettingsMenu::update calls once per GameSystem::update (GameSystem.cpp:280). readInput samples MR::getPointerPastPos and NebulaHost::menuFinger then (:3530-3580). The panes are interpolated between steps (RenewDrawIdentity comments at :2603 and :4509), so motion looks smooth but trails the hand by up to one step plus the interpolation delay.
- Why and fix: the menu is a game layout and lives on the game step. Fix: hit-test and drag on the host side per picture, or sample the pointer at draw time for the slider knob and the drag position only.
- Confidence: verified in code; the size of the delay needs a run (a 240 Hz display, drag a slider and record the screen at 240 fps).

**Correction to Round 10 (R10.3, FactoryGalaxy).** The Petari copy at C:\Users\<user>\Documents\MarioGalaxyRecomp\Petari is a snapshot from 2026-06-06 and is out of date. The current decompilation (D:\NebulaWork\build\audit-petari-upstream\Petari-master; MarioJump.cpp last changed 2026-10-19) reads, at src/Game/Player/MarioJump.cpp:2223, `if ((getMovementStates()._1 && !mMovementStates.jumping) || isAnimationRun(_728))`. That is the same logic as Nebula MarioJump.cpp:2162. Candidate (a), the inverted entry test, is **withdrawn**: the port follows the current decompilation. Two candidates stay open: (b) the collision lookup (cooked prism data, cell lookup, attribute row) and (c) where the spin-driver flight ends. From here on, every Petari comparison in this audit uses the current snapshot.

Extends **P-158**: the cutscene skip's update is not called while the settings menu freezes the scene (GameSystem.cpp:283-285). P-158's 15 s wall-time limit therefore keeps running behind the menu, and the skip can be gone when the menu closes. Suspected, needs a run: open the menu during a cutscene's skip prompt, wait 20 s, close it, and try to skip.

System files, read completely: NebulaText.cpp (631), NebulaText.hpp, NebulaAchievements.cpp (788), NebulaAchievements.hpp, NebulaHost.hpp, NebulaInput.hpp, NebulaAtomic.hpp, NebulaList.hpp, NebulaClock.hpp, NebulaRegion.hpp, NebulaModule.c, NebulaFormat.c. NebulaTextTable.inc (2,597 lines, generated) was checked by a script rather than read line by line. NebulaNotActors.inc and NebulaNeedTheirLevel.inc are the mods' spawn lists (out of scope); only their headers were read. NebulaArena.cpp and .hpp were read in Round 8.
- **NebulaText.**
  - Lookup order: an override file in the language folder, then the built-in table's column, then the sister spelling, then American English (:255-277). The override file is read once into one root-arena block sized from the file (:221-253).
  - storeValue decodes four-byte UTF-8 to surrogate pairs (:110-133). The settings menu's own fromUtf8 does not (P-160); NebulaAchievements has a third decoder that also handles them (:215-246). There are three UTF-8 decoders in the game and the weakest one is used for host text.
  - Table completeness (a script over NebulaTextTable.inc, scratchpad textnulls.py): 1,292 rows. Every language has a text for every row apart from 14 or 15 rows per column. All of those are language-neutral by design: BoardCount "{n} / {total}", BoardPercent "{n}%", BoardBack, HomeMenuYes and HomeMenuNo (which are the game's own messages through {message:...}), and the brand and region names (S.WII, S.XBOX, S.US, S.PAL...). No row falls back to English by mistake.
  - The Luma talk's line fitting (fitTalk, :555-630) binary-searches the narrowest width that keeps the greedy line count, in 12 steps, and runs once per talk. It is correct as read: memmove stays inside sTalkRoom, which is twice the text.
- **NebulaAchievements.** What is checked and when:
  - Every goal is checked at stage start and at the first step of a star's get sequence (GameScene.cpp:256, 728). Lives, Star Bits and coins are checked once a second (:530-557).
  - Play seconds count game steps through NebulaFrameClock::stepsOfSeconds(1) (:536). The stat flush uses a literal cStatFlushFrames = 60 (:135) rather than stepsOfSeconds(1); this is the only frame literal in these files. The header says the stats flush "every few seconds"; the code flushes every second.
  - The header has an orphan comment "Tests (GALAXY_TEST_EVENT_FLAGS=Achievement:<id>)" with no declaration under it (NebulaAchievements.hpp:64).
  - See P-164.
- **NebulaFormat.c** (the game's printf).
  - %e, %E, %g and %G are printed as fixed notation (:302-308). A grep of native/petari for those conversions in format strings found no use, so nothing is wrong today. A future %g would print "1.500000" and not "1.5".
  - Wide %ls prints non-ASCII as '?' (:290). This is log-only.
- **NebulaModule.c.** The module's malloc, calloc, realloc and free go to the host heap on Windows (:13-44). Every log line becomes one host call (:95-120). nebula_game_region asks the host each time (:160-162). Nothing to fix.
- **NebulaHost.hpp.** Every wrapper calls through the host table with no null check, except input_players (:21-25); benchmark_state is checked by its callers. This is consistent with the attach check of version and size (NebulaModule.c:50-53). objectId is the object's address (:38-40), so a freed object's id can come back for a new object. That matters only to host-side caches keyed by it, which were not traced.
- **NebulaList.hpp and NebulaAtomic.hpp.** Correct as read. NebulaAtomic's default constructor leaves the value uninitialised (:8).

**P-164** wrong behaviour (late unlock): the Prankster Comet, Daredevil, Cosmic, Speedy, purple coin and hidden-star achievements do not unlock with the star that completes them, but at the next stage start.
- Evidence: GameScene::stageClear calls evaluate on the get sequence's first step so that "the star just collected ... unlocks what it completes now" (GameScene.cpp:724-728). evaluate counts the pending star through hasStar and countStars (:145-174). But checkGalaxies tests comets, purple coins and hidden stars with galaxy.hasPowerStar (:330, :352, :362), which does not see the pending star. When the hub later records the star, countStars does not change (pending plus one becomes saved plus zero), so update's "a star recorded since" check (:549-552) does not evaluate again. The next evaluate is the next GameScene::start. First Comet (mAnyComet, :335, :356) is late in the same way. Galactic Wayfarer uses hasStar (:324) and is on time.
- Why and fix: two ways of asking "is this star owned" are mixed. Fix: use hasStar(pName, scenario) in checkGalaxies for every owned test.
- Confidence: verified in code. Run: on a save one comet star short of "comets", collect it. The pop-up shows at the next stage start rather than during the star's sequence.

**P-165** performance (small): every NebulaText lookup is a linear scan of the 1,292-row table, and NebulaText::get with a growing buffer expands each text twice (measure, then get, :431-435).
- Evidence: find (:255-277) and english (:421-429) loop over cNebulaTextRows with a string compare. The settings menu translates every changed row's label and value through lookUp, and translate splits misses into pieces and looks each one up again (NebulaSettingsMenu.cpp:669-780). A page change refills every row. The achievement board does the same for each card.
- Why and fix: the table is generated (tools/nebula_text.py), so it can be emitted sorted for a binary search, or as a perfect hash. get could also return the found pointer and length from one pass.
- Confidence: verified in code; the cost is not measured. Run: time fillTexts on a page change of the GRAPHICS page in Japanese with the frame-phase timer.

Item 1 is finished.

### R11.2 Area 4 leftovers

Read completely: Particle/ParticleDraw.cpp (201-996; with Round 9's 1-200 the whole file), ParticleResource.cpp (1-329 and 710-948; with Round 9's 330-709 the whole file), ParticleField.cpp (230).
- **ParticleDraw shapes (:492-996).**
  - Every shape builds a 3x4 eye-space matrix per particle on the CPU and hands it to setParticleMtx, which multiplies it back to world space (P-149). The billboards (:492-542, :664-763), the world planes (:545-562) and the direction shapes (:573-662) all do this.
  - Stripes write two or four corners per particle into a strip (:784-996).
  - Lines and points use corner() directly (:564-571, :765-782).
  - The direction and stripe shapes write the particle's mBaseAxis while drawing (:592-593, :637-638, :839-840, :924-925, :976-977). That state carries to the next draw, and StripeX updates it twice in one draw (its two loops). This is how JParticle's draw works, and it is correct only while each emitter is drawn exactly once per game step. It must be kept in mind for P-149's instance design (the axis has to move into the calc step) and for any second view of the same emitters.
  - Six shapes test `checkStatus(8)` (:574 and others) where the billboards use the named ParticleStatus_Invisible (:493). This is the same bit written two ways.
- **ParticleResource.**
  - init (:205-333) turns JParticle's function lists into one Plan of enum choices, chosen once per resource. calc_p, drawParticle and drawChild switch on it per particle (:780-873). This replaces the console's function-pointer lists and is a sound modern form.
  - setPTev (:742-770) still computes the material's "place" from JParticle's GX table byte offsets: `center_offset = (scaleCenterX + 3 * scaleCenterY) * 0xC`, `0x30` default, `base_plane_type * 0x6C`, then `/ 3`. setCTev does the same (:773-774). This is a console-era remnant. The index is the only thing used, so it should be computed as one (row = scaleCenterX + 3 * scaleCenterY, plane = base_plane_type) with the offsets gone.
  - Degrees become the game's 16-bit angle by `* 182` (:926) and `* 0xB6` (:944): the same truncated constant spelled two ways. 360 degrees maps to 65,520, not 65,536 (0.08 degrees short). This is inherited from JParticle and is a behaviour detail to keep.
- **ParticleField (:7-230).** Each field is a virtual calc per particle per step, as in JParticle. FieldRandom draws z, y, then x to keep the console's argument order (:160-171), so the port is deterministic across compilers. FieldVortex divides by the radius squared (:65-66). A vortex with radius 0 and a particle exactly on its axis makes 0 * inf, which is NaN (:78). This is inherited and was not seen in data. No new finding.

Also read completely: Particle/EmitterDynamics.cpp (205), ParticleKey.cpp, ParticleResources.cpp, ExTexShape.cpp, ChildShape.cpp, ParticleMath.cpp 78-146, Effect/ParticleDrawExecutor.cpp (106), Effect/MultiEmitter.cpp (441). For ParticleShape.cpp (374) and ExtraShape.cpp (93), only the function list was read; their bodies are JParticle's per-particle colour, scale and texture-index steps, covered by P-149's design note.
- **EmitterDynamics.**
  - The sphere's fixed-interval path guards its integer divisions by the division and angle counts (:61-66), with a comment.
  - The line's fixed-interval path divides by `mEmitCount - 1.0f` (:16). With one division (DivNumber 1, so emitCount 1) that is 0 / 0, which is NaN, and the particle is placed at NaN. This is the same as JParticle, but the port fixed the sphere's equivalent and not this one. Guard it the same way (the single particle at the line's middle).
  - The circle's integer division by mEmitCount (:31) runs only when particles are made, so mEmitCount >= 1 there.
  - Emission (:154-205) is per game step with a fractional carry (mEmitCount), so it is rate-exact at the fixed step.
- **ParticleResources.** It panics on a particle file that is not the importer's current version (:29-31), so a stale cooked file fails loudly. Resources are found by user index through a table built once (:44-86). swapTexture is a linear name search (:95-105) and runs only on a swap.
- **ParticleKey and ParticleMath.** Looped keys wrap by the last key's time (:7-11). keyValue binary-searches the keys and uses a Hermite curve (:124-146). Correct as read.
- **ParticleDrawExecutor.** Seven draw passes, each calling EmitterManager::draw for its group (0 and 1 3D; 2 indirect; 3 after indirect; 4 bloom; 5 after the image effect; 6 and 7 2D; 8 for 2D models). A 2D draw uses an orthographic camera over the layout canvas (cScreenLines high, the 16:9 screen width or cLayoutWidth wide, :39-48).
- **MultiEmitter.**
  - An effect name is split into words or numbered entries once, at creation (:371-412).
  - A missing particle word keeps its slot and is reported once per session, through a lock-free list (:26-99).
  - The clipping on and off paths walk the emitters (:319-349).
  - isEqualName(u16) compares the low 16 bits of the name hash only (:243-245). This follows the original's u16 lookup; a collision would pick the wrong effect by name, and it was not traced in data.
  - No per-step cost beyond the emitters themselves.

Item 2 stops here, at a file boundary. Not started in item 2: Effect/MultiEmitterCallBack.cpp (232), MultiEmitterAccess.cpp (119), MultiEmitterParticleCallBack.cpp (5), EffectSystemUtil.cpp (300); Gravity/ConeGravity.cpp (171), CubeGravity.cpp (362), DiskTorusGravity.cpp (109), ParallelGravity.cpp (170), GlobalGravityObj.cpp (127), GravityCreator.cpp (340); LiveActor/ViewGroupCtrl.cpp (130); ShadowSurfaceBox, ShadowSurfaceCircle, ShadowSurfaceOval, ShadowVolumeCylinder, ShadowVolumeFlatModel, ShadowVolumeLine, ShadowVolumeModel, ShadowVolumeOval, ShadowVolumeOvalPole and ShadowVolumeSphere (16 to 146 lines each, 813 in all); Scene/SceneObjHolder.cpp outside 147-225; NameObj/NameObjAdaptor.cpp, MovementOnOffGroupHolder.cpp, NameObjFinder.cpp, NameObjRegister.cpp, ModelChangableObjFactory.cpp and NameObjFactory.cpp (4,892, mostly the creation table). The top ten is unchanged: nothing read in Round 11 outranks an entry.

Round 11 items 3 (render-side leftovers and the upscaler and frame-generation combinations), 4 (engine core rest and the importer; closing P-157) and 5 (the Player and KCollision comparison with the current Petari snapshot, including FactoryGalaxy candidates (b) and (c)) were **not started** in this round.

## Round 12

All comparisons are against D:\NebulaWork\build\audit-petari-upstream\Petari-master only.

### R12.1 Method, and what "read" means here

- **Read completely in both trees:**
  - Map/KCollision.cpp (port 872, upstream 1458)
  - Map/CollisionParts.cpp (600 / 567), lines 285-600
  - Map/CollisionCode.cpp (149 / 214), the lookups
  - LiveActor/Binder.cpp (345 / 342) and include/Game/LiveActor/Binder.hpp
  - Demo/ScenarioStarter.cpp (366 / 369)
  - MapObj/SpinDriverShootPath.cpp
  - Player/MarioActorRush.cpp: beginRush and endRush, all of both
  - Util/MapUtil.cpp, port 39-300 against upstream 45-330 (the line and ground checks), by a whitespace-normalised diff that was then read
  - crates/nebula-import/src/cook/kcl.rs (674)
  - native/include/nebula_float_cast.h
- **Compared by a condition-and-constant diff, with every function it flagged then read in both trees:**
  - Mario.cpp, MarioJump.cpp, MarioMove.cpp, MarioWall.cpp, MarioSwim.cpp, MarioActor.cpp, MarioModule.cpp, MarioState.cpp
  - The diff lists every if, while, switch and ternary condition (identifiers normalised away, literals kept) and every numeric constant that exists on one side only.
- **What that diff can miss:** a change inside a plain assignment, such as swapped operands or a changed sign in arithmetic with no condition or constant involved. Those eight Player files were **not** read line by line. They are named under "not read" at the end.
- **Absent on both sides:** Map/KCollisionServer.cpp (the server lives in KCollision.cpp), Map/Triangle.cpp and MapObj/ScenarioStarter.cpp (the file is Demo/ScenarioStarter.cpp).
- **Upstream has a file the port lacks:** Map/KCollisionPlus.cpp. No MarioState*.cpp other than MarioState.cpp exists on either side.

### R12.2 The difference list

Renames, type spellings and console calls replaced with the same result are left out. "B" marks a difference that changes behaviour for real game data.

**Map/KCollision.cpp**
1. setData. Port :26-47 checks the "NCOL" magic and panics on anything else, then fills a separate KCLFile. Upstream :46-55 fixes the pointers in place unless isBinaryInitialized. This is a format change and is explained by the panic text.
2. Prism lists. The port's are u32 (`const u32* prismList`, :116); upstream's are u16 (`u16* prismList`, :116). Octree words in the port are word offsets from the block's first word; upstream's are byte offsets. Both are explained in the comment at the head of kcl.rs.
3. searchBlock root slot. Port :851-854: `if (xyShift >= 0 && xShift >= 0) slot = ...;`. Upstream :1427-1432 computes the offset, then `if (xyShift == -1 && xShift == -1) offset = 0;`. They differ only when exactly one shift is negative. No disc file and no from_triangles output has that. Comment: "-1 for both: one root block".
4. checkSphere and checkSphereWithThickness. Once full, the port stops walking (`if (foundCount >= maxCount) return true;`, :336-338). Upstream keeps walking and adds nothing (`if (foundCount >= maxCount) continue;`, :401-403). The output is the same. Commented ("Full: nothing more is written").
5. checkArea3D. The port stops at `foundCount == maxCount` through the visitor (:286); upstream does `return maxCount` (:297-299). Same output.
6. KCHitArrow hit part. The port uses the table `{1, 2, 3, 5, 4, 7, 6, 1}` (:12, :760); upstream uses the nested ifs (:1302-1328). All 8 entries checked: same.
7. hitSpherePrism (:568-685) against KCHitSphere (:783-1002) and KCHitSphereWithThickness (:1004-1218). Checked:
   - side selection, the next and previous neighbour, and the tie rules;
   - nn, the t and s formulas for regions 5-7, and hit parts 2-4 and 5-7;
   - the `!isThick && ... > distances[0]` tests against the thin version's vertex and edge exits;
   - the `isThick && distances[0] + distance < 0` test against the thick finish.

   All the same.
8. V3u::setUsingCast. The port uses `nebula_f32_to_s32_console`: NaN gives INT32_MIN, and the rest is the saturating cast under -fno-strict-float-cast-overflow. Upstream uses `static_cast<s32>`, which compiles to fctiwz on the console. Same as the console. Commented in nebula_float_cast.h.
9. getTriangleNum. The port reads the header count; upstream derives it from the octree offset. Same for cooked disc data.

**Map/CollisionCode.cpp**

10. getCode type test. Port :103 is `getColumnKind(pName) != TableKind_Text`; upstream :146 is `getValueType(pName) == 0`.
    - Absent column: the port takes the integer read and upstream the string read. Both fail and return 0.
    - Integer and bit-field columns: same.
    - A float or short column would take different paths. The attribute files use integer bit fields (Round 10).
    - No comment.
11. Name lookup. The port matches the exact name (`mIndex.find(pCode)`); upstream matches the 32-bit hash (`hash == pCodeTable->mHashTable[i]`). They differ only on a hash collision. No comment.

**Map/CollisionParts.cpp**

12. checkStrikeBall steps. Port :345-346 is `movedLength < 35000.0f ? (s32)((1/35) * movedLength) + 1 : 1000`; upstream :317 is unbounded. They differ only for a part that moves 35,000 units or more in one step, or by NaN. Commented.
13. Strike lists. The port sizes them to maxHits, on the heap above 64 (StrikeLists, :290-318); upstream uses fixed stack arrays of 64 (:297-299). The keeper never asks for more than 32 (CollisionCategorizedKeeper.cpp:13), so the result is the same.
14. Area lists. The port grows a permanent list (:520-532); upstream uses a 512-entry stack array (:511, :532). Same up to 512.
15. createAreaPolygonListArray. The port returns 0 for 0 points and builds the box inline. Upstream has a 32-point local array (:533) and createBoundingBox. Same for 1-32 points.
16. calcCollidePosition. Cases 2-4 and 5-7 are merged (:455-472); upstream has them separate (:429-464). Same, including `plane == 2 ? 1 : 0` for the edge origin.

**LiveActor/Binder.cpp**

17. **B. storeCurrentHitInfo keeps every hit.**
    - Port :254-266: `planeRoom(mPlaneNum + hitCount)` grows the list, then all hits are stored.
    - Upstream :245-265: `if (capacity <= count + mPlaneNum) { ...; mPlaneNum = capacity; return stored; }` caps at the binder's capacity, or at 32 for a binder without a list (`HitInfo localPlanes[32]`, :125-133).
    - Binder.hpp:10-11 says the list grows. No comment says the push-out and ground choice change. See P-166.
18. findBindedPos steps. Port :200-201 has the same cap of 1000 at 35,000 as item 12; upstream :192 is unbounded. Commented.
19. copyPlaneArrayAndSortingSensor. The port stops at the caller's capacity (:58); upstream copies mPlaneNum entries (:71-73), which would overflow the caller's array. Same while mPlaneNum fits.

**Demo/ScenarioStarter.cpp and MapObj/SpinDriverShootPath.cpp**

20. init. The port adds a no-rail guard: the starter runs without the flight. Upstream would read a null rail. Commented.
21. calcShootMotionTime. The port has `startTime != 0 ? startTime * (flyTime / startTime) : 0`; upstream has an unguarded integer divide. Same for every startTime above 0.
22. 1500 ms against 90 steps: the same at the fixed 60 Hz step. SpinDriverShootPath: no logic difference (calcPosition, calcDirection, calcInitPose, getTotalLength and the 32 parabola samples all match).

**Player/MarioActorRush.cpp**

23. endRush swim conversion. The port converts Jump and PowerJump (modes 2 and 3) to Release; upstream converts modes 1, 2 and 3 to 4. No caller in either tree builds mode 1: RushEndInfo::Mode has no 1, and the upstream callers in PlayerUtil.cpp, MarioAccess.cpp, MarioActor*.cpp and MarioActorRushMsg.cpp use 0, 2, 3 and 4. Same for every caller.
24. endRush cases 1 and 5. Upstream's `case 1` (hold-button and squat flags) and `case 5` (`takeSensor`) are absent in the port. No caller builds them. Same.
25. endRush drops `_EF8 = 0; _F04 = 0;` (upstream :366-367). In upstream those two members are only ever written (MarioActorPad.cpp, MarioActorInit.cpp, MarioActor.cpp), never read. Same.
26. Port-only additions, outside the comparison: the `!mExtra` branches for LumaCore extra players, and `NebulaHost::gameContext(NEBULA_CONTEXT_TILT_SECTION, 0)`.
27. RushEndInfo fields. The port names them mForced (upstream bit 30), mInputOff (bit 31), mLimitJump (bit 23), mRollLanding (bit 22) and mDamage (bits 24-27). These were checked against the upstream setters in PlayerUtil.cpp:500-630; endBindAndSpinDriverJump sets Forced and InputOff on both sides. The damage numbers (1 Hit, 2 Fire, 3 Freeze, 4 Acid, 5 Electric, 6 Flip) map to the same Mario flags: fire and acid both set the flag MarioDamage.cpp reads as fire.

**Util/MapUtil.cpp (line and ground checks)**

28. isWaterPolygon. The port returns false; upstream compares the floor code string with "Water" and "Shallow". Upstream's getFloorCodeString returns only names from the floor table, which holds neither (CollisionCode.cpp:29-63). Same. Commented.
29. isPullBackPolygon. The port compares the index, upstream the strings. Same.
30. Line hits. The port insertion-sorts with a growing buffer; upstream does a selection sort into 32. Equal distances keep the keeper's order on both sides, and the keeper's room is 32. Same.
- isFloorPolygon, isWallPolygon, isFloorPolygonCos (cos 70) and getFirstPolyOnLineCategory: same.

**Player files (condition-and-constant diff, then the flagged functions read)**

31. Mario::createAngleMtx. The port ends after createDirectionMtx. Upstream :2069-2151 then concatenates _C4 and computes an upper-body aim at a sensor (_58, _64, _94). That block has no effect: _C4 is only ever set to identity (:211), and _412 is only set to 0 (:205), so the clamp is ±0. _64 and _94 are read nowhere. Same. No comment.
32. The debugMode tests are dropped (actionMain, checkForceGrounding, createAngleMtx); the port has no debug mode.
33. Write-only members are dropped:
    - Mario::_410 (Mario.cpp:713-714)
    - Mario::_934 and _3C (constructor 0.8 and -0.515)
    - Mario::_340 (MarioJump.cpp:301, 1060)
    - Mario::_406 (MarioBee.cpp:85, MarioJump.cpp:1352)
    - the dead connectRatio (MarioJump.cpp:1247-1250)
    - Mario::_3FC (MarioMove.cpp:835-841)
    - MovementStates._28 (MarioWall.cpp:344: "not NotWallSlip"; written in five places, read in none)
    - MarioActor::_7E2

    Same. No comments.
34. Empty `if (MR::isNearZero(mAirGravityVec)) {}` blocks are dropped (setGroundNorm, getAirGravityVec, getGravityVec, fixFrontVecByGravity, doExtraServices, doLanding). No-ops.
35. **B. Mario::procHipDrop.** Upstream MarioJump.cpp:1732-1734 is `if (MR::isNearZero(mActor->_240)) { mMovementStates.digitalJump = true; }`. The port (MarioJump.cpp:1676-1679) goes from the stun-timer test straight to the gravity-jump halving. No comment. See P-167.
36. Mario::checkWallCode and the sound codes compare indices; upstream compares strings. Same.
37. MarioModule::setJointGlobalMtx. The port skips a null transform list; upstream writes through null plus an offset. MarioModule::startPadVib adds checks for a missing pad.
38. MarioActor::decLife. The port's max-health reset is `mMaxHealth <= startingHealth || mHealth > startingHealth`; upstream's is `mMaxHealth == 3` / `mHealth > 3`, with a duplicate Luigi branch. Same with the default rules (3).
    - On a one-health stage, upstream sets max health to 3 at the hit that kills; the port leaves it.
    - The port also adds a mod damage veto and the cHitTakesAll rule (off).
39. MarioActor::doRush. The port adds an early return when the rush ended inside updateGroundInfo or updateCubeCode, and `mInRush &&` before isForceStopRush. Upstream reads `_924` after it may have been cleared. Commented ("Those can end the rush").
40. MarioActor::isSleeping, calcAnim, calcAnimInMovement, init2 and the forceGameOver family: same logic, plus extra-player branches and mod power-up models (port additions).
41. MarioSwim::close and start: extra-player branches only. Upstream's dead `copyPos` (:1682) is dropped. MarioSwim::updateTilt and decideEffect: same.
42. Mario::changeStatus: the port fires the mod action event before Notice; the order is otherwise the same. MarioMove isEnableTurn and mainMove: same, with constants given as enum names.

**Count:** 42 entries.
- **Change behaviour for real game data:** 2 (items 17 and 35).
- **Change behaviour only for inputs the shipped data does not produce:** 5 (items 3, 10, 11, 12 and 18).
- **Same:** the rest.
- **Changes every result at the last bit:** the build itself (P-168), outside any one function.

### R12.3 FactoryGalaxy, candidates (b) and (c)

**(b) Can the port's collision lookup return a different triangle or floor code under a point?**

Not through its logic. checkPoint, checkSphere, checkArrow, checkArea3D, hitSpherePrism, KCHitArrow and searchBlock decide exactly as the current decompilation does (items 1-9). The cook keeps:
- positions, normals and heights bit for bit (kcl.rs:114-130);
- the prism order;
- every list's prism order (kcl.rs:154-171);
- the attribute row, widened from u16 to u32.

The floor-code read matches for the integer attribute columns (item 10). Three things remain:
1. **Rounding.** The console build has `-fp_contract on` (Petari configure.py:210), so every `a*b + c` in these tests is one fused multiply-add rounded once. The port builds with `-ffp-contract=off` (native/CMakeLists.txt:276), so each is rounded twice. A point within about one ulp of an edge plane (`> 0.0f`, `> height`) or of the thickness limit can fall on the other side, and checkPoint then returns the next prism in the list (P-168).
2. **The cook's octree proof.** walks_agree runs only with the proof-tools feature (kcl.rs:296-297). A normal import does not check that every path reaches the same list (P-169).
3. **Binder.** Not the lookup itself, but what Mario stands on: item 17 changes which planes push him and which floor becomes mGroundInfo (P-166).

**(c) Can the opening flight end at a different point?**

No difference was found in the flight:
- ScenarioStarter (items 20-22) is the same.
- SpinDriverShootPath is the same: calcPosition is the rail coordinate at rate times length, plus the start offset eased out over the first half.
- The rail read is the same.
- The ScenarioStarter branch of endRush is the same: launchKind 2, jumping, not on ground, gravity refreshed, binder offset at -70 along gravity.

The flight ends at the rail end on both sides. After it, the landing goes through MarioActor's binder. Its capacity is 8 on both sides (MarioActor.cpp:301, upstream :361), but only the console drops planes past 8 (P-166). If the landing spot touches more than 8 triangles within 60 units, the push-out and the chosen ground differ from the console's.

**Run that settles both:** FactoryGalaxy scenario 4 from the stage start, logging Mario's `mBinder->mPlaneNum`, `mGroundInfo` triangle index and floor code each step until he stands still.
- Any step with mPlaneNum above 8 is where the port departs from the console. P-166 is then the cause.
- If no step goes above 8, the remaining suspect is P-168. Compare that log with a Dolphin run's position trace.

### R12.4 Findings

#### P-166 Binder keeps every plane, so Mario's push-out and ground can differ from the console's
- Severity: wrong behaviour
- **What:** The console keeps at most `capacity` planes per bind: 8 for Mario, 64 for the camera interpolator, 32 for binders without a list. The port keeps every plane, so obtainMomentFixReaction and storeContactPlane see planes the console never stored.
- Evidence:
  - Port: Binder.cpp:254-266 (`planeRoom(mPlaneNum + hitCount)`), :85-103 (growth), Binder.hpp:10-11.
  - Upstream: Binder.cpp:245-265 (`if (capacity <= count + mPlaneNum) {... mPlaneNum = capacity; return stored; }`), :125-133 (`HitInfo localPlanes[32]`).
  - Mario: MarioActor.cpp:301 `initBinder(60.0f, 0.0f, 8)`, the same as upstream :361.
  - The keeper returns up to 32 hits per call (CollisionCategorizedKeeper.cpp:13), so a Mario bind can carry 32 or more planes in the port and only 8 on the console.
  - On the console, once the first pass fills 8, the slide pass stores none, and its reaction (`obtainMomentFixReaction(.., firstHitCount)`) is zero. In the port it is not.
  - Mario.cpp:1498-1511, MarioActor.cpp:781-821 and MarioCollision.cpp:1379-1386 loop over mPlaneNum and see the extra planes too.
- Why and fix: Removing the console limit changes which planes push Mario and which floor becomes his ground on dense meshes and corners. Keep the cap in the bind: store the first `capacity` hits in the order the keeper returns them, as the console does, and keep the growable list only as storage. If the owner wants the console limit gone, that is a gameplay change for him to decide, not a storage fix.
- Confidence: the difference is verified in code. How often more than 8 planes occur is suspected, needs a run: FactoryGalaxy scenario 4 and a dense-mesh stage, logging mBinder->mPlaneNum above 8 per step.

#### P-167 The hip drop no longer marks a digital jump when the actor's gravity is zero
- Severity: wrong behaviour
- **What:** In zero gravity, upstream procHipDrop sets mMovementStates.digitalJump; the port never does. This changes isDigitalJump in procJump, which gates the connect gravity (MarioJump.cpp:1181).
- Evidence: upstream MarioJump.cpp:1732-1734 `if (MR::isNearZero(mActor->_240)) { mMovementStates.digitalJump = true; }`; port MarioJump.cpp:1672-1679 has no counterpart and no comment.
- Why and fix: Add `if (MR::isNearZero(mActor->mGravityVec)) { mMovementStates.digitalJump = true; }` after the mStunTimer test. It is rare: the actor's gravity is zero only outside every gravity field.
- Confidence: verified in code.

#### P-168 Collision and movement arithmetic rounds differently from the console
- Severity: wrong behaviour
- **What:** The game module is built with `-ffp-contract=off`, so every multiply-add the console fused is rounded twice. Edge and face tests at their boundaries can decide the other way, and positions drift by ulps.
- Evidence: native/CMakeLists.txt:276 (`-ffp-contract=off  # the same results on every CPU (A-10)`), :347 and :375; Petari configure.py:210 (`-fp_contract on` in cflags_base, used by the game objects). KCollision's tests are the pattern this changes: checkPoint :135-160, hitSpherePrism :582-603, KCHitArrow :702-757.
- Why and fix: The choice gives the same result on every CPU, but not the console's result. fmaf is correctly rounded on every IEEE platform, so it can keep both properties. Write `fmaf` explicitly where the console fused a multiply-add: start with the dot products in KCollision, Binder and the Player physics, where a boundary decides a branch. Leave `-ffp-contract=off` for everything else. The console's fmadds also rounds its frC operand to 25 bits when it holds a double value. Single values loaded with lfs are not affected, so most sites match with plain fmaf.
- Confidence: suspected, needs a run. Trace Mario's position, the ground triangle and checkPoint's prism per step in the port and in Dolphin over the same input recording (FactoryGalaxy scenario 4 start, then a long walk on a tessellated floor). The first step that differs names the site.

#### P-169 A normal import does not prove the cooked collision octree reaches the same prisms
- Severity: untested
- **What:** The proof that every root and path reaches the same list runs only with the proof-tools feature. The shipped cook trusts the walk.
- Evidence:
  - kcl.rs:296-297 (`#[cfg(feature = "proof-tools")] proof::walks_agree(bytes, &ncol)?;`).
  - The unit test covers one root leaf (kcl.rs:631-672).
  - from_disc counts roots from the masks (:135-140). A disc file whose root layout does not match `1 << x_shift` per axis would fail at `place[...]` or, if the counts happen to fit, skip roots silently.
- Why and fix: KCollision returns the cooked lists as-is, so a wrong list is a wrong floor with no error. Run walks_agree in the normal cook (it is one walk per model). At least run it in CI over every KCL of both discs (RMGE01, RMGP01, RMGJ01) and record the model and leaf counts.
- Confidence: verified in code (that the proof does not run). Whether any shipped file differs is suspected, needs a run: the importer with proof-tools over all three discs.

### R12.5 What item 1 did not cover

Not read line by line (condition-and-constant diff only, flagged functions read):
- Mario.cpp (2004)
- MarioJump.cpp (2658)
- MarioMove.cpp (1096)
- MarioWall.cpp (751)
- MarioSwim.cpp (3253)
- MarioActor.cpp (2858)
- MarioModule.cpp (618)
- MarioState.cpp

A swapped operand or sign inside a plain assignment in these files would not have been caught. Map/KCollisionPlus.cpp exists upstream only and was not compared.

Round 12 items 2 (area 8 engine side and the importer; closing P-157 and the cooked-data answer for P-151, P-154 and P-155), 3 (area 2 leftovers and the upscaler and frame-generation combinations) and 4 (Round 11 item 2's remainder) were **not started** in this round.

## Round 13

No helper scripts were written this round. The only file written is this one.

### R13.0 FactoryGalaxy scenario 4: the settled observation and what the code says

**Observation (the coordinator's probe on the current build, sampled at the start of each game frame, frames 1000-1700):**
- Mario's binder holds 0 planes at every sample.
- His floor code is 0, and he is never on the ground.
- x and z stay fixed at (-65691.99, -39761.16).
- His height follows an exact cycle from 790 to 1096, repeating every 280 frames (983.6158 at 1060, 1340 and 1620; 913.6858 at 1080, 1360 and 1640).

So P-166 (the binder cap) and a jump-code floor are **not** the cause: he touches nothing. He swings in mid-air through a gravity field with no solid ground under him, and nothing kills him.

**What the shape of the motion says:**
- x and z are fixed to the hundredth over 700 frames, and the cycle repeats exactly with no loss. So he has no horizontal velocity at all.
- The flight cannot leave him like that. It ends with `MR::endBindAndSpinDriverJump(this, mJumpVec)` (ScenarioStarter.cpp:163), a forced power jump along the rail's end direction, which carries a horizontal component.
- A start with no flight can: he is put at the start position at rest, and gravity moves him only along its own line.

**What is normally under a scenario start point after the flight:**
- The starter's start position (`mStartPos = *MR::getPlayerPos()`, ScenarioStarter.cpp:80) is out in space, where the flight begins.
- The rail carries him to its end. The rail end lies over the stage's first planet, placed in the common layer or the scenario's layer.
- He lands on that planet's collision, and the death areas of the stage lie below it.
- Nothing is placed under the start position itself, because on the console Mario is never left there. Upstream builds `new RailRider(rIter)` unconditionally (SpinDriverShootPath.cpp:10, 15), so a starter always flies.

**Layers.** How the scenario's layer mask is built is the same in both trees:
- `getCurrentScenarioLayers` is the zone's column of the scenario's row in ScenarioData, times 2. Bit 0 is the common layer. See ScenarioDataParser.cpp:226-233 against Petari :223-231.
- StageDataHolder::initAfterScenarioSelected ORs in the common layer for zones (StageDataHolder.cpp:116-123).
- A comet adds no layer bits in either tree. A comet scenario is chosen only by the scenario number, so its row alone decides. The comet changes timings (Dossun, Onimasu, BGM) through `isGalaxyQuickCometAppearInCurrentStage`, never the placement.
- One difference: the port walks 32 layer folders (`cLayerDirName[32]`, StageDataHolder.cpp:22, loops at :463 and in eachPlacementTable). The console walks 17, Common and LayerA-LayerP (Petari StageDataHolder.cpp:31, the loops at :464, :477, :495 and :510). This matters only if a ScenarioData value sets a bit above 16. It can add objects but cannot remove ground. See P-171.

**Collision registered late.** A map object whose collision is not valid at the start still leaves Mario with the flight's horizontal speed. That does not fit the fixed x and z, so it is the second candidate, not the first. The mechanisms are:
- Objects made dead until their appear switch (SW_APPEAR, through the MapObjActor appear switch), and MapParts planets behind MapPartsAppearController. Their gravity, placed separately in PlanetObjInfo, stays on.
- Zone collision bounds, which are made once at the keeper's first movement (CollisionCategorizedKeeper.cpp:17-72). They match Petari :14-40, and addParts remakes them (:579-585). Not a port difference.

**The two most likely places in the port's code:**
1. **The starter is never made.**
   - ScenarioStarter.cpp:47-61 is the port-only no-rail guard. It logs, renames the starter to "ScenarioStarter (no rail)", and returns, so the stage starts with Mario at rest at the start position, out in space.
   - The rail comes from MR::getRailInfo (SceneUtil.cpp:293-297), which goes through findPlacedStageDataHolder (StageDataHolder.cpp:318-348) to getCommonPathPointInfo (StageDataHolder.cpp:171-190). That returns no points when the starter's zone has no CommonPathInfo or no row with that l_id, or when the starter's row has no CommonPath_ID (`MR::isConnectedWithRail`, PlacementUtil.cpp:464).
   - On the console this cannot happen without a crash, so if it happens in the port, the port's rail or column read differs for this scenario.
2. **The flight runs, but the landing planet's collision is absent.**
   - The planet or its MapParts row is not in the layer set (StageDataHolder.cpp:116-123 with ScenarioDataParser.cpp:226-233: a ScenarioData zone column read that returns 0 gives only the common layer), or the planet is waiting on an appear switch.
   - This needs Mario to lose his horizontal speed, which the jump-out gives him. That is why it is second.

**The one log line that decides:** the run's log either holds ScenarioStarter.cpp:58's existing line or it does not:

> `ScenarioStarter: its rail is not in the stage (CommonPath_ID names no rail with two points); not made, the stage starts without its opening flight`

- Present: place 1 is the cause. Next, find which of the three reads failed: the CommonPath_ID column, the zone's CommonPathInfo table, or the l_id row.
- Absent: place 2. Then one added line in initAfterScenarioSelected, `nebula_log("layers %s scenario %d mask %08x\n", mStageName, curScenarioNo, curLayers)`, compared with the disc's ScenarioData row, settles the layer half.

#### P-170 A starter without a rail strands the player in space instead of failing
- Severity: wrong behaviour
- **What:** When the starter's rail lookup returns fewer than two points, the port drops the starter and starts the stage with Mario at the start position. That position is in space, with no ground and no death area, so the stage cannot be played and nothing ends it.
- Evidence: ScenarioStarter.cpp:47-61 (`setName("ScenarioStarter (no rail)"); return;`). Upstream SpinDriverShootPath.cpp:10, 15 build the rail unconditionally. The probe above fits this path exactly: fixed x and z, a lossless vertical cycle, 0 planes.
- Why and fix: The guard turns a failed table read into a silent soft-lock. The data always has the rail on the console, so a miss is a port bug in the read.
  - Make the miss loud: an error in the log and the on-screen notice the mod loader uses.
  - Keep the player's start safe: put Mario at the rail's end point when only the flight data is unusable.
  - If no rail can be read at all, hold the start in the stage's normal start position for scenarios without a starter, and say so.
  - Then fix the read that failed.
- Confidence: verified in code (the path exists). That it is the cause for FactoryGalaxy scenario 4 is suspected, needs a run: the log line above.

#### P-171 The port loads layer folders past LayerP that the console never loads
- Severity: console-era remnant
- **What:** initLayerJmpInfo and eachPlacementTable walk 32 layer names. The console walks 17, so a ScenarioData value with a bit above 16 loads LayerQ to LayerAE placement in the port only.
- Evidence: StageDataHolder.cpp:22 (`cLayerDirName[32]`), :463 (`i < ARRAY_SIZEU(::cLayerDirName)`), eachPlacementTable (`for (u32 i = 0; i < ARRAY_SIZEU(::cLayerDirName); i++)`); Petari StageDataHolder.cpp:31 (`cLayerDirName[0x11]`), :464, :477, :495, :510.
- Why and fix: Disc data stops at LayerP, so the extra 15 entries serve mods only. Record that intent in a comment at :22. Count only the bits the zone's archive has folders for, so a disc stage reads exactly the console's set.
- Confidence: verified in code.

### R13.1 Area 8: engine side and the importer; the modernisation map for the render data path

What was read:

| File | Read |
|---|---|
| runtime/src/render/core/registries.cpp | 1-351, 535-817, 864-1050 this round. With 352-534 and 818-863 from Round 9, the file is now read completely. |
| frame_core.h, draw_record.h, program_table.cpp, world_lights.cpp | completely |
| compose.cpp | 1-117 (118-247 in Round 9), so completely |
| pipelines_vk.cpp, pipelines_d3d12.cpp, pipelines_d3d11.cpp | the state-word decode: vk 600-740, d3d12 310-450, d3d11 170-330 |
| crates/nebula-import/src/cook/material.rs, material_layout.rs | completely |
| programs.rs | 1-340, 1322-1521 (generate: forms and row order), 1755-2054 |
| model_write.rs | 36-125 (pipeline_state, material_slots), 1593-2300 (write: the material, form, texture and hit records) |
| model_ir.rs | 1008-1132 (read_material: blend, cull, alpha, depth) |
| texture.rs | its declarations only (record layout, image id at byte 32, name hash at byte 40) |
| game side, for the map | Render/Material.cpp 290-880 (fillRow, constantsOf, tableOf), GameTexture.hpp, RenderTexture.cpp 120-230 |

Not read: programs.rs 341-1321 and 2055-2209; model_write.rs 126-1592 and 2301-2558; texture.rs bodies (decode and level making).

What is already modern, and does not need to move:
- **Programs.** Programs are generated per form at import (programs.rs generate) and compiled into one linked blob, looked up by binary search (program_table.cpp:115-121).
- **Pipeline state.** The state word is cooked per material (model_write.rs:40-47), in the engine's own codes: the GX blend factors 2 and 3 are already turned into destination-colour codes at import (model_ir.rs:1019-1027), and cull is mapped. Pipelines are registered by hash and written to Pipelines.bin for pre-building (registries.cpp:326-345, 790-817).
- **Layout materials.** These are cooked to programs and rows at import (material_layout.rs form()).
- **Registered objects.** Models, meshes and plain materials are registered once. Texture tables are de-duplicated and retired after 600 idle game frames (registries.cpp:941-1029).
- **The three back ends.** All three decode the state word the same way: blend, write mask, cull, reversed depth, and the stencil fields. Vulkan and D3D12 skip the stencil in the tracer's prepass; D3D11 has no prepass (pipelines_d3d11.cpp:176-178).

**The map.** Each entry gives the current record, the proposed record, who fills it and when.

1. **Material constant rows (P-151).**
   - *Current.*
     - Each form's file record holds two lists of u32 row codes named by register: REGISTER 0-2, CONSTANT 3-6, MATERIAL_COLOR 7-8, AMBIENT 9-10, FOG 11, FOG_COLOR 12, ALPHA_REF 13, OVERRIDE 14, SCALE, WARP and COORD (programs.rs:16-27, model_write.rs:1932-1959).
     - The game turns every code into a float4 through Material::fillRow (Material.cpp:296-424). The sources are bytes /255 and i16 /255, alpha_ref /255, texture width and height read from the texture header, and coordinate matrices with a billboard view multiply.
     - A material that nothing has touched is filled once and registered as a slot (constantsOf :731-741). Any other material is filled again each frame into the arena, or each draw when edited (:743-767), and the engine blends those rows per picture (P-156).
   - *Proposed.* For each form, the cook writes:
     - a **static block**: float4 rows in final form for every row that no clip and no game code can write. The cook already knows which registers any animation writes (programs.rs Written, :2010-2055).
     - a **dynamic list**: one entry `(row index, source, source index)` for each row something can write. The sources are a coordinate SRT or eye matrix, a register written by a clip, ambient from the light set, a fog edit, and the player-pass override.
   - *Who fills it and when.*
     - The cook writes both at import.
     - The game registers the static block once per form at model load.
     - Once per game step, the game writes only the dynamic rows of each drawn instance into a small instance block. Clip colours go in as floats: no byte rounding and no 11-bit clamp in Clip.cpp.
     - The engine binds slot plus instance block and blends only the instance block between steps.
     - fillRow's switch, the before-and-after memcmp in Material::calc and the "touched once, per-frame forever" path (P-173) all go.
2. **Ray-hit and engine-side material facts (P-157).**
   - *Current.* read_model (registries.cpp:403-512) searches the first form that replaces nothing:
     - for the glow source's register code, to find glow_row;
     - for GX texgen type 1 with sources 4-19, to find the map 0 and map 1 coordinate rows;
     - for alpha_comp0 4 or 6, to find the alpha cutoff.
   - *Proposed.* NebulaModelHit gains, **per form**: glow_row, map0 (row, uv), map1 (row, uv). It also gains, once: glow_value as float3, alpha_cutoff as a float, and an alpha mode.
   - *Who fills it and when.* The cook fills these in model_write.rs while it writes each form, because generate() returns that form's row lists (Text.vertex_rows and pixel_rows). The engine copies the fields at load and indexes them by the draw's form. This fixes P-172.
3. **Textures and samplers (P-155).**
   - *Current.*
     - The model's texture record (40 bytes, model_write.rs:1975-1985) holds GX wrap codes, the GX min-filter code 0-5, and the anisotropy level.
     - The cooked image record already holds a stable image id and a name hash (texture.rs IMAGE_ID_AT 32, NAME_HASH_AT 40).
     - The game decodes the filter codes when it rebuilds a table (Material.cpp:789-797, cached by stamps :774-777) and on every site draw (TypedDraw SiteTextures::toFrame).
     - The game finds the host id by the image's address (RenderTexture::idOf, RenderTexture.cpp:175-203).
   - *Proposed.*
     - The cook writes the sampler already in NebulaFrameTexture form: min_linear, mag_linear, mip_filter, anisotropy as a count, and the lods.
     - The cook writes each form's texture list as `(texture number, sampler)`.
   - *Who fills it and when.*
     - At model load the game registers each image record once (registerRecord) and keeps the id in the model's texture slot.
     - Per draw the table is that list with ids filled in, cached per form as it is today.
     - The address table remains only for images written after load.
4. **Site draws (P-154).**
   - *Current.* Immediate mode: per draw, arena constants, a texture table, ring vertices, and CPU-built indices.
   - *Proposed.*
     - Each emitter resource and each site material gets a cooked record: program pair, state word, static rows, texture list with samplers. The cook sides already exist: particles.rs converts blend factors, material_layout.rs makes programs and rows.
     - The fixed shapes (quad, fan, ring, disc, beam) become meshes the engine registers once at start.
   - *Who fills it and when.* The cook writes the record at import. The game registers it at resource load. Per draw the game passes a matrix and one colour/alpha row; line and point widening moves to the vertex shader.
5. **Draw matrices (P-156).** Unchanged from R9.8: joint matrices once per game step; compose, skin and normal matrix in the vertex shader.

Order:
1. Item 2. It is small, cook-only, and fixes P-172.
2. Item 3.
3. Item 1.
4. Item 4.
5. Item 5.

Item 1 needs a model format version bump: VERSION 11 to 12 in model_write.rs:12, with the reader in Material.cpp. Every disc model is re-cooked by the store cook anyway.

#### P-172 The engine reads a glowing or water material's live rows at positions taken from the unlit form, so with pixel lighting it can read the wrong row
- Severity: wrong behaviour (ray-traced path only; RT is shelved, so not in the release)
- **What:**
  - read_model takes glow_row and the map 0 and map 1 coordinate rows from the first form that replaces nothing, which is always the unlit plain form.
  - The lit form moves the colour channel rows (MATERIAL_COLOR, AMBIENT) from the vertex rows to the end of the pixel rows.
  - Both forms can hold the same number of rows. rt_walk accepts the rows when only the total count matches, so a lit draw reads another row as its glow colour or water coordinates.
- Evidence:
  - forms_of pushes Form::default() first and the lit form second (programs.rs:1939-1948).
  - generate fills v_rows with channel_lines before the coordinate rows when the form is unlit (:1495-1511). When the form is lit, channel_lines writes into p.rows after the stage rows (:1423-1441).
  - read_model uses the first `form.replace == 0` form and breaks (registries.cpp:431-446, 459-507).
  - The guards are count-only: rt_walk.cpp:2977 and :2999 (`constants.vertex_rows + constants.pixel_rows != mesh.glow_row_count`), and :1936 for map 0.
- Why and fix: A row index is only meaningful for the form it came from, and the draw does not carry its form to the engine. Write the indices per form in the hit record at import (map item 2), and pass the form index with the draw. Until then, check the form's row codes, not just their count.
- Confidence: verified in code (row order by form, and the count-only guards). That a real material hits it needs one check: dump the row codes of forms 0 and 1 of a lit material whose hit record has glow_source 8 or 9 (material colour) and compare the positions.

#### P-173 A material touched once never returns to its registered slot
- Severity: performance
- **What:** constantsOf uses the registered slot only while `mSlotStamp == mStamp`. One touch() (a colour set by a switch, or a clip that played once) moves the material to the per-frame fill for the rest of the scene, even if it never changes again.
- Evidence: Material.cpp:731-741 (the slot condition), :751-767 (the per-frame path); Material.hpp:235-237 (`touch() { mStamp++; }`).
- Why and fix: Slots are content-addressed (material_register_locked de-duplicates by FNV hash plus memcmp, registries.cpp), so re-registering after the rows have stayed the same for a few frames costs one fill and one hash. Slots are never freed, so do not re-register on every change; re-register once the stamp has been still for, say, a second of game steps. With map item 1, the problem goes away.
- Confidence: verified in code. Run: count the materials on the per-frame path at the end of a long scene (frame-list capture, constants with slot NONE) in a galaxy with switch-coloured parts.

### R13.2 Area 2: upscalers, frame generation and their combinations

Read this round:
- post_process_settings.cpp, completely.
- temporal_upscaler.cpp, xess_upscaler.cpp, fsr_frame_generation.cpp and metalfx_mtl.mm, completely.
- ui/settings_graphics_page.cpp and ui/settings_model.cpp, completely: where the choices are offered.
- render_config.h, 85-464.
- renderer_d3d12.cpp, 4678-5070: temporal_mode, prepare_temporal_upscaler, interpolation_mode and temporal_inputs_match.
- renderer_vk.cpp, 1955-1970 and 2736-2751.
- renderer_vk_frame.cpp, 3108-3177 and 3915-3951.
- render_core.cpp, 880-1040: the generated-frame budget.
- post_process_mtl.mm, 500-660.
- The HDR, dynamic-resolution and frame-generation lines of streamline_bridge.cpp, streamline_vk.cpp, temporal_upscaler_vk.cpp, sgsr2_vk.cpp, sgsr2_mtl.mm and sgsr2.hlsl (by grep).

Not read: post_process.cpp (rest), post_process_vk.cpp, post_process_mtl.mm outside 500-660, game_bloom*, scene_camera*, ambient_occlusion*, sgsr2_vk.cpp and sgsr2_mtl.mm bodies, streamline_bridge.cpp and streamline_vk.cpp bodies, fsr_frame_generation_vk.cpp, fsr_frame_generation_core.inl, mtl_context.mm (rest), texture_backend_mtl.mm.

**What is offered, and what each needs**

The settings live in render_config.h:99-113 and settings_graphics_page.cpp:23-98 and 191-243.
- `upscaler`: 0 off, 1 FSR 1, 2 FSR 3, 3 DLSS, 4 XeSS, 5 SGSR 2, 6 MetalFX.
- `frame_generation`: 1 DLSS, 2 FSR 3.
- Native interpolation is the frame-rate target with frame_generation 0.

| Choice | Offered where | Needs | Dynamic resolution | HDR / wide scene | With interpolation |
|---|---|---|---|---|---|
| FSR 1 | every back end | nothing (post EASU) | yes, the part (set_post_input_part) | runs on display values after tone map | every picture |
| FSR 3 | D3D12 (built with GALAXY_WITH_FSR3, SM 6.2); Vulkan with GALAXY_WITH_FSR3_VK and temporal_upscaling() | jittered colour, depth, motion, reactive | render size changes per dispatch, but the context is made **without** FFX_FSR3UPSCALER_ENABLE_DYNAMIC_RESOLUTION (P-175) | ENABLE_HIGH_DYNAMIC_RANGE when the scene is wide | every picture upscaled; suspended for good on a slow GPU (P-174) |
| DLSS | D3D12 and Vulkan with Streamline loaded at start | the same; Ray Reconstruction when tracing | floor from renderWidthMin | colorBuffersHDR when wide | as FSR 3 |
| XeSS | D3D12, Vulkan (library present) | colour, depth, motion; responsive mask | floor from xessGetOptimalInputResolution lowest | LDR flag unless wide | as FSR 3 |
| SGSR 2 | Vulkan, Metal (sgsr2_available) | colour, depth, motion | any part up to the size it was made for | **no HDR handling**: Lanczos and box clamp on wide values (P-176) | as FSR 3 |
| MetalFX temporal | Metal with metalfx_temporal | colour, depth (or a depth-only copy), motion | inputContentPropertiesEnabled from macOS 14 / iOS 17, else whole size only | **no HDR setting**, auto exposure off (P-176) | as FSR 3 |
| MetalFX spatial | Metal without the temporal scaler | display picture | a part makes it fall back to EASU silently (post_process_mtl.mm:623-626) | perceptual mode on display values | every picture |
| DLSS Frame Generation | D3D12 and Vulkan with Streamline FG at start | DLSS on (the row sets upscaler 3) | through DLSS | Streamline | replaces native interpolation: the game presents 60 (presented_max_fps) |
| FSR 3 Frame Generation | D3D12 (upscaler 2 or 4), Vulkan (2, 4 or 5) with a generation present queue | the upscaler's depth and motion; replaces the swap chain, windowed only | as the upscaler | back-buffer format as given | replaces native interpolation; fixed target 120 (settings_model.cpp:89-92) |

**Combinations the settings allow**

1. **FSR 3 FG, then the upscaler changed to OFF, FSR 1 or DLSS.**
   - The row says "(NEEDS FSR 3, XESS OR SGSR 2)" and frame_generation stays 2.
   - Neither renderer wants generation (renderer_d3d12.cpp:3152, renderer_vk.cpp:2739-2745), so presented_max_fps is the 120 target and native interpolation runs at 120.
   - Handled, but the row reads as FSR FG while native interpolation is running.
2. **DLSS FG, then the upscaler changed away from DLSS.**
   - Same pattern: "(NEEDS DLSS)", and native interpolation at the kept target.
   - Handled, as the comment in render_config.h:103-107 says.
3. **Any temporal upscaler with native interpolation on a GPU that cannot afford a generated frame.**
   - The renderer turns all native interpolation off until a setting in the key changes, not just the upscaling of generated frames (P-174).
4. **FSR 3 with dynamic resolution.** The context is not told the size moves (P-175).
5. **SGSR 2 or MetalFX temporal with a wide scene** (enhanced lighting, HDR output). Neither gets an HDR mode (P-176).
6. **MetalFX spatial with dynamic resolution.** Below the full size, every picture is EASU and nothing says so. Small; the status row could name it.
7. **A temporal upscaler on Direct3D 11.** Not offered: Pipelines::build refuses aux targets (pipelines_d3d11.cpp:174-178), and post_process_settings_from maps 2-5 to FSR 1.
8. **A pending XeSS or FSR 3 context (created on a worker thread).**
   - Until the context is ready, the scene renders at the render scale and post enlarges it with EASU (renderer_d3d12.cpp:4962-4974; post_process_settings.cpp:24-26 maps 2-5 to FSR 1).
   - Handled.
9. **Ray tracing with no temporal upscaler.** FSR 3 runs in native AA (render_config.h:326-330). RT is shelved for the release.

#### P-174 One slow stretch turns native interpolation off for the rest of the session when a temporal upscaler is on
- Severity: wrong behaviour
- **What:** When a generated frame does not fit beside the upscaled one, or a cadence drop to 0 finds the walk at t = 1 costs a quarter of a game frame, the renderer sets temporal_interp_suspended_. From then on, interpolation_mode() returns false, so no generated frames are made at all. The flag is cleared only when the upscaler, quality, scene or output size, rate cap, VSync or refresh changes.
- Evidence:
  - render_core.cpp:903-907 and :1020-1025 (the two suspend calls).
  - renderer_vk_frame.cpp:3919-3926 and renderer_d3d12.cpp:4839-4848 (`if (temporal_mode() && temporal_interp_suspended_) return false;`).
  - The only reset is renderer_vk.cpp:1963-1966, renderer_d3d12.cpp:3633-3635 and renderer_mtl.mm:568-570 (key change). The key leaves out dynamic resolution steps by design (renderer_vk_frame.cpp:3943).
  - The log line says "temporal upscaling of generated frames off", but the effect is all generated frames off.
  - host_window_win32.cpp:197 is a comment that promises "the FRAME RATE row says so" with nothing after it. The row shows only the measured rate.
- Why and fix: A heavy first scene (a galaxy load, a boss intro) costs the player interpolation in every later scene.
  - Re-test on a timer, as the generated-frame cap already does at 1200 sequences (render_core.cpp:920-924), and on every scene change.
  - Name the state in the FRAME RATE row.
- Confidence: verified in code. Run: SGSR 2 performance mode with a 120 Hz target on the tablet; enter HoneyBeeKingdom, then a light galaxy; watch the `[interp]` line and the shown rate.

#### P-175 FSR 3 runs with a moving render size but is never told the size moves
- Severity: wrong behaviour (image)
- **What:** Dynamic resolution changes the render size each dispatch within the context's maximum. Both FSR 3 contexts are made without FFX_FSR3UPSCALER_ENABLE_DYNAMIC_RESOLUTION, the flag the SDK defines for this use.
- Evidence:
  - temporal_upscaler.cpp:50-52 and temporal_upscaler_vk.cpp:250-252: only DEPTH_INVERTED, AUTO_EXPOSURE and HIGH_DYNAMIC_RANGE are set.
  - FidelityFX-SDK-1.1.4 ffx_fsr3upscaler.h:139 defines `FFX_FSR3UPSCALER_ENABLE_DYNAMIC_RESOLUTION = (1<<6)`.
  - renderer_d3d12.cpp:5027-5030 accepts any render size up to the context's.
  - Fsr3Upscaler gives no smallest_render_width, so the floor is the governor's lowest step (renderer_vk_frame.cpp:3129-3141).
- Why and fix: Set the flag when dynamic resolution is offered and on. Re-create the context when the setting flips; the key in prepare_temporal_upscaler would then include it.
- Confidence: verified in code for the missing flag. The visible effect needs a run: FSR 3 balanced with dynamic resolution on, in a heavy scene where [dynres] steps; compare history ghosting at a step with the flag set and unset.

#### P-176 SGSR 2 and MetalFX temporal get wide (HDR) scene colour with no HDR mode
- Severity: wrong behaviour (image), suspected
- **What:** FSR 3, DLSS and XeSS are each told when the scene holds light past white. SGSR 2 has no such input; its Lanczos taps and neighbourhood clamp run on raw wide values in half precision. MetalFX temporal is made with auto exposure off and no HDR hint.
- Evidence:
  - sgsr2.hlsl:106, 173 and 187 (Lanczos weight, box clamp, history clamp on min16float colour); no wide or HDR parameter in sgsr2_vk.cpp or sgsr2_mtl.mm.
  - metalfx_mtl.mm:136-138 (`autoExposureEnabled = NO`, "The scene is display values: nothing to expose").
  - The others: temporal_upscaler.cpp:52, xess_upscaler.cpp:40-41, streamline_bridge.cpp:466.
- Why and fix: Temporal filters on HDR input ring and flicker around highlights unless the colour is compressed first. Apply a reversible tone map before the filter (for example `c / (1 + max(c))`) and its inverse after, in SGSR 2's shader. For MetalFX, do the same around the scaler, or keep it on display values when the scene is wide.
- Confidence: suspected, needs a run: enhanced lighting on, HDR output on, SGSR 2 performance mode on Android; look for dark rims and flicker on sun glints and coins, then the same with FSR 3 on PC.

### R13.3 Round 11 item 2 remainder: effects, gravity, view groups, shadow drawers, scene objects, NameObj

Read completely, all under native/petari/src/Game:

| Files | How |
|---|---|
| Effect/MultiEmitterCallBack.cpp (232), MultiEmitterParticleCallBack.cpp (5), SpinPullParticleCallBack.cpp (22), EffectSystemUtil.cpp (300) | read; MultiEmitterCallBack compared with Petari's |
| Gravity/ConeGravity.cpp (171), CubeGravity.cpp (362), DiskTorusGravity.cpp (109), ParallelGravity, GlobalGravityObj, GravityCreator.cpp (340), PlanetGravityManager.cpp (95), GravityInfo.cpp | Cone, Cube, DiskTorus, PlanetGravityManager and the end of GravityCreator read; all eight diffed against Petari (`diff -w`, names and types ignored) |
| LiveActor/ViewGroupCtrl.cpp (130), with LodCtrl.cpp 1-140 for the flags it hands out | read and diffed against Petari |
| LiveActor/ShadowSurfaceBox, ShadowSurfaceCircle, ShadowSurfaceOval, ShadowVolumeCylinder, ShadowVolumeFlatModel, ShadowVolumeLine, ShadowVolumeModel, ShadowVolumeOval, ShadowVolumeOvalPole, ShadowVolumeSphere (.cpp) | read |
| Scene/SceneObjHolder.cpp (401) | read; the 114 SceneObj ids in the header checked against the 114 cases of newEachObj (inline count): none falls to `default: return nullptr` |
| NameObj/NameObjAdaptor.cpp, NameObjFinder.cpp, NameObjRegister.cpp | read |
| NameObj/NameObjFactory.cpp (4892) | 1-120 and 4780-4892 read (the creators, the index, the lookups); the table (1,186 rows) checked by inline count: no row has a null creator, no name is listed twice (case folded) |

What matches the console code, so no finding: the cube, cone, disk-torus and parallel gravity maths (names only differ; the cube's face bits in GravityCreator map to the same 1, 2, 4, 8, 16, 32 as Petari's if-chain); PlanetGravityManager's search order and sum (it now grows past 128 and no longer stores GravityInfo::mLargestPriority, which nothing reads); the MultiEmitter follow logic. The port's LodCtrl has two levels (near model, far "Low" model) and no camera-distance switch; Petari's had three levels and switched by distance when no view cube said otherwise. ViewGroupCtrl therefore drops the "middle" cube (Obj_arg1 2). That is a design choice stated in LodCtrl.cpp:23-27, not a fault: modern hardware draws the near model at any distance.

#### P-177 A view-group cube with a negative group number writes before the start of the group table
- Severity: crash
- **What:** ViewGroupCtrl::update indexes mViewGroupData by the cube's Obj_arg0 with no lower bound, so a cube placed with Obj_arg0 -1 (the placement default) writes one entry before the heap block every step the player is inside it.
- Evidence: LiveActor/ViewGroupCtrl.cpp:69-73 (`&mViewGroupData[area->mObjArg0]`, then three bool writes); :107-112 (startInitViewGroupTable only raises mMaxViewGroupId from Obj_arg0, so a negative one is never caught); :115 (the table is `new ViewGroupCtrlDataEntry[mMaxViewGroupId + 1]`). Petari's ViewGroupCtrl.cpp has the same unchecked index, so shipped data presumably never does this; a mod's stage or a hand-edited placement can.
- Why and fix: in startInitViewGroupTable, skip (and log once) any cube whose Obj_arg0 is below 0; in update, skip those cubes too. Cost: one compare per cube.
- Confidence: verified in code that nothing checks the value; whether any shipped stage has such a cube needs a run: a debug log in startInitViewGroupTable printing every cube with Obj_arg0 < 0, over a load of every galaxy (the FactoryGalaxy-style direct start for each stage).

#### P-178 A flat-model shadow hands the renderer joint matrices it never wrote
- Severity: wrong behaviour
- **What:** ShadowVolumeFlatModel takes room for every joint of its model from the frame arena, fills only the root and the first child, and draws with all of them; any third joint gets whatever bytes were left in the arena page.
- Evidence: LiveActor/ShadowVolumeFlatModel.cpp:31-35 (only the root and its first child are found), :60-63 (`Frame::arena(getJointCount() * sizeof(Mtx), 16)`, two memcpy, `setModelJoints`); Render/FrameList.cpp:539-560 (arena pages are reused and not cleared); Render/TypedDraw.cpp:230-236, :252-254 (the draw takes the whole joint array). Callers: Util/ActorShadowUtil.cpp:125-144 and the models SpinDriverShadow, SuperSpinDriverShadow, RainCloudVolume, SnakeheadShadow, SnakeheadSmallShadow, SkeletalFishGuardShadow, ShadowVolumeOval (Syati), TripodBossLeg's and SkeletalFishBoss's named shadows.
- Why and fix: with a two-joint model this is harmless; with three or more, the extra vertices land anywhere and the stencil shadow shows a spike or a hole. Write identity, or the root matrix, into every joint row first (or check at load that the model has two joints and refuse otherwise).
- Confidence: verified in code that only two rows are written; the joint counts are disc data, so whether any of these models has a third joint needs a run: a debug log in initJointDrawMtxResister printing `mModelData->getJointCount()` for each name, over the galaxies that use them (Gateway's spin drivers, Gusty Garden, Drip Drop, Bigmouth, Melty Monster).

#### P-179 Effects marked as lit by their area never take the area's light
- Severity: wrong behaviour
- **What:** every effect row with a LightAffectValue is tinted by a fixed white "light", so those effects keep their own colours in dark caves, at night and under coloured light areas.
- Evidence: Effect/MultiEmitterCallBack.cpp:18-22 (`getEffectLight` returns 255, 255, 255 and ignores the emitter), :217-232 (setColor mixes that white by the affect value); EffectSystemUtil.cpp:63-65 (the value is read from the effect table); Petari's MultiEmitterCallBack.cpp:177-181 (effectLight is an empty stub there, so Petari does not tell what the console fed).
- Why and fix: the comment at :18-19 names the source: the blended ambient of the host actor's ActorLightCtrl. Pass it in (the callback's MultiEmitter knows its host through the EffectKeeper) and use it in getEffectLight. The behaviour reference is the console's MultiEmitterCallBack::effectLight in the game's code, since Petari has no body for it.
- Confidence: verified in code that the light is always white; what the console did needs the effectLight assembly read, then a run: Ghostly Galaxy or Bowser's Dark Matter Plant with effects that have a LightAffectValue (the table names them), compared with a console capture.

#### P-180 A wire gravity has no point at the rail's end
- Severity: console-era remnant
- **What:** WireGravityCreator places numPointsInBetween + 1 points from the rail's start at intervals of length / (numPointsInBetween + 1), so the last point is one interval short of the rail's end and the final stretch of wire has no segment of its own.
- Evidence: Gravity/GravityCreator.cpp:314-336 (the loop adds `numPoints - 1` points; the comment at :324 says the last slot stays empty); WireGravity.cpp:7-21 (segments run between added points only). Petari's GravityCreator.cpp:374-392 is the same.
- Why and fix: with the default 20, the last 1/21 of a wire pulls toward its last point instead of across the wire. Add the rail's end as the final point (`mRailRider->setCoord(totalLength)` then addPoint). This changes gameplay near wire ends, so it is the owner's call under the "console quirks are not sacred" rule.
- Confidence: verified in code (both trees).

#### P-181 Shadow shapes are rebuilt on the CPU every draw, and the oval surface shadow draws its model three times
- Severity: performance
- **What:** the line, surface-box and surface-circle shadows write their vertices through TypedDraw for every draw, the circle with a sine and cosine per vertex for a shape that never changes; ShadowSurfaceOval draws the sphere model three times at scales 1, 0.9 and 0.81 to fake a soft edge.
- Evidence: LiveActor/ShadowVolumeLine.cpp:90-112 (two quads and a 10-vertex strip per pass, two passes per shadow); ShadowSurfaceBox.cpp:72-89 (three quads); ShadowSurfaceCircle.cpp:72-74 (`MR::cos(angle), MR::sin(angle)` per vertex, every draw); ShadowSurfaceOval.cpp:58-65 (three drawModelFile calls).
- Why and fix: this is the P-154 shape. Register a unit circle fan, a unit box and the line volume as meshes once and draw them with a world matrix (the line's eight corners become a matrix per end). Replace the oval's three stacked draws with one draw whose pixel program fades alpha toward the edge. Cost on a phone: a few hundred vertex writes and two extra draws per visible shadow per frame.
- Confidence: verified in code; the gain needs a run: Gateway Galaxy start with many surface shadows on the water, frame time with the shapes as meshes against as now.

#### P-182 View groups special-case one galaxy by name
- Severity: maintainability
- **What:** whether a hiding view cube under a story switch hides its planets is decided by comparing the stage name with "HeavensDoorGalaxy".
- Evidence: LiveActor/ViewGroupCtrl.cpp:103-105 (`mPlanetsFromStart = MR::isEqualStageName("HeavensDoorGalaxy")`), :78-81 (the hiding cube is skipped while its appear switch is valid).
- Why and fix: a stage-name test is invisible to anyone reading the stage data and silently does nothing for a mod's stage built the same way. Make it a rule on the cube itself (a cube with an appear switch does not hide until that switch is on) and apply it to every stage, after checking with the owner that Gateway is the only stage with such a cube.
- Confidence: verified in code; the "only stage" claim needs a run: a debug log in startInitViewGroupTable of every hiding cube (Obj_arg1 4) with a valid appear switch, over a load of every galaxy.

#### P-183 The creator index is built lazily with no lock, and NameObjAdaptor leaks a replaced functor
- Severity: maintainability
- **What:** NameObjFactory builds its name index on the first lookup through a plain bool, and NameObjAdaptor::connectTo* overwrite a functor pointer without deleting the old one.
- Evidence: NameObj/NameObjFactory.cpp:4805-4838 (static table and `sCreateIndexBuilt`), :4878-4881 (first caller builds); callers on the scene-load path (Scene/StageDataHolder.cpp:412, PlacementInfoOrdered.cpp:21) and in the mod loader (System/LumaClasses.cpp:299-303, LumaMake.cpp:303, LumaRefs.cpp:147, :166). NameObj/NameObjAdaptor.cpp:40-50.
- Why and fix: if the mod loader and a scene load ever run on two threads before the first lookup, one can read a half-built index and get "no creator" for a valid name. Build the index at static initialisation or under std::call_once. In NameObjAdaptor, delete the previous functor before cloning the new one.
- Confidence: verified in code; whether two threads reach the first lookup together needs a run: a debug assertion in getName2CreateFunc that the calling thread is the one that built the index, over a start with mods enabled.

Other notes from this item, no finding:
- ShadowVolumeSphere and ShadowVolumeCylinder take only mScale.x from a host that follows scale (ShadowVolumeCylinder.cpp:36-39); Petari's are the same, and an unevenly scaled host is rare.
- SceneObjHolder::create calls initWithoutIter on whatever newEachObj returns (:128-129); every current id has a case, so the null return is unreachable today. A new id without a case would crash there, not at the switch.
- NameObjRegister::add has no null check on mHolder (:7-9), as in Petari.
- NameObjFactoryStubs.hpp holds only include lines; the name says stubs.

<!-- END -->
