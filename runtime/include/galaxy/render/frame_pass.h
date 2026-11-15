#pragma once

// DisplayPictures: the frame pictures (one each time the game ends a frame) and the scene target copies' textures.
//
// Each frame end takes the next of a few pictures in turn (a ring the renderer owns, so a picture still being shown
// is not drawn over); RenderCore presents the newest, latest().
//
// A picture is never converted to YUV or written to game memory.

#include "galaxy/windows_api.h"
#include "galaxy/render/gpu_types.h"
#include "galaxy/render/texture_sampler.h"
#include "galaxy/render/pointer_response.h"
#include "galaxy/render/render_config.h"

#include <nebula_game_api.h>

#include <array>
#include <cmath>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

namespace galaxy::render {

// A capture of the whole frame (shapes::Record FramePass) or the frame shown (FrameShow): the texture it becomes,
// the picture's channels (NebulaTargetChannels) and NebulaFramePassFlags.
// Rectangles from the game (viewports, scissors, clears) count kFrameUnits across a frame or target on each axis.
inline constexpr unsigned kFrameUnits = 65536;
// The picture height effects and arbitrary mip chains were made for: their reach and bias follow the frame's height
// over it.
inline constexpr double kAuthoredLines = NEBULA_AUTHORED_LINES;

// A target's size in pixels and its pixels per unit.
struct FrameMap {
    unsigned width = 0;
    unsigned height = 0;
    float sx = 0.0f;
    float sy = 0.0f;

    constexpr FrameMap() noexcept = default;
    constexpr FrameMap(unsigned pixels_wide, unsigned pixels_high) noexcept
        : width(pixels_wide), height(pixels_high), sx(static_cast<float>(pixels_wide) / static_cast<float>(kFrameUnits)),
          sy(static_cast<float>(pixels_high) / static_cast<float>(kFrameUnits)) {}
    [[nodiscard]] constexpr bool empty() const noexcept { return width == 0u || height == 0u; }
    [[nodiscard]] constexpr bool operator==(const FrameMap&) const noexcept = default;
};

struct FramePassRequest {
    std::uint64_t key = 0;          // the texture (0: the frame shown)
    std::uint8_t channels = 0;      // NebulaTargetChannels
    bool display = false;           // the frame shown (frame_show), not a texture
};
struct DisplayTexture {
    gpu::TextureRef texture;  // RGBA8
    std::uint32_t slot = 0;  // place in the picture ring
    // The picture's size in pixels: the frame map it was taken at, which is also its texture's size.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t frame_stamp = 0;  // frame index of the last copy into it
    PointerResponseStamp pointer_response{};
    bool prerecorded_movie{};
    std::uint64_t picture_number = 0;  // monotonically published copy order
    // Render thread only: the serial last counted as presented, so each copy's first presentation counts once.
    mutable std::uint64_t last_shown_number =
        ~std::uint64_t{0};
    gpu::CpuDescriptor rtv{};
    bool presentable = false;
    bool rtv_valid = false;
};

// A capture's size in pixels.
struct FrameExtent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

[[nodiscard]] FrameExtent frame_pass_extent(const FrameMap& map) noexcept;

struct FramePassGeometry {
    float src_x = 0.0f;
    float src_y = 0.0f;
    float step_x = 0.0f;
    float step_y = 0.0f;
};

[[nodiscard]] FramePassGeometry frame_pass_geometry(
    unsigned dest_width,
    unsigned dest_height,
    const FrameMap& map) noexcept;

// A capture's channels: the game's NebulaTargetChannels (RGBA, ALPHA, RED, RGB), then the frame shown (opaque colour).
inline constexpr std::uint8_t kCopyLayoutDisplay = 4u;

struct ScaledSceneRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;

    [[nodiscard]] bool empty() const noexcept {
        return right <= left || bottom <= top;
    }
};

