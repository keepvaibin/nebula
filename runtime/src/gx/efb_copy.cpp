// efb_copy.cpp — EfbCopyManager: XFB texture registry for display copies.
//
// Threading: all methods are render-thread only.  The registry_ map and
// device_ ComPtr are never shared with other threads.

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/render_config.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
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

XfbPresentSelection select_xfb_for_present(
    bool requested_specific,
    const XfbTexture* requested,
    const XfbTexture* latest,
    const XfbTexture* last_valid_selected,
    std::uint64_t current_frame_stamp,
    std::uint64_t max_preserved_age,
    std::uint64_t last_presented_frame_stamp) {
    XfbPresentSelection selection{};
    static_cast<void>(max_preserved_age);
    static_cast<void>(last_presented_frame_stamp);
    static_cast<void>(current_frame_stamp);
    if (!requested_specific) {
        selection.selected = latest;
        return selection;
    }

    if (requested != nullptr) {
        if (latest != nullptr && requested->frame_stamp < latest->frame_stamp) {
            selection.requested_stale = true;
            // A valid explicit VI-selected XFB may legitimately trail the
            // newest GX copy in a double/triple-buffered game. Once the guest
            // names an XFB, the native renderer must honor that selection
            // instead of falling forward to latest(); latest() is only for
            // unknown/pre-VI requests.
            selection.stale_preserved = true;
        }
        selection.selected = requested;
        return selection;
    }

    selection.missing = true;
    if (last_valid_selected != nullptr) {
        selection.selected = last_valid_selected;
        selection.missing_preserved = true;
    }
    return selection;
}

namespace {

constexpr std::size_t kMaxIdleXfbTextures = 8u;

[[nodiscard]] std::uint32_t normalize_xfb_guest_addr(
    std::uint32_t address) noexcept {
    // GX copy destination registers are physical byte addresses. JUTXfb keeps
    // CPU pointers, so compare after removing the cached/uncached alias bases.
    if (address >= 0x80000000u && address < 0x81800000u) {
        return address - 0x80000000u;
    }
    if (address >= 0xC0000000u && address < 0xC1800000u) {
        return address - 0xC0000000u;
    }
    if (address >= 0x90000000u && address < 0x94000000u) {
        return 0x10000000u + (address - 0x90000000u);
    }
    if (address >= 0xD0000000u && address < 0xD4000000u) {
        return 0x10000000u + (address - 0xD0000000u);
    }
    return address;
}

}  // namespace

// ---------------------------------------------------------------------------
// initialize / shutdown
// ---------------------------------------------------------------------------

bool EfbCopyManager::initialize(ID3D12Device* device, unsigned efb_scale) {
    device_ = device;
    efb_scale_ = std::clamp(efb_scale, 1u, kMaxEfbScale);
    return device_ != nullptr;
}

void EfbCopyManager::shutdown() {
    // Release all D3D12 resources; std::unordered_map destruction releases
    // the ComPtrs automatically, but we clear explicitly for clarity.
    registry_.clear();
    retired_textures_.clear();
    idle_textures_.clear();
    has_latest_ = false;
    latest_addr_ = 0;
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
                // An idle resource is already fence-safe to release. Admit
                // current sizes instead of keeping eight cold sizes forever.
                idle_textures_.erase(idle_textures_.begin());
            }
            idle_textures_.push_back(std::move(texture));
        }
    }
    retired.clear();
}

Microsoft::WRL::ComPtr<ID3D12Resource> EfbCopyManager::acquire_idle_texture(
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

// ---------------------------------------------------------------------------
// acquire_xfb
// ---------------------------------------------------------------------------

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
    constexpr unsigned kMaxTextureDimension =
        D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
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
    // Captured RMGE01 color-mask bloom chain: 160x114 bright/blur
    // workspace at (480,0), downsampled80x57, blur result at(320,114),
    // then final160x114 color composite at(480,114).
    // Exclude(0,0)/(320,0) early masks/focus work even when format matches.
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
    // The scaled VRAM copy filter operates on physical source texels, as in
    // Dolphin TextureCacheBase::CopyEFBToCacheEntry's scaled pixel_height.
    // Preserve origin, resampling, coefficients, gamma and clamp behavior.
    geometry.filter_row_offset = 1.0f;
    return geometry;
}

ScaledEfbRect compute_scaled_efb_rect(
    std::int32_t logical_left,
    std::int32_t logical_top,
    std::int32_t logical_right,
    std::int32_t logical_bottom,
    unsigned logical_target_width,
    unsigned logical_target_height,
    unsigned efb_scale) noexcept {
    const std::int64_t scale = static_cast<std::int64_t>(
        std::clamp(efb_scale, 1u, kMaxEfbScale));
    // The public helper accepts arbitrary unsigned target extents, while its
    // D3D12_RECT-compatible result is signed 32-bit. Saturate before the
    // narrowing cast even though production EFB extents are much smaller.
    constexpr std::int64_t kRectLimit =
        std::numeric_limits<std::int32_t>::max();
    const std::int64_t target_width = std::min(
        static_cast<std::int64_t>(logical_target_width) * scale,
        kRectLimit);
    const std::int64_t target_height = std::min(
        static_cast<std::int64_t>(logical_target_height) * scale,
        kRectLimit);
    const auto scaled_clamped = [scale](
                                    std::int32_t coordinate,
                                    std::int64_t maximum) {
        return static_cast<std::int32_t>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(coordinate) * scale,
            0,
            maximum));
    };
    return ScaledEfbRect{
        scaled_clamped(logical_left, target_width),
        scaled_clamped(logical_top, target_height),
        scaled_clamped(logical_right, target_width),
        scaled_clamped(logical_bottom, target_height)};
}

