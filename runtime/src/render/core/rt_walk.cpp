// SPDX-License-Identifier: GPL-3.0-only
// RenderCore's part of ray tracing: when the tracer runs, which meshes it holds structures for, and what a frame
// builds (render/rt holds the policy, rt_backend.h the API).
#include "galaxy/render/render_core.h"

#include "galaxy/launch_args.h"
#include "galaxy/render/ambient_occlusion_shape.h"
#include "galaxy/render/program_table.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/rt_caps.h"
#include "render/core/pipelines.h"
#include "render/core/world_lights.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace galaxy::render {

using core::kRowsPerMatrix;

namespace {

#include "render/rt/rt_effect_lights.inc"
#include "render/rt/rt_blue_noise.inc"

// A particle effect's luminance (candela a square metre of what it draws), by the name its draws carry: -1 for a
// name that is not one of the game's own effects (a mod's, another archive's).
float effect_luminance(std::uint32_t name) noexcept {
    const RtEffectLight* const end = std::end(kRtEffectLights);
    const RtEffectLight* const at =
        std::lower_bound(std::begin(kRtEffectLights), end, name, [](const RtEffectLight& e, std::uint32_t n) { return e.name < n; });
    return at != end && at->name == name ? static_cast<float>(at->luminance) : -1.0f;
}

// 100 units of the game are a metre (the player is 1.6 m tall).
constexpr float kUnitsPerMetre = 100.0f;

// What an emitter is from one frame to the next (its kind, whose it is, which of them): a pixel's reservoir finds its
// light again by it. Two emitters with one number are taken for each other, which costs a little noise and nothing
// else.
constexpr std::uint32_t rt_emitter_id(std::uint32_t kind, std::uint32_t whose, std::uint32_t which) {
    std::uint32_t h = (whose * 0x9E3779B1u) ^ (which * 0x85EBCA6Bu) ^ (kind * 0xC2B2AE35u);
    h ^= h >> 15u;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12u;
    return h;
}

// The words of an item in the light pass's table (rt_light).
constexpr std::size_t kRtItemWords = 16u;

// The sky's dome among the sky's draws: what a mirror shows where nothing of the scene stands.
constexpr bool item_is_sky_dome(const NebulaFrameItem& item) noexcept {
    const std::uint32_t category = (item.flags & NEBULA_ITEM_CATEGORY_MASK) >> NEBULA_ITEM_CATEGORY_SHIFT;
    return category == NEBULA_CATEGORY_SKY || category == NEBULA_CATEGORY_ASTRO_DOME_SKY;
}

// What the game drew the item as (NEBULA_CATEGORY_*).
constexpr std::uint32_t item_category(const NebulaFrameItem& item) noexcept {
    return (item.flags & NEBULA_ITEM_CATEGORY_MASK) >> NEBULA_ITEM_CATEGORY_SHIFT;
}

// The sky and what is drawn with it: around the scene, not in it. It stops no light and takes none of the scene's.
constexpr bool item_is_sky(const NebulaFrameItem& item) noexcept {
    const std::uint32_t category = item_category(item);
    return category == NEBULA_CATEGORY_SKY || category == NEBULA_CATEGORY_AIR || category == NEBULA_CATEGORY_SUN ||
           category == NEBULA_CATEGORY_ASTRO_DOME_SKY;
}

}  // namespace

// Ray tracing starts with the device when the setting is on and the device can trace (rt_caps.h). A change of the
// setting takes a new device (Vulkan makes its device with the tracer's extensions or without them).
void RenderCore::rt_start() {
    if (!get_render_config().ray_tracing_enabled || !rt_capable(rt_caps())) {
        return;
    }
    if (!rt_backend_.start(renderer_.device())) {
        std::fprintf(stderr, "[rt] the device did not start the tracer: ray tracing is off\n");
        return;
    }
    registries_.tell_retired_meshes(true);
    rt_log_at_ = 0u;
    rt_building_ = false;
}

