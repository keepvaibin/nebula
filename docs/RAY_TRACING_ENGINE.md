# Ray tracing engine (v2)

One physically based renderer for the traced picture. Scene light is radiance in the stage's units; light comes only
from emitting geometry, the sun and the sky; the scene renders into an HDR target and a camera brings it to the
display. It replaces the art model and its physical retrofit (the light multiplier relative to the game's painted
look, sprite and halo lights, light caps, the picture-feedback bounce, the legacy finish), which are deleted: ray
tracing is the engine under physical light or nothing. It needs shader model 6.6 and resource binding tier 3 (the
lighting is unavailable without them); a scene without the game's stage light (it sets one each frame a stage draws)
is not traced and shows the game's picture.

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
  names a light source, drawn as opaque geometry, is traced (the fire bars' cores). An emitting triangle's radiance is
  its TEV output's mean over seven points (97752e9 lost the division: every model light source lit the scene at seven
  times its stated radiance until b90). A light source shows its emitted colour (the TEV output at full light, or the
  table's for `recolour`) times its radiance past the composite's albedo, so an environment-mapped lamp keeps its
  colour; a faint glow adds its glow in the game's painted colours of it (a coin's gradient and embossed ?).
  `near-glow`: a material emits only within 1.25 x the size of a glow light source's light (the game's glow sprite of
  it): the Observatory's entrance stars share one material and one draw with their gold border.
- Exposure: stage exposure (CPU, from the stage light) x eye adaptation (GPU, eased), applied once where scene light
  enters the HDR scene EFB (composite, emission pass). The camera's tone curve is the only compression. The
  adaptation meters the light the surfaces receive centre-weighted (the picture's middle, where the player is, four
  times) to a fifth of the full light, as a camera meters its subject, at most 4 x (a night stays darker than a day),
  with highlight priority: no further than shows the brightest tenth of the surfaces at the stage's full light
  (a log2 histogram of 16 buckets); sunlit ground keeps its colour instead of entering the tone curve's shoulder.
