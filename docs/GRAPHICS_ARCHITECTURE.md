# Graphics architecture

The GX backend (`galaxy::gx`, the `galaxy_gx` static library) turns the GX command stream that
the game writes into Direct3D 12 work. It implements what Super Mario Galaxy uses, not the whole
GX hardware, and is not derived from Dolphin's video backend. Headers are in
`runtime/include/galaxy/gx/`, sources in `runtime/src/gx/`.

## Design decisions

| Decision | Choice | Reason |
|---|---|---|
| API and shaders | Direct3D 12, HLSL shader model 5.1 (FXC) | Dynamic texture indexing for the uber pixel shader; a path to DXR |
| Render targets | All draws go to an offscreen EFB (RGBA8 and D24S8, 640x528 times the internal scale). `GXCopyDisp` copies into an XFB texture; presentation scales the XFB the video interface selected | EFB-to-texture copies, which the game relies on, need a real EFB |
| Depth format | `D24_UNORM_S8_UINT`, reversed | Matches the Wii's 24-bit Z; copy-clear values map exactly |
| Shader compilation | Specialized pipelines compiled on worker threads, an uber shader while they compile, and a disk cache | No compile stalls on the render thread; no uber-shader cost in steady state |
| Texture data | The game hands each image's bytes over where it loads or rewrites it (`texture_image`); the renderer decodes from its copy. Images not handed over yet decode from game memory, invalidated by the game's flushes | Records name images by identity; no hashing of texture contents on low-end CPUs |
| Byte order | Checked big-endian reads for commands; SSSE3 byte-swap kernels for vertex arrays | Vertex data is the hot path |
| Unknown input | Unknown opcodes, register writes, truncated payloads and unresolvable guest pointers raise `GxFatalError` and write a `.gxdump` | Unsupported behaviour must not be skipped silently |

## FIFO format

The parser reads an opcode-dispatched, big-endian byte stream:

| Opcode | Command | Payload |
|---|---|---|
| `0x00` | NOP | none |
| `0x08` | LOAD_CP_REG | u8 register, u32 value (vertex descriptors, attribute formats, array bases) |
| `0x10` | LOAD_XF_REG | u16 count-1, u16 base, count u32 values (matrices, texgen, channels, projection) |
| `0x20`, `0x28`, `0x30`, `0x38` | LOAD_INDX A-D | u16 index, u16 length and address: indexed matrix load from guest arrays (skinning) |
| `0x48` | INVAL_VTX_CACHE | none |
| `0x61` | LOAD_BP_REG | u32: register in bits 31-24, value in bits 23-0 |
| `0x80`-`0xBF` | DRAW | primitive in bits 7-3, vertex format in bits 2-0; u16 count and the vertex data |

BP register numbers are named in `gx_bitfields.h` (`namespace bp`). Display lists (`0x40`) are no longer sent: the game's
geometry arrives as records, and the parser treats `0x40` as an unknown command.

## Native record stream

The game also hands the renderer typed records (`native/include/nebula_game_api.h`, host services), the native
path that replaces the GX stream step by step. Each record carries the GX writer's position when it was made, so
`run_stream` applies records and remaining GX commands in the order the game issued them.

