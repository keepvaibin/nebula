#pragma once

// SceneCopyManager: the frame pictures (one each time the game ends a frame) and the scene target copies' textures.
//
// Each frame end takes the next of a few pictures in turn (a ring the renderer owns, so a picture still being shown
// is not drawn over); RenderCore presents the newest, latest().
//
// A picture is never converted to YUV or written to game memory.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/draw_types.h"
#include "galaxy/render/pointer_response.h"
#include "galaxy/render/render_config.h"

#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace galaxy::render {

// A capture of the frame (shapes::Record TargetCapture) or the frame shown (FrameShow), as the game asks for it: the
// rectangle in logical pixels, the texture the capture becomes and the picture's channels (NebulaTargetChannels), and
// NebulaCaptureFlags. Every sample stays inside the rectangle.
// The game's frame in its units (640x456).
inline constexpr unsigned kFrameWidth = 640;
inline constexpr unsigned kFrameHeight = 456;

// A target's map from the game's units to its pixels (E-08): the frame's size in pixels and pixels per unit.
struct FrameMap {
    unsigned width = 0;
    unsigned height = 0;
    float sx = 0.0f;
    float sy = 0.0f;

    constexpr FrameMap() noexcept = default;
    constexpr FrameMap(unsigned scale) noexcept  // NOLINT: an integer scale is a map
        : width(kFrameWidth * scale), height(kFrameHeight * scale), sx(static_cast<float>(scale)),
          sy(static_cast<float>(scale)) {}
    // A frame of exactly `pixels_wide` x `pixels_high`: square pixels at any size.
    constexpr FrameMap(unsigned pixels_wide, unsigned pixels_high) noexcept
        : width(pixels_wide), height(pixels_high), sx(static_cast<float>(pixels_wide) / static_cast<float>(kFrameWidth)),
          sy(static_cast<float>(pixels_high) / static_cast<float>(kFrameHeight)) {}
    // The smallest whole scale that covers it, for what still works in them.
    [[nodiscard]] unsigned scale() const noexcept {
        return static_cast<unsigned>(std::ceil(std::max(sx, sy) - 1e-4f));
    }
    // An integer multiple of the game's frame.
    [[nodiscard]] bool whole() const noexcept {
        const unsigned s = scale();
        return width == kFrameWidth * s && height == kFrameHeight * s;
    }
    [[nodiscard]] constexpr bool operator==(const FrameMap&) const noexcept = default;
};

struct CaptureRequest {
    std::uint16_t src_x = 0, src_y = 0;
    std::uint16_t src_width = 0, src_height = 0;
    std::uint64_t key = 0;          // the texture (0: the frame shown)
    std::uint8_t channels = 0;      // NebulaTargetChannels
    bool display = false;           // the frame shown (frame_show), not a texture
    bool clear = false;             // the rectangle cleared afterwards (colour and alpha 0, depth far)
    bool clear_color = false, clear_alpha = false, clear_depth = false;  // what the clear touches
    bool half_scale = false;        // each texel averages 2x2 of the frame's
    bool luma = false;              // the red channel holds the colour's luminance
    bool depth = false;             // the red channel holds the depth's high byte
};
// A line's weights in a copy, in 64ths: the line alone.
[[nodiscard]] constexpr std::uint32_t capture_filter_upper(const CaptureRequest&) { return 0u; }
[[nodiscard]] constexpr std::uint32_t capture_filter_middle(const CaptureRequest&) { return 64u; }
[[nodiscard]] constexpr std::uint32_t capture_filter_lower(const CaptureRequest&) { return 0u; }

struct DisplayTexture {
    gpu::TextureRef texture;  // RGBA8
    std::uint32_t slot = 0;  // place in the picture ring
    // GPU dimensions after scene target scaling; the copy's rectangle stays in Wii units.
    std::uint16_t width = 0;
    std::uint16_t height = 0;       // after logical y-scale and scene target scale
    std::uint64_t frame_stamp = 0;  // frame index of the last copy into it
    PointerResponseStamp pointer_response{};
    bool prerecorded_movie{};
    // Scene-owned surround travels with this copy, never with a later producer.
    float safety_surround{};
    std::uint64_t copy_serial = 0;  // monotonically published copy order
    // Render thread only: the serial last counted as presented, so each copy's first presentation counts once.
    mutable std::uint64_t last_accounted_present_serial =
        ~std::uint64_t{0};
    gpu::CpuDescriptor rtv{};
    bool presentable = false;
    bool rtv_valid = false;
};