- Glows: a table light source's own model (indexed positions) shows its radiance times the exposure; the game's
  sprites (halos, flares, fire cards) stay as the game drew them for the display, except a `recolour` light source's:
  shown in the table's colour at the sprite's peak (the Observatory's lamp orbs, tinted pale blue by the game, warm
  white). Glow sprites a table names a light source are area lights from their drawn triangles (GlowEmitterCS); a
  `tint` one emits what it draws: its TEV output over each triangle (16 points, textures filtered over each point's
  share), linear, times what its source blend factor passes (an alpha-blended flame its colour over its alpha's
  coverage), times the table's radiance, so a fire's light follows the game's colour keys and fading. A `volume` glow
  (a glowing volume the game draws as geometry: the Dreadnought's beam-go-round flame beams) emits as its sprites do,
  every way from each triangle, whatever its faces' normals; as a lit surface its ribbons sent their light only away
  from the disc under them.
- Effects: the game's particle effects by name. Each emitter's marker is followed by its effect's (motion::
  kMarkerEffect: "Effect/<ParticleNames.bcsv name>", the resource's user index in Effect.arc), and a table row with
  `effect` in the hash column names effects in its material column; its properties hold for the effect's particles over
  their textures' (particle textures are shared: mr_glow00_ia by 130 effects, a meteor's impact and an apple's alike).
  Incandescent effects (fires, fireballs, explosions, sparks, fireworks, the meteors; not their smoke, steam, heat
  haze or dust) emit their drawn colour (`emitter+tint`), each group's brightest drawable colour at a large fire's
  luminance: 1.38 x a white surface under a full light of 100,000 lx (a hydrocarbon fireball's peak surface emissive
  power, 450 kW/m2, at emissivity 1: 1678 K; Planck x CIE 1931). That colour comes from the effect archive (the
  port's tools/fire_rows.py): JPA's TEV stage 0 (JPABaseShape::st_ca, over prm and env and their colour keys) with a
  texel of 1 bounds what a particle can draw; the row's intensity is 1.38 over its luminance. A meteor's impact is a
  hypervelocity impact flash, not a fire: measured on the Moon at 1,300-5,800 K (NELIOTA's two-band flashes, 85 %
  2,000-4,500 K) and at 2,500-5,000 K in the laboratory; its group is taken at the measured range's cool end, 1,850 K
  (5.36 x full light), which lights the ground under it to about twice the full light at the flash without washing it
  out ([rt-light-meter] below). The burning rock itself (MeteorStrike/rock, a meteoroid melting in flight) is at the
  same 1,850 K at its brightest drawn point (0.815 at intensity 1): 6.58; mostly dark crust with glowing cracks, it
  sends little until it lands. The meteors: trail 1.49 (Meteorite02's yellow at birth), impact 5.62
  (MeteoriteBreak00's white-yellow debris). The fire textures' own rows emit their drawn colour at 1: smoke drawn with
  them (black smoke, extinguished fire) sends its own dark colour, a blue fire blue.
- Metals: where the game maps an environment onto a metal, its specular colour is what the material passes of that
  environment (TEV with the map white less TEV with it black), weighted by how much of its look the map carries; in
  the hue of its own colour where that is a coloured metal's (saturated: gold, copper), neutral for a grey metal's
  cast. A metal of a colour no metal has (saturated, hue outside 8-60 degrees: teal, blue, green, olive, violet, pure
  red) is paint over metal: a dielectric, its colour its albedo, F0 0.04, a gloss lacquer (roughness at most 0.2).
- Under water: the sun and the sky reach a point under water through last frame's water surfaces (the water pass's
  acceleration structure, reprojected; a near-level surface above the point, not a waterfall): attenuated by clear ocean
  water's diffuse attenuation at half (Jerlov I Kd 0.35, 0.065, 0.019 per metre, halved: the game's water is clearer
  than the sea), 35 % of its hue shift kept (the rest dims evenly: at the sea's own the lagoons went teal and their
  depths cyan-blue), the sun's disc widened 0.5 degrees per metre by forward scattering. The water pass adds caustics,
  shafts and a veil (its colour 40 % toward grey) while the camera is under water. Seen from below, the surface mirrors
  the water under it by the water-to-air Fresnel (n 1.333), all of it past the critical angle (Snell's window).
- The camera in water (NEBULA_CONTEXT_CAMERA_WATER: the game's WaterAreaFunction::getCameraWaterDepth each frame):
  what the camera sees, and what a bounce or reflection ray meets in the water (engine_water_ray), is seen through the
  water between, its beam attenuation c (the same clear water's 0.36, 0.08, 0.03 per metre, halved) with the water's
  own light added in the same share: a seventh of the stage's full light (near the surface the horizontal radiance lies
  between the upwelling and the downwelling: Mobley, Ocean Optics Web Book), in coastal water's colour (single-
  scattering albedo 0.30, 0.71, 0.70: pure water's absorption, Pope and Fry 1997, with the dissolved organic matter of
  Jerlov's coastal types), dimmed by the water above the camera at Kd. The water carries the stage's light into its
  caves as it is lit from above. The game's own filter (WaterCameraFilter: red and green lowered with the depth) still
  follows the composite.
- Fog: each traced draw's GX fog (type, A, B magnitude and shift, C, colour: the draw table's words 12-15).
  EngineGBufferCS evaluates the raster's share at the pixel as the hardware does (ze = A 2^24 / (B - z >> shift) in
  perspective, A z / 2^24 orthographic; f = saturate(ze - C) through the exponential types; 1/256 steps). The composite
  takes the fog out of the game's colour, (c - fc f) / (1 - f), lights that, and lays the fog over the lit surface in
  the same share at the camera's exposure: fogged distances keep the game's fog over physical light. Multiplied with
  the light as part of the colour, Deep Dark's water cave went black with it.
- Procedural oceans (OceanBowl, OceanSphere, OceanRing: Beach Bowl's sea, Drip Drop's sphere, Sea Slide's ring) are
  water to the water pass as the models' surfaces are, though drawn with direct vertices (rt_ocean_water_draw): the sun
  had reached Beach Bowl's sea floor 33 metres down unattenuated.
- Curved surfaces (Phong tessellation, curved_surfaces.h) are drawn rounded while the scene holds their flat
  triangles. A pixel's rays start from the drawn point, lifted above its traced face where the drawn surface lies under
  it (an inward bend: a beach, a sea floor, a dome's rims, by up to tens of units; primary_surface's negative lift), else
  by the shading normals' lift less what the drawn point already stands above the face (surface_start). From under its
  face every ray met the surface's own triangle: black blotches of self-shadow on Beach Bowl's sand and sea floor and on
  Ghostly Galaxy's dome.
- Water from above (every translucent water the water pass sees: lagoons, seas, the Observatory's moat and the Fountain
  dome's pool) reflects by its Fresnel, F0 0.02: 2 % looking straight down, 6 % at 60 degrees from the normal, 21 % at
  75, 61 % at 85, a mirror at grazing angles; the reflection takes the place of that share of the water's own light
  (cover 1). F0 0.2-0.5 by quality with cover 0.3-0.6 (builds 55-102) laid a third to a half of the bright
  surroundings over water seen from above: the near lagoon turned milky (b103).
- Texture de-lighting takes out half of each texel's measured shading (GALAXY_RT_DELIGHT_STRENGTH 0.5) and turns it
  into relief at 0.5 (GALAXY_RT_DELIGHT_RELIEF): Retinex cannot tell painted light from a pattern of one colour.
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
those surfaces alone (on top of their texture's row); with a `stage` as well, on that stage alone (the Observatory's
torches: its CommonGlow00 effect there is a warm lamp, elsewhere a Boo's or a jellyfish's glow; a texture's stage row
had made every draw of the shared glow texture a lamp there, the Lumas' glows and the star plates' silver among them).
Texture rows remain for particles and untextured stages.
Every light source is an identity row with its emitting surface; the game's fake light geometry (halo shells,
beam cones, lens glows) is `hide` under ray tracing. Row tokens for identities: `own` (the row alone, not on top of
its texture's), `traced` (clear glass the engine traces, E5b; its game draw, a painted reflection, not shown), `solid`
(with `traced`: a closed clear solid, the crystal cages, traced through from the scene's own geometry, E5b), `seen`
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
| E1b | LightGridCS | 24^3 cells of 256 units about the camera, 64 slots each | grid-based light reservoirs (ReGIR: Boksansky, Jukarainen and Wyman, Ray Tracing Gems II, 2021): each slot resamples 8 lights drawn by power by their light at the cell (power over the squared distance from its centre, the cell's half diagonal added); a surface in a cell draws its light candidates from its slots, each weighed by its slot's resampling weight (chained RIS), outside the grid by power. Drawn by power alone, the scene's biggest lights (the star core, a lava sea) took the candidates wherever a surface was, and a lamp, laser or fireball beside it went unfound |
| E2 | DirectInitialCS | full | the sun's disc (one direction: interleaved gradient noise across pixels, the R2 sequence across frames; a shadow ray; kept as the penumbra its blocker makes, 0.5 d tan(radius), and the water its light crosses); ReSTIR DI: 8 (16 high) light candidates (emissive triangles and the game's point lights, from the surface's light grid cell, E1b), temporal reuse (reprojected, M cap 20 x candidates, depth / normal checks) through the light remap (below): a reservoir keeps its light and the point on it, and its sample is rebuilt from the light as this frame has it; then the boiling filter (below) |
| E3 | DirectSpatialCS | full | 4 spatial neighbours (30 px at 1056 lines: footprints below; normal > 0.9, depth 10%) combined by the generalized balance heuristic (below), final shadow ray, diffuse irradiance and specular (GGX) of the kept light; an occluded sample stays in its reservoir; reservoir stored as its light, the point on it (a triangle's barycentrics, a sphere's world direction), W and M: 16 bytes |
| E3b | ShadowBlurCS, ShadowPostCS, ShadowStabilizeCS | full | the sun's visibility (after NVIDIA's SIGMA): two blurs, each a dense 5 x 5 estimating the penumbra about the pixel (occluded neighbours', the smaller preferred) and 12 taps on a disc of its radius on screen (at most 32 px at 1056 lines, narrowed on slanted surfaces, turned per frame), on the pixel's plane, a smaller penumbra than the estimate counting less; then a history of at most 7 frames (weight at most 5/6) clamped to the 5 x 5 neighbourhood's mean +- its deviation (x 1 to 3 as the history grows), shortened by how far the clamp moved it. Contact shadows stay sharp, far blockers' soft; a moving caster leaves no trail |
| E4 | IndirectCS | full at High (GALAXY_RT_ENGINE_GI_FULL), else half (one pixel per 2x2, rotating) | cosine ray; hit: emission + albedo x (sun with a shadow ray + one emitter by RIS with a shadow ray + sky SH), or, where the picture showed the hit when its slot last ran, its whole diffuse light then (a radiance cache: every bounce after the first; GALAXY_RT_ENGINE_BOUNCES); miss: sky radiance. ReSTIR GI temporal reuse and the boiling filter, then spatial reuse (sample point, normal, radiance; within 10 half-pixels at 1056 lines; Jacobian for the spatial, neighbours past tenfold rejected). The temporal reservoirs are the history; the spatial ones live one frame (fed back, their biased gain compounded into white blobs: Beach Bowl, the Garden doorway, b40-b75) |
| E5 | SpecularCS | full | GGX VNDF ray, hit shaded as E4 (emission, sun, lights by RIS, sky ray) |
| E5c | CacheCS | 1M cells | the world's radiance cache (below): each cell's new samples from E4's and E5's hits onto its light |
| E5b | ClearCS (the water's trace) | full | the water window's glass and crystal by their own geometry (closest-hit traversal of the water acceleration structure): up to two thin walls along the camera ray (front, far side), each R = 2F/(1+F), T = (1-F)/(1+F) of the exact dielectric Fresnel (n 1.5), a traced GGX reflection and the sun's highlight; ClearCompositePS with the water's composite: picture behind x T + R, exposed. A `traced` material's game draw is hidden but still captured. A `traced solid` (the crystal cages: the game draws them as a screen capture bent by an indirect map, which can only show what is behind them on screen) stays in the scene for the other rays and is no camera ray's surface; here it is a closed dielectric (its row's index, `ior<n>`, else quartz's 1.544; the crystal cages take moissanite's 2.65: 20.7 % reflected at normal incidence, where quartz's 4.6 % mirrored almost nothing beside them; RTX Remix's translucent material without thin walls, Whitted's recursion as in Unity HDRP's recursive rendering): the front face's Fresnel reflection and sun highlight, the refracted ray followed inside up to 16 segments, each far face passing its Fresnel share out (none past the critical angle: 40.4 degrees for quartz, 22.2 for moissanite) and reflecting the rest on, what is still inside then leaving as the light that left (total internal reflection holds some rays for many faces: at 8 segments about 1 % of a crystal's light on average, 20 % at worst, was lost), the row's colour as the transmittance after its range (Beer-Lambert); the first share out is the picture where the picture shows what it meets (its pixel kept in the record), the others shaded as the frame lit their hits (g_final) or as a glass reflection's. Records are 32 B: R, T per channel, eye z, the pixel seen through |
| E6 | DenoiseTemporalCS | full | diffuse (demodulated: the lights' and the bounce; the sun's is E3b's) and specular histories, reprojection by depth / normal / draw id (the raster's motion where a surface moved: below), moments, history length; variance clipping of specular history; anti-lag of the diffuse history (a 4-frame fast history of its luminance against the moments' mean: past 1.5 x the frames' deviation the history shortens, to 4 frames at most); a new sample past twice its brightest neighbour lowered to that (fireflies; lowered to the neighbours themselves, the first pixels to find a small light never entered the history) |
| E7 | DenoiseAtrousCS x4 + log2(lines / 1056) | full | variance-guided a-trous (steps 1, 2, 4, 8, 16 at 2112 lines): depth plane, normal^p (p 2 at one frame of history to 64 from eight: moving characters), luminance / sqrt(variance); half-res GI joins through a joint bilateral upsample in the first step; a reflection spreads no wider than its lobe (none below roughness 0.05, full from 0.3) |
| E7b | FinalCS | full | the sun's diffuse and specular light by E3b's visibility (its facing from the disc's centre, the water's tint) onto the a-trous result; the slot's result at the exposure |
| E8 | CompositePS | full | albedo x diffuse + specular + emission, x exposure, sRGB-encoded into the HDR scene EFB |
| E9 | The game's additive draws | full | drawn as the game drew them (gain 1): halos, sparks, number plates, flames are display effects; their light reaches the scene as emitting geometry (raised to radiance x exposure, b40-b71, whole halos saturated into white blobs) |
| E10 | Camera | full | adaptation from the scene's log luminance (up to 4x for dim views; highlight priority both ways, down to half: the brightest tenth of surfaces no brighter than the stage's own light on a surface facing it, as the camera's stage exposure shows it), a stage sun at 1.25 x the stage's key (GALAXY_RT_SUN_STRENGTH) with the camera exposed for the stage's own light, bloom, sun shafts (screen-space radial gather from a visible sun, quarter resolution, weighted by Henyey-Greenstein g 0.76 forward scattering of the pixel's angle from the sun and the air in front of its surface, 1 - exp(-d / sky distance); samples above 8 clamped), tone curve |

Translucent draws after the composite read the diffuse irradiance as the light texture (the existing interface).
Sub-frames re-run E8 per sub-frame with reprojection from their traced slot (kLightSlots results kept: diffuse,
specular, eye z, camera).

The water's trace runs renderer frames after the trace (a game frame spans several): the draws' tables (draw table,
texture table, TEV records) it and the glass pass read are uploaded again into its own frame's ring
(refresh_draw_tables); read from the trace's ring, since reused, they hung the GPU under Dusty Dune's crystals.
The water pass makes its own samples' buffers (RtReflections::ensure_buffers without the opaque pass's): the opaque
pass made them before the engine took its place, and from then to b103 the water pass refused every frame (no water
reflections, mirror glass or metal coats; GALAXY_RT_LOG_SEQ prints each step and refusal).

Emitting triangles: nearer than its own size (d^2 < A / pi) a triangle lights a point as its near field (at most about
its radiance), not 1 / d^2.

Robustness: every non-opaque traversal weighs at most 64 candidates, then takes the one it is at as its hit (layered
alpha cards, Dusty Dune's hourglass sand, ran a dispatch past the GPU watchdog); no ray is traced with an empty span.
A depth clear through a flattened viewport restarts the traced scene only before its trace (Good Egg clears the depth
late: the restart closed the water window untraced).

Player choices (Graphics, RAY TRACING; `render_config.h` `rt_effect_on`, read each frame): REFLECTIONS (E5 and E5b,
`SceneRayTracer::surface_reflections_active`; off, the composite keeps each surface's painted reflection in its albedo
and metals take diffuse light, so they show the game's own reflection, and glass and crystal are the game's draws
again), WATER REFLECTIONS (the water draws join the water window, `GxBackend::rt_water_draw`; off, the game's water),
SUN RAYS and LENS BLOOM (E10's shafts and pyramid, independent). There is no switch for the bounce light: E4 also
carries the sky's light, which needs a fallback before it can be turned off. Tests: GALAXY_TEST_RT_EFFECT_AB turns one
off in frames that end in an odd present, so at 60 fps consecutive presents of one run show it on and off.

## 6. Algorithms
- BRDF: Lambert + GGX (Smith height-correlated visibility, Schlick Fresnel, F0 = lerp(0.04, albedo, metallic)).
- RIS target: luminance of unshadowed BRDF x Le x G. Candidate pdf: in a light grid cell 1 / (the slot's weight x area),
  outside the grid power / total x 1 / area. The sun is apart (E3b).
- Reservoir combination (direct light, temporal and spatial): the generalized balance heuristic (resampled importance
  sampling with multiple importance weights, Lin et al. 2022; RTXDI's unbiased mode without visibility; our own code):
  each reservoir's sample is weighed by its own pixel's count-weighted share of all the combined pixels' targets for
  it (the last frame's target at that surface then, carried into this frame's eye space; neighbours' at their
  G-buffer points), and the result's W is the weight sum over its target. Combined by 1/M (RTXDI's biased mode) with
  an occluded sample's reservoir emptied but counted, the direct light read 36 % darker than the converged reference
  in the Observatory's still night (each pixel's candidates come from its own light grid cell, each frame's slots
  drawn anew, so the combined pixels' samples cover different lights; the spatial result fed the next frame's
  temporal reuse): now 7.6 % (the whole engine 2.6 %, against 32 %), its error 18 % where the reference's own
  estimator reads 16.7 %. Visibility shades the kept sample only. GALAXY_RT_ENGINE_DI_SPATIAL=0,
  GALAXY_RT_ENGINE_LIGHT_HISTORY=0 and GALAXY_RT_ENGINE_FIREFLY=0 take the reuse and the history's firefly clamp out
  for such measurements.
- Indirect light at every pixel at High: one cosine ray a pixel a frame (half resolution: one pixel of each 2 x 2,
  rotating, which an animated character's pixels, with little history, showed as sparkle on his sleeves).
- Light identity (RTXDI's light index mapping, RAB_TranslateLightIndex): every traced draw carries a key from the game's
  markers (the object, model shape, emitter or particle drawing it, a particle by its pool node and lifetime; its J3D
  material; its draws since), the point lights theirs (the game's light info). Per trace the lights are grouped by draw
  key (the scene's emitting draws, the glow emitters the last frame wrote, the point lights), and the last traced
  frame's groups map onto this trace's index for index (kEvLightRemap; a key two draws share maps nothing). Temporal
  reuse reads a reservoir's light through it: a light that moved carries its samples along, one no longer drawn ends
  its history. Kept as a point and a radiance, a sample lit where a light had been (a meteor's flash on the ground for
  a second after it was gone, at 5 % less a frame) and trailed a moving light. GALAXY_RT_LOG_EMITTERS: [rt-remap].
- Light change (A-SVGF's temporal gradients, Schied, Peters and Dachsbacher 2018, on RTXDI's reservoirs, as its
  confidence pass does): each pixel shades the last frame's kept light sample again as this frame has its light (the
  same light point and weight: no sampling noise in the difference; gone: 0) at the surface point it was shaded at
  (that pixel's G-buffer then, carried into this frame's eye space by the inverse reprojection, as A-SVGF shades its
  gradient samples at the last frame's visibility points) against the shade the reservoir kept, and
  flags its own kept sample when its light is new (engine_light_existed: the remap's other way). Over a 5 x 5
  neighbourhood the mean of the relative change and of the new flags (the new lights' share of the light) is the light
  change; the diffuse and specular histories weigh this frame (1 - change) / length + change, and ReSTIR GI's temporal
  reservoir keeps (1 - change) of its weight (Remix's validateLightingChange drops such samples). The diffuse
  history's own anti-lag compares a fast and a slow history against their variance, which a flash inflates: after a
  meteor's impact the picture held 0.36 of the full light on the ground 150 frames after the light had gone to 0.01,
  and showed 0.7 of a 1.9 onset; with the light change, 1.79 of 1.91 and the bounce's own 0.07 two seconds later.
  Shaded at this frame's point of the pixel instead, the change read the jitter's sub-pixel step and the camera's move
  as light changing: about 1 % a frame on still ground, which holds a history near 10 frames (the Gateway's start under
  DLSS: 10.5 frames, 31.5 measured at the same surface point; without a temporal upscaler 20 to 30, now 26 to 31.6).
- World radiance cache (our implementation of path-space filtering, Binder, Fricke and Keller 2019, as NVIDIA's
  SHaRC and other engines' world-space caches use it): a bounce or reflection hit outside the picture was lit by one
  light of four candidates behind one shadow ray, one sun ray and one sky ray. In the Gateway's Grand Star room nearly
  all of the floor's light came from those single samples, most dark and a few bright: speckle that no filter at the
  floor makes clean (with the hits' light sample off the floor's bounce was smooth and dark). A hash grid over world
  space (2^20 entries of 32 B, 8 linear probes; cells about 8 pixels wide at their distance at 1056 lines, powers of
  two from 2 units, one per facing axis) gathers every hit's light, the picture's own filtered light where the picture
  shows the hit; CacheCS blends each cell's new samples into its light by their count, at least 1/16 a frame (a change
  is followed within 16 frames), and frees cells unused for 600 frames. A hit takes its cell's light times its own
  albedo once the cell has 8 samples; a reflection whose ray cone is narrower there than the cell keeps its own sample
  (sharp reflections would show the cells). A hit's sky ray that meets a surface takes that surface's cell light times
  its albedo (no sample of its own): every further bounce through the cache, converging over frames. With one bounce
  the off-picture hits had only their direct light, and the Grand Star room read 31 to 58 % darker than the converged
  reference's 8 bounces. GALAXY_RT_ENGINE_CACHE=0 turns it off.
- ReSTIR GI: sample = (hit position, hit normal, outgoing radiance); spatial reuse with the solid-angle Jacobian and
  a shadow ray for the reused sample.
- Denoiser: SVGF (Schied 2017) with ReLAX's history clamping for specular and ReBLUR's fast-history anti-lag for
  diffuse; history length cap 32 (diffuse), 16 (specular). The sun's shadow apart (E3b, after SIGMA): in the diffuse
  history (32 frames unclamped) and its a-trous it took half a second to form and to leave, and blurred to blobs.
- Russian roulette / clamps: none on the primary estimators; firefly rejection on the history input (a sample past
  twice its 8 neighbours' brightest lowered to that).
- Footprints: every pixel takes one sample whatever the resolution, so a reach in pixels covers less of a surface as
  the lines grow. The widths were set at 1056 lines (2x); at 6x (2112 lines in a 2560 x 1440 window) the spatial reuse
  and the denoiser gathered from a quarter of the surface they had at 2x, and the light stayed speckled. The direct
  and indirect spatial radii, the sun blur's widest radius and the light change's 5 x 5 spacing scale by lines / 1056
  (never below the 2x widths); the a-trous adds an iteration each time the lines double.
- Boiling filter (RTXDI's RTXDI_BoilingFilter, our implementation of its published idea): a light that matters to
  much of a surface but that the candidates rarely find is held by the one reservoir that found it, and the reuse
  spreads it as a bubble that appears, grows and fades (the Gateway's Grand Star room under its sparks). After
  temporal reuse, a reservoir whose weight sum passes 41 times the mean nonzero weight sum of its 8 x 8 group is
  emptied (strength 0.2, RTXDI's default, for direct and indirect light). GALAXY_RT_ENGINE_BOILING=0 turns it off.
- Moving surfaces: the camera's reprojection puts a surface that moved by itself (a character, a hopping Star Bunny's
  ears, a rotating platform) where something else was, so its histories failed the depth and normal checks every
  frame and it showed one frame's samples: glitter on everything that moves. The raster's motion target (every opaque
  draw's own previous position, R16G16 float, render pixels; written in every scene the engine lights, without a
  temporal upscaler too: no jitter, no upscale) is read where it differs from the camera's by more than half a pixel,
  with looser checks (10 % depth, normals 0.7). The engine's projections
  carry the upscaler's jitter (positions are rebuilt from the jittered depth) and the motion does not, so the motion
  takes the previous frame's jitter less this frame's (g_motion_jitter): without it every pixel was off by the jitter's
  step (up to 0.8 px, the Halton sequence's frame differences, measured). Still surfaces read 0.001 to 0.03 px.
- Diagnostics: GALAXY_RT_ENGINE_VIEW=14 shows the light sources' direct light alone (no sun, no bounce);
  GALAXY_RT_LIGHT_METER=x,y,r (fractions of the picture) logs per traced frame the means over that disc's surfaces of
  the light sources' direct light, the whole diffuse light as the picture takes it, the sun's, the bounce's, the
  filtered light less the sun and the diffuse history's length ([rt-light-meter], the stage's light units; frames),
  and with the raster's motion where it takes
  them against the camera's reprojection ([rt-motion-meter]: mean x, y, distance in render pixels, the share past half
  a pixel); GALAXY_RT_ENGINE_VIEW=15 shows the motion less the camera's (red x, green y, 0.5 none, 8 px to full); GALAXY_RT_LOG_INTERVAL sets the emitter logs'
  spacing (120 frames); GALAXY_D3D_DRED=1 records device-removed breadcrumbs, page faults and each ray tracing pass's
  marker (printed in order as [dred] marker n name, so a stopped list's markers name its passes).
  GALAXY_RT_DEBUG_UNTRACE=id,... keeps those table materials out of the traced scene (still drawn), with
  GALAXY_RT_DEBUG_UNTRACE_AB=1 on odd presents only: two snapshots in a row differ by what those draws add to the
  traced picture (Mario's image in a crystal). GALAXY_RT_ENGINE_CLEAR=10 shows a clear solid's light still inside
  where its path ran out of segments; GALAXY_RT_LOG_TEV=id prints that material's TEV program once.
- Visible surface: the camera ray's triangle within the raster's depth; else the candidate nearest that depth (the
  raster drew a surface there): an alpha-tested light source a few units off was no draw at all. Draws the picture
  never shows are skipped (kDrawTableUnseen): the clear solids and the trace-only draws.
- Trace-only draws (host marker kind 14): geometry in the traced scene the picture never shows. In the first-person
  view the game fades Mario out of the camera (MarioActor::calc1stPersonView: nothing drawn within 300 units), so no
  shadow or reflection of him existed; MarioActor::drawTracedBody draws his body whole between trace-only markers while
  the ray tracer lights the scene, as Remix keeps a first-person player model for its rays alone (rtx.playerModel).

## 7. Deleted (b90)
TraceCS, the art GBufferCS, TemporalCS, SpatialA/M/BCS, ReferenceCS and ReferenceExposureCS (the engine's reference
replaces them), BakedFitCS / BakedReduceCS (the baked-light fit), the glow bloom (BloomDown/Blur/PS), rt_post.hlsl (the
cinematic finish), GALAXY_RT_PHYSICAL and GALAXY_RT_ENGINE (always on), and every shader declaration only those
reached (port/tools/hlsl_reach.py: 2278 lines). Still to go: the art model's knobs and constants the kept shaders no
longer read, the old passes' buffers (light history, reservoirs: 242 MB), CaptureCS's picture feedback, GlowEmitCS /
AuthoredEmitCS / EmitMergeCS, the night model, RtReflections for opaque surfaces (skipped under the engine; the water
pass stays). The game's point lights and the star core stay: they are the game's own
light sources (dropping them in build 39 left the Octoombas' lamps unlit, owner report 10-20).

## 8. Budget (reference GPU, 2x EFB 1280x1056, 60 fps)
G-buffer 0.3 ms, DI 1.2 ms, GI 1.5 ms, specular 1.0 ms, denoise 1.0 ms, composite + camera 0.4 ms: 5.4 ms.

## 9. Verification
1. Converged reference: GALAXY_RT_ENGINE_REFERENCE=1 (from GALAXY_RT_REFERENCE_FROM, a still view): EngineReferenceCS
   traces one unbiased path a pixel a present on the engine's own G-buffer, materials and light (next-event estimation
   at every vertex, the sky through the cosine ray, Russian roulette from the third bounce, 8 bounces);
   [rt-reference-engine] prints the engine's diffuse light against it over the ordinary non-metal surfaces (bias, mean
   error). Good Egg, 4140 paths: -2.5 % bias, 6.8 % mean per-pixel difference. GALAXY_RT_ENGINE_VIEW=8 shows it.
   Diagnostic views: 1 direct, 2 indirect, 3 specular, 7 albedo, 8 reference, 9 / 10 the texture coordinate of the
   table's glowing region (u / v as 16 hues), 11 the sun's filtered visibility, 12 its ray's, 13 the penumbra's radius
   on screen; GALAXY_RT_PROBE prints [rt-probe-engine], [rt-probe-uv] and [rt-probe-sun] (what stops the sun's
   centre ray there: triangle, draw, material, instance, distance, the water above).
2. Pictures vs the game and the previous build, hidden runs, frozen bins, rt-images.
3. Temporal stability: frame-to-frame change of a still view against the game's own (port/tools/flicker.py, 60
   consecutive presents, GALAXY_PRESENT_SNAPSHOT_EVERY=1 with _LAST): Observatory plaza b93, 0.16 / 255 more than
   the game on average, 0.08 % of pixels more than 4 / 255.
4. GALAXY_RT_PASS_TIMING against the budget.
5. Sub-frame replay (interpolation on), DLSS/FSR, every resolution and aspect.

## 10. Then
De-baking audit of every galaxy (identity rows: painted light removed, painted shadows kept only where geometry
cannot cast them); light inventory per galaxy (every lamp, crystal, lava, Luma, star as geometry emitters; halos
and beams hidden); volumetric scattering for beams; per-galaxy sweep closing the board.
