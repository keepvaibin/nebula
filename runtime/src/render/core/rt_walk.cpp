// SPDX-License-Identifier: GPL-3.0-only
// RenderCore's part of ray tracing: when the tracer runs, which meshes it holds structures for, and what a frame
// builds (render/rt holds the policy, rt_backend.h the API).
#include "galaxy/render/render_core.h"

#include "galaxy/launch_args.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/rt_caps.h"

#include <cstdio>

namespace galaxy::render {

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
    rt_geometry_.clear();
    rt_backend_.stop();
}

// A mesh one matrix places is traced through a structure built once from its vertices as they lie, in the form the
// scene draws (the refined one with curved surfaces on). Key: the mesh's number, twice, plus one for the refined form.
// A mesh whose vertices carry matrices of their own is skinned first (the scene's step), and a site's vertices are
// screen furniture.
void RenderCore::rt_want(std::uint32_t id, const core::GpuMesh& mesh) {
    if (mesh.site_stride != 0u || mesh.matrices != 0u || mesh.positions == 0u || mesh.indices == 0u) {
        return;
    }
    const bool refined = curved_refine_ && mesh.refined_index_count != 0u && mesh.refined_indices != 0u;
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
    rt_geometry_.frame(renderer_.rt_commands_begin(), renderer_.frame_slot(), frame_index_, budget);
    renderer_.rt_commands_end();
    const rt::GeometryStats& stats = rt_geometry_.stats();
    const bool diagnosing = ::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs);
    if (diagnosing && (stats.builds != 0u || stats.refits != 0u || stats.compactions != 0u)) {
        std::fprintf(stderr, "[rt] frame %llu: built %u (%llu triangles), refitted %u, compacted %u, waiting %u; %u structures, %.2f MB\n",
                     static_cast<unsigned long long>(frame_index_), stats.builds,
                     static_cast<unsigned long long>(stats.built_triangles), stats.refits, stats.compactions, stats.waiting,
                     stats.structures, static_cast<double>(stats.structure_bytes) / 1048576.0);
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

}  // namespace galaxy::render
