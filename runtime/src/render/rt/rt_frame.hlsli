// SPDX-License-Identifier: GPL-3.0-only
// The constants every pass of a frame's traced light reads (rt_walk.cpp RtFrameConstants, field for field): b0, the
// frame's, written once a frame and shared by every pass and both layers; b1, the pass's own (RtPassConstants).
#ifndef NEBULA_RT_FRAME_HLSLI
#define NEBULA_RT_FRAME_HLSLI

cbuffer FrameConstants : register(b0) {
    float4 g_ray;       // rt_eye_ray
    float4 g_depth;     // rt_eye_z
    float4 g_viewport;  // the scene's viewport in target pixels
    uint4 g_at;         // where each buffer's first byte is: items, sets, drops, tiles
    uint4 g_more;       // x: the drops' entries' first byte (in the tiles' buffer); y: the frame's number (the noise's
                        // turn); z: bit 0 the sun is traced, bit 3 retired (the sky rays: 22.10), bit 5 mirrors (what
                        // glass, metal and gloss show of the scene), bit 6 the actors' point lights are emitters (the
                        // light set's own lamp is put out), bit 7 the emitters alone (--diagnostics rt-added: no sun, no
                        // sky), bit 9 the point's motion shown (--diagnostics rt-motion), bit 12 the camera under water
                        // (the game's report: water is seen from inside), bit 13 the landing cue under the player
                        // (rt_light_space.hlsl, l80). Bits 1, 2 and 4 are retired
                        // (the set's lamp, the drops, the second layer: g_pass.x). w: ray tracing's parts as the
                        // player set them (rt_part), bit 12 the reference preset.
    uint4 g_grid;       // tiles across, tiles down, pixels a tile
    float4 g_sun;       // xyz: the direction to the shadow sun; w: the tangent of its half angle
    float4 g_stage;     // x: the stage's ambient, y: its key light, as brightness: a painted surface in the key's
                        // shadow keeps x / (x + y * n.l) of its colour; z: the game's time at the frame in seconds,
                        // wrapped at an hour (the caustics' drift); w: the drop light's tangent (kDropTan 0.035,
                        // 2 degrees): the landing cue's edge (l80)
    float4 g_look;      // x: what a surface in the sun's shadow keeps of the sky's light (the sky turned down so the
                        // sun's shadows read); y 1: a painted surface whose model's paint holds its shadows (hit
                        // flag NEBULA_HIT_PAINTED_SHADOW) is shadowed by lit things only (the painted style, 14.6),
                        // 0: every painted surface by everything solid; z: the facing (n.l) from which a painted surface keeps
                        // its authored brightness; w: the stage's water clear (units of water to mostly its colour; rt_stages.tsv,
                        // 600 by default; FB b58)
    uint4 g_glow;       // the emitters (48 bytes each: centre, radius; colour, reach; slot): x their first byte in the
                        // drops' buffer, y their tiles' and z their entries' first byte in the tiles' buffer, w how many
    uint4 g_mesh_at[3]; // where each arena's first byte is in its buffer (eight of them), then the rows'
    float4 g_sky;       // rgb: what a mirror shows where its ray meets nothing at all
    float4 g_up;        // xyz: the player's up in the eye's space (where the sky is)
    float4 g_ray_before;   // rt_eye_ray of the frame before
    float4 g_before[3];    // the frame before's eye space from this frame's (rows of a 3x4 matrix): for a pixel whose
                           // mesh the pass cannot read, only the camera's move is known
    uint4 g_kept;          // x: the first byte, in the drops' buffer, of where each emitter of the frame before is in
                           // this frame's list (a uint each; past the list: gone), then (from x + max(y, 1) x 4)
                           // where each emitter of this frame's list was in the frame before (float4 each: centre in
                           // that frame's eye space, reach; reach 0: it was not there); y: how many the frame before had;
                           // z 1: there is a frame before to read (the filters' history and the reservoirs'); w the
                           // histories' length factor (a float: rt_history_scale)
    uint4 g_clip;          // the clip areas' volumes (plan 26 M10, clip_shows): x their first byte in the items' buffer
                           // (after the items; 64 each: the rows from the eye to the unit shape, then the shape), y how
                           // many, z where the clipped map parts show: 1 inside a volume, 2 outside every one, 0 whole
    float4 g_dev[8];       // the tracer's development tunables (rt_dev.h from painted_glass, kDev* below; rt_dev_or)
    uint4 g_dev_set;       // x: a bit a g_dev slot, set where the run gave it
};

