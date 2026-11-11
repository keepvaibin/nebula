// SPDX-License-Identifier: GPL-3.0-only
// What is seen through glass, crystal, ice and water, bent (rt_transmit.hlsl; the master plan's 13.2 High, 13.3 High;
// work list 22.14): one pass over the second layer, after its light pass, High only. Its picture (T) goes to the
// programs as picture 6 (program.hlsli traced_mirror): where it holds a value the blended surface covers what lies
// behind with T, Fresnel and its own body over it, instead of blending with it.
#include "galaxy/render/render_core.h"

#include "galaxy/launch_args.h"

namespace galaxy::render {

bool RenderCore::rt_pass_transmit_on() const {
    return rt_part(get_render_config(), RtPart::Transmit) >= 3u && !rt_fb_reference() &&
           !::galaxy::diagnosing(::galaxy::kDiagnoseRtAdded) && !::galaxy::diagnosing(::galaxy::kDiagnoseRtMotion);
}

void RenderCore::rt_pass_transmit() {
    const RtFrame& frame = rt_light_frame_;
    rt_transmit_drawn_ = false;
    if (frame.layer != 1u || !rt_pass_transmit_on()) {
        return;
    }
    const std::uint32_t out = rt_image(RtImage::Transmit);
    if (out == rt::kNoImage) {
        return;
    }
    rt::PassBinds binds;
    std::uint32_t shade_at = 0u;
    if (!rt_shade_binds(binds, shade_at)) {
        return;
    }
    const float none[3][4] = {};
    binds.pass_constants = rt_bounce_constants(0u, 0u, shade_at, none);
    binds.reads[0] = rt_layer_guide();  // the second layer's (its normals and motion are in the usual images now)
    binds.reads[1] = rt_image(RtImage::Normal);
    binds.reads[2] = rt_image(RtImage::Motion);
    binds.writes[0] = out;
    rt_backend_.dispatch(RtShader::Transmit, binds, frame.across, frame.down);
    rt_transmit_drawn_ = true;
}

}  // namespace galaxy::render
