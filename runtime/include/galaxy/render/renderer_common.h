// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What both renderers share (renderer_d3d12.h, vk/renderer_vk.h): frame and ring counts, the scene target's logical
// size, one resolved draw, clear values, the scene target conversion shader and its constants, and the sampler-table cache.

#include <nebula_game_api.h>
#include <nebula_limits.h>
#include "galaxy/render/texture_sampler.h"
#include "galaxy/render/frame_pass.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace galaxy::render {

using ScreenProbe = ::NebulaScreenProbe;
// Probe tiles fit every supported device; batches and readbacks grow independently.
inline constexpr std::uint32_t kScreenProbeTile = 256u;
// Textures one draw can read.
inline constexpr std::uint32_t kTableTextures = NEBULA_MATERIAL_SLOTS;

// A scene target kind, as a pipeline is built for it: log2 samples, then these bits.
inline constexpr std::uint8_t kSceneVariantSamplesMask = 3u;
inline constexpr std::uint8_t kSceneVariantHdr = 4u;
inline constexpr std::uint8_t kSceneVariantTarget = 8u;

// The picture channel each sampled channel reads (0-3: R, G, B, A) for a target pass's NebulaTargetChannels
// (E-07): RGBA, INTENSITY_ALPHA (R R R A), ALPHA (A A A A), RED, GREEN, BLUE (that channel in all four), RGB
// (alpha one: 4).
[[nodiscard]] constexpr std::array<std::uint8_t, 4> target_channel_swizzle(std::uint8_t channels) noexcept {
    constexpr std::array<std::array<std::uint8_t, 4>, 7> kSwizzle{{
        {0, 1, 2, 3}, {0, 0, 0, 3}, {3, 3, 3, 3}, {0, 0, 0, 0}, {1, 1, 1, 1}, {2, 2, 2, 2}, {0, 1, 2, 4}}};
    return kSwizzle[channels < kSwizzle.size() ? channels : 0u];
}

inline constexpr unsigned kFramesInFlight = 2;
// The vertex and matrix rings' segments: a frame reads the rows and vertices of the walk before it.
inline constexpr unsigned kRingSegments = kFramesInFlight + 2u;
inline constexpr std::size_t kVertexRingBytes = 32u * 1024u * 1024u;
inline constexpr std::size_t kIndexRingBytes = 12u * 1024u * 1024u;
inline constexpr std::size_t kConstantRingBytes = 12u * 1024u * 1024u;
inline constexpr std::size_t kMatrixRingBytes = 32u * 1024u * 1024u;
inline constexpr unsigned kSamplerHeapSlots = 2048;
inline constexpr unsigned kSamplerTableSlots = NEBULA_MATERIAL_SLOTS;

struct SamplerTableKey {
    std::array<SamplerKey, kSamplerTableSlots> samplers{};
    std::uint32_t count = 0;
    std::uint32_t config = 0;
    bool operator==(const SamplerTableKey&) const = default;
};
struct SamplerTableKeyHasher {
    [[nodiscard]] std::size_t operator()(const SamplerTableKey& key) const noexcept;
};
// The render settings sampler tables depend on (anisotropy, LOD bias).
[[nodiscard]] std::uint32_t sampler_config_key(const RenderConfig& cfg) noexcept;
[[nodiscard]] SamplerTableKey make_sampler_table_key(const std::array<SamplerKey, kSamplerTableSlots>& keys, unsigned count,
                                                     const RenderConfig& cfg) noexcept;

// The tables written in one sampler heap: each takes only the slots it uses. Written once; the renderer starts again between
// frames, with the GPU idle, when the heap is nearly full.
class SamplerTableCache {
public:
    struct Allocation { unsigned index; bool created; };
    [[nodiscard]] Allocation get_or_allocate(const SamplerTableKey& key);
    [[nodiscard]] bool nearly_full() const noexcept { return cursor_ + kSamplerHeapSlots / 4u > kSamplerHeapSlots; }
    void reset() noexcept {
        tables_.clear();
        cursor_ = kSamplerTableSlots;
    }
private:
    std::unordered_map<SamplerTableKey, unsigned, SamplerTableKeyHasher> tables_;
    unsigned cursor_ = kSamplerTableSlots;
};


struct SceneClearValues {
    std::array<float, 4> color{};
    float reversed_depth = 0.0f;
};
// A clear's values as given; on a frame without alpha (frame_format 0, 1 with alpha) alpha 255.
[[nodiscard]] SceneClearValues make_scene_clear_values(
    std::uint32_t rgba,
    std::uint8_t frame_format) noexcept;

// The scene target conversion shader's root constants (16 dwords).
struct SceneConversionShaderConstants {
    std::array<float, 16> values{};
};

[[nodiscard]] std::string_view scene_conversion_shader_source() noexcept;
// The frame shown, one frame pixel to one picture pixel (scene_conversion_shader_source's arithmetic for it).
[[nodiscard]] std::string_view display_filter_shader_source() noexcept;

