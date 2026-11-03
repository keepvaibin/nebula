// SceneCopyManager, the display picture texture registry for display copies (render thread only), and the scene target-copy geometry
// helpers.

#include "galaxy/render/capture.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_common.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace galaxy::render {

DisplaySerialAccountingEvent account_display_present_serial(
    DisplaySerialAccountingState& state,
    std::uint64_t& texture_accounted_serial,
    std::uint64_t copy_serial) noexcept {
    DisplaySerialAccountingEvent event{};
    const bool first_for_texture = texture_accounted_serial != copy_serial;
    if (!state.has_presented) {
        state.has_presented = true;
        state.highest_presented_serial = copy_serial;
        state.last_presented_serial = copy_serial;
        texture_accounted_serial = copy_serial;
        event.first_presentation = true;
        return event;
    }

    if (first_for_texture) {
        event.first_presentation = true;
        texture_accounted_serial = copy_serial;
        if (copy_serial > state.highest_presented_serial) {
            event.delta_one =
                copy_serial - state.highest_presented_serial == 1u;
            event.delta_multi = !event.delta_one;
            state.highest_presented_serial = copy_serial;
        } else {
            event.below_high_water =
                copy_serial < state.highest_presented_serial;
        }
        state.last_presented_serial = copy_serial;
        state.current_nonfirst_run = 0;
        return event;
    }

    event.adjacent_repeat = copy_serial == state.last_presented_serial;
    event.not_first_presentation = true;
    event.below_high_water = copy_serial < state.highest_presented_serial;
    state.last_presented_serial = copy_serial;
    ++state.current_nonfirst_run;
    event.nonfirst_run = state.current_nonfirst_run;
    return event;
}

namespace {

constexpr std::size_t kMaxIdleDisplayTextures = 8u;
// Three pictures: the one being drawn, the one shown and the one before it, which a present may still read.
constexpr std::uint32_t kPictures = 3u;

}  // namespace

bool SceneCopyManager::initialize(gpu::Device* device, const FrameMap& map) {
    device_ = device;
    map_ = map;
    return device_ != nullptr;
}

void SceneCopyManager::shutdown() {
    registry_.clear();
    retired_textures_.clear();
    idle_textures_.clear();
    has_latest_ = false;
    latest_slot_ = 0;
    next_slot_ = 0;
    next_copy_serial_ = 1;
    map_ = FrameMap{};
    device_.Reset();
}

void SceneCopyManager::begin_frame(
    unsigned frame_slot,
    unsigned frames_in_flight) {
    if (frames_in_flight == 0u || frame_slot >= frames_in_flight) {
        throw std::runtime_error("[frame-read] invalid frame slot");
    }
    if (retired_textures_.empty()) {
        retired_textures_.resize(frames_in_flight);
    } else if (retired_textures_.size() != frames_in_flight) {
        throw std::runtime_error("[frame-read] frame count changed");
    }
    recycle_retired_textures(frame_slot);
    current_frame_slot_ = frame_slot;
}

void SceneCopyManager::retire_texture(DisplayTexture& texture) {
    if (!texture.texture) {
        return;
    }
    if (retired_textures_.empty()) {
        throw std::runtime_error(
            "[frame-read] texture retired before begin_frame");
    }
    // Reserve ownership storage before moving the live registry resource.
    // A vector allocation failure must not release an in-flight display picture.
    auto& retired = retired_textures_[current_frame_slot_];
    retired.emplace_back();
    retired.back().width = texture.width;
    retired.back().height = texture.height;
    retired.back().texture = std::move(texture.texture);
}

void SceneCopyManager::recycle_retired_textures(unsigned frame_slot) {
    auto& retired = retired_textures_[frame_slot];
    for (auto& texture : retired) {
        if (texture.texture) {
            if (idle_textures_.size() >= kMaxIdleDisplayTextures) {
                // Idle textures are past their fence; the oldest goes so unused sizes do not accumulate.
                idle_textures_.erase(idle_textures_.begin());
            }
            idle_textures_.push_back(std::move(texture));
        }
    }
    retired.clear();
}

