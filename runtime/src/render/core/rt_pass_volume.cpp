// SPDX-License-Identifier: GPL-3.0-only
// The light volume (rt_volume.hlsl; the master plan's 15.2): the light in the air over the scene camera's view, 96 x
// 54 x 48 froxels, this frame's and the frame before's by turns (VolumeA, VolumeB). On High where the stage has air,
// the light the air scatters toward the eye, integrated through it (rt_scatter.hlsl, 15.4: Scatter). Published after
// the light pass as pictures the programs sample (rt_walk.cpp rt_light, pictures 4 and 5): a particle draw that is
// not a light itself (rt_volume_reads) takes its colour times the air's light there (program.hlsli traced_air, draw
// flag 4); a draw that covers or blends over what is behind it takes the air in front of it (traced_scatter, flag 8).
#include "galaxy/render/render_core.h"

namespace galaxy::render {

namespace {

// The air (15.4) is a stage's number (rt_stages.tsv's air column, per unit: 0.000001 a day sky's, more in haze, dust),
// none in space. A stage without a row has none: most stages are in space, and a sky's brightness does not tell
// (Ghostly's and Melty Molten's domes are bright and in space).

}  // namespace

bool RenderCore::rt_pass_volume_on() const { return rt_levels_[kFbVolume] != 0u && !rt_fb_reference(); }

// The stage's air: its row's (rt_stage_air_, kept by the walk); none without one.
float RenderCore::rt_air_density() const {
    return rt_stage_air_;
}

void RenderCore::rt_pass_volume() {
    const RtFrame& frame = rt_light_frame_;
    if (!rt_pass_volume_on() || frame.layer != 0u) {
        return;
    }
    rt_volume_drawn_ = false;
    rt_scatter_drawn_ = false;
    rt::PassBinds binds;
    std::uint32_t shade_at = 0u;
    if (!rt_shade_binds(binds, shade_at)) {
        return;
    }
    // The frame before's volume is read where the walk before traced one and the camera did not cut.
    const bool history = frame.history && rt_volume_turn_ != 0u && rt_volume_turn_ == frame.turn;
    constexpr float kNone[3][4] = {};
    // The shader's modes: 0 medium, 1 high, 2 low (the part's levels 2, 3, 1).
    const unsigned level = rt_levels_[kFbVolume];
    binds.pass_constants = rt_bounce_constants(level >= 3u ? 1u : level == 2u ? 0u : 2u, history ? 1u : 0u, shade_at, kNone);
    binds.reads[0] = rt_before(RtImage::VolumeA);
    const std::uint32_t volume = rt_now(RtImage::VolumeA);
    binds.writes[0] = volume;
    if (binds.reads[0] == rt::kNoImage || volume == rt::kNoImage) {
        return;
    }
    // 96 / 8 groups across; a slice of 54 rows is 7 group rows, 48 slices.
    rt_backend_.dispatch(RtShader::Volume, binds, 12u, 7u * 48u);
    rt_volume_turn_ = frame.turn + 1u;
    rt_volume_drawn_ = true;

    // The light in the air: High, where the stage has air.
    const float density = rt_air_density();
    const std::uint32_t scatter = rt_image(RtImage::Scatter);
    if (level < 3u || !(density > 0.0f) || scatter == rt::kNoImage) {
        return;
    }
    const float values[3][4] = {{density, 0.0f, 0.0f, 0.0f}};
    rt::PassBinds through;
    std::uint32_t at = 0u;
    if (!rt_shade_binds(through, at)) {
        return;
    }
    through.pass_constants = rt_bounce_constants(0u, 0u, at, values);
    through.reads[0] = volume;
    through.writes[0] = scatter;
    rt_backend_.dispatch(RtShader::Scatter, through, 12u, 7u);
    rt_scatter_drawn_ = true;
}

}  // namespace galaxy::render
