#pragma once

// EfbCopyManager: tracks the XFB textures produced by GXCopyDisp.
//
// Presentation model: the game double/triple-buffers XFBs in guest memory; every
// PE_COPY_EXECUTE with copy_to_xfb set blits the EFB into the XFB texture
// registered at the copy's destination address. GxBackend presents the XFB
// selected by the guest VI/XFB manager; latest() is only the source when the
// caller has no explicit VI-selected address yet.
//
// The XFB is never converted to YUV and never written back to guest memory;
// a guest CPU read of the XFB range is a hard-fail tripwire on the
// native_host side, not handled here.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <wrl/client.h>

#include "galaxy/gx/gx_bitfields.h"
// kMaxEfbScale lives in render_config.h and std::clamp lives in <algorithm>.
// This header uses both, so it must include both itself: it previously compiled
// only because a transitive include supplied them, and that broke the moment the
// include order above it changed. Direct includes here, not elsewhere.
#include "galaxy/gx/render_config.h"

#include <algorithm>
#include <cstddef>
#include "galaxy/gx/pointer_response.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace galaxy::gx {

struct XfbTexture {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;  // RGBA8
    std::uint32_t guest_addr = 0;
    // Physical GPU dimensions after EFB scaling. The guest address/stride and
    // VI selection remain in logical Wii units and are never multiplied.
    std::uint16_t width = 0;
    std::uint16_t height = 0;       // after logical y-scale and EFB scale
    std::uint64_t frame_stamp = 0;  // frame index of the last copy into it
    PointerResponseStamp pointer_response{};
    bool prerecorded_movie{};
    // Scene-owned surround travels with this copy, never with a later producer.
    float safety_surround{};
    std::uint64_t copy_serial = 0;  // monotonically published copy order
    // Render-thread-only measurement state. Because a resident texture has
    // exactly one current globally unique copy_serial, this is sufficient to
    // count the first presentation of every copy exactly without an unbounded
    // global serial set.
    mutable std::uint64_t last_accounted_present_serial =
        ~std::uint64_t{0};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    bool presentable = false;
    bool rtv_valid = false;
};

// Bounded, exact accounting for monotonically assigned EFB -> XFB copy
// serials. Exact identity comes from the resident texture's accounted serial;
// the global high-water below is diagnostic-only (order/gap reporting). No
// pixel or content identity is inferred here.
struct XfbSerialAccountingState {
    bool has_presented = false;
    std::uint64_t highest_presented_serial = 0;
    std::uint64_t last_presented_serial = 0;
    std::uint64_t current_nonfirst_run = 0;
};

struct XfbSerialAccountingEvent {
    bool first_presentation = false;
    bool adjacent_repeat = false;
    bool not_first_presentation = false;
    bool below_high_water = false;
    bool delta_one = false;
    bool delta_multi = false;
    std::uint64_t nonfirst_run = 0;
};

[[nodiscard]] XfbSerialAccountingEvent account_xfb_present_serial(
    XfbSerialAccountingState& state,
    std::uint64_t& texture_accounted_serial,
    std::uint64_t copy_serial) noexcept;

struct XfbPresentSelection {
    const XfbTexture* selected = nullptr;
    bool requested_stale = false;
    bool stale_preserved = false;
    bool stale_expired = false;
    bool missing = false;
    bool missing_preserved = false;
    bool missing_expired = false;
};

[[nodiscard]] XfbPresentSelection select_xfb_for_present(
    bool requested_specific,
    const XfbTexture* requested,
    const XfbTexture* latest,
    const XfbTexture* last_valid_selected,
    std::uint64_t current_frame_stamp,
    std::uint64_t max_preserved_age,
    std::uint64_t last_presented_frame_stamp);

[[nodiscard]] std::uint16_t compute_xfb_copy_height(
    std::uint16_t src_height,
    std::uint32_t y_scale_raw,
    bool scale_y);

// GPU-only EFB-copy surfaces retain the configured internal resolution. Guest
// addresses, strides, and VI selection remain in native Wii units; only the
// backing D3D12 texture dimensions are scaled.
struct ScaledEfbCopyExtent {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
};

[[nodiscard]] ScaledEfbCopyExtent compute_scaled_efb_copy_extent(
    std::uint16_t logical_width,
    std::uint16_t logical_height,
    unsigned efb_scale) noexcept;

// Default-off Galaxy bloom experiment. The observed late color workspaces
// are distinguished from earlier mask/depth passes by their copy rectangle,
// format, stride and flags, not only by dimensions. Guest memory is unchanged.
[[nodiscard]] bool is_galaxy_bloom_workspace_copy(
    const EfbCopyParams& params,
    bool is_depth_copy) noexcept;

