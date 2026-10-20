# Ray tracing engine (v2)

One physically based renderer for the traced picture. Scene light is radiance in the stage's units; light comes only
from emitting geometry, the sun and the sky; the scene renders into an HDR target and a camera brings it to the
display. It replaces the art model and its physical retrofit (the light multiplier relative to the game's painted
look, sprite and halo lights, game point lights, light caps, the picture-feedback bounce, the legacy finish).

## 1. Units
- Radiance relative to a white Lambertian surface under the stage's full light (ambient + key as rt-suns.tsv / the
  game set them): that surface reads 1 before exposure.
- Sun: irradiance over pi = `sun_light` (rgb); its disc 0.533 deg across.
- Sky: radiance per direction from the traced sky map (octahedral, 64x64, linear); its SH-9 for hit shading.
- Emitters: radiance L = table colour (linear) x table intensity, or the draw's TEV output x intensity
  ("what it shows is what it sends"). Power pi L A (one-sided), 2 pi L A (two-sided).
- Exposure: stage exposure (CPU, from the stage light) x eye adaptation (GPU, eased), applied once where scene light
  enters the HDR scene EFB (composite, emission pass). The camera's tone curve is the only compression.

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
beam cones, lens glows) is `hide` under ray tracing.

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
| E2 | DirectInitialCS | full | ReSTIR DI: 8 light candidates (emissive triangles by power, the sun's disc), temporal reuse (reprojected, M cap 20 x candidates, depth / normal / draw checks) |
| E3 | DirectSpatialCS | full | 4 spatial neighbours (30 px, normal > 0.9, depth 10%), final shadow ray, diffuse irradiance and specular (GGX) of the kept light; visibility reuse (occluded: W 0, M kept); reservoir stored in world space |
| E4 | IndirectCS | half (one pixel per 2x2, rotating) | cosine ray; hit: emission + albedo x (sun with a shadow ray + one emitter by RIS with a shadow ray + sky SH); miss: sky radiance. ReSTIR GI temporal + spatial reuse (sample point, normal, radiance; Jacobian for the spatial) |
| E5 | SpecularCS | full where roughness < 0.25 or metal / glass, else half | GGX VNDF ray, hit shaded as E4; glass also its transmitted ray |
| E6 | DenoiseTemporalCS | full | diffuse (demodulated) and specular histories, reprojection by depth / normal / draw id, moments, history length; variance clipping of specular history |
| E7 | DenoiseAtrousCS x4 | full | variance-guided a-trous (steps 1, 2, 4, 8): depth plane, normal^64, luminance / sqrt(variance); half-res GI joins through a joint bilateral upsample in the first step |
| E8 | CompositePS | full | albedo x diffuse + specular + emission, x exposure, sRGB-encoded into the HDR scene EFB |
| E9 | Emission (camera) | full | glow-volume emitters drawn at radiance (rt_camera.hlsl) |
| E10 | Camera | full | adaptation from the scene's log luminance, bloom, tone curve |

Translucent draws after the composite read the diffuse irradiance as the light texture (the existing interface).
Sub-frames re-run E8 per sub-frame with reprojection from their traced slot (kLightSlots results kept: diffuse,
specular, eye z, camera).

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
/ EmitMergeCS (art glow lights), point-light table and game point lights, StarSphereCS's core light (the core's
geometry emits), BakedFit, the night model, local exposure / shafts / grade / rim / haze of rt_post.hlsl, RtReflections
for opaque surfaces (water and translucent glass stay), GALAXY_RT_* switches of the removed paths.

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
