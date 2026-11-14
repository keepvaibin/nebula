// DisplayPictures, the display picture texture registry for display copies (render thread only), and the scene target-copy geometry
// helpers.

#include "galaxy/render/frame_pass.h"
#include "galaxy/platform_time.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_common.h"
#if !defined(NEBULA_GPU_VULKAN) && !defined(NEBULA_GPU_METAL) && !defined(NEBULA_GPU_D3D11)
#include "barriers_d3d12.h"
#include "galaxy/render/renderer_d3d12.h"
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace galaxy::render {

namespace {

constexpr std::size_t kMaxIdleDisplayTextures = 8u;
// A picture idle this long is freed: in a steady frame every pooled picture is taken again within a few frames, so
// only sizes the frame no longer has (a resize, a scale change) and one-off uses wait this long.
constexpr std::uint64_t kIdleTextureMs = 5000u;
// Three pictures: the one being drawn, the one shown and the one before it, which a present may still read.
constexpr std::uint32_t kPictures = 3u;

}  // namespace

bool DisplayPictures::initialize(gpu::Device* device, const FrameMap& map) {
    device_ = device;
    map_ = map;
    return device_ != nullptr;
}

void DisplayPictures::shutdown() {
    registry_.clear();
    retired_textures_.clear();
    idle_textures_.clear();
    has_latest_ = false;
    latest_slot_ = 0;
    next_slot_ = 0;
    next_picture_number_ = 1;
    map_ = FrameMap{};
    device_.Reset();
}

void DisplayPictures::begin_frame(
    unsigned frame_slot,
    unsigned frames_in_flight) {
    if (frames_in_flight == 0u || frame_slot >= frames_in_flight) {
        throw std::runtime_error("[frame-pass] invalid frame slot");
    }
    if (retired_textures_.empty()) {
        retired_textures_.resize(frames_in_flight);
    } else if (retired_textures_.size() != frames_in_flight) {
        throw std::runtime_error("[frame-pass] frame count changed");
    }
    recycle_retired_textures(frame_slot);
    current_frame_slot_ = frame_slot;
}

void DisplayPictures::retire_texture(DisplayTexture& texture) {
    if (!texture.texture) {
        return;
    }
    if (retired_textures_.empty()) {
        throw std::runtime_error(
            "[frame-pass] texture retired before begin_frame");
    }
    // Reserve ownership storage before moving the live registry resource.
    // A vector allocation failure must not release an in-flight display picture.
    auto& retired = retired_textures_[current_frame_slot_];
    retired.emplace_back();
    retired.back().width = texture.width;
    retired.back().height = texture.height;
    retired.back().texture = std::move(texture.texture);
}

void DisplayPictures::recycle_retired_textures(unsigned frame_slot) {
    auto& retired = retired_textures_[frame_slot];
    const std::uint64_t now = galaxy::steady_ms();
    // Idle textures are past their fence and kept oldest first: those idle too long go.
    const auto stale = std::find_if(idle_textures_.begin(), idle_textures_.end(), [now](const PooledTexture& pooled) {
        return now - pooled.idle_since_ms < kIdleTextureMs;
    });
    idle_textures_.erase(idle_textures_.begin(), stale);
    for (auto& texture : retired) {
        if (texture.texture) {
            if (idle_textures_.size() >= kMaxIdleDisplayTextures) {
                // The oldest goes so unused sizes do not accumulate.
                idle_textures_.erase(idle_textures_.begin());
            }
            texture.idle_since_ms = now;
            idle_textures_.push_back(std::move(texture));
        }
    }
    retired.clear();
}

gpu::TextureRef DisplayPictures::acquire_idle_texture(
    std::uint32_t width,
    std::uint32_t height) {
    for (auto it = idle_textures_.begin(); it != idle_textures_.end(); ++it) {
        if (it->texture && it->width == width && it->height == height) {
            auto texture = std::move(it->texture);
            idle_textures_.erase(it);
            return texture;
        }
    }
    return {};
}

// A capture's size: the frame's own (internal_extent keeps the frame within the largest texture the device makes).
FrameExtent frame_pass_extent(const FrameMap& map) noexcept {
    return FrameExtent{map.width, map.height};
}

FramePassGeometry frame_pass_geometry(
    unsigned dest_width,
    unsigned dest_height,
    const FrameMap& map) noexcept {
    FramePassGeometry geometry{};
    geometry.step_x = dest_width == 0u ? 0.0f : static_cast<float>(map.width) / static_cast<float>(dest_width);
    geometry.step_y = dest_height == 0u ? 0.0f : static_cast<float>(map.height) / static_cast<float>(dest_height);
    return geometry;
}

DisplayTexture* DisplayPictures::acquire_display(
    const FramePassRequest& /*params*/,
    std::uint64_t frame_index) {
    // The ring moves on when a picture is published (mark_display_copied).
    const std::uint32_t slot = next_slot_;

    const FrameExtent extent = frame_pass_extent(map_);
    const std::uint32_t wanted_w = extent.width;
    const std::uint32_t wanted_h = extent.height;

    auto it = registry_.find(slot);
    if (it != registry_.end()) {
        DisplayTexture& entry = it->second;
        // A failed allocation can leave an entry with matching dimensions
        // but no resource. Retry allocation instead of returning that entry.
        if (entry.width == wanted_w && entry.height == wanted_h &&
            entry.texture) {
            return &entry;
        }
        // Size changed: drop the old texture and recreate it below.
        if (entry.texture) {
            retire_texture(entry);
        }
        entry.rtv = {};
        entry.rtv_valid = false;
    } else {
        auto [ins_it, ok] = registry_.emplace(slot, DisplayTexture{});
        (void)ok;
        it = ins_it;
    }

    DisplayTexture& entry = it->second;
    entry.slot        = slot;
    entry.width       = wanted_w;
    entry.height      = wanted_h;
    entry.frame_stamp = frame_index;
    entry.picture_number = 0;
    entry.presentable = false;
    entry.rtv = {};
    entry.rtv_valid = false;

    entry.texture = acquire_idle_texture(wanted_w, wanted_h);
    if (entry.texture) {
        return &entry;
    }

    entry.texture = gpu::create_render_texture(device_.Get(), wanted_w, wanted_h);
    if (!entry.texture) {
        std::ostringstream message;
        message << "[frame-pass] picture texture allocation failed"
                << " slot=" << slot
                << " size=" << std::dec << wanted_w << "x" << wanted_h;
        throw std::runtime_error(message.str());
    }

    return &entry;
}

void DisplayPictures::adopt_picture(DisplayTexture& display_picture, gpu::TextureRef texture) {
    retire_texture(display_picture);
    display_picture.texture = std::move(texture);
    display_picture.rtv = {};
    display_picture.rtv_valid = false;
}

gpu::TextureRef DisplayPictures::take_idle_texture(std::uint32_t width, std::uint32_t height) {
    return acquire_idle_texture(width, height);
}

void DisplayPictures::release_texture(gpu::TextureRef texture, std::uint32_t width, std::uint32_t height) {
    if (!texture || retired_textures_.empty()) {
        return;
    }
    auto& retired = retired_textures_[current_frame_slot_];
    retired.emplace_back();
    retired.back().width = width;
    retired.back().height = height;
    retired.back().texture = std::move(texture);
}

void DisplayPictures::mark_display_copied(
    DisplayTexture& texture,
    std::uint64_t frame_index) {
    texture.frame_stamp = frame_index;
    texture.picture_number = next_picture_number_++;
    texture.presentable = true;
    latest_slot_ = texture.slot;
    next_slot_ = (texture.slot + 1u) % kPictures;
    has_latest_ = true;
}

const DisplayTexture* DisplayPictures::latest() const {
    return has_latest_ ? picture(latest_slot_) : nullptr;
}

const DisplayTexture* DisplayPictures::picture(std::uint32_t slot) const {
    const auto it = registry_.find(slot);
    if (it == registry_.end()) {
        return nullptr;
    }
    return it->second.presentable ? &it->second : nullptr;
}


}  // namespace galaxy::render

