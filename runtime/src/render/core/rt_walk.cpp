// SPDX-License-Identifier: GPL-3.0-only
// RenderCore's part of ray tracing: when the tracer runs, which meshes it holds structures for, and what a frame
// builds (render/rt holds the policy, rt_backend.h the API).
#include "galaxy/render/render_core.h"
#include "galaxy/render/rt/rt_denoiser.h"

#include "galaxy/launch_args.h"
#include "galaxy/render/ambient_occlusion_shape.h"
#include "galaxy/render/program_table.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/rt_caps.h"
#include "galaxy/platform_time.h"
#include "galaxy/platform_paths.h"
#include "galaxy/render/post_process_settings.h"
#include "galaxy/render/rt_materials.h"
#include "render/core/pipelines.h"
#include "render/core/world_lights.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace galaxy::render {

using core::kRowsPerMatrix;

namespace {

#include "render/rt/rt_effect_lights.inc"
#include "render/rt/rt_blue_noise.inc"

// A light's colour as the game or the cooker gives it (a display value: sRGB-encoded) as linear light of the same
// luminance (l65): the composite is linear, so the chroma decodes while the candela keeps the light's strength.
// `decode` false (dev light_srgb=0): as given.
void light_colour(const float in[3], float out[3], bool decode) {
    float lin[3];
    for (unsigned c = 0; c < 3u; ++c) {
        const float x = std::max(in[c], 0.0f);
        lin[c] = !decode ? x : x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
    }
    const float before = 0.2126f * std::max(in[0], 0.0f) + 0.7152f * std::max(in[1], 0.0f) + 0.0722f * std::max(in[2], 0.0f);
    const float after = 0.2126f * lin[0] + 0.7152f * lin[1] + 0.0722f * lin[2];
    const float scale = decode && after > 1e-6f ? before / after : 1.0f;
    for (unsigned c = 0; c < 3u; ++c) {
        out[c] = lin[c] * scale;
    }
}

// A particle effect's luminance (candela a square metre of what it draws), by the name its draws carry: the pak's row
// (rt_effects.tsv, a mod's pack may add its effects), else the compiled table's; -1 for a name neither holds.
float effect_luminance(std::uint32_t name) noexcept {
    if (const RtEffectRow* const row = rt_effect(name); row != nullptr) {
        return row->luminance;
    }
    const RtEffectLight* const end = std::end(kRtEffectLights);
    const RtEffectLight* const at =
        std::lower_bound(std::begin(kRtEffectLights), end, name, [](const RtEffectLight& e, std::uint32_t n) { return e.name < n; });
    return at != end && at->name == name ? static_cast<float>(at->luminance) : -1.0f;
}

// 100 units of the game are a metre (the player is 1.6 m tall).
constexpr float kUnitsPerMetre = 100.0f;

// The glow a draw gives now (defined with rt_emitter): the item words read it too (c60, A2-12).
void live_glow(const NebulaFrameList& list, const NebulaFrameItem& item, const core::GpuMesh& mesh, float out[4]);

// What an emitter is from one frame to the next (its kind, whose it is, which of them): a pixel's reservoir finds its
// light again by it. Two emitters with one number are taken for each other. Never 0: kNoEmitterId is an emitter no
// reservoir finds again.
constexpr std::uint32_t kNoEmitterId = 0u;
constexpr std::uint32_t rt_emitter_id(std::uint32_t kind, std::uint32_t whose, std::uint32_t which) {
    std::uint32_t h = (whose * 0x9E3779B1u) ^ (which * 0x85EBCA6Bu) ^ (kind * 0xC2B2AE35u);
    h ^= h >> 15u;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12u;
    return h != kNoEmitterId ? h : 1u;
}

// A particle's number by a place of it (its matrix's column 3, or its first corner), bit for bit: the place a frame ago
// the game hands on is a copy of what it wrote then (ParticleDraw.cpp startFrame), so hashing it finds the number the
// frame before gave (l84, A2-09).
inline std::uint32_t rt_particle_id(float x, float y, float z) {
    return rt_emitter_id(2u, std::bit_cast<std::uint32_t>(x) ^ (std::bit_cast<std::uint32_t>(y) * 0x27D4EB2Fu),
                         std::bit_cast<std::uint32_t>(z));
}

// The words of an item in the light pass's table (rt_light).
constexpr std::size_t kRtItemWords = 20u;  // rt_frame.hlsli kItemBytes

// The sky's dome among the sky's draws: what a mirror shows where nothing of the scene stands.
constexpr bool item_is_sky_dome(const NebulaFrameItem& item) noexcept {
    const std::uint32_t category = (item.flags & NEBULA_ITEM_CATEGORY_MASK) >> NEBULA_ITEM_CATEGORY_SHIFT;
    return category == NEBULA_CATEGORY_SKY || category == NEBULA_CATEGORY_ASTRO_DOME_SKY;
}

// The draw types (MR::DrawType) whose code draws a water surface: OceanBowl 6, OceanRing 7, its pipe's inside 8 and
// outside 9, OceanSphere 10, WaterRoad 11, WhirlPoolAccelerator 12 (SceneFunction.hpp; WaterPlant is weed).
constexpr bool category_is_water(std::uint32_t category) noexcept {
    return category >= NEBULA_CATEGORY_DRAW + 6u && category <= NEBULA_CATEGORY_DRAW + 12u;
}

// A billboard mesh's rows turned to face the sun (l83): `rows` as drawn (turned to the eye), `kind` 1 a full
// billboard, 2 a Y billboard; `sun` and `up` unit, in the same eye space. The place and each axis's size stay; a full
// billboard's face (+z) goes to the sun, its up the player's across it; a Y billboard keeps its up axis and turns
// about it. False: no turn (no size, or the sun along the axis it would turn about).
bool sun_card(const float (*rows)[4], std::uint8_t kind, const float sun[3], const float up[3], float out[3][4]) noexcept {
    float axis[3][3], size[3];
    for (unsigned c = 0; c < 3u; ++c) {
        for (unsigned r = 0; r < 3u; ++r) {
            axis[c][r] = rows[r][c];
        }
        size[c] = std::sqrt(axis[c][0] * axis[c][0] + axis[c][1] * axis[c][1] + axis[c][2] * axis[c][2]);
        if (!(size[c] > 1e-6f) || !std::isfinite(size[c])) {
            return false;
        }
    }
    const auto across = [](const float a[3], const float b[3], float out_[3]) {
        out_[0] = a[1] * b[2] - a[2] * b[1];
        out_[1] = a[2] * b[0] - a[0] * b[2];
        out_[2] = a[0] * b[1] - a[1] * b[0];
    };
    // A mirrored matrix stays mirrored.
    float xy[3];
    across(axis[0], axis[1], xy);
    const float hand = xy[0] * axis[2][0] + xy[1] * axis[2][1] + xy[2] * axis[2][2] < 0.0f ? -1.0f : 1.0f;
    float y[3], z[3];
    if (kind == 2u) {
        // The Y billboard's own up; its face the sun across it.
        for (unsigned r = 0; r < 3u; ++r) {
            y[r] = axis[1][r] / size[1];
        }
        const float along = sun[0] * y[0] + sun[1] * y[1] + sun[2] * y[2];
        for (unsigned r = 0; r < 3u; ++r) {
            z[r] = sun[r] - y[r] * along;
        }
    } else {
        // The face the sun; its up the player's across it.
        for (unsigned r = 0; r < 3u; ++r) {
            z[r] = sun[r];
        }
        const float along = up[0] * sun[0] + up[1] * sun[1] + up[2] * sun[2];
        for (unsigned r = 0; r < 3u; ++r) {
            y[r] = up[r] - sun[r] * along;
        }
    }
    float* const turn = kind == 2u ? z : y;
    const float length = std::sqrt(turn[0] * turn[0] + turn[1] * turn[1] + turn[2] * turn[2]);
    if (!(length > 0.05f)) {
        return false;  // within 3 degrees of the axis: no card
    }
    for (unsigned r = 0; r < 3u; ++r) {
        turn[r] /= length;
    }
    float x[3];
    across(y, z, x);
    for (unsigned r = 0; r < 3u; ++r) {
        out[r][0] = x[r] * size[0] * hand;
        out[r][1] = y[r] * size[1];
        out[r][2] = z[r] * size[2];
        out[r][3] = rows[r][3];
    }
    return true;
}

// What the game drew the item as (NEBULA_CATEGORY_*).
constexpr std::uint32_t item_category(const NebulaFrameItem& item) noexcept {
    return (item.flags & NEBULA_ITEM_CATEGORY_MASK) >> NEBULA_ITEM_CATEGORY_SHIFT;
}

// The player's body and what he wears (the landing cue's caster, l80).
constexpr bool item_is_player(const NebulaFrameItem& item) noexcept {
    const std::uint32_t category = item_category(item);
    return category == NEBULA_CATEGORY_PLAYER || category == NEBULA_CATEGORY_PLAYER_DECORATION ||
           category == NEBULA_CATEGORY_PLAYER_PARTS;
}

// What a skinned body reaches past its joints (a hand's or a sole's thickness, in game units): the landing cue's body.
constexpr float kBodySkin = 10.0f;

// The sky and what is drawn with it: around the scene, not in it. It stops no light and takes none of the scene's.
constexpr bool item_is_sky(const NebulaFrameItem& item) noexcept {
    const std::uint32_t category = item_category(item);
    return category == NEBULA_CATEGORY_SKY || category == NEBULA_CATEGORY_AIR || category == NEBULA_CATEGORY_SUN ||
           category == NEBULA_CATEGORY_ASTRO_DOME_SKY;
}

}  // namespace

// Ray tracing starts with the device when the setting is on and the device can trace (rt_caps.h). A change of the
// setting takes a new device (Vulkan makes its device with the tracer's extensions or without them).
// The development tunables (rt_dev.h): a run with --diagnostics reads rt_dev.txt in its settings folder each time ray
// tracing starts; a player's run reads nothing and every number stays the code's.
void RenderCore::rt_dev_read() {
    rt_dev_set_ = 0u;
    if (::galaxy::launch_args().diagnostics == 0u) {
        return;
    }
    const std::filesystem::path& named = ::galaxy::launch_args().settings;
    const std::filesystem::path folder = named.empty() ? ::galaxy::nebula_data_directory() : named;
    std::FILE* const file = folder.empty() ? nullptr : ::galaxy::open_path(folder / "rt_dev.txt", "r");
    if (file == nullptr) {
        return;
    }
    std::string text;
    char chunk[512];
    for (std::size_t got = 0; (got = std::fread(chunk, 1u, sizeof(chunk), file)) != 0u;) {
        text.append(chunk, got);
    }
    std::fclose(file);
    constexpr std::string_view kBlank = " \t\r\n";
    std::string_view rest = text;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0u, end).substr(0u, rest.substr(0u, end).find('#'));
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1u);
        for (;;) {
            const std::size_t start = line.find_first_not_of(kBlank);
            if (start == std::string_view::npos) {
                break;
            }
            line.remove_prefix(start);
            const std::size_t stop = line.find_first_of(kBlank);
            const std::string_view item = line.substr(0u, stop);
            line = stop == std::string_view::npos ? std::string_view{} : line.substr(stop);
            const std::size_t eq = item.find('=');
            unsigned which = kRtDevCount;
            for (unsigned i = 0; eq != std::string_view::npos && i < kRtDevCount; ++i) {
                if (kRtDevNames[i] == item.substr(0u, eq)) {
                    which = i;
                }
            }
            const std::string number(eq != std::string_view::npos ? item.substr(eq + 1u) : std::string_view{});
            char* after = nullptr;
            const float value = number.empty() ? 0.0f : std::strtof(number.c_str(), &after);
            if (which == kRtDevCount || number.empty() || after == nullptr || *after != '\0' || !std::isfinite(value)) {
                std::fprintf(stderr, "[rt-dev] not understood: %.*s\n", static_cast<int>(item.size()), item.data());
                continue;
            }
            rt_dev_[which] = value;
            rt_dev_set_ |= std::uint64_t{1} << which;
        }
    }
    std::string taken;
    for (unsigned i = 0; i < kRtDevCount; ++i) {
        if ((rt_dev_set_ >> i & 1u) != 0u) {
            char one[64];
            std::snprintf(one, sizeof(one), " %.*s=%g", static_cast<int>(kRtDevNames[i].size()), kRtDevNames[i].data(),
                          static_cast<double>(rt_dev_[i]));
            taken += one;
        }
    }
    std::fprintf(stderr, "[rt-dev]%s\n", taken.empty() ? " none taken" : taken.c_str());
}

void RenderCore::rt_start() {
    if (!get_render_config().ray_tracing_enabled || !rt_capable(rt_caps()) ||
        (::galaxy::diagnosing(::galaxy::kDiagnoseRtToggle) && rt_setting_ == 0)) {
        return;
    }
    if (!rt_backend_.start(renderer_.device())) {
        std::fprintf(stderr, "[rt] the device did not start the tracer: ray tracing is off\n");
        return;
    }
    registries_.tell_retired_meshes(true);
    rt_dev_read();
    rt_log_at_ = 0u;
    rt_building_ = false;
    set_render_tracing(true, fsr3_available());  // single-sampled, anti-aliased temporally (plan 16.3)
}

// The setting changed while the game runs (plan 5.3 G-6): the tracer starts or stops here, between frames, on the same
// device. The GPU finishes first, so nothing it still reads goes; the draws give back the light pictures. With
// --diagnostics rt-toggle (and the setting on) it is switched off and on every 90 walks.
void RenderCore::rt_follow_setting(const RenderConfig& render) {
    bool wanted = render.ray_tracing_enabled;
    if (wanted && ::galaxy::diagnosing(::galaxy::kDiagnoseRtToggle)) {
        wanted = (rt_toggle_walks_++ / 90u) % 2u == 0u;
    }
    const int now = wanted ? 1 : 0;
    // Plan 26: the light images could not be made for 30 walks running (the device's memory is full): ray tracing
    // turns itself off until the player turns the setting off and on again; the game goes on as without it.
    if (rt_image_failures_ >= 30u && rt_backend_.started()) {
        std::fprintf(stderr, "[rt] the device has no memory for the light images: ray tracing is off until the setting is turned on again\n");
        renderer_.rt_wait_idle();
        renderer_.rt_unpublish_lights(texture_store_);
        rt_stop();
        rt_gave_up_ = true;
    }
    if (rt_gave_up_) {
        rt_image_failures_ = 0u;
        if (!wanted) {
            rt_gave_up_ = false;
            rt_setting_ = 0;
        }
        return;
    }
    if (rt_setting_ < 0 || now == rt_setting_) {
        rt_setting_ = now;  // the device started as the setting said
        return;
    }
    rt_setting_ = now;
    renderer_.rt_wait_idle();
    if (!wanted) {
        if (rt_backend_.started()) {
            renderer_.rt_unpublish_lights(texture_store_);
            rt_stop();
        }
    } else if (!rt_backend_.started() && rt_capable(rt_caps())) {
        (void)rt::shaders_load(renderer_.device());
        rt_start();
    }
    std::fprintf(stderr, "[rt] the setting changed: ray tracing %s\n", rt_backend_.started() ? "on" : "off");
}

// Before the device's memory goes (it is lost: nothing recorded runs any more), or when the setting turns it off.
void RenderCore::rt_stop() noexcept {
    set_render_tracing(false, false);  // the player's anti-aliasing again
    registries_.tell_retired_meshes(false);
    rt_view_image_ = rt::kNoImage;  // the back end's stop frees its images and buffers
    rt_view_retired_.clear();
    rt_images_ = rt_no_images();
    rt_noise_filled_ = false;
    rt_blend_seen_ = false;
    rt_blend_walk_before_ = 0u;
    rt_light_frame_ = RtFrame{};
    rt_light_history_ = false;
    rt_light_retired_.clear();
    rt_bounce_stop();
    rt_light_on_ = false;
    rt_prepass_walk_ = false;
    rt_skin_buffer_ = rt::kNoBuffer;
    rt_skin_bytes_ = 0u;
    rt_skin_retired_.clear();
    rt_micromap_wants_.clear();
    rt_micromap_asked_.clear();
    rt_drop_volumes_.clear();
    rt_sun_known_ = false;
    rt_sun_light_ = 0u;
    rt_sun_source_ = 0u;
    rt_scene_pass_ = NEBULA_NONE32;
    rt_scene_view_ = NEBULA_NONE32;
    rt_scene_.clear();
    rt_geometry_.clear();
    rt_backend_.stop();
}

// A mesh one matrix places is traced through a structure built once from its vertices as they lie, in the form the
// scene draws (the refined one with curved surfaces on). Key: the mesh's number, twice, plus one for the refined form.
// As a mesh arrives, the form the setting draws is built ahead; a draw that takes the other form asks for it when it
// is first in a scene. A mesh whose vertices carry matrices of their own is skinned first, and a site's vertices are
// their own step.
void RenderCore::rt_want(std::uint32_t id, const core::GpuMesh& mesh) {
    if (mesh.site_stride != 0u || mesh.matrices != 0u) {
        return;
    }
    rt_want_form(id, mesh, curved_refine_ && mesh.refined_index_count != 0u);
}

void RenderCore::rt_want_form(std::uint32_t id, const core::GpuMesh& mesh, bool refined) {
    refined = refined && mesh.refined_indices != 0u;
    if (mesh.positions == 0u || mesh.indices == 0u) {
        return;
    }
    rt::TriangleInput input;
    input.positions = mesh.positions;
    input.position_stride = 12u;
    input.vertex_count = refined ? mesh.refined_vertex_count : mesh.vertex_count;
    input.indices = refined ? mesh.refined_indices : mesh.indices;
    input.index_count = refined ? mesh.refined_index_count : mesh.index_count;
    input.index_bytes = mesh.index_bytes;
    rt_geometry_.want(std::uint64_t{id} * 2u + (refined ? 1u : 0u), input);
}

// Once a frame, before anything is drawn: the builds the frame can afford (5.3 G-3, measured, not constant). With a
// scene on screen, what half the time a frame has left after its scene's GPU time holds at the rate this GPU's builds
// were timed at, between 4,000 and 64,000 triangles (8,000 until timed); a frame with none (a loading screen) 64,000.
void RenderCore::rt_frame(const NebulaFrameList& list) {
    std::uint64_t budget = 64000u;
    if ((list.flags & NEBULA_FRAME_HAS_SCENE) != 0u) {
        const double frame_ms = 1000.0 / std::max(static_cast<double>(rt_frame_rate_), 10.0);
        const double spare_ms = std::max(frame_ms - renderer_.scene_gpu_ema_ms(), 0.0);
        budget = rt_build_rate_ > 0.0 ? static_cast<std::uint64_t>(std::clamp(rt_build_rate_ * spare_ms * 0.5, 4000.0, 64000.0)) : 8000u;
    }
    rt_budget_ = budget;
    rt_backend_.frame(renderer_.frame_slot());
    rt_geometry_.frame(renderer_.rt_commands_begin().commands, renderer_.frame_slot(), frame_index_, budget);
    renderer_.rt_commands_end();
    const rt::GeometryStats& stats = rt_geometry_.stats();
    // The rate: triangles a millisecond of the builds timed (a frame slot's last frame), over builds of 2,000 or more.
    if (stats.timed_triangles >= 2000u && stats.timed_ticks != 0u) {
        const double ms = static_cast<double>(stats.timed_ticks) * renderer_.gpu_ms_per_tick();
        if (ms > 0.0) {
            const double rate = static_cast<double>(stats.timed_triangles) / ms;
            rt_build_rate_ = rt_build_rate_ > 0.0 ? rt_build_rate_ + (rate - rt_build_rate_) * 0.2 : rate;
        }
    }
    const bool diagnosing = ::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs);
    if (diagnosing && (stats.builds != 0u || stats.compactions != 0u)) {
        std::fprintf(stderr, "[rt] frame %llu: built %u (%llu triangles), compacted %u, waiting %u; %u structures, %.2f MB\n",
                     static_cast<unsigned long long>(frame_index_), stats.builds,
                     static_cast<unsigned long long>(stats.built_triangles), stats.compactions, stats.waiting, stats.structures,
                     static_cast<double>(stats.structure_bytes) / 1048576.0);
    }
    // One line when a scene's meshes are all built (and, diagnosing, every 600 frames).
    const bool caught_up = rt_building_ && stats.waiting == 0u;
    rt_building_ = stats.waiting != 0u;
    if (caught_up || (diagnosing && frame_index_ >= rt_log_at_)) {
        rt_log_at_ = frame_index_ + 600u;
        const rt::Backend::Live live = rt_backend_.live();
        std::fprintf(stderr,
                     "[rt] structures: %u built (%u compacted), %u waiting, %llu triangles, %.2f MB in %.2f MB of pages, most in a frame "
                     "%llu triangles; budget %llu, %.0f built a ms, %u released past the ceiling; micromaps %u (%.2f MB, %u built); "
                     "images %u (%.2f MB), buffers %u (%.2f MB)\n",
                     stats.structures, stats.compacted, stats.waiting, static_cast<unsigned long long>(stats.triangles),
                     static_cast<double>(stats.structure_bytes) / 1048576.0, static_cast<double>(stats.page_bytes) / 1048576.0,
                     static_cast<unsigned long long>(stats.most_built_triangles), static_cast<unsigned long long>(rt_budget_),
                     rt_build_rate_, stats.released, stats.micromaps, static_cast<double>(stats.micromap_bytes) / 1048576.0,
                     stats.micromaps_built, live.images, static_cast<double>(live.image_bytes) / 1048576.0, live.buffers,
                     static_cast<double>(live.buffer_bytes) / 1048576.0);
    }
}

