// SPDX-License-Identifier: GPL-3.0-only
// The one path every presented frame takes through a frame list: a real frame at t = 1, a generated one at its t.
#include "galaxy/render/program_table.h"
#include "galaxy/render/render_core.h"

#include "galaxy/cpu_count.h"
#include "galaxy/frame_timeline.h"
#include "galaxy/launch_args.h"
#include "galaxy/loss_report.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/scene_camera_shape.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace galaxy::render {

using core::kParticleRows;
using core::kRowsPerLight;
using core::kRowsPerMatrix;
using core::ListData;
using core::WalkTime;

namespace {

// The state word's fields (nebula_model.h).
constexpr std::uint32_t blend_op(std::uint32_t s) noexcept { return s & 3u; }
constexpr std::uint32_t blend_src(std::uint32_t s) noexcept { return (s >> 2u) & 15u; }
constexpr bool state_depth_test(std::uint32_t s) noexcept { return (s & (1u << 10u)) != 0u; }
constexpr bool state_stencil(std::uint32_t s) noexcept { return ((s >> 20u) & 3u) != 0u; }
constexpr bool state_color_write(std::uint32_t s) noexcept { return (s & NEBULA_STATE_COLOR_WRITE) != 0u; }


// Two constants entries are one material when they are one entry or name one registered slot.
inline bool same_material(const NebulaFrameList& list, std::uint32_t a, std::uint32_t b) noexcept {
    return a == b || (list.constants[a].slot != NEBULA_NONE32 && list.constants[a].slot == list.constants[b].slot);
}

// Which targets a draw of the 3D scene writes beside the colour: an opaque surface the motion, a blended draw the reactive
// mask, and a blended draw whose factors never read the scene the effects layer too.
std::uint8_t scene_aux(std::uint32_t state, bool layer_window) noexcept {
    if (!state_color_write(state)) {
        return 0u;
    }
    const std::uint32_t op = blend_op(state);
    if (op == 0u) {
        return state_depth_test(state) ? 1u : 0u;
    }
    if (layer_window && (op == NEBULA_BLEND_SUBTRACT || (blend_src(state) != NEBULA_FACTOR_DST_COLOR && blend_src(state) != NEBULA_FACTOR_INV_DST_COLOR))) {
        return 3u;
    }
    return 2u;
}

inline Affine34 affine_of(const float (*rows)[4]) noexcept {
    Affine34 out;
    std::memcpy(out.data(), rows, sizeof(out));
    return out;
}

// One presented frame's picture of a light, at t.
void light_rows_at(const NebulaFrameLight* a, const NebulaFrameLight& b, const CameraMotion& camera, const Affine34& camera_t,
                   float t, bool split, float (*out)[4]) noexcept {
    float words[16];
    light_at(reinterpret_cast<const float*>(a), reinterpret_cast<const float*>(&b), camera, camera_t, t, split, words);
    std::memcpy(out, words, sizeof(words));
}

}  // namespace

void RenderCore::scan_list(ListData& now) {
    const NebulaFrameList& list = *now.list;
    now.scanned = true;
    now.block_camera.assign(list.block_count, NEBULA_NONE32);
    now.block_world_wanted.assign(list.block_count, 0u);
    now.light_camera.assign(list.light_set_count, NEBULA_NONE32);
    for (std::uint32_t s = 0; s < list.light_set_count; ++s) {
        const auto camera = list.light_sets[s].camera_plus_one;
        if (camera != 0u && camera <= list.camera_count) now.light_camera[s] = camera - 1u;
    }
    std::uint32_t scene_camera = NEBULA_NONE32;
    std::uint32_t mirror_camera = NEBULA_NONE32;
    for (std::uint32_t c = list.camera_count; c != 0u; --c) {
        if (list.cameras[c - 1u].role == NEBULA_CAMERA_SCENE) scene_camera = c - 1u;
        if (list.cameras[c - 1u].role == NEBULA_CAMERA_MIRROR) mirror_camera = c - 1u;
    }
    for (std::uint32_t b = 0; b < list.block_count; ++b) {
        now.block_camera[b] = list.blocks[b].camera;
    }
    for (std::uint32_t p = 0; p < list.pass_count; ++p) {
        const NebulaFramePass& pass = list.passes[p];
        if (pass.kind != NEBULA_PASS_DRAW && pass.kind != NEBULA_PASS_TRACED_DRAW) {
            continue;
        }
        for (std::uint32_t i = pass.first_item; i < pass.first_item + pass.item_count; ++i) {
            const NebulaFrameItem& item = list.items[i];
            const NebulaFrameBlock& block = list.blocks[item.block];
            if ((block.flags & NEBULA_BLOCK_WORLD_ROWS) != 0u && registries_.pipeline(item.pipeline).reads_world) {
                now.block_world_wanted[item.block] = 1u;
            }
            // Eye-space matrices hold still in the world under the camera they were drawn for, when the view is the camera's.
            std::uint32_t camera = block.camera;
            if (camera == NEBULA_NONE32 && !list.views[item.view].orthographic) {
                const std::uint32_t role_camera = pass.layer == NEBULA_LAYER_SCENE    ? scene_camera
                                                  : pass.layer == NEBULA_LAYER_MIRROR ? mirror_camera
                                                                                      : NEBULA_NONE32;
                if (role_camera != NEBULA_NONE32) {
                    camera = role_camera;
                    now.block_camera[item.block] = static_cast<std::uint32_t>(role_camera);
                }
            }
            if (item.light_set != NEBULA_NONE32 && camera != NEBULA_NONE32 && now.light_camera[item.light_set] == NEBULA_NONE32) {
                now.light_camera[item.light_set] = static_cast<std::uint32_t>(camera);
            }
        }
    }
}