struct EfbCopySamplingGeometry {
    float src_x = 0.0f;
    float src_y = 0.0f;
    float step_x = 0.0f;
    float step_y = 0.0f;
    float filter_row_offset = 1.0f;
};

[[nodiscard]] EfbCopySamplingGeometry compute_efb_copy_sampling_geometry(
    const EfbCopyParams& params,
    unsigned dest_width,
    unsigned dest_height,
    unsigned efb_scale) noexcept;

// High-resolution display copies retain GX filter weights but sample adjacent
// physical source rows. Texture/depth copies keep their logical-row footprint.
[[nodiscard]] EfbCopySamplingGeometry compute_xfb_sampling_geometry(
    const EfbCopyParams& params,
    unsigned dest_width,
    unsigned dest_height,
    unsigned efb_scale) noexcept;

struct ScaledEfbRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;

    [[nodiscard]] bool empty() const noexcept {
        return right <= left || bottom <= top;
    }
};

// Both helpers below are called from `RendererD3D12::draw`, i.e. once per draw
// (tens of thousands of times per frame on a replayed display list). They lived
// in `efb_copy.cpp`, so that translation unit could not inline them back into
// the callers and every draw paid two real calls for twenty-odd instructions of
// clamps and multiplies. Defining them here changes no arithmetic and no result
// type; it only lets the compiler decide per call site.
[[nodiscard]] inline ScaledEfbRect compute_scaled_efb_rect(
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

struct ScaledGxViewport {
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

// XF layout: wd, ht, zRange, xOrig, yOrig, farZ. Defined here for the same
// per-draw inlining reason as `compute_scaled_efb_rect` above.
[[nodiscard]] inline ScaledGxViewport compute_scaled_gx_viewport(
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

struct EfbPeekSamplingGeometry {
    float src_x = 0.0f;
    float src_y = 0.0f;
    float step = 1.0f;
    float center_y = 0.5f;
    // A depth peek reads the single physical texel nearest the center of the
    // scaled logical pixel.  At even scales the center lies on a texel
    // boundary, so point-sampling semantics select the positive X/Y texel:
    // physical = logical * scale + floor(scale / 2).
    std::uint32_t depth_texel_x = 0;
    std::uint32_t depth_texel_y = 0;
    unsigned color_reduction_samples = 1;
};

[[nodiscard]] EfbPeekSamplingGeometry compute_efb_peek_sampling_geometry(
    std::uint16_t x,
    std::uint16_t y,
    unsigned efb_scale) noexcept;

class EfbCopyManager {
public:
    bool initialize(ID3D12Device* device, unsigned efb_scale);
    void shutdown();
    void begin_frame(unsigned frame_slot, unsigned frames_in_flight);

    // Returns the XFB texture registered at the copy destination, creating
    // or resizing it to match the copy parameters (src size x y-scale).  The
    // caller (renderer) then records the EFB -> XFB blit into it.
    XfbTexture* acquire_xfb(
        const EfbCopyParams& params,
        std::uint64_t frame_index);
    void mark_xfb_copied(XfbTexture& texture, std::uint64_t frame_index);

    // Most recently copied-into XFB across all addresses; nullptr only
    // before the first display copy ever.
    [[nodiscard]] const XfbTexture* latest() const;

    // Returns the XFB texture registered for a VI-selected guest address.
    // The lookup accepts cached/uncached MEM1/MEM2 aliases and normalizes
    // them to the physical address form stored in GX copy destinations.
    [[nodiscard]] const XfbTexture* find_xfb(
        std::uint32_t guest_addr) const;

private:
    struct PooledTexture {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        std::uint16_t width = 0;
        std::uint16_t height = 0;
    };
    void retire_texture(XfbTexture& texture);
    void recycle_retired_textures(unsigned frame_slot);
    [[nodiscard]] Microsoft::WRL::ComPtr<ID3D12Resource>
        acquire_idle_texture(std::uint16_t width, std::uint16_t height);

    std::unordered_map<std::uint32_t, XfbTexture> registry_;
    std::uint32_t latest_addr_ = 0;
    std::uint64_t next_copy_serial_ = 1;
    bool has_latest_ = false;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    std::vector<std::vector<PooledTexture>> retired_textures_;
    std::vector<PooledTexture> idle_textures_;
    unsigned current_frame_slot_ = 0;
    unsigned efb_scale_ = 1;
};

}  // namespace galaxy::gx
