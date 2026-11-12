// SPDX-License-Identifier: GPL-3.0-only
// RenderCore's passes of the light that travels: hit shading's frame (rt_shade.hlsli), the world light grid, the
// reference path tracer (--diagnostics rt-reference) and the lighting signals as pictures (--diagnostics
// rt-signals). The frame's light (rt_walk.cpp rt_light) calls rt_pass_reference after its last pass and
// rt_reference_publish after its filters; the prepass calls rt_signals once the light is over.
#include "galaxy/render/render_core.h"

#include "galaxy/launch_args.h"
#include "render/core/world_lights.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace galaxy::render {

namespace {

// The light model's tunables (the master plan's appendix G).
constexpr float kShadowTint = 0.2f;    // how much of the per-channel ratio a painted surface's P takes
constexpr float kWrap = 0.25f;         // the wrap of a local light's facing
constexpr float kSkyGain = 1.0f;       // the dome as light
constexpr float kAreaEmission = 3.0f;  // an emissive surface as a ray sees it, in shares
constexpr float kHitSky = 0.7f;        // a hit's own indirect when no screen knows it
constexpr float kBounce = 1.0f;        // the bounce light's strength
constexpr float kBounceMax = 2.5f;     // the indirect ratio's limits
constexpr float kSkyMin = 0.25f;
constexpr float kHitDark = 0.15f;      // a hit's light in sky-only mode
constexpr float kSunGlint = 4.0f;      // the sun as a mirror shows it

// The world light grid (the plan's 10.3): 32 x 16 x 32 cells of 400 units about the eye, the middle axis the player's up.
constexpr unsigned kGridAcross = 32u, kGridUp = 16u, kGridCells = kGridAcross * kGridUp * kGridAcross;
constexpr float kGridCell = 400.0f;
constexpr std::size_t kGridMostEntries = std::size_t{1} << 19u;

// The reference: samples a walk, and the most a pixel keeps.
constexpr std::uint32_t kRefPaths = 4u;
constexpr std::uint32_t kRefMostSamples = 4096u;
// The walks a kept picture is held for (the same list walked again): enough for 1,024 samples of the reference, and
// for the real-time filters to settle when only the signals are taken.
constexpr unsigned kRefHoldWalks = 256u;
constexpr unsigned kSignalHoldWalks = 60u;  // the signals from 20 walks in (A, l65), the kept picture 35 walks after S
// The dome's mean (11.1) is measured over the first walks of a hold, then kept while the reference adds up.
constexpr unsigned kDomeWalks = 16u;

// A pass's own constants (rt_frame.hlsli PassConstants, as rt_walk.cpp RtPassConstants).
struct BouncePassConstants {
    std::uint32_t pass[4];  // the layer, a filter's step, 1 on its last step, free
    float value[3][4];
};

// The signals a kept picture is given (the plan's 2.2 names): which signal, its channels (0 r, 1 rgb, 2 a), and 1
// when it goes over 1 (written as x / (1 + x)).
enum class SignalOf : std::uint8_t { Light, Added, Mirror, Indirect, LightsSpec, Share, Air, Fog };
struct Signal {
    const char* name;
    SignalOf of;
    std::uint32_t channels, over;
};
constexpr Signal kSignals[] = {{"V", SignalOf::Light, 0u, 0u},
                               {"G", SignalOf::Light, 2u, 0u},
                               {"D", SignalOf::Added, 1u, 1u},
                               {"R", SignalOf::Mirror, 1u, 1u},
                               {"I", SignalOf::Indirect, 1u, 1u},
                               {"H", SignalOf::LightsSpec, 1u, 1u},  // the local lights' highlight alone (l63)
                               {"S", SignalOf::Share, 1u, 1u},       // the programs' share (rt_program_signal; l63)
                               {"A", SignalOf::Air, 1u, 1u},         // the air's own light (rt_program_signal; l65)
                               {"F", SignalOf::Fog, 1u, 1u}};        // the fog's lit share (rt_program_signal; FB b68)
constexpr unsigned kSignalApart = 5u;  // walks between two signals (a readback is one at a time)
static_assert(std::size(kSignals) * kSignalApart <= kSignalHoldWalks, "every signal inside the hold (FB b68)");

}  // namespace