// A development tunable (rt_dev.h; FA l63): the run's value where it gave one, else the code's own number.
static const uint kDevPaintedGlass = 0u, kDevPaintedMetal = 1u, kDevPaintedGloss = 2u, kDevPaintedMirror = 3u,
                  kDevKeepMetal = 4u, kDevKeepGloss = 5u;
// The programs' keys, copied into the tail by the host (rt_walk.cpp; FB b66): sun_strength, sky_chroma, stage_light.
static const uint kDevSunStrength = 6u, kDevSkyChroma = 7u, kDevStageLight = 8u;
// The share of the dome's chroma O keeps under the relight (FB b87; rt_sky_ratio): the next slot after the shared
// three, the host's dome_chroma once it fills it (t4); unset, kSkyChroma 0.4.
static const uint kDevDomeChroma = 9u;
float rt_dev_or(uint slot, float otherwise) {
    return ((g_dev_set.x >> slot) & 1u) != 0u ? g_dev[slot >> 2u][slot & 3u] : otherwise;
}

cbuffer PassConstants : register(b1) {
    uint4 g_pass;          // x: the layer (0 the opaque surfaces, 1 the nearest blended surface a pixel); y: a filter's
                           // step in pixels; z 1: a filter's last step; w: free for the pass (the second layer's
                           // passes: bit 0, the layer has a frame before: rt_history)
    float4 g_pass_value[3];  // free for the pass
};

// The item table (rt_walk.cpp rt_tables: sixteen uints an item). Flags (word 1): bit 0 lit (its program sums the key
// at the pixel), 1 takes the game's drop shadows, 2 a surface of the scene, 3 a sheet drawn from both sides, 4 the sky
// (shown as it is); bits 8 up its light set from 1.
static const uint kItemBytes = 80u;  // rt_walk.cpp kRtItemWords
static const uint kItemLit = 1u, kItemGround = 2u, kItemSurface = 4u, kItemThin = 8u, kItemSky = 16u;
// Bit 6: a map part its clip areas show (g_clip): a ray meets it where it shows alone. Bit 7: its program alpha-tests
// (can discard): a clipped part without it is whole where it shows.
static const uint kItemClipped = 64u, kItemAlphaTested = 128u;
// A two-sided cutout (a leaf, a petal, a flower card; D5): its sun ray starts this far out, so the cards of one clump do
// not black each other out, and its far side takes this share of the sun (light through a petal or a leaf).
static const float kCardClear = 30.0f, kCardThrough = 0.6f;
// c90: a crown of leaves or a cloud (item word 14 bits 26-31: its share of the sun on its far side x 63) is lit from
// both sides by that share; its sun ray, and a card's, passes every triangle of its own item (occluder_at_but's `own`
// with kOwnWhole).
static const uint kOwnWhole = 0x80000000u;
// Material classes (nebula_model.h NEBULA_HIT_*).
static const uint kClassGloss = 1u, kClassMetal = 2u, kClassGlass = 3u, kClassEmissive = 6u, kClassTranslucent = 8u,
                  kClassWater = 9u;

// What V raw's distance says where nothing stood on the sun's ray: the largest half float.
static const float kNoOccluder = 65504.0f;

bool rt_second_layer() {
    return g_pass.x != 0u;
}

// How long the filters' histories are against 60 frames a second: clamp(frames a second / 60, 0.5, 2), so a history
// lasts as many seconds at 30 or 120 frames a second (plan 16.5).
#define NEBULA_RT_HISTORY_SCALE 1
float rt_history_scale() {
    const float scale = asfloat(g_kept.w);
    return scale > 0.0f ? scale : 1.0f;
}

// Ray tracing's parts as the player set them (plan 19.1; RtPart's order): 0 off, 1 low, 2 medium, 3 high.
static const uint kPartSun = 0u, kPartLights = 1u, kPartIndirect = 2u, kPartReflect = 3u, kPartTransmit = 4u, kPartVolume = 5u;
uint rt_part(uint part) {
    return g_more.w >> (part * 2u) & 3u;
}

// Whether the layer's images of the frame before hold its surfaces (the camera did not cut; the second layer was
// traced the frame before too).
bool rt_history() {
    return g_kept.z != 0u && (g_pass.x == 0u || (g_pass.w & 1u) != 0u);
}

#endif
