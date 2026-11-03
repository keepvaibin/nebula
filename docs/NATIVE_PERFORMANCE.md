# Native port: performance

The target is the slowest machine that can run Nebula. Every decision that costs time is listed here, with its cost
and the evidence.
- No speed-affecting design is decided or announced before the code paths it touches have been read and the cost
  measured, or labelled H with a measurement plan.
- **M**: measured here. **D**: read from source, assembly or generated code. **H**: hypothesis, with the measurement
  that would settle it.

Measurements:
- Machine: Ryzen 9 9950X3D, 4.3 GHz, Windows 11, clang 22, `-O3` with the module's flags, pinned to one core.
- Older and low-power CPUs forward stores and divide more slowly, so the ratios below are lower bounds there (H).

## The start

Launch to the title screen (`boot:` lines, a start with no shader cache; stand-in: the 2-CU iGPU and two
cores with SMT of the machine above, `GALAXY_GPU_PREFERENCE=low GALAXY_TEST_CPU_AFFINITY=F`):

| | before | after | evidence |
|---|---|---|---|
| Stand-in | 2.86 s: Streamline 1.08 s loaded for a GPU that cannot run DLSS, the game's black frames before the title 1.08 s | 0.80 s | M |
| RTX 5090 | 3.37 s | 2.35 s (Streamline 0.78 s, beside the device's creation now, and 0.92 s handing it the device) | M |

- Streamline loads only for an NVIDIA RTX GPU, beside the device's creation.
- Before its first 3D scene the game shows nothing and its loads advance one step a frame while each frame takes well
  under a millisecond: until then frames come every 2 ms and one a display period is shown. 65 frames take 0.29 s
  instead of 1.08 s.
- Today, RTX 5090, shader cache warm: 689 ms to the title, 320 of them the device and Streamline (`boot:` lines).

## Loading a galaxy

The `scene:` lines of the log time each phase (real time at 60 fps, 8 stops; the same on the RTX 5090 and on the stand-in; **M**):

| phase | before | now |
|---|---|---|
| `[initialize Scene]` to `[scene ready]` | 834-1,534 ms | 50-67 ms |
| destroy and wave banks | about 50 ms | about 50 ms |
| frames over 25 ms in a load | 1 (37 ms) | 5-6 (29-144 ms) |

Each archive's resource holder used to wait for the main thread, which took one a frame. The thread that asks now makes it: all
game threads hold the world while they run. The picture is black at each long frame. Frames are byte-identical to before at the
12 stops compared (`matchframes`), each stop starting earlier.

## Fixed

| path | before | after | evidence |
|---|---|---|---|
| Timer resolution | `timeBeginPeriod(1)` at boot: raises the whole system's timer rate (power on laptops) | not used; high-resolution waitable timers | D |
| `sqrtf` and other math calls | math-errno on: compare, branch and library call on the error path | `-fno-math-errno`; the game never reads `errno` | D (assembly) |
| Square roots | an estimate and a Newton step | `sqrtss`, 13 cycles, exact | M |
| `NO_INLINE` (Petari's matching hint) | 499 functions forced out of line, `TUtil::sqrt` and `clamp` among them | the compiler decides | D |

The game thread waits 92 % of the time at four heavy stops (real time, 900 frames each, RTX 5090, `threadprof`), and no math function takes above 0.2 % of it. `nebula_math.h` has SSE paths for the 3x4 multiply and the column scale; more would save nothing measurable.

## Kept, with their cost

| choice | cost | why | what would change it |
|---|---|---|---|
| Saturating float-to-int casts (`-fno-strict-float-cast-overflow`) | no difference in scalar code; a vectorised `f32` to `u8` loop 4.7x slower (0.43 against 0.09 ns per value) | game logic may depend on out-of-range results | a profile showing a hot conversion loop: convert that loop explicitly |
| `-fno-strict-aliasing` | H: 0-5% (no type-based alias analysis) | the decompiled code puns types | fix the puns, then A/B the frame time |

## GPU

The stand-in's 2-CU iGPU at Battlerock Galaxy's first stop: a 1280x720 window, 60 fps, default settings. GPU time per
frame from the frame's timestamps; by pass with `GALAXY_GPU_PASSES=1` (present-stats' `gpu-pass-ms`, with the
pipeline statistics in `gpu-stats`), by pipeline with `GALAXY_GPU_DRAWS=1` (`[gpu-draws]`, `[pso-desc]`); the same on
Vulkan, whose log also lists the device's memory heaps and types (`[vulkan] memory`).

| internal resolution 1x | GPU ms/frame | evidence |
|---|---|---|
| 2.0.13-beta.1 | 11.6: the scene 10.5, presentation (post-processing, UI) 0.68, Frame reads 0.35 | M |
| Curved surfaces off | 5.15 | M |
| Every curved patch held to one segment (diagnostic build) | 11.37: the cost is the patch stages, not the triangles they add | M |
| A curved draw's flat triangles drawn as triangles | 9.04 against 11.88 in the run before it; render thread 80.1 against 77.6 ms/s | M |
| Patches the hull shader would leave whole drawn as triangles (GPU sort) | 5.86 against 9.02; the sort 0.6-0.9 of it | M |

At 3x: 17.48 ms (57 fps) against 18.97 (52.7 fps) with the sort; 15.84 ms (60 fps) with debanding in the window
(presentation 4.18 -> 1.85 ms); 14.82 ms with the game's clears as clears (1x: 5.75 against 5.99). The frame is
pixel-bound there (24 million pixel shader invocations against 4.1 million at 1x).

- Curved surfaces: in the models that bend, 53 % of the triangles (Battlerock: 141,087 of 265,000) have no corner
  with a displacement or crease normal. The hull shader gives each such edge one segment and the domain shader moves
  nothing, so the tessellation stages output the triangle unchanged. A cached display list's triangles are split once,
  flat ones first: those draw as a triangle list with the same shaders less the patch stages, the rest as patches
  (`DrawCall::flat_index_count`). An edge shared with a curved neighbour has flat corners, so it stays one straight
  segment there too. Only draws whose triangle order cannot show are split: no blending or logic op, depth tested by
  nearness and written; or sums (both blend factors one, added or subtracted, into the fixed-point frame, depth not
  written), whose clamped results do not depend on the order. The plain pipeline compiles on a worker; until it is
  ready the draw stays whole.
- Of the remaining patches, 79 % at 1x and 65 % at 3x get one segment on every edge from the hull shader (domain
  shader invocations with and without them, pipeline statistics): small or distant triangles. Dropping only those
  cut 2.4 ms though they cover almost no pixels. A compute pass before the draws (`ShaderGenerator::curve_sort_source`;
  D3D12: a command list before each, Vulkan: the end of the frame's upload commands) evaluates the hull shader's
  edge rule (`curve_value`, one HLSL function for both) for every curved triangle and writes two index lists in the
  draw's order, the whole triangles and the patches, drawn indirectly (`ExecuteIndirect`,
  `vkCmdDrawIndexedIndirect`). A triangle goes whole only well under the rule's limit (0.8 of it) and never
  with a corner near the eye plane, so the two stages' rounding cannot part an edge. Interpolated frames draw the
  static split (their camera is another).
- A split draw's flat triangles come first; they draw from its own indices before the sort's lists, so the sort reads
  only the rest (Battlerock: 16,058 of 38,550 triangles a frame): the sort 0.92 -> 0.69 ms, the frame 5.79 -> 5.67
  (Vulkan 0.91 -> 0.82 and 6.78 -> 6.76). The triangles draw in the order they did. Not kept: each vertex's corner
  worked out once in a pass before the classification and stored (64 bytes a vertex) took the sort from 0.88 to
  1.82 ms; the stored corners cost more to read back than the arithmetic they save.
- The GPU read the resident decoded vertices from upload memory: the sort took 1.27 ms reading them there and 0.46 ms
  from a local copy. They are now kept in local memory only, each run copied once when it becomes resident through
  staging memory of that frame (Vulkan writes them straight into device memory the CPU can map,
  `vk::BufferPlace::DeviceMapped`: unified memory, resizable BAR, else host memory).
- Debanding ran at the picture's own size before scaling, 3 ms of the 3x frame (`GALAXY_DEBAND=0`: presentation 4.18
  -> 1.16 ms): 3 million pixels, taps up to 70 pixels away. A reduced picture is now scaled to the window first and
  debanded there (a third of the pixels, the radius the same share of the height, the dither at the size it is
  seen); an enlarged one as before. Same run (`GALAXY_TEST_DEBAND_AB=1` alternates the two): the paused Battlerock
  frame differs by at most 4 of 255 everywhere; the title's sky, contrast stretched, is smooth in both.
