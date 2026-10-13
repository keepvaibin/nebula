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
| Texture invalidation | Explicit only (`GXInvalidateTexAll`, EFB copies to the same address) | Hashing texture contents costs too much on low-end CPUs |
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
| `0x40` | CALL_DL | u32 guest address, u32 size: nested display list, depth up to 4 |
| `0x48` | INVAL_VTX_CACHE | none |
| `0x61` | LOAD_BP_REG | u32: register in bits 31-24, value in bits 23-0 |
| `0x80`-`0xBF` | DRAW | primitive in bits 7-3, vertex format in bits 2-0; u16 count and the vertex data |

BP register numbers are named in `gx_bitfields.h` (`namespace bp`).

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
- **Presentation**: the XFB the game's video interface selected. While the selection is between
  valid states, the previous valid XFB stays on screen. Frames the game did not redraw are not
  presented again.
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

## HUD separation and temporal inputs

The game module routes two call sites through the host: `GameScene::draw3D` from
`GameScene::draw` (`0x8033E820`) and the closing `MR::clearZBuffer` of `draw3D` (`0x8033F024`).
The render thread splits its parse at those points (`FifoMarker::SceneBegin`, `SceneEnd`). With
`hud_output_resolution`, the finished 3D image is resampled into an output EFB at the window's
integer scale and the 2D layer (HUD, menus) is drawn there.

The optional FSR 3 path (built with the AMD FidelityFX SDK) uses the same split: sub-pixel jitter
on perspective draws inside the 3D scene (`GxBackend::apply_scene_jitter`), the EFB depth, and
motion vectors from `transform_history.{h,cpp}`, which matches each scene draw to the previous
frame's draw with the same shader keys and vertex source and writes per-pixel motion.

## Frame rate

The game renders 60 frames per second. FRAME RATE offers 30 (the newest frame every other display
frame) and 60. With VSYNC, the sync interval is the refresh rate divided by the frame rate when
that is an integer from 2 to 4.

Interpolated frames above 60 exist behind `GALAXY_EXPERIMENTAL_INTERPOLATION=1` and are not
offered in the settings.
