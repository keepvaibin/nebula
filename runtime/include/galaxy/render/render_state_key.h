#pragma once

// The fixed-function state a native pipeline is built for (E-04): blending, depth, culling, the scene target's pixel format and
// variant, and the primitive topology. Native materials' shaders are compiled at build; a pipeline is a shader pair
// under one of these.

#include "galaxy/render/drawing_state.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace galaxy::render {

inline constexpr std::uint8_t kTopologyTypeMask = 0x0Fu;

// RenderStateKey::samples_log2 bit 2: the draw renders into the ray tracer's HDR scene scene target
// (RGBA16F) instead of the RGBA8 scene target. Ray tracing runs without MSAA, so the bits never combine.
inline constexpr std::uint8_t kSceneVariantHdr = 4u;
inline constexpr std::uint8_t kSceneVariantSamplesMask = 3u;

// Fixed-function pipeline state, kept apart from the shader keys so one compiled shader pair
// serves every blend and depth variant.
struct RenderStateKey {
    std::uint32_t blend_bits;       // see build_render_state_key; bit27 dual-source alpha,
                                    // bit28 constant scene target alpha overwrite
    std::uint16_t zmode_bits;       // canonical depth mode
    std::uint8_t cull;              // CullMode
    std::uint8_t pixfmt;            // 1: the frame keeps alpha (destination alpha)
    std::uint8_t primitive_topology; // D3D12_PRIMITIVE_TOPOLOGY_TYPE value (bits 0-3)
    std::uint8_t samples_log2;      // the scene target's variant: bits 0-1 its samples per pixel (MSAA) as a power of
                                    // two (0 for one), bit 2 kSceneVariantHdr
    std::uint8_t reserved[6];

    // Field-wise, not byte-wise: PsoKey::hash() leaves out `reserved`, and equality must not be
    // finer than the hash, or an entry could become unreachable.
    [[nodiscard]] bool operator==(const RenderStateKey& other) const noexcept {
        return blend_bits == other.blend_bits &&
            zmode_bits == other.zmode_bits &&
            cull == other.cull &&
            pixfmt == other.pixfmt &&
            primitive_topology == other.primitive_topology &&
            samples_log2 == other.samples_log2;
    }
};
static_assert(sizeof(RenderStateKey) == 16);
static_assert(std::has_unique_object_representations_v<RenderStateKey>);

[[nodiscard]] RenderStateKey build_render_state_key(const NativeDrawState& state);

// Whether the device has a second blend source (a Vulkan device's dualSrcBlend; every D3D12 device). Without one a
// draw blends at its one output's alpha, which is then the material's, and a constant destination alpha (0, every one the
// game sets) comes from the alpha channel's own blend (kRenderStateZeroAlpha).
void set_dual_source_blending(bool available) noexcept;
[[nodiscard]] bool dual_source_blending() noexcept;
// `key` as this device draws it.
[[nodiscard]] RenderStateKey for_device_blending(RenderStateKey key) noexcept;

}  // namespace galaxy::render
