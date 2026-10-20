# Ray tracing engine (v2)

One physically based renderer for the traced picture. Scene light is radiance in the stage's units; light comes only
from emitting geometry, the sun and the sky; the scene renders into an HDR target and a camera brings it to the
display. It replaces the art model and its physical retrofit (the light multiplier relative to the game's painted
look, sprite and halo lights, light caps, the picture-feedback bounce, the legacy finish).

## 1. Units
- Radiance relative to a white Lambertian surface under the stage's full light (ambient + key as rt-suns.tsv / the
  game set them): that surface reads 1 before exposure.
- Sun: irradiance over pi = `sun_light` (rgb); its disc 0.533 deg across. Its direction is the game's BrightSun where
  the sky shows it (a sunset sun on the horizon leaves the ground in shadow); a key light (no sun in the sky) leans to
  at most 65 degrees up so characters' shadows show.
- The game's point lights (its lamps: the Octoombas' bulbs) and the Comet Observatory's star core: sphere lights at
  the game's falloff, colour x weight x n.l x h^2 / (h^2 + d^2) (the on-axis light of a disc of radius h, the light's
  half distance), shadows traced to the hemisphere of the light's sphere (lamp 60 units, the core's ball) facing the
  surface. Sampled with the emitting triangles by power (4 pi^2 h^2 L).
- Sky: radiance per direction from the traced sky map (octahedral, 64x64, linear); its SH-9 for hit shading.
- Emitters: radiance L = table colour (linear) x table intensity, or the draw's TEV output x intensity
  ("what it shows is what it sends"). Power pi L A (one-sided), 2 pi L A (two-sided).
- Exposure: stage exposure (CPU, from the stage light) x eye adaptation (GPU, eased), applied once where scene light
  enters the HDR scene EFB (composite, emission pass). The camera's tone curve is the only compression. The
  adaptation meters the light the surfaces receive centre-weighted (the picture's middle, where the player is, four
  times), as a camera meters its subject: dark stages stay dark with the player readable.
- Glows: a table light source's own model (indexed positions) shows its radiance times the exposure; the game's
  sprites (halos, flares, fire cards) stay as the game drew them for the display.
- Metals: where the game maps an environment onto a metal, its specular colour is what the material passes of that
  environment (TEV with the map white less TEV with it black), weighted by how much of its look the map carries.

## 2. Scene (kept)
| Data | Producer | Layout |
|---|---|---|
| Triangles (eye space, this frame) | GeometryCS | g_positions 36 B/tri |
| Vertex normals, draw flags + draw index | GeometryCS | g_surfaces 16 B/tri |
| UVs, vertex colours | GeometryCS | g_tri_uv 72 B/tri |
| BVH: scene (mask 1), late occluders, water, sky | build_tlas | TLAS |
| Draw materials | add_draw | engine material records (sec. 4) |
| TEV programs for hit colour | note_tev | g_tev 128 B + stages; rt_tev.hlsli |
| Textures, de-light maps | texture_view, delight_view | SM 6.6 heap |
| Emissive triangles + power CDF | EmitTriangleCS, EmitScanCS, GlowEmitterCS | g_emit_tris |
| Traced sky map + SH | SkyProbeCS, SkyTraceShCS | sky buffers |
| Stage sun / sky / core | rt-suns.tsv, game key | CPU constants |
| Water surfaces, refraction, caustics | RtReflections::trace_water | unchanged |