// The scene of this walk: every draw of the scene's passes that a ray can meet, as an instance in the scene camera's
// eye space (the rows the walk made), and the structure over them, built before the scene's first draw.
void RenderCore::rt_scene(const NebulaFramePass& begin) {
    const core::ListData& now = *walk_.now;
    const NebulaFrameList& list = *now.list;
    rt_scene_.begin();
    for (const RtMicromapWant& want : rt_micromap_wants_) {
        rt_micromap_asked_.erase(want.key);
    }
    rt_micromap_wants_.clear();
    rt_casters_.clear();
    rt_emitters_.clear();
    rt_emitters_finished_ = false;
    // The point lights actors asked for this frame: each a light of its own (the game's one blended light gives way
    // to them in the traced programs, rt_light.hlsl).
    if (begin.scene_camera < walk_cameras_.size() && walk_.now->list->point_lights != nullptr) {
        const NebulaFrameList& frame = *walk_.now->list;
        const Affine34& eye = walk_cameras_[begin.scene_camera];
        for (std::uint32_t n = 0; n < frame.point_light_count && rt_emitters_.size() < 512u; ++n) {
            const NebulaFramePointLight& light = frame.point_lights[n];
            if (!(light.half_distance > 0.0f || light.candela > 0.0f) ||
                !std::isfinite(light.half_distance + light.candela + light.position[0] + light.position[1] + light.position[2])) {
                continue;
            }
            // Its intensity: the caller's, or from the game's own falloff: its light is half at `half_distance`, and
            // the game means a surface there well lit (a quarter of a bright day's exposure).
            const float half_metres = light.half_distance / kUnitsPerMetre;
            const float candela = light.candela > 0.0f ? light.candela : 37.5f * half_metres * half_metres;
            RtEmitter& emitter = rt_emitters_.emplace_back();
            float linear[3];
            light_colour(light.color, linear, rt_dev(RtDev::LightSrgb, 1.0f) > 0.5f);
            for (unsigned r = 0; r < 3u; ++r) {
                emitter.centre[r] = eye[r * 4u] * light.position[0] + eye[r * 4u + 1u] * light.position[1] + eye[r * 4u + 2u] * light.position[2] +
                                    eye[r * 4u + 3u];
                emitter.colour[r] = linear[r] * std::min(candela, 5000.0f);
            }
            // Its body's size where the game knows it (the actor's binder), else a small lamp's.
            emitter.radius = light.radius > 0.0f && std::isfinite(light.radius) ? std::clamp(light.radius, 5.0f, 400.0f) : 25.0f;
            emitter.reach = 0.0f;
            emitter.slot = NEBULA_NONE32;
            emitter.kind = 0u;
            emitter.reserved[1] = rt_emitter_id(0u, 0u, n);
        }
    }
    rt_emitter_blocks_.clear();
    rt_water_ring_item_ = ~0u;
    rt_emissive_areas_.assign(walk_.now->list->item_count, 0u);
    rt_glow_bodies_.assign(walk_.now->list->item_count, 0u);
    rt_drops_.clear();
    rt_drop_volumes_.clear();
    rt_scene_pass_ = NEBULA_NONE32;
    rt_scene_view_ = NEBULA_NONE32;
    rt_scene_camera_ = begin.scene_camera;
    if (begin.scene_camera == NEBULA_NONE32) {
        return;
    }
    rt::SceneCount count{};
    rt_seen_.clear();
    rt_skins_.clear();
    // The sun in this walk's eye space, for the billboards' sun cards (l83): the stage's fixed sun (d05) through the
    // scene camera, and the player's up; none before the stage has its sun.
    float card_sun[3]{}, card_up[3] = {0.0f, 1.0f, 0.0f};
    bool cards = false;
    if (rt_sun_fixed_known_ && rt_sun_fixed_stage_ == list.stage_hash && begin.scene_camera < walk_cameras_.size()) {
        const Affine34& eye = walk_cameras_[begin.scene_camera];
        float size = 0.0f;
        for (unsigned r = 0; r < 3u; ++r) {
            card_sun[r] = eye[r * 4u] * rt_sun_world_[0] + eye[r * 4u + 1u] * rt_sun_world_[1] + eye[r * 4u + 2u] * rt_sun_world_[2];
            size += card_sun[r] * card_sun[r];
        }
        size = std::sqrt(size);
        cards = size > 1e-6f && std::isfinite(size);
        for (unsigned r = 0; cards && r < 3u; ++r) {
            card_sun[r] /= size;
        }
        if (list.sky_up_known != 0u) {
            std::memcpy(card_up, list.sky_up, sizeof(card_up));
        }
    }
    // A billboard's second instance, turned to the sun and met by the shadow rays alone (l83).
    const auto add_card = [&](const float (*rows)[4], std::uint32_t item_index, std::uint8_t billboard, std::uint64_t structure,
                              rt::Faces faces, std::uint32_t kind) {
        float card[3][4];
        if (cards && sun_card(rows, billboard, card_sun, card_up, card)) {
            rt_scene_.add(card, item_index, rt::kMaskShadowOnly, structure, faces, kind | rt::kKindCard,
                          rt::Micromapped::Other);  // every hit asked: a micromap's opaque states would pass no own card
        }
    };
    std::uint64_t skin_bytes = 0u;
    const auto first = static_cast<std::uint32_t>(&begin - list.passes) + 1u;
    bool early = true;  // before the scene's opaque end
    for (std::uint32_t p = first; p < list.pass_count && list.passes[p].kind != NEBULA_PASS_SCENE_END; ++p) {
        const NebulaFramePass& pass = list.passes[p];
        early = early && pass.kind != NEBULA_PASS_OPAQUE_END;
        // A traced pass (NEBULA_PASS_TRACED_DRAW): things the game draws for the tracer alone, which stop its light.
        const bool traced_only = pass.kind == NEBULA_PASS_TRACED_DRAW;
        if ((pass.kind != NEBULA_PASS_DRAW && !traced_only) || pass.layer != NEBULA_LAYER_SCENE || pass.target != 0u) {
            continue;
        }
        for (std::uint32_t i = pass.first_item; i < pass.first_item + pass.item_count; ++i) {
            const NebulaFrameItem& item = list.items[i];
            // The sky's dome stands in the scene under its own mask: no light stops at it, a mirror shows it. It is
            // drawn around the eye whatever its camera, so its rows serve as they are.
            const bool sky = item_is_sky_dome(item) && (item.flags & (NEBULA_ITEM_RING | NEBULA_ITEM_INSTANCES)) == 0u;
            if ((now.block_camera[item.block] != begin.scene_camera && !sky) || list.views[item.view].orthographic != 0u) {
                ++count.other_view;
                continue;
            }
            // A shadow drawn by hand is no thing of the scene: it stops no ray (draw_walker.cpp leaves its draw out).
            if (item_category(item) == NEBULA_CATEGORY_GAME_SHADOW ||
                ((item.flags & (NEBULA_ITEM_RING | NEBULA_ITEM_INSTANCES)) == 0u &&
                 (registries_.mesh(item.mesh).hit_flags & (NEBULA_HIT_DRAWN_SHADOW | NEBULA_HIT_DRAWN_SHEEN)) != 0u)) {
                ++count.flat;
                continue;
            }
            const core::PipelineEntry& pipeline = registries_.pipeline(item.pipeline);
            const std::uint32_t state = pipeline.desc.state;
            const bool depth_test = (state & 1u << 10u) != 0u;
            const bool depth_write = (state & 1u << 14u) != 0u;
            const std::uint32_t cull = state >> 15u & 3u;
            // No surface of the scene: drawn flat (no depth test), without colour, or as the sky around it (but its dome).
            const std::uint32_t compare = state >> 11u & 7u;
            const bool hidden_side = depth_test && (compare == NEBULA_COMPARE_NEVER || compare == NEBULA_COMPARE_GREATER ||
                                                    compare == NEBULA_COMPARE_NEQUAL || compare == NEBULA_COMPARE_GEQUAL);
            if (((!depth_test || item_is_sky(item)) && !sky) || hidden_side || (state & NEBULA_STATE_COLOR_WRITE) == 0u ||
                cull == NEBULA_CULL_ALL) {
                ++count.flat;
                continue;
            }
            const rt::Faces faces = sky                        ? rt::Faces::Both
                                    : cull == NEBULA_CULL_BACK ? rt::Faces::Front
                                    : cull == NEBULA_CULL_FRONT ? rt::Faces::Back
                                                                : rt::Faces::Both;
            // A pixel program that can discard has a per-sample form (compile_programs.py): an alpha-tested surface.
            // A map part the game shows by its clip areas alone (Matter Splatter, plan 26 M10: drawn whole, painted
            // over where not revealed) stands where a ray meets it inside them only: a cutout the volumes test (g_clip).
            const bool clipped = list.clip_rule != NEBULA_CLIP_RULE_NONE && item_category(item) == NEBULA_CATEGORY_CLIPPED_MAP_PARTS;
            // (kKindClipped: no micromap may settle its hits, the volumes move each frame.)
            const std::uint32_t kind =
                (!sky && (clipped || program_variant(pipeline.desc.pixel, 0u, 1u) == kProgramSample) ? rt::kKindCutout : 0u) |
                (!sky && clipped ? rt::kKindClipped : 0u);
            const bool lit_program = program_lit(pipeline.desc.pixel) || program_sums_key(pipeline.desc.pixel);
            // A mesh on a billboard joint turns to the eye each frame (compose.cpp): as a caster its shadow would swing
            // with the camera. It is seen as drawn and stops no ray (l79; a sun-facing caster of its own is for later).
            const bool turned = (item.flags & (NEBULA_ITEM_RING | NEBULA_ITEM_INSTANCES)) == 0u && registries_.mesh(item.mesh).billboard != 0u;
            if (turned && traced_only) {
                ++count.flat;
                continue;
            }
            // An opaque draw whose material's row says translucent (steam, a cloud's body; FB b101, EVERYTHING
            // 1.15): it dims the sun by its alpha (let_through, as a blended translucent thing), it stops no light.
            const bool dims = (item.flags & (NEBULA_ITEM_RING | NEBULA_ITEM_INSTANCES)) == 0u &&
                              registries_.mesh(item.mesh).hit_class == NEBULA_HIT_TRANSLUCENT;
            const std::uint32_t mask =
                sky ? rt::kMaskSky
                // Seen by no view, mirror, bounce or probe: the sun's and the local lights' shadow rays alone meet it.
                : traced_only ? rt::kMaskShadowOnly
                : depth_write && (state & 3u) == 0u
                    // What stops light is a surface of the scene's opaque phases; an opaque draw made after them (a
                    // fill, a silhouette, a helper shape) is seen by the views and casts nothing.
                    ? rt::kMaskOpaque | (early ? rt::kMaskEarly : 0u) |
                          (dims && early && !turned ? rt::kMaskBlended : 0u) |
                          (kind == 0u && early && !turned && !dims ? rt::kMaskSolid | (lit_program ? rt::kMaskLit : 0u) : 0u) |
                          (kind != 0u && early && !turned && !dims ? rt::kMaskCutout : 0u)
                    // A blended one would tint the sun's rays (let_through) as the camera turns it: no ray meets it.
                    : turned ? 0u : rt::kMaskBlended;
            if ((item.flags & (NEBULA_ITEM_INSTANCES | NEBULA_ITEM_PARTICLE | NEBULA_ITEM_WIDE)) != 0u) {
                ++count.sprites;
                // No surface a ray meets, but what glows among them is a light.
                if ((item.flags & (NEBULA_ITEM_INSTANCES | NEBULA_ITEM_PARTICLE)) != 0u) {
                    rt_particle_emitter(item, state, begin.scene_camera);
                }
                continue;
            }
            if ((item.flags & NEBULA_ITEM_RING) != 0u) {
                // This frame's own vertices: its triangles, through the draw's matrix, join the frame's ring mesh of
                // their kind.
                const std::uint32_t stride = item.mesh;
                const std::uint64_t room = list.ring_vertex_bytes > item.ring_at ? list.ring_vertex_bytes - item.ring_at : 0u;
                if (stride < 12u || list.ring_vertices == nullptr) {
                    continue;
                }
                const bool water = category_is_water(item_category(item));
                RtRing& ring = rt_rings_[water ? 3u : (mask & rt::kMaskEarly) != 0u ? 0u : (mask & rt::kMaskOpaque) != 0u ? 1u : 2u];
                if (ring.positions.empty()) {
                    ring.item = i;
                    rt_water_ring_item_ = water ? i : rt_water_ring_item_;
                }
                ring.regenerated = ring.regenerated || (item.flags & NEBULA_ITEM_DISCRETE_VERTICES) != 0u;
                const auto* vertices = static_cast<const std::byte*>(list.ring_vertices) + item.ring_at;
                const float (*place)[4] = walk_rows_cpu_ + walk_block_rows_[item.block] + item.matrix * kRowsPerMatrix;
                const bool indexed = (item.flags & NEBULA_ITEM_INDEXED) != 0u;
                const std::uint32_t corners = item.count - item.count % 3u;
                for (std::uint32_t c = 0; c < corners; ++c) {
                    const std::uint64_t vertex = indexed ? list.ring_indices[item.first + c] : c;
                    float at[3] = {0.0f, 0.0f, 0.0f};
                    if (vertex * stride + 12u <= room) {
                        std::memcpy(at, vertices + vertex * stride, sizeof(at));
                    }
                    for (unsigned r = 0; r < 3u; ++r) {
                        ring.positions.push_back(place[r][0] * at[0] + place[r][1] * at[1] + place[r][2] * at[2] + place[r][3]);
                    }
                }
                ++count.ring;
                count.ring_vertices += corners;
                continue;
            }
            const core::GpuMesh& mesh = registries_.mesh(item.mesh);
            const bool one_matrix = (item.flags & NEBULA_ITEM_ONE_MATRIX) != 0u;
            if (mesh.site_stride != 0u) {
                // A site's mesh, registered once: one stream, its place first. Its vertices may name matrices of their
                // own (the pipeline's layout says so): those are left out; the rest stand under one matrix.
                if (pipeline.matrix_at != NEBULA_NONE32 && !one_matrix) {
                    ++count.site;
                    continue;
                }
                const std::uint64_t key = std::uint64_t{item.mesh} * 2u;
                const std::uint64_t structure = rt_geometry_.name(key);
                if (structure == 0u) {
                    rt::TriangleInput input;
                    input.positions = mesh.positions;
                    input.position_stride = mesh.site_stride;
                    input.vertex_count = mesh.vertex_count;
                    input.indices = mesh.indices;
                    input.index_count = mesh.index_count;
                    input.index_bytes = 4u;
                    rt_geometry_.want(key, input);
                    const float (*waiting_at)[4] = walk_rows_cpu_ + walk_block_rows_[item.block] + (one_matrix ? item.matrix : 0u) * kRowsPerMatrix;
                    rt_geometry_.drawn_at(key, waiting_at[0][3] * waiting_at[0][3] + waiting_at[1][3] * waiting_at[1][3] + waiting_at[2][3] * waiting_at[2][3]);
                    ++count.waiting;
                    continue;
                }
                const std::uint32_t matrix = one_matrix ? item.matrix : 0u;
                const auto [seen, fresh] =
                    rt_seen_.try_emplace(RtPlace{key, item.block, matrix}, static_cast<std::uint32_t>(rt_scene_.instances().size()));
                const rt::Micromapped micromapped = rt_micromap_for(key, item, i, kind, mask, mesh.index_count / 3u);
                if (!fresh) {
                    rt_scene_.also(seen->second, mask);
                    if (micromapped != rt::Micromapped::Own && rt_geometry_.micromap_tag(key) != 0u) {
                        rt_scene_.no_micromap(seen->second);
                    }
                    ++count.repeats;
                    continue;
                }
                rt_scene_.add(walk_rows_cpu_ + walk_block_rows_[item.block] + matrix * kRowsPerMatrix, i, mask, structure, faces, kind,
                              micromapped);
                if (turned && mask != 0u) {
                    add_card(walk_rows_cpu_ + walk_block_rows_[item.block] + matrix * kRowsPerMatrix, i, registries_.mesh(item.mesh).billboard,
                             structure, faces, kind);
                }
                count.triangles += (mesh.indices != 0u ? mesh.index_count : mesh.vertex_count) / 3u;
                continue;
            }
            const bool refined = curved_refine_ && mesh.refined_index_count != 0u && (item.flags & NEBULA_ITEM_PLAIN_MESH) == 0u;
            const std::uint32_t triangles = (refined ? mesh.refined_index_count : mesh.index_count) / 3u;
            const std::uint64_t key = std::uint64_t{item.mesh} * 2u + (refined ? 1u : 0u);
            if (mesh.matrices != 0u && !one_matrix) {
                // Its structure is this draw's own (the mesh under this block's matrices), named by the block's slot.
                const NebulaFrameBlock& block = list.blocks[item.block];
                const std::uint64_t moving = 1ull << 63u | (key & 0x7FFFFFFFu) << 32u | block.slot;
                const auto [seen, fresh] = rt_seen_.try_emplace(RtPlace{moving, item.block, NEBULA_NONE32},
                                                                 static_cast<std::uint32_t>(rt_skins_.size()));
                if (!fresh) {
                    rt_skins_[seen->second].mask |= mask;
                    ++count.repeats;
                    continue;
                }
                RtSkin& skin = rt_skins_.emplace_back();
                skin.key = moving;
                skin.vertices = refined ? mesh.refined_vertex_count : mesh.vertex_count;
                skin.positions = mesh.positions;
                skin.matrices = mesh.matrices;
                const std::uint32_t matrices =
                    block.model != 0u ? registries_.model(block.model).tables.draw_matrix_count : block.joint_count;
                skin.rows = walk_.rows_address + (std::uint64_t{walk_.rows_base} + walk_block_rows_[item.block]) * 16u;
                skin.rows_bytes = matrices * kRowsPerMatrix * 16u;
                // The block's first matrix is the space its vertices are kept in; one that cannot be turned back
                // (no size) leaves them in the eye's.
                static constexpr Affine34 kSame = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
                Affine34 root{}, back{};
                std::memcpy(root.data(), walk_rows_cpu_ + walk_block_rows_[item.block], sizeof(root));
                if (!affine_inverse(root, back)) {
                    root = back = kSame;
                }
                std::memcpy(skin.root, root.data(), sizeof(skin.root));
                std::memcpy(skin.root_inverse, back.data(), sizeof(skin.root_inverse));
                if ((item.slot != 0u && list.shadow_drop_count != 0u) || mesh.glow[3] != 0.0f) {
                    // A skinned object's reach: where its matrices stand, and as much again about them.
                    const float (*rows)[4] = walk_rows_cpu_ + walk_block_rows_[item.block];
                    float low[3], high[3];
                    for (unsigned r = 0; r < 3u; ++r) {
                        low[r] = high[r] = rows[r][3];
                    }
                    for (std::uint32_t m = 1; m < matrices; ++m) {
                        for (unsigned r = 0; r < 3u; ++r) {
                            low[r] = std::min(low[r], rows[m * kRowsPerMatrix + r][3]);
                            high[r] = std::max(high[r], rows[m * kRowsPerMatrix + r][3]);
                        }
                    }
                    const float centre[3] = {(low[0] + high[0]) * 0.5f, (low[1] + high[1]) * 0.5f, (low[2] + high[2]) * 0.5f};
                    const float half = 0.5f * std::sqrt((high[0] - low[0]) * (high[0] - low[0]) + (high[1] - low[1]) * (high[1] - low[1]) +
                                                        (high[2] - low[2]) * (high[2] - low[2]));
                    if (item.slot != 0u && list.shadow_drop_count != 0u) {
                        rt_caster(item.slot, centre, half * 2.0f + 30.0f);
                        rt_caster_body(item.slot, low, high, kBodySkin, item_is_player(item));
                    }
                    if (mesh.glow[3] != 0.0f) {
                        rt_emitter(item, mesh, centre, half + 30.0f);
                    }
                }
                skin.item = i;
                skin.mask = mask;
                skin.faces = faces;
                skin.kind = kind;
                skin.out = skin_bytes;
                skin.input.position_stride = 12u;
                skin.input.vertex_count = skin.vertices;
                skin.input.indices = refined ? mesh.refined_indices : mesh.indices;
                skin.input.index_count = refined ? mesh.refined_index_count : mesh.index_count;
                skin.input.index_bytes = mesh.index_bytes;
                skin_bytes += (std::uint64_t{skin.vertices} * 12u + 15u) & ~std::uint64_t{15u};
                ++count.skinned;
                count.skinned_triangles += triangles;
                continue;
            }
            const std::uint64_t structure = rt_geometry_.name(key);
            if (structure == 0u) {
                rt_want_form(item.mesh, mesh, refined);
                const std::uint32_t at_matrix = one_matrix || mesh.single_matrix != NEBULA_MODEL_NONE ? item.matrix : 0u;
                const float (*waiting_at)[4] = walk_rows_cpu_ + walk_block_rows_[item.block] + at_matrix * kRowsPerMatrix;
                rt_geometry_.drawn_at(key, waiting_at[0][3] * waiting_at[0][3] + waiting_at[1][3] * waiting_at[1][3] + waiting_at[2][3] * waiting_at[2][3]);
                ++count.waiting;
                continue;
            }
            const std::uint32_t matrix = one_matrix || mesh.single_matrix != NEBULA_MODEL_NONE ? item.matrix : 0u;
            const auto [seen, fresh] =
                rt_seen_.try_emplace(RtPlace{key, item.block, matrix}, static_cast<std::uint32_t>(rt_scene_.instances().size()));
            const rt::Micromapped micromapped = rt_micromap_for(key, item, i, kind, mask, triangles);
            if (!fresh) {
                rt_scene_.also(seen->second, mask);
                if (micromapped != rt::Micromapped::Own && rt_geometry_.micromap_tag(key) != 0u) {
                    rt_scene_.no_micromap(seen->second);
                }
                ++count.repeats;
                continue;
            }
            const float (*place)[4] = walk_rows_cpu_ + walk_block_rows_[item.block] + matrix * kRowsPerMatrix;
            rt_scene_.add(place, i, mask, structure, faces, kind, micromapped);
            if (turned && mask != 0u) {
                add_card(place, i, mesh.billboard, structure, faces, kind);
            }
            count.triangles += triangles;
            // Off screen (the game keeps it drawn for the tracer while it traces, 5.3 G-1): the raster passes leave it out.
            count.outside += item_culled(item, pass, state, &mesh) ? 1u : 0u;
            if ((item.slot != 0u && list.shadow_drop_count != 0u) || mesh.glow[3] != 0.0f) {
                // The mesh's box through its matrix: for the drop shadow its object may cast and the light it may give.
                float centre[3], extent[3], radius = 0.0f;
                for (unsigned r = 0; r < 3u; ++r) {
                    extent[r] = 0.0f;
                    centre[r] = place[r][3];
                    for (unsigned c = 0; c < 3u; ++c) {
                        centre[r] += place[r][c] * (mesh.min[c] + mesh.max[c]) * 0.5f;
                        extent[r] += std::fabs(place[r][c]) * (mesh.max[c] - mesh.min[c]) * 0.5f;
                    }
                    radius += extent[r] * extent[r];
                }
                if (item.slot != 0u && list.shadow_drop_count != 0u) {
                    rt_caster(item.slot, centre, std::sqrt(radius));
                    const float low[3] = {centre[0] - extent[0], centre[1] - extent[1], centre[2] - extent[2]};
                    const float high[3] = {centre[0] + extent[0], centre[1] + extent[1], centre[2] + extent[2]};
                    rt_caster_body(item.slot, low, high, 0.0f, item_is_player(item));
                }
                if (mesh.glow[3] != 0.0f) {
                    rt_emitter(item, mesh, centre, std::sqrt(radius), place);
                }
            }
        }
    }
    const Renderer::RtCommands commands = renderer_.rt_commands_begin();
    rt_backend_.begin(commands.commands, renderer_.frame_slot(), commands.descriptors);
    std::erase_if(rt_skin_retired_, [&](const std::pair<std::uint64_t, std::uint32_t>& retired) {
        if (retired.first + rt::kFramesToRelease > frame_index_) {
            return false;
        }
        rt_backend_.free_buffer(retired.second);
        return true;
    });
    if (!rt_skins_.empty()) {
        if (skin_bytes > rt_skin_bytes_) {
            if (rt_skin_buffer_ != rt::kNoBuffer) {
                rt_skin_retired_.emplace_back(frame_index_, rt_skin_buffer_);
            }
            rt_skin_bytes_ = std::max<std::uint64_t>(std::bit_ceil(skin_bytes), 1u << 20u);
            rt_skin_buffer_ = rt_backend_.make_buffer(rt_skin_bytes_, "Nebula RT Skinned Vertices");
            if (rt_skin_buffer_ == rt::kNoBuffer) {
                rt_skin_bytes_ = 0u;
            }
        }
        if (rt_skin_buffer_ != rt::kNoBuffer) {
            rt_skin();
        } else {
            rt_skins_.clear();
        }
    }
    // The ring meshes: their triangles into this frame's rows memory, each kind one structure.
    static constexpr std::uint32_t kRingMasks[4] = {rt::kMaskOpaque | rt::kMaskEarly | rt::kMaskSolid, rt::kMaskOpaque, rt::kMaskBlended,
                                                    rt::kMaskBlended};
    for (unsigned kind = 0; kind < 4u; ++kind) {
        RtRing& ring = rt_rings_[kind];
        if (ring.positions.empty()) {
            continue;
        }
        const std::size_t bytes = ring.positions.size() * sizeof(float);
        const UploadRing::Allocation at = renderer_.matrix_ring().allocate(bytes, 16u);
        std::memcpy(at.cpu, ring.positions.data(), bytes);
        rt::TriangleInput input;
        input.positions = at.gpu;
        input.position_stride = 12u;
        input.vertex_count = static_cast<std::uint32_t>(ring.positions.size() / 3u);
        rt_geometry_.move(1ull << 62u | kind, input, ring.regenerated);
    }
    static constexpr float kNoTransform[3][4] = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
    rt_geometry_.refit();
    count.rebuilt = rt_geometry_.stats().rebuilds;
    for (const RtSkin& skin : rt_skins_) {
        if (const std::uint64_t structure = rt_geometry_.name(skin.key); structure != 0u) {
            rt_scene_.add(skin.root, skin.item, skin.mask, structure, skin.faces, skin.kind);
        }
    }
    for (unsigned kind = 0; kind < 4u; ++kind) {
        RtRing& ring = rt_rings_[kind];
        if (!ring.positions.empty()) {
            if (const std::uint64_t structure = rt_geometry_.name(1ull << 62u | kind); structure != 0u) {
                rt_scene_.add(kNoTransform, ring.item, kRingMasks[kind], structure);
            }
        }
        ring.positions.clear();
        ring.regenerated = false;
    }
    const std::span<const rt::Instance> instances = rt_scene_.instances();
    count.instances = static_cast<std::uint32_t>(instances.size());
    for (const rt::Instance& instance : instances) {
        count.opaque += (instance.id_mask >> 24u & rt::kMaskOpaque) != 0u ? 1u : 0u;
        count.blended += (instance.id_mask >> 24u & rt::kMaskOpaque) == 0u ? 1u : 0u;
    }
    if (!instances.empty()) {
        const UploadRing::Allocation at = renderer_.matrix_ring().allocate(instances.size_bytes(), 16u);
        std::memcpy(at.cpu, instances.data(), instances.size_bytes());
        if (rt_scene_.build(at.gpu, frame_index_)) {
            rt_scene_pass_ = first - 1u;
            // The scene's view is the one its opaque end names: the begin pass may still name the view before it (a
            // flat one). A pass that traces before the opaque end asks for it here.
            for (std::uint32_t p = first; p < list.pass_count && list.passes[p].kind != NEBULA_PASS_SCENE_END; ++p) {
                const NebulaFramePass& pass = list.passes[p];
                if (pass.kind == NEBULA_PASS_OPAQUE_END && pass.scene_view != NEBULA_NONE32 &&
                    list.views[pass.scene_view].orthographic == 0u) {
                    rt_scene_view_ = pass.scene_view;
                    break;
                }
            }
            if (rt_scene_view_ != NEBULA_NONE32) {
                rt_drops_collect(begin);
            }
        }
    }
    renderer_.rt_commands_end();
    if (::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs)) {
        rt_census_.add(count);
        if (rt_census_.scenes >= 300u) {
            rt_census_.report(rt_scene_.bytes());
        }
    }
}

// The frame's skinned draws: each one's vertices through its matrices into the tracer's vertex buffer, in its root
// matrix's space (rt_skin.hlsl), and its structure named to be refitted from them. Recorded through the back end's
// open commands.
// 5.3 G-2: what a draw has of its mesh's opacity micromap. One made from this draw's map serves an alpha-tested draw of
// the opaque phases (its mask holds kMaskCutout; a blended one weighs every hit itself); any other draw of a mesh with
// a micromap turns it off for its instance. An alpha-tested draw of a static mesh with none asks for one, once, from
// this draw (rt_micromaps).
rt::Micromapped RenderCore::rt_micromap_for(std::uint64_t key, const NebulaFrameItem& item, std::uint32_t i, std::uint32_t kind,
                                            std::uint32_t mask, std::uint32_t triangles) {
    const std::uint32_t has = rt_geometry_.micromap_tag(key);
    std::uint32_t tag = 0u;
    if ((kind & rt::kKindCutout) != 0u && (mask & rt::kMaskCutout) != 0u) {
        if (const core::TextureTable& table = registries_.table(item.textures);
            table.count != 0u && !table.list.empty() && table.list[0].texture != 0u && !TextureStore::is_target(table.list[0].texture)) {
            tag = table.list[0].texture;
        }
    }
    if (tag == 0u) {
        return has != 0u ? rt::Micromapped::Other : rt::Micromapped::None;
    }
    if (has != 0u) {
        return has == tag ? rt::Micromapped::Own : rt::Micromapped::Other;
    }
    if (rt_backend_.micromaps() && !::galaxy::diagnosing(::galaxy::kDiagnoseRtNoMicromaps) && rt_geometry_.takes_micromap(key) &&
        rt_micromap_asked_.insert(key).second) {
        rt_micromap_wants_.push_back({key, i, tag, triangles});
    }
    return rt::Micromapped::None;
}

