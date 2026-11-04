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
// Numbers from here on are shaders a converted mod brings (mod_shaders.h).
inline constexpr std::uint32_t kModShaderIdFirst = 0x40000000u;

enum NativeShaderVariant : unsigned {
    kNativeVariantBase = 0u,
    kNativeVariantReactive = 1u,  // + the reactive mask
    kNativeVariantLayer = 2u,     // + the reactive mask and the effects layer's targets
    kNativeVariantMotion = 3u,    // + the motion vector
    kNativeVariantDual = 4u,      // + the material alpha for dual-source blending (render state bit 27)
    kNativeVariantSample = 5u,      // an alpha-tested material under MSAA: base, per sample
    kNativeVariantSampleDual = 6u,  // dual, per sample
    kNativeVariantPixel = 7u,            // lit at the pixel (j3d_material.hlsli), plainly or enhanced
    kNativeVariantPixelDual = 8u,
    kNativeVariantPixelSample = 9u,
    kNativeVariantPixelSampleDual = 10u,
    kNativeVariantPixelReactive = 11u,
    kNativeVariantPixelLayer = 12u,
    kNativeVariantPixelMotion = 13u,
    kNativeVariants = 14u,
};

[[nodiscard]] constexpr unsigned native_shader_variant(std::uint32_t blend_bits) noexcept {
    return (blend_bits & kRenderStateLayerTarget) != 0u      ? kNativeVariantLayer
           : (blend_bits & kRenderStateMotionTarget) != 0u   ? kNativeVariantMotion
           : (blend_bits & kRenderStateReactiveTarget) != 0u ? kNativeVariantReactive
           : (blend_bits & (1u << 27u)) != 0u                ? kNativeVariantDual
                                                             : kNativeVariantBase;
}

// `shader` (NebulaShaderId)'s vertex (pixel false) or pixel shader in `variant`; empty when the build has none.
// The variant a draw takes. In a target of 2^samples_log2 samples an alpha-tested material shades per sample, where it has
// that variant and the device can (per_sample); lit at the pixel where the material lights and the setting asks.
[[nodiscard]] unsigned native_draw_variant(std::uint32_t shader, unsigned variant, unsigned samples_log2, bool per_sample,
                                           bool pixel_lit) noexcept;
[[nodiscard]] constexpr bool native_variant_dual(unsigned variant) noexcept {
    return variant == kNativeVariantDual || variant == kNativeVariantSampleDual || variant == kNativeVariantPixelDual ||
           variant == kNativeVariantPixelSampleDual;
}

[[nodiscard]] NativeShaderBytes native_shader_bytes(std::uint32_t shader, unsigned variant, bool pixel) noexcept;

// A native pipeline's render state: the draw's (its dual-source and constant-alpha bits the shader's dual variant
// and PE word serve).
[[nodiscard]] inline RenderStateKey native_render_state(RenderStateKey rs) noexcept {
    std::memset(rs.reserved, 0, sizeof(rs.reserved));
    return rs;
}

}  // namespace galaxy::render
