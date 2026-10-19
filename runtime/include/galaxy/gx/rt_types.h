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
// With kRtDrawGlow: a black hole's lens draw (the material table's `lens`): its triangles give the
// finish the hole's centre and size on screen (rt_post.hlsl PostLensCS), it lights nothing.
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
// RtDrawInfo.surface only: an alpha-tested opaque draw outside the foliage table (wood panels, rocks, bells,
// Gearmos: tests that cut little or nothing), traced whole; each ray's hit counts where its texture's alpha
// passes the draw's test (RtDrawInfo.alpha; rt_lighting.hlsl alpha_test). Cards carry the same test.
inline constexpr std::uint32_t kRtDrawAlphaTest = 1u << 17u;
// Bits 8-11: the draw's surface class (rt_surfaces.h).
inline constexpr std::uint32_t kRtDrawClassShift = 8u;
inline constexpr std::uint32_t kRtDrawClassMask = 0xFu << kRtDrawClassShift;
// RtDrawInfo.phase of the draws after the screen capture (motion::ScenePhase::AfterIndirect): the indirect-
// textured models (Rosalina, glassy NPCs) come after the trace. Their opaque draws are the late geometry.
inline constexpr std::uint32_t kRtLatePhase = 8u;

// A texture a draw samples: its view in the texture cache's CPU descriptor heap (D3D12_CPU_DESCRIPTOR_HANDLE::ptr) and
// its resource (ID3D12Resource; null: none). Opaque here so the Vulkan build, which traces nothing yet, shares the type.
struct RtTextureSource {
    std::uintptr_t view = 0;
    void* resource = nullptr;
};

// Diagnostics for GALAXY_RT_DUMP: the draw's GX projection (A-F), viewport depth range, cull
// mode and SceneFunction phase; colour channel 0 (XF control word, ambient and material RGBA8)
// and the first two lights of its mask (index, eye-space position and direction, RGBA8).
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
    // Its material texture: high quality's reflections sample it where they hit the draw off screen.
    RtTextureSource texture{};
    // An alpha-tested draw (kRtDrawAlphaTest, kRtDrawCard): its alpha map and the alpha its test passes from
    // (1-255; 0: none, solid).
    RtTextureSource alpha{};
    std::uint32_t alpha_from = 0u;
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
};

}  // namespace galaxy::gx
