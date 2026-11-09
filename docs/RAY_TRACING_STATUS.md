# Ray tracing: status at halt (2026-10-17)

Work on ray tracing stopped on 2026-10-17 by the owner's order (the project moves to Petari as the game's
core). Everything is committed on branch `ray-tracing` (same commit as `wt-motion`, a19ad0a and this document).
Nothing is pushed. This file is the hand-off: what exists, what was measured, what is wrong, what comes next.

## What exists

- Scene: every opaque scene draw's triangles in one BLAS per frame (late geometry such as Rosalina in a second
  instance), traced with DXR 1.1 inline ray queries (`ray_tracing.cpp`, `rt_lighting.hlsl`, `rt_reflections.hlsl`).
- Lighting (`TraceCS`): sun (4 disc rays on High), game point lights (16, area emitters), glow and table lights
  (up to 512 remembered, per-tile bit lists), 4 cosine bounce rays of 1,600 units whose hit light is the previous
  on-screen picture. Temporal (`TemporalCS`) and two a-trous filters, then `SpatialBCS`'s day/night art model.
- Reflections (`ReflectTraceCS`): one GGX visible-normal ray; hit colour from the current picture, else High's
  partial hit shading, screen memory, probe or average; analytic highlights; virtual-image reprojection.
- Emitters: glow sprites and material-table rows become lights (`GlowEmitCS`, `AuthoredEmitCS`, `EmitMergeCS`),
  clustered in 120-unit cells, matched across frames by identity (material x cell).
- Material table: `rt-materials.tsv` (texture-hash keyed; shipped beside the executable, kept outside the repository
  with the game data it describes). Stage rows; `untextured` stage rows; tokens include
  `emitter`, `emissive-area`, `tint`, `strip`, `star`, `mirror`.
- Reference path tracer (its test switches are no longer read): one path per sample per
  frame, every bounce shaded from the scene in linear light, averaged in float32 while the camera holds still.
  0.5-2.5 ms per frame on an RTX 5090 at internal 2x.

## Audit (2026-10-17)

Full page: https://claude.ai/artifact/AZAGsPyU5fRP8oRXhryc7g (paused). Findings, most severe first:

| | Finding | State |
|---|---|---|
| F1 | Bounce and reflection hits take their light from the screen, not the scene | open (plan R1) |
| F2 | The composite is an art model (fixed shadow depth, brightness caps), not light transport | open (R4) |
| F3 | Light lists and shadow selection changed frame to frame | fixed a19ad0a |
| F4 | No reference image | fixed a19ad0a (reference path tracer) |
| F5 | Denoising: 3x3 clamp of single samples, geometry-only filters | open (R3) |
| F6 | Glowing surfaces light nothing unless a bounce ray hits them | cables via strip lights (a19ad0a); general case open (R2) |
| F7 | Bounce rays end at 1,600 units | open |
| F8 | Ray count follows internal resolution (6x internal = 3.2 M samples) | open (R5) |
| F9 | Emitter merge is O(lights x found) | open |
| F10 | Light multiplies display-encoded colour (no sRGB decode) | open (R4; the reference is linear) |
| F11 | Samples rebuilt without the upscaler's jitter | fixed a19ad0a |

## Measurements (RTX 5090, 2560x1440, internal 2x, High, DLSS DLAA)

Flicker = per pixel |f(t) - (f(t-1) + f(t+1)) / 2| on luminance, mean over presents 6600-6660 (plaza, Mario's
spawn view) or 3900-3960 (garden door), tools in `tools/rt-eval`.

