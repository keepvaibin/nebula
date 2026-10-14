// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/rt_surfaces.h"

#include "galaxy/gx/gx_bitfields.h"

#include <algorithm>
#include <iterator>

namespace galaxy::gx {
namespace {

constexpr unsigned kTexColor = static_cast<unsigned>(TevColorArg::TexColor);
constexpr unsigned kTexAlpha = static_cast<unsigned>(TevColorArg::TexAlpha);
constexpr unsigned kRasColor = static_cast<unsigned>(TevColorArg::RasColor);
constexpr unsigned kRasAlpha = static_cast<unsigned>(TevColorArg::RasAlpha);
constexpr unsigned kZero = static_cast<unsigned>(TevColorArg::Zero);

bool lit(std::uint32_t channel) {
    const XfChannelCtrl ctrl = decode_xf_channel_ctrl(channel);
    return ctrl.lighting_enable && ctrl.light_mask != 0u;
}

bool texture_input(unsigned input) {
    return input == kTexColor || input == kTexAlpha;
}

}  // namespace

RtSurface classify_rt_surface(const VertexShaderKey& vs, const PixelShaderKey& ps) noexcept {
    RtSurface surface{};
    std::uint32_t features = 0u;
    bool lit_colour = false;
    const unsigned stages = std::min<unsigned>(ps.num_tev_stages, 16u);
    const unsigned texgens = std::min<unsigned>(vs.num_texgens, 8u);
    for (unsigned s = 0; s < stages; ++s) {
        const TevStagePacked& stage = ps.stages[s];
        // Colour combiner: d + lerp(a, b, c).
        const unsigned d = stage.color_env & 0xFu;
        const unsigned c = (stage.color_env >> 4u) & 0xFu;
        const unsigned b = (stage.color_env >> 8u) & 0xFu;
        const unsigned a = (stage.color_env >> 12u) & 0xFu;
        const unsigned inputs[4] = {d, c, b, a};
        const auto reads = [&](unsigned x) {
            return std::find(std::begin(inputs), std::end(inputs), x) != std::end(inputs);
        };
        const bool texture = ((stage.order >> 6u) & 1u) != 0u && (reads(kTexColor) || reads(kTexAlpha));
        const unsigned coord = (stage.order >> 3u) & 7u;
        if (texture && coord < texgens) {
            const XfTexGen texgen = decode_xf_texgen(vs.texgen[coord]);
            if (texgen.type == TexGenType::Regular && texgen.source_row == 1u) {
                // Added: d is the lookup, or d is another colour plus the lookup times a factor.
                const bool added = texture_input(d) ||
                                   (a == kZero && d != kZero && (texture_input(b) || texture_input(c)));
                features |= kRtSurfaceEnvMap | (added ? kRtSurfaceEnvAdded : kRtSurfaceEnvBlended);
                if (surface.env_stage == 0xFFu) {
                    surface.env_stage = static_cast<std::uint8_t>(s);
                }
            } else if (texgen.type == TexGenType::Regular && texgen.source_row == 0u) {
                features |= kRtSurfaceProjected;
            }
        }
        const unsigned ras = (stage.order >> 7u) & 7u;
        if (ras < 2u && ras < vs.num_chans) {
            const bool reads_colour = reads(kRasColor);
            const bool reads_alpha = reads(kRasAlpha);
            if ((reads_colour && lit(vs.channel[ras])) || (reads_alpha && lit(vs.channel[2u + ras]))) {
                lit_colour = true;
            }
            for (const unsigned channel : {ras, 2u + ras}) {
                if ((channel == ras ? reads_colour : reads_alpha) && lit(vs.channel[channel]) &&
                    decode_xf_channel_ctrl(vs.channel[channel]).atten_func == 1u) {
                    features |= kRtSurfaceSpecular;
                }
            }
        }
        if (ps.num_ind_stages != 0u && ps.ind_cmd[s / 4u][s & 3u] != 0u) {
            features |= kRtSurfaceIndirect;
        }
    }
    if (!lit_colour) {
        features |= kRtSurfaceUnlit;
    }
    surface.features = static_cast<std::uint8_t>(features);
    if ((features & kRtSurfaceIndirect) != 0u) {
        surface.surface_class = RtSurfaceClass::Water;
    } else if ((features & kRtSurfaceEnvMap) != 0u) {
        surface.surface_class = RtSurfaceClass::Metal;
    } else if ((features & kRtSurfaceSpecular) != 0u) {
        surface.surface_class = RtSurfaceClass::Glossy;
    } else if ((features & kRtSurfaceUnlit) != 0u) {
        surface.surface_class = RtSurfaceClass::Unlit;
    }
    return surface;
}

}  // namespace galaxy::gx
