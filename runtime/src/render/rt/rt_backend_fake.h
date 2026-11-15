// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A stand-in for the tracer's back end (rt_backend.h), for tests of what drives it: no GPU. It keeps what was recorded
// and counts what a real back end would not forgive.

#include <cstdint>

namespace galaxy::render::rt::fake {

struct Log {
    std::uint32_t builds = 0u, refits = 0u, barriers = 0u, compactions = 0u, asks = 0u;
    std::uint32_t scenes = 0u;           // top-level builds
    std::uint32_t images = 0u, passes = 0u, buffers = 0u;
    std::uint32_t scene_instances = 0u;  // of the last one
    std::uint32_t structures = 0u;  // alive now
    std::uint32_t micromaps = 0u, micromap_builds = 0u;  // alive now; built
    std::uint32_t pages = 0u;       // alive now
    // What must stay zero: two builds between barriers sharing scratch, a build outside its pages, a refit or a copy of
    // a structure not finished by a barrier, a structure named after it was freed.
    std::uint32_t faults = 0u;
};

// The log since the last reset (the back end's start resets it too).
[[nodiscard]] Log& log() noexcept;
// The stand-in's sizes for `triangles`: a structure, a build's scratch, a refit's scratch, and the compacted size it
// answers for a structure in a range of `bytes`.
[[nodiscard]] std::uint64_t structure_bytes(std::uint32_t triangles) noexcept;
[[nodiscard]] std::uint64_t compacted_bytes(std::uint64_t bytes) noexcept;
[[nodiscard]] std::uint64_t scene_bytes(std::uint32_t instances) noexcept;

}  // namespace galaxy::render::rt::fake