- The game clears the frame by drawing a quad (`MainLoopFramework::clearFrame`, colour and depth; `MR::clearZBuffer`, depth
  only) whose pixel shader writes the depth of a Z texture with the compare always: every pixel exports its depth,
  which keeps the early depth test and fast clears off (at 3x: 0.81 and 0.76 ms). The game now marks the quad
  (`__NebulaGXClearQuadMarker`, the texture's depth); the renderer clears the depth (the shader's formula,
  1 - z/2^24) and draws the colour, where written, with the same quad less the Z texture and the depth compare, so the
  colour is the shader's own (0.33 ms). A quad that is not the expected one (another compare, blending, a 16-bit
  depth frame, a viewport short of the scissor) draws as written. Same run (`GALAXY_TEST_CLEAR_QUAD_AB=1`, paused):
  identical at 1x and at 3x with 4x MSAA.
- Pictures (same run, `GALAXY_TEST_CURVED_SPLIT_AB=1` draws curved surfaces whole every other frame; the pause menu
  holds the scene, which is still drawn each frame), with the sort: identical at 1x, at 3x, and at 1x with 4x MSAA.
  At 3x with 4x MSAA two pixels at an edge differ by up to 3 of 255: with the sort a triangle moves between the two
  lists, and samples exactly on an edge where two surfaces meet at equal depth go to whichever is drawn last. With a
  tighter limit (0.25) two other pixels differ by 1. With ray tracing High the paused scene changes a little every
  frame by itself: frames two apart, drawn alike, differ more (28k pixels) than neighbours (21k). At 120 fps game and
  interpolated frames differ as without the split (one spot, 1 of 255). One D3D12 run of seven (and one of two since)
  at 1x paused where 36 pixels next to a curved model, under the pause menu, differ by up to 3 of 255: open, never
  seen on Vulkan (32-bit float depth against D3D12's 24 bits). Not position rounding (the plain and curved vertex
  shaders compute it with the same DXBC, the same dp4s); the lead is draw order, a triangle the sort moves between
  the lists tying at equal depth with one that stays.