// 5.3 G-2: each micromap asked for this frame, a pass over its mesh as the item that asked draws it (RtShader::Omm, one
// group a triangle) into a buffer of the tracer's, which the geometry builds the micromap from in a coming frame. Up
// to kTrianglesAFrame triangles a frame (one want at least); a want whose item has no mesh or map bound this frame,
// or that waits, asks again when next drawn.
void RenderCore::rt_micromaps() {
    constexpr std::uint32_t kTrianglesAFrame = 65536u, kGroupsAcross = 65535u;
    std::uint32_t done = 0u;
    for (const RtMicromapWant& want : rt_micromap_wants_) {
        const std::uint32_t* words = rt_item_words_.data() + std::size_t{want.item} * kRtItemWords;
        if ((done != 0u && done + want.triangles > kTrianglesAFrame) || std::size_t{want.item} * kRtItemWords >= rt_item_words_.size() ||
            words[2] >= rt::kPassMeshArenas || words[8] == NEBULA_NONE32 || !rt_geometry_.takes_micromap(want.key)) {
            rt_micromap_asked_.erase(want.key);
            continue;
        }
        const auto round = [](std::uint64_t bytes) { return (bytes + rt::kRangeAlign - 1u) & ~(rt::kRangeAlign - 1u); };
        rt::MicromapInput input;
        input.triangles = want.triangles;
        input.triangles_at = round(std::uint64_t{want.triangles} * 64u);
        input.indices_at = round(input.triangles_at + std::uint64_t{want.triangles} * 8u);
        input.buffer = rt_backend_.make_buffer(input.indices_at + std::uint64_t{want.triangles} * 4u, "Nebula RT Micromap Input");
        if (input.buffer == rt::kNoBuffer) {
            rt_micromap_asked_.erase(want.key);
            continue;
        }
        rt::PassBinds binds = rt_pass_binds();
        binds.write_buffer = input.buffer;
        binds.pass_constants = rt_pass_constants(want.item, want.triangles, static_cast<std::uint32_t>(input.triangles_at),
                                                 static_cast<std::uint32_t>(input.indices_at));
        rt_backend_.dispatch(RtShader::Omm, binds, std::min(want.triangles, kGroupsAcross), (want.triangles + kGroupsAcross - 1u) / kGroupsAcross);
        if (!rt_geometry_.give_micromap(want.key, input, want.tag)) {
            rt_skin_retired_.emplace_back(frame_index_, input.buffer);  // goes once no frame can run the pass
        }
        done += want.triangles;
    }
    rt_micromap_wants_.clear();
}

void RenderCore::rt_skin() {
    struct SkinConstants {
        std::uint32_t at[4];
        std::uint32_t count[4];
        float root[3][4];
    };
    rt_backend_.buffer_for(rt_skin_buffer_, false);
    for (const RtSkin& skin : rt_skins_) {
        SkinConstants constants{};
        rt::PassBinds binds;
        binds.buffers[0] = rt_backend_.view_of(skin.positions, std::uint64_t{skin.vertices} * 12u, constants.at[0]);
        binds.buffers[1] = rt_backend_.view_of(skin.matrices, std::uint64_t{skin.vertices} * 4u, constants.at[1]);
        binds.buffers[2] = rt_backend_.view_of(skin.rows, skin.rows_bytes, constants.at[2]);
        constants.at[3] = static_cast<std::uint32_t>(skin.out);
        constants.count[0] = skin.vertices;
        std::memcpy(constants.root, skin.root_inverse, sizeof(constants.root));
        const UploadRing::Allocation at = renderer_.constant_ring().allocate(256u, 256u);
        std::memcpy(at.cpu, &constants, sizeof(constants));
        binds.constants = {at.gpu, 256u};
        binds.write_buffer = rt_skin_buffer_;
        rt_backend_.dispatch(RtShader::Skin, binds, (skin.vertices + 63u) / 64u, 1u);
    }
    rt_backend_.buffer_for(rt_skin_buffer_, true);
    for (RtSkin& skin : rt_skins_) {
        skin.input.own_buffer = rt_skin_buffer_;
        skin.input.positions = skin.out;
        rt_geometry_.move(skin.key, skin.input);
    }
}

// The tracer's scene drawn by camera rays over the game's opaque scene, in the scene target's own format so it is
// copied over the colour. rt-check compares each ray with the depth drawn and counts the classes (rt_view.hlsl).
void RenderCore::rt_view(const NebulaFramePass& pass) {
    const bool check = ::galaxy::diagnosing(::galaxy::kDiagnoseRtCheck);
    const rt::Structure scene = rt_scene_.structure();
    const ScaledViewport viewport = viewport_pixels(walk_.now->list->views[pass.scene_view].viewport, renderer_.frame_map());
    if (scene.name == 0u || !viewport.valid()) {
        return;
    }
    const RtView camera = scene_depth_view(walk_view_terms_[pass.scene_view].data(), viewport);
    const Renderer::RtCommands commands = renderer_.rt_commands_begin();
    rt::TextureView depth{};
    unsigned width = 0u, height = 0u;
    rt::ImageFormat format = rt::ImageFormat::Rgb10a2;
    if (renderer_.rt_depth_begin(depth, width, height, format)) {
        rt_backend_.begin(commands.commands, renderer_.frame_slot(), commands.descriptors);
        static_cast<void>(rt_scene_image(format, width, height));
        struct ViewConstants {
            float ray[4], depth[4], viewport[4];
            std::uint32_t mode[4];
        } constants{};
        ao::Constants eye{};
        bool drawn = false;
        if (rt_view_image_ != rt::kNoImage && ao::make_constants(camera, width, height, 1u, false, eye)) {
            std::memcpy(constants.ray, eye.ray, sizeof(constants.ray));
            std::memcpy(constants.depth, eye.depth, sizeof(constants.depth));
            std::memcpy(constants.viewport, eye.viewport, sizeof(constants.viewport));
            constants.mode[0] = check ? 1u : 0u;
            constants.mode[1] = check ? rt::kMaskEarly : rt::kMaskOpaque | rt::kMaskBlended;
            const UploadRing::Allocation at = renderer_.constant_ring().allocate(256u, 256u);
            std::memcpy(at.cpu, &constants, sizeof(constants));
            rt::PassBinds binds;
            binds.scene = scene;
            binds.textures[0] = depth;
            binds.writes[0] = rt_view_image_;
            binds.constants = {at.gpu, 256u};
            binds.counters = true;
            rt_backend_.dispatch(RtShader::View, binds, (width + 7u) / 8u, (height + 7u) / 8u);
            rt_backend_.end_counters();
            drawn = true;
        }
        renderer_.rt_depth_end();
        if (drawn) {
            renderer_.rt_copy_to_scene(rt_backend_.image_to_copy(rt_view_image_));
        }
    }
    renderer_.rt_commands_end();
    if (check) {
        // What the GPU counted in this frame slot's last frame.
        std::uint32_t counted[rt::kCounters];
        rt_backend_.read_counters(renderer_.frame_slot(), counted);
        for (unsigned i = 0; i < 8u; ++i) {
            rt_check_sum_[i] += counted[i];
        }
        if (++rt_check_frames_ >= 300u) {
            const double all = static_cast<double>(std::max<std::uint64_t>(rt_check_sum_[0], 1u));
            const double surface = static_cast<double>(std::max<std::uint64_t>(rt_check_sum_[0] - rt_check_sum_[1], 1u));
            std::fprintf(stderr,
                         "[rt-check] %u frames, %llu pixels: sky %.2f%%; of the rest: same %.2f%% not-traced %.2f%% traced-behind %.2f%% "
                         "traced-in-front %.2f%% traced-only %.2f%% cutout-in-front %.2f%%\n",
                         rt_check_frames_, static_cast<unsigned long long>(rt_check_sum_[0]), 100.0 * static_cast<double>(rt_check_sum_[1]) / all,
                         100.0 * static_cast<double>(rt_check_sum_[2]) / surface, 100.0 * static_cast<double>(rt_check_sum_[3]) / surface,
                         100.0 * static_cast<double>(rt_check_sum_[4]) / surface, 100.0 * static_cast<double>(rt_check_sum_[5]) / surface,
                         100.0 * static_cast<double>(rt_check_sum_[6]) / surface, 100.0 * static_cast<double>(rt_check_sum_[7]) / surface);
            for (std::uint64_t& sum : rt_check_sum_) {
                sum = 0u;
            }
            rt_check_frames_ = 0u;
        }
    }
}

// Whether the prepass draws this item: an opaque surface of the scene (core::prepass_draws) under the scene's camera,
// its vertices a mesh's or the frame's own.
bool RenderCore::rt_prepass_draws(const NebulaFrameItem& item, std::uint32_t state) const {
    const core::ListData& now = *walk_.now;
    if ((item.flags & (NEBULA_ITEM_INSTANCES | NEBULA_ITEM_PARTICLE | NEBULA_ITEM_WIDE)) != 0u ||
        now.block_camera[item.block] != rt_scene_camera_ || now.list->views[item.view].orthographic != 0u) {
        return false;
    }
    if (!rt_blended_walk_) {
        return core::prepass_draws(state);
    }
    // The second layer: a blended surface whose mesh the light pass reads (its place comes from its triangle); and an
    // added one whose mesh is a glass row's, a clear shell drawn as an exchange (FB b94, EVERYTHING 6.7).
    const auto i = static_cast<std::size_t>(&item - now.list->items);
    const bool shell = (item.flags & NEBULA_ITEM_RING) == 0u && core::added_prepass_draws(state) &&
                       registries_.mesh(item.mesh).hit_class == NEBULA_HIT_GLASS;
    return (core::blended_prepass_draws(state) || shell) && (item.flags & NEBULA_ITEM_RING) == 0u &&
           i * kRtItemWords + 2u < rt_item_words_.size() && rt_item_words_[i * kRtItemWords + 2u] < rt::kPassMeshArenas;
}

// The second layer: the scene's blended surfaces take the traced light too. At the opaque phases' end the item target
// is drawn again, by the blended items whose mesh lies in the pass's memory (the last drawn names the pixel; the
// scene's depth stands, unwritten), and the frame's passes run over them again (layer 1, the frame's tables and
// constants as they are): a pixel's place is where its eye ray meets its triangle, its rays go straight (no average
// over time follows) and the pictures the opaque draws read, done with, take the result. The blended draws then read
// them in their traced form.
void RenderCore::rt_blended(const NebulaFramePass& end) {
    // Glass and water below medium: no second layer (plan 19.1; blended things read the opaque layer's light).
    if (!rt_light_on_ || rt_light_frame_.across == 0u || rt_scene_view_ == NEBULA_NONE32 ||
        rt_part(get_render_config(), RtPart::Transmit) < 2u) {
        return;
    }
    const NebulaFrameList& list = *walk_.now->list;
    const auto first = static_cast<std::uint32_t>(&end - list.passes) + 1u;
    rt_blended_walk_ = true;
    std::uint32_t wanted = 0u;
    for (std::uint32_t p = first; p < list.pass_count && list.passes[p].kind != NEBULA_PASS_SCENE_END; ++p) {
        const NebulaFramePass& pass = list.passes[p];
        if (pass.kind != NEBULA_PASS_DRAW || pass.layer != NEBULA_LAYER_SCENE || pass.target != 0u) {
            continue;
        }
        for (std::uint32_t i = pass.first_item; i < pass.first_item + pass.item_count; ++i) {
            wanted += rt_prepass_draws(list.items[i], registries_.pipeline(list.items[i].pipeline).desc.state) ? 1u : 0u;
        }
    }
    if (::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs) && rt_light_frames_ == 0u) {
        std::fprintf(stderr, "[rt-inputs]   second layer: %u blended items\n", wanted);
    }
    if (wanted == 0u || !renderer_.rt_items_begin()) {
        rt_blended_walk_ = false;
        return;
    }
    rt_prepass_walk_ = true;
    for (std::uint32_t p = first; p < list.pass_count && list.passes[p].kind != NEBULA_PASS_SCENE_END; ++p) {
        const NebulaFramePass& pass = list.passes[p];
        if (pass.kind == NEBULA_PASS_DRAW && pass.layer == NEBULA_LAYER_SCENE && pass.target == 0u) {
            walk_draw_pass_direct(pass);
        }
    }
    rt_prepass_walk_ = rt_blended_walk_ = false;
    const Renderer::RtCommands commands = renderer_.rt_commands_begin();
    rt::TextureView depth{}, items{};
    unsigned width = 0u, height = 0u;
    if (renderer_.rt_light_begin(depth, items, width, height)) {
        rt_backend_.begin(commands.commands, renderer_.frame_slot(), commands.descriptors);
        RtFrame& frame = rt_light_frame_;
        frame.binds.textures[0] = depth;
        frame.binds.textures[1] = items;
        // The layer's history images, once blended things are seen; with none, the layer has no traced light (the
        // programs read the opaque layer's there).
        if (!rt_blend_seen_) {
            rt_blend_seen_ = true;
            rt_blend_walk_before_ = 0u;
        }
        if (rt_images(width, height, rt_light_wanted_ | kRtPassBlend)) {
            const bool history = frame.history && rt_blend_walk_before_ + 1u == walk_.serial;
            frame.layer = 1u;
            frame.pass = rt_pass_constants(1u, 0u, 0u, history ? 1u : 0u);
            rt_pass_surface();
            rt_pass_sun();
            rt_sun_filter();
            rt_pass_lights();
            rt_pass_light();
            rt_pass_transmit();
            rt_blend_filters();
            frame.layer = 0u;
            rt_blend_walk_before_ = walk_.serial;
            // The second layer's guide (FB, 15.2): picture 9, what blended draws compare with (traced_by_air).
            // With none, none at all: layer 1's passes read the opaque layer's guide as compute work since its publishing.
            if (rt_blend_guide_mode_ != 0u) {
                void* const guide = rt_backend_.image_to_sample(rt_now(RtImage::BlendGuideA));
                rt_blend_guide_mode_ = guide != nullptr ? 1u : 0u;
                if (guide != nullptr) {
                    rt_blend_guide_slot_ = renderer_.rt_publish_light(guide, texture_store_, 9u);
                }
                ++walk_view_stamp_;  // the blended draws' views carry it
            }
        }
        renderer_.rt_light_end();
        // The draws read the pictures again.
        (void)rt_backend_.image_to_sample(rt_image(RtImage::Light));
        (void)rt_backend_.image_to_sample(rt_image(RtImage::Added));
        (void)rt_backend_.image_to_sample(rt_image(RtImage::Mirror));
        (void)rt_backend_.image_to_sample(rt_image(RtImage::Facing));
        // What is seen through glass and water, bent (rt_pass_transmit.cpp): picture 6, for the blended draws.
        if (rt_transmit_drawn_) {
            if (void* const seen = rt_backend_.image_to_sample(rt_image(RtImage::Transmit)); seen != nullptr) {
                rt_transmit_slot_ = renderer_.rt_publish_light(seen, texture_store_, 6u);
                ++walk_view_stamp_;  // the blended draws' views carry it
            } else {
                rt_transmit_drawn_ = false;
            }
        }
    }
    renderer_.rt_commands_end();
}

// Of the walk's 3D scenes into the frame, the one to trace: a perspective view over at least half the frame, the most
// scene draws, the later on a tie (plan 26: one scene a walk, never half of two).
std::uint32_t RenderCore::rt_pick_scene() const {
    const NebulaFrameList& list = *walk_.now->list;
    const FrameMap map = renderer_.frame_map();
    std::uint32_t picked = NEBULA_NONE32;
    std::uint64_t most = 0u;
    for (std::uint32_t p = 0; p < list.pass_count; ++p) {
        const NebulaFramePass& begin = list.passes[p];
        if (begin.kind != NEBULA_PASS_SCENE_BEGIN || begin.target != 0u) {
            continue;
        }
        bool seen = false;
        std::uint64_t draws = 0u;
        for (std::uint32_t q = p + 1u; q < list.pass_count && list.passes[q].kind != NEBULA_PASS_SCENE_END; ++q) {
            const NebulaFramePass& pass = list.passes[q];
            if (pass.kind == NEBULA_PASS_DRAW && pass.layer == NEBULA_LAYER_SCENE && pass.target == 0u) {
                draws += pass.item_count;
            }
            if (pass.kind == NEBULA_PASS_OPAQUE_END && pass.scene_view != NEBULA_NONE32 && pass.scene_view < list.view_count) {
                const NebulaFrameView& view = list.views[pass.scene_view];
                const ScaledViewport viewport = viewport_pixels(view.viewport, map);
                const double area = static_cast<double>(viewport.width) * viewport.height;
                seen = view.orthographic == 0u && viewport.valid() &&
                       area * 2.0 >= static_cast<double>(map.width) * static_cast<double>(map.height);
            }
        }
        if (seen && draws != 0u && draws >= most) {
            most = draws;
            picked = p;
        }
    }
    return picked;
}

// A scene pass this walk does not trace: drawn as with ray tracing off (its own drop shadows, no traced light).
void RenderCore::rt_untraced() {
    rt_light_on_ = false;
    rt_light_frame_.across = 0u;
    rt_prepassed_.assign(walk_.now->list->item_count, 0u);
    rt_drop_volumes_.clear();
    rt_drops_.clear();
}

// The prepass and the light pass, before the scene's first draw. Without them (no scene this walk, a multisampled
// target, no memory) the scene draws as ever and no light is traced.
void RenderCore::rt_prepass(const NebulaFramePass& begin) {
    const NebulaFrameList& list = *walk_.now->list;
    rt_light_on_ = false;
    rt_transmit_drawn_ = false;
    rt_blend_guide_mode_ = 0u;
    rt_prepassed_.assign(list.item_count, 0u);
    rt_light_frame_.across = 0u;  // no light pass yet this walk: no second layer either
    if (rt_scene_view_ == NEBULA_NONE32 || rt_scene_.name() == 0u || !renderer_.rt_items_begin()) {
        rt_drop_volumes_.clear();  // no light pass this walk: the game's own drop shadows draw
        rt_drops_.clear();
        return;
    }
    const auto first = static_cast<std::uint32_t>(&begin - list.passes) + 1u;
    const bool report = ::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs) && rt_light_frames_ == 0u;
    rt_sun_now_ = rt_sun(rt_sun_eye_);
    if (rt_sun_now_) {
        rt_sun_keys(rt_sun_eye_);
    }
    rt_prepass_walk_ = true;
    for (std::uint32_t p = first; p < list.pass_count; ++p) {
        const NebulaFramePass& pass = list.passes[p];
        if (report) {
            // The scene's passes, once a report: what the prepass has to stand beside.
            std::fprintf(stderr, "[rt-inputs]   pass %u: kind %u layer %u phase %u target %x items %u clear %u load %u\n", p, pass.kind,
                         pass.layer, pass.phase, pass.target, pass.item_count, pass.clear_channels, pass.load);
        }
        if (pass.kind == NEBULA_PASS_OPAQUE_END || pass.kind == NEBULA_PASS_SCENE_END) {
            break;
        }
        if (pass.kind == NEBULA_PASS_DRAW && pass.layer == NEBULA_LAYER_SCENE && pass.target == 0u) {
            walk_draw_pass_direct(pass);
        }
    }
    rt_prepass_walk_ = false;
    rt_light();
    rt_signals();
    if (!rt_light_on_) {
        rt_drop_volumes_.clear();
        rt_drops_.clear();
    }
}

