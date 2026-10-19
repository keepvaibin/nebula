// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Frame interpolation's renderer-facing records (RendererD3D12 replays them; gx_backend fills them for either
// renderer).

#include "galaxy/gx/interp_gpu.h"

#include <cstdint>
#include <span>

namespace galaxy::gx {

// One recorded game frame's replay: the evaluation program and where each record's constants come from.
struct ReplaySequence {
    interp_gpu::Layout layout{};
    std::span<const std::int32_t> record_vs_slot;       // per scene record, -1 none
    std::span<const std::int32_t> record_palette_slot;  // per scene record, -1 none
    std::span<const std::int32_t> ps_slot;              // per pixel-constant record, -1 none
    std::span<const std::uint32_t> palette_source;      // per palette slot: float4 offset in the matrix ring
    const interp_gpu::Program* program = nullptr;
    bool vertex_blend = false;
};

// A presented frame of the sequence: t, the t of the frame presented before it (motion vectors) and its jitter in
// render pixels (temporal upscaling).
struct SubframeTime {
    float t = 0.0f;
    float t_prev = 0.0f;
    float jitter_x = 0.0f;
    float jitter_y = 0.0f;
    bool reset = false;  // the upscaler's history starts again (cut, gap)
};

struct CopyRedirectStats {
    std::uint64_t redirected = 0;
    std::uint64_t patched_tables = 0;
    std::uint64_t snapshots = 0;
    std::uint64_t fallback_logs = 0;
};

}  // namespace galaxy::gx