| Build | Plaza | Garden |
|---|---|---|
| ray tracing off | 0.170 | 0.312, 0.341 |
| lights21 (2569a9b's emitter work) | 0.363 | |
| + identity, tile bitmask, rank hand-off | 0.262 | |
| + jitter fix | 0.220 | 0.332 |
| a19ad0a (+ lit cables, light fade-in) | 0.226, 0.230 | 0.357, 0.367 |

Garden runs vary by about 0.03 run to run (fireflies). Remaining plaza share: the glossy platform reflecting the
game's animated sparkles, Mario's idle feet. Per stage (lights22 build, before the fixes): Beach Bowl +0.03,
Freezeflame +0.06, Battlerock +0.03, Flip Panel +0.10 (mostly its rotating red ring seen in the glossy panels).

## Plan when resumed

R0 reference and bench (done: reference; bench of 8 views still to script), R1 scene hit shading with a world-space
radiance cache, R2 every emitter a sampled light (emissive triangles included), R3 denoising (SVGF-class; DLSS Ray
Reconstruction on RTX through the user's Streamline 2.14.1, `sl_dlss_d.h`), R4 linear physically based composite
(owner sign-off on the look), R5 cost. With Petari as the core, the game's own light and material calls become
direct inputs: sun and point lights from the actor code, materials per model instead of per texture hash.

## Native rebuild (2026-10-19)

Inputs now come from the game: lights from their owners (d93f4a0), the stage (1b637a3), object names (964e4f7).

Measured on a still Good Egg view (internal 2x, linear luminance mean): game 0.385, ray tracing 0.226 (41% darker:
the art model), reference 0.386. The reference is not independent: it multiplies the game's lit picture by traced
light and normalises exposure to the scene's geometric mean.

Light units: the game lights a surface as material x (ambient + light colour x N.L), so its area light's ambient is
the sky's radiance and its key light's colour the sun's irradiance over pi. Traced with those, a scene without
shadows or bounces equals the game's own per-pixel lighting; occlusion, shadows, bounces and emitters are what
tracing adds. Order:
1. The game's area light (LightDirector's default AreaLightInfo, the planet light: ambient, key and fill lights)
   reaches the tracer natively.
2. Reference: each pixel's own albedo (texture x vertex colour, linear) and emission times its traced light, in the
   game's light units; no exposure normalisation. This is the ground truth of the bench.
3. Real-time: the same estimator at one path per pixel (sun and emitter sampling, textured bounce hits), temporal
   and spatial denoising, composite of radiance instead of a lighting ratio.
4. Reflections and water on the same hit shading; materials by the models' own names; emitters by class.

Licences checked from each repository's LICENSE (2026-10-17): NVIDIA NRD, RTXDI, RTXGI are under the "NVIDIA RTX
SDKs LICENSE" (not usable in GPL-3.0 source); Streamline headers MIT; AMD Capsaicin (GI-1.0) MIT; NVIDIA FLIP BSD-3.
ReSTIR patent status not verified.

## Open tracker rows at halt

Tracker (paused): https://claude.ai/artifact/5QdgTtKhqPvCuYukorJqY7, collection `issues`. Open or in progress: 18
Rosalina's reflection, 20 Battlerock metal flashes, 21 reflection angles, 22 water, 23 ice, 13 green platforms as
glass, emissive (glowing things light nothing), lamps, particles, metals, glitter, selfshadow, shake, popin, perf,
32 plaza flicker (residual), 33 audit plan.

## How to run the tests again

- Hidden sessions: the game started with a hidden window, private saves and settings, and the test switches below
  (a galaxy tour, teleports, present snapshots), one session at a time.
- Views: plaza = tour `GALAXY_TEST_GRAND_STARS=6;GALAXY_TEST_TELEPORT=3100,700,1150,900/...` presents 6600-6660;
  garden = `GALAXY_TEST_TELEPORT=3200.7,392,1050.6,900`, presents 3900-3960.
- The views of single terms (sun, sky and bounce, lamps and glows, contact occlusion), the material-row probe, the
  emitter log and the validation dumps were selected by `GALAXY_RT_*` switches that no code reads now, and the glow log
  (`GALAXY_RT_LOG_GLOWS`) is gone with them.
- A reference run lasts until `-Seconds` after its snapshot; give it about 200 s.
