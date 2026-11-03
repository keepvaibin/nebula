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
| Models | Cooked meshes: float arrays, corners, triangles (`cook/mesh.rs`). Vertices expand once and stay in the resident vertex buffer |
| Materials | A shader compiled at build (`NebulaShaderId`) with its constants, textures and state (`NebulaNativeMaterial`). J3D materials come from `tools/dev/gen_j3d_materials.py`, sites and layouts from `runtime/src/render/materials` |
| Draw state | `NebulaPassState` on every draw: blend, depth, cull, writes, position matrix, line and point sizes |
| Vertices the game makes | `immediate`, written into the frame's pool. Consecutive quads with nothing changing between them are one draw; an emitter's particles are one draw |
| Lines and points | Four corners each, made by the host (`widen_lines_points`), moved by the vertex stage. No geometry stage |
| Passes into textures | `target_begin` and `target_end`: the texture is the renderer's, at the scene's scale |
| Reading the frame | `target_capture` copies a rectangle to a texture (a grab pass). `screen_probe` reads named points a frame or two later (lens flare, star pointer, talk balloon) |

The rings are write-combined: code writes them and never reads them.

## Textures

Cooked at import into records (`nebula_cooked_texture.h`): BC1 for CMPR without transparent blocks, L8 and LA8 for intensity formats,
RGBA8 otherwise, palettes applied. Textures the game writes while running are linear (`NEBULA_TEXTURE_LINEAR_*`). A texture's key is
the address of its image; a texture leaves when its scene's heap goes (`shape_forget`).

## Resolution

The scene renders at the window's exact size. The game counts viewports, 2D and effect passes in its 640x456 canvas; `FrameMap`
maps the canvas to pixels at any size, non-integer and non-uniform.

## Precision

Colour is float through the shader: no 6-bit alpha, no 8-bit rounding, no dither. Depth is reversed. Rows of differences this made are in
the project's look-change list.

## Pipelines

810 known pipelines (`pipeline_seeds.inc`, from tours by `tools/dev/pipeline_seed.py`) are built on two background threads at start
(`[pipelines] N prepared at T ms`). Anything else is created at its first draw, synchronously, on the render thread.

## Failure

A lost device (driver reset) restarts the game from its save, twice in a row at most; the third is report NEB-G01
([ERROR_CODES.md](ERROR_CODES.md)). Vulkan drivers' refused features are learned per driver build (`vk_compat.txt`, [VULKAN.md](VULKAN.md)).

## Testing

`tools/verify`: identical pictures between builds in deterministic mode (`GALAXY_TEST_DETERMINISTIC=1`), a window against the same build's
control in real time, all stops in the tour, `[loss]` counters for draws without a material or pipeline.
