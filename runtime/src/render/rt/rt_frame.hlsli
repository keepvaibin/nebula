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
                        // turn); z: bit 0 the sun is traced, bit 3 the sky's light near the surface, bit 5 mirrors (what
                        // glass, metal and gloss show of the scene), bit 6 the actors' point lights are emitters (the
                        // light set's own lamp is put out), bit 7 the emitters alone (--diagnostics rt-added: no sun, no
                        // sky), bit 9 the point's motion shown (--diagnostics rt-motion). Bits 1, 2 and 4 are retired
                        // (the set's lamp, the drops, the second layer: g_pass.x).
    uint4 g_grid;       // tiles across, tiles down, pixels a tile
    float4 g_sun;       // xyz: the direction to the shadow sun; w: the tangent of its half angle
    float4 g_stage;     // x: the stage's ambient, y: its key light, as brightness: a painted surface in the key's
                        // shadow keeps x / (x + y * n.l) of its colour; z, w: retired (the drops)
    float4 g_look;      // x: what a surface in the sun's shadow keeps of the sky's light (the sky turned down so the
                        // sun's shadows read); y 1: a painted surface is shadowed by lit things only (the painted
                        // style), else by everything solid; z: the facing (n.l) from which a painted surface keeps
                        // its authored brightness; w: how far the sky's ray looks for what stands near
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
                           // z 1: there is a frame before to read (the filters' history and the reservoirs')
};

cbuffer PassConstants : register(b1) {
    uint4 g_pass;          // x: the layer (0 the opaque surfaces, 1 the nearest blended surface a pixel); y: a filter's
                           // step in pixels; z 1: a filter's last step; w: free for the pass (the second layer's
                           // passes: bit 0, the layer has a frame before: rt_history)
    float4 g_pass_value[3];  // free for the pass
};

// The item table (rt_walk.cpp rt_tables: sixteen uints an item). Flags (word 1): bit 0 lit (its program sums the key
// at the pixel), 1 takes the game's drop shadows, 2 a surface of the scene, 3 a sheet drawn from both sides, 4 the sky
// (shown as it is); bits 8 up its light set from 1.
static const uint kItemBytes = 64u;
static const uint kItemLit = 1u, kItemGround = 2u, kItemSurface = 4u, kItemThin = 8u, kItemSky = 16u;
// Material classes (nebula_model.h NEBULA_HIT_*).
static const uint kClassGloss = 1u, kClassMetal = 2u, kClassGlass = 3u, kClassEmissive = 6u, kClassTranslucent = 8u,
                  kClassWater = 9u;

// What V raw's distance says where nothing stood on the sun's ray: the largest half float.
static const float kNoOccluder = 65504.0f;

bool rt_second_layer() {
    return g_pass.x != 0u;
}

// Whether the layer's images of the frame before hold its surfaces (the camera did not cut; the second layer was
// traced the frame before too).
bool rt_history() {
    return g_kept.z != 0u && (g_pass.x == 0u || (g_pass.w & 1u) != 0u);
}

#endif
