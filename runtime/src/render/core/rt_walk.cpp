// SPDX-License-Identifier: GPL-3.0-only
// RenderCore's part of ray tracing: when the tracer runs, which meshes it holds structures for, and what a frame
// builds (render/rt holds the policy, rt_backend.h the API).
#include "galaxy/render/render_core.h"

#include "galaxy/launch_args.h"
#include "galaxy/render/ambient_occlusion_shape.h"
#include "galaxy/render/program_table.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/rt_caps.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>

namespace galaxy::render {

using core::kRowsPerMatrix;

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
    rt_skin_buffer_ = rt::kNoBuffer;
    rt_skin_bytes_ = 0u;
    rt_skin_retired_.clear();
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
            if (now.block_camera[item.block] != begin.scene_camera || list.views[item.view].orthographic != 0u) {
                ++count.other_view;
                continue;
            }
            const core::PipelineEntry& pipeline = registries_.pipeline(item.pipeline);
            const std::uint32_t state = pipeline.desc.state;
            const bool depth_test = (state & 1u << 10u) != 0u;
            const bool depth_write = (state & 1u << 14u) != 0u;
            const std::uint32_t cull = state >> 15u & 3u;
            if (!depth_test || (state & NEBULA_STATE_COLOR_WRITE) == 0u || cull == NEBULA_CULL_ALL) {
                ++count.flat;
                continue;
            }
            const rt::Faces faces = cull == NEBULA_CULL_BACK ? rt::Faces::Front : cull == NEBULA_CULL_FRONT ? rt::Faces::Back : rt::Faces::Both;
            // A pixel program that can discard has a per-sample form (compile_programs.py): an alpha-tested surface.
            const std::uint32_t kind = program_variant(pipeline.desc.pixel, 0u, 1u) == kProgramSample ? rt::kKindCutout : 0u;
            const std::uint32_t mask =
                depth_write && (state & 3u) == 0u ? rt::kMaskOpaque | (early ? rt::kMaskEarly : 0u) : rt::kMaskBlended;
            if ((item.flags & (NEBULA_ITEM_INSTANCES | NEBULA_ITEM_PARTICLE | NEBULA_ITEM_WIDE)) != 0u) {
                ++count.sprites;
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
            rt_scene_.add(walk_rows_cpu_ + walk_block_rows_[item.block] + matrix * kRowsPerMatrix, i, mask, structure, faces, kind);
            count.triangles += triangles;
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
    static constexpr std::uint32_t kRingMasks[3] = {rt::kMaskOpaque | rt::kMaskEarly, rt::kMaskOpaque, rt::kMaskBlended};
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
        static_cast<void>(rt_scene_.build(at.gpu, frame_index_));
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
    std::erase_if(rt_view_retired_, [&](const std::pair<std::uint64_t, std::uint32_t>& retired) {
        if (retired.first + rt::kFramesToRelease > frame_index_) {
            return false;
        }
        rt_backend_.free_image(retired.second);
        return true;
    });
    const Renderer::RtCommands commands = renderer_.rt_commands_begin();
    rt::TextureView depth{};
    unsigned width = 0u, height = 0u;
    rt::ImageFormat format = rt::ImageFormat::Rgb10a2;
    if (renderer_.rt_depth_begin(depth, width, height, format)) {
        rt_backend_.begin(commands.commands, renderer_.frame_slot(), commands.descriptors);
        if (rt_view_image_ == rt::kNoImage || rt_view_width_ != width || rt_view_height_ != height || rt_view_format_ != format) {
            if (rt_view_image_ != rt::kNoImage) {
                rt_view_retired_.emplace_back(frame_index_, rt_view_image_);
            }
            rt_view_image_ = rt_backend_.make_image(format, width, height, "Nebula RT View");
            rt_view_width_ = width;
            rt_view_height_ = height;
            rt_view_format_ = format;
        }
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

}  // namespace galaxy::render