namespace {

// The traced light's images (render_core.h RtImage), in its order: name, format, size, and the passes that need one
// (RenderCore::kRtPass*): an image is made only while one of them runs.
enum : std::uint8_t { kRtFull, kRtHalf, kRtVolume, kRtNoise, kRtTiles };
struct RtImageKind {
    const char* name;
    rt::ImageFormat format;
    std::uint8_t size;
    std::uint8_t passes;
};
constexpr std::uint8_t kForLight = 1u, kForIndirect = 2u, kForReflect = 4u, kForVolume = 8u, kForTransmit = 16u, kForBlend = 32u, kForGuides = 64u,
                       kForNear = 128u;
constexpr rt::ImageFormat kHalf4 = rt::ImageFormat::Rgba16f;  // shares as fractions: an average over time in bytes stalls
constexpr RtImageKind kRtImageKinds[] = {
    {"Nebula RT Guide A", kHalf4, kRtFull, kForLight},
    {"Nebula RT Guide B", kHalf4, kRtFull, kForLight},
    {"Nebula RT Normal", kHalf4, kRtFull, kForLight},
    {"Nebula RT Motion", kHalf4, kRtFull, kForLight},
    {"Nebula RT Material", rt::ImageFormat::Rgba8, kRtFull, kForLight},
    {"Nebula RT Base", rt::ImageFormat::Rgba8, kRtFull, kForLight},
    {"Nebula RT Sun Raw", kHalf4, kRtFull, kForLight},
    {"Nebula RT Light", kHalf4, kRtFull, kForLight},
    {"Nebula RT Lights", kHalf4, kRtFull, kForLight},
    {"Nebula RT Lights Average A", kHalf4, kRtFull, kForLight},
    {"Nebula RT Lights Average B", kHalf4, kRtFull, kForLight},
    {"Nebula RT Lights Moments A", kHalf4, kRtFull, kForLight},
    {"Nebula RT Lights Moments B", kHalf4, kRtFull, kForLight},
    {"Nebula RT Light Added", kHalf4, kRtFull, kForLight},
    {"Nebula RT Reservoirs A", kHalf4, kRtFull, kForLight},
    {"Nebula RT Reservoirs B", kHalf4, kRtFull, kForLight},
    {"Nebula RT Mirror", kHalf4, kRtFull, kForLight},
    {"Nebula RT Blue Noise", rt::ImageFormat::Rgba8, kRtNoise, kForLight},
    {"Nebula RT Sun Tiles", rt::ImageFormat::R8, kRtTiles, kForLight},
    {"Nebula RT Sun Average A", kHalf4, kRtFull, kForLight},
    {"Nebula RT Sun Average B", kHalf4, kRtFull, kForLight},
    {"Nebula RT Sun Distance A", rt::ImageFormat::R16f, kRtFull, kForLight},
    {"Nebula RT Sun Distance B", rt::ImageFormat::R16f, kRtFull, kForLight},
    {"Nebula RT Sun Blur", kHalf4, kRtFull, kForLight},
    {"Nebula RT Sun", kHalf4, kRtFull, kForLight},
    {"Nebula RT Lights Spec", kHalf4, kRtFull, kForLight},
    {"Nebula RT Blend Guide A", kHalf4, kRtFull, kForBlend},
    {"Nebula RT Blend Guide B", kHalf4, kRtFull, kForBlend},
    {"Nebula RT Blend Reservoirs A", kHalf4, kRtFull, kForBlend},
    {"Nebula RT Blend Reservoirs B", kHalf4, kRtFull, kForBlend},
    {"Nebula RT Blend Sun A", kHalf4, kRtFull, kForBlend},
    {"Nebula RT Blend Sun B", kHalf4, kRtFull, kForBlend},
    {"Nebula RT Blend Distance A", rt::ImageFormat::R16f, kRtFull, kForBlend},
    {"Nebula RT Blend Distance B", rt::ImageFormat::R16f, kRtFull, kForBlend},
    {"Nebula RT Blend Lights A", kHalf4, kRtFull, kForBlend},
    {"Nebula RT Blend Lights B", kHalf4, kRtFull, kForBlend},
    {"Nebula RT Blend Moments A", kHalf4, kRtFull, kForBlend},
    {"Nebula RT Blend Moments B", kHalf4, kRtFull, kForBlend},
    {"Nebula RT Facing", rt::ImageFormat::Rgba8, kRtFull, kForLight},
    {"Nebula RT Blend Body", rt::ImageFormat::R8, kRtFull, kForBlend},
    {"Nebula RT Indirect Raw", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT Indirect Average A", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT Indirect Average B", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT Indirect Moments A", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT Indirect Moments B", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT Indirect", kHalf4, kRtFull, kForIndirect},
    {"Nebula RT GI Reservoirs A", rt::ImageFormat::Rgba32ui, kRtHalf, kForIndirect},
    {"Nebula RT GI Reservoirs B", rt::ImageFormat::Rgba32ui, kRtHalf, kForIndirect},
    {"Nebula RT GI Weights A", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT GI Weights B", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT Indirect Guide A", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT Indirect Guide B", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT Indirect Motion", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT Indirect Step", kHalf4, kRtHalf, kForIndirect},
    {"Nebula RT Reflect Raw", kHalf4, kRtFull, kForReflect},
    {"Nebula RT Reflect Average A", kHalf4, kRtFull, kForReflect},
    {"Nebula RT Reflect Average B", kHalf4, kRtFull, kForReflect},
    {"Nebula RT Reflect", kHalf4, kRtFull, kForReflect},
    {"Nebula RT Transmit", kHalf4, kRtFull, kForTransmit},
    {"Nebula RT Volume A", kHalf4, kRtVolume, kForVolume},
    {"Nebula RT Volume B", kHalf4, kRtVolume, kForVolume},
    {"Nebula RT Scatter", kHalf4, kRtVolume, kForVolume},  // FB: the light in the air, integrated (rt_scatter.hlsl)
    {"Nebula RT Reflect Moved", rt::ImageFormat::R8, kRtFull, kForReflect},  // FB: where R changed (rt_reflect_time.hlsl)
    {"Nebula RT Guide Diffuse", rt::ImageFormat::Rgba8, kRtFull, kForGuides},  // FB: an outside denoiser's (rt_rr_guides.hlsl)
    {"Nebula RT Guide Specular", rt::ImageFormat::Rgba8, kRtFull, kForGuides},
    {"Nebula RT Guide Normal Roughness", kHalf4, kRtFull, kForGuides},
    {"Nebula RT Guide Hit", rt::ImageFormat::R16f, kRtFull, kForGuides},
    {"Nebula RT Near Raw", rt::ImageFormat::R16f, kRtFull, kForNear},  // FB b76: the near field (rt_near.hlsl)
    {"Nebula RT Near A", rt::ImageFormat::Rg16f, kRtFull, kForNear},
    {"Nebula RT Near B", rt::ImageFormat::Rg16f, kRtFull, kForNear},
};
// The light volume's cells: across, down and deep over the scene camera's view.
constexpr std::uint32_t kRtVolumeCells[3] = {96u, 54u, 48u};

// The frame's constants (rt_frame.hlsli FrameConstants, field for field): b0 of every pass of the frame.
struct RtFrameConstants {
    float ray[4], depth[4], viewport[4];
    std::uint32_t at[4], more[4], grid[4];
    float sun[4], stage[4], look[4];
    std::uint32_t glow[4];
    std::uint32_t mesh_at[12];  // where each bound arena's first byte is in its view; [8] the rows'
    float sky[4];               // what a mirror shows where its ray meets nothing at all
    float up[4];                // the player's up in the eye's space: where the sky is
    float ray_before[4];        // the frame before's eye ray terms
    float before[3][4];         // the frame before's eye space from this frame's
    std::uint32_t kept[4];      // the reservoirs' way from the frame before's emitters to this frame's
    std::uint32_t clip[4];      // the clip areas' volumes: first byte in the items' buffer, how many, the rule
    float dev[8][4];            // the tracer's development tunables (rt_dev.h from PaintedGlass; rt_frame.hlsli rt_dev_or)
    std::uint32_t dev_set[4];   // [0]: a bit a dev slot the run gave
};
// A pass's own (rt_frame.hlsli PassConstants): b1.
struct RtPassConstants {
    std::uint32_t pass[4];  // the layer, a filter's step, 1 on its last step, free
    float value[3][4];
};

}  // namespace

// The images the passes that run need, at the picture's size: made where missing, let go where no pass that runs
// needs them or the picture changed size (then no history is read either).
bool RenderCore::rt_images(unsigned width, unsigned height, std::uint32_t wanted) {
    static_assert(std::size(kRtImageKinds) == kRtImages);
    static_assert(kRtPassLight == kForLight && kRtPassIndirect == kForIndirect && kRtPassReflect == kForReflect && kRtPassVolume == kForVolume &&
                  kRtPassTransmit == kForTransmit && kRtPassBlend == kForBlend && kRtPassGuides == kForGuides &&
                  kRtPassNear == kForNear);
    std::erase_if(rt_light_retired_, [&](const std::pair<std::uint64_t, std::uint32_t>& retired) {
        if (retired.first + rt::kFramesToRelease > frame_index_) {
            return false;
        }
        rt_backend_.free_image(retired.second);
        return true;
    });
    const bool resized = rt_light_width_ != width || rt_light_height_ != height;
    bool whole = true;
    for (std::size_t i = 0; i < kRtImages; ++i) {
        const RtImageKind& kind = kRtImageKinds[i];
        std::uint32_t& image = rt_images_[i];
        const bool needed = (kind.passes & wanted) != 0u;
        if (image != rt::kNoImage && (!needed || resized)) {
            rt_light_retired_.emplace_back(frame_index_, image);
            image = rt::kNoImage;
        }
        if (needed && image == rt::kNoImage) {
            if ((kind.passes & kForNear) != 0u) {
                rt_near_before_ = false;  // made fresh: no history in it (FB b76d)
            }
            if (kind.size == kRtVolume) {
                image = rt_backend_.make_image(kind.format, kRtVolumeCells[0], kRtVolumeCells[1], kind.name, kRtVolumeCells[2]);
            } else if (kind.size == kRtNoise) {
                image = rt_backend_.make_image(kind.format, kRtBlueNoiseSize, kRtBlueNoiseSize, kind.name);
                rt_noise_filled_ = false;
            } else if (kind.size == kRtTiles) {
                image = rt_backend_.make_image(kind.format, (width + 15u) / 16u, (height + 15u) / 16u, kind.name);
            } else if (kind.size == kRtHalf) {
                image = rt_backend_.make_image(kind.format, (width + 1u) / 2u, (height + 1u) / 2u, kind.name);
            } else {
                image = rt_backend_.make_image(kind.format, width, height, kind.name);
            }
        }
        whole = whole && (!needed || image != rt::kNoImage);
    }
    if (resized) {
        rt_light_width_ = width;
        rt_light_height_ = height;
        rt_light_history_ = false;
    }
    return whole;
}

// A pass's own constants (b1): its layer, a filter's step and whether it is the last, and a word of its own.
rt::BufferView RenderCore::rt_pass_constants(std::uint32_t layer, std::uint32_t step, std::uint32_t last, std::uint32_t free) {
    RtPassConstants constants{};
    constants.pass[0] = layer;
    constants.pass[1] = step;
    constants.pass[2] = last;
    constants.pass[3] = free;
    const UploadRing::Allocation at = renderer_.constant_ring().allocate(256u, 256u);
    std::memcpy(at.cpu, &constants, sizeof(constants));
    return {at.gpu, 256u};
}

// What every pass of the frame binds: the scene, the frame's buffers and mesh memory, the depth and the item map, the
// frame's constants, the layer's, and the blue noise (t20: rt_sample.hlsli).
rt::PassBinds RenderCore::rt_pass_binds() const {
    rt::PassBinds binds = rt_light_frame_.binds;
    binds.pass_constants = rt_light_frame_.pass;
    binds.reads[rt::kPassReads - 1u] = rt_image(RtImage::BlueNoise);
    return binds;
}

// The blue noise into its image, once each time the image is made: the bytes of rt_blue_noise.inc through the upload
// ring (t1, at byte g_pass.w) into the Rgba8 image (rt_noise_fill.hlsl).
void RenderCore::rt_noise_fill() {
    const std::uint32_t noise = rt_image(RtImage::BlueNoise);
    if (rt_noise_filled_ || noise == rt::kNoImage) {
        return;
    }
    static_assert(sizeof(kRtBlueNoise) == std::size_t{kRtBlueNoiseSize} * kRtBlueNoiseSize * 4u);
    const UploadRing::Allocation bytes = renderer_.matrix_ring().allocate(sizeof(kRtBlueNoise), 256u);
    std::memcpy(bytes.cpu, kRtBlueNoise, sizeof(kRtBlueNoise));
    std::uint32_t offset = 0u;
    rt::PassBinds binds;
    binds.buffers[0] = rt_backend_.view_of(bytes.gpu, sizeof(kRtBlueNoise), offset);
    binds.constants = rt_light_frame_.binds.constants;
    binds.pass_constants = rt_pass_constants(0u, 0u, 0u, offset);
    binds.writes[0] = noise;
    rt_backend_.dispatch(RtShader::NoiseFill, binds, kRtBlueNoiseSize / 8u, kRtBlueNoiseSize / 8u);
    rt_noise_filled_ = true;
}

// The sun's V filtered (plan 7.3): the tiles that hold a penumbra (rt_sun_tiles), the average over time with the
// occluder's distance (rt_sun_time), then two blurs as wide as the penumbra is (rt_sun_blur; one on the second layer,
// plan 13.1). Each layer keeps its own averages. The light pass reads the last.
void RenderCore::rt_sun_filter() {
    const RtFrame& frame = rt_light_frame_;
    const bool second = frame.layer != 0u;
    const RtImage average_pair = second ? RtImage::BlendSunA : RtImage::SunAverageA;
    const RtImage distance_pair = second ? RtImage::BlendDistanceA : RtImage::SunDistanceA;
    const std::uint32_t guide = rt_layer_guide(), tiles = rt_image(RtImage::SunTiles), raw = rt_image(RtImage::SunRaw);
    const std::uint32_t average = rt_now(average_pair), distance = rt_now(distance_pair);
    rt::PassBinds tile;
    tile.constants = frame.binds.constants;
    tile.pass_constants = frame.pass;
    tile.reads[0] = raw;
    tile.reads[2] = guide;
    tile.writes[0] = tiles;
    rt_backend_.dispatch(RtShader::SunTiles, tile, (frame.width + 15u) / 16u, (frame.height + 15u) / 16u);
    rt::PassBinds over_time;
    over_time.constants = frame.binds.constants;
    over_time.pass_constants = frame.pass;
    over_time.reads[0] = raw;
    over_time.reads[1] = rt_before(average_pair);
    over_time.reads[2] = guide;
    over_time.reads[3] = rt_before(second ? RtImage::BlendGuideA : RtImage::GuideA);
    over_time.reads[4] = rt_image(RtImage::Motion);
    over_time.reads[5] = tiles;
    over_time.reads[6] = rt_before(distance_pair);
    over_time.writes[0] = average;
    over_time.writes[1] = distance;
    rt_backend_.dispatch(RtShader::SunTime, over_time, frame.across, frame.down);
    const std::uint32_t blurs = second ? 1u : 2u;
    for (std::uint32_t pass = 0u; pass < blurs; ++pass) {
        const bool last = pass + 1u == blurs;
        rt::PassBinds blur;
        blur.constants = frame.binds.constants;
        blur.pass_constants = rt_layer_constants(pass, last ? 1u : 0u);
        blur.reads[0] = pass == 0u ? average : rt_image(RtImage::SunBlur);
        blur.reads[1] = distance;
        blur.reads[2] = guide;
        blur.reads[3] = rt_image(RtImage::Normal);
        blur.reads[5] = tiles;
        blur.reads[rt::kPassReads - 1u] = rt_image(RtImage::BlueNoise);
        blur.writes[0] = rt_image(last ? RtImage::SunOut : RtImage::SunBlur);
        rt_backend_.dispatch(RtShader::SunBlur, blur, frame.across, frame.down);
    }
}

// The guide the layer's passes write and read: this frame's of its own pair.
std::uint32_t RenderCore::rt_layer_guide() const noexcept {
    return rt_now(rt_light_frame_.layer == 0u ? RtImage::GuideA : RtImage::BlendGuideA);
}

// A filter step's constants for the layer being traced (the second layer's say whether it has a frame before).
rt::BufferView RenderCore::rt_layer_constants(std::uint32_t step, std::uint32_t last) {
    const RtFrame& frame = rt_light_frame_;
    return rt_pass_constants(frame.layer, step, last, frame.layer != 0u && frame.history && rt_blend_walk_before_ + 1u == walk_.serial ? 1u : 0u);
}

// The second layer's local lights filtered: over time with its own averages and moments, then one step over space
// into the added light at its pixels (the rest keep the opaque layer's).
void RenderCore::rt_blend_filters() {
    const RtFrame& frame = rt_light_frame_;
    const std::uint32_t guide = rt_now(RtImage::BlendGuideA), average = rt_now(RtImage::BlendLightsA);
    const std::uint32_t moments = rt_now(RtImage::BlendMomentsA);
    rt::PassBinds over_time;
    over_time.constants = frame.binds.constants;
    over_time.pass_constants = frame.pass;
    over_time.reads[0] = rt_image(RtImage::Lights);
    over_time.reads[1] = rt_before(RtImage::BlendLightsA);
    over_time.reads[2] = guide;
    over_time.reads[3] = rt_before(RtImage::BlendGuideA);
    over_time.reads[4] = rt_image(RtImage::Motion);
    over_time.reads[5] = rt_before(RtImage::BlendMomentsA);
    over_time.reads[6] = rt_now(RtImage::BlendReservoirsA);
    over_time.writes[0] = average;
    over_time.writes[1] = moments;
    rt_backend_.dispatch(RtShader::LightsTime, over_time, frame.across, frame.down);
    rt::PassBinds over_space;
    over_space.constants = frame.binds.constants;
    over_space.pass_constants = rt_layer_constants(1u, 1u);
    over_space.reads[0] = average;
    over_space.reads[1] = moments;
    over_space.reads[2] = guide;
    over_space.reads[3] = rt_image(RtImage::BlendBody);  // glass's and water's body into the added light's a
    over_space.writes[0] = rt_image(RtImage::Added);
    rt_backend_.dispatch(RtShader::LightsSpace, over_space, frame.across, frame.down);
}

// The surfaces: what every later pass needs of a pixel's surface, once (rt_surface.hlsl).
void RenderCore::rt_pass_surface() {
    rt::PassBinds binds = rt_pass_binds();
    binds.writes[0] = rt_layer_guide();
    binds.writes[1] = rt_image(RtImage::Normal);
    binds.writes[2] = rt_image(RtImage::Motion);
    binds.writes[3] = rt_image(RtImage::Material);
    binds.writes[4] = rt_image(RtImage::Base);
    rt_backend_.dispatch(RtShader::Surface, binds, rt_light_frame_.across, rt_light_frame_.down);
}

// The sun: how much of it reaches each surface (rt_sun.hlsl).
void RenderCore::rt_pass_sun() {
    rt::PassBinds binds = rt_pass_binds();
    binds.reads[0] = rt_layer_guide();
    binds.reads[1] = rt_image(RtImage::Normal);
    binds.reads[2] = rt_image(RtImage::Motion);
    binds.reads[3] = rt_image(RtImage::Material);
    binds.writes[0] = rt_image(RtImage::SunRaw);
    rt_backend_.dispatch(RtShader::Sun, binds, rt_light_frame_.across, rt_light_frame_.down);
}

// The local lights: each emitter by reservoir resampling (rt_lights.hlsl). The second layer's sum goes straight into
// the added light the programs read.
void RenderCore::rt_pass_lights() {
    const bool second = rt_light_frame_.layer != 0u;
    rt::PassBinds binds = rt_pass_binds();
    binds.reads[0] = rt_layer_guide();
    binds.reads[1] = rt_image(RtImage::Normal);
    binds.reads[2] = rt_image(RtImage::Motion);
    binds.reads[3] = rt_before(second ? RtImage::BlendReservoirsA : RtImage::ReservoirsA);
    binds.reads[4] = rt_before(second ? RtImage::BlendGuideA : RtImage::GuideA);
    binds.reads[5] = rt_image(RtImage::Material);
    binds.writes[0] = rt_image(RtImage::Lights);
    binds.writes[1] = rt_now(second ? RtImage::BlendReservoirsA : RtImage::ReservoirsA);
    binds.writes[2] = rt_image(RtImage::LightsSpec);
    rt_backend_.dispatch(RtShader::Lights, binds, rt_light_frame_.across, rt_light_frame_.down);
}

// The shares the programs read, with the sky's light near the surface and what the surface mirrors (rt_light.hlsl).
void RenderCore::rt_pass_light() {
    // The shading frame too (t4): what glass, water and the other mirrors here show is the shared hit shading (FB b75);
    // g_pass.y 1 where it is bound, g_pass.w its first byte.
    rt::PassBinds binds;
    std::uint32_t shade_at = 0u;
    const bool shaded = rt_shade_binds(binds, shade_at);
    if (!shaded) {
        binds = rt_pass_binds();
    }
    const float none[3][4] = {};
    binds.pass_constants = rt_bounce_constants(shaded ? 1u : 0u, 0u, shade_at, none);
    binds.reads[0] = rt_layer_guide();
    binds.reads[1] = rt_image(RtImage::Normal);
    binds.reads[2] = rt_image(RtImage::Motion);
    binds.reads[3] = rt_image(RtImage::SunOut);
    binds.reads[4] = rt_image(RtImage::Material);
    binds.reads[5] = rt_image(RtImage::LightsSpec);
    binds.writes[0] = rt_image(RtImage::Light);
    binds.writes[1] = rt_image(RtImage::Mirror);
    binds.writes[2] = rt_image(RtImage::Facing);
    binds.writes[3] = rt_image(RtImage::BlendBody);         // the second layer's glass and water (kNoImage before one)
    binds.reads[6] = rt_now(RtImage::GuideA);               // the opaque layer's, for the water's depth
    rt_backend_.dispatch(RtShader::Light, binds, rt_light_frame_.across, rt_light_frame_.down);
}

// The filters: each share averaged with what the same surface point had in the frames before (where the camera's move
// finds the same surface), then three steps over space (taps 1, 2 and 4 pixels apart), the last into the picture the
// programs read. History counts from the walk just before this one, with no cut between.
void RenderCore::rt_filters() {
    const RtFrame& frame = rt_light_frame_;
    const std::uint32_t guide = rt_now(RtImage::GuideA), guide_before = rt_before(RtImage::GuideA);
    const std::uint32_t motion = rt_image(RtImage::Motion);
    rt::BufferView steps[3];
    for (std::uint32_t step = 0u; step < 3u; ++step) {
        steps[step] = rt_pass_constants(0u, 1u << step, step == 2u ? 1u : 0u);
    }
    // The sun's and the sky's shares as the programs read them, in one step (rt_light_space.hlsl's last; A5-P1): the
    // sun's V as its own filter left it (SunOut) and the sky and bounce light's mean ratio, else 1. The light pass's own
    // sky share is the constant 1, so nothing of the Light picture needs a filter over time or space.
    const std::uint32_t light = rt_image(RtImage::Light);
    rt::PassBinds compose;
    compose.constants = frame.binds.constants;
    compose.pass_constants = steps[2];
    compose.reads[2] = guide;
    compose.reads[3] = motion;  // for --diagnostics rt-motion
    compose.reads[4] = rt_pass_indirect_on() ? rt_image(RtImage::Indirect) : rt::kNoImage;  // the sky's share
    compose.reads[5] = rt_image(RtImage::SunOut);  // V (rgb)
    compose.buffers[2] = frame.binds.buffers[2];   // the drops and their tiles: the landing cue (l80)
    compose.buffers[3] = frame.binds.buffers[3];
    compose.writes[0] = light;
    compose.counters = true;  // 19: the pixels under the landing cue (l80b)
    rt_backend_.dispatch(RtShader::LightSpace, compose, frame.across, frame.down);
    rt_backend_.end_counters();
    // The local lights' share: over time (its averages and the moments of its noise), then three steps over space
    // weighed by that noise, the last into the added light the programs read.
    const std::uint32_t lights = rt_image(RtImage::Lights), added = rt_image(RtImage::Added);
    const std::uint32_t lights_average = rt_now(RtImage::LightsAverageA), moments = rt_now(RtImage::LightsMomentsA);
    rt::PassBinds lights_time;
    lights_time.constants = frame.binds.constants;
    lights_time.pass_constants = frame.pass;
    lights_time.reads[0] = lights;
    lights_time.reads[1] = rt_before(RtImage::LightsAverageA);
    lights_time.reads[2] = guide;
    lights_time.reads[3] = guide_before;
    lights_time.reads[4] = motion;
    lights_time.reads[5] = rt_before(RtImage::LightsMomentsA);
    lights_time.reads[6] = rt_now(RtImage::ReservoirsA);  // the boiling filter's weights
    lights_time.writes[0] = lights_average;
    lights_time.writes[1] = moments;
    rt_backend_.dispatch(RtShader::LightsTime, lights_time, frame.across, frame.down);
    for (std::uint32_t step = 0u; step < 3u; ++step) {
        rt::PassBinds over_space;
        over_space.constants = frame.binds.constants;
        over_space.pass_constants = steps[step];
        over_space.reads[0] = step == 0u ? lights_average : step == 1u ? added : lights;
        over_space.reads[1] = moments;
        over_space.reads[2] = guide;
        over_space.writes[0] = step == 1u ? lights : added;
        if (step == 2u) {
            // The added light's guard, counted (l82, A2-05): the pixel's base colour, counter 51.
            over_space.reads[4] = rt_image(RtImage::Base);
            over_space.counters = true;
        }
        rt_backend_.dispatch(RtShader::LightsSpace, over_space, frame.across, frame.down);
    }
    rt_backend_.end_counters();
}

// The frame's tables and constants: each item's words and each light set's lamp, the sun, the stage's ambient and key,
// the emitters by tile, the mesh memory, the frame before's camera; uploaded once and bound by every pass of the frame
// and of its second layer. `world_from_now`: the scene camera's inverse (with history).
void RenderCore::rt_tables(const Affine34& world_from_now) {
    const core::ListData& now = *walk_.now;
    const NebulaFrameList& list = *now.list;
    RtFrame& frame = rt_light_frame_;
    const unsigned width = frame.width, height = frame.height;
    // Each light set's lamp (the game's point light, the set's light 4), as the walk's rows hold it: its place in the
    // scene camera's eye space and its radius.
    constexpr float kLampRadius = 60.0f;
    rt_set_words_.assign(std::max<std::size_t>(std::size_t{list.light_set_count} * 4u, 4u), 0.0f);
    for (std::uint32_t s = 0; s < list.light_set_count; ++s) {
        const NebulaFrameLightSet& set = list.light_sets[s];
        if (now.light_camera[s] != rt_scene_camera_ || list.light_set_owners[s].point_light == 0u || set.count <= 4u) {
            continue;
        }
        const float (*rows)[4] = walk_rows_cpu_ + walk_light_rows_[s] + core::kLightSetHeaderRows + std::size_t{core::kRowsPerLight} * 4u;
        float* out = rt_set_words_.data() + std::size_t{s} * 4u;
        out[0] = rows[2][2];
        out[1] = rows[2][3];
        out[2] = rows[3][0];
        out[3] = std::isfinite(out[0] + out[1] + out[2]) ? kLampRadius : 0.0f;
    }
    // Each item: its slot, and for a surface of the prepass (bit 2) what it takes. Lit: its program sums the key light
    // at the pixel (the sun's share shadows it there). Ground: it takes drop shadows; what the game draws in the phase it keeps
    // free of them (ScenePhase_OpaBeforeSilhouette, 1) does not.
    constexpr std::uint32_t kPhaseNoShadow = 1u;
    // Eight words an item: its slot; its flags and light set; then where a pixel of it finds its triangle
    // (a mesh one matrix places, whose streams lie in an arena bound to the pass): the arena's number among
    // those bound (else none), its attributes', indices' and positions' first bytes there, the bytes of an
    // index, and the first row of its matrix among the frame's rows.
    // Then, for an alpha-tested material, its first map's place among the textures (else none) and the alpha from
    // which it is there; the mesh's matrix stream from 1 (0: the one row). Then what a ray that meets it shows
    // (nebula_model.h NebulaModelHit): its class, its flags, its roughness and metal as bytes; its colour under
    // full light and the share of its look a painted reflection carries, as bytes; then the first row its matrix had
    // the frame before; two words spare.
    constexpr std::size_t kItemWords = kRtItemWords;
    rt_item_words_.assign(std::size_t{list.item_count} * kItemWords, 0u);
    rt_item_coords_.clear();
    for (std::uint32_t i = 0; i < list.item_count; ++i) {
        rt_item_words_[i * kItemWords] = list.items[i].slot;
        rt_item_words_[i * kItemWords + 2u] = NEBULA_NONE32;
        rt_item_words_[i * kItemWords + 8u] = NEBULA_NONE32;
    }
    struct BoundArena {
        std::uint32_t arena = NEBULA_NONE32;
        gpu::Address base = 0u;
        std::size_t bytes = 0u;
    };
    BoundArena arenas[rt::kPassMeshArenas];
    unsigned arena_count = 0u;
    rt_light_unplaced_ = 0u;
    rt_light_skinned_ = 0u;
    const auto arena_of = [&](std::uint32_t arena) -> unsigned {
        for (unsigned a = 0; a < arena_count; ++a) {
            if (arenas[a].arena == arena) {
                return a;
            }
        }
        if (arena_count == rt::kPassMeshArenas || !registries_.arena_memory(arena, arenas[arena_count].base, arenas[arena_count].bytes)) {
            return rt::kPassMeshArenas;
        }
        arenas[arena_count].arena = arena;
        return arena_count++;
    };
    // A report's count of the scene's items by what the game drew them as: in the prepass (the traced light
    // reaches them), or not and why (blended, no depth written, another state).
    std::uint32_t cover[256][4]{};
    const bool covering = ::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs) && rt_light_frames_ == 0u;
    // And, of those left out, how many by category and pipeline state (the state word says why).
    std::map<std::uint64_t, std::uint32_t> left_out;
    for (std::uint32_t p = rt_scene_pass_ + 1u; p < list.pass_count && list.passes[p].kind != NEBULA_PASS_SCENE_END; ++p) {
        const NebulaFramePass& pass = list.passes[p];
        if ((pass.kind != NEBULA_PASS_DRAW && pass.kind != NEBULA_PASS_TRACED_DRAW) || pass.layer != NEBULA_LAYER_SCENE) {
            continue;
        }
        for (std::uint32_t i = pass.first_item; i < pass.first_item + pass.item_count; ++i) {
            const NebulaFrameItem& item = list.items[i];
            if (covering) {
                const std::uint32_t state = registries_.pipeline(item.pipeline).desc.state;
                const unsigned why = rt_prepassed_[i] != 0u                  ? 0u
                                     : (state & 3u) != NEBULA_BLEND_REPLACE ? 1u
                                     : (state & (1u << 14u)) == 0u          ? 2u
                                                                            : 3u;
                ++cover[item_category(item) & 255u][why];
                if (why != 0u) {
                    ++left_out[std::uint64_t{item_category(item) & 255u} << 40u | std::uint64_t{item.flags & 0xFFFu} << 28u |
                               (state & 0xFFFFFFu)];
                }
            }
            // What a surface takes (one of the prepass, or of the second layer later). The sky is in the
            // prepass (its depth is the scene's) and takes no light of the scene's.
            if (item_is_sky(item)) {
                rt_item_words_[i * kItemWords + 1u] = 16u;  // the sky: shown as it is
            } else {
                const bool lamp = item.light_set != NEBULA_NONE32 && item.light_set < list.light_set_count &&
                                  rt_set_words_[std::size_t{item.light_set} * 4u + 3u] != 0.0f;
                const core::PipelineEntry& pipeline = registries_.pipeline(item.pipeline);
                const bool thin = (pipeline.desc.state >> 15u & 3u) == NEBULA_CULL_NONE;  // drawn from both sides: a sheet
                // 32: a glowing surface too large to be one light (kItemEmissiveArea, rt_emitter, L-3).
                const bool area = i < rt_emissive_areas_.size() && rt_emissive_areas_[i] != 0u;
                // 64: a map part its clip areas show (kItemClipped, cutout_there asks the volumes).
                const bool clipped =
                    list.clip_rule != NEBULA_CLIP_RULE_NONE && item_category(item) == NEBULA_CATEGORY_CLIPPED_MAP_PARTS;
                // 128: its program alpha-tests (kItemAlphaTested: a clipped part without it is whole where it shows).
                const bool alpha_tested = program_variant(pipeline.desc.pixel, 0u, 1u) == kProgramSample;
                rt_item_words_[i * kItemWords + 1u] = (program_sums_key(pipeline.desc.pixel) ? 1u : 0u) |
                                                      (pass.phase != kPhaseNoShadow ? 2u : 0u) | 4u | (thin ? 8u : 0u) |
                                                      (area ? 32u : 0u) | (clipped ? 64u : 0u) | (alpha_tested ? 128u : 0u) |
                                                      (lamp ? (item.light_set + 1u) << 8u : 0u);
            }
            // Its mesh, for a pixel of it and for a ray that meets it.
            if ((item.flags & (NEBULA_ITEM_RING | NEBULA_ITEM_INSTANCES)) != 0u) {
                if (i == rt_water_ring_item_) {
                    // The code-drawn water's ring: water to a ray (class, and water's roughness 0.03).
                    rt_item_words_[i * kItemWords + 11u] = std::uint32_t{NEBULA_HIT_WATER} | 8u << 16u;
                }
                continue;
            }
            const core::GpuMesh& mesh = registries_.mesh(item.mesh);
            const bool one_matrix = (item.flags & NEBULA_ITEM_ONE_MATRIX) != 0u;
            if (mesh.site_stride != 0u || mesh.attributes == 0u || mesh.indices == 0u) {
                continue;  // a site's stream: the depth's own normal serves
            }
            // A mesh whose vertices name their own matrices (a character's skin, a model of many joints): the
            // pass takes each corner through its own.
            const bool own_matrices = mesh.matrices != 0u && !one_matrix;
            rt_light_skinned_ += own_matrices ? 1u : 0u;
            const unsigned arena = arena_of(mesh.arena);
            if (arena == rt::kPassMeshArenas) {
                ++rt_light_unplaced_;
                continue;
            }
            const bool refined = curved_refine_ && mesh.refined_index_count != 0u && (item.flags & NEBULA_ITEM_PLAIN_MESH) == 0u;
            const gpu::Address indices = refined ? mesh.refined_indices : mesh.indices;
            const gpu::Address base = arenas[arena].base;
            if (mesh.attributes < base || indices < base || mesh.positions < base || mesh.attributes - base >= arenas[arena].bytes ||
                indices - base >= arenas[arena].bytes || mesh.positions - base >= arenas[arena].bytes ||
                (own_matrices && (mesh.matrices < base || mesh.matrices - base >= arenas[arena].bytes))) {
                continue;
            }
            const std::uint32_t matrix = one_matrix || mesh.single_matrix != NEBULA_MODEL_NONE ? item.matrix : 0u;
            std::uint32_t* words = rt_item_words_.data() + i * kItemWords;
            words[2] = arena;
            words[3] = static_cast<std::uint32_t>(mesh.attributes - base);
            // D2: where its vertices' de-light gains are (c37; FB b54): word 19, vertex 0's gain word's arena byte / 4 and
            // the stride / 4 in the top 4 bits; 0 none (a refined draw's added vertices are in stream 3 too: b54b).
            if (mesh.gains >= base && mesh.gains - base < arenas[arena].bytes && mesh.further_stride % 4u == 0u &&
                mesh.further_stride / 4u <= 15u && (mesh.gains - base) / 4u < (gpu::Address{1u} << 28u)) {
                words[19] = static_cast<std::uint32_t>((mesh.gains - base) / 4u) | (mesh.further_stride / 4u) << 28u;
            }
            words[4] = static_cast<std::uint32_t>(indices - base);
            words[5] = mesh.index_bytes;
            words[6] = static_cast<std::uint32_t>(mesh.positions - base);
            words[7] = walk_.rows_base + walk_block_rows_[item.block] + matrix * kRowsPerMatrix;
            // Its matrix stream's first byte, from 1 (0: one matrix places it, the row above).
            words[10] = own_matrices ? static_cast<std::uint32_t>(mesh.matrices - base) + 1u : 0u;
            // The first row its matrix had the frame before (draw_walker.cpp walk_previous_: the same rows where
            // there is no frame before): the pass follows each pixel's point by it.
            words[13] = item.block < walk_previous_.size() ? walk_previous_[item.block] + matrix * kRowsPerMatrix : words[7];
            // Its first map: what an alpha test reads (a cutout's instance asks at each hit) and what a ray that
            // meets it shows.
            if (const core::TextureTable& table = registries_.table(item.textures);
                table.count != 0u && !table.list.empty() && table.list[0].texture != 0u && !TextureStore::is_target(table.list[0].texture)) {
                words[8] = renderer_.texture_slot(texture_store_.handle(table.list[0].texture));
                std::memcpy(&words[9], &mesh.hit_alpha, sizeof(float));
            }
            const auto byte_of = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            // Its maps include a picture the renderer made (a capture of the scene, as the frozen lake's mirror):
            // hit flag 128 (the cooker's flags use bits 0-6), and the reflection that takes its painted map's place
            // keeps none of that map (rt_reflect_out.hlsl, FB).
            std::uint32_t captured = 0u;
            if (const core::TextureTable& maps = registries_.table(item.textures); maps.count != 0u) {
                const std::size_t listed = std::min<std::size_t>(maps.count, maps.list.size());
                for (std::size_t k = 0; k < listed && captured == 0u; ++k) {
                    captured = TextureStore::is_target(maps.list[k].texture) ? 128u : 0u;
                }
            }
            words[11] = std::uint32_t{mesh.hit_class} | ((std::uint32_t{mesh.hit_flags} & 127u) | captured) << 8u |
                        byte_of(mesh.hit_roughness) << 16u | byte_of(mesh.hit_metal) << 24u;
            words[12] = byte_of(mesh.hit_base[0]) | byte_of(mesh.hit_base[1]) << 8u | byte_of(mesh.hit_base[2]) << 16u |
                        byte_of(mesh.hit_environment) << 24u;
            // Water's first map's coordinate rows (plan 13.3, l54: its surface ripples as the map moves): the two rows
            // the program's coordinate takes, the walk's own at t for a listed material, after the items; word 18
            // their first byte from the items' first. Only a map read from uv set 0 (the tracer's vertices carry no
            // other), and only rows of this form's layout (FC's c30, as live_glow checks).
            if (mesh.hit_class == NEBULA_HIT_WATER && mesh.map0_row != 0xFFFFu && mesh.map0_uv == 0u && words[8] != NEBULA_NONE32 &&
                list.constants != nullptr && item.constants < list.constants_count) {
                const NebulaFrameConstants& constants = list.constants[item.constants];
                const float* rows = constants.rows != nullptr          ? walk_material_rows(item.constants)
                                    : constants.slot != NEBULA_NONE32 ? registries_.slot(constants.slot).cpu
                                                                      : nullptr;
                if (rows != nullptr && constants.vertex_rows + constants.pixel_rows == mesh.map0_form_rows &&
                    std::uint32_t{mesh.map0_row} + 2u <= constants.vertex_rows) {
                    words[18] = static_cast<std::uint32_t>((rt_item_words_.size() + rt_item_coords_.size()) * 4u);
                    const std::size_t at = rt_item_coords_.size();
                    // Map 0's two rows, map 1's two rows, map 1's texture slot (NONE: none), three words free (FB b90).
                    rt_item_coords_.resize(at + 20u, 0u);
                    std::memcpy(rt_item_coords_.data() + at, rows + std::size_t{mesh.map0_row} * 4u, 8u * sizeof(float));
                    rt_item_coords_[at + 16u] = NEBULA_NONE32;
                    const core::TextureTable& maps = registries_.table(item.textures);
                    if (mesh.map1_row != 0xFFFFu && mesh.map1_uv == 0u && std::uint32_t{mesh.map1_row} + 2u <= constants.vertex_rows &&
                        mesh.map1_slot < std::min<std::size_t>(maps.count, maps.list.size()) && maps.list[mesh.map1_slot].texture != 0u &&
                        !TextureStore::is_target(maps.list[mesh.map1_slot].texture)) {
                        std::memcpy(rt_item_coords_.data() + at + 8u, rows + std::size_t{mesh.map1_row} * 4u, 8u * sizeof(float));
                        rt_item_coords_[at + 16u] = renderer_.texture_slot(texture_store_.handle(maps.list[mesh.map1_slot].texture));
                    }
                }
            }
            // What it shows with no light, and whether its light is in the list (rt_shade.hlsli rt_hit_read). Listed
            // only where rt_emitter kept its block's light (not a glowing surface too large to be one light, not one
            // past the list's room; a point light the game asked at its place stands for it too); its glow turned
            // off by the game, it shows none and is not listed (c60, A2-12).
            float glow_now[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            if (mesh.glow[3] != 0.0f) {
                live_glow(list, item, mesh, glow_now);
            }
            const bool glow_off = mesh.glow[3] != 0.0f && glow_now[3] == 0.0f;
            const bool glow_listed = glow_now[3] != 0.0f && rt_emitter_blocks_.contains(item.block);
            // c81 (L4): bit 25, and the raster's draw flag 256: a light this frame, never duller than its colour.
            if (glow_now[3] != 0.0f && i < rt_glow_bodies_.size()) {
                rt_glow_bodies_[i] = 1u;
            }
            words[14] = glow_off ? 0u
                                 : byte_of(mesh.hit_emission[0]) | byte_of(mesh.hit_emission[1]) << 8u |
                                       byte_of(mesh.hit_emission[2]) << 16u | (glow_listed ? 1u << 24u : 0u) |
                                       (glow_now[3] != 0.0f ? 1u << 25u : 0u);
            // Its paint as one light (the Physical style, FB 22.19): the share in bits 0-7, the direction in the
            // model's space as two signed 12-bit octahedral numbers (8-19, 20-31). 0: it stays painted.
            if (mesh.paint_share > 0.0f) {
                const auto twelve = [](std::int16_t v) {
                    return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(v * (2047.0 / 32767.0)))) & 0xFFFu;
                };
                words[16] = std::max(byte_of(mesh.paint_share), 1u) | twelve(mesh.paint_direction[0]) << 8u |
                            twelve(mesh.paint_direction[1]) << 20u;
            }
            // Its mirror and see-through numbers (FA and FB read them): bits 0-11 its painted environment map's own
            // colour, 4 bits a channel, the largest 15 (all 0: no painted map): what tints the real reflection that
            // takes the map's place (12.4); bits 12-19 its index of refraction, (ior - 1) * 100; bits 20-25 its
            // shine * 4 (63: the class's own); bits 26-31 its body * 62 (63: the class's own) (13.2, 14.4).
            const auto bits_of = [](float v, float scale, std::uint32_t top) {
                return std::min(top, static_cast<std::uint32_t>(std::max(v, 0.0f) * scale + 0.5f));
            };
            // The mirror's tint by the material's tint keep (A4-W6): lerp(white, the map's colour, keep), the class's
            // own keep (below 0) all of it; a clear class's 0 mirrors untinted. No painted map: 0 as before.
            const bool painted_map = mesh.hit_env_tint[0] > 0.0f || mesh.hit_env_tint[1] > 0.0f || mesh.hit_env_tint[2] > 0.0f;
            const float keep = mesh.hit_tint_keep < 0.0f ? 1.0f : std::min(mesh.hit_tint_keep, 1.0f);
            const auto tinted = [&](unsigned c) { return painted_map ? 1.0f + (mesh.hit_env_tint[c] - 1.0f) * keep : 0.0f; };
            words[15] = bits_of(tinted(0), 15.0f, 15u) | bits_of(tinted(1), 15.0f, 15u) << 4u |
                        bits_of(tinted(2), 15.0f, 15u) << 8u | bits_of(mesh.hit_ior - 1.0f, 100.0f, 255u) << 12u |
                        (mesh.hit_shine < 0.0f ? 63u : bits_of(mesh.hit_shine, 4.0f, 62u)) << 20u |
                        (mesh.hit_body < 0.0f ? 63u : bits_of(mesh.hit_body, 62.0f, 62u)) << 26u;
            // Its thickness in units (13.2: a body bends what is seen through it, a shell does not; rt_transmit.hlsl).
            std::memcpy(&words[17], &mesh.hit_thickness, sizeof(float));
        }
    }
    if (covering) {
        unsigned told = 0u;
        for (const auto& [key, count] : left_out) {
            if (told++ == 48u) {
                break;
            }
            std::fprintf(stderr, "[rt-inputs]   left out: category %u flags %03x state %06x: %u\n", static_cast<unsigned>(key >> 40u),
                         static_cast<unsigned>(key >> 28u) & 0xFFFu, static_cast<unsigned>(key & 0xFFFFFFu), count);
        }
        for (unsigned c = 0; c < 256u; ++c) {
            if ((cover[c][0] | cover[c][1] | cover[c][2] | cover[c][3]) != 0u) {
                std::fprintf(stderr, "[rt-inputs]   cover: category %u: prepassed %u blended %u no-depth %u other %u\n", c, cover[c][0],
                             cover[c][1], cover[c][2], cover[c][3]);
            }
        }
    }
    // The sun, the stage's ambient and key as brightness, and the drops by tile.
    // 0.75 degrees (the real sun's is 0.27; l77's 1.5 halved: the target's edge is 5 px where 1.5 gave 10; FB b99,
    // EVERYTHING 1.3): a shadow's edge 2 tan d wide, d its caster's distance, crisp at contact and soft far off; a
    // stage row's sun_size over it.
    constexpr float kSunTan = 0.0131f;
    constexpr float kDropTan = 0.035f;   // the drop light's 2 degrees (decision D1): the landing cue's edge (l80)
    constexpr float kSkyInShadow = 0.5f;  // what the sun's shadow keeps of the sky's light: the sky turned down (the
                                          // view's tint gives back up to 0.3 of the rest in the sky's colour)
    constexpr float kFullFacing = 0.6f;  // a painted surface keeps its authored brightness from this n.l up
    constexpr float kPaintedStyle = 1.0f;  // a painted surface whose model's paint holds its shadows
                                           // (NEBULA_HIT_PAINTED_SHADOW) is shadowed by lit things only (14.6)
    const float* const sun = rt_sun_eye_;
    const bool sunned = rt_sun_now_;
    float ambient = 0.0f, key = 0.0f;
    if (list.stage_light_known != 0u) {
        if (const NebulaFrameWorldLight* light = core::find_world_light(list, list.stage_light); light != nullptr) {
            ambient = 0.2126f * list.stage_ambient[0] + 0.7152f * list.stage_ambient[1] + 0.0722f * list.stage_ambient[2];
            key = 0.2126f * light->color[0] + 0.7152f * light->color[1] + 0.0722f * light->color[2];
        }
    }
    const bool stage_named = ambient > 0.02f && key > 0.02f;
    if (!(ambient > 0.02f) || !(key > 0.02f)) {
        ambient = key = 0.5f;  // a stage that names no light of its own: an even split
    }
    // The stage's own numbers where its row gives them (rt_stages.tsv, RT-MASTER-PLAN 23.2).
    const RtStage& stage_row = rt_stage(list.stage_hash) != nullptr ? *rt_stage(list.stage_hash) : kRtStageDefaults;
    // A run's stage gains over the row's (rt_dev.h ambient_gain, sun_gain: the look's dials per run, D4).
    const float ambient_gain = rt_dev(RtDev::AmbientGain, stage_row.ambient_gain);
    const float sun_gain = rt_dev(RtDev::SunGain, stage_row.sun_gain);
    key *= sun_gain;
    rt_stage_air_ = stage_row.air > 0.0f ? stage_row.air * std::max(rt_dev(RtDev::AirScale, 1.0f), 0.0f) : 0.0f;  // the light in the air's density (FB, 15.4)
    ambient *= ambient_gain / kRtStageDefaults.ambient_gain;
    // The stage's ambient and key colours for the programs' painted composite (RT-MASTER-PLAN 3.4): a shadow takes
    // the sky's colour, a channel at a time, by kShadowTint. By the row's gains; an even grey where the stage names
    // no light of its own.
    {
        constexpr float kShadowTint = 0.2f;
        const NebulaFrameWorldLight* const key_light = stage_named ? core::find_world_light(list, list.stage_light) : nullptr;
        for (unsigned c = 0; c < 3u; ++c) {
            const float a = key_light != nullptr ? list.stage_ambient[c] : 0.5f;
            const float k = key_light != nullptr ? key_light->color[c] : 0.5f;
            rt_stage_a_[c] = a * ambient_gain / kRtStageDefaults.ambient_gain;
            rt_stage_k_[c] = k * sun_gain;
        }
        rt_stage_a_[3] = rt_dev(RtDev::ShadowTint, kShadowTint);
        rt_stage_k_[3] = 0.0f;
    }
    if (!rt_drops_.empty()) {
        rt_drop_bins(width, height, frame.ray);
    } else {
        rt_tiles_across_ = rt_tiles_down_ = 0u;
        rt_tiles_.assign(2u, 0u);
        rt_tile_entries_.assign(1u, 0u);
    }
    RtFrameConstants constants{};
    UploadRing& ring = renderer_.matrix_ring();
    const std::size_t item_bytes = (rt_item_words_.size() + rt_item_coords_.size()) * sizeof(std::uint32_t);  // l54: + water's rows
    const std::size_t set_bytes = rt_set_words_.size() * sizeof(float);
    static_assert(sizeof(RtDrop) == 48u && sizeof(RtEmitter) == 48u);
    // The stage's exposure: the light a fully lit surface of it stands in, from its own key and ambient: 40 lux
    // in the dark of space (a glow carries far), 150 on a bright day (a glow is a touch of colour near it).
    rt_emitters_finish(stage_row.exposure_low +
                       (stage_row.exposure_high - stage_row.exposure_low) * std::clamp((ambient + key) / 1.2f, 0.0f, 1.0f));
    const bool glowing = !rt_emitters_.empty();
    if (glowing) {
        rt_emitter_bins(width, height, frame.ray);
    } else {
        rt_glow_tiles_.clear();
        rt_glow_entries_.clear();
    }
    const std::size_t drops_only = std::max<std::size_t>(rt_drops_.size() * sizeof(RtDrop), sizeof(RtDrop));
    const std::size_t remap_at = drops_only + rt_emitters_.size() * sizeof(RtEmitter);
    const std::size_t was_at = remap_at + std::max<std::size_t>(rt_emitter_ids_before_.size(), 1u) * sizeof(std::uint32_t);
    const std::size_t drop_bytes = was_at + std::max<std::size_t>(rt_emitters_.size(), 1u) * 4u * sizeof(float);
    const std::size_t drop_tile_words = rt_tiles_.size() + rt_tile_entries_.size();
    const std::size_t tile_words = drop_tile_words + rt_glow_tiles_.size() + rt_glow_entries_.size();
    // The clip areas' volumes (plan 26 M10), after the items: each the rows from the eye to its unit shape, then the
    // shape (16 words).
    constexpr std::uint32_t kClipVolumes = 128u;
    std::uint32_t clip_words[kClipVolumes * 16u];
    std::uint32_t clip_count = 0u;
    std::uint32_t clip_dropped = 0u;  // past the table, or a place that cannot be turned back (the report counts them)
    for (std::uint32_t v = 0; list.clip_rule != NEBULA_CLIP_RULE_NONE && list.clip_volumes != nullptr && v < list.clip_volume_count;
         ++v) {
        if (clip_count == kClipVolumes) {
            clip_dropped += list.clip_volume_count - v;
            break;
        }
        const NebulaFrameClipVolume& volume = list.clip_volumes[v];
        Affine34 place{};
        std::memcpy(place.data(), volume.place, sizeof(volume.place));
        // From the eye's space (the rays') by the scene camera itself: `world_from_now` is filled with history only.
        Affine34 unit_from_eye{};
        if (!affine_inverse(affine_multiply(walk_cameras_[rt_scene_camera_], place), unit_from_eye)) {
            ++clip_dropped;
            continue;
        }
        std::memcpy(clip_words + clip_count * 16u, unit_from_eye.data(), 12u * sizeof(float));
        clip_words[clip_count * 16u + 12u] = volume.shape;
        clip_words[clip_count * 16u + 13u] = clip_words[clip_count * 16u + 14u] = clip_words[clip_count * 16u + 15u] = 0u;
        ++clip_count;
    }
    rt_clip_dropped_ = std::max(rt_clip_dropped_, clip_dropped);
    const std::size_t clip_bytes = std::size_t{clip_count} * 16u * sizeof(std::uint32_t);
    const UploadRing::Allocation items_at = ring.allocate(item_bytes + clip_bytes, 256u);
    const UploadRing::Allocation sets_at = ring.allocate(set_bytes, 256u);
    const UploadRing::Allocation drops_at = ring.allocate(drop_bytes, 256u);
    const UploadRing::Allocation tiles_at = ring.allocate(tile_words * sizeof(std::uint32_t), 256u);
    std::memcpy(items_at.cpu, rt_item_words_.data(), rt_item_words_.size() * sizeof(std::uint32_t));
    if (!rt_item_coords_.empty()) {
        std::memcpy(items_at.cpu + rt_item_words_.size() * sizeof(std::uint32_t), rt_item_coords_.data(),
                    rt_item_coords_.size() * sizeof(std::uint32_t));
    }
    if (clip_count != 0u) {
        std::memcpy(items_at.cpu + item_bytes, clip_words, clip_bytes);
    }
    constants.clip[0] = static_cast<std::uint32_t>(item_bytes);
    constants.clip[1] = clip_count;
    constants.clip[2] = list.clip_rule;
    std::memcpy(sets_at.cpu, rt_set_words_.data(), set_bytes);
    std::memset(drops_at.cpu, 0, drop_bytes);
    if (!rt_drops_.empty()) {
        std::memcpy(drops_at.cpu, rt_drops_.data(), rt_drops_.size() * sizeof(RtDrop));
    }
    std::memcpy(tiles_at.cpu, rt_tiles_.data(), rt_tiles_.size() * sizeof(std::uint32_t));
    std::memcpy(tiles_at.cpu + rt_tiles_.size() * sizeof(std::uint32_t), rt_tile_entries_.data(),
                rt_tile_entries_.size() * sizeof(std::uint32_t));
    if (glowing) {
        std::memcpy(drops_at.cpu + drops_only, rt_emitters_.data(), rt_emitters_.size() * sizeof(RtEmitter));
        std::memcpy(tiles_at.cpu + drop_tile_words * sizeof(std::uint32_t), rt_glow_tiles_.data(),
                    rt_glow_tiles_.size() * sizeof(std::uint32_t));
        std::memcpy(tiles_at.cpu + (drop_tile_words + rt_glow_tiles_.size()) * sizeof(std::uint32_t), rt_glow_entries_.data(),
                    rt_glow_entries_.size() * sizeof(std::uint32_t));
    }
    {
        // Where each emitter of the frame before is in this frame's list (by what it is), or nowhere: a pixel's
        // reservoir names its light by the list of the frame it was written in. A particle is found by the number
        // its place a frame ago gives (reserved[0], l84), the others by their own.
        rt_emitter_now_.clear();
        for (std::size_t e = 0; e < rt_emitters_.size(); ++e) {
            const RtEmitter& emitter = rt_emitters_[e];
            const std::uint32_t was = emitter.kind == 2u ? emitter.reserved[0] : emitter.reserved[1];
            if (was != kNoEmitterId) {
                rt_emitter_now_.try_emplace(was, static_cast<std::uint32_t>(e));
            }
        }
        rt_emitter_remap_.assign(rt_emitter_ids_before_.size(), ~0u);
        for (std::size_t e = 0; e < rt_emitter_ids_before_.size(); ++e) {
            if (const auto found = rt_emitter_now_.find(rt_emitter_ids_before_[e]); found != rt_emitter_now_.end()) {
                rt_emitter_remap_[e] = found->second;
            }
        }
        if (!rt_emitter_remap_.empty()) {
            std::memcpy(drops_at.cpu + remap_at, rt_emitter_remap_.data(), rt_emitter_remap_.size() * sizeof(std::uint32_t));
        }
        // Where each of this frame's emitters was in the frame before, and how far it reached (the buffer is zeroed:
        // reach 0 for a new one): the reservoirs' bias correction asks what the frame before's tries could reach.
        for (std::size_t e = rt_emitter_remap_.size(); e-- > 0;) {
            const std::uint32_t to_now = rt_emitter_remap_[e];
            if (to_now < rt_emitters_.size() && (e + 1u) * 4u <= rt_emitter_places_before_.size()) {
                std::memcpy(drops_at.cpu + was_at + to_now * 4u * sizeof(float), rt_emitter_places_before_.data() + e * 4u,
                            4u * sizeof(float));
            }
        }
        constants.kept[0] = static_cast<std::uint32_t>(remap_at);
        constants.kept[1] = static_cast<std::uint32_t>(rt_emitter_ids_before_.size());
        rt_emitter_ids_before_.resize(rt_emitters_.size());
        rt_emitter_places_before_.resize(rt_emitters_.size() * 4u);
        for (std::size_t e = 0; e < rt_emitters_.size(); ++e) {
            rt_emitter_ids_before_[e] = rt_emitters_[e].reserved[1];
            const RtEmitter& emitter = rt_emitters_[e];
            const float place[4] = {emitter.centre[0], emitter.centre[1], emitter.centre[2], emitter.reach};
            std::memcpy(rt_emitter_places_before_.data() + e * 4u, place, sizeof(place));
        }
    }
    std::memcpy(constants.ray, frame.ray, sizeof(constants.ray));
    std::memcpy(constants.depth, frame.depth, sizeof(constants.depth));
    std::memcpy(constants.viewport, frame.viewport, sizeof(constants.viewport));
    rt::PassBinds& binds = frame.binds;
    binds.buffers[0] = rt_backend_.view_of(items_at.gpu, item_bytes + clip_bytes, constants.at[0]);
    binds.buffers[1] = rt_backend_.view_of(sets_at.gpu, set_bytes, constants.at[1]);
    binds.buffers[2] = rt_backend_.view_of(drops_at.gpu, drop_bytes, constants.at[2]);
    binds.buffers[3] = rt_backend_.view_of(tiles_at.gpu, tile_words * sizeof(std::uint32_t), constants.at[3]);
    constants.more[0] = constants.at[3] + static_cast<std::uint32_t>(rt_tiles_.size() * sizeof(std::uint32_t));
    constants.grid[0] = rt_tiles_across_;
    constants.grid[1] = rt_tiles_down_;
    constants.grid[2] = 16u;
    constants.sun[0] = sun[0];
    constants.sun[1] = sun[1];
    constants.sun[2] = sun[2];
    constants.sun[3] = std::clamp(rt_dev(RtDev::SunSize, stage_row.sun_size > 0.0f ? stage_row.sun_size : kSunTan), 0.0f, 0.2f);
    constants.stage[0] = ambient;
    constants.stage[1] = key;
    // The game's time at the frame in seconds, wrapped at an hour (l58: the caustics' drift keeps the game's pace at
    // any display rate).
    constants.stage[2] = static_cast<float>(std::fmod((static_cast<double>(list.game_frame) - 1.0 + static_cast<double>(walk_.time.t)) / 60.0, 3600.0));
    constants.stage[3] = kDropTan;
    constants.look[0] = kSkyInShadow;
    constants.look[1] = kPaintedStyle;
    constants.look[2] = rt_dev(RtDev::FullFacing, kFullFacing);
    constants.look[3] = stage_row.water_clear;  // the water's clear way (rt_stages.tsv; R4: FB b58)
    // The mesh memory: the arenas the items named, and the frame's rows. A slot no arena fills reads the
    // first (a bound buffer the pass never indexes).
    for (unsigned a = 0; a < rt::kPassMeshArenas; ++a) {
        const BoundArena& arena = arenas[a < arena_count ? a : 0u];
        if (arena_count != 0u) {
            binds.mesh[a] = rt_backend_.view_of(arena.base, arena.bytes, constants.mesh_at[a]);
        }
    }
    binds.mesh[rt::kPassMeshArenas] =
        rt_backend_.view_of(walk_.rows_address, renderer_.matrix_ring().capacity(), constants.mesh_at[rt::kPassMeshArenas]);
    if (arena_count == 0u) {
        for (unsigned a = 0; a < rt::kPassMeshArenas; ++a) {
            binds.mesh[a] = binds.mesh[rt::kPassMeshArenas];
        }
    }
    rt_light_arenas_ = arena_count;
    // The emitters lie after the drops, their tiles and entries after the drops' (0 emitters: none read).
    constants.glow[0] = constants.at[2] + static_cast<std::uint32_t>(drops_only);
    constants.glow[1] = constants.at[3] + static_cast<std::uint32_t>(drop_tile_words * sizeof(std::uint32_t));
    constants.glow[2] = constants.glow[1] + static_cast<std::uint32_t>(rt_glow_tiles_.size() * sizeof(std::uint32_t));
    constants.glow[3] = static_cast<std::uint32_t>(rt_emitters_.size());
    constants.more[1] = frame.turn;
    // Bit 6: the point lights are emitters (rt_scene), so a lit program's own one point light is put out.
    // No bit 2: the landing blob under an actor is a shadow drawn by hand, traced outline or not (the
    // owner, 11-09: no fake shadows with ray tracing on). What stands on the ground darkens it by the sun's
    // shadow and by the sky it hides (bit 3), as it would.
    // The player's parts (plan 19.1): two bits each in more[3] (rt_part), bit 12 the reference preset. Sun off: no
    // traced sun; reflections off or low: no mirror rays (the painted maps stay, plan 19.1).
    const RenderConfig render = get_render_config();
    for (unsigned part = 0u; part < static_cast<unsigned>(RtPart::Count); ++part) {
        constants.more[3] |= rt_part(render, static_cast<RtPart>(part)) << (part * 2u);
    }
    constants.more[3] |= render.rt_quality == kRtPresetReference ? 4096u : 0u;
    rt_lamps_own_ = rt_part(render, RtPart::Lights) == 0u;
    rt_physical_ = render.rt_style == 1u;
    {
        // The painted composite's look (program.hlsli traced_painted; DIRECTOR.md D1, D4, section 12). kForm: 1 the
        // relight (October's engine v2: every painted mesh by the traced sun in the stage's linear units), 0 Painted
        // (the art's own facing). y: the sun's height over the player's up, at least 0.5 (M3's exposure: a sun below
        // the horizon does not leave the reference to the sky). kDebake: D2's per-vertex gain (the cooker's, c37), all
        // of it under the relight. kAmbientLook: the share of the stage's sky the relight keeps. Painted: (0, -, 0, 1).
        constexpr float kForm = 1.0f, kDebake = 1.0f, kAmbientLook = 1.0f;
        // No traced sun (a stage without one, the sun part off): the facing is 1 everywhere and the art keeps its own.
        const bool relit = rt_physical_ && sunned && rt_part(render, RtPart::Sun) != 0u;
        const float up_sun = list.sky_up_known != 0u ? sky_up_[0] * sun[0] + sky_up_[1] * sun[1] + sky_up_[2] * sun[2] : 1.0f;
        rt_look_[0] = relit ? kForm : 0.0f;
        // The sun's height eased (c53; the director 11-11): a gravity flip under the fixed sun (d05) does not pop the
        // exposure; set at once on the first frame, a stage change, a cut, a time reset. 0 steps between walks.
        const float up_target = std::max(up_sun, 0.5f);
        if (!rt_look_up_known_ || rt_look_up_stage_ != list.stage_hash || now.cut || walk_.time.reset) {
            rt_look_up_ = up_target;
            rt_look_up_known_ = true;
            rt_look_up_stage_ = list.stage_hash;
        } else {
            const std::uint32_t steps = walk_.between ? 0u : std::min(list.game_frame - rt_look_up_frame_, 4u);
            rt_look_up_ += (up_target - rt_look_up_) * (1.0f - std::pow(0.98f, static_cast<float>(steps)));
        }
        if (!walk_.between) {
            rt_look_up_frame_ = list.game_frame;
        }
        rt_look_[1] = rt_look_up_;
        // c87 (PLANS-UNBUILT "Sunless places"): with no traced sun the art's painted light is the room's light, taken
        // out only as far as the lamps supply it (rt_emitters_finish), eased over about a second.
        if (!rt_supplied_known_ || rt_supplied_stage_ != list.stage_hash || now.cut || walk_.time.reset) {
            rt_supplied_ = rt_supplied_target_;
            rt_supplied_known_ = true;
            rt_supplied_stage_ = list.stage_hash;
        } else {
            const std::uint32_t steps = walk_.between ? 0u : std::min(list.game_frame - rt_supplied_frame_, 4u);
            rt_supplied_ += (rt_supplied_target_ - rt_supplied_) * (1.0f - std::pow(0.95f, static_cast<float>(steps)));
        }
        if (!walk_.between) {
            rt_supplied_frame_ = list.game_frame;
        }
        const bool lamps_light = rt_physical_ && rt_part(render, RtPart::Lights) != 0u;
        rt_look_[2] = relit ? kDebake : lamps_light ? kDebake * rt_supplied_ : 0.0f;
        rt_look_[3] = rt_physical_ ? kAmbientLook : 1.0f;
        // A run's own (rt_dev.h form, art_facing, debake, ambient_look), over the style's.
        rt_look_[0] = rt_dev(RtDev::Form, rt_look_[0]);
        rt_look_[1] = rt_dev(RtDev::ArtFacing, rt_look_[1]);
        rt_look_[2] = rt_dev(RtDev::Debake, rt_look_[2]);
        rt_look_[3] = rt_dev(RtDev::AmbientLook, rt_look_[3]);
        // The relight takes the facing as it is (max(n.s, 0)); Painted from kFullFacing up (the light pass, g_look.z).
        if (rt_look_[0] > 0.0f) {
            constants.look[2] = rt_dev(RtDev::FullFacing, 1.0f);
            constants.look[0] = 1.0f;  // the relight keeps all of the sky in the sun's shadow (October had no cut)
        }
        constants.look[0] = rt_dev(RtDev::SkyInShadow, constants.look[0]);
        // The model's numbers once a stage (rt-inputs; FB b59's sky, sun and E from the programs' A and K).
        if (::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs) && list.stage_hash != rt_stage_logged_) {
            rt_stage_logged_ = list.stage_hash;
            const auto lin = [](float x) {
                x = std::clamp(x, 0.0f, 1.0f);
                return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
            };
            const auto luma = [](const float v[3]) { return 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2]; };
            float sky[3], full[3], rest[3];
            for (unsigned c = 0; c < 3u; ++c) {
                sky[c] = lin(rt_stage_a_[c]);
                full[c] = std::max(lin(std::min(rt_stage_a_[c] + rt_stage_k_[c], 1.0f)), 1e-4f);
                rest[c] = std::max(full[c] - sky[c], 0.0f);
            }
            const float e = luma(full) / std::max(luma(sky) + luma(rest) * rt_look_[1], 1e-4f);
            const float strength = rt_dev(RtDev::SunStrength, 1.0f);  // D3: the sun at the stage's key
            std::fprintf(stderr, "[rt-stage] %08x E %.3f sky %.3f %.3f %.3f sun %.3f %.3f %.3f up %.3f\n", list.stage_hash,
                         static_cast<double>(e), static_cast<double>(sky[0] * e / full[0]), static_cast<double>(sky[1] * e / full[1]),
                         static_cast<double>(sky[2] * e / full[2]), static_cast<double>(rest[0] * strength * e / full[0]),
                         static_cast<double>(rest[1] * strength * e / full[1]), static_cast<double>(rest[2] * strength * e / full[2]),
                         static_cast<double>(rt_look_[1]));
        }
    }
    constants.more[2] = (sunned && rt_part(render, RtPart::Sun) != 0u ? 1u : 0u) |
                        (rt_part(render, RtPart::Reflect) >= 2u ? 32u : 0u) | 64u;  // low: the painted maps stay, as off
    // Bit 12: the camera under water, as the game reports it: water is seen from inside (rt_light.hlsl clear_surface).
    constants.more[2] |= camera_water_depth() >= 0.0f ? 4096u : 0u;
    // Bit 13: the landing cue under the player (the setting; l80, rt_light_space.hlsl).
    constants.more[2] |= render.rt_landing_shadow ? 8192u : 0u;
    std::memcpy(constants.up, sky_up_, sizeof(sky_up_));
    if (rt_pass_indirect_on()) {
        // The sky and bounce light pass gives the sky's share (bit 8; the light pass traces none: 22.10). A painted
        // surface keeps kShadowTint of its colour from it (traced_painted: plan 11.4's faint cast).
        constants.more[2] |= 256u;
    }
    if (rt_pass_reflect_on()) {
        constants.more[2] |= 1024u;  // the reflections pass writes the opaque layer's mirror (rt_pass_reflect.cpp)
    }
    // For a pixel whose mesh the pass cannot read: the camera's own move since the frame before (none where there is
    // no frame before: the filter starts anew there anyway).
    Affine34 before_from_now = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    std::memcpy(constants.ray_before, frame.ray, sizeof(constants.ray_before));
    if (frame.history) {
        before_from_now = affine_multiply(rt_light_view_before_, world_from_now);
        std::memcpy(constants.ray_before, rt_light_ray_before_, sizeof(constants.ray_before));
        constants.kept[2] = 1u;  // the filters' history and the frame before's reservoirs count
    }
    // The rate frames are traced at, smoothed: the filters count frames, so at 30 a second each history holds half as
    // many and at 120 twice as many, as many seconds either way (plan 16.5).
    const std::int64_t ticks = ::galaxy::qpc_now();
    if (rt_light_ticks_before_ != 0) {
        const double seconds = static_cast<double>(ticks - rt_light_ticks_before_) / static_cast<double>(::galaxy::qpc_frequency());
        if (seconds > 1e-4 && seconds < 0.25) {
            rt_frame_rate_ += (static_cast<float>(1.0 / seconds) - rt_frame_rate_) * 0.1f;
        }
    }
    rt_light_ticks_before_ = ticks;
    if (const float pinned = rt_dev(RtDev::Rate, 0.0f); pinned > 0.0f) {
        rt_frame_rate_ = pinned;  // rt_dev.h rate: a picture run on a busy machine filters as at this rate
    }
    constants.more[2] |= rt_raw_ ? 2048u : 0u;  // FB (22.20): the signals raw, for an outside denoiser
    constants.kept[3] = std::bit_cast<std::uint32_t>(std::clamp(rt_frame_rate_ / 60.0f, 0.5f, 2.0f));
    std::memcpy(constants.before, before_from_now.data(), sizeof(constants.before));
    if (::galaxy::diagnosing(::galaxy::kDiagnoseRtAdded)) {
        // The scene under its emitters alone: no sun, no sky, no mirror.
        constants.more[2] = (constants.more[2] & ~32u) | 128u;
    }
    if (::galaxy::diagnosing(::galaxy::kDiagnoseRtMotion)) {
        // Each point's motion in place of the light (shown the way rt-light shows it), nothing added over it.
        constants.more[2] |= 512u;
        constants.glow[3] = 0u;
    }
    // A ray that leaves the scene and meets no sky (a stage without a dome): the stage's ambient colour.
    for (unsigned c = 0; c < 3u; ++c) {
        constants.sky[c] = list.stage_light_known != 0u ? list.stage_ambient[c] : 0.25f;
    }
    for (unsigned i = 0; i < kRtDevTracerSlots; ++i) {
        const unsigned which = kRtDevTracerFirst + i;
        if ((rt_dev_set_ >> which & 1u) != 0u) {
            constants.dev[i / 4u][i % 4u] = rt_dev_[which];
            constants.dev_set[0] |= 1u << i;
        }
    }
    // The programs' sun_strength, sky_chroma and stage_light again in the tracer's tail (g_dev 6, 7, 8): hits light a
    // surface as the programs do under the same keys (FB b66).
    constexpr RtDev kShared[3] = {RtDev::SunStrength, RtDev::SkyChroma, RtDev::StageLight};
    for (unsigned j = 0; j < 3u; ++j) {
        const unsigned which = static_cast<unsigned>(kShared[j]);
        const unsigned i = kRtDevTracerSlots + j;
        if ((rt_dev_set_ >> which & 1u) != 0u) {
            constants.dev[i / 4u][i % 4u] = rt_dev_[which];
            constants.dev_set[0] |= 1u << i;
        }
    }
    static_assert(kRtDevTracerSlots + 3u <= 32u, "g_dev holds 32 slots");
    static_assert(kRtDevTracerSlots == 6u, "rt_frame.hlsli names each tracer slot (kDev*)");
    static_assert(offsetof(RtFrameConstants, dev) == 336u && offsetof(RtFrameConstants, dev_set) == 464u);
    static_assert(sizeof(RtFrameConstants) <= 512u && offsetof(RtFrameConstants, more) == 64u);
    static_assert(offsetof(RtFrameConstants, kept) == 304u);  // rt_pass_indirect.cpp HalfFrameConstants
    const UploadRing::Allocation at = renderer_.constant_ring().allocate(512u, 256u);
    std::memcpy(at.cpu, &constants, sizeof(constants));
    binds.constants = {at.gpu, 512u};
    binds.counters = true;
}