- Vulkan, the stand-in at 1x: 5.63 ms of scene, 0.92 of sort, 1.08 of copies and presentation (D3D12: 3.92, 0.87,
  1.02); without curved surfaces 5.07 ms a present against 3.93. The sort read the resident vertices from host memory
  and wrote its lists there (1.48 ms); both now lie in device memory. The draws do the same work on both APIs (the
  same vertex, patch, primitive and pixel shader invocations) and the scene's gap is the same at 3x (10.94 against
  9.23 ms), so it is not pixel work: by pipeline, most draws take 1.3 to 2 times as long, and the shadow volumes'
  additive alpha pass 0.98 ms against 0.20 while their subtractive pass, the same geometry and shaders, costs the same
  on both (open; measured on AMD's Windows driver only). Placing the rings in device memory changed nothing.
  Pictures: same run, paused, identical at 1x; at 3x with 4x MSAA two pixels differ by 1 of 255. Curved surfaces had
  never drawn on Vulkan (the curve data, TEXCOORD16, had no location between stages); once built they drew unculled,
  under a lower-left domain origin that reversed their winding. NVIDIA's driver crashes building them: they stay off
  there (`DeviceQuirks::no_tessellation`).
- Pipelines were created again at every start (`GALAXY_TEST_HITCHES=1`, frames of 50 ms or more, stand-in at 1x: 7 on
  a second start, the worst 748 ms; 11-13 on the first, 800-860 ms). psos.bin's reload stopped at the first curved
  surface's key (patch topology, which the key check did not accept) and the repair dropped the rest: 2 of 167 keys
  known at the next start. The D3D12 pipeline library (pipelines.bin) was written only at shutdown, which the host
  never reaches (it ends with `std::_Exit`): every start had the driver compile every pipeline. The background flush
  that writes the shader and key caches every 20 s now writes the library too (Vulkan's already wrote
  `vk_pipelines.bin`), and curved keys reload; their stand-in (uber) pipelines are built flat, as the draw path builds
  them. Second start at max settings: 174 of 174 pipelines from the library, none compiled. The frames of 50 ms or
  more left on a second start are the boot (754-777 ms after the first game frame) and the galaxy's load, not
  pipelines.
- The shadow volumes (the alpha-only sums above: 40 draws, 6,240 triangles a frame at Battlerock) drew whole, every
  triangle a patch. Split, the stand-in at 1x: 5.28 / 5.37 / 5.28 -> 5.02 / 5.01 / 5.14 ms (three alternating pairs;
  scene 3.58 -> 3.20, sort 0.67 -> 0.81), hull shader invocations 14.2k -> 7.6k a frame, domain 74.9k -> 55.0k. Same
  run (`GALAXY_TEST_CURVED_SPLIT_AB`, paused): identical.
- Interpolation's vertex blend and physical ray tracing's de-baking are left out of the vertex shaders of frames that
  run neither (`kVsFeature*`). The blend read a buffer per vertex and every attribute (it blends them all), so every
  attribute was fetched. No change measured on the stand-in at 1x (three pairs, within run-to-run noise); same run
  (blend and none on alternate frames, paused): identical.
- Code only ray tracing runs is left out of the pixel shaders of a scene drawn without it (`kPsKeyRtPaths`): the
  enhanced lighting's branches on header bits 16, 17 and 31 and the clear-ice tint (two sRGB round trips) in every
  textured shader. Lit enhanced shaders: 709-741 -> 468-505 DXBC slots, RDNA3 1391-1764 -> 1180-1535 instructions,
  VGPRs 46-51 -> 43-46. The stand-in at 1x with every setting but ray tracing: 8.07 / 7.85 -> 7.64 / 7.53 ms (two
  alternating pairs); at 4K within run-to-run noise. Pictures without ray tracing are unchanged by construction: the
  removed code ran only on header bits ray tracing sets.

### Native draws

How the game became native draws, and what each step measured. The command stream, its parser and the markers
named here are gone ([GRAPHICS_ARCHITECTURE.md](GRAPHICS_ARCHITECTURE.md)); the numbers stand as the record of each step.

- Model shapes: J3DShapeDraw draws its list by a handle (`__NebulaGXCallShapeDisplayList`, the shape-list marker).
  Each list is built once on a worker at background priority (`galaxy/render/native_shapes.h`): decoded by the renderer's
  own decoder, each distinct vertex once, its triangles in the list's order. Registered when its model is final
  (`NebulaNativeShapes::registerModel` from ResourceHolder, after ModelUtil's edits: texture-matrix indices added to
  lists, new vertex descriptions, position arrays halved) with its vertex description, the arrays J3DShape sets before
  each draw and its current matrix; a list registered nowhere is built from its first draw. Drawn by handle while the
  list and vertex state match; the array bytes it was built from are compared with memory once per batch of game
  writes, as the decoded cache does; a changed list or state is built again once; arrays the game rewrites are built each frame. Shapes not drawn for 3,600 render chunks are dropped (the game frees models with their heaps, no destructors).
  Battlerock, stand-in at 1x, every render chunk counted: 1,008,899 shape draws native and 3 drawn the old way while built
  (first-draw building alone: 999,672 of 1,000,000); the parser's commands a frame 77 KB -> 28 KB; render thread
  84.0 / 84.2 / 84.4 -> 80.1 / 78.6 / 77.2 ms/s, GPU 4.96 / 4.99 / 5.03 -> 4.92 / 4.83 / 4.80 ms; boot's two frames over
  50 ms unchanged (76 + 87 against 75 + 88 ms; the builder at normal priority made one of 218 ms). Same run
  (`GALAXY_TEST_NATIVE_SHAPES_AB`, native every other presented frame, paused): identical; with ray tracing High the
  native and old-path frames differ no more than two old-path frames do.
- Matrix palette snapshots copy 3,584 of their 6,656 bytes: XF memory has nothing at 0x100-0x3FF, which no reader
  addresses (98 snapshots a frame at Battlerock: 652,288 -> 351,232 bytes copied).
- Display copies. SMG's copy to the picture filters every frame (weights 32, 32, 0: each row averaged with the one above,
  the Wii's deflicker), so it is a pass of its own: `display_filter_shader_source`, the general conversion's integer
  arithmetic for a 1:1 copy without gamma (rows loaded, not filtered by the sampler; truncated to 8 bits, weighted,
  shifted down by six). Same run (`GALAXY_TEST_DISPLAY_FILTER_AB`, paused): identical pictures and pictures. Stand-in at
  1x: Display copy 0.14 -> 0.10 ms; 4K at 5x: 3.65 -> 2.61 ms. Vulkan has the same pass.
- A copy that changes nothing (the identity filter at boot, no gamma, the whole picture, one frame pixel to one picture
  pixel) is not copied: the frame's colour target becomes the picture (`can_hand_over_display_copy`), and the frame takes an
  idle texture. Its old picture is owed until the frame is next read or partly drawn; a clear of all of it pays nothing,
  a clear of part restores the bands around it. Presentation reads the picture from the top left of the taller
  texture with its own passes (`SOURCE_BOX`: resampling, debanding, EASU, RCAS at window size); a runtime branch in
  the shared passes cost 0.13 ms at 1x and 0.36 ms at 4K, so they are compiled apart. WARP test (`held_picture_on_warp`,
  debanded and not): the held picture presents bit for bit as the same picture in its own texture, bilinear and
  FSR 1, sharpened or not, enlarged, reduced, the same size and each axis apart; with the box passes switched off the
  test fails in every path that samples. D3D12; Vulkan copies as before.

### Low end at 4K: every setting but ray tracing

The stand-in's 2-CU iGPU, Battlerock, a 3840 x 2160 window, internal resolution 5x (3200 x 2640; the picture
3200 x 2280), SMAA ultra, ambient occlusion, per-pixel and enhanced lighting, curved surfaces, 16x anisotropic
filtering, enhanced mipmaps and bloom, 240 fps asked. The game runs at 15-18 fps there, so nothing is interpolated.
D3D12, GPU ms a game frame:

| pass | before | after | evidence |
|---|---|---|---|
| presentation | 16.1 (debanding 13.8) | 8.8 (debanding 6.0, scaling to the window 2.4) | M |
| SMAA (scene) | 6.9 | 6.2 | M |
| ambient occlusion | 4.3 (its apply 3.5) | apply 2.3 | M |
| Frame reads | 5.0 | 4.9 | M |
| Display copy | 3.8 | 3.6 | M |
| scene | 22-28, by run | the clear quad's colour draw (1.1) gone | M |

- Debanding at the picture's size was 13.8 ms: for every pixel, the rings' random taps (up to 200 pixels away at
  this size) and their arithmetic (a hash per random number, a sine and a cosine a ring). Taking only the far taps
  from a reduced picture left 10.9 ms: the arithmetic, not the cache, was the cost. From 1080 lines the rings run on
  the picture reduced by a box (2x2 to 2159 lines, 4x4 to 4319, 8x8 above: `PASS_DEBAND_REDUCE`,
  `PASS_DEBAND_SMOOTH`), and each pixel keeps its own edge test, takes the smoothed picture where it lies within the
  threshold of it, and is dithered only where it lies between levels (`PASS_DEBAND_APPLY`): 6.0 ms. 1920 x 1080 at
  3x: the frame 33.6 -> 23.8 ms. Below 1080 lines it is the one pass as before. Same run (the one pass and the
  three on alternate frames, paused): at most 4 of 255 apart with the 2x2 box, 3 with 4x4 and on the source-size
  path, the size of the dither; the smooth regions alike when contrast stretched.