void RenderCore::prepare_walk(ListData& now, ListData* before, const WalkTime& time) {
    const NebulaFrameList& list = *now.list;
    if (!now.scanned) {
        scan_list(now);
    }
    pairing_.pair(now, before);
    walk_ = {};
    walk_.now = &now;
    walk_.before = before;
    walk_.time = time;
    walk_.serial = ++walk_serial_;
    walk_.mip_lines = static_cast<float>(std::log2(std::max(1.0, static_cast<double>(renderer_.frame_map().height) / kAuthoredLines)));
    const bool between = time.t < 1.0f && before != nullptr;
    if (between) {
        pairing_.prepare_motion(now, *before);
    }
    walk_.between = between;

    // Cameras at t.
    walk_cameras_.resize(list.camera_count);
    for (std::uint32_t c = 0; c < list.camera_count; ++c) {
        walk_cameras_[c] = between && now.motion_ready ? camera_at(now.camera_motion[c], time.t) : affine_of(list.cameras[c].view);
    }

    // Rows: each block's eye rows, its world rows where a program reads them, the light sets, then the instance rows
    // of instanced draws as the list holds them.
    std::uint32_t total = 0u;
    walk_block_rows_.resize(list.block_count);
    walk_block_world_.resize(list.block_count);
    for (std::uint32_t b = 0; b < list.block_count; ++b) {
        const NebulaFrameBlock& block = list.blocks[b];
        const std::uint32_t matrices = block.model != 0u ? registries_.model(block.model).tables.draw_matrix_count : block.joint_count;
        walk_block_rows_[b] = total;
        total += matrices * kRowsPerMatrix;
        walk_block_world_[b] = now.block_world_wanted[b] != 0u ? total : walk_block_rows_[b];
        total += now.block_world_wanted[b] != 0u ? matrices * kRowsPerMatrix : 0u;
    }
    // The light sets: an empty one first (a draw without a light set reads it), then each set as its own count of lights
    // makes it.
    const std::uint32_t lights_first = total;
    total += core::light_set_rows(0u);
    walk_light_rows_.resize(list.light_set_count);
    for (std::uint32_t s = 0; s < list.light_set_count; ++s) {
        walk_light_rows_[s] = total;
        total += core::light_set_rows(list.light_sets[s].count);
    }
    const std::uint32_t instances_first = total;
    total += list.instance_row_count;
    walk_rows_.resize(std::size_t{instances_first} * 4u + 4u);
    auto* rows = reinterpret_cast<float (*)[4]>(walk_rows_.data());
    UploadRing& matrix_ring = renderer_.matrix_ring();
    const UploadRing::Allocation rows_at = matrix_ring.allocate(std::size_t{total} * 16u + 16u, 16u);
    const auto base = static_cast<std::uint32_t>((rows_at.gpu - matrix_ring.gpu_base()) / 16u);

    for (std::uint32_t b = 0; b < list.block_count; ++b) {
        const NebulaFrameBlock& block = list.blocks[b];
        core::ComposeInput in;
        in.joints = block.joints;
        in.scale_flags = block.scale_flags;
        in.model = block.model != 0u ? &registries_.model(block.model).tables : nullptr;
        in.joint_count = block.joint_count;
        in.normal_scale = block.normal_scale;
        const std::uint32_t camera = now.block_camera[b];
        const bool eye_block = block.camera == NEBULA_NONE32;
        if (!eye_block && block.calc_mode != 1u) {
            in.view = reinterpret_cast<const float (*)[4]>(walk_cameras_[block.camera].data());
        }
        float base_at[3][4];
        if (block.calc_mode == 2u) {
            in.base = block.base;
        }
        if (between && now.motion_ready && before != nullptr) {
            const std::uint32_t pair = now.block_before[b];
            if (block.calc_mode == 2u && pair != NEBULA_NONE32) {
                const float* a = &before->list->blocks[pair].base[0][0];
                const float* c = &block.base[0][0];
                for (unsigned i = 0; i < 12u; ++i) (&base_at[0][0])[i] = a[i] + (c[i] - a[i]) * time.t;
                in.base = base_at;
            }
            if (now.motion_first[b] != NEBULA_NONE32) {
                walk_joints_.resize(std::size_t{block.joint_count} * 12u);
                auto* joints = reinterpret_cast<float (*)[4]>(walk_joints_.data());
                core::joints_at(now, block, b, time.t, camera != NEBULA_NONE32 ? &walk_cameras_[camera] : nullptr, joints);
                in.joints = joints;
            }
        }
        core::compose_rows(in, rows + walk_block_rows_[b]);
        if (block.normals_from != NEBULA_NONE32) {
            const std::uint32_t matrices = core::draw_matrices_of(in);
            for (std::uint32_t m = 0; m < matrices; ++m) {
                std::memcpy(rows + walk_block_rows_[b] + m * kRowsPerMatrix + 3u,
                            rows + walk_block_rows_[block.normals_from] + m * kRowsPerMatrix + 3u, 3u * 16u);
            }
        }
        if (now.block_world_wanted[b] != 0u) {
            core::ComposeInput world_in = in;
            Affine34 eye_to_world{};
            if (block.calc_mode == 1u && camera != NEBULA_NONE32 && affine_inverse(walk_cameras_[camera], eye_to_world)) {
                // These joints already include the view. World-generated texture coordinates must remove that view.
                world_in.world_view = reinterpret_cast<const float (*)[4]>(eye_to_world.data());
            }
            core::compose_world_rows(world_in, rows + walk_block_world_[b]);
        }
    }

    // Each set's header row holds its light count as a float; its lights follow, four rows each.
    std::memset(rows + lights_first, 0, core::kLightSetHeaderRows * sizeof(float[4]));
    for (std::uint32_t s = 0; s < list.light_set_count; ++s) {
        const NebulaFrameLightSet& set = list.light_sets[s];
        float (*header)[4] = rows + walk_light_rows_[s];
        float (*out)[4] = header + core::kLightSetHeaderRows;
        header[0][0] = static_cast<float>(set.count);
        header[0][1] = header[0][2] = header[0][3] = 0.0f;
        const NebulaFrameLight* lights = list.lights + set.first;
        const std::uint32_t pair = now.light_set_before.empty() ? NEBULA_NONE32 : now.light_set_before[s];
        if (!between || !now.motion_ready) {
            if (set.count != 0u) {
                std::memcpy(out, lights, std::size_t{set.count} * sizeof(NebulaFrameLight));
            }
            continue;
        }
        const std::uint32_t camera = now.light_camera[s];
        static const CameraMotion kNoCamera{};
        const CameraMotion& motion = camera != NEBULA_NONE32 ? now.camera_motion[camera] : kNoCamera;
        const Affine34 camera_t = camera != NEBULA_NONE32 ? walk_cameras_[camera] : Affine34{};
        // The set the light was in before: its lights of the same number (a paired set holds the same follow words).
        const NebulaFrameLightSet* paired = pair != NEBULA_NONE32 ? &before->list->light_sets[pair] : nullptr;
        for (std::uint32_t l = 0; l < set.count; ++l) {
            const NebulaFrameLight* a = paired != nullptr && l < paired->count ? before->list->lights + paired->first + l : nullptr;
            const bool split = (list.light_follow[set.follow_first + l / 32u] >> (l % 32u) & 1u) == 0u;
            light_rows_at(a, lights[l], motion, camera_t, time.t, split, out + std::size_t{kRowsPerLight} * l);
        }
    }

    std::memcpy(rows_at.cpu, rows, std::size_t{instances_first} * 16u);
    walk_rows_upload_ = rows_at.cpu;
    if (list.instance_row_count != 0u) {
        std::memcpy(rows_at.cpu + std::size_t{instances_first} * 16u, list.instance_rows, std::size_t{list.instance_row_count} * 16u);
    }
    walk_.rows_address = matrix_ring.gpu_base();
    walk_.rows_base = base;

    // Where these rows were on the frame presented before: its own last walk, or its pair's.
    const bool ring_same = walk_rows_ring_ == matrix_ring.gpu_base() && walk_vertex_ring_ == renderer_.vertex_ring().gpu_base();
    walk_rows_ring_ = matrix_ring.gpu_base();
    walk_vertex_ring_ = renderer_.vertex_ring().gpu_base();
    const bool same_list = ring_same && !time.reset && now.rows_walk + 1u == walk_.serial;
    const bool from_before = ring_same && !time.reset && before != nullptr && !same_list && before->rows_walk + 1u == walk_.serial;
    walk_previous_.resize(list.block_count);
    for (std::uint32_t b = 0; b < list.block_count; ++b) {
        std::uint32_t previous = base + walk_block_rows_[b];
        if (same_list) {
            previous = now.block_rows[b];
        } else if (from_before && now.block_before[b] != NEBULA_NONE32) {
            previous = before->block_rows[now.block_before[b]];
        }
        walk_previous_[b] = previous;
    }
    now.block_rows.resize(list.block_count);
    for (std::uint32_t b = 0; b < list.block_count; ++b) {
        now.block_rows[b] = base + walk_block_rows_[b];
    }
    now.rows_walk = walk_.serial;

    // The instance table: one entry per item, in draw order. An instanced draw's light base is its first instance's
    // row.
    const UploadRing::Allocation instances = matrix_ring.allocate(std::size_t{list.item_count} * 16u + 16u, 16u);
    walk_.instances = instances.gpu;
    {
        auto* out = reinterpret_cast<std::uint32_t*>(instances.cpu);
        for (std::uint32_t i = 0; i < list.item_count; ++i) {
            const NebulaFrameItem& item = list.items[i];
            const bool ring = (item.flags & NEBULA_ITEM_RING) != 0u;
            const bool instanced = (item.flags & NEBULA_ITEM_INSTANCES) != 0u;
            const bool rigid = ring || instanced || (item.flags & NEBULA_ITEM_ONE_MATRIX) != 0u ||
                               registries_.mesh(item.mesh).single_matrix != NEBULA_MODEL_NONE;
            const std::uint32_t add = rigid ? item.matrix * kRowsPerMatrix : 0u;
            out[0] = base + walk_block_rows_[item.block] + add;
            out[1] = walk_previous_[item.block] + add;
            out[2] = instanced                         ? base + instances_first + item.ring_at
                     : item.light_set != NEBULA_NONE32 ? base + walk_light_rows_[item.light_set]
                                                       : base + lights_first;
            out[3] = base + walk_block_world_[item.block] + add;
            out += 4u;
        }
    }

    // The frame's vertices for this walk: copied once a walk, positions moved at t where an item's pair has as many.
    walk_ring_cpu_ = nullptr;
    walk_index_cpu_ = nullptr;
    if (list.ring_vertex_bytes != 0u) {
        const UploadRing::Allocation vertices = renderer_.vertex_ring().allocate(list.ring_vertex_bytes, 16u);
        walk_.ring_vertices = vertices.gpu;
        walk_ring_cpu_ = vertices.cpu;
        // The ring is write-combined memory: the positions moved at t are made in a copy of the vertices, which then go
        // out in one sequential copy, not patched in place after it.
        if (between && before != nullptr && !now.cut) {
            const auto* source = static_cast<const std::byte*>(list.ring_vertices);
            walk_ring_stage_.assign(source, source + list.ring_vertex_bytes);
            std::byte* const shaped = walk_ring_stage_.data();
            for (std::uint32_t i = 0; i < list.item_count; ++i) {
                const NebulaFrameItem& item = list.items[i];
                const std::uint32_t pair = now.item_before[i];
                if ((item.flags & NEBULA_ITEM_RING) == 0u || (item.flags & NEBULA_ITEM_PARTICLE) != 0u || pair == NEBULA_NONE32) {
                    continue;
                }
                const NebulaFrameItem& other = before->list->items[pair];
                if (!core::ring_shape_matches(item, other)) {
                    continue;
                }
                const auto* a = static_cast<const std::byte*>(before->list->ring_vertices) + other.ring_at;
                const auto* b = static_cast<const std::byte*>(list.ring_vertices) + item.ring_at;
                std::byte* to = shaped + item.ring_at;
                const std::uint32_t second = (item.flags & NEBULA_ITEM_WIDE) != 0u ? item.mesh - 16u : 0u;
                const auto lerp3 = [t = time.t](const std::byte* from_a, const std::byte* from_b, std::byte* out) {
                    float pa[3], pb[3], p[3];
                    std::memcpy(pa, from_a, sizeof(pa));
                    std::memcpy(pb, from_b, sizeof(pb));
                    for (unsigned c = 0; c < 3u; ++c) p[c] = pa[c] + (pb[c] - pa[c]) * t;
                    std::memcpy(out, p, sizeof(p));
                };
                for (std::uint32_t v = 0; v < item.ring_count; ++v) {
                    lerp3(a + v * item.mesh, b + v * item.mesh, to + v * item.mesh);
                    if (second != 0u) {
                        lerp3(a + v * item.mesh + second, b + v * item.mesh + second, to + v * item.mesh + second);
                    }
                }
            }
            std::memcpy(vertices.cpu, shaped, list.ring_vertex_bytes);
        } else {
            std::memcpy(vertices.cpu, list.ring_vertices, list.ring_vertex_bytes);
        }
        // The vertices they were at on the frame before, for what writes motion: its own, or its pair's.
        walk_ring_previous_ = same_list ? now.ring_address : from_before ? before->ring_address : gpu::Address{0u};
        walk_.same_list_ring = same_list;
        now.ring_address = vertices.gpu;
    }
    if (list.ring_index_count != 0u) {
        const std::size_t bytes = std::size_t{list.ring_index_count} * 4u;
        const UploadRing::Allocation indices = renderer_.index_ring().allocate(bytes, 4u);
        std::memcpy(indices.cpu, list.ring_indices, bytes);
        walk_.ring_indices = indices.gpu;
        walk_index_cpu_ = reinterpret_cast<std::uint32_t*>(indices.cpu);
    }

    // Views: their terms at t, and where the projection was on the frame before.
    walk_view_terms_.resize(list.view_count);
    walk_view_previous_.resize(list.view_count);
    for (std::uint32_t v = 0; v < list.view_count; ++v) {
        const NebulaFrameView& view = list.views[v];
        std::array<float, 6>& terms = walk_view_terms_[v];
        std::copy_n(view.terms, 6u, terms.begin());
        const std::uint32_t pair = now.view_before.empty() ? NEBULA_NONE32 : now.view_before[v];
        const NebulaFrameView* other = pair != NEBULA_NONE32 ? &before->list->views[pair] : nullptr;
        if (between && other != nullptr && other->orthographic == view.orthographic && !now.cut) {
            for (unsigned i = 0; i < 6u; ++i) {
                if (std::isfinite(other->terms[i]) && std::isfinite(view.terms[i])) {
                    terms[i] = other->terms[i] + (view.terms[i] - other->terms[i]) * time.t;
                }
            }
        }
        std::array<float, 6>& previous = walk_view_previous_[v];
        previous = terms;
        if (same_list) {
            previous = now.view_terms[v];
        } else if (from_before && other != nullptr && other->orthographic == view.orthographic) {
            previous = before->view_terms[pair];
        }
    }
    now.view_terms = walk_view_terms_;
    walk_views_.assign(list.view_count, WalkView{});
    walk_constants_.assign(list.constants_count, core::StaticSlot{});
    walk_constants_known_.assign(list.constants_count, 0u);
    walk_material_at_.assign(list.constants_count, NEBULA_NONE32);
    walk_lerped_.clear();
    ++walk_view_stamp_;
    walk_rows_cpu_ = rows;
}