ScaledGxViewport compute_scaled_gx_viewport(
    const float xf_viewport[6],
    unsigned efb_scale) noexcept {
    if (xf_viewport == nullptr) {
        return {};
    }
    const float scale = static_cast<float>(
        std::clamp(efb_scale, 1u, kMaxEfbScale));
    const float wd = xf_viewport[0];
    const float ht = xf_viewport[1];
    const float z_range = xf_viewport[2];
    const float x_origin = xf_viewport[3] - 342.0f;
    const float y_origin = xf_viewport[4] - 342.0f;
    const float far_z = xf_viewport[5];
    constexpr float kZ24 = 16777216.0f;
    float min_depth = std::clamp(1.0f - far_z / kZ24, 0.0f, 1.0f);
    const float max_depth = std::clamp(
        1.0f - (far_z - z_range) / kZ24,
        0.0f,
        1.0f);
    if (min_depth > max_depth) {
        min_depth = max_depth;
    }
    return ScaledGxViewport{
        (x_origin - wd) * scale,
        (y_origin + ht) * scale,
        2.0f * wd * scale,
        -2.0f * ht * scale,
        min_depth,
        max_depth};
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

XfbTexture* EfbCopyManager::acquire_xfb(
    const EfbCopyParams& params,
    std::uint64_t frame_index) {
    const std::uint32_t registry_addr =
        normalize_xfb_guest_addr(params.dest_addr);

    const std::uint16_t logical_h =
        compute_xfb_copy_height(
            params.src_height,
            params.y_scale_raw,
            params.scale_y);
    const ScaledEfbCopyExtent extent = compute_scaled_efb_copy_extent(
        params.src_width,
        logical_h,
        efb_scale_);
    const std::uint16_t wanted_w = extent.width;
    const std::uint16_t wanted_h = extent.height;

    // Look up or insert an entry keyed by guest destination address.
    auto it = registry_.find(registry_addr);
    if (it != registry_.end()) {
        XfbTexture& entry = it->second;
        // A failed allocation can leave an entry with matching dimensions
        // but no resource. Retry allocation instead of returning that entry.
        if (entry.width == wanted_w && entry.height == wanted_h &&
            entry.texture) {
            static_cast<void>(frame_index);
            return &entry;
        }
        // Wrong size — drop the old texture and recreate below.
        if (entry.texture) {
            retire_texture(entry);
        }
        entry.rtv = {};
        entry.rtv_valid = false;
    } else {
        // Insert a default-constructed entry.
        auto [ins_it, ok] = registry_.emplace(registry_addr, XfbTexture{});
        (void)ok;
        it = ins_it;
    }

    // Create or recreate the XFB texture.
    XfbTexture& entry = it->second;
    entry.guest_addr  = registry_addr;
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

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment          = 0;
    desc.Width              = wanted_w;
    desc.Height             = wanted_h;
    desc.DepthOrArraySize   = 1;
    desc.MipLevels          = 1;
    desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    // ALLOW_RENDER_TARGET so copy_efb_to_xfb can blit into it.
    desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    // Initial state: PIXEL_SHADER_RESOURCE — we read it in the present blit
    // before the first EFB copy transitions it to RENDER_TARGET.
    const D3D12_RESOURCE_STATES initial_state =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // Clear value for the render target allocation hint (optional but avoids
    // debug-layer warnings when the texture is first used as an RTV).
    D3D12_CLEAR_VALUE clear_val{};
    clear_val.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    clear_val.Color[0] = 0.0f;
    clear_val.Color[1] = 0.0f;
    clear_val.Color[2] = 0.0f;
    clear_val.Color[3] = 1.0f;

    const HRESULT hr = device_->CreateCommittedResource(
        &heap_props,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initial_state,
        &clear_val,
        IID_PPV_ARGS(entry.texture.ReleaseAndGetAddressOf()));

    if (FAILED(hr)) {
        std::ostringstream message;
        message << "[EfbCopyManager] XFB texture allocation failed"
                << " addr=0x" << std::hex << registry_addr
                << " size=" << std::dec << wanted_w << 'x' << wanted_h
                << " hr=0x" << std::hex
                << static_cast<unsigned long>(hr);
        throw std::runtime_error(message.str());
    }

    return &entry;
}

void EfbCopyManager::mark_xfb_copied(
    XfbTexture& texture,
    std::uint64_t frame_index) {
    texture.frame_stamp = frame_index;
    texture.copy_serial = next_copy_serial_++;
    texture.presentable = true;
    latest_addr_ = texture.guest_addr;
    has_latest_ = true;
}

// ---------------------------------------------------------------------------
// latest
// ---------------------------------------------------------------------------

const XfbTexture* EfbCopyManager::latest() const {
    if (!has_latest_) {
        return nullptr;
    }
    const auto it = registry_.find(latest_addr_);
    if (it == registry_.end()) {
        return nullptr;
    }
    return it->second.presentable ? &it->second : nullptr;
}

const XfbTexture* EfbCopyManager::find_xfb(
    std::uint32_t guest_addr) const {
    const auto it = registry_.find(normalize_xfb_guest_addr(guest_addr));
    if (it == registry_.end()) {
        return nullptr;
    }
    return it->second.presentable ? &it->second : nullptr;
}

}  // namespace galaxy::gx