| Game side | Record | Host side |
|---|---|---|
| `J3DNativeShape` (J3D shapes) | `shape_array`, `shape_geometry` once; `shape_draw` with its matrices, current matrices and texgen overrides | `native_shapes` expands corners into resident vertices; `draw_record_shape` |
| `J3DNativeMaterial` (J3D materials, per-instance differed parts) | `material` (`NebulaMaterial`: every block's values) | `native_material::apply` |
| `NativeMesh` (geometry the game builds once: water road, ocean ring and pipe, spheres, electric rails) | the shape records, positions owned or the game's arrays by key | as shapes; owned positions keep the direct-vertex classification |
| `J3DNativeDraw` (vertices sent as drawn: effects, shadows, 2D, text, particles, movie quad) | `immediate` (`NebulaImmediate`: primitive and typed vertex arrays) | `draw_record_immediate`, batched like direct draws |
| `J3DStateList` (state lists built once: Mario's, CollisionShadow) and `J3DNativeState` (state set as the game draws: TEV, blend, depth, channels, texgens, counts, cull, lights, pixel format, fog) | `state` (`NebulaStateOp`: GX setter semantics; a partial setter changes only its fields of the live register) | `native_state::encode`, applied with the parser's side effects (copy, texture invalidation, TLUT load) |
| `J3DNativeView` (projection, viewport, depth scale, scissor: kept game-side for read-back) | `state` | as above |
| `J3DNativeState` copies (settings kept game-side) | `state` `NEBULA_STATE_COPY` | GXCopyDisp/GXCopyTex's sequence on the live Z, blend and pixel state, then `on_efb_copy` |
| `J3DNativeShape::send*` (matrices the game loads) | a state-only `shape_draw` | `apply_record_matrices` |
| RVLFaceLib (Miis) | its state through C entry points; its shapes as immediate vertices from its own index streams | as above |

Values the game reads back or that decide whether a setter writes (cull mode, pixel format, copy settings, the view)
are kept in the game's native layer. Each op's encoding was checked against the SDK function it replaces before the
conversion (random arguments and register files; every register, XF and CP word, light memory and, for copies, the
write sequence). Texture coordinate scales are not sent: the renderer derives each coordinate's scale from the
texture sampled with it (`coordinate_scales`). What still goes through the GX stream: the frame's sync calls, Nebula's
own markers, and the state GX's initialisation writes.

## Components

```
FIFO bytes --> FifoParser --(FifoSink callbacks)--> GxBackend
                   | register loads                   | draws, EFB copies
                   v                                  v
                GxState <---- reads ---- VertexLoader --> vertex and index rings
            (CP/XF/BP + dirty bits)                   |
                   | keys                             v
                   v                            RendererD3D12 --> EFB
             PipelineCache <-- ShaderGenerator         | display copy
             TextureCache (decode, TLUTs,              v
                           EFB-copy aliases)     XFB registry --> post-processing --> present
```

| Header | Responsibility |
|---|---|
| `gx_bitfields.h` | Register decoding for CP, XF and BP |
| `gx_state.h` | Register files, decoded views and dirty bits |
| `fifo_parser.h` | `FifoParser`, `FifoSink`, `GxFatalError`, byte-swap kernels |
| `vertex_loader.h` | Any vertex descriptor and format to the canonical vertex, indexed triangle lists |
| `shader_keys.h` | Canonical shader, render-state and pipeline keys |
| `uber_constants.h` | Constant buffer layouts shared by the shaders |
| `shader_gen.h` | HLSL generation from keys |
| `pipeline_cache.h` | Pipeline cache and the completion queue for worker compiles |
| `texture_cache.h` | Texture format decoding, palettes, EFB copy aliases, invalidation |
| `efb_copy.h` | XFB registry and copy geometry |
| `renderer_d3d12.h` | Device, swap chain, EFB targets, upload rings, draw submission |
| `gx_backend.h` | `GxBackend`, which implements `FifoSink` |

The register, parser, vertex and shader headers include no Windows or Direct3D headers, so their
tests run as plain programs and replay `.gxdump` captures.

### Per frame

```
render_frame(fifo, memory, services)
  renderer.begin_frame()        wait for the fence of frame N - frames in flight;
                                publish finished pipelines (render thread only)
  parser.run(fifo)
    register loads              GxState and dirty bits
    LOAD_INDX                   resolve the guest array, load the matrix
    draw                        flush state if dirty (pipeline, constants, matrices,
                                textures), load vertices, record the draw
    BP 0x52                     EFB copy: display copy, texture copy or copy-clear
    BP 0x45/0x47/0x48           PE finish and token interrupts
  renderer.present(selected XFB)
  renderer.end_frame()          submit and signal; never waits
```

## Shaders and the pipeline cache

Specialized pipelines are the steady state. When a draw needs a pipeline that is not compiled
yet, it uses the uber pipeline, which interprets the TEV configuration from constants, while a
worker compiles the specialized one. Both use the same constant layouts and root signature, so
switching is a pipeline swap. Workers push finished pipelines to a lock-free queue; the render
thread publishes them in `begin_frame()`, before any recording.

Keys zero every field that does not affect code for the enabled stages before hashing. The
render state (blend, depth mode, culling, pixel format) is part of the pipeline key but not the
shader key, so one compiled shader pair serves all render-state variants.

The disk cache is in `%LOCALAPPDATA%\Nebula\shadercache\RMGE01\`:

- `shaders.bin`: compiled shader pairs with checksums, so later runs skip FXC;
- `psos.bin`: pipeline keys, prewarmed on worker threads at start;
- `pipelines.bin`: the driver's pipeline library, discarded when the adapter or driver changes.

TEV arithmetic runs in float, and each stage result is rounded to the 8-bit lattice before
comparisons and the alpha test, so comparisons against `ref/255` behave as on hardware.

## EFB copies and presentation

- **Display copies** (`GXCopyDisp`): the XFB registry maps each guest XFB address to a texture;
  the copy applies the vertical scale. Nothing is written back to guest memory.
- **Presentation**: each game frame as soon as it is drawn. The game queues three XFBs and its
  video interface names a frame two retraces after drawing it (47 ms from the copy to the present
  at 60 fps). The render chunk with a frame's display copy into one of the XFBs the VI has named
  presents it, and a retrace shows the newest finished copy in place of the one it names
  (`newest_finished_xfb`). Retraces present instead while the VI is black, the settings overlay
  shows, or another clock paces presents (interpolation, a temporal upscaler, frame generation).
  Pointer jump to present: 60 fps 59.7 ms before, 32.6 ms after; 120 fps interpolated 43.6 ms,
  31.4 ms; the low-end stand-in at 60 fps 61.7 ms, 34.9 ms. `GALAXY_PRESENT_ON_COPY=0` and
  `GALAXY_PRESENT_NEWEST_XFB=0` restore the game's timing. While the selection is between valid
  states, the previous valid XFB stays on screen. Frames the game did not redraw are not presented
  again.
- **Texture copies** (`GXCopyTex`): the EFB region goes into a texture registered at the
  destination address; a later texture load from that address binds it directly. Shadows,
  water, depth of field and screen transitions use these.
- **Scissor offset** (BP `0x59`, `GXSetScissorBoxOffset`): draw viewports and scissor boxes are
  moved by the offset, with the 1024-pixel wrap of the register. The depth-of-field blur uses it
  to draw into EFB rows 464-527, below the area the display copy reads.
- **PE tokens and finish** (BP `0x45`, `0x47`, `0x48`): passed to the runtime's PE interrupt
  handling (`GXDrawDone`, `GXSetDrawSync` waiters).

## Post-processing

`PostProcessor` (`post_process.{h,cpp,hlsl}`) runs on the selected XFB: SMAA or FXAA at the game's
resolution, then FSR 1 EASU or a box resample to the window, then RCAS sharpening. At start the
SMAA lookup textures are uploaded and both anti-aliasing passes run on a test edge; a pass that
fails is replaced (SMAA by FXAA).

Effect copies up to 320x228 (bloom, depth of field, Mario's shadow) are made at the game's own
resolution, so their blur kernels keep their footprint at any internal scale.

The game's bloom (`BloomEffect`, `BloomEffectSimple`) goes through the renderer by host markers
(`motion_signals.h` `kMarkerBloom`, `NEBULA_OPTION_BLOOM`). BLOOM NATIVE runs the game's own chain on
those effect copies, except the scene it works over: the renderer keeps and restores it in the EFB's
own format (`GameBloom::keep`/`restore`), where the game's 8-bit copy would cut the ray tracer's HDR
scene at 1. BLOOM ENHANCED is `GameBloom` (`game_bloom.{h,cpp,hlsl}`, `game_bloom_shape.h`;
`vk/game_bloom_vk.cpp` on Vulkan): the same chain at the internal resolution, the game's threshold over
its 4x4 pixel footprint, each ring of taps replaced by the Gaussian of the same spread (the game's own
resampling included, this chain's subtracted), the game's tap weights, clamps and fade, scaled by
INTENSITY. Interpolated frames replay both (`ReplayKind::Bloom`).

## Enhanced lighting and ambient occlusion

Enhanced lighting (`enhanced_lighting.h`, `shader_gen.cpp` `kEnhancedLighting`) shades lit and
environment-mapped draws per pixel from the game's GX lights: authored falloff summed in linear
light, a sky hemisphere about the player's up (`GxVsConstants::sky_up`, from the 3D scene marker),
the sky's Fresnel reflection, GGX highlights with specular antialiasing; the material table
decides metal. Ambient occlusion (`ambient_occlusion.{h,cpp,hlsl}`) runs GTAO on the EFB depth
when the first translucent phase begins and multiplies the opaque scene's colour; in-between
frames replay it (`ReplayKind::SceneAo`).

## Curved surfaces

The models are polygons made for a 456-line picture (a warp pipe is a 16-sided cylinder); at
several times that their outlines show the polygon. CURVED SURFACES (`curved_surfaces` in
`config.ini`, `GALAXY_CURVED_SURFACES`; offered where the GPU has hull and domain stages) rounds
them with Phong tessellation (Boubekeur and Alexa 2008).

- **Registration** (`NebulaCurvedSurfaces.cpp`): each J3D model as it loads (`J3DModelLoader`)
  reads its shapes' display lists into triangles of position and normal indices and passes them
  with its position array to the host (`services->curved_surface`). The host works out one
  displacement normal per position (`curved_surfaces.{h,cpp}`: a smooth position whose faces are
  within 40 degrees of each other and of its normal within 35; a curved crease's own bend; a
  straight crease or corner none), so every triangle sharing an edge bends it alike (no cracks).
  A model in which nothing bends is not kept. Models are keyed by the position array's GX address
  and told from later contents of that memory by a 64-word fingerprint.
- **Draws**: a perspective scene draw with indexed positions, no NBT, from a registered model, is
  drawn as patches (`kVsKeyCurved`; `primitive_topology` type 4 with the vertex output variant
  in bits 4-5). The vertex loader writes each vertex's displacement normal (`GxVertexOut::bend`);
  the vertex shader turns it into eye space (the cofactor of the position matrix).
- **Hull stage**: a patch wholly outside the view's side planes or behind the eye, at the most
  its curve can bulge, gets no triangles; otherwise each edge is cut into
  `clamp(sqrt(chord_px * angle / 2), 1, 16)` segments (fractional odd, symmetric per edge, so
  neighbours agree), where `angle` is between its ends' displacement normals: flat edges stay one
  segment.
- **Domain stage**: every field is blended barycentrically; the point moves half way to the
  blend of its projections onto the corners' tangent planes (alpha 0.5). The move is
  applied to the clip position (with the frame's jitter), the per-pixel lighting position and
  both motion positions (the previous one by the same eye-space move), so DLSS, interpolation
  and ray tracing see the rounded surface.
- Uber stand-ins draw the same patches as plain triangles (`flat_render_state`). Vulkan uses the
  same HLSL stages through glslang (tessellation control and evaluation; the default upper-left
  domain origin keeps `triangle_cw` wound as the patch, so patches cull as triangles; texture
  coordinates as eight members, which glslang sizes by the patch), where the device has
  `tessellationShader` and the driver builds them (not NVIDIA's: `DeviceQuirks::no_tessellation`).
- `GALAXY_TEST_CURVED_AB=1` draws flat in frames that end in an odd present, for same-run A/B
  snapshots.

## HUD separation and temporal inputs

The game module routes two call sites through the host: `GameScene::draw3D` from
`GameScene::draw` (`0x8033E820`) and the closing `MR::clearZBuffer` of `draw3D` (`0x8033F024`).
The render thread splits its parse at those points (`FifoMarker::SceneBegin`, `SceneEnd`). With
`hud_output_resolution`, the finished 3D image is resampled into an output EFB at the window's
integer scale and the 2D layer (HUD, menus) is drawn there. Without it, a window wider than 16:9 or
narrower than 4:3 still takes that path at the scale that keeps the layouts' density at the Wii's
own (`layout_density_scale`: their logical width past 16:9, or height under 4:3, spreads over the
same 640x456 picture; 2x at 21:9 and 32:9, 3x at 9:16), at most the window's.

The optional FSR 3 path (built with the AMD FidelityFX SDK) uses the same split: sub-pixel jitter
on perspective draws inside the 3D scene (`GxBackend::apply_scene_jitter`), the EFB depth, and
motion vectors from `transform_history.{h,cpp}`, which matches each scene draw to the previous
frame's draw with the same shader keys and vertex source and writes per-pixel motion.

## Frame rate

The game renders 60 frames per second. FRAME RATE offers 30 (the newest frame every other display
frame), 60, the standard rates above 60 up to the display's refresh rate, and Unlimited (the
refresh rate). With VSYNC, the sync interval is the refresh rate divided by the frame rate when
that is an integer from 2 to 4.

### Interpolated frames

Above 60, the frames between two game frames are replays of the newer frame's command log. Once
per pair the render thread classifies what moves (transforms decomposed into rotation, stretch
and translation; matrix palettes; lights; texture matrices; colours) into a small program
(`interp_gpu.h`); per presented frame a compute pass evaluates it at t into the constants the
replay reads, and matched draws blend their vertices with the older frame's. The replay's
command list is recorded once per pair. Two governors keep the game at 60: GPU time per
presented frame (as many frames as fit in 15 ms) and VIs the game misses.

A vertex colour that turns visible or invisible (alpha to or from zero) or jumps by more than
half its range between the two frames shows the newer value, as a texture coordinate that steps
does. The sequence ends on the frame the VI names: a retrace shows that frame, not a newer
finished copy (which a 60 fps present shows), or each game frame would step back in time.
Measured at 240 fps: closing the pause menu lit one game frame's sub-frames 18 levels above both
frames; fades see-sawed 5-8 levels every game frame; both gone.

With a temporal upscaler (DLSS, FSR 3) at rates that are multiples of 60,
every presented frame, the one at t = 1 included, is a replay with its own jitter phase and
motion vectors relative to the frame presented before it, upscaled once. When no sub-frame fits
beside that replay, the game frames take the upscaler at 60.

### Planned: warped frames for low-end GPUs

A replay costs about one game frame of GPU time, so it cannot fit where the game frame already
takes most of the 16.7 ms (measured: Ryzen 9 9950X3D graphics, 2 CUs, about 7 ms per replay at
2x; the game itself holds 60). There, sub-frames are to be image warps of the game frames:

- per game frame, in three slots: the scene before the 2D layer (output resolution), the final
  image, motion vectors and depth (render resolution); the game frame writes motion vectors;
- per pair: motion vectors dilated to the nearest and the farthest surface of each 3x3;
- per sub-frame: a fixed-point search q = p - (1 - t) v(q) into the newer scene; a pixel the
  search misses takes the older scene at p + t v_far; a pixel the 2D layer changed takes the
  final image unmoved; then the display copy and present;
- chosen when the GPU governor finds that no replay fits, and for temporal upscaling at rates
  that are not multiples of 60;
- cost: one full-screen pass independent of the scene (hypothesis: under 1 ms on that GPU at
  1280x1056).

Open: particles moved by CPU-written vertices carry no motion vectors in the game frame.
