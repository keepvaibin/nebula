# Ray tracing engine (v2)

One physically based renderer for the traced picture. Scene light is radiance in the stage's units; light comes only
from emitting geometry, the sun and the sky; the scene renders into an HDR target and a camera brings it to the
display. It replaces the art model and its physical retrofit (the light multiplier relative to the game's painted
look, sprite and halo lights, light caps, the picture-feedback bounce, the legacy finish).

## 1. Units
- Radiance relative to a white Lambertian surface under the stage's full light (ambient + key as rt-suns.tsv / the
  game set them): that surface reads 1 before exposure.
- Sun: irradiance over pi = `sun_light` (rgb); its disc the real sun's, 0.27 deg in radius (crisp shadows). Its
  direction is the compass direction of the game's BrightSun where the sky shows it, at least 30 degrees over the
  player's horizon (the ground under the player, by the game's gravity): a painted sun often stands below the
  playfield's horizon (Hurry-Scurry's 17 degrees under its platforms, Good Egg's behind the planet: no sunlight
  reached them). The shafts come from the disc the sky shows. Any other light (a key, the game's stage key light
  where there is no sun, the baked-light fit) stands 50 to 65 degrees up: an enclosed playfield lit across (Bowser's
  Star Reactor's stage key stood 7 degrees up, half of every floor in the tube's shadow), characters' shadows beside
  them. The hold is applied to the final direction in eye space against the player's up.
- The game's point lights (its lamps: the Octoombas' bulbs) and the Comet Observatory's star core: sphere lights at
  the game's falloff, colour x weight x n.l x h^2 / (h^2 + d^2) (the on-axis light of a disc of radius h, the light's
  half distance), shadows traced to the hemisphere of the light's sphere (lamp 60 units, the core's ball) facing the
  surface. Sampled with the emitting triangles by power (4 pi^2 h^2 L).
- Sky: radiance per direction from the traced sky map (octahedral, 64x64, linear); its SH-9 for hit shading. The light
  it sends onto surfaces keeps 0.4 of the backdrop's chroma (a painted sky is far more saturated than a sky's light:
  the game's blue holds 10-15 x its red in linear light, a clear sky's light 2-3 x); reflections show it as drawn.