const RenderCore::WalkView& RenderCore::walk_view(std::uint32_t index, const NebulaFramePass& pass) {
    WalkView& entry = walk_views_[index];
    if (entry.stamp == walk_view_stamp_) {
        return entry;
    }
    entry.stamp = walk_view_stamp_;
    const NebulaFrameView& view = walk_.now->list->views[index];
    const FrameMap& map = renderer_.frame_map();

    const ScaledViewport scaled = viewport_pixels(view.viewport, map);
    entry.viewport = gpu::ViewportRect{scaled.x, scaled.y, scaled.width, scaled.height, scaled.min_depth, scaled.max_depth};
    const auto left = static_cast<std::int32_t>(view.scissor[0]);
    const auto top = static_cast<std::int32_t>(view.scissor[1]);
    const ScaledSceneRect rect = rect_pixels(left, top, left + static_cast<std::int32_t>(view.scissor[2]),
                                             top + static_cast<std::int32_t>(view.scissor[3]), map);
    entry.empty = rect.empty() || !scaled.valid();
    entry.scissor = gpu::rect(rect.left, rect.top, rect.right, rect.bottom);

    const UploadRing::Allocation out = renderer_.constant_ring().allocate(sizeof(core::ViewRows), 256u);
    core::ViewRows rows{};
    const std::array<float, 6>& terms = walk_view_terms_[index];
    const float a = terms[0], b = terms[1], c = terms[2], d = terms[3], e = terms[4], f = terms[5];
    constexpr float kFarEps = 1.0f - 1e-7f;
    if (view.orthographic != 0u) {
        rows.projection[0][0] = a;
        rows.projection[0][3] = b;
        rows.projection[1][1] = c;
        rows.projection[1][3] = d;
        rows.projection[2][2] = -e * kFarEps;
        rows.projection[2][3] = -f * kFarEps;
        rows.projection[3][3] = 1.0f;
    } else {
        rows.projection[0][0] = a;
        rows.projection[0][2] = b;
        rows.projection[1][1] = c;
        rows.projection[1][2] = d;
        rows.projection[2][2] = -e * kFarEps;
        rows.projection[2][3] = -f * kFarEps;
        rows.projection[3][2] = -1.0f;
    }
    const float width = std::fabs(view.viewport[2]) * map.sx;
    const float height = std::fabs(view.viewport[3]) * map.sy;
    float jitter_x = 0.0f, jitter_y = 0.0f;
    if (temporal_scene_ && pass.layer == NEBULA_LAYER_SCENE && view.orthographic == 0u && width > 0.0f && height > 0.0f) {
        jitter_x = 2.0f * scene_jitter_px_[0] / width;
        jitter_y = -2.0f * scene_jitter_px_[1] / height;
        for (unsigned col = 0; col < 4u; ++col) {
            rows.projection[0][col] += jitter_x * rows.projection[3][col];
            rows.projection[1][col] += jitter_y * rows.projection[3][col];
        }
    }
    const std::array<float, 6>& previous = walk_view_previous_[index];
    rows.previous[0][0] = previous[0];
    rows.previous[0][1] = previous[1];
    rows.previous[0][2] = previous[2];
    rows.previous[0][3] = previous[3];
    rows.previous[1][0] = previous[4];
    rows.previous[1][1] = previous[5];
    rows.previous[1][2] = view.orthographic != 0u ? 1.0f : 0.0f;
    rows.motion[0] = jitter_x;
    rows.motion[1] = jitter_y;
    rows.motion[2] = 0.5f * width;
    rows.motion[3] = -0.5f * height;
    rows.raster[0] = width;
    rows.raster[1] = height;
    std::memcpy(rows.sky_up, sky_up_, sizeof(rows.sky_up));
    rows.view_flags = view_flags_;
    rows.traced[0] = (view_flags_ & 8u) != 0u ? static_cast<float>(rt_light_slot_) : 0.0f;
    rows.traced[1] = (view_flags_ & 8u) != 0u ? static_cast<float>(rt_added_slot_) : 0.0f;
    rows.traced[2] = (view_flags_ & 8u) != 0u ? static_cast<float>(rt_mirror_slot_) : 0.0f;
    if ((view_flags_ & 8u) != 0u && rt_volume_drawn_ && index == rt_scene_view_) {
        // The light volume over the scene camera's view (program.hlsli traced_air): the depth's eye terms and the
        // viewport the volume spans (rt_light_frame_'s), its picture's place + 1 and its sampler.
        std::memcpy(rows.volume[0], rt_light_frame_.depth, sizeof(rows.volume[0]));
        std::memcpy(rows.volume[1], rt_light_frame_.viewport, sizeof(rows.volume[1]));
        rows.volume[2][0] = static_cast<float>(rt_volume_slot_) + 1.0f;
        rows.volume[2][1] = static_cast<float>(rt_volume_sampler_);
        rows.volume[2][3] = rt_scatter_drawn_ ? static_cast<float>(rt_scatter_slot_) + 1.0f : 0.0f;
    }
    // The indirect ratio's place + 1 (program.hlsli traced_o): the scene view's, where the pass ran.
    rows.volume[2][2] = (view_flags_ & 8u) != 0u && rt_indirect_shown_ && index == rt_scene_view_
                            ? static_cast<float>(rt_indirect_slot_) + 1.0f : 0.0f;
    rows.traced[3] = (view_flags_ & 8u) != 0u ? static_cast<float>(rt_facing_slot_) : 0.0f;
    if ((view_flags_ & 8u) != 0u && rt_transmit_drawn_ && index == rt_scene_view_) {
        rows.transmit[0] = static_cast<float>(rt_transmit_slot_);  // program.hlsli traced_mirror: glass and water, High
        rows.transmit[1] = 1.0f;
    }
    if ((view_flags_ & 8u) != 0u && rt_blend_guide_mode_ != 0u && index == rt_scene_view_) {
        // The guide blended draws compare with (program.hlsli traced_by_air, traced_mirror_own; FB 15.2): the second
        // layer's, else the opaque layer's; and the depth's eye terms for the compare (with the volume or without).
        rows.blend_guide[0] = static_cast<float>(rt_blend_guide_mode_ == 1u ? rt_blend_guide_slot_ : rt_opaque_guide_slot_);
        rows.blend_guide[1] = static_cast<float>(rt_blend_guide_mode_);
        std::memcpy(rows.volume[0], rt_light_frame_.depth, sizeof(rows.volume[0]));
    }
    if ((view_flags_ & 8u) != 0u) {
        std::memcpy(rows.stage_a, rt_stage_a_, sizeof(rows.stage_a));
        std::memcpy(rows.stage_k, rt_stage_k_, sizeof(rows.stage_k));
        std::memcpy(rows.look, rt_look_, sizeof(rows.look));
        for (unsigned i = 0; i < kRtDevProgramSlots; ++i) {  // rt_dev.h: program.hlsli view_dev
            const unsigned which = kRtDevProgramFirst + i;
            const bool given = (rt_dev_set_ >> which & 1u) != 0u;
            rows.dev[0][i] = given ? rt_dev_[which] : 0.0f;
            rows.dev[1][i] = given ? 1.0f : 0.0f;
        }
        // The relight's view constants (program.hlsli traced_painted, view_relight; A3-F18): the program's formulas
        // on the rows above, once a view, not a pixel. sRGB decoded as traced_lin; dev y, z, w as the program reads them.
        const auto lin = [](float v) {
            v = std::max(v, 0.0f);
            return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
        };
        const auto luma = [](const float* v) { return 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2]; };
        const float sun_strength = rows.dev[1][1] > 0.5f ? rows.dev[0][1] : 1.0f;
        const float sky_chroma = rows.dev[1][2] > 0.5f ? rows.dev[0][2] : 0.4f;
        const float stage_light = rows.dev[1][3] > 0.5f ? rows.dev[0][3] : 0.0f;
        float sky[3], full[3], key[3], held[3];
        for (unsigned ch = 0; ch < 3u; ++ch) {
            sky[ch] = lin(rows.stage_a[ch]);
            full[ch] = lin(std::clamp(rows.stage_a[ch] + rows.stage_k[ch], 0.0f, 1.0f));
            key[ch] = std::max(full[ch] - sky[ch], 0.0f);
            held[ch] = full[ch] + (1.0f - full[ch]) * stage_light;
        }
        const float exposure = luma(full) / std::max(luma(sky) + luma(key) * rows.look[1], 1e-4f);
        const float sky_l = luma(sky), held_l = luma(held);
        for (unsigned ch = 0; ch < 3u; ++ch) {
            rows.relight[0][ch] = sky[ch];
            rows.relight[1][ch] = rows.look[3] * (sky_l + (sky[ch] - sky_l) * sky_chroma) * exposure;
            rows.relight[2][ch] = key[ch] * sun_strength * exposure;
            rows.relight[3][ch] = held_l > 1e-4f ? held[ch] / held_l : 1.0f;
        }
        rows.relight[0][3] = held_l;
    }
    rows.scale[1] = walk_.time.t;
    rows.scale[2] = walk_.time.t_prev;
    std::memcpy(out.cpu, &rows, sizeof(rows));
    entry.address = out.gpu;
    // Half a line's and a point's width in this target's pixels (the item's are lines of the frame).
    entry.frame_height = static_cast<float>(map.height);
    return entry;
}

const core::StaticSlot& RenderCore::walk_material(std::uint32_t index) {
    const NebulaFrameList& list = *walk_.now->list;
    const NebulaFrameConstants& constants = list.constants[index];
    if (constants.slot != NEBULA_NONE32) {
        return registries_.slot(constants.slot);
    }
    core::StaticSlot& slot = walk_constants_[index];
    if (walk_constants_known_[index] != 0u) {
        return slot;
    }
    walk_constants_known_[index] = 1u;
    UploadRing& ring = renderer_.constant_ring();
    const std::size_t vertex_bytes = std::size_t{constants.vertex_rows} * 16u;
    const std::size_t pixel_bytes = std::size_t{constants.pixel_rows} * 16u;
    const UploadRing::Allocation vertex = ring.allocate(std::max<std::size_t>(vertex_bytes, 16u), 256u);
    const UploadRing::Allocation pixel = ring.allocate(std::max<std::size_t>(pixel_bytes, 16u), 256u);
    slot.vertex_rows = vertex.gpu;
    slot.pixel_rows = pixel.gpu;

    const float* rows = walk_material_rows(index);
    std::memcpy(vertex.cpu, rows, vertex_bytes);
    std::memcpy(pixel.cpu, rows + std::size_t{constants.vertex_rows} * 4u, pixel_bytes);
    return slot;
}

// The list's own rows, or at t: once a walk, kept in walk_lerped_ for walk_same_material.
const float* RenderCore::walk_material_rows(std::uint32_t index) {
    constexpr std::uint32_t kAsListed = NEBULA_NONE32 - 1u;
    const NebulaFrameConstants& constants = walk_.now->list->constants[index];
    std::uint32_t& at = walk_material_at_[index];
    if (at == NEBULA_NONE32) {
        at = kAsListed;
        const NebulaFrameConstants* other = nullptr;
        if (walk_.between && !walk_.now->constants_before.empty() && walk_.now->constants_before[index] != NEBULA_NONE32) {
            const NebulaFrameConstants& candidate = walk_.before->list->constants[walk_.now->constants_before[index]];
            if (candidate.slot == NEBULA_NONE32 && candidate.vertex_rows == constants.vertex_rows &&
                candidate.pixel_rows == constants.pixel_rows && candidate.coord_row_count == constants.coord_row_count &&
                (constants.coord_row_count == 0u || std::memcmp(candidate.coord_rows, constants.coord_rows,
                    constants.coord_row_count * sizeof(constants.coord_rows[0])) == 0) && !walk_.now->cut) {
                other = &candidate;
            }
        }
        const std::size_t count = std::size_t{constants.vertex_rows} + constants.pixel_rows;
        if (other != nullptr && count != 0u) {
            const float t = walk_.time.t;
            const std::size_t first = walk_lerped_.size();
            walk_lerped_.resize(first + count * 4u);
            float* out = walk_lerped_.data() + first;
            const float* a = &other->rows[0][0];
            const float* b = &constants.rows[0][0];
            for (std::size_t i = 0; i < count * 4u; ++i) {
                out[i] = std::isfinite(a[i]) && std::isfinite(b[i]) ? a[i] + (b[i] - a[i]) * t : b[i];
            }
            // Coordinate matrices move as matrices: scrolling unwraps, and a step shows the new cell.
            for (std::uint32_t i = 0; i < constants.coord_row_count; ++i) {
                const auto row = constants.coord_rows[i];
                if (row + 3u > count) {
                    continue;
                }
                const float* ma = &other->rows[row][0];
                const float* mb = &constants.rows[row][0];
                bool spatial = false;
                for (unsigned component = 0; component < 12u; ++component) {
                    spatial = spatial || ((component & 3u) != 3u && ma[component] != mb[component]);
                }
                tex_matrix_at(ma, mb, spatial, t, out + row * 4u);
            }
            at = static_cast<std::uint32_t>(first);
        }
    }
    return at == kAsListed ? reinterpret_cast<const float*>(constants.rows) : walk_lerped_.data() + at;
}

bool RenderCore::walk_same_material(std::uint32_t a, std::uint32_t b) {
    const NebulaFrameList& list = *walk_.now->list;
    if (same_material(list, a, b)) {
        return true;
    }
    const NebulaFrameConstants& ca = list.constants[a];
    const NebulaFrameConstants& cb = list.constants[b];
    if (ca.slot != NEBULA_NONE32 || cb.slot != NEBULA_NONE32 || ca.vertex_rows != cb.vertex_rows || ca.pixel_rows != cb.pixel_rows) {
        return false;
    }
    const std::size_t bytes = (std::size_t{ca.vertex_rows} + ca.pixel_rows) * 16u;
    if (bytes == 0u) {
        return true;
    }
    (void)walk_material_rows(a);
    const float* rb = walk_material_rows(b);
    return std::memcmp(walk_material_rows(a), rb, bytes) == 0;  // a's again: b's may have grown walk_lerped_
}

