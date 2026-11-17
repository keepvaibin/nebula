# Modding engine audit (read-only), 2026-11-16

Trees read: D:\NebulaWork\MarioGalaxyRecomp\Nebula-Clean (working tree as is), D:\NebulaWork\lumalink-work\lumalink-sweep.
Nothing was built, run or written in either tree. Paths below are relative to Nebula-Clean unless they start with
crates/lumascript, lumakit or crates/lumalink (lumalink-sweep).

## Coverage

| Area | Read completely | Read partly (lines) | Not read |
|---|---|---|---|
| 1 Kernel | native/include/luma_hook.h; LumaHook.cpp, LumaHook.hpp; LumaGuard.cpp; native/include/luma_guard.h; native/host/luma_guard.cpp; LumaLoader.cpp; native/include/luma.h; runtime/src/mod_plugins.cpp; LumaCore.cpp; LumaServices.cpp; LumaClasses.cpp; LumaNumbers.cpp; LumaMake.cpp; crates/lumascript/src/emit/library.rs | LumaTypes.cpp 500-880; LumaModLog.cpp 440-860 (rest by outline); GameSystem.cpp 55-294; host_main.cpp 3060-3119; tools/lumacore/gen_types.py 1300-1409; crates/lumascript/rt/lumart.c 1-143, 3743-3765 | - (round 7 read completely: LumaTypes.cpp, LumaModLog.cpp, luma_types.h, luma_modlog.h, gen_classes.py) |
| 2 Services (round 4) | runtime/src/luma_input.cpp (1753); native/include/luma_input.h (311); native/petari/src/Game/System/LumaDrawView.cpp (907); native/include/luma_draw_view.h (155); the uncommitted diffs of LumaPlayer.cpp, luma_player.h, MarioActor.cpp, MarioActorDraw.cpp, MarioActorHand.cpp, MarioActorShadow.cpp, MarioAccess.cpp, MarioHolder.cpp, host_input.cpp, live_controls_engine.cpp/.h, luma_input_service.h, pointer_control.cpp/.h, host_window_sdl.cpp, native_game_host_sdl.cpp, sdl_host.h, host_window_apple.mm, host_window_win32.cpp, host_window.h, GameActivity.java; runtime/src/input/raw_mouse_win32.cpp; raw_mouse.h | live_controls_engine.cpp 150-570; host_input.cpp 985-1220; host_main.cpp 1080-1135, 3595-3790, 3890-3925; GameSystem.cpp 180-345; GameScene.cpp 300-348, 493-505; SceneExecutor.cpp 1-80, 95-111; PlayerInputs.cpp 60-200; GameSystemObjHolder.cpp 55-80; NebulaFrameClock.cpp 1-91; LumaTime.cpp 315-345, 977-1074; MirrorCamera.cpp 37-86; CameraUtil.cpp 140-220; MarioActor.cpp 1672-1690, 2805-2845; MarioActorDraw.cpp 750-775; MarioActorSpecialDraw.cpp 160-200, 692-712; MarioActorHand.cpp 169-200; MarioActorGameOver.cpp 72-80; LumaPlayer.cpp 100-125, 196-218; JointUtil.cpp 23-58; host_window_apple.mm 570-645, 1028-1150; host_window_sdl.cpp 985-1012, 1600-1640; TouchControlsView.java 578-600; GameActivity.java 272-300, 806-816; frame_interpolation.h 1-60 | LumaDraw.cpp, LumaDrawInst.cpp, LumaDrawLight.cpp, LumaDrawPost.cpp and their headers; LumaPlayer.cpp outside set_drawn; items 3 to 8 of round 4 (LumaSave 190-1551, LumaSaveChunk, luma_audio, LumaGameSound, luma_fs, luma_assets, luma_game_store, luma_net rest, luma_net_reach, luma_system*, luma_services_host, luma_capture, luma_mods, LumaAnim, LumaMovie, LumaContentFx, LumaContentModel, LumaFont, LumaUi) |
| 2 Services (round 5) | LumaDraw.cpp (1934); LumaDrawInst.cpp (715); LumaDrawLight.cpp (384); LumaDrawPost.cpp (450); luma_draw.h (209); luma_draw_inst.h (85); luma_draw_light.h (83); luma_draw_post.h (81); luma_draw_native.h (124); LumaPlayer.cpp (362); LumaSave.cpp (1551); LumaSaveChunk.cpp (558); luma_save.h (115); runtime/src/luma_audio.cpp (2012); luma_audio.h (193); LumaGameSound.cpp (326); runtime/src/luma_fs.cpp (1387); luma_fs.h (126); runtime/src/luma_assets.cpp (1206); luma_assets.h (136); runtime/src/luma_game_store.cpp (112) | GameSystem.cpp 60-283; SaveDataHandler.cpp 401-441 plus a grep; native_audio.cpp 235-290; draw_walker.cpp 2095-2172; host_main.cpp 3295-3319; CMakeLists.txt 585-609 plus a grep; CameraUtil.cpp 214-220; PlayerInputUtil.cpp 35-41; LumaDrawNative.cpp 100-179 | item 5: luma_net.cpp (rest), luma_net_reach.cpp, luma_net.h; item 6: luma_system.cpp, luma_system_win32.cpp, luma_system_apple.mm, luma_system_sdl.cpp (rest), luma_services_host.cpp, luma_capture.cpp, runtime/src/luma_mods.cpp, luma_system.h, luma_capture.h, luma_mods.h, luma_host.h, luma_service_provider.h, luma_services.h; item 7: LumaAnim.cpp, LumaMovie.cpp, LumaContentFx.cpp, LumaContentModel.cpp, LumaFont.cpp, the rest of LumaUi.cpp and LumaTime.cpp, their headers |
| 2 Services (round 6) | LumaAnim.cpp (901); LumaMovie.cpp (568); LumaContentFx.cpp (962); LumaContentModel.cpp (963); LumaFont.cpp (381); luma_anim.h (124); luma_movie.h (55); luma_content.h (36); luma_content_fx.h (60); luma_content_model.h (84); luma_text_tags.h (238); runtime/src/luma_system.cpp (1175); luma_system_win32.cpp (602); luma_system_apple.mm (335); luma_system_sdl.cpp (561, the rest of it); luma_system.h (148); luma_capture.cpp (1080); luma_capture.h (103); runtime/src/luma_mods.cpp (1748); luma_mods.h (174); luma_host.h (87); luma_services.h (86); luma_services_facts.h (57); luma_refs.h (34); luma_harness.h (42); luma_net.cpp (the rest: 1-245, 706-1106, 1557-1595, 1935-2005, 2245-3237, 3947-4475, 4691-5169, so all 5169 over rounds 2, 3, 6); luma_net_reach.cpp (201); luma_net.h (332). LumaTime.cpp, LumaUi.cpp, luma_time.h, luma_ui.h, luma_service_provider.h and luma_services_host.cpp count from round 1 (not read again) | Table.hpp 80-100; GameSystem.cpp 273-278; LumaLoader.cpp 440-455, 1050-1065; LumaServices.cpp 620-635, 695-755; luma_service_provider.h 40-96 (lifecycle comments); luma.cpp 255-272 | none of Area 2 |
| 2 Services (round 3 additions) | - | luma_net.cpp 246-705, 1107-1556, 2006-2244, 3527-3946, 4476-4690 (plus round 2's 1596-1934, 3238-3525); luma_capture.cpp 820-944; luma_mods.cpp 1-12, 700-775, 1030-1189; luma_system.cpp 605-625, 680-990 in parts; platform/sdl/luma_system_sdl.cpp 60-334; every entry function's signature line in all 26 provider files (M-029); every `->call(` and every thread start in runtime/src and platform/*/luma_system_* | luma_net.cpp 1-245, 706-1106, 1557-1595, 1935-2005, 2245-3237, 3947-4475, 4691-5169; luma_net_reach.cpp; luma_input.cpp, luma_audio.cpp, luma_fs.cpp, luma_assets.cpp, luma_system_win32.cpp, luma_services_host.cpp, luma_game_store.cpp, luma_system_apple.mm bodies; LumaSave.cpp 190-1551, LumaSaveChunk.cpp, LumaAnim.cpp, LumaMovie.cpp, LumaContentFx.cpp, LumaContentModel.cpp, LumaDraw*.cpp, LumaFont.cpp, LumaGameSound.cpp bodies; the service headers line by line (no LumaInput*.cpp exists in native/petari/src/Game/System) |
| 2 Services | luma_service_provider.h; LumaDrawNative.cpp; luma_draw_native.h; runtime/src/luma_services_host.cpp; luma_system_platform.h; LumaTime.cpp, luma_time.h; LumaUi.cpp, luma_ui.h; LumaPlayer.cpp, luma_player.h; luma_anim.h; luma_save.h | luma_audio.cpp 1180-1380; LumaDraw.cpp 190-270, 640-880; luma_fs.cpp 60-389; luma_net.cpp 180-260, 1596-1934, 3238-3525, 4476-4605; luma_input.cpp 445-485, 1296-1405; luma_input.h 240-311; luma_capture.cpp 795-812; LumaContentModel.cpp 668-707; LumaSave.cpp 1-190; LumaAnim.cpp by grep; every event delivery site by grep; entry checks counted by grep in all 21 service files | the rest of luma_net, luma_input, luma_audio, luma_fs, luma_capture, LumaSave, LumaAnim; luma_mods.cpp, luma_assets.cpp, luma_system.cpp, LumaMovie.cpp, LumaContentFx.cpp, LumaDrawView/Inst/Light/Post.cpp in full; the service headers not named in the first two columns |
| 3 Call safety | round 7: tools/lumacore/instrument.py (2005), gen_types.py (1594), self_description.py (182), gen_lumacore.py (69), gen_classes.py (item 2) | MarioAccess.cpp 15-30, 282-290, 615-640; MarioHolder.cpp 17-27; PlayerUtil.cpp 70-76, 392-394, 508-510; SceneUtil.cpp 36-38, 359-363; CameraUtil.cpp 25-39 plus a grep; SceneObjHolder.cpp 136-140, 384-392; SceneObjHolder.hpp 163-176; LumaMarks.hpp 21-27; luma_guard.h 1-70; lumascript game.rs 866-950 and check/body.rs given_* by grep; smg_game.luma-self.json keys counted over all 27095 rows by script | - (closed) |
| 4 Lifecycle, events | native/include/nebula_mod_api.h | NebulaMods.hpp 9-28; NebulaMods.cpp 553-586; mod_plugins.cpp 93-103, 350-410; docs/modding/reference/events.md 1-60; crates/lumascript/src/parse.rs 760-799 | - (round 7: every raise site of the 29 events read with its function, and the dispatch path; item 3) |
| 5 Compiler | round 8: diag.rs; rt/lumart.h; emit/task.rs; emit/game.rs. round 9: check.rs; check/body.rs; check/body/expr.rs, call.rs, pat.rs, service.rs; ctype.rs. round 10: emit.rs; emit/expr.rs; emit/flow.rs; rt/lumart.c (with round 9's 2930-3080); emit/closure.rs, states.rs, class.rs, json.rs, library.rs, cside.rs; lumi.rs; lib.rs | game.rs 1-130, 866-950, 989-1035; parse.rs 1-349, 1320-1362 and the depth lines; lex.rs 262-281 and the position lines; emit/service.rs 1-60, 200-320, 800-827, 985-1010, 1076-1105, 1680-1710 and every keep/callback line; svc.rs 1095-1114, 1295-1320; native luma_hook.h, LumaHook.cpp, LumaGuard.cpp, LumaTime.cpp 407-420, 1055-1125, LumaServices.cpp 238-249, 398-421, LumaLoader.cpp 1290-1318 for the cost path; LumaLinkText.tsv S0048, S0053, S0067, S0083 rows; tests: every file's head and test names, feature keywords counted; emit_small.rs 2080-2130 | game.rs (130-866, 950-1168 less 989-1035); lex.rs and parse.rs beyond the ranges; ast.rs; fmt.rs; emit/service.rs beyond the ranges; svc.rs beyond the ranges |
| 6 LumaKit | round 16: docs/modding/reference/lumakit/camera.md (every sentence, against camera.lum and LumaPlayer.cpp); lumakit/luma.json; round 3: all 22 modules every line (actor, area, camera, cutscene, gravity, hud, item, menu, net/kit, player/character, player/form, player/state, pointer, progress, projectile, replay, ride, screen, shop, stage, talk, ui/kit: 11,587 lines); all 11 lumakit/code/lumakit.kit/*/1.json (1,707 lines) | docs/modding/concepts/lumakit.md 58-68; reference pages compared through their generator's source comments (gen_kit.py builds them from the module headers) and by script for `pub` signatures (round 2), not line by line; round 15: all 22 module pages' `pub` lines compared both ways with the sources (0 missing, 0 undocumented), item.md and camera.md by grep for first-frame warnings | the reference pages' prose (behaviour, defaults) line by line |
| 7 LumaLink | round 11: lumalink-core build.rs 1-1018 (all but its tests); round 12: lumalink-core dev.rs 1-218 | crates/lumalink/src/main.rs 458-547, 715-730, 926-1010, 1165-1190 and its names/sdk lines by grep; lumalink-core/src/update.rs by grep plus 580-662; jobs.rs 8-66, 109-173, 280-388, 1415-1500; add.rs 330-365, 585-660; luma_file.rs 1-330; luma-validate rules/layers.rs 120-171, rules/manifest.rs 255-271, 1063-1071, semver.rs by grep; round 12: update.rs 1-18, 33, 117-183; lumalink-setup install.rs 36, 343-367, 370-503, 673-841; Nebula mod_plugins.cpp 770-1037, host_main.cpp 4204-4214; round 13: installed.rs 1-57; jobs.rs 848-1135; gecko.rs 1-60; plan.rs 206-249, 382-402, 439-470, 495-646; convert.rs 1-45, 483; riivolution.rs 1-25 and its path lines; texture_pack.rs 1-20, 228-231, 327-391; update.rs 1-16, 263-303 and its error lines; lumalink main.rs 56-112, 629, 783, 851-865 and its installed() call sites; lumalink-gui main.rs 119-134, 255-272, 570-600, updates.rs 40-121, error-dropping lines by grep; lumalink-setup main.rs 1-90, 161, 262, 426-440, install.rs 322-328, 1101-1206 | NOT CLOSED. Not read whole: lumalink-core new, add, editor (and query, complete), lsp, source, character, object_table, player_constants, known_patches, inspect, files_check, effects_check, layout, region_map, table_patch, script, numbers_code, vscode, game_functions, ppc, the rest of plan, convert, riivolution and update; lumalink main.rs beyond the ranges, cli_text.rs; lumalink-gui screen, chrome, look, words, sky, shot, picture, icon; lumalink-setup main.rs and install.rs beyond the ranges |
| 8 Validator, import | docs/modding/concepts/permissions.md; crates/luma-validate/src/zip.rs; mod_consent.cpp; mod_start_check.cpp; round 11: runtime/src/mod_check.cpp; luma-validate rules/container.rs | mod_import.cpp 60-831; crates/luma-validate/src/rules/code.rs 200-310, rules/mods.rs 1-60, rules/manifest.rs 505-525; binary.rs 1-30; mods.cpp 1030-1043; LumaLoader.cpp 1047-1064 (apiService); round 11: mod_log.cpp 627-829, 983-1014; native_settings.cpp 3196-3275; nebula-import validate_c.rs 380-579, cook/gltf.rs 80-280, 1575-1605, 1868-1881 and its refusal list, cook/modern_audio.rs 270-314 and header reads, cook/thp.rs 60-90, cook/beside.rs 86-92, Cargo.toml; validator-rules.md code table compared by script; round 12: host_main.cpp 5113-5124, 5541-5553; mods.cpp 86, 1966; cook/flac.rs 119-175, 194, 255-290, 396-416; sound_stream.rs 95-106; sound_effect.rs 153, 455-466; modern_audio.rs 20-21, 88; sound.rs 955-972; audio_data.rs 290-302; layout_json.rs 25, 126, 146-156, 398-410, 436-448, 512-520, 650-662; post_shader.rs 1-25, 60-64, 230-290; surface_shader.rs 1-25; mod_shader_compile_d3d12.cpp 98; catch_unwind sites by grep; round 13: mod_shader_compile_d3d12.cpp 1-152, mtl/mod_shader_compile_mtl.cpp 1-41, vk/mod_shader_compile_vk.cpp 1-44 (all three whole); vk/shader_compile_vk.cpp 1-30 and by grep; d3d11/compile_stage_d3d11.cpp 200-243; mod_post.cpp 178-255; mod_shader_build.cpp 1-30, 150-205; mod_check.cpp 143-236; every allocation site (vec!/with_capacity/resize) and every while/loop in sound, sound_stream, sound_effect, sound_sequence, font_cook, movie_cook, ui_layout, layout_json, layout_anim, texture, gltf, post_shader, surface_shader, audio_data, thp, modern_audio, flac, by grep with each site read; sound.rs 1-30, 1318-1460; sound_effect.rs 95-176, 222-262, 440-470; sound_stream.rs 85-110; movie_cook.rs 1-40, 155-200, 245-312, 340-360; ui_layout.rs 1-40, 296-312, 510-522, 905-975; texture.rs 360-420, 615-628, 795-878; gltf.rs 396-412, 1660-1680, 1808-1816, 2826-2848; archive.rs 25-75, 140-196; mod.rs 226-380; luma-validate schema.rs 1-30, 118-153, 278-294, 668-705; saved.rs 1-28, 279-289; rules/mod.rs 95-135; binary.rs loop sites; lib.rs 452-488; every not_checked( site; LumaLoader.cpp 1040-1064; round 14: every allocation, loop and self-call site in cook/ and luma-validate/ by script and grep, each read in context; model_ir.rs 484-490, 852-922, 1028-1050, 1118-1123, 1285-1310; formats.rs 42-67, 90; table.rs 107-121, 186-273, 491-503; overlay_patches.rs 130-142; kcl.rs 70-122, 288-296, 550-575; camera.rs 236-244; ghost.rs 196-204; sound.rs 505-530, 678-687; model_write.rs 1245-1262; patch.rs 520-626; table_keys.rs 50-75; native_data.rs 181-198, 292-301, 423-446, 494-498; particles.rs 204-209, 652-670; clip.rs 100-105; luma-validate rules/patches.rs 70-90, 660-690; KCollision.hpp 30-62, KCollision.cpp init lines; Cargo.toml profiles. Coordinator, round 15: the cook and validator files were covered by a review of every allocation, loop, recursion and index site rather than line by line; that site review stands as done | site review DONE; not read line by line (rounds 13-14). Not read whole: the cook files above, luma-validate json.rs, names.rs, lib.rs, saved.rs, rules manifest/code/patches/layers/mods/kit/after_start; the game-format cooks reachable from mod files (model_ir, model_write, clip, particles, formats, message, table, ghost, camera, native_data, kcl, fur, mii_parts, hit_record); pipelines_mtl.mm; Vulkan device loss |
| 9 Mod manager, UI | mod_import.cpp; mod_start_check.cpp; mod_consent.cpp; native/host/crash_watch.cpp; ModImport.kt; mod_import_jni.cpp; round 6: mod_screens.cpp (79), mod_check.cpp (318) | round 6: mods.cpp 1055-1099; native_settings.cpp 3500-3634, 4515-4574; CMakeLists.txt 200-1000 by search; audit-conf j/tests/safestart 6601-6603 test.json; NebulaText.tsv 480, 939, 940; platform/sdl/crash_log.cpp 60-189; LauncherModel.kt 500-536; crash_report.cpp 165-214; native_settings.cpp 3270-3354; host_main.cpp 300-339, 440-455, 1738-1870, 4905-4990, 5840-5850, 6085-6124; mods.cpp 1075-1090, 1330-1345, 1939-1962 | - (round 7 read completely: mods.cpp, mod_xml.cpp, mod_log.cpp, luma.cpp, native_settings.cpp 2759-4815, GameFiles.java; LauncherModel.kt 113-189; no iOS host in the tree) |
| 10 Docs | permissions.md; round 16: reference/services/anim.md (every sentence, against luma_anim.h) | events.md, lumakit.md, detailed-rules.md 84-92 and 274-281, errors.md 26-27, lumascript.md 31, capture.md by grep, system.md by grep; lumakit reference pages by grep for broken sentences; round 15: all 271 files by mechanical check: 593 lumascript blocks for refused spellings, lumalink-commands flags and commands both ways against main.rs, services pages' calls against native/include/luma_*.h, tutorials' code lines against examples/*/source, S codes against diag.rs, S0800 constructs against pages; hits read (functions-structs-and-enums.md 28, hooks.md 13-27 and 43, tasks.md 15-17, values-and-memory.md 27, lifecycle.md 23-33, c-and-cpp.md 18, compiled-form.md 97, detailed-rules.md 37-81, livesplit-link.md 222-248) | every page line by line: services prose (limits, defaults, error codes) against the service bodies; get-started, concepts and how-to steps against tool output; nerves pages; classes, events, tables, table-keys, test-files against the game |
| 11 Tests, claims | - | D:/NebulaWork/agents/luma/PATH-FORWARD-MODS.md 169-322; board/PROVEN.md 1-100; the 26 test.json listed, one read; audit-conf/luma/{f,fb,g,h,j,k}/tests: every suite's test names listed, `use kit.*` of every test mod by grep, j/tests/safestart/6602-crash/test.json; round 15: one test.json per suite (f/tests/reload/1140, fb/tests/kit-camera, h/tests/save/2261, j/tests/safestart/6601, k/tests/k8-gone), the `proves` tags of every test.json in the old suites and examples (1,082 tags) | each old test's test.json and mod source beyond those; g/k-hostile |

26 findings. Crash: M-001, M-007, M-008, M-011, M-014. Wrong behaviour: M-002, M-003, M-009, M-013, M-021.
Missing feature: M-006, M-012, M-015. Docs wrong: M-004, M-016, M-017, M-018 (also a held-back feature), M-022,
M-024, M-025. Maintainability: M-005, M-010, M-019, M-020, M-023, M-026. M-008 and M-009 have no known trigger in
this tree (see M-018). Areas 9 and 11 (old suites) are unfinished; areas 2, 5, 7 and 8 are partly read.

Round 2 adds 10 findings, M-027 to M-036. Crash: M-029 (the full M-011 list), M-031. Wrong behaviour: M-027, M-030,
M-033. Missing feature: M-032, M-036. Docs wrong: M-034. Maintainability: M-028, M-035. Area 3's "what would have to be
added" is written under Area 3 (round 2). Area 5 (check/ and emit/) and area 10 (reference pages line by line, example
mods) got no new reading in round 2.

Round 3 adds 19 findings, M-037 to M-055, and the exhaustive M-029 list with lines. Crash: M-038, M-049, M-051.
Wrong behaviour: M-037, M-041, M-042, M-043, M-044, M-045, M-046, M-047, M-048, M-050, M-052, M-053, M-054, M-055.
Docs wrong: M-039. Maintainability: M-040. Checks 1 (M-027) and 2 (M-031, M-032) are written under Round 3. Area A
(LumaKit) is read in full; Area B is read in part (see row "2 Services (round 3 additions)").

Round 4 adds 14 findings, M-056 to M-069. Wrong behaviour: M-056, M-057, M-058, M-059, M-060, M-061, M-062, M-063,
M-064, M-065, M-066, M-068. Docs wrong: M-067. Maintainability: M-069 (no mod, no cost). No new crash: every new
entry that reads a mod pointer is already on M-029's list (see "M-029, round 4"). Items 1 and 2 of round 4 are done for luma_input and draw.view with the set_drawn path;
the rest of item 2 (LumaDraw, LumaDrawInst, LumaDrawLight, LumaDrawPost) and items 3 to 8 are not started. The new
code (look, main_rig, set_drawn) has never been run, so every "suspected" below names the run that settles it.

Round 5 adds 17 findings, M-070 to M-086, and the Area 2 closing table (end of the file). Crash: M-076 (export_file
during the boot), M-084 (suspected, memory). Wrong behaviour: M-070, M-071, M-072, M-077, M-080, M-083, M-085
(suspected), M-086. Suspected wrong behaviour: M-078, M-081. Missing feature: M-074. Docs wrong: M-073, M-075, M-079,
M-082. M-029 gains three entries (LumaDraw.cpp 755, 986, 1381). Items 1 to 4 are read completely; items 5, 6 and 7 are
not started (row "2 Services (round 5)", last column).

Round 6 adds 14 findings, M-087 to M-100 (M-100 is Area 9's safe-start notice), and closes Area 2: every row of the closing table reads "read completely:
yes". Suspected crash: M-088 (cooked-table cells), M-090 (clipboard pictures through stb_image), M-092 and M-098
(detached threads at exit). Wrong behaviour: M-087, M-089, M-093, M-095, M-096 (also docs wrong), M-097. Missing
feature: M-091 (also docs wrong), M-099. Docs wrong: M-094 (also missing feature), M-100 (tests and capability text).
Area 9 is started: mod_screens.cpp and mod_check.cpp read completely, the safe-start question answered. M-029 needs no new entry: the round 3
list is complete for every service read this round. Round 5's closing table said LumaTime.cpp and LumaUi.cpp were not
read completely; they were, in round 1, and the rows are corrected.

## Area 1: kernel

### M-001 crash: a fault inside a library mod's code (LumaKit) called by another mod is never resumed; the game ends
- Evidence:
  - A kind 6 import binds the raw export address, no boundary: native/petari/src/Game/System/LumaLoader.cpp:1870-1871
    `const bool bound = pExport != nullptr && sameSig && !layoutsDiffer(pInfo, pOwner);`
    `*static_cast<const void**>(rImport.slot) = bound ? pExport->address : nullptr;`
  - The compiler calls it directly through that slot: lumalink-sweep crates/lumascript/src/emit/library.rs:802
    `fx.fail_if(&format!("((int32_t (*)({}))(const void*){slot})({}) != 0", ...))`
  - The guard's frame carries only the calling mod's code range: LumaGuard.cpp:472 `modCode(pMod, &base, &frame.code_begin, &frame.code_end);`
  - The host resumes only a pc inside that range: native/host/luma_guard.cpp:101-103
    `if (frame->code_begin == 0 || !(pc >= frame->code_begin && pc < frame->code_end)) { return "the fault is outside the mod's code ..."`
    and on Windows the frame walk also stops at any other image: luma_guard.cpp:168-170.
  - LumaKit is a library mod: lumalink-sweep lumakit/luma.json `"library": true`.
- Why: mod A (hook or start) calls `lumakit.actor.something(x)`; LumaKit code reads through a stale or null pointer. The
  fault pc is in LumaKit's image, the innermost frame is A's, so `why_not_here` refuses, the handler passes the fault on,
  and the game ends with a crash report naming A. Every LumaKit-based mod shares this.
- Confidence: verified in code (path). A run would show it: a test mod whose start calls a LumaKit export that faults.

### M-002 wrong behaviour: the hook-chain grace covers hooks only; a mod with no published hook is freed at once, with no grace, while its service callbacks may still be held by providers
- Evidence: LumaLoader.cpp:1411-1416
  `luma::guard::modEnd(pMod); if (!pMod->published) { finishMod(pMod); return; }`
  `published` is set only by a hook (LumaLoader.cpp:315, 2016). finishMod closes the library (LumaLoader.cpp:1380-1382)
  right after `luma::services::modEnd` (1378).
- Why: safe only if every provider's mod_gone/mod_end synchronously stops calling into the mod on every thread before
  returning. The kernel's own event path is game-thread only (LumaServices.cpp:398-421), so for kernel-delivered events
  this holds; for host services that run a mod's function on another thread (audio, capture, net) it depends on each
  provider. See area 2 for which providers call mod code off the game thread.
- Confidence: suspected, needs per-provider check (area 2) or a run: a mod with no hooks that registers an audio
  callback and is taken out while the callback runs.

### M-003 wrong behaviour: a block header overwrite by a mod (buffer underflow) stops the whole game instead of turning the mod off
- Evidence: LumaLoader.cpp:907-910 (`free`) and 940-943 (`freeAll` at take-out):
  `NEBULA_PANIC("LumaCore: %s's block at %p was written over in front of its bytes", ...)`
- Why: the header (Block, 32 bytes) sits right before the mod's bytes; a mod that writes index -1 of its own buffer
  makes its next `free` (or its take-out) panic the game. The guard and the Mods page are bypassed. freeAll runs at
  every take-out, so even a mod that never frees ends the game when the player turns it off.
- Confidence: verified in code.

### M-004 docs wrong: luma.h says an out-of-memory `new` "stops the game"; inside any boundary the trap is a mod fault the guard resumes
- Evidence: luma.h:355 `luma.h's new then stops the game`, luma.h:703 `A throwing new the system has no memory for logs and
  stops the game`; the code is `__builtin_trap()` in the mod's own image (luma.h:751). The host resumes SIGILL /
  EXCEPTION_ILLEGAL_INSTRUCTION inside the mod's code (luma_guard.cpp:353-354, 529-530).
- Why: a trap in a hook or start is resumed and the mod is turned off; the game goes on. The header promises otherwise.
- Confidence: verified in code.

### M-005 maintainability: kernel entry points that take a mod-supplied `LumaFunction*` dereference it without checking it is a row
- Evidence: LumaLoader.cpp:307-313 (`apiHook` passes `asRow(pFunction)` to addHook), LumaHook.cpp:353-355 reads
  `pFunction->hooked` and `resultKind` calls `pFunction->invoke(kAskKind, ...)` (LumaHook.cpp:331); apiCallMethod
  invokes with a null `result` unchecked (LumaLoader.cpp:846).
- Why: a LumaScript mod never passes a bad row, but a C mod (luma.h is public) passing a stale or wrong pointer faults in
  the kernel, which the guard never resumes (pc outside the mod). A range check `rowsBegin() <= p < rowsEnd()` is cheap.
  call_method with `result == nullptr` for a non-void virtual writes through null in Result::put (luma_hook.h:75).
- Confidence: verified in code.

### M-006 missing feature: Windows on ARM64 has no guard in the game module and no build; mods still ship windows-arm64 code
- Evidence: LumaGuard.cpp:250-251 `#error "LumaCore's guard has no call for this processor or system (luma_guard.h; Windows on ARM64)"`;
  luma_guard.cpp:386-388 `// Windows on ARM64: no handler yet`; website/content/pages/download.json:50
  "Windows on ARM runs the x64 version."; lumakit/luma.json and docs/modding/examples/*/luma.json list "windows-arm64".
- Why: a windows-arm64 mod library can never load (the game is an x64 process there); LumaLink spends a build on it and
  a reader of the docs is told it is a target.
- Confidence: verified in code.

### M-007 crash: own_class, as_class, is_a, class_data, overrider and call_method read the object's vtable in the kernel; a stale or wrong object pointer ends the game, though the comment says the guard covers it
- Evidence: LumaTypes.cpp:537-538 `What a mod passes C cannot check further without reading it; a read that faults is
  the guard's (1.6), under which mod code runs.` The reads are kernel code: LumaTypes.cpp:549 (`memcpy(&pVtable, pObject, ...)`),
  554 (`pVtable[-1]`, Windows), 571-573 (`pVtable - 2`, `pVtable[-1]`, Itanium), and typeInfoName 853/859 reads the
  type_info. The host resumes only a pc inside the mod's image (luma_guard.cpp:101-103), so a fault here is passed on.
  luma.h:247-249 promises -3 "not a game object the kernel can read" as an answer.
- Why: a mod keeps an actor pointer across a scene change (the arena is reused or unmapped) and asks `own_class(p)` or
  LumaScript's `as`/`is` on it. The first word is any aligned value; `pVtable[-1]` or `pVtable - 2` faults in the game
  module and the game ends. The -3 answer only covers null and misaligned pointers.
- Confidence: verified in code (path). A run: a test mod that calls own_class on an 8-aligned pointer to a block whose
  first word is an aligned non-pointer (for example a zeroed+0x1000 word).

### M-008 crash: an override whose call is still running on another thread when its mod is taken out panics the game in slotOriginal
- No known trigger in this tree: it needs an actor virtual called off the game thread, and none was found (scenes are
  built on the game thread, GameSystemSceneController.cpp:101-143; LumaScript overrides skip off the game thread).
- Evidence: LumaClasses.cpp:1166-1181 (modGone puts back the base table and removes the object's record:
  `*pObject = ... pRow->baseTable; removeAtLocked(at);`); then slotOriginal LumaClasses.cpp:1117-1124:
  `ClassRow* pRow = rowOfTable(pObject); if (pRow == nullptr) { pRow = rowOfObject(pObject); } ... if (pOverride == nullptr || pOverride->code == nullptr) { NEBULA_PANIC(...)`.
  The comment at 1115-1116 says "the base's again (its mod went while the call was in it): the kernel's records say
  the class then", but modGone has just removed those records, so rowOfObject finds nothing.
- Why: an override entered through slotCall on a thread other than the game thread (a C mod's override; LumaScript
  skips off the game thread, lumalink emit/class.rs:213) calls proceed after the game thread ran modGone: kSo's invoke
  calls slotOriginal (tools/lumacore/gen_types.py:1364), which panics. On the game thread this cannot happen (take-outs
  run at the frame's start, LumaLoader.cpp:2108, GameSystem.cpp:270), so the trigger is virtuals called off the game thread.
- Confidence: verified in code (the contradiction); whether the game calls actor virtuals off the game thread was not
  checked. A run: a C mod class overriding a virtual another thread calls, taken out while that call runs.

### M-009 wrong behaviour: LumaClasses.modGone spins on the game thread until every make in progress on any thread ends
- No known trigger in this tree: scenes, and so makes, run on the game thread (GameSystemSceneController.cpp:101-143,
  M-018), where take-out cannot overlap a make.
- Evidence: LumaClasses.cpp:1153-1165 (busy loop on `sMaking` with pause/yield, no limit).
- Why: if a stage is being made on another thread that waits on the game thread, the game hangs. On the game thread a
  make cannot be on the stack at take-out time (take-outs run at the frame's start), so the risk is cross-thread only.
- Confidence: suspected, needs a run: take out a class mod while a scene with its placed objects is loading.

### M-010 maintainability: kMaxClasses (1024) class rows are never reused in a session; dev reloads use them up
- Evidence: LumaClasses.cpp:24 `kMaxClasses = 1024;  // in a session; a row is never reused`, 693-695 refuses past it.
- Why: reloading a mod that defines 8 classes 128 times leaves define_class answering -1 for the rest of the session.
- Confidence: verified in code.

Checked in area 1 and found correct:
- Hook chains are immutable and swapped atomically; retired chains are freed only after the SRCU grace (sEpoch,
  sReaders[2]) and the game thread's own count (LumaHook.cpp, reclaim and graceOver).
- Order: afters run in reverse; skips are refused for kFixed results; noResult handled; the kernel's factory hook at
  order 0x7FFFFFFF runs first. The proceed/wrap fault path unwinds to the guard's frame.
- addHook and removeHook check the game thread; removeHooks and unloadMod are called only on the game thread.
- The guard's assembly saves each ABI's callee-saved registers (win-x64, SysV x64, AArch64) and resumes with the
  stack 512 bytes below the saved frame; the host refuses a fault outside the mod's code or with a game or host frame
  between (luma_guard.cpp:101-103, 138-173).
- Take-outs run only at the frame's start on the game thread (takeOutRequested, then luma::frame, then
  services::frameStart); dev_frame runs on the game thread inside svc_frame_begun (host_main.cpp:3060-3119).
- Reload waits for luma_mod_gone before opening the library again; dev mode hashes the code again on reload.
- start_plugins orders mods by needs and checks each library's code hash before loading it.
- set_number and set_table check bounds and restore the game's values at take-out; const tables are made writable
  in the instrumented copy, so set_table on a const table does not fault.
- make checks the scene's state and that places are finite (LumaMake.cpp).
- The mod log keeps a bound per mod and per session (LumaModLog.cpp, cPerMod and cSession).
- refusedWhileGoing refuses hook, define_class, set_number, set_table, alias_class and make_gravity from a mod being
  taken out.

Note on M-002, settled in area 2: no service calls a mod's function off the game thread except draw.native, and
draw.native's mod_gone waits for a running call before returning (LumaDrawNative.cpp:72-81, 180-187). Every other
provider gives events through LumaServiceKernel.call (luma_service_provider.h:57-60; game module: LumaAnim.cpp:323,
LumaMovie.cpp:143, LumaSave.cpp:887/1089, LumaTime.cpp:419/518, LumaDraw.cpp:1830, LumaUi.cpp:424; host: luma_audio.cpp:568,
luma_mods.cpp:690/952/1081, luma_assets.cpp:943, luma_fs.cpp:661, luma_input.cpp:312/385/535, luma_capture.cpp:868,
luma_system.cpp:523, luma_net.cpp:2797/3701), which is game-thread only. Audio never calls back (luma_audio.h:6-8). So
for services M-002 does not happen. What is left of M-002 is a class override (LumaClasses) running on another thread
in a mod with no hook: `published` stays false, so the library closes at once (same trigger as M-008).

## Area 2: services

### M-011 crash: every pointer a mod gives a service is read in kernel or host code; a bad pointer ends the game, not the mod
- Evidence: the guard resumes only a fault whose pc is in the calling mod's image (native/host/luma_guard.cpp:101-103).
  Example: audio.stream_push reads the mod's `samples` inside the host while holding the mixer's lock:
  runtime/src/luma_audio.cpp:1318-1329 `const std::lock_guard<std::mutex> lock(g_lock); ... const float value = samples[f * channels + c];`.
  `frames` is bounded only by the ring's free space (1324), so a short buffer is read past its end. The same holds for
  every `const char*` name (read to its NUL), every desc struct and every out pointer in all 33 services.
- Why: a C mod (or a LumaScript bug in the generated glue) passes a freed or short buffer; the read faults in
  nebula.exe or the game module and the game ends. luma.h and the service docs describe argument checks (null, size,
  range) but never say that a pointer that is not null and not valid is fatal to the game.
- Confidence: verified in code. This is the C ABI's nature; the finding is that no document says it and the crash
  report names the host, not the mod.

### M-012 missing feature: the microphone and camera services have backends only on Windows and Android
- Evidence: runtime/src/luma_capture.cpp:27 `#if defined(_WIN32)`, 48 `#elif defined(__ANDROID__)`, 802-806
  `#else constexpr bool kHasDevices = false; void microphone_thread(...) {} void camera_thread(...) {}`. Linux, macOS
  and iOS answer LUMA_E_ABSENT. docs/modding/reference/services/capture.md:10 says only "On platforms with no capture
  backend, the calls answer LUMA_E_ABSENT", without naming them.
- Why: a mod built for Linux or macOS asking for `audio.microphone` gets a consent prompt for a permission that can
  never do anything there. Also: tools/lumacore/capabilities.json:222-223 says "Windows (WASAPI) now, ABSENT elsewhere
  until 8.2" and "Windows (Media Foundation) now", but Android has a backend (luma_capture.cpp:48, 502), so the
  capability text is out of date.
- Confidence: verified in code.

### M-013 wrong behaviour: draw.native's take-out waits without limit for a mod function running on the render thread
- Evidence: LumaDrawNative.cpp:72-81 (`quiet` spins until `calling` is 0, no bound), called from modGone 180-187 on the
  game thread. luma_draw_native.h:10-12 documents the wait.
- Why: a native-permission mod whose function blocks (waits on its own game-thread code, or a GPU fence the host
  holds) hangs the game when the player turns it off. Documented under the `native` consent, so this is a known cost;
  listed because the Mods page gives no way out of it.
- Confidence: verified in code (the wait); the hang needs a run with such a mod.

Checked in area 2 and found correct:
- The provider contract (luma_service_provider.h) gives mod code only through `call`, on the game thread, guarded.
- Entry checks: every service function counted in LumaDraw, LumaUi, LumaSave, LumaAnim, LumaMovie, LumaPlayer,
  LumaDrawLight, LumaDrawPost, luma_audio, luma_input, luma_mods, luma_system, luma_assets calls its file's
  enter/begin (null mod, unknown mod, live, game thread). LumaDraw's 2-D calls go through enter2D (LumaDraw.cpp:220-229),
  so the "where" count above the "enter" count there is enter2D, not a missing check.
- draw.native's trampoline/quiet handshake is sequentially consistent on both sides (LumaDrawNative.cpp:94-107, 72-81).
- fs paths: each part is checked against a strict rule (no leading or trailing dot or space, ASCII letters, digits,
  " -_." only, no Windows device names, no ".tmp.<n>" names; luma_fs.cpp:73-104), and resolve walks without following
  links below a root (luma_fs.cpp:148-162, 250-297). ".." cannot appear.
- net.local judges the literal address it connects to, with no name lookup (luma_net.cpp:212-244). net.internet looks
  the name up itself, keeps only public addresses and connects to those (luma_net.cpp:4480-4498, 4557-4602), so a
  rebinding name cannot reach the local network; redirects are checked against luma.json's hosts (2126-2128).
- Not checked line by line: the argument checks inside luma_net.cpp (5169 lines), luma_input.cpp, luma_mods.cpp,
  LumaSave.cpp, LumaTime.cpp, LumaUi.cpp, LumaAnim.cpp, LumaMovie.cpp, LumaContentFx.cpp, LumaContentModel.cpp past
  their entry checks. See the coverage table.
- Host services are built into every host: CMakeLists.txt:227-236, platform/sdl/CMakeLists.txt:194-202,
  native/CMakeLists.txt:407-408; the system service's platform layer exists for Win32, SDL and Apple
  (luma_system_platform.h:45-49).


## Area 3: call safety (game calls, no scene, no player)

### M-014 crash: a game function a mod calls when its state is missing (no scene, no player) faults in game code and ends the game; the rows carry no precondition a checker could use
- Evidence:
  - The self-description's function rows (D:\NebulaWork\build\b200-game\smg_game.luma-self.json, 27095 rows) have only
    the keys name, signature, kind, owner, virtual, pure, const, overrides, marks (counted over all rows). `marks` are
    pointer-parameter ways (in/out/inout/kept, count, nullable: tools/lumacore/instrument.py:20-21, 1600-1661). No row
    says "needs a scene", "needs the player" or "game thread only".
  - Example row: `MarioAccess::validateSensor void()` is in the file. Its body,
    native/petari/src/Game/Player/MarioAccess.cpp:634-636: `getPlayerActor()->getSensor("eye")->validate();`, while
    getPlayerActor (629-632) returns null "while there is no player". MarioAccess.cpp has 226 unchecked
    `getPlayerActor()->` uses (256 across src), for example getTakePos 22-27, preventRush 624-626,
    calcWorldPadDir 620-622.
  - A fault there has its pc in the game module, so the guard refuses it (native/host/luma_guard.cpp:101-103).
- Why: a LumaScript `on frame` or `on start` that calls a player function such as `MarioAccess.preventRush()` while no
  scene with a player exists: the boot frames before the title. (Corrected in round 5 from the coordinator's probe runs:
  at the boot's first `on frame`, raw MarioAccess::validateSensor, kit item.give(Coins), item.give(Health) and
  camera.now end the game and the report names the mod (NEB-M03); the same calls at the title (frame 90) survive,
  because the title runs inside the game scene with a player. The earlier "on the title screen" was wrong.) LumaScript's checker types the call's pointer results as `T?` (game.rs:17-18) but cannot know
  the callee itself needs a player. docs/modding/reference/lumascript/errors.md:26-27 says a script error "cannot damage
  memory"; it does not say a game call at the wrong time ends the game.
- Confidence: verified in code (the path and the missing data); a run would show it: a script whose `on start` calls
  `MarioAccess.preventRush()`.

Checked in area 3 and found correct:
- MarioAccess::getPlayerActor and MR::getPlayerPos were made null-safe for scripts (MarioAccess.cpp:628-632,
  PlayerUtil.cpp:70-73), and LumaScript maps every pointer result to `T?` (game.rs:17-18).
- LumaScript keeps game views inside their statement: S0103 refuses a view in a module value, a field, a later
  closure or a task parameter, S0104 a view used after a `wait` (crates/lumascript/src/check/body.rs:7-10, 657-662).
- The invoke rows' kinds and result handling were checked in area 1 (LumaHook.cpp).

## Area 4: lifecycle and events

### M-015 missing feature: the 29 NEBULA_MOD_EVENT_* events never reach mod code; only the host's achievement rows subscribe, so their "return to cancel / return to replace" semantics are unused
- Evidence: the only subscriber is runtime/src/mod_plugins.cpp:390 (`g_api.subscribe(..., achievement_event, ...)`),
  for luma.json achievements; achievement_event returns `data->value` unchanged (mod_plugins.cpp:93-103). `fire` does
  nothing unless an achievement wants the event (NebulaMods.hpp:18-20 `sWanted[event] ? dispatch(...) : value`), and
  stageFrame computes spin/jump/land only then (NebulaMods.cpp:565-585). LumaScript's `on` handlers are start, stop,
  frame, load, step, option, stage, draw, layers and service events (crates/lumascript/src/parse.rs:760-799): none of
  these events. nebula_mod_api.h:34-89 and docs/modding/reference/events.md:22-47 still describe return values
  ("Return 0 to cancel the hit", "The return value is added instead", "Return -1 to cancel it").
- Why: a mod author reading events.md expects to cancel damage through the event; nothing can. events.md:11-14 says to
  hook the raising function instead, which works, but the table's return-value text describes a path no mod has.
  Also, hooking NebulaMods::dispatch (a callable row) sees an event only when some achievement waits for it.
- Confidence: verified in code.

### M-016 docs wrong: events.md links docs/modding/EVENTS.md, which does not exist
- Evidence: docs/modding/reference/events.md:46-47 "(numbers in docs/modding/EVENTS.md)"; the file is absent; the
  header it is generated from says "docs/modding/reference/events.md" (nebula_mod_api.h:85, 87), so the page was not
  generated again after the header changed (tools/modding/events_list.py).
- Confidence: verified in code.

Checked in area 4 and found correct:
- kEventNames in mod_plugins.cpp matches NEBULA_MOD_EVENT_COUNT by static_assert (29 events plus COUNT).
- Take-outs, luma::frame and services::frameStart run in that order at the frame's start on the game thread
  (GameSystem.cpp, LumaLoader.cpp:2108), and the services' events are delivered then.
- The header's thread note (nebula_mod_api.h:25-26: objects made while loading are reported on the loading thread) is
  safe for the one subscriber: achievement_event only reads the rows and sets atomics (mod_plugins.cpp:93-103, 378).

## Area 5: LumaScript compiler (lumalink-sweep crates/lumascript)

### M-017 docs wrong: detailed-rules.md promises a build warning for hooking a function that runs off the game thread, from a self-description mark that does not exist
- Evidence: docs/modding/reference/lumascript/detailed-rules.md:88-89 "The self-description marks functions known to run
  off the game thread (the self-description); hooking one is a build warning naming it." The self-description's rows
  have no such key (27095 rows, keys listed under M-014), and neither crates/lumascript/src nor crates/luma-validate
  has any off-thread warning (grep for off_thread / "off the game thread" / game_thread: only emit/class.rs:8, 19
  comments and the runtime test).
- Why: an author relies on the build to warn and gets a silent S0901 at run time instead (lumart.c:3743-3748).
- Confidence: verified in code.

### M-018 docs wrong / missing feature: two places assume stage objects are made on a loading thread; scenes are made on the game thread, and a class feature is held back on that assumption
- Evidence: native/include/nebula_mod_api.h:25-26 "objects created while a stage loads are reported on the loading
  thread". The game makes the scene inside GameSystemSceneController::update on the game thread: the load's steps
  run in a loop within the frame (GameSystemSceneController.cpp:101-110) and initializeScene calls
  `mScene->init()` directly (113-143); the game module creates no thread (no thread type or create call in
  native/petari/src outside SpiderThread, a LiveActor). LumaScript refuses class field first values that need code,
  S0800, because "`made` may run off the game thread" (crates/lumascript/src/emit/class.rs:8, 18-21).
- Why: the header misleads C authors, and LumaScript authors lose computed field defaults for a reason the game does
  not have. This also lowers M-008 and M-009: no off-game-thread caller of a mod class's virtuals or of make was
  found, so their trigger is theoretical until a thread is added.
- Confidence: verified in code (no game-module thread found by grep; the host's render and audio threads run frame
  lists and the mixer, not actor code).

### M-019 maintainability: the compiler's run-time checks (overflow, division, shifts, conversions) are tested as text only; the one test that runs generated C is skipped silently without LUMA_SDK
- Evidence: crates/lumascript/tests/emit_small.rs:2088-2092 `let Some(sdk) = std::env::var_os("LUMA_SDK") else {
  eprintln!("lumart is not run: LUMA_SDK is not set"); return; };` and it covers text and message rows only
  (2080-2087). The checks in emit/expr.rs:279, 592-628 and the conversions 676-701 are found in tests only as strings
  (`"908, 2"` at emit_small.rs:1491). 183 tests in 15 files (tests/*.rs).
- Why: a change that emits a wrong bound passes the suite; a CI without LUMA_SDK reports the run test as passed.
- Confidence: verified in code.

### M-020 maintainability: an off-thread hook's report reads the runtime's global caller without synchronisation
- Evidence: lumart.c:101 `lumart_as lumart_caller;` (global); lumart_on_game_thread (lumart.c:3743-3748) calls
  lumart_raise off the game thread, which reads `lumart_caller.mod/file/line` (lumart.c:105-106) while the game thread
  may be writing them.
- Why: the S0901 report can be filed against another mod (the library caller set at that moment) or a torn
  file/line. No crash: report copies strings it is given.
- Confidence: verified in code (race by reading); the misfiled report needs a run with a library mod and a hook hit
  on another thread.

Checked in area 5 and found correct:
- Integer + - * use __builtin_*_overflow; / and % check zero and MIN / -1; shifts refuse negative counts and counts
  at or past the width and shift in the unsigned type (emit/expr.rs:592-628). Unary minus is checked (279).
- Float to integer conversion checks the open range with exact double bounds, NaN refused (emit/expr.rs:688-699,
  emit.rs:2345-2357; the i64 lower bound is the next double below -2^63, correct).
- Integer narrowing is checked (S0908, emit/expr.rs:676-686).
- Pointer results map to `T?`; views cannot be kept in module values, fields, enum cases, later closures, task
  parameters or across a wait; these rules have tests (tests/body.rs:442-458).
- draw.native is refused to scripts (svc.rs:1757-1760; check/body/service.rs:273-275), so no script code runs on the
  render thread.
- Hook wrappers: off the game thread a before does not skip, an after leaves the result, a wrap or replace proceeds
  (emit.rs:1965-2012), as detailed-rules.md:277-279 says. Overrides do the same (emit/class.rs:226-232).
- Not read: parse.rs, check.rs, most of check/body*.rs, emit/flow.rs, task.rs, states.rs, closure.rs, json.rs,
  lumi.rs, fmt.rs; error message wording was not reviewed.

## Area 6: LumaKit (lumalink-sweep lumakit, 22 modules, 11587 lines)

### M-021 wrong behaviour: kit.camera's field of view, mirror and shake are one value each; with two mods the newer one's end resets the game's default instead of giving the earlier mod's back
- Evidence: lumakit/source/camera.lum:321-325 (`mirror`: `mirrored = enabled; mirror_owner = caller()`),
  342-349 (`fov`: one `fov_degrees`, one `fov_owner`; `fov(none)` clears it whoever set it), 352-357 (`shake`),
  and the stop handler 473-490 (`if fov_degrees >= 0.0 and fov_owner == gone { fov_degrees = -1.0 }`, the same for
  the mirror and the shake). docs/modding/concepts/lumakit.md:63 promises "When the newer request ends, the earlier
  one holds it again." Controls (the camera rig) do keep a stack (camera.lum:35-42, 473-480); these three do not.
- Why: mod A sets fov 90, mod B sets fov 60 and is turned off: the camera goes to the game's own fov, not A's 90.
  B calling `mirror(false)` turns off A's mirror; B calling `fov(none)` clears A's fov.
- Confidence: verified in code.

### M-022 docs wrong: "A call from another thread is a script error" for LumaKit; the library's export wrappers have no thread test
- Evidence: docs/modding/concepts/lumakit.md:67. The wrapper LumaLink writes for each export
  (crates/lumascript/src/emit/library.rs:432-499, written per export) saves and sets the global `lumart_caller` and
  calls the function; there is no `lumart_on_game_thread` in library.rs (grep: none), unlike hook wrappers
  (emit.rs:1968) and overrides (emit/class.rs:226-232).
- Why: a C mod calling a LumaKit export from its own thread runs LumaKit's code against lumart's global pools and
  `lumart_caller` (lumart.c:101) concurrently with the game thread: corrupted lists, a crash in LumaKit's image, which
  the guard does not resume for the caller (M-001). A LumaScript caller cannot hit it (its hooks skip off the thread).
- Confidence: verified in code (no check). The corruption needs a run: a C mod calling a kit function from a thread.

Checked in area 6 and found correct:
- Every module that records state per caller() also handles `on mods.stopped` (22 of 22 modules; counts per file:
  actor 7/1, item 10/1, replay 12/1, stage 8/2, ... all have at least one).
- Every `pub fn` / `pub task` signature of the 22 modules appears word for word in its docs/modding/reference/lumakit
  page, and every public struct and enum is named there (script comparison, both trees as they are).
- Script errors inside a kit export are reported against the calling mod at its line (lumart.c:105-106 with the
  wrapper's `lumart_caller.mod = lm_caller`, library.rs:477).
- The camera controls themselves are a stack per owner and the newest live one wins (camera.lum:35-42, 207-228,
  473-480).
- Not read: the module bodies beyond camera.lum's ownership parts and the headers of hud.lum and actor.lum; the
  data-file (kit/MODULE/) checks.

## Area 7: LumaLink commands (crates/lumalink/src/main.rs, crates/lumalink-core/src)

### M-023 maintainability: `pack` rebuilds code only when a source file is newer than the oldest code file; a LumaLink update or a new game self-description packs code built by the old compiler against the old rows
- Evidence: crates/lumalink-core/src/jobs.rs:1470-1491 (`code_out_of_date`: newest mtime under source/ and luma.json
  against the oldest under code/); crates/lumalink/src/main.rs:969-981 (pack builds only when that says so).
- Why: after a compiler fix ships in LumaLink, `lumalink pack` of an unchanged mod ships the code with the old bug;
  after the game's self-description changes, the code's row bindings are the old ones and fail at load (the kernel
  binds by name and signature) instead of at pack time.
- Confidence: verified in code.

Checked in area 7 and found correct:
- `update` downloads over HTTPS only (update.rs:154-156), keeps the Setup only when its RSA-SHA256 signature verifies
  against the built-in release key and the signed version is newer (update.rs:10-12); the non-Windows verifier
  rebuilds the whole PKCS#1 v1.5 block and compares all of it (update.rs:639-662), so no partial parse; Windows uses
  CNG (the `cng` module after it).
- `check`, `pack`, `verify`, `convert` and `new` return a non-zero exit status on any error or refusal
  (main.rs:463-487, 926-1005, 1172-1190); pack refuses while any rule could not be checked (main.rs:995-998).
- Not read: `add` (main.rs:642-770), `test` (798-869), `build` internals (lumalink-core/src/build.rs), `convert`
  internals (convert.rs, riivolution.rs, gecko.rs), the GUI.

## Area 8: validator and import

### M-024 docs wrong (overclaim): permissions are described as limits, but only an honest mod is held to them; every code mod is native code in the game's process
- Evidence: docs/modding/concepts/permissions.md:23 gives "full access to the device" to `native` alone, and :71
  says "Mod code cannot open, answer or skip the question." Every code mod's entry library is loaded with
  LoadLibraryExW / dlopen into the game (runtime/src/mod_plugins.cpp:112-114) with no sandbox. The only gate on system
  access is the validator's rule that the entry library imports nothing from the system (L504,
  crates/luma-validate/src/rules/code.rs:246-281), which a library meets while still reaching the system (raw system
  calls, or finding the system's loader through the process's own structures). Services trust the `mod` argument:
  apiService checks the grants of whatever LumaMod* it is given (LumaLoader.cpp:1047-1063), and every library export
  receives its caller's LumaMod* (crates/lumascript/src/emit/library.rs:434 `const LumaMod* lm_caller`), so a C
  library can keep it and use the caller's permissions later.
- Why: an author or player reading permissions.md concludes that a mod without `native` cannot touch files or the
  network, or that it cannot change its own consent. The consent screen itself is truthful: "This mod runs its own
  code and can do anything on this device" (localization/NebulaText.tsv:911-912).
- Confidence: verified in code (no run-time enforcement exists beyond the import-table check); the bypass was not run.

Checked in area 8 and found correct:
- Archive entries: absolute paths, drives, `..`, device names and links are refused before extraction
  (mod_import.cpp:86-101 with mods.cpp:1030-1043; links skipped at 357 and 414); size and entry limits count what is
  extracted, not the header's claims (mod_import.cpp:81-84).
- A refused package is removed before anything is placed (mod_import.cpp:556-564); a replaced mod is moved aside and
  put back if the move fails (566-595).
- The entry library: luma_mod must be exported, no system imports (L504), no code run at load or close (L509), iOS
  platform and encryption checked (L511) (rules/code.rs:218-281).
- The importer's C entry points catch Rust panics (catch_unwind in validate_c.rs, c_api.rs, cook_c.rs, patch_c.rs,
  jni.rs).
- Not read: mod_check.cpp, mod_consent.cpp, mod_start_check.cpp in full; luma-validate's zip.rs and the JSON rules.

## Area 9: mod manager and UI flows

Only runtime/src/mod_import.cpp:60-200 and 495-644 were read (import, update in place, pending update). No finding
there beyond the checks listed in area 8. mods.cpp, mod_screens.cpp, luma_mods.cpp and the Android Java/Kotlin side
were not read: this area is unfinished.

## Area 10: documentation against code

Findings from other areas that are documentation faults: M-004 (luma.h out-of-memory), M-012 (capture platforms,
capabilities.json), M-015 and M-016 (events.md), M-017 (detailed-rules.md build warning), M-018 (nebula_mod_api.h
loading thread), M-021 (lumakit.md "earlier one holds it again"), M-022 (lumakit.md thread rule), M-024
(permissions.md).

### M-025 docs wrong: concepts/lumascript.md says a script cannot read a game object after it is gone, and errors.md that a script error cannot damage memory; a game call at the wrong time ends the game (M-014) and a LumaKit fault ends it too (M-001)
- Evidence: docs/modding/concepts/lumascript.md:31; docs/modding/reference/lumascript/errors.md:26-27; see M-014
  (MarioAccess.cpp:634-636) and M-001 (luma_guard.cpp:101-103).
- Why: the sentence is true of the script's own values but is read as "a LumaScript mod cannot crash the game".
- Confidence: verified in code.

Checked in area 10 and found correct:
- All 22 LumaKit reference pages match the sources' public signatures (area 6).
- docs/modding/reference/lumascript/detailed-rules.md:277-279 matches the hook wrappers' off-thread behaviour.
- The consent text (NebulaText.tsv:911-912) is accurate.
- Not done: the example mods' sources against their pages, services reference pages against headers line by line.

## Area 11: tests and recorded claims

### M-026 maintainability: no recorded run covers this tree; PROVEN.md proves an older commit, and the two board files disagree on its count
- Evidence: D:/NebulaWork/agents/luma/board/PROVEN.md:1-3 "luma-int 0880934f3, integration run int0400 ... 288 of 359
  tests pass" (file dated 11-12). D:/NebulaWork/agents/luma/PATH-FORWARD-MODS.md:197 "279 of 359 tests pass, 65
  suites (board/PROVEN.md ...)". The audited tree is at 0b1982c6f (2026-11-15). PROVEN.md has no guard suite (the
  board's own note M3, PATH-FORWARD-MODS.md:283, says the guard was not in that build), so the guard has no recorded
  passing run. None of the 26 in-repo tests (docs/modding/examples/*/tests/*/test.json) appears in PROVEN.md.
- Why: "proven" claims cannot be relied on for this tree; the guard, LumaKit and the examples need a run.
- Confidence: verified (files read); whether the 26 tests pass needs a run of each example's tests.

Claims in PATH-FORWARD-MODS.md sections 4b and 5 checked against this tree:
- R5 (reports folded without their arguments, LumaLoader.cpp:596): fixed; record now compares arguments
  (LumaLoader.cpp:619-650).
- C8 (`Script.S0921` row missing) and T8 (`Draw.Post.Empty` row missing): both rows are in
  localization/NebulaText.tsv with 15 columns.
- M3 (guard not in the build): the guard is in this tree (LumaGuard.cpp, native/host/luma_guard.cpp).
- R8 (make refused during play): LumaMake allows play time now (area 1).
- S1 ("acting for", a library using its caller's permissions): exists as lumart_for_caller (lumart.c:113-116),
  without a registry acting_begin/end; see M-024 for what that allows a C library.
- Not checked: R6, R7, R9 to R17, C1a to C1k, C2 to C10, K1 to K9, P1 to P4 (each needs a run or the compiler
  sections not read).

# Round 2

Binary facts below come from reading D:\NebulaWork\build\b200-game\smg_game.dll (built 2026-11-16 21:30) with
llvm-objdump; no symbols beyond its exports, so internal functions are located by their callers and strings.

## Guard: why 1619/1620/1621 (time timer) are not resumed

### M-027 wrong behaviour: every service event (guard kind 6) of a mod without unwind data is refused by the word scan, because luma_guard_call's own saved rbx holds the mod's function address
- Evidence:
  - kernelCall keeps `fn` (its third argument, r8) in rbx and passes it from there to guard::run (smg_game.dll):
    `18092501d: movq %r8, %rbx` ... `180925056: movq %rbx, %r9` / `180925059: callq guard::run`
    (source: native/petari/src/Game/System/LumaServices.cpp:416-417, `guard::run(..., reinterpret_cast<const void*>(fn), ...)`).
  - guard::run does not touch rbx on its normal path (prologue 1808f1da0-1808f1dfc saves it; the only write,
    `1808f1ea8: movq %r9, %rbx`, is on the first-use path) before `1808f1ff9: callq luma_guard_call`.
  - luma_guard_call pushes that rbx inside the range the host later scans: LumaGuard.cpp:62 `pushq %rbx` (win-x64),
    LumaGuard.cpp:140 (sysv-x64); aarch64 stores x30 (return into guard::run) and x19 at LumaGuard.cpp:207-212.
    The scanned range is [fault sp, regs[SP]) and regs[SP] is the stack pointer above luma_guard_call's return address
    (LumaGuard.cpp:46-47 `leaq 8(%rsp), %rdx` / `movq %rdx, 64(%rcx)`), so the saved rbx (regs[SP]-16) is scanned.
  - why_not_words skips only `call_return` (native/host/luma_guard.cpp:201); a game or host code word anywhere below
    followed by any word inside the mod's code range returns kGameBetween (luma_guard.cpp:202-205, 210-212).
  - Windows uses this scan for a library with no exception directory (luma_guard.cpp:369); POSIX always uses it
    (luma_guard.cpp:192 comment; the POSIX on_fault).
  - The guard test mods have no exception directory (exception directory size 0 in every guard-*-* mod.dll;
    g/tools/build_guard_mods.sh builds with -ffreestanding and no -fasynchronous-unwind-tables). WHERE 7 is the only
    guard case that runs through kernelCall (g/mods-src/guard/guard.c:47-55, 96-101; timerDeliver passes the mod's
    own `fn` at LumaTime.cpp:518).
  - The 23 other callers of guard::run (hooks, start, stop, classes) load fn into r9 from memory
    (e.g. `1808b6e86: movq 0xf8(%rax), %r9`, `1808f5f29: movq (%rbx), %r9`), not from rbx. Only kernelCall
    (kind 6, `movl $0x6, %edx` at 18092504e) has the mod's address in rbx. That is why 32 of 35 pass and exactly the
    three timer cases fail.
  - The mod's handler `due` (guard-7-1 mod.dll 180001990-180001a61) has a 0x78-byte frame, of which only 0x28 and
    0x30-0x3e are written before the fault at 180001a1f; 0x20-0x27, 0x40-0x77 and luma_guard_call's 32-byte shadow
    space hold whatever earlier, deeper game calls left there.
- Why: the mod's handler faults; the scan meets a stale game or host return address in the uninitialised slots
  (game_seen), then the saved rbx (= the handler's address, inside the mod's code range) above it, and reports
  "the game's or the host's code is between the boundary and the fault". The comment at LumaGuard.cpp:17-18 says
  "nothing of the kernel is there but luma_guard_call's own few bytes", but the scan does not leave those bytes out.
  Scope: every kernel->call site (all service events, list below) for C mods without unwind data on Windows, and for
  every mod on Linux, macOS and Android if their kernelCall build keeps fn in rbx / x19 too. On aarch64 the trigger
  needs no stale word: x30 (a game address) is stored 8 bytes below x19.
  Fix direction: scan only below the call's return slot (stop at regs[SP] - kCallReturnSlot on win-x64, the
  matching slot per ABI), so luma_guard_call's own words are never read.
- Confidence: the rbx word is verified in the built dll and the source; the stale game word below it is suspected
  (a minidump at the fault in 1619 would show its address). Linux/macOS/Android codegen: suspected, needs the .so /
  dylib disassembly of kernelCall or a run of 1619 there.

Every path that calls a mod's function as a service event (all go through LumaServices.cpp:416 kernelCall, then
guard::run kind 6; none calls luma_guard_call another way). "direct" = the mod's own function is `fn`, so M-027
applies; "trampoline" = a game function is `fn`:
- direct: LumaTime.cpp:518 (timer, event 1); LumaAnim.cpp:323; LumaUi.cpp:424; LumaMovie.cpp:143;
  LumaSave.cpp:887 and 1089; runtime/src/luma_assets.cpp:943; luma_audio.cpp:568 and 573; luma_capture.cpp:868;
  luma_fs.cpp:661; luma_input.cpp:312, 385, 535; luma_mods.cpp:690, 952, 1081; luma_net.cpp:2797, 3701;
  luma_system.cpp:523.
- trampoline: LumaTime.cpp:419 (handlerTrampoline, events 2 and 3) and LumaDraw.cpp:1830 (pointTrampoline). See M-028.
- not guarded at all: draw.native (M-013, LumaDrawNative.cpp, render thread).

### M-028 maintainability: time handlers and draw points are resumable only because the compiler turns the trampoline's call into a tail jump
- Evidence: LumaTime.cpp:407-411 and LumaDraw.cpp:1672-1676 call `pCall->fn(...)` as their last statement. In the built
  dll both compile to a tail jump with no frame:
  `180929069: movq 0x10(%rcx), %rdx / movq (%rcx), %rax / movq 0x8(%rcx), %rcx / 180929074: jmpq *%rax` (time) and
  `1808ca4e9 ... 1808ca4f4: jmpq *%rax` (draw).
- Why: in a build without that optimisation (a debug build, or a change that adds code after the call), the
  trampoline's return address is a game frame between the boundary and the mod: why_not_frames returns kGameBetween at
  luma_guard.cpp:167 for every fault in a time handler or draw point. Nothing in the source requires the tail call.
  (Because fn is the game's trampoline here, kernelCall's rbx is a game address and M-027 does not apply to them.)
- Confidence: verified in code and in this build's disassembly; the non-optimised case needs a debug build run.

## Area 2 (round 2): service bodies

### M-029 crash: the full list for M-011 - every service function whose pointer argument is read or written by kernel or host code (a bad one ends the game, not the mod)
- Evidence (from the function tables in native/include/luma_*.h, parameters other than `mod`; `^` marks a callback
  stored and called later, whose bad value faults at a pc outside the mod, which luma_guard.cpp:101-103 never resumes):
  - anim (luma_anim.h:98-117): play(clip, how, how->marks, out, fn^), play_on_model(same), frame(frame, last).
    `actor` is also a raw pointer read through its vtable (LumaAnim.cpp:679-681, `lumacore::classOf`, see M-007).
  - assets: find(path, out), bytes(path, data), entry(path, name, needed, out), provide(path, files), withdraw(path),
    cook(path, data, request, fn^).
  - audio: play(name, how, out, fn^), stream_open(desc, out, fn^), stream_push(samples: `frames` x channels floats read
    under the mixer lock, luma_audio.cpp:1318-1329), music(name), tap_open(out, fn^), play_at(name, how, at, out, fn^),
    voice_place(at), game_play(name, at, out), game_place(at), stream_channels(out).
  - capture: microphone/camera open(desc, out, fn^).
  - content.fx: start(joint, place, out) with a raw `actor` (LumaContentFx.cpp:627-633, classOf), start_on_model(socket,
    place, out). content.model: attach(joint, place, out) with a raw `actor` (LumaContentModel.cpp:534-541, classOf),
    player_parts(parts), set_param(material, param, rgba).
  - draw: at(fn^), screen(out), rect, rect_outline, image(dst, src), text(text, advance), text_measure(text, out),
    clip_push(rect), font(name, out), texture(name, out), texture_from_pixels(rgba8: width x height x 4 bytes read),
    texture_update(rgba8), texture_size(w, h), line3(a, b), mesh(model), billboard(pos). draw.inst: of(out) with a raw
    `actor` (LumaDrawInst.cpp:412-414, classOf), set_material_param(material, name), set_texture(material),
    set_visible(part). draw.light: add(desc, out), set(desc). draw.native: at(fn^, called unguarded on the render
    thread, M-013), backend(out). draw.post: program(name, out), params(values). draw.view: create(out),
    camera(eye, at, up), follow(offset) with a raw `actor` (LumaDrawView.cpp:408-409, classOf), texture(out), rig(camera,
    camera->actor raw, 458-459), eye_of(camera, out), main_eye(eye), main_rig(camera, look).
  - fs and fs.exchange: read, read_package, write(path, data: `bytes` read), remove, move(from, to), list,
    list_package (each with path, out, fn^), take_read(buf: `size` written), take_list(buf).
  - input: action(id, out), read(out), game(out), game_raw(out), device(out), watch_device(fn^), text_request(request,
    out, fn^), text_peek(buffer, length, caret), user_action(id, out), look(out), begin(out), set(state), list(out,
    found), open(path, out, fn^), write(data: `bytes` read).
  - mods: find(id), option(key, buf), watch_options(fn^), watch_mods(fn^), kit_names(module, buf, length),
    kit_read(module, name, buf, length), needing(out, count), get(space, name, out), of(space, mod_id, name, out),
    request(path, out, fn^).
  - movie: play(name, out, fn^), position(frame, frames).
  - net.local and net.internet (each): tcp_connect(host, out, fn^), tcp_listen(out, fn^), port(out), send(data: `bytes`
    copied under the network lock, luma_net.cpp:3488-3492), take(buf: memcpy under the lock, 3521-3522), udp_open,
    udp_connect(host), send_to(host, data), take_datagram(buf, from), ipc_connect(name), ipc_listen(name),
    tls_connect(host), tls_listen(certificate, key), http(request and its strings), ws_connect(url, protocol),
    ws_send(data), address(buf).
  - player: make(init, out); remove and set_drawn take a raw `player` but only compare it (LumaPlayer.cpp:107-114,
    221-233), never read it.
  - save.game and save.global: get(key, buf), set(key, value), remove(key), file(number, character), export_file(out,
    fn^), take(buf), offer(path, out, fn^).
  - system: facts(out), display(out), get_text(out, fn^), take_text(buf), get_image(out, fn^), take_image(rgba),
    set_text(text, out, fn^), set_image(rgba: width x height x 4 read, out, fn^), open(url, out, fn^).
  - time: now(out), wall(out), timer(out, fn^), on_frame(fn^), on_step(fn^).
  - ui: open(layout, out), pane(name, out), set_text(text), play(animation, fn^), alive(out), menu_rows(rows: `count`
    read), menu_chosen(row), share_rows(rows), shared_focus(focused, chosen), play_pane(animation, fn^).
- Why: each is checked for null, size and range only; a non-null pointer that is freed, short or wrong faults inside the
  provider, whose pc is not the mod's. No service checks that a callback lies in the calling mod's code range, though
  the kernel knows that range (LumaGuard.cpp:472).
- Confidence: verified in code (the tables and the guard's rule; no code-range use outside LumaLoader.cpp:1429, 1596 and
  LumaGuard.cpp). A fix with real effect: refuse at registration any `fn` outside the code of the caller and of the
  libraries it imports (a library acting for its caller, lumart_for_caller, registers its own functions under the
  caller's handle).

### M-030 wrong behaviour: player.make and player.remove leak each extra player's memory until the scene ends; a mod that makes and removes in a loop ends the game out of memory
- Evidence: LumaPlayer.cpp:183-192 makes a MarioActor in the scene's arena each call; remove (205-218) and modGone
  (310-315) only call endExtra and free the slot ("the player stays dead in its scene (its memory is the scene's)",
  116). The only limit is LUMA_PLAYER_MOST_EXTRA standing at once (luma_player.h:23-24; LumaPlayer.cpp:160-172); there
  is no limit per scene. The ui service has one for the same reason (LumaUi.cpp:50, 658-663: kMostMade). An arena that
  cannot commit more panics (NebulaArena.cpp:212-221).
- Why: a co-op or respawn mod that removes and makes its player on each death, or a bug that does it every frame,
  grows the scene arena without bound until NEBULA_PANIC "out of memory" ends the game.
- Confidence: the unbounded growth is verified in code; how many makes it takes (a MarioActor's size) needs a run.

Checked in area 2 (round 2) and found correct:
- time (LumaTime.cpp, all 1191 lines; luma_time.h): entry checks, struct sizes (now writes 64 bytes only after
  checking size >= 64, wall 32), rate product exact and order-free (204-243), timers: slot reuse, once timers closed
  before the call, a closed timer freed by its deliver or drop (486-527; deliverMod sets the node idle before deliver,
  LumaServices.cpp:500-502), saturating due times (467), handler passes copy their order list (1063-1073, 1115-1124),
  the Mods page's lock-free view (245-295), let-go requests (956-974, 1169-1190).
- ui (LumaUi.cpp, all 1501 lines; luma_ui.h): every call checks mod, thread, handle owner; UTF-8 decode rejects
  overlong, surrogate and >U+10FFFF (554-599); the text cut never splits a pair; text boxes grow only within the
  game's AllocStringBuffer (TextBox.cpp:167-217 bounds every copy); screens per scene capped (kMostMade); end events
  carry their own copy; released() runs before the arena's memory goes (NebulaArena.cpp:166-184).
- player (LumaPlayer.cpp, all 362 lines; luma_player.h): remove and set_drawn compare the raw player pointer against
  their own records only; released() forgets slots in a freed arena.
- save (LumaSave.cpp 1-190): get/set/remove/file follow luma_save.h (limits, ABSENT at the title, newer chunk kept unread).
- net (luma_net.cpp 1596-1934, 3238-3525): the HTTP answer parser bounds the head (kHeadMost), chunk sizes (15 hex
  digits), body (LUMA_NET_BODY_MOST, also after gzip), refuses folded headers and two disagreeing lengths; send/take
  bound the queue per mod (room_locked); tls_listen reads only the mod's own files through safe_path_part.
- input.dev (luma_input.cpp 1296-1405): open accepts only a path `each_hid` lists and that is not a keyboard or mouse.
  Windows only, and input.md:12 says so.
- Platforms: capture has backends on Windows and Android only (M-012); input.dev on Windows only (documented); the
  guard is off on Windows ARM64 (M-006). No other host service has a platform gap in its #if blocks (luma_fs.cpp,
  luma_assets.cpp, luma_net.cpp: per-platform code for each of Windows, Linux/Android, Apple).

## Area 9: mod manager, import, crash recovery

### M-031 crash: on Linux, macOS and Android a fault with mods on never makes the next start safe; a mod whose code crashes the game at start crashes it on every start
- Evidence:
  - The file that makes the next start safe is written only in gather_crash_facts (native/host/host_main.cpp:314-326,
    gCodeCrashFile set at 4980-4982 for code, 6101-6105 for any mod).
  - gather_crash_facts is called by the Windows handlers (host_main.cpp:1526, 1665, 1726; install_crash_reporter is
    called only `#if defined(_WIN32)`, 5845-5847) and by die (453) and svc_fatal (1861), which are the game's own
    fatal errors, not faults.
  - The SDL hosts' fault handler (platform/sdl/crash_log.cpp:88-183) logs the signal and passes it on; it never calls
    gather_crash_facts. The guard's POSIX handler, when it does not resume, writes its line and passes the fault on
    (native/host/luma_guard.cpp:467-478).
  - The next start reads `.code_crashed` (host_main.cpp:4921-4931, runtime/src/game_module.cpp:32) and `.safe_start`
    (runtime/src/mods.cpp:1075-1090); with neither written, every mod and its code is on again.
  - Android's launcher sees the crash (LauncherModel.kt:521-530) and says only R.string.GameCrashed.
- Why: a mod (or a mod's file) that faults the game during start or the first scene (a bad hook, M-001, M-014) ends
  every later start on these platforms. The player's only way out is the Mods page, if the game gets that far, or
  deleting the mod's folder by hand.
- Confidence: verified in code (no write on the POSIX fault path). A run: a mod whose start writes through null, on
  Linux or Android, started twice.

### M-032 missing feature: after a crash that names a mod, nothing turns that mod off or offers to; the next start turns off every mod's code once, and a restart turns it all on again
- Evidence:
  - The Windows report names the mod and tells the player to turn it off by hand: runtime/src/crash_report.cpp:177-183
    (NEB-M03, "Turn this mod off in Settings > MODS"); the dialog only offers to open the report's folder
    (host_main.cpp:1787-1818).
  - The next start removes `.code_crashed` and takes every non-dev mod's code off for that run only
    (host_main.cpp:4921-4931); the Mods page shows the same line on every code mod (mods.cpp:1338-1339:
    "THIS MOD'S CODE IS OFF AFTER A CRASH. RESTART TO TRY IT AGAIN"). The named mod's id (luma_guard.cpp:596-598
    note_mod, gCrashPlugin) is not carried to the next start.
- Why: with several code mods, the player cannot tell from the Mods page which one crashed; the following start runs
  them all again and the crash repeats every second start.
- Confidence: verified in code.

Checked in area 9 and found correct:
- Import (mod_import.cpp 200-831): entries that are links, devices or unsafe paths are skipped or refuse the file;
  entry and byte limits; unpacked into a staging folder and renamed into place, so a failed import leaves no half
  mod; an update to a running mod waits as a pending update applied at the next start.
- Start check (mod_start_check.cpp) and consent (mod_consent.cpp): the code answer is stored against the code's digest,
  so a changed code part asks again; dev mods are cleared on Android and iOS (mods.cpp:1939-1962).
- Desktop import from Settings runs off the game thread and says "MODS APPLY WHEN NEBULA RESTARTS"
  (native_settings.cpp:3270-3354); Android's import goes through the same importer (mod_import_jni.cpp).
- Shipped mods (install_shipped_mods, mod_import.cpp:681-720) skip the question by design and are recorded as allowed.
- Android's picker copies the whole picked file before the importer's limits apply (ModImport.kt:52-66): a disk-space
  cost only, the importer then refuses an oversized file.
- Not read in area 9: mods.cpp beyond 1030-1090, 1330-1345 and 1939-1962; mod_screens.cpp; mod_check.cpp;
  runtime/src/luma_mods.cpp; GameFiles.java.

## Area 6 (round 2): LumaKit module bodies

### M-033 wrong behaviour: kit.screen's loading tips are one value with one owner; when the newer mod stops, the earlier mod's tips are gone, not given back (the M-021 pattern)
- Evidence: lumakit/source/screen.lum:269-280 (`loading` sets `tips`, `tip_owner = caller()`, `tip_picture`), stop
  handler 379-383 (`if tip_owner == gone { tips = []; tip_picture = none; tip_owner = 0 }`).
  docs/modding/concepts/lumakit.md:63: "When the newer request ends, the earlier one holds it again."
- Why: mod A sets loading tips, mod B sets its own and is turned off: no tips show, A's are lost until A calls again.
- Confidence: verified in code.
- The other single values in the kit were checked: kit.screen's open screen (one at a time, the older one's waiter
  gets none, screen.md:20), kit.cutscene's scene (a newer scene ends the older, play returns false, cutscene.lum:136-145,
  358-363) and kit.hud's popup (a new one replaces the one shown, hud.lum:338-343) are documented as newest-wins with
  no hand-back, so they are not findings. kit.player.form keeps a stack (form.lum:80-82, 577-595); kit.camera's
  controls keep a stack. kit.camera's fov, mirror and shake remain M-021.

### M-034 docs wrong: three LumaKit doc comments lost words, and the reference pages print the gap
- Evidence: lumakit/source/replay.lum:782-783 ("After remove(): a script error" then a line that is only `//.`),
  screen.lum:269-270 ("the newest caller's" then `//. An empty list takes them away.`), ui/kit.lum:14-15
  ("Callbacks run at the frame's start after the press" then `//. Everything belongs ...`). The generated pages carry
  them: docs/modding/reference/lumakit/replay.md:47 and 222 ("a script error ."), screen.md:38 and 125 ("the newest
  caller's ."), ui-kit.md:20 ("after the press .").
- Why: the reader of those pages gets broken sentences; "the newest caller's" says nothing about what holds.
- Confidence: verified in code (grep for comment lines starting with punctuation: these three, no others).

Checked in area 6 (round 2) and found correct:
- Every use of the player in the kit is guarded: `MarioAccess::getPlayerActor()` and `MR::getPlayerPos()` are read
  through `if let` / `guard let` (camera.lum:84, 134; cutscene.lum:150; gravity.lum:124, 183; replay.lum:202, 715, 995;
  actor.lum:636, 693; area.lum:266), MR::getPlayerGravity through `?? vec3(...)`; replay.lum:200-205 reads the
  getPlayer* calls only after the player is known.
- Actors held across frames are `ref<...>` and read with `.get()` each frame, a gone or dead actor ends the shot or
  the use (cutscene.lum:182-188; replay.lum:747-779; camera.lum:92-97).
- The only force unwrap in the kit is replay.view (replay.lum:786-787), documented as a script error on a removed
  puppet.
- Per-mod records: actor, area, gravity, item, stage, progress, player.character, player.form, player.state, hud,
  screen layouts, cutscene keep the owner per record and drop or end them on `mods.stopped` (actor.lum:880-910;
  area.lum:326-335; form.lum:577-595; hud.lum:348-358; screen.lum:365-389; cutscene.lum:358-363).
- `on frame` counts (hud's popup 180, form timers) run in GameSystem::update (GameSystem.cpp:279), the game step,
  so they are not tied to the shown frame rate.
- Read in full this round: cutscene.lum 56-200 and 330-363, hud.lum 325-394, player/form.lum 1-103 and 520-595,
  screen.lum 150-165 and 261-389, replay.lum 770-800, ride.lum 335-370, the module-level state of all 22 modules (grep).
- Not read: the remaining bodies of actor, area, item, menu, net/kit, player/character, player/state, pointer,
  progress, projectile, replay (most), ride (most), shop, stage, talk, ui/kit, gravity; the kit/MODULE/ data checks.

## Area 7 and 8 (round 2)

### M-035 maintainability: the LumaLink test each_manifest_part_is_written_in_the_format_order_and_checked asks for lumakit >=1.2.0 while LumaKit is 1.0.0, so it fails
- Evidence: crates/lumalink-core/src/add.rs:598 and 610 (`version: ">=1.2.0"`); `done` asserts the check passed
  (add.rs:585-591); lumakit/luma.json:4 (`"version": "1.0.0"`); the validator's need rule reports a need whose
  installed version is out of range (Nebula-Clean crates/luma-validate/src/rules/mods.rs:31-40, L800 Need.Version).
- Why: the test was written against a kit version that the v1 renumbering (all first releases at 1.0.0) removed; the
  suite has one standing failure, which hides a new one.
- Confidence: the test and the versions are verified in code; that L800 is the failing assert matches the coordinator's
  run (the failing test) but the exact message needs the test output.

Checked in area 8 (round 2) and found correct, crates/luma-validate/src/zip.rs (all 401 lines):
- The end record must end exactly at the file's end (134); Zip64 locator and record are checked; several disks are
  refused (156-173); at most 200000 entries (174); the directory must fit in the file and be at most 64 MiB (177).
- Every directory read is bounds checked (u16_at/u32_at/u64_at return 0 past the end; 187, 205).
- A link or device is found from the Unix mode whatever system the entry says made it (234-246).
- Methods other than stored and deflate, and every kind of encryption, are marked for the rules (247-253).
- A local header whose name differs from the directory's is marked (255-260); overlapping data ranges (one entry's
  data reused, a zip bomb) are found (303-312).
- Unpacking counts size and CRC over every byte, stops at `limit`, never keeps more than asked (327-397); a deflate
  stream that ends early is Broken (389-390).
- Not read in area 7/8: luma-validate json.rs, schema.rs, names.rs, binary.rs, rules/*.rs other than mods.rs 1-60
  and code.rs 200-310; lumalink-core add.rs beyond 585-660, new, pack, build.rs beyond 205-245, test, convert,
  update.rs beyond round 1, verify; lumalink-gui; lumalink-setup. Whether a crafted .luma can pass the validator and
  then unpack differently in the game's importer (Rust validator, C++ importer, two readers) was not settled.

## Area 11 (round 2): old suites in audit-conf/luma

One line per suite (count of tests, what they cover, from the test folders' names and test.json):
- f/classes (5): made, taken out, thrown, refusals, a scene again, slots of LumaScript classes.
- f/cook (2): a cooked texture named; a merge refused.
- f/gone (2): actors gone under a mod, new and old form.
- f/guard (35): fault in before/after/wrap/replace hooks, start, stop, timer, made, override, nested, each with access,
  illegal instruction, divide; cost; code time; a walk after a host call. 1619-1621 fail (M-027).
- f/guard-crash (2): faults that must not be resumed (deep, in the game).
- f/harness (1): the harness's spin reason.
- f/input (13): follow, own handles, host menu, controls row, game read, inject (declined, undeclared, take-out),
  text, dev (declined), rumble.
- f/k1 (10): unhook last, stop hook, number ports, number screen, fault in body, stage of sources, runtime layout.
- f/kitdata (2): kit data with and without a schema.
- f/layers (12): choose, options, unknown name, faults, needs, unstable, hand over.
- f/loader (23): replace order, skip rules, api 0, services present/absent/optional/newer, memory, report, unhook,
  thread, frame, partial, churn, release.
- f/loader5 (12): mod functions between mods (missing, signature, optional), needs missing and cycles, signatures.
- f/make (1), f/probe (1), f/patch (0 tests), f/modules (4: plain, rows code, safe start asked and after a crash).
- f/numbers (4): numbers and tables by name, taken out.
- f/objects (7): own class, as class, methods, overrider, unit record, not readable, call method.
- f/reload (4): take-out with a call held, none, dependents, C++.
- f/types (10): shapes, bits, globals, fields and their signatures, unbound, optional, same twice.
- f/varargs (6): variadic calls and hooks.
- fb/kit-area, kit-camera (with kit-camera-b), kit-gravity (with -b), kit-pointer, kit-projectile, kit-ride: one
  scenario each.
- fb/light (3), fb/world (10: draw world, instances, mesh, view, post, native and its refusal).
- g/anim (6): walk replace and events, blink. g/harness (23) and g/harness-r6 (28): the harness's own facts and
  refusals. g/layout (4), g/sound (3), g/stage (2). g/k-hostile and g/macho: empty folders.
- h/assets (7), audio (11, with capture mic and camera), code (2), fs (13), mods (16), net (10), options (2),
  player (2: one extra player, draw), registry (17), save (14), system (17), time (22), kit-net.kit (1).
- j/anim-play (10), blocklist (3), consent (9), controls (2), draw (4), effects (13), import (5), kit-menu, kit-screen,
  kit-stage, kit-talk (one each), materials (2), models (10), models-cook (4), modlog (11), modlog-host (0),
  modscreen (3), movie (5), refs (4), safestart (3: asked, crash, after), stage (1), startcheck (2), startval (5),
  ui (17).
- k/k1-hook ... k10-chain (19 single scenarios: hooks, arrays, core, frame, game calls, containers, destructors, json,
  refs, structs, text, tasks, C, classes, states, gone, kit), kit-hud, kit-menu-key.

### M-036 missing feature: behaviours with no test in any suite
- Evidence (from the list above and the test mods' `use kit.*` lines, found by grep over every .lum under audit-conf):
  - LumaKit modules no test mod uses: kit.cutscene, kit.player.character, kit.player.form, kit.replay, kit.shop
    (the 17 others appear at least once).
  - Two mods asking for the same single kit value (fov, mirror, shake, loading tips) and one of them stopping: no test
    (kit-camera's mods use mirror and controls only, fb/mods/kit-camera/source/main.lum:81-89). M-021 and M-033 are
    in that gap.
  - A real fault and the next start: j/safestart/6602-crash writes the flag file itself before the start
    (test.json: `"safe_start": "crash"`) and checks the Mods page's line; no test faults and then checks that the
    file was written, on any platform. M-031 (no write on Linux, macOS, Android) is in that gap.
  - player.make and remove repeated (h/player has one make), the M-030 leak.
  - A game call with a function pointer from outside the mod (M-029): no test hands a service a pointer into the game.
- Why: the gaps line up with the findings that the suites did not catch.
- Confidence: verified by listing (the folders, the grep, j/tests/safestart/6602-crash/test.json).

## Area 3 (round 2): the generators' per-function data

What exists per function (tools/lumacore/instrument.py:9-22, the row's facts; self_description.py:1-21 passes them
through unchanged, 134 and 158):
- name, signature, kind (free, method, static, constructor, destructor), owner, virtual, pure, const, overrides.
- marks: one entry per parameter, from the LUMA_ marks on a declaration (native/petari/include/Game/LumaMarks.hpp:21-27:
  LUMA_OUT, IN, INOUT, KEPT, COUNT(param), COUNT_FIXED(n), NULLABLE). All are parameter marks; there is no mark on the
  function itself.
- Not per function: the body's file and line (types and nerves carry file and line, self_description.py:96;
  function rows do not), and anything about when the function may run.

What would have to be added for a checker to refuse M-014's calls (none of it exists):
- A function-level fact, "needs the player", "needs a scene", "game thread only", per row. instrument.py already parses
  every function body with libclang to put the LUMA_HOOK line in it, so a body pass there can find an unchecked
  `getPlayerActor()->` (226 in MarioAccess.cpp alone, M-014) or a scene singleton read, and gen_types.py's merge can
  carry it to callers through the rows it already joins. A LUMA_ mark on the declaration (beside LumaMarks.hpp's) would
  let a person state what the pass cannot see.
- The checker side: crates/lumascript game calls read only signature and marks (game.rs:1-130, round 1), so it would
  need a rule that a call to a row with "needs the player" sits under a test that the player exists.

Checked in area 3 (round 2):
- gen_lumacore.py (68 lines, header read): writes class ids, class rows and a hash that `--check` compares, so the
  committed class rows cannot drift from the headers without the check failing (gen_lumacore.py:1-14).
- self_description.py (181 lines; 1-40 and the outline read): a row given twice stops the tool, an unknown owner or exe
  is null, nothing is guessed (self_description.py:21).
- Not read: gen_types.py (1593 lines) beyond its role in instrument.py's header; gen_classes.py bodies (235 lines,
  outline only); instrument.py beyond 1-40 and round 1's 1600-1700.

# Round 3

## Check 1 (M-027): does every LumaLink-built mod carry unwind data on Windows x64?

Yes, by the flags; only a library built by hand without them reaches the word scan.
- The C flags every platform shares: crates/lumascript/src/emit.rs:49-66 (`-std=c17 -O2 -nostdlib -ffreestanding
  -fno-stack-protector -fvisibility=hidden -fno-builtin-malloc -fno-common -Wall -Werror=implicit-function-declaration
  -Wframe-larger-than=4096 -Werror=frame-larger-than -fasynchronous-unwind-tables`). lumalink-core takes them as
  COMMON_FLAGS (crates/lumalink-core/src/build.rs:20).
- The one compile-and-link command for a platform: build.rs:814 (`--target=<triple>` + COMMON_FLAGS + the target's own
  flags), with the generated C (lumart.c and the mod's emitted C) and the mod's own C sources on the same command
  (build.rs:812-829). windows-x64 is `x86_64-pc-windows-msvc` with no extra code flags (build.rs:102); its link flags
  are `-shared -fuse-ld=lld -Wl,/NOENTRY -Wl,/NODEFAULTLIB -Wl,/NOIMPLIB -Wl,/Brepro` (build.rs:75).
- A mod's C++ sources: build.rs:796-806, COMMON_FLAGS minus `-std=` and the implicit-declaration error, plus
  `-std=c++20 -fno-exceptions -fno-rtti` (build.rs:23); `-fasynchronous-unwind-tables` stays.
- lumart.c has no assembly on x64 (its one `__asm__` is aarch64's `yield`, lumart.c:2335).
- The host's test is per image: has_unwind_data reads the library's exception directory size
  (native/host/luma_guard.cpp:178-189), then why_not_frames walks with RtlLookupFunctionEntry/RtlVirtualUnwind,
  allowing a leaf (no entry) only for the faulting function (luma_guard.cpp:144-153). A frameless leaf function gets
  no .pdata entry from clang, which is what that allowance covers.
- So on Windows x64 a LumaLink mod takes why_not_frames, which does not read luma_guard_call's saved rbx; M-027 hits
  hand-built C mods (the guard test mods, exception directory size 0) on Windows, and every mod on POSIX, where
  why_not_words is the only path (luma_guard.cpp:541).
- Confidence: verified in code (flags and the host's choice). A mod whose own C uses `__attribute__((naked))` or
  inline assembly still builds; such a function has no unwind entry, and a fault after it called the game is refused
  (kUnreadable, luma_guard.cpp:151), which errs toward not resuming.

## Check 2 (M-031, M-032): the safe-start file per platform

Files: `<settings>/mods/.safe_start` (galaxy::mods::safe_start_file, runtime/src/mods.cpp:1070-1073) and
`<settings>/mods/.code_crashed` (host_main.cpp:4921).

When it is armed (all platforms, the same host_main.cpp; Windows builds Nebula/NebulaVk/NebulaD11 from it,
CMakeLists.txt:590, 959, 1086; the SDL and Apple hosts too, native/CMakeLists.txt:407, platform/apple/metal.cmake:100):
- At the mods' scan, when this start is not a safe start and at least one mod is on: gCodeCrashFile = `.safe_start`
  (host_main.cpp:6101-6105).
- At svc_mods_start, when some mod changes the game and some mod has code: gCodeCrashFile = `.code_crashed`
  (host_main.cpp:4975-4982), so a crash from then on turns off code only.

When it is written: only in gather_crash_facts, an empty file (host_main.cpp:313-326). Its callers:
- Windows: the unhandled-exception and vectored handlers (host_main.cpp:1526, 1665, 1726; installed only
  `#if defined(_WIN32)`, 5845-5847), die (453) and svc_fatal (1861).
- Linux and Android: die (453) and svc_fatal (1861) only. A fault (SIGSEGV, SIGILL, SIGFPE, SIGBUS, SIGABRT) goes to
  platform/sdl/crash_log.cpp on_fatal (145-170): it logs the report and hands the signal to the previous handler; it
  never calls gather_crash_facts. The host installs it at start (host_window_sdl.cpp:1072-1076; Android through
  forward_output_to_logcat, 229-248).
- macOS and iOS: die and svc_fatal only. crash_log.cpp is `#if defined(__linux__)`; on Apple install_crash_log is an
  empty function (crash_log.cpp:185-188), so a fault is not even logged by Nebula.

When it is read: take_safe_start, once a process (mods.cpp:1075-1091: the first call removes the file and caches the
answer), called before the game module is chosen (host_main.cpp:6009) and again at the scan (6092, which gets the
cached answer). An empty `.safe_start` is kAfterCrash, a non-empty one kAsked (mods.cpp:1084-1086). `.code_crashed`
is read and removed in svc_mods_start (host_main.cpp:4921-4931).

What the player sees:
- `.safe_start` (a crash before code started or with no code): every mod is off for this start, the copy only, mods.ini
  untouched (host_main.cpp:6091-6100); log "safe start: the last run crashed with mods on; every mod is off for this
  start"; Settings > Mods top row "NEBULA CLOSED UNEXPECTEDLY WITH MODS ON. EVERY MOD IS OFF UNTIL THE NEXT START"
  (runtime/src/ui/native_settings.cpp:3518). The start after is normal again.
- `.code_crashed`: every non-dev mod's code is off for this start (host_main.cpp:4925-4931), each code mod's row says
  "THIS MOD'S CODE IS OFF AFTER A CRASH. RESTART TO TRY IT AGAIN" (mods.cpp:1338-1339). No mod is named.
- Windows in addition: the crash dialog and report naming the mod (crash_report.cpp:177-183, NEB-M03), offering only to
  open the report's folder (host_main.cpp:1787-1818).
- Android: the launcher's generic "GameCrashed" (LauncherModel.kt:521-530); no Kotlin or Java writes either file
  (grep for `safe_start` and `code_crashed` over *.kt, *.java, *.mm, *.swift: none).
- Linux, macOS, iOS: nothing; the next start runs every mod and its code again.

Per platform, a native fault in a mod's code (or the game's, with mods on):

| Platform | Flag written on a fault | Next start | Player told |
|---|---|---|---|
| Windows x64 | yes (handlers 1526/1665/1726) | `.safe_start`: all mods off once; `.code_crashed`: all code off once | crash dialog naming the mod; Mods page row |
| Linux | no (crash_log logs only) | everything on again | nothing in the game; the log has the report |
| Android | no (crash_log logs only) | everything on again | launcher "GameCrashed" |
| macOS | no (no handler at all) | everything on again | nothing; the system's crash report |
| iOS | no | everything on again | nothing |

This confirms M-031 and adds to it: on macOS and iOS Nebula installs no fault handler, so not even the log line is
written. The Windows path works when the file exists, as the coordinator's run shows.

## Area A (round 3): LumaKit, every module read in full

The reference pages under docs/modding/reference/lumakit/ are generated from the module's own comments and `pub`
declarations (the generator: D:\NebulaWork\agents\docs-new\tools\gen_kit.py; each page says "The module's source is
lumakit/source/X.lum"). So item (4), page against module, is a comparison of the comment with the code, and a
difference in the comment is a difference on the page.

What LumaKit installs whenever it is loaded (any mod that is on needs `lumakit`; the library loads only then,
host_main.cpp:6000-6001): 38 hooks (grep `^hook` over lumakit/source; listed per module below), 9 `on frame`
handlers (actor, cutscene, hud, item, menu, screen, stage, net/kit, ui/kit) plus one `on step` (replay), and 5
`on start` (area, ride, talk, player/character, player/state). Every hook runs on every call of its game function whatever module the mod uses. The hot ones:
MR::calcGravityVectorOrZero (gravity.lum:179; called by every actor that asks its gravity, several times each step),
MarioActor::movement twice (area.lum:256 before, player/state.lum:321 after), MarioActor::drawMarioModel twice
(player/character.lum:184, player/form.lum:475), MR::getPlayerStickX (camera.lum:465), MR::isPressedPause
(menu.lum:369), MR::getStarPieceNum (pointer.lum:189), MR::isPlayerLuigi (player/character.lum:202). Each returns
early when nothing of its module is in use; their cost is the hook call itself.

### kit.gravity (gravity.lum, 223 lines)
1. Raw game uses: MarioAccess::getPlayerActor (124, 183: guarded); MR::calcGravityVector (125: under the player
   guard); MR::initDefaultPos, MR::getPlacementArg0NoInit, makeActorDead in KitGravityPlace.init (160-163: runs in a
   scene's placement, so a scene exists); make_gravity (169, its none handled); `actor` in the calcGravityVectorOrZero
   hook (184: `guard let`). All guarded.
2. Shared single values: none. Overrides are a stack with owners (30-33, 83-91); a stopped mod's overrides end and the
   one under them holds (208-215).
3. Per frame: hook on MR::calcGravityVectorOrZero (179-191) returns at once with no override; with one, a compare per
   call. Hook after MarioActor::updateGravityVec (194-205): a loop over the overrides.
4. Page against code: matches (gravity.md is the header comment and the signatures).
5. Crash from a mod's argument: none found. A shape's `vector` closure runs inside PlanetGravity's call (137-153);
   a script error there is the mod's.

### M-037 wrong behaviour: kit.gravity's timed overrides count down once per MarioActor, so with svc.player's extra players a `frames` override ends early
- Evidence: lumakit/source/gravity.lum:194-205 (`hook after MarioActor::updateGravityVec(reset, use_position)`
  decrements every timed override, with no test of which MarioActor it runs for); native/include/luma_player.h:54-55
  ("a hook on MarioActor's code runs for it too: `this` tells them apart"). The override itself is applied only to
  the main player (gravity.lum:183-187).
- Why: a mod gives the player gravity for 120 frames while another mod (or the same one) has two extra players
  standing: each step decrements three times and the override ends after 40 steps.
- Confidence: verified in code; the count needs a run (an override with frames and one extra player).

### kit.shop (shop.lum, 235 lines)
1. Raw game uses: MR::getCoinNum, MR::getStarPieceNum (44, 51) and MR::payCoin, MR::addStarPiece (66, 68): not
   guarded (M-038); MR::isDead on the keeper (117: after `get()`); MR::startSystemSE (183, 192): safe without a scene.
2. Shared single values: `open_shop`/`shop_owner` (35-38): one shop for all mods, documented newest-wins (11), the
   stopped owner's shop closes (227-235). Not a finding.
3. Per frame: nothing of its own; an open shop waits in kit.screen.
4. Page against code: the header comment (shop.lum:3-4, shop.md) says the price is taken with "MR::incCoin and
   MR::addStarPiece with the price as a negative count"; the code uses MR::payCoin for coins (60-67), and its own
   comment says why incCoin would be wrong (a heal). M-039. The header says the shop closes "if the keeper is gone"
   (10-11): it is tested only between choices (161-165); a keeper killed while the list is open closes nothing until
   the player chooses. Folded into M-039.
5. Crash from a mod: shop.open outside a stage (M-038).

### M-038 crash: kit.shop.open in a scene without a stage's playing result (the title, the file select, a mod's own menu before any stage) ends the game
- Evidence: shop.lum:165 calls `held(s.currency)` before the list shows; held calls MR::getCoinNum or
  MR::getStarPieceNum (44, 51). native/petari/src/Game/Util/SceneUtil.cpp:36-38 and 373-375:
  `getScenePlayingResult()->getCoinNum()` with getScenePlayingResult = `MR::getSceneObj<ScenePlayingResult>(...)`,
  which is `getSceneObjHolder()->getObj(id)` with no test (include/Game/Scene/SceneObjHolder.hpp:174-176); a scene
  that never made that object gives null. EventUtil.cpp:87-93 is the same for star bits outside the Comet Observatory.
  The fault's pc is in the game module, so the guard does not resume it (M-014).
- Why: a mod opens a shop from `on start`, from a kit.menu entry on the title, or from its own option screen.
- Confidence: verified in code (the call chain and the missing test). Which scenes lack the object needs a run: a mod
  whose `on start` runs `shop.open` on the title screen.

### M-039 docs wrong: kit.shop's header says coins are taken with MR::incCoin and a negative count, and that the shop closes when the keeper goes; the code uses MR::payCoin and checks the keeper only between choices
- Evidence: shop.lum:3-5 and 10-11 (and docs/modding/reference/lumakit/shop.md, generated from them) against
  shop.lum:60-70 (`MR::payCoin(price as i32)`) and 161-170 (keeper_gone before each list only).
- Why: a modder reading the page expects the HUD's heal side effect of incCoin to be avoided only if they read the
  code; a keeper killed while the list shows leaves the shop open.
- Confidence: verified in code.

### kit.pointer (pointer.lum, 244 lines)
1. Raw game uses:
   - MR::isStarPointerInScreen(0), MR::getStarPointerScreenPosition(0) (21, 24): read the star pointer director's
     controller (StarPointerUtil.cpp:60-62, 518-534), which the game system holds; no scene needed.
   - MR::calcWorldRayDirectionFromScreen and MR::getCamPos (32, 40): both read `getCameraContext()`, a scene object with
     no null test (CameraUtil.cpp:29-31, 113-117, 191-196). `pointer.world()` called in a scene that has no camera
     context faults in game code. Suspected only: which scenes lack a CameraContext needs a run (a mod calling
     pointer.world() on the title screen). Noted under M-038's pattern, not a separate finding.
   - MR::getFirstPolyNormalOnLineToMap (40): the map's collision, a scene's.
   - In the shooter hooks (163-222): `self.mPort`, `self.mLockOnSensor`, `self.mSearchSensor`, `sensor.mHost`
     (`if let`), MR::isPressedJump, MR::getStarPieceDirector (`guard let`), MR::getCoinNum (149, called from tryShoot,
     so inside a stage), MR::addStarPiece, MR::payCoin, MR::startCSSound. Inside the game's own call: guarded by
     construction.
2. Shared single values: none; throws and grabs are stacks with owners (57-64), a stopped mod's sets end and the one
   before holds (225-244).
3. Per frame: hooks before and after StarPieceShooter::tryShoot (each shooter, each step), after MR::getStarPieceNum
   (every call in the game; returns at once outside tryShoot), before StarPieceShooter::shoot.
4. Page against code: matches. Not said on the page: the grab also fires for the co-star's shooter (the hook runs for
   every StarPieceShooter and tests `self.mPort`'s press, 166), and `on_grab`'s bool answer is ignored (173).
5. Crash from a mod: none from arguments. A `Cost.StarBits(n)` or `Coins(n)` with n above 2^31 turns negative in the
   `as s32` (148-149, 156-157): the "cost" then adds star bits or coins. Not a crash.

### kit.projectile (projectile.lum, 336 lines)
1. Raw game uses: inside KitShot's own init/movement/attackSensor/kill (193-312), on `self` and the sensors the game
   passes (`guard let` on both, 292-294); MR::isDead and `.get()` on held refs (77-80, 322-326); MR::emitEffect,
   MR::startSound, MR::showModel/hideModel, MR::on/offCalcGravity, MR::validate/invalidateHitSensors on an object
   just read through `get()` (114-164). All guarded.
2. Shared single values: none.
3. Per frame: each live shot, one map ray (MR::getFirstPolyNormalOnLineToMap, 272) per step in KitShot.movement.
4. Page against code: matches. Not said: `life_frames: 0` means the shot never ends by age (257-263 counts only above 0).
5. Crash from a mod: none found.

### M-040 maintainability: kit.projectile's slot lists only grow; a slot is reused only while its object is alive in the current stage, so each new stage, and each throw that cannot make an object, adds slots for the session
- Evidence: projectile.lum:52-64 (ten parallel lists), 75-86 (a slot is taken again only when `h.get()` is an actor and
  it is dead), 87-104 (otherwise a new slot is pushed; when `make` gives nothing, 102, the pushed slot keeps
  `shot_obj = none` and can never be taken again). Nothing removes a slot.
- Why: a session that plays many stages with a mod throwing several shots, or a mod that throws while no stage can make
  one (on the title), grows the lists and the linear scan in every throw without end.
- Confidence: verified in code; the size reached needs a run.

### kit.talk (talk.lum, 285 lines)
1. Raw game uses: MR::createTalkCtrlDirect on the speaker (89, after `who.get()` is tested, 74-77);
   MR::tryTalkForceAtEnd and MR::isDead (124, 130, each after `get()`); `c.endTalk()` (263, after `get()`). Guarded for
   a gone speaker. Not guarded for a scene with no talk director: createTalkCtrlDirect makes `new TalkMessageCtrl` and
   its message (native/petari/src/Game/Util/TalkUtil.cpp:69-73); whether that needs a stage's talk objects was not
   followed. Suspected, needs a run: `talk.say` on an actor of a mod's menu scene.
2. Shared single values: the line being said is one set of globals for every mod (29-35: `ctrl`, `speaker`, `words`,
   `line_id`, `talk_owner`, `talking`). M-041.
3. Per frame: none of its own; a running `say` polls `talk_done` once a frame (113). The hook on NebulaMods::text (51-57)
   runs on every message lookup: two text compares.
4. Page against code: matches. Not said on the page: each line makes a new TalkMessageCtrl with `new` (TalkUtil.cpp:70)
   that nothing deletes; lines in a long stage add up until the stage's memory goes.
5. Crash from a mod: none found from arguments; the scene question in 1.

### M-041 wrong behaviour: kit.talk says one line at a time for all mods with no hand-over; a second `say` while a line shows takes the globals, and whichever ends first ends both
- Evidence: talk.lum:29-35 (one `ctrl`, `speaker`, `words`, `line_id`, `talking`); start_line (73-98) overwrites them
  with no test that a line is showing; `say` (107-113) waits on `talk_done`, which reads the globals (116-131), and its
  `defer end_line()` (111) clears them (100-104) for whoever holds them. The first line's TalkMessageCtrl is then no
  longer driven (tryTalkForceAtEnd is called only on `ctrl`, 130). talk.md (generated from 1-9) does not say one line at
  a time, unlike kit.screen and kit.shop.
- Why: mod A's NPC says a line, mod B's sign says one in the same frames (or one mod runs two `say` tasks): A's task now
  waits on B's balloon; when B's ends, A's `say` returns at once; A's balloon is left without the call that ends it.
- Confidence: verified in code (the shared globals and their use). What the left balloon does on screen needs a run.

### kit.progress (progress.lum, 320 lines)
1. Raw game uses: GameDataFunction::calcCurrentPowerStarNum, hasPowerStar, setGameFlagPowerStarSuccess (77, 88, 93).
   They go through getCurrentGameDataHolder (GameDataFunction.cpp:271-273, the current file) and makeAccessor, which
   gives the empty `mNoGalaxy` storage for a galaxy name the game does not know (GameDataGalaxyStorage.cpp:203-206), so
   an unknown name or a mod galaxy is safe. A scenario of 0 or above 32 reaches `hasPowerStar(mScenarioNum - 1)`
   (GameDataGalaxyStorage.cpp:29-31), a bit test with an out-of-range shift: undefined in C++, no crash in practice.
   `give_star` with a scenario above 2^31 turns negative (93) and goes to the game unchanged (107-109). Not a crash.
2. Shared single values: none. Comets are a list per owner and the newest for a galaxy answers (151-160); a stopped
   mod's comet goes and the earlier mod's answers again (310-319). on_star functions are asked newest first (112-123).
3. Per frame: the hook on GameDataFunction::calcCurrentPowerStarNum (71-73) reads the save value "kit.progress.stars"
   and decodes its JSON list on every call, mods or not (no early return). The three comet hooks scan the comet list.
   How often the game asks the total per frame was not counted.
4. Page against code: the header (5-6) says the kit's stars are counted "wherever the game asks for it
   (GameDataFunction::calcCurrentPowerStarNum, MR::getPowerStarNum)"; only calcCurrentPowerStarNum is hooked. Whether
   MR::getPowerStarNum goes through it was not followed. Stars of a mod galaxy stay counted in the total while that
   mod is off (the list is the kit's, 52-59, 71-73); the header says they "stay on the file", not that they still count.
5. Crash from a mod: none found.
- Checked: a mod's stored function (on_star, on_new_file) called from a kit hook that calls back into the kit is
  attributed to that mod, because each export wrapper takes the calling library's own mod as `lm_caller` and sets
  `lumart_caller` from it (crates/lumascript/src/emit/library.rs:432-477); `caller()` outside an export is the kit
  itself (emit/expr.rs:1836-1837).

### kit.hud (hud.lum, 394 lines)
1. Raw game uses: none; everything goes through svc.ui (ui.open, ui.pane, ui.set_text, set_visible, set_scale) and
   svc.time (time.now().state, 105-108), and the screen is opened only in Play or Menu state, so no scene question.
2. Shared single values: the popup (63-66, 338-343), documented as replaced by a new one; a stopped owner's popup is
   hidden (354-361). Slots carry an owner each (62, 189) and a stopped mod's slots are freed (348-353).
3. Per frame (364-394): returns at once with no element; else ready() (ui.alive), a loop over 48 slots, a ui.set_text
   per running timer.
4. Page against code: matches. `clock` (233-238) shows frames at 60 a second; `on frame` is the game step
   (GameSystem.cpp:279), so the time shown is right at any display rate.
5. Crash from a mod: none. A handle whose slot is out of range is ignored (200, 208, 295, 318); a slot below 48 used
   before any element was ever made indexes the empty lists (200: `used[slot - 1]` with `used` empty), a script error
   at the caller's line.

### M-042 wrong behaviour: kit.hud's handles are bare slot numbers checked against no owner; a mod's handle to an element that went (removed, timed out, or its mod stopped) changes or removes whatever element of any mod now holds that slot
- Evidence: hud.lum:16-37 (Text, Counter, Meter, Timer are `{ slot: u32 }`), change (199-205), free (207-224),
  set_meter (287-300), set_timer (317-323), run_timer (326-331): each tests only `used[slot - 1]`, never
  `owners[slot - 1] == caller()`. take (177-197) gives the first free row of the corner, so a slot freed by a timed text
  (on frame, 377-380) or a removed element is given to the next element made in that corner, by any mod.
- Why: mod A shows `hud.text(TopLeft, "Hi", 120)` and keeps the handle; after 120 frames the slot is free; mod B makes
  a counter in TopLeft and gets the same slot; A's later `t.set("Bye")` overwrites B's counter, A's `t.remove()` removes
  it. Item, actor and shop handles in the kit check the owner (e.g. actor.lum:129, 757); hud's do not.
- Confidence: verified in code.

### kit.area (area.lum, 347 lines)
1. Raw game uses: `o.isInVolume`, `area.mFormType`, MR::calcCubeRotate on AreaObj read through `ref.get()` (147-148,
   161-163, 199-205); MR::getPlayerPos (266, `guard let`); MR::setPlayerWalkingResist (288) and `self.mMario.addTrans`
   (291-293, `if let`) inside the hook on MarioActor::movement, so a player exists; `o.mObjArg0` in the area's own init
   (314). Guarded.
2. Shared single values: none in the module; the camera an area takes is a kit.camera control (a stack, 235, 246).
3. Per frame: hook before MarioActor::movement (256-295): drops gone areas, then for each kind a scan of every area
   standing (`area_inside`, 144-155: kinds x areas `isInVolume` tests per step), plus the callbacks.
4. Page against code: the header (4-5, on area.md) says a kind's areas are placed in other mods' stages as
   "<mod id>/<name>Cube"; the code aliases the bare "<name>Cube" (102). M-043.
5. Crash from a mod: none found.
- The hook runs for every MarioActor (M-045).

### M-043 wrong behaviour: kit.area, kit.gravity and kit.ride give the game bare names for their kinds; two mods with one name clash and the second's kind silently places the first's, and a data kind read at LumaKit's start is aliased for LumaKit, not its mod
- Evidence:
  - The alias names: area.lum:102 (`"{k.name}{shape_ends[s]}"`), gravity.lum:109 (`s.name`), ride.lum:98 (`m.name`),
    each with the result ignored. kit.actor and kit.item use `"<mod id>/<name>"` (actor.lum:164-171, item.lum:211-241).
  - Aliases are one space for the whole session: the kernel refuses a name that is "another mod's alias"
    (native/petari/src/Game/System/LumaClasses.cpp:897-910) and returns -2; the kind was already pushed with its
    number (area.lum:93-104, gravity.lum:105-111), so the second mod gets a KindId/ShapeId, and its stage's "WindCube"
    makes the first mod's kind (the alias's argument is the first kind's number).
  - docs/modding/concepts/lumakit.md:64: "Names never clash. LumaKit names your kinds, items and forms MOD-ID/NAME to
    the game"; area.lum:5 says the same for areas.
  - Data kinds: area.lum:121-129 reads every mod's kit/area files in `on start`, where `lumart_caller.mod` is 0, so
    alias_class registers the alias for LumaKit itself (crates/lumascript/rt/lumart.c:3846-3848). When that mod stops,
    its kind goes dead (area.lum:330-336) but the alias stays LumaKit's; if the mod is turned on again (a reload), its
    `find` makes a new kind whose alias is refused (the name is LumaKit's, LumaClasses.cpp:907), and its placed areas
    stand inert with "no mod defines it" (area.lum:315-317).
- Why: two area mods both define "Wind" (a likely name), or one gravity mod's "Spiral" meets another's: the second
  mod's areas do the first mod's thing. A mod reloaded in the Mods page loses its data-defined areas until restart.
- Confidence: verified in code (names, the kernel's rule, the caller in `on start`). The reload case needs a run:
  a mod with kit/area/Wind.json, turned off and on, then a stage with WindCube.

### kit.camera (camera.lum, 492 lines)
1. Raw game uses:
   - MarioAccess::getPlayerActor (84, 134: `if let`); `h.get()` on held actors (93, 120, 170, 432).
   - `camera.now()` (361-369): MR::getCameraDirector and getCurrentCameraMan are tested, but MR::getCamPos,
     MR::getCameraWatchPos and MR::getCamYdir are not; they read the scene's CameraContext with no test
     (CameraUtil.cpp:29-31, 191-196). M-038 (extended below).
   - free_step (372-409): MR::getCamPos, MR::getCameraWatchPos, MR::getMoveStickX/Y, MR::testCorePadButton1/2; it runs
     only from the CameraDirector::calcPose hook, inside a scene with a camera.
   - In the calcPose hook: `self.mPoseParam1` (`guard let`), `self.getCurrentCameraMan()` (`if let`).
   - `side()` (286-292): `the_player()!`, a script error at the caller's line with no player, as its comment says.
2. Shared single values: fov (56-57, 343-350), mirror (59-61, 321-325), shake (63-66, 353-358): M-021 stands, read
   again in full. In addition: `fov(none)` (347-348) clears another mod's fov without being its owner, and
   `mirror(false)` from any mod turns off another's mirror and frees the post program (336-339).
3. Per frame: hook after CameraDirector::calcPose (439-462): with no control, no shake and no fov, one compare; with a
   control, view.main_rig or view.main_eye each step. Hook after MR::getPlayerStickX (465-469): one compare per call.
4. Page against code: matches, except M-021's behaviour is the page's own wording (11-12: "takes a stopped mod's
   away"), which hides that the earlier mod's value is not given back while lumakit.md:63 promises it.
5. Crash from a mod: `camera.now()` outside a stage (M-038).

### kit.cutscene (cutscene.lum, 363 lines)
1. Raw game uses: MR::offPlayerControl / MR::onPlayerControl (145, 99), MR::isStopCinemaFrame,
   MR::tryScreenToFrameCinemaFrame, MR::tryFrameToScreenCinemaFrame (70, 91, 152-158): not guarded (M-038 extended).
   MR::isPressedPause / isHeldPause (108, 349): pad reads. In shots: `at.get()`, `who.get()`, MR::isDead,
   MR::tryStartAction, MR::isActionEnd (186-191, 217-219, 230-241), each after `get()`. MR::getPlayerGravity with `??`.
   NameObjFinder::find and as_class in find (278-283).
2. Shared single values: one scene for all mods, documented (22); a stopped owner's scene is put back (358-363). The
   camera is a kit.camera control, the music svc.audio's single music (165-167: "one for every mod").
3. Per frame: `on frame` (347-351) one compare; a playing scene's task each frame; hook after
   GameScene::isPermitToPauseMenu.
4. Page against code: matches.
5. Crash from a mod: play outside a stage or with no player (M-038 extended).

### kit.screen (screen.lum, 388 lines)
1. Raw game uses: MR::isPressedFire, isPressedJump, isPressedLeft, isPressedRight on pad 0 (326-361): controller
   reads, no scene needed. Everything else is svc.ui, svc.time and svc.draw; the screen opens only in Play or Menu
   (66-69).
2. Shared single values: the open screen (30-42), documented one at a time (4-5); the loading tips (M-033). The answer
   a waiter reads is one global too (41): a choose ended by a newer screen returns none only while `answer` is still -1
   (186-188). If the newer screen is answered before the older waiter's `wait until` is next tested, the older waiter
   returns the newer screen's row. Suspected, needs a run (two mods opening `choose` in consecutive frames); not a
   finding.
3. Per frame (303-363): a loop over the layouts opened through `layout` with ui.alive each; with a screen open,
   ready() and the menu read.
4. Page against code: matches, but for the broken sentence of M-034 (269-270).
5. Crash from a mod: none.

### kit.menu (menu.lum, 477 lines)
1. Raw game uses: in the PauseMenu hooks, `self` and MR::getPane on it (`guard let`, 313-327) and the panes'
   mTranslate/mScale (329-344): inside the game's own call. `back_at!`, `quit_at!`, `group_scale!` (337-343) are set
   just before (328-332). Everything else is svc.ui.
2. Shared single values: at most ten entries in all, for every mod (16, 46-49), documented; the page and the decide
   in progress are the menu's. Entries are owned and checked (69-76).
3. Per frame (399-435): with the pause menu closed, one compare; open, ui.shared_focus and a loop over at most four
   rows. Hooks: before/after PauseMenu::exeSelecting, after MR::isPressedPause (every call), after PauseMenu::appear,
   before PauseMenu::kill.
4. Page against code: matches, but M-044.
5. Crash from a mod: none.

### M-044 wrong behaviour: kit.menu moves the game's Back and Quit only when the pause menu opens; an entry added, removed or stopped while the menu is open leaves them in the wrong place
- Evidence: place_game_buttons (312-346) is called only from the PauseMenu::appear hook (375-381), with
  `room = ids.len() > 0`. add_pause_entry (45-58), remove (94-104) and the stop handler (464-477) call only refresh()
  (246-303), which never calls it.
- Why: a mod that adds its entry from a pause-menu callback, or whose last entry is removed (or whose mod is stopped
  from the Mods page) while the menu shows: the rows draw over full-size Back and Quit, or Back and Quit stay shrunk and
  moved up with no rows under them until the menu is closed and opened again.
- Confidence: verified in code; the look needs a run.

### kit.ride (ride.lum, 428 lines)
1. Raw game uses: all inside KitMount's own init, receiveMessage, receiveMsgEnemyAttack, movement and
   calcAndSetBaseMtx (238-402), on `self`, the sensor's host (`guard let`, 282-283), the rider through `h.get()`
   (303-304), the seat joint (`guard let`, 307); MR::startBckPlayer (289) inside the player's own AutoRushBegin message;
   MR::getCamZdir/getCamXdir (355-359) inside a mount's movement (a stage). MR::validateClipping and
   MR::endBindAndPlayerJump on the mount through `r.get()` (180-195). Guarded.
2. Shared single values: one ride (60-65), the player's, documented (15). Definitions per owner.
3. Per frame: each mount's movement; a ridden one does the drive (345-385).
4. Page against code: the header (5) says mounts are placed as "<mod id>/<name>" in other mods' stages; the code
   aliases the bare name (98). In M-043. The header (15) says one rider, the player; any MarioActor's sensor that sends
   AutoRushBegin is taken as the rider (282-287), so an extra player of svc.player can mount, while
   MR::startBckPlayer (289) plays the action on the main player.
5. Crash from a mod: none found.
- `obj_ref`/`obj_def` (58-59) are pruned only in the stop handler (407-422), never when a stage's mounts go: the
  M-040 pattern (lists that grow for the session).

### M-046 wrong behaviour: when a mod stops while the player rides one of its mounts, kit.ride calls that stopped mod's on_dismount
- Evidence: ride.lum:407-418: the stop handler, for a ridden mount of the gone mod, calls `let_off(true)`, which calls
  `d.on_dismount` with the mount (196-198). Other modules skip the stopped mod's callbacks on purpose: kit.area
  (`leave(i, none)`, area.lum:243-253, 331-333: "without its callback (its mod is gone)"), kit.player.form
  ("Ended.ModStopped: its closures are not called", form.lum:20-21).
- Why: the mod's own code runs after its `on stop`, against state it may already have released (a file it closed, a
  handle it ended); a script error there is reported for a mod that is being taken out.
- Confidence: verified in code; the effect depends on the mod's own on_dismount.

### kit.stage (stage.lum, 646 lines)
1. Raw game uses:
   - MR::requestChangeStageInGameMoving (206) with any galaxy name and scenario the mod passes: no test that the
     galaxy exists. A name the game cannot load goes to the game's galaxy move (SequenceUtil.cpp:57-63). Suspected
     fault or fatal error, needs a run (`stage.go_to("NoSuchGalaxy", 1)`).
   - MR::getCurrentStageName (`??`), MR::getCurrentScenarioNo (368-369, 591): inside the game's stage hooks.
   - In KitStarHolder and KitPlanet init (410-417, 506-535): `self`, `body!` after MR::addHitSensorMapObj (531; its
     result unwrapped with no test; a script error, not a fault, if it ever gave none).
   - h.mPosition and MR::requestAppearPowerStar on a holder read through `get()` (440-443).
2. Shared single values:
   - Galaxies and domes are one per name for all mods (42-53, 60-70): a second mod's `define_galaxy` with the same
     name takes the first mod's galaxy and its ownership; when it stops, the galaxy is gone for both (227-233). M-047.
   - Music per galaxy is a stack (341-363). Hubs are a stack per owner (168-191).
3. Per frame: `on frame` calls read_data (153-155), which calls `mods.needing()` every frame and scans it. Hooks:
   after NebulaMods::hubStage, GameDataFunction::isAppearGalaxy, GameDataConst::getPowerStarNumToOpenGalaxy,
   GameDataFunction::getPowerStarNumOwned (each star of a mod galaxy asks progress.has_star, which decodes the save's
   JSON star list), GameDataFunction::hasPowerStar, MR::startStageBGMFromStageName, GameScene::startStagePlayFirst.
4. Page against code: M-048 (code-built fields). The header lists `comets_of`, `music`, `appear_star`, `build`;
   matches otherwise.
5. Crash from a mod: `go_to` with a bad name (suspected, above).

### M-047 wrong behaviour: kit.stage galaxies and domes are one per name for all mods; a second mod's definition takes the first's, and when it stops the galaxy is gone for both
- Evidence: stage.lum:42-53 (`galaxy_for`: same name, any owner, replaced and `owners[i] = owner`), 60-70 (domes the
  same), stop handler 227-242 (removes by owner). The comment at 37 says "a second of the same name replaces the first"
  for "the calling mod"; the code does it across mods. lumakit.md:63 promises the earlier request holds again.
- Why: two galaxy packs both define "ModGalaxyA" or a dome "Observatory2": the second wins; turning it off removes the
  galaxy from the first pack too until the first mod defines it again (it does not; data is read once per mod,
  129-151).
- Confidence: verified in code.

### M-048 wrong behaviour: kit.stage.build's gravity fields never pull; each load of a built stage defines a new gravity shape and alias that nothing places or ends, until the session's 4096 alias rows run out
- Evidence: stage.lum:613-616: for each field of a built stage, `gravity.define_shape(shape_of(f, built_shapes))`.
  define_shape (gravity.lum:105-111) only records the shape and aliases its name to KitGravityPlace; a gravity is
  made only when a stage places that name (gravity.lum:156-175). Nothing makes a "kit.stage.build.N" object
  (grep: the name appears only in shape_of, stage.lum:576-582). The comment at 542 says the shapes are "ended at the
  next stage"; nothing ends them. The call runs in a hook (caller() is LumaKit, lumart.c:3847), so the shapes and
  aliases are LumaKit's and never go with the mod. Alias rows: kMaxAliases = 4096 for the session
  (LumaClasses.cpp:88, 920-923).
- Why: a mod builds a planet stage with `b.field(Field.Point { at: centre })`: the player is not pulled to it. Each
  time that stage loads, one more shape and alias stay; after enough loads every mod's alias_class is refused
  ("the session's 4096 aliases are all standing").
- Confidence: verified in code (no placement, no end). The pull missing needs a run (a built stage with a Point field).

### kit.player.state (player/state.lum, 656 lines)
1. Raw game uses: MarioAccess::getPlayerActor (119; every public entry tests it: 511, and finish takes `MarioActor?`);
   MR::isPlayerInWaterMode, MR::isOnGroundPlayer, MR::getPlayerGravity (`??`) inside the player hook or after the
   player test; `p.mMario` (`if let`/`guard let`, 171, 224, 258); p.changeAnimation, p.isAnimationRun; MR::emitEffect
   with the mod's effect name (165-167): an unknown name is safe (EffectKeeper::getEmitterOrNone gives an empty
   emitter, native/petari/src/Game/LiveActor/EffectKeeper.cpp:222-237); MR::startSoundPlayer; MR::isSensorEnemy and
   MR::sendArbitraryMsg in the attackSensor hook (359-377); setMarioConstant/getMarioConstant by name (556-579).
   Guarded.
2. Shared single values: one move in progress (92-99), documented ("a move in progress ends first", 150); a stopped
   mod's move ends without its closures (633-636). Tunings are a product of all in force, each undone exactly
   (541-567, 642-655).
3. Per frame: hook after MarioActor::movement (321-356): returns for an extra player (`is_player_one`, 312-318); with
   no move, a loop over the defined moves reading each action (`input.read`). Hook after MarioActor::attackSensor.
4. Page against code: matches. Not said: a tuning's base value is the constant's value when a tuning first touches it
   (578-579); a change made to that constant later by anything else (a mod's set_number on MarioConst, the game) is
   overwritten by the next apply() (556-557) and lost when the last tuning ends.
5. Crash from a mod: none found.
- This module tests `is_player_one(self)` in its MarioActor hooks; kit.area and kit.gravity do not (M-045).

### kit.player.form (player/form.lum, 595 lines)
1. Raw game uses: MarioAccess::getPlayerActor (132-134); set/settle/kit_step test it (`guard let`, 340, 513).
   p.setPlayerMode (276), MR::emitEffect / MR::deleteEffect with the form's effect (302, 242: an unknown name is safe,
   EffectKeeper.cpp:222-237), MR::startSubBGM / MR::stopSubBGM (307, 247), MR::isPlayerLuigi (292), the sub meter
   through `r.get()` (259-263, 327-333), inst.of(p) (266, 314). All after the player test. Guarded.
2. Shared single values: the holder is a stack per mod (the newest holds, a stopped mod's entry ends with
   Ended.ModStopped and no closures, 20-21); sound. The form's music is the game's single sub BGM (MR::startSubBGM, 307),
   not svc.audio's music: a kit.stage or kit.cutscene music change and a form's music do not know of each other.
3. Per frame: kit_step (509-) from kit.replay's `on step`: one length test with no form. Hooks: after
   MarioSubMeter::init, after MarioActor::initDrawAndModel, before MarioActor::drawMarioModel (every draw of every
   MarioActor), before resetPlayerModeOnDamage, after setPlayerMode.
4. Page against code: matches, but M-045 (the page says "the player"; the hooks act for every MarioActor).
5. Crash from a mod: none found.

### kit.player.character (player/character.lum, 510 lines)
1. Raw game uses: MarioAccess::getPlayerActor (70-72, `if let`/`guard let` everywhere); p.mPosition/p.mRotation (400);
   svc.player.make/remove (400-405, 453); as_class (403). In the wraps of MarioActor::changeAnimation,
   changeAnimationNonStop, changeAnimationUpper and playSound (210-252), `self` is the game's own call. Guarded.
2. Shared single values: the plays are a stack with owners (52-53, 353-370); the stop handler removes the gone mod's plays
   and any play of a character that went (468-490), then settle(); sound. One look shown (59-61), for the holder.
3. Per frame: no `on frame`. The four wraps run on every animation change and every player voice of every MarioActor,
   each a loop over `seconds` (109-120, at most a few rows) and a map lookup. drawMarioModel hook: one compare and the
   same loop.
4. Page against code: matches. Not said: an extra player made by a mod through svc.player directly (not through
   second_player) plays the holding character's clips and voice (character_of falls back to now(), 119), while its body is
   drawn as the game's (the skip at 184-188 is for player 1 only).
5. Crash from a mod: none found. `CharacterId { n }` is constructible by a mod; every use goes through slot_of (89-94)
   or compares (373).
- This module tests player 1 in initDrawAndModel and drawMarioModel (164-169, 184-188), the right pattern; kit.player.form
  does not (M-045).

### M-045 wrong behaviour: kit.area, kit.gravity and kit.player.form act in MarioActor hooks with no test that the actor is player 1; svc.player's extra players trigger the main player's effects
- Evidence:
  - svc.player makes extra MarioActors whose game calls go through the same hooks (native/include/luma_player.h:54-55).
    kit.player.state and kit.player.character test player 1 (state.lum:312-318, character.lum:164-169, 184-188); these do
    not:
  - kit.player.form, after MarioActor::initDrawAndModel (form.lum:464-472): any MarioActor's making sets `applied = 0`
    and `look_model = none` without detaching the attached look, then settle() takes off and puts on the holder again on
    player 1 (339-358, 280-335). With a form holding, an extra player made mid-level leaves the old look attached to player
    1 with its handle lost (never detached until the scene ends), attaches a second, emits the aura again and restarts the
    form's sub BGM.
  - form.lum:475-477: drawMarioModel is skipped for every MarioActor while a look shows, so extra players are invisible.
  - form.lum:480-484: resetPlayerModeOnDamage is skipped for every MarioActor by player 1's form.
  - form.lum:488-505: an extra player's setPlayerMode (it takes a Fire Flower) compares its mode with player 1's form and
    ends player 1's form (`end_top(Ended.Replaced)`).
  - kit.area (area.lum:256-295): inside and outside are decided by player 1's position (MR::getPlayerPos, 266) in
    every MarioActor's movement; on_inside is called once per player per step with that player as its argument (283),
    on_enter/on_leave with whichever actor moved first that step (272-274), MR::setPlayerWalkingResist is applied again
    per actor, and `self.mMario.addTrans(push)` (291-293) pushes each extra player by player 1's area.
  - kit.gravity (gravity.lum:194-205): M-037.
- Why: any mod that uses svc.player (a co-op mod, kit.player.character.second_player) together with a form, an area kind
  or a timed gravity.
- Confidence: verified in code (no test, the hook dispatch per instance); the visible results need a run
  (second_player with a form holding).

### kit.item (item.lum, 638 lines)
1. Raw game uses:
   - give (75-95): MR::incCoin (77) is `NebulaMods::fire` then `::getScenePlayingResult()->incCoin` then
     incPlayerLife (native/petari/src/Game/Util/SceneUtil.cpp:359-363); MR::addStarPiece (78) reads the playing result
     outside the observatory (EventUtil.cpp:95-101); MR::incPlayerLife (84) is MarioAccess::incLife, which calls
     `getPlayerActor()->IsMarioSwimming()` with no test (PlayerUtil.cpp:392-394, MarioAccess.cpp:282-286);
     MR::incPlayerLeft writes the current game data (EventUtil.cpp:39-42, GameDataFunction.cpp:218-220). None guarded.
     M-049.
   - Gives.Call: MR::getPlayerDemoActor with `if let` (88). Gives.Form: form.set (guarded).
   - KitItem (399-542): inside its own init, movement and attackSensor; the cage through `c.get()`; `make("CrystalCageS")`
     with `if let`. Guarded.
   - archive_of (307-313): MR::isFileExist on names a data file gives; no scene needed.
2. Shared single values: `making` (70) is set and cleared around one make (550-552); safe on the game thread.
   Definitions are per owner, aliases "<mod id>/<id>" (222), re-taken on restart (225-236). Sound.
3. Per frame: `on frame` (607-609) read_data, which calls `mods.needing()` every frame and scans it against `data_read`
   (381-393). Each KitItem's movement (464-505): a few float ops. attackSensor per contact.
4. Page against code: matches. Not said: a KitItem is picked up by any player sensor (514), an extra player's too, while
   what it gives goes to player 1.
5. Crash from a mod: `item.give(Gives.Coins(n))`, `StarBits`, `Health`, and `give_item` of such an item, with no stage
   (M-049). `Gives.Lives(n)` with a large n loops n times on the game thread (80-82), a hang, not a fault.
- obj_item/obj_ref (61-62) are pruned only in the stop handler (621-632): the M-040 pattern.
- `kept` (128-130) is pub and has no owner check, unlike `put` (135-137, whose comment explains why it is not pub): any
  mod can read any other mod's kit values (inventory counts, shop stock, progress) by building the key text, since the
  key form is documented (104-114) and hash32 is a builtin. Read-only; not a finding, noted for the owner.

### M-049 crash: kit.item.give (and give_item) of coins, star bits or health with no stage playing ends the game
- Evidence: item.lum:77-84 call MR::incCoin, MR::addStarPiece and MR::incPlayerLife with no test of the scene or the
  player. MR::incCoin dereferences `::getScenePlayingResult()` (SceneUtil.cpp:359-363), made only by GameScene
  (GameScene.cpp:138); MR::addStarPiece the same outside the observatory (EventUtil.cpp:95-101); MR::incPlayerLife reads
  `getPlayerActor()->IsMarioSwimming()` (MarioAccess.cpp:282-286). kit.shop's purchase calls item.give (shop.lum:191).
- Why: a mod that rewards the player from a menu entry, a kit.screen choice, a net message (kit.net) or `on frame` while
  the title, the file select, the star select or an intermission shows.
- Confidence: the missing tests are verified in code; the fault needs a run (`item.give(Gives.Coins(1))` from a pause
  or title menu entry).

### kit.actor (actor.lum, 915 lines)
1. Raw game uses:
   - KitActor (371-576): its own init, movement, calcAndSetBaseMtx, attackSensor, receiveMessage; `getSensor("body")!`
     (434) after the body is always added (417-432). player_can_talk (41-62) and MarioAccess::startTalk/endTalk
     (357, 365) run only from a talkable KitActor's movement, in a stage. Guarded.
   - Public helpers for a mod's own class (590-723): add_collision `a.getSensor("body")!` (619, a script error with no
     body sensor, as documented: "after initHitSensor"); move_toward_player and shock_hurt test the player (`if let`,
     636, 693, 702); sees_player (650-654) and near_player (657-659) call MR::isInSightFanPlayer and MR::isNearPlayer,
     which read the player with no test. They take a live object, so they run in a stage; a call from a scene with
     objects and no player (the star select's own actors are the game's, not a mod's) is not reachable from kit objects.
   - attach (729-749): `make("KitAttached")`, `if let`; the frame loop reads the rider through `get()` and
     MR::getJointMtx with `guard let` (836).
   - drop (713-723): MR::declareCoin / MR::appearCoinPop on the object; others through item.give (M-049 when called
     with no stage; from a KitActor's movement a stage is playing).
2. Shared single values: `making` around one make (268-270). Kinds per owner, aliases "<mod id>/<name>" (171). A data
   kind is read in `on frame` (809-810), so its alias is LumaKit's (lumart.c:3846-3848); on the mod's restart the same
   owner (LumaKit) re-aliases the name to the new kind (the kernel overwrites its own row, LumaClasses.cpp:897, 907,
   912, 936-940), so the reload works, unlike kit.area (M-043). The comment at 882 ("Its alias rows go with the mod")
   is wrong for data kinds: they stay LumaKit's while the mod is off, and a placement then logs "its mod is off".
3. Per frame: `on frame` (809-847): read_data (`mods.needing()` every frame) and a loop over every attach ever made
   (att_* only grow, 115-121; detached rows stay as none), each live one doing MR::getJointMtx by name. Per KitActor:
   movement with stand (313-337), the kind's on_frame, talk_step.
4. Page against code: matches, but the stop text above.
5. Crash from a mod: none found.
- obj_kind/obj_owner/obj_ref (109-111) are pruned only on stop; all() (276-292) walks every object made in the session
  with a `get()` each: the M-040 pattern, here also a cost per call that grows with each stage.

### kit.replay (replay.lum, 1208 lines)
1. Raw game uses: player_pose (201-221) tests MarioAccess::getPlayerActor first, as its comment says the MR::getPlayer*
   calls need; put_player (994-1023) the same; puppet (693-728) `if let`; set_pose/pose read the puppet through
   `get()`; rewind actors through `get()` (1052-1057, 1154-1157). The updateCameraInfo wrap (874-919) works on `self`
   inside the game's call. Guarded. current_pose's unwraps (231-240) are documented script errors.
2. Shared single values:
   - The seed (110-113, 926-945): one value for every mod; the newest `seed` wins and its owner's stop turns seeding off
     (1205-1207), so the earlier mod's seed is not given back. M-050.
   - One input playback for every mod (93-98, 838-861): a newer play_inputs from another mod ends the first,
     documented (835-836).
   - Puppets: at most 15 for all mods (68, 691-692), documented.
   - The encoder's `out`/`src`/`at_byte`/`bad` (286-289): game thread only, safe.
3. Per frame (`on step`, 1075-1164): form's kit_step; one time.now(); the playback; every recorder ever made (rec_* only
   grow, 81-85; stopped ones skipped); every rewind, each kept thing taking one pose (a text name read per step) and,
   once full, `remove(0)` of the oldest (1159-1161), which shifts up to 3,600 poses per kept thing per step. The wraps
   of MR::getRandom (one compare when no seed) and MarioActor::updateCameraInfo (a loop over the recorders).
4. Page against code: matches. Not said: the updateCameraInfo wrap runs for every MarioActor, so with svc.player's extra
   players a played track's camera axes are set on them too and a recorder keeps the axes of whichever player updated
   last (the M-045 pattern; the extra players read the same camera, so the values are likely equal; needs a run).
5. Crash from a mod: none. `decode` (543-602) checks every read against the length (326-370), the count against an
   hour (552) and the count times the record size against the bytes left (437, 557) before allocating: untrusted track
   bytes cannot fault or make a huge list.
- `back` with a rewind that keeps nothing (only What.Inputs given, 966) returns `frames` (1032-1060) though nothing was
  put back; harmless, wrong number.

### M-050 wrong behaviour: kit.replay's seed is one value for every mod; a second mod's seed replaces the first's, and when that mod stops the game's own random numbers come back instead of the first mod's sequence (the M-021 pattern)
- Evidence: replay.lum:110-113 (`seed_set`, `seed_state`, `seed_owner`), 926-933 (seed overwrites all three), 1205-1207
  (the stop handler clears seeding when the owner goes). lumakit.md:63 promises that the earlier request holds again.
- Why: a replay or speedrun mod seeds the game's random numbers for a recorded run; another mod (a randomiser, a
  second replay tool) seeds too, or seeds and is turned off: the first mod's playback meets different random numbers
  and goes out of step with no message.
- Confidence: verified in code.

### kit.ui.kit (ui/kit.lum, 592 lines)
1. Raw game uses: none. svc.draw, svc.input, svc.time only.
2. Shared single values:
   - The touch layout (550-560): one for every mod; the newest caller's shows, and its stop clears it (588-591) instead
     of giving the earlier mod's back (documented as "the newest caller's layout shows"; the M-021 pattern).
   - One text field edited at a time (72-73, 300-311); the edit task checks `editing_id` before calling on_done, so a
     closed panel's or a stopped mod's on_done is not called. Sound.
   - World hold counted (68, 149-161) and released on close and stop. Sound.
   - `queued` (75): callbacks of every mod, run at the next frame's start (315-319). Not filtered on stop. M-051.
3. Per frame: `on frame` (314-435): the queued callbacks, one input.game, and boxes_of/stops_of for panels from the
   newest down (allocations per panel per frame). paint (438-467) on the HUD draw point: boxes_of twice per panel.
4. Page against code: matches (the ModelView and touch-layout limits are said at 16).
5. Crash from a mod: none found by a bad argument. A Wheel with no entries: left or right while it is focused computes
   `(at + step + 0) % 0` (420), a division-by-zero script error raised in LumaKit's own frame handler (caller is
   LumaKit), each frame the press repeats.
- set/close/alive (115-132) check no owner; ids are never reused (89), so only a mod that builds `Open { id }` itself
  can close or change another mod's panel.

### M-051 crash: kit.ui.kit runs a button's or slider's callback one frame after the press; if its mod is taken out in between, LumaKit calls into the freed mod
- Evidence:
  - ui/kit.lum:369-430 push the widget's fn value (or a closure calling it) onto `queued`; 314-319 run them at the next
    `on frame`. The stop handler (580-592) removes the gone mod's panels but leaves `queued` as it is.
  - A kit stop handler runs from the library's dependent_gone, synchronously after the mod's stop and before it is
    freed (native/petari/src/Game/System/LumaLoader.cpp:1307-1318, called at 1463 and 2050 before endMod). endMod frees a
    mod with no hooks at once: finishMod frees its blocks (freeAll, 1379) and closes its library (1380-1382). A kit-based
    mod usually has no hooks of its own.
  - A fn value is a block whose `code` is the mod's C function and whose `drop` is the maker's code
    (crates/lumascript/rt/lumart.h:173-196); a closure that captures lives in the maker's freed memory.
  - The call faults in LumaKit's image, which M-001 shows is never resumed: the game ends.
- Why: the player presses A on a mod's panel button and the mod is taken out before LumaKit's next frame (a take-out
  request handled at the end of that frame, a dev reload, the mod stopping itself from another callback that frame).
  Every other kit module that keeps a mod's fn values removes them in its stop handler (menu keeps ids, 412-430).
- Confidence: the missing filter and the free order are verified in code; whether a take-out can land between the two
  frames needs a run (a panel button whose press is followed by a take-out request in the same frame).

### kit.net.kit (net/kit.lum, 1295 lines)
1. Raw game uses: none. svc.net.local (optional; every use tests `local != none`, 156, 169, 175, 475, 1011) and
   svc.time.
2. Shared single values: links, pairs, pages and connections are lists with owners; the stop handler closes the gone
   mod's links and pages (1221-1232) and end_link/end_page clear their callbacks (653-654, 1101). Shared across all
   mods' links: `outs`, `helds` (106-107; helds capped at 1,024 in all, 344), the clock maps (110-111) and `frame`.
   One peer filling `helds` makes every other pair's out-of-order DATA wait for its resend (12 frames): slower, still
   correct.
3. Per frame (`on frame`, 1258-1295): with no pairs, one compare; else a pass over every unacked DATA (up to 256 per
   pair) and every pair. Each send scans every unacked DATA (in_flight, 212-220).
4. Page against code: matches the header's wire form. Not said: no peer is authenticated; see M-052.
5. Crash from a mod or a peer: none. Datagrams: length checked before the header read (369); payload reads checked
   (303, 428, 434). WebSocket frames: the 16/64-bit length is checked against 1 MiB before use (899-905), the whole frame
   before slicing (903), fragments against 1 MiB (923-927); HTTP heads capped at 16 KiB (994-997); files only from the
   mod's list (859-865, no path from disk).
- A page connection's bytes are joined by copying (985, join at 147-153 pushes byte by byte), so a 1 MiB WebSocket
  message that arrives in many small pieces copies the growing buffer each time on the game thread. Suspected frame
  stall, needs a run (a page sending one 1 MiB message in 1 KiB writes).
- answer() then drop_conn() (861-862, 866-867) closes the socket right after the send; whether net.local's close
  flushes a large file first is a service question (Area B, luma_net.cpp).

### M-052 wrong behaviour: kit.net.kit trusts what any peer says: a player can tell the host that any id joined or left, any machine on the network can give a waiting player its id, any player can grow the host's clock maps without limit, and the page server takes WebSocket connections from any web page
- Evidence:
  - deliver (302-317) acts on kind K_JOINED/K_LEFT with the payload's `origin` for any DATA, including a player's DATA to
    a host: `tell_peer(link, origin, true/false)`. Only K_USER's sender is fixed to the pair's id (319). So a player can
    make the host mod's on_peer say that player 3 left, or that player 200 joined.
  - T_WELCOME (401-410) is handled before the "a player hears only its host" test (411-420): the first WELCOME from any
    address and port sets the waiting player's id (`own_id[link] = to`), including 0 (the host's id).
  - T_CLOCK_A (434-442) has no `not hosting[link]` test: on a host, each CLOCK_A from any player adds two map entries
    under a token the player picks; only the asking task removes its own four tokens (690-692). A player sending
    CLOCK_A datagrams grows the host's memory for as long as it is connected.
  - The WebSocket upgrade (843-850) checks no Origin header and the page needs no token: any web page opened in a
    browser on the network (on the phone, or on the PC) can connect to the page's port and send messages to the mod's
    on_page_message.
  - HELLO (378-399) gives a pair to any sender until 32 are held, each held 5 s after it goes quiet (1281-1283).
- Why: a modified game or a small script on the local network in a co-op session; or a web page that knows the port a
  mod uses (a mod that serves a fixed port, e.g. a phone-controller mod on 8080).
- Confidence: verified in code (each check absent). The browser case needs a run (a page on another origin opening
  `new WebSocket("ws://<ip>:<port>/")`).

### The kit data schemas (lumakit/code/lumakit.kit/*/1.json, 11 files, 1,707 lines in all)
Read in full: actor (201), area (144), cutscene (271), item (184), player.character (91), player.form (82),
player.state (161), ride (57), shop (200), stage (242), talk (74). Each compared field by field with the module's data
struct (actor.lum:92-96, area.lum:45-47, cutscene.lum:42-53, item.lum:48-50, character.lum:274-283, form.lum:68-70,
state.lum:60-71, ride.lum:46-47, shop.lum:31-32, stage.lum:74-96, talk.lum:149-164) and enum (form.lum:33,
stage.lum:16-18, shop.lum:26). Names, defaults and enum cases match. Differences:
- The schemas are stricter than the code in places (character `look` required, the code defaults ""; cutscene shots
  oneOf one key, the code's ShotData takes several): harmless.
- actor `base` (actor/1.json, "type": "string", any value up to 64): the code accepts only "LiveActor" and `fail`s
  otherwise (actor.lum:144-146), inside LumaKit's `on frame` read (795-806). M-053.
- Every count is u32 up to 4294967295 (actor drops, item gives, shop gives; stage counts and scenarios): the code turns
  coins and star bits into i32 (item.lum:77-78, actor.lum:718-719) and loops once per life (item.lum:80-82). M-053.

### M-053 wrong behaviour: the kit data schemas accept values the kit cannot take; a data-only mod that passes the validator can stop LumaKit's data read or hang the game
- Evidence:
  - actor/1.json `base`: any string; actor.lum:144-146 `fail`s for anything but "LiveActor". The data read runs in
    LumaKit's own `on frame` (actor.lum:795-810) after `data_read.push(m)` (800), so the failure ends that frame's read
    there: the mod's later kit/actor files are never read, and the error is LumaKit's, not the mod's.
  - item/1.json, actor/1.json and shop/1.json give counts up to 4294967295. `Gives.Lives(n)` calls MR::incPlayerLeft n
    times on the game thread (item.lum:79-83); `Gives.Health(n)` passes n to MarioAccess::incLife, which loops n times
    while swimming with an `int` counter against a `u32` (MarioAccess.cpp:282-286). `Coins(n)`/`StarBits(n)` above
    2147483647 fail the i32 conversion (item.lum:77-78, actor.lum:718-719).
- Why: an item file `{"look": "Coin", "gives": {"Lives": {"n": 4294967295}}}` validates and, when picked up, holds the
  game thread for billions of calls. A typo'd base ("NPC") makes every later actor file of that mod disappear with an
  error that names LumaKit.
- Confidence: verified in code and schema; the hang's length needs a run.

### M-054 wrong behaviour: kit.cutscene and kit.stage share svc.audio's one music without knowing of each other; a cutscene with music in a galaxy that has a mod's music gives the game's music back when it ends
- Evidence: kit.stage plays a galaxy's mod music with `audio.music(s, 500)` and sets `music_playing` (stage.lum:391-399).
  kit.cutscene starts its own with `audio.music(...)` (cutscene.lum:166) and at its end calls `audio.music_restore(500)`
  (cutscene.lum:97-100), which gives the game's music back, while kit.stage still has `music_playing = true` and does
  nothing until the next MR::startStageBGMFromStageName. Both calls name LumaKit (a library's service calls name the
  library, item.lum:125-127), so svc.audio holds one music for both. kit.player.form uses the game's sub BGM instead
  (form.lum:307, 247), a third path.
- Why: a galaxy pack with its own stage music and an intro cutscene with music: after the intro the galaxy plays the
  game's track, not the pack's.
- Confidence: verified in code; the audible result needs a run.

### M-038 extended: other kit calls that read a scene object with no test
- Scenes that lack the objects: CameraContext is made only by GameScene and BootScene (GameScene.cpp:130,
  BootScene.cpp:34); ScenePlayingResult only by GameScene (GameScene.cpp:138). ScenarioSelectScene (the star select)
  and IntermissionScene make neither (include/Game/Scene/*.hpp; no createSceneObj of them in their .cpp). The kernel's
  own services test `MR::isExistSceneObj(SceneObj_CameraContext)` first (LumaDrawView.cpp:597, LumaGameSound.cpp:47);
  the kit does not.
- Calls that fault there: `shop.open` (coins or star bits, shop.lum:44-56), `pointer.world()` (pointer.lum:32, 40),
  `camera.now()` (camera.lum:368), and `cutscene.play` (below).
- kit.cutscene.play with no player or outside a GameScene: play calls MR::offPlayerControl (cutscene.lum:144-146),
  which is `MarioAccess::offControl()` = `getPlayerActor()->mRemoteDemo = true` (PlayerUtil.cpp:805-807,
  MarioAccess.cpp:470-472) with no null test, then `camera.now()` (147), then MR::tryScreenToFrameCinemaFrame
  (152-155), `getSceneObj<CinemaFrame>` with no test (ScreenUtil.cpp:78-80, 437-439); only GameScene makes the
  CinemaFrame (GameScene.cpp:142). put_back (82-102) calls MR::tryFrameToScreenCinemaFrame and MR::onPlayerControl
  (same getPlayerActor()) when the scene ends, so a scene still playing when the stage gives way to the star select or
  an intermission faults at its end. That last case is suspected and needs a run (a cutscene with a long Wait, the
  player leaving the stage through a pipe during it).
- Why: a mod's `on frame` (which runs in every scene) or a menu entry calling any of them while the star select shows.
- Confidence: the missing objects and the missing tests are verified in code; the fault needs a run (a mod calling
  `camera.now()` in `on frame`, then opening a galaxy's star select).

### Area A table: every LumaKit module
Tests: the mods in D:\NebulaWork\build\audit-conf\luma\{f,fb,g,h,j,k} that `use kit.<module>` (grep over every .lum).
No suite tests a module's behaviour with two mods, with an extra player, or outside a stage.

| Module | Lines | Tests that use it | Verdict |
|---|---|---|---|
| actor | 915 | k k9-kit (one) | Sound; data-kind alias reload works; session-long lists (M-040 pattern); schema base (M-053) |
| area | 347 | fb kit-area | Bare alias names, data kinds LumaKit's (M-043); acts per MarioActor (M-045) |
| camera | 492 | fb kit-camera, kit-camera-b, one more | fov/mirror/shake one value (M-021); camera.now with no CameraContext (M-038) |
| cutscene | 363 | none | Unguarded player and CinemaFrame calls (M-038 ext.); music clash with stage (M-054) |
| gravity | 223 | fb kit-gravity, kit-gravity-b | Bare alias names (M-043); timed overrides count per MarioActor (M-037, M-045) |
| hud | 394 | k kit-hud | Handles with no owner check (M-042) |
| item | 638 | k k9-kit (one) | give with no stage ends the game (M-049); counts the code cannot take (M-053) |
| menu | 477 | j kit-menu, k kit-menu-key, one more | Back/Quit placed only on appear (M-044) |
| net/kit | 1295 | h kit-net.kit | Untrusted parsing safe; peers not authenticated, no Origin check (M-052) |
| player/character | 510 | none | Sound; tests player 1 where it must |
| player/form | 595 | none | Hooks act for every MarioActor (M-045); game sub BGM (M-054 note) |
| player/state | 656 | k k9-kit (one) | Sound; tuning bases overwrite later changes (noted) |
| pointer | 244 | fb kit-pointer | pointer.world with no CameraContext (M-038 ext.) |
| progress | 320 | k k9-kit (one) | Sound; header names a hook that does not exist (noted) |
| projectile | 336 | fb kit-projectile, one more | Slot lists only grow (M-040) |
| replay | 1208 | none | Seed one value (M-050); untrusted track bytes safe |
| ride | 428 | fb kit-ride | Bare alias names (M-043); stopped mod's on_dismount (M-046) |
| screen | 388 | j kit-screen | Tips one value (M-033); answer race suspected |
| shop | 235 | none | open with no stage ends the game (M-038); header wrong (M-039) |
| stage | 646 | j kit-stage, f k1 stage | Galaxies one per name (M-047); build fields never pull (M-048); music clash (M-054) |
| talk | 285 | j kit-talk | One line for every mod (M-041) |
| ui/kit | 592 | j kit-build / kit-ui.kit | Queued callbacks survive the mod (M-051) |
| data schemas | 1,707 (11 files) | none directly | Accept values the code cannot take (M-053) |

Checked in area A (round 3) and found correct:
- Every module's handle functions check the caller as owner, except hud (M-042) and ui/kit (ids never reused).
- Every module's stop handler drops the gone mod's fn values before the mod is freed: the handler runs from
  dependent_gone, synchronously after the mod's stop and before endMod frees it (LumaLoader.cpp:1307-1318, 1463, 2050;
  finishMod 1372-1408). The one exception is ui/kit's queue (M-051).
- kit.replay.decode and kit.net.kit's datagram, HTTP and WebSocket parsing bound every read.
- kit.item and kit.actor re-take a restarted mod's data names (item.lum:225-236; actor via the kernel's own-row
  overwrite, LumaClasses.cpp:897-940).
- kit.player.character and kit.player.state test player 1 in their MarioActor hooks.

## Area B (round 3): service bodies

Not every listed file was read line by line this round (see the coverage table). What was read in full or by whole
section is named under each heading.

### M-029, exhaustive, with file and line
Every service entry a mod reaches whose pointer argument (other than `mod`) is read or written by provider code. A
non-null bad pointer (freed, short, wrong type) faults at a pc outside the mod, which luma_guard.cpp:101-103 never
resumes: the game ends. `fn^` marks a callback stored and called later (a bad one faults in the kernel's call, the same
way). Each was found by its definition (`(const LumaMod* mod, ...)` / `(const LumaMod* pMod, ...)`) and its parameter
list; each checks null and, where it has one, the struct's `size`, nothing more.

runtime/src/luma_net.cpp (net.local; net.internet's twins at 4687-4913 take the same pointers):
- 3171 net_tcp_connect(host, fn^, user, out); 3175 net_udp_connect(host, fn^, out); 3179 net_tls_connect(host, fn^, out)
- 3186 net_udp_open(fn^, out); 3410 net_tcp_listen(fn^, out); 3414 net_tls_listen(certificate, key, fn^, out)
- 4141 net_port(out); 4144 net_send(data x bytes read); 4147 net_take(buf x size written); 4154 net_send_to(host, data)
- 4245 net_take_datagram(buf, from); 4099 net_ws_connect(url, protocol, fn^, out); 4103 net_ws_send(data)
- 4343 net_ipc_connect(name, fn^, out); 4398 net_ipc_listen(name, fn^, out); 4851 net_http(request and every string
  and body pointer in it, fn^, out); 5062 net_address(buf x size)
- 4687 internet_tcp_connect, 4691 internet_udp_connect, 4695 internet_tls_connect, 4855 internet_http, 4859
  internet_ws_connect, 4863 internet_ws_send, 4881 internet_take_datagram, 4904 internet_port, 4907 internet_send, 4910
  internet_take: as their net.local twins. 4701, 4867, 4874, 4885, 4891, 4897 answer "not on net.internet" without
  reading their pointers.
runtime/src/luma_input.cpp:
- 771 input_action(id, out); 794 input_user_action(id, out); 832 input_read(out); 887 input_game(out); 891
  input_game_raw(out); 895 input_device(out); 910 input_watch_device(fn^); 927 input_text_request(request,
  request->initial, fn^, out); 995 input_text_peek(buffer x size, length, caret); 1164 input_look(out); 1195
  inject_begin(out); 1233 inject_set(state); 1281 dev_list(out x capacity, found); 1329 dev_open(path, fn^, out); 1407
  dev_write(data x size)
runtime/src/luma_audio.cpp:
- 1031 audio_play(name, how, fn^, out); 1036 audio_play_at(name, how, at, fn^, out); 1041 audio_voice_place(at); 1075
  audio_game_play(name, at, out); 1124 audio_game_place(at); 1230 audio_stream_open(desc, fn^, out); 1305
  audio_stream_push(samples: frames x channels floats); 1349 audio_stream_channels(out); 1410 audio_music(name); 1537
  audio_tap_open(fn^, out)
runtime/src/luma_fs.cpp:
- 1134 fs_read, 1137 fs_read_package, 1147 fs_remove, 1153 fs_list, 1157 fs_list_package (path, fn^, out); 1143
  fs_write(path, data x bytes, fn^, out); 1150 fs_move(from, to, fn^, out); 1140 fs_take_read(buf x size); 1161
  fs_take_list(buf x size)
- 1170 ex_read, 1176 ex_write, 1180 ex_remove, 1183 ex_move, 1186 ex_list, 1173 ex_take_read, 1190 ex_take_list: as
  their fs twins.
runtime/src/luma_capture.cpp:
- 940 microphone_open(desc, fn^, out); 985 camera_open(desc, fn^, out)
runtime/src/luma_mods.cpp:
- 201 mods_at(out); 220 mods_find(id, out); 544 ids_get(space, name, out); 605 ids_of(space, mod_id, name, out); 702
  install_request(path, fn^, out); 960 mods_option(key, buf x size); 1014 mods_watch_options(fn^); 1089
  mods_watch_mods(fn^); 1244 kit_names(module, buf x size, length); 1299 kit_read(module, name, buf x size, length);
  1366 kit_needing(out x capacity, count); 1446 mods_of_mod(out)
runtime/src/luma_assets.cpp:
- 608 assets_find(text, out); 630 assets_bytes(text, data, bytes); 671 assets_entry(text, name x name_size, needed,
  out); 975 assets_provide(text, files x count and each file's pointers); 1022 assets_withdraw(text); 1043
  assets_cook(text, data x bytes, fn^, request)
runtime/src/luma_system.cpp:
- 547 system_facts(out); 593 system_display(out); 657 clipboard_get_text(fn^, out); 697 clipboard_take_text(buf x size);
  722 clipboard_get_image(fn^, out); 779 clipboard_take_image(rgba x size); 801 clipboard_set_text(text x bytes, fn^,
  out); 849 clipboard_set_image(rgba x size, fn^, out); 945 links_open(url, fn^, out)
native/petari/src/Game/System/LumaSave.cpp:
- 92 svcGet(key, buf x size); 113 svcSet(key, value x bytes); 147 svcRemove(key); 163 svcFile(number, character); 471
  svcGlobalGet; 492 svcGlobalSet; 525 svcGlobalRemove (as the game's); 909 svcExport(fn^, out); 961 svcTake(buf x size);
  1274 svcOffer(path, fn^, out)
LumaAnim.cpp: 684 svcPlay(actor raw, read through its vtable at 679-681; clip, how, how->marks, fn^, out); 698
  svcPlayOnModel(clip, how, fn^, out); 742 svcFrame(frame, last)
LumaMovie.cpp: 298 svcPlay(name, fn^, out); 386 svcPosition(frame, frames)
LumaContentFx.cpp: 750 svcStart(actor raw via classOf 627-633; joint, place, out); 756 svcStartOnModel(socket, place,
  out)
LumaContentModel.cpp: 528 svcAttach(actor raw via classOf 534-541; joint, place, out); 671 svcPlayerParts(parts x
  count, total); 795 svcSetParam(material, param, rgba)
LumaDraw.cpp: 579 svcAt(fn^); 623 svcScreen(out); 648 svcRect(rect); 666 svcRectOutline(rect); 790 svcImage(dst, src);
  852 svcText(text, advance); 906 svcTextMeasure(text, out); 934 svcClipPush(rect); 1030 svcFont(name, out); 1133
  svcTexture(name, out); 1173 svcTextureFromPixels(rgba8 w x h x 4, out); 1221 svcTextureUpdate(rgba8 by stride); 1255
  svcTextureSize(w, h); 1325 svcLine3(a, b); 1545 svcMesh(model, matrix[12]); 1584 svcBillboard(pos)
LumaDrawView.cpp: 321 svcCreate(out); 375 svcCamera(eye, at, up); 401 svcFollow(actor raw via classOf 408-409, offset);
  495 svcRig(camera, camera->actor raw 458-459); 527 svcEyeOf(camera, out); 565 svcTexture(out); 690 svcMainEye(eye);
  724 svcMainRig(camera, look)
LumaDrawInst.cpp: 407 svcOf(actor raw via classOf 412-414, out); 482 svcSetMaterialParam(material, name, value); 547
  svcSetTexture(material); 559 svcSetVisible(part); 595 svcSetScale(scale)
LumaDrawLight.cpp: 199 svcAdd(desc, out); 247 svcMove(position); 267 svcSet(desc)
LumaDrawPost.cpp: 212 svcProgram(name, out); 326 svcParams(values x count)
LumaDrawNative.cpp: 116 svcAt(fn^, called on the render thread with no guard, M-013); 159 svcBackend(out)
LumaUi.cpp: 603 svcOpen(layout, out); 700 svcPane(name, out); 764 svcSetText(text); 893 svcPlay(animation, fn^); 961
  svcAlive(out); 1035 svcMenuRows(rows x count); 1076 svcMenuChosen(row); 1095 svcShareRows(rows x count); 1131
  svcSharedFocus(focused, chosen); 1157 svcPlayPane(animation, fn^)
LumaTime.cpp: 544 svcNow(out); 580 svcWall(out); 701 svcTimer(fn^, out); 785 svcOnFrame(fn^); 801 svcOnStep(fn^)
LumaPlayer.cpp: 125 svcMake(init, out)
- Not on this list (no pointer but `mod`, or the pointer only compared): every close/cancel/stop, audio_voice_set/stop,
  audio_bus_volume, audio_music_restore, input_rumble, input_pointer_source, input_look_mode, LumaPlayer remove and
  set_drawn (compare only, LumaPlayer.cpp:107-114, 221-233), mods_count.
- LumaGameSound.cpp and LumaFont.cpp have no entry a mod calls (luma_audio and LumaDraw call them).
- Confidence: verified in code (each signature at its line); a run would show one (a mod passing a freed buffer to
  net_take).

### Thread and lifetime (round 3)
- Every host service calls mod code only from a kernel-posted delivery on the game thread: luma_audio.cpp:568-573,
  luma_capture.cpp:868, luma_input.cpp:312, 385, 535, luma_fs.cpp:661, luma_system.cpp:523, luma_mods.cpp:690, 952,
  1081, luma_assets.cpp:943, luma_net.cpp:2797, 3701 (grep of every `->call(` in runtime/src). The one provider that
  calls mod code on another thread is draw.native (M-013). So M-002's risk is limited to draw.native.
- Threads that hold provider objects: the net lookup thread holds its Socket until `resolving` is false, and reap
  waits for it (luma_net.cpp:501, 4594-4600); capture threads hold a shared_ptr to their Item (luma_capture.cpp:912);
  input.dev's reader is joined before its device is deleted (luma_input.cpp:467-468, 484-491). Sound.
- WebSocket deliveries keep their Socket while `g_delivering` names it, so a close inside the mod's callback does not
  free it mid-parse (luma_net.cpp:3726, 3749-3752, 3930-3931). Sound.

### luma_net.cpp: what a remote peer or server can send (round 3 reading: 246-705, 1107-1556, 2006-2244, 3527-3946, 4476-4690; round 2: 1596-1934, 3238-3525)
Found correct:
- UDP (1107-1185): each read is at most kDatagramRoom into one shared buffer, the count and bytes waiting are capped
  (kDatagramsWaitingMost 4096, LUMA_NET_RECEIVED_MOST), and udp_open's sockets drop senders outside this machine and the
  local network (1133-1135).
- Stream reads stop at the caller's bound (1195-1221); TLS input is capped at kWireInMost before BoringSSL (1366,
  1475).
- TLS client (1257-1352, 1356-1450): TLS 1.2 to 1.3, SSL_VERIFY_PEER against Mozilla's roots plus the mod's own,
  host name (SNI and X509_VERIFY_PARAM_set1_host) or IP checked; a verify failure ends the handshake with
  Net.CertificateRefused (1384-1390, 1517-1522); a truncated stream (no close_notify) is CLOSED with LUMA_E_IO (1550).
- net.internet: a name is looked up on its own thread and only public addresses are kept (4480-4498, 4572-4588); the
  socket connects to those addresses, so a name that resolves to a LAN address is refused and DNS rebinding cannot
  reach the local network. Redirects are re-checked against luma.json's hosts (net.internet) or the local rule
  (net.local), never https to http, at most LUMA_NET_REDIRECTS_MOST (2130-2167). URLs refuse user info, control bytes
  and bad ports (2006-2101).
- WebSocket client (3630-3683, 3706-3933): the upgrade must be 101 with the right Accept and no unasked extension or
  protocol; frames refuse RSV bits, masked server frames, bad opcodes, control frames over 125 bytes or fragmented,
  continuation errors, and lengths over LUMA_NET_MESSAGE_MOST before any allocation (3815-3829); text is checked as
  UTF-8 without overlongs or surrogates (3588-3626).
- A closed socket's queued bytes are still sent for up to kCloseTime (249-251, 1179-1184), so kit.net.kit's answer then
  close (net/kit.lum:861-867) sends the file: the round-3 question under kit.net.kit is answered.

### M-055 wrong behaviour: a WebSocket server can make the game queue pongs without limit; pongs bypass the send budget every other send is held to
- Evidence: luma_net.cpp:3853-3858 appends a pong to `socket->outgoing` for every ping, with no room_locked check;
  ws_send_on checks the mod's budget (luma_net.cpp:4079-4086, room_locked with bytes + 14) and send_on likewise
  (3454-3500). The I/O thread keeps reading while `received` has room, independent of whether `outgoing` drains.
- Why: a server (one the mod's luma.json names, or any local-network peer for net.local) that sends pings and never
  reads: TCP's window closes, `outgoing` grows by about the ping rate times six bytes plus each ping's payload, without
  end, until the game is out of memory.
- Confidence: the missing check is verified in code; the growth needs a run (a test server that floods pings with its
  receive window closed).

### Per-platform gaps (round 3 additions)
- system (luma_system.cpp; luma_system_win32.cpp, platform/sdl/luma_system_sdl.cpp, platform/apple/luma_system_apple.mm
  by grep): each window layer sets a platform (render/host_window_win32.cpp:600, host_window_sdl.cpp:1750,
  host_window_apple.mm:1257). Clipboard pictures: Linux only on x11/wayland drivers (luma_system_sdl.cpp:85-91);
  Android through ClipboardBridge, claimed available before the bridge is found (83 against 227-263), so a missing
  bridge answers LUMA_E_IO instead of LUMA_E_ABSENT. Not a crash.
- capture: Windows and Android only (M-012, luma_capture.cpp:11, 801-803).
- input.dev: Windows only (luma_input.cpp:1316, 1403; documented).
- input text entry: answered LUMA_E_ABSENT where the host sets no text entry (luma_input.cpp:960-962, set at 1679).
- mods.install: luma_mods.cpp:1-4 says the request answers LUMA_E_ABSENT "no fs yet"; install_request (702-790) asks
  the import screen and answers for real. A stale file comment, not a contract.

# Round 4

Scope: item 1 (luma_input with the 11-16 look code) and item 2's draw.view (with the 11-16 main_rig) and the
set_drawn path. All of it is uncommitted and has never been played; it is read as new code. Paths below are relative to
D:\NebulaWork\MarioGalaxyRecomp\Nebula-Clean.

## Item 1: input (luma_input.cpp, luma_input.h) and the look's host side

### M-056 wrong behaviour: the game's mirrors reflect the game's own camera, not the mod's main camera
- Evidence: SceneExecutor.cpp:31 puts the game's own view back (mainCameraBeforeGame), 32 runs the camera director, 33
  runs MovementType_MirrorCamera, and only 34 sets the mod's view (mainCameraAfterGame). MirrorCamera::movement
  (MirrorCamera.cpp:81-86) builds the mirror's view from MR::getCameraInvViewMtx() at that moment (37-51) and the
  projection from the game's (54-62). GameScene::drawMirror (GameScene.cpp:493-505) draws that view. Nothing recomputes
  it after line 34 or at the second apply (GameScene.cpp:346). luma_draw_view.h:13-16 and 129-133 say everything that
  reads the camera reads the mod's.
- Why: main_eye or main_rig held while the player stands in a MirrorArea.
- Confidence: verified in code (order and source of the mirror's view); how far off the reflection looks needs a run in
  a mirror area with a first-person rig.

### M-057 wrong behaviour: on macOS the look turns by the system's accelerated pointer motion, not the mouse's counts
- Evidence: host_window_apple.mm:225-231 adds NSEvent deltaX/deltaY (points) to mouse_dx/dy while held. Windows adds raw
  counts from WM_INPUT (raw_mouse_win32.cpp:52-64). pointer_control.h:133-134 and 138 define the turn as 0.0011 rad per
  raw count "not the system's pointer curve". The same mod scale turns a different amount on a Mac, and the turn is
  speed-dependent there.
- Why: input.look_mode on, on macOS with a mouse.
- Confidence: verified in code that deltaX is used; that deltaX carries the system curve needs a Mac run (turn the same
  physical distance slowly and quickly, compare the yaw).

### M-058 wrong behaviour: on macOS the held cursor is frozen where it was, not moved into the picture
- Evidence: update_cursor (host_window_apple.mm:242-260) calls CGAssociateMouseAndMouseCursorPosition(false) (256) when
  the window is key and front; the hold does not need the cursor inside (`hold = front && wanted`). No
  CGWarpMouseCursorPosition exists in the file. Clicks are still delivered where the frozen cursor is; the button handler
  treats a press outside the picture as the system's (comment at the mouse-button case, about 1060).
- Why: look turned on while the cursor is over the title bar, the menu bar or another display (keyboard play, then the
  mod turns the look on).
- Confidence: verified in code (no warp, hold not gated on inside); what a click there does needs a Mac run.

### M-059 wrong behaviour: one look for all mods: a second mod's on silently replaces the first, its off or end ends both, and its scale reaches every reader
- Evidence: input_look_mode (luma_input.cpp:1137-1162) stores one g_look_mod, g_look_scale (1158-1159). Off from the holder
  or its end (1146-1148, mod_gone 1469-1471) clears g_look_on for everyone; the first mod's on is not restored.
  game_read multiplies the turn by the holder's scale (1583-1586) and input_look gives that to any mod (1164-1182).
  luma_input.h:223 says only "the newest mod's on holds; it ends with off or the mod".
- Why: two mods using look_mode (a first-person mod and a library or a second camera mod), or a mod reading look() while
  another holds it.
- Confidence: verified in code.

### M-060 wrong behaviour: with more than one controller read in a frame, look() keeps only the last read's turn
- Evidence: GameSystem::frameLoop reads the controllers again for each extra world step (GameSystem.cpp:193-197, a rate
  above real time from the time service) and each cutscene-skip step (198-201). Each read runs game_read, which
  overwrites g_look_frame and drains the gathered turn (luma_input.cpp:1581-1590). A mod reading look() once a frame
  loses every read's turn but the last. Rumble has the same base: its time is counted in reads at 60 a second
  (luma_input.cpp:64, 1063), so it ends early at a faster world rate.
- Why: any mod setting a world rate above real time while a first-person mod reads look per frame.
- Confidence: verified in code.

### M-061 wrong behaviour (Windows): the mouse clip is given back only by the next controller read
- Evidence: hold_mouse_for_look (host_input.cpp:1027-1051) is called only from build_live_input (1184), on the game
  thread's controller read (host_main.cpp:3745) and the real Wii Remote worker (3910). The window code never calls
  ClipCursor (grep: host_input.cpp:1039 and 1047 are the only calls). When the game stops reading (minimised: the game is
  held, host_main.cpp:925; a hang; the end of the process while held), release depends on Windows dropping the clip
  itself, which the comment at host_input.cpp:1024-1026 assumes.
- Why: look held, then Win+D or minimise, or Quit from the pause menu with the look on.
- Confidence: suspected, needs a run on Windows: hold the look, minimise, and move the mouse on the desktop; then quit
  with the look held.

### M-062 wrong behaviour (Linux): where SDL cannot hold the mouse the look gets no mouse motion and asks again every pump
- Evidence: host_window_sdl.cpp:1165-1179 sets g_mouse_look_held only when SDL_SetWindowRelativeMouseMode succeeds
  (1174) and calls it again on every pump while it fails. mouse_dx only grows while held (the SDL_EVENT_MOUSE_MOTION case
  in pump_events), and the look's mouse input is only mouse_dx (native_game_host_sdl.cpp:481). Nothing is logged and the
  mod is not told (look() still answers on 1 with the stick's turn only).
- Why: a compositor or X server without relative pointer support, or a remote session.
- Confidence: verified in code (the retry and the empty motion); which Linux setups refuse needs runs.

### M-063 wrong behaviour (Android): a mouse click on the picture drops the mouse hold
- Evidence: TouchControlsView.onTouchEvent (TouchControlsView.java:578-600) takes every touch, including a mouse button
  press, and sends it to nativePointer (GameActivity.java:280-285, host_window_sdl.cpp:1625-1632). finger_event sets
  mouse_pointer false on a new finger (host_window_sdl.cpp:1016-1017). The hold is asked only while mouse_pointer is true
  (native_game_host_sdl.cpp:574). A click ends the hold until the mouse moves again, and the click becomes a finger
  (a tap or the finger look).
- Why: Android with a USB or Bluetooth mouse and look_mode on.
- Confidence: suspected, needs a run on the tablet with a mouse (whether SDL's pointer capture routes the click to SDL's
  view instead decides it).

### M-067 docs wrong: luma_input.h's first comment names input 1.3
- Evidence: luma_input.h:4 "`input` 1.3 ... LUMA_SERVICE(slot, "input", "1.3")"; LUMA_INPUT_MINOR is 4 (34) and 1.4 is
  described at 14-15 and 216-229. A mod importing "1.3" as the comment says gets no look_mode or look.
- Why: a modder copying the import line.
- Confidence: verified in code.

### M-068 wrong behaviour: a fast turn of the look counts as a camera cut for the player and for the frame interpolation
- Evidence: MarioActor::updateCameraInfo (MarioActor.cpp:2819-2841) treats a 30 degree change of the camera's z direction
  in one frame as a cut: Mario's animation blend restarts (changeAnimationInterpoleFrame(0)), the spin effect stops.
  frame_interpolation.h CameraCutLimits.max_turn_degrees is 60: the presented pictures snap instead of turning between the
  two game frames. At 0.0011 rad a count (pointer_control.h:138), 30 degrees in one 60 Hz step is 476 counts, about
  0.75 cm at 1600 counts an inch, before the mod's scale.
- Why: a quick mouse flick with the look on.
- Confidence: verified in code (both limits); how visible it is needs a run at 240 presented pictures a second with
  quick flicks (the body is hidden in first person, the reflection and the shadow are not).

### M-069 maintainability: the look's host work runs on every controller read with no mod
- Evidence: live_controls_engine.cpp:365-366 calls look_choice() and host.look() on every sample; on Windows look()
  drains the raw mouse accumulator under its mutex (host_input.cpp:995-1002, raw_mouse.h:22-27); on SDL hosts the finger
  look is projected on every read (native_game_host_sdl.cpp:478-498). The rule is no mod, no cost.
- Why: every game frame.
- Confidence: verified in code (the cost is one lock and a few loads per frame).

### Checked in item 1 and found correct
- Argument checks of look_mode and look match the header; a NaN scale is refused (luma_input.cpp:1137-1182).
- The look ends with its mod (1469-1471); each host lets go of the mouse at its next sample (Windows host_input.cpp:1046-
  1049; SDL host_window_sdl.cpp:1165-1179; macOS host_window_apple.mm:252-258).
- The look holds only in play, in front, with no host menu and no text request (live_controls_engine.cpp:367-368); the
  game keeps reading the controllers under its pause menu and under the settings and HOME menus (GameSystem.cpp:258-261,
  311-325), so a menu gives the mouse back within one read.
- Mouse counts are gathered per event and taken whole at each read on every platform (raw_mouse.h:16-27, SDL and Apple
  take_input_state zero mouse_dx after the copy): none lost, none counted twice.
- The real Wii Remote worker's samples (host_main.cpp:3909-3910) and the game's (3745) are both under
  gInputSampleLock, so hold_mouse_for_look's static and the engine's statics are not raced; g_look_* is under
  g_look_mutex or atomic; g_look_frame and g_look_mod are game-thread only (PlayerInputReader::read on the game thread,
  PlayerInputs.cpp:89-93).
- The pointer is pinned to the picture's middle while the look holds (live_controls_engine.cpp:510-514); the Windows
  cursor is hidden over the client while in front (host_window_win32.cpp:914-921) and the clip is set again whenever
  Windows dropped it (host_input.cpp:1037-1040).
- The pre-change parts of luma_input.cpp (text entry, inject, dev, actions) are unchanged from round 2's reading; the
  text request, injection and device lifetimes free each node once (deliver or drop) as round 2 found.

## Item 2: draw.view main camera (LumaDrawView.cpp, luma_draw_view.h) and player.set_drawn

### M-064 wrong behaviour: a hidden body hides the carried object every frame but shows only the one carried when the body comes back
- Evidence: MarioActor::calcViewAndEntry (MarioActorDraw.cpp:419-427) calls hideBeeFur each frame while
  kModHiddenBody is set; hideBeeFur (MarioActorSpecialDraw.cpp:177-191) calls MR::hideModel on the carry sensor's host.
  showBeeFur (698-712) shows only the host carried at that moment. An object carried while the body was hidden and then
  thrown or dropped keeps its hidden model. showBeeFur also brings back the Bee's wings while the game's own close-camera
  fade wants them hidden (mFpViewHidden, MarioActorSpecialDraw.cpp:168-171).
- Why: set_drawn without BODY, pick up a shell or a Star Chip carrier, throw it, then set_drawn ALL (or never).
- Confidence: suspected, needs a run (carry and throw a shell in first person); the hide and show calls are verified.

### M-065 wrong behaviour: set_drawn's four slots fill with removed extra players; a later hide is not recorded and outlives its mod
- Evidence: sDrawn has 1 + LUMA_PLAYER_MOST_EXTRA slots (LumaPlayer.cpp:53). player.remove keeps the dead actor in the
  scene (forget 116-123, svcRemove 205-216, endExtra MarioActorGameOver.cpp:72-80), so its slot stays until released()
  (338) at the scene's end or the mod's end. With no free slot svcSetDrawn still writes mModHidden but records nothing
  (281). At the mod's end modGone restores only recorded players (300-308): a game player hidden then stays hidden for the
  scene.
- Why: a mod that hides and removes four extra players in one scene, then hides the game's own player.
- Confidence: verified in code.

### M-066 wrong behaviour: a JOINT rig on Mario reads the skeleton without Mario's own body scale and look offset
- Evidence: actorEye (LumaDrawView.cpp:193-200) takes the eye from MR::getJointMtx (JointUtil.cpp:28-35, the model
  manager's anim matrix). Mario's own code places a joint with mBaseScaleMtx and mLookOfs (MarioActor::getRealMtx,
  MarioActor.cpp:1680-1684). Where scaleBody or mLookOfs is not zero the eye is off the drawn head.
- Why: a first-person rig on "Face0" or the head joint while Mario's body is scaled (a squash) or his look offset is set.
- Confidence: suspected, needs a run (watch the eye against the reflection while Mario is squashed).

### Checked in item 2 and found correct
- main_rig validates every field before keeping anything (LumaDrawView.cpp:724-771); a JOINT name must end inside its 32
  bytes (463-474); the look's up along the look is refused (749-753).
- The game's camera works from its own pose: its view, field of view and near plane go back before the camera director
  runs and are taken anew whenever the game changed them (mainCameraBeforeGame 832-850, mainApply 661-688).
- A rig whose actor dies or whose memory goes gives the game's camera back (mainApply 666-668, released 887-891).
- The stick-to-world mapping reads the mod's camera: MarioActor::updateCameraInfo reads CameraContext through
  MR::getCamXdir/Zdir (CameraUtil.cpp:191-220) during MovementType_Player (SceneExecutor.cpp:61), after the apply at 34.
- set_drawn: the player is found by pointer comparison only (standingPlayer, LumaPlayer.cpp:221-234), so no unknown pointer
  is read; the hidden body still draws in mirrors and water (MarioActorDraw.cpp:857-861), hands follow it
  (MarioActorHand.cpp:99-101), the shadow is its own bit (MarioActorShadow.cpp:283), and model, indirect, blur and
  silhouette use isAllHidden (MarioActorDraw.cpp:483, 592, 807, 937; MarioActorShadow.cpp:522).
- getMarioHolder and getPlayerActor now answer null without a scene (MarioHolder.cpp:22-25, MarioAccess.cpp:628-632).

## M-029, round 4
- LumaDrawView.cpp entry lines are unchanged (321, 375, 401, 495, 527, 565, 690, 724); svcMainRig also reads `look`
  (742-757) and the rig's actor through classOf (458-459).
- luma_input.cpp: input_look (1164) writes through `out`, already listed; input_look_mode (1137) takes no pointer.
- LumaPlayer.cpp svcSetDrawn (236) compares only; not on the list.

## Per platform (round 4)
- Windows: look complete (raw counts, clip, hidden cursor); release on minimise or exit unproven (M-061).
- Linux: SDL relative mode; silent where it is refused (M-062).
- macOS: accelerated deltas and no warp (M-057, M-058).
- Android: finger look works through the pointer finger only (fingers on controls are excluded, host_window_sdl.cpp:
  1622-1632); a mouse's capture is doubtful (M-063).
- Headless host: no live controls run, so the look never holds (look() answers on 0).

## Answers (a) to (d)
(a) One game frame, in order: frame wait at the 60 Hz step (host_main.cpp:1099-1103) and luma::time::beginFrame
(GameSystem.cpp:187); GameSystem::update reads the controllers (GameSystem.cpp:261, GameSystemObjHolder.cpp:69,
PlayerInputs.cpp:89-93, host_main.cpp:3682 and 3745), the live controls sample the look and set the mouse hold, and
game_read turns it into this frame's look (luma_input.cpp:1581-1590); the mods' step handlers run (luma::time::beginStep,
GameSystem.cpp:336, LumaTime.cpp:1055-1073), so a mod calling main_rig there uses this frame's look; then
executeMovementList: the game's view back (SceneExecutor.cpp:31), the camera director (32), the mirror camera (33,
M-056), the mod's view applied (34), the pointer's camera (35), loadViewMtx (36), clipping (37), and the player's
movement, which maps the stick through the camera, at 61; then GameScene::calcAnim poses every model (SceneExecutor.cpp:
102 for the player) and applies the mod's view again (GameScene.cpp:346) before executeCalcViewAndEntryList; the views
render at draw (GameScene.cpp:526). A mod that sets the camera from a later hook acts at 346 only, so its picture is
right and its stick mapping is one frame old.
(b) At 34 the joint is last frame's pose (joint matrices come from the previous frame's executeCalcAnimList; FOLLOW and
WORLD use the actor's place before this frame's movement). At 346 it is this frame's pose. The picture uses 346.
(c) Windows: ClipCursor to a 16 px box at the client's middle, set at each read while held, released at the first read
after focus loss, a pause menu (play context Menu), the settings or HOME menu (host_menu) or a text request
(host_input.cpp:1027-1051, live_controls_engine.cpp:367-368, 398); alt-tab releases at the next read; minimise depends on
Windows (M-061). SDL (Linux, Android): the pump sets relative mode while the window is focused and the game asks
(host_window_sdl.cpp:1165-1179), so focus loss releases at once without a game read; Android mouse doubtful (M-063).
macOS: the cursor is detached from the mouse while the window is key, the app active and no panel is open, applied at
every pump and on resign key (host_window_apple.mm:242-260, 642, 828); no warp (M-058). A second mod: the newest takes
the look; its off or end ends it for both (M-059).
(d) Per event gathered, per game read used: 60 times a second whatever the presented rate. At 30 presented pictures the
step still aims at 60; a late step is skipped, not caught up (host_main.cpp:1099-1101); mouse counts arrive whole, but the
stick's and keys' turn per step is capped at 50 ms of time (live_controls_engine.cpp:329-331, pointer_control.cpp:78), so
a step gap above 50 ms loses stick turn the mouse would not. At 60 it is one read per picture. At 240 the turn changes
60 times a second; the presentation interpolates the camera between game frames (frame_interpolation.h:4-6), so the
pictures between are smooth and up to one step behind; a turn above 60 degrees in one step snaps (M-068).

# Round 5

Read completely this round, one file at a time: LumaDraw.cpp (1934), luma_draw.h (209), LumaDrawInst.cpp (715),
luma_draw_inst.h (85), LumaDrawLight.cpp (384), luma_draw_light.h (83), LumaDrawPost.cpp (450), luma_draw_post.h (81),
luma_draw_native.h (124), LumaPlayer.cpp (362, all of it now). Plus the lines named in each finding outside these.
M-014 corrected at its own entry (boot frames, not the title) from the coordinator's probe runs.

## Item 1: draw, draw.inst, draw.light, draw.post, draw.native header, player

### M-070 draw.inst: two mods on one actor compound each other's colours (and scale) every frame
- Severity: wrong behaviour.
- One sentence: each override takes any colour that differs from its own last write as "the game's", so two overrides
  of the same actor multiply each other's output again every frame and the actor fades to black; at release the
  darkened value is put back as the game's.
- Evidence: LumaDrawInst.cpp:227-229 and 249-251 (game value re-taken when the material's value is not this override's
  last write); 425-440 (of() returns the existing handle only for the same mod and actor; another mod gets a second
  override of the same actor); 674-688 (each frame each live override writes, in slot order); 385-391 with 228 (end puts
  back game8, which by then holds the other override's product). Scale: 323-325 and 347-349 compare the override's own
  stamp with the pose's shared mStamp, which the other override bumps, so on a frame the model's pose is not captured
  again (the case 79-80 names) both scale the already scaled pose again. Parts: 301-310 and 287-289 clear
  MeshEntry::Flag_ModHidden, one bit a mesh shared by both overrides, so A's release shows meshes B still hides.
- Why: mod A and mod B each call draw.inst.of on the same actor and set_colour 0x808080FF. Frame 1: A writes G x 0.5,
  B takes that as the game's and writes G x 0.25. Frame 2: A finds G x 0.25 (not its G x 0.5), takes it as the game's
  and writes G x 0.125; B then writes G x 0.0625. Black within a few frames, and it stays after both release.
- Confidence: verified in code. A run with two example mods colouring the same actor would show it.

### M-071 Shared pools behind per-mod limits: draw textures and fonts, draw.mesh, draw.post programs and slots
- Severity: wrong behaviour (a mod within its documented limit is refused because of other mods); docs wrong for the
  limits the headers do not state.
- One sentence: several draw tables are one pool for all mods, smaller than the per-mod limit times the mods a player
  may run, and the headers state only the per-mod limit (or none).
- Evidence: LumaDraw.cpp:66-67 (kMostTextures = 256 x 4 = 1024, kMostFonts = 32 x 4 = 128, all mods), 1108-1131
  (textureSlot answers LUMA_E_LIMIT when the shared pool is full, whatever the mod holds), the font pool at 1082 alike;
  luma_draw.h promises TEXTURES_MOST 256 and FONTS_MOST 32 "per mod". LumaDraw.cpp:1410 (kMeshesMost 32, "all mods
  together"), 1440-1486 (meshOf keeps an entry for a name with no model, model null, until the scene ends); luma_draw.h
  mesh comment names only NOT_FOUND. LumaDrawPost.cpp:27-28 (kPrograms 64 all mods, kSlots 32 a stage all mods),
  234-238 and 309-316; luma_draw_post.h:29-30 states only the 8 a mod. Compare draw.light and draw.inst, whose headers
  state both limits (luma_draw_light.h:26-28, luma_draw_inst.h INSTS_ALL_MOST 512).
- Why: five mods each loading 210 textures: the fifth is refused at 1024 total while holding 184 of its own 256. One
  mod calling draw.mesh with 32 misspelled names fills the mesh table for every mod until the next scene.
- Confidence: verified in code.

### M-072 draw.mesh answers LUMA_E_LIMIT "32" when no scene runs
- Severity: wrong behaviour (wrong code, a report row in the mod log every call).
- One sentence: with no scene (the LOADING point, a scene change) meshOf returns null and svcMesh reports Service.Limit
  "32" instead of a state refusal.
- Evidence: LumaDraw.cpp:1449 (pScene == nullptr returns null), 1545-1582 (null is reported as 104 with "32").
- Why: a mod that draws a mesh at LUMA_DRAW_LOADING or keeps drawing through a scene load.
- Confidence: verified in code.

### M-073 draw.light: radius and CASTS are checked and then dropped; the header describes traced lights
- Severity: docs wrong (the release ships no ray tracing; owner decision 11-14).
- One sentence: luma_draw_light.h promises a traced emitter with a body radius that casts shadows when CASTS is set,
  but the code passes only place, colour and reach to the game's light set; radius and flags reach the facts only.
- Evidence: luma_draw_light.h:3-5, 31-34, 40-41, 52; LumaDrawLight.cpp:27-28 (says so itself: "until then the world
  light carries its place, colour and reach"), 353-371 (what is sent), 176-181 (radius and casts in facts only).
- Why: any mod that sets CASTS or a radius and expects a difference.
- Confidence: verified in code.

### M-074 draw.native on macOS and iOS: at() succeeds, the function never runs, backend() answers 0
- Severity: missing feature (Apple) and docs wrong.
- One sentence: on Apple builds the backend option is 0, a value LumaDrawNativeBackend does not have, and there is no
  Metal record_native_call, so a mod that sets a function gets LUMA_OK and is never called.
- Evidence: host_main.cpp:3303-3314 ("Metal has none yet (0: draw.native's functions are not called)");
  luma_draw_native.h:33-37 (enum 1 to 3 only), 110-117 (at's and backend's answers, no word of 0 or Apple);
  record_native_call exists only in runtime/src/render/mod_post_d3d12.cpp:252, d3d11/mod_post_d3d11.cpp:151 and
  vk/mod_post_vk.cpp:368 (grep). Windows: Nebula.exe is D3D12 (CMakeLists.txt:598), NebulaVk Vulkan (963), NebulaD11
  D3D11 (1067, 1090): one backend a program, so backend() is right there.
- Why: any draw.native mod run on a Mac or iPhone.
- Confidence: verified in code.

### M-075 draw.post.program takes a store path from the root, undocumented
- Severity: docs wrong.
- One sentence: a name that starts with '/' is used as a store path, so a mod can load another mod's cooked program;
  the header says the program is `<name>.post` in the mod's own store.
- Evidence: LumaDrawPost.cpp:240-247; luma_draw_post.h:45-46.
- Why: a mod passing "/Luma/<other id>/post/<name>".
- Confidence: verified in code. Read-only and limited to .post files, so not a security fault.

### M-029, round 5 (additions only)
- LumaDraw.cpp: 755 svcTri(points[3], colours[3]), 986 svcTransformPush(m[6]), 1381 svcTri3(points[3], colours[3]):
  each reads mod memory in host code; a bad pointer ends the game. svcLine 702 takes its points by value: not an entry.
- LumaDrawInst.cpp, LumaDrawLight.cpp, LumaDrawPost.cpp: the round 3 entries are complete (checked against each
  signature this round). LumaPlayer.cpp: svcMake 125 is the only entry; set_drawn's standingPlayer (221-234) and remove's
  extraOf (107-114) only compare the pointer.

### Shared state across mods (M-059 pattern), item 1 services
- draw: texture pool, font pool, mesh table (M-071). draw.mesh's colours write into the shared model's materials but are
  set again on every call before its draw (LumaDraw.cpp:1509-1543): correct.
- draw.inst: per-actor materials, mesh flags and pose (M-070).
- draw.post: program table and stage slots (M-071).
- draw.light: one game point light for all (documented, luma_draw_light.h:5-6); 256 lights across mods (documented).
- player: 3 extra players across mods (documented, luma_player.h:23-24); set_drawn newest-mod-wins (documented in code
  47-48; M-065 covers its slots).

### Per-frame work with no mod (M-069 pattern), item 1 services
None. draw: point() and frame() one load each (LumaDraw.cpp:1775-1846, 1855-1869). draw.inst: frame() and released()
one load each (669-672, 699-702). draw.light: frame() one load (339-342). draw.post: stage() one load a stage
(414-417). player: released() two loads (338-351).

### Checked and found correct (item 1)
- draw.inst: of() checks class, death and model before taking the actor (412-418); hidden bit array sized meshCount/32+1
  (576) and every mesh index checked against meshCount (281); params capped at 16 with replace-in-place (523-534);
  value ranges checked and finite (499-506); scale range (601-606); actor death ends the override (679-682); scene end
  forgets without touching freed memory (699-708).
- draw.light: every desc field range-checked, NaN fails (85-113); StructSize row (122-131); per-mod and all-mod limits
  as the header (211-230); remove and the mod's end free the slot (284-316).
- draw.post: cooked file length checked exactly against header, params and text (157-190); param row and column
  bounds (179); stage bound (292, 331); count and finite checks (334-344); freeing a program empties the slots that ran
  it (129-141); stage order is the Mods list, bottom first (418-436).
- player: make checks size, input, pad, finite place and turn, scene with player (130-159); co-star is a counter
  (PlayerInputUtil.cpp:35-41), so two pad extras and their removal balance.
- draw.native: one backend a program on Windows (CMakeLists.txt:598, 963, 1067, 1090), so backend() is right there.

## Item 2: save, save.global, save.game (LumaSave.cpp 1-1551 all, LumaSaveChunk.cpp 558, luma_save.h 115)

### M-076 save.game.export_file during the boot ends the game
- Severity: crash.
- One sentence: the export is delivered at the next frame's start and calls makeExport through
  mSaveDataHandleSequence->mSaveDataHandler with no check, but the handler is made only once the system archive has
  loaded, several frames after mods start.
- Evidence: LumaSave.cpp:862-872 (no null check; makeExport on the handler); SaveDataHandleSequence.cpp:44 (the handler
  made in initAfterResourceLoaded), called from GameSystem.cpp:179 (initAfterSystemArchiveLoaded, reached through
  exeLoadSystemArchive 155-161 after the InitializeAudio and InitializeBootScene states); mods start at GameSystem.cpp:102,
  before even the sequence director (108); frameStart runs from the first frame (GameSystem.cpp:272). svcExport
  (909-959) checks nothing of the game's state. The same pointers are checked in svcOffer (1292-1297) and forgetWork
  (718-725), so the gap is export's alone. SaveDataHandler::makeExport reads members at once (SaveDataHandler.cpp:410,
  findFile on a null this).
- Why: a mod with the save.game permission calling export_file in its start or its first frames. The fault is in host
  code, so the guard does not resume it (M-011).
- Confidence: verified in code; a run would confirm: a probe mod calling save.game.export_file(1) in `on start`.

### M-077 export_file and offer read the user files before the game has read them
- Severity: wrong behaviour (boot frames only).
- One sentence: before the boot's save read is over, export answers LUMA_E_NOT_FOUND ("that user file is empty") for a
  file that exists, and offer's screen shows every file empty, because neither waits for the read that offerWork and
  forgetWork wait for.
- Evidence: SaveDataHandler.cpp:410-413 (an empty in-memory config is Export_Empty); LumaSave.cpp:877-879 (Empty is
  NOT_FOUND), 1175-1184 (offerFacts reads pHandler->getFile); the wait used elsewhere: 723 and 1255
  (getPreLoadState() == 0 || isActive()).
- Why: export_file or offer between the system archive's load and the end of BootScene::exeMountGameData's read.
- Confidence: verified in code that no wait exists; a run would show how many frames the window spans.

### M-078 save.global waits for the host's file thread with an unbounded spin on the game thread
- Severity: wrong behaviour (a hitch or a hang), suspected.
- One sentence: the first save.global call, a broken file's set-aside copy and the mod's stop each spin with pause or
  yield and no limit until the host's request is done.
- Evidence: LumaSave.cpp:296-304 (waitFor), 365-366 (first call), 368-372 (set-aside copy), 580-583 (globalGone waits for
  a running write).
- Why: a save folder on a slow, sleeping or network drive, or a write the OS holds (antivirus, cloud sync): the game
  frame does not end until the request does.
- Confidence: suspected, needs a run with the save folder on a stalled drive.

### M-079 luma_save.h leaves out answers and a limit the code gives
- Severity: docs wrong.
- One sentence: save get, set and remove answer LUMA_E_ABSENT for a file whose mod chunk is a later version's; save.global
  answers LUMA_E_ABSENT for a later version's file or one over 1 MiB; export_file has a 32-request pool across mods;
  none of these is in the header.
- Evidence: LumaSave.cpp:105-106, 126-127, 156-157 (isNewer is ABSENT); 340-345 and 375-377 (global ABSENT); 807-808 and
  933-935 (cExportSlots 32); luma_save.h:41-45, 62-74 (global lists NOT_FOUND, ARGUMENT, LIMIT, IO, MEMORY), 92 (only
  "a 5th open request").
- Why: a file saved by a later Nebula, then played on this one; nine mods each with four exports open.
- Confidence: verified in code.

### M-029, round 5, item 2
The round 3 entries for LumaSave.cpp (92, 113, 147, 163, 471, 492, 525, 909, 961, 1274) are complete; cancel (1385)
compares the handle only. LumaSaveChunk.cpp has no entry a mod calls directly (the service copies the key length with
textLength on the mod's key, already counted under svcGet/svcSet/svcRemove).

### Shared state and no-mod cost, item 2
- Shared across mods, documented: one file's 1 MiB of mods' values (luma_save.h:48-49), one offer at a time (101).
  Undocumented: the 32 export requests (M-079). save.global is per mod (a store a mod, LumaSave.cpp:227-264).
- No-mod cost: none. The save hook is set only when a store, a FORGET or an offer has work and unset when none has
  (457-464); the chunk is 16 bytes a file (30-32).

### Checked and found correct (item 2)
- The chunk parser checks the version, every length against what remains, ids and keys by character, unique ids and
  unique keys, each value at most 64 KiB, each mod at most 256 KiB, and that the blocks fill the chunk exactly
  (LumaSaveChunk.cpp:162-247); a later version's chunk is kept whole and written back unchanged (165-173, 134-137).
- splice moves overlapping bytes in the safe direction (346-359); remove and forgetMod drop an empty mod block (445-449,
  526-528); forgetMod also removes every "<id>/" key other mods keep (511-532).
- save.global: a broken file is copied to .bin.broken before the good file is written over (306-317, 406-412); a refused
  write backs off from 1 s to 1 min and is said once a run (418-425); the mod's stop starts its last write (574-588).
- save.game: offer checks the data path (984-1005: no absolute, no "..", no '\' or ':'), size (1009, 1340), the parts
  (1353), and that the other character's data loads (1152-1160); the old file is kept in luma/replaced first and a
  refused copy means nothing is replaced (1231-1242); a loaded file is never replaced under the player (1262-1266).
- luma_save_kept copies under the lock and calls the host's fn outside it (1478-1527); luma_save_forget checks the id's
  characters and caps the session at 256 ids, logging the rest (1531-1550).

## Item 3: audio (runtime/src/luma_audio.cpp 2012 all, luma_audio.h 193, LumaGameSound.cpp 326)

### M-080 audio.game_play takes another mod's ended slot; that mod's stop then answers HANDLE
- Severity: wrong behaviour (a false Service.Handle Error in the other mod's log).
- One sentence: when no slot is free, game_play reuses any ended slot, whoever owned it, so the first owner's
  game_stop or game_place on its ended handle answers LUMA_E_HANDLE, reported, where the header promises LUMA_OK.
- Evidence: LumaGameSound.cpp:163-182 (pEnded taken from any owner), 88-95 (find needs owner and id to match), 205-206
  (new owner and id); luma_audio.cpp:1136-1138 and 1162-1164 (HANDLE reported); luma_audio.h:177-182 ("LUMA_OK also
  when it has ended"). The 64 slots are one pool for all mods (LumaGameSound.cpp:23), so a fifth mod holding none is
  refused with BUSY once four hold 16 each (186-187).
- Why: mod A plays SE_SY_STAR_GET, it ends; with the 64 slots used, mod B's game_play takes A's slot; A's tidy-up
  game_stop(handle) logs Service.Handle.
- Confidence: verified in code.

### M-081 A replaced music stream is read from the store mapping on the audio thread, under the mixer lock
- Severity: wrong behaviour (audio drop-outs, game-thread waits), suspected.
- One sentence: music's cooked streams (up to 20 minutes, six channels) are mapped whole and the mix reads each sample
  from the mapping, so every new page is a drive read on the audio thread while g_lock is held.
- Evidence: luma_audio.cpp:1435-1458 (the .ast found and mapped whole), 351-357 (sample_at copies from the mapping),
  1955-1981 (mix holds g_lock over every item); luma_audio.h:142-143 ("read from the store as it plays");
  GameAssets.hpp:33 says the same mapping "may wait for the drive".
- Why: audio.music with a long cooked stream on a hard disk, an SD card or a cold cache; every game-thread audio call
  waits on g_lock meanwhile.
- Confidence: suspected, needs a run with a long stream on slow storage and an audio underrun count.

### M-082 luma_audio.h: "every call never blocks" is not kept
- Severity: docs wrong.
- One sentence: every call takes g_lock, which the audio thread holds for the whole mix of every voice, stream and tap
  of every mod, so a call waits up to one mix pass.
- Evidence: luma_audio.h:6-7; luma_audio.cpp:1973-1992 (mix under the lock), 1999-2005 (tap_output under it), each call's
  lock_guard (for example 1181, 1201, 1318).
- Why: many mods with many voices; worse with M-081.
- Confidence: verified in code.

### M-029, round 5, item 3
The round 3 list for luma_audio.cpp (1031, 1036, 1041, 1075, 1124, 1230, 1305, 1349, 1410, 1537) is complete:
voice_set, voice_stop, bus_volume, stream_close, music_restore, tap_close and game_stop take values only. In
LumaGameSound.cpp the pointers come from the provider after its checks, so no extra entry.

### Shared state and no-mod cost, item 3
- Shared across mods: one music (documented, luma_audio.h:145-146); the game's 64 sound slots (M-080); the 32
  replacement voices and the replacement index, highest mod wins (documented, sound.md 6). Per mod and correct: 32
  voices, 4 streams, 2 taps, the four bus shares (ModState, 66-70).
- No-mod cost: the audio thread zero-fills a mods buffer every audio block (native_audio.cpp:246, `s.mods.assign`)
  before mix() finds nothing (1960-1966, two loads); tap_output one load (1997); LumaGameSound::frame and replaced one
  test each (LumaGameSound.cpp:114-126). The zero-fill is the one instance of the M-069 pattern here (small, but every
  block with no mod).

### Thread and lifetime, item 3
- Items are made and freed on the game thread only, under g_lock; the mix skips owner-null and silent items and never
  allocates (header comment 8-10, reap_locked 268-279, mod_gone 1830-1851). Sound.
- A tap's blocks are copied out under the lock and handed in the game thread's own buffer (547-571). Sound.
- Sample pointers are into the store mapping, which lives for the process. Sound.

### Checked and found correct (item 3)
- read_sound and read_stream check magic, version, rate, channels, length against the file, loop points, volume, pitch
  and bus (624-702); a replacement's name is checked to end inside the file (1652-1661).
- play: name length by strnlen (890), every `how` field ranged (898-915), place checked (759-772), 33rd-voice rule as
  the header (991-1019). stream_push clamps to the ring's room and silences NaN (1323-1338).
- Placed voices: camera front is the view's forward (CameraUtil.cpp:214-219 negates the inverse view's Z), so
  front x up is the right side and left and right are not swapped (luma_audio.cpp:819-833).
- A mod's stop silences its voices, streams, taps and game sounds the same frame (1830-1851, LumaGameSound.cpp:254-265);
  its music ends and the game's comes back (1984-1987).

## Item 4: fs, fs.exchange, assets (luma_fs.cpp 1387, luma_assets.cpp 1206, luma_game_store.cpp 112, luma_fs.h 126,
## luma_assets.h 136, all read completely)

### M-083 fs refuses a data folder to every mod whose id has a '.'
- Severity: wrong behaviour.
- One sentence: luma_fs's own id check allows only a-z, 0-9 and '-', while a mod id may hold '.', so a dotted id gets
  no data folder: every fs request answers LUMA_E_ABSENT, FORGET removes nothing and save.game.offer finds no file.
- Evidence: luma_fs.cpp:132-137 (valid_id: a-z, 0-9, '-' only); luma.cpp:259-269 (the real
  rule allows '.', not first, not last, no ".."); LumaSaveChunk.cpp:43-56 and LumaSave.cpp:677-690 also allow '.'.
  Uses: root_folder 957-959 then 1060-1061 (ABSENT); data_folder 1284-1287, used by find_data_file 1303-1322 (the
  data_file fact behind save.game.offer, LumaSave.cpp:1305), data_bytes 1324-1331, has_data_folder 1333-1340, data_ids
  1342-1359 (a dotted folder name is skipped at 1352) and forget_data 1361-1366.
- Why: any mod with an id such as "speedrun.timer": its fs.write answers ABSENT ("no data folder on this platform").
- Confidence: verified in code. No conformance test uses a dotted id (h/tests: no "id" with '.').

### M-084 One mod can pin gigabytes of host memory through fs and assets.cook
- Severity: crash (the OS ends the process for memory), suspected.
- One sentence: fs keeps a copy of every queued write (32 requests x 64 MiB = 2 GiB a mod) and assets.cook a copy of
  every waiting source (8 x 256 MiB = 2 GiB a mod), with no bound across mods or on the total.
- Evidence: luma_fs.cpp:912 (the only bound is 32 open a mod), 1011-1020 (64 MiB copied at the call); luma_assets.cpp:
  1073-1079 (8 a mod), 1059 and 1088 (up to 256 MiB copied).
- Why: a mod writing a 64 MiB replay file 32 times in a frame on a 4 GB Android tablet or an 8 GB PC.
- Confidence: suspected, needs a run watching the process's memory with such a mod.

### M-085 Every fs write walks the whole folder; one worker serves every mod
- Severity: wrong behaviour (slow writes delay every mod's reads), suspected.
- One sentence: do_write measures the folder's space by walking every file under it each time, and the Exchange
  folder is shared and may be a link the player made to a large folder (followed once at the root), while one worker
  thread does every mod's requests in order.
- Evidence: luma_fs.cpp:692-705 (folder_bytes each write), 300-314 (recursive walk), 166-180 (the root followed once),
  484-524 (one worker for all mods).
- Why: a mod saving a small file each second into an Exchange folder of tens of thousands of files; every other mod's
  fs.read waits behind it.
- Confidence: suspected, needs a run timing writes in a large Exchange folder.

### M-086 assets.provide's "256 MiB over a session" is per run of the mod
- Severity: wrong behaviour (memory grows without bound across reloads).
- One sentence: a mod's end erases its spent count while its bytes stay for the process, so a mod taken out and loaded
  again provides another 256 MiB each time and none of it is freed.
- Evidence: luma_assets.cpp:1110-1114 (spent erased at mod end), 145-149 and 488-505 (blocks never freed);
  luma_assets.h:68-69 ("by one mod over a session").
- Why: reload of a mod that provides large archives at its start (reload.md); each reload adds its 256 MiB.
- Confidence: verified in code.

### M-029, round 5, item 4
The round 3 entries for luma_fs.cpp and luma_assets.cpp are complete (each request function reads a path string; write
and provide read `bytes` and the file array; take_read/take_list write the caller's buffer; assets_entry writes `name`).
luma_game_store.cpp is the validator's view, called by the host, not by a mod.

### Shared state and no-mod cost, item 4
- Shared across mods: one fs worker and queue (M-085); the Exchange folder's 1 GiB (documented, luma_fs.h:38); the
  provided-path table, first provider wins and others get EXISTS (documented, luma_assets.h:110); one cook thread in
  order. Per mod and correct: fs's 32 requests (912-916) and the full-folder report once a mod (643-651); provide's
  256 MiB a run.
- No-mod cost: none. fs has no frame hook (the worker starts at the first request, 496-524); assets' provided lookups
  are one atomic load while nothing was ever provided (538-541).

### Per platform, item 4
- fs: data and Exchange folders under nebula_data_directory(); none on a platform where that is empty (ABSENT,
  882-885, 1289-1292). Case rule (find_child 223-240): the exact name first; on a miss Linux, Android and Apple scan
  the folder, Windows does not; on Windows and Apple a found name is scanned again for its spelling when asked.
- assets.cook: the importer is linked weak on Apple and loaded at run time elsewhere (luma_assets.cpp:754-789); with
  none, cook answers ABSENT (1068-1069). Whether each platform's package ships the importer was not
  checked this round.

### Checked and found correct (item 4)
- The path rule (parts, bytes, characters, device names, the game's .tmp. files) is applied on the game thread before
  queueing (73-130, 921-932); every step below a root is checked not to be a link or junction (148-162, 250-297);
  remove never follows a link inside (318-343); move refuses `to` inside `from` (1034-1041) and never replaces (781-783).
- A cancelled read or list frees its place at once; a cancelled write still happens in order (603-608, 1102-1130);
  a stopped mod's writes still land and nothing is posted for it (1213-1225, 611-625).
- assets: a provide is built off the lock and committed under it; indices, names and bytes are never freed, so the
  game's loading threads may hold them (145-149, 447-508); a provide inside an archive or onto a game folder of the
  other form is refused (451-476); a cooker path the table does not take is FORMAT, the file's fault (927-930).

## Area 2 closing table

"Read" means every line of every file in "files" was read in some round. Crash count is that service's entries on the
M-029 list (host code that reads or writes a mod pointer, so a bad pointer ends the game), plus other crash findings.
Tests are the suites in D:\NebulaWork\build\audit-conf\luma.

| Service | Files | Lines | Read completely | Platforms where it works | Crash on bad argument | Shared-state faults | Per-frame cost, no mod | Tests | Verdict |
|---|---|---|---|---|---|---|---|---|---|
| draw (2-D, 3-D, mesh, text) | LumaDraw.cpp, luma_draw.h | 1934 + 209 | yes | all (TypedDraw, every renderer) | 19 (16 round 3 + 755, 986, 1381) | texture, font and mesh pools (M-071) | point() and frame() one load | j/tests/draw 2401-2404; fb/tests/world 2454-mesh | Sound and complete; shared pools and an undocumented mesh limit refuse mods within their stated limits. |
| draw.inst | LumaDrawInst.cpp, luma_draw_inst.h | 715 + 85 | yes | all | 5 | per-actor colours, flags, pose (M-070) | frame(), released() one load | fb/tests/world 2452, 2453 | Good alone; two mods on one actor compound to black (M-070); set_texture is a stub (ABSENT). |
| draw.light | LumaDrawLight.cpp, luma_draw_light.h | 384 + 83 | yes | all, as the game's lamp only | 3 | one game point light (documented) | frame() one load | fb/tests/light 2461-2463 | Works as a game lamp; radius and CASTS do nothing on this tree (M-073). |
| draw.post | LumaDrawPost.cpp, luma_draw_post.h | 450 + 81 | yes | D3D12, D3D11, Vulkan (host post_register) | 2 | 64 programs, 32 slots a stage (M-071) | stage() one load a stage | fb/tests/world 2481 | Sound; shared pools and an undocumented root path (M-075). |
| draw.native | LumaDrawNative.cpp, luma_draw_native.h | 243 + 124 | yes | Windows, Linux, Android; never runs on macOS, iOS (M-074) | 2, and its fn runs unguarded (M-013) | 16 functions across mods (documented) | (round 1) | fb/tests/world 2491, 2492 | Full-access by design; Apple accepts and never calls (M-074). |
| draw.view | LumaDrawView.cpp, luma_draw_view.h | 907 + 155 | yes (round 4) | all | 8 | main rig and look (M-059) | round 4 | fb/tests/world 2471, 2472 | See round 4 (M-059, M-064 to M-068). |
| player | LumaPlayer.cpp, luma_player.h | 362 + 82 | yes | all | 1 | 3 extras across mods (documented); set_drawn newest wins (M-065) | released() two loads | h/tests/player 2961, 2962 | Sound; set_drawn slot faults as round 4. |
| save | LumaSave.cpp 1-190, LumaSaveChunk.cpp, luma_save.h | 190 + 558 + 115 | yes | all | 4 | 1 MiB a file for all mods (documented) | none | h/tests/save 2261-2268 | Sound; ABSENT for a later version's chunk undocumented (M-079). |
| save.global | LumaSave.cpp 215-620 | 405 | yes | all | 3 | none (a store a mod) | none (hook only with work) | h/tests/save 2269a, 2269b | Sound; unbounded spin on the file thread (M-078). |
| save.game | LumaSave.cpp 621-1551 (with the shared file thread) | 931 | yes | hosts with the offer screen (ABSENT else) | 3, plus M-076 (export at boot) | one offer across mods (documented); 32 exports (M-079) | none | h/tests/save 2270a-c, 2271 | Ends the game if export runs during the boot (M-076); reads files before the boot's read (M-077). |
| audio | luma_audio.cpp, luma_audio.h, LumaGameSound.cpp | 2012 + 193 + 326 | yes | all hosts with NativeGameAudio | 10 | one music (documented); 64 game-sound slots (M-080) | a buffer zero-fill each audio block (native_audio.cpp:246) | h/tests/audio 2601-2611; g/tests/sound 3871-3873 | Sound threading; HANDLE after slot reuse (M-080); long music streams read on the audio thread (M-081). |
| fs, fs.exchange | luma_fs.cpp, luma_fs.h | 1387 + 126 | yes | every platform with a data directory | 9 + 7 | one worker; the Exchange folder's 1 GiB | none | h/tests/fs 2301-2312; h/tests/mods 2291a, 2291b | Dotted ids get no folder (M-083); memory and walk costs suspected (M-084, M-085). |
| assets | luma_assets.cpp, luma_game_store.cpp, luma_assets.h | 1206 + 112 + 136 | yes | all; cook where the importer ships | 6 | provided-path table, first wins (documented) | none | h/tests/assets 2801-2807 | Sound; the 256 MiB bound resets on reload (M-086); cook can pin 2 GiB (M-084). |
| input | luma_input.cpp, luma_input.h | 1753 + 311 | yes (round 4) | see round 4 per platform | 15 | look (M-059) | M-069 | h/tests: none named input; j/tests/controls 6305a, 6305b | See round 4 (M-056 to M-063, M-067 to M-069). |
| time | LumaTime.cpp, luma_time.h | 1191 + 134 | yes (round 1; corrected in round 6) | all | 5 | world hold and rate (documented) | one load | h/tests/time 2201-2218 | Closed: findings as rounds 1 to 3; nothing new. |
| ui | LumaUi.cpp, luma_ui.h | 1501 + 171 | yes (round 1; corrected in round 6) | all | 10 | menus on top, one at a time | frame() one load | j/tests/ui 3801-3817 | Closed: findings as rounds 1 to 3; nothing new. |
| net | luma_net.cpp, luma_net_reach.cpp, luma_net.h | 5169 + 201 + 332 | yes (rounds 2, 3, 6) | all for TCP, UDP, TLS, HTTP, WebSocket; ipc as pipes (Windows), abstract names (Linux, Android), files (Apple; iOS suspected never, M-097); link-local IPv6 nowhere (M-099) | every call (round 3 list) | none: every limit per mod or per socket; one I/O thread for all | none (I/O thread waits with no socket; 10 ms wakes while one is open) | h/tests/net 2701-2710 | Limits and host checks sound; pongs unbounded (M-055), lookups unbounded (M-095), ws_send 4 MiB (M-096), endpoint lengths (M-097), exit (M-098, suspected). |
| system, clipboard, links | luma_system.cpp, luma_system_win32.cpp, luma_system_apple.mm, luma_system_sdl.cpp, luma_system.h | 1175 + 602 + 335 + 561 + 148 | yes (round 6) | Windows, Linux, macOS, Android; iOS display never filled (M-091) | 9, plus M-090 (suspected) | facts read mods state from any thread (note) | none (no frame hook) | h/tests/system 2221-2234 | Clipboard pictures from other programs go through stb_image (M-090, suspected crash); SDL and Apple clipboard waits have no time limit (note). |
| capture | luma_capture.cpp, luma_capture.h | 1080 + 103 | yes (round 6) | Windows, Android (M-012) | 2, plus M-092 (exit, suspected) | one camera for all mods; a second gets ENDED ABSENT (note) | none (no frame hook) | h/tests/audio 2606, 2607 | Detached device threads at exit (M-092); time_ns is conversion time (note). |
| mods | runtime/src/luma_mods.cpp, luma_mods.h | 1748 + 174 | yes (round 6) | all | 13 | the mods.ids record (M-093) | none (no frame hook) | h/tests/mods 2241-2291b | A dotted id poisons mods.ids and cannot be forgotten (M-093); a key option gives the default, not the binding (M-094). |
| anim, movie, content.fx, content.model, font | LumaAnim.cpp, LumaMovie.cpp, LumaContentFx.cpp, LumaContentModel.cpp, LumaFont.cpp (+ luma_anim.h, luma_movie.h, luma_content.h, luma_content_fx.h, luma_content_model.h, luma_text_tags.h) | 901 + 568 + 962 + 963 + 381 (+ 124 + 55 + 36 + 60 + 84 + 238) | yes (round 6) | all | 10 (anim 3, movie 2, fx 2, model 3; font none), plus M-088 (suspected) | plays, events, effects, models shared behind per-mod limits (M-087); a reused model part keeps old colours (M-089) | anim, fx, model frame() return with none live; movie two loads and one scene lookup | j/tests anim-play 3401, 3611-3619; movie 3101-3105; effects 3501-3513; models 3201-3211 | Shared pools refuse within stated limits (M-087); cooked-table cells unchecked (M-088); colours leak across handles (M-089). |

Tests that would catch this round's findings and do not exist: two mods on one actor (draw.inst); export_file during
the boot; a dotted mod id with fs; a mod reloaded while providing assets; game_stop after slot reuse.

# Round 6

Files read completely this round are named at the head of each item. Correction to the round 5 closing table: LumaTime.cpp
(1191) and LumaUi.cpp (1501) with luma_time.h and luma_ui.h were read completely in round 1 (coverage row "2 Services",
and the round 1 notes "time (LumaTime.cpp, all 1191 lines" and "ui (LumaUi.cpp, all 1501 lines"); the round 5 rows that
said "no" are corrected in the closing table. They were not read again.

## Item 1: anim, movie, content.fx, content.model, font

### anim (LumaAnim.cpp 901, luma_anim.h 124: read completely)

### M-087 anim: the play slots and the event pool are shared by every mod behind a per-mod limit, and a lost END is silent
- Severity: wrong behaviour.
- One sentence: plays are 512 slots for all mods (64 x 8) and waiting events 2048 for all mods, so a ninth busy mod is
  refused below its own 64 with "512" reported, and when the event pool is full an END or MARK is dropped with only a
  host log line, though the header calls END "last of its play's events".
- Evidence: LumaAnim.cpp:43-44 (kMostOpen, kMostEvents across mods), 521-540 (LIMIT reports kMostOpen when the mod is
  below 64), 334-361 (no free event: nebula_log, nothing given); luma_anim.h:25-26 ("Plays one mod has going at once"),
  40-41 (END is the last event of a play).
- Why: nine mods each keeping 60 looping plays on actors; or one mod with many plays and marks while another mod's
  callback holds events (each event is busy from its post to its deliver). A mod that misses END keeps a handle that now
  answers HANDLE.
- Confidence: verified in code (the pool sizes and the drop); the event case needs a run to reach 2048 waiting events.

M-029 for anim is complete as written in round 3 (684, 698, 742; stop takes no pointer). Shared state: one play of a kind
per actor across mods, a later mod's play ends an earlier mod's (documented, luma_anim.h:88-89). No-mod cost: frame() and
released() return on sLive == 0 (843-846, 886-889). Platforms: all (game code). Tests: j/tests/anim-play 3401, 3611-3619.

Checked and found correct (anim): every how field checked before any start, including non-finite marks and start past
the clip's last frame read from the resource before the start (467-520); kinds of one name started together with the
first present leading (499-508, 549-569); replaced plays ended before the slot is filled and sHigh lowered safely while
looping (542-547, 443-459); an event's name copied into the call's own buffer and the slot freed before the mod's
function so a replay can post (303-324); marks on a wrap, a once-then-reset clip and a reverse clip (811-840, 869-879);
a model's play ends when its part is detached or reused (852-859); a scene end ends plays by address range (886-897); a
mod's stop ends its plays without an event (783-790).


### movie (LumaMovie.cpp 568, luma_movie.h 55: read completely)

### M-088 A mod's cooked table (.subs, .events) is read with no bounds check on its cells; the comment says otherwise
- Severity: crash, suspected (a host-code read outside the mapped file ends the game; M-011).
- One sentence: movie's tableFits checks the header, the cell block and that the words end in a NUL, but not each text
  cell's offset, though its comment says "each text cell inside the words"; anim's .events check is the magic only, so
  a .events whose rows, columns or cell offsets point past the file is read as is.
- Evidence: LumaMovie.cpp:226-239 (tableFits: no per-cell check), 519-525 (a text cell becomes a pointer and goes to
  MessageSystem); LumaAnim.cpp:397-420 (only "NTBL" checked, then searchItemInfo, getNumEntries) and 594-599, 831-838
  (rows read each frame); Table.hpp:88-96 (getValueFast: text() + cell, unchecked); Table.cpp:17-19 (the game's own
  reader trusts the importer's cook). LumaContentModel.cpp:732 is the same magic-only check (see content.model).
- Why: a .luma that carries a hand-made <clip>.events or <movie>.subs instead of the cook's output. Whether the
  installer re-cooks or validates these files was not read in this round (mod_import.cpp, Area 8).
- Confidence: suspected, needs a run with a .luma holding a .subs whose text cell is 0xFFFFFF00, played with
  movie.play; and a read of the import path to see whether such a file reaches the store.

M-029 for movie is complete (298 name and out, 386 frame and frames). Shared state: one movie for the whole game
(documented, luma_movie.h:19-20, 36); one mod's play ends another mod's or the game's movie (documented). The ends pool
is 4 for all mods; a fifth end in one frame is dropped with a log line only (LumaMovie.cpp:36, 166-184), the same
silent drop as M-087. The name is put into "/Luma/<id>/<name>.nmov" with no check for ".." (317); whether the store's
find settles ".." (host_main.cpp:2922-2930, asset_table().find) was not read, so a mod playing another mod's movie is
the M-075 pattern, suspected. No-mod cost: frame() is two loads (451-461); drawSubtitles is one scene-object lookup a
frame for the game's own movies too (480-487). Platforms: AV1 decode is a build option, on by default for every target
(CMakeLists.txt:1104-1110, platform/sdl/CMakeLists.txt:283-287); where it is off the game's player gets an AV1 file it
cannot decode (not read further). Tests: j/tests/movie 3101-3105.

Checked and found correct (movie): the header magic and length read before the play (327-334); the scene's player is
compared each frame so a scene end, the game's own movie over it and a skip each end the mod's movie once (462-477);
the skip wait is dropped when the scene changes on the black screen (452-457); stop and position check the owner and
the scene (374, 391); the store path the player keeps lives in a static (72, 344-346).

### content.fx (LumaContentFx.cpp 962, luma_content_fx.h 60, luma_content.h 36: read completely)

- The M-087 pattern again: open effects are 4096 slots for all mods (LumaContentFx.cpp:60, 256 x 16) behind the
  header's "Effects one mod has started" 256 (luma_content_fx.h:22-23); the seventeenth busy mod is refused with
  "4096" (648-666). Verified in code; recorded under M-087 rather than as a new finding.
- A place's offset and turn are not checked for NaN or infinity (703-706), unlike tint's scale (801-804); a NaN place
  puts the effect's emitters at NaN. Wrong behaviour of the mod's own effect only; not given an ID.
- The draw-group list (Luma/<id>/effects.txt) is read once a session per mod id and kept (292-331), so a mod updated
  while the game runs keeps its old groups until the game restarts. The read's count is not compared with the length
  (321: `>= 0`), so a short read parses the rest of an uninitialised buffer; the parser stops at the first byte outside
  the form, so the effect falls to group 0 with a Content.EffectsFile entry. Not given an ID (no crash, the mod's own).

M-029 for content.fx is complete as round 3 lists it (750 actor, name, joint, place, out; 756 socket, place, out; stop
and tint take no pointer). Shared state: shells are per actor and reused only when free and their particles are gone
(447-455), never across a busy handle. No-mod cost: frame() returns on sLive == 0 (877-880); released() is a scene-end
hook, not a frame one. Platforms: all (game code). Tests: j/tests/effects 3501-3513.

Checked and found correct (content.fx): the class check before any use of the actor (632-635); a missing name falls to
the placeholder and a missing placeholder shows nothing without failing (669-681); a shell lives in the actor's arena
and is unlinked when the arena or the effect system goes (456-460, 930-956); an effect ends when its actor dies, its
model is detached or the mod stops, each stopping the emitter as the game does for its own (844-855, 889-904); the
clipped state follows the actor (908-916); the empty-effect check runs once per name a session (353-370, 588-617).

### content.model (LumaContentModel.cpp 963, luma_content_model.h 84: read completely)

### M-089 content.model: a reused part keeps the colours an earlier handle set, from the same mod or another
- Severity: wrong behaviour.
- One sentence: a detached part is used again for the next attach of the same host, model, follow and joint, by any
  mod, and set_param wrote into that part's own material copy, which the reuse does not reset, so the new handle's
  model starts with the old handle's colours.
- Evidence: LumaContentModel.cpp:342-360 (reuse: place, appear, show; nothing about materials), 760-792 (writeParam
  writes Model::ownMaterial of the part's model), 345-347 (the match has no mod); compare content.fx, which resets its
  reused shell's scale and colour (LumaContentFx.cpp:727-729, "A shell used again starts as a new one").
- Why: mod A attaches "a:hat" to Mario and sets its "Body" colour red, then detaches; mod B (or A again) attaches
  "a:hat" to Mario with the same follow and joint: the hat is red before any set_param.
- Confidence: verified in code.

Also here:
- The M-087 pattern: attached models are 1024 slots for all mods (59: 64 x 16) behind the per-mod 64
  (luma_content_model.h:22-23), refused with "1024" (560-579).
- <model>.params is read with the magic-only check of M-088 (731-753).
- The header says set_param holds "on this model only, until it is set again or a clip that moves that colour plays"
  (luma_content_model.h:68-72); M-089 is the case it misses.

M-029 for content.model is complete as round 3 lists it (528 actor, name, joint, place, out; 671 parts x count, total;
795 material, param, rgba; detach and show take no pointer). Shared state: parts by host, model, follow and joint
(M-089). No-mod cost: frame() returns on sLive == 0 (896-899); actorOf and partOf return on sLive == 0 (942-958).
Platforms: all (game code). Tests: j/tests/models 3201-3211.

Checked and found correct (content.model): the class, death and model checks on the host before any use (541-544);
a level block ("<id>:<path>Low") refused as a model (198-217, 545-547); the placeholder in place of a missing model,
nothing shown without one (581-592); a missing joint falls to the base (594-605); the skeleton follow's joint tree
walked parents first from the model's own data, guarded when the host draws another model (233-300); the Low level
appears exactly while the host's far model does (502-524); a host's death, a detach and a mod's stop make the part dead
(478-500, 867-874, 906-908); a scene end unlinks parts in the released range without touching them (920-940);
set_param checks finite values and clamps to the register's range (806-811, 775-791).

### font (LumaFont.cpp 381: read completely; no header of its own, no entry a mod calls)

- Every distinct name asked for is kept for the process, a missing one included, in a list searched one by one under
  a spin lock (LumaFont.cpp:199-210, 347-380). Callers: draw.font (names at most 190 bytes, LumaDraw.cpp:1035-1037,
  1077) and LayoutHolder.cpp:45 (font names in layouts). A name of 200 bytes or more from a layout is cut to 199 when
  kept (365) and then never matches itself (355), so each such lookup adds another entry and another
  Content.MissingFont line. Verified in code; reached only by a mod layout naming a font that long. Not given an ID.
- The lock is a spin while load() reads files (353-376); a second thread asking at that time spins. Which threads call
  LayoutHolder's lookup was not read.
- The Content.MissingFont entry is filed under the id taken from the name ("Luma/<id>/"), not the calling mod
  (213-242), so a mod asking for another mod's missing font fills the other mod's log.
- Checked and found correct: the kerning table is read through Table::getValue (bounds-checked columns by name), each
  pair checked and sorted, at most 65536 (275-305); a fallback glyph is scaled to this font's height and baseline
  (116-131); a font that does not parse is freed (319-324).

### text tags (luma_text_tags.h 238: read completely)

Checked and found correct: the encoder never writes past 3 + 124 units (128, 136, 163) and its length byte stays under
256 (188); "[[" is one '['; numbers above 0xFFFF and an empty "0x" are refused (66-92); the decoder's row match is the
encoder's inverse for every argument kind (196-232).

### ui and time

Read completely in round 1 (see the correction at the head of Round 6); no new reading. Their round 1 to 3 findings
stand.

### Item 1 summary

New: M-087 (anim's shared pools and silent END drop; the same shared pools in content.fx and content.model), M-088
(table cells read unchecked, suspected crash), M-089 (content.model reuse keeps colours). M-029 is complete for anim,
movie, content.fx and content.model as round 3 wrote it; font has no mod entry. No service of item 1 costs more than
one or two loads a frame with no mod.

## Item 2: system, system.clipboard, system.links, capture, mods, services host

### system, clipboard, links (luma_system.cpp 1175, luma_system_win32.cpp 602, luma_system_apple.mm 335,
### luma_system_sdl.cpp 561: read completely; lines 60-334 of the SDL file were read in round 3)

### M-090 A picture another program put on the clipboard is decoded by stb_image inside the game
- Severity: crash, suspected (stb_image is not made for hostile files; a fault in host code ends the game, M-011).
- One sentence: system.clipboard.get_image hands the clipboard's PNG (Windows, macOS, Linux) and also its BMP and JPEG
  (Linux, SDL) to stb_image on a worker thread, so a crafted picture copied from a web page or a chat reaches a decoder
  that the game process runs unguarded.
- Evidence: luma_system.cpp:37-48 (stb_image with PNG, BMP, JPEG), 755-758 (decode in the platform's answer), 1135-1166
  (decode: stbi_info, size checks, stbi_load_from_memory); luma_system_win32.cpp:412-426 (the "PNG" format passed as
  is); luma_system_apple.mm:190-204 (macOS PNG as is; a TIFF is re-encoded by AppKit first, 205-216);
  luma_system_sdl.cpp:388-419 (image/png, image/bmp, image/jpeg as is). Windows' DIB is read by the game's own checked
  reader (luma_system_win32.cpp:306-397).
- Why: a player with a mod that reads the clipboard's picture (a sign editor, a paint mod) copies a crafted PNG; the
  next get_image decodes it. The header checks (size, sides) run first, so only bugs inside stb's decode are reachable.
- Confidence: suspected, needs a run with known-bad PNG and JPEG files from stb's past reports placed on the clipboard,
  under a sanitizer build of the host.

### M-091 system.display is never filled on iOS, and the comment that says it will be is wrong
- Severity: missing feature (iOS) and docs wrong.
- One sentence: apple_window_changed exists only for macOS and no iOS host in this tree calls display_changed, so on
  iOS every display field is 0 and "in front" is assumed; luma_system.cpp says iOS fills it "until its host window
  calls apple_window_changed", which the iOS build cannot do.
- Evidence: luma_system_apple.mm:295-333 (`#if !TARGET_OS_IOS`); luma_system.cpp:284-293; callers of display_changed
  are only host_window_apple.mm:762, 1258 (AppKit), host_window_sdl.cpp:1418, 1751 and luma_system_win32.cpp:599;
  platform/apple holds no iOS host (only the macOS host_window_apple.mm is built, metal.cmake:97-113).
- Why: any mod on an iOS build reading system.display for layout gets width 0, height 0, scale 0.
- Confidence: verified in code (for this tree, which has no iOS host).

Also here, not given IDs:
- system.facts is "any thread" (luma_system.cpp:549) and reads galaxy::mods::mods_game_id(), played_language() and
  language_folder() (565-572); whether those are safe off the game thread was not read (runtime/src/mods.cpp, Area 9).
- Links: http and https only, no userinfo, ASCII hosts, port at most 5 digits (160-254); one open link a mod and one
  link a mod in 10 seconds (968); every mod has its own spacing, so two mods may open two tabs at once (by design).
- A request the platform never answers (an OS clipboard that hangs) stays counted against the mod's 4 until it is
  cancelled (cancel frees the count, 919-927); Windows gives up after 250 ms (luma_system_win32.cpp:148-156); SDL and
  Apple wait on the main thread with no time limit.

M-029 for system is complete as round 3 lists it (547, 593, 657, 697, 722, 779, 801, 849, 945). Shared state: one
record a mod shared by the three providers (999-1011, by design); the deterministic clipboard is one for the process,
as the system's is. Thread and lifetime: answers come on a platform thread and post under g_lock; a mod gone first
frees the request there (459-484); cancel marks and the answer frees (906-937). No-mod cost: none (no frame hook; the
workers start at the first request: luma_system_win32.cpp:128-146, luma_system_sdl.cpp header lines 5-6). Platforms:
Windows (worker with a message-only window), macOS (main queue plus a utility queue), Linux and Android (SDL main
thread plus a worker; Android pictures through ClipboardBridge.java), iOS branch present but no host (M-091). Tests:
h/tests/system 2221-2234.

Checked and found correct (system): UTF-8 checked both ways and no NUL (111-158, 681-684, 814); picture sides, bytes and
the buffer's own length checked before a copy (861-871); take_text and take_image only inside the event's own call and
once (697-799); the Windows DIB reader checks header size, masks, colour table and stride against the block (306-397);
the seqlock display read and single writer (256-282, 1085-1104).

### capture: audio.microphone, capture.camera (luma_capture.cpp 1080, luma_capture.h 103: read completely)

### M-092 Device threads are detached and may still run while the process tears down its statics
- Severity: crash, suspected (at quit or at a mod's stop just before quit).
- One sentence: close and a mod's stop only set `stop` and return; the microphone and camera threads are detached,
  wait up to 100 ms (WASAPI, AAudio) or block inside Media Foundation's ReadSample with no time limit, and then lock
  g_lock, so a quit soon after leaves a thread that can touch the destroyed g_lock and g_kernel.
- Evidence: luma_capture.cpp:5-6 (no call waits), 912 (detach), 305-306 and 569-570 (100 ms waits), 419-424 (ReadSample,
  synchronous, no timeout), 236-249 and 456-465 (the threads take g_lock), 350-353 and 474-477 (and again at the end),
  108 (g_lock is a namespace-scope std::mutex, destroyed at exit), 1032-1041 (mod_gone only marks).
- Why: a mod with the camera open when the player quits from the pause menu; the camera stops giving frames (a USB
  camera unplugged, or one that pauses when the screen sleeps) so ReadSample does not return before exit.
- Confidence: suspected, needs a run on Windows quitting with a camera open (and once with the camera unplugged), to see
  whether the crash watcher records a fault after the window closes.

Also here, not given IDs:
- One camera for all mods: a second mod's open starts its own thread, which finds the device busy (Windows:
  ActivateObject fails, 386-387; Android: openCamera "in use", 670-671) and ends with ENDED LUMA_E_ABSENT, which the
  header gives as "none, or the player's system refused it" (luma_capture.h:46-48). The limit of one is per mod
  (888-896), not per device. Wrong behaviour for the second mod; documented nowhere.
- FRAME's time_ns is "when the camera took it" (luma_capture.h:83) but the code stamps the time the conversion ended
  (454-455, 747-748); Media Foundation's sample time (`when`, 422) and AImage's timestamp are not used. Docs wrong.
- Platforms: Windows (WASAPI, Media Foundation) and Android (AAudio, Camera2) only; Linux, macOS and iOS answer
  LUMA_E_ABSENT at open (802-806, 898-900), as the file's head says (8-11) and the header allows (luma_capture.h:7-8).
- A camera that stops giving frames without an error (Windows) never sends ENDED: ReadSample blocks (419-424).

M-029 for capture is complete as round 3 lists it (940 desc, out; 985 desc, out; close takes no pointer). Thread and
lifetime: items are shared_ptr held by their thread, kept in g_items while posted or being delivered (167-175,
810-876); a close or stop inside the mod's own event call is safe (g_delivering, 821, 874). Shared state: one device of
each kind for all mods (above). No-mod cost: none (threads start at open). Tests: h/tests/audio 2606-capture-mic,
2607-capture-camera.

Checked and found correct (capture): the microphone's resampler and its 8-block ring, with BUSY after a loss
(188-251, 847-858); the Android camera's plane lengths checked against every read before the YUV conversion (718-731);
the Windows camera's stride and buffer length checked (441-443); the Android permission asked on the device's thread,
its answer held by its own reference so a stop before the answer is safe (507-535).

### mods, mods.ids, mods.install (runtime/src/luma_mods.cpp 1748: read completely)

### M-093 A mod whose id has a '.' makes the whole mods.ids record unreadable at the next start, and its data can never be forgotten
- Severity: wrong behaviour (every mod's moved numbers lost; a removed mod's data kept for good).
- One sentence: mods.ids.get stores "<id>/<name>" for the calling mod without checking the id, but the record's reader,
  ids.of, kept and forget accept only ids of a-z, 0-9 and '-', so one call from a dotted mod writes a key that makes the
  next start move the record aside as unreadable, and the Mods page's FORGET never lists or clears that mod's saves,
  chunks or data folder.
- Evidence: luma_mods.cpp:259-264 (valid_mod_id: no '.'; luma.cpp:259-269 allows it, the comment at 43 says it is
  luma.cpp's rule), 558-585 (ids_get: key from kernel->mod_id, no valid_key), 434-449 (written as is), 418 (the reader
  refuses any key that fails valid_key), 529-537 (unreadable: renamed to .bad, a new record, Mods.IdsUnreadable), 577-582
  (numbers that collided were moved; those moves are what the record keeps), 613-614 (ids.of refuses a dotted mod_id),
  1597-1620 (kept: dotted ids skipped), 1668-1681 (forget: save, chunk and fs forget skipped for a dotted id).
- Why: a mod "speedrun.timer" calls mods.ids.get("message", "title") once. At the next start every mod that had a moved
  number (a collision) gets the derived one again, so two mods' message numbers can swap meaning; and when the player
  removes "speedrun.timer" its kept values never show on the Mods page's FORGET list.
- Confidence: verified in code (the chain of checks); a run would show it with a dotted test mod calling
  mods.ids.get, a restart, and the .bad file. No conformance test uses a dotted id.

Also here, not given IDs:
- The record's writer thread is detached and waits on a condition variable of a function-local static for the rest of
  the process (459-497, 306-309); at exit the static is destroyed under a waiting thread (the M-092 family). flush_ids
  waits for the last write only when called (1730-1741).
- kit_names and kit_read walk and read the other mod's folder on the game thread (1265-1279, 1330-1350); files are capped
  at LUMA_MODS_KIT_FILE_MOST and cached per mod until its stop (1324, 1414-1419).
- mods.install answers NOT_FOUND for a dotted id through fs (730-732, M-083).

M-029 for mods is complete as round 3 lists it (13 entries: at, find, option, watch, kit_names, kit_read, needing,
of_mod, ids get and of, install request; count, cancel and the watch clear take no pointer but `mod`). Shared state: the
ids record is everyone's (by design); one install request for all mods (a second is BUSY, by design, 725-727); the
session list is made once (158-184) and follows take-out and reload (119-134, 1403-1431). Thread: options_applied
comes from the Mods page on any thread and posts under g_options_lock (1532-1572). No-mod cost: none (no frame hook).
Platforms: all; install's screen answers ABSENT where the host has no menu (770-772). Tests: h/tests/mods 2241-2291b.

Checked and found correct (mods): the record's strict reader (number range, no duplicate key or number, exact form)
(316-432); derived numbers the same everywhere and collisions probed in order (272-287, 575-585); kit reads allowed only
for the mod itself or a mod that needs it, the number compared with the kernel's records and never read through
(1222-1242, 1444-1471); a watcher's node reused only when not queued (1097-1103); an install answer finds its request
by handle so a cancelled or gone one takes none (750-766, 778-789); take_over_ids keeps the new id's own numbers
(1685-1724).

### M-094 mods.option for a "key" option always gives the manifest's default, never the player's binding
- Severity: missing feature and docs wrong.
- One sentence: the header says a key option reads "the control's name as input's store has it (the manifest's
  default until the player binds another)", but the code returns the default in every case.
- Evidence: luma_mods.h:69-70; luma_mods.cpp:878-880 ("input's store: its binding_name once it exists").
- Why: a mod with a "key" option whose player rebinds it on the Mods page; the mod keeps reading the default.
- Confidence: verified in code.

Also: luma_mods.cpp:2-4 still says mods.install has "no fs yet, so LUMA_E_ABSENT after its checks"; the code uses fs
(728-732). Docs wrong in a comment; not given an ID.

### Headers of item 2 (read completely)

luma_mods.h 174, luma_system.h 148, luma_capture.h 103, luma_host.h 87, luma_services.h 86, luma_services_facts.h 57,
luma_refs.h 34, luma_harness.h 42. luma_service_provider.h (138) and runtime/src/luma_services_host.cpp (193) were read
completely in round 1 and not again. Promises compared with the code above; the ones not kept are M-091 (display on iOS,
luma_system.h:58 "1 at the first snapshot"), M-093 (luma_mods.h:141-143 "the same in every session"), M-094, the capture
time stamp, and luma_services.h:8 "No call blocks" (M-082 for audio; luma_mods.cpp kit_names and kit_read read files on
the game thread inside the call, 1265-1350). luma_host.h, luma_services_facts.h, luma_refs.h and luma_harness.h are host
or harness contracts with no mod entry; nothing in them was found contradicted by the files read this round.

### Item 2 summary

New: M-090 (stb decode of clipboard pictures, suspected crash), M-091 (iOS display), M-092 (detached device threads
at exit, suspected crash), M-093 (dotted id poisons mods.ids; FORGET skips it), M-094 (key option). M-029 is complete
for system, capture and mods as round 3 wrote it. No service of item 2 has a frame hook; none costs anything with no mod.

## Item 3: net.local and net.internet

Read completely this round: luma_net.h (332), luma_net_reach.cpp (201), and the luma_net.cpp ranges not read in rounds
2 and 3: 1-245, 706-1106, 1557-1595, 1935-2005, 2245-3237, 3947-4475, 4691-5169. With rounds 2 and 3 (246-705,
1107-1556, 1596-1934, 2006-2244, 3238-3946, 4476-4690) every line of luma_net.cpp (5169) has now been read.

### Limits a mod or a remote peer can push against

| Limit | Value | Where | Per | What happens past it |
|---|---|---|---|---|
| Sockets and listeners | 16 | luma_net.h:33; make 3019, internet_connect 4661, make_pipe 4288 | mod, both services | LIMIT, reported; an accepted connection past it is closed at once (2412, 899) |
| Bytes queued to send | 4 MiB | luma_net.h:34; room_locked 3440-3452 | mod | LIMIT, reported; a closed socket's bytes count for up to 10 s |
| Bytes received, not taken | 1 MiB | luma_net.h:35; 2486, 1002, 1535, 2291 | socket | reading pauses (TCP back-pressure) |
| Datagrams waiting | 4096 and 1 MiB | 265, 1120-1121, 2649-2650 | socket | reading pauses; the system drops the rest |
| TLS bytes in, not given to TLS | 256 KiB | 344 | socket | reading pauses |
| HTTP answer: raw, head, body out | 256 KiB, 64 KiB, 1 MiB | 346-348 | request | reading or parsing pauses; a head past 64 KiB is FORMAT (round 2) |
| HTTP body | 64 MiB | luma_net.h:39 | request | ERROR TOO_LARGE |
| HTTP request body | 4 MiB | 350, 4805-4808 | request | LIMIT, reported (not in the header's list of http's mistakes) |
| Requests open | 8 | luma_net.h:38; 4827-4836 | mod | LIMIT, reported |
| Request time | 30 s default, 600 s most | luma_net.h:43-44; 4737, 4822-4823 | request | ERROR TIMEOUT |
| Connect time | 10 s, the lookup included | 248, 1251, 4654 | socket | ERROR TIMEOUT |
| Close time | 10 s | 251, 4124 | socket | the queued bytes are dropped |
| WebSocket message | 16 MiB in; 4 MiB less 14 bytes out in practice | luma_net.h:45; 3815-3829; 4079-4086 | message | M-096 |
| Name lookups | none | 4672, 2211 | process | M-095 |
| Pongs | none | 3853-3858 | socket | M-055 (round 3) |
| Peers refused | counted, one log line | 750-755, 2407-2410 | socket | a refused peer is closed at once |

A local-network peer can hold a listener's 16 slots for as long as it likes: an open plain connection has no idle time
(2484-2553 has no timer for an open socket). That is the mod's own listener on its own machine or network; noted, not
a finding.

### What a refused permission or a refused host does
- Permission not granted: the loader does not hand the mod the service table; the import fails with
  Hook.ServiceNotGranted (LumaLoader.cpp:444, 1060; luma.h:224). luma_net.cpp never asks `granted` itself (grep), so any
  code holding the table is treated as granted.
- net.internet, a host luma.json does not name: LUMA_E_DENIED at the call, reported Net.HostNotNamed (4632-4635); no
  socket is made and none is counted. A name whose every address is local: ERROR DENIED after the lookup (4588). A
  redirect to a host not named: ERROR DENIED with Net.RedirectRefused (2160; rounds 2 and 3).
- net.local, a host outside this machine and the local network: LUMA_E_ARGUMENT, reported (3133-3135, 4004-4006,
  4775-4777, 4171-4173). The literal address is judged; "localhost" is 127.0.0.1 only (215-217). An address of this
  machine's own interfaces counts as local even when public (luma_net_reach.cpp:192-199; the list is read again at most
  once a second, 178-189).
- A listener or udp_open socket on the local network: a peer outside it is closed or dropped with no event, and the first
  gives one Net.PeerRefused line (2407-2410, 750-755).
- net.internet's server calls (tcp_listen, udp_open, send_to, ipc_connect, ipc_listen, tls_listen): LUMA_E_DENIED, not
  reported (4701-4707, 4867-4903), as the header says.

### Platform differences
- Windows: endpoints are named pipes (856-1100, 4272-4310) with PIPE_REJECT_REMOTE_CLIENTS and the default pipe
  security (863, null security attributes); the connect side asks SECURITY_IDENTIFICATION (875-877). A loopback TCP
  connect to a closed port fails at once (refuse_fast 108-123); elsewhere the system does this already. UDP ports are
  taken with SO_EXCLUSIVEADDRUSE (3209-3213).
- Linux and Android: endpoints are abstract Unix names (4318-4323): no file and no permission check on Linux. On Android
  the system's app separation decides whether another app may connect (EACCES or EPERM is DENIED, 792); not checked here.
- Apple: endpoints are socket files in TMPDIR (4324-4337); ipc_listen first probes the file with a blocking connect on
  the game thread (4444-4455) and removes a stale one. SIGPIPE is held off by SO_NOSIGPIPE (175-178); Linux and Android
  by MSG_NOSIGNAL (182-183).
- IPv6 link-local addresses are accepted by the rule (luma_net_reach.cpp:107) but cannot connect (M-099).
- udp_open and send_to are IPv4 only (3206, 4171), as the header says.
- No iOS host exists in the tree (M-091); the Apple paths are compile-only there.

### M-095 wrong behaviour: name lookups have no limit; a mod or a slow DNS server can start threads without end
- Evidence: internet_connect starts a detached thread per lookup (luma_net.cpp:4672; a redirect's at 2211). close_on
  takes the socket out of the mod's count at once (4122; uncount_locked 758-762), while the Socket stays until its
  lookup thread ends (reap_locked 769, `!socket->resolving`). The 10 s connect time ends the socket's wait (2330-2334)
  but not getaddrinfo (4567), which has no time of its own.
- Why: a mod that calls tcp_connect then close on a named host every frame, or 16 connects that time out every 10 s
  while the DNS server does not answer: each starts a thread that lives as long as the system's lookup. Threads and
  their stacks grow with no stated bound. A failed thread start is caught (4673-4678) and ends as LUMA_E_IO, not a
  crash.
- Confidence: verified in code (no count of lookups exists); the thread count reached needs a run against a DNS server
  that does not answer.

### M-096 wrong behaviour and docs wrong: ws_send cannot send a message above 4 MiB, though the header gives 16 MiB "either way"; http's 4 MiB body limit is not in its header
- Evidence: luma_net.h:45 ("bytes of one WebSocket message, either way") and 243-247 (ws_send: past
  LUMA_NET_MESSAGE_MOST TOO_LARGE; "the send queue full" LIMIT). ws_send_on checks room_locked(bytes + 14)
  (luma_net.cpp:4086), and room_locked refuses any single call above LUMA_NET_SEND_QUEUED_MOST, 4 MiB (3447). http_on
  refuses a body above 4 MiB with LIMIT, reported (4805-4808); luma_net.h:221-228 lists http's mistakes without it.
- Why: a mod sends one 5 MiB binary message on an empty queue: LUMA_E_LIMIT and a Service.Limit report on every try,
  though the header allows up to 16 MiB and gives LIMIT only for a full queue.
- Confidence: verified in code.

### M-097 wrong behaviour: a mod's own endpoint may be up to 100 bytes, but ipc_connect accepts only 64, so another mod cannot reach it; Apple's socket path can be too long
- Evidence: ipc_listen allows "nebula.<id>.<name>" up to 100 bytes (luma_net.cpp:4416-4419); ipc_connect allows a
  name of at most LUMA_NET_ENDPOINT_MOST, 64 (4349; luma_net.h:37, 188-191). Mod ids run to 64 bytes (luma.cpp:256-260).
  On Apple the path is TMPDIR + endpoint + ".sock" and must fit sun_path (104 bytes) or the call answers LUMA_E_IO
  (4325-4334, 4441-4443), an answer the header does not list.
- Why: a mod with a 50-byte id listens on "telemetry-feed" (72 bytes): the listen succeeds, and every other Nebula mod's
  ipc_connect to that name is ARGUMENT. On iOS, whose TMPDIR is about 89 bytes, the endpoint would have to be under
  about 10 bytes, which "nebula.<id>.<name>" never is, so every ipc_listen fails.
- Confidence: 64 against 100 is verified in code; the iOS path length is suspected, needs a run on an iOS device (no
  iOS host is in the tree).

### M-098 crash at exit (suspected): the net I/O thread and the lookup threads are detached and use static objects that exit destroys
- Evidence: std::thread(io_loop).detach() (luma_net.cpp:715) and std::thread(look_up, ...).detach() (4672, 2211). Both
  lock g_lock and walk g_sockets (559-560, 2616-2619, 4594-4600), objects of static duration with no end step; g_io is
  never deleted, and the providers have no shutdown hook (5123-5159).
- Why: the game exits while a socket is open or a lookup runs: static destructors free g_sockets' storage while the I/O
  thread's wait predicate or a lookup's end walks or locks it. The same pattern as M-092 (capture).
- Confidence: suspected, needs a run (exit with a lookup pending against a DNS server that does not answer, under a heap
  checker).

### M-099 missing feature: IPv6 link-local addresses pass the local rule but cannot connect
- Evidence: luma_net.h:127 lists fe80::/10; local_v6 accepts it (luma_net_reach.cpp:107). local_address parses the text
  with inet_pton (luma_net.cpp:224), which takes no "%scope", and never sets sin6_scope_id (238-241).
- Why: tcp_connect to "fe80::..." has no interface to send by; on Linux connect fails with EINVAL, given as ERROR
  LUMA_E_IO (connect_status 781-783), not UNREACHABLE.
- Confidence: the missing scope is verified in code; the error each system returns needs a run with a link-local peer.

### Per-service fields for net
- Header promises not kept: M-096, M-097, M-099. luma_services.h:8 "No call blocks": Apple's ipc_listen probe connects
  on the game thread (4444-4455), and ws_send checks UTF-8 and builds a frame of up to 4 MiB while holding g_lock, which
  the I/O thread waits on (4070-4094). Deterministic mode's frame_start waits up to 1 s (2750-2781), in the test harness
  only (LumaServices.cpp:627-629).
- Crash on bad argument: M-029's net list (round 3) is complete; the calls read this round match it (take_datagram copies
  into the mod's buf under g_lock, 4235; address copies into buf, 5087).
- Thread and lifetime: M-095, M-098. mod_gone sets every socket's owner null and closing under g_lock (4997-5017), so
  the I/O thread posts nothing more (post_locked 739; turn_locked 2313-2315); sockets still resolving stay in g_sockets
  until their lookup ends and a later reap. Found correct: delivery copies the state under the lock and calls the mod
  outside it, checking `closing` again before each event (2811-2953).
- Missing per platform: the platform list above; ipc on iOS (M-097).
- Shared across mods: one I/O thread does every mod's TLS handshakes and gzip in turn (2725-2731); the client TLS context
  is shared (g_client_tried 1259) but each socket checks against its own mod's roots (366, 3092). Every limit is per mod
  or per socket; none is shared across mods.
- Per-frame cost with no mod using net: none. The I/O thread starts at the first socket (io_started 700-723) and waits on
  a condition while no socket is open (2617-2619); frame_start runs only in deterministic mode. While any socket is open
  the I/O thread wakes every 10 ms (252, 2675).
- Tests: h/tests/net 2701-2710.

### Item 3 summary
New: M-095 (lookup threads without limit), M-096 (ws_send 4 MiB; http body limit not in the header), M-097 (endpoint
lengths, Apple path), M-098 (detached threads at exit, suspected), M-099 (link-local IPv6). With rounds 2 and 3, M-055
(pongs) stays the one finding a remote peer can drive without the mod's help. The Area 2 closing table is finished in
the Round 5 section.

## Area 9 (round 6 start): the safe-start notice

The question: j/safestart 6601 and 6602 expect the fact modscreen.nebula with the label "SAFE START: EVERY MOD IS OFF
UNTIL THE NEXT START" or "NEBULA CLOSED UNEXPECTEDLY WITH MODS ON. EVERY MOD IS OFF UNTIL THE NEXT START", and get null,
while the log prints "safe start: the last run crashed with mods on".

Traced: the safe start itself works. host_main.cpp:6092-6096 takes it (mods.cpp:1075-1091: an empty mods/.safe_start
is kAfterCrash, then the file is removed) and logs the line; native_settings.cpp reads the same value through
galaxy::mods::safe_start() (mods.cpp:1093-1095). The exe links mods.cpp once (CMakeLists.txt:207-241 galaxy_native_host
and 450-458 galaxy_render both go into the one Nebula executable, 590), so there is no second copy of s_safe_start.

### M-100 docs wrong (tests and capability text stale): the safe-start notice moved from the Mods list to the LOG page; 6601 and 6602 still look for it on the list
- Evidence: the notice row and its fact are made only in log_page, with page "log" (native_settings.cpp:3512-3521),
  and log_page runs only while the LOG page is open (4519-4521; open_mod_log_locked 3612-3622). The list page shows the
  heading "ENABLED (OFF UNTIL THE NEXT START)" (4539-4544) and writes facts only for rows whose key starts "mod:", never
  for a heading (4548-4552). The tests open only the Mods tab (`"settings": "mods"`) and expect `"page": "list"`,
  `"line": 0` with the old label (audit-conf j/tests/safestart/6601-asked/test.json, 6602-crash/test.json).
  tools/lumacore/capabilities.json:104-105 still says "the Mods tab says so" and "the Mods tab says why". The move came
  with commit 0b1982c6f (git log -S on the heading); native_settings.cpp also has uncommitted changes.
- Why: every run of 6601 and 6602: the list never has a modscreen.nebula fact, so both fail with the fact null. 6603
  (`"absent": true`) passes whether or not the notice works, so it proves nothing about the notice.
- Also: the player is told why only one press away (LOG (1), mod_log_word_locked 3603-3607); after a crash nothing on
  the list or at the start says the last run crashed (host_main.cpp:6094-6096 writes only the log file). The notice is
  translated (localization/NebulaText.tsv:939, 940; the heading at 480).
- Confidence: verified in code; the fix is in the tests (open LOG, expect page "log") or in the code (a list row with a
  fact), the owner's choice.

### Area 9 files read completely this round: mod_screens.cpp (79), mod_check.cpp (318)
- mod_screens.cpp: with no Mods page hooks set, ask_install and ask_offer answer LUMA_E_ABSENT (36, 47), but
  install_ask_allowed answers true (41): a host without the page lets a mod ask and then answers ABSENT, which is
  consistent. post_screen_answer's push_back is not in a try (56); a throw there leaves the screen's answer unrun on a
  thread that is not the game's. Noted, no ID (memory exhaustion only).
- mod_check.cpp: validator() keeps its "looked" and "ok" flags as plain statics (34-38). Two threads that call it
  first at once could see looked true before `found` is filled. Whether two threads call it was not traced (mods.cpp not
  read); noted, suspected.
- mod_check.cpp: a missing importer library or a validator that refuses the game's self-description is said only on
  stderr (54, 70, 224-225, 239-240), and the second case checks the mod again without the names, types, services and
  placement rules (226-235). On Android and in the Apple app stderr reaches no file the player or a report sees; which
  rules went unchecked is not shown on the Mods page. ImportCheck.ran stays false when the library is missing (273); what
  the import does then is mod_import.cpp's (round 1).
- Found correct: findings are read in English and in the played language, the played text falling back to English
  (252-261); others' kept reports are read from mods/.cooked/validate/<id>.kept, and a package with none is checked
  alone, off when it has an Error or names no id (282-294).

### Area 9: not done this round
Not read: runtime/src/mods.cpp (2584; only 1055-1099 this round), mod_xml.cpp (320), mod_log.cpp (1020), luma.cpp
(1464; only 255-272), native_settings.cpp 2759-4815 (only 3500-3634 and 4515-4574 this round). mod_plugins.cpp was read
completely in round 1. The traces from "player adds a .luma" to "the mod runs" per platform, load order, needs and
conflicts, update, removal, the failed mod's row, and translated messages per failure are not started.

# Round 7

Scope: close Area 9 (item 1), Area 1 leftovers (item 2), Area 4 (item 3), Area 3 (item 4). Files are read one at a time;
each file's notes follow its reading. The coordinator's run result (LumaScript lane 21 of 21, 26 mods build) is taken as
given and not re-checked. Paths are relative to Nebula-Clean.

## Item 1: Area 9

### runtime/src/mods.cpp (2584 lines, read completely)

What it does, in order (the trace below uses these):
- scan_mods (1168-1560): every folder in the mods folder not starting with '.' is a mod; its id is the folder's name
  (1203). Per mod: layout_of (695-748: luma.json, else riivolution/, files/, disc folders, textures/, movies/, loose
  files), mods.ini answers (read_saved 850-898), Riivolution options (1236-1299), the .luma manifest (1300-1489), the
  problem text (1305-1340, 1497-1519), then sorting (1524-1548) and settle_mods (1600-1676).
- New mods go on top (1235: a saved place is +1000000); a new .luma's load_after / load_before rotate it once
  (1533-1548). Disabled mods follow by name.
- Needs, conflicts and version ranges: mark_needs (1562-1598) exists, but settle_mods says these are the validator's
  (1608-1611, "L800 to L802"); settle_mods itself checks only one family of saves (1612-1624).
- plan_mod_changes (2011-2440): bottom of the list up, the top mod's file wins (2406); plugins in top-down order
  (2063-2082); libraries load only when a mod that runs needs them (1965-1992, 2032-2035); a .luma with a problem
  adds only a note (2039-2042).
- plan_signature (2473-2518): file size and write time of every planned file; mods_differ_from_session (2526-2530)
  compares mods_signature (837-848).

### M-101 wrong behaviour: the Mods list's overlap count is quadratic in mods and in files, recomputed on every change of the list
- Evidence: settle_mods (mods.cpp:1632-1675) runs for every ordered pair of mods in use (i, j), and for each path of
  mods[i] not found exactly in mods[j] it walks every path of mods[j] (1649-1654). settle_mods runs at each scan
  (1558) and "whenever the player changes it" (1549).
- Why: 50 file mods of 2,000 files each (a Riivolution mod with a StageData folder patch is that size,
  list_files 478-487) make 2,450 pairs times 4,000,000 comparisons, about 10^10 steps. Each one is a starts_with test,
  plus a string built when either side is name-only. That cost is paid on every toggle or move in the list.
- Confidence: the loop shape is verified in code; how long it takes needs a run with such mods (which thread runs
  settle_mods is in native_settings.cpp, below).

### M-102 WITHDRAWN after reading native_settings.cpp: texture-only and movie-only mods apply live (apply_live_mods, native_settings.cpp:2666-2670, called by save_mods 2695 and reread_mods_locked 2737), so leaving them out of the restart signature is correct. Kept below as first written.
### (withdrawn) M-102: turning on or off a texture-only or movie-only mod does not count as a change needing a restart
- Evidence: mods_signature skips every mod with `!changes_game_files(*m)` (mods.cpp:843). changes_game_files is
  riivolution, files, loose or luma_changes_game (1789-1791). A .luma with code for this device does count:
  scan_mods ORs in manifest_sets_game (1474), which is true when the plugin has this target (luma.cpp:1388-1393). A
  texture pack (folder or .luma with only textures/) and a movies-only mod do not count. mods_differ_from_session
  compares only that signature (2526-2530).
- Why: the player turns a texture pack or a movie mod on or off during a session; mods_differ_from_session answers
  false. If textures and movies do not reload live, the change silently waits for a restart nobody asks for (see the
  native_settings.cpp notes, which settle it).
- Confidence: the signature is verified in code; the effect depends on whether the page reloads textures live, checked
  below.

### Other notes on mods.cpp
- Every scan reads more than the list needs:
  - plugin_token (631-645) hashes every byte of every file in each library's folder (code_hash 1002-1028, with a
    64 KiB buffer on the stack);
  - list_files walks each mod's folders recursively (478-487, 1269, 1456, 1492);
  - a .luma's merge files are read whole (1446);
  - each texture pack walks its pictures (read_pack 586-612).
  This is linear in mods and files, but runs at every scan_mods. When scan_mods runs is traced below.
- plan_mod_changes reads mods.ini from disk again for each Riivolution mod (2316). It does so through `settings().file`
  without the settings lock, which every other reader takes (1736-1739). It takes the options as last saved, not as
  shown.
- mark_needs (1562-1598) still exists with its own wording ("THIS MOD NEEDS: ...", "... A NEWER VERSION OF: ...",
  "THIS MOD CAN'T RUN WITH: ..."). The comment at 1608-1611 says the validator does this at each start; whether
  mark_needs has callers is checked below.
- "THIS MOD HAS NO VERSION FOR THIS DEVICE" looks only at the first plugin (1320-1321). A mod whose second library
  lacks this device starts without it: plan_mod_changes skips that library silently (2068-2069).
- On Android and iOS, a folder mod (Riivolution, files, loose) gets "CONVERT THIS MOD WITH LUMALINK ON A COMPUTER, THEN
  OPEN THE FILE HERE" (1508-1511), and the plan skips it (2087-2090).
- plan_signature uses size and write time (2476-2516). A replaced file with the same size and the same time keeps the
  old cook; whether the importer writes fresh times decides it (mod_import.cpp, round 1).
- Problem texts are English capitals built in code (1306-1339, 1500-1518, 1578-1590, 981-983, 1835), some with a mod's
  name or a number joined in. Whether each reaches the player translated is checked in native_settings.cpp and
  NebulaText.tsv below.
- Found correct:
  - Riivolution paths: no backslash and no ".." above the root (318-354, safe_path_part 1030-1043).
  - Hidden folders (imports in progress) are skipped (1188).
  - The blocklist is written atomically (1141-1143).
  - The plan leaves out names holding a tab or a line end, and says so (2411-2433).
- mark_needs is called by the Mods page (native_settings.cpp:2887) and by tests (mods_tests.cpp:669, 683).

### runtime/src/mod_xml.cpp (320 lines, read completely)
- Found correct:
  - nesting is capped at 256 (137);
  - entities decode to UTF-8, with bad code points becoming U+FFFD (103-107);
  - UTF-16 and Latin-1 patch files are converted (285-311);
  - an unterminated comment or declaration ends the text instead of looping (52-56).
- No finding.

### runtime/src/mod_log.cpp (1020 lines, read completely)

#### M-103 wrong behaviour: the start's cross-check validates every package again whenever any one package changes, and rounds can repeat it once per refused package
- Evidence: in pass 2 (mod_log.cpp:719-776), each package's key mixes in every other package's content hash, on/off
  state and id, plus its own position (731-751). A key not among its last 4 verdicts (kVerdicts 543) runs the validator
  with every other package's kept report (759). The rounds repeat while any package is newly refused, up to
  placed.size() + 1 rounds (724).
- Why: the player adds, updates, or turns on or off one .luma among N. Every one of the N keys changes, so the next
  start runs N validator calls, each reading N-1 kept reports, which is N squared report reads. A refusal that takes
  others with it adds a round each time. With 256 mods that is about 65,000 kept-report reads on the start thread
  before the game starts. Pass 1 also walks and stats every file of every package at each start (files_stamp 440-456).
- Confidence: the shape is verified in code; the time needs a run with many .luma mods.

#### Other notes on mod_log.cpp
- add_finding and add_not_started can hand the module entry.count = LUMA_MODLOG_ARGS + 1 with only ARGS names filled
  (151-153, 179-181, on purpose: "the log says once that the rest are not shown"). The module side must not index past
  ARGS; this is checked under item 2 (LumaModLog.cpp).
- words() and row_words() build the string from the byte count the module returns, `std::string(text, bytes)`, with a
  512- and 1024-byte buffer (316-318, 333-335). If the module returns the length it would need rather than what it
  wrote, a long row reads past the buffer. Checked under item 2.
- write_file (521-533) is not the atomic writer the rest uses (it renames over the target, but writes with no flush to
  disk); a crash mid-write leaves a .new beside the file, and read_check treats a malformed file as "validate again"
  (564). Not a fault.
- refused_sentence (1016-1018) is "THIS MOD HAS ERRORS. ITS LOG SAYS WHY", the Mods page's row for a refused .luma
  (translation checked below).
- Found correct:
  - an L506 refusal is worded "off: you declined" when the player declined the permission, and as a Nebula fault when
    the store says granted (251-268);
  - the kept-report cache key covers the Nebula version, region, platform, played language, LUMA_API, store key and
    self-description key (644-646).

### runtime/src/luma.cpp (1464 lines, read completely)
- version_in_range treats a range it cannot read as met (1223-1232: `first == 2 ? 0 : first`). For example, a need
  written "latest" or "1.2 - 1.4" is accepted by mark_needs (mods.cpp:1579). It is noted, not given an ID, because the
  start's validator owns needs (mod_log.cpp check_packages; validator rules L800-L802, not read). Whether the
  validator refuses an unreadable range is open (Area 8).
- `^0.0.3` allows up to 0.1 (1213-1214), where semver allows only 0.0.3. The docs' wording on ranges was not compared
  (Area 10).
- A manifest of a newer format is returned with its id and format only (999-1000): no name, no options, no code. The
  validator's L205 refuses it at the start. mods.cpp then shows it as a mod with no problem until the start check
  marks it (mods.cpp:1305 checks only "not read").
- Found correct:
  - the JSON reader caps depth at 32, refuses control bytes and lone surrogates, and gives no value rather than
    crashing (54-248);
  - paths in a manifest must stay inside the mod (356-368), and libraries may not have a folder part (1070-1075);
  - number options refuse hex, infinity and NaN (952-982);
  - numbers are read with strtod and written with %g, and no product code calls setlocale (grep: only tests and
    third_party/xz), so a decimal comma locale does not change them;
  - plugin_target and code_entry name all nine platform targets (1275-1305).
- Note: dependency ids, conflict ids and load order use the manifest id (luma->id), while the Mods list and mods.ini
  use the folder name (mods.cpp:1203). The importer makes them the same (round 1); a folder renamed by hand splits them.

### native_settings.cpp 2759-4815 (read completely), plus the helpers it calls: 2580-2758, 5575-5689, 6040-6093, 6420-6464; mod_import.cpp 520-610 and 736-769; NebulaSettingsMenu.cpp 657-787 (the translator)

How the page works:
- s_mods is the draft and s_mods_saved is mods.ini as read. mods_list() scans once (2654-2662); reread_mods_locked
  (2707-2738) scans again and keeps the draft's choices.
- Changes are a draft until Apply. commit_mods_locked (2856-2869) writes mods.ini. It then opens the consent question
  for the first mod on with unanswered code, or sets s_mods_restart_ask when mods_differ_from_session.
- save_mods (2674-2696) writes mods.ini, posts OPTION events for changed options and hands texture and movie folders
  to the renderer at once (apply_live_mods 2666-2670).
- The status line says ModsRestart while on the tab and mods.ini differs from the session (6087).
- Translation: a host text is looked up whole as "S." plus the text with spaces as "_" and numbers as "#". Failing
  that, it is split at " (", ". ", ": " or ", " and each piece is looked up; a piece with no row is shown in English
  (NebulaSettingsMenu.cpp:666-780).

#### M-104 wrong behaviour: the Mods page does work that grows with the square of the number of mods, several times per frame, while the page shows
- Evidence:
  - mod_row checks every other mod for the same title (4127-4129). mod_title builds a string each call (2594-2596).
    Over the list that is N x N string builds.
  - Each row also calls mod_not_started_text (4134). declined_at_start walks plugins_not_started()
    (mod_log.cpp:243), and plugin_not_started_text does a find_if over g_not_started (mod_plugins.cpp:1048).
  - mods_page_rows also calls update_pack_categories on every mod (4494-4496); it returns at once after the first
    sort.
  - The page is built again by every settings_page call (5046). The view builds it (5878), the row count builds it
    (1057), and every key on the page builds it again (4632, 6430, 6457).
- Why: with 256 mods the twin check alone is 65,536 title builds per page build, and a frame builds the page more than
  once. With 50 mods it is 2,500.
- Confidence: the shape is verified in code. Whether the frame time shows needs a run with 256 folder mods on a
  low-end Android device.

#### M-105 wrong behaviour: on Android, a Riivolution or files mod in a .zip is reported as imported, then never runs
- Evidence:
  - The launcher's importer (ModImport.kt:67-76) and the game's own importer use mod_import.cpp. It has no
    platform check (grep "__ANDROID__" in mod_import.cpp: none).
  - The launcher says ModsReady (LauncherModel.kt:156) when ok.
  - The game's scan then gives every such mod "CONVERT THIS MOD WITH LUMALINK ON A COMPUTER, THEN OPEN THE FILE HERE"
    (mods.cpp:1508-1511).
- Why: a player on a phone picks a Riivolution .zip. They are told it is ready. The Mods page shows it with a problem.
  The archive's space is used for nothing.
- Confidence: verified in code.

#### M-106 wrong behaviour: importing an older version of an installed mod replaces the newer one, and the result says UPDATED
- Evidence:
  - import_mod only stops when the content is identical (mod_import.cpp:547-555). Otherwise it replaces the folder
    whatever the versions (565-596).
  - Its message says "OLDER VERSION: ..." (606-609). import_outcome_word gives "UPDATED" for any replace
    (native_settings.cpp:3202-3207).
  - For a mod's mods.install request, install_page_locked's comment says "The same or a newer one installed: the
    import answers LUMA_E_EXISTS" (4296-4297). The code shows INSTALL (not UPDATE) for an older file (4298-4299).
    finish_install_ask then answers LUMA_OK for "UPDATED" (3014-3015).
- Why: a mod calls mods.install with a file older than the installed copy, and the player presses INSTALL. Or the
  player imports an old download. The installed mod goes back to the older version. The request's caller is told
  LUMA_OK, where the comment and the docs' J-SM2 wording promise LUMA_E_EXISTS.
- Confidence: verified in code. Which of the comment or the code is meant is for the owner.

#### M-107 wrong behaviour: on Linux, macOS and Android, importing an update of a mod this session is using replaces its folder while the game still reads it
- Evidence:
  - REMOVE refuses a mod the session started with: "IN USE UNTIL NEBULA RESTARTS" (ask_mod_remove 5589-5593). Its
    comment says the files "are read while the game plays" (5579-5580).
  - The importer has no such check. It renames the live folder aside and puts the new one in place (mod_import.cpp
    568-596).
  - Only on Windows does an open file make the rename fail, so that the update waits in ".update-<id>" (572-585).
- Why: during play, the player imports a newer version of a files or Riivolution mod that is on. Later loads in the
  session read the new files against the plan made at the start. That plan's file sizes and order came from the old
  folder.
- Confidence: the asymmetry is verified in code. The effect needs a run on Linux: start with a files mod on, import a
  changed version during play, then enter a stage that loads a changed file.

#### M-108 docs wrong (translation): two strings on a mod's page have no translation
- Evidence:
  - The TEXT row's value "THE MOD'S <LANGUAGE>" / "THE GAME'S <LANGUAGE>" (4050) has no "S." row. The translator
    cannot split it.
  - The first half of the help, "THIS MOD HAS NO <LANGUAGE> TEXT" (4057), has none either. It shows in English beside
    the translated second half.
  - Checked with a grep of NebulaText.tsv: "S.THE_MOD'S_", "S.THE_GAME'S_#" and "S.THIS_MOD_HAS_NO_" (other than
    THIS_MOD_HAS_NO_VERSION_FOR_THIS_DEVICE) are absent.
- Why: the player plays in French and opens a mod with only English text.
- Confidence: verified in code and the table.

#### Other notes on the page
- A mod's name inside a problem ("THIS MOD NEEDS: <name>", "OVERRIDDEN BY: <name> (2 FILES)") goes through the
  translator as plain text. A name holding ". ", ": " or ", " is split, and a piece that matches a row is translated
  (for example, a mod named "Off" shows the translated word). This is cosmetic.
- reread_mods_locked keeps a drafted option's selected index whenever the count of options is unchanged (2717-2718),
  even if an update reordered them. The text is copied only when the key matches (2719). After an update, a
  Riivolution choice can land on a different option.
- After an import, no restart question opens: s_mods_restart_ask is set only by commit and consent (2846, 2866). The
  player sees the import's line "... MODS APPLY WHEN NEBULA RESTARTS" (3340) and the tab's ModsRestart status
  (6087). That is enough.
- The removal question is refused for a mod in this session (5589). A mod imported and then removed in the same
  session is not in session.active, so it is removed at once. That is correct.
- Found correct:
  - every list change finds its mod by id, not by position (3450-3453), so an import ending while the page shows
    cannot change the wrong mod;
  - the consent question runs one mod at a time, Escape is Not now, and the focus starts on Not now (2780-2852,
    4661-4665);
  - a blocked mod cannot be turned on (2902), and BLOCK turns it off first (4111);
  - a second import while one runs is refused with a message (3274-3277); removal waits for an import (5585);
  - an exception in the import thread becomes a FAILED result, not a crash (3304-3309);
  - mods.install and save offers allow one screen at a time and answer LUMA_E_BUSY otherwise (4430-4441, 4457-4461);
    an unanswered screen is a no (3001-3005);
  - a newer-version install shows UPDATE and both versions (4298-4339).

### platform/android: GameFiles.java (133 lines, read completely), LauncherModel.kt mod parts (113-189), ModImport.kt (90 lines, read completely)
- The launcher imports through the same mod_import.cpp, built as libnebula_mods.so (ModImport.kt:28-31, 67). Files go
  into the game's data folder, and the validator runs at import as on desktop. Several dropped files queue one at a
  time (LauncherModel.kt:125-149).
- The shipped mods (LumaKit) are unpacked once per app install or update (GameFiles.java:76-122). The game takes them
  with --shipped-mods (GameActivity.java:161-163), and the importer installs each only when the player does not have
  that version or a later one (mod_import.cpp:696).
- If the game process is running when the launcher imports, the game's Mods page does not see the new mod until RESCAN
  or a restart, because mods_list() scans only once (native_settings.cpp:2654-2662). Suspected; needs a run with the
  game in the background and an import from the launcher.
- Found correct:
  - a stale shipped-mods folder is cleared before unpacking, and each file is written as .part and renamed
    (GameFiles.java:89-115);
  - the copy for import is deleted in a finally block (ModImport.kt:77-79);
  - paths cross JNI as UTF-8 bytes (ModImport.kt:38-42).

### Trace (a): from "the player adds a .luma" to "the mod runs"

Windows, Linux and macOS (the steps are the same; macOS has no Mods page, see the end):
1. Ways in: IMPORT FILE or IMPORT FOLDER (the picker, 4357-4368); a file dropped on the window or opened with Nebula,
   or --import (run_mod_imports 3272); a link. A file copied into the mods folder by hand needs RESCAN or a restart
   (4210-4213).
2. run_mod_imports runs on its own thread (3279). import_mod unpacks to a staging folder and picks the folder name
   (mod_import.cpp:520-535). An identical mod gives "ALREADY IN YOUR MODS" (547-555). The validator runs with every
   other installed package (check_import_package 3254-3269; mod_log.cpp check_import). A refusal gives "NOT IMPORTED"
   with the findings on the result page (557-563).
3. The folder is moved into place. On Windows, an in-use mod's update waits in .update-<id> until the next start
   (572-585). On the others, it replaces the live folder (M-107).
4. The result page shows (3346, import_page 3561). The list is read again with the new mod on and at the top
   (reread_mods_locked 2723-2725; new mods default to on, mods.cpp:1214).
5. If the mod has code, the consent question opens when the result page is closed (4486-4492, needs no draft changes
   pending). Not now turns it off (2835).
6. Nothing is drafted, so there is no Apply. The status line says the mods need a restart (6087), and the import line
   says "MODS APPLY WHEN NEBULA RESTARTS" (3340).
7. Next start: apply_pending_updates; then check_packages (pass 1 and the cross-check, M-103). A refusal leaves the
   mod listed with "THIS MOD HAS ERRORS. ITS LOG SAYS WHY" (mark_refused 2646-2652).
8. plan_mod_changes and the plugin load follow. The loader refuses the 257th code mod with L500 Lib.TooManyMods
   (LumaLoader.cpp:1628-1631). Code that did not start shows "ON, NOT RUNNING (n OF m)" and the reason
   (3779-3793, 4133-4139).

Android:
- The same steps happen in the game's Mods page, but IMPORT FILE is the only import button (4163-4169). The launcher
  can also import (ModImport.kt), and the game does not see that until RESCAN if it is running.
- Folder, Riivolution and files mods import and then show the convert message (M-105).

macOS and iOS:
- The Mods page is one line, "THIS IS STILL A WORK IN PROGRESS. PLEASE COME BACK LATER!" (4479-4481). Its keys are
  the menu's (4625-4630).
- An --import or a dropped file still runs the importer, through run_mod_imports's callers outside this range (not
  traced). The page cannot show the result.

Places it can do nothing without saying so:
- A file copied into the mods folder by hand while the game runs: nothing until RESCAN.
- On Android, a launcher import while the game runs: nothing until RESCAN.
- A .luma with no code for this device: the scan says "THIS MOD HAS NO VERSION FOR THIS DEVICE" (mods.cpp:1322), so
  this one is said. That message checks only the first plugin (round 7 note at mods.cpp:1320).
- An unreadable version range counts as met (luma.cpp:1232). mark_needs says nothing, and only the start's validator
  can refuse it.

Places it can leave a mod half installed:
- None found in the importer. Staging is renamed into place, and a crash leaves .importing-* or -previous folders
  that apply_pending_updates puts back or removes at the next start (mod_import.cpp:771-800).

### Trace (b): load order, needs, conflicts and version ranges
- Order: the list's order, top first; the top wins an overlap (plan_mod_changes runs bottom-up). New mods go on top.
  A manifest's load_after and load_before move a new mod once at its first scan (mods.cpp, round 7 notes). A player's
  later move wins.
- Needs and conflicts, before Apply: mark_needs on the draft (2887) marks:
  - "THIS MOD NEEDS: <name>" (missing or off),
  - "THIS MOD NEEDS A NEWER VERSION OF: <name>",
  - "THIS MOD NEEDS: <name> <range>" (wrong version, older or not in the range),
  - "THIS MOD CAN'T RUN WITH: <name>" (a declared conflict, or the mod above it: mods.cpp:1578-1620).
  The row's help shows it (mod_details 3406-3407). The mod is not turned off by the page, so nothing stops Apply. The
  start's validator refuses it, and it shows as "THIS MOD HAS ERRORS. ITS LOG SAYS WHY".
- Overlaps: "OVERRIDES: <name> (n FILES, n TEXTURES, n SETTINGS)" and "OVERRIDDEN BY: ..." (3418-3432), computed
  per save family (M-101 is the cost).
- Version ranges: an unreadable range is met (luma.cpp:1232); "^0.0.3" allows 0.0.x and 0.1 (1213-1214, noted).
- What the player is told when it cannot be resolved: the line under the page, in the played language where the table
  has the phrase. All four sentences have rows: NebulaText.tsv 853-855, and S.THIS_MOD_CAN'T_RUN_WITH.

### Trace (c): updating an installed mod
- Same id, newer version: the folder is replaced (on Windows, waiting if in use). The message is "UPDATED: <name>
  (<old> TO <new>)" (mod_import.cpp:608). mods.ini keeps its place, on/off and options by folder name. A changed code
  token asks the code question again (Consent.CodeChanged, 3646). A permission whose digest changed is asked again
  and marked new (3653-3658). An answer for a permission no longer declared is dropped (mods.cpp:1752-1763).
- Same id, older version: replaced the same way, said as "OLDER VERSION", outcome word UPDATED (M-106).
- Same content: "ALREADY IN YOUR MODS", nothing changes.
- Changed permissions: asked at the next page open, before anything else is drafted (4486-4492). Until answered the mod
  stays on but its code waits; Not now turns it off.
- In use: on Linux, macOS and Android, M-107.

### Trace (d): removing a mod and what is left
- REMOVE asks first. It is refused for a mod this session started (5589-5593). On Windows it goes to the recycle
  bin (mod_import.cpp:745-756). Elsewhere it is renamed aside and deleted, along with any waiting .update-<id>
  (758-768).
- Left behind:
  - its numbers and saved data (luma_mods::kept). They are listed under REMOVED MODS on the LOG page with FORGET
    (4237-4259). FORGET drops the numbers at once and the save chunks at each file's next save (5628-5643).
  - the blocklist entry, if blocked (by design: "never starts again until UNBLOCK, even removed and imported again",
    4098-4099).
  - .cooked/validate/<id>.kept and <id>.check. Nothing removes them (mod_log.cpp writes them at 683, 691 and 765; no
    remove call in the file). They are small; noted, not given an ID.
  - input bindings of its key options in input's store (mods.cpp:1771 says key options live there). Not traced
    further.
- Dropped: its mods.ini section, at the next save_mod_settings, because only listed mods are written (1743). So
  re-importing it later starts with no order, options or answers: on, at the top, asked again.

### Trace (e): the row a failed mod gets, by kind of failure, with the translation check
Every text below was looked up in NebulaText.tsv. Each found row has all 13 non-empty language columns plus the empty
EuEnglish, which falls back to UsEnglish.

| Failure | Row text (where) | Translated |
|---|---|---|
| Damaged .luma | THIS FILE IS DAMAGED (mods.cpp:1306, 1435) | yes (905) |
| Unreadable folder | A FOLDER OF THIS MOD COULD NOT BE READ IN FULL (81) | yes |
| Not a mod | THIS DOESN'T LOOK LIKE A MOD (1504) | yes |
| Other game | THIS MOD IS FOR SUPER MARIO GALAXY 2 / ANOTHER VERSION OF THE GAME / ANOTHER GAME (981-983) | yes (770-771 and S.THIS_MOD_IS_FOR_ANOTHER_VERSION_OF_THE_GAME) |
| No code for this device | THIS MOD HAS NO VERSION FOR THIS DEVICE (1322) | yes (904) |
| Built for an older Nebula | ITS CODE DID NOT START. IT WAS BUILT FOR AN OLDER NEBULA (1329) | yes, by sentence (1630 and S.IT_WAS_BUILT_FOR_AN_OLDER_NEBULA) |
| Off after a crash | THIS MOD'S CODE IS OFF AFTER A CRASH. RESTART TO TRY IT AGAIN (1339) | yes, whole (938) |
| Code patches missing | MOST OF THIS MOD WORKS. n CODE CHANGES NEED A NEBULA UPDATE OR A PLUGIN (1500) | yes, by sentence (781 and S.#_CODE_CHANGES_...) |
| Not converted (Android, iOS) | CONVERT THIS MOD WITH LUMALINK ON A COMPUTER, THEN OPEN THE FILE HERE (1510) | yes, whole |
| Blocked | YOU BLOCKED THIS MOD. UNBLOCK IT ON ITS PAGE TO USE IT (1518) | yes, whole (879) |
| Needs, newer, conflict | THIS MOD NEEDS / NEEDS A NEWER VERSION OF / CAN'T RUN WITH: <name> (1578-1620) | yes, by label (853-855) |
| Textures for another game | THESE TEXTURES ARE FOR ANOTHER GAME (1835) | yes (504) |
| Refused at the start | THIS MOD HAS ERRORS. ITS LOG SAYS WHY (mod_log.cpp:1016-1018) | yes, whole (772) |
| Code did not start (loader refusal) | the validator's own sentence, already in the played language (mod_plugins.cpp:1052-1061) | yes, by the validator's tables |
| Declined a required permission | Consent.OffDeclined row (2895-2899) | yes (row_words) |
| Not now | Consent.Waiting (4140-4143) | yes (row_words) |
| Status value | ON, NOT RUNNING (n OF m) (3782) | yes (2262) |
| Safe start | SAFE START: ... / NEBULA CLOSED UNEXPECTEDLY ... (3517-3518) | yes, whole (939-940), but only on the LOG page (M-100) |
| Import outcomes | IMPORTED, UPDATED, ALREADY IN YOUR MODS, NOT IMPORTED, FAILED, SOME FAILED | yes |
| Remove refused | IN USE UNTIL NEBULA RESTARTS: TURN IT OFF AND RESTART FIRST (5591) | yes, by label (882 and S.TURN_IT_OFF_AND_RESTART_FIRST) |
| Import busy | AN IMPORT IS RUNNING: TRY AGAIN WHEN IT ENDS (3276, 5586) | yes, whole (533) |
| Remove messages | REMOVED: <id>, MOVED TO THE RECYCLE BIN: <id>, IN USE: <id>. CLOSE WHAT HAS ITS FILES OPEN AND TRY AGAIN, NO MOD: <id> (mod_import.cpp:741-767) | yes, by label (S.REMOVED, S.MOVED_TO_THE_RECYCLE_BIN, S.IN_USE, S.CLOSE_WHAT_HAS_..., S.NO_MOD) |
| Mod text language | THE MOD'S / THE GAME'S <language>; THIS MOD HAS NO <language> TEXT | no (M-108) |

### Trace (f): 0, 1, 50 and 256 mods
- 0 mods: the page shows the two headings and the buttons; keys on a heading go to the buttons (4711-4714). The start
  check has nothing to do. Correct.
- 1 mod: no cross-check partner; the check key covers only itself. Correct.
- 50 and 256 mods: the work that grows with the square of N:
  - settle_mods' overlap pass (M-101), at every scan and every list change (place_mod_locked 2926);
  - check_packages' cross-check when anything changed (M-103), at the start;
  - the Mods page's twin-title check and per-row lookups, several times a frame (M-104);
  - mods_draft_changes_locked (2742-2773), a find_if per mod over s_mods_saved, plus a find over `order` per saved
    mod. It runs on every page build (4490) and every commit.
  - save_mods' change scan (2681-2692), also N x N.
- Work at every start whatever changed:
  - scan_mods reads every mod's manifest and walks every folder (round 7 notes on mods.cpp);
  - check_packages pass 1 stats every file of every package (mod_log.cpp:440-456);
  - plan_signature stats the plan's files.
- Limits: the Mods list has no cap. The loader caps code mods at LUMA_MAX_MODS = 256 (luma.h:25) and refuses the
  next with L500 (LumaLoader.cpp:1628-1631). The 257th code mod's row shows the loader's refusal as "ON, NOT
  RUNNING", which is said.

## Item 2: Area 1 leftovers

### native/include/luma_types.h (127), luma_modlog.h (117), luma_refs.h (34): read completely
### native/petari/src/Game/System/LumaModLog.cpp (1141): read completely
### native/petari/src/Game/System/LumaTypes.cpp 1-499 and 881-1141 (500-880 in round 1): read completely
### tools/lumacore/gen_classes.py (235) and gen_lumacore.py (68): read completely; lumacore_classes.cpp rows 1-40, 1964-1970, the tail and the count; lumacore_class_ids.hpp 1066-1114

#### M-109 docs wrong: luma_modlog.h says an entry's count is at most LUMA_MODLOG_ARGS, but the host sends ARGS + 1 on purpose
- Evidence: the header says `count; /* how many of names/values; at most LUMA_MODLOG_ARGS */` (luma_modlog.h:49).
  The host sets count = ARGS + 1 with ARGS names filled to say "more were cut" (mod_log.cpp:151-153, 179-181). The
  module clamps every read to ARGS (argCount, LumaModLog.cpp:177-179, 491, 1051) and adds ModLog.ArgsCut when count is
  above ARGS (838-840).
- Why: someone writing a second host or a test from the header would treat ARGS + 1 as a caller bug, and a reader
  written from the header could index past the arrays.
- Confidence: verified in code. The code is safe; the header is wrong.

#### M-110 maintainability: a game class the header scanner misses gets class id 0 ("unknown") with no build error
- Evidence:
  - gen_classes.scan finds class heads with regular expressions, not the compiler (gen_classes.py:31-85). Template
    detection looks back 200 characters (75-76).
  - A class name defined in two headers is skipped after the first, and only printed (110-112;
    gen_lumacore.py:66).
  - kClassId<T> is 0 for any type with no row (lumacore_class_ids.hpp:1067-1068). So
    `lumacore::setClass(this, lumacore::kClassId<Missed>)` compiles and names the object NameObj.
  - The generated file checks only that each row's parent is a base (static_assert, gen_classes.py:220-222).
- Why: a header adds a class behind a macro, or in a form the expressions do not match (for example a base list
  holding a comment-stripped string, or a class head split by a preprocessor branch). Its objects report class 0 to
  mods, and a mod's is-a test against its parent fails. Today there is no such case: a grep of every
  `kClassId<...>` used in native/petari (985 names) finds each one among the 1,058 rows.
- Confidence: verified in code. A fix is to make the primary template `static_assert(false)` for a type that is a
  NameObj with no row.

#### Notes
- Class ids: the row index, NameObj first, then parents before children, siblings sorted by name (gen_classes.py
  161-178). Ids change whenever a header gains a class. They are stable within one build only, so a mod must look a
  class up by name each session. Whether the mod docs say so was not checked (Area 10).
- luma_refs.h is one callback, luma_refs_check_after_start. Its use was traced in round 1 (references section 3); the
  header matches.
- Found correct in LumaModLog.cpp:
  - the store is made once by compare-and-swap (70-85), and lists are published with release stores and read with
    acquire loads (601-603, 805-807, 885-897);
  - entries are built outside the spin lock, and the key is looked up again after (770-787);
  - per-mod and per-session caps count the rest as "past" (595-597, 761-767);
  - UTF-8 decoding refuses overlongs, surrogates and values past U+10FFFF (112-146);
  - rowWords returns lengthOf(pDst), which is never above size - 1 (1063-1066). So mod_log.cpp's
    std::string(text, bytes) at 316-318 and 333-335 is safe, which closes that round 7 note;
  - a view is copied only up to the caller's size, whole fields only (1091-1112).
- Found correct in LumaTypes.cpp:
  - rows are sorted once with a heap sort, and two rows of one name stop the game with a named panic rather than a
    guess (161-188);
  - field lookup follows C++ hiding, refuses ambiguity and virtual bases, and stops at depth 64 (277-308);
  - leftOut's 512-byte join gives "no reason" for an over-long name (987-994);
  - describeTypes writes in 4 KB pieces (338-380).
- Minor: describeTypes calls bindField twice per inherited name (1052, 1068), and Names::add is linear per name
  (388-392). It runs only for `describe`, so no ID.

## Item 3: Area 4, the 29 game events

### Read completely: NebulaMods.cpp (870), NebulaMods.hpp (98), NebulaModCalls.hpp (26), mod_plugins.cpp (1138), nebula_mod_api.h 25-92 and 160-200; every raise site with its function (git grep NEBULA_MOD_EVENT_)

The dispatch path:
- A raise site calls NebulaMods::fire. It returns `value` at once unless sWanted[event] (NebulaMods.hpp:18-20).
- dispatch then calls every subscriber in order. Each gets the previous one's return as data.value (NebulaMods.cpp
  556-563). There is no guard, timing, thread check or re-entry check on this path.
- Subscribing goes only through the NebulaModApi table, nebula_game_mod_api (NebulaMods.cpp:156-164, 546-549), which
  only the host holds (mod_plugins.cpp:374).
- The host's only subscriber is achievement_event, one per event that an achievement's `when` names
  (mod_plugins.cpp:380-393). It returns data->value unchanged (93-103).
- Code that is not a LumaCore mod (the old nebula_plugin_start) is refused: "IT WAS BUILT FOR AN OLDER NEBULA"
  (717-720).
- So today no mod code receives any of the 29 events. A mod's code reaches the same moments only by hooking the
  function that raises them, as docs/modding/reference/events.md:10-15 says.

#### M-111 docs wrong / maintainability: the events' return-value contracts have no possible user, and the events page is stale
- Evidence:
  - The header and the events page promise return values: "Return 0 to cancel the hit", "Return -1 to cancel",
    "The return value is used", "added instead", "plays instead" (nebula_mod_api.h:34-46, 77-86;
    events.md:22-26, 43, 46).
  - The only subscriber returns the value unchanged (mod_plugins.cpp:93-103), and no mod can subscribe (above). The
    game still carries the branches for them: MarioActor.cpp:1460, MarioActorGameOver.cpp:97,
    MarioActorMorph.cpp:37, SceneUtil.cpp:360, EventUtil.cpp:96, AudSystem.cpp:205-211 and 256-262, and
    HitSensor.cpp:18-21.
  - events.md:46-47 still say "numbers in docs/modding/EVENTS.md", a file that does not exist. The header now says
    docs/modding/reference/events.md (nebula_mod_api.h:84-89), so the page was not generated again after the header
    changed (tools/modding/events_list.py builds it from the header).
- Why: a modder reads "Return 0 to cancel the hit" on the events page and looks for a way to subscribe. There is none,
  so they hook MarioActor's damage function instead. The return-value branches are code that only a future feature
  could use.
- Confidence: verified in code and docs.

#### M-112 wrong behaviour (cost): an achievement on a frequent event does string work for every achievement on every raise, for the whole session
- Evidence:
  - achievement_event loops over every achievement and calls mod_event_by_name(achievement.event) for each
    (mod_plugins.cpp:95-97). That is up to 29 string compares per achievement, per raise.
  - An achievement already unlocked is skipped only inside unlock_achievement_at (85), after the compares.
  - The subscription is never removed, so sWanted stays true after every achievement is done (NebulaMods.cpp:162).
- Why: one mod has an achievement whose `when` is "actor_message", "sound_start", "effect_start" or "actor_appear".
  Every hit-sensor message (HitSensor.cpp:18-19) and every sound (AudSystem.cpp:204-205) then costs
  (number of achievements) x (up to 29 compares) for the rest of the session. That stays true after the achievement
  is unlocked.
- Confidence: the shape is verified in code. The cost needs a run: a mod with 50 achievements, one on actor_message,
  timing a busy stage.

#### Note
- plugin_at (mod_plugins.cpp:1112-1131) walks g_loaded, which taken_out changes on the game thread (250-256). If the
  crash or guard path calls plugin_at on another thread during a take-out, the walk races. It is a crash path only; no
  ID.
- Every plugin "not started" sentence has a translation row (checked: TAKEN_OUT_WHILE_THE_GAME_RUNS,
  IT_NEEDS_A_MOD_THAT_DID_NOT_START, ITS_CODE_CAN'T_BE_READ, ITS_CODE_CHANGED_AFTER_IT_WAS_ALLOWED and the three
  load_sentence rows).
- The needs order (mod_plugins.cpp:545-554) starts again from the top after each placement, and ready() walks all
  plugins for each need. That is about N cubed string compares with 256 code mods, at the start; it belongs with
  trace (f).

### The event table
"Safe for a mod to act" means: could a handler, called right then, change game state (spawn, kill, move the player,
change counters, open a screen) without breaking the code around the raise site?

| Event | Raise site | Game state at that moment | Safe for a mod to act |
|---|---|---|---|
| FRAME | GameSystem.cpp:279, after luma frame work, before the settings menu update | game thread, between frames; any scene, including menus | yes: it is the frame start, the same point where services deliver LumaEvents |
| STAGE_FRAME | NebulaMods.cpp:586 (stageFrame, from GameScene::update) | game thread, before objects move | yes: the documented "before objects move" point |
| STAGE_START | GameScene.cpp:252, first line of GameScene::start | scene and its objects built; audio scene not yet started | mostly: objects exist, but sound started here comes before AudWrap's startScene (253) |
| STAGE_END | GameScene.cpp:118, in ~GameScene | the scene is being destroyed; actors and layouts go next | no: anything made or kept now dies with the arena; read only |
| PLAYER_DAMAGE | MarioActor.cpp:1460, before the hit is applied | inside the player's damage path | no for changes to the player (re-entry into the damage code); reading is fine |
| PLAYER_MISS | MarioActorGameOver.cpp:97, before the death sequence | inside the player's death decision | no: the game-over path is half decided |
| POWER_UP | MarioActorMorph.cpp:37, start of setPlayerMode | inside the player's mode change | no: calling setPlayerMode again re-enters |
| COIN | SceneUtil.cpp:360, in MR::incCoin before the count and life change | inside a pickup | no for counters (they are about to change); reading is fine |
| STAR_BIT | EventUtil.cpp:96, in MR::addStarPiece before the add | inside a pickup | no for counters, as COIN |
| STAR_GET | GameDataFunction.cpp:49, after setPowerStar | save data just changed, still in the star sequence | read only: the result sequence continues |
| CUTSCENE_START | DemoDirector.cpp:61, 67, before startDemo | the demo is not yet active | no: a second demo request now collides |
| CUTSCENE_END | DemoDirector.cpp:91 (start of endDemo); AstroDomeGalaxySelector.cpp:262 | the demo is ending; the director still holds it | no: wait a frame |
| CUTSCENE_PART | DemoTimeKeeper.cpp:30, 52, 111 | inside the demo's time step | read only |
| ACTOR_APPEAR | LiveActor.cpp:52, first line of makeActorAppeared | the actor is still marked dead; while a stage loads, on the loading thread (nebula_mod_api.h:25-26) | no: wrong thread during loads, and the actor is half appeared |
| ACTOR_KILL | LiveActor.cpp:87, first line of makeActorDead | the actor is still alive | no: half killed; killing others here re-enters |
| PLAYER_SPIN, PLAYER_JUMP, PLAYER_LAND | NebulaMods.cpp:574-580, in stageFrame | game thread, before objects move | yes: same point as STAGE_FRAME |
| PAUSE | GamePauseSequence.cpp:39 (open), 60 (close) | the pause menu is changing | read only: the menu's nerve is being set |
| GAME_SAVE | GameSequenceFunction.cpp:277, start of the save sequence | before the comet state and save are written | yes to add data to the save (its moment); no to start a scene |
| POWER_STAR_APPEAR | PowerStarHolder.cpp:27, before the request is looked up | inside a star request | read only |
| TALK_LINE | TalkDirector.cpp:500, first step of exeTalk | a balloon opening | read only: talk state is mid-change |
| TALK_CHOICE | TalkDirector.cpp:546, after a yes/no is chosen | before the answer is handled | read only |
| CAMERA_CHANGE | CameraDirector.cpp:306 (event camera start, before push), 341 (end); CameraManGame.cpp:211 (area camera after the chunk change) | inside camera selection | no: changing the camera here re-enters the director |
| SOUND_START | AudSystem.cpp:205, 256, before the sound starts | inside the sound system; the caller's thread (not traced) | no: starting a sound here re-enters startSound |
| EFFECT_START | EffectUtil.cpp:55, 71, before the emitter is made | inside an effect start | no: emitting here re-enters |
| SCREEN | LayoutActor.cpp:52 (appear from dead), 62 (kill) | a layout changing state | read only |
| ACTOR_MESSAGE | HitSensor.cpp:18-19, before the receiver handles it | inside sensor exchange, many times per frame | no: sending messages here re-enters sensor code |
| PLAYER_ACTION | MarioState.cpp:78, start of Mario::changeStatus | inside the player's state change | no: changing status here re-enters |

What it would take to deliver them to mod code:
- For notification, the safe way is to queue the event and deliver it at the next frame start, as the services already
  deliver LumaEvent (luma_services.h:9, 65-80; GameSystem.cpp:272 frameStart).
  - An actor pointer must then be a handle checked at delivery, because the actor may be gone by then.
  - The event's name strings must be copied.
  - A per-frame cap already exists: LUMA_EVENTS_PER_FRAME 256.
  - The guard and the per-mod time accounting come with the services' delivery.
- For the "change the value" contracts (cancel damage, change coins), a queue cannot work. Those need synchronous
  delivery inside the guard on the game thread, with a re-entry flag per event. That is what a `hook before` on the
  raising function already gives, so the honest fix is to drop the return-value wording (M-111) and point to hooks.
- ACTOR_APPEAR during a load runs on the loading thread. It must be queued for the game thread whatever the design.

## Item 4: Area 3, what the build knows per function, and the "needs" design

Read completely in round 7: tools/lumacore/instrument.py (2005 lines), gen_types.py (1594), self_description.py (182),
gen_lumacore.py (69); gen_classes.py was read completely for item 2 (M-110). Bodies checked for the test case:
MarioAccess.cpp 282-290 and 615-640, MarioHolder.cpp 17-27, PlayerUtil.cpp 392-394 and 508-510, SceneUtil.cpp 36-38 and
359-363, CameraUtil.cpp 25-31 and 62-188 (grep), SceneObjHolder.cpp 136-140 and 384-392, SceneObjHolder.hpp 163-176,
luma_guard.h 1-70. LumaScript side: crates/lumascript/src/game.rs 866-950 and check/body.rs 355-365, 505-561 (grep).

### How the generators run (the facts below depend on it)
- instrument.py runs once per game .cpp (main, 1943-1998). It parses the unit with the build's own compile command
  through libclang (1957-1980; LLVM 21 or later, 129-151), then: Unit.walk (row per defined function), a gen_types
  Reader in unit pass (1983-1985), read_marks (1986), function_facts (1987), places (1988), write (1989).
- Per unit it writes OUTPUT and OUTPUT.numbers, .types, .rows, .functions, .places, .left, .globals (1795-1805).
- gen_types.py runs once over one unit that includes every game header (main 203-261, parse with bodies 273-276). Its
  write() (892-923) is the only place that sees every unit at once: merge_units reads every .types (1088-1142) and,
  with --defined, merge_functions reads every .rows and .functions (1509-1541) and writes luma_functions.json (913-918);
  merge_numbers does the same for numbers (1544-1578).
- self_description.py (126-177) copies luma_functions.json's rows unchanged into smg_game.luma-self.json "functions"
  (134, 158). Its `names.nerves` is the one place a call list is used (nerves 80-97).

### Per-function facts: what exists, what can be derived

| Fact | In the build today | Derivable from the parse it already runs |
|---|---|---|
| Identity (row name, signature) | yes: identity() 1200-1225; a unit-local function is "file#name" (1209-1212) | - |
| kind, owner, virtual, pure, const, overrides | yes: function_facts 1227-1251; merge checks owners and marks every override's `row` (1533-1540) | - |
| Parameter marks (in, out, inout, kept, count, fixed, nullable) | yes: read_marks 1470-1575, mark_entry 1599-1661 | - |
| Callees | no per function. Two partial lists: gen_types `calls` for a virtual whose body is in the parse (virtual 575-580, calls_in 582-601), and instrument `places` helpers, only calls that take a placement iterator (1298-1323) | yes, directly: every K_CALL_EXPR (103) in a body and api.referenced (instrument.py:219) give the callee, and identity() names it. The walk pattern is places() 1260-1289 |
| Template callees (MR::getSceneObj<T>) | dropped: scope() is None for a template (376-378), so identity() is None (1204-1206) and calls_in leaves the call out (593-597) | yes: api.specialized (200) gives the pattern; api.argument(call, i) (192) and the referenced enum constant or api.evaluate (232-238) give the SceneObj id |
| Which singletons or scene objects a body reaches | no | yes, as roots at call sites: MR::getSceneObj<T>(id) (SceneObjHolder.hpp:174-176) with its id; MR::getSceneObjHolder (SceneObjHolder.cpp:384-386); SingletonHolder<T>::get(); then closed over the call graph in the merge |
| Whether each use is null-tested | no | partly, by an AST rule with no flow analysis (below): libclang gives K_IF (205), the conditional operator, comparisons (K_BINARY 114 and the operator text, operator_between 839-844) and member access (K_MEMBER_REF_EXPR 102) |
| Which thread calls it | no | not from the parse. A thread's entry is a function whose address is passed to a thread start; which thread runs which entry needs a hand list. Propagation over the call graph is then mechanical |
| Body file and line | no in .functions; places rows have them (1281-1283) | yes: self.line(cursor) |

### Design note: marking every function with what it needs

Vocabulary. A need is "scene object X exists" (X is a SceneObj id), plus "game thread". Named groups for people and
the checker come from one hand table of ids: player = SceneObj_MarioHolder; camera = SceneObj_CameraContext (and the
camera director's id); stage = the objects a stage build creates (ScenePlayingResult among them); scene = any other
SceneObj, or the scene controller itself (getSceneObjHolder dereferences mSceneController with no test,
SceneObjHolder.cpp:385, while isExistSceneObj tests it, 389-392).

1. Unit pass (instrument.py, a new `needs_facts(types)` beside function_facts and places, called after 1988). For each
   row in self.facts, walk the body once (as places() does) and record:
   - `calls`: every callee with an identity, once each, with line.
   - `roots`: each call to MR::getSceneObj<T>(id) (recognised by api.specialized on the target, not by name text), with
     the id read from argument 0, and each MR::getSceneObjHolder() or SingletonHolder<T>::get().
   - For each root call and each call to a row, how its result is used:
     - `deref`: the call is the base of `->` or a member call, or under unary `*` (getPlayerActor()->getSensor(...),
       MarioAccess.cpp:635; ::getScenePlayingResult()->incCoin(term), SceneUtil.cpp:361);
     - `returned`: the call is the operand of the function's `return` (getScenePlayingResult, SceneUtil.cpp:37;
       getCameraContext, CameraUtil.cpp:30);
     - `tested`: the call or the local it is stored in is compared with nullptr or is an if or ?: condition before the
       use; or the use sits under an if or ?: whose condition calls MR::isExistSceneObj with the same id
       (MR::getMarioHolder, MarioHolder.cpp:25).
     Anything the rule cannot place is `deref` (wrong on the safe side: it marks more, never less).
   - Written to a new OUTPUT.needs (JSON, one entry per row, in .rows order), so merge_functions' row-for-row check
     (1514-1528) covers it too.
2. Whole-program pass (gen_types.py, in write() right after merge_functions at 913, before luma_functions.json is
   written). Two results per row, computed to a fixed point over `calls`:
   - `null_when`: ids whose absence makes the result null. From a root's id when `returned`, and from a callee's
     null_when when `returned`. A `tested` root adds nothing (getMarioHolder: null_when {MarioHolder}, needs {}).
   - `needs`: ids whose absence faults. From a root or callee result used as `deref` (adds that call's null_when, or
     the root's id), plus every callee's `needs`.
   - Virtual calls: a call through a virtual adds the needs of the declared function's own row only; each override
     keeps its own row's needs. Taking the union of every override would mark nearly all actor code as needing
     everything; a checked script calls through a row it names, so the row named is the one to check.
   - game_thread: true when a function is reached only from the hand list's game-thread entries; "any" when reached
     from none of the listed entries (a free helper); "load" when reachable from the loading thread's entry.
3. The row gains, in luma_functions.json and so in luma-self.json (self_description copies rows whole, 158):
   `"needs": ["MarioHolder", ...]`, `"null_when": [...]`, `"thread": "game" | "load" | "any"`, and `"needs_why"`: the
   first edge of each need (callee or root, file, line), so a refusal can say why.
4. What the LumaScript checker enforces (crates/lumascript):
   - game.rs reads the new fields beside `marks` (866-950).
   - A call to a row with needs N is accepted where N is known present, using the same mechanism body.rs already has
     for optional services (`given`, `given_marks`, is_given: 361-365, 505-561): known present inside a hook on a row
     whose own needs include N; inside an event or `on frame` whose context table (hand list, per LumaEvent kind)
     provides N; or after a test such as `if game.has(player)` on the path (a `wait` forgets it, as S0104 forgets
     views).
   - Elsewhere it refuses with a new S-code naming the need and needs_why's first edge.
   - A row with thread "load" cannot be called from a script at all; hooks on such rows run on the loading thread and
     are refused or queued (the ACTOR_APPEAR point in item 3).
   - A row's null_when makes its pointer result `T?` only when null_when is not empty; today every pointer is `T?`
     (game.rs:17).
5. What needs a hand list (one checked-in table, refused by the build when an entry no longer names a row or an id):
   - id to group (player, camera, stage, scene);
   - thread entries: the game thread's frame entry, the loading thread's scene build entry, any audio callback entry;
   - the contexts: which needs each LumaEvent kind and each `on` handler provides (FRAME and STAGE_FRAME: scene,
     player, camera; STAGE_START: scene without camera; and so on from item 3's table);
   - overrides for the rule's misses: a function that tests in a way the rule does not see (a helper returning bool,
     then early return) gets a "tested" exception, each with a line saying why; a function that reaches a need the
     pass cannot see (through a function pointer or a table of functions, such as nerve tables) gets an added need.
   - A LUMA_NEEDS(...) mark on the declaration, read as LumaMarks.hpp's marks are (read_marks 1470-1575), is the
     in-source form of these overrides.

Test case, traced by hand through the rules above (verified in code; no run):
- MarioAccess::getPlayerActor (MarioAccess.cpp:629-632): calls MR::getMarioHolder, tests it (631), so needs {} and
  null_when {MarioHolder}; getMarioHolder itself tests isExistSceneObj(SceneObj_MarioHolder) (MarioHolder.cpp:25):
  null_when {MarioHolder}.
- MarioAccess::validateSensor (634-636): getPlayerActor() is `deref` at 635: needs {MarioHolder} (player).
  MR::validatePlayerSensor (PlayerUtil.cpp:508-510) calls it: needs {MarioHolder}.
- MarioAccess::incLife (282-290): `getPlayerActor()->IsMarioSwimming()` at 283 is `deref`: needs {MarioHolder}.
  MR::incPlayerLife (PlayerUtil.cpp:392-394): needs {MarioHolder} (player).
- SceneUtil.cpp#getScenePlayingResult (36-38): returns getSceneObj<ScenePlayingResult>(SceneObj_ScenePlayingResult):
  null_when {ScenePlayingResult} and, through getSceneObjHolder, needs {scene controller}.
- MR::incCoin (SceneUtil.cpp:359-363): `::getScenePlayingResult()->incCoin(term)` at 361 is `deref`, and it calls
  incPlayerLife at 362: needs {ScenePlayingResult, MarioHolder} (stage and player). (The fire() at 360 adds nothing.)
- CameraUtil.cpp#getCameraContext (29-31): null_when {CameraContext}. Every MR camera function that writes
  `::getCameraContext()->` (CameraUtil.cpp:62, 69, 95, 126-188 and on) is `deref`: needs {CameraContext} (camera).
  This is kit camera.now's fault in M-014's round 5 runs and M-038.
- All four come out marked. Today none has any such fact (M-014; round 2 Area 3).

#### M-113 maintainability: the only call lists the build keeps drop template callees, so a needs pass built on them would see no scene reach at all
- Evidence:
  - gen_types.py calls_in (582-601) keeps a callee only when identity() names it (593-597); identity() is None when
    scope() is None (instrument.py:1204-1206), and scope() is None for a template (376-378).
  - Every scene object read goes through the template MR::getSceneObj<T> (SceneObjHolder.hpp:174-176), for example
    SceneUtil.cpp:37, CameraUtil.cpp:30, MarioHolder.cpp:25.
  - calls_in also exists only for virtuals with a body in the parse (575-580), not for free functions or non-virtual
    methods, which is where MR::incCoin and MarioAccess live.
- Why: anyone who extends `calls` to every function and closes over it (the natural first step) gets getScenePlayingResult
  and getCameraContext with no callee and so no need; incCoin, validateSensor and the camera functions come out unmarked.
- Confidence: verified in code. The pass must read template calls at the call site (api.specialized, argument 0), as
  the design above does.

Checked in item 4 and found correct:
- merge_functions (1509-1541) stops the build when a unit's .functions does not match its .rows row for row, when a row
  has no marks, when an owner is no record, or an override has no name; a new .needs file can join the same check.
- read_marks (1470-1575) stops on a mark that marks nothing, a mark inside a macro (1577-1597), a mark on a template,
  and two declarations that mark one parameter differently. mark_entry (1599-1661) refuses a count with no way, a
  count with kept, and a mark on a type LumaScript cannot cross.
- Unit uses calls only gen_types.Api has (canonical 1630, canonical_cursor 1552, members 1678); main builds
  gen_types.Api (1965), so they resolve (gen_types.py:156, 167, 188-197, __getattr__ 199-200).
- merge_numbers (1544-1578) stops on a number named twice and on a number of a function with no row.
- self_description.py stops on a function or type listed twice (137-149) and writes through a temporary file then
  replace (167-170), so a failed run never leaves half a file.
- gen_lumacore.py --check (47-52) compares the hash of every header plus both scripts; it does not regenerate. Its own
  docstring says no build runs it (12): drift is caught only when someone runs --check (as M-110 notes).

## Round 7 count

Round 7 findings: M-101 to M-113, with M-102 withdrawn: 12 standing. By severity: crash 0; wrong behaviour 7
(M-101, M-103, M-104, M-112 are costs; M-105, M-106, M-107); docs wrong 3 (M-108, M-109, M-111); maintainability 2
(M-110, M-113). Areas 1, 3, 4 and 9 are closed.

# Round 8

Area 5, the LumaScript compiler (D:\NebulaWork\lumalink-work\lumalink-sweep\crates\lumascript). The crate is 46807 lines
(src, rt and tests). This round could not read it all; it went by the questions, reading whole files where they answer
one and named ranges elsewhere.
- Read completely: src/diag.rs (708), rt/lumart.h (768), src/emit/task.rs (328), src/emit/game.rs (396).
- Read in part: rt/lumart.c 1-260, 2930-3080; src/emit/expr.rs 272-282, 380-392, 560-715, 1335-1390;
  src/emit/service.rs 800-820, 985-1010, 1076-1105, 1680-1710; src/emit.rs 1690-1760; src/emit/flow.rs 190-214;
  src/svc.rs 1095-1114, 1295-1320. Every run-time code raise in src and rt by grep. Test files: every file's head
  and test names; feature keywords counted over tests and the audit-conf suites.
- Not started: lex.rs, parse.rs, ast.rs, check.rs, check/ (body.rs, body/expr.rs, body/call.rs, body/pat.rs,
  body/service.rs), ctype.rs, game.rs, lumi.rs, fmt.rs, lib.rs, emit/flow.rs (the rest), states.rs, closure.rs, json.rs,
  class.rs, library.rs, cside.rs, the rest of emit.rs, emit/expr.rs, emit/service.rs, svc.rs and lumart.c.
- Also checked outside the crate: LumaServices.cpp:697-707 (how the game matches a service's minor version),
  LumaLoader.cpp:456-474 (how a script error is reported), luma_guard.cpp:344, 357 (stack overflow),
  detailed-rules.md:125, 324, 330.

#### M-114 wrong behaviour: texts given to the game and closures given to services are kept until the mod stops, one per distinct value; a mod that does either each frame grows without limit
- Evidence:
  - emit/expr.rs:1364-1383: any game parameter `const char *` given a non-literal text goes through
    lumart_text_keep.
  - lumart.c:3046-3079: lumart_text_keep keeps one copy per distinct text in a table that only grows. lumart.h:621-626
    says these copies live "while the library is loaded". keep16 (lumart.c:2930-3017) adds a UTF-16 copy per text.
  - emit/service.rs:1051, 1093, 1285: every request's closure, stream callback and service callback goes through
    lumart_fn_keep.
  - lumart.c:3024-3044: lumart_fn_keep keeps each closure block, with what it captured, until the mod ends. It scans
    every kept block first (3026-3028), so each new closure costs more than the last.
- Why: either of these, every frame, grows the mod's memory and the two tables until the mod stops:
  - `on frame` passing an interpolated text that changes each frame, such as a timer, to a game function that takes
    a `const char *`;
  - a request made with a new closure each time, such as an http or fs call inside a loop or a task.
  The mod's heap ends with S0911. Before that, fn_keep's scan grows with every call made.
- Confidence: verified in code. A run would show it: a mod calling such a function with `"{frame}"` each frame while
  logging its heap use.

#### M-115 docs wrong: detailed-rules.md tells modders to write `if let p = MarioAccess::getPlayerActor { ... }`, which the compiler refuses
- Evidence: docs/modding/reference/lumascript/detailed-rules.md:125, 324 and 330 (Nebula-Clean) use the form without
  `()`. diag.rs:199-200 defines S0083 NOT_A_VALUE, "`{found}` is a `{what}`, not a value", and Script.Kind.Function
  (diag.rs:673) fills the phrase.
- Why: a modder copying the documented safe pattern for "no player" (the M-014 guard) gets a build error instead.
- Confidence: verified in docs. The refusal is from the coordinator's run.

#### M-116 wrong behaviour (compatibility): a mod asks each service at the SDK's newest minor version, not the lowest one that has the functions it calls, so a mod rebuilt with a newer LumaLink stops loading on every older game
- Evidence:
  - svc.rs:1313-1314: a face's version is the header's `LUMA_<NAME>_MAJOR`.`LUMA_<NAME>_MINOR`. svc.rs:1102-1105:
    "the version its import asks".
  - emit/service.rs:816 copies it into the slot, and 1696-1704 writes it into every import row: LUMA_SERVICE, or the
    LUMA_OPTIONAL row. emit/service.rs:996-997 passes the same numbers to lumart_for_caller.
  - emit.rs:1836 asks the time service at LUMA_TIME_MAJOR and LUMA_TIME_MINOR, the SDK's own.
  - Nothing in src records the minor version that added a function: no "since" field in any face.
  - The game accepts a provider only when its minor is at least the one asked (LumaServices.cpp:707,
    `rEntry.minor >= minor`).
- Why: the coordinator's run. balloon-world, built against an SDK with input 1.4, is refused with L505 by a game with
  input 1.3, whatever input functions it calls. The rule is: asked minor = the minor of the SDK the mod was built with,
  for every service it uses. A mod built with a newer LumaLink loads only on a game at least as new, service by service,
  even when it uses nothing new.
- Fix shape: record, per function, the minor version that added it. The header already orders functions by version:
  tables grow at the end. Then ask the highest of those over the functions the mod calls.
- Confidence: verified in code; the refusal is from the coordinator's run.

#### M-117 maintainability (tests): language features that no in-game suite runs, and features no compiler test compiles
- Evidence: keyword counts over crates/lumascript/tests (*.rs and data/programs/*.lum) and over every .lum under
  D:\NebulaWork\build\audit-conf\luma. These are counts of a keyword's presence, not of a run:
  - no in-game test at all: `on option` (3 compiler test files, 0 in-game) and `set_number` (1, 0).
  - one in-game test only: `set<`, `sort_by`, `set_table`, `hook ... replace`, `every override`, `is_state`,
    `text "Key"`.
  - No compiler test runs the emitted C in the game. tests/emit.rs compiles the corpus's C only when LUMA_SDK is set
    (M-019). emit_small.rs's 65 tests compare C text.
- Why: a regression in these features is found only by a modder.
- Confidence: verified by count. Whether the audit-conf suites still pass on this tree is M-026.

## Point 6: the service version a mod asks for
The rule (M-116): the import row of each service a mod calls carries the SDK header's own MAJOR.MINOR (svc.rs:1313-1314,
emit/service.rs:1700-1702; time: emit.rs:1836). The game accepts it only from a provider of the same major with a minor
at least that (LumaServices.cpp:707). So a mod built with a newer LumaLink, whose SDK has a higher minor for any
service the mod uses, is refused (L505) by an older game, even when it calls only functions the older game has. An
optional service (`use optional`) is left off instead of refusing the mod (emit/service.rs:1699-1700).

## Point 3: script errors
- Build time (diag.rs:43-349): S0001 to S0025 are the lexer and parser; S0026 to S0046 items and uses; S0047 to S0106
  bodies; S0107 to S0111 services and `wait`; S0112 to S0132 hooks, tasks and states; S0133 to S0153 game crossing,
  content, classes and tables; S0160 a library built by another LumaLink; S0800 "the back end does not write this
  yet". Each code's trigger is the comment beside it at the listed line. Notes, which do not stop the build:
  S0033, S0046, S0097 (diag.rs:508).
- Run time (every raise in src and rt):

  | Code | Trigger |
  |---|---|
  | S0901 | a hook ran off the game thread (lumart.h:720-721) |
  | S0902 | an index outside a list, or outside a game array's extent (game.rs:85-111) |
  | S0903 | a service handle whose object has gone (emit/service.rs:674-681) |
  | S0904 | integer overflow in + - * and unary -, a shift count outside the width, MIN / -1 (expr.rs:272-282, 595-638) |
  | S0905 | division or remainder by 0 (expr.rs:609) |
  | S0906 | `x!` on none (expr.rs:380-392) |
  | S0907 | a missing map key read with `m[k]` |
  | S0908 | `as` or a bit-field write that does not fit (expr.rs:674-712; game.rs:243-269) |
  | S0909, S0910 | a text slice off a code point boundary, or outside the text or the bytes |
  | S0911 | out of memory |
  | S0914 | a library's wrong call |
  | S0917 | NaN or infinity in a fact, in json or in `wait N seconds` |
  | S0918 | a NUL byte in a text given to C |
  | S0919 | a negative wait |
  | S0920 | a handle's watch could not be hooked |
  | S0921 | `fail` |

  Every run-time code has a row in localization/NebulaText.tsv.
- What each failure stops:
  - Every script error ends only that run of the handler, hook, task step or callback (lumart.h:6-7). It is said once
    and repeats are counted. The mod stays on: LumaLoader.cpp:470-472 only turns the code into a text row.
  - A fault in the mod's own code (a null view read, a stack overflow from deep script recursion,
    luma_guard.cpp:344, 357) is resumed by the guard, and the mod is off for the session.
  - A fault in game code that a script called ends the game (M-014; M-001 for LumaKit).

## Point 1 and 2 (what was read)
- Numbers are sound in what was read:
  - + - * use __builtin_*_overflow, and unary minus checks too (expr.rs:595-605, 272-282).
  - Division checks 0 and MIN / -1 (606-615).
  - A shift count is checked against the width. A left shift is done unsigned. A right shift of a negative number is
    an arithmetic shift, which C leaves to the implementation; clang defines it, so it is not undefined (624-638).
  - `as` checks both integer and float ranges, and NaN never fits (674-712).
  - Float `%` is refused (643).
- `T?`: `x!` checks for none (S0906). A game pointer result is `T?` until tested (game.rs places 173-181). No path from
  `T?` to `T` without a test was found in the code read. check/ was not read, so this is not proven.
- Game places:
  - A field's offset and a global's address come from slots the loader binds by name and type text (game.rs:1-5,
    128-167). A wrong row there fails to bind; it is not wrong code.
  - Two things are fixed at build time: a game array's extent, used for the index check (game.rs:92-111), and a folded
    constant's value (game.rs:271-296). If a game build changed an extent while keeping the field's type text, the
    check would be wrong. The type text includes the extents, so binding should fail first. Suspected, needs a run
    across two game builds.
- Views: a pointer field is read raw, `*(void**)` (game.rs:173-174). A dangling game pointer read by a script faults in
  the mod (mod off) or, passed back to a game call, in the game (game ends).
- Tasks (task.rs):
  - Locals are copied into the frame at every wait (255-320).
  - `stop` runs the way out (290).
  - `wait` on a pending ends the task as stopped when the value is not there (186-191).
  - Tasks run from the frame handler after `on frame`. Both run in every time state, BOOT and LOADING included
    (emit.rs:1714-1742). Only `on stage` checks the state (1726).
  - So a `wait until` condition that calls a player or scene function is read during a stage load: an M-014 case. It
    is not a new ID.
- Memory:
  - Blocks up to 512 bytes come from pools of 64 KiB pages (lumart.c:8-85).
  - Scratch chunks start at 64 KiB and double (lumart.c:176-246).
  - Neither pages nor scratch chunks are given back before the mod ends (lumart.c:8-11; lumart.h:109-113). Freed
    blocks are reused, but the peak stays.
  - The kept tables of M-114 only grow.
- Not answered this round, because the files were not read:
  - per-hook instruction and allocation cost (emit.rs hook code);
  - closures, json and states in detail;
  - diagnostics quality beyond M-115 (check/);
  - the first-person report's wrong places.

# Round 9

Area 5, the compiler, continued. Read completely this round, in the order given: check.rs (lines 940-1475; 1-939
were read at the start of the round), check/body.rs, check/body/expr.rs, check/body/call.rs, check/body/pat.rs,
check/body/service.rs, ctype.rs. Paths in this section are under crates/lumascript/src unless they start with docs/
(Nebula-Clean). Read only where cited: diag.rs code numbers, parse.rs 1320-1362, emit/game.rs 320-395,
emit/service.rs 818-827, the S00xx rows of crates/lumalink-core/text/LumaLinkText.tsv, and the docs lines cited.

## Point 1: the checker

### The `T?` question: no proof; four counter-examples

How a `T?` becomes a `T`:
- Binding constructs:
  - `if let` and `guard let`: body.rs:2313-2347 unwraps the subject's `?` before the pattern binds.
  - A `match` arm whose pattern is not a bare name: pat.rs:95-98 matches it inside `Some`.
- Operators:
  - `??` gives `T` when its right side is a `T` (expr.rs:1145-1167).
  - `x!` gives `T` with a run-time test, S0906 (expr.rs:483-499).
- Checks that stop a `T?` where a `T` goes:
  - Every typed place goes through check_fit/fits (expr.rs:328-373). `fits(T?, T)` is false (expr.rs:353-372), and
    the error is S0067.
  - Places that pass through it: arguments (call.rs:446, 1638), `return` (body.rs:1977), a typed `let`
    (body.rs:2173), assignment (body.rs:2371), struct fields (expr.rs:1996, 2001), list and map items, and `skip`
    and `result` (body.rs:2127, 2359).
  - Separate checks: a member or method of a `T?` without `?.` (expr.rs:1765-1772, call.rs:627-635); an index
    (expr.rs:2114-2119); interpolation and `log` (expr.rs:699-709).
  - Other refusals: arithmetic (binary_types, expr.rs:1319-1346, no `Opt` arm), ordering (expr.rs:1203-1210),
    `as` (expr.rs:601-613), `for` (body.rs:2148-2159), `wait` (expr.rs:536-545), and a condition (Want Bool).
  - Builtin closures: a closure given to a builtin must take the element type (call.rs:844).
  - Function values must match exactly (expr.rs:370).

How a `T?` stays a `T?`:
- `?.` gives `T?` (expr.rs:1786-1791, call.rs:648-653).
- `x as? T` and `as_class` give `T?` (expr.rs:556-584, call.rs:1302-1344).
- A bare-name `match` arm binds the whole `T?` (pat.rs:91-94).

The routes asked about, each verified in code:
- Field of an optional: refused with S0067 unless written `?.`, which keeps the `?`.
- Closure capture: a capture is the same local, read-only (body.rs:590-605). No test ever narrows a local (see
  below), so a capture has nothing to lose.
- A value kept across a `wait`: the type does not change. A view held across the wait is S0104 (body.rs:704-742).
- A `var` reassigned after its test: a test does not narrow. `if let v = x` copies into a new `let`, so writing `x`
  later cannot reach `v`.
- A value returned from a hook: the result takes the row's type. A pointer result is `T?` (body.rs:1273-1277).
  `skip v` and `result = v` are checked against it (body.rs:2126-2128, 2356-2359).
- Match arms: bare-name arms keep the `?` (pat.rs:91-94). Other arms match inside `Some` (pat.rs:95-98).
- `!`: the one admitted escape. It is a run-time test (S0906), a script error, not a fault.
- Index of a map and of a list: they give `V` and `T`, not `V?` and `T?`. The run time tests them (S0907, S0902).
  This is by design (expr.rs:2095-2106).

The counter-examples:
1. M-118: the checker refuses a `T?` that the game takes. This is too strict, not unsound.
2. M-123: the checker lets a `none` reach a game pointer field the game may read without a test.
3. M-119: S0067 says "test it first", but only optional services narrow on a test. For a value, `if x != none`
   narrows nothing: body.rs:2316-2324 records only optional services in `given`, and expr.rs:1168-1195 gives
   `Bool`.
4. `Ty::Error` fits every place, including a `T` place (expr.rs:354).
   - It always follows an error that was said, with one exception: a service the build has no SDK faces for. That
     is only a note (S0033, check.rs:709-712). Every call, field and type of that service is then `Ty::Error` with
     nothing said (service.rs:242-245, expr.rs:1877, expr.rs:1957, check.rs:876-878).
   - The back end then stops with its internal "lost" error (emit/service.rs:826). So no unchecked code is made,
     but the modder gets an internal error, not a place.

### What the checker trusts from the game description

The checker takes all of these as given. It checks none of them against the game binary.
- Nullability: never-null only from `T &`; may-be-null parameter marks (detailed-rules.md:301-310).
- The rows' signature text, read by ctype.rs. A text that does not read is refused, never guessed
  (ctype.rs:1-4, 440-447).
- The function facts:
  - What they give: kind, owner, `is_virtual`, `is_const` and `overrides` (body.rs:1294-1339, 1523-1538;
    call.rs:1587-1608).
  - Without facts, a hook's `this` comes from the name's qualifier (body.rs:1334, 1780-1786). Overrides then cannot
    be checked: they get one note per class (check.rs:1408-1415).
- The types file:
  - records and their bases (`derives`, expr.rs:376-397);
  - `polymorphic` and by-value (body.rs:2233-2238);
  - field `read_only`, from a top-level `const` only (game.rs:989-1035);
  - enums and anonymous enums (body.rs:410-437).
- Folded constants and tables (call.rs:1171-1297).
- Factory rows, `derivable` and `gravity_base` (check.rs:1316-1374).
- Service faces from the SDK headers. Each LumaLink build carries them, which is the M-116 finding.
- Content names. A kind in `game_unlisted` is not checked (expr.rs:894).

### Checks skipped for C mixed with scripts (cside)

- `use c "<header>"`: the checker checks only that the header is in the mod's list (check.rs:679-685).
- `extern fn`: its body is skipped (body.rs:939). Its written parameter and result types are taken as declared
  (check.rs:1246-1251). Nothing in check/ reads a C header, so the checker never compares them with the header.
  - Calls are checked against the written types only (call.rs:483, expr.rs:1493).
  - A view passed to an extern is never S0103. kept() is called only for:
    - fields (body.rs:903, 916, 1094),
    - module bindings (1034),
    - task parameters (992),
    - closures (720, 698).
  - A game call has the same gap: the game can keep the pointer too.
- These are M-124.

## Point 5 (reached early): the causes of the first-person report's errors

All four come from the docs. They write calls without `()`, and `vec3` alone as a zero vector. The compiler takes
neither. Bare `vec3` is the standard function, never the type (expr.rs:1408-1416 looks up STD_NAMES before
BUILTIN_TYPES), so it is S0083.
- S0083 for `target?.get`: docs/modding/reference/lumascript/values-and-memory.md:27 writes
  `if let k = target?.get { k.mVelocity = vec3 }`. tasks.md:15 and 17 write `if let a = h.get`; detailed-rules.md:14
  writes `let p = r.peers.get`. A method named without `()` is S0083 "is a function, not a value" (expr.rs:1884-1886).
- S0083 for getCurrentStageName: detailed-rules.md:321 says to write `MR::getCurrentStageName ?? ""`.
  `MR::getCurrentStageName` without `()` is S0083 (expr.rs:1619-1620). M-115's lines 125, 324 and 330 are the same
  fault.
- S0067 for getCurrentStageName: the result is `text?` (detailed-rules.md:319-320), so S0067 is right where a `text`
  goes. Its advice is not. "test it first" (LumaLinkText.tsv:469) leads to `if x != none { ... x ... }`, which stays
  S0067 (counter-example 3). M-119.
- S0083 for a bare `vec3`:
  - The docs use it as a zero value: functions-structs-and-enums.md:28 (`vel: vec3 = vec3`), types.md:32
    (`let v = vec3`), values-and-memory.md:27 (`k.mVelocity = vec3`).
  - S0053 then follows from the obvious fix, `vec3()`. vec3 takes exactly 3 arguments (call.rs:926, 878-881), while
    a mod struct's `T()` is allowed (call.rs:494-501).
- Wrong places: every postfix node of a chain keeps the position of the chain's first word (parse.rs:1337, 1344,
  1349, 1359, 1362). An error about `.b` in `a(x).b.c`, or about a call's argument count, points at the start of
  `a`, on the chain's first line. M-121.

## Findings

#### M-118 wrong behaviour: a `T?` given to a game pointer parameter the self-description marks may-be-null is refused with S0067
- Evidence:
  - call.rs:261-266: game_param drops the parameter's `?` and returns `nullable` beside it.
  - call.rs:1618-1639: only a literal `none` (probe Open, 1621) takes the nullable path. Any other `T?` goes to
    check_fit against the bare `T` (1636-1638), which is S0067.
  - The same parameter is typed `T?` when a hook or override receives it (body.rs:1417-1418, 1157-1158).
  - detailed-rules.md:304-310 says such a parameter takes a `T?`.
- Why: a hook that receives `p: X?` and passes `p` on to the same may-be-null parameter is refused. So is any value
  that may be none, such as a `ref.get()` result. The modder must write `if let` and duplicate the call for the
  `none` case.
- Confidence: verified in code. A compiler test would show it: pass a `text?` local as the second argument of
  `LiveActor::initModelManagerWithAnm`.

#### M-119 docs wrong (message): S0067 tells the modder to "test it first", but a test does not narrow a value; for an optional service the message names a type that does not exist
- Evidence:
  - LumaLinkText.tsv:469 has the message "a `{found}?` where a `{found}` goes: test it first, or give a value with
    `??`".
  - Only `if let`, `guard let`, `??`, `!` and a non-name `match` pattern give the `T`. `if x != none` gives `Bool`
    and changes nothing (expr.rs:1168-1195; body.rs:2316-2324 narrows optional services only).
  - The docs quote two other texts for the same error: detailed-rules.md:309-310 "test it first (`if let`) or give
    a value", naming the function and parameter; and :316-317 "`x` may be none: write `{x ?? ...}` or test it with
    `if let`". The message has one argument, `found` (diag.rs:529), so it names neither.
  - call.rs:600-601 fills `found` with "optional svc.<name>". The message then reads "a `optional svc.net?` where a
    `optional svc.net` goes ... give a value with `??`", and a service cannot take `??`.
- Why: the advice leads to code that fails with the same error. This is the first-person report's "suggested fix
  that does not compile".
- Confidence: verified in code and docs.

#### M-120 docs wrong: the reference writes calls without `()` and uses `vec3` alone as a zero vector; the compiler refuses both
- Evidence:
  - Calls without `()`: values-and-memory.md:27 (`target?.get`); tasks.md:15 and 17 (`h.get`); detailed-rules.md:14
    (`r.peers.get`), 125, 321, 324 and 330 (the M-115 lines and `MR::getCurrentStageName ?? ""`).
  - `vec3` alone: functions-structs-and-enums.md:28, types.md:32, values-and-memory.md:27.
  - Compiler side: expr.rs:1884-1886, 1619-1620 and 1408-1416, and call.rs:926.
- Why: each line, copied, is S0083. The `vec3()` fix is S0053. This is the cause of the first-person report's
  S0083 errors. M-115 is a subset of this finding.
- Confidence: verified in code and docs.

#### M-121 wrong behaviour (diagnostics): an error on any part of a postfix chain is placed at the chain's first word
- Evidence: parse.rs:1336-1362. `let at = e.at` is taken from the primary, and every Call, Index, Member, `?.` and
  `!` node built in the loop reuses it. The checker places errors at these nodes: MAY_BE_NONE at base.at
  (expr.rs:1770, call.rs:632), ARG_COUNT at e.at (call.rs:405, 880, 1571), and NO_VALUE.
- Why: in `a(x)\n  .b\n  .c`, an error about `.c`'s base names the line and column of `a`. With one chain per line
  the column is still wrong. This is the first-person report's "wrong place".
- Confidence: verified in code. A compiler test would show it: `foo().bar.baz` with `bar` optional.

#### M-122 missing feature: no zero or default value for the built-in value types, though the docs promise one
- Evidence: call.rs:926-927. vec2, vec3, vec4, quat and color take exactly their component count, and no other
  arm makes a default. A mod struct's `T()` gives its defaults (call.rs:494-501), and the docs write `vec3` as a
  value (M-120).
- Why: either the docs or the language are wrong. Today a modder must write `vec3(0.0, 0.0, 0.0)`.
- Confidence: verified in code.

#### M-123 wrong behaviour (game fault): a script may write `none` into a game object's pointer field, which the game may read without a test
- Evidence:
  - game.rs:1008: a pointer field is `GameTy::Opt(..)` and read-only only when its top level is `const` or it is
    text.
  - body.rs:2434-2435: a GameField member is a place.
  - expr.rs:1584-1596: its type is `T?`, so `none` fits it (expr.rs:424-426).
  - Nothing ties a write to the game's own nullability. The never-null mark is only for reads
    (detailed-rules.md:122-127).
- Why: `actor.mSensor = none`, or a write of an optional that is none, puts null where the game's code assumes an
  object. The next game read faults inside the game, and a game-code fault ends the game.
- Confidence: verified in code on the checker side. The emit side is suspected, needs a run, or a read of
  emit/game.rs game_field_place: a mod writing `none` to a pointer field the game dereferences each frame.

#### M-124 maintainability (C and scripts): an `extern fn` is never compared with the C header it names
- Evidence:
  - check.rs:679-685: `use c` checks only that the header is listed.
  - body.rs:939 skips externs.
  - check.rs:1246-1251 takes the written types.
  - No code in check/ reads a C header. emit.rs:1211 and 1547-1556 write the header and the prototypes.
- Why: a mismatch is found, if at all, by the C compiler. Its message names the generated C, not the `.lum` line.
  Arguments to an extern also escape S0103 (no kept() call), so a script can hand C a view that C keeps.
- Confidence: verified in code for the checker. Whether the C compile sees a conflicting prototype is suspected and
  needs a read of emit.rs:1200-1230 and cside.rs.

## Checked and found correct (Round 9)

- Data cycles:
  - Value cycles are found with a full path and said once per ring (check.rs:1037-1102).
  - Containers, `ref` and functions break them (1460-1469).
- Hook heads:
  - The row is chosen by quoted signature, then parameter count, then written types (body.rs:1458-1521).
  - Ambiguity and no-match are both said with the signatures listed.
- Wrap and replace: the result is given on every path (S0123). `proceed()` once on any path, loops included (S0122,
  body.rs:791-794). `result` is read only once given (S0124).
- `defer`: leaving or waiting inside it is refused (body.rs:840-877).
- Flow: `wait` is allowed only in tasks, `on start`, stage, option and event handlers, `enter` and state bodies
  (body.rs:1806, 1907-1921). `update` and `exit` keep no view made before them (S0103 Keep.State, body.rs:729-731).
- Literals:
  - A literal fits its place, with an exact float bound for integers (2^24, 2^53) (expr.rs:617-668).
  - Literal expressions are folded and checked once (expr.rs:1233-1257).
- Match coverage is Maranget's usefulness algorithm. Game enum aliases count once (pat.rs:400-406, 433-483).
- Overloads of game calls:
  - Viable rows are chosen first, then exact ones, then by receiver const (call.rs:1544-1564).
  - A literal argument is checked after the choice (1628-1630).
- Constness:
  - Const views stay const through fields (expr.rs:1689-1691) and calls (call.rs:1610-1612).
  - A copy is never passed where the game's object goes, except to a `const T &` of the same record (call.rs:1631-1635).
- ctype.rs:
  - It refuses volatile pointers and member functions at the word.
  - Builtin spellings are canonical.
  - A text that does not read names its byte (ctype.rs:279-288, 59-110, 664-680).
  - Its whole-game read test is `#[ignore]` and needs LUMA_ROWS or LUMA_TYPES (684-686), so no ordinary test run
    covers the game's real texts.

## Point 2, 3 and 4: not reached

- Point 2: the hook entry path and the cost of one hook call and of an empty `on frame` were not reached.
  - Files: emit.rs (rest), emit/expr.rs (rest), emit/flow.rs (rest).
  - The Round 8 note stands: tasks and `on frame` run in BOOT and LOADING too.
- Point 3: not started. Files: emit/closure.rs, states.rs, json.rs, class.rs (rest), library.rs, cside.rs,
  service.rs (rest), svc.rs (rest).
- Point 4: not started. Files: rt/lumart.c (rest), lumart.h.
- Point 5: only the error causes were reached, through the docs and parse.rs:1320-1362.
  - Files: lex.rs, parse.rs (rest), ast.rs, lumi.rs, fmt.rs, lib.rs.
  - The .lumi format and its version check were not read.
- Point 1 was left at a file boundary. game.rs (rest: 130-866, 950-1168) was not read this round.

# Round 10

Trees as before. Read completely this round, in order: emit.rs (1-2391), emit/expr.rs (1-3193), emit/flow.rs (1-1017).
For the cost path also: native/include/luma_hook.h (1-205), LumaHook.cpp (run, proceed, enterBody, callHook, Hold),
LumaGuard.cpp (run, ticks, charge), LumaTime.cpp (fill 376-389, callHandler 401-420, beginStep 1055-1074,
frameHandlers 1109-1125), LumaServices.cpp (recordOf 238-249, kernelCall 398-421), lumart.c:3743-3748, game.rs 866-1168.

## Cost path, as counts

### A. A game function with no hook
- 1 load of the function's flag byte and 1 branch (luma_hook.h:162). Nothing else.

### B. One call of game function F that one mod hooks `before` (one body), F called by game code
1. F's entry check (luma_hook.h:162-169):
   - flag load and branch;
   - 1 TLS read of tBody, compare, branch;
   - a call of the cold lambda, which fills `void* args[1 + params]` on the stack.
2. dispatch<R> (luma_hook.h:125-129):
   - Result<R> built: for a plain R, 1 memset of sizeof(R) and 1 default construction (68-74);
   - 1 call of luma::run.
3. luma::run (LumaHook.cpp:527-574):
   - Hold on the game thread: 1 TLS read, 1 chain load, gameUsers++ (207-237);
   - the befores loop: 1 callHook.
4. callHook (477-488): the owner is a mod, so 1 call of guard::run (LUMA_GUARD_BEFORE).
5. guard::run (LumaGuard.cpp:440-498):
   - result zeroed;
   - 1 atomic load (faulted);
   - TLS tThread read;
   - tNestedTicks saved and zeroed;
   - 2 calls into LumaLoader.cpp (modId, modCode, 1427-1434);
   - 2 signal fences;
   - frame push;
   - 2 clock reads (rdtsc or cntvct_el0, 274-280);
   - 1 call through the luma_guard_call trampoline;
   - charge: 1 call and 1 relaxed atomic fetch_add (324-333);
   - frame pop.
6. The mod's group wrapper (emit.rs:1971-1976):
   - lumart_on_game_thread: 1 call, then 1 indirect call luma_rt_core->game_thread, then 1 TLS read (lumart.c:3743-3744);
   - the re-entry flag: load, branch, store;
   - per body: 1 direct call and 1 branch on `skip`;
   - flag store.
7. The body (Fx::function, emit.rs:2236-2294):
   - `lm_st = 0`;
   - 1 zeroing store per declared local and temporary (`= {0}`, structs included);
   - 1 init per text builder;
   - 1 scratch mark when the body builds a text;
   - per named `const char *` parameter (emit.rs:1386-1406): 1 read, 1 lumart_text_from_game (an allocation and a copy of
     the bytes, done even when the body never reads the name), and 1 lm_cp (a count taken);
   - the statements;
   - on the way out:
     - 1 lm_rel per counted declaration (2 per `const char *` parameter);
     - 1 lumart_sb_free per builder;
     - 1 scratch release;
     - for a before with text parameters, 1 lumart_text_eq per parameter (text_args_back, emit.rs:2211-2229).
8. No skip, so proceed (LumaHook.cpp:504-525) finds no wrap and calls enterBody (496-501):
   - tBody saved and set (TLS write);
   - 1 indirect call of the row's invoke: a kAskKind compare, then Result::put calls F;
   - F's entry check runs a second time: flag load, branch, TLS read, TLS write of null; then F's own code runs;
   - tBody restored (TLS write).
9. The afters loop is empty. Hold ends (gameUsers--). take() moves the result and destroys the slot.

Totals for that one call, outside F's own code and the script's statements:
- 2 clock reads;
- 1 atomic read-modify-write;
- 3 indirect calls (the trampoline into the wrapper, game_thread, invoke);
- about 9 direct non-inline calls (cold lambda, run, guard::run, modId, modCode, charge, lumart_on_game_thread, the
  body, enterBody);
- F's entry check twice;
- 0 heap allocations in the kernel or the wrapper;
- k allocations in the body, k being the named `const char *` parameters (M-128).

Each further mod hooking F `before` adds one guard::run and one group wrapper (step 4 to 7). Each further `before` body
of the same mod adds one direct call (the group, step 6).

`resultKind` (LumaHook.cpp:326-333, one more indirect invoke) runs only on a skip, or when F's own code did not run.

### C. An empty `on frame`, per frame, per mod that has one
1. frameHandlers (LumaTime.cpp:1109-1125), once a frame for all mods:
   - fill LumaTimeNow: 13 stores and 1 atomic load;
   - copy of the frame order.
2. Per mod:
   - `gone` and `frameFn` tested;
   - callHandler: 2 small structs filled (413-420).
3. kernelCall (LumaServices.cpp:398-421):
   - 1 TLS read (onGameThread);
   - 2 null tests;
   - recordOf: a linear scan over every mod record, 1 acquire load each (238-249);
   - isGone: 1 acquire load;
   - 1 nebula_snprintf of "%s event %u" into a 96-byte stack buffer (415).
4. guard::run, as in B step 5: 2 clock reads, 1 atomic add, 2 calls, the trampoline.
5. handlerTrampoline: 1 indirect call, into lm_frame (emit.rs:1715-1742).
6. lm_frame makes 1 direct call per `on frame` handler. The empty handler sets lm_st and returns, with no locals.
7. Only when the mod uses tasks: lumart_tasks_pass and lumart_tasks_run, 2 more calls (1719, 1740).
8. Only when the mod has `on stage`:
   - a branch every frame;
   - on PLAY frames after a load, until the opening ends: 2 game-function invokes per frame (1726-1733).

Totals: 0 allocations; 2 clock reads, 1 snprintf and 1 scan over all mod records per mod per frame (M-129). An `on step`
costs the same, once per world step (beginStep, LumaTime.cpp:1055-1074).

### What in the emitted C allocates per call
- Each named `const char *` hook or override parameter: one text per call (emit.rs:1398).
- A game call taking `const char *` that is not marked `in`: lumart_text_keep per call, kept until the mod unloads,
  past a stop (expr.rs:1366-1383; lumart.c:3918-3959; M-114).
- A text with interpolations (expr.rs:2512-2518):
  - built in the scratch;
  - then one allocation of its size: one block.
- A game call's `const char *` result: one text (expr.rs:1495-1504).
- A game record by value: one block per result or copy (expr.rs:1468-1472, 83-86).
- `[...]`, `{...}` literals: one block plus growth.
- list.map/filter/indexed, keys/values, text methods (lower, upper, slice, chars, split, bytes): one block each.
- json.encode/decode.
- `ref(x)`: a table slot (expr.rs:416-426).
- Not allocating:
  - numbers;
  - vectors;
  - text literals (static, LUMART_FOREVER, emit.rs:1240-1256);
  - log and fail lines (scratch, given back at once, flow.rs:192-214);
  - comparisons;
  - a game call with scalar arguments;
  - transient `in` texts (scratch, released after the call, expr.rs:1336-1363, 1483-1485).

## Findings

#### M-125 docs wrong: `hook ... every override of` is documented and checked, but the back end refuses it
- Evidence:
  - emit.rs:916-918: `if h.every_override { self.say(m, h.at, "hook ... every override of"); continue; }`. The
    statement is "not written yet" (S0800), so every mod using it fails to build.
  - The checker accepts it and lists the overrides (check/body.rs:1319).
  - The docs present it as working:
    - docs/modding/reference/lumascript/hooks.md:43;
    - compiled-form.md:97 ("one row per override the self-description lists");
    - detailed-rules.md:37, 81, 335.
  - Two corpus programs use it: tests/data/programs/038-A.7.13-L1269.lum:3 and 046-D-L1470.lum:3.
- Effect: M-117 counted one in-game suite using `every override`. That suite cannot build on this tree.
- Why: any `hook`/`wrap ... every override of`.
- Confidence: verified in code. Whether emit tests expect the S0800 for those two programs: needs `cargo test -p
  lumascript --test emit` (not run).

#### M-126 crash: `skip <text>` in a before hook of a `const char *` or `const char16_t *` function hands the game the runtime's block, not characters
- Evidence:
  - flow.rs:229-239: `skip v` writes `LUMA_RESULT(call, {rc}) = {x};` with x the script's value as it is. There is no
    coercion to `text?` and no kept copy.
  - For these functions result_c is "void*" or "const uint16_t*" (emit.rs:1358-1359).
  - The checker accepts any value of the result's type (check/body.rs:2126-2128).
  - The assignment path does it right: `result = t` makes the kept copy, lumart_text_keep or lumart_text_keep16
    (flow.rs:327-351).
- Effect, by the value written:
  - A literal: a pointer to the static lumart_tb. The game reads the block's count bytes (0xFFFFFFFF) as characters.
  - An interpolated text: a pointer to a block the body releases on its way out. The game reads freed memory.
  - A `text?` value: a struct assigned to a pointer, so the C does not compile (an internal error, not a script
    diagnostic).
- Why: `hook before MR::getSomeName() { skip "x" }` on any function returning `const char *`.
- Confidence: verified in code for the emitted line. The C result and the run: suspected, needs an emit_small test with
  that hook, compiled, and one run.

#### M-127 wrong behaviour (C does not compile): reading `result`, or an after hook's named result, of a `const char *` function
- Evidence:
  - The script type of such a result is `text?` (game.rs:775, 980-983).
  - Its C variable is `void*`:
    - `result` read: `fx.temp(rc, LUMA_RESULT(call, rc))` with rc "void*" (expr.rs:197-199, emit.rs:1359);
    - an after's `-> r`: `void* l<id> = LUMA_RESULT(...)` (emit.rs:1425-1431).
  - Every use as a `text?` writes `.some`/`.value` on that `void*`, or passes it where a struct goes.
  - The only test with such a result names it and never reads it (emit_small.rs:1222-1224).
- Why: `hook after MR::getSomeName() -> r { log r }`.
- Confidence: suspected, needs that hook through emit and the C compiler.

#### M-128 maintainability (cost): each hook body copies every named `const char *` parameter on every call
- Evidence:
  - emit.rs:1386-1406: at the body's start, for each named `const char *` parameter:
    - lumart_text_from_game (an allocation and a copy);
    - a second count (lm_cp into `was`);
    - at the end, two releases and a text compare.
  - This runs whether or not the body reads the parameter.
- Why: a frequent function with a name parameter, hooked by a body that looks only at other parameters.
- Fix shape: read the text on first use, or only when the body names it.
- Confidence: verified in code.

#### M-129 maintainability (cost): every service event call formats a string and scans every mod record
- Evidence:
  - LumaServices.cpp:414-415, nebula_snprintf of "%s event %u", runs on every kernelCall.
  - recordOf (238-249) is a linear scan with an acquire load per record.
  - kernelCall is the path of every `on frame` and `on step` handler (LumaTime.cpp:413-420, 1065-1072, 1116-1123),
    and of every timer, input, stream and service callback.
- Effect: with N mods each having `on frame`, that is N formatted prints and N^2 record loads per frame.
- Fix shape: keep the record index in the clock or the event node, and build the place text only when the guard
  reports a fault.
- Confidence: verified in code.

#### M-130 wrong behaviour: JSON of a map keyed by a game enum writes a bare number as an object key
- Evidence:
  - container_text (expr.rs:2457) does not quote a game enum key, relying on value_text to do it.
  - value_text quotes only a named value: named_value_text writes `lumart_sb_i64` unquoted for a number with no name
    (expr.rs:2827-2829, 3005).
  - So `{5: ...}` is written, which is not JSON.
- Why: `json.encode(m)` or `log fact k m` where m is `map<SomeGameEnum, T>` and holds a value the enum does not name.
- Confidence: verified in code.

#### M-131 wrong behaviour: `clamp(v, lo, hi)` with lo > hi gives lo for every v below hi, with no report
- Evidence: expr.rs:1745-1748: `v < lo ? lo : (v > hi ? hi : v)`, with no check that lo <= hi.
- Why: a script passing its bounds the wrong way round.
- Confidence: verified in code. Low weight. The docs' rule for this case was not checked.

## Checked and found correct (round 10, so far)
- Integer arithmetic is checked:
  - add, sub and mul through __builtin_*_overflow;
  - div and rem by zero, and MIN / -1;
  - shift counts below 0 or past the width;
  - unary minus.
  - (expr.rs:272-281, 595-638)
- int_fits covers every sign and width pair (emit.rs:2321-2342).
- Float-to-integer bounds are open and exact, with NaN refused (emit.rs:2345-2357; expr.rs:701-712).
- `and`, `or` and `??` read their right side only when needed (expr.rs:294-336). `?.` writes none each time, so a loop
  sees no stale value (expr.rs:263-269).
- These are checked:
  - list index (expr.rs:1988);
  - insert and remove (2218, 2224);
  - map key, S0907 (2030-2044);
  - matrix row and column (2620);
  - table index (1537).
- Counted temporaries are given back at each statement's end (flow.rs:107-113; emit.rs:2127-2132). The way out releases
  every counted declaration after the defers, and frees builders and the scratch (emit.rs:2271-2294).
- Defers run once each, on a block's end, break, continue, return and script error (flow.rs:38-85, 167-174).
- `for a..=b` reaches the type's last value without overflow (flow.rs:861-868). `for x in xs` holds the container, so
  a write in the body copies it (flow.rs:944-955).
- A game call checks its argument count (expr.rs:1238). A transient `in` text is given back after the call
  (expr.rs:1483-1485).
- `normal` of a zero vector is zero (expr.rs:1785-1788).
- Text and json map keys are quoted for every key type except a game enum with no name for the value (M-130).
- An after hook that ends in a script error restores the result it found (emit.rs:1984). A wrap that ends in an error
  before proceeding proceeds (emit.rs:2001-2008).

### M-132 maintainability (cost): a mod holding any live `ref` keeps two before hooks on every game free and every NameObj destruction
- Evidence: lumart.c:2306-2328 (the watches installed with the first handle), 2361-2366, 2387-2425 (refs_forget: the
  game-thread check through core, a CAS spin lock, a hash probe); the watches are removed only when refs_live reaches 0.
- Why: while one handle lives, each NebulaArena::free and each NameObj::~NameObj runs luma::run, guard::run (2 clock
  reads) and refs_forget, per mod that holds handles. A mod that keeps one `ref` for its whole life pays this on every
  free in the game.
- Confidence: verified in code.

### M-133 crash: a class override keeps the object's data pointer across `base.f()`; if the base call destroys the object, later field reads and writes use freed memory
- Evidence: emit/class.rs:325-328 (`lm_d` read once at the start of the override), 341-387 (`base.f()` proceeds through
  lm_next), 372-375 (fields read through the same `lm_d` after it); `gone` releases the class data (class.rs, the
  `gone` function). No re-entry flag and no recheck after the base call.
- Why: an override of a function whose game body can kill or free the object (a death or a `kill` path) that reads a
  field after `base.f()`.
- Confidence: suspected, needs a run: a class with an override of a function that destroys the object, with a field
  read after `base.f()`.

### M-134 wrong behaviour: tasks started by `on stop` have their way-outs run inside lumart_end, after luma_finish
- Evidence: emit.rs:1832-1911 (lm_stop calls lumart_tasks_stop_all before each `on stop`; lm_finish is luma_finish()
  then lumart_end()); lumart.c:3920 (lumart_end stops tasks first, running their way-outs, which are script code).
- Why: an `on stop` that starts a task. Its way-out then runs after the core's finish, against the comment that
  nothing of the mod runs after it; a service call in that way-out goes to a mod the core has finished.
- Confidence: verified in code; the effect of a service call there needs a run.

### M-135 crash: a library function that returns its own struct or enum writes its own size into the caller's value; a struct that gained a field overruns the older caller's
- Evidence: library.rs:293-304 (cross_result gives Cross::Struct, Cross::Enum and plain Deep the caller's out pointer
  as is), 438-439 and 524-525 (the export passes `lm_out` straight to the function), 784-789 (the caller passes its own
  `&r`); lumi.rs:74-81 and library.rs:330-357 (the signature hashes type names, not layouts); compiled-form.md:281-283
  and 293-295 (a struct gaining a field keeps every hash; fields are only ever added at the end).
- Why: library 1.1 adds a field at the end of a struct a function returns; a dependent built against 1.0 binds (same
  hash) and the export writes the larger struct into the caller's smaller stack value. The parameter direction is
  guarded (memcpy of the smaller size, library.rs:487); the result direction is not. The same holds for an enum
  parameter: library.rs:515 copies the library's case size from the caller's payload, reading past an older caller's.
- Confidence: verified in code; the overrun needs a run with an older dependent.

### M-136 crash: a library keeps a dependent's closure past that mod's stop; the generated dependent_gone drops nothing
- Evidence: library.rs:213 (a function value crosses shared: its count taken, its code and drop the maker's);
  emit.rs:1899-1906 (lm_dependent_gone only calls `on mods.stopped` handlers, and is only written when the mod has
  both such handlers and exports); LumaLoader.cpp:1309-1318 (dependent_gone is called after the dependent's stop);
  compiled-form.md:305-306 ("The library drops a mod's closures in `dependent_gone`, so no closure is called after
  that mod's `stop`").
- Why: a library stores a callback a dependent passed (a list of listeners) and calls it each frame. When the
  dependent is turned off or reloaded, its DLL and pool pages go (lumart_end frees the pages); the library's next call,
  or its final release, runs code and frees memory of an unloaded mod. Only a library author who writes
  `on mods.stopped` and clears the list by hand avoids it.
- Confidence: verified in code (nothing drops the closures); the crash needs a run: a library holding a dependent's
  closure, the dependent turned off in game.

### M-137 maintainability (memory, cost): every distinct closure given to a service is kept until unload, and each keep scans the whole table
- Evidence: lumart.c:3020-3043 (fn_kept: linear search for the same pointer, grows by doubling, freed only in
  lumart_end); emit/service.rs:11-13, 217, 1051, 1093, 1285 (each callback and each request with a closure calls
  lumart_fn_keep).
- Why: a capturing closure is a new block each time it is made (closure.rs). A mod that sends a request with
  `fn(r) { ... uses x }` each frame keeps one block, and everything it captured, per request until the mod unloads,
  and each new keep costs a scan of all earlier ones.
- Confidence: verified in code.

### M-138 wrong behaviour: a struct a caller passes to a newer library gets zero, not the defaults, in the fields the caller does not have
- Evidence: library.rs:485-487 ("what the caller has, the rest left zero"); compiled-form.md:293-295 ("The library
  reads the fields within that size and takes the defaults for the rest").
- Why: library 1.1 adds `speed: f32 = 1.0` to a struct parameter; a 1.0 caller's call gives the library 0.0.
- Confidence: verified in code.

### M-139 docs wrong (low): `keep()` logs "more than 1 MiB as json" when the core refused the value for another reason
- Evidence: emit/json.rs:243-246 (one message for both `n > 1 MiB` and `core->keep(...) != 0`); the doc comment at
  238-239 names both causes.
- Confidence: verified in code.

### M-140 crash (low): an `export c fn` runs script code on whatever thread the mod's C calls it from
- Evidence: emit/cside.rs:140-213 (the C wrapper copies texts and calls the script's function; no
  lumart_on_game_thread check); lumart's pools, scratch and tables have no locks (lumart.c 1-260).
- Why: a mod's C that calls its exported script function from a thread it made (an audio callback, a worker).
- Confidence: verified in code that nothing checks; whether the docs forbid it was not checked.

### Round 10: what json accepts from untrusted input (emit/json.rs all; lumart.c 1401-1660)
- json.decode<T>, kept<T>() and json.Value methods read through one reader. Depth: lumart_jr_open refuses past
  JR_DEPTH (256) levels (lumart.c:1598), and a struct holding a list of itself recurses one C reader call per level,
  so the C stack is bounded by that. Strings: UTF-8 checked, surrogate pairs checked, a lone surrogate refused
  (1569-1587); a field key that is a lone surrogate names no field and is skipped (1631-1641).
- No limit on element count or document size beyond the input's length; memory is bounded by what the input can
  build. kept() data is at most 1 MiB (keep_value refuses more, json.rs:244).
- Duplicate keys: the last wins for maps and for struct fields (the earlier value released, json.rs:515-520).
  Unknown fields are skipped; a missing field takes its default, else the value is refused (json.rs:529-552).
- Integers are range checked per type (json.rs:319-329); a colour's channels must be 0..1 (353-355); a game enum
  reads by name only (474-481); trailing bytes other than white space make the decode none (json.rs:277).
- A failed decode releases what it read in part (json.rs:278-282). Reused text places are released before each
  write (lumart_text_make, lumart.c:671-681), so per-element reads do not leak.

### Round 10: what happens to a task
- When its stage ends: nothing. lumart_tasks_run resumes every task once per frame from lm_frame with no stage check
  (lumart.c:3454-3707), so tasks run through stage changes and loading (M-014 stands).
- When its mod stops: lm_stop runs lumart_tasks_stop_all before `on stop` (8 rounds of stop, each running way-outs);
  a frame left after that is freed without its way-out and logged ("a task did not end when the mod stopped").
  Tasks started by `on stop` are stopped in lumart_end, after luma_finish (M-134).
- When the object it waits on dies: a view across a wait is refused (S0104) and a closure capturing a view is refused
  (S0103); a `ref` becomes none through the watches (M-132); a state machine's parked frame is released by its
  object's `_gone`, with no script code run (states.rs).

### Round 10: kept until the mod stops or unloads (complete list for the compiler and lumart)
Kept until unload (lm_finish, lumart_end, lumart.c:3918-3959), surviving a stop and a start in the same load:
- lumart_text_keep's keep table (M-114): every distinct text given to a game function that keeps a `const char*`.
- lumart_text_keep16's copies (k16).
- fn_kept: every closure given to a service callback or request (M-137).
- The handle slot table and its hash: they grow, never shrink.
- Pool pages and free lists: a freed block goes back to its class's free list, never to the core.
- The task slot and order arrays.
Given back at stop (lm_stop, emit.rs:1832-1896):
- The scratch chunks (lumart_scratch_free_all).
- Task results not read.
- Counted module globals and option values (lm_rel).
Never given back (static in the library):
- LUMART_FOREVER blocks: text literals, closures with no captures, functions used as values.
- Message row texts hold their message while the row lives.
Held by others:
- A service keeps a callback until it is replaced or the mod ends (service.rs SParam::Callback).
- `keep()` data: held by the core across a reload, at most 1 MiB.
- A closure passed to a library: held by the library until it releases it, with no drop at the mod's stop (M-136).

Checked in round 10 and found correct, part 2:
- JSON reader depth and recursion bounds, string checks and partial-value release (above).
- Copy on write for lists and maps (list_own, map_own); the map is a sorted array with binary search; sorting is a
  merge sort.
- Handle slots and task slots carry generations: an old handle or task id never reaches a reused slot.
- parse_i64 is overflow checked; parse_span keeps 800 digits and is exact.
- Number literals in JSON: integers exact to u128, floats as the shortest round trip; a colour literal is 6 or 8 hex
  digits only (lex.rs:268-278), so json.rs:178-182 never reads a short one.
- The lexer and parser: MAX_DEPTH 512 (lib.rs:24) on brackets, blocks, operator chains, types, patterns and
  interpolations (parse.rs:88-120), S0022 past it; every pass runs on a 64 MiB thread (lib.rs:26-47; lumalink-core
  build.rs:613). Recovery always moves forward: parse.rs:243-271 skips to the line end with brackets matched and
  takes one token when nothing moved, so a stray `}` cannot loop.
- The .lumi file: the runtime layout is in its header (lumi.rs:95, layout_of 175-186) and in every signature
  (lumi.rs:70-81), so two different lumart layouts never bind. A struct's layout is not in either (M-135).
- Library deep values: a deep parameter of another size is refused (library.rs:491-498); an enum case past the
  library's count is refused (500-511); a handle crosses as its object, made again in the receiver's table (203-209).
- `extern c fn`: a `var` parameter is refused; text and bytes go as pointer and length for the call only
  (cside.rs:68-135). `export c fn` releases its text copies after the call (cside.rs:203-205).

Not read in round 10: emit/service.rs other than 1-60 and 200-320 and the keep lines; svc.rs; game.rs 130-866 and
950-1168 (less 989-1035); lex.rs other than 262-281 and the position lines; parse.rs other than 1-349 and the depth
lines; ast.rs; fmt.rs.

## Area 7 (round 10, started only)

### M-141 maintainability (low): `pack` leaves `<id>-<version>.luma.part` beside the folder when writing or the second check fails with an error (a full disk)
- Evidence: lumalink-core jobs.rs:358-361 (`luma_file::write(...)?` and `validate_as(...)?` return early);
  luma_file.rs:88 (File::create, with no removal on error). The file is removed only when the second check runs and
  refuses it (jobs.rs:362-364).
- Confidence: verified in code.

The two producer/refuser pairs from the first-person report, in this tree:
- `add need <id>` with no version writes `"*"` (lumalink main.rs:726). Range::parse accepts `*` (luma-validate
  semver.rs:128; its test at 245), and add.rs:362 checks the range with the same parser before writing. Not refused
  any more by reading. The unit test still asks for `>=1.2.0` against LumaKit 1.0.0 (add.rs:598, 610): M-035 stands.
- `new` then `pack` and L300: `new` writes luma.json with no `layers` key and makes an empty layers/base/files
  (jobs.rs:292-319). With no `layers` key the validator lists `base` only when layers/base exists (manifest.rs:1063-1069),
  and layers.rs:161-170 then finds its folder, so the folder check gives no L300. The packed file has no empty
  folders, so it lists no layer and has none: no L300 either. Looks fixed by reading; needs a run (`lumalink new`
  then `pack`) to confirm that no other rule refuses an empty mod.

Not read in Area 7: new/add/pack/build/test/convert/update/verify bodies beyond the lines in the coverage row,
lumalink-core's other files, crates/lumalink/src beyond the ranges, lumalink-gui, lumalink-setup. Behaviour with no
installed game, no toolchain or a full disk was checked only for `pack` (M-141).

# Round 11

Round 11 read the build driver, the .luma writer, the game's import and check path, the validator's container and
code rules where they bear on the questions, and the importer's model and audio readers by their limits. The files
not read are listed at the end.

## The nine-platform build (lumalink-core build.rs, all of 1-1018; emit.rs:49-66)

Every platform: `--target=<triple>`, the C flags `-std=c17 -O2 -nostdlib -ffreestanding -fno-stack-protector
-fvisibility=hidden -fno-builtin-malloc -fno-common -Wall -Werror=implicit-function-declaration
-Wframe-larger-than=4096 -Werror=frame-larger-than -fasynchronous-unwind-tables`, `-ffile-prefix-map` for the work
and source folders, `-I` lum, SDK, source; C++ files take `-std=c++20 -fno-exceptions -fno-rtti` instead of
`-std=c17` (build.rs:799). clang must report exactly 22.1.6 (build.rs:697-700).

| platform | triple | code flags | link flags |
|---|---|---|---|
| windows-x64, windows-arm64 | x86_64/aarch64-pc-windows-msvc | (C++ adds -mno-incremental-linker-compatible) | -shared -fuse-ld=lld /NOENTRY /NODEFAULTLIB /NOIMPLIB /Brepro |
| linux-x64, linux-arm64 | x86_64/aarch64-unknown-linux-gnu | -fPIC -fno-use-init-array | -shared lld --build-id=none --hash-style=gnu -z defs -z noexecstack -T {sdk}/luma_ctors.ld |
| android-arm64 | aarch64-linux-android24 | -fPIC -fno-use-init-array -mno-outline-atomics | as Linux plus -z max-page-size=16384 |
| android-x64 | x86_64-linux-android24 | -fPIC -fno-use-init-array | as android-arm64 |
| macos-arm64, macos-x64 | arm64/x86_64-apple-macos12.0 | -fPIC | -dynamiclib lld -undefined error -adhoc_codesign -install_name @rpath/mod.dylib -platform_version macos 12.0 |
| ios-arm64 | arm64-apple-ios15.0 | -fPIC | as macOS with -platform_version ios 15.0 |

- Unwind data: `-fasynchronous-unwind-tables` is in the shared flags (emit.rs:63-65), so every Windows x64 library
  has .pdata/.xdata. M-027's premise holds.
- What an author on Linux or macOS can build: all nine from any host with the bundled clang and the SDK folder (no
  system SDK is used: -nostdlib, -ffreestanding, lld for every format). The exception is M-142.
- Signing and hashing: Mach-O libraries are signed ad hoc by lld; nothing else is signed. The .luma has CRC-32 per
  entry only.

### M-142 wrong behaviour: linux-arm64 is built with outline atomics on a Linux host that has GCC 9.3.1 or newer, so its link fails, and the output depends on the host
- Evidence: build.rs:78, 105 (linux-arm64 takes ELF_CODE, without the `-mno-outline-atomics` that android-arm64 has,
  build.rs:87-88); lumart.c:2346-2355 and 2388 (`__atomic_compare_exchange_n` and others in every mod); build.rs:80
  (`-z defs` turns the `__aarch64_cas4_acq` import into a link error); rules/code.rs:264-276 (L504 would refuse it).
- Why: clang turns outline atomics on for aarch64 Linux when it finds a GCC installation of 9.3.1 or newer, or uses
  compiler-rt (its Linux toolchain driver). A Windows or macOS host has no such GCC and gets inline atomics; a typical
  Linux host gets the helpers. The same sources then give different bytes per host, against compiled-form.md 1.2.
- Confidence: suspected, needs a run: `lumalink build` for linux-arm64 on a Linux host with GCC installed.

### M-143 crash (disk and memory exhaustion): an archive added to the game is unpacked up to 256 GiB before the validator sees it, and an asset up to 4 GiB is read whole into memory
- Evidence: mod_import.cpp:83-84 (256 GiB, 1,000,000 entries), 298-397 (libarchive streams every entry to the
  staging folder; no compression ratio check and no free-space check); native_settings.cpp:3254-3268 (the
  validator runs on the staged folder afterwards); luma-validate rules/container.rs:9-10 and 137-147 (4 GiB a file,
  64 GiB in all, no ratio check); nebula-import cook/beside.rs:86-92 and the whole-file reads in overlay.rs, store.rs
  and others (31 sites).
- Why: a few megabytes of xz or deflate can unpack to tens of gigabytes. The import fills the disk before any rule
  runs. A file within the validator's limits is then read whole by the cook.
- Confidence: the limits and the order are verified in code; the effect needs a run with a crafted archive.

### M-144 wrong behaviour (fails open): when the validator cannot run, import accepts the mod and the start records no refusal
- Evidence: mod_check.cpp:53-55 and 68-71 (no importer library: no validator), 237-243 (state -1: "the check did not
  run", false); nebula-import validate_c.rs:514-518 (a panic anywhere in the Rust validator becomes state -1);
  mod_check.cpp:309 (`ran` stays false, `refused` false); native_settings.cpp:3262-3268 (the import goes on when
  not refused); mod_log.cpp:687-694 and 728 (no kept report: the package is skipped, not refused), 983-992
  (package_refused looks only at checked packages).
- Why: a .luma crafted to hit any panic in the validator (an index or an arithmetic bug) is imported unchecked, and
  at the start it is neither checked nor refused.
- Confidence: verified in code for the import and the refusal list; whether the loader then starts its code needs a
  run with a package the validator cannot finish.

### M-145 crash (memory exhaustion): a small glTF can make the cook allocate gigabytes; only each accessor is limited
- Evidence: nebula-import cook/gltf.rs:151-201 (an accessor of up to 4,194,304 elements; with no bufferView it is
  `vec![0.0; count * width]` from nothing in the file, 197; a byteStride of 0 is taken, 143-148, so a 12-byte view
  serves every element); 1585-1605 (each primitive reads its POSITION, NORMAL and the others again); the refusal list
  of gltf.rs has no total vertex, primitive or mesh count below 2^32 (1876-1877).
- Why: a mesh with many primitives that all point at one zero accessor costs a few bytes each in the file and
  tens of megabytes each in memory, and the shapes are kept until the model is written. The importer library runs in
  the game's process (mod_check.cpp:44-45).
- Confidence: suspected, needs a run with such a glTF.

### M-146 hang: a BRSTM, BCSTM or BFSTM header with a block size of 0 and a large block count loops for billions of iterations
- Evidence: nebula-import cook/modern_audio.rs:203-218 and 244-259 (`blocks` and `block_size` taken from the header
  with no check), 286-307 (`for block in 0..info.blocks` × channels; a block size of 0 gives an empty slice that is
  always in range, so nothing stops the loop).
- Confidence: suspected, needs a run with such a stream.

### M-147 docs wrong (usability): `check` and `pack` check against the installed game, `build` against what `--names` or LUMA_INCLUDE gave, and nothing records which
- Evidence: lumalink main.rs:58-63 (check, pack, install: resolved from the installed game), 478 and 86-90 (build:
  `--names` or the installed game); jobs.rs:1463-1466 (the SDK: `--sdk`, then LUMA_INCLUDE, then names/sdk, so the
  headers and the names can come from two games); validator-rules.md:109 (L505 "the game has no {kind}" names neither
  the game checked nor its version).
- Why: the balloon-world run: built against newer names, checked by default against an older installed game, nine
  L505 errors that do not say "the installed game is older than the one this mod was built for". Neither luma.json nor
  the library records the self-description the code was built against, so check cannot say it.
- Confidence: verified in code.

### M-148 maintainability (low): `build` rewrites luma.json in place, and silently does not record a platform for a luma.json with a byte order mark
- Evidence: build.rs:857-866 (`std::fs::write` over the manifest; an interrupted run can leave it cut short),
  875 (serde_json refuses a leading BOM, so `with_platform` gives None and nothing is said).
- Confidence: verified in code.

### M-149 maintainability (low): an SDK path that contains a comma breaks every ELF link
- Evidence: build.rs:80, 84 and 785 (`-Wl,-T,{sdk}/luma_ctors.ld`: `-Wl,` splits its argument at commas).
- Confidence: verified in code.

## Answer: crafted .luma files

- The game unpacks an added archive with libarchive (zip, 7z, RAR, tar, xz, a native C library), skipping links,
  absolute and `..` names and OS clutter (mod_import.cpp:78-89, 353-357), then validates the staged folder with the
  same Rust validator LumaLink uses (nebula_import's luma_validate). Rust and the game do not read one archive two
  ways: the validator reads the folder libarchive wrote. As a result, an archive's duplicate names collapse to the
  last one written (case-insensitively on Windows) before the validator can refuse them (L103), and the outcome
  differs by platform.
- Can a crafted package pass and then hurt the game: yes, by size (M-143), by an allocation pattern in the glTF cook
  (M-145), by a loop in the stream reader (M-146), and by any validator panic, which fails open (M-144).
- What parses untrusted bytes:
  - archives: libarchive, C, 256 GiB and 1,000,000 entries;
  - the validator: Rust; names, sizes and CRCs bounds checked;
  - glTF: Rust, serde_json plus own code; accessors 4M elements, images 16384 a side, cycles refused, clips 65535
    frames;
  - PNG: the png crate; JPEG: zune-jpeg with a 16384 limit;
  - OGG: lewton; FLAC and Nintendo streams: own Rust code;
  - fonts: ttf-parser;
  - movies: own Rust (movie_cook.rs, thp.rs; a THP audio record is limited to 0x10000 samples).
  Rust's bounds checks turn most malformed input into a refusal or a panic, and a panic is M-144.
- The binary rule (L503, L504, L509, L510, rules/code.rs:246-281):
  - What it stops: a library that imports anything from the system, needs a library, has load or unload
    constructors, or lacks luma_mod.
  - What it does not stop: code that finds system functions at run time (a PEB walk, raw system calls), or that
    uses whatever the game passes it. It is a check of the import tables, not a sandbox.
- validator-rules.md and the code: every L code in the page's tables is raised in luma-validate or by the cook (L804
  through lib.rs:577; L702 at kit.rs:126), and no raised code is missing from the page (a scripted compare plus the
  three hand checks).

Checked in round 11 and found correct:
- The .luma writer is deterministic. It writes luma.json first, then names by bytes, with no folder entries, a fixed
  date, no attributes and fixed "made by", and deflate level 9 from the pinned miniz_oxide. Links and non-UTF-8
  names are refused by name, and a file that changes while it is read is an error (luma_file.rs:1-131). PE /Brepro,
  -mno-incremental-linker-compatible, --build-id=none and the prefix maps keep the libraries deterministic.
- A needed mod's shipped .luma is unpacked only when every name is a plain relative path, into its own folder
  first, then renamed (build.rs:430-455).
- `build` deletes a platform's old library before building it, so a failed platform never leaves an earlier one in
  place (build.rs:778-781). It writes only `code/`, `code/<id>.lumi` and luma.json's `code.platforms` in the mod folder.
- Validator container (rules/container.rs, all):
  - names of at most 240 bytes;
  - Windows reserved names, forbidden characters and combining marks refused;
  - duplicates folded with full Unicode lowercasing;
  - a name both file and folder refused;
  - unknown top-level names refused;
  - overlaps and size and CRC checked on a stream.
- The glTF reader bounds-checks the GLB chunks and every view and accessor before it allocates (gltf.rs:94-140,
  185-188), and refuses node cycles (1244-1246).

Not read in round 11:
- lumalink-core: everything but build.rs, luma_file.rs 1-330, and jobs.rs 8-66, 109-173, 280-388 and 1415-1500
  (that is new, add, test, convert, update, verify, dev and run, installed, game_store, editor, lsp and the rest);
- crates/lumalink/src beyond the main.rs ranges in the coverage row;
- lumalink-setup and lumalink-gui.
- Not answered: `run`/dev reload, `update` trust, no network, interrupted runs other than M-148.
- Area 8:
  - luma-validate json.rs, schema.rs, names.rs, binary.rs, saved.rs, lib.rs and the rules files other than
    container.rs, layers.rs 120-171 and code.rs 240-290;
  - the importer's cook steps other than the lines named above (sound.rs, flac.rs, font_cook.rs, movie_cook.rs,
    ui_layout.rs, the shader steps).
- Area 5 leftovers: not started.

# Round 12

### M-150 wrong behaviour (fails open, verified): a package the validator cannot finish is started with its code
- Evidence: nebula-import validate_c.rs:514-518 (a panic becomes state -1); mod_log.cpp:687-694 (no kept report when
  the validator fails), 728 (a package with no kept report is skipped), 983-992 (`package_refused` looks only at
  checked packages); host_main.cpp:5113-5124 (`problem` is set only when `package_refused`); mods.cpp:86
  (`mod.enabled && (!mod.package || mod.problem.empty())`) and 1966 (`runs = mod.enabled && mod.luma &&
  (!mod.package || mod.problem.empty())`); host_main.cpp:5541-5553 (the headless import also takes `!found.refused`).
- Why: an unchecked package has an empty `problem`, so mods.cpp:86 and 1966 treat it as allowed and its code runs.
- Fix shape: a package with no checked result gets a problem of its own ("not checked"), and mods.cpp:86 and 1966
  require a checked, passed result rather than the absence of a refusal.
- Confidence: verified in code. This closes M-144 as verified; the run M-144 asked for is no longer needed.

### M-151 crash (memory exhaustion): FLAC and OGG are decoded whole before the length limit is checked; FLAC can reach 1 GiB a channel
- Evidence: nebula-import cook/flac.rs:119 (`MOST_SAMPLES = 1 << 28`), 162-170 (the cap is per channel and checked
  after each frame), 255-290 (up to 8 channels, each up to `block` samples), 396-412 (an escape partition with
  `raw` 0 pushes `count` samples and reads no bits); sound_stream.rs:95-106 and sound_effect.rs:455-466 (the
  20-minute limit is checked on the decoded audio); sound_effect.rs:153 and modern_audio.rs:88 (lewton decodes a
  whole OGG, with no sample cap of ours).
- Why: a FLAC frame of a few bytes can stand for 65,535 samples on each of 8 channels. 2^28 i32 samples a channel is
  1 GiB a channel, 8 GiB in all, before line 170 refuses it, while the 20-minute limit is about 53 million samples.
  Rust aborts the process on a failed allocation, and `catch_unwind` (cook/mod.rs:533, cook_c.rs:88) does not catch
  an abort.
- Fix shape: compute the duration from STREAMINFO and the OGG headers and refuse before decoding; make the FLAC cap
  the duration limit times the rate, not 2^28.
- Confidence: the FLAC numbers are verified in code; the OGG case is suspected and needs a run with a crafted OGG.

### M-152 crash (device lost) or hang: a mod's shader may loop without bound; neither the compiler nor the game limits it
- Evidence: nebula-import cook/post_shader.rs:64 (`for`, `while` and `do` are allowed words), 230-290 (the scan
  refuses other words and `#` lines, but does nothing about loops); mod_shader_compile_d3d12.cpp:98 (DXC with
  `-O3 -WX`, no time limit on the compile).
- Why: `while (true) {}`, or a loop bound read from a texture, runs on the GPU until Windows' TDR resets the device.
  The renderer then has to handle a lost device. A deeply unrolled loop can also make DXC run a long time inside the
  game.
- Confidence: suspected, needs a run with a post shader that loops forever, on D3D12 and on Vulkan
  (vk/shader_compile_vk.cpp is unread).

### M-153 wrong behaviour (data loss): uninstall deletes the whole LUMALINK_DATA folder
- Evidence: lumalink-core update.rs:170-174 (`LUMALINK_DATA` is used as the data folder when it is any absolute
  path); lumalink-setup install.rs:361-363 (`remove_dir_all(data)`, error ignored).
- Why: a person who set LUMALINK_DATA to a folder they also use for other things, such as a synced folder or their
  documents, loses all of it when they uninstall.
- Fix shape: uninstall removes only the files LumaLink writes (settings and the last check) and then the folder when
  it is empty, or keeps its data in a `LumaLink` subfolder of what the variable names.
- Confidence: verified in code.

### M-154 wrong behaviour: a reload asked while another runs is dropped, though the log says it will be asked again
- Evidence: Nebula runtime/src/mod_plugins.cpp:986-990 (`begin_reload` logs "asked again after it" and returns,
  with no queue); 1002-1004 (`reload`, reached by host_main.cpp:4210-4214, the extension call); 1027-1036 (dev
  requests are read only while no reload runs, so they are not lost).
- Why: a mod or the extension calls `reload` for a second mod during a reload. That second reload never happens.
- Confidence: verified in code.

### M-155 wrong behaviour: a mod that cannot be taken out is loaded a second time beside its running copy
- Evidence: mod_plugins.cpp:994-997 (`asked = running && take_out(id)`; take_out is false when the game module has no
  `luma_take_out_request` or the request returns 0, 788-796); 1010 (with `asked` false, the next frame goes straight
  to `load_again`); 884-974 (load_again opens and loads the mod again and pushes a second `Loaded` for the same id;
  it never checks `g_loaded`).
- Why: if the take-out request is refused for a running mod, its old code and its new code both run, each with its
  hooks.
- Confidence: the path is verified in code; whether `luma_load_mod` refuses an id already loaded needs a run (or a
  read of LumaLoader's luma_load_mod) with a take-out request that returns 0.

## Answer: M-144

Verified, not downgraded. See M-150. The loader's start path for a package: `check_packages` (host_main.cpp:5113)
gives a result only for checked packages; a package the validator could not finish has none; `package_refused`
(mod_log.cpp:983-992) is false for it; its `problem` stays empty (host_main.cpp:5115-5118); mods.cpp:86 and 1966 start
its code.

## Answer: the M-145 pattern (allocations sized by values in the file)

- glTF accessors without a bufferView, read again by each primitive: gltf.rs:151-201, 1585-1605. Suspected (M-145).
- FLAC samples up to 2^28 a channel, before the length limit: flac.rs:119-170, 396-412. Verified in code (M-151).
- OGG decoded whole by lewton: sound_effect.rs:153, modern_audio.rs:88. Suspected (M-151).
- Archive entries unpacked before validation: mod_import.cpp:83-84, 298-397. Verified in code (M-143).
- Whole-file reads of assets up to 4 GiB: cook/beside.rs:86-92 and the other 30 sites. Verified in code (M-143).
- Wave archives: audio_data.rs:294-299 (output at most 8 times the input plus 1 MiB). Checked, bounded.
- Images: texture side at most 16384 (pack_texture, gltf MAX_IMAGE_SIDE), at most 32 levels (texture.rs MAX_LEVELS).
  Bounded at about 1 GiB an image. Checked; whether a mod can list many such images at once is not read.
- Layout fields: layout_json.rs:406-409 (`vec![0u8; len]` with a fixed len of 16 or 20; names limited to 16 at
  25 and 126). Not file-driven.
- Not read: sound.rs 329-1481 (the grep sites listed in round 12's notes), sound_stream.rs:58-59,
  sound_effect.rs:238 and 258-269, sound_sequence.rs:110, font_cook.rs:408-411 and 483-504, movie_cook.rs:293,
  350, 436 and 519, ui_layout.rs, layout_anim.rs:55, texture.rs:163-180, 286, 337, 626, 761-813 and 839-877.

## Answer: the M-146 pattern (loops driven by values in the file)

- Nintendo streams with a block size of 0: modern_audio.rs:286-307. Suspected hang (M-146).
- Mod shader loops on the GPU: post_shader.rs:64. Suspected (M-152).
- FLAC escape partitions: flac.rs:403-410. Bounded by the block size and by line 170; an amplification, not a hang
  (M-151).
- Bank chunks: sound.rs:962-971 (`at` grows by at least 8 a step, and each chunk is checked against the file).
  Checked, ends.
- THP: thp.rs:129-170 (at most 16 tracks; frames bounded by the byte length). Checked, ends.
- Shader source scan: post_shader.rs:236-285 (each loop moves `at` forward). Checked, ends.
- Not read: the remaining sites in the M-145 "not read" list above, which are also loops.

## Answer: Area 7 questions

- Dev reload (lumalink-core dev.rs 1-218, all; Nebula mod_plugins.cpp 798-1037):
  - The signal is a file: LumaLink writes `<mods>/.reload/<id>.request.part`, then renames it to `<id>.request`
    (dev.rs:182-189). The game looks every 30 frames, only for ids named with `--mod-dev` (mod_plugins.cpp:1027-1036).
  - The game takes out the mod and the mods that use it, waits up to 600 frames for each to be gone (1010-1022),
    then loads each again from a fresh copy of its library (926, `running_copy`), so the installed file is not held
    open. Permissions are the ones granted at the start (882-883, 947-948). The reloaded code is not validated by the
    game again.
  - The game writes `<id>.result` whole and renames it (845-856, 874-875), and LumaLink reads it (dev.rs:130-153).
  - A build that fails during play: dev.rs:157-162 returns before installing, so the running code stays as it was.
  - If the game never answers (for example, the game was not started with `--mod-dev` for that id), the watch waits
    until the game ends (dev.rs:130-137, 126-129). It has no time limit of its own.
  - Wrong: M-154 and M-155.
- `update`: the download's RSA-SHA256 `.sig` is checked against the release key, and the version must be signed and
  newer (update.rs:1-18). Only https or localhost URLs are taken (117-157). Channel and skipped-version rules are at
  159-166. The check gives up after 15 s (33). What the GUI and the CLI show with no network is not read.
- Setup: the payload is built into the exe (install.rs:36). On Windows it writes registry and Apps entries and the
  HKCU PATH (370-503); on Linux and macOS it edits the shell start files (673-841). Uninstall refuses while a file is
  in use, removes its own files and the folder if empty, then the data folder (343-367; M-153). The Setup download
  itself is not verified by Setup (it carries its payload); the in-app update path is (above).
- M-147 fix shape:
  - `build` records in `code/<id>.lumi` the game id, version and names hash it was built against.
  - `check` and `pack` print, first, which game they check against, and compare it with the record.
  - When the installed game is older, they say so once, in place of nine L505 lines.
  - L505's text names the game and version it checked.
- Not answered (unread): `convert` on Riivolution XML, Gecko codes and texture packs; `test` with missing parts; each
  command with no installed game; every GUI action.

Checked in round 12 and found correct:
- Update trust: signature against the release key, newer signed versions only, https or localhost only
  (update.rs:1-18, 117-166).
- A dev build that fails leaves the running mod untouched (dev.rs:157-162); request and result files are written
  whole and renamed on both sides (dev.rs:182-189, mod_plugins.cpp:845-856); a stale result from an earlier session
  is deleted when a watch starts (dev.rs:108-111).
- The game's reload loads from a copy of the library, not the installed file (mod_plugins.cpp:926).
- Panics in the cook are caught (cook/mod.rs:533, overlay.rs:1083, cook_c.rs:88, jni.rs:20, patch_c.rs:258); only an
  allocation abort escapes them.
- Bank chunk walk, THP frame walk and the post-shader scan all end (above).

Not read in round 12:
- Area 8:
  - luma-validate json.rs, schema.rs, names.rs, binary.rs beyond 1-30, saved.rs, lib.rs, and every rules file other
    than container.rs, layers.rs 120-171, code.rs 200-310, mods.rs 1-60 and manifest.rs ranges;
  - the cook files in the "not read" list of the M-145 answer, gltf.rs beyond the round 11 ranges, the Vulkan shader
    compile (vk/shader_compile_vk.cpp), surface_shader.rs beyond 1-25, post_shader.rs beyond the lines named, fonts
    (font_cook.rs, ttf-parser use), movies (movie_cook.rs), UI layouts (ui_layout.rs, layout_anim.rs).
- Area 7:
  - lumalink-core: new, add, test, convert, verify, installed, game_store, editor, lsp, update.rs beyond the lines
    named;
  - crates/lumalink/src beyond the main.rs ranges in the coverage row;
  - lumalink-gui (all);
  - lumalink-setup beyond install.rs's ranges above.
- Area 5 leftovers: not started.

# Round 13

Method: areas 7 and 8 are about 50,000 lines. This round enumerates the sites by grep and reads each one, rather
than reading every file whole. Each list below says which.

### M-156 crash (memory exhaustion): a mod's sound archive can ask for 64 GiB with one 12-byte command
- Evidence:
  - nebula-import cook/sound.rs:1362-1366: the `ws  ` command's first word becomes a wave bank number, any u32.
  - sound.rs:1402-1403: `vec![None; max number + 1]` of `Option<usize>` (16 bytes each), before any wave bank is read.
  - Reachable from a mod's `AudioRes/SMR.szs`: overlay.rs:594 and mod.rs:341.
- Why: one `ws  ` command with number 0xFFFFFFFE asks for about 64 GiB. Rust aborts the process on a failed
  allocation, and `catch_unwind` does not stop an abort.
- Confidence: verified in code.

### M-157 hang and memory exhaustion: a layout animation (BRLAN) can repeat one key curve billions of times
- Evidence:
  - nebula-import cook/ui_layout.rs:917-960. Content offsets (u16 count), info offsets (u8 count) and target offsets
    (u8 count) are each read from the file and may all point at the same record.
  - Nothing remembers a record already read, and each target's keys are read again and kept.
  - Reachable from any mod file that starts with `RLAN`: mod.rs:333-335 through cook_entry (overlay.rs:526, 551).
- Why: 65,535 content offsets to one content, which has 255 info offsets to one info, which has 255 target offsets to
  one target. That is about 4.3 billion targets from a file of a few hundred kilobytes.
- Confidence: verified in code; the effect needs a run with such a file.

### M-158 crash or hang: nested game archives multiply the 4 GiB member limit
- Evidence:
  - cook/archive.rs:141-142 (MEMBERS_MAX 4 GiB per archive; "entries can share bytes"), 186-196 (each member copied
    and expanded, up to 256 MiB per Yaz0, 26-29).
  - mod.rs:226 (ARCHIVE_DEPTH_MAX 8) and 233-262 (each member that is an archive is walked again).
  - The parent's members and the cooked items of every level stay in memory together.
- Why: an archive whose entries all share one small member that is itself such an archive. Each level stays within
  4 GiB, but the work and the cooked output multiply level by level.
- Confidence: verified in code; the effect needs a run.

### M-159 crash (memory exhaustion), corrects M-151: an Ogg stream given in place of a game stream is decoded with no limit
- Evidence:
  - cook/modern_audio.rs:87-101: `read_ogg` extends the samples until the stream ends. It has no cap.
  - overlay.rs:408-409: a mod's `.ogg` in place of a stream goes to `modern_audio::to_stream`.
  - Correction to M-151: sound_effect.rs:162-173 (`read_ogg` for effects, streams and movies) does stop. Its cap is
    20 minutes at 384 kHz times 2, about 921 million f32 samples (3.7 GB). That is large, but not unbounded.
- Confidence: verified in code.

### M-160 hang: a library mod's data schema can make the check take exponential time, in LumaLink and at every game start
- Evidence:
  - luma-validate schema.rs:149-153 (`passes` checks a branch afresh, with no memo).
  - schema.rs:278-289 (`oneOf` checks every branch).
  - schema.rs:118-137: the cycle check follows only `$ref`, `oneOf` and `anyOf`. A `$ref` reached through `items` or
    `properties` is allowed.
  - json.rs:45: values may nest 64 deep.
  - schema.rs:1-4: schemas come from a library mod and are applied to other mods' kit data, in LumaLink and in the
    game.
- Why: `$defs/x = {"oneOf": [{"items": {"$ref": "#/$defs/x"}}, {"items": {"$ref": "#/$defs/x"}}]}` checked against
  arrays nested 63 deep costs 2^63 branch checks. The start's check (mod_log.cpp) never ends.
- Confidence: verified in code; the effect needs a run.

### M-161 wrong behaviour (fails open, partly): a mod that makes the validator fail only when the game's self-description is given is checked again without it
- Evidence:
  - runtime/src/mod_check.cpp:218-235: when the state is below 0 with names given, the cause is assumed to be the
    install's self-description. The mod is checked again with names, types, services and placement removed.
  - validate_c.rs:514-518: a panic gives state -1.
- Why: a mod that triggers a panic in the code rules (names-dependent) passes the second check with L505, L506, L508
  and part of L403 listed as not checked. L506 is still enforced when the mod runs (LumaLoader.cpp:1047-1064,
  `apiService`). L505 and L508 are not.
- Confidence: verified in code.

### M-162 wrong behaviour, revises M-152: a post program a mod's code registers while running skips the cook's checks; three of four renderers compile it in the game's process with no time limit
- Evidence:
  - runtime/src/render/mod_post.cpp:178-188: the mod's text is appended after the prelude.
  - mod_post.cpp:190-236: only an empty text, a NUL, or an unknown `reads` bit is refused. post_shader.rs's word scan
    is the cook's and does not run here.
  - mod_shader_compile_d3d12.cpp:96-131: D3D12 runs dxc.exe as a child process and kills it after 120 s.
  - d3d11/compile_stage_d3d11.cpp:230-243: D3D11 uses d3dcompiler_47 in the game's process.
  - vk/mod_shader_compile_vk.cpp:27-41 and vk/shader_compile_vk.cpp:448-496: Vulkan uses glslang in the game's
    process.
  - mtl/mod_shader_compile_mtl.cpp:29-39: Metal uses glslang, then SPIRV-Cross.
- Why: a deeply unrolled loop stalls the thread that called `mod_post_register` for as long as the compiler runs.
  A compiled loop that never ends hangs the GPU. Then D3D11 throws at present (renderer_d3d11.cpp:693-696) and D3D12
  throws when a fence never completes (renderer_d3d12.cpp:4659): the game ends with an error.
- Confidence: the paths are verified in code; the GPU effect needs a run with a post program that loops without end.

### M-163 maintainability (low): uninstall deletes every path the install manifest lists, including absolute ones
- Evidence: lumalink-setup install.rs:322-324. Each manifest line has `..` refused, then `folder.join(line)`; an
  absolute line replaces the folder. install.rs:353-358 then removes each file.
- Why: a manifest that was edited or damaged deletes files outside the install folder. Writing the manifest needs the
  person's own rights, so this is not a privilege gain.
- Confidence: verified in code.

### M-164 wrong behaviour (low): `test` counts a test as passed when any one renderer left a pass, and one unreadable result loses every result
- Evidence:
  - lumalink-core jobs.rs:889-893: the doc says a result is needed "for each renderer run". The code only checks
    `!results.is_empty()` and that every present verdict is `pass`.
  - jobs.rs:1116-1118: `test_result(f)?` returns early on the first unreadable file.
- Confidence: verified in code.

### M-165 wrong behaviour (low): failures that LumaLink's window does not show
- Evidence:
  - lumalink-gui updates.rs:93: a check the person asked for whose thread ended without an answer becomes `Quiet`.
  - main.rs:578-588: a job's panic is shown, but an allocation abort in the cook (M-145, M-156 to M-159) ends the
    window with no message.
- Confidence: verified in code.

### M-166 crash (suspected): a texture record's width times height is computed in 32 bits before anything is read
- Evidence: cook/texture.rs:868-876. `w * h` in u32, from the record's header, sizes `vec![[0u8; 4]; n]` before the
  texels are checked. It is called for a model's records (model_write.rs:1043, 1480, 1514, 1535; hit_record.rs:1113,
  2190).
- Why: whether a mod can supply a record, rather than have one cooked, depends on model_ir's `Source::of`, which is
  not read.
- Confidence: suspected; needs a read of model_ir.rs or a run with a model that carries its own record.

## Answer: allocations sized by values in the file (complete for the files named, by grep of `vec![_; n]`, `with_capacity`, `resize`)

Unbounded or too large:
- gltf.rs:189, 197, 1678 (M-145);
- flac.rs:165-170 (M-151);
- modern_audio.rs:98 (M-159);
- sound.rs:1403 (M-156);
- ui_layout.rs:927, 933, 938 via shared offsets (M-157);
- archive.rs:186-196 nested (M-158);
- texture.rs:874 (M-166, suspected).

Bounded by a fixed limit:
- archive.rs:30 (256 MiB);
- gltf.rs:410-412 (PNG side at most 16384: 1 GiB plus a 1 GiB copy);
- texture.rs:372, 417 (texel count below 2^30, and the first level must be in the file: at most 8 times the image
  data);
- font_cook.rs:408 and 483-501 (cells at most 1024, at most 32 sheets of 1024 x 1024);
- sound_effect.rs:235 (rate 8 to 384 kHz is checked before the resample, sound_effect.rs:455, sound_stream.rs:96,
  movie_cook.rs:499, so the taps are at most 2 x 32 x 48);
- sound_effect.rs:258 (out_frames: at most 20 minutes, times 48 kHz over 8 kHz);
- audio_data.rs:299 (8 times the input plus 1 MiB);
- ui_layout.rs:308 (u8 count), 519 (2 x names);
- layout_json.rs:407 (16 or 20);
- binary.rs (MAX_IMPORTS 65,536).

Bounded by the file's length, because each element is read from its own bytes before it is kept:
- sound.rs:976, 1330 (u16 groups), 1411, 1417, 1452 (offsets of parts already built);
- sound_stream.rs:54; sound_effect.rs:506 and the WAV reader 101-145;
- movie_cook.rs:436, 518-534 and the WebM walk 155-200;
- ui_layout.rs:536, 645-713, 746 (pane and material counts from blocks in the file);
- layout_json.rs:381, 390, 423, 667, layout_anim.rs:105, 134, 193-241 (from a mod's JSON, bounded by its length);
- texture.rs:322, 594, 607, 625, 804;
- gltf.rs:28, 43, 443-511, 966-995, 1192, 1234-1236, 1315, 1419-1436, 1562-1564, 1607, 1670-1671, 1721, 1814, 2079,
  2495-2515, 2652;
- post_shader.rs:210.

Not traced (beyond the files named): model_ir, clip, particles, formats (TPL, BTI, RFNT), message, table, ghost,
camera, native_data, kcl, fur and mii_parts. Every one of them is reachable from a mod's file through cook_file
(mod.rs:300-380), so Area 8 is larger than the list the coordinator gave.

## Answer: loops driven by values in the file (complete for the files named, by grep of `while`, `loop`, `for _ in 0..n`)

- Do not end in practice:
  - modern_audio.rs:286-307 (M-146);
  - ui_layout.rs:917-960 (M-157);
  - schema.rs:278-289 (M-160);
  - archive nesting (M-158).
- End, with each step moving forward or bounded by the data:
  - sound.rs:290, 308 (fixed 0x80);
  - sound.rs:430, 522, 562, 781, 817 (work lists with a `seen` set, depth at most STACK_LIMIT);
  - sound.rs:458 (reads stop at the end), 604 (each point reads new bytes), 962 (`at` grows by at least 8), 1356
    (each command moves `p`);
  - sound_effect.rs:107 (WAV chunks move forward), 164 (OGG, capped);
  - sound_sequence.rs:129 (the reader moves), 257 and 262 (count down);
  - ui_layout.rs:327, 335 (move by 2), 526 and 1052 (probing a table at least twice the names), 562, 661 (tree
    links, built by the cook);
  - texture.rs:259, 277, 286 (move forward), 331 (at most 32), 337 (at most `wanted`, at most 32);
  - gltf.rs:1242 (cycles refused, 1244-1246), 1331, 2082, 2507, 2887 (parent chains of a checked tree);
  - gltf.rs:2840-2843 (angles from atan2; a NaN does not enter the loop);
  - surface_shader.rs:340-385 and post_shader.rs:236-285 (each moves forward), surface_shader.rs:719;
  - audio_data.rs:138, 204, and 218 (bounded by reads);
  - flac.rs:73-76, 128, 167, 403-410 (bounded; M-151);
  - movie_cook.rs:162 (each element moves `p`), 249-280 (an OBU size of at most 8 bytes), 293 and 302 (at most 32);
  - thp.rs:129-170;
  - archive.rs:35, 68 (until `size`, at most 256 MiB, and each step reads input or copies);
  - binary.rs:204-853 (each count's reads go through `?` and stop at the data's end; u16 counts for sections and
    headers; relr bits at most 63).

## Answer: shaders

- What a mod's shader may contain:
  - In the cook (post_shader.rs:60-64, 230-290; surface_shader.rs:1-25, 340-385): HLSL limited to a word list that
    includes `for`, `while` and `do`, with no `#` lines.
  - At run time (`mod_post_register`, mod_post.cpp:190-236): any text without a NUL. This covers `#include`, which
    makes the compiler read files on disk. Mod code is native already, so this gives it nothing new.
- Which compiler, where, with what limit:
  - D3D12: dxc.exe as a child process, `-O3 -WX`, 120 s, then killed (mod_shader_compile_d3d12.cpp:96-131).
  - D3D11: d3dcompiler_47 in the game's process, no limit.
  - Vulkan: glslang plus SPIRV-Tools in the game's process, no limit.
  - Metal: glslang, then SPIRV-Cross to MSL in the game's process. The MSL is compiled by the system when a draw first
    needs it (pipelines_mtl.mm, not read).
- Which thread:
  - Programs a mod needs: at start, on a pool of threads that the start waits for (mod_shader_build.cpp:150-205).
  - Run-time post programs: on the thread that calls `mod_post_register`.
- A program that compiles but hangs or faults the GPU: no watchdog of the game's. The OS resets the device (TDR on
  Windows); D3D11 and D3D12 throw, and the game ends with an error (M-162). The Vulkan and Metal device-loss paths are
  not read beyond pipelines_vk.cpp:910.

## Answer: the validator's rules that may go unchecked (complete, by grep of `not_checked(`)

| Code | Missing input | What a mod gets past when it is not checked |
| L302, L303 | the game's store (layers.rs:209-210) | a new file of a kind that cannot be new; replacing a whole table with rows |
| L401 | other mods' rows (patches.rs:381) | a key that does not match another mod's table |
| L402 | the store, scenarios, rows (patches.rs:191, 195, 262, 387, 424) | patch lines that the game cannot apply |
| L403 | the factory's classes, the store (after_start.rs:138, 187, 201) | placements of unknown classes; the game checks after the mods' start |
| L407 | the load (patches.rs:666) | a note only |
| L505 | self-description, types, virtuals (code.rs:408, 455, 476, 497, 501, 513) | calls to functions or types the game lacks; failure at load |
| L506 | services catalog (code.rs:514) | none at run time: apiService refuses a service not granted |
| L508 | self-description (code.rs:295) | code built for another LumaCore |
| L803 | the game (mods.rs:21) | a Nebula version the mod refuses |
| L806 | renderer features or device (manifest.rs:1008-1009) | the game checks it at its start |

- The game supplies the store, names, types, services and placement (mod_check.cpp:178-216). It supplies none of the
  self-description on a build without a `luma` folder (143-151), and none after M-161's rerun.
- Does the start re-check what `lumalink check` checked? Yes, every rule runs again at each start (rules/mod.rs:104-118).
  A kept report only replaces the patches block's findings (`kept_at_start`, saved.rs:279-289), and only for the same
  `.luma` fingerprint, validator version and game key. Its SHA-1 end line detects damage; it is not a signature. Since
  only patch findings are reused, a forged kept report can hide only L106 and patch-file findings.
- Limits:
  - JSON nesting 64 (json.rs:45);
  - schema patterns 256 characters and repeats 1000, matched in linear time (schema.rs:27-28, 671-704);
  - features 64 (names.rs:75);
  - imports 65,536 (binary.rs:811);
  - kept reports 64 MiB (saved.rs:59);
  - message phrase nesting 2 (lib.rs:601).

## Answer: Area 7 questions

- `convert` on untrusted packs:
  - Riivolution `external` paths are resolved under the pack. `..` cannot climb above the root and `:` is refused
    (plan.rs:206-235). Single files follow links (`is_file`, 402); the folder walk names links and skips them (455).
  - Gecko lists (gecko.rs:15-60): only plain writes are read, and fill counts are capped at 0x1000. Writes that match
    known patches become settings or table changes (plan.rs:570-620). Everything else goes on `unknown_code`, which
    becomes text stubs for the maker (convert.rs:483, code_stubs.rs). No converted Gecko code runs.
  - Texture packs: walkdir without following links, and only files (texture_pack.rs:228-231, 327).
  - Every converted file is cooked with nebula-import in LumaLink's process, so M-145 and M-156 to M-159 can end
    LumaLink.
- `test` with missing parts (jobs.rs:1050-1101): a missing luma.json, tests, runner, bin folder or Python each stops
  with a named reason. A test with no result prints Test.NoResult (lumalink main.rs:851-853). See M-164.
- Commands with no installed game:
  - Commands that need game.pak (add, run, convert, the character jobs) refuse with LumaLink.NoInstalled (main.rs:100-101,
    548, 708, 1028, 1097; GUI main.rs:261).
  - `check` and `pack` run without the game, and list L302, L303, L40x, L505, L508 and L803 as not checked (main.rs:56-64;
    rules as above). `pack` still writes the .luma.
  - `build` needs `--names` or the installed self-description (main.rs:86-90).
  - The language server says Lsp.NoGame (main.rs:783).
- `update` with no network (update.rs:1-16, 281-303; lumalink-gui updates.rs:49-101): an automatic check that fails
  keeps the last answer and says nothing. A check the person asked for shows Failed (except M-165). Downloads are kept
  only when the signature verifies (round 12).
- GUI actions (lumalink-gui main.rs:119-134 and 570-600): Check, Build, Pack, Install, Convert, MakeLuma, AddCharacter,
  CharacterMod, Verify, List, Extract and Explain. Each runs on a thread with `catch_unwind`; errors and panics are
  shown in red. Silent cases are in M-165. CharacterMod with 99 existing "<name> mod" folders gives an empty path
  (main.rs:264), which then fails with a shown error.
- Setup (lumalink-setup main.rs:1-16, 60-90; install.rs):
  - It installs for the signed-in user into `--folder` or the default, with a PATH option, menu entry, desktop
    shortcut and update check.
  - Its payload is inside the signed exe; it downloads nothing.
  - Uninstall removes the manifest's files (M-163), the folder if empty, the registry or shell entries, the
    `~/Applications` bundle only when its Info.plist names LumaLink (install.rs:1168-1206), and the data folder (M-153).
- M-153's exact condition:
  - The uninstalling process must have LUMALINK_DATA set to an absolute path.
  - On Windows, the uninstaller inherits the user's variables when started from Settings or the Setup window. On
    Linux and macOS, it inherits whatever the shell that starts Setup exports.
  - That folder is then removed whole, with errors ignored (install.rs:361-363).
  - Nothing documents LUMALINK_DATA; only update.rs's tests set it (update.rs:1157, 1171). Without it, the deleted
    folder is LumaLink's own (`%LOCALAPPDATA%\LumaLink`, `~/Library/Application Support/LumaLink`,
    `$XDG_CONFIG_HOME/lumalink` or `~/.config/lumalink`), and relative values are ignored (update.rs:171).

Checked in round 13 and found correct:
- Sample rates are checked before every resample (sound_effect.rs:455, sound_stream.rs:96, movie_cook.rs:499).
- The WAV, WebM, AV1 OBU and Yaz0/Yay0 readers stop at the data's end.
- The sequence and Me program walks keep `seen` sets and a depth cap.
- Schema patterns are matched in linear time.
- The binary reader's loops are bounded by the data.
- Riivolution paths stay under the pack.
- Converted Gecko codes never run.
- Setup removes the macOS bundle only when it is its own.
- A GUI job's panic is shown.

Not read in round 13 (the whole mod audit):
- Area 8:
  - Files not read whole: the cook files named above (sites read by grep plus the ranges cited), luma-validate json.rs,
    names.rs, lib.rs, saved.rs beyond the lines cited, and rules/manifest.rs, code.rs, patches.rs, layers.rs, mods.rs,
    kit.rs and after_start.rs (only their not-checked sites and the ranges of earlier rounds).
  - The game-format cooks reachable from mod files: model_ir, model_write, clip, particles, formats, message, table,
    ghost, camera, native_data, kcl, fur, mii_parts, hit_record.
  - pipelines_mtl.mm and the Vulkan device-loss path.
- Area 7:
  - Files not read whole: lumalink-core (new, add, editor, editor/query, editor/complete, lsp, source, character,
    object_table, player_constants, known_patches, inspect, files_check, effects_check, layout, region_map,
    table_patch, script, numbers_code, vscode, game_functions, ppc, plan.rs beyond the lines cited, convert.rs beyond
    its head, riivolution.rs beyond its head, update.rs 300-1174 beyond the lines cited);
  - lumalink/src main.rs beyond the ranges cited, and cli_text.rs;
  - lumalink-gui: screen.rs, chrome.rs, look.rs, words.rs, sky.rs, shot.rs, picture.rs, icon.rs;
  - lumalink-setup: main.rs beyond 1-90 and 150-170, 255-270 and 420-440; install.rs beyond the ranges cited.
- Area 5 leftovers: not started.

# Round 14

Method and limit: the files named for this round come to about 36,000 lines. I did not read them line by line, so no
file moves to "read completely".
- I ran a mechanical pass over every cook and validator file: every allocation sized by a count, every
  `while`/`loop`, and every function that calls itself (a script over the sources, in the scratchpad, recur.py).
- I then read the sites that pass file values in.
- I read the code that settles M-166 and M-160.
- A note on what can end the process: the release profile keeps `panic = unwind` and has no overflow checks
  (Cargo.toml:20-23, nothing set). Panics, slice-index faults and debug overflow panics therefore become refusals
  through `catch_unwind` (cook/mod.rs:533, cook_c.rs:88; validate_c.rs:514-518, M-150), and arithmetic wraps
  silently. Only three things end the process: a failed allocation, a stack overflow, and a loop that does not end.
  The verdicts below are about those three.

### M-167 crash (stack overflow) and hang: a patch selector's `glob` recurses once per `*` and backtracks without limit, in the validator at every start and in the cook
- Evidence:
  - luma-validate rules/patches.rs:75-82 and the cook's copy at cook/patch.rs:619-626. A `*` tries every split of
    the rest by recursion. Recursion depth equals the number of pattern characters matched. Nothing remembers a
    failed split.
  - The pattern is the mod's own selector value (patches.rs:672-685; patch.rs:520-531). The only size limit is the
    64 MiB patch file (patches.rs:7).
  - The cell is a game table cell or one of the mod's own added rows (patches.rs:688-690).
- Why:
  - A selector of about 20,000 `*` characters overflows the stack. That is an abort, which `catch_unwind` cannot
    stop, so the game ends at every start until the mod is removed.
  - The pattern `*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b` against a 60-character cell of `a`s takes about C(60,15)
    (around 5 × 10^13) steps.
- Smallest input: one patch file whose selector `match` holds `{"<text column>": {"glob": "*…*"}}` with 20,000 stars.
- Confidence: verified in code; the stack depth at which it overflows needs a run.

### M-168 memory safety (game process): the cook passes already-cooked files through unchecked, and the game reads them in place with only a magic check
- Evidence:
  - These cooked forms are returned unchanged when the mod's file starts with the magic:
    - kcl.rs:291-293 (NCOL);
    - ghost.rs:199-201 (NGST);
    - sound.rs:1344-1346 (NSND, version only);
    - table.rs:491-493 (NTBL);
    - formats.rs:90 and native_data.rs:182 (NBTI);
    - mod.rs bti path (`texture::is_record`).
  - camera.rs:236-242 is the counter-example done right: a cooked NCAM is accepted only when it reads whole and
    writes back to the same bytes.
  - sound.rs:12 states the design: what the console read past its own arrays "is refused here, so the game never has
    to check it".
  - The game's collision loader checks only the four magic bytes (native/petari/src/Game/Map/KCollision.cpp:28).
    It then uses the header's offsets and counts (KCollision.hpp:40-58: mPosOffset, mOctreeOffset, mListOffset and
    the counts) as given.
  - The validator has no rule for these formats (grep of luma-validate for NSND, NTBL, NbTextur: none).
- Why: a mod's `files/` NCOL with an octree offset outside the file makes the game read memory out of bounds when
  collision is tested. The same pattern holds for every pass-through form the game reads in place.
- Smallest input: a 0x58-byte file that starts `NCOL`, with mOctreeOffset set to 0x7FFFFFF0, placed over any stage's
  `.kcl`.
- Fix shape: accept a cooked form only when it parses whole and writes back to the same bytes, as camera.rs does.
- Confidence: the pass-through and the game's magic-only check are verified in code. The out-of-bounds read needs a
  run. The NSND, NTBL, NGST and NBTI readers in the game are not read.

### M-169 crash (memory exhaustion): a mod's cooked table reserves rows and columns from its header before reading any
- Evidence: cook/table.rs:252-273 (`from_ntbl`: `rows` and `count` are le32 values; `Vec::with_capacity(count)` and
  `Vec::with_capacity(rows)` come before any row is read). It is reached for a mod's table under a patch
  (overlay_patches.rs:139-142).
- Why: rows = 0xFFFFFFFF reserves about 100 GB (24 bytes per row). The allocation fails and the process aborts.
- Smallest input: a 24-byte NTBL header with rows 0xFFFFFFFF, plus a patch that names that table.
- Confidence: verified in code.

### M-166 settled: not reachable from a mod
- Evidence: `record_level0` (texture.rs:864-876) is called only on a model's `records` (model_write.rs:1043, 1480,
  1514, 1535; hit_record.rs:1113, 2190). Those records are made only by `formats::timg_record` → `texture::cook`
  (model_ir.rs:1305, formats.rs:42-46). Its width and height are u16 (formats.rs:56-57), and `texel_count` caps them
  below 2^30 (texture.rs:348-354). A record a mod gives as a file (the bti path) is passed through and never read by
  `record_level0`. Its risk is M-168's.
- Confidence: verified in code. M-166 is withdrawn as a crash and folded into M-168.

### M-160, exact trigger and smallest input
- The trigger:
  - A schema node with `oneOf` (every branch is evaluated, schema.rs:278-279), or `anyOf` whose early branches fail.
  - Its branches reach the same node again through `items` or `properties` plus `$ref`.
  - The cycle check (schema.rs:118-137) follows only `$ref`, `oneOf` and `anyOf` edges, so this is accepted.
  - The cost doubles with each level of nesting in the data, up to json.rs:45's 64.
- Smallest schema (library mod, `code/<id>.kit/<module>/1.json`):
  `{"$defs":{"x":{"oneOf":[{"items":{"$ref":"#/$defs/x"}},{"items":{"$ref":"#/$defs/x"}}]}},"$ref":"#/$defs/x"}`.
- Data (a mod that uses the library, `kit/<module>/<name>.json`): 62 nested `[` then 62 `]`, 124 bytes.
- Cost: about 2^62 node checks. Each one also allocates (`passes` makes a Vec and pointer strings).

## Gaps between the validator and the cook

- Pre-cooked forms (M-168): the validator does not look inside files, the cook passes cooked magics through, and
  the game trusts the cook. Nothing checks a mod's NCOL, NGST, NSND, NTBL or NBTI.
- Sizes: the validator allows 4 GiB a file and 64 GiB in all (rules/container.rs:9-10). The cook reads files whole
  and has its own per-format limits, which are below that for some formats and absent for others (M-143, M-145,
  M-156 to M-159, M-169).
- Selectors: the same unbounded `glob` is in both (M-167), so neither protects the other.
- Kit schemas: only the validator reads them (M-160); the cook does not use them.
- The cook's refusals (`Cook.*`) happen when the overlay is cooked at start, not in `lumalink check`. A mod that
  passes `check` can still have files left out at start. That is by design (convert.rs:5-6 cooks during convert),
  but `check` does not say it.

## Area 8 closing table

| File | Lines | Read completely | Parses untrusted bytes | Verdict | Limits it enforces |
| cook/model_ir.rs | 1573 | no (sites, 1028-1050, 1285-1310) | yes (J3D) | not determined; counts are u16 at the sites read | u16 counts |
| cook/model_write.rs | 2558 | no | no (writes the IR) | not determined | - |
| cook/clip.rs | 437 | no (sites) | yes | not determined; name count checked against the table (100-105) | - |
| cook/particles.rs | 777 | no (sites) | yes (JPC) | not determined; colour table at most 32,768 (204-209) | i16 frames |
| cook/formats.rs | 369 | no (42-67, sites) | yes (TPL, BTI, RFNT) | breaks: M-168 (NBTI pass-through) | texel count below 2^30 |
| cook/message.rs | 394 | no (sites) | yes (BMG) | not determined | - |
| cook/table.rs | 770 | no (107-121, 186-273, 491-503) | yes (BCSV, NTBL) | breaks: M-169, M-168 | BCSV fields at most 1024, row size at most 0x10000, entries × size ≤ file |
| cook/ghost.rs | 262 | no (196-204, sites) | yes | breaks: M-168 (NGST pass-through) | - |
| cook/camera.rs | 413 | no (236-244) | yes | safe at the sites read; cooked form round-tripped | - |
| cook/native_data.rs | 753 | no (sites, 181-198, 292-301, 423-446) | yes (fonts, BAS, BTI) | breaks: M-168 (NBTI) | glyphs below NO_GLYPH; BAS count × 0x20 ≤ file |
| cook/kcl.rs | 673 | no (70-122, 288-296, 550-575) | yes (KCL, NCOL) | breaks: M-168 (NCOL) | octree depth 64; parts in order |
| cook/fur.rs | 171 | no (66-67) | yes | safe at the sites read; cooked form needs an exact size | fixed SIZE |
| cook/mii_parts.rs | 377 | no (sites) | yes | not determined | - |
| cook/hit_record.rs | 2683 | no (recursion sites) | no (classes cooked models) | not determined | - |
| cook/archive.rs | 288 | no (25-75, 140-196) | yes (Yaz0, Yay0, RARC) | breaks: M-158 | 256 MiB a member, 4 GiB an archive, folder depth 32 |
| cook/overlay.rs | 2070 | no (1-40, 408-409, 437-551, 594) | routes mod files | breaks through what it calls | - |
| cook/mod.rs | 788 | no (226-380, 502-533) | routes | breaks through what it calls | archive depth 8 |
| cook/sound.rs | 1499 | no (sites, 1-30, 1318-1460) | yes (BAA, BMS, BME) | breaks: M-156, M-168 (NSND) | STACK_LIMIT, seen sets |
| cook/sound_stream.rs | 164 | no (85-110) | yes | safe at the sites read | 20 min, 8-384 kHz, channels |
| cook/sound_effect.rs | 554 | no (95-176, 222-262, 440-470) | yes (WAV, OGG) | safe at the sites read (OGG 3.7 GB cap) | 20 min, 8-384 kHz, 2 channels |
| cook/sound_sequence.rs | 584 | no (sites) | yes (MIDI) | not determined; loops count down | - |
| cook/font_cook.rs | 668 | no (sites) | yes (TTF/OTF via ttf-parser) | safe at the sites read | size 96, cell 1024, 32 sheets, 65,536 kerns |
| cook/movie_cook.rs | 725 | no (1-40, 155-200, 245-360) | yes (WebM, IVF, AV1) | safe at the sites read | 1920 wide, 1920×1080, 60 fps, 1 hour |
| cook/ui_layout.rs | 1065 | no (sites, 296-312, 510-522, 905-975) | yes (BRLYT, BRLAN) | breaks: M-157 | u8/u16 counts |
| cook/layout_json.rs | 679 | no (sites) | yes (JSON) | safe at the sites read | names 16, textures 64 |
| cook/layout_anim.rs | 302 | no (sites) | yes (JSON) | safe at the sites read | - |
| cook/texture.rs | 1046 | no (sites) | yes | safe (M-166 withdrawn) | texels below 2^30, 32 levels |
| cook/gltf.rs | 2970 | no (rounds 11-13 ranges) | yes (glTF, GLB, PNG, JPEG) | breaks: M-145 | 4M elements, 16384 side, 65,535 frames |
| cook/post_shader.rs | 376 | no (1-25, 60-64, 230-290) | yes (HLSL text) | breaks: M-152/M-162 (GPU) | word list, no `#` |
| cook/surface_shader.rs | 734 | no (1-25, 340-385) | yes (HLSL text) | same as post_shader | word list |
| cook/audio_data.rs | ~310 | no (136-302 sites) | yes (AW, AST, WSYS) | safe at the sites read | 8 times the input plus 1 MiB |
| cook/thp.rs | ~200 | no (42-170) | yes (THP) | safe at the sites read | 16 tracks, 0x10000 samples |
| cook/modern_audio.rs | ~320 | no (1-108, 203-310) | yes (WAV, OGG, BRSTM/BCSTM/BFSTM) | breaks: M-146, M-159 | none on streams |
| cook/flac.rs | ~470 | no (54-416 sites) | yes (FLAC) | breaks: M-151 | 2^28 samples a channel, comments 1<<16 |
| cook/patch.rs | - | no (520-626) | yes (patch JSON) | breaks: M-167 | - |
| luma-validate json.rs | 452 | no (45, 196-204, 334-387) | yes | safe at the sites read | depth 64 |
| luma-validate schema.rs | 790 | no (1-30, 118-153, 278-294, 668-705) | yes (kit schemas) | breaks: M-160 | patterns 256, repeats 1000, linear match |
| luma-validate names.rs | 943 | no (75-100) | yes (game's self-description) | not determined | 64 features |
| luma-validate binary.rs | 869 | no (loop sites, 801-853) | yes (PE, ELF, Mach-O) | safe at the sites read | 65,536 imports |
| luma-validate saved.rs | 289 | no (1-28, 59, 279-289) | yes (kept report) | safe at the sites read | 64 MiB, SHA-1 end line |
| luma-validate lib.rs | 644 | no (452-488, 567-601) | routes | safe at the sites read | phrase nesting 2 |
| luma-validate semver.rs | 275 | no (grep) | yes | not determined | - |
| luma-validate zip.rs | 401 | yes (round 10) | yes | safe | names, CRC, overlaps |
| luma-validate rules/container.rs | 268 | yes (round 11) | yes | safe | 240-byte names, 4 GiB a file, 64 GiB in all |
| luma-validate rules/patches.rs | 699 | no (7, 75-82, 191-424 not-checked sites, 660-690) | yes | breaks: M-167 | 64 MiB a patch |
| luma-validate rules/manifest.rs | 1462 | no (rounds 10-13 ranges) | yes | not determined | - |
| luma-validate rules/code.rs | 943 | no (200-515 sites) | yes | M-161 (fails open) | - |
| luma-validate rules/layers.rs, mods.rs, kit.rs, after_start.rs, mod.rs | 265, 153, 211, 220, 162 | no (ranges cited) | yes | not determined beyond the not-checked list | - |

Area 8 is NOT closed. The "not determined" rows and every row marked "no" are unread line by line.

Not read in round 14 (the whole mod audit):
- Area 8: every file in the table marked "read completely: no", line by line. The runtime readers of the cooked
  forms M-168 names (NSND, NTBL, NGST, NBTI) are not read. pipelines_mtl.mm and Vulkan device-loss handling are not
  read.
- Area 7: as listed at the end of round 13.
- Area 5 leftovers: not started.

# Round 15

Method and limit: docs/modding is 271 files and 32,247 lines, so I did not read every page line by line. I ran six
mechanical cross-checks over every page, using inline commands only, then read the hits. A page counts as "checked"
below for the claim classes those checks cover, not as read completely.
1. Every ```lumascript block (593 blocks) was scanned for the compiler's refused spellings: `&&`, `||` and `!`; a call
   written without `()`; a type name used as a value. Each hit was then read.
2. Every LumaLink flag and command in reference/lumalink-commands was compared both ways with lumalink main.rs's clap
   definitions.
3. Every `<service>.<fn>(` call in each reference/services page's code was checked against
   native/include/luma_<service>.h.
4. Every `pub` line in each reference/lumakit page was compared both ways with its module source.
5. Every code line of each tutorial was compared with its example mod's source (tests' helper mods included).
6. Every S code named in the docs was compared with diag.rs's table. The compiler's not-yet-emitted constructs
   (emit.rs `say` → S0800) were compared with the pages that describe them.
- Area 11 was done by sampling: one test.json per old suite, plus the `proves` tags of every test.json in the old suites
  and the examples (1,082 distinct tags).

### M-170 docs wrong: code blocks the compiler refuses
- Evidence. A type name used as a value is S0083 "Script.Kind.Type" (lumascript diag.rs:663-672). `vec3` is a builtin
  type name (check.rs:485-509), not a value. The pages that use it as one:
  - reference/lumascript/functions-structs-and-enums.md:28: `vel: vec3 = vec3`.
  - reference/lumascript/hooks.md:27: `self.mVelocity = vec3`.
  - reference/lumascript/values-and-memory.md:27: `if let k = target?.get { k.mVelocity = vec3 }`. It also calls
    `get` without `()`.
  - reference/lumascript/tasks.md:15 and 17: `if let a = h.get { ... }`. Every other page and every example writes
    `.get()` (tutorials/a-new-power-up.md:224; tutorials/balloon-world.md:170).
- Confidence: the vec3 lines are verified against the code table. The `.get` lines are verified as inconsistent with
  every other page; the exact refusal code for them needs a compile.

### M-171 docs wrong: features documented as working that the compiler refuses with S0800
- Evidence. emit.rs:662-665 and 917 (`say` → BACKEND_NOT_YET, diag.rs:349) refuse `hook ... every override of`,
  `on layers` and `c struct`. These pages present them as working:
  - hooks.md:43, detailed-rules.md:37, 60, 81 and 335, compiled-form.md:97: `every override of`.
  - lifecycle.md:23-33 and luma-json.md: `on layers`.
  - c-and-cpp.md:18, how-tos/writing-code-in-c.md, compiled-form.md: `c struct`.
- Also refused with S0800 and named as unsupported on no page:
  - a hook in a class; a class's own fn; a class field whose first value needs code, or bound by a pattern;
  - module-level patterns, patterns without a value, patterns in `for`;
  - `expect` outside a test;
  - `go` from here or to this state;
  - states for a class whose base has no control();
  - a whole game object or array written;
  - extern or exported C `var` parameters;
  - `on draw`, `on step` or `on option` without their service's header; `on stage` with a name made at run time;
  - some `return` with a value;
  - structs and enums holding some kinds of value.
  The full list is the `say` strings in emit.rs and emit/*.rs.
- Confidence: verified in code.

### M-172 docs missing: 80 of the compiler's build-time codes appear on no page, including S0012 and S0800
- Evidence: the codes in diag.rs that no page names are S0002-3, S0005-6, S0010-12, S0014, S0016-19, S0021, S0026-32,
  S0034-46, S0048-49, S0051-57, S0070-72, S0074, S0076-79, S0081-82, S0084-85, S0087-89, S0091-92, S0095-97,
  S0099, S0134-36, S0140-50, S0152-53 and S0800. The S0901-S0918 that pages name are lumart's run-time codes
  (diag.rs:43), so they are correct.
- Confidence: verified by comparison. `lumalink explain` may cover these codes; I have not read it.

### M-173 docs missing: no page warns about calls that crash on the first `on frame`
- Evidence: in the owner's run, validateSensor, kit `item.give` and kit `camera.now` crash on the first `on frame`
  and work at the title. reference/lumakit/item.md:20 says data items are defined "on its first frame". Neither
  item.md nor camera.md, nor the lifecycle page, says when these calls are safe. The fault itself is the engine's
  (the M-014 class).
- Confidence: verified by grep, for the absence.

### M-174 missing feature (tests): behaviours no test proves, most likely to be hit first (extends M-036 and M-117)
1. Calls on the first `on frame` before the player or scene exists (validateSensor, item.give, camera.now).
2. kit.shop, kit.progress, kit.player.character, kit.player.state and kit.ui: no `proves` tag. kit.cutscene and
   kit.replay: used by no test mod.
3. Two mods asking for the same single kit value, and one stopping (M-021, M-033).
4. svc.content.fx: no tag. svc.content.model: `set_param` only.
5. Pre-cooked files over game files (M-168).
6. A real fault followed by the next start's safe start, per platform (M-031).
7. player.make and remove repeated (M-030).
8. `on option`, `set_number`: none. `set<`, `sort_by`, `set_table`, `hook ... replace`, `is_state`, `text "Key"`: one
   test each (M-117).
9. S0800 constructs (M-171): no test checks each one is refused rather than miscompiled. Not checked.
10. Reload while another reload runs (M-154), and a reload whose take-out is refused (M-155).
11. Hostile importer and validator inputs (M-143, M-145, M-146, M-156 to M-160, M-167, M-169). g/k-hostile is unread.
12. A game call with a pointer from outside the mod (M-029).
13. The validator failing, and the start that follows (M-150, M-161).
14. Uninstall with LUMALINK_DATA set (M-153).
- Confidence: items 2 and 4 are verified by the tags; the rest are as cited.

## Answer: Area 11, what each old suite proves (sampled)
- f, 21 tests: kernel and loader. Classes, cook, gone, guard and guard-crash, harness, input, kit data, layers,
  loader, modules, numbers, objects, patch, reload take-out (f/tests/reload 1140-1143), types, varargs. Not proved:
  M-154, M-155.
- fb, 8 tests: kit area, camera, gravity, pointer, projectile and ride, plus light and world (fb/tests/kit-camera checks
  each rig on EggStarGalaxy). Not proved: first-frame calls, two mods sharing a single value.
- g: not a test suite. It holds outputs and surveys, plus the folders anim, harness, k-hostile, layout, macho, sound
  and stage.
- h, 13 tests: services. assets, audio, code, fs, kit-net, mods, net, options, player, registry, save (2261-2271),
  system, time. Not proved: content.fx.
- j, 24 tests: the game's mod surface. Consent, blocklist, import, mod log, mods screen, safe start (6601-6603,
  simulated, not caused by a fault), start check, draw, effects, models, movie, ui, and kit menu, screen, stage and
  talk.
- k, 21 tests: the language in the game. Hooks, chains, arrays, frame, containers, JSON, refs, structs, text, tasks,
  C, classes, states, gone, kit hud and menu. Not proved: `every override of` (unbuilt), `on option`, `set_number`.

## Round 15 table
| Pages | Claims checked | False claims | Unbuilt documented | Refused code lines | Verdict |
| reference/lumascript (29) | code spellings, S codes, S0800 constructs | 3 | 3 (M-171) | 5 lines on 4 pages (M-170) | needs fixes |
| reference/services (24) | each `<service>.<fn>(` against its header | 0 | 0 | 0 | accurate for names; prose not compared |
| reference/lumalink-commands (17) | flags and commands, both ways | 0 | 0 | 0 | accurate for flags and commands |
| reference top-level (14) | luma-json, errors codes | 1 (on layers) | 1 | 0 | needs fixes (M-172) |
| reference/lumakit (23) | every `pub` signature, both ways | 0 | 0 | 0 | accurate for signatures; M-173 missing |
| tutorials (11) | every code line against its example | 0 | 0 | 0 | accurate for code |
| how-tos (18) | code spellings | 1 (`c struct`) | 1 | 0 | needs fixes (one page) |
| get-started (5), concepts (10) | code spellings | 0 | 0 | 0 | steps not compared with tool output |

Totals:
- 271 files mechanically checked; none read line by line.
- False claims: three constructs documented as working, across 7 pages plus luma-json.
- Refused code: 5 lines on 4 pages.
- Unbuilt features documented: 3.
- Compiler codes on no page: 80.
- No mismatches found in flags, commands, service names, LumaKit signatures or tutorial code.

Not read in round 15 (the whole mod audit):
- Area 10: every page line by line. Service prose (limits, defaults, errors) against the bodies; the guides' steps
  against tool output; the nerves pages (no sampling done); classes, events, tables, table-keys and test-files against
  the game.
- Area 6: the lumakit pages' prose line by line.
- Area 11: every old test beyond one per suite; g/k-hostile.
- Area 8: the runtime readers of M-168's forms; pipelines_mtl.mm; Vulkan device loss.
- Area 7: as at the end of round 13.
- Area 5 leftovers: not started.

# Round 16

Limit: my working room ran out after two pages. I read two pages completely, sentence by sentence, against the code:
reference/services/anim.md (154 lines) and reference/lumakit/camera.md (156 lines). I took camera.md out of order
because it is the page that misled the first-person mod's author. Every other page named for this round is not
started.

### M-175 docs wrong (misleading): kit.camera promises the player's body stays hidden while a first-person rig holds the camera; it is hidden once and comes back
- Evidence:
  - The page: camera.md, `eye` ("the player's own model hidden while the main camera holds it"), in the
    declarations table and the `eye` section.
  - The code hides the body once, when the control on top changes. In lumakit/source/camera.lum:125-141, `settle()`
    calls `player.set_drawn(m, DRAWN_SHADOW)` and keeps `hidden_player`. It does not re-hide while
    `hidden_player != none`, and nothing calls it again until the top control changes.
  - The game clears the flag whenever a player actor is set up: `mModHidden = false` at MarioActor.cpp:133 and
    MarioActorInit.cpp:127.
  - set_drawn only writes the flag once and records the slot (native/petari/src/Game/System/LumaPlayer.cpp:236-285).
- Why: after an arrival or a scene change, the player is set up again and drawn whole, while the first-person
  control still holds. This is what the owner saw.
- Corrected sentence: "The player's body is hidden when this rig takes the main camera. After a scene change or
  arrival the game draws it again; call `player.set_drawn` again (for example on `on stage`) or take the control
  again."
- Fix shape for the module: re-apply the hide in the frame hook whenever a first-person control holds and the current
  player's `mModHidden` lacks the body bit.
- Confidence: verified in code.

### M-176 docs wrong: three service pages tell C modders to import an older version than the header provides
- Evidence:
  - anim.md:14 gives version 1.1 and `LUMA_SERVICE(slot, "anim", "1.1")`.
  - The same page's "Details from the header" at line 20 says `Import: LUMA_SERVICE(slot, "anim", "1.0")`, copied
    from the header's own comment (native/include/luma_anim.h:6-7), while luma_anim.h:22-23 is MAJOR 1, MINOR 1.
  - The same mismatch (comment 1.0, header 1.1) is in luma_content_model.h and luma_draw_view.h, and so on their pages.
- Why: a C mod that copies the import asks for 1.0. Whether it then receives the 1.1 behaviour (anim's event rows,
  anim.md:49 "1.1: ...") is not stated.
- Confidence: verified in code (headers and page).

### M-177 docs wrong (low): anim.md's limits table gives three limits one description
- Evidence: anim.md:26-28. `LUMA_ANIM_MOST_PLAYS` (64) is described as "Plays one mod has going at once, marks one
  play carries, and event rows one clip's events give". `LUMA_ANIM_MOST_MARKS` (16) and
  `LUMA_ANIM_MOST_EVENT_ROWS` (64) have no description. The generator took the shared comment above the three
  #defines (luma_anim.h:25-28) for the first one only.
- Why: read row by row, the table says one play can carry 64 marks; the limit is 16.
- Corrected rows: MOST_PLAYS "Plays one mod has going at once"; MOST_MARKS "Marks one play carries"; MOST_EVENT_ROWS
  "Event rows one clip's events give".
- Confidence: verified in code.

### Notes from the two pages (correct, or already covered)
- anim.md: every function's argument errors, limits, thread rule and events match luma_anim.h (lines 85-106 of the
  header, read by grep of each limit). The event field named `socket` on non-network pages is the language's real
  field name for an event's handle (lumascript svc.rs:1845), so it is correct, though confusing.
- camera.md:
  - `side` correctly warns that it needs a scene with a player.
  - `now` (camera.lum:361-369) reads `MR::getCamPos()` and the camera director with no guard. The page gives no
    warning about calling it before the scene's camera exists (M-173; the owner's first-frame crash).
  - `fov`'s range and `shake`'s fading are not checked against camera.lum.

## Round 16 table
| Page | Lines | False statements | Missing warnings | Verdict |
| reference/services/anim.md | 154 | 2 (M-176 import version; M-177 limits rows) | 0 | needs fixes |
| reference/lumakit/camera.md | 156 | 1 (M-175) | 1 (`now` before the scene's camera, M-173) | misleading |

Statements most likely to mislead (only three found, since only two pages were read):
1. camera.md `eye`: "the player's own model hidden while the main camera holds it" → "hidden when the rig takes the
   camera; drawn again after a scene change or arrival until hidden again".
2. anim.md:20 (and content-model, draw-view): `Import: LUMA_SERVICE(slot, "anim", "1.0")` → `"1.1"`.
3. anim.md:26: one description for three limits → one each (plays 64, marks 16, event rows 64).

Not started in round 16:
- reference/services: assets, audio, calling-services, capture, content-fx, content-model, draw, draw-inst,
  draw-light, draw-native, draw-post, draw-view, fs, index, input, mods, movie, net, player, save, system, time, ui
  (23 pages).
- reference/lumakit: player-state, player-form, item, actor, stage, hud, ui-kit, screen and the remaining 13 (22
  pages).
- concepts (10 pages).
- reference/lumascript (29 pages).
- get-started (5) and how-tos (18).
- Area 11, Area 8's runtime readers, Area 7 and Area 5: as at the end of round 15.
