// SPDX-License-Identifier: GPL-3.0-only
// The reflections (rt_reflect.hlsl): the specular lobe of every lit surface up to the preset's roughness, one ray a
// pixel of the opaque layer; over time by the mirror image's own point (rt_reflect_time), over space by the
// reflection's lobe (rt_reflect_space, two steps; mirrors stay sharp); then into the specular picture the programs
// read (rt_reflect_out: lobes.mirror x Spec + the sun's highlight). The light pass leaves the opaque layer's specular
// picture to this pass (bit 10 of the frame's flags); where it does not run, the light pass writes it from I alone.
// Off at the part's levels off and low, with the reference and with the emitters alone.
#include "galaxy/render/render_core.h"

#include "galaxy/launch_args.h"
#include "galaxy/render/rt/rt_denoiser.h"

#include <cstring>

namespace galaxy::render {

bool RenderCore::rt_pass_reflect_on() const {
    return rt_levels_[kFbReflect] >= 2u && !rt_fb_reference() &&
           !::galaxy::diagnosing(::galaxy::kDiagnoseRtAdded) && !::galaxy::diagnosing(::galaxy::kDiagnoseRtMotion);
}

void RenderCore::rt_pass_reflect() {
    const RtFrame& frame = rt_light_frame_;
    if (frame.layer != 0u) {
        return;
    }
    rt_reflect_moved_ = false;
    rt_reflect_traced_ = false;
    // The walk before's average and count are this pass's history only where its pass over time was recorded.
    const bool kept_before = rt_reflect_kept_;
    rt_reflect_kept_ = false;
    if (!rt_pass_reflect_on()) {
        return;
    }
    rt::PassBinds binds;
    std::uint32_t shade_at = 0u;
    if (!rt_shade_binds(binds, shade_at)) {
        return;
    }
    const float none[3][4] = {};
    const std::uint32_t guide = rt_now(RtImage::GuideA), raw = rt_image(RtImage::ReflectRaw), out = rt_image(RtImage::Reflect);
    const std::uint32_t material = rt_image(RtImage::Material), average = rt_now(RtImage::ReflectAverageA);
    // g_pass.z bit 0: this walk's I to read at hits (where this walk's step up wrote it); bit 1: the walk before's local
    // lights (rt_screen_lights), where it is known which pixels its blended layer took; bit 3: that layer ran and its
    // guide is bound (t17 read only then; without that layer Added holds the opaque layer's lights alone).
    const bool indirect = rt_pass_indirect_on() && rt_indirect_drawn_;
    const std::uint32_t blended_before = rt_before(RtImage::BlendGuideA);
    const bool blended_walk = rt_blend_walk_before_ + 1u == walk_.serial;
    const bool blended_bound = frame.history && blended_walk && blended_before != rt::kNoImage;
    const bool screen_lights = frame.history && (blended_bound || !blended_walk);
    binds.pass_constants = rt_bounce_constants(rt_levels_[kFbReflect] >= 3u ? 2u : 1u,
                                               (indirect ? 1u : 0u) | (screen_lights ? 2u : 0u) | (blended_bound ? 8u : 0u), shade_at,
                                               none);
    binds.reads[0] = guide;
    binds.reads[1] = rt_image(RtImage::Normal);
    binds.reads[2] = rt_image(RtImage::Motion);
    binds.reads[3] = material;
    binds.reads[4] = indirect ? rt_image(RtImage::Indirect) : rt::kNoImage;
    binds.reads[5] = rt_image(RtImage::LightsSpec);
    binds.reads[6] = rt_image(RtImage::Base);
    binds.reads[7] = rt_image(RtImage::Added);  // the walk before's (this walk's filters write it after this pass)
    binds.reads[8] = blended_bound ? blended_before : rt::kNoImage;
    binds.writes[0] = raw;
    if (!rt_backend_.dispatch(RtShader::Reflect, binds, frame.across, frame.down)) {
        return;  // no rays: nothing below would filter or compose anything this walk wrote
    }
    rt_reflect_traced_ = true;

    // The frames the average holds at each pixel (R16; ReBLUR's accumulated frame count): this frame's and the frame
    // before's by turns, made, let go and reset as the average is (g_pass.w bit 0: the walk before's are this pass's).
    const std::uint32_t frames = rt_now(RtImage::ReflectFramesA);
    rt::PassBinds over_time;
    over_time.constants = frame.binds.constants;
    over_time.pass_constants = rt_pass_constants(0u, 0u, 0u, kept_before ? 1u : 0u);
    over_time.reads[0] = raw;
    over_time.reads[1] = rt_before(RtImage::ReflectAverageA);
    over_time.reads[2] = guide;
    over_time.reads[3] = rt_before(RtImage::GuideA);
    over_time.reads[4] = rt_image(RtImage::Motion);
    over_time.reads[5] = material;
    over_time.reads[6] = rt_before(RtImage::ReflectFramesA);
    over_time.writes[0] = average;
    over_time.writes[1] = rt_image(RtImage::ReflectMoved);  // where R changed: the reactive mask's (rt_walk.cpp rt_reactive)
    over_time.writes[2] = frames;
    rt_reflect_kept_ = rt_backend_.dispatch(RtShader::ReflectTime, over_time, frame.across, frame.down);
    rt_reflect_moved_ = rt_reflect_kept_ && over_time.writes[1] != rt::kNoImage;
    // The surface's normal at full precision (S.normal, t13) for the lobe test; g_pass.w bit 1 says it is bound. The
    // frames at t14 widen the blur while few are held; bit 2 says they are bound.
    const std::uint32_t normals = rt_image(RtImage::Normal);
    const std::uint32_t space_bits = (normals != rt::kNoImage ? 2u : 0u) | (rt_reflect_kept_ && frames != rt::kNoImage ? 4u : 0u);
    for (std::uint32_t step = 0u; step < 2u; ++step) {
        rt::PassBinds over_space;
        over_space.constants = frame.binds.constants;
        over_space.pass_constants = rt_pass_constants(0u, 1u << step, step == 1u ? 1u : 0u, space_bits);
        over_space.reads[0] = step == 0u ? average : raw;
        over_space.reads[2] = guide;
        over_space.reads[3] = material;
        over_space.reads[4] = normals;
        over_space.reads[5] = (space_bits & 4u) != 0u ? frames : rt::kNoImage;
        over_space.writes[0] = step == 0u ? raw : out;
        rt_backend_.dispatch(RtShader::ReflectSpace, over_space, frame.across, frame.down);
    }

    rt::PassBinds into = rt_pass_binds();  // the item table and the pixels' items too (a painted map's colour)
    into.constants = frame.binds.constants;
    into.pass_constants = frame.pass;
    into.reads[0] = out;
    into.reads[1] = guide;
    into.reads[2] = rt_image(RtImage::Normal);
    into.reads[3] = material;
    into.reads[4] = rt_image(RtImage::Base);
    into.reads[5] = rt_image(RtImage::LightsSpec);
    into.reads[6] = rt_image(RtImage::SunOut);  // V for the sun's highlight
    into.reads[7] = rt_pass_indirect_on() && rt_indirect_drawn_ ? rt_image(RtImage::Indirect) : rt::kNoImage;  // I: the fallback
    into.writes[0] = rt_image(RtImage::Mirror);
    rt_backend_.dispatch(RtShader::ReflectOut, into, frame.across, frame.down);
}

// The guides of an outside denoiser that takes the signals raw (rt_rr_guides.hlsl), the
// opaque layer's, after the passes: the world's turn from the eye's in the pass's values.
void RenderCore::rt_pass_guides() {
    const RtFrame& frame = rt_light_frame_;
    rt_guides_drawn_ = false;
    if (!rt_raw_ || frame.layer != 0u) {
        return;
    }
    const Affine34& view = walk_cameras_[rt_scene_camera_];
    float rows[3][4] = {};
    for (unsigned i = 0u; i < 3u; ++i) {
        rows[i][0] = view[0u * 4u + i];
        rows[i][1] = view[1u * 4u + i];
        rows[i][2] = view[2u * 4u + i];
    }
    const bool reflected = rt_reflect_traced_;  // the reflections pass ran this frame (R raw holds its rays)
    rt::PassBinds binds = rt_pass_binds();  // the item table and the pixels' items too (the index's F0)
    binds.constants = frame.binds.constants;
    // g_pass.w bit 0: the reflections pass ran; bit 1: S.normal at full precision in t13.
    const std::uint32_t normals = rt_image(RtImage::Normal);
    binds.pass_constants = rt_bounce_constants(0u, 0u, (reflected ? 1u : 0u) | (normals != rt::kNoImage ? 2u : 0u), rows);
    binds.reads[0] = rt_image(RtImage::Material);
    binds.reads[1] = rt_image(RtImage::Base);
    binds.reads[2] = rt_now(RtImage::GuideA);
    binds.reads[3] = reflected ? rt_image(RtImage::ReflectRaw) : rt::kNoImage;
    binds.reads[4] = normals;
    binds.writes[0] = rt_image(RtImage::RrDiffuse);
    binds.writes[1] = rt_image(RtImage::RrSpecular);
    binds.writes[2] = rt_image(RtImage::RrNormalRoughness);
    binds.writes[3] = rt_image(RtImage::RrHit);
    for (unsigned i = 0u; i < 4u; ++i) {  // its own four (the pass's other writes stay unbound)
        if (binds.writes[i] == rt::kNoImage) {
            return;
        }
    }
    rt_guides_drawn_ = rt_backend_.dispatch(RtShader::RrGuides, binds, frame.across, frame.down);
}

// Hands this frame's guides to the renderer's denoiser (with the light pictures, once the passes are recorded).
void RenderCore::rt_guides_publish() {
    rt::Denoiser* const denoiser = renderer_.rt_denoiser();
    if (!rt_guides_drawn_ || denoiser == nullptr) {
        return;
    }
    rt_guides_drawn_ = false;
    rt::DenoiseGuides guides;
    guides.diffuse_albedo = rt_backend_.image_to_sample(rt_image(RtImage::RrDiffuse));
    guides.specular_albedo = rt_backend_.image_to_sample(rt_image(RtImage::RrSpecular));
    guides.normal_roughness = rt_backend_.image_to_sample(rt_image(RtImage::RrNormalRoughness));
    guides.hit_distance = rt_backend_.image_to_sample(rt_image(RtImage::RrHit));
    std::memcpy(guides.world_to_view, walk_cameras_[rt_scene_camera_].data(), sizeof(guides.world_to_view));
    guides.valid = guides.diffuse_albedo != nullptr && guides.specular_albedo != nullptr && guides.normal_roughness != nullptr &&
                   guides.hit_distance != nullptr;
    denoiser->guides(guides);
}

}  // namespace galaxy::render