// Ring draws after `first` join its draw while they differ from it only in their rows, vertices and indices. Each
// joining draw's vertices name their rows by matrix number from the first draw's rows (the rows lie in block order, six
// a matrix), and its indices count from the first draw's vertices. The patches go into this walk's copies of the ring,
// which each draw pass reads once per walk.
std::uint32_t RenderCore::merge_ring_run(std::uint32_t first, std::uint32_t end, std::uint32_t matrix_at, std::uint32_t& index_count) {
    const NebulaFrameList& list = *walk_.now->list;
    const NebulaFrameItem& head = list.items[first];
    const std::uint32_t stride = head.mesh;
    index_count = head.count;
    if (walk_ring_cpu_ == nullptr || walk_index_cpu_ == nullptr || stride == 0u) {
        return 1u;
    }
    const auto rows_of = [&](const NebulaFrameItem& item) {
        return std::int64_t{walk_block_rows_[item.block]} + std::int64_t{item.matrix} * kRowsPerMatrix;
    };
    const auto world_of = [&](const NebulaFrameItem& item) {
        return std::int64_t{walk_block_world_[item.block]} + std::int64_t{item.matrix} * kRowsPerMatrix;
    };
    const std::int64_t head_rows = rows_of(head), head_world = world_of(head);
    std::uint32_t count = 1u;
    while (first + count < end) {
        const NebulaFrameItem& next = list.items[first + count];
        if (next.flags != head.flags || next.pipeline != head.pipeline || next.textures != head.textures || next.view != head.view ||
            next.light_set != head.light_set || next.mesh != stride || next.line_width != head.line_width ||
            next.point_size != head.point_size || next.first != head.first + index_count || next.ring_at < head.ring_at ||
            (next.ring_at - head.ring_at) % stride != 0u) {
            break;
        }
        const std::int64_t rows = rows_of(next) - head_rows;
        // A wide draw's second position (stride - 16) is moved at t in the same copy: its matrix field stays as listed.
        if (rows < 0 || rows % kRowsPerMatrix != 0 || world_of(next) - head_world != rows ||
            (rows != 0 && (matrix_at == NEBULA_NONE32 || (head.flags & NEBULA_ITEM_WIDE) != 0u))) {
            break;
        }
        if (!walk_same_material(next.constants, head.constants)) {
            break;
        }
        // A drop shadow's volume the tracer draws is left out by itself: it does not ride in a run that is drawn.
        if (!rt_drop_volumes_.empty() && rt_drop_volumes_.contains(next.slot) != rt_drop_volumes_.contains(head.slot)) {
            break;
        }
        index_count += next.count;
        ++count;
    }
    const auto* vertices = static_cast<const std::byte*>(list.ring_vertices);
    for (std::uint32_t k = 1u; k < count; ++k) {
        const NebulaFrameItem& item = list.items[first + k];
        const auto add_matrix = static_cast<std::uint32_t>((rows_of(item) - head_rows) / kRowsPerMatrix);
        if (add_matrix != 0u) {
            for (std::uint32_t v = 0; v < item.ring_count; ++v) {
                const std::size_t at = std::size_t{item.ring_at} + std::size_t{v} * stride + matrix_at;
                std::uint32_t matrix;
                std::memcpy(&matrix, vertices + at, sizeof(matrix));
                matrix += add_matrix;
                std::memcpy(walk_ring_cpu_ + at, &matrix, sizeof(matrix));
            }
        }
        const std::uint32_t add_vertex = (item.ring_at - head.ring_at) / stride;
        for (std::uint32_t n = 0; n < item.count; ++n) {
            walk_index_cpu_[item.first + n] = list.ring_indices[item.first + n] + add_vertex;
        }
    }
    return count;
}

void RenderCore::walk_tables(std::uint32_t id, gpu::Address& lists, std::uint32_t& first) {
    core::TextureTable& table = registries_.table(id);
    table.last_frame = frame_index_;
    table.retired = false;
    // A list written earlier in this walk stands unless the generation moved since (the sampler array started again, or
    // the Vulkan image array grew and its null slot moved): then it is written again.
    const auto generation = renderer_.sampler_generation();
    if (table.walk_stamp == walk_.serial && table.samplers_generation == generation) {
        lists = table.lists;
        first = table.list_first;
        return;
    }
    const float mip_lines = walk_.mip_lines;  // taken once in prepare_walk
    // The run is as long as the list: no count caps it.
    const std::uint32_t count = table.count;
    const NebulaFrameTexture none{};
    const auto entry_of = [&](std::uint32_t i) -> const NebulaFrameTexture& {
        return table.count == 0u ? none : table.list[std::min(i, table.count - 1u)];
    };
    walk_handles_.assign(count, TextureHandle{});
    bool has_target = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t texture = entry_of(i).texture;
        if (texture == 0u) {
            continue;
        }
        if (TextureStore::is_target(texture)) {
            has_target = true;
            const std::uint64_t key = walk_.between && generated_written(texture) ? (kGeneratedPicture | texture) : texture;
            (void)renderer_.target_picture(key, walk_handles_[i]);
        } else {
            walk_handles_[i] = texture_store_.handle(texture);
        }
    }
    // A sampler depends on its texture (levels, generated levels, a replacement's shift): made again when the textures,
    // the heap, the frame's height or the scene's bias changed, and each walk for a table holding a target.
    if (has_target || table.sampler_slots.size() != count || table.textures_revision != texture_store_.revision() ||
        table.samplers_generation != generation || table.samplers_mip_lines != mip_lines ||
        table.samplers_scene_bias != scene_mip_bias_) {
        table.sampler_slots.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const NebulaFrameTexture& t = entry_of(i);
            TextureSampler mode;
            mode.wrap_s = static_cast<TexWrap>(t.wrap_s);
            mode.wrap_t = static_cast<TexWrap>(t.wrap_t);
            mode.min_linear = t.min_linear != 0u;
            mode.mag_linear = t.mag_linear != 0u;
            mode.mip = t.mip_filter;
            mode.anisotropy = t.anisotropy;
            mode.lod_bias = t.lod_bias;
            mode.min_lod = t.min_lod;
            mode.max_lod = t.max_lod;
            const TextureHandle& handle = walk_handles_[i];
            const bool generated = TextureStore::samples_generated(mode, handle);
            if (handle.generated_mips && !generated) {
                mode.min_lod = 0.0f;
                mode.max_lod = 0.0f;
            }
            table.sampler_slots[i] = renderer_.sampler_slot(make_sampler_key(mode, generated, handle.mip_levels, handle.lod_shift,
                                                                             handle.lod_arbitrary, mip_lines, scene_mip_bias_));
        }
        table.textures_revision = texture_store_.revision();
        table.samplers_generation = generation;
        table.samplers_mip_lines = mip_lines;
        table.samplers_scene_bias = scene_mip_bias_;
    }
    walk_pairs_.resize(std::size_t{count} * 2u);
    for (std::uint32_t i = 0; i < count; ++i) {
        walk_pairs_[2u * i] = renderer_.texture_slot(walk_handles_[i]);
        walk_pairs_[2u * i + 1u] = table.sampler_slots[i];
    }
    renderer_.write_texture_list(walk_pairs_.data(), count, table.lists, table.list_first);
    if (count > table.count && !table.clamp_logged) {
        table.clamp_logged = true;
        std::fprintf(stderr, "[textures] test: a list of %u maps walked as a run of %u (first entry %u)\n", table.count, count,
                     table.list_first);
    }
    table.walk_stamp = walk_.serial;
    lists = table.lists;
    first = table.list_first;
}

#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
// A device without descriptor indexing: each draw binds its table, as many maps as a bound table holds on this device
// (renderer_.table_slots(); program.hlsli NEBULA_BOUND_TABLES). A longer list draws with its first maps.
void RenderCore::walk_bound_tables(std::uint32_t id, gpu::Table& textures, gpu::Table& samplers) {
    core::TextureTable& table = registries_.table(id);
    table.last_frame = frame_index_;
    table.retired = false;
    // The list cut to the bound table's compile-time size; told once per list.
    const std::uint32_t listed = table.count;
    const std::uint32_t count = std::min(listed, renderer_.table_slots());
    if (count < listed && !table.clamp_logged) {
        table.clamp_logged = true;
        std::fprintf(stderr, "[textures] a list of %u maps: this device's bound tables hold %u, the first are drawn\n",
                     listed, count);
    }
    const NebulaFrameTexture none{};
    const auto entry_of = [&](std::uint32_t i) -> const NebulaFrameTexture& {
        return table.count == 0u ? none : table.list[std::min(i, table.count - 1u)];
    };
    const bool stale = table.textures.ptr == 0u || table.textures_revision != texture_store_.revision();
    const bool per_walk = table.has_target && table.walk_stamp != walk_.serial;
    const float mip_lines = walk_.mip_lines;  // taken once in prepare_walk
    const auto generation = renderer_.sampler_generation();
    const bool samplers_stale = table.samplers.ptr == 0u || table.samplers_generation != generation ||
        table.samplers_mip_lines != mip_lines || table.samplers_scene_bias != scene_mip_bias_;
    if (stale || per_walk || samplers_stale) {
        walk_handles_.assign(count, TextureHandle{});
        std::vector<TextureHandle>& handles = walk_handles_;
        bool has_target = false;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t texture = entry_of(i).texture;
            if (texture == 0u) {
                continue;
            }
            if (TextureStore::is_target(texture)) {
                has_target = true;
                const std::uint64_t key = walk_.between && generated_written(texture) ? (kGeneratedPicture | texture) : texture;
                (void)renderer_.target_picture(key, handles[i]);
            } else {
                handles[i] = texture_store_.handle(texture);
            }
        }
        table.has_target = has_target;
        if (stale || has_target) {
            if (has_target) {
                // A target's picture changes from frame to frame: the table lives for this frame.
                table.textures = renderer_.texture_table_for(handles.data(), count, texture_store_);
                table.walk_stamp = walk_.serial;
            } else {
                table.textures = renderer_.persistent_texture_table(handles.data(), count, texture_store_, table.textures);
            }
            table.textures_revision = texture_store_.revision();
        }
        if (samplers_stale || has_target) {
            std::vector<SamplerKey>& keys = walk_keys_;
            keys.assign(count, SamplerKey{});
            for (std::uint32_t i = 0; i < count; ++i) {
                const NebulaFrameTexture& t = entry_of(i);
                TextureSampler mode;
                mode.wrap_s = static_cast<TexWrap>(t.wrap_s);
                mode.wrap_t = static_cast<TexWrap>(t.wrap_t);
                mode.min_linear = t.min_linear != 0u;
                mode.mag_linear = t.mag_linear != 0u;
                mode.mip = t.mip_filter;
                mode.anisotropy = t.anisotropy;
                mode.lod_bias = t.lod_bias;
                mode.min_lod = t.min_lod;
                mode.max_lod = t.max_lod;
                const TextureHandle& handle = handles[i];
                const bool generated = TextureStore::samples_generated(mode, handle);
                if (handle.generated_mips && !generated) {
                    mode.min_lod = 0.0f;
                    mode.max_lod = 0.0f;
                }
                keys[i] = make_sampler_key(mode, generated, handle.mip_levels, handle.lod_shift, handle.lod_arbitrary,
                                           mip_lines, scene_mip_bias_);
            }
            table.samplers = renderer_.sampler_table_for(keys.data(), count);
            table.samplers_generation = generation;
            table.samplers_mip_lines = mip_lines;
            table.samplers_scene_bias = scene_mip_bias_;
        }
    } else if (table.touched_walk != walk_.serial) {
        // The table stands as it was built: its textures are named again all the same (the budget evicts the unnamed ones). Once
        // a walk is enough (the store counts frames), and this runs for every draw that binds the table.
        table.touched_walk = walk_.serial;
        for (std::uint32_t i = 0; i < count; ++i) {
            texture_store_.touch(entry_of(i).texture);
        }
    }
    textures = table.textures;
    samplers = table.samplers;
}
#endif

bool RenderCore::item_culled(const NebulaFrameItem& item, const NebulaFramePass& pass, std::uint32_t state,
                             const core::GpuMesh* mesh) noexcept {
    if (mesh == nullptr || (item.flags & NEBULA_ITEM_NO_CULL) != 0u || pass.layer != NEBULA_LAYER_SCENE || !state_color_write(state) ||
        state_stencil(state)) {
        return false;
    }
    if ((item.flags & NEBULA_ITEM_ONE_MATRIX) == 0u && mesh->single_matrix == NEBULA_MODEL_NONE) {
        return false;
    }
    const NebulaFrameView& frame_view = walk_.now->list->views[item.view];
    if (frame_view.orthographic != 0u || mesh->site_stride != 0u) {
        return false;
    }
    const float (*place)[4] = walk_rows_cpu_ + walk_block_rows_[item.block] + item.matrix * kRowsPerMatrix;
    float centre[3], radius = 0.0f;
    for (unsigned r = 0; r < 3u; ++r) {
        float extent = 0.0f;
        float value = place[r][3];
        for (unsigned c = 0; c < 3u; ++c) {
            value += place[r][c] * (mesh->min[c] + mesh->max[c]) * 0.5f;
            extent += std::fabs(place[r][c]) * (mesh->max[c] - mesh->min[c]) * 0.5f;
        }
        centre[r] = value;
        radius += extent * extent;
    }
    radius = std::sqrt(radius);
    if (!std::isfinite(radius) || !std::isfinite(centre[0] + centre[1] + centre[2])) {
        return false;
    }
    // The four side planes in eye space, from x and y against w = -z, with the jitter's room.
    const std::array<float, 6>& terms = walk_view_terms_[item.view];
    const float x_slack = std::fabs(terms[0]) * 0.01f + 0.01f, y_slack = std::fabs(terms[2]) * 0.01f + 0.01f;
    const auto outside = [&](float nx, float ny, float nz) {
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        return nx * centre[0] + ny * centre[1] + nz * centre[2] < -radius * length;
    };
    const float a = terms[0], b = terms[1], c = terms[2], d = terms[3];
    return outside(a, 0.0f, b - 1.0f - x_slack) || outside(-a, 0.0f, -b - 1.0f - x_slack) ||
           outside(0.0f, c, d - 1.0f - y_slack) || outside(0.0f, -c, -d - 1.0f - y_slack);
}