#if !defined(NEBULA_GPU_VULKAN) && !defined(NEBULA_GPU_METAL) && !defined(NEBULA_GPU_D3D11)
namespace galaxy::render::gpu {

TextureRef create_render_texture(Device* device, std::uint32_t width, std::uint32_t height) {
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment          = 0;
    desc.Width              = width;
    desc.Height             = height;
    desc.DepthOrArraySize   = 1;
    desc.MipLevels          = 1;
    desc.Format             = kSceneColorFormat;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    // A frame's last pass writes into it.
    desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    // Starts readable: a present can come before the first frame writes it.
    const gpu::Use initial_use = gpu::Use::PixelRead;
    // An optimized clear value avoids debug-layer warnings on the first RTV clear.
    D3D12_CLEAR_VALUE clear_val{};
    clear_val.Format   = kSceneColorFormat;
    clear_val.Color[0] = 0.0f;
    clear_val.Color[1] = 0.0f;
    clear_val.Color[2] = 0.0f;
    clear_val.Color[3] = 1.0f;
    TextureRef texture;
    if (FAILED(create_texture(device, &heap_props, D3D12_HEAP_FLAG_NONE, &desc, initial_use, &clear_val,
                              IID_PPV_ARGS(texture.ReleaseAndGetAddressOf())))) {
        return {};
    }
    return texture;
}

}  // namespace galaxy::render::gpu
#endif