// What every pass that shades a hit reads first (rt_shade.hlsli RtShadeFrame): the stage and the model's tunables.
void RenderCore::rt_shade_frame(RtShadeFrame& frame) const {
    static_assert(sizeof(RtShadeFrame) == 256u);
    const NebulaFrameList& list = *walk_.now->list;
    frame = RtShadeFrame{};
    // The stage: its ambient and key as the programs take them (rt_stage_a_, rt_stage_k_ from rt_tables: the row's
    // gains, an even grey where the stage names no light; one stage light, FB b71), its fill the world light
    // stage_light + 1.
    float fill[3] = {0.0f, 0.0f, 0.0f};
    if (list.stage_light_known != 0u) {
        if (const NebulaFrameWorldLight* light = core::find_world_light(list, list.stage_light + 1u); light != nullptr) {
            std::copy_n(light->color, 3u, fill);
        }
    }
    for (unsigned c = 0; c < 3u; ++c) {
        frame.ambient[c] = rt_stage_a_[c];
        frame.authored[c] = rt_stage_a_[c];
        frame.key[c] = rt_stage_k_[c];
        frame.fill[c] = fill[c];
    }
    frame.key[3] = rt_dev(RtDev::ShadowTint, kShadowTint);  // rt_dev.h: one key with the programs' view_stage_a.w
    frame.fill[3] = rt_dev(RtDev::Wrap, kWrap);
    frame.sky[3] = rt_dev(RtDev::SkyGain, kSkyGain);
    frame.gains[0] = rt_dev(RtDev::AreaEmission, kAreaEmission);
    frame.gains[1] = rt_dev(RtDev::HitSky, kHitSky);
    frame.gains[2] = rt_dev(RtDev::Bounce, kBounce);
    frame.gains[3] = rt_dev(RtDev::BounceMax, kBounceMax);
    std::memcpy(frame.axis, rt_grid_axis_, sizeof(frame.axis));
    frame.look[0] = rt_dev(RtDev::SkyMin, kSkyMin);
    frame.look[1] = rt_dev(RtDev::HitDark, kHitDark);
    frame.look[2] = rt_dome_mean_ > 1e-3f ? 1.0f / rt_dome_mean_ : 0.0f;
    // The sky against its own mean colour (b48): a channel's mean at least a tenth of the mean luminance.
    if (rt_dome_mean_ > 1e-3f && rt_dome_colour_[0] + rt_dome_colour_[1] + rt_dome_colour_[2] > 0.0f) {
        for (unsigned c = 0; c < 3u; ++c) {
            frame.dome[c] = 1.0f / std::max(rt_dome_colour_[c], 0.1f * rt_dome_mean_);
        }
        frame.dome[3] = 1.0f;
    }
    // A surface met counts as it is (b51: b46's gain against the running mean of such hits is held at 1; the mean
    // is still measured, for the sheet).
    // rt_dev.h hit_gain 1: b46's gain against that mean again, for the sheet's comparison.
    frame.authored[3] = rt_dev(RtDev::HitGain, 0.0f) > 0.5f && rt_hit_mean_ > 1.0f ? 1.0f / rt_hit_mean_ : 1.0f;
    frame.look[3] = rt_dev(RtDev::SunGlint, kSunGlint);
    // The painted composite's look as the programs take it (FC c36's rt_look_; hit shading follows it: FB b53).
    std::memcpy(frame.form, rt_look_, sizeof(frame.form));
    frame.grid[3] = rt_light_frame_.turn;
}

