# Graphics architecture

The game makes typed records; the renderer (`galaxy::render`, `runtime/src/render`, Direct3D 12 or Vulkan) draws them. One shader source, compiled at build to DXIL and SPIR-V.

## A frame

1. The main loop asks the host for its next frame (`frame_wait`).
2. Game code builds a frame list (`nebula_frame_list.h`, `Game/Render/FrameList.cpp`): passes in draw order, each naming a range
   of items, and the blocks, views, cameras, light sets and constants the items read. Models, meshes, materials, texture tables and
   pipelines are registered with the host once (`model_register`, `mesh_register`, `material_register`, `texture_table`,
   `pipeline_register`).
3. The vertices the game makes (the ring), the glyph and particle instances and the constants sit in memory the game owns until the
   renderer is done with the list.
4. `submit_frame` hands the list to the renderer by pointer; the renderer sets `released` when it no longer reads it and keeps the
   last two lists to interpolate between.
5. The render thread draws the passes in order, submits, presents.

The list is the only order. Scene boundaries are passes too (`NEBULA_PASS_SCENE_BEGIN`, `_OPAQUE_END`, `_SCENE_END`).

## What the game sends

| Thing | How |
|---|---|
| Models | Cooked meshes: float arrays, corners, triangles (`cook/mesh.rs`). Vertices expand once and stay in the resident vertex buffer, in two streams: 44 bytes every vertex has, 92 more only for meshes with tangents or further coordinates |
| A model's matrices | One block a view (`NebulaFrameBlock`): twelve floats a position matrix, then twelve a normal matrix. Vertices name a row; interpolation moves the block |
| Materials | A shader compiled at build (`NebulaShaderId`) with its constants, textures and state (a pipeline from `pipeline_register`). The programs are built by `tools/dev/build_programs.py` into `runtime/src/render/programs`; sites and layouts are in `runtime/src/render/materials` |
| Draw state | A state word with the pipeline (`NEBULA_STATE_*`: blend, depth, cull, writes), the draw's block, and its line and point sizes |
| Vertices the game makes | The ring (`ring_vertices`), written into the list. Consecutive quads with nothing changing between them are one draw |
| Particles | An emitter's corners, in the world, with the camera as the draw's matrix; each knows where it was a game frame ago (`NebulaParticleInstance`). The vertex stage places them at the presented frame's time |
| Lines and points | Four corners each, made by the host (`NEBULA_ITEM_WIDE`), moved by the vertex stage. No geometry stage |
| Passes into textures | Draw passes into a target (`NEBULA_LAYER_TARGET`): the texture is the renderer's, at the scene's scale |
| Reading the frame | `NEBULA_PASS_COPY_FRAME` makes the frame so far a texture (a grab pass). A probe pass (`NEBULA_PASS_PROBE`, `probe_results`) reads named points a frame or two later (lens flare, star pointer, talk balloon) |

The rings are write-combined: code writes them and never reads them.

## Textures

Cooked at import into records (`nebula_cooked_texture.h`): BC1 for CMPR without transparent blocks, L8 and LA8 for intensity formats,
RGBA8 otherwise, palettes applied. Textures the game writes while running are linear (`NEBULA_TEXTURE_LINEAR_*`). A texture's key is
the address of its image; a texture leaves when its scene's heap goes (`texture_free`).

## Resolution

The scene renders at the window's exact size. Viewports and scissors are fractions of the pass's target (`NEBULA_FRAME_UNITS` across);
targets have their own pixels (`FrameMap`), at a share of the frame or an exact size. 2D has one space: the screen's width by 456 lines.

## Precision

Colour is float through the shader and blends at 10 bits a channel, in gamma as the art was made; texture targets are 8. The last write to
an 8-bit window adds triangular noise of one level to pixels between two levels, so a gradient has no steps (`FinalWrite`, `post_process.hlsl`).
Depth is reversed. Rows of differences this made are in the project's look-change list.

## Pipelines