// The traced light over the prepass's depth and items, before the scene's first draw: the frame's tables, then its
// passes, each a shader of its own over the surface images the first writes (rt_common.hlsli): the surfaces, the sun,
// the local lights, the sky and bounce light, the shares the programs read with what glass and metal mirror, the
// reflections, the light volume; then the filters. Its pictures become textures the scene's views name.
void RenderCore::rt_light() {
    const core::ListData& now = *walk_.now;
    const NebulaFrameList& list = *now.list;
    const ScaledViewport viewport = viewport_pixels(list.views[rt_scene_view_].viewport, renderer_.frame_map());
    const rt::Structure scene = rt_scene_.structure();
    if (scene.name == 0u || !viewport.valid() || list.item_count == 0u) {
        return;
    }
    const RtView camera = scene_depth_view(walk_view_terms_[rt_scene_view_].data(), viewport);
    const Renderer::RtCommands commands = renderer_.rt_commands_begin();
    rt::TextureView depth{}, items{};
    unsigned width = 0u, height = 0u;
    if (renderer_.rt_light_begin(depth, items, width, height)) {
        rt_backend_.begin(commands.commands, renderer_.frame_slot(), commands.descriptors);
        // FB (22.20): the renderer's outside denoiser takes the signals raw (rt_denoiser.h).
        const rt::Denoiser* const denoiser = renderer_.rt_denoiser();
        rt_raw_ = denoiser != nullptr && denoiser->raw_signals() == rt::kSignalAll;
        // The reference writes its rgb O into the Indirect image (A1-16; FB b79): made in its runs too.
        const std::uint32_t wanted = (rt_raw_ ? kRtPassGuides : 0u) | kRtPassLight |
                                     (rt_pass_indirect_on() || rt_fb_reference() ? kRtPassIndirect : 0u) |
                                     (rt_pass_near_on() ? kRtPassNear : 0u) |
                                     (rt_pass_reflect_on() ? kRtPassReflect : 0u) | (rt_pass_volume_on() ? kRtPassVolume : 0u) |
                                     (rt_blend_seen_ ? kRtPassBlend : 0u) | (rt_blend_seen_ && rt_transmit_wanted() ? kRtPassTransmit : 0u);
        rt_light_wanted_ = wanted & ~kRtPassBlend;
        ao::Constants eye{};
        bool lit = false;
        const bool images = rt_images(width, height, wanted);
        rt_image_failures_ = images ? 0u : rt_image_failures_ + 1u;
        if (images && ao::make_constants(camera, width, height, 1u, false, eye)) {
            RtFrame& frame = rt_light_frame_;
            frame.binds = rt::PassBinds{};
            frame.binds.scene = scene;
            frame.binds.textures[0] = depth;
            frame.binds.textures[1] = items;
            frame.width = width;
            frame.height = height;
            frame.across = (width + 7u) / 8u;
            frame.down = (height + 7u) / 8u;
            std::memcpy(frame.ray, eye.ray, sizeof(frame.ray));
            std::memcpy(frame.depth, eye.depth, sizeof(frame.depth));
            std::memcpy(frame.viewport, eye.viewport, sizeof(frame.viewport));
            frame.turn = rt_light_number_++;
            frame.layer = 0u;
            const Affine34& view_now = walk_cameras_[rt_scene_camera_];
            Affine34 world_from_now{};
            frame.history = rt_light_history_ && rt_light_walk_before_ + 1u == walk_.serial && !walk_.time.reset && !now.cut &&
                            affine_inverse(view_now, world_from_now);
            rt_tables(world_from_now);
            frame.pass = rt_pass_constants(0u, 0u, 0u);
            rt_micromaps();
            rt_noise_fill();
            rt_pass_surface();
            const bool timed = rt_measure(width);
            if (timed) {
                rt_backend_.time_mark(rt::kTimedSun, false);
            }
            rt_pass_sun();
            if (timed) {
                rt_backend_.time_mark(rt::kTimedSun, true);
            }
            rt_sun_filter();
            rt_pass_lights();
            rt_pass_indirect();
            rt_pass_light();
            rt_pass_reflect();
            rt_pass_volume();
            rt_pass_guides();
            rt_pass_reference();
            rt_backend_.end_counters();
            rt_filters();
            rt_reference_publish();
            rt_light_view_before_ = view_now;
            std::memcpy(rt_light_ray_before_, eye.ray, sizeof(rt_light_ray_before_));
            rt_light_walk_before_ = walk_.serial;
            rt_light_history_ = true;
            lit = true;
        }
        renderer_.rt_light_end();
        if (lit) {
            void* const added = rt_backend_.image_to_sample(rt_image(RtImage::Added));
            void* const mirror = rt_backend_.image_to_sample(rt_image(RtImage::Mirror));
            void* const facing = rt_backend_.image_to_sample(rt_image(RtImage::Facing));
            if (void* const texture = rt_backend_.image_to_sample(rt_image(RtImage::Light));
                texture != nullptr && added != nullptr && mirror != nullptr) {
                rt_light_slot_ = renderer_.rt_publish_light(texture, texture_store_);
                rt_added_slot_ = renderer_.rt_publish_light(added, texture_store_, 1u);
                rt_mirror_slot_ = renderer_.rt_publish_light(mirror, texture_store_, 2u);
                rt_facing_slot_ = renderer_.rt_publish_light(facing, texture_store_, 3u);
                // The light volume (FB): picture 4, read through a linear sampler (program.hlsli traced_air).
                if (rt_volume_drawn_) {
                    if (void* const volume = rt_backend_.image_to_sample(rt_now(RtImage::VolumeA)); volume != nullptr) {
                        rt_volume_slot_ = renderer_.rt_publish_light(volume, texture_store_, 4u);
                        TextureSampler linear;
                        linear.mag_linear = linear.min_linear = true;
                        rt_volume_sampler_ = renderer_.sampler_slot(make_sampler_key(linear, false, 1u));
                    } else {
                        rt_volume_drawn_ = false;
                    }
                }
                // The light in the air (FB): picture 5 (program.hlsli traced_scatter).
                rt_scatter_drawn_ = rt_scatter_drawn_ && rt_volume_drawn_;
                if (rt_scatter_drawn_) {
                    if (void* const scatter = rt_backend_.image_to_sample(rt_image(RtImage::Scatter)); scatter != nullptr) {
                        rt_scatter_slot_ = renderer_.rt_publish_light(scatter, texture_store_, 5u);
                    } else {
                        rt_scatter_drawn_ = false;
                    }
                }
                // The opaque layer's guide (FB, 15.2): picture 8, what blended draws compare with where no second layer
                // runs (program.hlsli traced_by_air); with the volume or without (traced_mirror_own reads picture 9).
                if (void* const guide = rt_backend_.image_to_sample(rt_now(RtImage::GuideA)); guide != nullptr) {
                    rt_opaque_guide_slot_ = renderer_.rt_publish_light(guide, texture_store_, 8u);
                    rt_blend_guide_mode_ = 2u;
                }
                // The sky and bounce light's ratio a channel at a time (FB 22.10): picture 7 (program.hlsli traced_o).
                rt_indirect_shown_ = false;
                if (rt_pass_indirect_on() || (rt_fb_reference() && rt_ref_ready_)) {  // the reference's once it has one (b79)
                    if (void* const ratio = rt_backend_.image_to_sample(rt_image(RtImage::Indirect)); ratio != nullptr) {
                        rt_indirect_slot_ = renderer_.rt_publish_light(ratio, texture_store_, 7u);
                        rt_indirect_shown_ = true;
                    }
                }
                rt_light_on_ = true;
                rt_guides_publish();
                rt_program_signal_ = rt_program_signal_due();
                view_flags_ |= 8u | (::galaxy::diagnosing(::galaxy::kDiagnoseRtLight | ::galaxy::kDiagnoseRtMotion) ? 16u : 0u) |
                               (rt_lamps_own_ ? 32u : 0u) |  // local lights off: the programs' own lamp (FC)
                               rt_program_signal_;  // signals S (64), A (128), F (256): drawn in place of the colour
                ++walk_view_stamp_;  // the scene's views are written again, with the light
            }
        } else {
            rt_light_frame_.across = 0u;  // no second layer either
        }
    }
    renderer_.rt_commands_end();
    rt_report();
}