// The world light grid: each cell the emitters whose reach touches it. Two words a cell (its first entry, how many),
// then the entries: an emitter's number in the low 16 bits, how many it stands for in the high 16. Past 2^19 entries
// particles are kept one in two (then four...), each standing for those left out.
void RenderCore::rt_world_grid() {
    // The axes in the eye's space: the player's up the middle one, the eye's right flattened against it the first.
    const float up[3] = {sky_up_[0], sky_up_[1], sky_up_[2]};
    float right[3] = {1.0f, 0.0f, 0.0f};
    {
        const float along = right[0] * up[0] + right[1] * up[1] + right[2] * up[2];
        for (unsigned c = 0; c < 3u; ++c) {
            right[c] -= up[c] * along;
        }
        float size = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
        if (size < 0.1f) {
            right[0] = 0.0f;
            right[1] = 0.0f;
            right[2] = 1.0f;
            const float back = up[2];
            for (unsigned c = 0; c < 3u; ++c) {
                right[c] -= up[c] * back;
            }
            size = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
        }
        for (float& v : right) {
            v /= std::max(size, 1e-6f);
        }
    }
    const float depth_axis[3] = {right[1] * up[2] - right[2] * up[1], right[2] * up[0] - right[0] * up[2], right[0] * up[1] - right[1] * up[0]};
    const float* const axes[3] = {right, up, depth_axis};
    const unsigned counts[3] = {kGridAcross, kGridUp, kGridAcross};
    for (unsigned a = 0; a < 3u; ++a) {
        for (unsigned c = 0; c < 3u; ++c) {
            rt_grid_axis_[a][c] = axes[a][c] / kGridCell;
        }
        rt_grid_axis_[a][3] = 0.5f * static_cast<float>(counts[a]);  // the eye in the middle
    }
    const std::size_t emitters = std::min<std::size_t>(rt_emitters_.size(), 0xFFFFu);
    // Each emitter's cells, as ranges along the three axes.
    using Range = RtGridRange;
    std::vector<Range>& ranges = rt_grid_ranges_;
    ranges.assign(emitters, Range{});
    std::size_t entries = 0u, particle_entries = 0u;
    for (std::size_t e = 0; e < emitters; ++e) {
        const RtEmitter& emitter = rt_emitters_[e];
        Range& r = ranges[e];
        const float reach = std::max(emitter.reach, emitter.radius) / kGridCell;
        std::size_t cells = 1u;
        for (unsigned a = 0; a < 3u; ++a) {
            const float centre = emitter.centre[0] * rt_grid_axis_[a][0] + emitter.centre[1] * rt_grid_axis_[a][1] +
                                 emitter.centre[2] * rt_grid_axis_[a][2] + rt_grid_axis_[a][3];
            r.low[a] = std::max(static_cast<int>(std::floor(centre - reach)), 0);
            r.high[a] = std::min(static_cast<int>(std::floor(centre + reach)), static_cast<int>(counts[a]) - 1);
            cells = r.low[a] > r.high[a] || !std::isfinite(centre) ? 0u : cells * static_cast<std::size_t>(r.high[a] - r.low[a] + 1);
        }
        if (cells == 0u) {
            r.low[0] = 1;
            r.high[0] = 0;
        }
        entries += cells;
        particle_entries += emitter.kind == 2u ? cells : 0u;
    }
    std::uint32_t keep_every = 1u;
    while (entries - particle_entries + particle_entries / keep_every > kGridMostEntries && keep_every < 64u) {
        keep_every *= 2u;
    }
    const auto kept = [&](std::size_t e) { return rt_emitters_[e].kind != 2u || e % keep_every == 0u; };
    // Count, then place (a counting sort by cell).
    std::vector<std::uint32_t>& words = rt_grid_words_;
    words.assign(std::size_t{kGridCells} * 2u, 0u);
    const auto each_cell = [&](const Range& r, auto&& with) {
        for (int z = r.low[2]; z <= r.high[2]; ++z) {
            for (int y = r.low[1]; y <= r.high[1]; ++y) {
                for (int x = r.low[0]; x <= r.high[0]; ++x) {
                    with((static_cast<std::uint32_t>(z) * kGridUp + static_cast<std::uint32_t>(y)) * kGridAcross + static_cast<std::uint32_t>(x));
                }
            }
        }
    };
    for (std::size_t e = 0; e < emitters; ++e) {
        if (kept(e)) {
            each_cell(ranges[e], [&](std::uint32_t cell) { ++words[cell * 2u + 1u]; });
        }
    }
    std::uint32_t first = 0u;
    for (std::uint32_t cell = 0; cell < kGridCells; ++cell) {
        words[cell * 2u] = first;
        first += words[cell * 2u + 1u];
        words[cell * 2u + 1u] = 0u;
    }
    words.resize(words.size() + std::max<std::size_t>(first, 1u), 0u);
    std::uint32_t* const entry = words.data() + std::size_t{kGridCells} * 2u;
    for (std::size_t e = 0; e < emitters; ++e) {
        if (!kept(e)) {
            continue;
        }
        const std::uint32_t stands_for = rt_emitters_[e].kind == 2u ? keep_every : 1u;
        each_cell(ranges[e], [&](std::uint32_t cell) {
            entry[words[cell * 2u] + words[cell * 2u + 1u]++] = static_cast<std::uint32_t>(e) | stands_for << 16u;
        });
    }
    rt_grid_entries_ = first;
}

