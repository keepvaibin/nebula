#pragma once
// The native materials' shaders (E-04; runtime/src/render/materials/<name>.hlsl), compiled at build: DXIL in the
// D3D12 build, SPIR-V in the Vulkan build (the generated native_shader_table.cpp of each).

#include "galaxy/render/pso_key.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace galaxy::render {

struct NativeShaderBytes {
    const void* data = nullptr;
    std::size_t size = 0;
};

// Output variants (native_material.hlsli): the targets a pipeline's render state names.
// The J3D material set's first NebulaShaderId (nebula_game_api.h NEBULA_SHADER_J3D_FIRST).
inline constexpr std::uint32_t kNativeShaderJ3DFirst = 256u;

enum NativeShaderVariant : unsigned {
    kNativeVariantBase = 0u,
    kNativeVariantReactive = 1u,  // + the reactive mask
    kNativeVariantLayer = 2u,     // + the reactive mask and the effects layer's targets
    kNativeVariantMotion = 3u,    // + the motion vector
    kNativeVariantDual = 4u,      // + the material alpha for dual-source blending (render state bit 27)
    kNativeVariants = 5u,
};

[[nodiscard]] constexpr unsigned native_shader_variant(std::uint32_t blend_bits) noexcept {
    return (blend_bits & kRenderStateLayerTarget) != 0u      ? kNativeVariantLayer
           : (blend_bits & kRenderStateMotionTarget) != 0u   ? kNativeVariantMotion
           : (blend_bits & kRenderStateReactiveTarget) != 0u ? kNativeVariantReactive
           : (blend_bits & (1u << 27u)) != 0u                ? kNativeVariantDual
                                                             : kNativeVariantBase;
}

// `shader` (NebulaShaderId)'s vertex (pixel false) or pixel shader in `variant`; empty when the build has none.
[[nodiscard]] NativeShaderBytes native_shader_bytes(std::uint32_t shader, unsigned variant, bool pixel) noexcept;

// The curved surfaces' hull (domain false) or domain stage for every native material (native_curved.hlsl), by
// whether the output layout carries motion vectors (kNativeVariantMotion).
[[nodiscard]] NativeShaderBytes native_curved_bytes(bool motion, bool domain) noexcept;


// A native pipeline's render state: the draw's (its dual-source and constant-alpha bits the shader's dual variant
// and PE word serve).
[[nodiscard]] inline RenderStateKey native_render_state(RenderStateKey rs) noexcept {
    std::memset(rs.reserved, 0, sizeof(rs.reserved));
    return rs;
}

}  // namespace galaxy::render
