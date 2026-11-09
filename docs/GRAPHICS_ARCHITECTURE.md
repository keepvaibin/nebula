# Graphics architecture

The game makes typed records; the renderer (`galaxy::render`, `runtime/src/render`, Direct3D 12 or Vulkan) draws them. One shader source, compiled at build to DXIL and SPIR-V.

## A frame

1. The main loop asks the host for its next frame (`frame_wait`).
2. Game code draws through `nebula_game_api.h`: `shape_draw`, `immediate`, `native_material`, `state`, `target_begin`,
   `target_end`, `target_capture`, `screen_probe`, `scene_marker`. Each call becomes one `shapes::Record`.
3. A record's matrices, vertices and materials go in the frame's pool (`shapes::RecordPool`): one allocation a frame.
4. `render_frame` hands records and pool to the render thread through a bounded queue.
5. The render thread runs `on_shape_record` over the list in order, submits, presents.

The list is the only order. Scene boundaries are records too.

## What the game sends

| Thing | How |
|---|---|
| Models | Cooked meshes: float arrays, corners, triangles (`cook/mesh.rs`). Vertices expand once and stay in the resident vertex buffer, in two streams: 44 bytes every vertex has, 92 more only for meshes with tangents or further coordinates |
| A model's matrices | One block a view (`shape_palette`): twelve floats a position matrix, then twelve a normal matrix. Vertices name a row; interpolation moves the block |
| Materials | A shader compiled at build (`NebulaShaderId`) with its constants, textures and state (`NebulaNativeMaterial`). J3D materials come from `tools/dev/gen_j3d_materials.py`, sites and layouts from `runtime/src/render/materials` |
| Draw state | `NebulaPassState` on every draw: blend, depth, cull, writes, position matrix, line and point sizes |
| Vertices the game makes | `immediate`, written into the frame's pool. Consecutive quads with nothing changing between them are one draw |
| Particles | An emitter's corners, in the world, with the camera as the draw's matrix; each knows where it was a game frame ago (`NebulaParticleCorner`). The vertex stage places them at the presented frame's time |
| Lines and points | Four corners each, made by the host (`widen_lines_points`), moved by the vertex stage. No geometry stage |
| Passes into textures | `target_begin` and `target_end`: the texture is the renderer's, at the scene's scale |
| Reading the frame | `target_capture` copies a rectangle to a texture (a grab pass). `screen_probe` reads named points a frame or two later (lens flare, star pointer, talk balloon) |

The rings are write-combined: code writes them and never reads them.

## Textures

Cooked at import into records (`nebula_cooked_texture.h`): BC1 for CMPR without transparent blocks, L8 and LA8 for intensity formats,
RGBA8 otherwise, palettes applied. Textures the game writes while running are linear (`NEBULA_TEXTURE_LINEAR_*`). A texture's key is
the address of its image; a texture leaves when its scene's heap goes (`shape_forget`).

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

## No fixed caps

A count of things (textures, tables, lights, layers, draws, uploads, descriptors, pads, monitors, files) never refuses or stalls at a
fixed number. A limit read from the device or the API is fine; each is named with the query that gives it. `helpers/runtime_caps.tsv`
(agents/native-progress) lists every constant of runtime/src and runtime/include that bounds a count, with its class: **B** budget
(a number someone chose: 23 rows, all fixed or growing except the SDL host's pad arrays and the model format's limits, which are
Q's), **D** design (58: page sizes, rings of frames, policy, algorithm constants, bounds on untrusted input and on a format's field) and
**H** hardware or API (10).

- **Upload rings** (vertices, indices, constants, matrix rows): a ring grows into a new buffer between frames when a frame took over half
  its budget, without waiting for the GPU (the old buffer is freed after the frames in flight); a request that fits neither the frame's
  segment nor the spill goes to a chunk of its own, kept until that frame slot begins again. The matrix ring, whose rows are indexed
  from its base address, only grows between frames.