// Presentation accounting of scene target -> display picture copy serials. A first presentation is decided by the texture's own
// accounted serial; the high-water mark is diagnostic only.
struct DisplaySerialAccountingState {
    bool has_presented = false;
    std::uint64_t highest_presented_serial = 0;
    std::uint64_t last_presented_serial = 0;
    std::uint64_t current_nonfirst_run = 0;
};

struct DisplaySerialAccountingEvent {
    bool first_presentation = false;
    bool adjacent_repeat = false;
    bool not_first_presentation = false;
    bool below_high_water = false;
    bool delta_one = false;
    bool delta_multi = false;
    std::uint64_t nonfirst_run = 0;
};

[[nodiscard]] DisplaySerialAccountingEvent account_display_present_serial(
    DisplaySerialAccountingState& state,
    std::uint64_t& texture_accounted_serial,
    std::uint64_t copy_serial) noexcept;


// scene target-copy textures are at the internal resolution; addresses, strides and VI selection stay in Wii units.
struct ScaledSceneCopyExtent {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};

[[nodiscard]] ScaledSceneCopyExtent compute_scaled_scene_copy_extent(
    std::uint16_t logical_width,
    std::uint16_t logical_height,
    const FrameMap& map) noexcept;

struct SceneCopySamplingGeometry {
    float src_x = 0.0f;
    float src_y = 0.0f;
    float step_x = 0.0f;
    float step_y = 0.0f;
    float filter_row_offset = 1.0f;
};

[[nodiscard]] SceneCopySamplingGeometry compute_scene_copy_sampling_geometry(
    const CaptureRequest& params,
    unsigned dest_width,
    unsigned dest_height,
    const FrameMap& map) noexcept;

// The frame shown at a high resolution keeps the filter's weights but samples adjacent physical rows; captures
// keep their logical-row footprint.
[[nodiscard]] SceneCopySamplingGeometry compute_display_sampling_geometry(
    const CaptureRequest& params,
    unsigned dest_width,
    unsigned dest_height,
    const FrameMap& map) noexcept;

// A copy that is an exact colour/alpha identity at `map`: its source rectangle in scaled scene target texels. Rejects
// filtering, conversion, resampling and any source rectangle that would need the conversion shader's clamp.
struct SceneCopyRect {
    std::uint32_t left = 0;
    std::uint32_t top = 0;
    std::uint32_t right = 0;
    std::uint32_t bottom = 0;
};
// A colour copy whose texture is one channel (I4, or I8 from R, G, B or A): the conversion writes (i, i, i, i), so
// its destination holds the one byte (R8, viewed as R in every channel, as decoded I8 textures are).
// A copy's picture layout: the game's NebulaTargetChannels (0 RGBA, 1 intensity and alpha, 2 alpha, 3 red, 4 green,
// 5 blue, 6 colour with alpha one) for captures; kCopyLayoutDisplay for display copies (opaque colour through the
// display filter).
inline constexpr std::uint8_t kCopyLayoutRgba = 0u;
inline constexpr std::uint8_t kCopyLayoutIntensityAlpha = 1u;
inline constexpr std::uint8_t kCopyLayoutDisplay = 7u;

// One channel read in every channel (alpha, red, green or blue): an R8 picture.
[[nodiscard]] constexpr bool scene_copy_single_channel(std::uint8_t channels, bool is_depth_copy) noexcept {
    return !is_depth_copy && channels >= 2u && channels <= 5u;
}

[[nodiscard]] std::optional<SceneCopyRect> scene_identity_copy_rect(
    const CaptureRequest& params, bool is_depth_copy, const FrameMap& map,
    unsigned dest_width, unsigned dest_height) noexcept;

struct ScaledSceneRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;

    [[nodiscard]] bool empty() const noexcept {
        return right <= left || bottom <= top;
    }
};