The cook's pipeline list (Pipelines.bin) is built at start by one worker per logical core, at the lowest priority, so the game and
render threads keep their cores (`[pipelines] N prepared by K workers at T ms`). No frame waits for it (the title's preparation screen is
the game's own gate, drawn like any frame). A draw whose pipeline is not made yet makes that one itself, on the render thread. The
driver's cache file is written whole (temporary file, then a rename), once when the first queue drains and once at exit, never on the
render thread.

On Vulkan with `VK_EXT_graphics_pipeline_library` and a driver that links fast, a pipeline is four parts linked: the vertex input, the
pre-raster shaders (vertex program, raster state), the fragment shader (pixel program, depth and stencil) and the fragment output
(blend, targets). Each part is made once per distinct state of its own and shared by every pipeline that has it. A draw whose pipeline
is not ready links one from parts at once (no optimisation, little wait) and a worker links the optimised one and replaces it for the
draws after; the workers' own builds are optimised links. A part or link the driver refuses sends that pipeline down the whole-pipeline
path. `[pipelines] graphics pipeline libraries` is logged at the first use; without the extension nothing changes. Direct3D 12 has no
parts to link: its `ID3D12PipelineLibrary` (the driver cache loaded at start) makes a pipeline a load, not a compile, for every one an
earlier run made, and a draw that misses still waits for its whole pipeline. Test builds also keep the order pipelines were first drawn
in (`pipeline_use.tsv` in the shader cache folder, written at shutdown) and warm in that order; a release warms in the cooked order.

## No fixed caps

A count of things (textures, tables, lights, layers, draws, uploads, descriptors, pads, monitors, files) never refuses or stalls at a
fixed number. A limit read from the device or the API is fine; each is named with the query that gives it. A table
lists every constant of runtime/src and runtime/include that bounds a count, with its class: **B** budget
(a number someone chose: 23 rows, all fixed or growing except the SDL host's pad arrays and the model format's limits, which are
fixed by the hardware and the format), **D** design (58: page sizes, rings of frames, policy, algorithm constants, bounds on untrusted input and on a format's field) and
**H** hardware or API (10).

- **Upload rings** (vertices, indices, constants, matrix rows): a ring grows into a new buffer between frames when a frame took over half
  its budget, without waiting for the GPU (the old buffer is freed after the frames in flight); a request that fits neither the frame's
  segment nor the spill goes to a chunk of its own, kept until that frame slot begins again. The matrix ring, whose rows are indexed
  from its base address, only grows between frames.
- **Direct3D 12 descriptors**: the shader-visible heap's limit is read from the device (`ResourceBindingTier`: one million on tiers 1 and 2,
  more on tier 3: it names none, so the heaps double from their first sizes until the device refuses one; a refused
  `CreateDescriptorHeap` is logged once and the old heap stays). The texture region doubles to that limit; the persistent region
  doubles when full (a new heap and persistent shadow, indices kept, the replaced heap freed at the fence). A frame's segment holds the
  renderer's own tables only (a few hundred): it starts at 32 768 and doubles too if a frame fills it (`grow_frames`), reported by
  `frame_high_water`. The sampler heap holds 2048 (the API's number) and starts again between frames. Every ring, table and pool with
  its first size and how it grows is listed in `helpers/present_audit.md`.
- **Textures per draw**: with descriptor indexing (both backends) a draw's textures and samplers are one run of any length in the
  bindless heaps (`write_texture_list`; the table records its length; the list's buffer grows). A device without it (Vulkan, bound
  tables, `NEBULA_BOUND_TABLES`) binds an array whose length is the shader's compile-time size (16, `vk::kBoundTableSlots`); a list
  longer than that draws with its first maps and logs once per list.
- **Lights**: a draw's light set holds as many lights as the game lit (`NebulaFrameLightSet {first, count, follow_first}` over the list's
  `lights` and `light_follow`). In the matrix rows a set is one header row (the count) and four rows a light; a light number past the
  count lights nothing (`light_term`), and a draw without a set reads an empty one. The cooked light masks (8 bits) are the model
  format's. The game-lights store keeps every light lit at once.
- **Material constants**: a draw's b1 and b2 constants are read through a range of 4096 rows (what a Direct3D 12 constant buffer view
  holds), on Vulkan cut to `maxUniformBufferRange`. Instances in a batch and draws in a list have no cap (32-bit counts).