// Plan 19.2: the default preset by measurement, not by device lists. Once a config (rt_probe_ms 0), in the first stage
// traced with a sun: the sun pass, one shadow ray a pixel, timed by the GPU over kRtMeasureFrames frames after 60
// settled ones (a sun, history unbroken); the median scaled to the output's pixels is the measure. High under 0.35 ms,
// medium under 1.2, low above (over 3.5 the log says so; the player turned tracing on, so it stays on). It sets the
// preset unless the player chose one (rt_chosen) and is saved with the settings. Whether this frame's sun pass is timed.
bool RenderCore::rt_measure(unsigned width) {
    if (rt_measure_done_) {
        return false;
    }
    const double ms_per_tick = renderer_.gpu_ms_per_tick();
    if (ms_per_tick <= 0.0 || get_render_config_chosen().rt_probe_ms > 0.0f) {
        rt_measure_done_ = true;
        return false;
    }
    const std::uint64_t ticks = rt_backend_.timed_ticks(rt::kTimedSun, renderer_.frame_slot());
    const unsigned output = renderer_.temporal_output_width();
    const float scale = width != 0u && output > width ? static_cast<float>(output) / static_cast<float>(width) : 1.0f;
    if (ticks != 0u && rt_measure_count_ < kRtMeasureFrames) {
        rt_measure_ms_[rt_measure_count_++] = static_cast<float>(static_cast<double>(ticks) * ms_per_tick) * scale * scale;
    }
    if (rt_measure_count_ == kRtMeasureFrames) {
        rt_measure_done_ = true;
        std::nth_element(rt_measure_ms_, rt_measure_ms_ + kRtMeasureFrames / 2u, rt_measure_ms_ + kRtMeasureFrames);
        const float ms = std::max(rt_measure_ms_[kRtMeasureFrames / 2u], 0.001f);
        RenderConfig chosen = get_render_config_chosen();
        const unsigned preset = ms < 0.35f ? 1u : ms < 1.2f ? 0u : 2u;  // high, medium, low
        static constexpr const char* kNames[] = {"medium", "high", "low"};
        std::fprintf(stderr, "[rt] measured: %.3f ms a shadow ray a pixel at the output size: preset %s%s%s\n", ms, kNames[preset],
                     chosen.rt_chosen ? " (the player's own stays)" : "", ms >= 3.5f ? "; over 3.5 ms: too slow to trace well" : "");
        chosen.rt_probe_ms = ms;
        if (!chosen.rt_chosen) {
            chosen.rt_quality = preset;
            rt_apply_preset(chosen);
        }
        set_render_config(chosen);
        (void)save_render_config_to_file();
        return false;
    }
    rt_measure_settled_ = rt_sun_now_ && rt_light_frame_.history ? rt_measure_settled_ + 1u : 0u;
    return rt_measure_settled_ > 60u;
}

// --diagnostics rt-inputs: what the passes counted, every 300 frames.
void RenderCore::rt_report() {
    if (!::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs)) {
        return;
    }
    std::uint32_t counted[rt::kCounters];
    rt_backend_.read_counters(renderer_.frame_slot(), counted);
    for (unsigned i = 0; i < 5u; ++i) {
        rt_light_sum_[i] += counted[16u + i];
    }
    rt_light_sum_[6] += counted[21];
    rt_light_sum_[7] += counted[22];
    rt_light_sum_[8] += counted[23];
    rt_light_sum_[9] += counted[24];
    rt_light_sum_[10] += counted[51];
    std::uint32_t prepassed = 0u;
    for (const std::uint8_t drawn : rt_prepassed_) {
        prepassed += drawn;
    }
    rt_light_sum_[5] += prepassed;
    if (++rt_light_frames_ < 300u) {
        return;
    }
    const double n = static_cast<double>(rt_light_frames_);
    std::fprintf(stderr,
                 "[rt-inputs] light pass over %u frames, a frame: items in the prepass %.1f, surface pixels %.0f, rays %.0f, "
                 "in the sun's shadow %.0f, under the landing cue %.0f, in a lamp's shadow %.0f; emitters %zu now; cutout tests %.0f, "
                 "cutouts unread %.0f, mirror rays %.0f; mesh arenas %u, items without %u, items of many matrices %u; "
                 "clip volumes left out (most a frame) %u\n",
                 rt_light_frames_, static_cast<double>(rt_light_sum_[5]) / n, static_cast<double>(rt_light_sum_[0]) / n,
                 static_cast<double>(rt_light_sum_[1]) / n, static_cast<double>(rt_light_sum_[2]) / n,
                 static_cast<double>(rt_light_sum_[3]) / n, static_cast<double>(rt_light_sum_[4]) / n, rt_emitters_.size(),
                 static_cast<double>(rt_light_sum_[6]) / n, static_cast<double>(rt_light_sum_[7]) / n,
                 static_cast<double>(rt_light_sum_[8]) / n, rt_light_arenas_,
                 rt_light_unplaced_, rt_light_skinned_, rt_clip_dropped_);
    rt_clip_dropped_ = 0u;
    unsigned kinds[3]{};
    for (const RtEmitter& e : rt_emitters_) {
        ++kinds[std::min<std::uint32_t>(e.kind, 2u)];
    }
    std::fprintf(stderr, "[rt-inputs]   emitters now: %u point lights of the game, %u glowing meshes, %u particles\n", kinds[0],
                 kinds[1], kinds[2]);
    std::fprintf(stderr, "[rt-inputs]   refracted pixels %.0f a frame (glass and water bodies, High)\n",
                 static_cast<double>(rt_light_sum_[9]) / n);
    std::fprintf(stderr, "[rt-inputs]   added light over the guard (kAddedMost 1.5 of the base) %.0f pixels a frame\n",
                 static_cast<double>(rt_light_sum_[10]) / n);
    // What they are, over the report's frames (emitter_census.py names them): most lights first.
    if (!rt_emitter_sources_.empty()) {
        std::vector<std::pair<std::uint64_t, std::uint32_t>> sources(rt_emitter_sources_.begin(), rt_emitter_sources_.end());
        std::sort(sources.begin(), sources.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        std::fprintf(stderr, "[rt-inputs]   sources:");
        for (std::size_t s = 0; s < sources.size() && s < 96u; ++s) {
            std::fprintf(stderr, " %c%08x x%u", (sources[s].first >> 32u) == 1u ? 'm' : 'p',
                         static_cast<unsigned>(sources[s].first & 0xFFFFFFFFu), sources[s].second);
        }
        std::fprintf(stderr, "\n");
        rt_emitter_sources_.clear();
    }
    // The six strongest, as the pass reads them: kind, the source's radius and reach in metres, and its
    // intensity back in candela (what it adds at its own radius, times the exposure and the radius squared).
    std::vector<const RtEmitter*> order;
    order.reserve(rt_emitters_.size());
    for (const RtEmitter& e : rt_emitters_) {
        order.push_back(&e);
    }
    const auto strength = [](const RtEmitter* e) { return std::max({e->colour[0], e->colour[1], e->colour[2]}) * e->radius * e->radius; };
    std::sort(order.begin(), order.end(), [&](const RtEmitter* a, const RtEmitter* b) { return strength(a) > strength(b); });
    for (std::size_t rank = 0; rank < order.size() && rank < 6u; ++rank) {
        const RtEmitter& e = *order[rank];
        static constexpr const char* kKind[3] = {"point light", "glowing mesh", "particles"};
        std::fprintf(stderr, "[rt-inputs]     %s: radius %.2f m, reach %.1f m, %.0f candela at exposure %.0f, colour %.2f %.2f %.2f\n",
                     kKind[std::min<std::uint32_t>(e.kind, 2u)], static_cast<double>(e.radius / kUnitsPerMetre),
                     static_cast<double>(e.reach / kUnitsPerMetre),
                     static_cast<double>(strength(&e) / (kUnitsPerMetre * kUnitsPerMetre) * rt_exposure_), static_cast<double>(rt_exposure_),
                     static_cast<double>(e.colour[0]), static_cast<double>(e.colour[1]), static_cast<double>(e.colour[2]));
    }
    for (std::uint64_t& sum : rt_light_sum_) {
        sum = 0u;
    }
    rt_light_frames_ = 0u;
}