## 3. Material identity
Draws are keyed by J3D material: MotionSignal::drawMaterial marks each J3DMatPacket with FNV-1a("model/material"),
the model's resource name registered when ResourceHolder loads it. rt-materials.tsv `material` column rows apply to
those surfaces alone (on top of their texture's row). Texture rows remain for particles and untextured stages.
Every light source is an identity row with its emitting surface; the game's fake light geometry (halo shells,
beam cones, lens glows) is `hide` under ray tracing. Row tokens for identities: `own` (the row alone, not on top of
its texture's), `traced` (clear glass the engine traces, E5b; its game draw, a painted reflection, not shown), `seen`
(a light source's surface at its radiance whose light is the game's point light there). port/tools/material_names.py
lists every model's materials, identities and textures from the disc (port/tables/material-names.tsv).

## 4. Engine material record (per draw, 32 B)
| Word | Content |
|---|---|
| 0 | class (opaque, metal, glass, crystal, emitter, foliage card, star surface) 4b, flags (two-sided, no shadow, alpha test, de-lit) 12b, roughness unorm8, metallic unorm8 |
| 1 | emission: intensity (half) and kind (none, surface, volume) |
| 2 | emission colour RGBA8 (linear after decode) |
| 3 | TEV record index (hits), de-light map view |
| 4-7 | material texture view, alpha map view + alpha_from, colour source (vertex / register), spare |

## 5. Frame
All passes cs_6_6 inline ray queries, one engine root signature: root constants (frame constants index), root CBV
(frame constants), TLAS; every buffer and texture by descriptor-heap index (an engine descriptor block per frame
slot, rewritten each frame: no root-signature limits).

| # | Pass | Resolution | Output |
|---|---|---|---|
| E1 | GBufferCS | full | normal (oct16x2), face normal, albedo (linear, de-lit: EFB colour x exp(-painted shading)), roughness, metallic, class, emission, eye z, draw id |
| E2 | DirectInitialCS | full | the sun's disc (one direction, a shadow ray; its specular lobe at least the disc wide); ReSTIR DI: 4 (8 high) light candidates (emissive triangles and the game's point lights by power), temporal reuse (reprojected, M cap 20 x candidates, depth / normal checks) |
| E3 | DirectSpatialCS | full | 4 spatial neighbours (30 px, normal > 0.9, depth 10%), final shadow ray, diffuse irradiance and specular (GGX) of the kept light; visibility reuse (occluded: W 0, M kept); reservoir stored in world space |
| E4 | IndirectCS | half (one pixel per 2x2, rotating) | cosine ray; hit: emission + albedo x (sun with a shadow ray + one emitter by RIS with a shadow ray + sky SH); miss: sky radiance. ReSTIR GI temporal + spatial reuse (sample point, normal, radiance; Jacobian for the spatial) |
| E5 | SpecularCS | full | GGX VNDF ray, hit shaded as E4 (emission, sun, lights by RIS, sky ray) |
| E5b | ClearCS (the water's trace) | full | the water window's glass and crystal by their own geometry (closest-hit traversal of the water acceleration structure): up to two thin walls along the camera ray (front, far side), each R = 2F/(1+F), T = (1-F)/(1+F) of the exact dielectric Fresnel (n 1.5), a traced GGX reflection and the sun's highlight; ClearCompositePS with the water's composite: picture behind x T + R, exposed. A `traced` material's game draw is hidden but still captured |
| E6 | DenoiseTemporalCS | full | diffuse (demodulated) and specular histories, reprojection by depth / normal / draw id, moments, history length; variance clipping of specular history |
| E7 | DenoiseAtrousCS x4 | full | variance-guided a-trous (steps 1, 2, 4, 8): depth plane, normal^64, luminance / sqrt(variance); half-res GI joins through a joint bilateral upsample in the first step; a reflection spreads no wider than its lobe (none below roughness 0.05, full from 0.3) |
| E8 | CompositePS | full | albedo x diffuse + specular + emission, x exposure, sRGB-encoded into the HDR scene EFB |
| E9 | Emission (camera) | full | glow-volume emitters drawn at radiance (rt_camera.hlsl) |
| E10 | Camera | full | adaptation from the scene's log luminance, bloom, tone curve |

Translucent draws after the composite read the diffuse irradiance as the light texture (the existing interface).
Sub-frames re-run E8 per sub-frame with reprojection from their traced slot (kLightSlots results kept: diffuse,
specular, eye z, camera).

Robustness: every non-opaque traversal weighs at most 256 candidates, then takes the one it is at as its hit (layered
alpha cards, Dusty Dune's hourglass sand, ran a dispatch past the GPU watchdog); no ray is traced with an empty span.
A depth clear through a flattened viewport restarts the traced scene only before its trace (Good Egg clears the depth
late: the restart closed the water window untraced).

## 6. Algorithms
- BRDF: Lambert + GGX (Smith height-correlated visibility, Schlick Fresnel, F0 = lerp(0.04, albedo, metallic)).
- RIS target: luminance of unshadowed BRDF x Le x G. Candidate pdf: power / total x 1 / area; the sun with probability
  P_sun = min(0.5, sun share of the unshadowed target estimate).
- Temporal reservoir combination: biased 1/M with geometry checks (RTXDI default); visibility reuse keeps M.
- ReSTIR GI: sample = (hit position, hit normal, outgoing radiance); spatial reuse with the solid-angle Jacobian and
  a shadow ray for the reused sample.
- Denoiser: SVGF (Schied 2017) with ReLAX's history clamping for specular; history length cap 32 (diffuse), 16
  (specular).
- Russian roulette / clamps: none on the primary estimators; a firefly clamp only on history input (5 sigma).

## 7. Deleted when E1-E10 pass verification
TraceCS, TemporalCS, SpatialA/M/BCS, the art composite path, CaptureCS's picture feedback, GlowEmitCS / AuthoredEmitCS
/ EmitMergeCS (art glow lights), BakedFit, the night model, local exposure / shafts / grade / rim / haze of
rt_post.hlsl, RtReflections for opaque surfaces and clear glass (water, mirror glass and metal coats stay),
GALAXY_RT_* switches of the removed paths. The game's point lights and the star core stay: they are the game's own
light sources (dropping them in build 39 left the Octoombas' lamps unlit, owner report 10-20).

## 8. Budget (reference GPU, 2x EFB 1280x1056, 60 fps)
G-buffer 0.3 ms, DI 1.2 ms, GI 1.5 ms, specular 1.0 ms, denoise 1.0 ms, composite + camera 0.4 ms: 5.4 ms.

## 9. Verification
1. Converged reference: ReferenceCS accumulation (sun + emitters + sky, 4 bounces) vs the engine, per region mean
   within 5% at the Observatory spawn, Terrace, Fountain, Good Egg, Honeyhive, Battlerock, Beach Bowl.
2. Pictures vs the game and the previous build, hidden runs, frozen bins, rt-images.
3. Temporal stability: frame-to-frame luminance noise in static views.
4. GALAXY_RT_PASS_TIMING against the budget.
5. Sub-frame replay (interpolation on), DLSS/FSR, every resolution and aspect.

## 10. Then
De-baking audit of every galaxy (identity rows: painted light removed, painted shadows kept only where geometry
cannot cast them); light inventory per galaxy (every lamp, crystal, lava, Luma, star as geometry emitters; halos
and beams hidden); volumetric scattering for beams; per-galaxy sweep closing the board.
