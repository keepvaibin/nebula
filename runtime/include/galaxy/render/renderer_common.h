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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace galaxy::render {

using ScreenProbe = ::NebulaScreenProbe;

// A scene target kind, as a pipeline is built for it: log2 samples, then these bits.
inline constexpr std::uint8_t kSceneVariantSamplesMask = 3u;
inline constexpr std::uint8_t kSceneVariantHdr = 4u;
inline constexpr std::uint8_t kSceneVariantTarget = 8u;
// Every variant those bits make (a target pass with samples or the wide scene among them): the size of every cache kept
// by variant.
inline constexpr std::size_t kSceneVariantCount = std::size_t{kSceneVariantTarget} * 2u;

// The picture channel each sampled channel reads (0-3: R, G, B, A; 4 one) for a target pass's NebulaTargetChannels:
// RGBA, ALPHA (A A A A), RED (R R R R), RGB (alpha one).
[[nodiscard]] constexpr std::array<std::uint8_t, 4> target_channel_swizzle(std::uint8_t channels) noexcept {
    constexpr std::array<std::array<std::uint8_t, 4>, 4> kSwizzle{{
        {0, 1, 2, 3}, {3, 3, 3, 3}, {0, 0, 0, 0}, {0, 1, 2, 4}}};
    return kSwizzle[channels < kSwizzle.size() ? channels : 0u];
}

inline constexpr unsigned kFramesInFlight = 2;
// The upload rings' first size: one megabyte each. A ring grows at a frame boundary to what the frames take (the descriptor
// heaps and pools the same way), so the growth runs in every session and no first size is a guess of the content.
inline constexpr std::size_t kFirstRingBytes = std::size_t{1} << 20u;
// The vertex and matrix rings' segments:a frame reads the rows and vertices of the walk before it.
inline constexpr unsigned kRingSegments = kFramesInFlight + 2u;
inline constexpr std::size_t kVertexRingBytes = 32u * 1024u * 1024u;
inline constexpr std::size_t kIndexRingBytes = 12u * 1024u * 1024u;
inline constexpr std::size_t kConstantRingBytes = 12u * 1024u * 1024u;
inline constexpr std::size_t kMatrixRingBytes = 32u * 1024u * 1024u;
// The sampler array's length: Direct3D 12's sampler heap limit, and Vulkan's bindless sampler array (vk::kBindlessSamplerSlots).
inline constexpr unsigned kSamplerHeapSlots = 2048;
// The slots before the first sampler a draw names: the default sampler's.
inline constexpr unsigned kSamplerReservedSlots = 1;

// The render settings sampler slots depend on (anisotropy, LOD bias).
[[nodiscard]] std::uint32_t sampler_config_key(const RenderConfig& cfg) noexcept;

// One sampler slot's identity: the sampler and the settings it was made under.
struct SamplerSlotKey {
    SamplerKey sampler{};
    std::uint32_t config = 0;
    bool operator==(const SamplerSlotKey&) const = default;
};
struct SamplerSlotKeyHasher {
    [[nodiscard]] std::size_t operator()(const SamplerSlotKey& key) const noexcept;
};
[[nodiscard]] SamplerSlotKey make_sampler_slot_key(const SamplerKey& sampler, const RenderConfig& cfg) noexcept;

// A slot for each distinct sampler of one sampler array, made once. The renderer starts again between frames, with the
// GPU past them, when the array is nearly full.
class SamplerSlotCache {
public:
    struct Allocation { unsigned index; bool created; };
    // The index may pass kSamplerHeapSlots: the renderer checks it where it writes the slot.
    [[nodiscard]] Allocation get_or_allocate(const SamplerSlotKey& key);
    void reset() noexcept {
        slots_.clear();
        cursor_ = kSamplerReservedSlots;
    }
    // Slots taken, the reserved ones included.
    [[nodiscard]] unsigned used() const noexcept { return cursor_; }
private:
    std::unordered_map<SamplerSlotKey, unsigned, SamplerSlotKeyHasher> slots_;
    unsigned cursor_ = kSamplerReservedSlots;
};

// A bound table's samplers (Vulkan without descriptor indexing): one descriptor set holds them all, as many as the table
// has maps.
struct SamplerTableKey {
    std::vector<SamplerKey> samplers;
    std::uint32_t config = 0;
    bool operator==(const SamplerTableKey&) const = default;
};
struct SamplerTableKeyHasher {
    [[nodiscard]] std::size_t operator()(const SamplerTableKey& key) const noexcept;
};
[[nodiscard]] SamplerTableKey make_sampler_table_key(const SamplerKey* keys, unsigned count, const RenderConfig& cfg);


struct SceneClearValues {
    std::array<float, 4> color{};
    float reversed_depth = 0.0f;
};
// A clear's values as given; on a frame without alpha (frame_format 0, 1 with alpha) alpha 255.
[[nodiscard]] SceneClearValues make_scene_clear_values(
    std::uint32_t rgba,
    std::uint8_t frame_format) noexcept;

// The capture shader's constants (b0, five 32-bit values): the rectangle's origin in frame pixels, frame pixels per
// picture pixel, and the picture's channels (NebulaTargetChannels; kCopyLayoutDisplay the frame shown).
struct SceneConversionShaderConstants {
    float src_xform[4]{};
    std::uint32_t channels = 0;
};
static_assert(sizeof(SceneConversionShaderConstants) == 5u * sizeof(std::uint32_t));
inline constexpr unsigned kSceneConversionConstantCount = 5u;

[[nodiscard]] std::string_view scene_conversion_shader_source() noexcept;
// The frame shown, one frame pixel to one picture pixel (scene_conversion_shader_source's arithmetic for it).
[[nodiscard]] std::string_view display_filter_shader_source() noexcept;

// Screen probes (one compute dispatch a probe pass): t0 the scene's depth, t1 each point's frame pixel, u0 each point's
// depth as the game reads it (0 near, 1 far); b0 the count. 64 points a group.
[[nodiscard]] std::string_view screen_probe_shader_source() noexcept;
inline constexpr unsigned kScreenProbeGroup = 64u;
struct ScreenProbePixel {
    std::uint32_t x = ~0u;
    std::uint32_t y = ~0u;
};
// The frame pixel a probe names in the target `map` describes; ~0u for a point the game did not ask about (x or y below 0).
[[nodiscard]] ScreenProbePixel screen_probe_pixel(const ScreenProbe& probe, const FrameMap& map) noexcept;

// The renderers' utility shaders (HLSL; the Vulkan backend annotates and compiles the same source): the
// fullscreen-triangle blit, the scene target colour-channel clear, the depth carry after a resolution change, and the
// overlay's textured and solid quads (root constants / push constants b0).
[[nodiscard]] std::string_view depth_resolve_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view blit_vertex_shader_source() noexcept;
[[nodiscard]] std::string_view blit_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view clear_pixel_shader_source() noexcept;
// Depth clear as a draw (Vulkan drivers whose vkCmdClearAttachments races with draws): writes clear_value.x.
[[nodiscard]] std::string_view clear_depth_pixel_shader_source() noexcept;
[[nodiscard]] SceneConversionShaderConstants make_scene_conversion_constants(std::uint8_t channels, float src_x, float src_y,
                                                                             float step_x, float step_y) noexcept;

// Variable refresh. A display whose refreshes follow the presents (G-SYNC, FreeSync, Adaptive-Sync) shows itself in the vblank grid:
// IDXGIOutput::WaitForVBlank returns at each refresh, so the interval between wakes is the frame time, well over the mode's own
// period, where a fixed-rate display gives the mode's period. `was` keeps the answer between the two thresholds.
[[nodiscard]] inline bool next_variable_refresh(bool was, double mode_hz, double measured_hz) noexcept {
    if (mode_hz <= 1.0 || measured_hz <= 1.0) {
        return was;
    }
    return was ? measured_hz < mode_hz * 0.95 : measured_hz < mode_hz * 0.8;
}
// The rate a variable refresh display is held under so that it stays in its range: a presented frame never waits for a
// refresh. The mode's rate less mode^2 / 3600 (the rule the latency-reduction libraries use: 144 gives 138.2, 60 gives 59).
[[nodiscard]] inline double variable_refresh_cap(double mode_hz) noexcept {
    return mode_hz > 1.0 ? mode_hz - mode_hz * mode_hz / 3600.0 : 0.0;
}

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

// A test capture's pixels (RGB rows, top first) as a binary PPM at `path`, written off the render thread (the Vulkan
// and Metal renderers; Direct3D 12 keeps its own writers). wait_picture_files returns once every one asked is written:
// the renderers' finish_pictures, before a process that ends without their shutdown.
void write_ppm_async(std::string path, std::vector<std::uint8_t> rgb, unsigned width, unsigned height);
void wait_picture_files();

}  // namespace galaxy::render