// The scene's blended draws wrote their alpha into the upscaler's reactive mask; the tracer raises it where the added
// light changed by more than a quarter since the frame before (plan 16.4: rt_reactive.hlsl), so the upscaler takes
// this frame's colour there and a flickering light leaves no trail. Only with a temporal upscaler (its mask).
void RenderCore::rt_reactive() {
    if (!rt_light_on_ || !temporal_scene_ || rt_light_frame_.across == 0u) {
        return;
    }
    const std::uint32_t moments = rt_now(RtImage::LightsMomentsA);
    if (moments == rt::kNoImage) {
        return;
    }
    const Renderer::RtCommands commands = renderer_.rt_commands_begin();
    rt::TextureView reactive{};
    if (renderer_.rt_reactive_begin(reactive)) {
        rt_backend_.begin(commands.commands, renderer_.frame_slot(), commands.descriptors);
        rt::PassBinds binds;
        binds.constants = rt_light_frame_.binds.constants;
        binds.pass_constants = rt_light_frame_.pass;
        binds.reads[0] = moments;
        // FB: where the reflections changed too (plan 16.4's R), when the reflection pass wrote it this frame.
        const std::uint32_t reflect_moved = rt_reflect_moved_ ? rt_image(RtImage::ReflectMoved) : rt::kNoImage;
        if (reflect_moved != rt::kNoImage) {
            binds.reads[1] = reflect_moved;
            binds.pass_constants = rt_pass_constants(0u, 0u, 0u, 1u);
        }
        rt_reflect_moved_ = false;
        binds.write_texture = reactive;
        rt_backend_.dispatch(RtShader::Reactive, binds, rt_light_frame_.across, rt_light_frame_.down);
        renderer_.rt_reactive_end();
    }
    renderer_.rt_commands_end();
}

// The scene is over: what is drawn after it reads no traced light.
void RenderCore::rt_light_end() noexcept {
    if (rt_light_on_) {
        rt_light_on_ = false;
        view_flags_ &= ~504u;  // bits 3 to 8 (the light, motion, own lamp, S, A, F)
    }
}

// The lit programs' key light from the sun's direction (render_core.h).
void RenderCore::rt_sun_keys(const float eye_direction[3]) {
    const core::ListData& now = *walk_.now;
    const NebulaFrameList& list = *now.list;
    if (walk_rows_upload_ == nullptr) {
        return;
    }
    constexpr float kFar = 1.0e6f;
    for (std::uint32_t s = 0; s < list.light_set_count; ++s) {
        if (now.light_camera[s] != rt_scene_camera_ || list.light_set_owners[s].actor_light == 0u || list.light_sets[s].count == 0u) {
            continue;
        }
        // Light 0's four rows: its place is the third row's z and w and the fourth's x; its direction the fourth's
        // y, z and w (the direction to it, for a light that is one).
        const std::size_t row = std::size_t{walk_light_rows_[s]} + core::kLightSetHeaderRows;
        auto* cpu = const_cast<float (*)[4]>(walk_rows_cpu_) + row;
        const float old_place[3] = {cpu[2][2], cpu[2][3], cpu[3][0]};
        const float size = std::sqrt(old_place[0] * old_place[0] + old_place[1] * old_place[1] + old_place[2] * old_place[2]);
        const bool aimed = size > 1.0f && (old_place[0] * cpu[3][1] + old_place[1] * cpu[3][2] + old_place[2] * cpu[3][3]) / size > 0.9f;
        cpu[2][2] = eye_direction[0] * kFar;
        cpu[2][3] = eye_direction[1] * kFar;
        cpu[3][0] = eye_direction[2] * kFar;
        if (aimed) {
            cpu[3][1] = eye_direction[0];
            cpu[3][2] = eye_direction[1];
            cpu[3][3] = eye_direction[2];
        }
        std::memcpy(walk_rows_upload_ + (row + 2u) * 16u, cpu[2], 32u);
    }
}

// The stage's shadow sun (render_core.h).
bool RenderCore::rt_sun(float eye_direction[3]) {
    const core::ListData& now = *walk_.now;
    const NebulaFrameList& list = *now.list;
    const Affine34& view = walk_cameras_[rt_scene_camera_];  // eye = R world + t
    const auto to_world = [&](const float eye[3], float world[3]) {
        for (unsigned c = 0; c < 3u; ++c) {
            world[c] = view[c] * eye[0] + view[4u + c] * eye[1] + view[8u + c] * eye[2];
        }
    };
    const auto unit = [](float v[3]) {
        const float size = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (!(size > 1e-6f) || !std::isfinite(size)) {
            return false;
        }
        v[0] /= size;
        v[1] /= size;
        v[2] /= size;
        return true;
    };
    const auto dot = [](const float x[3], const float y[3]) { return x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; };
    const bool report = ::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs) && rt_light_frames_ == 0u;
    const RtStage* const row = rt_stage(list.stage_hash);
    const RtStage::Sun mode = row != nullptr ? row->sun : RtStage::Sun::Auto;
    float up[3]{};
    const bool up_known = list.sky_up_known != 0u && (to_world(sky_up_, up), unit(up));
    float target[3]{};
    std::uint8_t source = 0u;
    std::uint64_t key_id = 0u;
    bool follows = false;
    float key_brightness = -1.0f;
    if (mode == RtStage::Sun::Fixed) {
        std::memcpy(target, row->direction, sizeof(target));
        source = unit(target) ? 3u : 0u;
    }
    // The sun the player sees, where the stage has one: `position` is the unit direction from the camera to it.
    for (std::uint32_t i = 0; i < list.world_light_count && source == 0u && (mode == RtStage::Sun::Auto || mode == RtStage::Sun::Object); ++i) {
        const NebulaFrameWorldLight& light = list.world_lights[i];
        if (light.kind == NEBULA_WORLD_LIGHT_SUN) {
            if ((light.flags & NEBULA_WORLD_LIGHT_FOLLOWS_CAMERA) != 0u) {
                to_world(light.position, target);
            } else {
                std::memcpy(target, light.position, sizeof(target));
            }
            source = unit(target) ? 1u : 0u;
        }
    }
    // Else the stage's key light, from where the camera stands; none where the key is off (an interior, a night).
    if (source == 0u && (mode == RtStage::Sun::Auto || mode == RtStage::Sun::Key) && list.stage_light_known != 0u) {
        if (const NebulaFrameWorldLight* key = core::find_world_light(list, list.stage_light); key != nullptr) {
            key_brightness = 0.2126f * key->color[0] + 0.7152f * key->color[1] + 0.0722f * key->color[2];
            if (mode == RtStage::Sun::Key || key_brightness >= 0.05f) {
                follows = (key->flags & NEBULA_WORLD_LIGHT_FOLLOWS_CAMERA) != 0u;
                if (follows) {
                    to_world(key->position, target);
                } else {
                    const float offset[3] = {view[3], view[7], view[11]};
                    float camera[3];
                    to_world(offset, camera);  // the camera stands at -R^T t
                    for (unsigned c = 0; c < 3u; ++c) {
                        target[c] = key->position[c] + camera[c];
                    }
                }
                if (unit(target)) {
                    source = 2u;
                    key_id = list.stage_light;
                }
            }
        }
    }
    // Else, for a stage that says nothing of its light, straight overhead.
    if (source == 0u && mode == RtStage::Sun::Auto && list.stage_light_known == 0u && up_known) {
        std::memcpy(target, up, sizeof(target));
        source = 4u;
    }
    if (source == 0u) {
        if (report) {
            std::fprintf(stderr, "[rt-inputs] sun: none (stage %08x %s, key %.3f)\n", list.stage_hash, row != nullptr ? "row" : "default",
                         static_cast<double>(key_brightness));
        }
        rt_sun_known_ = false;
        rt_sun_source_ = 0u;
        rt_sun_light_ = 0u;
        return false;
    }
    // ONE FIXED SUN A STAGE (director d05; the owner, 11-11: "we need just the one fixed sun point and no moving suns
    // or anything"). The direction is taken once, the first frame the stage shows a sun, and held in the world until
    // the stage changes: not taken again on a cut, when the zone's light changes, or as the camera or the player moves
    // or turns over. A walk round a planet leaves the sun where it is: the far side is in shade, and the exposure's
    // anchor and the eye keep it readable (LIGHT-MODEL.md 2 and 9). It is lifted ONCE, as it is taken, to at least 30
    // degrees over the horizon of the player as he stands then, so the place a stage starts in has the sun (taken once
    // more when his up first becomes known, for that lift); a stage row's own direction is taken as written. Ray
    // tracing switched off and on again keeps it; a frame with no sun (an interior zone) leaves it waiting.
    bool lifted = false;
    if (!rt_sun_fixed_known_ || rt_sun_fixed_stage_ != list.stage_hash || (!rt_sun_fixed_final_ && up_known)) {
        float taken[3] = {target[0], target[1], target[2]};
        if (source != 3u && up_known) {
            constexpr float kLeastRise = 0.5f, kFlat = 0.8660254f;  // 30 degrees above the player's horizon
            const float rise = dot(taken, up);
            if (rise < kLeastRise) {
                lifted = true;
                float flat[3] = {taken[0] - up[0] * rise, taken[1] - up[1] * rise, taken[2] - up[2] * rise};
                if (!unit(flat)) {
                    // Straight below: any side will do.
                    const float side[3] = {std::fabs(up[0]) < 0.9f ? 1.0f : 0.0f, std::fabs(up[0]) < 0.9f ? 0.0f : 1.0f, 0.0f};
                    flat[0] = up[1] * side[2] - up[2] * side[1];
                    flat[1] = up[2] * side[0] - up[0] * side[2];
                    flat[2] = up[0] * side[1] - up[1] * side[0];
                    static_cast<void>(unit(flat));
                }
                for (unsigned c = 0; c < 3u; ++c) {
                    taken[c] = flat[c] * kFlat + up[c] * kLeastRise;
                }
            }
        }
        std::memcpy(rt_sun_world_, taken, sizeof(taken));
        rt_sun_fixed_known_ = true;
        rt_sun_fixed_final_ = up_known || source == 3u;
        rt_sun_fixed_stage_ = list.stage_hash;
    }
    rt_sun_known_ = true;
    rt_sun_source_ = source;
    rt_sun_light_ = key_id;
    const float sun[3] = {rt_sun_world_[0], rt_sun_world_[1], rt_sun_world_[2]};
    if (report) {
        static constexpr const char* kSources[] = {"none", "the visible sun", "the key", "the row", "overhead"};
        std::fprintf(stderr, "[rt-inputs] sun: %s%s, world %.3f %.3f %.3f%s (stage %08x %s, key %.3f)\n", kSources[source],
                     follows ? " (latched)" : "", static_cast<double>(sun[0]), static_cast<double>(sun[1]), static_cast<double>(sun[2]),
                     lifted ? " lifted" : "", list.stage_hash, row != nullptr ? "row" : "default",
                     static_cast<double>(key_brightness));
    }
    for (unsigned r = 0; r < 3u; ++r) {
        eye_direction[r] = view[r * 4u] * sun[0] + view[r * 4u + 1u] * sun[1] + view[r * 4u + 2u] * sun[2];
    }
    return unit(eye_direction);
}

namespace {

// The glow a draw gives now (L-1): the cooked glow, its colour scaled channel by channel by how the game has changed
// the material's glow-carrying row since cooking (a lamp it dims, tints or turns off), then made 0 to 1 again with
// the largest channel's change moved into the strength. A draw without live rows, or whose rows are another form's,
// keeps the cooked glow.
void live_glow(const NebulaFrameList& list, const NebulaFrameItem& item, const core::GpuMesh& mesh, float out[4]) {
    std::copy_n(mesh.glow, 4u, out);
    if (mesh.glow_row == 0xFFFFu || item.constants >= list.constants_count || list.constants == nullptr) {
        return;
    }
    const NebulaFrameConstants& constants = list.constants[item.constants];
    if (constants.rows == nullptr || constants.vertex_rows + constants.pixel_rows != mesh.glow_row_count) {
        return;
    }
    const float* live = constants.rows[mesh.glow_row];
    // c81 (L1): a row black at the cook (a lamp the game lights by it) gives its live colour as it is; the cooked glow
    // is what the material shows with it white.
    const bool dark = std::max({mesh.glow_cooked[0], mesh.glow_cooked[1], mesh.glow_cooked[2]}) < 0.5f / 255.0f;
    float colour[3], now[3], largest = 0.0f;
    for (unsigned c = 0; c < 3u; ++c) {
        // A channel cooked black takes the live value against a byte's step: never a division by nothing.
        now[c] = std::clamp(live[c], 0.0f, 8.0f);
        const float ratio = dark ? now[c] : now[c] / std::max(mesh.glow_cooked[c], 1.0f / 255.0f);
        colour[c] = mesh.glow[c] * std::min(ratio, 8.0f);
        largest = std::max(largest, colour[c]);
    }
    // c81 (L1.3): a row the game has dimmed under a twentieth of white (and under its cooked value) shows no light.
    const auto luma = [](const float* v) { return 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2]; };
    const bool dimmed = luma(now) < 0.05f && (dark || luma(now) < luma(mesh.glow_cooked));
    if (!std::isfinite(largest) || !(largest > 1.0e-4f) || dimmed) {
        out[3] = 0.0f;  // turned off
        return;
    }
    for (unsigned c = 0; c < 3u; ++c) {
        out[c] = colour[c] / largest;
    }
    out[3] = mesh.glow[3] * largest;
}

}  // namespace

// A glowing mesh's draw as a light: one a block (a model's glowing meshes are one light, the largest of them). Its
// intensity is the cooker's (candela, or below zero a luminance by the mesh's size); a long thin mesh (a rail of
// electricity, a beam) is a row of lights along its length, its value taken a metre.
void RenderCore::rt_emitter(const NebulaFrameItem& item, const core::GpuMesh& mesh, const float centre[3], float radius,
                            const float (*place)[4]) {
    if (!(radius > 0.0f) || !std::isfinite(radius + centre[0] + centre[1] + centre[2])) {
        return;
    }
    constexpr std::size_t kMost = 4608u;  // past the particles' own 4096: a mesh's light is not crowded out by sparks
    float glow[4];
    live_glow(*walk_.now->list, item, mesh, glow);
    light_colour(glow, glow, rt_dev(RtDev::LightSrgb, 1.0f) > 0.5f);  // its rgb as linear light (l65); glow[3] stays
    if (glow[3] == 0.0f) {
        return;  // the game has turned this glow off
    }
    // How the mesh's box lies: its three half axes through its matrix; with none (a skinned mesh: only where its
    // matrices stand is known), its own box's proportions at the size given.
    float axis[3][3]{}, half[3];
    {
        float extent[3], length = 0.0f;
        for (unsigned k = 0; k < 3u; ++k) {
            extent[k] = (mesh.max[k] - mesh.min[k]) * 0.5f;
            length += extent[k] * extent[k];
        }
        length = std::sqrt(length);
        for (unsigned k = 0; k < 3u; ++k) {
            if (place != nullptr) {
                for (unsigned r = 0; r < 3u; ++r) {
                    axis[k][r] = place[r][k] * extent[k];
                }
                half[k] = std::sqrt(axis[k][0] * axis[k][0] + axis[k][1] * axis[k][1] + axis[k][2] * axis[k][2]);
            } else {
                half[k] = length > 0.0f ? extent[k] * (radius / length) : radius * 0.57735027f;
            }
        }
    }
    // A luminance lights by the area the mesh shows, its mean over all directions: a quarter of its surface (Cauchy;
    // a box of half axes a, b, c: 2(ab + bc + ca)), in square metres (c60, A2-10; plan s5 I = L a).
    const auto shown_m2 = [](float a, float b, float c) {
        return 2.0f * (a * b + b * c + c * a) / (kUnitsPerMetre * kUnitsPerMetre);
    };
    const auto candela_of = [&](float area_m2) { return glow[3] > 0.0f ? glow[3] : -glow[3] * area_m2; };
    // One lamp: each half no wider than 6 m (a glowing plain is not one lamp).
    const float lamp_candela =
        candela_of(shown_m2(std::min(half[0], 600.0f), std::min(half[1], 600.0f), std::min(half[2], 600.0f)));
    const auto [found, fresh] = rt_emitter_blocks_.try_emplace(item.block, static_cast<std::uint32_t>(rt_emitters_.size()));
    if (fresh && ::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs)) {
        ++rt_emitter_sources_[std::uint64_t{1u} << 32u | item.source_hash];
    }
    if (!fresh) {
        RtEmitter& same = rt_emitters_[found->second];
        if (same.kind == 1u && same.reserved[0] == 0u && radius > same.radius) {
            const float candela = lamp_candela;
            std::memcpy(same.centre, centre, sizeof(same.centre));
            same.radius = std::clamp(radius, 25.0f, 600.0f);
            for (unsigned c = 0; c < 3u; ++c) {
                same.colour[c] = glow[c] * candela;
            }
        }
        return;
    }
    float along[3]{}, thick = radius;
    unsigned count = 1u;
    float piece = radius;
    float row_candela = lamp_candela;  // a row's piece: its share of the whole box's area
    if (place != nullptr) {
        // Over 6 m in two axes: a glowing surface, not a lamp (L-3). Its light comes by the rays that meet it.
        constexpr float kAreaHalf = 300.0f;
        if ((half[0] > kAreaHalf ? 1 : 0) + (half[1] > kAreaHalf ? 1 : 0) + (half[2] > kAreaHalf ? 1 : 0) >= 2) {
            const NebulaFrameList& list = *walk_.now->list;
            const std::ptrdiff_t index = &item - list.items;
            if (index >= 0 && static_cast<std::size_t>(index) < rt_emissive_areas_.size()) {
                rt_emissive_areas_[static_cast<std::size_t>(index)] = 1u;
            }
            rt_emitter_blocks_.erase(found);
            return;
        }
        const unsigned longest = half[0] >= half[1] && half[0] >= half[2] ? 0u : half[1] >= half[2] ? 1u : 2u;
        thick = std::max({half[(longest + 1u) % 3u], half[(longest + 2u) % 3u], 12.0f});
        if (half[longest] > 3.0f * thick) {
            count = std::clamp(static_cast<unsigned>(half[longest] / (2.0f * thick)), 2u, 16u);
            piece = half[longest] / static_cast<float>(count);
            std::memcpy(along, axis[longest], sizeof(along));
            row_candela = glow[3] > 0.0f ? glow[3] * (2.0f * piece / kUnitsPerMetre)
                                         : candela_of(shown_m2(half[longest], std::min(half[(longest + 1u) % 3u], 600.0f),
                                                               std::min(half[(longest + 2u) % 3u], 600.0f)) /
                                                      static_cast<float>(count));
        }
    }
    for (unsigned n = 0; n < count; ++n) {
        if (rt_emitters_.size() >= kMost) {
            if (n == 0u) {
                rt_emitter_blocks_.erase(found);
            }
            return;
        }
        RtEmitter& emitter = rt_emitters_.emplace_back();
        // A row's piece: at its place along the axis, as long as a piece, its value a metre of that length.
        const float t = count == 1u ? 0.0f : (2.0f * (static_cast<float>(n) + 0.5f) / static_cast<float>(count) - 1.0f);
        const float size = count == 1u ? radius : std::max(thick, piece);
        const float candela = count == 1u ? lamp_candela : row_candela;
        for (unsigned c = 0; c < 3u; ++c) {
            emitter.centre[c] = centre[c] + along[c] * t;
            emitter.colour[c] = glow[c] * candela;
        }
        emitter.radius = std::clamp(size, 25.0f, 600.0f);
        emitter.reach = 0.0f;
        emitter.slot = item.slot;
        emitter.kind = 1u;
        emitter.reserved[0] = count == 1u ? 0u : 1u;  // a row's piece keeps its place
        emitter.reserved[1] = rt_emitter_id(1u, item.slot, n);
    }
}

// The scene's lights as the light pass reads them. A glowing mesh at whose place the game itself asked a point light
// (a Luma, a power star, a star bit) gives way to that light: one source, in the game's own colour. Then each
// source's intensity becomes what it adds at a surface: illuminance = intensity / (distance^2 + its radius^2), in
// metres, over the stage's exposure (`exposure_lux`: the light a fully lit surface of this stage stands in). Its
// light ends where it would add less than a fiftieth.
void RenderCore::rt_emitters_finish(float exposure_lux) {
    if (rt_emitters_finished_) {
        return;
    }
    rt_emitters_finished_ = true;
    rt_exposure_ = exposure_lux;
    std::size_t points = 0u;
    while (points < rt_emitters_.size() && rt_emitters_[points].kind == 0u) {
        ++points;
    }
    if (points != 0u) {
        std::erase_if(rt_emitters_, [&](const RtEmitter& e) {
            if (e.kind != 1u) {
                return false;
            }
            const float beside = std::max(e.radius, 80.0f);  // (`near` is a macro of the Windows headers)
            for (std::size_t p = 0; p < points; ++p) {
                const RtEmitter& light = rt_emitters_[p];
                const float d[3] = {e.centre[0] - light.centre[0], e.centre[1] - light.centre[1], e.centre[2] - light.centre[2]};
                if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < beside * beside) {
                    return true;
                }
            }
            return false;
        });
    }
    float lamps = 0.0f;  // c87: lux where the eye is (the emitters' centres are in its space)
    for (RtEmitter& e : rt_emitters_) {
        const float metres = e.radius / kUnitsPerMetre;
        // c81 (L3): a glowing mesh's light at three times its radius adds at least half a lit surface's colour at the
        // stage's exposure, I >= 0.5 E (3 r)^2: raised so (up to the 5,000 candela cap), never lowered.
        if (e.kind == 1u) {
            const float was = std::max({e.colour[0], e.colour[1], e.colour[2]});
            const float least = std::min(0.5f * exposure_lux * 9.0f * metres * metres, 5000.0f);
            if (was > 0.0f && was < least) {
                for (float& c : e.colour) {
                    c *= least / was;
                }
            }
        }
        const float candela = std::max({e.colour[0], e.colour[1], e.colour[2], 0.0f});
        const float far2 = (e.centre[0] * e.centre[0] + e.centre[1] * e.centre[1] + e.centre[2] * e.centre[2]) /
                           (kUnitsPerMetre * kUnitsPerMetre);
        lamps += candela / std::max(far2 + metres * metres, 1e-4f);
        const float at_source = 1.0f / (exposure_lux * metres * metres);
        for (float& c : e.colour) {
            c *= at_source;
        }
        e.reach = std::clamp(kUnitsPerMetre * std::sqrt(candela / (0.02f * exposure_lux)), e.radius * 2.0f, 4000.0f);
    }
    // c87 (the sunless rule): the share of a fully lit surface's light the lamps give where the eye is.
    rt_supplied_target_ = std::isfinite(lamps) ? std::clamp(lamps / std::max(exposure_lux, 1.0f), 0.0f, 1.0f) : 0.0f;
}

// A particle draw as a light. Its kind gives its luminance (render/rt/rt_effect_lights.inc: an arc or a blast far
// over a flame, a flame over a sparkle, a sparkle over a streak; white water and smoke none); a draw whose name is not
// the game's (a mod's effect) is a light when it is added to the picture, at a modest 200 candela a square metre.
// Each particle is a light of its own, where it is and in its own colour, its intensity the luminance over what it
// shows this frame: so a spin's ring lights the ground around the player, a fuse sputters and a fire flickers as the
// effect itself does, and the thing in the middle of a cloud of sparks throws its shadow outward.
// A particle draw the light volume lights (FB, rt_pass_volume.cpp): one that is not a light (rt_particle_emitter's rule).
bool RenderCore::rt_volume_reads(const NebulaFrameItem& item, std::uint32_t state) const {
    if ((item.flags & (NEBULA_ITEM_INSTANCES | NEBULA_ITEM_PARTICLE)) == 0u) {
        return false;
    }
    const bool added = (state & 3u) == NEBULA_BLEND_ADD && ((state >> 6u) & 15u) == NEBULA_FACTOR_ONE;
    float luminance = effect_luminance(item.source_hash);
    if (luminance < 0.0f) {
        luminance = added ? 200.0f : 0.0f;
    }
    return !(luminance > 0.0f);
}

