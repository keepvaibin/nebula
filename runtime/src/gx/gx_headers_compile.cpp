// Builds the core galaxy/gx headers in one unit under /W4 /WX /permissive- and checks the constexpr register
// decoders at compile time.

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_backend.h"
#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/pipeline_cache.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/resident_vertex_arena.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/texture_decode_pool.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/vertex_loader.h"

namespace galaxy::gx {
namespace {

// GEN_MODE with 2 texgens, 1 color chan, 8 TEV stages (count-1 = 7),
// back-face culling, no indirect stages.
constexpr auto kGenMode = decode_gen_mode(
    (7u << 10) | (2u << 14) | (1u << 4) | 2u);
static_assert(kGenMode.num_tev_stages == 8);
static_assert(kGenMode.num_texgens == 2);
static_assert(kGenMode.num_color_chans == 1);
static_assert(kGenMode.cull == CullMode::Back);
static_assert(kGenMode.num_ind_stages == 0);

// ZMODE: test on, LEQUAL, update on.
constexpr auto kZMode = decode_z_mode(0x17u);
static_assert(kZMode.test_enable);
static_assert(kZMode.func == CompareFunc::LEqual);
static_assert(kZMode.update_enable);

// CMODE0: blend on, color/alpha update, src = SrcAlpha, dst = InvSrcAlpha.
constexpr auto kBlend = decode_blend(
    1u | (1u << 3) | (1u << 4) | (5u << 5) | (4u << 8), 0u);
static_assert(kBlend.blend_enable);
static_assert(kBlend.src_factor == BlendFactor::SrcAlpha);
static_assert(kBlend.dst_factor == BlendFactor::InvSrcAlpha);
static_assert(!kBlend.subtract);

// effective_blend: an alpha blend stays a blend; BLEND ONE ZERO and logic COPY write the source (no blend, no logic op);
// DSTALPHA / INVDSTALPHA write the source only where the EFB has no alpha; a subtract of ONE ZERO still subtracts.
static_assert(effective_blend(kBlend, true).blend_enable);
static_assert(effective_blend(kBlend, false).blend_enable);
constexpr auto kBlendOneZero = decode_blend(1u | (1u << 3) | (0u << 5) | (1u << 8), 0u);
static_assert(kBlendOneZero.blend_enable && !effective_blend(kBlendOneZero, true).blend_enable);
static_assert(!effective_blend(kBlendOneZero, false).logic_op_enable);
static_assert(effective_blend(kBlendOneZero, true).color_update);
constexpr auto kBlendDstAlpha = decode_blend(1u | (1u << 3) | (7u << 5) | (6u << 8), 0u);
static_assert(!effective_blend(kBlendDstAlpha, false).blend_enable);
static_assert(effective_blend(kBlendDstAlpha, true).blend_enable);
constexpr auto kSubtractOneZero = decode_blend(1u | (1u << 3) | (0u << 5) | (1u << 8) | (1u << 11), 0u);
static_assert(effective_blend(kSubtractOneZero, true).blend_enable && effective_blend(kSubtractOneZero, true).subtract);
constexpr auto kLogicCopyMode = decode_blend((1u << 1) | (1u << 3) | (3u << 12), 0u);
static_assert(!effective_blend(kLogicCopyMode, true).logic_op_enable);
constexpr auto kLogicXor = decode_blend((1u << 1) | (1u << 3) | (6u << 12), 0u);
static_assert(effective_blend(kLogicXor, true).logic_op_enable);

// TEX_SETIMAGE0/3: 640x480 RGB565 at physical 0x01000000.
constexpr auto kTexImage = decode_tex_image(
    (640u - 1u) | ((480u - 1u) << 10) | (4u << 20), 0x01000000u >> 5);
static_assert(kTexImage.width == 640);
static_assert(kTexImage.height == 480);
static_assert(kTexImage.format == TexFormat::RGB565);
static_assert(kTexImage.guest_addr == 0x01000000u);

constexpr auto kEfbCopyNoAutoConv = decode_efb_copy(
    0u, 0u, 0u, 0u, 256u, 0u, 0u, 0u, 1u << 15);
static_assert(kEfbCopyNoAutoConv.intensity_format);
static_assert(!kEfbCopyNoAutoConv.auto_conv);
static_assert(!kEfbCopyNoAutoConv.intensity);

constexpr auto kEfbCopyIntensity = decode_efb_copy(
    0u, 0u, 0u, 0u, 256u, 0u, 0u, 0u, (1u << 15) | (1u << 16));
static_assert(kEfbCopyIntensity.intensity_format);
static_assert(kEfbCopyIntensity.auto_conv);
static_assert(kEfbCopyIntensity.intensity);

constexpr auto kEfbCopyFormatSwizzle = decode_efb_copy(
    0u, 0u, 0u, 0u, 256u, 0u, 0u, 0u, 1u << 3);
static_assert(kEfbCopyFormatSwizzle.target_format == 8u);

constexpr EfbCopyParams make_efb_copy_filter(
    std::uint32_t w0,
    std::uint32_t w1,
    std::uint32_t w2,
    std::uint32_t w3,
    std::uint32_t w4,
    std::uint32_t w5,
    std::uint32_t w6) {
    EfbCopyParams params{};
    params.filter0_raw = w0 | (w1 << 6) | (w2 << 12) | (w3 << 18);
    params.filter1_raw = w4 | (w5 << 6) | (w6 << 12);
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
