// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/rt_caps.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace galaxy::render {

namespace {

// The record as bits: one atomic word, read without a lock.
std::atomic<std::uint32_t> s_caps{0u};

constexpr std::uint32_t pack(const RtCaps& c) noexcept {
    return (c.ray_queries ? 1u : 0u) | (c.acceleration_structures ? 2u : 0u) | (c.bindless ? 4u : 0u) | (c.shaders ? 8u : 0u) |
           (c.micromaps ? 16u : 0u) | (c.unified_memory ? 32u : 0u) | (c.enabled ? 64u : 0u);
}

constexpr RtCaps unpack(std::uint32_t bits) noexcept {
    RtCaps c;
    c.ray_queries = (bits & 1u) != 0u;
    c.acceleration_structures = (bits & 2u) != 0u;
    c.bindless = (bits & 4u) != 0u;
    c.shaders = (bits & 8u) != 0u;
    c.micromaps = (bits & 16u) != 0u;
    c.unified_memory = (bits & 32u) != 0u;
    c.enabled = (bits & 64u) != 0u;
    return c;
}

}  // namespace

void set_rt_caps(const RtCaps& caps) noexcept { s_caps.store(pack(caps), std::memory_order_relaxed); }

RtCaps rt_caps() noexcept { return unpack(s_caps.load(std::memory_order_relaxed)); }

void log_rt_caps(const char* api, const RtCaps& caps, bool wanted) {
    const char* const refusal = rt_refusal(caps);
    std::fprintf(stderr,
                 "[rt] caps %s: ray-queries=%d acceleration-structures=%d bindless=%d shaders=%d micromaps=%d unified-memory=%d "
                 "enabled=%d setting=%d: %s\n",
                 api, caps.ray_queries ? 1 : 0, caps.acceleration_structures ? 1 : 0, caps.bindless ? 1 : 0, caps.shaders ? 1 : 0,
                 caps.micromaps ? 1 : 0, caps.unified_memory ? 1 : 0, caps.enabled ? 1 : 0, wanted ? 1 : 0,
                 refusal != nullptr ? refusal : wanted ? "ray tracing can run" : "capable; the setting is off");
}

}  // namespace galaxy::render