// The binds every pass that shades a hit shares: the frame's (rt_pass_binds), with the shading frame and the world
// light grid in t4 in place of the emitters' tiles (no such pass reads them). `at`: the shading frame's first byte
// there, for the pass's constants. The index ring: a request past its frame's segment goes to a chunk of its own.
bool RenderCore::rt_shade_binds(rt::PassBinds& binds, std::uint32_t& at) {
    // Once a frame: the passes of the frame share it.
    if (rt_shade_turn_ == rt_light_frame_.turn + 1u) {
        binds = rt_pass_binds();
        binds.buffers[3] = rt_shade_view_;
        at = rt_shade_at_;
        return true;
    }
    RtShadeFrame frame;
    rt_world_grid();
    rt_shade_frame(frame);
    const std::size_t grid_bytes = rt_grid_words_.size() * sizeof(std::uint32_t);
    frame.grid[0] = static_cast<std::uint32_t>(sizeof(RtShadeFrame));
    frame.grid[1] = frame.grid[0] + kGridCells * 2u * static_cast<std::uint32_t>(sizeof(std::uint32_t));
    frame.grid[2] = rt_grid_entries_ != 0u ? 1u : 0u;
    UploadRing& ring = renderer_.index_ring();
    const UploadRing::Allocation block = ring.allocate(sizeof(RtShadeFrame) + grid_bytes, 256u);
    if (block.cpu == nullptr) {
        return false;
    }
    std::memcpy(block.cpu, &frame, sizeof(frame));
    std::memcpy(static_cast<std::byte*>(block.cpu) + sizeof(frame), rt_grid_words_.data(), grid_bytes);
    binds = rt_pass_binds();
    binds.buffers[3] = rt_backend_.view_of(block.gpu, sizeof(RtShadeFrame) + grid_bytes, at);
    rt_shade_view_ = binds.buffers[3];
    rt_shade_at_ = at;
    rt_shade_turn_ = rt_light_frame_.turn + 1u;
    return true;
}

// A pass's own constants (b1) for the passes of this file: the layer, then `y`, `z`, `w` and the values the pass's
// shader names.
rt::BufferView RenderCore::rt_bounce_constants(std::uint32_t y, std::uint32_t z, std::uint32_t w, const float (&values)[3][4]) {
    BouncePassConstants constants{};
    constants.pass[0] = rt_light_frame_.layer;
    constants.pass[1] = y;
    constants.pass[2] = z;
    constants.pass[3] = w;
    std::memcpy(constants.value, values, sizeof(constants.value));
    const UploadRing::Allocation at = renderer_.constant_ring().allocate(256u, 256u);
    std::memcpy(at.cpu, &constants, sizeof(constants));
    return {at.gpu, 256u};
}

// Images of the bounce passes' own, at the light pass's size: made again when it changes, the old ones freed once no
// frame in flight reads them.
std::uint32_t RenderCore::rt_bounce_image(std::uint32_t& image, rt::ImageFormat format, unsigned width, unsigned height, const char* name) {
    if (image != rt::kNoImage && (rt_bounce_width_ != width || rt_bounce_height_ != height)) {
        rt_bounce_retired_.emplace_back(frame_index_, image);
        image = rt::kNoImage;
    }
    if (image == rt::kNoImage) {
        image = rt_backend_.make_image(format, width, height, name);
    }
    return image;
}

// The dome's mean luminance as the bounce rays see it (11.1): what they added to the counters 44 and 45 (the frame
// slot's last frame), a running mean (0.05 a frame at 60 a second, as many seconds at any rate: b50), kept still once a hold has measured it over its first walks so
// the reference adds up under one light. False when it is kept. Beside it the mean ratio of the surfaces they meet
// (counters 46 and 47, rt_count_hit; FB b46), kept the same way.
bool RenderCore::rt_dome_read() {
    if (rt_hold_left_ != 0u && rt_hold_walks_ - rt_hold_left_ > kDomeWalks) {
        return false;
    }
    std::uint32_t counted[rt::kCounters];
    rt_backend_.read_counters(renderer_.frame_slot(), counted);
    // A walk's step, by the rate as the filters' histories take it (rt_history_scale; FB b50).
    const float step = 0.05f / std::clamp(rt_frame_rate_ / 60.0f, 0.5f, 2.0f);
    if (counted[45] != 0u) {
        const float mean = static_cast<float>(counted[44]) / 256.0f / static_cast<float>(counted[45]);
        rt_dome_mean_ = rt_dome_mean_ > 0.0f ? rt_dome_mean_ + (mean - rt_dome_mean_) * step : mean;
        for (unsigned c = 0; c < 3u; ++c) {  // its colour (b48)
            const float channel = static_cast<float>(counted[48u + c]) / 256.0f / static_cast<float>(counted[45]);
            rt_dome_colour_[c] = rt_dome_colour_[c] > 0.0f ? rt_dome_colour_[c] + (channel - rt_dome_colour_[c]) * step : channel;
        }
    }
    if (counted[47] != 0u) {
        const float hits = static_cast<float>(counted[46]) / 256.0f / static_cast<float>(counted[47]);
        rt_hit_mean_ = rt_hit_mean_ > 0.0f ? rt_hit_mean_ + (hits - rt_hit_mean_) * step : hits;
    }
    return true;
}