- **Pipelines**: the table of built pipelines is a page table over the 32-bit id, made as ids appear; nothing is evicted by count (the
  driver's cache file keeps them between runs); the only number is the driver's two refusals of a program pair.
- **Worker threads** follow the logical processors of every processor group (`logical_cores()`, `galaxy/cpu_count.h`): pipeline builds,
  mod shader builds, picture conversion, texture pack decoding. Queues grow.
- **Vulkan descriptors**: a new pool, twice the size of the last, when one is full; the image array doubles to the device's
  update-after-bind limit.
- **Texture tables**: a table no walk has named for 600 frames releases what was built for it (its persistent descriptor table on a
  device without descriptor indexing, freed after the fence) and is rebuilt when a walk names it again. Ids are never reused (32 bits do
  not run out in a session), because the game keeps them in its materials; a retired table keeps only its small record.
- **Input**: the HID worker waits on four handles whatever the number of pads (each read and write arms a thread pool wait), so the
  Windows 64-handle limit is gone. A device key's slot is 32 bits (the keyboard and the phone's screen take the top two values); the
  HID worker's tables hold 256 pads (`kMaxHidPads`), a size of those tables and not of the key. The controller list is XInput's four
  and the HID pads; the list the CONTROLS page shows has no cap; the raw input device list is as long as the system says.
- **First sizes**: the rings start at 1 MB, the Direct3D 12 heap's regions at 1024 per frame, 4 persistent and 64 texture
  descriptors, the Vulkan texture array at 64 and the first Vulkan pools at a handful of sets; each grows to what the content takes,
  so every growth above runs in the first seconds of every session.
- **Video memory**: no size of it is assumed. The renderer reads the adapter's usage and budget at most ten times a second (a kernel
  call; DXGI's budget-change event is taken at once) and each frame boundary acts on the latest reading: DXGI `QueryVideoMemoryInfo`
  (usage at most every 100 ms; the budget at once when the OS signals `RegisterVideoMemoryBudgetChangeNotificationEvent`, which is
  also logged), Vulkan `VK_EXT_memory_budget` of the largest device-local heap (read at most every 100 ms), else the allocator's
  estimate. Past the budget (or when walks want reduced textures whole and nothing is free) the texture store makes the textures no
  frame in flight names smaller: the least recently named first, the largest of equals first, its top levels left out, at least 4 texels
  on the short side kept, at most 64 MB freed a batch and a batch every 12 frames, down to 95% of the budget. The old texture draws
  until the smaller one, staged from the image the game keeps (on the game's thread in `take_ops`, from level `drop` of the record),
  replaces it at the next list's first walk: a texture is never blank and no draw is skipped. A reduced texture a walk names again is
  made whole the same way while the memory in use is under 80% of the budget (at most 32 MB a frame); with no room, what it lacks
  makes room from the textures nothing names. A texture of one level cannot be reduced. The memory returns as the frames that may read
  the old texture finish; on Direct3D 12 a placed heap with nothing in it (not the first) is released, so usage can fall.
  `GALAXY_TEST_VRAM_BUDGET_MB=N` sets the budget in test builds. Log: `[vram] budget N MB used M MB evicted K MB (R textures reduced
  of T)`, when the budget or K changes (looked at once a second) and once an hour.
- **Host memory**: the four upload rings (vertex, index, constant, matrix) grow when a frame takes over half its budget; after 600
  frames in a row in which no frame took more than a quarter of the budget they are made again at half the capacity, never under the
  capacity they first had (`UploadRing::wants_shrink`, `shrunk_capacity`; log `[gpu-rings] shrink`).
- **Out of sight**: a minimised window stops the game and its sound and the renderer's own presents (no repeated pictures, no generated
  frames), and the threads wait on events. The device and swap chain stay, so showing the window again rebuilds nothing; the game's
  clock starts again from the return and owes no steps. A window the OS reports covered (Direct3D 12: `DXGI_STATUS_OCCLUDED`, asked
  again every 100 ms with a test present) does not stop the game or its sound: the frames are drawn and submitted, nothing is presented,
  and no generated frames are made, until the window shows again. On Vulkan a surface with no area (the window minimised) is not
  retried or waited for until it has one. A second start of the game finds the first's window by its data folder's key, lets it take
  the foreground and ends.
- **Displays**: the process is per-monitor DPI aware (version 2, from the manifest). A move to another monitor, a change of the
  window's density, or a display mode change makes the renderer read the output again: the refresh rate (the pacing period), and on
  Direct3D 12 the colour space and peak luminance (logged when they change; the picture is SDR on every display, so nothing is
  rebuilt), on Vulkan whether the surface still offers the swap chain's format, colour space and present mode (the swap chain is made
  again only if not). A window takes the rectangle Windows suggests for its new density; the menus' size follows the window's width in
  density-independent units.