namespace {

// The walker's draws as the renderer takes them at once: the encoder, and the renderer's own draw targets and texture arrays.
class DirectDraws {
public:
    static constexpr bool kDirect = true;
    DirectDraws(gpu::Encoder& encoder, Renderer& renderer) noexcept : encoder_(encoder), renderer_(renderer) {}
    [[nodiscard]] gpu::Encoder& raw() noexcept { return encoder_; }
    void set_rows(gpu::Address rows, gpu::Address instances) noexcept { encoder_.set_rows(rows, instances); }
    void set_draw_aux(unsigned aux) { renderer_.set_draw_aux(aux); }
    void bind_textures(gpu::Address lists) { renderer_.bind_draw_textures(lists); }
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
    void set_tables(gpu::Table textures, gpu::Table samplers) noexcept {
        renderer_.bind_draw_tables(textures, samplers);
        encoder_.set_tables(textures, samplers);
    }
#endif
    void set_pipeline(gpu::Pipeline* pipeline) noexcept { encoder_.set_pipeline(pipeline); }
    void set_view(gpu::Address view) noexcept { encoder_.set_view(view); }
    void set_material(gpu::Address vertex_rows, gpu::Address pixel_rows) noexcept { encoder_.set_material(vertex_rows, pixel_rows); }
    void set_root(const gpu::DrawRoot& root) noexcept { encoder_.set_root(root); }
    void set_scissor(const gpu::Rect& rect) noexcept { encoder_.set_scissor(rect); }
    void set_viewport(const gpu::ViewportRect& rect) noexcept { encoder_.set_viewport(rect); }
    void set_stream(unsigned slot, const gpu::StreamView& view) noexcept { encoder_.set_stream(slot, view); }
    void set_indices(gpu::Address address, std::uint32_t bytes, std::uint32_t index_bytes) noexcept {
        encoder_.set_indices(address, bytes, index_bytes);
    }
    void draw_indexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t base_vertex) noexcept {
        encoder_.draw_indexed(count, instances, first, base_vertex);
    }
    void draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first) noexcept { encoder_.draw(count, instances, first); }

private:
    gpu::Encoder& encoder_;
    Renderer& renderer_;
};

// The same calls recorded (draw_record.h) for a pass replayed in chunks. What the renderer's own state answers is asked now.
class RecordedDraws {
public:
    static constexpr bool kDirect = false;
    RecordedDraws(gpu::DrawRecorder& recorder, Renderer& renderer) noexcept : recorder_(recorder), renderer_(renderer) {}
    void set_rows(gpu::Address rows, gpu::Address instances) noexcept { recorder_.set_rows(rows, instances); }
    void set_draw_aux(unsigned aux) { recorder_.set_draw_aux(aux); }
    void bind_textures(gpu::Address lists) { recorder_.set_texture_arrays(renderer_.draw_texture_arrays(lists)); }
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
    void set_tables(gpu::Table textures, gpu::Table samplers) { recorder_.set_tables(textures, samplers); }
#endif
    void set_pipeline(gpu::Pipeline* pipeline) { recorder_.set_pipeline(pipeline); }
    void set_view(gpu::Address view) { recorder_.set_view(view); }
    void set_material(gpu::Address vertex_rows, gpu::Address pixel_rows) { recorder_.set_material(vertex_rows, pixel_rows); }
    void set_root(const gpu::DrawRoot& root) { recorder_.set_root(root); }
    void set_scissor(const gpu::Rect& rect) { recorder_.set_scissor(rect); }
    void set_viewport(const gpu::ViewportRect& rect) { recorder_.set_viewport(rect); }
    void set_stream(unsigned slot, const gpu::StreamView& view) { recorder_.set_stream(slot, view); }
    void set_indices(gpu::Address address, std::uint32_t bytes, std::uint32_t index_bytes) {
        recorder_.set_indices(address, bytes, index_bytes);
    }
    void draw_indexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t base_vertex) {
        recorder_.draw_indexed(count, instances, first, base_vertex);
    }
    void draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first) { recorder_.draw(count, instances, first); }

private:
    gpu::DrawRecorder& recorder_;
    Renderer& renderer_;
};

// A draw pass of at least this many items is recorded in chunks, on a machine with at least kRecordMinCores logical processors.
// A chunk is at least kRecordMinChunkDraws draws, and a recorded pass of fewer than kRecordMinParallelDraws draws is replayed by the render thread.
constexpr std::uint32_t kRecordThreshold = 1500u;
constexpr unsigned kRecordMinCores = 6u;
constexpr std::uint32_t kRecordMinChunkDraws = 64u;
constexpr std::uint32_t kRecordMinParallelDraws = 96u;

}  // namespace

// 0: the pass is drawn through the encoder as it is walked. Else the threads that replay its recording.
unsigned RenderCore::recording_workers(const NebulaFramePass& pass) const {
    // Asked once: this runs for every draw pass of every walk, and the count asks the OS (it cannot change in a run).
    static const unsigned cores = logical_cores();
    if (cores < kRecordMinCores || pass.item_count < kRecordThreshold) {
        return 0u;
    }
    return std::clamp(cores / 2u, 2u, 8u);
}

// The recorded pass, replayed: in chunks on the pool's threads and the render thread, each into a command list or secondary
// command buffer of its own (the renderer runs them in order), or chunk by chunk on the render thread's own list.
void RenderCore::submit_recorded(unsigned workers) {
    const std::vector<gpu::DrawRecorder::Chunk>& chunks = draw_record_.chunks();
    const std::vector<gpu::DrawOp>& ops = draw_record_.ops();
    if (draw_record_.draws() == 0u) {
        return;
    }
    bool parallel = workers > 1u && chunks.size() > 1u && draw_record_.draws() >= kRecordMinParallelDraws;
    if (parallel) {
        record_chunk_aux_.clear();
        for (const gpu::DrawRecorder::Chunk& chunk : chunks) {
            record_chunk_aux_.push_back(chunk.aux);
        }
        parallel = renderer_.parallel_record_begin(record_chunk_aux_.data(), static_cast<unsigned>(record_chunk_aux_.size()));
    }
    if (!parallel) {
        for (const gpu::DrawRecorder::Chunk& chunk : chunks) {
            renderer_.replay_draws(chunk.aux, ops.data() + chunk.first, chunk.count);
        }
        return;
    }
    if (record_pool_ == nullptr || record_pool_->extra_threads() + 1u != workers) {
        record_pool_ = std::make_unique<core::RecordPool>(workers - 1u);
    }
    record_pool_->run(static_cast<unsigned>(chunks.size()), [this, &chunks, &ops](unsigned chunk) {
        renderer_.parallel_record_chunk(chunk, ops.data() + chunks[chunk].first, chunks[chunk].count);
    });
    renderer_.parallel_record_end();
}

void RenderCore::walk_draw_pass(const NebulaFramePass& pass) {
    const unsigned workers = recording_workers(pass);
    if (workers == 0u) {
        DirectDraws draws(renderer_.encoder(), renderer_);
        walk_draw_items(pass, draws);
        return;
    }
    // A big pass is resolved here as ever, in order; its encoder calls are recorded and replayed afterwards.
    draw_record_.begin(std::max(kRecordMinChunkDraws, pass.item_count / (workers * 2u)));
    RecordedDraws draws(draw_record_, renderer_);
    walk_draw_items(pass, draws);
    draw_record_.finish();
    submit_recorded(workers);
}