- The game's clear quad (`MainLoopFramework::clearFrame`) draws one colour, C0 and A0 of a one-stage material. Its marker
  now carries the colour, and the renderer clears it as it clears the depth, with the values the quad's pixel shader
  stores (8-bit colour; on a frame with alpha the 6-bit alpha: the material's, where one writes zero, or the destination
  alpha constant; `kFrameClearExact`). Fog, the temporal upscalers' targets or a lit translucent keep the quad. Same run
  (`GALAXY_TEST_CLEAR_QUAD_AB`): identical at 1x and 3x on D3D12 and Vulkan.
- Ambient occlusion's apply read the depth and four occlusion pixels with five loads a pixel. It now gathers the four
  (two gathers) and, before reading the depth, skips a pixel whose least occlusion leaves its colour within a half
  level: the visibility is a weighted mean of the four and the colour's multiplier rises with it, so those pixels
  were left unwritten before too. The same values and arithmetic: the picture is unchanged by construction.
- SMAA's blend into the frame wrote every pixel, a pixel without blending weights writing back the copy's own colour;
  it now leaves those unwritten (`PASS_SMAA_BLEND_KEEP`): 2.7 -> 1.9 ms. Same run: identical at 1x and 3x.
- One-channel Frame reads (I4, and I8 from R, G, B or A, such as the A8 screen capture) are R8 textures viewed as R in
  every channel, as decoded I8 textures are: a quarter of the memory (the 5x capture 30 -> 7.5 MB); the copy's time
  did not change (it reads more than it writes). Same run: identical at 1x; at 3x one pixel differs by 1 of 255.