- **Present modes**: Direct3D 12 with VSync off presents with `ALLOW_TEARING` when the system offers it (flip model, windowed or
  borderless; `[present]` says which). Vulkan: FIFO with VSync; without it MAILBOX, else IMMEDIATE, else FIFO. Where the device takes a
  present mode per present (swapchain maintenance 1) a frame that reaches the present a quarter of a period late is presented as
  FIFO_RELAXED (shown at once, it may tear) instead of waiting for the next refresh. The latency rule is one frame: Direct3D 12 waits on
  the swap chain's latency object, Vulkan on `vkWaitForPresentKHR` for the previous present (`VK_KHR_present_wait`), both at most 100 ms;
  without present wait the frame fences bound the queue. `[vk] swap chain:` names the modes offered, the one chosen and which of these
  is used.
- **GPU timing**: the SYSTEM page of the settings has a GPU TIMING switch (off at every start). While it is on, both renderers write a
  timestamp at each pass boundary of every frame and read them a frame later, and the page shows each pass's running average in
  milliseconds; while it is off nothing is recorded or read (`GpuPassTime` in `render_api.h`).
- **Power**: every limiter sleeps on a high-resolution waitable timer; none spins. A synced present waits for the swap chain's frame
  latency object (asleep, 100 ms at most). On battery, background threads (pipeline workers, shape builder) ask the OS for efficiency; the
  frame threads never do.

## Beside the frame: uploads, sync, memory, recording

Four things the renderer does beside the draws (batch 12). Each has a test switch to fall back to the plain path, and each logs which path it took.

**Texture uploads on their own queue.** Where the adapter has memory of its own (Direct3D 12: not unified; Vulkan: a queue family that only
transfers, with timeline semaphores) a new texture is written by a copy queue / transfer queue, beside the frame, not on the frame's queue.
The texture is made in the common state (Vulkan: undefined layout) and stays there until a draw names it: `TextureStore::handle` and `touch`
tell the backend (`note_named`), and at the end of the frame (`submit_uploads`) the frame's queue waits for the copy queue's fence or the
transfer queue's timeline semaphore (Vulkan: the fragment and transfer stages only) and takes the texture (Direct3D 12: a transition from
common to shader resource in a small list before the frame's; Vulkan: the queue family ownership acquire, matching the release recorded
after the copy, in the frame's upload command buffer). A texture no draw names is never taken and costs the frame nothing. A texture
already taken is rewritten on the frame's queue as before (a pooled or movie texture), and one rewritten before it was taken goes through
the copy queue again (Direct3D 12) or is taken first (Vulkan). A frame slot waits for the copy queue to finish what the slot's last frame
sent it before its staging chunks (the 16 MB pool of `TextureStore`) and retired textures are reused; `Context::defer` also waits for the
transfer queue's value at the time of the call before it destroys anything. With enhanced barriers a texture in the common layout is copied
to and from on a copy queue without a barrier; with legacy states the copy list moves it common to copy destination and back. Not used with
direct host writes (unified memory with coherence, or `VK_EXT_host_image_copy`), which have no copy to overlap.

**Vulkan frame sync.** Where the device has timeline semaphores (core 1.2, or `VK_KHR_timeline_semaphore`) every submission signals the
next value of one timeline semaphore; `completed_value` reads it and `wait` waits on it. One object instead of a fence for each
submission (and no fence pool). Without them the fences stay.

**Rings in video memory.** The vertex and constant rings are written once, in order, and read by the GPU every frame. Where the device maps
its video memory for the CPU in bulk they live there, on a discrete adapter only (on one that shares system memory an upload heap is
already memory the GPU reads at full speed) (Direct3D 12: `D3D12_HEAP_TYPE_GPU_UPLOAD`, where `OPTIONS16` says it is supported and the
adapter is not UMA;
Vulkan: a device-local host-visible coherent memory type in a heap larger than the 256 MB window, beside video memory the CPU cannot map).
The index and matrix rings and everything else stay in host memory. `[ring]` lines and `[gpu-rings]` say `memory=video (CPU-mapped)` or
`host`.

**Recording a big pass on several threads.** A draw pass of at least 1500 items on a machine with at least 6 logical processors is
resolved on the render thread as ever (views, materials, pipelines, texture lists), but its encoder calls go to a recorder as plain
operations (`render/core/draw_record.h`), cut into chunks of at least 64 draws (never across a change of the draw targets). The chunks are
replayed through an encoder each on a pool of worker threads (`logical_cores() / 2`, 2 to 8, the render thread taking a share) into one
command list each (Direct3D 12: the frame's list is closed at the pass, the chunks' lists follow it in `ExecuteCommandLists`, and the frame
continues in a new list) or one secondary command buffer each (Vulkan: run in a render pass opened for secondary buffers, one per run of
chunks with the same draw targets). A chunk's lists start with the state the frame's list had at the pass: root signature, draw targets
and descriptor heaps (Direct3D 12); pipeline, sets and dynamic state are the encoder's own (both). Vulkan's descriptor-set cache
(`constants_set`) is locked while chunks record. A pass with pipeline statistics queries (Direct3D 12), or a recorded pass with fewer
than 96 draws, is drawn by the render thread alone (replayed from the recording on its own list, or at once).