template <class Draws>
void RenderCore::walk_draw_items(const NebulaFramePass& pass, Draws& encoder) {
    const NebulaFrameList& list = *walk_.now->list;
    encoder.set_rows(walk_.rows_address, walk_.instances);
    const unsigned target_variant = renderer_.scene_variant();
    const bool scene_layer = pass.layer == NEBULA_LAYER_SCENE;
    const bool layer_window = renderer_.layer_window();
    const bool one_pass = renderer_.motion_pass_only();
    const std::uint32_t end = pass.first_item + pass.item_count;
    const bool collect_ao = collect_ao_objects_ && scene_layer && !rt_prepass_walk_;
    const bool tracing = rt_backend_.started();
    for (std::uint32_t i = pass.first_item; i < end;) {
        const NebulaFrameItem& item = list.items[i];
        const bool ring = (item.flags & NEBULA_ITEM_RING) != 0u;
        const bool instanced = (item.flags & NEBULA_ITEM_INSTANCES) != 0u;
        const core::GpuMesh* mesh = ring || instanced ? nullptr : &registries_.mesh(item.mesh);
        const core::PipelineEntry& entry = registries_.pipeline(item.pipeline);
        const std::uint32_t state = entry.desc.state;
        // The tracer's prepass draws the scene's opaque surfaces only.
        if (rt_prepass_walk_ && !rt_prepass_draws(item, state)) {
            ++i;
            continue;
        }

        if (collect_ao && (item.flags & NEBULA_ITEM_REFLECT_AO) != 0u && mesh != nullptr) {
            const float (*block_rows)[4] = walk_rows_cpu_ + walk_block_rows_[item.block];
            ao_objects_.add(item.block, registries_.object_slots(*mesh),
                            [block_rows](std::uint32_t row, unsigned col) { return block_rows[row][col]; });
        }
        // With traced light no shadow is drawn by hand: not the game's own (drop shadow volumes and their fill, the
        // player's alpha shadow, shadow surfaces: one category), not a shadow model or decal (its material says so).
        // The prepass leaves them out too (they are no surfaces).
        if ((rt_light_on_ || rt_prepass_walk_) && scene_layer &&
            (((item.flags & NEBULA_ITEM_CATEGORY_MASK) >> NEBULA_ITEM_CATEGORY_SHIFT) == NEBULA_CATEGORY_GAME_SHADOW ||
             (mesh != nullptr && (mesh->hit_flags & NEBULA_HIT_DRAWN_SHADOW) != 0u))) {
            ++i;
            continue;
        }
        // A drop shadow's stencil volume whose shadow the tracer draws (rt_drops_collect) is left out.
        if (!rt_drop_volumes_.empty() && scene_layer && !state_color_write(state) && state_stencil(state) &&
            rt_drop_volumes_.contains(item.slot)) {
            ++i;
            continue;
        }
        const WalkView& view = walk_view(item.view, pass);
        if (view.empty || (mesh != nullptr && mesh->positions == 0u) || item_culled(item, pass, state, mesh)) {
            ++stat_items_culled_;
            ++i;
            continue;
        }
        // With one pass for the scene, a draw that writes no motion stays in the motion draws' pass (aux 4).
        std::uint8_t aux = scene_layer && motion_scene_ ? scene_aux(state, layer_window) : 0u;
        if (scene_layer && motion_scene_ && one_pass && aux != 1u) {
            aux = 4u;
        }
        if (rt_prepass_walk_) {
            // Through the vertex stage the scene pass will use for it: the two passes find one depth.
            aux = static_cast<std::uint8_t>(aux == 1u ? kAuxPrepassMotion : kAuxPrepass);
        } else if (rt_light_on_ && scene_layer && rt_prepassed_[i] != 0u && aux <= 4u &&
                   program_traced(entry.desc.pixel)) {
            aux = static_cast<std::uint8_t>(aux | kAuxTraced);  // a surface the light pass saw: its colour and lights by their shares
        }
        gpu::Pipeline* pipeline = pipelines_.get(item.pipeline, entry, target_variant, aux);
        if (pipeline == nullptr) {  // the driver refused it: the player misses this draw
            galaxy::loss::note(galaxy::loss::Kind::DrawNoPipeline);
            ++stat_draws_no_pipeline_;
            ++i;
            continue;
        }

        // Consecutive draws of one mesh under one material are one instanced draw.
        std::uint32_t instances = 1u;
        if (mesh != nullptr && !rt_prepass_walk_) {  // the prepass: one item a draw, so a pixel names its own item
            while (i + instances < end) {
                const NebulaFrameItem& next = list.items[i + instances];
                if (next.mesh != item.mesh || next.pipeline != item.pipeline || !walk_same_material(next.constants, item.constants) ||
                    next.textures != item.textures || next.view != item.view || next.flags != item.flags ||
                    next.line_width != item.line_width || next.point_size != item.point_size) {
                    break;
                }
                if (collect_ao && (next.flags & NEBULA_ITEM_REFLECT_AO) != 0u) {
                    const float (*block_rows)[4] = walk_rows_cpu_ + walk_block_rows_[next.block];
                    ao_objects_.add(next.block, registries_.object_slots(*mesh),
                                    [block_rows](std::uint32_t row, unsigned col) { return block_rows[row][col]; });
                }
                if (item_culled(next, pass, state, mesh)) {
                    break;
                }
                // The light pass reads a draw's light set from its first item: one set a draw.
                if (tracing && next.light_set != item.light_set) {
                    break;
                }
                if (!rt_drop_volumes_.empty() && !state_color_write(state) && state_stencil(state) && rt_drop_volumes_.contains(next.slot)) {
                    break;  // a traced drop shadow's volume: left out when its turn comes
                }
                ++instances;
            }
        }
        // Consecutive ring draws of one material are one draw.
        std::uint32_t ring_index_count = item.count;
        const bool motion_form = aux_motion_form(aux);
        if (ring && !instanced && (item.flags & NEBULA_ITEM_INDEXED) != 0u && !motion_form && !rt_prepass_walk_) {
            instances = merge_ring_run(i, end, entry.matrix_at, ring_index_count);
        }

        encoder.set_draw_aux(aux_targets(aux));
        renderer_.note_scene_draw(state);
        encoder.set_pipeline(pipeline);
        encoder.set_view(view.address);
        const core::StaticSlot& material = walk_material(item.constants);
        encoder.set_material(material.vertex_rows, material.pixel_rows);
        gpu::DrawRoot root;
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
        if (!renderer_.bindless()) {
            gpu::Table textures{}, samplers{};
            walk_bound_tables(item.textures, textures, samplers);
            encoder.set_tables(textures, samplers);
        } else
#endif
        {
            gpu::Address lists = 0u;
            walk_tables(item.textures, lists, root.table);
            encoder.bind_textures(lists);
        }
        // A metal (draw flag 128, A3-F4): no body, all its light its mirror's: share 1, no added light.
        const bool metal = rt_light_on_ && scene_layer && (item.flags & (NEBULA_ITEM_RING | NEBULA_ITEM_INSTANCES)) == 0u &&
                           registries_.mesh(item.mesh).hit_class == NEBULA_HIT_METAL;
        root.first_instance = i;
        // Shown as it is (program.hlsli draw flag 64; the director's D2): the sky and what is drawn with it, and a
        // surface the cooker classed glowing: no share, no added light, no mirror. The sky takes no air in front (8).
        const std::uint32_t category = (item.flags & NEBULA_ITEM_CATEGORY_MASK) >> NEBULA_ITEM_CATEGORY_SHIFT;
        const bool sky_draw = category == NEBULA_CATEGORY_SKY || category == NEBULA_CATEGORY_AIR || category == NEBULA_CATEGORY_SUN ||
                              category == NEBULA_CATEGORY_ASTRO_DOME_SKY;
        const bool shown = rt_light_on_ && scene_layer &&
                           (sky_draw || ((item.flags & (NEBULA_ITEM_RING | NEBULA_ITEM_INSTANCES)) == 0u &&
                                         registries_.mesh(item.mesh).hit_class == NEBULA_HIT_EMISSIVE));
        root.flags = ((item.flags & NEBULA_ITEM_SPREAD_LINES) != 0u ? 1u : 0u) |
                     (shown ? 64u : 0u) |
                     (metal ? 128u : 0u) |
                     ((item.flags & NEBULA_ITEM_SPREAD_POINTS) != 0u ? 2u : 0u) |
                     (rt_light_on_ && rt_volume_drawn_ && scene_layer && rt_volume_reads(item, state) ? 4u : 0u) |
                     // A blended surface off the one the light pass saw (program.hlsli traced_by_air; FB 15.2).
                     (rt_light_on_ && rt_volume_drawn_ && rt_blend_guide_mode_ != 0u && scene_layer && state_color_write(state) &&
                              (item.flags & (NEBULA_ITEM_INSTANCES | NEBULA_ITEM_PARTICLE)) == 0u &&
                              (state & 3u) == NEBULA_BLEND_ADD && ((state >> 2u) & 15u) == NEBULA_FACTOR_SRC_ALPHA &&
                              ((state >> 6u) & 15u) == NEBULA_FACTOR_INV_SRC_ALPHA
                          ? 16u
                          : 0u) |
                     // A glow (its blend adds): no reflection of its own (program.hlsli traced_mirror_own; FB).
                     (rt_light_on_ && scene_layer && (state & 3u) == NEBULA_BLEND_ADD && ((state >> 6u) & 15u) == NEBULA_FACTOR_ONE
                          ? 32u
                          : 0u) |
                     // The air in front (traced_scatter): a draw that covers what is behind it or blends over it by
                     // its alpha (an added one would add the air twice).
                     (rt_light_on_ && rt_scatter_drawn_ && scene_layer && !sky_draw && state_color_write(state) &&
                              ((state & 3u) == NEBULA_BLEND_REPLACE ||
                               ((state & 3u) == NEBULA_BLEND_ADD && ((state >> 2u) & 15u) == NEBULA_FACTOR_SRC_ALPHA &&
                                ((state >> 6u) & 15u) == NEBULA_FACTOR_INV_SRC_ALPHA))
                          ? 8u
                          : 0u);
        root.half_line = item.line_width * view.frame_height;
        root.half_point = item.point_size * view.frame_height;
        encoder.set_root(root);
        encoder.set_scissor(view.scissor);
        encoder.set_viewport(view.viewport);

        if (instanced) {
            // The vertex stage reads its instances from the rows: every stream the layout names reads zero.
            const gpu::StreamView zero{renderer_.zero_stream(16u), 16u, 0u};
            encoder.set_stream(gpu::kStreamPosition, zero);
            if (motion_form) {
                encoder.set_stream(gpu::kStreamBefore, zero);
            }
            encoder.set_stream(gpu::kStreamZero, zero);
            encoder.draw(item.count, item.ring_count, 0u);
        } else if (ring) {
            const std::uint32_t stride = item.mesh;
            const auto bytes = static_cast<std::uint32_t>(list.ring_vertex_bytes - item.ring_at);
            const gpu::Address at = walk_.ring_vertices + item.ring_at;
            encoder.set_stream(gpu::kStreamPosition, {at, bytes, stride});
            if (motion_form) {
                encoder.set_stream(gpu::kStreamBefore, {ring_item_before(i, at), bytes, stride});
            }
            encoder.set_stream(gpu::kStreamZero, {renderer_.zero_stream(16u), 16u, 0u});
            if ((item.flags & NEBULA_ITEM_INDEXED) != 0u) {
                encoder.set_indices(walk_.ring_indices, list.ring_index_count * 4u, 4u);
                encoder.draw_indexed(ring_index_count, 1u, item.first, 0);
            } else {
                encoder.draw(item.count, 1u, item.first);
            }
        } else if (mesh->site_stride != 0u) {
            const std::uint32_t bytes = mesh->vertex_count * mesh->site_stride;
            encoder.set_stream(gpu::kStreamPosition, {mesh->positions, bytes, mesh->site_stride});
            if (motion_form) {
                encoder.set_stream(gpu::kStreamBefore, {mesh->positions, bytes, mesh->site_stride});
            }
            encoder.set_stream(gpu::kStreamZero, {renderer_.zero_stream(16u), 16u, 0u});
            if (mesh->index_count != 0u) {
                encoder.set_indices(mesh->indices, mesh->index_count * 4u, 4u);
                encoder.draw_indexed(mesh->index_count, instances, 0u, 0);
            } else {
                encoder.draw(mesh->vertex_count, instances, 0u);
            }
        } else {
            const bool refined = curved_refine_ && mesh->refined_index_count != 0u && (item.flags & NEBULA_ITEM_PLAIN_MESH) == 0u;
            const std::uint32_t vertices = refined ? mesh->refined_vertex_count : mesh->vertex_count;
            encoder.set_stream(gpu::kStreamPosition, {mesh->positions, vertices * 12u, 12u});
            encoder.set_stream(gpu::kStreamAttributes, {mesh->attributes, vertices * 24u, 24u});
            const bool own_matrix = mesh->matrices != 0u && (item.flags & NEBULA_ITEM_ONE_MATRIX) == 0u;
            encoder.set_stream(gpu::kStreamMatrix, own_matrix ? gpu::StreamView{mesh->matrices, vertices * 4u, 4u}
                                                              : gpu::StreamView{renderer_.zero_stream(vertices * 4u), vertices * 4u, 4u});
            if (mesh->further != 0u) {
                encoder.set_stream(gpu::kStreamFurther, {mesh->further, vertices * mesh->further_stride, mesh->further_stride});
            }
            encoder.set_stream(gpu::kStreamZero, {renderer_.zero_stream(16u), 16u, 0u});
            const std::uint32_t index_count = refined ? mesh->refined_index_count : mesh->index_count;
            encoder.set_indices(refined ? mesh->refined_indices : mesh->indices, index_count * mesh->index_bytes, mesh->index_bytes);
            encoder.draw_indexed(index_count, instances, 0u, 0);
        }
        if (rt_prepass_walk_) {
            std::fill_n(rt_prepassed_.begin() + i, std::min<std::size_t>(instances, rt_prepassed_.size() - i),
                        rt_blended_walk_ ? std::uint8_t{2u} : std::uint8_t{1u});
        }
        i += instances;
    }
}

// A draw pass through the encoder as it is walked (the tracer's prepass).
void RenderCore::walk_draw_pass_direct(const NebulaFramePass& pass) {
    DirectDraws draws(renderer_.encoder(), renderer_);
    walk_draw_items(pass, draws);
}

gpu::Address RenderCore::ring_item_before(std::uint32_t index, gpu::Address own) const noexcept {
    if (walk_ring_previous_ == 0u) {
        return own;
    }
    const NebulaFrameItem& item = walk_.now->list->items[index];
    if (walk_.same_list_ring) {
        return walk_ring_previous_ + item.ring_at;
    }
    const std::uint32_t pair = walk_.now->item_before.empty() ? NEBULA_NONE32 : walk_.now->item_before[index];
    if (pair == NEBULA_NONE32) {
        return own;
    }
    const NebulaFrameItem& other = walk_.before->list->items[pair];
    if (!core::ring_shape_matches(item, other)) {
        return own;
    }
    return walk_ring_previous_ + other.ring_at;
}

// The sky's up and the flags a scene's views carry (walk_view), from its SCENE_BEGIN.
void RenderCore::set_scene_sky(const NebulaFramePass& scene_begin) {
    const NebulaFrameList& list = *walk_.now->list;
    const bool wide = wide_scene_;
    std::memcpy(sky_up_, list.sky_up, sizeof(sky_up_));
    view_flags_ = (list.sky_up_known != 0u ? 1u : 0u) | (enhanced_lighting_ ? 2u : 0u) | (wide ? 4u : 0u);
    if (scene_begin.scene_camera != NEBULA_NONE32 && walk_.between && list.sky_up_known != 0u) {
        eye_direction_at(list.sky_up, affine_of(list.cameras[scene_begin.scene_camera].view),
                         walk_cameras_[scene_begin.scene_camera], sky_up_);
    }
}

