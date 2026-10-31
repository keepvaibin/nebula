#pragma once
// The native materials' shaders (E-04; runtime/src/render/materials/<name>.hlsl), compiled at build: DXIL in the
// D3D12 build, SPIR-V in the Vulkan build (the generated native_shader_table.cpp of each).

#include "galaxy/render/shader_keys.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace galaxy::render {

struct NativeShaderBytes {
    const void* data = nullptr;
    std::size_t size = 0;
};

// Output variants (native_material.hlsli): the targets a pipeline's render state names.
enum NativeShaderVariant : unsigned {
    kNativeVariantBase = 0u,
    kNativeVariantReactive = 1u,  // + the reactive mask
    kNativeVariantLayer = 2u,     // + the reactive mask and the effects layer's targets
    kNativeVariantMotion = 3u,    // + the motion vector
    kNativeVariants = 4u,
};

[[nodiscard]] constexpr unsigned native_shader_variant(std::uint32_t blend_bits) noexcept {
    return (blend_bits & kRenderStateLayerTarget) != 0u      ? kNativeVariantLayer
           : (blend_bits & kRenderStateMotionTarget) != 0u   ? kNativeVariantMotion
           : (blend_bits & kRenderStateReactiveTarget) != 0u ? kNativeVariantReactive
                                                             : kNativeVariantBase;
}

// `shader` (NebulaShaderId)'s vertex (pixel false) or pixel shader in `variant`; empty when the build has none.
[[nodiscard]] NativeShaderBytes native_shader_bytes(std::uint32_t shader, unsigned variant, bool pixel) noexcept;

// A native pipeline's render state: the draw's, without dual-source blending (a native shader writes one colour).
[[nodiscard]] inline RenderStateKey native_render_state(RenderStateKey rs) noexcept {
    rs.blend_bits &= ~((1u << 27u) | (1u << 28u));
    std::memset(rs.reserved, 0, sizeof(rs.reserved));
    return rs;
}

}  // namespace galaxy::render
