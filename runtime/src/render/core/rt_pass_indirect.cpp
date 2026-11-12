// SPDX-License-Identifier: GPL-3.0-only
// The sky and bounce light (rt_indirect.hlsl; the master plan's section 11): at half resolution, what the sky and the
// lit surfaces near each surface give it, as a ratio to the stage's ambient; averaged over time and space at half
// resolution by the local lights' filters (rt_lights_time, rt_lights_space: they keep the moments of a noise of any
// size), then brought to full resolution by the picture's own edges (rt_indirect_up.hlsl). The light pass takes its
// mean through the ground share (bit 8 of the frame's flags) until the programs take it themselves (work list 22.13).
#include "galaxy/render/render_core.h"

#include "galaxy/launch_args.h"
#include "galaxy/render/affine.h"

#include <cstddef>
#include <cstring>

namespace galaxy::render {

namespace {

// The preset's mode (rt_indirect.hlsl): rt_quality 0 Medium, 1 High, 2 Low.
constexpr std::uint32_t kIndirectSky = 0u, kIndirectMedium = 1u, kIndirectHigh = 2u;
constexpr float kIndirectSkyMin = 0.25f, kIndirectBounceMax = 2.5f;  // rt_bounce.cpp kSkyMin, kBounceMax

// The frame's constants as the filters at half resolution read them: the eye's ray terms for a half-res pixel (twice
// the step a pixel) and whether there is a frame before; the rest unread. rt_frame.hlsli FrameConstants' offsets
// (rt_walk.cpp asserts kept's).
struct HalfFrameConstants {
    float ray[4];
    std::uint32_t unread[72];
    std::uint32_t kept[4];
};
static_assert(offsetof(HalfFrameConstants, kept) == 304u);

}  // namespace

bool RenderCore::rt_pass_indirect_on() const {
    // The reference traces its own (rt_bounce.cpp): the pass would only be covered over.
    return rt_levels_[kFbIndirect] != 0u && !rt_fb_reference() && !::galaxy::diagnosing(::galaxy::kDiagnoseRtAdded);
}

// The near field (rt_near.hlsl; FB b76): High alone, where the gather sees the far field only.
bool RenderCore::rt_pass_near_on() const {
    return rt_pass_indirect_on() && rt_levels_[kFbIndirect] >= 3u && rt_dev(RtDev::NearField, 1.0f) > 0.5f;  // b76e
}

void RenderCore::rt_pass_indirect() {
    const RtFrame& frame = rt_light_frame_;
    if (!rt_pass_indirect_on() || frame.layer != 0u) {
        return;
    }
    const unsigned half_width = (frame.width + 1u) / 2u, half_height = (frame.height + 1u) / 2u;
    const std::uint32_t half_across = (half_width + 7u) / 8u, half_down = (half_height + 7u) / 8u;
    (void)rt_dome_read();
    rt::PassBinds binds;
    std::uint32_t shade_at = 0u;
    if (!rt_shade_binds(binds, shade_at)) {
        return;
    }
    // This frame's eye space from the frame before's: the reservoirs' samples are kept where they were seen.
    float rows[3][4] = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
    bool history = frame.history;
    if (history) {
        Affine34 world_from_before{};
        history = affine_inverse(rt_light_view_before_, world_from_before);
        if (history) {
            const Affine34 now_from_before = affine_multiply(walk_cameras_[rt_scene_camera_], world_from_before);
            std::memcpy(rows, now_from_before.data(), sizeof(rows));
        }
    }
    // The part's level: 1 low, 2 medium, 3 high.
    const std::uint32_t mode = rt_levels_[kFbIndirect] <= 1u ? kIndirectSky : rt_levels_[kFbIndirect] == 2u ? kIndirectMedium : kIndirectHigh;
    // g_pass.z bit 1: the near field runs this frame, the gather sees the far field only (FB b76e).
    const bool near_ready = rt_pass_near_on() && rt_image(RtImage::NearRaw) != rt::kNoImage && rt_now(RtImage::NearA) != rt::kNoImage;
    binds.pass_constants = rt_bounce_constants(mode, (history ? 1u : 0u) | (near_ready ? 2u : 0u), shade_at, rows);
    binds.reads[0] = rt_now(RtImage::GuideA);
    binds.reads[1] = rt_image(RtImage::Normal);
    binds.reads[2] = rt_image(RtImage::Motion);
    binds.reads[3] = rt_image(RtImage::Material);
    binds.reads[4] = rt_before(RtImage::GiReservoirsA);
    binds.reads[5] = rt_before(RtImage::GiWeightsA);
    binds.reads[6] = rt_before(RtImage::IndirectGuideA);
    binds.reads[7] = rt_image(RtImage::Indirect);  // the frame before's, until rt_indirect_up writes this frame's
    // The walk before's local lights at its pixels, and where its blended layer had them instead (rt_screen_lights;
    // b49): this walk's filters write Added after this pass.
    binds.reads[8] = rt_image(RtImage::Added);
    binds.reads[9] = rt_before(RtImage::BlendGuideA);
    const std::uint32_t raw = rt_image(RtImage::IndirectRaw), step_image = rt_image(RtImage::IndirectStep);
    const std::uint32_t half_guide = rt_now(RtImage::IndirectGuideA), half_motion = rt_image(RtImage::IndirectMotion);
    binds.writes[0] = rt_now(RtImage::GiReservoirsA);
    binds.writes[1] = rt_now(RtImage::GiWeightsA);
    binds.writes[2] = half_guide;
    binds.writes[3] = half_motion;
    binds.writes[4] = raw;
    rt_backend_.dispatch(RtShader::Indirect, binds, half_across, half_down);

    // The filters at half resolution: the eye's terms for a half-res pixel.
    HalfFrameConstants half{};
    half.ray[0] = frame.ray[0] * 2.0f;
    half.ray[1] = frame.ray[1];
    half.ray[2] = frame.ray[2] * 2.0f;
    half.ray[3] = frame.ray[3];
    half.kept[2] = history ? 1u : 0u;
    // The history scale, the frame's own (rt_walk.cpp: clamp(rate / 60, 0.5, 2); A1-15; FB b79).
    const float scale = rt_frame_rate_ / 60.0f < 0.5f ? 0.5f : rt_frame_rate_ / 60.0f > 2.0f ? 2.0f : rt_frame_rate_ / 60.0f;
    std::memcpy(&half.kept[3], &scale, sizeof(scale));
    const UploadRing::Allocation half_at = renderer_.constant_ring().allocate(512u, 256u);
    std::memset(half_at.cpu, 0, 512u);
    std::memcpy(half_at.cpu, &half, sizeof(half));
    const rt::BufferView half_constants{half_at.gpu, 512u};
    const std::uint32_t average = rt_now(RtImage::IndirectAverageA), moments = rt_now(RtImage::IndirectMomentsA);
    rt::PassBinds over_time;
    over_time.constants = half_constants;
    over_time.pass_constants = frame.pass;
    over_time.reads[0] = raw;
    over_time.reads[1] = rt_before(RtImage::IndirectAverageA);
    over_time.reads[2] = half_guide;
    over_time.reads[3] = rt_before(RtImage::IndirectGuideA);
    over_time.reads[4] = half_motion;
    over_time.reads[5] = rt_before(RtImage::IndirectMomentsA);
    over_time.writes[0] = average;
    over_time.writes[1] = moments;
    rt_backend_.dispatch(RtShader::LightsTime, over_time, half_across, half_down);
    for (std::uint32_t step = 0u; step < 3u; ++step) {
        rt::PassBinds over_space;
        over_space.constants = half_constants;
        over_space.pass_constants = rt_pass_constants(0u, 1u << step, step == 2u ? 1u : 0u);
        over_space.reads[0] = step == 0u ? average : step == 1u ? step_image : raw;
        over_space.reads[1] = moments;
        over_space.reads[2] = half_guide;
        over_space.writes[0] = step == 1u ? raw : step_image;
        rt_backend_.dispatch(RtShader::LightsSpace, over_space, half_across, half_down);
    }
    // The near field at full size (High): its rays, then over time; the step up multiplies it in (FB b76).
    const std::uint32_t near_raw = rt_image(RtImage::NearRaw), near_now = rt_now(RtImage::NearA);
    const bool near_on = near_ready;
    if (near_on) {
        rt::PassBinds rays = rt_pass_binds();  // the scene, the items, the depth, the blue noise
        rays.constants = frame.binds.constants;
        rays.pass_constants = frame.pass;
        rays.reads[0] = rt_now(RtImage::GuideA);
        rays.reads[1] = rt_image(RtImage::Normal);
        rays.reads[2] = rt_image(RtImage::Motion);
        rays.reads[3] = rt_image(RtImage::Material);
        rays.writes[0] = near_raw;
        rt_backend_.dispatch(RtShader::NearRays, rays, frame.across, frame.down);
        rt::PassBinds near_time;
        near_time.constants = frame.binds.constants;
        near_time.pass_constants = rt_pass_constants(0u, 0u, 0u, rt_near_before_ ? 1u : 0u);  // its history is there (b76c)
        near_time.reads[0] = near_raw;
        near_time.reads[1] = rt_before(RtImage::NearA);
        near_time.reads[2] = rt_now(RtImage::GuideA);
        near_time.reads[3] = rt_before(RtImage::GuideA);
        near_time.reads[4] = rt_image(RtImage::Motion);
        near_time.writes[0] = near_now;
        rt_backend_.dispatch(RtShader::NearTime, near_time, frame.across, frame.down);
    }
    rt_near_before_ = near_on;
    // To full resolution by the picture's edges.
    const float values[3][4] = {{rt_dev(RtDev::SkyMin, kIndirectSkyMin), rt_dev(RtDev::BounceMax, kIndirectBounceMax),
                                 near_on ? 1.0f : 0.0f, 0.0f}};
    rt::PassBinds up;
    up.constants = frame.binds.constants;
    up.pass_constants = rt_bounce_constants(0u, 0u, 0u, values);
    up.reads[0] = step_image;
    up.reads[1] = half_guide;
    up.reads[2] = rt_now(RtImage::GuideA);
    up.reads[3] = near_on ? near_now : rt::kNoImage;
    up.writes[0] = rt_image(RtImage::Indirect);
    rt_backend_.dispatch(RtShader::IndirectUp, up, frame.across, frame.down);
}

}  // namespace galaxy::render