// The renderers' utility shaders (HLSL; the Vulkan backend annotates and compiles the same source): the
// fullscreen-triangle blit, the scene target colour-channel clear, the depth carry after a resolution change, and the
// overlay's textured and solid quads (root constants / push constants b0).
[[nodiscard]] std::string_view depth_resolve_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view motion_debug_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view blit_vertex_shader_source() noexcept;
[[nodiscard]] std::string_view blit_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view clear_pixel_shader_source() noexcept;
// Depth clear as a draw (Vulkan drivers whose vkCmdClearAttachments races with draws): writes clear_value.x.
[[nodiscard]] std::string_view clear_depth_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view ui_textured_quad_vertex_shader_source() noexcept;
[[nodiscard]] std::string_view ui_textured_quad_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view ui_solid_quad_vertex_shader_source() noexcept;
[[nodiscard]] std::string_view ui_solid_quad_pixel_shader_source() noexcept;
// The solid quad's constants; glyph mode draws a 5x7 bitmap as instanced cells.
struct UiSolidQuadConstants {
    std::int32_t rect[4];
    float color[4];
    std::uint32_t screen[2];
    std::uint32_t glyph_mask[2];
    std::uint32_t glyph_mode;
};
static_assert(sizeof(UiSolidQuadConstants) == 13u * sizeof(std::uint32_t));
[[nodiscard]] SceneConversionShaderConstants make_scene_conversion_constants(
    bool from_depth,
    std::uint8_t channels,
    float src_x,
    float src_y,
    float step_x,
    float step_y) noexcept;

// A screen probe's constants: the capture shader reads the one pixel of the frame the point names (`depth`: as the
// 24-bit value in three bytes, otherwise as it is).
[[nodiscard]] SceneConversionShaderConstants make_screen_probe_constants(float x, float y, const FrameMap& map, bool depth) noexcept;

// The scene target-copy destination cache's byte budget, 192 MiB. The cache grows with the internal resolution squared while
// the budget stays fixed, so it is the one resolution-scaled GPU pool that is neither a render target nor visible in
// a recording.

// Effect copies (destinations up to 320x228: the bloom pyramid, simple bloom, depth of field, Mario's 160x160
// shadow capture) are built at the game's own resolution, box-filtered over the scaled scene target. Their low resolution is
// part of the effect: the blur kernels use fixed screen-relative tap offsets and rely on it to soften, so scaled
// copies turn the taps into separate offset images. Full-size captures stay scaled. After Super Mario

// Present pacing with vsync off and a frame-rate cap. The game's frame clock paces presents; the pacer smooths one that arrives
// early by waiting at most 2 ms toward its slot at the cap: a high-resolution sleep to 0.2 ms before the target,
// then yields.
class PresentPacer {
public:
    PresentPacer() noexcept;
    ~PresentPacer();
    PresentPacer(const PresentPacer&) = delete;
    PresentPacer& operator=(const PresentPacer&) = delete;
    // Waits for this present's turn; the milliseconds waited.
    double pace(unsigned vsync_interval, float max_fps);
    void reset() noexcept;

private:
    std::int64_t frequency_ = 0;
    std::int64_t last_qpc_ = 0;
    // A high-resolution waitable timer (Windows 10 1803+). Null: Sleep.
    void* timer_ = nullptr;
    bool timer_tried_ = false;
    // Sleeps until a target `remaining_ms` away.
    void sleep_before(double remaining_ms);
};

// One fully resolved draw. The renderer sets the pipeline and root arguments
// only where they differ from the previous draw.
struct DrawCall {
    gpu::Pipeline* pipeline = nullptr;
    gpu::Address vs_constants = 0;   // root CBV b0
    gpu::Address ps_constants = 0;   // root CBV b1
    gpu::Table texture_table{};      // t0-t7
    gpu::Table sampler_table{};      // s0-s7
    gpu::Address matrix_palette = 0; // root SRV t0,space1: the draw's row table
    gpu::Address matrix_rows = 0;    // root SRV t3,space1: the rows its table names (the matrix ring, a replay's output)
    gpu::VertexView vertex_view{};
    gpu::VertexView extra_view{};  // RenderVertexExtra; no address: zeros
    gpu::IndexView index_view{};
    std::uint32_t index_count = 0;
    gpu::Topology topology = gpu::kTopologyTriangleList;
    gpu::Rect scissor{};
    // The game's viewport (drawing_state.h: left, top, width, height, near and far depth); has_viewport=false (not set
    // yet, or empty) falls back to the full frame.
    float viewport[6]{};
    bool  has_viewport = false;
    // Render target 1 in temporal scenes: 1 the motion target (opaque
    // motion writers), 2 the reactive mask (blended draws), 0 none.
    std::uint8_t aux_target = 0;
};

}  // namespace galaxy::render
