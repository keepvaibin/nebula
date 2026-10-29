// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What both GX renderers share (renderer_d3d12.h, vk/renderer_vk.h): frame and ring counts, the EFB's logical
// size, one resolved draw, clear values, the EFB conversion shader and its constants, and the sampler-table cache.

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/gpu_types.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx_d3d12.h"  // EfbPeekKind

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace galaxy::gx {

inline constexpr unsigned kFramesInFlight = 2;
// Matrix-ring segments in temporal mode (see RendererD3D12::begin_frame):
// two more than the frames in flight.
inline constexpr unsigned kRingSegments = kFramesInFlight + 2u;
// Interpolation mode: one segment of every ring, the descriptor ring and the
// sampler heaps per game frame, for the frame being recorded, the completed
// log waiting for its present and the log whose sub-frames play. A segment
// is reused only after its fence (the oldest log is then invalidated).
inline constexpr unsigned kInterpSegments = 3u;
inline constexpr unsigned kSamplerHeapSlots = 2048;

inline constexpr unsigned kEfbWidth = 640;
inline constexpr unsigned kEfbHeight = 528;
// The game's frame within it (its render mode's EFB height; the display copy's source).
inline constexpr unsigned kGameFrameHeight = 456;

struct SamplerTableKey {
    std::array<std::uint32_t, 8> samplers{};
    std::uint32_t config = 0;
    bool operator==(const SamplerTableKey&) const = default;
};
struct SamplerTableKeyHasher {
    [[nodiscard]] std::size_t operator()(const SamplerTableKey& key) const noexcept;
};
// The render settings sampler tables depend on (anisotropy, LOD bias).
[[nodiscard]] std::uint32_t sampler_config_key(const RenderConfig& cfg) noexcept;
[[nodiscard]] SamplerTableKey make_sampler_table_key(
    const std::uint32_t keys[8], const RenderConfig& cfg) noexcept;

// One cache describes one physical sampler heap. reset(config) starts a new
// frame only after that heap's GPU fence completes. Unchanged tables survive;
// every lookup pins its index for this frame, including pending draw batches.
// At capacity, only a table not used this frame may be replaced. Thus history
// cannot exhaust a legal frame, and no recorded draw's descriptors are changed.
// A replacement reports created=true so the caller writes all eight samplers.
// Configuration changes and unconditional reset invalidate all retained tables.
class SamplerTableCache {
public:
    struct Allocation { unsigned index; bool created; };
    [[nodiscard]] Allocation get_or_allocate(const SamplerTableKey& key);
    void reset(std::uint32_t config_key) noexcept {
        if (config_key != config_key_ || frame_ == UINT64_MAX) {
            reset();
            config_key_ = config_key;
        }
        ++frame_;
    }
    // Unconditional clear for teardown paths, where the heap contents must not
    // be assumed valid again.
    void reset() noexcept {
        tables_.clear();
        cursor_ = 8u;
        config_key_ = 0u;
        frame_ = 0u;
        eviction_cursor_ = 0u;
    }
private:
    std::unordered_map<SamplerTableKey, unsigned, SamplerTableKeyHasher> tables_;
    unsigned cursor_ = 8u;
    std::uint32_t config_key_ = 0u;
    static constexpr unsigned kDynamicTables = (kSamplerHeapSlots - 8u) / 8u;
    // A bounded clock scan avoids repeatedly scanning all previously pinned
    // entries for each miss. Reverse keys allow exception-safe map replacement.
    std::array<SamplerTableKey, kDynamicTables> slot_keys_{};
    std::array<std::uint64_t, kDynamicTables> slot_frames_{};
    std::uint64_t frame_ = 0u;
    unsigned eviction_cursor_ = 0u;
};

// A stripe particle's vertex in the draw logged next (JPA stripes, two vertices per particle): interpolated per
// particle between game frames (RendererD3D12::note_stripe_vertices).
struct StripeVertex {
    std::uint32_t vertex = 0;        // in the draw's vertex buffer
    std::uint32_t particle = 0;      // JPABaseParticle
    std::uint32_t age_lifetime = 0;  // age << 16 | lifetime
    std::uint8_t strip = 0;          // JPADrawStripe 0, JPADrawStripeX passes 1-2
    std::uint8_t side = 0;           // the particle's first or second vertex
};

struct EfbClearValues {
    std::array<float, 4> color{};
    float reversed_depth = 0.0f;
};
[[nodiscard]] EfbClearValues make_efb_clear_values(
    std::uint32_t ar, std::uint32_t gb, std::uint32_t z24,
    std::uint8_t pixel_format) noexcept;
// With an EFB pixel format (0-2), clear values as given, not quantized to the format: the 8-bit values a draw stores
// (alpha 255 on a format without alpha).
inline constexpr std::uint8_t kEfbClearExact = 0x80u;

// The EFB conversion shader's root constants (16 dwords).
struct EfbConversionShaderConstants {
    std::array<float, 16> values{};
};

[[nodiscard]] std::string_view efb_conversion_shader_source() noexcept;
// A display copy one EFB pixel to one XFB pixel without gamma (efb_conversion_shader_source's arithmetic for it).
[[nodiscard]] std::string_view display_filter_shader_source() noexcept;

// The renderers' utility shaders (HLSL; the Vulkan backend annotates and compiles the same source): the
// fullscreen-triangle blit, the EFB colour-channel clear, the depth carry after a resolution change, and the
// overlay's textured and solid quads (root constants / push constants b0).
[[nodiscard]] std::string_view blit_vertex_shader_source() noexcept;
[[nodiscard]] std::string_view blit_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view clear_pixel_shader_source() noexcept;
// Depth clear as a draw (Vulkan drivers whose vkCmdClearAttachments races with draws): writes clear_value.x.
[[nodiscard]] std::string_view clear_depth_pixel_shader_source() noexcept;
// The depth carry by texel loads, for a depth format the device cannot filter linearly.
[[nodiscard]] std::string_view depth_carry_load_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view depth_carry_pixel_shader_source() noexcept;
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
[[nodiscard]] EfbConversionShaderConstants make_efb_conversion_constants(
    const EfbCopyParams& params,
    bool is_depth_copy,
    bool intensity,
    bool keep_alpha,
    std::uint8_t copy_format,
    float src_x,
    float src_y,
    float step_x,
    float step_y,
    float filter_row_offset,
    unsigned efb_scale,
    unsigned logical_efb_height,
    unsigned reduction_width = 0u) noexcept;

// The 1x1 GXPeek conversion constants. Color peeks box-reduce the complete
// scaled logical-pixel block; depth peeks address the exact point-selected
// physical texel recorded in EfbPeekSamplingGeometry.
[[nodiscard]] EfbConversionShaderConstants make_efb_peek_conversion_constants(
    const EfbPeekSamplingGeometry& sampling,
    EfbPeekKind kind) noexcept;

// The EFB-copy destination cache's byte budget, 192 MiB. The cache grows with the internal resolution squared while
// the budget stays fixed, so it is the one resolution-scaled GPU pool that is neither a render target nor visible in
// a recording.
[[nodiscard]] std::uint64_t efb_copy_dest_budget_bytes();

// Effect copies (destinations up to 320x228: the bloom pyramid, simple bloom, depth of field, Mario's 160x160
// shadow capture) are built at the game's own resolution, box-filtered over the scaled EFB. Their low resolution is
// part of the effect: the blur kernels use fixed screen-relative tap offsets and rely on it to soften, so scaled
// copies turn the taps into separate offset images. Full-size captures stay scaled. After Dolphin's Super Mario
// Galaxy Native Bloom mod (iwubcode, GPL-2.0-or-later).
[[nodiscard]] bool is_effect_copy(unsigned width, unsigned height) noexcept;

// Present pacing with vsync off and a frame-rate cap. The VI paces presents; the pacer smooths one that arrives
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
    // A high-resolution waitable timer (Windows 10 1803+), so a wait sleeps instead of spinning on
    // SwitchToThread. Null: Sleep and yield.
    void* timer_ = nullptr;
    bool timer_tried_ = false;
    // Sleeps toward a target `remaining_ms` away, waking kPaceSpinMs early; yields when closer.
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
    gpu::Address matrix_palette = 0; // root SRV t0,space1
    gpu::VertexView vertex_view{};
    gpu::IndexView index_view{};
    std::uint32_t index_count = 0;
    gpu::Topology topology = gpu::kTopologyTriangleList;
    gpu::Rect scissor{};
    // Raw XF viewport registers 0x101A-0x101F in hardware order:
    // [0]=wd (w/2), [1]=ht (-h/2), [2]=zRange ((f-n)*(2^24-1)),
    // [3]=xOrig (cx+342), [4]=yOrig (cy+342), [5]=farZ (f*(2^24-1)).
    // has_viewport=false (XF regs not yet written) falls back to full EFB.
    float xf_viewport[6]{};
    bool  has_viewport = false;
    // Render target 1 in temporal scenes: 1 the motion target (opaque
    // motion writers), 2 the reactive mask (blended draws), 0 none.
    std::uint8_t aux_target = 0;
    // Interpolated redraw: the scene transform record whose VS constants a
    // sub-frame replaces, -1 for draws outside the 3D scene.
    std::int32_t replay_record = -1;
    // Sub-frames only: the matching draw's vertices in the frame before,
    // blended with this draw's (root SRV t1, space1); 0 for none. The pair's
    // GPU verdict (same geometry or not) is at `blend_slot`.
    gpu::Address blend_vertices = 0;
    std::uint32_t blend_slot = 0;
    // Interpolated redraw: the scene's pixel-constant record, -1 for none.
    std::int32_t ps_record = -1;
    // A split curved surface (flat_pipeline set): its first `flat_index_count` indices are triangles no corner bends,
    // drawn as a triangle list with `flat_pipeline` (the same shaders without the patch stages), the rest as patches
    // with `pipeline`.
    gpu::Pipeline* flat_pipeline = nullptr;
    std::uint32_t flat_index_count = 0;
    // The split draw's place in its command list's curve sort (the renderer's sort_curved), -1 for none: its
    // triangles the hull shader would leave whole are drawn as a triangle list too. Interpolated redraws draw the
    // split as above.
    std::int32_t curve_sort = -1;
    std::uint32_t curve_sort_list = 0;
};

}  // namespace galaxy::gx