// Before the device's memory goes (it is lost: nothing recorded runs any more).
void RenderCore::rt_stop() noexcept {
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
    rt_light_on_ = false;
    rt_prepass_walk_ = false;
    rt_skin_buffer_ = rt::kNoBuffer;
    rt_skin_bytes_ = 0u;
    rt_skin_retired_.clear();
    rt_drop_volumes_.clear();
    rt_sun_known_ = false;
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

// Once a frame, before anything is drawn: the builds the frame can afford. With a scene on screen a frame builds a few
// thousand triangles; a frame with none (a loading screen) builds eight times as many.
void RenderCore::rt_frame(const NebulaFrameList& list) {
    constexpr std::uint64_t kSceneTriangles = 8000u;
    const std::uint64_t budget = (list.flags & NEBULA_FRAME_HAS_SCENE) != 0u ? kSceneTriangles : kSceneTriangles * 8u;
    rt_backend_.frame(renderer_.frame_slot());
    rt_geometry_.frame(renderer_.rt_commands_begin().commands, renderer_.frame_slot(), frame_index_, budget);
    renderer_.rt_commands_end();
    const rt::GeometryStats& stats = rt_geometry_.stats();
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
        std::fprintf(stderr,
                     "[rt] structures: %u built (%u compacted), %u waiting, %llu triangles, %.2f MB in %.2f MB of pages, most in a frame "
                     "%llu triangles\n",
                     stats.structures, stats.compacted, stats.waiting, static_cast<unsigned long long>(stats.triangles),
                     static_cast<double>(stats.structure_bytes) / 1048576.0, static_cast<double>(stats.page_bytes) / 1048576.0,
                     static_cast<unsigned long long>(stats.most_built_triangles));
    }
}