## HDR output

On a display Windows has in HDR mode (`IDXGIOutput6::GetDesc1`, read at the first frame and again on a monitor move or `WM_DISPLAYCHANGE`
through a new factory; on Vulkan the surface must also list an HDR10 format, `VK_EXT_swapchain_colorspace`), the display page offers HDR
and PAPER WHITE (80 to 500 nits, default 200; `hdr=` and `paper_white=` in config.ini). With HDR on, the swap chain becomes
R10G10B10A2 in the ST 2084 / BT.2020 colour space (`ResizeBuffers` and `SetColorSpace1`, or a swap chain made with the HDR10 format) with the
display's figures as metadata (`SetHDRMetaData`, `vkSetHdrMetadataEXT`). Only the post chain's last write changes (`FinalWriteHdr` in
post_process.hlsl, selected by `PostConstants::origin[2] == 2`): gamma 2.2 decoded, BT.709 to BT.2020, scaled so 1.0 is the paper white,
BT.2390 roll-off of the brightest channel to the display's peak when that is under four times the paper white, PQ encoded, 10-bit
triangular dither. `hdr10_encode` (post_process_settings.cpp) is the CPU twin the tests check. With HDR off nothing differs: the same
passes write the same 8-bit values (`origin[2] == 1`). Screenshots and test pictures stay SDR: while the chain is HDR, a capture draws
the last write again for an 8-bit window into a scratch target and reads that. HDR stays off under frame generation (AMD's swap chain,
DLSS), on a pre-rotated Vulkan surface, and without the post chain.
Log lines: `[display] output HDR|SDR ...`, `[hdr] on peak N nits paper M`, `[hdr] off`.

## Power

**Waiting.** Every wait for a time is on a waitable timer made with `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (Linux and Android:
`clock_nanosleep` on `CLOCK_MONOTONIC`, absolute on the display's vsync grid on Android), never a spin: the host's frame clock
(`sleep_until_precise`, host_main.cpp), the present pacer's wait for its slot (`PresentPacer::sleep_before`, at most 2 ms, renderer_common.cpp)
and the render thread's wait for an interpolated frame's slot (`RenderCore::interp_sleep_us`, render_core.cpp; coarse by condition variable
until 1.5 ms before the slot, then the timer). A whole-tree search for `yield`, `Sleep(0)`, `_mm_pause`, `YieldProcessor`, `SwitchToThread`
and loops on an atomic found none outside tests. The waits that poll are the render thread's 1 ms park while it presents repeated
pictures (a frame rate above the game's, which the frame must follow), the movie decoder's 4 ms retry for a free upload buffer, and the exit
path's 10 ms look at saves still being written. Everything else waits on an event, a condition variable or the message queue.

**In the background.** A window that is minimised or covered presents nothing and makes no GPU work (`RenderCore::out_of_sight`; the host
holds the game when minimised). A window that is open but not in front (`host_window::is_background`, `WM_ACTIVATEAPP`) keeps the game running
but the renderer, told by `render::set_window_background`, makes no generated frames (`interp_stopping_`) and no repeated pictures
(`idle_present`, `overlay_idle`): the game's own frames at the game's rate, which is the display's rate or less. Coming back to the front
restarts generated frames from a new sequence, as after a minimise.

**Hints to the OS** (`runtime/include/galaxy/power_policy.h` decides, `apply_power_wish` in host_main.cpp tells, Windows host): the process
asks for efficiency (`PROCESS_POWER_THROTTLING_EXECUTION_SPEED` and `IGNORE_TIMER_RESOLUTION` set, EcoQoS) while the window is behind
another or minimised, or while the game has had no button, stick, spin or pointer movement for 60 s and no cutscene (movie sound within
2 s); it goes back to the opt-out of the start as soon as any of that ends. The device's audio thread opts out of it on its own
(`set_current_thread_efficiency(false)`, which the OS honours over the process). `SetThreadExecutionState(
ES_CONTINUOUS | ES_DISPLAY_REQUIRED)` holds the display on only while the window is in front and an input came in the last 3 minutes or a
cutscene plays; never in the background. Log lines `[power] efficiency requested (away or idle)`, `[power] full speed (playing)`,
`[power] display held on (playing)`, `[power] display left to the OS's timers`.

