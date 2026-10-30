// EfbCopyManager, the XFB texture registry for display copies (render thread only), and the EFB-copy geometry
// helpers.

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/renderer_common.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace galaxy::gx {

XfbSerialAccountingEvent account_xfb_present_serial(
    XfbSerialAccountingState& state,
    std::uint64_t& texture_accounted_serial,
    std::uint64_t copy_serial) noexcept {
    XfbSerialAccountingEvent event{};
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

constexpr std::size_t kMaxIdleXfbTextures = 8u;
// Three pictures: the one being drawn, the one shown and the one before it, which a present may still read.
constexpr std::uint32_t kPictures = 3u;

}  // namespace

bool EfbCopyManager::initialize(gpu::Device* device, unsigned efb_scale) {
    device_ = device;
    efb_scale_ = std::clamp(efb_scale, 1u, kMaxEfbScale);
    return device_ != nullptr;
}

void EfbCopyManager::shutdown() {
    registry_.clear();
    retired_textures_.clear();
    idle_textures_.clear();
    has_latest_ = false;
    latest_slot_ = 0;
    next_slot_ = 0;
    next_copy_serial_ = 1;
    efb_scale_ = 1;
    device_.Reset();
}

void EfbCopyManager::begin_frame(
    unsigned frame_slot,
    unsigned frames_in_flight) {
    if (frames_in_flight == 0u || frame_slot >= frames_in_flight) {
        throw std::runtime_error("[EfbCopyManager] invalid frame slot");
    }
    if (retired_textures_.empty()) {
        retired_textures_.resize(frames_in_flight);
    } else if (retired_textures_.size() != frames_in_flight) {
        throw std::runtime_error("[EfbCopyManager] frame count changed");
    }
    recycle_retired_textures(frame_slot);
    current_frame_slot_ = frame_slot;
}

void EfbCopyManager::retire_texture(XfbTexture& texture) {
    if (!texture.texture) {
        return;
    }
    if (retired_textures_.empty()) {
        throw std::runtime_error(
            "[EfbCopyManager] texture retired before begin_frame");
    }
    // Reserve ownership storage before moving the live registry resource.
    // A vector allocation failure must not release an in-flight XFB.
    auto& retired = retired_textures_[current_frame_slot_];
    retired.emplace_back();
    retired.back().width = texture.width;
    retired.back().height = texture.height;
    retired.back().texture = std::move(texture.texture);
}

void EfbCopyManager::recycle_retired_textures(unsigned frame_slot) {
    auto& retired = retired_textures_[frame_slot];
    for (auto& texture : retired) {
        if (texture.texture) {
            if (idle_textures_.size() >= kMaxIdleXfbTextures) {
                // Idle textures are past their fence; the oldest goes so unused sizes do not accumulate.
                idle_textures_.erase(idle_textures_.begin());
            }
            idle_textures_.push_back(std::move(texture));
        }
    }
    retired.clear();
}

gpu::TextureRef EfbCopyManager::acquire_idle_texture(
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

// GX BP 0x4E is the nine-bit inverse vertical step consumed by the RVL SDK's
// __GXGetNumXfbLines calculation. PE_COPY_EXECUTE bit 10 controls whether that
// calculation is applied at all. The source-size register stores height - 1;
// EfbCopyParams stores the decoded height, so the base result is
// 1 + floor((height - 1) * 256 / y_scale_raw).
std::uint16_t compute_xfb_copy_height(
    std::uint16_t src_height,
    std::uint32_t y_scale_raw,
    bool scale_y) {
    constexpr std::uint32_t kMaximumXfbLines = 1024u;
    if (src_height == 0u) {
        throw std::invalid_argument(
            "XFB copy source height must be nonzero");
    }
    if (!scale_y) {
        return static_cast<std::uint16_t>(
            std::min<std::uint32_t>(src_height, kMaximumXfbLines));
    }

    // The register is nine bits wide. A zero inverse step cannot describe a
    // finite display copy; fail instead of inventing a scale.
    if (y_scale_raw == 0u || y_scale_raw > 0x1FFu) {
        throw std::invalid_argument(
            "XFB copy inverse y-scale is outside the 9-bit finite range");
    }

    const std::uint32_t count =
        static_cast<std::uint32_t>(src_height - 1u) * 256u;
    std::uint32_t output_lines = count / y_scale_raw + 1u;

    // Exact RMGE01 RVL SDK __GXGetNumXfbLines divisor correction.
    std::uint32_t reduced_step = y_scale_raw;
    if (reduced_step > 0x80u && reduced_step < 0x100u) {
        while ((reduced_step & 1u) == 0u) {
            reduced_step >>= 1u;
        }
        if (src_height % reduced_step == 0u) {
            ++output_lines;
        }
    }
    // The RVL SDK caps the guest-visible result at 1024 XFB lines.
    return static_cast<std::uint16_t>(
        std::min(output_lines, kMaximumXfbLines));
}

ScaledEfbCopyExtent compute_scaled_efb_copy_extent(
    std::uint16_t logical_width,
    std::uint16_t logical_height,
    unsigned efb_scale) noexcept {
    const unsigned scale = std::clamp(efb_scale, 1u, kMaxEfbScale);
    constexpr unsigned kMaxTextureDimension = gpu::kMaxTexture2DDimension;
    const unsigned width = std::min(
        static_cast<unsigned>(logical_width) * scale,
        kMaxTextureDimension);
    const unsigned height = std::min(
        static_cast<unsigned>(logical_height) * scale,
        kMaxTextureDimension);
    return ScaledEfbCopyExtent{
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height)};
}

bool is_galaxy_bloom_workspace_copy(
    const EfbCopyParams& p, bool is_depth_copy) noexcept {
    if (is_depth_copy || p.copy_to_xfb || p.yuv || p.clear ||
        p.intensity || p.target_format != 4u || p.gamma != 0u ||
        p.scale_y || p.frame_to_field != 0u) return false;
    // The game's colour-mask bloom chain: a 160x114 bright/blur workspace at (480,0), an 80x57 downsampled blur
    // result at (320,114) and the final 160x114 colour composite at (480,114). The early mask and focus copies at
    // (0,0) and (320,0) are excluded even when their format matches.
    if (p.src_x == 480u && p.src_y == 0u &&
        p.src_width == 160u && p.src_height == 114u)
        return p.dest_stride == (p.half_scale ? 640u : 1280u);
    if (p.src_x == 320u && p.src_y == 114u &&
        p.src_width == 80u && p.src_height == 57u)
        return !p.half_scale && p.dest_stride == 640u;
    return p.src_x == 480u && p.src_y == 114u &&
        p.src_width == 160u && p.src_height == 114u &&
        !p.half_scale && p.dest_stride == 1280u;
}

EfbCopySamplingGeometry compute_efb_copy_sampling_geometry(
    const EfbCopyParams& params,
    unsigned dest_width,
    unsigned dest_height,
    unsigned efb_scale) noexcept {
    const float scale = static_cast<float>(
        std::clamp(efb_scale, 1u, kMaxEfbScale));
    EfbCopySamplingGeometry geometry{};
    geometry.src_x = static_cast<float>(params.src_x) * scale;
    geometry.src_y = static_cast<float>(params.src_y) * scale;
    geometry.step_x = dest_width == 0u
        ? 0.0f
        : static_cast<float>(params.src_width) * scale /
              static_cast<float>(dest_width);
    geometry.step_y = dest_height == 0u
        ? 0.0f
        : static_cast<float>(params.src_height) * scale /
              static_cast<float>(dest_height);
    // GX copy-filter taps are one logical EFB row apart. At Nx internal
    // resolution that separation is N physical source texels.
    geometry.filter_row_offset = scale;
    return geometry;
}

EfbCopySamplingGeometry compute_xfb_sampling_geometry(
    const EfbCopyParams& params,
    unsigned dest_width,
    unsigned dest_height,
    unsigned efb_scale) noexcept {
    auto geometry = compute_efb_copy_sampling_geometry(
        params, dest_width, dest_height, efb_scale);
    // Display copies filter adjacent physical rows, as Dolphin's TextureCacheBase::CopyEFBToCacheEntry does with
    // its scaled pixel_height; only the tap spacing differs from texture copies.
    geometry.filter_row_offset = 1.0f;
    return geometry;
}

EfbPeekSamplingGeometry compute_efb_peek_sampling_geometry(
    std::uint16_t x,
    std::uint16_t y,
    unsigned efb_scale) noexcept {
    const unsigned integer_scale = std::clamp(efb_scale, 1u, kMaxEfbScale);
    const float scale = static_cast<float>(integer_scale);
    const std::uint32_t src_x =
        static_cast<std::uint32_t>(x) * integer_scale;
    const std::uint32_t src_y =
        static_cast<std::uint32_t>(y) * integer_scale;
    const std::uint32_t center_offset = integer_scale / 2u;
    return EfbPeekSamplingGeometry{
        static_cast<float>(src_x),
        static_cast<float>(src_y),
        scale,
        static_cast<float>(src_y) + 0.5f * scale,
        src_x + center_offset,
        src_y + center_offset,
        integer_scale * integer_scale};
}

void EfbCopyManager::set_efb_scale(unsigned efb_scale) noexcept {
    efb_scale_ = std::clamp(efb_scale, 1u, kMaxEfbScale);
}

XfbTexture* EfbCopyManager::acquire_xfb(
    const EfbCopyParams& params,
    std::uint64_t frame_index) {
    // The ring moves on when a picture is published (mark_xfb_copied).
    const std::uint32_t slot = next_slot_;

    const std::uint16_t logical_h =
        compute_xfb_copy_height(
            params.src_height,
            params.y_scale,
            params.scale_y);
    const ScaledEfbCopyExtent extent = compute_scaled_efb_copy_extent(
        params.src_width,
        logical_h,
        efb_scale_);
    const std::uint16_t wanted_w = extent.width;
    const std::uint16_t wanted_h = extent.height;

    auto it = registry_.find(slot);
    if (it != registry_.end()) {
        XfbTexture& entry = it->second;
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
        auto [ins_it, ok] = registry_.emplace(slot, XfbTexture{});
        (void)ok;
        it = ins_it;
    }

    XfbTexture& entry = it->second;
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
        message << "[EfbCopyManager] picture texture allocation failed"
                << " slot=" << slot
                << " size=" << std::dec << wanted_w << "x" << wanted_h;
        throw std::runtime_error(message.str());
    }

    return &entry;
}