gpu::TextureRef SceneCopyManager::acquire_idle_texture(
    std::uint16_t width,
    std::uint16_t height) {
    for (auto it = idle_textures_.begin(); it != idle_textures_.end(); ++it) {
        if (it->texture && it->width == width && it->height == height) {
            auto texture = std::move(it->texture);
            idle_textures_.erase(it);
            return texture;
        }
    }
    return {};
}

// A capture's size at the frame scale, within the largest texture the device makes.
FrameExtent capture_extent(bool half_scale, const FrameMap& map) noexcept {
    constexpr unsigned kMaxTextureDimension = gpu::kMaxTexture2DDimension;
    const unsigned width = half_scale ? std::max(map.width / 2u, 1u) : map.width;
    const unsigned height = half_scale ? std::max(map.height / 2u, 1u) : map.height;
    return FrameExtent{static_cast<std::uint16_t>(std::min(width, kMaxTextureDimension)),
                       static_cast<std::uint16_t>(std::min(height, kMaxTextureDimension))};
}

SceneCopySamplingGeometry capture_sampling_geometry(
    unsigned dest_width,
    unsigned dest_height,
    const FrameMap& map) noexcept {
    SceneCopySamplingGeometry geometry{};
    geometry.step_x = dest_width == 0u ? 0.0f : static_cast<float>(map.width) / static_cast<float>(dest_width);
    geometry.step_y = dest_height == 0u ? 0.0f : static_cast<float>(map.height) / static_cast<float>(dest_height);
    return geometry;
}

DisplayTexture* SceneCopyManager::acquire_display(
    const CaptureRequest& /*params*/,
    std::uint64_t frame_index) {
    // The ring moves on when a picture is published (mark_display_copied).
    const std::uint32_t slot = next_slot_;

    const FrameExtent extent = capture_extent(false, map_);
    const std::uint16_t wanted_w = extent.width;
    const std::uint16_t wanted_h = extent.height;

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
    entry.copy_serial = 0;
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
        message << "[frame-read] picture texture allocation failed"
                << " slot=" << slot
                << " size=" << std::dec << wanted_w << "x" << wanted_h;
        throw std::runtime_error(message.str());
    }

    return &entry;
}

void SceneCopyManager::adopt_picture(DisplayTexture& display_picture, gpu::TextureRef texture) {
    retire_texture(display_picture);
    display_picture.texture = std::move(texture);
    display_picture.rtv = {};
    display_picture.rtv_valid = false;
}

gpu::TextureRef SceneCopyManager::take_idle_texture(std::uint16_t width, std::uint16_t height) {
    return acquire_idle_texture(width, height);
}

void SceneCopyManager::mark_display_copied(
    DisplayTexture& texture,
    std::uint64_t frame_index) {
    texture.frame_stamp = frame_index;
    texture.copy_serial = next_copy_serial_++;
    texture.presentable = true;
    latest_slot_ = texture.slot;
    next_slot_ = (texture.slot + 1u) % kPictures;
    has_latest_ = true;
}

const DisplayTexture* SceneCopyManager::latest() const {
    return has_latest_ ? picture(latest_slot_) : nullptr;
}

const DisplayTexture* SceneCopyManager::picture(std::uint32_t slot) const {
    const auto it = registry_.find(slot);
    if (it == registry_.end()) {
        return nullptr;
    }
    return it->second.presentable ? &it->second : nullptr;
}


}  // namespace galaxy::render

#if !defined(NEBULA_GPU_VULKAN)
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
    desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    // ALLOW_RENDER_TARGET so copy_scene_to_display can blit into it.
    desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    // Initial state is PIXEL_SHADER_RESOURCE: the present blit reads it before the first scene target
    // copy transitions it to RENDER_TARGET.
    const D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    // An optimized clear value avoids debug-layer warnings on the first RTV clear.
    D3D12_CLEAR_VALUE clear_val{};
    clear_val.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear_val.Color[0] = 0.0f;
    clear_val.Color[1] = 0.0f;
    clear_val.Color[2] = 0.0f;
    clear_val.Color[3] = 1.0f;
    TextureRef texture;
    if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc, initial_state, &clear_val,
                                               IID_PPV_ARGS(texture.ReleaseAndGetAddressOf())))) {
        return {};
    }
    return texture;
}

}  // namespace galaxy::render::gpu
#endif
