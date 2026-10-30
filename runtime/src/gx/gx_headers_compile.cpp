// Builds the core galaxy/gx headers in one unit under /W4 /WX /permissive- and checks constexpr helpers at compile
// time.

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/gx_backend.h"
#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/matrix_memory.h"
#include "galaxy/gx/pipeline_cache.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/resident_vertex_arena.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/texture_decode_pool.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/gx_vertex.h"

namespace galaxy::gx {
namespace {

// Blends as typed values: enable, logic op, dither, colour and alpha update, destination and source factors, subtract,
// logic mode, constant alpha.
constexpr BlendMode blend_of(bool blend, bool logic, BlendFactor dst, BlendFactor src, bool subtract, std::uint8_t mode) {
    return BlendMode{blend, logic, false, true, false, dst, src, subtract, mode, false, 0u};
}
constexpr auto kBlend = blend_of(true, false, BlendFactor::InvSrcAlpha, BlendFactor::SrcAlpha, false, 0u);

// effective_blend: an alpha blend stays a blend; BLEND ONE ZERO and logic COPY write the source (no blend, no logic op);
// DSTALPHA / INVDSTALPHA write the source only where the EFB has no alpha; a subtract of ONE ZERO still subtracts.
static_assert(effective_blend(kBlend, true).blend_enable);
static_assert(effective_blend(kBlend, false).blend_enable);
constexpr auto kBlendOneZero = blend_of(true, false, BlendFactor::Zero, BlendFactor::One, false, 0u);
static_assert(kBlendOneZero.blend_enable && !effective_blend(kBlendOneZero, true).blend_enable);
static_assert(!effective_blend(kBlendOneZero, false).logic_op_enable);
static_assert(effective_blend(kBlendOneZero, true).color_update);
constexpr auto kBlendDstAlpha = blend_of(true, false, BlendFactor::InvDstAlpha, BlendFactor::DstAlpha, false, 0u);
static_assert(!effective_blend(kBlendDstAlpha, false).blend_enable);
static_assert(effective_blend(kBlendDstAlpha, true).blend_enable);
constexpr auto kSubtractOneZero = blend_of(true, false, BlendFactor::Zero, BlendFactor::One, true, 0u);
static_assert(effective_blend(kSubtractOneZero, true).blend_enable && effective_blend(kSubtractOneZero, true).subtract);
constexpr auto kLogicCopyMode = blend_of(false, true, BlendFactor::Zero, BlendFactor::Zero, false, 3u);
static_assert(!effective_blend(kLogicCopyMode, true).logic_op_enable);
constexpr auto kLogicXor = blend_of(false, true, BlendFactor::Zero, BlendFactor::Zero, false, 6u);
static_assert(effective_blend(kLogicXor, true).logic_op_enable);

// TEX_SETIMAGE0/3: 640x480 RGB565 at physical 0x01000000.
constexpr auto kTexImage = decode_tex_image(
    (640u - 1u) | ((480u - 1u) << 10) | (4u << 20), 0x01000000u >> 5);
static_assert(kTexImage.width == 640);
static_assert(kTexImage.height == 480);
static_assert(kTexImage.format == TexFormat::RGB565);
static_assert(kTexImage.guest_addr == 0x01000000u);

constexpr EfbCopyParams make_efb_copy_filter(
    std::uint32_t w0,
    std::uint32_t w1,
    std::uint32_t w2,
    std::uint32_t w3,
    std::uint32_t w4,
    std::uint32_t w5,
    std::uint32_t w6) {
    EfbCopyParams params{};
    const std::uint32_t taps[7] = {w0, w1, w2, w3, w4, w5, w6};
    for (unsigned i = 0; i < 7u; ++i) {
        params.vfilter[i] = static_cast<std::uint8_t>(taps[i] & 63u);
    }
    return params;
}

// Every tap is distinct, so a tap moved to the wrong row fails an assertion.
constexpr auto kAsymmetricEfbCopyFilter =
    make_efb_copy_filter(1u, 2u, 4u, 8u, 16u, 32u, 63u);
static_assert(kAsymmetricEfbCopyFilter.filter_upper() == 3u);
static_assert(kAsymmetricEfbCopyFilter.filter_middle() == 28u);
static_assert(kAsymmetricEfbCopyFilter.filter_lower() == 95u);

constexpr auto kStandardEfbCopyFilter =
    make_efb_copy_filter(0u, 0u, 21u, 22u, 21u, 0u, 0u);
static_assert(kStandardEfbCopyFilter.filter_upper() == 0u);
static_assert(kStandardEfbCopyFilter.filter_middle() == 64u);
static_assert(kStandardEfbCopyFilter.filter_lower() == 0u);

// D3D12 constant buffers start on 256-byte boundaries. A layout change also needs the matching HLSL edit and a
// kShaderCacheVersion bump (pipeline_cache.cpp).
static_assert(sizeof(GxPsConstants) % 256 == 0);
static_assert(sizeof(GxVsConstants) % 256 == 0);

}  // namespace
}  // namespace galaxy::gx