void RenderCore::rt_particle_emitter(const NebulaFrameItem& item, std::uint32_t state, std::uint32_t camera) {
    constexpr std::size_t kMost = 4096u;
    const bool added = (state & 3u) == NEBULA_BLEND_ADD && ((state >> 6u) & 15u) == NEBULA_FACTOR_ONE;
    float luminance = effect_luminance(item.source_hash);
    if (luminance < 0.0f) {
        luminance = added ? 200.0f : 0.0f;
    }
    if (!(luminance > 0.0f) || rt_emitters_.size() >= kMost || camera >= walk_cameras_.size()) {
        return;
    }
    if (::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs)) {
        ++rt_emitter_sources_[std::uint64_t{2u} << 32u | item.source_hash];
    }
    const NebulaFrameList& list = *walk_.now->list;
    // R << 24 | G << 16 | B << 8 | A. A particle shows its own colour where its map is white and its environment
    // colour where the map is black: most of what glows is the first.
    const auto tint_of = [](std::uint32_t prm, std::uint32_t env, float out[4]) {
        for (unsigned c = 0; c < 3u; ++c) {
            const unsigned shift = 24u - 8u * c;
            out[c] = (0.7f * static_cast<float>((prm >> shift) & 255u) + 0.3f * static_cast<float>((env >> shift) & 255u)) / 255.0f;
        }
        out[3] = static_cast<float>(prm & 255u) / 255.0f;
    };
    // What a particle shows: its quad by its alpha (a glow fills about a third of its quad), in square metres; its
    // own size a disc of that. A draw of more particles than `kEach` gives every n-th, each standing for n.
    constexpr std::uint32_t kEach = 256u;
    const std::uint32_t particles = item.ring_count / ((item.flags & NEBULA_ITEM_INSTANCES) != 0u ? 1u : 4u);
    const std::uint32_t every = std::max<std::uint32_t>((particles + kEach - 1u) / kEach, 1u);
    std::uint32_t ordinal = 0u;
    // `id`: the particle's number by its place now; `was`: by its place a frame ago (l84).
    const auto take = [&](const float eye[3], double size, const float colour[4], std::uint32_t id, std::uint32_t was) {
        const double shown = size * static_cast<double>(colour[3]) * 0.33;
        if (ordinal++ % every != 0u || !(shown > 0.0) || !std::isfinite(shown + eye[0] + eye[1] + eye[2]) || rt_emitters_.size() >= kMost) {
            return;
        }
        const float candela =
            luminance * static_cast<float>(shown / (static_cast<double>(kUnitsPerMetre) * kUnitsPerMetre)) * static_cast<float>(every);
        RtEmitter& emitter = rt_emitters_.emplace_back();
        float linear[3];
        light_colour(colour, linear, rt_dev(RtDev::LightSrgb, 1.0f) > 0.5f);
        for (unsigned c = 0; c < 3u; ++c) {
            emitter.centre[c] = eye[c];
            emitter.colour[c] = linear[c] * std::min(candela, 20000.0f);
        }
        emitter.radius = std::clamp(static_cast<float>(std::sqrt(shown / 3.14159265358979)), 10.0f, 400.0f);
        emitter.reach = 0.0f;
        emitter.slot = item.slot;
        emitter.kind = 2u;
        // Its number by its place, not by its order in the draw (which shifts as others are born and die, A2-09):
        // the next frame finds it by the place this frame gives it (l84).
        emitter.reserved[0] = was;
        emitter.reserved[1] = id;
    };
    if ((item.flags & NEBULA_ITEM_INSTANCES) != 0u) {
        // Instanced particles: each a matrix in the world, its quad as wide and tall as the matrix's first two columns.
        constexpr std::uint32_t kRows = sizeof(NebulaParticleInstance) / 16u;
        if (item.mesh != kRows || list.instance_rows == nullptr ||
            std::uint64_t{item.ring_at} + std::uint64_t{item.ring_count} * kRows > list.instance_row_count) {
            return;
        }
        const Affine34& view = walk_cameras_[camera];
        const std::uint32_t count = std::min<std::uint32_t>(item.ring_count, 2048u);
        for (std::uint32_t n = 0; n < count; ++n) {
            NebulaParticleInstance p;
            std::memcpy(&p, list.instance_rows + item.ring_at + std::size_t{n} * kRows, sizeof(p));
            float eye[3];
            for (unsigned r = 0; r < 3u; ++r) {
                eye[r] = view[r * 4u] * p.now[0][3] + view[r * 4u + 1u] * p.now[1][3] + view[r * 4u + 2u] * p.now[2][3] + view[r * 4u + 3u];
            }
            const double across = std::sqrt(static_cast<double>(p.now[0][0]) * p.now[0][0] + static_cast<double>(p.now[1][0]) * p.now[1][0] +
                                            static_cast<double>(p.now[2][0]) * p.now[2][0]);
            const double up = std::sqrt(static_cast<double>(p.now[0][1]) * p.now[0][1] + static_cast<double>(p.now[1][1]) * p.now[1][1] +
                                        static_cast<double>(p.now[2][1]) * p.now[2][1]);
            float colour[4];
            tint_of(p.prm, p.env, colour);
            // Its corners stand 25 units each way from its middle along each column (the particle library's unit:
            // ParticleDraw.cpp builds the frame's own corners with the same 25).
            take(eye, 4.0 * 625.0 * across * up, colour, rt_particle_id(p.now[0][3], p.now[1][3], p.now[2][3]),
                 rt_particle_id(p.before[0][3], p.before[1][3], p.before[2][3]));
        }
    } else {
        // The frame's own particle corners, four a particle: place, place before, two colours, coordinate.
        const std::uint32_t stride = item.mesh;
        if (stride < 32u || list.ring_vertices == nullptr || list.ring_vertex_bytes <= item.ring_at) {
            return;
        }
        const std::uint64_t room = list.ring_vertex_bytes - item.ring_at;
        const auto* vertices = static_cast<const std::byte*>(list.ring_vertices) + item.ring_at;
        const float (*place)[4] = walk_rows_cpu_ + walk_block_rows_[item.block] + item.matrix * kRowsPerMatrix;
        const std::uint32_t count = std::min<std::uint32_t>(item.ring_count, 8192u);
        for (std::uint32_t v = 0; v + 3u < count; v += 4u) {
            if (std::uint64_t{v + 3u} * stride + 32u > room) {
                break;
            }
            float corner[4][3];
            for (unsigned k = 0; k < 4u; ++k) {
                float local[3];
                std::memcpy(local, vertices + std::size_t{v + k} * stride, sizeof(local));
                for (unsigned r = 0; r < 3u; ++r) {
                    corner[k][r] = place[r][0] * local[0] + place[r][1] * local[1] + place[r][2] * local[2] + place[r][3];
                }
            }
            std::uint32_t words[2];
            std::memcpy(words, vertices + std::size_t{v} * stride + 24u, sizeof(words));
            // The first corner's place now and a frame ago, as the game wrote them (x, y, z, bx, by, bz).
            float first[6];
            std::memcpy(first, vertices + std::size_t{v} * stride, sizeof(first));
            const float e1[3] = {corner[1][0] - corner[0][0], corner[1][1] - corner[0][1], corner[1][2] - corner[0][2]};
            const float e2[3] = {corner[3][0] - corner[0][0], corner[3][1] - corner[0][1], corner[3][2] - corner[0][2]};
            const double cross[3] = {static_cast<double>(e1[1]) * e2[2] - static_cast<double>(e1[2]) * e2[1],
                                     static_cast<double>(e1[2]) * e2[0] - static_cast<double>(e1[0]) * e2[2],
                                     static_cast<double>(e1[0]) * e2[1] - static_cast<double>(e1[1]) * e2[0]};
            const float eye[3] = {0.25f * (corner[0][0] + corner[1][0] + corner[2][0] + corner[3][0]),
                                  0.25f * (corner[0][1] + corner[1][1] + corner[2][1] + corner[3][1]),
                                  0.25f * (corner[0][2] + corner[1][2] + corner[2][2] + corner[3][2])};
            float colour[4];
            tint_of(words[0], words[1], colour);
            take(eye, std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]), colour,
                 rt_particle_id(first[0], first[1], first[2]), rt_particle_id(first[3], first[4], first[5]));
        }
    }
}

// The emitters by the 16-pixel screen tiles their reach covers (as rt_drop_bins): rt_glow_tiles_, rt_glow_entries_.
void RenderCore::rt_emitter_bins(unsigned width, unsigned height, const float ray[4]) {
    constexpr std::uint32_t kTile = 16u;
    const std::uint32_t across = (width + kTile - 1u) / kTile, down = (height + kTile - 1u) / kTile;
    rt_glow_counts_.assign(std::size_t{across} * down, 0u);
    struct Reach {
        std::uint32_t x0, y0, x1, y1;
    };
    std::vector<Reach> reaches(rt_emitters_.size());
    for (std::size_t e = 0; e < rt_emitters_.size(); ++e) {
        const RtEmitter& emitter = rt_emitters_[e];
        Reach& reach = reaches[e];
        const float nearest = -emitter.centre[2] - emitter.reach, farthest = -emitter.centre[2] + emitter.reach;
        if (!(farthest > 1.0f)) {
            reach = {1u, 1u, 0u, 0u};  // behind the eye: no tile
            continue;
        }
        if (!(nearest > 1.0f)) {
            reach = {0u, 0u, across - 1u, down - 1u};  // about the eye: every tile
        } else {
            float low_x = 1e30f, low_y = 1e30f, high_x = -1e30f, high_y = -1e30f;
            for (unsigned corner = 0; corner < 8u; ++corner) {
                const float x = emitter.centre[0] + ((corner & 1u) != 0u ? emitter.reach : -emitter.reach);
                const float y = emitter.centre[1] + ((corner & 2u) != 0u ? emitter.reach : -emitter.reach);
                const float w = (corner & 4u) != 0u ? nearest : farthest;
                const float px = (x / w - ray[1]) / ray[0];
                const float py = (y / w - ray[3]) / ray[2];
                low_x = std::min(low_x, px);
                high_x = std::max(high_x, px);
                low_y = std::min(low_y, py);
                high_y = std::max(high_y, py);
            }
            if (high_x < 0.0f || high_y < 0.0f || low_x >= static_cast<float>(width) || low_y >= static_cast<float>(height)) {
                reach = {1u, 1u, 0u, 0u};
                continue;
            }
            reach.x0 = static_cast<std::uint32_t>(std::max(low_x, 0.0f)) / kTile;
            reach.y0 = static_cast<std::uint32_t>(std::max(low_y, 0.0f)) / kTile;
            reach.x1 = std::min(static_cast<std::uint32_t>(std::min(high_x, static_cast<float>(width - 1u))) / kTile, across - 1u);
            reach.y1 = std::min(static_cast<std::uint32_t>(std::min(high_y, static_cast<float>(height - 1u))) / kTile, down - 1u);
        }
        for (std::uint32_t y = reach.y0; y <= reach.y1 && reach.x0 <= reach.x1; ++y) {
            for (std::uint32_t x = reach.x0; x <= reach.x1; ++x) {
                ++rt_glow_counts_[std::size_t{y} * across + x];
            }
        }
    }
    // The lists stay within a bound (a cloud of sparks about the eye is in every tile): past it every other particle
    // goes and the one before it stands for both, until they fit.
    constexpr std::size_t kMostEntries = std::size_t{1} << 20u;
    std::size_t listed = 0u;
    for (const std::uint32_t count : rt_glow_counts_) {
        listed += count;
    }
    if (listed > kMostEntries) {
        std::size_t out = 0u;
        bool paired = false, gone = false;
        for (std::size_t e = 0; e < rt_emitters_.size(); ++e) {
            RtEmitter emitter = rt_emitters_[e];
            if (emitter.kind == 2u) {
                if (paired) {
                    paired = false;
                    gone = true;
                    continue;
                }
                paired = true;
                for (float& c : emitter.colour) {
                    c *= 2.0f;
                }
            }
            rt_emitters_[out++] = emitter;
        }
        rt_emitters_.resize(out);
        if (gone) {
            rt_emitter_bins(width, height, ray);
            return;
        }
    }
    rt_glow_tiles_.assign(std::size_t{across} * down * 2u, 0u);
    std::uint32_t entries = 0u;
    for (std::size_t t = 0; t < rt_glow_counts_.size(); ++t) {
        rt_glow_tiles_[t * 2u] = entries;
        entries += rt_glow_counts_[t];
    }
    rt_glow_entries_.assign(std::max<std::size_t>(entries, 1u), 0u);
    for (std::size_t e = 0; e < rt_emitters_.size(); ++e) {
        const Reach& reach = reaches[e];
        for (std::uint32_t y = reach.y0; y <= reach.y1 && reach.x0 <= reach.x1; ++y) {
            for (std::uint32_t x = reach.x0; x <= reach.x1; ++x) {
                const std::size_t t = std::size_t{y} * across + x;
                rt_glow_entries_[rt_glow_tiles_[t * 2u] + rt_glow_tiles_[t * 2u + 1u]++] = static_cast<std::uint32_t>(e);
            }
        }
    }
    // Past eight lights a tile's list starts with its four strongest (rt_lights.hlsl tries them for certain): each
    // light's luminance by its falloff at the tile's middle ray, as far from the eye as the light is (no depth here).
    std::vector<float> luminance(rt_emitters_.size());
    for (std::size_t e = 0; e < rt_emitters_.size(); ++e) {
        const RtEmitter& emitter = rt_emitters_[e];
        luminance[e] = 0.2126f * emitter.colour[0] + 0.7152f * emitter.colour[1] + 0.0722f * emitter.colour[2];
    }
    for (std::size_t t = 0; t < rt_glow_counts_.size(); ++t) {
        const std::uint32_t first = rt_glow_tiles_[t * 2u], count = rt_glow_tiles_[t * 2u + 1u];
        if (count <= 8u) {
            continue;
        }
        const float slope_x = (static_cast<float>(t % across) * kTile + 0.5f * kTile) * ray[0] + ray[1];
        const float slope_y = (static_cast<float>(t / across) * kTile + 0.5f * kTile) * ray[2] + ray[3];
        std::uint32_t best[4] = {0u, 0u, 0u, 0u};  // places in the list
        float strength[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
        for (std::uint32_t i = 0; i < count; ++i) {
            const RtEmitter& emitter = rt_emitters_[rt_glow_entries_[first + i]];
            const float depth = std::max(-emitter.centre[2], 1.0f);
            const float dx = emitter.centre[0] - slope_x * depth, dy = emitter.centre[1] - slope_y * depth;
            const float far2 = dx * dx + dy * dy, r2 = emitter.radius * emitter.radius;
            const float s = far2 > emitter.reach * emitter.reach ? 0.0f : luminance[rt_glow_entries_[first + i]] * r2 / (far2 + r2);
            for (unsigned k = 0; k < 4u; ++k) {
                if (s > strength[k]) {
                    for (unsigned m = 3u; m > k; --m) {
                        strength[m] = strength[m - 1u];
                        best[m] = best[m - 1u];
                    }
                    strength[k] = s;
                    best[k] = i;
                    break;
                }
            }
        }
        // To the front, in order: a swap each; a later best whose place was swapped away follows it.
        for (unsigned k = 0; k < 4u; ++k) {
            const std::uint32_t from = best[k];
            std::swap(rt_glow_entries_[first + k], rt_glow_entries_[first + from]);
            for (unsigned m = k + 1u; m < 4u; ++m) {
                if (best[m] == k) {
                    best[m] = from;
                }
            }
        }
    }
}

// A slot's instances as one box in the eye's space.
void RenderCore::rt_caster(std::uint32_t slot, const float centre[3], float radius) {
    if (!std::isfinite(radius) || !std::isfinite(centre[0] + centre[1] + centre[2])) {
        return;
    }
    const auto [found, fresh] = rt_casters_.try_emplace(slot);
    RtCaster& box = found->second;
    for (unsigned r = 0; r < 3u; ++r) {
        box.low[r] = fresh ? centre[r] - radius : std::min(box.low[r], centre[r] - radius);
        box.high[r] = fresh ? centre[r] + radius : std::max(box.high[r], centre[r] + radius);
    }
}

// A slot's body (the landing cue's, l80): its meshes' boxes as one, `skin` wider; its reach (rt_caster) comes first.
void RenderCore::rt_caster_body(std::uint32_t slot, const float low[3], const float high[3], float skin, bool player) {
    const auto found = rt_casters_.find(slot);
    if (found == rt_casters_.end() || !std::isfinite(low[0] + low[1] + low[2] + high[0] + high[1] + high[2])) {
        return;
    }
    RtCaster& box = found->second;
    for (unsigned r = 0; r < 3u; ++r) {
        box.body_low[r] = box.body ? std::min(box.body_low[r], low[r] - skin) : low[r] - skin;
        box.body_high[r] = box.body ? std::max(box.body_high[r], high[r] + skin) : high[r] + skin;
    }
    box.body = true;
    box.player = box.player || player;
}

// The frame's drop shadows the tracer takes: those whose caster's draws the scene holds. Each becomes a line in the
// eye's space, from the game's base along the drop, as wide as the caster reaches about it; its volume's draws are
// left out of this walk.
void RenderCore::rt_drops_collect(const NebulaFramePass& begin) {
    const NebulaFrameList& list = *walk_.now->list;
    if (list.shadow_drop_count == 0u) {
        return;
    }
    const Affine34& view = walk_cameras_[begin.scene_camera];
    for (std::uint32_t d = 0; d < list.shadow_drop_count; ++d) {
        const NebulaFrameShadowDrop& drop = list.shadow_drops[d];
        const auto caster = rt_casters_.find(drop.caster);
        const float size = std::sqrt(drop.direction[0] * drop.direction[0] + drop.direction[1] * drop.direction[1] +
                                     drop.direction[2] * drop.direction[2]);
        if (drop.caster == 0u || caster == rt_casters_.end() || !(size > 1e-6f) || !(drop.length > 0.0f) || !std::isfinite(drop.length)) {
            continue;  // the game's volume stays
        }
        RtDrop& out = rt_drops_.emplace_back();
        for (unsigned r = 0; r < 3u; ++r) {
            out.base[r] = view[r * 4u] * drop.position[0] + view[r * 4u + 1u] * drop.position[1] + view[r * 4u + 2u] * drop.position[2] +
                          view[r * 4u + 3u];
            out.direction[r] =
                (view[r * 4u] * drop.direction[0] + view[r * 4u + 1u] * drop.direction[1] + view[r * 4u + 2u] * drop.direction[2]) / size;
        }
        // The line starts where the caster's box does (seen along the drop) so the whole caster stands on it, and is as
        // wide as the box reaches from it.
        const RtCaster& box = caster->second;
        float centre[3], half = 0.0f, along = 0.0f;
        for (unsigned r = 0; r < 3u; ++r) {
            centre[r] = (box.low[r] + box.high[r]) * 0.5f;
            half += (box.high[r] - box.low[r]) * (box.high[r] - box.low[r]) * 0.25f;
            along += (centre[r] - out.base[r]) * out.direction[r];
        }
        half = std::sqrt(half);
        float aside = 0.0f;
        for (unsigned r = 0; r < 3u; ++r) {
            const float off = centre[r] - out.base[r] - along * out.direction[r];
            aside += off * off;
        }
        const float back = std::max(0.0f, half - along);  // how far the caster reaches behind the game's base
        for (unsigned r = 0; r < 3u; ++r) {
            out.base[r] -= out.direction[r] * back;
        }
        out.length = drop.length + back;
        out.radius = std::sqrt(aside) + half;
        out.caster = drop.caster;
        // The landing cue (l80; rt_light_space.hlsl): the player's drop (every one with landing_all) as a line from the
        // body's centre along the drop to the game's far end; reserved[0] bit 0 the cue, [1] the body's half reach
        // along the drop, [2] its radius across it (for a square cross-section its half side); radius covers the cue's
        // widest edge (the drop light's 2 degrees at the far end, g_stage.w), for the tiles.
        if (box.body && (box.player || rt_dev(RtDev::LandingAll, 0.0f) > 0.5f)) {
            constexpr float kCueTan = 0.035f;  // rt_tables kDropTan
            float body[3], half_along = 0.0f, across = 0.0f, ends = out.length;
            for (unsigned r = 0; r < 3u; ++r) {
                body[r] = (box.body_low[r] + box.body_high[r]) * 0.5f;
                const float reach = (box.body_high[r] - box.body_low[r]) * 0.5f;
                half_along += std::fabs(out.direction[r]) * reach;
                across += reach * reach * (1.0f - out.direction[r] * out.direction[r]);
                ends += (out.base[r] - body[r]) * out.direction[r];
            }
            if (std::isfinite(ends) && ends > half_along) {
                const float wide = std::sqrt(std::max(across * 0.5f, 0.0f));
                std::memcpy(out.base, body, sizeof(body));
                out.length = ends;
                out.radius = wide + ends * kCueTan + 1.0f;
                out.reserved[0] = 1u;
                out.reserved[1] = std::bit_cast<std::uint32_t>(half_along);
                out.reserved[2] = std::bit_cast<std::uint32_t>(wide);
            }
        }
        if (drop.volume != 0u) {
            rt_drop_volumes_.insert(drop.volume);
        }
    }
}

// An image of the scene colour's format and size that a pass fills and the renderer copies over the scene.
std::uint32_t RenderCore::rt_scene_image(rt::ImageFormat format, unsigned width, unsigned height) {
    std::erase_if(rt_view_retired_, [&](const std::pair<std::uint64_t, std::uint32_t>& retired) {
        if (retired.first + rt::kFramesToRelease > frame_index_) {
            return false;
        }
        rt_backend_.free_image(retired.second);
        return true;
    });
    if (rt_view_image_ == rt::kNoImage || rt_view_width_ != width || rt_view_height_ != height || rt_view_format_ != format) {
        if (rt_view_image_ != rt::kNoImage) {
            rt_view_retired_.emplace_back(frame_index_, rt_view_image_);
        }
        rt_view_image_ = rt_backend_.make_image(format, width, height, "Nebula RT Scene Image");
        rt_view_width_ = width;
        rt_view_height_ = height;
        rt_view_format_ = format;
    }
    return rt_view_image_;
}

// The drops by the screen tiles their reach covers (render_core.h): the boxes about a drop's two ends, through the
// projection; a drop that comes nearer than the eye's plane covers the screen.
void RenderCore::rt_drop_bins(unsigned width, unsigned height, const float ray[4]) {
    constexpr std::uint32_t kTile = 16u;
    const std::uint32_t across = (width + kTile - 1u) / kTile, down = (height + kTile - 1u) / kTile;
    rt_tiles_across_ = across;
    rt_tiles_down_ = down;
    rt_tile_counts_.assign(std::size_t{across} * down, 0u);
    struct Reach {
        std::uint32_t x0, y0, x1, y1;
    };
    std::vector<Reach> reaches(rt_drops_.size());
    for (std::size_t d = 0; d < rt_drops_.size(); ++d) {
        const RtDrop& drop = rt_drops_[d];
        if ((drop.reserved[0] & 1u) == 0u) {
            reaches[d] = {1u, 1u, 0u, 0u};  // the landing cue's alone are read (l80): no tile
            continue;
        }
        float low_x = 1e30f, low_y = 1e30f, high_x = -1e30f, high_y = -1e30f;
        bool behind = false;
        for (unsigned end = 0; end < 2u; ++end) {
            const float along = end == 0u ? 0.0f : drop.length;
            const float c[3] = {drop.base[0] + drop.direction[0] * along, drop.base[1] + drop.direction[1] * along,
                                drop.base[2] + drop.direction[2] * along};
            const float nearest = -c[2] - drop.radius;
            if (!(nearest > 1.0f)) {
                behind = true;
                break;
            }
            for (unsigned corner = 0; corner < 8u; ++corner) {
                const float x = c[0] + ((corner & 1u) != 0u ? drop.radius : -drop.radius);
                const float y = c[1] + ((corner & 2u) != 0u ? drop.radius : -drop.radius);
                const float w = (corner & 4u) != 0u ? nearest : -c[2] + drop.radius;
                const float px = (x / w - ray[1]) / ray[0];
                const float py = (y / w - ray[3]) / ray[2];
                low_x = std::min(low_x, px);
                high_x = std::max(high_x, px);
                low_y = std::min(low_y, py);
                high_y = std::max(high_y, py);
            }
        }
        Reach& reach = reaches[d];
        if (behind) {
            reach = {0u, 0u, across - 1u, down - 1u};
        } else if (high_x < 0.0f || high_y < 0.0f || low_x >= static_cast<float>(width) || low_y >= static_cast<float>(height)) {
            reach = {1u, 1u, 0u, 0u};  // off screen: no tile
        } else {
            reach.x0 = static_cast<std::uint32_t>(std::max(low_x, 0.0f)) / kTile;
            reach.y0 = static_cast<std::uint32_t>(std::max(low_y, 0.0f)) / kTile;
            reach.x1 = std::min(static_cast<std::uint32_t>(std::min(high_x, static_cast<float>(width - 1u))) / kTile, across - 1u);
            reach.y1 = std::min(static_cast<std::uint32_t>(std::min(high_y, static_cast<float>(height - 1u))) / kTile, down - 1u);
        }
        for (std::uint32_t y = reach.y0; y <= reach.y1 && reach.x0 <= reach.x1; ++y) {
            for (std::uint32_t x = reach.x0; x <= reach.x1; ++x) {
                ++rt_tile_counts_[std::size_t{y} * across + x];
            }
        }
    }
    rt_tiles_.assign(std::size_t{across} * down * 2u, 0u);
    std::uint32_t entries = 0u;
    for (std::size_t t = 0; t < rt_tile_counts_.size(); ++t) {
        rt_tiles_[t * 2u] = entries;
        entries += rt_tile_counts_[t];
    }
    rt_tile_entries_.assign(std::max<std::size_t>(entries, 1u), 0u);
    for (std::size_t d = 0; d < rt_drops_.size(); ++d) {
        const Reach& reach = reaches[d];
        for (std::uint32_t y = reach.y0; y <= reach.y1 && reach.x0 <= reach.x1; ++y) {
            for (std::uint32_t x = reach.x0; x <= reach.x1; ++x) {
                const std::size_t t = std::size_t{y} * across + x;
                rt_tile_entries_[rt_tiles_[t * 2u] + rt_tiles_[t * 2u + 1u]++] = static_cast<std::uint32_t>(d);
            }
        }
    }
}

}  // namespace galaxy::render