void RenderCore::walk_scene_begin(const NebulaFramePass& pass) {
    const NebulaFrameList& list = *walk_.now->list;
    const bool wide = wide_scene_;
    set_scene_sky(pass);
    scene_phase_ = true;
    std::array<float, 12> view_at{};
    const float* view_rows = nullptr;
    if (pass.scene_camera != NEBULA_NONE32) {
        std::memcpy(view_at.data(), walk_cameras_[pass.scene_camera].data(), sizeof(view_at));
        view_rows = view_at.data();
        renderer_.set_scene_view(&view_at);
    } else {
        renderer_.set_scene_view(nullptr);
    }
    collect_ao_objects_ = ambient_occlusion_;
    if (collect_ao_objects_) {
        ao_objects_.begin(view_rows);
    }
    temporal_scene_ = renderer_.scene_temporal_begin(scene_jitter_px_[0], scene_jitter_px_[1]);
    {
        const unsigned output_width = renderer_.temporal_output_width();
        const unsigned render_width = renderer_.frame_map().width;
        scene_mip_bias_ = 0.0f;
        if (temporal_scene_ && output_width > render_width && render_width != 0u) {
            scene_mip_bias_ = static_cast<float>(std::log2(static_cast<double>(render_width) / output_width));
        }
    }
    motion_scene_ = renderer_.begin_motion_scene();
    if (walk_.time.reset || walk_.now->cut) {
        renderer_.reset_temporal_history();
    }
    if (pass.scene_view != NEBULA_NONE32) {
        const std::array<float, 6>& terms = walk_view_terms_[pass.scene_view];
        if (list.views[pass.scene_view].orthographic == 0u) {
            renderer_.set_scene_camera(terms[5] / (terms[4] - 1.0f), terms[5] / terms[4], 2.0f * std::atan(1.0f / std::fabs(terms[2])));
        }
    }
    bool switched = false;
    if (renderer_.output_scene_active()) {
        renderer_.end_output_scene();
        switched = true;
    }
    renderer_.begin_wide_scene(wide);
    if (switched) {
        display_pictures_.set_frame_map(renderer_.frame_map());
    }
    ++walk_view_stamp_;
    if (rt_backend_.started()) {
        if (static_cast<std::uint32_t>(&pass - list.passes) == rt_traced_begin_) {
            rt_scene(pass);
            rt_prepass(pass);
        } else {
            rt_untraced();  // another scene this walk is the traced one
        }
    }
}

void RenderCore::walk_opaque_end(const NebulaFramePass& pass) {
    if (rt_backend_.started() && scene_phase_) {
        rt_blended(pass);
    }
    if (rt_backend_.started() && scene_phase_ && pass.scene_view != NEBULA_NONE32 &&
        ::galaxy::diagnosing(::galaxy::kDiagnoseRtView | ::galaxy::kDiagnoseRtCheck)) {
        rt_view(pass);
    }
    if (!ambient_occlusion_ || !scene_phase_ || pass.scene_view == NEBULA_NONE32) {
        return;
    }
    const NebulaFrameView& view = walk_.now->list->views[pass.scene_view];
    const ScaledViewport viewport = viewport_pixels(view.viewport, renderer_.frame_map());
    if (!viewport.valid()) {
        return;
    }
    renderer_.set_scene_ao_view(scene_depth_view(walk_view_terms_[pass.scene_view].data(), viewport));
    ao_objects_.finish();
    collect_ao_objects_ = false;
    renderer_.set_scene_ao_objects(ao_objects_.list());
    renderer_.record_scene_ao();
}

void RenderCore::walk_scene_end() {
    rt_program_signal();  // signals S and A: the scene as the programs drew it, before anything after the scene
    rt_reactive();
    // The camera's finish (scene_camera.hlsl): traced, relight style on, form above 0 (rt_look_[0]; 0 is Painted).
    const bool traced_finish = rt_light_on_ && rt_physical_ && rt_look_[0] > 0.0f;
    rt_light_end();
    scene_phase_ = false;
    temporal_scene_ = false;
    motion_scene_ = false;
    scene_mip_bias_ = 0.0f;
    collect_ao_objects_ = false;
    renderer_.end_wide_scene(traced_finish);
    // Once 3D is complete, post-AA and the 2D layer need one value per pixel. Keep all the selected
    // geometry samples through the resolve, without writing their identical post-AA result back N times.
    if (renderer_.begin_output_scene(true)) {
        display_pictures_.set_frame_map(renderer_.frame_map());
    }
    renderer_.record_scene_aa();
    if (renderer_.begin_output_scene()) {
        display_pictures_.set_frame_map(renderer_.frame_map());
    }
    note_layer_2d_lines(renderer_.frame_map().height);
    ++walk_view_stamp_;
}

void RenderCore::walk_target_begin(const NebulaFramePass& pass) {
    std::uint64_t key = pass.target;
    if (walk_.between) {
        key |= kGeneratedPicture;
        note_generated_written(pass.target);
    }
    const bool over_frame = pass.over_frame_texture != 0u || pass.over_frame_depth != 0u;
    if (over_frame) {
        std::uint64_t color = pass.over_frame_texture, depth = pass.over_frame_depth;
        if (walk_.between) {
            if (color != 0u) {
                color |= kGeneratedPicture;
                note_generated_written(pass.over_frame_texture);
            }
            if (depth != 0u) {
                depth |= kGeneratedPicture;
                note_generated_written(pass.over_frame_depth);
            }
        }
        renderer_.open_frame_for_pass(color, static_cast<std::uint8_t>(pass.over_frame_channels), depth, texture_store_);
    }
    const FrameMap& frame = renderer_.frame_map();
    const auto pixels = [&](std::uint32_t size, float scale) {
        return pass.pixels != 0u ? size : std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(std::lround(size * scale)));
    };
    if (!renderer_.begin_target_pass(key, pixels(pass.width, frame.sx), pixels(pass.height, frame.sy))) {
        if (over_frame) {
            renderer_.close_frame_for_pass(texture_store_);
        }
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fprintf(stderr, "[target] a pass into %x could not begin\n", pass.target);
        }
        return;
    }
    target_open_ = true;
    ++walk_view_stamp_;
    renderer_.clear_scene(pass.clear_rgba, true, true, true, 0u, 0u, kFrameUnits, kFrameUnits, 1u);
}

void RenderCore::walk_target_end(std::uint32_t channels) {
    renderer_.end_target_pass(static_cast<std::uint8_t>(channels), texture_store_);
    target_open_ = false;
    ++walk_view_stamp_;
}

// A target pass breaks the frame's pass where it stands: on a tiled GPU the frame target's colour, depth and stencil are
// stored and loaded again around it (about 100 MB at 2960x1848). One that reads nothing the list makes before it, whose
// picture nothing before it reads, and that reads no frame, is drawn first: its picture is the same, the frame unbroken.
void RenderCore::plan_early_targets(ListData& now) {
    now.targets_planned = true;
    now.early_targets.clear();
    const NebulaFrameList& list = *now.list;
    bool any = false;
    for (std::uint32_t p = 0; p < list.pass_count && !any; ++p) {
        const NebulaFramePass& pass = list.passes[p];
        any = pass.kind == NEBULA_PASS_DRAW && pass.layer == NEBULA_LAYER_TARGET && pass.load != 0u &&
              pass.over_frame_texture == 0u && pass.over_frame_depth == 0u;
    }
    if (!any) {
        return;
    }
    // Pictures made by the passes that stay in place, and the pictures their draws read, so far.
    std::vector<std::uint32_t> written, read, run_read;
    const auto has = [](const std::vector<std::uint32_t>& keys, std::uint32_t key) {
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    };
    const auto note_reads = [&](const NebulaFramePass& pass, std::vector<std::uint32_t>& into) {
        const std::uint32_t end = pass.first_item + pass.item_count;
        for (std::uint32_t i = pass.first_item; i < end; ++i) {
            const core::TextureTable& table = registries_.table(list.items[i].textures);
            for (std::uint32_t t = 0; t < table.count; ++t) {
                const std::uint32_t texture = table.list[t].texture;
                if (TextureStore::is_target(texture) && !has(into, texture)) {
                    into.push_back(texture);
                }
            }
        }
    };
    std::uint32_t scene = NEBULA_NONE32;
    for (std::uint32_t p = 0; p < list.pass_count;) {
        const NebulaFramePass& pass = list.passes[p];
        if (pass.kind == NEBULA_PASS_DRAW && pass.layer == NEBULA_LAYER_TARGET && pass.load != 0u) {
            // The run walk_list draws with the target open: through the next pass outside it, or the next target's start.
            std::uint32_t end = p + 1u;
            while (end < list.pass_count && list.passes[end].layer == NEBULA_LAYER_TARGET &&
                   !(list.passes[end].kind == NEBULA_PASS_DRAW && list.passes[end].load != 0u)) {
                ++end;
            }
            bool early = pass.over_frame_texture == 0u && pass.over_frame_depth == 0u && !has(written, pass.target) &&
                         !has(read, pass.target);
            run_read.clear();
            for (std::uint32_t q = p; q < end; ++q) {
                if (list.passes[q].kind == NEBULA_PASS_DRAW) {
                    note_reads(list.passes[q], run_read);
                } else if (list.passes[q].kind != NEBULA_PASS_CLEAR) {
                    early = false;
                }
            }
            for (const std::uint32_t key : run_read) {
                early = early && !has(written, key);
            }
            if (early) {
                now.early_targets.push_back(core::ListData::EarlyTargets{p, end, scene});
            } else {
                written.push_back(pass.target);
                if (pass.over_frame_texture != 0u) {
                    written.push_back(pass.over_frame_texture);
                }
                if (pass.over_frame_depth != 0u) {
                    written.push_back(pass.over_frame_depth);
                }
                for (const std::uint32_t key : run_read) {
                    if (!has(read, key)) {
                        read.push_back(key);
                    }
                }
            }
            p = end;
            continue;
        }
        switch (pass.kind) {
        case NEBULA_PASS_DRAW:
            note_reads(pass, read);
            break;
        case NEBULA_PASS_COPY_FRAME:
        case NEBULA_PASS_TAKE_FRAME:
            written.push_back(pass.target);
            break;
        case NEBULA_PASS_SCENE_BEGIN:
            scene = p;
            break;
        case NEBULA_PASS_SCENE_END:
            scene = NEBULA_NONE32;
            break;
        default:
            break;
        }
        ++p;
    }
}