void EfbCopyManager::adopt_picture(XfbTexture& xfb, gpu::TextureRef texture) {
    retire_texture(xfb);
    xfb.texture = std::move(texture);
    xfb.rtv = {};
    xfb.rtv_valid = false;
}

gpu::TextureRef EfbCopyManager::take_idle_texture(std::uint16_t width, std::uint16_t height) {
    return acquire_idle_texture(width, height);
}

void EfbCopyManager::mark_xfb_copied(
    XfbTexture& texture,
    std::uint64_t frame_index) {
    texture.frame_stamp = frame_index;
    texture.copy_serial = next_copy_serial_++;
    texture.presentable = true;
    latest_slot_ = texture.slot;
    next_slot_ = (texture.slot + 1u) % kPictures;
    has_latest_ = true;
}

const XfbTexture* EfbCopyManager::latest() const {
    return has_latest_ ? picture(latest_slot_) : nullptr;
}

const XfbTexture* EfbCopyManager::picture(std::uint32_t slot) const {
    const auto it = registry_.find(slot);
    if (it == registry_.end()) {
        return nullptr;
    }
    return it->second.presentable ? &it->second : nullptr;
}


std::optional<EfbCopyRect> efb_identity_copy_rect(
    const EfbCopyParams& params, bool is_depth_copy, unsigned efb_scale,
    unsigned dest_width, unsigned dest_height) noexcept {
    if (is_depth_copy || params.copy_to_xfb || params.target_format != 6u ||
        params.intensity || params.yuv || (params.gamma & 3u) != 0u ||
        params.half_scale || params.scale_y || params.frame_to_field != 0u ||
        params.src_width == 0u || params.src_height == 0u ||
        static_cast<unsigned>(params.src_x) + params.src_width > kEfbWidth ||
        static_cast<unsigned>(params.src_y) + params.src_height > kEfbHeight) {
        return std::nullopt;
    }
    const unsigned upper = params.filter_upper();
    const unsigned middle = params.filter_middle();
    const unsigned lower = params.filter_lower();
    // All-zero startup registers use middle64 in the conversion shader too.
    if (upper != 0u || lower != 0u || (middle != 0u && middle != 64u)) {
        return std::nullopt;
    }
    const unsigned scale = std::clamp(efb_scale, 1u, kMaxEfbScale);
    if (dest_width != params.src_width * scale ||
        dest_height != params.src_height * scale) {
        return std::nullopt;
    }
    const std::uint32_t left = params.src_x * scale;
    const std::uint32_t top = params.src_y * scale;
    return EfbCopyRect{left, top, left + dest_width, top + dest_height};
}

}  // namespace galaxy::gx

#if !defined(NEBULA_GPU_VULKAN)
namespace galaxy::gx::gpu {

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
    // ALLOW_RENDER_TARGET so copy_efb_to_xfb can blit into it.
    desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    // Initial state is PIXEL_SHADER_RESOURCE: the present blit reads it before the first EFB
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

}  // namespace galaxy::gx::gpu
#endif
