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
#include <deque>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
struct IDXGIAdapter;  // dxgi.h
#endif

namespace galaxy::render {

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

// The capture shader's constants (b0, seven 32-bit values): the rectangle's origin in frame pixels, frame pixels per
// picture pixel, the picture's channels (NebulaTargetChannels; kCopyLayoutDisplay the frame shown), and the part of the
// source that holds the picture, in its pixels from the top left (dynamic resolution draws the scene in a part of its
// targets): no sample reaches past it. Two scalars, not a vector, so every shading language lays them out alike.
struct SceneConversionShaderConstants {
    float src_xform[4]{};
    std::uint32_t channels = 0;
    float src_limit_x = 0.0f;  // 0: the whole texture
    float src_limit_y = 0.0f;
};
static_assert(sizeof(SceneConversionShaderConstants) == 7u * sizeof(std::uint32_t));
inline constexpr unsigned kSceneConversionConstantCount = 7u;

[[nodiscard]] std::string_view scene_conversion_shader_source() noexcept;
// The frame shown, one frame pixel to one picture pixel (scene_conversion_shader_source's arithmetic for it).
[[nodiscard]] std::string_view display_filter_shader_source() noexcept;
// HDR output's 2D layer written back into the float scene: t0 the layer as drawn, t1 the layer as it started; a pixel the
// layer left as it started is discarded (the float scene keeps its light past white there), any other takes the layer's.
[[nodiscard]] std::string_view layer_merge_shader_source() noexcept;

// Direct3D 11's constants watch (d3d11::ConstantsWatch): b0's first three words, as one dispatch reads them, into u0.
[[nodiscard]] std::string_view constants_watch_shader_source() noexcept;

// The renderers' utility shaders (HLSL; the Vulkan backend annotates and compiles the same source): the
// fullscreen-triangle blit, the scene target colour-channel clear, the depth carry after a resolution change, and the
// overlay's textured and solid quads (root constants / push constants b0).
[[nodiscard]] std::string_view depth_resolve_pixel_shader_source() noexcept;
// The scene's depth as a picture (dynamic resolution's rectangle at the extent's size): R32F, point sampled, with the
// capture shader's constants (SceneConversionShaderConstants).
[[nodiscard]] std::string_view depth_copy_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view blit_vertex_shader_source() noexcept;
[[nodiscard]] std::string_view blit_pixel_shader_source() noexcept;
[[nodiscard]] std::string_view clear_pixel_shader_source() noexcept;
// Depth clear as a draw (Vulkan drivers whose vkCmdClearAttachments races with draws): writes clear_value.x.
[[nodiscard]] std::string_view clear_depth_pixel_shader_source() noexcept;
// `limit_x` x `limit_y`: the source's pixels that hold the picture (0: all of them).
[[nodiscard]] SceneConversionShaderConstants make_scene_conversion_constants(std::uint8_t channels, float src_x, float src_y,
                                                                             float step_x, float step_y, float limit_x = 0.0f,
                                                                             float limit_y = 0.0f) noexcept;

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

#if defined(_WIN32)
// The adapter's user-mode driver version as Windows shows it ("32.0.15.6094"); empty when it does not say.
[[nodiscard]] std::string dxgi_driver_version(::IDXGIAdapter* adapter);
#endif

// A test capture's pixels (RGB rows, top first) as a binary PPM at `path`, written off the render thread (every
// renderer). wait_picture_files returns once every one asked is written:
// the renderers' finish_pictures, before a process that ends without their shutdown.
void write_ppm_async(std::string path, std::vector<std::uint8_t> rgb, unsigned width, unsigned height);
void wait_picture_files();

// The forms a capture's rows come in.
enum class CaptureForm : std::uint8_t {
    Rgba8,   // RGBA8 rows
    Bgra8,   // BGRA8 rows
    TenBit,  // R10G10B10A2 words
    Half,    // RGBA16F halves (the float scene's), read with the 10-bit range
};

// A finished capture's mapped rows, `pitch` bytes apart in `form`, as RGB rows written by write_ppm_async: the one
// encoding of the four renderers' test pictures (ledger P-022, part 3).
void write_capture_rows(std::string path, const std::uint8_t* rows, unsigned width, unsigned height, unsigned pitch,
                        CaptureForm form);

// Test pictures copied out of the GPU (ledger P-323). Each capture has its own readback (`Readback`: the renderer's
// buffer or staging texture), fenced with the frame that recorded it, and is handed back for writing once that fence
// is reached, oldest first. A capture asked while others are in flight takes another readback, never one in flight, so
// no picture asked is dropped; written ones are kept for later captures (kSpares at most).
template <class Readback>
class PictureCaptures {
public:
    using Form = CaptureForm;
    struct Capture {
        Readback readback{};
        std::uint64_t fence = 0u;  // 0: recorded in the frame not yet submitted
        std::string path;
        unsigned width = 0u;
        unsigned height = 0u;
        unsigned pitch = 0u;  // bytes from one row to the next in the readback
        Form form = Form::Rgba8;
    };

    // A kept readback that `fits` accepts, taken from the spares; an empty one when none fits (the renderer makes one).
    template <class Fits>
    [[nodiscard]] Readback spare(Fits&& fits) {
        for (auto it = spares_.begin(); it != spares_.end(); ++it) {
            if (fits(*it)) {
                Readback taken = std::move(*it);
                spares_.erase(it);
                return taken;
            }
        }
        return Readback{};
    }
    // A copy recorded in the open frame.
    void recorded(Capture capture) { in_flight_.push_back(std::move(capture)); }
    // The open frame was submitted with `fence`.
    void submitted(std::uint64_t fence) noexcept {
        for (Capture& capture : in_flight_) {
            if (capture.fence == 0u) {
                capture.fence = fence;
            }
        }
    }
    // Hands each capture whose fence `completed` has reached to `write(Capture&)`, oldest first, and keeps its readback.
    template <class Write>
    void finished(std::uint64_t completed, Write&& write) {
        while (!in_flight_.empty() && in_flight_.front().fence != 0u && in_flight_.front().fence <= completed) {
            Capture capture = std::move(in_flight_.front());
            in_flight_.pop_front();
            write(capture);
            if (spares_.size() < kSpares) {
                spares_.push_back(std::move(capture.readback));
            }
        }
    }
    [[nodiscard]] bool in_flight() const noexcept { return !in_flight_.empty(); }
    // The device is gone: so is every readback (the pictures in flight are not written).
    void clear() noexcept {
        in_flight_.clear();
        spares_.clear();
    }

private:
    static constexpr std::size_t kSpares = 4u;
    std::deque<Capture> in_flight_;
    std::vector<Readback> spares_;
};

}  // namespace galaxy::render
