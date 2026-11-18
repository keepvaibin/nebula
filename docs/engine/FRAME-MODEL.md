# The frame model

How a frame moves through Nebula's threads: who owns what, where data changes hands, and what may wait for what.
Part 1 is the code as it is today, with file and line (branch docs/frame-model, on app/e). Part 2 is the target and the
rules the later stages hold (07-BASE-FIRST, stage 2 item 1). Part 1 grows section by section.

## Part 1. Today

### 1.1 The threads a frame touches

| Thread | Started at | Owns | Hands over |
| --- | --- | --- | --- |
| Game ("Nebula Game") | `native/host/host_main.cpp:6538`, running `gModule.run()`; the host's main thread (:6527) only waits for messages, the game's end and the once-a-second report (:6628) | the game's state, the frame list it writes | a written list to the render thread (1.2) |
| Render | `runtime/src/render/render_core.cpp:259` (`RenderCore::render_thread_main`, :1550) | the device, every GPU object, the lists it walks | released lists back to the game (1.2); pictures to the display |
| Window owner | `runtime/src/render/window_owner.cpp:20` | the OS window and its message loop | size, focus and input to the game and render threads |
| Display clock | `runtime/src/render/vblank_clock_win32.cpp:175` | the vertical blank's time | the moment a picture is due (pacing) |
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

Outside a frame, `wait_for_gpu` drains the GPU at a window size change (:1955), a swap chain's re-creation or resize
(:3371, :3419), an HDR switch (:4150), shutdown (:3114, :3141) and the pictures at exit (:6769). Those are allowed by the
rules of part 2; the waits inside a frame are not, and are in the list (1.9). Vulkan drains the same way at window size changes,
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
| The game thread waits for the whole render queue at an aspect change | `host_main.cpp:3236` | to be given |
| A capture's readback waits for the GPU inside a frame | `renderer_d3d12.cpp:6904`, :6907 | to be given |
| The game thread copies loads from the mapped store: a page not in memory is a drive read inside a step | `host_main.cpp:3152` (`svc_asset_read`) | to be given |
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

Every file stage 2 item 1 names has been read for this list. The parts marked "to be given" get new rows in the
ledger with the model's landing.

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

Model format 12 is designed once for these; no code for them now, and ray tracing stays off main.

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
- A later field does not make a format 13, and a mod's cooked model keeps loading: the format carries a table of
  sections by name and size, and a reader skips what it does not know.

### 2.4 Model format 12

What the importer writes, by E3's steps (file 01, E3):

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
| new: the aspect latch's whole-queue wait (`host_main.cpp:3236`) | B | F5, E4 | the game holds for the render queue | rule 2 |
| new: a capture's readback inside a frame (`renderer_d3d12.cpp:6904`) | B | F5, E4 | a capture answered by its frame later | rule 1 |
| new: loads read from the mapped store on the game thread (`host_main.cpp:3152`) | B | F7 | read ahead by the loader | rule 3 |

The list of 1.9 holds nothing that P-022, E3, E4, E6b and the step's threading do not cover: the three new rows
fall in E4 and F7. So these are the last rewrites the frame's rules ask for.