- Left: the planet's per-pixel-lit pipelines (about 10 ms; curved surfaces off barely change them, per-pixel and
  enhanced lighting off halves them), the game's full-screen effects (about 5 ms), SMAA's copy and passes, the picture
  copy, debanding's apply and the scaling. Vulkan (same settings): 54.2 ms, presentation 10.5.

### High end: 240 fps with ray tracing

RTX 5090, Battlerock, a 3840 x 2160 window, ray tracing High, 240 fps (frame interpolation fills the presents between
the game's 60):

| internal resolution | presented | ray tracing engine per game frame | evidence |
|---|---|---|---|
| 6x (3840 x 3168) | 29-35 fps: the game's own frames, none interpolated | 22.6 ms (denoise 8.0, direct 4.6, indirect 4.5, shadow 2.0, G-buffer 1.9) | M |
| 4x (2560 x 2112) | 57 fps: the GPU full, interpolation off | 9.4 ms | M |
| 3x (1920 x 1584) | 240 fps: three interpolated frames a game frame | 5.6 ms | M |
| 3x, ray tracing off | 240 fps, 0.14 ms of GPU a present | none | M |

- The engine's cost follows the internal resolution's pixels. An interpolated frame costs 1.43 ms of GPU at 3x; the
  interpolation's cadence governor drops to fewer interpolated frames when one misses its present slot, and a game
  frame's tracing runs ahead of them on the same queue (the first interpolated frame after it presents 1-2 ms late).
  240 fps at 4x needs the engine under about 4 ms.
- The neighbour taps (a-trous, spatial reuse, upsampling, shadow blurs) read a 12-byte guide the G-buffer pass copies
  from its records, bit for bit (`gb_guide`): 4x 9.77 -> 9.44 ms (denoise 2.95 -> 2.73, shadow 0.90 -> 0.80).
- GPU timers serialise the work: with `GALAXY_GPU_PASSES` and `GALAXY_RT_PASS_TIMING` on, 3x fell from 240 to 120 fps.
- By dispatch (`GALAXY_RT_PASS_TIMING=2`, `[rt-engine-kernels]`), 4x: G-buffer 0.92 ms, direct 1.05 and its spatial
  reuse 1.03 (up to 25 target evaluations a pixel: the generalized balance heuristic over five samples), the sun
  shadow's blur, post and stabilisation 0.30, 0.31 and 0.19, indirect 1.75 and its spatial reuse 0.30, specular 0.47,
  temporal 0.68, the five a-trous iterations 0.45, 0.38, 0.39, 0.42 and 0.43, final 0.35.
- The a-trous costs the same at every step, so it is not its far taps' cache misses. Tried and not kept, same picture:
  its weights' exponentials merged (half the transcendentals a tap; denoise 2.705 -> 2.696 ms), its taps of steps 1-4
  staged in group memory (every iteration slower, 0.45 -> 0.54 ms at step 1: the 16 KB a group lowers occupancy), its
  rows looped instead of unrolled (0.445 -> 0.465).

## ThinLTO and profile-guided optimisation (M)

A player's build is `native/build.ps1 -Pgo`: ThinLTO with the profile of the 102-stop tour (`native/pgo/game.profdata`, made by
`tools/verify/pgo_profile.sh`). Battlerock, 60 fps, the game's and the audio frame's CPU, 10th percentile, in the two rounds with an undisturbed
render thread (83-85 ms/s in every build), four rounds interleaved:

