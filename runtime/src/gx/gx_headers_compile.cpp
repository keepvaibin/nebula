// Includes every galaxy/gx header so each stays self-contained and clean under
// /W4 /WX /permissive-. The constexpr decoders are exercised at compile time.

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/gx_backend.h"
#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/pipeline_cache.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/texture_cache.h"
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

// GPU-shared constant layouts must keep this shape; changing them requires the
// matching HLSL edit and a shader-cache version bump.
static_assert(sizeof(GxPsConstants) % 256 == 0);
static_assert(sizeof(GxVsConstants) % 256 == 0);

}  // namespace
}  // namespace galaxy::gx