**Android.** The performance hint session (`perf_hint.cpp`, `APerformanceHint`, looked up at run time: API 33) holds the game and render
threads, reports each frame's CPU time, and takes the frame clock's period as its target (`perf_hint_set_target` from `svc_frame_wait`,
through `APerformanceHint_updateTargetWorkDuration` where present, else a new session): the short boot frame, then the game step. The
frame itself is paced by Choreographer's vsync grid (`display_clock.cpp`). Log lines `[perf] performance hint session ...`, `[perf] frame target N ms`.

## Which GPU, and what each graphics API runs

**The GPU.** On a machine with more than one, the GPU chosen in Nebula Settings (Display, Graphics card: shown only
then, taken at the next start) is used; with none chosen, the most capable:

- Direct3D 12 and Direct3D 11: Windows' order for performance (`IDXGIFactory6::EnumAdapterByGpuPreference` with
  `DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE`): an external GPU, then a discrete one, then the integrated one. It follows the
  per-app choice in Windows' Settings (System, Display, Graphics). Software adapters are never taken.
- Vulkan on Windows: the same GPU as Direct3D (each Vulkan device's LUID found in that order), so the two renderers
  never pick different GPUs on one machine.
- Vulkan elsewhere, and devices Windows does not list: discrete, then integrated, then virtual, then any other, then
  CPU (`VkPhysicalDeviceType`); among equals, the one with the most device-local memory.

What the chosen GPU supports decides every feature below: a feature of the other GPU is never offered.

**The features.** A setting is offered only where the renderer runs it on this GPU; a row it cannot run is not shown.

| Feature | Direct3D 12 | Vulkan (Windows, Linux, Android) | Direct3D 11 | Metal (Apple) |
|---|---|---|---|---|
| MSAA | yes | yes | yes (scene formats permitting) | see APPLE.md |
| FSR 1 (spatial upscaling) and RCAS | yes | yes | yes (16-bit where the GPU runs it) | see APPLE.md |
| Temporal upscalers (FSR 3, DLSS, XeSS) | where the GPU and its library allow | FSR 3; DLSS on NVIDIA RTX GPUs (Windows); XeSS where its library allows | no | see APPLE.md |
| Frame generation (FSR 3, DLSS) | where the GPU allows | FSR 3 where the GPU allows | no | no |
| Reflex | NVIDIA GPUs | no | no | no |
| Ray tracing | ray queries, acceleration structures and bindless resources required | the same, by Vulkan extensions | no | no |
| HDR10 output | while the display reports HDR | while the display reports HDR | no | see APPLE.md |
| Mod screen passes and native draws (`draw.post`, `draw.native`) | yes | yes | yes | see APPLE.md |

Direct3D 11 is the renderer for GPUs without Direct3D 12 (feature level 11 hardware): Nebula starts it by itself when
no adapter can run Direct3D 12, or with `--renderer d3d11` (COMMAND_LINE.md). It runs the whole game and every mod; it
has no ray tracing, temporal upscaling or frame generation, and offers none of them. Android builds are 64-bit only
(arm64-v8a; x86_64 for the emulator).

## Failure

A lost device (driver reset) restarts the game from its save, twice in a row at most; the third is report NEB-G01
([ERROR_CODES.md](ERROR_CODES.md)). Vulkan drivers' refused features are learned per driver build (`vk_compat.txt`, [VULKAN.md](VULKAN.md)).

## Testing

Test aids (test builds only): `GALAXY_PRESENT_SNAPSHOT_GAME_ONLY=1` leaves interpolated frames out of the snapshot
numbering, `tools/verify/interp_real.sh` uses it to compare a run with interpolation on against one with it off by the game's own
frames. A Vulkan device without a feature, or with another driver, is presented by the Khronos profiles layer
(`VK_LAYER_KHRONOS_profiles` with a gpuinfo.org profile); the game has no switch for it.

`tools/verify`: identical pictures between builds in deterministic mode (`GALAXY_TEST_DETERMINISTIC=1`), a window against the same build's
control in real time, all stops in the tour, `[loss]` counters for draws without a material or pipeline.