| build | game + audio, ms/s | module |
|---|---|---|
| plain | 33.9, 35.9 | 6.9 MB |
| ThinLTO | 31.8, 33.1 | 9.1 MB |
| ThinLTO + profile | 29.1, 28.2 (the disturbed rounds too: 30.2, 30.1) | 7.3 MB |

Pictures 399 of 399 and audio identical to the plain build in deterministic mode (run pcp). Development builds stay plain: they link in a fraction of the time.

## Memory (M)

`GALAXY_TRACE_MEMORY=1` prints what the renderer holds every 600 frames, `=2` adds the process's heaps by block size; `memws`-style
readings below are the 102-stop tour at stop 30, real time, 1x, NVIDIA. The store is mapped, not copied: waves and streams play from
it (`asset_map`), and its pages are the file's: no commit, and the system can drop them.

| what | before | now |
|---|---|---|
| process, private working set | 2,224 MB (first reading) | 357-372 MB |
| upload rings | 464 MB, all resident | 64 MB; the most a frame took: vertex 2.2, index 1.2, constant 1.4, matrix 0.7 |
| resident vertices | CPU copy + 64 MB upload copy + 64 MB local | 64 MB local |
| wave archives | 48 MB and more in the sound memory | none: mapped |
| streams | a whole file in the heap each (29 MB for one) | none: mapped |
| images of dead scenes | grew to 404 MB over a tour | gone with their scene's heap |
| archives | read into the heaps | private views of the store: only written pages are the game's |
| game block | 512 MB committed | 385 MB committed, about 41 MB in memory |
| limited to 4 GB of committed memory, the 102 stops | 2,378 MB peak | 1,363 MB peak, nothing lost |

## Planned, measured before adopted (H)

- **Two module builds:** x86-64 baseline and x86-64-v3 (AVX2, FMA). The host loads the best one the CPU runs.
- **NEON for `nebula_math.h`** with the other platforms (section S).
- **Linux huge pages for the game's block** (`MADV_HUGEPAGE`). Measure TLB misses.