// Inline: this and compute_scaled_viewport run once per draw.
[[nodiscard]] inline ScaledSceneRect compute_scaled_scene_rect(
    std::int32_t logical_left,
    std::int32_t logical_top,
    std::int32_t logical_right,
    std::int32_t logical_bottom,
    unsigned logical_target_width,
    unsigned logical_target_height,
    const FrameMap& map) noexcept {
    // Target extents are arbitrary unsigned values; saturate to the signed 32-bit rectangle range.
    constexpr std::int64_t kRectLimit = std::numeric_limits<std::int32_t>::max();
    const auto scaled = [](std::int64_t units, float scale) {
        return static_cast<std::int64_t>(std::lround(static_cast<double>(units) * scale));
    };
    const std::int64_t target_width = std::min(scaled(logical_target_width, map.sx), kRectLimit);
    const std::int64_t target_height = std::min(scaled(logical_target_height, map.sy), kRectLimit);
    return ScaledSceneRect{
        static_cast<std::int32_t>(std::clamp<std::int64_t>(scaled(logical_left, map.sx), 0, target_width)),
        static_cast<std::int32_t>(std::clamp<std::int64_t>(scaled(logical_top, map.sy), 0, target_height)),
        static_cast<std::int32_t>(std::clamp<std::int64_t>(scaled(logical_right, map.sx), 0, target_width)),
        static_cast<std::int32_t>(std::clamp<std::int64_t>(scaled(logical_bottom, map.sy), 0, target_height))};
}

struct ScaledViewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float min_depth = 0.0f;
    float max_depth = 1.0f;

    [[nodiscard]] bool valid() const noexcept {
        return width > 0.0f && height > 0.0f;
    }
};

// The viewport (drawing_state.h: left, top, width, height, near and far depth) at the frame scale, depth reversed.
[[nodiscard]] inline ScaledViewport compute_scaled_viewport(
    const float viewport[6],
    const FrameMap& map) noexcept {
    if (viewport == nullptr) {
        return {};
    }
    // Reversed depth: the far depth maps to 0, the near depth to 1.
    float min_depth = std::clamp(1.0f - viewport[5], 0.0f, 1.0f);
    const float max_depth = std::clamp(1.0f - viewport[4], 0.0f, 1.0f);
    if (min_depth > max_depth) {
        min_depth = max_depth;
    }
    return ScaledViewport{
        viewport[0] * map.sx,
        viewport[1] * map.sy,
        viewport[2] * map.sx,
        viewport[3] * map.sy,
        min_depth,
        max_depth};
}

class SceneCopyManager {
public:
    bool initialize(gpu::Device* device, const FrameMap& map);
    void shutdown();
    // Live internal-resolution change: later acquisitions re-create any display picture
    // texture whose physical extent no longer matches.
    void set_frame_map(const FrameMap& map) noexcept { map_ = map; }
    [[nodiscard]] const FrameMap& frame_map() const noexcept { return map_; }
    void begin_frame(unsigned frame_slot, unsigned frames_in_flight);

    // The next picture of the ring, created or resized to match the copy's size. The caller
    // (renderer) then records the scene target -> picture blit into it, or hands it the scene target's target (adopt_picture).
    DisplayTexture* acquire_display(
        const CaptureRequest& params,
        std::uint64_t frame_index);
    void mark_display_copied(DisplayTexture& texture, std::uint64_t frame_index);
    // Native presentation: `texture` (a handed-over scene target colour target, its picture from the top left, taller than the
    // display picture) becomes the display picture's; the one it had is released as a copied display picture's would be.
    void adopt_picture(DisplayTexture& display_picture, gpu::TextureRef texture);
    // A released texture of this size no frame in flight still reads (a handed-over scene target's replacement), or null.
    [[nodiscard]] gpu::TextureRef take_idle_texture(std::uint16_t width, std::uint16_t height);

    // The newest finished picture; nullptr before the first frame end.
    [[nodiscard]] const DisplayTexture* latest() const;
    // The finished picture in `slot`, or nullptr.
    [[nodiscard]] const DisplayTexture* picture(std::uint32_t slot) const;


private:
    struct PooledTexture {
        gpu::TextureRef texture;
        std::uint16_t width = 0;
        std::uint16_t height = 0;
    };
    void retire_texture(DisplayTexture& texture);
    void recycle_retired_textures(unsigned frame_slot);
    [[nodiscard]] gpu::TextureRef
        acquire_idle_texture(std::uint16_t width, std::uint16_t height);

    std::unordered_map<std::uint32_t, DisplayTexture> registry_;
    std::uint32_t latest_slot_ = 0;
    std::uint32_t next_slot_ = 0;
    std::uint64_t next_copy_serial_ = 1;
    bool has_latest_ = false;
    gpu::DeviceRef device_;
    std::vector<std::vector<PooledTexture>> retired_textures_;
    std::vector<PooledTexture> idle_textures_;
    unsigned current_frame_slot_ = 0;
    FrameMap map_ = FrameMap(1u);
};

}  // namespace galaxy::render