// The reference (rt_reference.hlsl): the means of every signal, added up while the same list is walked again
// (rt_hold), started again when anything differs; rt_reference_publish puts them in place of the real-time passes'
// pictures once the filters are done. Opaque layer only.
bool RenderCore::rt_fb_reference() const noexcept {
    return ::galaxy::diagnosing(::galaxy::kDiagnoseRtReference) || rt_quality_ == 3u;
}

void RenderCore::rt_pass_reference() {
    rt_ref_ready_ = false;
    // What was retired goes once no frame in flight reads it, whether the reference runs or not (FA).
    std::erase_if(rt_bounce_retired_, [&](const std::pair<std::uint64_t, std::uint32_t>& retired) {
        if (retired.first + rt::kFramesToRelease > frame_index_) {
            return false;
        }
        rt_backend_.free_image(retired.second);
        return true;
    });
    if (!rt_fb_reference()) {
        // Off: its sums go (8 Rgba32f images at the light pass's size); the next time it is on they are made again and
        // it starts again.
        for (std::uint32_t& image : rt_ref_images_) {
            if (image != rt::kNoImage) {
                rt_bounce_retired_.emplace_back(frame_index_, image);
                image = rt::kNoImage;
            }
        }
        rt_bounce_width_ = rt_bounce_height_ = 0u;
        for (std::uint32_t& image : rt_ref_t_images_) {  // and what it sees through glass and water (b41)
            if (image != rt::kNoImage) {
                rt_bounce_retired_.emplace_back(frame_index_, image);
                image = rt::kNoImage;
            }
        }
        rt_ref_t_width_ = rt_ref_t_height_ = 0u;
        return;
    }
    if (rt_light_frame_.layer != 0u) {
        return;
    }
    const unsigned width = rt_light_frame_.width, height = rt_light_frame_.height;
    static constexpr const char* kNames[8] = {"Nebula RT Reference V A", "Nebula RT Reference D A", "Nebula RT Reference I A",
                                              "Nebula RT Reference R A", "Nebula RT Reference V B", "Nebula RT Reference D B",
                                              "Nebula RT Reference I B", "Nebula RT Reference R B"};
    bool images = true;
    for (unsigned i = 0; i < 8u; ++i) {
        images = rt_bounce_image(rt_ref_images_[i], rt::ImageFormat::Rgba32f, width, height, kNames[i]) != rt::kNoImage && images;
    }
    const bool resized = rt_bounce_width_ != width || rt_bounce_height_ != height;
    rt_bounce_width_ = width;
    rt_bounce_height_ = height;
    if (!images) {
        return;
    }
    // Restarted once the dome's mean is measured (rt_dome_read): a hold's first walks.
    const bool measured = rt_dome_read();
    bool restart = resized || (measured && rt_hold_left_ != 0u && rt_hold_walks_ - rt_hold_left_ == kDomeWalks);
    // Anything else: another list, another camera, a cut.
    const core::ListData& now = *walk_.now;
    const Affine34& camera = walk_cameras_[rt_scene_camera_];
    if (now.serial != rt_ref_serial_ || now.list != rt_ref_list_ || camera != rt_ref_camera_ || now.cut || walk_.time.reset ||
        walk_.time.t != rt_ref_time_) {
        restart = true;
    }
    rt_ref_serial_ = now.serial;
    rt_ref_list_ = now.list;
    rt_ref_camera_ = camera;
    rt_ref_time_ = walk_.time.t;
    rt_ref_restart_ = restart;
    rt::PassBinds binds;
    std::uint32_t shade_at = 0u;
    if (!rt_shade_binds(binds, shade_at)) {
        return;
    }
    const unsigned now_set = (rt_light_frame_.turn & 1u) * 4u, before_set = 4u - now_set;
    // A walk that is not held shows the reference's noise.
    const float values[3][4] = {{rt_hold_left_ != 0u ? static_cast<float>(kRefPaths) : 1.0f, static_cast<float>(kRefMostSamples), 0.0f, 0.0f}};
    binds.pass_constants = rt_bounce_constants(0u, restart ? 1u : 0u, shade_at, values);
    binds.reads[0] = rt_now(RtImage::GuideA);
    binds.reads[1] = rt_image(RtImage::Normal);
    binds.reads[2] = rt_image(RtImage::Motion);
    for (unsigned i = 0; i < 4u; ++i) {
        binds.reads[3u + i] = rt_ref_images_[before_set + i];
        binds.writes[i] = rt_ref_images_[now_set + i];
    }
    rt_backend_.dispatch(RtShader::Reference, binds, rt_light_frame_.across, rt_light_frame_.down);
    rt_ref_now_ = now_set;
    rt_ref_ready_ = true;
}

