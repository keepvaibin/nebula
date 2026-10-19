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
// Ring segments in temporal and interpolation modes (see RendererD3D12::
// begin_frame): two more than the frames in flight.
inline constexpr unsigned kRingSegments = kFramesInFlight + 2u;
inline constexpr unsigned kSamplerHeapSlots = 2048;

inline constexpr unsigned kEfbWidth = 640;
inline constexpr unsigned kEfbHeight = 528;

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

struct EfbClearValues {
    std::array<float, 4> color{};
    float reversed_depth = 0.0f;
};
[[nodiscard]] EfbClearValues make_efb_clear_values(
    std::uint32_t ar, std::uint32_t gb, std::uint32_t z24,
    std::uint8_t pixel_format) noexcept;

// Exact 16-dword root-constant payload consumed by the EFB conversion shader.
// Exposed so device-only validation can exercise the production shader and
// the production clamp/coefficient setup without creating a game window.
struct EfbConversionShaderConstants {
    std::array<float, 16> values{};
};

[[nodiscard]] std::string_view efb_conversion_shader_source() noexcept;

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

// Builds the production 1x1 GXPeek conversion payload. Color peeks box-reduce
// the complete scaled logical-pixel block; depth peeks address the exact
// point-selected physical texel recorded in EfbPeekSamplingGeometry.
[[nodiscard]] EfbConversionShaderConstants make_efb_peek_conversion_constants(
    const EfbPeekSamplingGeometry& sampling,
    EfbPeekKind kind) noexcept;

// A byte budget from a megabyte count in `variable` (digits only), else `default_bytes`; zero means the default when
// `zero_uses_default`.
[[nodiscard]] std::uint64_t read_megabyte_budget(
    const char* variable, std::uint64_t default_bytes, bool zero_uses_default,
    std::uint64_t maximum_bytes = std::numeric_limits<std::uint64_t>::max());
// The EFB-copy destination cache's byte budget (GALAXY_EFB_COPY_DEST_BUDGET_MB, 192 MiB): it scales with the internal
// resolution squared while the budget stays fixed, so it is the one resolution-scaled GPU pool that is neither a
// render target nor visible in a recording.
[[nodiscard]] std::uint64_t efb_copy_dest_budget_bytes();

// Effect copies (destinations up to 320x228: the bloom pyramid, simple bloom, depth of field, Mario's 160x160
// shadow capture) are built at the game's own resolution, box-filtered over the scaled EFB. Their low resolution is
// part of the effect: the blur kernels use fixed screen-relative tap offsets and rely on it to soften, so scaled
// copies turn the taps into separate offset images. Full-size captures stay scaled.
// GALAXY_NATIVE_EFFECT_COPIES=0 keeps only the three bloom workspace copies native when
// GALAXY_NATIVE_BLOOM_WORKSPACE=1 (after Dolphin's Super Mario Galaxy Native Bloom mod, iwubcode, GPL-2.0-or-later).
struct EffectCopyPolicy {
    bool native_effect_copies = true;
    bool native_bloom_workspace = false;
};
[[nodiscard]] EffectCopyPolicy effect_copy_policy() noexcept;  // environment, read once
[[nodiscard]] bool is_effect_copy(const EffectCopyPolicy& policy, const EfbCopyParams& params, bool is_depth_copy,
                                  unsigned width, unsigned height) noexcept;

// Present pacing (environment, read once): the VI paces presents in real time (GALAXY_REALTIME_VI, on), with a
// bounded smoothing wait before each VI-paced present (GALAXY_SMOOTH_REALTIME_PRESENT, on;
// GALAXY_SMOOTH_REALTIME_PRESENT_MAX_WAIT_US, 2000, at most 8000).
[[nodiscard]] bool realtime_vi_paces_present();
[[nodiscard]] bool smooth_realtime_present_enabled();
[[nodiscard]] std::uint64_t smooth_realtime_present_max_wait_us();

// Holds each present to the frame-rate cap when the display does not (vsync off), and smooths a VI-paced present
// that arrives early by at most the smoothing wait: a high-resolution sleep to 0.2 ms before the target, then yields.
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
    // A high-resolution waitable timer (Windows 10 1803+): it sleeps to the target instead of spinning on
    // SwitchToThread, which cost the render thread about 100 ms of CPU a second. Null: Sleep and yield.
    void* timer_ = nullptr;
    bool timer_tried_ = false;
    // Sleeps toward a target `remaining_ms` away, waking kPaceSpinMs early; yields when closer.
    void sleep_before(double remaining_ms);
};

// One fully resolved draw.  The renderer sets PSO / root arguments only when
// they differ from the previous draw (cheap CPU-side batching).
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
};

}  // namespace galaxy::gx