// A rectangle in frame units in its target's pixels, clamped to the target. Inline: this and viewport_pixels run once
// per draw.
[[nodiscard]] inline ScaledSceneRect rect_pixels(
    std::int32_t logical_left,
    std::int32_t logical_top,
    std::int32_t logical_right,
    std::int32_t logical_bottom,
    const FrameMap& map) noexcept {
    // Target extents are arbitrary unsigned values; saturate to the signed 32-bit rectangle range.
    constexpr std::int64_t kRectLimit = std::numeric_limits<std::int32_t>::max();
    const auto scaled = [](std::int64_t units, float scale) {
        return static_cast<std::int64_t>(std::lround(static_cast<double>(units) * scale));
    };
    const std::int64_t target_width = std::min<std::int64_t>(map.width, kRectLimit);
    const std::int64_t target_height = std::min<std::int64_t>(map.height, kRectLimit);
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

// The viewport (drawing_state.h: left, top, width, height, near and far depth) in pixels, depth reversed.
[[nodiscard]] inline ScaledViewport viewport_pixels(
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

class DisplayPictures {
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
        const FramePassRequest& params,
        std::uint64_t frame_index);
    void mark_display_copied(DisplayTexture& texture, std::uint64_t frame_index);
    // Native presentation: `texture` (a handed-over scene target colour target, its picture from the top left, taller than the
    // display picture) becomes the display picture's; the one it had is released as a copied display picture's would be.
    void adopt_picture(DisplayTexture& display_picture, gpu::TextureRef texture);
    // A released texture of this size no frame in flight still reads (a handed-over scene target's replacement), or null.
    [[nodiscard]] gpu::TextureRef take_idle_texture(std::uint32_t width, std::uint32_t height);
    // A picture back into the pool, once no frame in flight reads it.
    void release_texture(gpu::TextureRef texture, std::uint32_t width, std::uint32_t height);

    // Calls `visit(gpu::TextureObject*)` for every texture the pictures hold: shown, waiting out their frames, idle (the
    // video memory breakdown).
    template <typename Visit>
    void for_each_texture(Visit&& visit) const {
        for (const DisplayTexture& entry : registry_) {
            if (entry.texture) {
                visit(gpu::raw(entry.texture));
            }
        }
        for (const std::vector<PooledTexture>& frame : retired_textures_) {
            for (const PooledTexture& pooled : frame) {
                if (pooled.texture) {
                    visit(gpu::raw(pooled.texture));
                }
            }
        }
        for (const PooledTexture& pooled : idle_textures_) {
            if (pooled.texture) {
                visit(gpu::raw(pooled.texture));
            }
        }
    }

    // The newest finished picture; nullptr before the first frame end.
    [[nodiscard]] const DisplayTexture* latest() const;
    // The finished picture in `slot`, or nullptr.
    [[nodiscard]] const DisplayTexture* picture(std::uint32_t slot) const;


private:
    // Three pictures: the one being drawn, the one shown and the one before it, which a present may still read.
    static constexpr std::uint32_t kPictures = 3u;
    struct PooledTexture {
        gpu::TextureRef texture;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t idle_since_ms = 0;  // steady_ms() when it became idle
    };
    void retire_texture(DisplayTexture& texture);
    void recycle_retired_textures(unsigned frame_slot);
    [[nodiscard]] gpu::TextureRef
        acquire_idle_texture(std::uint32_t width, std::uint32_t height);

    std::array<DisplayTexture, kPictures> registry_{};  // by slot
    std::uint32_t latest_slot_ = 0;
    std::uint32_t next_slot_ = 0;
    std::uint64_t next_picture_number_ = 1;
    bool has_latest_ = false;
    gpu::DeviceRef device_;
    std::vector<std::vector<PooledTexture>> retired_textures_;
    std::vector<PooledTexture> idle_textures_;
    unsigned current_frame_slot_ = 0;
    FrameMap map_{};
};

}  // namespace galaxy::render
