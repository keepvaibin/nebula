// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The ray tracer's per-draw records and flags (ray_tracing.h's SceneRayTracer uses them on D3D12), without any
// graphics API: gx_backend fills them for either renderer.

#include <array>
#include <cstdint>

namespace galaxy::gx {

// One draw's geometry, located in the renderer's buffers (byte offsets from each base).
struct RtDraw {
    std::uint32_t vertex_offset = 0;
    std::uint32_t flags = 0;
    // Vertex count (bits 0-23); the texture generators of the draw's two texture slots + 1 (bits 24-27 and 28-31; 0:
    // the vertices' first texture coordinates as they are).
    std::uint32_t vertex_count = 0;
    std::uint32_t index_offset = 0;
    std::uint32_t first_triangle = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t constants_offset = 0;
    std::uint32_t palette_offset = 0;
};
static_assert(sizeof(RtDraw) == 32u);
// Decoded vertex stride (GxVertexOut: position at 0, matrix indices at 120).
inline constexpr std::uint32_t kRtVertexStride = 144u;
// RtDraw.flags: these bits; surface classes in bits 8-15; the draw's index in its scene in bits
// 16-31 (SceneRayTracer::add_draw).
inline constexpr std::uint32_t kRtDrawResident = 1u;
inline constexpr std::uint32_t kRtDrawIndexed = 2u;
inline constexpr std::uint32_t kRtDrawWideIndices = 4u;
// The draw's colour is lit by GX lights (characters, lit objects): its shading already turns
// towards its lights.
inline constexpr std::uint32_t kRtDrawLit = 8u;
// A lit draw whose GX light 0 the actor lights moved towards the traced sun (actor_lights.h):
// its shading's key light is the sun.
inline constexpr std::uint32_t kRtDrawSunKey = 16u;
// It takes GX light 4, the slot LightFunction::loadPointLightInfo loads the game's point light into.
inline constexpr std::uint32_t kRtDrawPointLit = 32u;
// An additive translucent draw (a glow: lamps, cores, Lumas, sparks): an emitter, not in the BVH.
inline constexpr std::uint32_t kRtDrawGlow = 64u;
// A blended translucent draw of the water class (seas, lakes): its reflections are traced once the
// translucent phases drew it (SceneRayTracer::trace_water); not in the BVH.
inline constexpr std::uint32_t kRtDrawWater = 128u;
// A lit draw whose colour is its material alone (the enhanced path under traced lighting: the game's
// lights and ambient removed, shader_gen.cpp): the traced light is its whole light.
inline constexpr std::uint32_t kRtDrawDelit = 1u << 12u;
// A water draw the material table marks as falling water (rt_reflections.hlsl kWaterfallFlag).
inline constexpr std::uint32_t kRtDrawWaterfall = 1u << 13u;
// With kRtDrawGlow: a black hole's lens draw (the material table's `lens`): it lights nothing.
inline constexpr std::uint32_t kRtDrawLens = 1u << 14u;
// A water-pass draw only (kRtDrawWater): glass the material table marks `mirror` (rt_reflections.hlsl
// kWaterMirrorFlag). Shares the lens bit, which water draws never carry.
inline constexpr std::uint32_t kRtDrawWaterMirror = 1u << 14u;
// The material table's shadow 0 (wings, glow shells around planets, overlays): in the traced scene but
// never in the way of the sun, the sky or a light (rt_lighting.hlsl kDrawNoShadow).
inline constexpr std::uint32_t kRtDrawNoShadow = 1u << 15u;
// RtDrawInfo.surface only (above the RtDraw.flags bits): an alpha-tested foliage card, in the traced
// scene whole; add_draw marks its draw record (rt_lighting.hlsl kDrawTableCard).
inline constexpr std::uint32_t kRtDrawCard = 1u << 16u;
// A water-pass draw only (kRtDrawWater): an opaque late surface the table names water or crystal, the scene
// the trace saw rather than a layer over it (rt_reflections.hlsl kWaterOpaqueFlag). Shares the de-lit bit,
// which water draws never carry.
inline constexpr std::uint32_t kRtDrawWaterOpaque = 1u << 12u;
// A water-pass draw only (kRtDrawWater): ice (the material table's `ice`: Freezeflame's rink), reflecting as polished
// ice rather than water (rt_reflections.hlsl kWaterIceFlag). Shares the no-shadow bit, which water draws never carry.
inline constexpr std::uint32_t kRtDrawWaterIce = 1u << 15u;
// RtDrawInfo.surface only: an alpha-tested opaque draw outside the foliage table (wood panels, rocks, bells,
// Gearmos: tests that cut little or nothing), traced whole; each ray's hit counts where its texture's alpha
// passes the draw's test (RtDrawInfo.alpha; rt_lighting.hlsl alpha_test). Cards carry the same test.
inline constexpr std::uint32_t kRtDrawAlphaTest = 1u << 17u;
// RtDrawInfo.surface only: a draw of the game's sky lists (MR::DrawBufferType Sky, Air, Sun and AstroDomeSky, from its
// draw-buffer markers): the traced sky's geometry (SceneRayTracer's sky probe), never in the scene.
inline constexpr std::uint32_t kRtDrawSky = 1u << 18u;
// RtDrawInfo.surface only: a trace-only draw (motion::kMarkerTraceOnly): in the traced scene, never in the picture, so
// never a camera ray's surface; add_draw marks its draw record (rt_lighting.hlsl kDrawTableUnseen).
inline constexpr std::uint32_t kRtDrawUnseen = 1u << 19u;
// RtDraw.vertex_count: the vertex count's bits and the texture slots' generator (+ 1) shifts.
inline constexpr std::uint32_t kRtVertexCountMask = 0xFFFFFFu;
inline constexpr std::uint32_t kRtTexGenShift = 24u;
inline constexpr std::uint32_t kRtTexGen2Shift = 28u;
// Per triangle (ray_tracing.hlsl g_tri_uv): its vertices' coordinates in the draw's two texture slots (3 x 2 floats
// each), colour 0 and colour 1 (3 x RGBA8 each).
inline constexpr std::uint32_t kRtTriUvStride = 72u;

// A draw's TEV program for ray hits (rt_tev.hlsli): its stages as the uber pixel shader reads them (TevStagePacked)
// with each stage's coordinate scale (the texture coordinate's GX scale over its map's size), and one record. A stage's
// pad[0]: 1 on an environment map's stage; pad[1]: its map's addressing (GX wrap s | wrap t << 2).
inline constexpr unsigned kRtTevMaxStages = 16u;
// pad[0]: 1 on an environment map's stage. pad[1]: its map's addressing (bits 0-3: s, t), its indirect command
// (bits 8-28, BP 0x10-0x1F; 0: none) and whether the program moves coordinates by indirect textures (bit 31; then
// an RtTevIndirect follows the record's stages).
struct RtTevStage {
    std::uint32_t color_env = 0u;
    std::uint32_t alpha_env = 0u;
    std::uint32_t order = 0u;
    std::uint32_t ksel = 0u;
    float scale_s = 1.0f;
    float scale_t = 1.0f;
    std::uint32_t pad[2]{};
};
static_assert(sizeof(RtTevStage) == 32u);
// A program's indirect texturing, past its stages (rt_tev.hlsli): the indirect stages' maps and coordinates (BP 0x27,
// bits 0-23) and their count (bits 24-26); per indirect stage its coordinates' scale to its map (s, t: the coordinates'
// GX size over 2^its RAS1_SS shift over the map's size); the three indirect matrices' rows (int16 x | y << 16,
// z | shift << 16; ShaderGenerator's ind_mtx); per texture coordinate its GX size (s - 1 | t - 1 << 16).
struct RtTevIndirect {
    std::uint32_t order = 0u;
    float scale[8]{};
    std::uint32_t matrix[12]{};
    std::uint32_t coord_size[8]{};
    std::uint32_t pad[3]{};
};
inline constexpr unsigned kRtTevIndirectEntries = 4u;
static_assert(sizeof(RtTevIndirect) == kRtTevIndirectEntries * sizeof(RtTevStage));
struct RtTevMaterial {
    // Stages (bits 0-4); the vertices' colour 0 / 1 de-baked (bits 5-6); an environment map among its stages (bit 7;
    // RtTevStage.pad[0] 1 on those stages); per texture coordinate 0-7 its slot (bits 8 + 2n); colour 0, alpha 0, colour
    // 1, alpha 1 from the vertices (bits 24-27); texture generators (bits 28-31).
    std::uint32_t header = 0u;
    std::uint32_t alpha_test = 0u;   // ref0 | ref1 << 8 | comp0 << 16 | comp1 << 19 | logic << 22
    // Colour channels 0 and 1: material (RGBA8) and the light that lights it (RGBA8, white unlit).
    std::uint32_t channel[4]{};
    std::uint32_t konst[4]{};        // K0-K3, RGBA8
    std::uint32_t regs[8]{};         // PREV, C0-C2: half floats (r | g << 16, b | a << 16), value / 255
    std::uint32_t maps[8]{};         // texture views (SceneRayTracer: 0xFFFFFFFF none)
    std::uint32_t stage_offset = 0u; // SceneRayTracer: its first stage
    // pad[0]: de-baked channels' neutral light (bits 0-7, 8-15; rt_tev_eval.h). SceneRayTracer: pad[1] the material
    // texture's de-light map (its view; 0xFFFFFFFF none), pad[2] its map (bits 0-2) and texture slot (bit 3), pad[3]
    // and pad[4] that stage's coordinate scale (floats); pad[2] bits 4-7 its addressing (RtDrawInfo.texture_wrap), bits
    // 8-31 an emitter's glowing region of it (u0, v0, u1, v1 in 63rds; 0: all of it).
    std::uint32_t pad[5]{};
};
static_assert(sizeof(RtTevMaterial) == 128u);
// Bits 8-11: the draw's surface class (rt_surfaces.h).
inline constexpr std::uint32_t kRtDrawClassShift = 8u;
inline constexpr std::uint32_t kRtDrawClassMask = 0xFu << kRtDrawClassShift;
// RtDrawInfo.phase of the draws after the screen capture (motion::ScenePhase::AfterIndirect): the indirect-
// textured models (Rosalina, glassy NPCs) come after the trace. Their opaque draws are the late geometry.
inline constexpr std::uint32_t kRtLatePhase = 8u;

// A texture a draw samples: its view in the texture cache's CPU descriptor heap (D3D12_CPU_DESCRIPTOR_HANDLE::ptr) and
// its resource (ID3D12Resource; null: none), opaque here so the Vulkan build, which does not trace, shares the type.
// For texture de-lighting its texture-pack key (0: none, an EFB copy) and size.
struct RtTextureSource {
    std::uintptr_t view = 0;
    void* resource = nullptr;
    std::uint64_t pack = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t levels = 1;
};

// A draw's state beyond its geometry: its GX projection (A-F), viewport depth range, cull mode and SceneFunction
// phase; colour channel 0 (XF control word, ambient and material RGBA8) and the first two lights of its mask (index,
// eye-space position and direction, RGBA8), which GALAXY_RT_DUMP also writes out; then its surface, material and TEV.
struct RtDrawInfo {
    std::array<float, 6> projection{};
    float min_depth = 0.0f;
    float max_depth = 1.0f;
    std::uint32_t cull = 0;
    std::uint32_t phase = 0;
    std::uint32_t channel = 0;
    std::uint32_t ambient = 0;
    std::uint32_t material = 0;
    std::array<std::uint32_t, 2> light_index{0xFFu, 0xFFu};
    std::array<std::array<float, 6>, 2> light{};
    std::array<std::uint32_t, 2> light_color{};
    // Added to RtDraw.flags: kRtDrawLit, kRtDrawSunKey, kRtDrawPointLit or kRtDrawGlow; surface classes
    // in bits 8-15.
    std::uint32_t surface = 0;
    // TEV stages, texgens, indirect stages and surface features (bits 0, 8, 16, 24); the environment
    // map's colour combiner and stage (bits 24); the colour channel controls 0 and 1.
    std::array<std::uint32_t, 4> shading{};
    // Its material table entry (rt_materials.h; 0: none).
    std::uint16_t rt_material = 0;
    // Its identity from frame to frame (the object, model shape, emitter or particle drawing it, its J3D material and
    // its draws since them; GxBackend::rt_light_key), which keeps a light it emits the same light from one traced
    // frame to the next (SceneRayTracer's light remap).
    std::uint32_t light_key = 0;
    // Its material texture: high quality's reflections sample it where they hit the draw off screen; its map (0-7,
    // 0xFF: none).
    RtTextureSource texture{};
    std::uint32_t texture_map = 0xFFu;
    // Its addressing (GX TexWrap: 0 clamp, 1 repeat, 2 mirror): s (bits 0-1), t (bits 2-3).
    std::uint32_t texture_wrap = 5u;
    // A water draw whose texture the game moves by an indirect texture (its waves): that wave map and how its texels
    // become slopes (GxBackend after fill_rt_tev; rt_reflections.hlsl water_wave): flags (bit 0 waves, bits 1-3 the s t
    // u bias, bits 4-5 the format), the map's coordinate scale (slot 0 to its 0-1 coordinates), the indirect matrix's
    // direction part (s from x, s from y, t from x, t from y), the water texture's coordinate scale (slot 1).
    RtTextureSource wave{};
    std::array<float, 12> wave_params{};
    // An alpha-tested draw (kRtDrawAlphaTest, kRtDrawCard): its alpha map and the alpha its test passes from
    // (1-255; 0: none, solid).
    RtTextureSource alpha{};
    std::uint32_t alpha_from = 0u;
    // The texture generators (0-7) of its two texture slots (0xFF: the vertices' first coordinates): the material
    // texture's stage's, then the next one its stages read (GxBackend::fill_rt_tev).
    std::uint32_t texgen = 0xFFu;
    std::uint32_t texgen2 = 0xFFu;
    // Its TEV program for ray hits, and the textures its maps hold.
    RtTevMaterial tev{};
    std::array<RtTevStage, kRtTevMaxStages> tev_stages{};
    RtTevIndirect tev_indirect{};
    std::array<RtTextureSource, 8> tev_maps{};
    // kRtDrawSky, kRtDrawGlow: how the raster blends it: blending (bit 0), the source and destination factors (GX
    // BlendFactor, bits 1-3 and 4-6), subtract (bit 7), colour writes (bit 8), a logic op (bit 9).
    std::uint32_t blend = 0u;
    // Its GX fog as its pixel shader applies it (ShaderGenerator: BP 0xEE-0xF2 and the key's fog type): A (float
    // bits), B's magnitude (bits 0-23) | its shift << 24 | the type (FogType) << 29, C (float bits), the colour RGB8
    // (0xRRGGBB) | orthographic << 24. Type 0: no fog.
    std::array<std::uint32_t, 4> fog{};
};

// The scene camera, for rays through EFB pixels and depth reconstruction: GX projection A-D,
// the D3D projection's [2][2] and [2][3], the viewport in EFB pixels and its depth range.
struct RtView {
    std::array<float, 4> projection{};
    float z_scale = 0.0f;
    float z_offset = 0.0f;
    std::array<float, 4> viewport{};
    float min_depth = 0.0f;
    float max_depth = 1.0f;
    // The temporal upscaler's jitter in the projection (render pixels: x right, y down; 0 without it).
    std::array<float, 2> jitter{};
};

}  // namespace galaxy::gx