// The reference's T (section 18; FB): the transmit pass's paths with the light where they land by path tracing
// (rt_transmit_ref.hlsl), summed while the reference's own sums are (rt_ref_restart_ starts both again), written to T
// as the pass writes it; the second layer's walk, after the opaque layer's reference.
void RenderCore::rt_pass_transmit_reference() {
    const RtFrame& frame = rt_light_frame_;
    const std::uint32_t out = rt_image(RtImage::Transmit);
    if (!rt_ref_ready_ || out == rt::kNoImage) {
        return;
    }
    bool restart = rt_ref_restart_;
    if (rt_ref_t_width_ != frame.width || rt_ref_t_height_ != frame.height) {
        for (std::uint32_t& image : rt_ref_t_images_) {
            if (image != rt::kNoImage) {
                rt_bounce_retired_.emplace_back(frame_index_, image);
                image = rt::kNoImage;
            }
        }
        rt_ref_t_width_ = frame.width;
        rt_ref_t_height_ = frame.height;
    }
    static constexpr const char* kNames[2] = {"Nebula RT Reference T A", "Nebula RT Reference T B"};
    bool images = true;
    for (unsigned i = 0; i < 2u; ++i) {
        restart = restart || rt_ref_t_images_[i] == rt::kNoImage;  // new sums hold nothing yet
        images = rt_bounce_image(rt_ref_t_images_[i], rt::ImageFormat::Rgba32f, frame.width, frame.height, kNames[i]) != rt::kNoImage && images;
    }
    if (!images) {
        return;
    }
    rt::PassBinds binds;
    std::uint32_t shade_at = 0u;
    if (!rt_shade_binds(binds, shade_at)) {
        return;
    }
    const unsigned now = rt_ref_now_ != 0u ? 1u : 0u;  // by the reference's own turns
    const float values[3][4] = {{rt_hold_left_ != 0u ? static_cast<float>(kRefPaths) : 1.0f, static_cast<float>(kRefMostSamples), 0.0f, 0.0f}};
    binds.pass_constants = rt_bounce_constants(0u, restart ? 1u : 0u, shade_at, values);
    binds.reads[0] = rt_layer_guide();  // the second layer's, as the pass reads it
    binds.reads[1] = rt_image(RtImage::Normal);
    binds.reads[2] = rt_image(RtImage::Motion);
    binds.reads[3] = rt_ref_t_images_[1u - now];
    binds.writes[0] = out;
    binds.writes[1] = rt_ref_t_images_[now];
    rt_backend_.dispatch(RtShader::TransmitReference, binds, frame.across, frame.down);
    rt_transmit_drawn_ = true;
}

// The reference's means as the programs read them, over the pictures the filters made (rt_reference_out.hlsl).
void RenderCore::rt_reference_publish() {
    if (!rt_ref_ready_ || rt_light_frame_.layer != 0u) {
        return;
    }
    const float values[3][4] = {{rt_dev(RtDev::SkyMin, kSkyMin), rt_dev(RtDev::BounceMax, kBounceMax), 0.0f, 0.0f}};
    rt::PassBinds means;
    means.constants = rt_light_frame_.binds.constants;
    means.pass_constants = rt_bounce_constants(0u, 0u, 0u, values);
    for (unsigned i = 0; i < 4u; ++i) {
        means.reads[i] = rt_ref_images_[rt_ref_now_ + i];
    }
    means.writes[0] = rt_image(RtImage::Light);
    means.writes[1] = rt_image(RtImage::Added);
    means.writes[2] = rt_image(RtImage::Mirror);
    rt_backend_.dispatch(RtShader::ReferenceOut, means, rt_light_frame_.across, rt_light_frame_.down);
    // The reference's R through the reflections' own composite where they are on (rt_reflect_out.hlsl, g_pass.w bit 0:
    // the mean, the local lights' highlights added there): the Mirror picture in a preset's encoding, so the two
    // differ by the signal alone (section 18; FB).
    if (rt_levels_[kFbReflect] >= 2u) {
        rt::PassBinds into = rt_pass_binds();
        into.constants = rt_light_frame_.binds.constants;
        into.pass_constants = rt_pass_constants(0u, 0u, 0u, 1u);
        into.reads[0] = rt_ref_images_[rt_ref_now_ + 3u];
        into.reads[1] = rt_now(RtImage::GuideA);
        into.reads[2] = rt_image(RtImage::Normal);
        into.reads[3] = rt_image(RtImage::Material);
        into.reads[4] = rt_image(RtImage::Base);
        into.reads[5] = rt_image(RtImage::LightsSpec);
        into.writes[0] = rt_image(RtImage::Mirror);
        rt_backend_.dispatch(RtShader::ReflectOut, into, rt_light_frame_.across, rt_light_frame_.down);
    }
}