- Emitters: radiance L = table colour (linear) x table intensity, or the draw's TEV output x intensity. A light source
  (intensity 1 or more: Lumas, lamps and electricity 6-8, star bits 3) lights the scene at that radiance; its own
  surface is shown at most 1.5 x white so its colour survives the tone curve. A faint glow (intensity below 1: coins,
  star chips, Boos, glowing flowers) is a surface lit, shaded and reflecting as any other, its glow (colour x intensity)
  added, and sends that glow as light. Power pi L A (one-sided), 2 pi L A (two-sided). A particle texture the table
  names a light source, drawn as opaque geometry, is traced (the fire bars' cores).
- Exposure: stage exposure (CPU, from the stage light) x eye adaptation (GPU, eased), applied once where scene light
  enters the HDR scene EFB (composite, emission pass). The camera's tone curve is the only compression. The
  adaptation meters the light the surfaces receive centre-weighted (the picture's middle, where the player is, four
  times) to a fifth of the full light, as a camera meters its subject, at most 4 x (a night stays darker than a day),
  with highlight priority: no further than shows the brightest tenth of the surfaces at the stage's full light
  (a log2 histogram of 16 buckets); sunlit ground keeps its colour instead of entering the tone curve's shoulder.
- Glows: a table light source's own model (indexed positions) shows its radiance times the exposure; the game's
  sprites (halos, flares, fire cards) stay as the game drew them for the display, except a `recolour` light source's:
  shown in the table's colour at the sprite's peak (the Observatory's lamp orbs, tinted pale blue by the game, warm
  white). Glow sprites a table names a light source are area lights from their drawn triangles (GlowEmitterCS).
- Metals: where the game maps an environment onto a metal, its specular colour is what the material passes of that
  environment (TEV with the map white less TEV with it black), weighted by how much of its look the map carries; in
  the hue of its own colour where that is a coloured metal's (saturated: gold, copper), neutral for a grey metal's
  cast. A metal of a colour no metal has (saturated, hue outside 8-60 degrees: teal, blue, green, olive, violet, pure
  red) is paint over metal: a dielectric, its colour its albedo, F0 0.04, a gloss lacquer (roughness at most 0.2).
- Clear solids (crystal, glass, gems): the pixel is the game's refraction of the lit scene behind, shown at 1 - F
  (F0 0.08, a dense crystal's) plus the traced reflection; they stop 0.6 of the light of the sun and the lights behind
  them (a partial shadow, hashed per ray).

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
| E1 | GBufferCS | full | normal (oct16x2), face normal, albedo (linear, de-lit: EFB colour x exp(-painted shading), the painted reflection out by its luminance share (its tint kept), past 1 scaled by its brightest channel: hue kept), roughness, metallic, class, emission (only inside the table's glowing region of an atlas), eye z, draw id |
| E2 | DirectInitialCS | full | the sun's disc (one direction, a shadow ray; its specular lobe at least the disc wide); ReSTIR DI: 8 (16 high) light candidates (emissive triangles and the game's point lights by power), temporal reuse (reprojected, M cap 20 x candidates, depth / normal checks); the reservoir keeps the light's area (its near-field bound on reuse) |
| E3 | DirectSpatialCS | full | 4 spatial neighbours (30 px, normal > 0.9, depth 10%), final shadow ray, diffuse irradiance and specular (GGX) of the kept light; visibility reuse (occluded: W 0, M kept); reservoir stored in world space |
| E4 | IndirectCS | half (one pixel per 2x2, rotating) | cosine ray; hit: emission + albedo x (sun with a shadow ray + one emitter by RIS with a shadow ray + sky SH), or, where the picture showed the hit when its slot last ran, its whole diffuse light then (a radiance cache: every bounce after the first; GALAXY_RT_ENGINE_BOUNCES); miss: sky radiance. ReSTIR GI temporal + spatial reuse (sample point, normal, radiance; Jacobian for the spatial, neighbours past tenfold rejected). The temporal reservoirs are the history; the spatial ones live one frame (fed back, their biased gain compounded into white blobs: Beach Bowl, the Garden doorway, b40-b75) |
| E5 | SpecularCS | full | GGX VNDF ray, hit shaded as E4 (emission, sun, lights by RIS, sky ray) |
| E5b | ClearCS (the water's trace) | full | the water window's glass and crystal by their own geometry (closest-hit traversal of the water acceleration structure): up to two thin walls along the camera ray (front, far side), each R = 2F/(1+F), T = (1-F)/(1+F) of the exact dielectric Fresnel (n 1.5), a traced GGX reflection and the sun's highlight; ClearCompositePS with the water's composite: picture behind x T + R, exposed. A `traced` material's game draw is hidden but still captured |
| E6 | DenoiseTemporalCS | full | diffuse (demodulated) and specular histories, reprojection by depth / normal / draw id, moments, history length; variance clipping of specular history; a new sample past twice its brightest neighbour lowered to that (fireflies; lowered to the neighbours themselves, the first pixels to find a small light never entered the history) |
| E7 | DenoiseAtrousCS x4 | full | variance-guided a-trous (steps 1, 2, 4, 8): depth plane, normal^p (p 2 at one frame of history to 64 from eight: moving characters), luminance / sqrt(variance); half-res GI joins through a joint bilateral upsample in the first step; a reflection spreads no wider than its lobe (none below roughness 0.05, full from 0.3) |
| E8 | CompositePS | full | albedo x diffuse + specular + emission, x exposure, sRGB-encoded into the HDR scene EFB |
| E9 | The game's additive draws | full | drawn as the game drew them (gain 1): halos, sparks, number plates, flames are display effects; their light reaches the scene as emitting geometry (raised to radiance x exposure, b40-b71, whole halos saturated into white blobs) |
| E10 | Camera | full | adaptation from the scene's log luminance, bloom, sun shafts (screen-space radial gather from a visible sun, quarter resolution, weighted by Henyey-Greenstein g 0.76 forward scattering of the pixel's angle from the sun and the air in front of its surface, 1 - exp(-d / sky distance); samples above 8 clamped), tone curve |

Translucent draws after the composite read the diffuse irradiance as the light texture (the existing interface).
Sub-frames re-run E8 per sub-frame with reprojection from their traced slot (kLightSlots results kept: diffuse,
specular, eye z, camera).

The water's trace runs renderer frames after the trace (a game frame spans several): the draws' tables (draw table,
texture table, TEV records) it and the glass pass read are uploaded again into its own frame's ring
(refresh_draw_tables); read from the trace's ring, since reused, they hung the GPU under Dusty Dune's crystals.

Emitting triangles: nearer than its own size (d^2 < A / pi) a triangle lights a point as its near field (at most about
its radiance), not 1 / d^2.

Robustness: every non-opaque traversal weighs at most 64 candidates, then takes the one it is at as its hit (layered
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
- Russian roulette / clamps: none on the primary estimators; firefly rejection on the history input (a sample past
  twice its 8 neighbours' brightest lowered to that).
- Visible surface: the camera ray's triangle within the raster's depth; else the candidate nearest that depth (the
  raster drew a surface there): an alpha-tested light source a few units off was no draw at all.

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