// A probe answers with the scene depth where the game asked it. Asked while the scene pass is open, it waits for the
// pass's end, or ends it at the next draw or clear that changes the depth: a store and a load of the whole target on a
// tiled GPU. When nothing between the frame copy before it and the probe writes or clears the scene's depth, the depth
// right after that copy is the same, and the scene pass is closed there anyway: the probe is asked there.
void RenderCore::plan_early_probes(ListData& now) {
    now.early_probes.clear();
    const NebulaFrameList& list = *now.list;
    constexpr std::uint32_t depth_write = (1u << 10u) | (1u << 14u);
    const auto keeps_depth = [&](const NebulaFramePass& pass) {
        switch (pass.kind) {
        case NEBULA_PASS_PROBE:
        case NEBULA_PASS_TRACED_DRAW:  // draws nothing
            return true;
        case NEBULA_PASS_CLEAR:
            return pass.layer == NEBULA_LAYER_TARGET || (pass.clear_channels & 4u) == 0u;
        case NEBULA_PASS_DRAW: {
            if (pass.layer == NEBULA_LAYER_TARGET) {
                return true;  // into a target's own depth
            }
            const std::uint32_t end = pass.first_item + pass.item_count;
            for (std::uint32_t i = pass.first_item; i < end; ++i) {
                if ((registries_.pipeline(list.items[i].pipeline).desc.state & depth_write) == depth_write) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
        }
    };
    for (std::uint32_t p = 0; p < list.pass_count; ++p) {
        if (list.passes[p].kind != NEBULA_PASS_PROBE) {
            continue;
        }
        // Back to the scene's end if nothing between changes its depth (frame copies do not): asked there, the scene's
        // own depth is still the bound one (before an upscaler or the resolve trades the targets). Else after the
        // nearest frame copy, where the scene pass is closed anyway.
        std::uint32_t copy = NEBULA_NONE32;
        for (std::uint32_t q = p; q-- > 0u;) {
            const NebulaFramePass& pass = list.passes[q];
            if (pass.kind == NEBULA_PASS_SCENE_END) {
                copy = q;
                break;
            }
            if (pass.kind == NEBULA_PASS_COPY_FRAME || pass.kind == NEBULA_PASS_TAKE_FRAME) {
                if (copy == NEBULA_NONE32) {
                    copy = q;
                }
                continue;
            }
            if (!keeps_depth(pass)) {
                break;
            }
        }
        if (copy != NEBULA_NONE32) {
            now.early_probes.push_back(core::ListData::EarlyProbe{copy, p});
        }
    }
}

// A clear of the scene's depth that no later pass of the list reads or writes (the HUD's, before 2D draws that leave
// depth alone) is only seen by what comes after the list: the renderer may clear it as the next pass opens instead
// (defer_scene_depth_clear), where held probes would otherwise end the scene pass for it.
void RenderCore::plan_late_depth_clears(ListData& now) {
    now.late_depth_clears.clear();
    const NebulaFrameList& list = *now.list;
    const auto leaves_depth = [&](const NebulaFramePass& pass) {
        const std::uint32_t end = pass.first_item + pass.item_count;
        for (std::uint32_t i = pass.first_item; i < end; ++i) {
            const std::uint32_t state = registries_.pipeline(list.items[i].pipeline).desc.state;
            const bool depth = (state & (1u << 10u)) != 0u &&
                               (((state >> 11u) & 7u) != 7u || (state & (1u << 14u)) != 0u);  // a test, or a write
            if (depth || ((state >> 20u) & 3u) != 0u) {
                return false;
            }
        }
        return true;
    };
    for (std::uint32_t p = 0; p < list.pass_count; ++p) {
        const NebulaFramePass& clear = list.passes[p];
        if (clear.kind != NEBULA_PASS_CLEAR || clear.layer == NEBULA_LAYER_TARGET || (clear.clear_channels & 4u) == 0u) {
            continue;
        }
        bool late = true;
        for (std::uint32_t q = p + 1u; q < list.pass_count && late; ++q) {
            const NebulaFramePass& pass = list.passes[q];
            if (pass.layer == NEBULA_LAYER_TARGET) {
                late = false;
            } else if (pass.kind == NEBULA_PASS_CLEAR && (pass.clear_channels & 4u) != 0u) {
                break;  // the whole depth again
            } else if (pass.kind == NEBULA_PASS_DRAW) {
                late = leaves_depth(pass);
            } else if (pass.kind != NEBULA_PASS_CLEAR && pass.kind != NEBULA_PASS_SHOW && pass.kind != NEBULA_PASS_TRACED_DRAW) {
                late = false;
            }
        }
        if (late) {
            now.late_depth_clears.push_back(p);
        }
    }
}

void RenderCore::walk_list(ListData& now, ListData* before, const WalkTime& time) {
    prepare_walk(now, before, time);
    const NebulaFrameList& list = *now.list;
    rt_traced_begin_ = rt_backend_.started() ? rt_pick_scene() : NEBULA_NONE32;
    target_open_ = false;
    std::uint32_t target_channels = 0u;
    if (!now.targets_planned) {
        plan_early_targets(now);
        plan_early_probes(now);
        plan_late_depth_clears(now);
    }
    for (const core::ListData::EarlyTargets& run : now.early_targets) {
        if (run.scene != NEBULA_NONE32) {
            set_scene_sky(list.passes[run.scene]);  // as its views had it in place
        }
        for (std::uint32_t p = run.first; p < run.end; ++p) {
            walk_pass(now, p, target_channels);
        }
        if (target_open_) {
            walk_target_end(target_channels);
        }
    }
    std::size_t next_early = 0u;
    for (std::uint32_t p = 0; p < list.pass_count; ++p) {
        if (next_early < now.early_targets.size() && p == now.early_targets[next_early].first) {
            if (target_open_) {
                walk_target_end(target_channels);
            }
            p = now.early_targets[next_early++].end - 1u;  // drawn first
            continue;
        }
        walk_pass(now, p, target_channels);
        for (const core::ListData::EarlyProbe& early : now.early_probes) {
            if (early.after == p && list.passes[p].kind != NEBULA_PASS_SCENE_END && now.probes_due(walk_.between)) {
                const NebulaFramePass& probe = list.passes[early.probe];
                record_screen_probes(probe.probe_channel, list.probes + probe.first_probe, probe.probe_count,
                                     probe.arg0 == NEBULA_PROBE_COMPARED);
            }
        }
    }
    if (target_open_) {
        walk_target_end(target_channels);
    }
    // A generated frame asks nothing: its depth is the scene at t, and the points are the camera's at t = 1.
    now.probes_done = now.probes_done || !walk_.between;
}

void RenderCore::walk_pass(ListData& now, std::uint32_t p, std::uint32_t& target_channels) {
    const NebulaFrameList& list = *now.list;
    {
        const NebulaFramePass& pass = list.passes[p];
        if (pass.kind == NEBULA_PASS_TRACED_DRAW) {
            return;  // the tracer's alone (rt_walk.cpp): nothing drawn, no target closed
        }
        if (target_open_ && (pass.layer != NEBULA_LAYER_TARGET || (pass.kind == NEBULA_PASS_DRAW && pass.load != 0u))) {
            walk_target_end(target_channels);
        }
        switch (pass.kind) {
        case NEBULA_PASS_DRAW:
            if (pass.layer == NEBULA_LAYER_TARGET && pass.load != 0u) {
                walk_target_begin(pass);
            }
            if (pass.layer == NEBULA_LAYER_TARGET) {
                target_channels = pass.channels;
            }
            walk_draw_pass(pass);
            break;
        case NEBULA_PASS_CLEAR: {
            // The whole frame cleared just before the scene begins: the scene's own pass can open with the clear.
            const bool starts_scene = !target_open_ && (pass.clear_channels & 5u) == 5u &&
                p + 1u < list.pass_count && list.passes[p + 1u].kind == NEBULA_PASS_SCENE_BEGIN;
            const bool starts_hdr = starts_scene && wide_scene_ && !renderer_.temporal_mode();
            SceneClearValues clear = make_scene_clear_values(pass.clear_rgba, 0u);
            if (starts_hdr) {
                // The skipped SDR attachment would have rounded the clear before widening it to FP16.
                // Preserve that RGB10A2 conversion for non-black clears as well.
                for (unsigned c = 0u; c < 4u; ++c) {
                    const float levels = c == 3u ? 3.0f : 1023.0f;
                    clear.color[c] = std::round(clear.color[c] * levels) / levels;
                }
            }
            bool depth = (pass.clear_channels & 4u) != 0u;
            if (depth && !target_open_ &&
                std::find(now.late_depth_clears.begin(), now.late_depth_clears.end(), p) != now.late_depth_clears.end() &&
                renderer_.defer_scene_depth_clear(pass.clear_rgba)) {
                depth = false;
            }
            if ((!starts_hdr || !renderer_.begin_wide_scene(true, &clear)) &&
                (!starts_scene || starts_hdr || !renderer_.begin_cleared_motion_scene(pass.clear_rgba)) &&
                (depth || (pass.clear_channels & 3u) != 0u)) {
                renderer_.clear_scene(pass.clear_rgba, (pass.clear_channels & 1u) != 0u, (pass.clear_channels & 2u) != 0u,
                                      depth, 0u, 0u, kFrameUnits, kFrameUnits, target_open_ ? 1u : 0u);
            }
            if (pass.layer == NEBULA_LAYER_TARGET) {
                target_channels = pass.channels;
            }
            break;
        }
        case NEBULA_PASS_COPY_FRAME: {
            FramePassRequest request{};
            request.key = pass.target;
            if (walk_.between) {
                request.key |= kGeneratedPicture;
                note_generated_written(pass.target);
            }
            request.channels = pass.channels < kCopyLayoutDisplay ? static_cast<std::uint8_t>(pass.channels) : 0u;
            (void)renderer_.draw_frame_pass(request, texture_store_);
            break;
        }
        case NEBULA_PASS_TAKE_FRAME: {
            std::uint64_t key = pass.target;
            if (walk_.between) {
                key |= kGeneratedPicture;
                note_generated_written(pass.target);
            }
            if (renderer_.can_take_scene()) {
                const FrameMap& map = renderer_.frame_map();
                const auto width = static_cast<std::uint32_t>(map.width);
                const auto height = static_cast<std::uint32_t>(map.height);
                gpu::TextureRef released;
                (void)renderer_.take_scene(key, display_pictures_.take_idle_texture(width, height), texture_store_, released);
                display_pictures_.release_texture(std::move(released), width, height);
            } else {
                FramePassRequest request{};
                request.key = key;
                (void)renderer_.draw_frame_pass(request, texture_store_);
            }
            break;
        }
        case NEBULA_PASS_SCENE_BEGIN:
            walk_scene_begin(pass);
            break;
        case NEBULA_PASS_OPAQUE_END:
            walk_opaque_end(pass);
            break;
        case NEBULA_PASS_SCENE_END:
            // The probes asked after the scene with its depth unchanged (plan_early_probes): while it is still bound.
            if (now.probes_due(walk_.between)) {
                for (const core::ListData::EarlyProbe& early : now.early_probes) {
                    if (early.after == p) {
                        const NebulaFramePass& probe = list.passes[early.probe];
                        record_screen_probes(probe.probe_channel, list.probes + probe.first_probe, probe.probe_count,
                                             probe.arg0 == NEBULA_PROBE_COMPARED);
                    }
                }
            }
            walk_scene_end();
            break;
        case NEBULA_PASS_BLOOM_HOLD:
        case NEBULA_PASS_BLOOM_NORMAL:
        case NEBULA_PASS_BLOOM_SIMPLE:
            renderer_.game_bloom(pass.kind, pass.arg0, pass.arg1);
            break;
        case NEBULA_PASS_PROBE:
            // On the list's real walk (probes_due), so the points and the depth are of one camera; some were asked earlier.
            if (now.probes_due(walk_.between) && std::none_of(now.early_probes.begin(), now.early_probes.end(),
                                                 [p](const core::ListData::EarlyProbe& early) { return early.probe == p; })) {
                record_screen_probes(pass.probe_channel, list.probes + pass.first_probe, pass.probe_count,
                                     pass.arg0 == NEBULA_PROBE_COMPARED);
            }
            break;
        case NEBULA_PASS_SHOW:
            walk_show();
            break;
        default:
            break;
        }
    }
}

void RenderCore::walk_show() {
    FramePassRequest request{};
    request.display = true;
    const bool real = !walk_.between;
    if (real) {
        galaxy::timeline::record(galaxy::timeline::Event::DisplayCopy, stat_pictures_made_);
        picture_made_in_chunk_ = true;
    }
    DisplayTexture* picture = display_pictures_.acquire_display(request, frame_index_);
    if (picture == nullptr) {
        return;
    }
    const FrameMap& map = renderer_.frame_map();
    display_pictures_.adopt_picture(*picture, renderer_.take_frame_picture(display_pictures_.take_idle_texture(
                                                  static_cast<std::uint32_t>(map.width), static_cast<std::uint32_t>(map.height))));
    renderer_.copy_fg_hudless_display(*picture);
    if (renderer_.output_scene_active()) {
        renderer_.end_output_scene();
        display_pictures_.set_frame_map(renderer_.frame_map());
    }
    ++walk_view_stamp_;
    picture->pointer_response = render_pointer_response_;
    picture->prerecorded_movie = render_movie_content_;
    display_pictures_.mark_display_copied(*picture, frame_index_);
    if (real) {
        ++stat_pictures_made_;
        record_monotonic_cadence_sample(picture_made_cadence_);
    }
}

bool RenderCore::generated_written(std::uint32_t texture) const noexcept {
    const auto found = generated_written_.find(texture);
    return found != generated_written_.end() && found->second == walk_.serial;
}

void RenderCore::note_generated_written(std::uint32_t texture) {
    generated_written_[texture] = walk_.serial;
}

}  // namespace galaxy::render
