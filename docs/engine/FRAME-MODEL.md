# The frame model

How a frame moves through Nebula's threads: who owns what, where data changes hands, and what may wait for what.
Part 1 is the code as it is today, with file and line (read on app/e at 2026-11-18; the lines it names are main's
except where a later commit moved them). Part 2 is the target and the
rules the later stages hold (07-BASE-FIRST, stage 2 item 1). Part 1 grows section by section.

## Part 1. Today

### 1.1 The threads a frame touches

| Thread | Started at | Owns | Hands over |
| --- | --- | --- | --- |
| Game ("Nebula Game") | `native/host/host_main.cpp:6538`, running `gModule.run()`; the host's main thread (:6527) only waits for messages, the game's end and the once-a-second report (:6628) | the game's state, the frame list it writes | a written list to the render thread (1.2) |
| Render | `runtime/src/render/render_core.cpp:259` (`RenderCore::render_thread_main`, :1550) | the device, every GPU object, the lists it walks | released lists back to the game (1.2); pictures to the display |
| Window owner | `runtime/src/render/window_owner.cpp:20` | the OS window and its message loop | size, focus and input to the game and render threads |
| Display clock, Windows | `runtime/src/render/vblank_clock_win32.cpp:175` | the vertical blank's time | the moment a picture is due (pacing) |
| Display clock, Android | `platform/sdl/display_clock.cpp:30` on (Choreographer's callbacks on a looper thread of their own) | the vsync grid (CLOCK_MONOTONIC) | slots predicted on the grid under `g_mutex`, so a late callback delays nothing |
| Display clock, Apple | `platform/apple/display_clock_apple.mm` (the display link) | the vsync times and interval on Mach time | the same grid as Android |
| Display clock, Linux | none: `wait_display_frame` returns false (`platform/sdl/display_clock.cpp:202-212`) | — | the step keeps the host clock (I-005) |
| Pipeline workers | `runtime/src/render/core/pipelines.cpp:104` | compiling pipeline states | finished pipelines to the render thread |
| Record workers | `runtime/src/render/core/draw_record.h:279` | building draw records | records to the render thread's walk |
| Texture pack workers | `runtime/src/render/texture_pack.cpp:176` (below normal priority, :225) | reading and decoding replacements | decoded pixels, picked up at `pump` once a frame |
| Movie | `runtime/src/render/movie_playback.cpp:75`, :299, :720 | decoding a movie's pictures | pictures to the render thread |
| Sound | `runtime/src/native_audio.cpp:37`, `runtime/src/audio_device_wasapi.cpp:103` | the output device's buffer | none to the frame; it reads the game's mixed sound |
| Save and log writers | `runtime/src/achievements.cpp:215`, `runtime/src/stats.cpp:160`, `runtime/src/ui/settings_save_model.cpp:36` | files | none to the frame |
| Host sampler | `native/host/host_main.cpp:698` | the process's own figures | figures to the logs |

The remaining threads of `runtime/src` (network, mods' services, controllers, updates, Discord) do not take part in a
frame; they are listed in 1.6 with what each may touch.

### 1.2 The frame list: written, handed over, released

1. The game writes a `NebulaFrameList` and submits it: `native/host/host_main.cpp:2778` (`svc_submit_frame`) stamps
   the game frame, then `galaxy::render::attach_frame_list` (`runtime/src/render/render_api.cpp:63`) stores it as the
   producer's list, and `submit_frame` asks for a picture.
2. On the game thread, `RenderCore::render_frame` (`runtime/src/render/render_core.cpp:558`) takes the list into a
   `FrameChunk` (:579-601) with the registries' and the texture store's queued work (:593-594), taken under the device
   gate's hold (:590).
3. The game thread waits there for room in the render queue (:604-635): `queue_place` decides from the queue's depth
   (`render_queue_depth`, a stalled render thread gets `kStalledRenderQueueDepth`) and the render thread's busy time;
   the wait is `queue_space_cv_` (:631, :633). The time it waits is counted as the game's wait (`GameWaitTimer`, :618).
   Then the chunk joins `frame_queue_` (:645) and `queue_cv_` wakes the render thread (:647).
4. The render thread takes the front chunk (:1911-1912), or drops a chunk made for a lost device's epoch and releases
   its list at once (:1903-1907).
5. A list is released when the render thread no longer needs it: it keeps the newest list it walked and the one before
   (the in-between pictures read both), and `release_lists_before` (:2250) releases every older one with a release
   fence first (:2253), so every read of the list comes before the game writes it again. The game writes a list again
   only once it sees the release (`native/host/host_main.cpp:2061`).
6. Waits on the whole render thread: `RenderCore::wait_for_render_idle` (:696) holds the game until the queue is empty
   and no chunk is in flight. It is called at `native/host/host_main.cpp:2084` and :3236 (named in 1.5 with why).

So today the game and the render thread share two things per frame: the list (owned by the game until submitted, by
the render thread until released) and the chunk's queued registry and texture work (taken under the device gate).

### 1.3 The render thread's loop

1. `RenderCore::render_thread_main` (`runtime/src/render/render_core.cpp:1550`) names the thread, raises it above the
   normal priority on Windows (:1555), opts the process out of power throttling and holds the 1 ms timer (:1561-1568),
   then runs `render_loop` (:1760) until it ends; a lost device runs `restart_device` and the loop again (:1571-1576).
2. With nothing queued it sleeps on `queue_cv_` for up to 100 ms hidden or 250 ms shown (:1863), so a service waiting for
   a new card can reach it (DeviceGate).
3. A display slot that is due goes before queued chunks (:1867-1878): presents keep their cadence, and a saturated GPU
   cannot render every endpoint unseen.
4. It takes the front chunk (:1911); a render chunk followed by a present-only one takes that present with it
   (:1914-1922). In-flight counts (`render_chunks_in_flight_`, `present_chunks_in_flight_`, :1926, :1929) are what
   `wait_for_render_idle` (1.2 item 6) waits on; each taken chunk frees queue room (`queue_space_cv_`, :1908, :1941).
5. A present-only chunk presents the cached picture again, or hands its moment to the in-between sequence (:1942-1960);
   with no slot clock (an unlimited rate) it presents again once the last present is done (:1974-1981).
6. A render chunk is walked (`walk_frame`, :2588), and the lists older than the one before are released (1.2 item 5).
7. On a lost device, `drop_device_state` (:1605) releases every queued and walked list (:1608-1624) and clears the walk,
   the in-between sequence and the screen probes (:1629-1656), so no list stays held by a dead device.

### 1.4 What the render thread waits for on the GPU

Two frames are in flight (`kFramesInFlight`, `runtime/include/galaxy/render/renderer_common.h:49`).

| Renderer | Per frame | Inside a frame, the whole GPU |
| --- | --- | --- |
| Direct3D 12 | the slot about to be used again (`renderer_d3d12.cpp:3774-3782`); the oldest history ring segment when it is reused (:3952-3956); the frame latency waitable, 100 ms bound (:4364) | `wait_for_gpu` (:916) in `reset_sampler_frame` (:1410), `prepare_temporal_upscaler` (:5034, :5067), `record_temporal_upscale` (:5229), and a capture's readback (:6904, :6907) |
| Vulkan | the slot (`vk/renderer_vk.cpp:2013`); the history ring segment (:2111) | `vkDeviceWaitIdle` when a size no longer matches: the temporal upscaler's preparation and record (`vk/renderer_vk_frame.cpp:922`, :953, :1428) and frame generation's HUD-less targets (:1455, :1495, :1548) |
| Direct3D 11 | the slot (`d3d11/renderer_d3d11.cpp:536`); the frame latency waitable, 100 ms bound (:676) | none; a window size change waits for the GPU idle (`d3d11/renderer_d3d11_scene.cpp:246`) |
| Metal | the slot (`mtl/renderer_mtl.mm:597`, `wait_for_fence_value`); a drawable (`nextDrawable`, :1213, in `present`) | `wait_for_gpu_idle` (:499) when the temporal upscaler's size no longer matches (`mtl/renderer_mtl_draw.mm:736`, :757); the sampler array restarts when nearly full, its slots written once the GPU is past the frames that read them (:245-249; P-066 names its waits) |

Outside a frame, `wait_for_gpu` drains the GPU at a window size change (:1955), a swap chain's re-creation or resize
(:3371, :3419), an HDR switch (:4150), shutdown (:3114, :3141) and the pictures at exit (:6769). Those are allowed by the
rules of part 2; the waits inside a frame are not, and are in the list (1.9). Metal drains at a window size change (`mtl/renderer_mtl_scene.mm:353`), the pictures at exit
(`mtl/renderer_mtl_draw.mm:618`) and shutdown (`mtl/renderer_mtl.mm:366`). Vulkan drains the same way at window size changes,
a swap chain's re-creation and shutdown (`vk/renderer_vk_frame.cpp:425`, `vk/renderer_vk.cpp:1062`, :3031, :1723).

The game thread waits on the render thread at three places: queue room (1.2 item 3), the render thread idle (1.2 item
6), and a screen probe's answers for frame F - 2 (`render_core.cpp:2032`, bounded by `kProbeWaitNs`), which waits only
for that list's GPU work, as the list was walked when F - 1 superseded it (:2028-2031).

### 1.5 What the game thread waits for

The game thread runs the game module's loop; each wait below is a call it makes into the host (`native/host/host_main.cpp`).

| Wait | Where | Bound | Kind |
| --- | --- | --- | --- |
| The next step's moment | `svc_frame_wait` (:1140): the display's vertical blank on the SDL hosts (:1166), else a precise wait for the host clock's due time (:1182, `PreciseWait`), recorded as `StepWaitBegin`/`StepWaitEnd` | the step period (`kGameStepNs`) | pacing, every frame |
| Suspended (minimised, or the system's suspend) | `wait_while_suspended` (:1034), from the step wait (:1188) | until resumed | by design |
| Room in the render queue | `render_frame`'s wait (`render_core.cpp:604-635`, 1.2 item 3) | none of its own | every frame the render thread is behind |
| A screen probe's answers | `svc_probe_results` (:2792) to `screen_probe_results` (`render_core.cpp:2032`) | `kProbeWaitNs` | only frames with probes |
| The render thread idle | the output's shape latched (`svc_frame_begun`, :3232-3240, `wait_for_render_idle`) | none | a window or aspect change |
| The last save request | `svc_save_wait` (:3499) to `StatusBoard::wait` (`runtime/src/save_files.cpp:179`) | none | the game's own `waitRequest` |
| A file's bytes | `svc_asset_read` (:3152): a copy from the store's mapped view (`gPak.store.read`), so a page not in memory is read from the drive on this thread | the drive | loads |

The start check's wait for each list's release (:2070-2078) and its final `wait_for_render_idle` (:2084) are the
`--start-check` mode's own, not the game's.

Two of these hold the game for the whole render queue: the aspect latch (all queued frames, then the picture is fitted)
and a render queue already at its depth. The asset read is the one wait on the drive inside a step; every other file the
game touches goes through the save worker.

### 1.6 The other threads, and what of the frame each touches

| Thread | Touches | Hand-over |
| --- | --- | --- |
| Window owner (`window_owner.cpp:20`) | the window's size, focus, input | the size reaches the aspect latch (1.5) and the swap chain's resize (1.4, a whole-GPU wait) |
| Display clock (`vblank_clock_win32.cpp:175`, started once by `vblank_grid`) | the vertical blank's time and period | under `g_mutex`, read by the step wait and the present slots |
| Pipeline workers (`core/pipelines.cpp:104`, lowest priority) | compiling pipeline states | each published to an atomic cell (`cell` :145, `publish` :164); a draw whose pipeline is not there yet compiles it on the render thread (`create_now` :253; a quick link where pipelines link from parts, :271) |
| Record workers (`core/draw_record.h`, `RecordPool`) | encoding a pass's chunks | fork and join inside the walk: the render thread takes a share and returns when all are done |
| Texture pack workers (`texture_pack.cpp:176`) | reading and decoding replacements | decoded pixels picked up at `pump` once a frame; the upload is the render thread's |
| Movie (`movie_playback.cpp:75`, :299, :720; :1016 prepares the decoder) | decoding a movie | pictures through the stager to the render thread |
| Streamline start (`renderer_d3d12.cpp:2314`) | loading the vendor's library | joined at device creation, before any frame |
| Picture writer (`renderer_common.cpp:427`) | writing a captured picture to disk | the pixels are copied out first; counted so the end waits for it |
| HDR watcher (`os_hdr.cpp:393`) | the system's HDR state | read by the swap chain's HDR switch (1.4, a whole-GPU wait) |
| Telemetry publisher (`frame_telemetry.cpp:104`) | the frame counts | samples them into shared memory for outside readers |
| Texture categories index (`texture_categories.cpp:347`) | reading the store's index | `index_ready` (acquire) before any lookup |
| Sound, save writers, host sampler, input (`host_main.cpp:3914`) | none of the frame | — |

Not part of a frame: controllers (`controller_hid.cpp`, `wiimote_hid.cpp`, `switch2_ble_win.cpp`), mods' services
(`luma_net.cpp`, `luma_fs.cpp`, `luma_mods.cpp`, `luma_assets.cpp`, `luma_capture.cpp`, `luma_input.cpp`), the settings
screen's workers (`ui/native_settings.cpp`), updates (`update/update.cpp`), vendor downloads, Discord.

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
`vk/post_process_vk.cpp`, `mtl/post_process_mtl.mm`, `temporal_upscaler.cpp`, `vk/temporal_upscaler_vk.cpp`,
`xess_upscaler.cpp`, `mtl/metalfx_mtl.mm`, `mtl/sgsr2_mtl.mm`, `fsr_frame_generation.cpp`,
`fsr_frame_generation_core.inl`, `vk/fsr_frame_generation_vk.cpp`.

- Direct3D 12: the post-process programs are made at the renderer's start (`renderer_d3d12.cpp:2713`), where a
  self-test of the anti-aliasing waits for the GPU once (`post_process.cpp:296`, :794); allowed, it is before any frame.
  The temporal upscaler's preparation and record drain the whole GPU (1.4; P-028, written).
- Vulkan: the bloom's pipelines are built in `begin_frame` the first time it runs (`vk/renderer_vk.cpp:1998`,
  `vk/game_bloom_vk.cpp:52`) and the post-process's in `sync_post_process` the first frame that needs them
  (`vk/renderer_vk.cpp:2871`, `vk/post_process_vk.cpp:441`): compiles inside a frame. A swap chain made again builds
  the present pipelines again (`vk/post_process_vk.cpp:152-170`, from `create_swapchain`), outside a frame's walk.
- Metal: the post-process, bloom, ambient occlusion and MetalFX depth copy compile their programs from MSL source when
  first made (`mtl/post_process_mtl.mm:130`, `mtl/game_bloom_mtl.mm:56`, `mtl/ambient_occlusion_mtl.mm:59`,
  `mtl/metalfx_mtl.mm:244`), as the game's own pipelines do (`mtl/pipelines_mtl.mm:283`).
- The upscalers' and frame generation's bodies (XeSS, FSR, the Vulkan temporal upscaler, SGSR 2) wait for no fence and
  read no file in a frame.

### 1.9 What breaks the frame's rules today, or keeps the console's shape

The rules (part 2): the render thread never compiles, never reads a file and never waits for the whole GPU inside a
frame; the game thread waits for the render thread only at the points this file names. Each part with its row; a part
with no row has a new one.

| Part | Where | Row |
| --- | --- | --- |
| A draw whose pipeline is not ready compiles it on the render thread | `core/pipelines.cpp:253` (`create_now`) | P-010, E3 step 1 (a form known at load) |
| Utility pipelines compiled on the render thread at first use, outside the cache | `core/pipelines.cpp` | P-052 (written, engine/b) |
| A present waits for every pipeline creation in flight (Direct3D 12) | `core/present_gate.h` | N-1067, E3 step 1 |
| The sampler heap, full, drains the whole GPU | `renderer_d3d12.cpp:1410` (`reset_sampler_frame`) | P-048, E3 step 2 |
| Size changes drain the whole GPU inside a frame | `renderer_d3d12.cpp:5034`, :5067, :5229 (the temporal upscaler), :1955; Vulkan's at size changes inside a frame (1.4) | P-028 (written for Direct3D 12 on engine/b; Vulkan's side open) |
| The game thread waits for frame F - 2's probe answers | `render_core.cpp:2032` | N-1068, E4 |
| The game thread waits for the whole render queue at an aspect change | `host_main.cpp:3236` | N-1081 |
| A capture's readback waits for the GPU inside a frame | `renderer_d3d12.cpp:6904`, :6907 | N-1082 |
| The game thread copies loads from the mapped store: a page not in memory is a drive read inside a step | `host_main.cpp:3152` (`svc_asset_read`) | N-1083 |
| Matrix rows made in eye space for one camera on the CPU, each walk and again for each in-between picture; skin envelopes on the CPU | `draw_walker.cpp:225-316` | E3 step 5 |
| A block in calc mode 1 comes with the view inside its joints (the console's billboards and camera-bound models) | `draw_walker.cpp:309-313` | E3 step 5 (resolved per view on the GPU) |
| Vertices the game builds on the CPU each step, copied each walk and moved at t on the CPU | `draw_walker.cpp:399` on | E3 step 4 |
| Culling against the view's planes on the CPU | `draw_walker.cpp:482` | E3 step 5 |
| A target pass (the console's copy of the frame into a texture) breaks the frame's pass: store and load on tiled GPUs | `draw_walker.cpp:1461` | E4 (render-to-texture passes first) |
| A copy before the list writes the frame reads the picture shown last, as the console's frame buffer held it | `draw_walker.cpp:1583` | E4 (the last picture asked for by name) |
| The scene's camera found at its opaque end, as the game loads its projection after the scene begins | `draw_walker.cpp:1356` | E4 (`GameScene::draw3D` states its passes) |
| A target's texture table rebuilt each frame; each draw names its textures again | `draw_walker.cpp:878`, :920 | E3 step 2 (each form's texture list) |
| Vulkan's bloom and post-process pipelines built on the render thread in the first frame that needs them | `vk/renderer_vk.cpp:1998`, :2871 | P-052 (its Vulkan half: open) |
| Metal's own programs (post-process, bloom, ambient occlusion, MetalFX depth copy) compiled from source at first use | `mtl/*_mtl.mm` (1.9) | P-065 |

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

The engine's areas this file does not walk, by what the ledger and the audit's master report (sections 6 and 10)
hold of the console's shape there:

| Area | The console's shape known | Read |
| --- | --- | --- |
| The game's arenas | the console's heaps are gone (NebulaArena); objects and deletes search every arena linearly (P-013), new and delete belong to one thread (P-012, fixed) | read whole for P-012 and P-013 |
| Sound | none known; the mixer has rows (P-037, P-071, P-072) | the mixer read; the game's own sound code not read (master report 10) |
| Input | sampled once a game step for the star pointer and the menu (P-078, P-163; F6); the Nunchuk's centre (P-074) | read (master report 6) |
| Files and archives | the step copies its loads from the mapped store on the game thread (N-1083) | the store's read path read for this file; the archive code not read whole |
| Saves | none known; the faults that lose data are section 4.6's rows | read (master report 6) |
| Text | none known; the game's messages stay in the disc's format and are converted where the host shows them (`message_utf8`, `host_main.cpp:2159`) | not read whole |
| Layouts and fonts | the owner's order of 11-04 names both for rewriting; no open row in this ledger names them | not read whole |
| The game's actor classes | E14's read is paused; its rows (N-3xxx) are in its branch | not read whole (master report 10) |

## Part 2. The target

### 2.1 The rules

1. The render thread never compiles a program or pipeline, never reads a file and never waits for the whole GPU
   inside a frame. Every pipeline a frame can need is known when its content loads (a material's form, a utility
   pass) and made then, on the workers; a size change makes its targets beside the old ones and retires the old ones
   by the frame fence, as any other resource.
2. The game thread waits for the render thread at one point only: room in the render queue, bounded by the queue's
   depth. A picture's shape, a screen probe and a capture are answered later, by the frame they belong to, never by
   holding the step.
3. The game thread reads no file inside a step: what a step loads was read ahead by the loader's thread.
4. Each hand-over has one owner at a time and a release and acquire pair (1.2's list, the pipeline cells, the decoded
   textures); a debug build asserts the thread at each hand-over (`render_thread_only()`, `game_thread_only()`).
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
| Render thread | game | the released list; probe answers and pictures for the frame that asked | at release |

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
| 1 | per material form: which row glows, the map rows, the glow value, the alpha cutoff and mode | searching console register codes at load; with it, every pipeline made at load (P-010, N-1067) |
| 2 | samplers in final form, each form's texture list | hashing a texture's address and decoding filter codes per draw; the per-frame table rebuilds (`draw_walker.cpp:878`, :920); the sampler heap's drain (P-048) |
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
| N-1068 | B | F5, E4 | the game thread waits for frame F-2's screen probe answers (RenderCore | planner: not placed; rule 2: a probe is answered by the frame it belongs to |
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
| P-040 | C | F7 | The step has no worker parallelism, and the shared state that blocks i |  |
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
| N-1081: the aspect latch's whole-queue wait (`host_main.cpp:3236`) | B | F5, E4 | the game holds for the render queue | rule 2 |
| N-1082: a capture's readback inside a frame (`renderer_d3d12.cpp:6904`) | B | F5, E4 | a capture answered by its frame later | rule 1 |
| N-1083: loads read from the mapped store on the game thread (`host_main.cpp:3152`) | B | F7 | read ahead by the loader | rule 3 |

Of the render path as read for this file (1.9), nothing falls outside P-022, E3, E4, E6b and the step's threading: the
three new rows fall in E4 and F7. The areas 1.10 lists as not read whole may hold more; E14's read, paused, is where
they would come from.

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
| A site's shape (step 4) | the game, once at its start (`SiteMeshData`, today one user) | once | the walk, as any registered mesh | never |
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
this cooker keeps loading. A model cooked at 12 is refused at load with its version, as today
(`model_write.rs:908`); no mod has shipped one, since LumaCore v1 ships with 13.

| Section | Holds | Written from | Read until |
| --- | --- | --- | --- |
| `JNTS`, `STEP`, `JMSH`, `DMTX`, `ENVL`, `MIXS`, `IBND`, `IMPT`, `MESH`, `TEXR`, `GEOM`, `NAME` | 12's arrays as they are, by name instead of by fixed offset | the first landing (step 1's) | kept |
| `MATL` | per material: name, `same_as`, mesh, joint, state word, its forms' and slots' ranges | step 1 | kept |
| `GXMT` | the console's material fields, apart: registers, constant and material colours, channels, alpha codes and references, coordinate generators by GX type and source, fog codes | step 1 (moved out of the material) | the last landing of step 3: no reader is left, and the cooker stops writing it |
| `FORM` | per form: programs, state, inputs; its kind (opaque, alpha-tested, blended, water), the alpha cutoff as a float and its mode, the glow row and colour, the base map's slot, which texture or vertex colour is baked light, each coordinate's source (one that reads the eye is marked), reads a copy of the screen, two-sided; its ranges in `TXLS`, `CBLK`, `CROW` | step 1 | kept |
| `SAMP` | samplers in final form: min, mag and mip filter, wrap, anisotropy, lod range and bias as floats | step 2 | kept |
| `TXLS` | per form, its texture list: texture and sampler by index | step 2 | kept |
| `CBLK` | per form, its constant rows in final floats (colours divided once, the alpha reference a float, fog per view, not per draw) | step 3 | kept |
| `CROW` | per form, the rows that change: the row, its source (coordinate matrix n, colour track n, register n, ambient from the item's light set) and how it is written (a copy, three matrix rows) | step 3 | kept |

Step 4's records are the game's own, made at load (2.7.1): no model section. Particle materials go through the effect
cook (`luma_content_fx.h`), whose material entry takes the same `FORM`, `TXLS`, `CBLK` and `CROW` fields; that cook's
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

- The switch is one table in the game module, a default per step and group (`Game/Render/RenderPath.hpp`). A landing
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
  without, and the gate checks that, so the second list changes no game state. The second list carries no probe pass:
  probes are answered from the first, the run's default.
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