// The scene of this walk: every draw of the scene's passes that a ray can meet, as an instance in the scene camera's
// eye space (the rows the walk made), and the structure over them, built before the scene's first draw.
void RenderCore::rt_scene(const NebulaFramePass& begin) {
    const core::ListData& now = *walk_.now;
    const NebulaFrameList& list = *now.list;
    rt_scene_.begin();
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
            for (unsigned r = 0; r < 3u; ++r) {
                emitter.centre[r] = eye[r * 4u] * light.position[0] + eye[r * 4u + 1u] * light.position[1] + eye[r * 4u + 2u] * light.position[2] +
                                    eye[r * 4u + 3u];
                emitter.colour[r] = light.color[r] * std::min(candela, 5000.0f);
            }
            emitter.radius = 25.0f;
            emitter.reach = 0.0f;
            emitter.slot = NEBULA_NONE32;
            emitter.kind = 0u;
            emitter.reserved[1] = rt_emitter_id(0u, 0u, n);
        }
    }
    rt_emitter_blocks_.clear();
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
    std::uint64_t skin_bytes = 0u;
    const auto first = static_cast<std::uint32_t>(&begin - list.passes) + 1u;
    bool early = true;  // before the scene's opaque end
    for (std::uint32_t p = first; p < list.pass_count && list.passes[p].kind != NEBULA_PASS_SCENE_END; ++p) {
        const NebulaFramePass& pass = list.passes[p];
        early = early && pass.kind != NEBULA_PASS_OPAQUE_END;
        if (pass.kind != NEBULA_PASS_DRAW || pass.layer != NEBULA_LAYER_SCENE || pass.target != 0u) {
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
                 (registries_.mesh(item.mesh).hit_flags & NEBULA_HIT_DRAWN_SHADOW) != 0u)) {
                ++count.flat;
                continue;
            }
            const core::PipelineEntry& pipeline = registries_.pipeline(item.pipeline);
            const std::uint32_t state = pipeline.desc.state;
            const bool depth_test = (state & 1u << 10u) != 0u;
            const bool depth_write = (state & 1u << 14u) != 0u;
            const std::uint32_t cull = state >> 15u & 3u;
            // No surface of the scene: drawn flat (no depth test), without colour, or as the sky around it (but its dome).
            if (((!depth_test || item_is_sky(item)) && !sky) || (state & NEBULA_STATE_COLOR_WRITE) == 0u || cull == NEBULA_CULL_ALL) {
                ++count.flat;
                continue;
            }
            const rt::Faces faces = sky                        ? rt::Faces::Both
                                    : cull == NEBULA_CULL_BACK ? rt::Faces::Front
                                    : cull == NEBULA_CULL_FRONT ? rt::Faces::Back
                                                                : rt::Faces::Both;
            // A pixel program that can discard has a per-sample form (compile_programs.py): an alpha-tested surface.
            const std::uint32_t kind = !sky && program_variant(pipeline.desc.pixel, 0u, 1u) == kProgramSample ? rt::kKindCutout : 0u;
            const std::uint32_t mask =
                sky ? rt::kMaskSky
                : depth_write && (state & 3u) == 0u
                    // What stops light is a surface of the scene's opaque phases; an opaque draw made after them (a
                    // fill, a silhouette, a helper shape) is seen by the views and casts nothing.
                    ? rt::kMaskOpaque | (early ? rt::kMaskEarly : 0u) |
                          (kind == 0u && early ? rt::kMaskSolid | (program_lit(pipeline.desc.pixel) ? rt::kMaskLit : 0u) : 0u) |
                          (kind != 0u && early ? rt::kMaskCutout : 0u)
                    : rt::kMaskBlended;
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
                RtRing& ring = rt_rings_[(mask & rt::kMaskEarly) != 0u ? 0u : (mask & rt::kMaskOpaque) != 0u ? 1u : 2u];
                if (ring.positions.empty()) {
                    ring.item = i;
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
                    ++count.waiting;
                    continue;
                }
                const std::uint32_t matrix = one_matrix ? item.matrix : 0u;
                const auto [seen, fresh] =
                    rt_seen_.try_emplace(RtPlace{key, item.block, matrix}, static_cast<std::uint32_t>(rt_scene_.instances().size()));
                if (!fresh) {
                    rt_scene_.also(seen->second, mask);
                    ++count.repeats;
                    continue;
                }
                rt_scene_.add(walk_rows_cpu_ + walk_block_rows_[item.block] + matrix * kRowsPerMatrix, i, mask, structure, faces, kind);
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
                ++count.waiting;
                continue;
            }
            const std::uint32_t matrix = one_matrix || mesh.single_matrix != NEBULA_MODEL_NONE ? item.matrix : 0u;
            const auto [seen, fresh] =
                rt_seen_.try_emplace(RtPlace{key, item.block, matrix}, static_cast<std::uint32_t>(rt_scene_.instances().size()));
            if (!fresh) {
                rt_scene_.also(seen->second, mask);
                ++count.repeats;
                continue;
            }
            const float (*place)[4] = walk_rows_cpu_ + walk_block_rows_[item.block] + matrix * kRowsPerMatrix;
            rt_scene_.add(place, i, mask, structure, faces, kind);
            count.triangles += triangles;
            if ((item.slot != 0u && list.shadow_drop_count != 0u) || mesh.glow[3] != 0.0f) {
                // The mesh's box through its matrix: for the drop shadow its object may cast and the light it may give.
                float centre[3], radius = 0.0f;
                for (unsigned r = 0; r < 3u; ++r) {
                    float extent = 0.0f;
                    centre[r] = place[r][3];
                    for (unsigned c = 0; c < 3u; ++c) {
                        centre[r] += place[r][c] * (mesh.min[c] + mesh.max[c]) * 0.5f;
                        extent += std::fabs(place[r][c]) * (mesh.max[c] - mesh.min[c]) * 0.5f;
                    }
                    radius += extent * extent;
                }
                if (item.slot != 0u && list.shadow_drop_count != 0u) {
                    rt_caster(item.slot, centre, std::sqrt(radius));
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
    static constexpr std::uint32_t kRingMasks[3] = {rt::kMaskOpaque | rt::kMaskEarly | rt::kMaskSolid, rt::kMaskOpaque, rt::kMaskBlended};
    for (unsigned kind = 0; kind < 3u; ++kind) {
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
    for (unsigned kind = 0; kind < 3u; ++kind) {
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
    // The second layer: a blended surface whose mesh the light pass reads (its place comes from its triangle).
    const auto i = static_cast<std::size_t>(&item - now.list->items);
    return core::blended_prepass_draws(state) && (item.flags & NEBULA_ITEM_RING) == 0u &&
           i * kRtItemWords + 2u < rt_item_words_.size() && rt_item_words_[i * kRtItemWords + 2u] < rt::kPassMeshArenas;
}

// The second layer: the scene's blended surfaces take the traced light too. At the opaque phases' end the item target
// is drawn again, by the blended items whose mesh lies in the pass's memory (the last drawn names the pixel; the
// scene's depth stands, unwritten), and the frame's passes run over them again (layer 1, the frame's tables and
// constants as they are): a pixel's place is where its eye ray meets its triangle, its rays go straight (no average
// over time follows) and the pictures the opaque draws read, done with, take the result. The blended draws then read
// them in their traced form.
void RenderCore::rt_blended(const NebulaFramePass& end) {
    if (!rt_light_on_ || rt_light_frame_.across == 0u || rt_scene_view_ == NEBULA_NONE32) {
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
            rt_blend_filters();
            frame.layer = 0u;
            rt_blend_walk_before_ = walk_.serial;
        }
        renderer_.rt_light_end();
        // The draws read the pictures again.
        (void)rt_backend_.image_to_sample(rt_image(RtImage::Light));
        (void)rt_backend_.image_to_sample(rt_image(RtImage::Added));
        (void)rt_backend_.image_to_sample(rt_image(RtImage::Mirror));
    }
    renderer_.rt_commands_end();
}

// The prepass and the light pass, before the scene's first draw. Without them (no scene this walk, a multisampled
// target, no memory) the scene draws as ever and no light is traced.
void RenderCore::rt_prepass(const NebulaFramePass& begin) {
    const NebulaFrameList& list = *walk_.now->list;
    rt_light_on_ = false;
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
constexpr std::uint8_t kForLight = 1u, kForIndirect = 2u, kForReflect = 4u, kForVolume = 8u, kForTransmit = 16u, kForBlend = 32u;
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
    {"Nebula RT Light Step", kHalf4, kRtFull, kForLight},
    {"Nebula RT Light Average A", kHalf4, kRtFull, kForLight},
    {"Nebula RT Light Average B", kHalf4, kRtFull, kForLight},
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
    {"Nebula RT Reflect Raw", kHalf4, kRtFull, kForReflect},
    {"Nebula RT Reflect Average A", kHalf4, kRtFull, kForReflect},
    {"Nebula RT Reflect Average B", kHalf4, kRtFull, kForReflect},
    {"Nebula RT Reflect", kHalf4, kRtFull, kForReflect},
    {"Nebula RT Transmit", kHalf4, kRtFull, kForTransmit},
    {"Nebula RT Volume A", kHalf4, kRtVolume, kForVolume},
    {"Nebula RT Volume B", kHalf4, kRtVolume, kForVolume},
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
                  kRtPassTransmit == kForTransmit && kRtPassBlend == kForBlend);
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
    rt::PassBinds binds = rt_pass_binds();
    binds.reads[0] = rt_layer_guide();
    binds.reads[1] = rt_image(RtImage::Normal);
    binds.reads[2] = rt_image(RtImage::Motion);
    binds.reads[3] = rt_image(RtImage::SunOut);
    binds.reads[4] = rt_image(RtImage::Material);
    binds.reads[5] = rt_image(RtImage::LightsSpec);
    binds.writes[0] = rt_image(RtImage::Light);
    binds.writes[1] = rt_image(RtImage::Mirror);
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
    // The sun's and the sky's shares.
    const std::uint32_t light = rt_image(RtImage::Light), light_step = rt_image(RtImage::LightStep);
    const std::uint32_t average = rt_now(RtImage::LightAverageA);
    rt::PassBinds over_time;
    over_time.constants = frame.binds.constants;
    over_time.pass_constants = frame.pass;
    over_time.reads[0] = light;
    over_time.reads[1] = rt_before(RtImage::LightAverageA);
    over_time.reads[2] = guide;
    over_time.reads[3] = guide_before;
    over_time.reads[4] = motion;
    over_time.writes[0] = average;
    rt_backend_.dispatch(RtShader::LightTime, over_time, frame.across, frame.down);
    for (std::uint32_t step = 0u; step < 3u; ++step) {
        rt::PassBinds over_space;
        over_space.constants = frame.binds.constants;
        over_space.pass_constants = steps[step];
        over_space.reads[0] = step == 0u ? average : step == 1u ? light : light_step;
        over_space.reads[2] = guide;
        over_space.reads[3] = step == 2u ? motion : rt::kNoImage;  // the last step's, for --diagnostics rt-motion
        over_space.writes[0] = step == 1u ? light_step : light;
        rt_backend_.dispatch(RtShader::LightSpace, over_space, frame.across, frame.down);
    }
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
        rt_backend_.dispatch(RtShader::LightsSpace, over_space, frame.across, frame.down);
    }
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
        if (pass.kind != NEBULA_PASS_DRAW || pass.layer != NEBULA_LAYER_SCENE) {
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
                rt_item_words_[i * kItemWords + 1u] = (program_sums_key(pipeline.desc.pixel) ? 1u : 0u) |
                                                      (pass.phase != kPhaseNoShadow ? 2u : 0u) | 4u | (thin ? 8u : 0u) |
                                                      (lamp ? (item.light_set + 1u) << 8u : 0u);
            }
            // Its mesh, for a pixel of it and for a ray that meets it.
            if ((item.flags & (NEBULA_ITEM_RING | NEBULA_ITEM_INSTANCES)) != 0u) {
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
            words[11] = std::uint32_t{mesh.hit_class} | std::uint32_t{mesh.hit_flags} << 8u | byte_of(mesh.hit_roughness) << 16u |
                        byte_of(mesh.hit_metal) << 24u;
            words[12] = byte_of(mesh.hit_base[0]) | byte_of(mesh.hit_base[1]) << 8u | byte_of(mesh.hit_base[2]) << 16u |
                        byte_of(mesh.hit_environment) << 24u;
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
    constexpr float kSunTan = 0.014f;    // 0.8 of a degree: three times the real sun's, for a penumbra that reads soft
    constexpr float kDropTan = 0.035f;   // the drop light's 2 degrees (decision D1)
    constexpr float kSkyReach = 260.0f;   // how far the sky's ray looks for what stands near: a little over the player's height
    constexpr float kSkyInShadow = 0.5f;  // what the sun's shadow keeps of the sky's light: the sky turned down (the
                                          // view's tint gives back up to 0.3 of the rest in the sky's colour)
    constexpr float kFullFacing = 0.6f;  // a painted surface keeps its authored brightness from this n.l up
    const float* const sun = rt_sun_eye_;
    const bool sunned = rt_sun_now_;
    float ambient = 0.0f, key = 0.0f;
    if (list.stage_light_known != 0u) {
        if (const NebulaFrameWorldLight* light = core::find_world_light(list, list.stage_light); light != nullptr) {
            ambient = 0.2126f * list.stage_ambient[0] + 0.7152f * list.stage_ambient[1] + 0.0722f * list.stage_ambient[2];
            key = 0.2126f * light->color[0] + 0.7152f * light->color[1] + 0.0722f * light->color[2];
        }
    }
    // The sky's colour in shadow: the stage's ambient colour, its strongest part as one, by a share. A
    // shadow on a blue day is blue, under a red sky red; a stage that names none takes nothing back.
    constexpr float kSkyTint = 0.2f;
    rt_tint_[0] = rt_tint_[1] = rt_tint_[2] = 0.0f;
    if (list.stage_light_known != 0u && ambient > 0.02f) {
        const float most = std::max({list.stage_ambient[0], list.stage_ambient[1], list.stage_ambient[2]});
        // Told apart from grey before it counts: a grey ambient would only lift the shadow.
        const float least = std::min({list.stage_ambient[0], list.stage_ambient[1], list.stage_ambient[2]});
        const float coloured = most > 0.0f ? std::min(1.0f, (most - least) / most * 4.0f) : 0.0f;
        for (unsigned c = 0; c < 3u; ++c) {
            rt_tint_[c] = kSkyTint * (0.35f + 0.65f * coloured) * list.stage_ambient[c] / most;
        }
    }
    if (!(ambient > 0.02f) || !(key > 0.02f)) {
        ambient = key = 0.5f;  // a stage that names no light of its own: an even split
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
    const std::size_t item_bytes = rt_item_words_.size() * sizeof(std::uint32_t);
    const std::size_t set_bytes = rt_set_words_.size() * sizeof(float);
    static_assert(sizeof(RtDrop) == 48u && sizeof(RtEmitter) == 48u);
    // The stage's exposure: the light a fully lit surface of it stands in, from its own key and ambient: 40 lux
    // in the dark of space (a glow carries far), 150 on a bright day (a glow is a touch of colour near it).
    rt_emitters_finish(40.0f + 110.0f * std::clamp((ambient + key) / 1.2f, 0.0f, 1.0f));
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
    const UploadRing::Allocation items_at = ring.allocate(item_bytes, 256u);
    const UploadRing::Allocation sets_at = ring.allocate(set_bytes, 256u);
    const UploadRing::Allocation drops_at = ring.allocate(drop_bytes, 256u);
    const UploadRing::Allocation tiles_at = ring.allocate(tile_words * sizeof(std::uint32_t), 256u);
    std::memcpy(items_at.cpu, rt_item_words_.data(), item_bytes);
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
        // reservoir names its light by the list of the frame it was written in.
        rt_emitter_now_.clear();
        for (std::size_t e = 0; e < rt_emitters_.size(); ++e) {
            rt_emitter_now_.try_emplace(rt_emitters_[e].reserved[1], static_cast<std::uint32_t>(e));
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
    binds.buffers[0] = rt_backend_.view_of(items_at.gpu, item_bytes, constants.at[0]);
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
    constants.sun[3] = kSunTan;
    constants.stage[0] = ambient;
    constants.stage[1] = key;
    constants.stage[2] = 128.0f / 255.0f;  // the game's fill: black at alpha 0x80 (MR::fillSilhouetteColor)
    constants.stage[3] = kDropTan;
    constants.look[0] = kSkyInShadow;
    constants.look[2] = kFullFacing;
    constants.look[3] = kSkyReach;
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
    constants.more[2] = (sunned ? 1u : 0u) | 8u | 32u | 64u;
    std::memcpy(constants.up, sky_up_, sizeof(sky_up_));
    // For a pixel whose mesh the pass cannot read: the camera's own move since the frame before (none where there is
    // no frame before: the filter starts anew there anyway).
    Affine34 before_from_now = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    std::memcpy(constants.ray_before, frame.ray, sizeof(constants.ray_before));
    if (frame.history) {
        before_from_now = affine_multiply(rt_light_view_before_, world_from_now);
        std::memcpy(constants.ray_before, rt_light_ray_before_, sizeof(constants.ray_before));
        constants.kept[2] = 1u;  // the filters' history and the frame before's reservoirs count
    }
    std::memcpy(constants.before, before_from_now.data(), sizeof(constants.before));
    if (::galaxy::diagnosing(::galaxy::kDiagnoseRtAdded)) {
        // The scene under its emitters alone: no sun, no sky, no mirror, no tint.
        constants.more[2] = (constants.more[2] & ~32u) | 128u;
        rt_tint_[0] = rt_tint_[1] = rt_tint_[2] = 0.0f;
    }
    if (::galaxy::diagnosing(::galaxy::kDiagnoseRtMotion)) {
        // Each point's motion in place of the light (shown the way rt-light shows it), nothing added over it.
        constants.more[2] |= 512u;
        constants.glow[3] = 0u;
        rt_tint_[0] = rt_tint_[1] = rt_tint_[2] = 0.0f;
    }
    // A ray that leaves the scene and meets no sky (a stage without a dome): the stage's ambient colour.
    for (unsigned c = 0; c < 3u; ++c) {
        constants.sky[c] = list.stage_light_known != 0u ? list.stage_ambient[c] : 0.25f;
    }
    static_assert(sizeof(RtFrameConstants) <= 512u && offsetof(RtFrameConstants, more) == 64u);
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
        const std::uint32_t wanted = kRtPassLight | (rt_pass_indirect_on() ? kRtPassIndirect : 0u) |
                                     (rt_pass_reflect_on() ? kRtPassReflect : 0u) | (rt_pass_volume_on() ? kRtPassVolume : 0u) |
                                     (rt_blend_seen_ ? kRtPassBlend : 0u);
        rt_light_wanted_ = wanted & ~kRtPassBlend;
        ao::Constants eye{};
        bool lit = false;
        if (rt_images(width, height, wanted) && ao::make_constants(camera, width, height, 1u, false, eye)) {
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
            rt_noise_fill();
            rt_pass_surface();
            rt_pass_sun();
            rt_sun_filter();
            rt_pass_lights();
            rt_pass_indirect();
            rt_pass_light();
            rt_pass_reflect();
            rt_pass_volume();
            rt_backend_.end_counters();
            rt_filters();
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
            if (void* const texture = rt_backend_.image_to_sample(rt_image(RtImage::Light));
                texture != nullptr && added != nullptr && mirror != nullptr) {
                rt_light_slot_ = renderer_.rt_publish_light(texture, texture_store_);
                rt_added_slot_ = renderer_.rt_publish_light(added, texture_store_, 1u);
                rt_mirror_slot_ = renderer_.rt_publish_light(mirror, texture_store_, 2u);
                rt_light_on_ = true;
                view_flags_ |= 8u | (::galaxy::diagnosing(::galaxy::kDiagnoseRtLight | ::galaxy::kDiagnoseRtMotion) ? 16u : 0u);
                ++walk_view_stamp_;  // the scene's views are written again, with the light
            }
        } else {
            rt_light_frame_.across = 0u;  // no second layer either
        }
    }
    renderer_.rt_commands_end();
    rt_report();
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
                 "in the sun's shadow %.0f, under a drop %.0f, in a lamp's shadow %.0f; emitters %zu now; cutout tests %.0f, "
                 "cutouts unread %.0f, mirror rays %.0f; mesh arenas %u, items without %u, items of many matrices %u\n",
                 rt_light_frames_, static_cast<double>(rt_light_sum_[5]) / n, static_cast<double>(rt_light_sum_[0]) / n,
                 static_cast<double>(rt_light_sum_[1]) / n, static_cast<double>(rt_light_sum_[2]) / n,
                 static_cast<double>(rt_light_sum_[3]) / n, static_cast<double>(rt_light_sum_[4]) / n, rt_emitters_.size(),
                 static_cast<double>(rt_light_sum_[6]) / n, static_cast<double>(rt_light_sum_[7]) / n,
                 static_cast<double>(rt_light_sum_[8]) / n, rt_light_arenas_,
                 rt_light_unplaced_, rt_light_skinned_);
    unsigned kinds[3]{};
    for (const RtEmitter& e : rt_emitters_) {
        ++kinds[std::min<std::uint32_t>(e.kind, 2u)];
    }
    std::fprintf(stderr, "[rt-inputs]   emitters now: %u point lights of the game, %u glowing meshes, %u particles\n", kinds[0],
                 kinds[1], kinds[2]);
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

// The scene is over: what is drawn after it reads no traced light.
void RenderCore::rt_light_end() noexcept {
    if (rt_light_on_) {
        rt_light_on_ = false;
        view_flags_ &= ~24u;
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
    float up[3]{};
    const bool up_known = list.sky_up_known != 0u;
    if (up_known) {
        to_world(sky_up_, up);
    }
    float target[3]{};
    bool known = false, follows = false;
    // The sun the player sees, where the stage has one: `position` is the unit direction from the camera to it.
    for (std::uint32_t i = 0; i < list.world_light_count && !known; ++i) {
        const NebulaFrameWorldLight& light = list.world_lights[i];
        if (light.kind == NEBULA_WORLD_LIGHT_SUN) {
            if ((light.flags & NEBULA_WORLD_LIGHT_FOLLOWS_CAMERA) != 0u) {
                to_world(light.position, target);
            } else {
                std::memcpy(target, light.position, sizeof(target));
            }
            known = unit(target);
        }
    }
    // Else the stage's key light, from where the camera stands.
    if (!known && list.stage_light_known != 0u) {
        if (const NebulaFrameWorldLight* key = core::find_world_light(list, list.stage_light); key != nullptr) {
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
            known = unit(target);
        }
    }
    // Else straight overhead.
    if (!known && up_known) {
        std::memcpy(target, up, sizeof(target));
        known = unit(target);
    }
    if (!known) {
        return false;
    }
    if (follows && rt_sun_known_ && !walk_.time.reset && !now.cut) {
        if (!walk_.between) {
            constexpr float kShare = 1.0f / 60.0f;  // a second's lag at the game's 60 frames
            for (unsigned c = 0; c < 3u; ++c) {
                rt_sun_world_[c] += (target[c] - rt_sun_world_[c]) * kShare;
            }
            if (!unit(rt_sun_world_)) {
                std::memcpy(rt_sun_world_, target, sizeof(target));
            }
        }
    } else {
        std::memcpy(rt_sun_world_, target, sizeof(target));
    }
    rt_sun_known_ = true;
    float sun[3] = {rt_sun_world_[0], rt_sun_world_[1], rt_sun_world_[2]};
    if (up_known && unit(up)) {
        constexpr float kLeastRise = 0.5f, kFlat = 0.8660254f;  // 30 degrees above the player's horizon
        const float rise = sun[0] * up[0] + sun[1] * up[1] + sun[2] * up[2];
        if (rise < kLeastRise) {
            float flat[3] = {sun[0] - up[0] * rise, sun[1] - up[1] * rise, sun[2] - up[2] * rise};
            if (!unit(flat)) {
                // Straight below: any side will do.
                const float side[3] = {std::fabs(up[0]) < 0.9f ? 1.0f : 0.0f, std::fabs(up[0]) < 0.9f ? 0.0f : 1.0f, 0.0f};
                flat[0] = up[1] * side[2] - up[2] * side[1];
                flat[1] = up[2] * side[0] - up[0] * side[2];
                flat[2] = up[0] * side[1] - up[1] * side[0];
                static_cast<void>(unit(flat));
            }
            for (unsigned c = 0; c < 3u; ++c) {
                sun[c] = flat[c] * kFlat + up[c] * kLeastRise;
            }
        }
    }
    for (unsigned r = 0; r < 3u; ++r) {
        eye_direction[r] = view[r * 4u] * sun[0] + view[r * 4u + 1u] * sun[1] + view[r * 4u + 2u] * sun[2];
    }
    return unit(eye_direction);
}

// A glowing mesh's draw as a light: one a block (a model's glowing meshes are one light, the largest of them). Its
// intensity is the cooker's (candela, or below zero a luminance by the mesh's size); a long thin mesh (a rail of
// electricity, a beam) is a row of lights along its length, its value taken a metre.
void RenderCore::rt_emitter(const NebulaFrameItem& item, const core::GpuMesh& mesh, const float centre[3], float radius,
                            const float (*place)[4]) {
    if (!(radius > 0.0f) || !std::isfinite(radius + centre[0] + centre[1] + centre[2])) {
        return;
    }
    constexpr std::size_t kMost = 4608u;  // past the particles' own 4096: a mesh's light is not crowded out by sparks
    const auto intensity_of = [&](float r) {
        // A luminance lights by the mesh's size as the light itself stands for it (no wider than 6 m: a glowing
        // plain is not one lamp).
        const float metres = std::min(r, 600.0f) / kUnitsPerMetre;
        return mesh.glow[3] > 0.0f ? mesh.glow[3] : -mesh.glow[3] * 3.14159265f * metres * metres;
    };
    const auto [found, fresh] = rt_emitter_blocks_.try_emplace(item.block, static_cast<std::uint32_t>(rt_emitters_.size()));
    if (!fresh) {
        RtEmitter& same = rt_emitters_[found->second];
        if (same.kind == 1u && same.reserved[0] == 0u && radius > same.radius) {
            const float candela = intensity_of(radius);
            std::memcpy(same.centre, centre, sizeof(same.centre));
            same.radius = std::clamp(radius, 25.0f, 600.0f);
            for (unsigned c = 0; c < 3u; ++c) {
                same.colour[c] = mesh.glow[c] * candela;
            }
        }
        return;
    }
    // How the mesh's box lies: its three half axes through its matrix.
    float along[3]{}, thick = radius;
    unsigned count = 1u;
    float piece = radius;
    if (place != nullptr) {
        float axis[3][3], half[3];
        for (unsigned k = 0; k < 3u; ++k) {
            const float extent = (mesh.max[k] - mesh.min[k]) * 0.5f;
            for (unsigned r = 0; r < 3u; ++r) {
                axis[k][r] = place[r][k] * extent;
            }
            half[k] = std::sqrt(axis[k][0] * axis[k][0] + axis[k][1] * axis[k][1] + axis[k][2] * axis[k][2]);
        }
        const unsigned longest = half[0] >= half[1] && half[0] >= half[2] ? 0u : half[1] >= half[2] ? 1u : 2u;
        thick = std::max({half[(longest + 1u) % 3u], half[(longest + 2u) % 3u], 12.0f});
        if (half[longest] > 3.0f * thick) {
            count = std::clamp(static_cast<unsigned>(half[longest] / (2.0f * thick)), 2u, 16u);
            piece = half[longest] / static_cast<float>(count);
            std::memcpy(along, axis[longest], sizeof(along));
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
        const float candela = count == 1u ? intensity_of(radius)
                              : mesh.glow[3] > 0.0f ? mesh.glow[3] * (2.0f * piece / kUnitsPerMetre)
                                                    : intensity_of(std::sqrt(thick * piece));
        for (unsigned c = 0; c < 3u; ++c) {
            emitter.centre[c] = centre[c] + along[c] * t;
            emitter.colour[c] = mesh.glow[c] * candela;
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
    for (RtEmitter& e : rt_emitters_) {
        const float metres = e.radius / kUnitsPerMetre;
        const float candela = std::max({e.colour[0], e.colour[1], e.colour[2], 0.0f});
        const float at_source = 1.0f / (exposure_lux * metres * metres);
        for (float& c : e.colour) {
            c *= at_source;
        }
        e.reach = std::clamp(kUnitsPerMetre * std::sqrt(candela / (0.02f * exposure_lux)), e.radius * 2.0f, 4000.0f);
    }
}

// A particle draw as a light. Its kind gives its luminance (render/rt/rt_effect_lights.inc: an arc or a blast far
// over a flame, a flame over a sparkle, a sparkle over a streak; white water and smoke none); a draw whose name is not
// the game's (a mod's effect) is a light when it is added to the picture, at a modest 200 candela a square metre.
// Each particle is a light of its own, where it is and in its own colour, its intensity the luminance over what it
// shows this frame: so a spin's ring lights the ground around the player, a fuse sputters and a fire flickers as the
// effect itself does, and the thing in the middle of a cloud of sparks throws its shadow outward.
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
    const auto take = [&](const float eye[3], double size, const float colour[4]) {
        const double shown = size * static_cast<double>(colour[3]) * 0.33;
        if (ordinal++ % every != 0u || !(shown > 0.0) || !std::isfinite(shown + eye[0] + eye[1] + eye[2]) || rt_emitters_.size() >= kMost) {
            return;
        }
        const float candela =
            luminance * static_cast<float>(shown / (static_cast<double>(kUnitsPerMetre) * kUnitsPerMetre)) * static_cast<float>(every);
        RtEmitter& emitter = rt_emitters_.emplace_back();
        for (unsigned c = 0; c < 3u; ++c) {
            emitter.centre[c] = eye[c];
            emitter.colour[c] = colour[c] * std::min(candela, 20000.0f);
        }
        emitter.radius = std::clamp(static_cast<float>(std::sqrt(shown / 3.14159265358979)), 10.0f, 400.0f);
        emitter.reach = 0.0f;
        emitter.slot = item.slot;
        emitter.kind = 2u;
        emitter.reserved[1] = rt_emitter_id(2u, item.slot ^ static_cast<std::uint32_t>(item.source_hash), (ordinal - 1u) / every);
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
            take(eye, 4.0 * 625.0 * across * up, colour);
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
            take(eye, std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]), colour);
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