- **Direct3D 12 descriptors**: the shader-visible heap's limit is read from the device (`ResourceBindingTier`: one million on tiers 1 and 2,
  more on tier 3: it names none, so the heaps double from their first sizes until the device refuses one; a refused
  `CreateDescriptorHeap` is logged once and the old heap stays). The texture region doubles to that limit; the persistent region
  doubles when full (a new heap and persistent shadow, indices kept, the replaced heap freed at the fence). A frame's segment holds the
  renderer's own tables only (a few hundred), so it is sized by design and reported (`frame_high_water`). The sampler heap holds 2048
  (the API's number) and starts again between frames.
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
- **Vulkan descriptors**: a new pool when one is full; the image array doubles to the device's update-after-bind limit.
- **Texture tables**: a table no walk has named for 600 frames releases what was built for it (its persistent descriptor table on a
  device without descriptor indexing, freed after the fence) and is rebuilt when a walk names it again. Ids are never reused (32 bits do
  not run out in a session), because the game keeps them in its materials; a retired table keeps only its small record.
- **Input**: the HID worker waits on four handles whatever the number of pads (each read and write arms a thread pool wait), so the
  Windows 64-handle limit is gone. A device key's slot is 32 bits (the keyboard and the phone's screen take the top two values); the
  HID worker's tables hold 256 pads (`kMaxHidPads`), a size of those tables and not of the key. The controller list is XInput's four
  and the HID pads; the list the CONTROLS page shows has no cap; the raw input device list is as long as the system says.
- **Test switch** `GALAXY_TEST_SMALL_HEAPS=1` (test builds) starts the rings at 1 MB, the Direct3D 12 heap's regions at 4 persistent and
  64 texture descriptors, and the first Vulkan pools at a handful of sets, so every growth above runs in the first seconds.
  `GALAXY_TEST_WIDE_TABLES=1` walks every texture list as at least 24 maps (the extra maps repeat its last), so runs past sixteen are
  written, read and, on the bound-table path, clamped on both backends; in a development build it also draws a probe once the game's
  3D scene runs (`wide_probe.hlsl`, `Game/Render/NebulaWideProbe.cpp`): a list of 24 one-texel maps, the shader samples map 23 and writes
  its red as the pixel's depth, a depth probe brings it back, and one line says `[wide-table] map 23 of 24: red read 240, expected 240:
  ok` (or `WRONG`) on either backend.
- **Video memory**: no size of it is assumed. Each frame boundary reads the adapter's usage and budget (DXGI `QueryVideoMemoryInfo`;
  Vulkan's device-local heap budget, `VK_EXT_memory_budget` when the device has it). Past the budget the texture store gives back textures
  no frame in flight names, least recently named first (at most 64 MB a frame, down to 95% of the budget); the id keeps its place and
  draws blank until the next list's first walk, which has it made again from the image the game keeps (staged on the game's thread in
  `take_ops`). The memory returns as the frames that may read it finish; on Direct3D 12 a placed heap with nothing in it (not the first)
  is released, so usage can fall. `GALAXY_TEST_VRAM_BUDGET_MB=N` sets the budget in test builds; `[vram]` is logged every 600 frames.
- **Out of sight**: a minimised window, or on Direct3D 12 one the OS reports covered (`DXGI_STATUS_OCCLUDED`, asked again every 100 ms
  with a test present), stops the game and its sound and the renderer's own presents (no repeated pictures, no generated frames), and the
  threads wait on events (the host's message wait is at most 100 ms). The device and swap chain stay, so showing the window again
  rebuilds nothing; the game's clock starts again from the return and owes no steps. A second start of the game finds the first's window
  by its data folder's key, lets it take the foreground and ends.
- **Power**: every limiter sleeps on a high-resolution waitable timer; none spins. A synced present waits for the swap chain's frame
  latency object (asleep, 100 ms at most). On battery, background threads (pipeline workers, shape builder) ask the OS for efficiency; the
  frame threads never do.

## Failure

A lost device (driver reset) restarts the game from its save, twice in a row at most; the third is report NEB-G01
([ERROR_CODES.md](ERROR_CODES.md)). Vulkan drivers' refused features are learned per driver build (`vk_compat.txt`, [VULKAN.md](VULKAN.md)).

## Testing

`tools/verify`: identical pictures between builds in deterministic mode (`GALAXY_TEST_DETERMINISTIC=1`), a window against the same build's
control in real time, all stops in the tour, `[loss]` counters for draws without a material or pipeline.
