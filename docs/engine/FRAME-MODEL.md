# The frame model

How a frame moves through Nebula's threads: who owns what, where data changes hands, and what may wait for what.
Part 1 is the code as it is today, with file and line. Every line it names is the code of a1f423dd8 (rows/p022 before
its restack of 11-18; its code is 5526e1cf0's, where only `native/tests/CMakeLists.txt` differs), each checked by
reading it there; parts 1 and 2 were read once more against that code as a second reader (file 08 5.C, 11-18). Part 2
is the target and the rules the later stages hold (07-BASE-FIRST, stage 2 item 1). Part 1 grows section by section.

## Part 1. Today

### 1.1 The threads a frame touches

| Thread | Started at | Owns | Hands over |
| --- | --- | --- | --- |
| Game ("Nebula Game") | `native/host/host_main.cpp:6515`, running `gModule.run()`; the host's main thread (:6504) only waits for messages, the game's end and the once-a-second report (:6605) | the game's state, the frame list it writes | a written list to the render thread (1.2) |
| Render | `runtime/src/render/render_core.cpp:250` (`RenderCore::render_thread_main`, :1106) | the device, every GPU object, the lists it walks | released lists back to the game (1.2); pictures to the display |
| Window owner | `runtime/src/render/window_owner.cpp:20` | the OS window and its message loop | size, focus and input to the game and render threads |
| Display clock, Windows | `runtime/src/render/vblank_clock_win32.cpp:175` | the vertical blank's time | the moment a picture is due (pacing) |
| Display clock, Android | `platform/sdl/display_clock.cpp:30` on (Choreographer's callbacks on a looper thread of their own) | the vsync grid (CLOCK_MONOTONIC) | slots predicted on the grid under `g_mutex`, so a late callback delays nothing |
| Display clock, Apple | `platform/apple/display_clock_apple.mm` (the display link) | the vsync times and interval on Mach time | the same grid as Android |
| Display clock, Linux | none: `wait_display_frame` returns false (`platform/sdl/display_clock.cpp:202-212`) | — | the step keeps the host clock (I-005) |
| Pipeline workers | `runtime/src/render/core/pipelines.cpp:104` | compiling pipeline states | finished pipelines to the render thread |
| Record workers | `runtime/src/render/core/draw_record.h:279` | building draw records | records to the render thread's walk |
| Texture pack workers | `runtime/src/render/texture_pack.cpp:176` (below normal priority, :225) | reading and decoding replacements | decoded pixels, picked up at `pump` once a frame |
| Movie | `runtime/src/render/movie_playback.cpp:85`, :357, :903 | decoding a movie's pictures | pictures to the render thread |
| Sound | `runtime/src/native_audio.cpp:37`, `runtime/src/audio_device_wasapi.cpp:106` | the output device's buffer | none to the frame; it reads the game's mixed sound |
| Save and log writers | `runtime/src/achievements.cpp:215`, `runtime/src/stats.cpp:160`, `runtime/src/ui/settings_save_model.cpp:36` | files | none to the frame |
| Host sampler | `native/host/host_main.cpp:692` | the process's own figures | figures to the logs |

The remaining threads of `runtime/src` (network, mods' services, controllers, updates, Discord) do not take part in a
frame; they are listed in 1.6 with what each may touch.

### 1.2 The frame list: written, handed over, released

1. The game writes a `NebulaFrameList` and submits it: `native/host/host_main.cpp:2770` (`svc_submit_frame`) stamps
   the game frame, then `galaxy::render::attach_frame_list` (`runtime/src/render/render_api.cpp:63`) stores it as the
   producer's list, and `submit_frame` asks for a picture.
2. On the game thread, `RenderCore::render_frame` (`runtime/src/render/render_core.cpp:549`) takes the list into a
   `FrameChunk` (:570-592) with the registries' and the texture store's queued work (:584-585), taken under the device
   gate's hold (:581).
3. The game thread waits there for room in the render queue (:595-626): `queue_place` decides from the queue's depth
   (`render_queue_depth`, a stalled render thread gets `kStalledRenderQueueDepth`) and the render thread's busy time;
   the wait is `queue_space_cv_` (:622, :624). The time it waits is counted as the game's wait (`GameWaitTimer`, :609).
   Then the chunk joins `frame_queue_` (:636) and `queue_cv_` wakes the render thread (:638).
4. The render thread takes the front chunk (:1467-1468), or drops a chunk made for a lost device's epoch and releases
   its list at once (:1459-1463).
5. A list is released when the render thread no longer needs it: it keeps the newest list it walked and the one before
   (the in-between pictures read both), and `release_lists_before` (:1803) releases every older one with a release
   fence first (:1806), so every read of the list comes before the game writes it again. The game never waits for a
   release: it writes the next step into a store whose list is released, or makes one more store
   (`native/petari/src/Game/Render/FrameList.cpp:343-361`, `store()`).
6. Waits on the whole render thread: `RenderCore::wait_for_render_idle` (:687) holds the game until the queue is empty
   and no chunk is in flight. It is called at `native/host/host_main.cpp:2100` and :3228 (named in 1.5 with why).

So today the game and the render thread share two things per frame: the list (owned by the game until submitted, by
the render thread until released) and the chunk's queued registry and texture work (taken under the device gate).

### 1.3 The render thread's loop

1. `RenderCore::render_thread_main` (`runtime/src/render/render_core.cpp:1106`) names the thread, raises it above the
   normal priority on Windows (:1111), opts the process out of power throttling and holds the 1 ms timer (:1117-1124),
   then runs `render_loop` (:1316) until it ends; a lost device runs `restart_device` and the loop again (:1127-1132).
2. With nothing queued it sleeps on `queue_cv_` for up to 100 ms hidden or 250 ms shown (:1419), so a service waiting for
   a new card can reach it (DeviceGate).
3. A display slot that is due goes before queued chunks (:1423-1434): presents keep their cadence, and a saturated GPU
   cannot render every endpoint unseen.
4. It takes the front chunk (:1467); a render chunk followed by a present-only one takes that present with it
   (:1470-1478). In-flight counts (`render_chunks_in_flight_`, `present_chunks_in_flight_`, :1482, :1485) are what
   `wait_for_render_idle` (1.2 item 6) waits on; each taken chunk frees queue room (`queue_space_cv_`, :1464, :1497).
5. A present-only chunk presents the cached picture again, or hands its moment to the in-between sequence (:1498-1516);
   with no slot clock (an unlimited rate) it presents again once the last present is done (:1527-1535).
6. A render chunk is walked (`walk_frame`, :2141), and the lists older than the one before are released (1.2 item 5).
7. On a lost device, `drop_device_state` (:1161) releases every queued and walked list (:1164-1180) and clears the walk
   and the in-between sequence (:1185-1212), so no list stays held by a dead device.

### 1.4 What the render thread waits for on the GPU

Two frames are in flight (`kFramesInFlight`, `runtime/include/galaxy/render/renderer_common.h:49`).

| Renderer | Per frame | Inside a frame, the whole GPU |
| --- | --- | --- |
| Direct3D 12 | the slot about to be used again (`renderer_d3d12.cpp:3809-3817`); the oldest history ring segment when it is reused (:3966-3970); the frame latency waitable, 100 ms bound (:4356); the copy queue's slot before its staging is written again (`gpu_texture_backend_d3d12.cpp:513`, from `texture_store.cpp:666`) | `wait_for_gpu` (:916) in `reset_sampler_frame` (:1410), `prepare_temporal_upscaler` (:5019, :5052), `record_temporal_upscale` (:5214), and a capture's readback (:6889, :6892) |
| Vulkan | the slot (`runtime/src/render/vk/renderer_vk.cpp:2012`); the history ring segment (:2110) | `vkDeviceWaitIdle` when a size no longer matches: the temporal upscaler's preparation and record (`runtime/src/render/vk/renderer_vk_frame.cpp:922`, :953, :1428) and frame generation's HUD-less targets (:1455, :1495, :1548); the sampler array's restart waits for the last submitted frame (:2710, :2729); the post-process's self-test, in the first frame it is built (`runtime/src/render/vk/post_process_vk.cpp:947`, 1.8) |
| Direct3D 11 | the slot (`runtime/src/render/d3d11/renderer_d3d11.cpp:530`); the frame latency waitable, 100 ms bound (:653) | none; a window size change waits for the GPU idle (`runtime/src/render/d3d11/renderer_d3d11_scene.cpp:246`) |
| Metal | the slot (`runtime/src/render/mtl/renderer_mtl.mm:592`, `wait_for_fence_value`); the history ring segment (:679); a drawable (`nextDrawable`, :1183, in `present`) | `wait_for_gpu_idle` (:499) when the temporal upscaler's size no longer matches (`runtime/src/render/mtl/renderer_mtl_draw.mm:736`, :757); the sampler array restarts when nearly full and waits for the last submitted frame (:244-247, the wait :261; P-066); the post-process's self-test, in the first frame it is built (`runtime/src/render/mtl/post_process_mtl.mm:792`, 1.8) |

Outside a frame, `wait_for_gpu` drains the GPU at a window size change (:1955), a swap chain's re-creation or resize
(:3371, :3419), an HDR switch (:4164), shutdown (:3114, :3141) and the pictures at exit (:6754). Those are allowed by the
rules of part 2; the waits inside a frame are not, and are in the list (1.9). Metal drains at a window size change (`runtime/src/render/mtl/renderer_mtl_scene.mm:353`), the pictures at exit
(`runtime/src/render/mtl/renderer_mtl_draw.mm:618`) and shutdown (`runtime/src/render/mtl/renderer_mtl.mm:366`). Vulkan drains the same way at window size changes,
a swap chain's re-creation and shutdown (`runtime/src/render/vk/renderer_vk_frame.cpp:424`, `runtime/src/render/vk/renderer_vk.cpp:1062`, :3022, :1723),
when FSR's swap chain is made anew (`runtime/src/render/vk/renderer_vk.cpp:1067`), when the surface is let go (:1449) and for the pictures
at exit (`runtime/src/render/vk/renderer_vk_frame.cpp:3212`); Direct3D 11 at shutdown (`runtime/src/render/d3d11/renderer_d3d11.cpp:374`).

With vsync off and a frame-rate cap, each renderer's `PresentPacer::pace` sleeps up to 2 ms toward the present's slot
before it presents (`runtime/include/galaxy/render/renderer_common.h:181-202`; `renderer_d3d12.cpp:4311`, `runtime/src/render/vk/renderer_vk.cpp:2246`,
`runtime/src/render/d3d11/renderer_d3d11.cpp:649`, `runtime/src/render/mtl/renderer_mtl.mm:736`): a wait of the render thread on the clock, not on the GPU,
and pacing's (E6b).

The game thread waits on the render thread at two places: queue room (1.2 item 3) and the render thread idle (1.2 item
6). A third, for frame F - 2's screen probe answers (N-1068), went with the whole question-and-answer path at S0.2.6:
nothing of the step reads the picture.

### 1.5 What the game thread waits for

The game thread runs the game module's loop; each wait below is a call it makes into the host (`native/host/host_main.cpp`).

| Wait | Where | Bound | Kind |
| --- | --- | --- | --- |
| The next step's moment | `svc_frame_wait` (:1133): the display's vertical blank on the SDL hosts (:1159), else a precise wait for the host clock's due time (:1176, `PreciseWait`), recorded as `StepWaitBegin`/`StepWaitEnd` | the step period (`kGameStepNs`) | pacing, every frame |
| Suspended (minimised, or the system's suspend) | `wait_while_suspended` (:1027), from the step wait (:1181) | until resumed | by design |
| Room in the render queue | `render_frame`'s wait (`render_core.cpp:595-626`, 1.2 item 3) | none of its own | every frame the render thread is behind |
| The render thread idle | the output's shape latched (`svc_frame_begun`, :3224-3232, `wait_for_render_idle`) | none | a window or aspect change |
| The last save request | `svc_save_wait` (:3491) to `StatusBoard::wait` (`runtime/src/save_files.cpp:179`) | none | the game's own `waitRequest` |
| A file's bytes | `svc_asset_read` (:3144): a copy from the store's mapped view (`gPak.store.read`), so a page not in memory is read from the drive on this thread | the drive | loads |

The start check's wait for each list's release (:2086-2094) and its final `wait_for_render_idle` (:2100) are the
`--start-check` mode's own, not the game's.

Two of these hold the game for the whole render queue: the aspect latch (all queued frames, then the picture is fitted)
and a render queue already at its depth. The asset read is the one wait on the drive inside a step; every other file the
game touches goes through the save worker.

### 1.6 The other threads, and what of the frame each touches

| Thread | Touches | Hand-over |
| --- | --- | --- |
| Window owner (`window_owner.cpp:20`) | the window's size, focus, input | the size reaches the aspect latch (1.5) and the swap chain's resize (1.4, a whole-GPU wait) |
| Display clock (`vblank_clock_win32.cpp:175`, started once by `vblank_grid`) | the vertical blank's time and period | under `g_mutex`, read by the step wait and the present slots |
| Pipeline workers (`runtime/src/render/core/pipelines.cpp:104`, lowest priority) | compiling pipeline states | each published to an atomic cell (`cell` :145, `publish` :164); a draw whose pipeline is not there yet compiles it on the render thread (`create_now` :253; a quick link where pipelines link from parts, :271) |
| Record workers (`runtime/src/render/core/draw_record.h`, `RecordPool`) | encoding a pass's chunks | fork and join inside the walk: the render thread takes a share and returns when all are done |
| Texture pack workers (`texture_pack.cpp:176`) | reading and decoding replacements | decoded pixels picked up at `pump` once a frame; the upload is the render thread's |
| Movie (`movie_playback.cpp:85`, :357, :903; :1200 prepares the decoder) | decoding a movie | pictures through the stager to the render thread |
| Streamline start (`renderer_d3d12.cpp:2314`) | loading the vendor's library | joined at device creation, before any frame |
| Picture writer (`renderer_common.cpp:427`) | writing a captured picture to disk | the pixels are copied out first; counted so the end waits for it |
| HDR watcher (`os_hdr.cpp:393`) | the system's HDR state | read by the swap chain's HDR switch (1.4, a whole-GPU wait) |
| Telemetry publisher (`frame_telemetry.cpp:104`) | the frame counts | samples them into shared memory for outside readers |
| Texture categories index (`texture_categories.cpp:347`) | reading the store's index | `index_ready` (acquire) before any lookup |
| Sound, save writers, host sampler, input (`host_main.cpp:3906`) | none of the frame | — |

Not part of a frame: controllers (`controller_hid.cpp`, `wiimote_hid.cpp`, `switch2_ble_win.cpp`), mods' services
(`luma_net.cpp`, `luma_fs.cpp`, `luma_mods.cpp`, `luma_assets.cpp`, `luma_capture.cpp`, `luma_input.cpp`), the settings
screen's workers (`runtime/src/ui/native_settings.cpp`), updates (`runtime/src/update/update.cpp`), vendor downloads, Discord. Nor the host's
and the platforms' own: the crash writer (`native/host/host_main.cpp:1904`), the benchmark report's sender (:1965),
the start-again hand-over (:4513, :4603), the mods' background cook (:5505; it waits for the layers the mods' code
chooses, and nothing waits for it inside a step), the sound clock of a build without a renderer (:6507), the SDL
hosts' log reader (`platform/sdl/host_window_sdl.cpp:249`), their system jobs (`platform/sdl/luma_system_sdl.cpp:162`)
and thread sampler (`platform/sdl/thread_sampler.cpp:197`), and Apple's setup (`platform/apple/setup_apple.mm:192`).

### 1.7 The walk (`runtime/src/render/core/draw_walker.cpp`)

The render thread turns one list into GPU work in `walk_list` (:1464): `prepare_walk` (:194), then each pass in list
order (`walk_pass` :1523), then `walk_show` (:1787). What it does on the CPU, each walk, for each picture (an in-between
picture walks the same list again at its own t):

1. Cameras at t (:219-223): each of the list's cameras, moved to t when the picture is an in-between one.
2. Matrix rows (:225-316): for every block, `compose_rows` makes its rows in eye space, the camera's view times each
   joint, on the CPU; a skinned model's envelopes are made here too (:263-273), and a block in calc mode 1 arrives with
   the view already in its joints, so its world rows take the view out again (:309-313). Both poses are blended here for
   an in-between picture (`joints_at`, :288-294). The rows go to the matrix ring.
3. Light sets (:318 on) as the game gives them, blended at t.
4. The frame's vertices (:399 on): the vertices the game built this step on the CPU are copied into the ring once a
   walk; for an in-between picture their positions are moved to t on the CPU first, in a copy.
5. Views (:457 on) and each view's side planes in eye space (:482), for culling on the CPU.
6. Binding (`walk_bound_tables` :838): a target's picture is a texture made this frame, so its table lives one frame
   (:878); every draw that binds a table names its textures again (:920).
7. Draws (`walk_draw_items` :1102): consecutive draws of one mesh and material become one instanced draw, consecutive
   ring draws of one material one draw (:1145, :1166); a pass of many draws is recorded in chunks on the record workers
   (:1036, :1086).
8. Passes (`walk_pass` :1523): a target pass (the console's copy of the frame into a texture) breaks the frame's pass
   where it stands, so on a tiled GPU the frame's colour, depth and stencil are stored and loaded around it (:1461); one
   that reads nothing of the frame is moved first (:1501). A copy asked before the list writes the frame takes the
   picture shown last, "as the console's frame buffer still held it" (:1583). The scene's camera is taken at its opaque
   end, because the game loads its projection after the scene begins (:1356). The HUD and menus are passes of the same
   list after the scene (:1399, :1408).
9. `draw.post` (`walk_post` :1667) and `draw.native` (`walk_native` :1716): the frame so far becomes a mod program's
   input; the scene's depth is turned back into distance from GX's perspective terms (:1681-1696).

So the CPU work of a picture grows with the blocks, joints and vertices of the scene, and an in-between picture repeats
it: the records hold eye-space work for one camera, not geometry the GPU places itself.

### 1.8 After the scene: post-process, upscalers, frame generation

Read for the rules (whole-GPU waits, compiles, file reads on the render thread): `post_process.cpp`,
`runtime/src/render/vk/post_process_vk.cpp`, `runtime/src/render/mtl/post_process_mtl.mm`, `temporal_upscaler.cpp`, `runtime/src/render/vk/temporal_upscaler_vk.cpp`,
`xess_upscaler.cpp`, `runtime/src/render/mtl/metalfx_mtl.mm`, `runtime/src/render/mtl/sgsr2_mtl.mm`, `fsr_frame_generation.cpp`,
`fsr_frame_generation_core.inl`, `runtime/src/render/vk/fsr_frame_generation_vk.cpp`.

- Direct3D 12: the post-process programs are made at the renderer's start (`renderer_d3d12.cpp:2713`), where a
  self-test of the anti-aliasing waits for the GPU once (`post_process.cpp:296`, :794); allowed, it is before any frame.
  The temporal upscaler's preparation and record drain the whole GPU (1.4; P-028, written).
- Vulkan: the bloom's pipelines are built in `begin_frame` the first time it runs (`runtime/src/render/vk/renderer_vk.cpp:1997`,
  `runtime/src/render/vk/game_bloom_vk.cpp:52`) and the post-process's in `sync_post_process` the first frame that needs them
  (`runtime/src/render/vk/renderer_vk.cpp:2821`, `runtime/src/render/vk/post_process_vk.cpp:441`): compiles inside a frame, and the post-process's
  self-test then waits for the GPU in that frame (`runtime/src/render/vk/post_process_vk.cpp:947`). A swap chain made again builds
  the present pipelines again (`runtime/src/render/vk/post_process_vk.cpp:152-170`, from `create_swapchain`), outside a frame's walk.
- Metal: the post-process and bloom compile their MSL on a worker from the renderer's start
  (`runtime/src/render/mtl/post_process_mtl.mm:112-130`, `runtime/src/render/mtl/game_bloom_mtl.mm:46-56`); the render thread makes their pipeline states
  from it the first frame it is ready (`poll_ready`, `post_process_mtl.mm:272`, `game_bloom_mtl.mm:107`), and the
  post-process's self-test waits for the GPU there (`post_process_mtl.mm:792`). Ambient occlusion compiles at the
  renderer's start (`runtime/src/render/mtl/ambient_occlusion_mtl.mm:59`, from `runtime/src/render/mtl/renderer_mtl.mm:118`). The utility pipelines
  (`runtime/src/render/mtl/renderer_mtl_scene.mm:56`, from `utility_pipeline` :67) and the scene camera's (`runtime/src/render/mtl/scene_camera_mtl.mm:45`,
  from `pipeline` :108) compile from source on the render thread when a key is first asked for; SGSR 2
  (`runtime/src/render/mtl/sgsr2_mtl.mm:58`) and the MetalFX depth copy (`runtime/src/render/mtl/metalfx_mtl.mm:244`) when their upscaler is made; the game's
  programs from their text when their pipeline is made (`runtime/src/render/mtl/pipelines_mtl.mm:283`).
- The upscalers' and frame generation's bodies (XeSS, FSR, the Vulkan temporal upscaler, SGSR 2) wait for no fence and
  read no file in a frame.

### 1.9 What breaks the frame's rules today, or keeps the console's shape

The rules (part 2): the render thread never compiles, never reads a file and never waits for the whole GPU inside a
frame; the game thread waits for the render thread only at the points this file names. Each part with its row; a part
with no row has a new one.

| Part | Where | Row |
| --- | --- | --- |
| A draw whose pipeline is not ready compiles it on the render thread | `runtime/src/render/core/pipelines.cpp:253` (`create_now`) | P-010, E3 step 1 (a form known at load) |
| Utility pipelines compiled on the render thread at first use, outside the cache | `runtime/src/render/core/pipelines.cpp` | P-052 (written, engine/b) |
| A present waits for every pipeline creation in flight (Direct3D 12) | `runtime/src/render/core/present_gate.h` | N-1067, E3 step 1 |
| The sampler heap or array, full, waits for the whole GPU | `renderer_d3d12.cpp:1410`; `runtime/src/render/vk/renderer_vk_frame.cpp:2710`, :2729; `runtime/src/render/mtl/renderer_mtl_draw.mm:261` (each `reset_sampler_frame`) | P-048, P-056, P-066, E3 step 2 |
| Size changes drain the whole GPU inside a frame | `renderer_d3d12.cpp:5019`, :5052, :5214 (the temporal upscaler), :1955; Vulkan's at size changes inside a frame (1.4) | P-028 (written for Direct3D 12 on engine/b; Vulkan's side open) |
| The game thread waits for the whole render queue at an aspect change | `host_main.cpp:3228` | N-1081 |
| A capture's readback waits for the GPU inside a frame | `renderer_d3d12.cpp:6889`, :6892 | N-1082 |
| The game thread copies loads from the mapped store: a page not in memory is a drive read inside a step | `host_main.cpp:3144` (`svc_asset_read`) | N-1083 |
| Matrix rows made in eye space for one camera on the CPU, each walk and again for each in-between picture; skin envelopes on the CPU | `draw_walker.cpp:225-316` | E3 step 5 |
| A block in calc mode 1 comes with the view inside its joints (the console's billboards and camera-bound models) | `draw_walker.cpp:309-313` | E3 step 5 (resolved per view on the GPU) |
| Vertices the game builds on the CPU each step, copied each walk and moved at t on the CPU | `draw_walker.cpp:399` on | E3 step 4 |
| Culling against the view's planes on the CPU | `draw_walker.cpp:482` | E3 step 5 |
| A target pass (the console's copy of the frame into a texture) breaks the frame's pass: store and load on tiled GPUs | `draw_walker.cpp:1461` | E4 (render-to-texture passes first) |
| A copy before the list writes the frame reads the picture shown last, as the console's frame buffer held it | `draw_walker.cpp:1583` | E4 (the last picture asked for by name) |
| The scene's camera found at its opaque end, as the game loads its projection after the scene begins | `draw_walker.cpp:1356` | E4 (`GameScene::draw3D` states its passes) |
| A target's texture table rebuilt each frame; each draw names its textures again | `draw_walker.cpp:878`, :920 | E3 step 2 (each form's texture list) |
| Vulkan's bloom and post-process pipelines built on the render thread in the first frame that needs them, and the post-process's self-test waiting for the GPU there | `runtime/src/render/vk/renderer_vk.cpp:1997`, :2821; `runtime/src/render/vk/post_process_vk.cpp:947` | P-052 (its Vulkan half: open) |
| Metal's own pipelines made on the render thread in a frame: the utility and scene camera pipelines compiled from source there, the post-process's and bloom's states made from a worker's compile, and the post-process's self-test waiting for the GPU | `mtl/*_mtl.mm` (1.8) | P-065 (its text says every compile is on the render thread: the post-process's and bloom's are on a worker, ambient occlusion's at the start) |

This list covers the render path as read for this file: every file of 1.1 to 1.8, with the files stage 2 item 1
names (draw_walker.cpp whole; the post-process, upscaler and frame-generation files for waits, compiles and file
reads). The three parts that had no row are N-1081 to N-1083. Beyond the frame's rules, 1.10 lists the console's shape
elsewhere.

### 1.10 The console's shape outside the frame's rules

The owner's standard is wider than the frame's rules: no part of the engine shaped by the console. Every open row
whose title names the console:

| Row | Title, short | Pile | Step |
| --- | --- | --- | --- |
| P-142 | lights loaded per draw in the console's eight-register model | B | F4, E3 step 6 |
| P-143 | shadow volumes drawn twice with vertices rebuilt | B | F4, E3 step 4 |
| P-149 | particles built as eye-space matrices on the CPU | B | F4, E3 step 4 |
| P-150 | the particle material copies GX alpha-compare and combiner settings | B | F4, E3 step 4 |
| P-151 | the material path keeps the console's per-draw register model | B | F4, E3 step 3 |
| P-154 | the "site" draws work as the console's immediate mode | B | F4, E3 step 4 |
| P-155 | a texture known by its bytes' address and the console's header | B | F4, E3 step 2 |
| P-156 | each picture recomputes eye-space matrices on the CPU | B | F4, E3 step 5 |
| P-157 | the imported material format stores the console's register layout | B | F4, E3 step 1 |
| P-173 | a material touched once never returns to its slot | B | F4, E3 step 3 |
| P-059 | texture records analysed on the game thread at registration | B | F4, E3 step 2 |
| P-042 | the default frame rate is the game step, 60, everywhere | A | after the foundation (the owner's decision) |
| P-134 | collision prisms store one vertex, the other two rebuilt per query | A | after the foundation: the step's speed |
| P-118 | the THP movie decoder keeps the SDK's IDCT column swap | A | after the foundation (E8) |
| P-148 | a sensor's touching list fixed at its group size | A | after the foundation (E8; gameplay, the owner's call) |
| P-166 | Binder keeps every plane: push-out can differ from the console's | A | after the foundation (E8; gameplay) |
| P-168 | collision arithmetic rounds differently from the console | A | after the foundation (E8; gameplay) |
| P-074 | a Wii Remote Nunchuk takes its centre from the first report | A | after the foundation |

The engine's areas this file does not walk, by what the ledger, the audit's master report (sections 6 and 10) and the
inventory's pass two (`CONSOLE-FREE.md` 2, 11-18) hold of the console's shape there:

| Area | The console's shape known | Read |
| --- | --- | --- |
| The game's arenas | the console's heaps are gone (NebulaArena); objects and deletes search every arena linearly (P-013), new and delete belong to one thread (P-012, fixed) | read whole for P-012 and P-013 |
| Sound | the sequences read in the console's byte order (P-186); surround by a written law (P-193); the mixer's rows (P-037, P-071, P-072) | the sound engine and the mixer read, the rest swept by pattern, every hit read |
| Input | sampled once a game step for the star pointer and the menu (P-078, P-163; F6); the Nunchuk's centre (P-074) | read (master report 6) |
| Files and archives | the step copies its loads from the mapped store on the game thread (N-1083); a particle's index in 16 bits (P-188) | the load path read: no archive or compression is read while the game runs |
| Saves | none known; the faults that lose data are section 4.6's rows | read (master report 6) |
| Text | 16-bit character codes, the console font's range (P-187); the messages are cooked (`NMSG`) | the record and its readers read |
| Layouts and fonts | a layout material keeps the console's codes (P-184); a glyph sheet found by its bytes' address (P-155) | `Layout::Build`, `Material`, `ResFont` read |
| The game's actor classes | actors that draw for themselves (P-194); what runs follows the screen's shape (P-191); shared counts and id widths (P-192) | swept by pattern, every hit read; read whole in the whole read (file 08 5.H.1) |

## Part 2. The target

### 2.1 The rules

1. The render thread never compiles a program or pipeline, never reads a file and never waits for the whole GPU
   inside a frame. Every pipeline a frame can need is known when its content loads (a material's form, a utility
   pass) and made then, on the workers; a size change makes its targets beside the old ones and retires the old ones
   by the frame fence, as any other resource.
2. The game thread waits for the render thread at one point only: room in the render queue, bounded by the queue's
   depth. A picture's shape and a capture are answered later, by the frame they belong to, never by holding the
   step. Nothing of the step asks the graphics card a question (the screen probes went at S0.2.6).
3. The game thread reads no file inside a step: what a step loads was read ahead by the loader's thread.
4. Each hand-over has one owner at a time and a release and acquire pair (1.2's list, the pipeline cells, the decoded
   textures); a debug build asserts the thread at each hand-over (`render_thread_only()`, `game_thread_only()`: neither
   exists yet; each hand-over gets its assert in the step that rewrites it).
5. The CPU work of a picture does not grow with what the scene holds: an in-between picture is a GPU pass over both
   poses, not a second walk.

### 2.2 The threads and hand-overs

The threads stay as 1.1 lists them. What changes is what crosses between them:

| From | To | What | When |
| --- | --- | --- | --- |
| Loader (content) | workers | a model's or effect's records, its material forms and texture lists | when content loads |
| Workers | render thread | made pipelines (atomic cells), uploaded geometry, decoded textures | as they are made; a draw whose form is not ready waits for it at load, never in a frame |
| Game | render thread | per step: both poses of each object (transforms, joints), what changed in materials, the passes and what each reads and writes | once a step, in the frame list |
| Render thread | GPU | one pass that makes each picture's matrices from both poses at t, then the frame as the planner ordered it | each picture |
| Render thread | game | the released list; pictures for the frame that asked | at release |

### 2.3 What the records must allow later

The next model format is designed once for these (file 01 calls it 12; main's model is 12 since b6f26c82a, so
it is 13); no code for them now, and ray tracing stays off main.

- Nothing in a record is computed for one camera on the CPU: billboards, particles and coordinates that read the eye
  are marked and resolved per view on the GPU (today: calc mode 1's view inside the joints, `draw_walker.cpp:309-313`;
  eye-space rows, :225-316).
- Every drawn thing names geometry the renderer holds, plus a transform; skinning inputs live on the GPU; a vertex
  stream still built on the CPU is marked as such (today: the step's vertices, `draw_walker.cpp:399`).
- Each material form says what it is beside its program: the base map, the glow, the alpha mode, two-sided or not,
  which vertex colour is baked light, what each coordinate source is, whether it reads a copy of the screen.
- Lights and shadow shapes are records in world space (today: light sets as the game gives them each step).
- An object outside the view can stay in the list, marked (today: culled on the CPU, `draw_walker.cpp:482`).
- Both poses of each object (E3 step 5).
- The importer writes normals where a mesh has none.
- A later field does not make a format 14, and a mod's cooked model keeps loading: the format carries a table of
  sections by name and size, and a reader skips what it does not know.

What the removed tracer had to guess or rebuild, each a line here (its commits 7ecc72a4c, ca78c16cd, b6f26c82a; P-172
is the first case):
- a material's surface kind: alpha-tested by look rules, water from a hand table (`rt_water.tsv`) and five code-drawn
  water classes by name: the form names its kind (opaque, alpha-tested, blended glass, water);
- which colour of a material carries its unlit look and which material of a model glows (`rt_glow.tsv`, glow_source),
  read from the draw's live row so it follows the animation: the form names its glow row (E3 step 1);
- a glowing mesh's shape (wider than 6 m in two axes is no point light): emitters are records with their shape;
- a painted environment map, and a painted surface that holds its own shadows: the form names a coordinate source that
  reads the eye and a texture or vertex colour that is baked light;
- the generated programs' light sums, with the ambient term added by hand (AMBIENT_TERM): a form's lighting names
  its ambient and its light terms apart;
- actors outside the view kept drawn up to 30 m for shadows and mirrors: an object outside the view stays in the
  list, marked;
- a hit record per material and its twins, found by reading every model once more before a cook: a material form has
  one id across the models that share it;
- the stage's and the effects' light (`RtStages.tsv`, `RtEffects.tsv`): a stage's sky and sun, and an effect that
  lights, are records.

### 2.4 Model format 13

What the importer writes, by E3's steps (file 01, E3, where it is named 12); its sections, records and the switch
between the two paths are in 2.7:

| Step | Written at cook | Ends in the engine |
| --- | --- | --- |
| 1 | the section table (2.8); the material apart from the console's fields (2.9: `MATL`, `FORM`, `GXMT`); a form's descriptions of 2.3 (the glow row, the alpha cutoff and mode) each with its first reader (2.9) | every pipeline made at load (P-010, N-1067). The search for console register codes at load that file 01 names here was the tracer's and went with it (E13a stage 4a, a778266de): no `NEBULA_ROW_` is read in `runtime/` (grep: no file), and no stage record is written. What is left of P-157 is the register-shaped record (`NebulaModelMaterial`, `nebula_model.h:246-264`), read per draw by `Material::fillRow` (`Material.cpp:296-424`): step 3 |
| 2 | samplers in final form, each form's texture list | hashing a texture's address and decoding filter codes per draw; the per-frame table rebuilds (`draw_walker.cpp:878`, :920); the sampler heap's and arrays' waits (P-048, P-056, P-066) |
| 3 | per form: a block of final constant rows, and the short list of rows that change, with their source | filling register rows at draw time |
| 4 | prepared material records for the immediate-style draw sites and particles; fixed shapes as meshes registered once | vertices and indices built on the CPU each frame |
| 5 | (engine only) both poses once a step; a GPU pass makes each picture's matrices; skinning on the GPU | matrices, skin sums and blends on the CPU for every picture |
| 6 | lights as world-space records | lights per object in the eight-register form |

### 2.5 The pass list

Each pass of the frame list states what it reads and writes; a planner on the render thread orders them (E4):

| Pass | Reads | Writes | Order |
| --- | --- | --- | --- |
| Targets (render to texture: shadows, reflections, the game's screen copies) | the scene's records | its own texture | before the scene, so the scene opens once |
| Scene, opaque then translucent | records, targets | the scene's colour, depth, motion | one pass instance, loads and stores chosen by what reads it later |
| Scene copy (a material that reads the screen) | the scene | a copy | only when something visible reads it, once |
| Mod post (`draw.post`), ambient occlusion, bloom | the scene, depth | the scene | after the scene |
| Upscale (temporal or spatial) | scene, depth, motion | the display-size picture | after post |
| 2-D layer (HUD, menus) | layouts | the display-size picture | merged with bloom's composite and the final picture where no upscaler sits between |
| Show | the picture | the swap chain | last; "the last picture" is asked for by name, the last real frame |

### 2.6 Each open row of stages 2 to 6: its pile and the step that does it

Pile B: the foundation changes or replaces its code, done once in that step. Pile C: it needs the new model under
it. Pile A: the foundation does not touch its code; it comes after. Where this differs from the planner's reading
(file 07), the line says why.

| Row | Pile | Step | What | Note |
| --- | --- | --- | --- | --- |
| P-022 | B | F3 | The same per-renderer logic is written four times. |  |
| P-010 | B | F4, E3 step 1 | A draw whose pipeline is not made yet compiles it on the render thread |  |
| N-1067 | B | F4, E3 step 1 | a Present waits for every pipeline creation in flight (core/present_ga |  |
| P-157 | B | F4, E3 step 1 | the imported material format still stores the console's register layou |  |
| P-052 | B | F4, E3 step 1 | Utility pipelines are compiled on the render thread at first use, outs | pile A in the planner's reading: its Direct3D 12 half is written; Vulkan's first-frame bloom and post builds (1.8) are open |
| P-065 | B | F4, E3 step 1 | performance (hitches; Metal): the renderer's own shaders are compiled  | planner: A; it breaks rule 1, so it ends where every pipeline is made at load |
| P-155 | B | F4, E3 step 2 | a texture is still known by the address of its bytes and described by  |  |
| P-048 | B | F4, E3 step 2 | The D3D12 sampler heap is drained with a full GPU wait when it fills.  |  |
| P-056 | B | F4, E3 step 2 | When the sampler array fills, the Vulkan reset waits for the last subm |  |
| P-066 | B | F4, E3 step 2 | performance: Metal waits for the GPU whenever its sampler array nears  |  |
| P-061 | B | F4, E3 step 2 | performance (and a limit that can fail): the D3D11 sampler cache is ne |  |
| P-059 | B | F4, E3 step 2 | Every texture record with authored-looking levels is analysed on the g |  |
| P-151 | B | F4, E3 step 3 | the material path keeps the console's per-draw register model: texture |  |
| P-173 | B | F4, E3 step 3 | A material touched once never returns to its registered slot |  |
| P-060 | B | F4, E3 step 3 | performance: every recorded draw renames the root constant buffer with |  |
| P-064 | B | F4, E3 step 3 | performance: D3D11 upload_written scans every upload buffer on every f |  |
| P-154 | B | F4, E3 step 4 | the "site" draws (effects, shadows, 2D, gauges, glows) still work as t |  |
| P-149 | B | F4, E3 step 4 | every particle is built as a matrix in eye space on the CPU, multiplie |  |
| P-150 | B | F4, E3 step 4 | the particle material copies GX alpha-compare, combiner-stage and indi |  |
| P-143 | B | F4, E3 step 4 | each shadow volume is drawn twice, front faces then back faces, with t |  |
| P-181 | B | F4, E3 step 4 | Shadow shapes are rebuilt on the CPU every draw, and the oval surface  |  |
| P-145 | B | F4, E3 step 4 | the scenario-select and mod mesh draw buffers merge same-material entr |  |
| P-038 | B | F4, E3 step 4 | Draw-buffer packet insertion, removal and light re-sort are linear per |  |
| P-127 | B | F4, E3 step 4 | GameScene::draw does work every game frame that the shipping build nev |  |
| P-128 | B | F4, E3 step 4 | each CategoryList call fetches the list executor through two pointer h |  |
| P-156 | B | F4, E3 step 5 | every presented picture recomputes, on the CPU, the eye-space position |  |
| P-144 | B | F4, E3 step 5 | each animated model's joint matrices are copied twice per frame after  |  |
| P-046 | B | F4, E3 step 5 | Ring-run merging patches matrix numbers and indices in place in write- |  |
| I-003 | B | F4, E3 step 5 | performance (T3). Each in-between picture is a full CPU walk and a ful | the in-between picture as a GPU pass (rule 5) |
| P-142 | B | F4, E3 step 6 | lights are loaded per object draw in the console's eight-register mode |  |
| P-140 | B | F4, E3 step 6 | every moving actor asks for its shadow to be recomputed every step, an |  |
| P-152 | B | F4, E3 step 6 | every visible actor scans every light area every step to find which on |  |
| P-006 | B | F5, E4 | Every Vulkan scene pass stores depth and stencil, and the multisampled |  |
| P-007 | B | F5, E4 | Vulkan parallel recording splits the scene pass into several render pa |  |
| P-025 | B | F5, E4 | With MSAA, bloom and scene AA resolve the scene and then copy the reso |  |
| P-027 | B | F5, E4 | Opening the HDR 2D layer takes two full-size conversion draws from the |  |
| P-031 | B | F5, E4 | The Metal scene pass always stores depth and stencil, MSAA stores the  |  |
| P-053 | B | F5, E4 | A resolve is recorded for each consumer even when nothing was drawn si |  |
| P-055 | B | F5, E4 | see the finding's own text |  |
| P-063 | B | F5, E4 | performance (D3D11 parity): D3D11 scene AA always copies the scene; D3 |  |
| P-054 | B | F5, E4 | Vulkan waits for the GPU in the middle of a frame whenever it makes a  |  |
| P-026 | B | F5, E4 | The present pass recreates its shader views every frame, and the post- |  |
| N-1068 | B | F5, E4 | the game thread waits for frame F-2's screen probe answers (RenderCore | fixed at S0.2.6: the question-and-answer path is deleted, nothing waits and the planner has no part for it (plan decision 9) |
| P-028 | B | F5, E4 (a size change makes new targets beside the old, retired by the fence) | Several size changes drain the whole GPU in the middle of a frame (`wa | planner: A; Direct3D 12 written on engine/b, Vulkan's drains at size changes (1.4) open |
| P-030 | B | F5, E4 (a size change makes new targets beside the old, retired by the fence) | The frame-generation and FSR 3 paths call `vkDeviceWaitIdle` from the  | planner: not placed |
| P-049 | B | F5, E4 (a size change makes new targets beside the old, retired by the fence) | DLSS is created on the render thread inside begin_frame, after a full  | planner: not placed |
| P-050 | B | F5, E4 (a size change makes new targets beside the old, retired by the fence) | The FSR 3 frame-generation context is recreated mid-frame, with the co | planner: not placed |
| P-057 | B | F5, E4 (a size change makes new targets beside the old, retired by the fence) | Vulkan creates DLSS synchronously on the render thread, as D3D12 does  | planner: not placed |
| I-016 | B | F2 | gap in tests. The scheduler is not under test (J.8): the arrival clock |  |
| I-002 | C | F6, E6b | design gap (T3, T6). On vblanks, the real picture of a step is almost  |  |
| I-013 | C | F6, E6b | wrong behaviour (minor), suspected. FramePhase measures the wrong work |  |
| P-034 | C | F6, E6b | The Apple display link is never stopped. |  |
| I-004 | C | F6, E6b | wrong behaviour (T4). Interpolation adds about one to two steps of lat |  |
| I-005 | C | F6, E6b | wrong behaviour (T2, T7). Linux has no display clock. |  |
| I-006 | C | F6, E6b | wrong behaviour (T2), suspected. Metal drops a picture when `nextDrawa |  |
| I-007 | C | F6, E6b | wrong behaviour (T2), suspected. VRR detection and slot timing. |  |
| I-010 | C | F6, E6b | design gap (T6), suspected. Two governors act on the same GPU time wit |  |
| I-012 | C | F6, E6b | design gap (T7). Only Android Vulkan presents at a requested time; onl |  |
| I-014 | C | F6, E6b | wrong behaviour (T2), suspected. Game-declared camera cuts come from t |  |
| P-078 | C | F6, E6b | wrong behaviour (suspected). The star pointer and the mod look are sam |  |
| P-163 | C | F6, E6b | performance (input latency, small): the menu reads the mouse, touch an |  |
| P-040 | C | F7 | The step has no worker parallelism, and the shared state that blocks i | with it N-3013 and N-3014, the faults GitHub found in its frozen commits (`app/work`'s ledger, Agent 3's rows): the headless host's include, the pool counting processors the process may not use |
| P-146 | C | F7 | frame-list building, skeleton evaluation and collision results live in |  |
| P-082 | A | after the foundation: lost data | wrong behaviour (suspected). The in-game import's write is not ordered |  |
| P-094 | A | after the foundation: lost data | Setting up again deletes the installed game before the new one is buil |  |
| P-095 | A | after the foundation: lost data | A full device during setup ends with an untranslated OS error, and set |  |
| P-096 | A | after the foundation: lost data | Mod imports never check free space, show a raw ENOSPC text when the de |  |
| P-013 | A | after the foundation: the step's speed | Every object construction and every `delete` searches all arenas linea |  |
| P-039 | A | after the foundation: the step's speed | Sensor hits are found by testing every pair between groups, each step. |  |
| P-088 | A | after the foundation: the step's speed | performance: the open settings menu rebuilds its page twice a frame on |  |
| P-129 | A | after the foundation: the step's speed | every collision query walks every part in the global zone and in each  |  |
| P-130 | A | after the foundation: the step's speed | a moving part whose sphere touches its zone's box edge recomputes the  |  |
| P-131 | A | after the foundation: the step's speed | once any ball or line query has filled 64 hits, every later query allo |  |
| P-132 | A | after the foundation: the step's speed | ball queries against a moving part invert the part's previous matrix a |  |
| P-133 | A | after the foundation: the step's speed | prism lists deduplicate by linear search over the hits found so far, a |  |
| P-134 | A | after the foundation: the step's speed | each collision prism stores one vertex plus normals, and the other two |  |
| P-135 | A | after the foundation: the step's speed | the clipping pass is linear and cheap, but it calls MR::getClippingJud |  |
| P-136 | A | after the foundation: the step's speed | looking up a sensor by name hashes the string on every call, and actor |  |
| P-137 | A | after the foundation: the step's speed | picking a free actor from a pool scans the pool from the start every t |  |
| P-138 | A | after the foundation: the step's speed | a bound actor that does not move runs the same map query twice per ste |  |
| P-139 | A | after the foundation: the step's speed | gravity is found by testing every registered gravity for every actor a |  |
| P-141 | A | after the foundation: the step's speed | the emitter pool is walked in full, empty slots included, up to three  |  |
| P-058 | A | after the foundation | VMA is told Vulkan 1.1 even when the device and instance are 1.2 or 1. |  |
| P-117 | A | after the foundation | AV1 movie decode is plain C and can stall the render thread. |  |
| P-042 | A | after the foundation | The default frame-rate setting is 60, the game step, on every platform |  |
| P-005 | B | F0 (fixed) | the game side reused a frame list's store after reading `released` with no acquire | fixed on rows/p005; the test now runs under the thread sanitizer |
| P-012 | B | F0 (fixed) | the game's `operator new`/`delete` are not thread-safe | fixed on rows/p012; the game's unit tests run under the thread sanitizer |
| P-183 | B | F0 (fixed) | the creator index built lazily with no lock | fixed on rows/p183 (`NebulaOnce`) |
| P-044 | C | F2 | no unit test of the draw walker's planners, the display clocks or the Android layer | the arena and frame-list halves are done; F2 tests today's walker; E3's steps change the walker, so each step changes its tests with it |
| P-009 | C | after the foundation (E9) | D3D11 and Metal lack features the other two have; three silently (Metal drops mod post programs and native draws) | E4's pass list does not change its code: it makes mod post and native draws passes every renderer is asked to run, so a renderer that cannot run one says so instead of dropping it; Metal's own running of them stays its backend's work, written once on F3's shared code |
| P-032 | A | after the foundation (E9) | neither Metal nor D3D11 offers HDR output | E4's pass list does not change its code: HDR output is the Show pass's swap chain format and colour space, each backend's own; after F3 the shared part is written once |
| P-087 | A | after the foundation (E9) | options offered where the platform or renderer cannot do them |  |
| P-175 | A | after the foundation (E9) | FSR 3 runs with a moving render size but is never told it moves |  |
| P-176 | A | after the foundation (E9) | SGSR 2 and MetalFX temporal get wide scene colour with no HDR mode |  |
| N-1017 | A | after the foundation (E0) | the ending movie, the Mii picker and the full-screen switch have no harness test |  |
| P-014 | A | after the foundation (E0) | the Windows release links a stored PGO profile with its staleness warnings off | a profile taken before the foundation is stale after it: retaken after F7 |
| P-023 | A | after the foundation (E0) | release.yml builds from any v* tag and never starts what it built | the push workflow half is done (N-1045) |
| N-1084: a slot before its frame's arrival shows the frame before early (`frame_schedule.cpp`) | C | F6, E6b | the picture shows its own moment | found by I-016's test |
| N-1081: the aspect latch's whole-queue wait (`host_main.cpp:3228`) | B | F5, E4 | the game holds for the render queue | rule 2 |
| N-1082: a capture's readback inside a frame (`renderer_d3d12.cpp:6889`) | B | F5, E4 | a capture answered by its frame later | rule 1 |
| N-1083: loads read from the mapped store on the game thread (`host_main.cpp:3144`) | B | F7 | read ahead by the loader | rule 3 |
| P-194: actors that draw for themselves (100 files of `native/petari/src` define `draw() const`) | B | F4, E3 step 4 | each such actor is a site: its shape registered once at its start, its material a form made at its init, its changing rows per step (2.7.1) | the inventory's row (CONSOLE-FREE.md 3); read 11-19 at 241395b49, every body whole: each draw placed in E3's step or E4's pass, the console's shape of each held by P-154, P-195, P-143, P-181, P-155, P-185, P-197, P-198, P-213 and E4's target passes (1.7 item 8) |
| P-184: a layout material keeps the console's blend codes, packed alpha compares and coordinate generators | B | F4, E3 steps 1 and 3 | the cook writes the pass state and the compare in the renderer's terms (with P-157, step 1); the draw copies them (with P-151, step 3) | the ledger's "E3 step 3 (P-157)" named P-157's step wrong: it is step 1 here |
| P-190: NormalMapBase remakes its normal maps and its lighting gradient on the CPU | B | F4, E3 step 2, its light in step 6 | the texture in final form at the cook; the light on the GPU from the step's light table | the inventory's row |
| P-191: what the game runs follows the screen's shape (`ClippingJudge`) | C | after F7 (file 08 5.H.2) | every actor runs every step, or one run volume for every screen; behind the switch | the owner's decision of 11-18 18:18; changes the replay's reference once |
| P-185, P-186, P-187, P-188, P-189, P-192: the bloom and blur's 1.333 on a wide screen, the sound sequences' byte order, 16-bit text codes, the 16-bit particle index, table fields by column hash, shared counts and id widths | A | after the foundation (file 08 E) | the inventory's lines say each one's form | P-192 is file 08 5.H.3 |
| P-193: surround output by a written law | A | after the foundation (file 08 E, with the sound work); the native plan places it at steps G6.2 and G6.7, orders 28 and 29, directly after the tablet's pacing steps G2.1 to G2.5 (the owner, 11-22 and 11-23) | the law before the code (file 08 5.H.5) | the inventory's section 4 holds what it starts from |

Of the render path as read for this file (1.9), nothing falls outside P-022, E3, E4, E6b and the step's threading: the
three new rows fall in E4 and F7. The inventory (`CONSOLE-FREE.md`, 11-18) added P-184 to P-194: three in E3, P-191
after F7 by the owner's decision, the rest after the foundation. The areas 1.10 lists as not read whole may
hold more; the whole read (file 08 5.H.1) is where they would come from.

### 2.7 The data path: records, format 13 by step, and both paths side by side

#### 2.7.1 What a record is, who writes it, when it changes

A record is plain data in its final form that the renderer reads by id: no console code is decoded and no meaning is
searched for after the cook. It is written once by one owner and changed afterwards only in named fields, by one
thread.

| Record | Written by | When | Read by | Changes |
| --- | --- | --- | --- | --- |
| The model's sections (2.7.2) | the importer (`crates/nebula-import/src/cook/model_write.rs`); for a mod, the same cooker through `assets.cook` | at the cook | `ModelData` (game) and `Registries::model_register` (host), in place in the store | never; a new store version cooks again |
| A material form: programs, state, inputs, kind, texture list, constant block | the importer | at the cook | registered at load: its pipeline made on the workers (P-010), its constant block a slot (`material_register`) | never |
| A form's changing rows | the game thread (`Material`, from the form's list of changing rows: texture matrices, colour tracks, a colour a switch sets) | once a step, and only the rows that changed | the walk: the item names the form's slot and this step's changed rows | per step |
| A texture and its sampler | the importer; the game for a texture it makes (targets, mod pictures) | at the cook; when made | `Registries` at load: one id, one sampler | never; a target when its size changes |
| A site's shape (step 4) | the game, once at its start (`SiteMesh`, today nine files, 2.12) | once | the walk, as any registered mesh | never |
| A site's material (step 4) | the game, at the site's init, content-addressed | at load | as a material form | its changing rows, per step |
| A pose (step 5) | the game thread | once a step: each block's joints in the list (the list before holds the step before) | one GPU pass per picture makes the matrices at t | per step |
| A light (step 6) | the game thread (`LightDirector`), in world space, one table per list; an item names its light set | once a step | the GPU, the view applied there | per step |

The render thread writes no record. Per picture it writes the camera, t and the GPU pass's inputs; everything else
it reads.

#### 2.7.2 The file: sections by name

Format 13 is the last format number E3 needs. Its header is magic, version 13, size, flags, `section_count`,
`sections_at`; each section is `{name (four characters), flags, at, size, count, stride}`. A reader finds a section
by name, reads the fields it knows of each entry (fields are only added at the end, so `stride` may be larger than
it knows) and skips a section it does not know. A section flagged required that a reader does not know makes it
refuse the file, naming the section. So a later field or section makes no format 14, and a mod's model cooked by
this cooker keeps loading. A model of another version stops the load today without naming it
(`native/petari/src/Game/Render/ModelData.cpp:58-60`; the cooker's own reader names it, `model_write.rs:907-908`);
13's reader names the version it found. No mod has shipped a model of 12, since LumaCore v1 ships with 13.

| Section | Holds | Written from | Read until |
| --- | --- | --- | --- |
| `JNTS`, `STEP`, `JMSH`, `DMTX`, `ENVL`, `MIXS`, `IBND`, `IMPT`, `MESH`, `TEXR`, `GEOM`, `NAME` | 12's arrays as they are, by name instead of by fixed offset | the first landing (step 1's) | kept |
| `MATL` | per material: name, `same_as`, mesh, joint, state word, its forms' and slots' ranges | step 1 | kept |
| `GXMT` | the console's material fields, apart: registers, constant and material colours, channels, alpha codes and references, coordinate generators by GX type and source, fog codes | step 1 (moved out of the material) | the last landing of step 3: no reader is left, and the cooker stops writing it |
| `FORM` | per form: programs and inputs (the state word is the material's, 2.9); added with their first reader (2.9): its kind (opaque, alpha-tested, blended, water), the alpha cutoff as a float and its mode, the glow row and colour, the base map's slot, which texture or vertex colour is baked light, each coordinate's source (one that reads the eye is marked), reads a copy of the screen, two-sided; its ranges in `FTEX`, `CBLK`, `CROW` | step 1 | kept |
| `SAMP` | samplers in final form: min, mag and mip filter, wrap, anisotropy, lod range and bias as floats | step 2 | kept |
| `FTEX` | per form, its texture list: the material's map numbers; a texture names its sampler (`TEXR`, 2.10) | step 1 (2.9) | kept |
| `CBLK` | per form, its constant rows in final floats (colours divided once, the alpha reference a float, fog per view, not per draw) | step 3 | kept |
| `CROW` | per form, every row's source (a coordinate matrix, register, constant, material or ambient colour, fog, the replacing pass's values, a map's size, or none) and how it is written (a copy, three matrix rows); which rows change is decided at run time by the sources written (2.11) | step 3 | kept |

Step 4's records are the game's own, made at load (2.7.1): no model section. Particle materials go through the effect
cook (`luma_content_fx.h`), whose material entry takes the same `FORM`, `FTEX`, `CBLK` and `CROW` fields; that cook's
format has the same section table from step 4's first landing. Step 5 changes no file. Step 6's lights come from the
stage's light tables, read by the game; if the cook of those tables changes, it is in their own file, not the model.

The first landing writes 12's data under the section table and nothing more. Its proof: the arrays of every model of
the cooked store are byte for byte those of 12's cook of the same disc, and every picture is unchanged. The store
version goes up at each landing that changes what the cooker writes, so the store is cooked once again then.

#### 2.7.3 Both paths while a step lasts: the switch

A step's old and new path live side by side until its last landing. Which one draws is chosen per group; a landing
moves one group.

| Step | A group is | The groups |
| --- | --- | --- |
| 1 to 3 | a class of material forms, by `FORM`'s kind and pass | plain unlit; plain lit; alpha-tested; blended; water and indirect; billboard materials; each replacing pass (player light, reflection alpha, player opacity, flexible coordinates) |
| 4 | a family of draw sites (the 109 sites in 47 files of P-154) | quads, then fans (fixed index pattern, first); strips; shadows (volume, surface, drop: with P-143 and P-181); particles (P-149, P-150); widened lines and points; the three sites that choose their primitive at run time; 2-D gauges and glows |
| 5 | a kind of block | one joint; many rigid joints; weighted (skinned); billboard and calc mode 1; calc mode 2; eye-space blocks |
| 6 | a kind of light | the actor's lights (`loadActorLightInfo`); point lights; the player's light; the map's lights |

- The switch is one table in the game module, a default per step and group (Game/Render/RenderPath.hpp, a file R3.2 makes). A landing
  changes one group's default. The writer reads it where it writes a record into the list (`Material::constantsOf`,
  `TypedDraw`, `LightFunction`, the block writer); the item or block it writes carries which path made it (a flag
  bit per step), so the walk serves both without a table of its own and two groups in one list draw side by side.
- A test run can set every group (`old`, `new`) or ask for both (2.7.4) by a plan line. The settings screen has no
  such choice, and a release ships each group as main's default has it.
- The mod draw services (`draw.mesh`, `draw.inst`, `draw.native`, post programs) write their items by the default;
  the mod suites run on every landing.
- The last landing of a step deletes the old path, its flag bit and its groups in the table. Steps 1 to 3 share
  `GXMT`: it goes with step 3's last landing.
- P-173 goes with step 3's first group: a form's slot holds its constant block for good, and the changed rows ride in
  the list, so no material falls to a per-frame fill.

No step needs a landing that cannot be split this way: steps 1 to 3 and 6 split by what a draw is, step 4 by the file
family that writes it, step 5 by the block. If a group turns out to need two paths in one draw, the board says so
before that group's landing.

#### 2.7.4 How the gate compares both paths in one run

- The plan line `paths both` makes each picture frame give two pictures of the same game frame: one with every group
  on the old path, one with every group of the step on the new path (the others at their default).
- For steps 1 to 4 and 6 the writer is the game thread: at a picture frame it writes the step's list twice from the
  same state, once per path, and submits both. Writing a list reads the step's state and writes only the list and the
  render caches; the step hash (`tools/verify/step_hash.py`) after a picture frame is the same with `paths both` as
  without, and the gate checks that, so the second list changes no game state.
- For step 5 the list is the same: the render thread walks it twice, once with the CPU's matrices, once with the GPU
  pass. Because step 5 is about in-between pictures, its pictures include one at a fixed t between two frames, both
  walks at that t.
- The render thread walks the two lists at the same t into two pictures; the harness writes `<name>.old.png` and
  `<name>.new.png` beside the picture it keeps (the default path's), on every renderer.
- The gate compares the new picture with the old one, as `tools/verify/compare_snapshots.py` compares two renderers
  (largest and mean difference of any channel, pixels past the tolerance of 1), and the kept picture with its
  reference as before. A difference fails the landing, naming the stop, the picture, the renderer and the group, with
  the difference picture kept in the run. Step 5's GPU sums may differ from the CPU's in the last bit; its tolerance
  is the one Vulkan is held to against Direct3D 12, not wider.
- The cost is only at picture frames, in test runs; a player's run writes one list per step.

So each landing of E3 is proved on every tour stop and renderer by pictures of both paths from one run, not by two
runs whose frames could differ.

### 2.8 Step 1's first landing, to the file

Read at d4246a703. `nebula_model.h`, `native/petari/{src,include}/Game/Render`, `registries.cpp` and
`crates/nebula-import` are the same at 5526e1cf0 (`git diff --stat d4246a703 5526e1cf0` on them: empty).

**What 12 is.** One block in host order, used in place (`nebula_model.h:1-3`). A header of 28 words
(`nebula_model.h:49-59`, written at `model_write.rs:854-886`): magic, version, size, flags, 11 counts, 12 offsets and
the geometry's size. Records hold offsets from the block's start to their own parts (a mesh's streams, a material's
forms and slots). `model_write::write` lays the block out in this order (`:436-853`): joints, steps, joint meshes, draw
matrices, envelopes, mixes, inverse binds (16-aligned), important joints, meshes, materials with their parts
(coordinates, slots, coordinate matrices, warps, forms with their rows, texture lists and inputs), textures, names,
geometry (each stream 32-aligned), and last the texture images (32-aligned, the textures' `record_at`).

Two arrays have no count in 12's header; their readers assume one:

- The inverse binds. The host reads 12 x `joint_count` floats whenever there are envelopes
  (`registries.cpp:367-371`). The cook writes them only when the disc's `EVP1` has them (`model_ir.rs:1442-1450`)
  and for every joint of a glTF model with envelopes (`gltf.rs:2130-2139`). A disc model with envelopes and no
  inverse binds would have the host read the next arrays as matrices. None exists: in the US, PAL and JP stores of
  version 37 (1,762 models each), every model with envelopes (144 in the US store) has one bind per joint (a scan of
  each store's model headers, `D:/NebulaWork/plan/a1/inv/ibnd_scan.py`). A mod's glTF model with envelopes gets one
  per joint too.
- The important joints: one per draw matrix (`model_ir.rs:1303-1319`).

**The landing.** The format number and the layout change; no record changes, so every offset inside a record still
holds.

1. The header becomes magic, version 13, size, flags (the scale mode and `NEBULA_MODEL_*` as now), `section_count`,
   `sections_at`. Its room stays 112 bytes, the rest zero, and the section table goes at the block's end. So every
   array lies where 12 put it, and the proof below is a byte compare.
2. A section is `{name, flags, at, size, count, stride}`, six words. Flag bit 0: required. The table in file order:

| Section | Holds | Count | Stride |
| --- | --- | --- | --- |
| `MODL` | the model's scalars: `full_weight_count`, `source_vertex_count`; fields only added at the end | 1 | 8 |
| `JNTS` | joints | `joint_count` | 96 |
| `STEP` | the joint walk's steps | `step_count` | 8 |
| `JMSH` | each joint's meshes | `joint_mesh_count` | 4 |
| `DMTX` | draw matrices | `draw_matrix_count` | 8 |
| `ENVL` | envelopes | `envelope_count` | 8 |
| `MIXS` | mixes | `mix_count` | 8 |
| `IBND` | inverse binds, written count (none when the disc has none) | its own | 48 |
| `IMPT` | important joints | `draw_matrix_count` | 4 |
| `MESH` | meshes | `mesh_count` | 100 |
| `MT12` | 12's material records; their parts lie inside the section's span | `material_count` | 208 |
| `TEXR` | textures | `texture_count` | 40 |
| `NAME` | the names' text | bytes | 1 |
| `GEOM` | every stream, for one copy to the GPU | bytes | 1 |
| `TIMG` | the texture images | bytes | 1 |

   2.7.2's table lacks `MODL`, `IMPT`'s count, `MT12` and `TIMG`; this table is the one the code follows. `MT12` is
   required and goes in step 1's next landing, when `MATL`, `GXMT` and `FORM` replace it (2.7.2). It cannot be
   `MATL`, because removing fields from a record is not an added field.
3. Readers. One lookup in C, `nebula_model_section(const NebulaModelHeader*, uint32_t name)` in `nebula_model.h`,
   and one in Rust, `model_write::sections`. Each reader resolves its sections once, at load, into what it uses now:
   - The game: `ModelData.cpp` `loadFile` (`:56-130` and on), the accessors of `ModelData.hpp:133-193` (counts,
     the first joint, names, draw matrices, inverse binds), `Material.cpp:195`, `Model.cpp:155-156`. `ModelData`
     keeps the resolved arrays and counts, filled at load, in place of `mFile`'s header fields.
   - The host: `registries.cpp` `read_model` (`:351-380`), `place_model` (`:403`), `model_register` (`:430`) and
     `restart` (`:569`): 36 reads of header fields (`geometry_size` 8, `mesh_count` 5, `joint_count` 4, `geometry_at` 3,
     `envelope_count` 3, `draw_matrix_count` 3, `mix_count` 2, `meshes_at` 2, and one each of `mixes_at`,
     `joints_at`, `inverse_binds_at`, `flags`, `envelopes_at`, `draw_matrices_at`). The inverse binds are taken by
     `IBND`'s count, not `joint_count`.
   - The importer: `model_write::names` (`model_write.rs:899-926`); `gltf.rs` `GameModel::read` (`:955-978`) and
     `skeleton_gltf` (`:986-992`), which read header words 4 and 15 by number.
   - Tests that read the header: `gltf_tests.rs:75-86`, `:198`, `:1063-1064`.
   - Outside this repository (LumaKit, LumaLink): not checked.
   - A missing required section stops the load naming it, and a model of another version names its version
     (2.7.2).
4. `NEBULA_MODEL_VERSION` and `model_write::VERSION` become 13, and the store version goes up by one, so the store
   is cooked again. A mod's model is cooked by the same cooker (2.7.1).
5. Proof:
   - Once, for the landing (a script in `tools/verify`, its log kept with the gate's run): main's importer and the
     branch's cook the US disc, and every model's block from byte 112 to 12's end is equal.
   - A test in `crates/nebula-import` keeps it: the writer records each array's place and count as it writes, and
     the section table says the same.
   - A C test reads a cooked model through `nebula_model_section` and finds every section and count.
   - The gate's pictures on every stop and renderer are unchanged, and the replay is identical.
   - No switch (2.7.3) is needed: no path draws differently. The switch starts with step 1's second landing.

### 2.9 Step 1's second landing, to the file: the material apart from the console's fields

Read at c8222595e. In 12 a material is one record of 208 bytes (`NebulaModelMaterial`, `nebula_model.h:246-264`,
written at `model_write.rs:587-632`) followed by its parts (coordinates, slots, coordinate matrices, warps, forms with
their rows, texture numbers and inputs, `:633-718`); the record holds the parts' places as offsets from the block's
start. 2.8 keeps all of it as `MT12`. This landing replaces `MT12` with typed sections; no picture changes.

**Who reads the material record.** No file of `runtime/` or `platform/` names `NebulaModelMaterial` or `materials_at`
(`git grep` at c8222595e: the hits are `native/` and `crates/` only). In the game:

| Field | Read at |
| --- | --- |
| `name_at` | `ModelData.hpp:174` (`getMaterialName`) |
| `name_hash` | `Material.cpp:205` (the draw's source hash), `ModelData.cpp:193` (find by name) |
| `same_as` | `Material.hpp:151`, for `Model.cpp:445` |
| `mesh`, `joint` | `Material.cpp:199-200` |
| `mode` | `Material.hpp:155-157` (`isTranslucent`: neither of the low two bits), for `DrawBuffer.cpp:257` |
| `flags` | ambient `Material.hpp:220`; stage colours `Material.cpp:297`; billboard `:257`, `:313`; indirect `:334`; fog `:384`, `:392`, `:604` |
| slots | `Material.cpp:208-212`, `Material.hpp:223-228`, the scale rows `Material.cpp:363` |
| coordinates | `Material.cpp:231-237`, `Material.hpp:184-186` |
| coordinate matrices | `Material.cpp:238-258` |
| warps | `Material.cpp:340-344` |
| forms | `Material.cpp:279-293`, `:426-449`, `:696-710`, `:713-820`, `:917-1024` |
| colours, registers, constants, fog | `Material.cpp:213-229` |
| `channels` (enable and light mask only) | `Material.hpp:230-233` (`usesLight`), for `ModelUtil.cpp:274-294` and `NPCUtil.cpp:253`, `:266` |
| `alpha_ref0`, `alpha_ref1` | `Material.cpp:377-378` |
| `has_scale`, `normal_scale` | `Model.cpp:605-616`; `Model.hpp:229` keeps the record's address |
| `state` | `Material.cpp:614` |

Read by nothing outside the importer (`git grep -nw` of each name over `native`, `runtime` and `platform`, without
`nebula_model.h`): `cull`, `early_depth`, `channel_count`, `alpha_comp0`, `alpha_op`, `alpha_comp1`, `blend_type`,
the channels' `material_source` and `ambient_source` (0 hits each); `blend_src`, `blend_dst`, `depth_test`,
`depth_func`, `depth_write`, `diffuse` and `attenuation` have hits, all in other records (the pass state, particles,
the Mii head), none through a material. The cooker turned them into the state word (`model_write.rs:36-44`) and the
programs. The mod calls read a material through `Material` (`NebulaModCalls.cpp:129`, `:138`), not the file.

In the importer: `model_write::names` (`model_write.rs:923`, the names by `MATERIAL_BYTES`); the tests
`gltf_tests.rs:198-206` and `:1241-1246` (the slots at `MATERIAL_BYTES - 16`). Outside this repository: not checked.

**The sections.** Records name their parts by index and count in a section, not by offset; names stay offsets of
the block, as in every other record. Names starting `GX` hold the console's material, read until step 3's last
landing.

| Section | Entry | Stride | Kept |
| --- | --- | --- | --- |
| `MATL` | `name_at`, `name_hash`, `same_as`, `mesh`, `joint`; `flags`: translucent (12's `mode & 3` is 0, decided at the cook), normal scale (12's `has_scale`), on a billboard; `state`; `light_masks[4]` (a channel's mask, 0 when 12's `enable` is 0); `gx` (its `GXMT` entry); `first_slot`, `slot_count`; `first_form`, `form_count`; `normal_scale[3]` | 64 | kept |
| `FORM` | `vertex`, `pixel` (the programs' hashes); `material`; `first_vertex_row`, `vertex_row_count`, `first_pixel_row`, `pixel_row_count` (in `FROW`); `first_texture`, `texture_count` (in `FTEX`); `first_input`, `input_count` (in `FINP`); `replace`, `flags`, `pass` as in 12 | 56 | kept |
| `FINP` | a form's vertex inputs, `NEBULA_INPUT_*` | 1 | kept |
| `SLOT` | 12's `NebulaModelSlot` | 8 | kept: map n is what the game's texture animation changes (`Material::setTextureIndex`, from `BtpPlayer.cpp:17`); its texture column stays (2.10) |
| `FTEX` | a form's material map numbers | 1 | kept (2.10: no `TXLS`) |
| `FROW` | a form's rows, `NEBULA_ROW_*`, vertex rows then pixel rows | 4 | until step 3's last landing (`CBLK`, `CROW`) |
| `GXMT` | `flags` (ambient, stage colours, indirect, fog); material and ambient colours; registers; constants; `alpha_ref0`, `alpha_ref1`; fog; `first_coord`, `coord_count`; `first_matrix`, `matrix_count`; `first_warp`, `warp_count` | 120 | until step 3's last landing |
| `GXTG` | coordinate generators, 12's `NebulaModelCoord` | 8 | until step 3's last landing |
| `GXTM` | coordinate matrices, 12's `NebulaModelCoordMatrix` | 104 | until step 3's last landing, which names the record that takes the game's matrix values (the mode by name, not the info byte) |
| `GXWP` | warps, 12's `NebulaModelWarp` | 24 | until step 3's last landing |

The fields read by nothing are not written. `MT12` and `NebulaModelMaterial` go. The light masks leave the console's
channels now because the game counts a material's lights (`ModelUtil.cpp:274-294`) after `GXMT` is gone; step 6 says
how a material names its lights when lights are records.

Corrections to 2.7.2 and 2.4. The state word is the material's: every form of a material is listed with its
material's state (`model_write.rs:748-756`), so it is in `MATL`, not `FORM`. The descriptions of 2.3 (the kind, the
alpha cutoff, the glow row, the base map, the baked light, the coordinate sources, the copy of the screen, two-sided)
are not written in step 1: nothing on main reads them, and a field written with no reader cannot be proved. Each is
added at `FORM`'s end by the landing that brings its first reader; `stride` makes that an added field, not a format.
2.4's row 1 lists them as step 1's: they are not. The pipelines a model needs are already written at the cook
(NebulaData/Pipelines.bin, `pipeline_list.rs:1-5`, `:102-114`) and registered at boot; what is left of P-010 is that a
draw whose pipeline is not made yet builds it on the render thread, which is E5's code and needs nothing more from the
file.

**Readers.** `ModelData` resolves the sections once at load (2.8's lookup) and keeps the arrays. `Material` keeps its
`MATL` and `GXMT` entries in place of 12's record; every form reader takes the form's ranges in place of `*_at`;
`Material`'s accessors keep their names and results, so `DrawBuffer`, `ModelUtil`, `MaterialCtrl` and the mod calls do
not change. `Model.hpp:229` keeps the `MATL` entry's address. In Rust, `model_write::names` reads `MATL`, and the two
tests read `SLOT` through `MATL`'s `first_slot`.

**Proof.**

- A test in `crates/nebula-import` cooks every model of the US disc with 2.8's writer and with this one and compares,
  per material, every field of the reading table: `MATL` and `GXMT` with 12's record, `SLOT`, `GXTG`, `GXTM` and
  `GXWP` with its parts, each form's programs, rows, map numbers, inputs, `replace`, `flags` and `pass`; every other
  section byte for byte.
- `git grep -nw` of each field 12 dropped over `native/include`: 0. `NebulaModelMaterial`, `MATERIAL_BYTES` and
  `MT12` over the tree, outside `docs/`: 0.
- A C test: a model without `MATL` is refused, naming it.
- The gate's pictures on every stop and renderer are unchanged and the replay is identical; the store version goes up
  by one.
- No switch: no path draws differently.

### 2.10 Step 2: samplers decided at the cook, a texture list made once

Read at c8222595e.

**Today.**

1. `TEXR` (`NebulaModelTexture`, `nebula_model.h:267-274`) keeps the console's sampler codes: wrap codes, the min and
   mag filter codes (the console's min types 0 to 5), anisotropy, the lod range and bias. At load
   `ModelData.cpp:101-124` copies each into a `ResTIMG` (`GameTexture.hpp:21-35`), the console's texture header.
2. A form's list is made by `MaterialDraw::tableOf` (`Material.cpp:770-820`): for each map, the texture its slot shows,
   its host id found from the image's address (`RenderTexture::idOf`, `RenderTexture.cpp:175` on), and its sampler
   decoded from the console's codes (`Material.cpp:789-797`). The same decode is written a second time for the draw
   sites (`SiteTextures::toFrame`, `TypedDraw.cpp:78-84`). The list is registered by its hash
   (`Registries::texture_table`, `registries.cpp:682-726`: a per-thread memo of 64 lists of up to 8 textures, else a map
   under the lock). A form keeps its table while the material's textures and the model's texture data stand
   (`Material.cpp:774-777`); a draw that swaps an image builds and registers its list at every draw (`:814-818`); the
   one caller is the player's model (`PlayerModel.cpp:136`).
3. The render thread, for each table a draw names:
   - with descriptor indexing (`RenderCore::walk_tables`, `draw_walker.cpp:752-833`): each walk looks up every
     texture's handle and writes the table's pairs into that frame's list buffer again (`write_texture_list`; Direct3D
     12 `renderer_d3d12.cpp:2934` on, an upload ring per frame slot); the sampler slots are made again when the
     textures, the sampler heap or a target changed (`draw_walker.cpp:787-816`);
   - without it (`walk_bound_tables`, `draw_walker.cpp:838-930`): rebuilt when stale, per walk for a table holding a
     target.
4. A sampler's key holds the texture's levels, generated levels and a replacement's shift (`draw_walker.cpp:801-813`)
   and the render settings (`make_sampler_slot_key(key, cfg)`, `renderer_d3d12.cpp:2918-2920`). P-048, P-056 and P-066
   are the heaps' waits when they fill.

**The file.**

- `SAMP`: each distinct sampler of the model in the form the renderer takes, `NebulaFrameTexture`'s sampler fields
  (`nebula_frame_list.h:106-113`): wrap s and t, min and mag linear, the mip mode, anisotropy, `min_lod`, `max_lod`,
  `lod_bias`. The importer decodes the disc's codes once; the game copies.
- `TEXR` loses the codes and gains `sampler`, its `SAMP` entry. The sampler is the texture's, not the form's: when the
  texture animation puts another texture in a map (`BtpPlayer.cpp:17`), the map samples as that texture says
  (`Material.cpp:785-797`). So 2.7.2's `TXLS` (a texture and a sampler per entry) is not made: a form's list is its map
  numbers, 2.9's `FTEX`, kept. 2.7.2 and 2.9 are corrected by this.

**The engine.**

- `ModelData` registers each texture with the host at load and keeps, per texture, the host id, the size and the
  sampler, in a record of its own in place of `ResTIMG` for a model's textures. `tableOf` takes ids and samplers from
  it: no search by address and no decode at a draw.
- The player's swapped images (`PlayerModel.cpp:136`): its list is registered once per distinct list and kept by the
  model, not at every draw. How it is kept is decided when the caller is read whole, in the step's code.
- The render thread writes a table's pairs once, into a table buffer that lives as long as the table, and again only
  when one of its textures' slots or its samplers' slots changes (the texture store's revision, the heap's generation).
  A target, whose picture changes every frame, is read by the program through the target's own slot, which the frame
  sets once; a table holding a target is not written again each walk. Its samplers are made when it is registered.
- Corrected 11-19, while step 2's second landing was written (`f4/e3-2c`): the write is skipped, the look-up is not.
  Each walk still looks up every texture a table names (`TextureStore::handle`), because the look-up is what marks the
  texture in use for the video-memory budget and what gives a movie's texture its current frame; skipped when the
  store's revision stands, the budget would shrink textures on screen and a movie would stop. Only the copy of the pairs
  is skipped (`core::TextureLists`, `texture_lists.h`).
- In between until the step's last landing: a table holding a target is still written each walk, until the program
  reads a target through its own slot (a program change).
- The game's own uses of a model's texture take the `ModelTexture` too (`f4/e3-2a`): Mario's eyelid pictures, the
  normal-map objects and fur (`MR::getModelTexture`, a `GameTexture` made from a `ModelTexture`). A draw site keeps the
  filter numbers (`SiteTexture`, `NEBULA_FILTER_*`) until step 4, so `SiteTextures::setSampler` writes a cooked sampler
  into them; `toFrame`'s decode gives back the same sampler. Step 4 gives the sites the cooked sampler and removes both.
- The sites' decode (`TypedDraw.cpp:78-84`) and `ResTIMG` for disc pictures, particles and targets are step 4's
  (P-155 with P-154): their files' samplers are decoded at their cook the same way.

**Proof.**

- A test in `crates/nebula-import`: for every texture of every model of the US disc, its `SAMP` entry equals the decode
  of 12's codes by the rule of `Material.cpp:789-797`.
- A test of the walk with a renderer that counts: the same list walked twice writes each table once; a table with a
  target is not written again.
- The gate's pictures on every stop and renderer unchanged, the replay identical.
- `git grep -n "mMinType" native/petari/src/Game/Render/Material.cpp` and `RenderTexture::idOf` in the same file: 0.
- The walk's time per frame on the play route on the PC and the tablet, before and after (file 08 7.4: never slower).

### 2.11 Step 3: a form's constant rows written at the cook, the game writing only what changes

Read at cfc2e368e.

**Today.**

1. A form names its constant rows by the console's registers: `NEBULA_ROW_*` (`nebula_model.h:138-155`), a kind and
   an index: registers 1 to 3, constant colours 0 to 3, material and ambient colours, fog, fog colour, the alpha
   reference, the replacing pass's values, and per coordinate its scale, its warp rows and its three matrix rows. 2.9
   keeps the lists as `FROW`.
2. The game turns each into four floats in `Material::fillRow` (`Material.cpp:296-424`), one case per kind: bytes and
   the registers' 16-bit values divided by 255 at every fill, the alpha reference divided by 255, a scale row read from
   the texture header of the map's current texture (`:350-371`), a coordinate's rows from its matrix, and for a
   billboard's matrix that reads the eye, multiplied by the step's view (`:313-324`, `ModelSys::mViewMtx`).
3. `MaterialDraw::constantsOf` (`:713-768`): a form whose material nothing has touched since it was registered is one
   registered slot (`:730-741`; slots are content-addressed and kept for the registries' life, `registries.cpp:615-638`,
   `:538`). Any other form is filled whole into the frame's arena once a frame, or at every draw when the draw edits it
   (`Material.cpp:743-767`), and the edits are copied over the filled rows (`applyEdits`, `Material.cpp:641-679`). One
   `touch()` (`Material.hpp:235-237`) moves a material to the per-frame fill for good (P-173), and the getters touch
   even to read (`Material.hpp:180-183`, `:195-218`): `NormalMapBase::drawMaterialSetting` only reads a material's
   colours (`NormalMapBase.cpp:359-385`) and the mod call that lists a model's materials only reads their coordinates
   (`NebulaModCalls.cpp:129-131`), and each moves those materials to the per-frame fill. `Material::calc` copies every
   coordinate matrix before and after its calc and compares them (`Material.cpp:262-276`).
4. The ambient colours are a global pair (`MaterialAmbient`): set by each actor's light load
   (`LightFunction.cpp:189`), by a material with its own ambient when it is sent (`Material.cpp:481-488`), and to black
   by the 2-D layers (`native/petari/src/Game/Ui/Material.cpp:749-750`, `DrawUtil.cpp:32-33`). A form that reads them is filled again when the
   pair's generation changes in the plain module, and at every draw in a module with hook points
   (`Material.cpp:746-750`); a material that reads the pair after another set it reads that one's.
5. The render thread (`draw_walker.cpp:588-612`, `:620-664`): a slot is used as it is; a form's rows in the list are
   copied into the constant ring at each walk, and between two steps mixed component by component, coordinate rows as
   matrices (`tex_matrix_at`, `:659`), when the same form's rows of the step before are found by pairing. The frame's
   ring is sized for every listed form's rows (`render_core.cpp:1906-1910`).

Who writes, at run time (`git grep` of each setter over `native/petari`):

| What | Writers |
| --- | --- |
| a coordinate matrix | clips (`BtkPlayer.cpp:19-23`); `MaterialCtrl.cpp:143`, `:198`, `:256` |
| registers 0 to 2, constant colours | clips (`BrkPlayer.cpp:16`, `:23`, rounded to the console's 11-bit and byte forms, `Clip.cpp:124-133`, `:169-184`); mods (`LumaContentModel.cpp:760-793`, a mod's floats rounded to steps of 1/255 and clamped to the console's ranges, `:775-778`) |
| a material colour | clips (`BpkPlayer.cpp:15`); `MaterialCtrl.cpp:71` |
| a map's texture (its scale rows) | clips (`BtpPlayer.cpp:17`) |
| one draw's values | `PlayerModel.cpp:107-145`, `:237-292` (registers, colours, fog, opacity, the screen as map 0); `MarioSearchLight.cpp:268-326` (the replacing pass's values); `ElectricRailMoving.cpp:85-86`; `FlexibleSphere.cpp:42-45` |

The cook knows which registers the game's register clips write, by material name (`programs.rs:1853-1891`, from every
`.brk` of the disc, `:1630-1641`), and folds the rest into the programs; it does not know what game code or a mod
writes.

**The file.**

- `CBLK`: per form, every row in final floats, as `fillRow` gives it at load for the material's cooked values (colours
  divided once, the alpha reference a float, the scale rows from the cooked texture sizes, a coordinate's rows from its
  cooked matrix). The `PLAYER_OPACITY` form's alpha reference is written 0, the pass's edit (`Material.cpp:551-553`).
- `CROW`: per form, per row, its source and how it is written: a coordinate matrix n (three rows, or two for a warp),
  register n, constant n, material colour n, ambient n, fog, fog colour, the replacing pass's values, map n's size (a
  scale row: map n and its scale map, `:350-371`), or none (a row nothing writes, the warp rows when the material has
  no indirect stage, `:334-339`). A coordinate that reads the eye under a billboard is marked: its view is applied by
  the render thread per picture, not by the game per step.
- 2.7.2's `CROW` held only rows that change. Since game code and mods write any register of any material, which rows
  change is not known at the cook; every row names its source, and what changes is decided at run time by which
  sources were written. 2.7.2 is corrected by this.
- `FROW` and `GXMT`, `GXTG`, `GXTM`, `GXWP` go with this step's last landing. The cook keeps the console's codes only
  as its input.

**The engine.**

- The game holds, per material it draws, the sources' values (a few float4s), and no rows. A clip, a game call or a
  mod writes a source; the write marks it changed for the step. The game's setters take floats: the colour getters
  that hand out a writable pointer and touch to read go; `Material::calc`'s before-and-after compare goes, a matrix's
  calc reports whether it changed.
- At load the game registers each form's `CBLK` once (`material_register`, as the plain path today) and its `CROW` with
  it. A material written since its load (its own copy, `Model::ownMaterial`) has an instance: one record of its
  sources' values on the render thread, made at the first write. Per step the list carries only the sources written
  that step: `(instance, source, value)`. A draw's own values (the last row of the writers' table) ride on the item
  as its sources, as `MaterialDraw`'s edits do today, without a whole block in the arena.
- The render thread keeps each instance's block in GPU memory, built from the form's `CBLK` and `CROW` with the
  instance's sources, and writes it again only in a frame where one of its sources changed (a block per frame in
  flight while it changes, then one). Between two steps it mixes only the changed sources, a coordinate matrix as a
  matrix, as today's walk does for the whole block. So a material written once returns to a block written once (P-173),
  and `fillRow`, `applyEdits` and the per-frame fill go (P-151).
- The ambient pair: in this step the ambient in effect is a source of the list, written when the global pair changes,
  and an item names the entry in effect at its draw, as a light set is named; the picture of a draw that reads another
  material's ambient is unchanged. Step 6 moves it into the item's light set and removes `MaterialAmbient`.
- Clip colours: the first landing keeps the clips' and the mod call's rounding, so its pictures are unchanged. A
  later landing of the step gives sources the clips' floats and the mod's floats as given, with its pictures looked at
  and approved and the mod call's text updated (LumaCore's `content.model.set_param`).
- P-060 and P-064, the Direct3D 11 constant uploads, are placed with this step by 2.6. Today `write_roots`
  (`renderer_d3d11.cpp:1087` on) appends with `NO_OVERWRITE` where the device allows and discards only when the run does
  not fit, and `upload_written` (`d3d11_context.cpp:509-559`) skips buffers with no video-memory copy and copies the
  written pages. With this step the constant ring carries only changed blocks; both rows are measured again at its
  landing (pages written per frame on Direct3D 11 on the play route), and closed or rewritten by what that shows.

**Groups.** As 2.7.3: plain unlit, plain lit, alpha-tested, blended, water and indirect, billboard materials, each
replacing pass. The billboard group moves the eye's view to the render thread; its in-between pictures change, its
pictures at a step do not.

**Proof.**

- A test in `crates/nebula-import`: for every form of every model of the US disc, `CBLK` equals the rows `fillRow`
  gives the cooked material (the rule restated in Rust from `Material.cpp:296-424`, checked against a dump of the game's
  own fill for the tour's models), and each `CROW` entry names the source that rule reads.
- A C test of the render thread's block: a form's block from `CBLK` and `CROW` with a set of sources equals the rows
  of today's fill and edit for the same values; between two steps the mixed block equals today's mix.
- The gate's pictures of both paths on every stop and renderer (2.7.4) equal, and the replay identical.
- A count from a test run: the bytes of constants the list carries per frame and the blocks the render thread writes,
  before and after, on the play route and in the tours' animated stops (P-151 asked for a fill count in
  HoneyBeeKingdom and SeaGalaxy); after the last landing, materials on a per-frame fill: 0.
- `git grep -n "NEBULA_ROW_\|fillRow\|MaterialAmbient" native runtime platform`: 0 after step 6.
- The walk's time and the game step's time on the PC and the tablet, before and after (file 08 7.4: never slower).

### 2.12 Step 4: the site draws as records made once

Read at cfc2e368e.

**Today.** A site is game code that draws without a model's material: effects, shadows, debug and 2-D shapes,
gauges, glows, particles, the mods' draw services. It draws through `TypedDraw` (`native/petari/src/Game/Render/TypedDraw.cpp`):

1. A `SiteMaterial` (a written shader, its state, up to 256 floats inline, a texture list) is copied whole into the
   frame's arena and listed as new constants at every `TypedDraw` made or `setMaterial` called (`:193-208`), and its
   texture list is registered by hash at each (`:202-204`), each texture's sampler decoded from the console's codes and
   its host id found by the image's address (`SiteTextures::toFrame`, `:73-87`).
2. Each `vertices<T>(primitive, count)` takes room in the frame's vertex ring and opens an item (`begin`, `:410-455`);
   quads, strips and fans get their triangle indices written on the CPU into the index ring at every draw
   (`writeIndices`, `:369-400`). Lines and points are widened on the CPU: each becomes four copies of its vertex with
   the other end and a corner code, and six indices (`widen`, `:457-513`).
3. Each draw's matrix and a flag word are copied into the arena as a new block (`block`, `:291-319`); a draw under a
   camera other than the scene's compares the matrix to find the camera's role (`:133`, `:140`).
4. `SiteMeshData` (`TypedDraw.cpp:522-582`) and its typed form `SiteMesh<T>` (`TypedDraw.hpp:622-645`) are the
   registered form: written once, handed to the host, drawn by id. Nine files use it: `OceanRingDrawer.cpp`,
   `OceanRingPipeInside.cpp`, `OceanRingPipeOutside.cpp`, `OceanSphere.cpp`, `WaterRoad.cpp`, `ElectricRail.cpp`,
   `ElectricRailMoving.cpp`, `MiiHead.cpp:33-37` and `FurShader.cpp:75`, `:132`. Five sites switch between the kept
   mesh and writing the same shape into the ring at each draw (P-195): the ocean sphere waves near the player and
   stands still beyond 10000 (`OceanSphere.cpp:412-416`); an ocean ring's part is written near the player
   (`OceanRingDrawer.cpp:124-130`); a water road is cut at its rail coordinate in its growing demo
   (`WaterRoad.cpp:706-711`); the electric rails are written at each draw while they vanish
   (`ElectricRail.cpp:282-287`, `ElectricRailMoving.cpp:89-93`).

The sites (`git grep` of `vertices<` and `.vertices(` over `native/petari`, less `TypedDraw` itself and
`MiiHead.cpp:41`, which reads a Mii shape): 109 in 47 files, 56 triangle strips, 36 quads, 8 fans, 4 triangle lists,
1 line list, 1 line strip, and 3 whose primitive is chosen as they run (`MarioShadow.cpp:331`,
`ParticleDraw.cpp:243`, `:245`). P-154's count of 11-17 was 109 in 47 with 35 quads; one quad site is new since.
`native/petari/src/Game/Util/DirectDraw.cpp` holds 17 (lines, circles, fans, cylinders, sphere parts, boxes, textured quads, under the
`TDDraw` namespace), used from 19 files, most by `MarioActorSpecialDraw.cpp` (30 calls); `native/petari/src/Game/System/LumaDraw.cpp` holds
7, the mods' draw services.

Particles (P-149, P-150): each particle's matrix is built in eye space by its shape, taken back to the world through
the camera's inverse (`ParticleDraw.cpp:72-89`, `:131-134`) and written as three 3x4 matrices per instance (`:197-199`);
the emitter's material copies the console's stage modes, alpha compares and indirect matrix into constants 48 to 93
at every emitter draw, and the two colours carry over from the emitter drawn before (`ParticleResource.cpp:610-693`,
`:620`).

Shadows (P-143, P-181): a volume is drawn twice, front faces adding to the stencil and back faces taking
(`ShadowVolumeDrawer.cpp:91-114`), with its shape written at each pass (`ShadowVolumeBox.cpp:135-177`,
`ShadowVolumeLine.cpp:72-113`); the surface box writes three quads and the circle a fan with a sine and cosine per
vertex at every draw (`ShadowSurfaceBox.cpp:72-89`, `ShadowSurfaceCircle.cpp:70-74`); the oval draws its model three
times at scales 1, 0.9 and 0.81 (`ShadowSurfaceOval.cpp:59-65`). Every renderer's add and take saturate
(`nebula_game_api.h:150-151`; `pipelines_d3d12.cpp:410`, `pipelines_d3d11.cpp:302`, `pipelines_vk.cpp:481`,
`pipelines_mtl.mm:327`).

**The engine.** Step 4 changes no model file (2.7.2); its records are the game's, made at load or at a site's start.

- A site material is a record: registered once by its content (as `material_register` and `texture_table` already
  de-duplicate), kept by the site while its values stay, and written again only when a value changes (2.11's rule:
  sources, not rows). A disc texture's sampler is decoded at its cook and its host id held by `GameTexture` (2.10,
  P-155); `toFrame` and the search by address go.
- Indices of the fixed patterns (quads, strips, fans) are one index list per pattern, made once by the render thread
  for the longest run asked and grown when a longer one comes (no cap). An item names the pattern and its count;
  `writeIndices` goes.
- Each site's shape is one of three kinds, decided per site in 2.12.1's table (the first landing checks it against
  the code at its commit):
  - a fixed shape under a matrix (a box, a unit circle, a quad of fixed size, the line volume's ends): a mesh
    registered once, drawn with a matrix and its values, as `SiteMesh` does today;
  - a shape from a few numbers (`TDDraw`'s circles, cylinders and spheres by their segment count; a quad by its size):
    one unit mesh per shape and count, and the numbers in the item's matrix or a value row;
  - a shape from the game's state (ropes, ribbons, flags, water surfaces, beams along a path): written by the game
    once a step into a buffer the site keeps, updated in place (a copy per frame in flight while it changes), not into
    the ring at every list; whether a family's shape moves to a vertex program (points of a rope, a flag's cells) is
    decided when that family is read whole.
- Lines and points are widened in the vertex program from one instance per segment or point; `widen` goes.
- A block is the site's: its matrix written when it moves, the camera named by role, not found by comparing matrices.
- Particles: an instance holds the world position and the one before, size, rotation, the two colours, the texture
  frame and the shape's kind; the vertex program builds each of the emitter's shapes (`ParticleResource.hpp:73-86`:
  billboards turned or not, Y-billboards, directions, the D-billboard, rotation, point, line; stripes, `:66`) from the
  camera of the picture. The CPU's eye-space matrices and the inverse go. An emitter's material is a record
  made once per resource and shape at load; the carry-over of the colours goes (each instance already carries its own,
  `ParticleDraw.cpp:201-202`).
- Shadow volumes: one draw with no culling, front faces adding and back faces taking. Saturating operations make the
  count depend on the order of faces within that draw (a take before its add stops at 0), so the volume's draw uses
  two new operations that wrap, which commute; with an 8-bit stencil both forms give the same count wherever at most
  255 volumes overlap a pixel, the test after it (`IF_SET`) is unchanged, and the saturating pair stays for other
  users. Surface shadows draw unit meshes under a matrix. The oval's three stacked draws become one with the edge's
  fade in the program; its pictures change, and are looked at and approved in that group's landing.
- The mods' draw services (`LumaDraw.cpp`) write by the default path (2.7.3); the mod suites run at every landing.

**Groups.** As 2.7.3: quads, then fans; strips; shadows (with P-143 and P-181); particles (P-149, P-150); widened lines
and points; the three sites that choose their primitive as they run; 2-D gauges and glows. A group's landing moves
its sites, by the table, to their kind.

**Proof.**

- The gate's pictures of both paths on every stop and renderer equal (2.7.4), the replay identical; the oval's are
  approved by eye in its landing, named on the board.
- A test of the index patterns: for each pattern and count, the list equals `writeIndices`' output.
- A test of the widening program against `widen`'s corners, and of the particle program against today's matrices,
  for every shape kind (resolved corner positions equal within the renderers' tolerance).
- A test of the two-sided volume: random volumes over a pixel grid, wrapping one-draw count against saturating
  two-pass count, equal up to 255.
- Counts from a test run on the play route and the tours' particle and shadow stops, before and after: bytes of the
  vertex and index rings per frame, arena bytes for site constants, `texture_table` calls; after the last landing
  `git grep -n "writeIndices\|SiteTextures::toFrame\|void TypedDraw::widen" native`: 0.
- The game step's and the walk's time on the PC and the tablet, before and after (file 08 7.4: never slower).

#### 2.12.1 The 109 sites

Read at cfc2e368e, each site with its lines before and after and, where the shape's points come from elsewhere in the
file, the code that makes them. The kinds are 2.12's: **fixed**, a shape that is the same at every draw under a
matrix (made once, or once at the actor's start); **numbers**, a shape made from a few values at each draw;
**state**, a shape from a list of points the actor keeps, read at each draw (which of those lists change at every
step is read with each family). The primitives agree with 2.12's count: 56 strips,
36 quads, 8 fans, 4 lists, 1 line list, 1 line strip, 3 chosen as they run.

- Kinds: 22 fixed, 51 from numbers, 36 from state.
- Groups (2.7.3's order): quads 15, fans 4, strips 41, shadows 19, particles 2, lines 2, points 1, 2-D 18, mods 7.
- Paths are under `native/petari/src/Game`.

| # | Site | Primitive | Kind | Group | Read |
| --- | --- | --- | --- | --- | --- |
| 1 | `native/petari/src/Game/Enemy/RingBeam.cpp:80` | strip | state | shadows | RingBeamShadowDrawer; 32 point matrices along the beam's ring, band top |
| 2 | `native/petari/src/Game/Enemy/RingBeam.cpp:92` | strip | state | shadows | band 500 down |
| 3 | `native/petari/src/Game/Enemy/RingBeam.cpp:111` | strip | state | shadows | -x wall |
| 4 | `native/petari/src/Game/Enemy/RingBeam.cpp:126` | strip | state | shadows | +x wall |
| 5 | `native/petari/src/Game/LiveActor/ShadowSurfaceBox.cpp:72` | quad | fixed | shadows | unit box face, one of 8 arrangements by setPart; matrix carries size |
| 6 | `native/petari/src/Game/LiveActor/ShadowSurfaceBox.cpp:78` | quad | fixed | shadows | as 5 |
| 7 | `native/petari/src/Game/LiveActor/ShadowSurfaceBox.cpp:84` | quad | fixed | shadows | as 5 |
| 8 | `native/petari/src/Game/LiveActor/ShadowSurfaceCircle.cpp:70` | fan | fixed | shadows | unit circle, cSegments; radius in the matrix |
| 9 | `native/petari/src/Game/LiveActor/ShadowVolumeBox.cpp:136` | strip | numbers | shadows | 14 corners from the box and its drop (mPoints) |
| 10 | `native/petari/src/Game/LiveActor/ShadowVolumeBox.cpp:154` | fan | numbers | shadows | as 9, cap |
| 11 | `native/petari/src/Game/LiveActor/ShadowVolumeBox.cpp:166` | fan | numbers | shadows | as 9, cap |
| 12 | `native/petari/src/Game/LiveActor/ShadowVolumeLine.cpp:90` | quad | numbers | shadows | two ends, widths, drops (8 points) |
| 13 | `native/petari/src/Game/LiveActor/ShadowVolumeLine.cpp:96` | quad | numbers | shadows | as 12 |
| 14 | `native/petari/src/Game/LiveActor/ShadowVolumeLine.cpp:102` | strip | numbers | shadows | as 12 |
| 15 | `native/petari/src/Game/Map/Flag.cpp:394` | strip | state | strips | cloth points, one strip per row |
| 16 | `native/petari/src/Game/Map/Flag.cpp:417` | strip | numbers | strips | stick: position, up, length |
| 17 | `native/petari/src/Game/Map/OceanBowl.cpp:328` | strip | state | strips | wave grid, one strip per column |
| 18 | `native/petari/src/Game/Map/OceanBowlBloomDrawer.cpp:30` | strip | state | strips | 17's grid, bloom pass |
| 19 | `native/petari/src/Game/Map/OceanRingDrawer.cpp:152` | strip | state | strips | ring part's water points |
| 20 | `native/petari/src/Game/Map/OceanRingDrawer.cpp:162` | strip | state | strips | 19's points, bloom pass; near the camera only |
| 21 | `native/petari/src/Game/Map/OceanSphere.cpp:696` | strip | state | strips | wave points (updatePoints); SiteMesh twin mMeshFace drawn beyond 10000 from the player (OceanSphere.cpp:412-416) |
| 22 | `native/petari/src/Game/Map/OceanSphere.cpp:700` | strip | state | strips | as 21, mMeshBack |
| 23 | `native/petari/src/Game/Map/WaterPlant.cpp:284` | strip | state | strips | per plant swing, 4 points |
| 24 | `native/petari/src/Game/Map/WaterRoad.cpp:285` | strip | numbers | strips | the kept mesh (mMeshHigh, WaterRoad.cpp:241-260) cut at the rail coordinate in its demo, the end row mixed (:262-325, from :710) |
| 25 | `native/petari/src/Game/Map/WhirlPoolAccelerator.cpp:136` | strip | state | strips | whirl points |
| 26 | `native/petari/src/Game/MapObj/AstroDomeOrbit.cpp:133` | strip | numbers | strips | ring: center, radius, width, height (mRingCos/Sin fixed) |
| 27 | `native/petari/src/Game/MapObj/AstroDomeOrbit.cpp:164` | strip | numbers | strips | as 26, side wall |
| 28 | `native/petari/src/Game/MapObj/ClipFieldFillDraw.cpp:54` | strip | fixed | 2-D | screen quad in eye space, screen size in the matrix; stencil fill |
| 29 | `native/petari/src/Game/MapObj/ElectricRail.cpp:462` | strip | numbers | strips | vanishing: the kept mesh (ElectricRail.cpp:441-446, drawn while waiting, :283-287) at radius mEaseIn |
| 30 | `native/petari/src/Game/MapObj/ElectricRailMoving.cpp:269` | strip | numbers | strips | as 29 at mRailWidth (ElectricRailMoving.cpp:89-93, :244-249) |
| 31 | `native/petari/src/Game/MapObj/GCaptureRibbon.cpp:17` | strip | numbers | strips | quad from start, end, offset |
| 32 | `native/petari/src/Game/MapObj/SnowFloor.cpp:663` | quad | state | quads | footprint trail, one quad per segment |
| 33 | `native/petari/src/Game/MapObj/SnowFloorTile.cpp:469` | quad | numbers | quads | tile quad; drawn 16 times identical per layer (sStripCount), as Petari's drawLayer at cec16c113 |
| 34 | `native/petari/src/Game/MapObj/SpiderThreadPart.cpp:517` | strip | numbers | strips | two points, side, up: upper edge |
| 35 | `native/petari/src/Game/MapObj/SpiderThreadPart.cpp:524` | strip | numbers | strips | as 34, right lower |
| 36 | `native/petari/src/Game/MapObj/SpiderThreadPart.cpp:531` | strip | numbers | strips | as 34, left lower |
| 37 | `native/petari/src/Game/MapObj/SpiderThreadPart.cpp:545` | strip | state | strips | thread points |
| 38 | `native/petari/src/Game/MapObj/SpiderThreadPart.cpp:561` | strip | state | strips | as 37 |
| 39 | `native/petari/src/Game/MapObj/SpiderThreadPart.cpp:576` | strip | state | strips | as 37 |
| 40 | `native/petari/src/Game/MapObj/SpinDriverPathDrawer.cpp:312` | strip | fixed | strips | path made once (initPositionList, SpinDriverPathDrawer.cpp:132); coord, mask, fade are values |
| 41 | `native/petari/src/Game/MapObj/TrampleStar.cpp:617` | list | state | strips | surface rebuilt by calcSurface each step (TrampleStar.cpp:293-338) |
| 42 | `native/petari/src/Game/MapObj/WarpPod.cpp:606` | quad | fixed | quads | two crossed quads per segment of a path made once (WarpPod.cpp:501-517) |
| 43 | `native/petari/src/Game/MapObj/WhirlPool.cpp:112` | strip | state | strips | whirl points |
| 44 | `native/petari/src/Game/Mii/MiiFaceTexture.cpp:179` | quad | fixed | 2-D | unit quad under an eye matrix, into the face texture |
| 45 | `native/petari/src/Game/Particle/ParticleDraw.cpp:243` | chosen | state | particles | corner batch |
| 46 | `native/petari/src/Game/Particle/ParticleDraw.cpp:245` | chosen | state | particles | corner batch with extras |
| 47 | `native/petari/src/Game/Player/MarineSnow.cpp:174` | quad | state | points | one camera-facing quad per flake: an instance per point |
| 48 | `native/petari/src/Game/Player/MarioActorSpecialDraw.cpp:144` | quad | numbers | 2-D | screen box from position and size |
| 49 | `native/petari/src/Game/Player/MarioActorSpecialDraw.cpp:411` | fan | fixed | fans | 16-segment circle under a matrix (position, normal, radius) |
| 50 | `native/petari/src/Game/Player/MarioActorSpecialDraw.cpp:446` | strip | fixed | 2-D | screen quad (frame ripple) |
| 51 | `native/petari/src/Game/Player/MarioActorSpecialDraw.cpp:689` | quad | numbers | 2-D | screen box from a projected radius |
| 52 | `native/petari/src/Game/Player/MarioFoo.cpp:658` | quad | numbers | quads | beam: center, side, front, length |
| 53 | `native/petari/src/Game/Player/MarioFoo.cpp:668` | quad | numbers | quads | as 52, other side |
| 54 | `native/petari/src/Game/Player/MarioFoo.cpp:737` | quad | state | quads | trail points |
| 55 | `native/petari/src/Game/Player/MarioFoo.cpp:745` | quad | state | quads | as 54 |
| 56 | `native/petari/src/Game/Player/MarioShadow.cpp:331` | chosen | state | shadows | collision polygons under the player (3 or 4 corners) |
| 57 | `native/petari/src/Game/Player/MarioShadow.cpp:441` | fan | fixed | shadows | 16-segment ellipse in the shadow target's alpha |
| 58 | `native/petari/src/Game/Player/MarioShadow.cpp:520` | list | state | shadows | edge polygons |
| 59 | `native/petari/src/Game/Ride/Creeper.cpp:492` | strip | state | strips | vine points |
| 60 | `native/petari/src/Game/Ride/Creeper.cpp:498` | strip | state | strips | as 59 |
| 61 | `native/petari/src/Game/Ride/Creeper.cpp:504` | strip | state | strips | as 59 |
| 62 | `native/petari/src/Game/Ride/PlantStalk.cpp:164` | strip | state | strips | grown points |
| 63 | `native/petari/src/Game/Ride/PlantStalk.cpp:177` | strip | state | strips | as 62 |
| 64 | `native/petari/src/Game/Ride/PlantStalk.cpp:190` | strip | state | strips | as 62 |
| 65 | `native/petari/src/Game/Ride/SpaceCocoon.cpp:819` | strip | state | strips | stalk points |
| 66 | `native/petari/src/Game/Ride/SwingRope.cpp:678` | strip | numbers | strips | one segment: two points, side, front |
| 67 | `native/petari/src/Game/Ride/SwingRope.cpp:688` | strip | state | strips | rope points |
| 68 | `native/petari/src/Game/Ride/Trapeze.cpp:679` | strip | numbers | strips | rope segment A to B, side, front |
| 69 | `native/petari/src/Game/Ride/Trapeze.cpp:693` | strip | numbers | strips | as 68 |
| 70 | `native/petari/src/Game/Ride/Trapeze.cpp:707` | strip | numbers | strips | as 68 |
| 71 | `native/petari/src/Game/Screen/DepthOfFieldBlur.cpp:109` | strip | numbers | 2-D | ramp of 4 stops from two distances |
| 72 | `native/petari/src/Game/Screen/ImageEffectLocalUtil.cpp:132` | strip | fixed | 2-D | unit quad |
| 73 | `native/petari/src/Game/Screen/MovieDraw.cpp:46` | quad | numbers | 2-D | x, y, width, height |
| 74 | `native/petari/src/Game/Screen/ScreenBlurEffect.cpp:81` | strip | numbers | 2-D | quad at -scale from fovy and aspect |
| 75 | `native/petari/src/Game/Screen/StarPointerBlur.cpp:157` | strip | state | 2-D | pointer trail points, runs of the trail |
| 76 | `native/petari/src/Game/Screen/StarPointerBlur.cpp:178` | strip | state | 2-D | as 75 |
| 77 | `native/petari/src/Game/Screen/WaterCameraFilter.cpp:141` | strip | numbers | 2-D | screen quad, texture centred on the player's screen position |
| 78 | `native/petari/src/Game/System/LumaDraw.cpp:353` | quad | numbers | mods | 2-D rect |
| 79 | `native/petari/src/Game/System/LumaDraw.cpp:745` | quad | numbers | mods | 2-D thick line |
| 80 | `native/petari/src/Game/System/LumaDraw.cpp:780` | list | numbers | mods | 2-D triangle |
| 81 | `native/petari/src/Game/System/LumaDraw.cpp:837` | quad | numbers | mods | 2-D image |
| 82 | `native/petari/src/Game/System/LumaDraw.cpp:1372` | quad | numbers | mods | 3-D line, thickness |
| 83 | `native/petari/src/Game/System/LumaDraw.cpp:1398` | list | numbers | mods | 3-D triangle |
| 84 | `native/petari/src/Game/System/LumaDraw.cpp:1621` | quad | numbers | mods | 3-D camera-facing image |
| 85 | `native/petari/src/Game/Ui/UiCommon.cpp:51` | quad | numbers | 2-D | layout pane quad: base, size, colours, coords |
| 86 | `native/petari/src/Game/Util/BezierSurface.cpp:716` | strip | numbers | strips | patch from its control points, by a table per divide level |
| 87 | `native/petari/src/Game/Util/BezierSurface.cpp:746` | strip | numbers | strips | as 86 with normals and coords |
| 88 | `native/petari/src/Game/Util/DirectDraw.cpp:50` | line list | numbers | lines | TDDraw::drawLine |
| 89 | `native/petari/src/Game/Util/DirectDraw.cpp:75` | line strip | numbers | lines | TDDraw::drawCircle outline, segments |
| 90 | `native/petari/src/Game/Util/DirectDraw.cpp:86` | fan | numbers | fans | TDDraw::drawFillCircle (2-D), segments |
| 91 | `native/petari/src/Game/Util/DirectDraw.cpp:108` | fan | numbers | fans | TDDraw::drawFillCircle by a normal |
| 92 | `native/petari/src/Game/Util/DirectDraw.cpp:128` | fan | numbers | fans | TDDraw::drawFillFan, angles |
| 93 | `native/petari/src/Game/Util/DirectDraw.cpp:160` | strip | numbers | strips | TDDraw::drawCylinder, segments |
| 94 | `native/petari/src/Game/Util/DirectDraw.cpp:191` | strip | numbers | strips | TDDraw::drawSpherePart, segments |
| 95 | `native/petari/src/Game/Util/DirectDraw.cpp:227` | quad | numbers | 2-D | TDDraw::drawTexture: position, size |
| 96 | `native/petari/src/Game/Util/DirectDraw.cpp:258` | quad | numbers | quads | TDDraw::drawTexture3D: center, normal, up, width, height, flips |
| 97 | `native/petari/src/Game/Util/DirectDraw.cpp:266` | quad | numbers | quads | TDDraw::drawFillBox (3-D): one quad from two corners |
| 98 | `native/petari/src/Game/Util/DirectDraw.cpp:276` | quad | numbers | 2-D | TDDraw::drawFillBox (2-D) |
| 99 | `native/petari/src/Game/Util/DirectDraw.cpp:296` | quad | fixed | quads | TDDraw::drawFillBox3D: unit cube face under the matrix (center, height, side, front) |
| 100 | `native/petari/src/Game/Util/DirectDraw.cpp:302` | quad | fixed | quads | as 99 |
| 101 | `native/petari/src/Game/Util/DirectDraw.cpp:308` | quad | fixed | quads | as 99 |
| 102 | `native/petari/src/Game/Util/DirectDraw.cpp:314` | quad | fixed | quads | as 99 |
| 103 | `native/petari/src/Game/Util/DirectDraw.cpp:320` | quad | fixed | quads | as 99 |
| 104 | `native/petari/src/Game/Util/DirectDraw.cpp:326` | quad | fixed | quads | as 99 |
| 105 | `native/petari/src/Game/Util/DrawUtil.cpp:80` | quad | fixed | 2-D | screen colour quad |
| 106 | `native/petari/src/Game/Util/DrawUtil.cpp:99` | quad | fixed | 2-D | screen picture quad |
| 107 | `native/petari/src/Game/Util/DrawUtil.cpp:158` | strip | fixed | shadows | screen quad clearing the stencil |
| 108 | `native/petari/src/Game/Util/DrawUtil.cpp:179` | quad | numbers | 2-D | fillScreenArea: min, max |
| 109 | `native/petari/src/Game/Util/DrawUtil.cpp:220` | strip | fixed | shadows | screen quad, half black where the stencil is set |

Read in passing:

- `SnowFloorTile::drawLayer` draws the same full-tile quad 16 times for each of the 12 layers, blended by alpha with
  depth written (`SnowFloorTile.cpp:434-475`), as Petari's `drawLayer` does at cec16c113: 192 quads a tile where 12
  give the same colour by a closed form, one quad a layer with the program's alpha `1 - (1 - a)^16` (P-196).
- Of the 36 state sites, 22 draw a band along a list of points with an offset to each side (20 strips and the 2
  trail quads: sites 1-4, 23, 25, 37-39, 43, 54-55, 59-65, 67, 75-76): one family whose points could be kept by the
  site and widened by the vertex program, as 2.12 leaves to that family's reading.

### 2.13 Step 5: both poses once a step, the matrices of each picture made on the GPU

Read at cfc2e368e.

**Today.**

1. The game: `Model::viewCalc` copies the skeleton's matrices into the model's pose (`capturePose`,
   `Model.cpp:532-535`), and `addPoseBlock` copies the pose into the frame's arena once a frame and pose stamp
   (`:619-660`, the copy at `:624-631`): two copies a step of every animated model's joints (P-144). A block holds
   joints in the world, or with the view inside them (calc mode 1: the console's billboards and camera-bound models), or
   a camera's base (calc mode 2, `:646-654`).
2. The render thread, at every walk, which is every picture (`RenderCore`'s walk, `draw_walker.cpp:195-316`): each
   camera at t (`:219`); per block, its joints at t when it moved (`core::joints_at`, `:294`), then
   `core::compose_rows` (`:298`; `compose.cpp:170-228`): per draw matrix the view times the joint, an envelope's
   weighted sum of joints (`weighted_sum`, `:120-160`), the inverse transpose for a scaled one (`:43-67`), billboards
   (`:78-116`), six rows each (`compose.h:13`), and the world rows again where a program reads them
   (`draw_walker.cpp:313`). All of it goes into the matrix ring each walk (`draw_walker.cpp:252`) (P-156).
3. Culling reads those rows: an item of one matrix is tested against the view's planes with its place from this
   walk's rows (`item_culled`, `draw_walker.cpp:933` on).
4. A run of ring draws is joined by rewriting each joining draw's matrix numbers and indices in the upload memory
   (`merge_ring_run`, `:695` on) (P-046).
5. I-003's design (ledger, E6b) records each draw pass once per list and replays it for an in-between picture with
   only the rows changed. Its rows are still this walk's CPU rows.

**The engine.**

- The game writes a block's joints once a step, into the list, from the skeleton's evaluation (one copy, P-144); the
  model's pose for game reads stays the game's.
- The render thread uploads each list's joints, flags, envelopes' indices and weights (the model's, registered once)
  and calc-mode bases once, when the list arrives: the list before is already on the GPU.
- Per picture the CPU writes the cameras at t and t. One compute pass, the same on the four renderers, makes every
  block's rows for that picture: the joints at t (each pair as `joints_at` mixes them today, the same rule), the
  envelopes' sums, the view, the inverse transpose, the billboards, the world rows where wanted, into the rows buffer
  the recorded passes read (I-003). Calc mode 1 is undone once: the game's joints carry the view of their step, so the
  pass applies the view of the picture to `inverse(view of the step) x joint`, made once per list; calc mode 2's base is
  mixed in the pass.
- Culling without the rows: per list, once, each item of one matrix gets a bound covering the steps between: a sphere
  around its joint's translations at both steps, reaching as far as the mesh's sphere does from the joint (its
  centre's offset plus its radius, by the larger of the two steps' largest stretch). The mix moves the translation
  on a line, turns on the sphere and mixes the stretch on a line (`affine_motion_at`, `affine.cpp:256-298`), so for
  t from 0 to 1 the mesh stays inside. Per picture the CPU tests that bound against the picture's planes. On a
  still object it is today's sphere; on a moving one it keeps more, never less.
- Ring runs name each joining draw's first matrix through an instance row, not by rewriting vertices and indices
  (P-046); the run's vertices are written once by their site (2.12).
- The CPU compose (`compose.cpp`'s `compose_rows`, `compose_world_rows`, `joints_at`) stays as the old path of 2.7.4
  until the step's last landing, which deletes it. The pass is the same program on every renderer, written once in the
  shader source the renderers share.

**Groups.** As 2.7.3: one joint; many rigid joints; weighted (skinned); billboard and calc mode 1; calc mode 2;
eye-space blocks.

**Proof.**

- 2.7.4's step-5 compare: one list walked twice, by the CPU and by the pass, at a step and at a fixed t between two,
  pictures equal within the tolerance Vulkan is held to against Direct3D 12, on every stop and renderer.
- A test of the pass against `compose_rows` for every block kind of the tours' models (rows equal within the same
  tolerance), and of the cull bound: no item culled by the new test that today's keeps, on the play route's lists.
- The CPU's time per picture (from the walk's start to the end of its rows) and the GPU pass's time, before and after,
  at an unlimited picture rate in HoneyBeeKingdom (P-156's scene) on the PC and the tablet; the game step's time
  unchanged or lower (file 08 7.4).
- After the last landing: `git grep -n "compose_rows\|joints_at" runtime`: 0, and the walk writes no matrix row.

### 2.14 Step 6: lights as world records, a draw naming its set

Read at cfc2e368e.

**Today.**

1. The game loads lights into numbered places, the console's eight registers: all eight to white at every scene draw
   (`initLightRegisterAll`, `LightFunction.cpp:14-28`, from `GameScene.cpp:323`); an actor's two lights, its alpha light
   and its ambient before each run of its draws (`loadActorLightInfo`, `LightFunction.cpp:178-190`, from
   `ActorLightCtrl::loadLight`, `ActorLightCtrl.cpp:30-38`, and `LightUtil.cpp:27-31`); the coin's shine
   (`LightFunction.cpp:192-203`); one point light (`LightFunction.cpp:205-216`); eight white lights at the eye
   (`LightFunction.cpp:218-230`, from `LightDirector.cpp:18` and the mods' `LumaDraw.cpp:1538`). A light fixed in the
   world is multiplied by the step's view at each load (`LightFunction.cpp:170`, `:209`); a light that follows the
   camera is given in eye space and marked (`Frame::setLightFollows`).
2. The frame list copies the places into a new set whenever one changed or the block's camera did
   (`currentLightSet`, `FrameList.cpp:713-744`; `setLight`, `:746-758`); a set is a run of lights in eye space with
   follow bits (`nebula_frame_list.h:38-56`); an item names its set.
3. The render thread writes each set's rows at every walk, and between two steps mixes each light with its pair,
   taking a world light through the camera's motion (`draw_walker.cpp:317-343`, `light_rows_at`).
4. The ambient is not in the set: it is `MaterialAmbient`'s global pair (2.11), set by the same `loadActorLightInfo`
   (`LightFunction.cpp:189`).
5. In the game step: every visible actor looks for its light area through every light area (P-152:
   `ActorLightCtrl::update`, `ActorLightCtrl.cpp:23-28`), and every moving actor asks for its shadow's drop to be found
   again (P-140).

**The engine.**

- A light is a record in the world (or in the eye, for a light that follows the camera, named as such): colour, terms,
  position, direction. The area lights' entries (player, strong, weak, planet: two lights, the alpha light and the
  ambient each, `getAreaLightLightData`, `LightFunction.cpp:85-92`) are records made when the light data loads; the
  coin's shine, the point lights and the white eye lights likewise. A record changes only when its source does (a point
  light that moves, an actor's blend between two area lights, once a step while it lasts).
- A light set is a short list of light records and an ambient colour: one per area-light entry, one per blend in
  progress, one for the eye's white lights. An item names its set, as today; the set's lights are not copied into
  the list at each change of place, only named. The view is applied by the GPU with the picture's camera (in step 5's
  pass or the light's own rows), so no light is multiplied by the view on the CPU and none is mixed through the
  camera's motion: a world light does not move between two steps unless its record did.
- `initLightRegisterAll` and the numbered places go; `MaterialAmbient` goes (its readers, 2.11, read the set's
  ambient). A material names a light by its number in the set, as today (`NebulaFrameLightSet`'s comment); the
  numbers keep their meaning (0 and 1 the actor's lights, 2 the alpha light, 3 the shine, 4 the point light).
- The game half, kept to an identical replay: an actor tests the area it is in first and walks the areas only when
  it has left it, and a still actor (the binder's still test, P-138) does not ask (P-152); an actor's shadow keeps its
  drop when its position, direction and length are bit-equal to the step before and no collision part under it moved,
  was added, removed or switched (a change count on the keeper) (P-140).

**Groups.** As 2.7.3: the actors' lights (`loadActorLightInfo`); point lights; the player's light; the map's lights.

**Proof.**

- The gate's pictures of both paths on every stop and renderer equal (2.7.4); the step hash identical with and
  without P-140's and P-152's changes over the tours and the play route, with a test run that computes both ways and
  compares every step.
- A test of the light rows: for the tours' lists, the rows the GPU path gives each light at t equal `light_rows_at`'s.
- Counts before and after: light sets and light bytes per list, view multiplies per step (0 after), light-area volume
  tests per step in HoneyBeeKingdom (P-152's scene), shadow drop searches per step.
- `git grep -n "initLightRegisterAll\|MaterialAmbient" native`: 0 after the last landing.
- The game step's and the walk's time on the PC and the tablet, before and after (file 08 7.4).

### 2.15 E4: the frame's passes, planned from what each reads and writes

Read at 241395b49 (11-19). File 01's E4: the game states what each pass reads and writes, a planner builds the
frame, the console's draw order goes; done when the full-size scene opens once per picture on the tablet in every
tour stop when nothing reads it back, pictures unchanged, GPU time by pass recorded before and after.

**Today.**

1. The pass record. A pass is one of thirteen kinds (`nebula_frame_list.h:225-240`): draws, a clear, a copy of the
   frame (`COPY_FRAME`), the frame handed to a texture with a new picture after it (`TAKE_FRAME`), the scene's begin,
   opaque end and end, three bloom kinds, show, a mod's post program and a native call (the probe kind went at S0.2.6).
   The record (`:262-280`) holds a target key, the clear, a size, the channels, an over-frame texture and depth for a target
   pass that reads the frame, and the scene's camera and view. It names nothing it reads: what a draw pass samples is
   found by the host through each item's texture table.
2. The game makes passes in its draw order through `Frame::` (`FrameList.hpp:134-167`). A frame of play
   (`GameScene.cpp:323-329`): the Mii faces into their targets, the mirror, `draw3D`, `draw2D`. The mirror draws its
   view into the frame itself, has it copied at the capture's timing and clears the frame again
   (`GameScene.cpp:487-504`): the console's way of making a reflection texture. `draw3D` (`:511-573`) opens the scene
   and draws into it, in order, the fall-out field's target passes over the frame (`:534`; `FallOutFieldDraw.cpp:98`,
   `:109`), the capture's copy (`:556`; one target at the frame's size, `CaptureScreenDirector.cpp:23`, copied at the
   one timing the scene asks, `:27-38`), the image effects (`:561`: depth of field, `DepthOfFieldBlur.cpp:88`, `:144`;
   the blurs, `ImageEffectLocalUtil.cpp:115`, `:152`, `:155`, `FullScreenBlur.cpp:40`; the bloom), the player's
   indirect draws (`:568`; `MarioActorSpecialDraw.cpp:111`, `:436`), then ends it. Every target pass and frame copy of
   the game (18 calls, `git grep -n -E 'Frame::(targetBegin|targetBeginPixels|targetBeginOverFrame|copyFrame|takeFrame)\(' 241395b49 -- native/petari/src`)
   is one of these, or the shadow capture (`MarioShadow.cpp:404`), the galaxy map's capture
   (`GalaxyMapController.cpp:626`), the water filter's take (`WaterCameraFilter.cpp:126`) and the mods' views
   (`LumaDrawView.cpp:281`, `:319`, before the scene, `GameScene.cpp:520`).
3. The walk (1.7 item 8) runs the passes in list order with two plans made once per list (`walk_plan.h`): a target
   run that reads nothing written before it and is read by nothing before it is drawn first (`plan_early_targets`,
   `:28-115`); a scene depth clear no later pass reads is left for the next pass to make (`plan_late_depth_clears`,
   `:121-157`). `walk_plan_tests.cpp` tests the two on made-up lists.
4. Each renderer turns a target pass into a trade of its scene set for the target's set and back
   (`swap_target_set`, `renderer_d3d12.cpp:5554-5570`; `begin_target_pass` and `end_target_pass`, `:5572-5622`), with
   its own barriers written by hand at each end; Vulkan, Metal and Direct3D 11 have their own copies of the same trade
   (`renderer_vk_frame.cpp:725`, `renderer_mtl_scene.mm:723`, `renderer_d3d11_scene.cpp:418`). A target pass inside
   the scene ends the scene's render pass and opens it again after (1.9). Vulkan opens every pass from a render pass
   object picked from variants made ahead by load, clear and discard (`renderer_vk_frame.cpp:483-494`), with a
   secondary-command-buffer form for parallel recording (`:510-511`); Direct3D 12 and 11 bind targets with
   `OMSetRenderTargets` and have no pass object; Metal opens encoders from descriptors, with a parallel encoder for
   recording on workers (`renderer_mtl_draw.mm:353`, `renderer_mtl_scene.mm:482`). No backend makes a target that
   lives only in tile memory (`git grep -n -E 'MTLStorageModeMemoryless|LAZILY_ALLOCATED' 241395b49 -- runtime/src`:
   0).

**The game.**

- Each pass states its reads and writes as keys: the frame's colour, its depth and stencil, a target, a copy.
  `Frame::` knows them where it writes the pass: `targetBegin` writes its key, `targetBeginOverFrame` also reads the
  frame keys it names, `copyFrame` reads the frame and writes its key, `takeFrame` writes its key and starts a new
  frame picture, `post` and `native` read and write the frame. A draw pass's reads are the targets in its items'
  texture lists: the writer adds a target key to the pass's read list when it writes an item whose list names one
  (the list's builder sees each texture; RenderTexture's entry marks a target, `RenderTexture.cpp:192`), so the host
  scans no table. A draw pass's
  writes to the frame's depth and stencil follow its pipelines' state words, as `walk_plan.h:130` and `:193-195`
  read them today; the writer records the union of the pass's items' write bits once.
- The mirror draws into its own target (`targetBegin` with the mirror's key at its view's size), read by the mirror
  materials by that key; the frame is no longer drawn, copied and cleared for it (`GameScene.cpp:487-504`). Today the
  capture actor runs at both places, the mirror's (`:497`) and the scene's (`:556`), and at the default timing,
  "Indirect" (`CaptureScreenDirector.cpp:15-19`, not asked for, so it copies at each), the one capture target holds
  the mirror's picture for the mirror materials and then the scene's for refraction: two versions of one key, which
  the planner keeps apart (below). A scene that sets another timing while a mirror shows is looked for in the
  landing; the gate's mirror stops compare both paths.
- What stays in the game's order: the passes inside the scene that read the frame at other pixels than their own
  (the capture for refraction, the image effects' blurs, the depth of field, the fall-out field's mask and glow, the
  frame ripple): each is a real break of the scene, kept, and planned (below).

**The engine.** One planner in `runtime/src/render/core` (shared by the four renderers through F3's layer, P-022),
pure and tested with no GPU, as `walk_plan.h` is today; it replaces the three plans.

1. Versions. Each write of a key makes a new version; each read names the version current at its place in the list.
   The list's order is the game's meaning; the planner may move a pass only where every read still sees its version.
2. Culling. A pass whose versions nothing reads is dropped, unless it shows, calls native code, or writes a target
   read in a later frame. A target's last version of a real frame is kept for the next frame by name (5).
   A frame copy that no visible draw reads is not made.
3. Order. The list's order, as a stable topological sort with list position as its key; a pass that depends on
   nothing the open scene has written moves before the scene opens (`plan_early_targets`'s rule, now for every pass
   kind, so the Mii faces, the mods' views, the shadow capture and the mirror's own target never break the scene).
4. Instances. Consecutive passes that draw into the same attachments with no read of those attachments between become
   one render pass instance. A break stays only where a pass samples a version the open instance is writing. Each
   instance's load and store come from the versions: clear where its first use clears, nothing loaded where no
   earlier version is read, stored only where a later instance or frame reads it; depth and stencil not stored after
   the scene unless `draw.post` or `draw.native` reads them. A multisampled attachment resolves once, at the
   end of the last instance that writes it, only when its single-sample version is read; its multisampled contents
   are not stored unless a later instance loads them. An attachment neither loaded nor stored is transient: on a
   tiled GPU it lives in tile memory only (Metal's memoryless storage; Vulkan's transient attachment usage with
   lazily allocated memory, where the device offers it).
5. Names. Two pictures are asked for by name, not by what was shown last: the picture the player sees
   (`display_pictures_.latest()`, an in-between picture included; the camera's cover copies it, P-021's decision) and
   a target's version from the last real frame (an in-between walk writes its own versions under
   `kGeneratedPicture`, `draw_walker.cpp:1416-1419`, and never replaces it). E6b 6.3's unseen walk at t = 1 goes with
   the second name.
6. Barriers. The planner lists each key's uses in plan order and emits a transition before the pass whose use
   differs from the last: from attachment write to sampled, sampled to attachment, copy source and destination,
   resolve. The shared layer turns them into Direct3D 12 resource barriers, Vulkan image barriers and Metal's encoder
   boundaries; Direct3D 11 needs none. The trade of scene and target sets in each backend goes: a pass names its
   attachments.
7. Memory. Targets come from a pool keyed by size, format and samples. A size change makes the new target beside the
   old and the old is freed when the frame slot that last used it has passed its fence; nothing waits for the whole
   GPU inside a frame (P-028's form, for every renderer and for the upscalers' and frame generation's own targets).
8. Pass objects. Vulkan opens instances with dynamic rendering (core in 1.3, `VK_KHR_dynamic_rendering` before),
   load and store given per begin, so the variant tables of render pass objects go; where a device has neither, the
   plan's load and store pick a render pass object made once per key and kept. Parallel recording keeps one instance:
   the workers record secondary buffers executed inside it (Vulkan), a parallel encoder's children (Metal), or command
   lists that continue the same bound targets (Direct3D 12).
9. After the scene. Where no upscaler sits between, the bloom's composite, the 2-D layer and the conversion to the
   output are one instance writing the swap chain's picture (on tiled GPUs this is the gain; elsewhere it costs
   nothing). The bloom's chain keeps its taps; a phone setting for a shorter chain (the owner allowed it) is measured
   for time and for the picture's difference before it is offered.
10. Probes: none. S0.2.6 deleted the probe pass with the whole question-and-answer path, so the planner has no part for
    depth questions (plan decision 9).

**Rows.**

| Row | What E4 does | Landing |
| --- | --- | --- |
| P-006, P-055 | Vulkan's scene stores depth, stencil and the multisampled colour at every pass end: stored only where read later; transient attachments | 3 |
| P-031 | Metal's the same | 3 |
| P-053 | a resolve per consumer with nothing drawn since: one resolve at the last write that is read (Vulkan and Metal; Direct3D 12 and 11 already skip it) | 3 |
| P-025 | with MSAA, bloom and the scene's anti-aliasing resolve and then copy: the resolved version is read where it lies | 3 |
| P-063 | Direct3D 11's scene anti-aliasing copies the scene: it takes it, as Direct3D 12 and Vulkan do | 3 |
| P-007 | parallel recording splits Vulkan's scene into several instances: one instance | 5 |
| P-027 | the HDR 2-D layer's three full-size conversions: the merged after-scene instance | 6 |
| P-021 | the cover's copy and the frame pass's draw folded into one | 6 |
| P-026 | Direct3D 12's present views made every frame (built 11-18, 85eeb6d67): the planner's attachments keep their views | 4 |
| P-028 (Vulkan), P-054, P-030, P-049, P-050, P-057 | size changes and the upscalers' and frame generation's contexts made beside the old, retired by the fence | 7 |
| N-1082 | a capture's readback answered by its frame later, through the planner's readback place | 2 |
| P-044 (1) | the planner's test on recorded frame lists | 1 |
| P-009, P-032 | the mods' post and native passes are passes every renderer is asked to run; HDR output is the show pass's format (2.6) | 2 |

**Landings,** each with the old and new path side by side behind one switch (as 2.7.3; the groups here are the
planner's parts: order and culling; instances, load and store; barriers; the after-scene instance), the gate comparing
both paths' pictures in one run:

1. The records: each pass's read and write keys written by `Frame::`; a test that, over the tours' recorded lists, the
   stated sets equal what the host derives today through the tables and state words; P-044's recorded-list fixtures
   (pass kinds, keys, item counts and state words only, no game content) and their expected plans.
2. The planner (versions, culling, order, names) in core, under its tests; the walker runs its order. The
   game's mirror target. `walk_plan.h`'s two plans deleted in this landing's last part.
3. Instances, load and store, resolves and transient attachments, per backend through F3's layer.
4. Barriers from the plan; each backend's set trade and hand barriers deleted.
5. One instance under parallel recording.
6. The after-scene instance on tiled GPUs; the cover's single draw; the phone bloom setting, measured.
7. Targets beside the old at a size change for every renderer and the upscalers' and frame generation's contexts.

**Proof.**

- Tests with no GPU: the planner over the recorded lists gives the stored plans; over random lists (any kinds, keys
  and reads), every read in the plan sees the version list order gives it, every dropped pass writes nothing read,
  the order is a topological order of the dependencies, and no key is stored that nothing reads.
- The gate's pictures of both paths equal on every stop and renderer (2.7.4).
- The measure E4 is done by: `--diagnostics gpu-passes` on the tablet in every tour stop counts the full-size scene's
  openings per picture (one where nothing reads it back) and its loads and stores; GPU time by pass on the tablet
  before and after; the targets' memory before and after.
- After the last landing: `git grep -n -E "plan_early_targets|plan_late_depth_clears|swap_target_set|discard_color_passes" runtime`: 0.
- The game step's and the walk's time on the PC and the tablet, before and after (file 08 7.4: never slower).

### 2.16 E6b: how a picture reaches the display, and what it costs

Read at 241395b49 and, for the schedule, at I-016's commit on `rows/i016` (697535a86, built, not landed), on 11-19.
File 01's E6b with audit file F (11-17) and its measures; the owner's decisions of file 00 section 3 ("Frame rate":
a device that cannot hold 120 is never switched to 60 by itself; the 59.94 lock at 0.2%). Done when every measure of
file 00 4.3 holds on the PC and the tablet.

**Today.**

1. The schedule. I-016 moves it out of `render_core.cpp` into `FrameSchedule` (`runtime/src/render/frame_schedule.cpp`
   on `rows/i016`), which asks the display and the GPU through `ScheduleDisplay` (`frame_schedule.h:27-47`) and is
   driven by `frame_schedule_tests.cpp` on simulated displays. What file F section 3 found right stays in it: slots on
   the display's vblanks where they are timed (`frame_schedule.cpp:136-141`), the step clock's delay that tracks the
   frames' lateness at its 97th percentile and moves by at most 2% of a step a frame (:98-121), a stall that skips the
   slots it missed (:461-467), and the two governors (GPU :160-212, cadence :214-379).
2. A frame's in-between pictures start a step late (G1, I-004). A list that shows is held (`render_core.cpp` on
   `rows/i016` :2230-2245) until the next step asks for its present; only then does its sequence start (:1504-1510),
   with the present's asked moment as its arrival (`frame_schedule.cpp:63-64`). Measured 11-17: 17 to 23 ms.
3. The real frame is rarely shown on vblanks (I-002). A lead that puts `t = 1` on a slot is given only off vblanks
   (`locked`, `frame_schedule.cpp:84-85`, :135); on vblanks the lead is 0, `t = 1` falls between refreshes, and the
   next sequence's start walks the frame unseen (`render_core.cpp` on `rows/i016` :796-805). Measured: the real frame
   shown for 5 to 13 of about 2,640 steps with VSync on.
4. The present, by renderer.
   - Direct3D 12 and 11: flip model, frame latency 1 (`renderer_d3d12.cpp:3356`, `runtime/src/render/d3d11/renderer_d3d11.cpp:308`); the
     render thread waits on the latency object before each present (:4405-4413). A game frame at 60 goes out with sync
     interval 1 (aba64e331, M1: `present_sync_interval`, :4305-4331). The frame statistics are read after every present
     into the pacing recorder (`dxgi_pacing.h:15-37`, :4479-4483). No display time is asked. At 240 Hz a picture
     reached the display two refreshes after its moment, against one on Vulkan (M2, measured 11-17; no change since).
   - Vulkan: with VSync, the present before the last must be shown before a picture on the slot clock is queued
     (present wait, `renderer_vk.cpp:2276-2299`). Present timing is taken where the surface offers it
     (`:1034-1058`), for the reports only: no time is asked with it (:2324-2334). On Android the display timing
     extension asks each picture's time on the vsync grid (:2361-2398). A frame a refresh late goes out as FIFO_RELAXED
     (:2335-2349).
   - Metal: a drawable is presented as soon as its commands are scheduled (`renderer_mtl.mm:802-803`); its presented
     time is reported (:788-801). A drawable that waited over `kDrawableWaitMs` drops that picture, never two running
     (:1222-1237): right for the game's own frames, a lost picture for an in-between one (I-006). The clock is
     `CVDisplayLink` on macOS and `CADisplayLink` at the display's fastest rate on iOS
     (`display_clock_apple.mm:76-105`, :143-157), held while out of sight (:243-262, 17ad0a4cd; P-034's proof owed).
   - Linux: no display clock (`platform/sdl/display_clock.cpp:202-220`).
   - Variable refresh: detected when the measured vblank interval is under 0.8 of the mode's
     (`renderer_common.h:166-174`), which in-between pictures near the full rate hide (I-007); presents are held under
     the mode less mode² / 3600 (:175-179).
5. What is measured. The pacing recorder is on in every run on every renderer and writes the `[pace]` measures of
   file 00 4.3 every ten seconds (`pacing_recorder.h:4-24`). The timeline is Windows only (I-012). A pointer's
   latency from its sample to its picture is counted (`render_core.cpp:476-487`).
6. Waits. A wait to a slot sleeps and then spins the last margin, which follows the timer's lateness between 100 µs
   and 2 ms (`precise_wait.h:38-50`, :72-86); M4 measured the render thread at 163 ms of CPU a second at 120 pictures
   a second on the PC, and it has not been measured on a phone. With VSync off and a cap, `PresentPacer` sleeps up to
   2 ms (`renderer_common.h:181-202`).
7. Holding the rate. The GPU governor fits the in-between pictures into nine tenths of a step by a GPU reading
   (`frame_schedule.cpp:160-212`); the cadence governor drops them when they cost the game its steps and tries again
   with back-off (:214-379). Dynamic resolution aims the scene's GPU time at a picture's budget
   (`resolution_governor.h:3-10`, :58-60). Neither knows of the other (I-010); neither reads what is shown (T3: 72
   uneven pictures a second, nothing acting); the GPU reading follows the clock speed the governor's own decision
   left (T4). The performance hint's target is the game step (`native/host/host_main.cpp:1191`), told the busier
   thread's wall time a step (`perf_hint.h:4-9`, P-069). Nothing in play reads the device's heat
   (`git grep -n -i -E "thermal|headroom" 241395b49 -- runtime platform`: the benchmark's own heat measure and
   unrelated names only).
8. What the hand controls is drawn from the list: the pointer and the menu's cursor as old as their step (P-078,
   P-163).
9. Cuts and teleports. Two places state a camera cut (`CameraAnim.cpp:132`, `CameraViewInterpolator.cpp:209`, through
   `MotionSignal::cameraCut`, `MotionSignal.cpp:136-137`), carried as `NEBULA_FRAME_CAMERA_CUT`
   (`FrameList.cpp:502-504`); a turn is never a cut (`frame_interpolation.h:35-45`, M-068), a jump of the eye past the
   limits is. Stated teleports: a new life, a death, the player placed, a draw slot's epoch (`LiveActor.cpp:88`,
   `MarioActorGameOver.cpp:66`, `MarioAccess.cpp:264`, :552, `ActorMovementUtil.cpp:511`, `NameObjCategoryList.cpp:25`).
   The list's `camera_epoch` is written (`FrameList.cpp:502`) and read by nothing
   (`git grep -n camera_epoch 241395b49 -- runtime`: 0).

**The target.** File F section 5's rules, with rule 1 as I-001 landed it (`step_lock.h`): exactly 60 steps a second
except a lock within 0.2% with in-between pictures off; each picture shows the game at the moment it reaches the
display less one constant delay; a picture's CPU cost does not grow with what is drawn; what the hand controls is read
at the picture's moment; every platform does the same or says on screen why not.

1. Start at arrival. A list's sequence starts when the list arrives. Its reference is its own step's moment plus the
   step clock's delay, tracked as now; the held list and the present-only gate go, and the next step's present request
   only feeds the cadence count. A slot before the reference (a list in earlier than its step's usual delay) shows
   the sequence before at its own `t`, as N-1084's row says, never the frame before early. The step clock keeps its due
   times (P-274's form: missed steps run back to back up to a bound, the rest dropped with a log line), so the
   reference never slips against the wall clock.
2. The phase at whole multiples. On vblanks at a rate that is a whole multiple of 60 with the step locked to the
   display (`step_lock.h`, exact multiples only with in-between pictures on), the sequence takes the lead that puts
   `t = 1` on a refresh: the real frame is shown and no walk is thrown away at 120, 240 and 360 Hz. At other rates the
   unseen walk goes with E4's named last real frame (2.15, the engine's item 5).
3. A display model. `ScheduleDisplay` answers two more questions, through each platform: when will a picture presented
   now be shown, and when was the last one shown. The schedule then picks each picture's `t` from its predicted
   display time and corrects the lead from the actual one.
   - Windows: the frame statistics already read at every present feed the model; the lead adapts as Android's does
     (`present_slots`), so a picture starts as late as it safely can. M2's second refresh is traced with those
     statistics before anything changes (the latency object's wait, the flip queue or the compositor), and the change
     makes a picture show at the first refresh after its slot, as on Vulkan.
   - Vulkan: present timing asks each picture's time where the surface offers it (the same check that already takes
     its reports); otherwise present wait as now. Android adds present timing beside the display timing extension,
     each picked at start by what the device offers. Android's lead at 60 Hz (T6: three refreshes) comes down to what
     the device holds once its GPU clock is held (5. below): file 00 4.3 asks two.
   - Linux: present timing where present; otherwise a refresh grid measured from when presents complete (present
     wait's answers). Until one runs, the settings mark in-between pictures on Linux as not synchronised.
   - Apple: the newer Metal display link, which gives each frame its target presentation time, replaces the display
     clock and paces the render thread; the drawable is presented at that time; the rate is asked as a range; an
     in-between picture is never dropped for a drawable wait (I-006). The names and versions are taken from Apple's
     documentation in the landing. Built by the Mac job, run by the owner later.
   - Variable refresh: told by the system where it says so; otherwise measured on the display clock's own vblank
     waits with no in-between picture in flight, so its pictures cannot hide it. Its own path: presents spaced evenly
     by the precise wait under the cap, each drawn for its own moment; any target rate is then even.
4. The spin. Where a display time is asked (Vulkan present timing, Android display timing, Metal's display link), the
   render thread starts a picture its lead ahead and the display holds it to its time; the closing spin of
   `PreciseWait` stays only where no time can be asked. The render thread's CPU time a second is measured on the tablet
   before and after.
5. Holding the target (6.4). One arbiter in the schedule, pure and under its tests, replaces the GPU governor's
   picture cap and feeds dynamic resolution. Its inputs: the share of pictures on time and the late moments (the
   pacing recorder's counts), the wall time from picture to picture against the slot, the GPU time, and the device's
   thermal headroom. Its order: resolution first, then nothing else; the one reduction of pictures that stays is the
   cadence guard that protects the game's own 60 steps. No decision reads a GPU time taken under a clock its own
   decision left: the performance hint holds the clock (its target the slot, told the wall time of a picture's CPU and
   GPU work with the newest calls the system has), and a return is judged by trying it, with back-off. When the guard
   acts its pictures are placed on every n-th refresh by the display clock (T5). A target missed for a sustained time
   is said in the settings with what to change; nothing switches by itself. The first start's measure picks a target
   the device holds.
6. Fresh input (6.5). The pointer and the menu's cursor are drawn from the newest device sample at the picture's
   moment; the game's logic keeps its per-step value. A mod's free-look camera: the host applies the newest look
   rotation to the picture's view through one call; LumaKit's mark for it waits for the mods' work (parked 11-18).
7. Stated cuts and teleports (6.6). Every place the camera code changes view without blending states the cut, and
   every warp or respawn of an object states it (the landing reads `Game/Camera` and the position setters whole and
   lists them); the thresholds stay as a net. The host compares `camera_epoch` between the two lists of a pair instead
   of the flag, so a cut in a list never shown still parts the pair; the flag goes.
8. The cost of a picture (6.3). No unseen walk (2. and E4); blending on the GPU (E3 step 5, 2.13); a step's draws
   recorded once and executed again for each picture where the API allows it, by each API's own rule read in its
   documentation, else recorded with no per-draw arithmetic; a pass none of whose read versions changes with `t` (E4's
   versions say this exactly) is kept from the real frame, not drawn again. Each is measured per pass on the tablet
   first.
9. Defaults (6.7). Decided 11-22 (the owner: "should be 60fps."): the default stays 60 on every system and a player
   chooses a higher rate (P-042); the Android fall-back and the surface vote read the step rate and not the literal 61;
   every setting on these pages shows only where it acts. The earlier recommendation, a 120 Hz display starting at 120,
   is withdrawn.

**Rows.**

| Row | What E6b does | Landing |
| --- | --- | --- |
| I-004, N-1084, P-274 | the sequence starts at its list's arrival on its step's moment; an early slot shows the sequence before; the step keeps its due times | 1 |
| I-013 | the step's phase measured from the step's own work, with the start moved | 1 |
| I-002 | the lead at whole multiples on vblanks (the rest with E4's last real frame) | 2 |
| I-005, I-006, I-007, I-012 | a display model per platform; the Linux clock; the Apple display link; variable refresh told or measured unhidden; the timeline on every platform | 3 |
| P-312 (M2), P-317 (T6) | a picture at the first refresh after its slot on Direct3D; Android's lead down to what the held clock allows | 3 |
| P-314 (M4) | no closing spin where a display time is asked | 4 |
| I-010, P-315 (T4), P-316 (T5), P-318 (T3) | one arbiter on what is shown, the clock held, capped pictures placed evenly (P-318 closes at landing 8 where the render thread's time for a picture, not the arbiter, keeps a tour stop off the slot: the native plan's G2.5, "a rule against a jam") | 5 |
| P-078, P-163 | the pointer, the cursor and a mod's look read at the picture | 6 |
| I-014 | every cut and warp stated; `camera_epoch` read by the host | 7 |
| I-003 | the cost of a picture (with E3 step 5 and E4) | 8 |
| P-042 | the default stays 60 (decided 11-22); the Android fall-back and the vote on the step rate | 9 |
| P-311 (M1), P-034 | the after-measure of aba64e331 on the PC; P-034's start check in the Mac job | 3 |
| P-313 (M3) | traced with the timeline at a scene restart on Vulkan; fixed where it lies | 3 |
| P-319 (T7) | the bloom's share on the tablet: E4's after-scene instance and the phone setting (2.15, landing 6) | E4 |

**Landings,** each behind its own switch with the old path beside it until its measures hold:

1. The start at arrival in `FrameSchedule`, with its tests; the step's due times (P-274).
2. The lead at whole multiples.
3. The display model, one platform at a time (Windows, Vulkan and Android, Linux, Apple, variable refresh), each
   with the `[pace]` measures before and after.
4. No closing spin where a time is asked.
5. The arbiter, the hint's target and calls, the heat input.
6. Fresh pointer, cursor and look.
7. Stated cuts and teleports; `camera_epoch`.
8. The cost of a picture, with E3 step 5 and E4 landing 2.
9. The default.

**Proof.**

- `frame_schedule_tests.cpp` on simulated displays at every rate of file E A.5, under steady, varying and spiking
  load: a sequence starts at its arrival; at 120, 240 and 360 Hz with the step locked a refresh lands on `t = 1`;
  every slot's `t` is its predicted display time's; the arbiter lowers resolution before anything else, never reads
  its own clock's effect (a simulated GPU whose time rises when it idles), places capped pictures evenly, and returns
  by trying.
- The `[pace]` measures of file 00 4.3, recorded by the game: on the PC with Direct3D 12, Direct3D 11 and Vulkan at
  60, 120, 144 and 240 Hz with VSync on and off; on the tablet at 60 and 120 Hz in every tour stop, with Mario moving
  where scripted input reaches. The tablet at 120 Hz holds in all 102 stops.
- The render thread's CPU time a second on the tablet before and after landing 4.
- Linux and Apple: built by the CI jobs, the Mac job's start check run; their measures are the owner's runs.
- After landing 1: `git grep -n -E "interp_start_pending_|interp_list_waiting_|run_pending_interp_start" runtime`: 0.
- The game step's time and the walk's, PC and tablet, before and after (file 08 7.4: never slower).

### 2.17 F7: the game step on the cores

Read at 241395b49 (11-19), from audit file B R8.2 (what stops each part running on workers), each blocker found again
at this commit. File 08 5.D.6: it lands only with the replay identical, the thread sanitizer clean and the `soak` check
passes. Its rows: P-040, P-146 (with N-3013 and N-3014, `app/work`'s), N-1083. E7's other changes (collision and
sensors found by position, P-129 to P-141) are faster single-threaded work and are not this section's.

**Today.** The step runs on one thread (`git grep -n -E "std::thread|parallel_for" 241395b49 -- native/petari/src`:
0). What a second thread would collide with:

| Part | Shared state at 241395b49 |
| --- | --- |
| Animation and skinning | the `Skeleton` globals (`Model.cpp:13-19`) and its one stack (:136); joint callbacks call actor code (:175) |
| Visibility | `ClippingActorHolder::movement` (`ClippingActorHolder.cpp:36`) moves actors between lists as it judges them; `LiveActor::sClipScope` is one global mode (`LiveActor.cpp:292`, set at `ClippingActorInfo.cpp:78-114`) |
| Particles | one `EmitterWork` for every emitter (`EmitterManager.cpp:65`) |
| Collision | each keeper's one result array (`CollisionCategorizedKeeper.cpp:14`); `sAreaPrisms` (`CollisionParts.cpp:527-530`); the binder's `sLocalPlanes` (`Binder.cpp:78`) |
| Shadows | the holder's request list (`ShadowController.cpp:43`) and the collision queries above |
| Frame list | 13 file-scope variables of the writer (`FrameList.cpp:250-320`) |
| Loads | every read copies from the mapped store on the game thread (N-1083) |

**The target.**

1. One worker pool for the host and the game, sized from the processors the process may use (N-3014), at the game
   thread's priority, with a fork-and-join call the game makes inside its step: the game thread takes a share and
   returns when every share is done. No work crosses a step.
2. Order is the game's. Each parallel part computes into per-item results and the game thread applies them in list
   order, so a step's effects are the same as one thread's whatever the worker count:
   - visibility: a pure judge (camera planes and the actor's sphere) writes one result per actor; the moves between
     lists and the draw-list changes are applied after, in order; the ground cache a judge reads is filled first;
   - animation: models with no joint callback and no calc callback pose on workers, each with its own skeleton state
     (a struct passed by pointer, replacing the globals); the rest pose on the game thread in list order;
   - particles: emitters with no callback run on workers, each worker with its own work record and chunk cache;
     finished emitters are freed on the game thread in order; seeds stay per emitter;
   - shadows: once a collision query takes its caller's result buffer and an extra part as a candidate (not inserted
     in the shared zone), the requested shadow updates run on workers: they read collision after all movement ran;
   - frame list: one recorder per worker holding today's file-scope state, one store each, joined in category order
     with indices rebased; the material and pose caches written in drawing get a per-step first-writer rule.
3. Actor movement stays on the game thread in order: an actor's bind reads parts moved by actors before it in the
   same step. That is the game's rule, not a console limit.
4. Loads: what a step will read is read ahead by the loader's thread; the game thread copies only what is in memory
   (N-1083, 2.1 rule 3).
5. The owner's P-191 (c), every actor running every step, is decided after F7: its cost lands on these workers.

**Landings,** each behind a switch (workers on or off, and a worker count), the old path beside it:

1. The pool and the fork-and-join call; N-3013 and N-3014.
2. Visibility's judge.
3. Collision queries with caller buffers; shadows on workers.
4. Animation without callbacks.
5. Particles without callbacks.
6. The frame list's recorders.
7. Loads read ahead.

**Proof.** The replay's step hash identical with workers off, on, and at one, two and every core, over the tour and
the replay suite; the frame list's hash identical the same way; the thread sanitizer clean over the tour (the SDL
host on Linux); the `soak` check passes; the step's time on the tablet in the five heaviest stops before and after each
landing, and on the PC never slower (a step of 1.4 ms at most there: the pool's cost must not show).

### 2.18 5.H.2: what the game runs no longer follows the screen

The owner's decisions (file 08 5.H.2; the rows' "Owner 11-19" answers): every actor of the loaded scene runs in every
step (P-191 (c)); a rule that asks whether something is seen asks one reference view volume, the same on every screen
(P-243, P-256); no rule follows one player's screen or place (P-244, P-253). Built after F7 (2.17), behind a switch,
kept if file 00 4.3's measures hold on the three Windows renderers and the tablet at each offered rate; else (b), one
run volume the same on every screen. Its rows: P-191, P-243, P-253, P-256, and the actors' own skips the owner's
answer brings in (P-191's OceanBowl, P-253's view groups and SimpleEffectObj).

**Today** (from the rows, read at 241395b49). One state, "clipped", does three jobs: it stops an actor's step, it hides
its picture, and game rules read it as "unseen". `ClippingJudge` builds its volumes with `MR::getAspect()`
(`ClippingJudge.cpp:20-21`, `:33`, `:42`), so a wider screen runs more actors (N-1034: the tablet and the PC differ in
steps at 100 of 101 stops). Beside it, actors skip by themselves: the ocean bowl by distance (`OceanBowl.cpp:160-163`),
view groups by the player's cube with the port's 30-step hold (`ViewGroupCtrl.cpp:15`, `:60-99`).

**The reads of the clipped state**, all 42 (`git grep -n -E "isClipped|isInvalidClipping|isClippingInvalid" 697535a86
-- 'native/petari/src/*.cpp'`: 42), classed. 14 were read with their function (`CameraTargetObj` both, `MogucchiHill`,
`LiveActor`, `SensorHitChecker`, `OceanHomeMapCtrl`, `Butterfly` both, `BenefitItemObj`, `Candlestand`, `CrystalCage`,
`MorphItemObjNeo`, `PhantomTorch`, `WoodBox`); the other 28 from their line and its condition only, and each is read
whole in the landing that changes it.

| Class | Reads | Under (c) |
| --- | --- | --- |
| The machinery (16) | `ClippingActorHolder.cpp:152`, `:158`, `:171`; `ClippingActorInfo.cpp:26`, `:31`, `:40`; `ClippingGroupHolder.cpp:27`, `:30`, `:42`, `:52`, `:60`, `:70`; `LiveActor.cpp:57`; `LiveActorUtil.cpp:722`, `:726`; `BenefitItemObj.cpp:76` (a shadow's clip actor passing its end on) | the holder no longer calls `startClipped`; it judges each actor once a step against the reference volume and stores "seen" |
| Rules asking "is it seen" (4) | `CrystalCage.cpp:190` (removed unseen, broken when seen); `MogucchiHill.cpp:130` (a piece unseen is killed, a seen one crumbles); `Candlestand.cpp:94`, `PhantomTorch.cpp:75` (the fire's effect started on appearing when seen) | ask "seen" in the reference volume (P-243): the same answer on every screen |
| Work stopped while the actor is stopped (8) | `CameraTargetObj.cpp:30`, `:97`; `GravityFollower.cpp:16`; `SensorHitChecker.cpp:13`; `OceanHomeMapCtrl.cpp:52`; `FlashingCtrl.cpp:24`; `MirrorReflectionModel.cpp:55`; `PartsModel.cpp:93` | never true: the actor runs, so its gravity follows, its sensors hit and its timers count |
| Drawing (8) | `FlashingCtrl.cpp:47`; `MirrorActor.cpp:35`; `MirrorReflectionModel.cpp:34`; `PartsModel.cpp:67`; `FurCtrl.cpp:37`; `WoodBox.cpp:119`; `LumaContentFx.cpp:733`, `:908` | the picture's own culling by the real view (E3's draw records), apart from the step |
| Forced running (6) | `DemoCtrlBase.cpp:64`, `DemoExecutor.cpp:103`, `Butterfly.cpp:339`, `:349`, `MorphItemObjNeo.cpp:649`, `TalkDirector.cpp:300` (`invalidateClipping` and its readers) | no effect on running, which is always; the calls stay, as they mean "must run" with the switch off and for mods |

**The target.**

1. Two facts, not one state. *Runs*: every actor of the scene, every step (the switch off: today's judge). *Seen*: an
   answer the step computes once for each actor against the reference view volume: the game camera's position and
   direction, with a volume at least as wide and as tall as the widest and tallest picture offered (48:9 and the
   tallest phone shape), the game's far distances kept. *Drawn* is not the step's: each picture culls by its own view.
2. The reference volume's numbers are data in one place (the widest and tallest shapes offered), never read from the
   window, so a screen added later widens no rule unless the data says so.
3. The actors' own skips go with (c): the ocean bowl's distance skip, the view groups and their hold (the groups'
   cubes stay for what the game does on entering them, not for running), the far distances of always-on effects.
4. The cost lands on F7's workers: visibility's judge becomes the "seen" judge (2.17 landing 2); actors run in list
   order on the game thread (2.17 rule 3).
5. Multiplayer: "seen" is asked of one volume made from the game camera; a mod with two cameras asks each, and a
   rule may say "seen by any". No rule reads one player's screen.

**Landings,** behind the switch (run all or run by the judge), the old path beside it:

1. The "seen" judge on the reference volume, stored per actor; the 4 "is it seen" reads take it. (With the switch off
   the judge's numbers are today's, so this landing alone changes nothing.)
2. Running split from the judge: the holder stops calling `startClipped` when the switch is on; the 8 "stopped" reads
   are then never true.
3. Drawing culled per picture for the 8 drawing reads (on E3's records).
4. The actors' own skips (ocean bowl, view groups and their hold, the effects' far distances).
5. The measure (file 00 4.3, every renderer and the tablet, each offered rate; no step over budget): (c) kept, or (b)
   built with the same "seen" volume as its run volume. The replay's reference changes once; said on the board.

**Proof.** One test plays the same inputs at 4:3, 16:9, 21:9, 48:9 and the tablet's and a tall phone's shape and gets
the same step hash at every step; every tour stop green and the `soak/long` check passes; the 42 reads above each checked under the new
rule and named in the landing that changes it; the search for the clipped state run again at the last landing, its
hits only the machinery and the forced-running calls.