// The signals as pictures (--diagnostics rt-signals), on the walks of a hold kSignalApart apart, the last ones: each
// one into an image of the scene's format, copied over the scene's colour (the scene then draws over it as ever) and
// read back as signal_<name>_<n>.ppm beside the kept picture present_<n>.ppm.
void RenderCore::rt_signals() {
    if (rt_hold_left_ != 0u || rt_signal_waiting_) {
        renderer_.debug_log_readback();  // a signal taken a walk before is written once the GPU is past it
    }
    if (!::galaxy::diagnosing(::galaxy::kDiagnoseRtSignals) || rt_hold_left_ == 0u || rt_hold_left_ % kSignalApart != 0u || !rt_light_on_) {
        return;
    }
    const unsigned which = rt_hold_left_ / kSignalApart - 1u;
    if (which >= std::size(kSignals)) {
        return;
    }
    const Signal& signal = kSignals[which];
    std::uint32_t source = rt::kNoImage;
    switch (signal.of) {
    case SignalOf::Light: source = rt_image(RtImage::Light); break;
    case SignalOf::Added: source = rt_image(RtImage::Added); break;
    case SignalOf::Mirror: source = rt_image(RtImage::Mirror); break;
    case SignalOf::LightsSpec: source = rt_image(RtImage::LightsSpec); break;
    case SignalOf::Share:
    case SignalOf::Air:
    case SignalOf::Fog: return;  // the programs draw them (rt_program_signal)
    case SignalOf::Indirect:
        // The reference's mean, or the preset's own (the sky and bounce light pass, when it runs).
        if (rt_fb_reference()) {
            source = rt_ref_ready_ ? rt_ref_images_[rt_ref_now_ + 2u] : rt::kNoImage;
        } else if (rt_pass_indirect_on()) {
            source = rt_image(RtImage::Indirect);
        }
        break;
    }
    if (source == rt::kNoImage) {
        return;
    }
    const Renderer::RtCommands commands = renderer_.rt_commands_begin();
    rt::TextureView depth{};
    unsigned width = 0u, height = 0u;
    rt::ImageFormat format = rt::ImageFormat::Rgb10a2;
    bool drawn = false;
    if (renderer_.rt_depth_begin(depth, width, height, format)) {
        rt_backend_.begin(commands.commands, renderer_.frame_slot(), commands.descriptors);
        const std::uint32_t image = rt_scene_image(format, width, height);
        if (image != rt::kNoImage) {
            const float values[3][4] = {};
            rt::PassBinds binds;
            binds.constants = rt_light_frame_.binds.constants;
            binds.pass_constants = rt_bounce_constants(0u, 0u, signal.channels | signal.over << 8u, values);
            binds.reads[0] = source;
            binds.writes[0] = image;
            rt_backend_.dispatch(RtShader::Signal, binds, (width + 7u) / 8u, (height + 7u) / 8u);
            drawn = true;
        }
        renderer_.rt_depth_end();
        if (drawn) {
            renderer_.rt_copy_to_scene(rt_backend_.image_to_copy(image));
        }
    }
    renderer_.rt_commands_end();
    if (drawn) {
        renderer_.set_debug_capture_paths(rt_hold_folder_ + "signal_" + signal.name + "_" + rt_hold_number_ + ".ppm", {});
        if (!renderer_.debug_copy_scene_to_readback()) {
            std::fprintf(stderr, "[rt-signals] %s of picture %s not taken (the scene's colour cannot be read now)\n", signal.name,
                         rt_hold_number_.c_str());
        }
        renderer_.set_debug_capture_paths({}, {});
        rt_signal_waiting_ = true;
    }
}

// Signals the programs draw: S, the share traced_painted gives (program.hlsli traced_colour, view_flags 64; FC c36),
// and A, the air's own light (traced_scatter, view_flags 128; l65). Its walk of the hold draws the scene with the
// signal in place of the colour, read back at the scene's end (walk_scene_end), before anything drawn after the
// scene. The scene's later walks draw as ever; the kept picture is 35 walks or more on.
std::uint32_t RenderCore::rt_program_signal_due() const noexcept {
    if (!::galaxy::diagnosing(::galaxy::kDiagnoseRtSignals) || rt_hold_left_ == 0u || rt_hold_left_ % kSignalApart != 0u) {
        return 0u;
    }
    const unsigned which = rt_hold_left_ / kSignalApart - 1u;
    if (which >= std::size(kSignals)) {
        return 0u;
    }
    return kSignals[which].of == SignalOf::Share ? 64u
           : kSignals[which].of == SignalOf::Air ? 128u
           : kSignals[which].of == SignalOf::Fog ? 256u
                                                 : 0u;
}

void RenderCore::rt_program_signal() {
    if (rt_program_signal_ == 0u) {
        return;
    }
    const char* const name = rt_program_signal_ == 64u ? "S" : rt_program_signal_ == 128u ? "A" : "F";
    rt_program_signal_ = 0u;
    renderer_.set_debug_capture_paths(rt_hold_folder_ + "signal_" + name + "_" + rt_hold_number_ + ".ppm", {});
    if (!renderer_.debug_copy_scene_to_readback()) {
        std::fprintf(stderr, "[rt-signals] %s of picture %s not taken (the scene's colour cannot be read now)\n", name,
                     rt_hold_number_.c_str());
    }
    renderer_.set_debug_capture_paths({}, {});
    rt_signal_waiting_ = true;
}

// Whether a kept picture is held (rt_hold) before it is shown.
bool RenderCore::rt_holds() const noexcept {
    return rt_backend_.started() && ::galaxy::diagnosing(::galaxy::kDiagnoseRtReference | ::galaxy::kDiagnoseRtSignals);
}

// A picture the test keeps (`picture`, ".../present_<n>.ppm"), with the reference or the signals asked for: the same
// list is walked again, kRefHoldWalks times (kSignalHoldWalks for the signals alone), each walk a frame of its own that
// shows nothing; the walk after them is the one shown. The reference adds up over them; the real-time passes settle as
// they would over a still scene; the signals are taken on the last ones.
void RenderCore::rt_hold(ListEntry& entry, const core::WalkTime& time, const std::string& picture) {
    const std::size_t slash = picture.find_last_of("/\\");
    rt_hold_folder_ = slash == std::string::npos ? std::string{} : picture.substr(0u, slash + 1u);
    std::string name = slash == std::string::npos ? picture : picture.substr(slash + 1u);
    if (name.rfind("present_", 0u) == 0u) {
        name.erase(0u, 8u);
    }
    if (const std::size_t dot = name.rfind('.'); dot != std::string::npos) {
        name.erase(dot);
    }
    rt_hold_number_ = name;
    rt_hold_walks_ = ::galaxy::diagnosing(::galaxy::kDiagnoseRtReference) ? kRefHoldWalks : kSignalHoldWalks;
    std::fprintf(stderr, "[rt-hold] picture %s: the list walked %u times more\n", rt_hold_number_.c_str(), rt_hold_walks_);
    for (unsigned left = rt_hold_walks_; left != 0u; --left) {
        (void)renderer_.end_frame(false);
        renderer_.debug_log_readback();
        ++frame_index_;
        rt_hold_left_ = left;
        begin_walk_frame(entry);
        walk_list(entry.data, entry.before != nullptr ? &entry.before->data : nullptr, time);
    }
    rt_hold_left_ = 0u;
}

// At the end, and when the setting goes off while the game runs (the GPU idle): the back end's stop frees every image
// and buffer, so they are only forgotten here; the next start makes them again.
void RenderCore::rt_bounce_stop() noexcept {
    for (std::uint32_t& image : rt_ref_images_) {
        image = rt::kNoImage;
    }
    for (std::uint32_t& image : rt_ref_t_images_) {
        image = rt::kNoImage;
    }
    rt_ref_t_width_ = rt_ref_t_height_ = 0u;
    rt_ref_restart_ = false;
    rt_bounce_retired_.clear();
    rt_bounce_width_ = rt_bounce_height_ = 0u;
    rt_dome_mean_ = 0.0f;
    rt_hit_mean_ = 0.0f;
    std::fill_n(rt_dome_colour_, 3u, 0.0f);
    rt_ref_list_ = nullptr;
    rt_ref_ready_ = false;
    rt_ref_now_ = 0u;
    rt_ref_serial_ = 0u;
    rt_shade_turn_ = 0u;
    rt_shade_at_ = 0u;
    rt_shade_view_ = rt::BufferView{};
    rt_grid_entries_ = 0u;
    rt_signal_waiting_ = false;
}

}  // namespace galaxy::render
