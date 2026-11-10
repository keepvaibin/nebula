// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The world lights of a frame list (nebula_frame_list.h NebulaFrameWorldLight): the rules whatever reads them relies on,
// checked, and what a run's lists held. No GPU, no Windows: tested offline.

#include <nebula_frame_list.h>

#include <cstdint>

namespace galaxy::render::core {

// What a list breaks, by rule; all zero when it keeps every one.
struct WorldLightFaults {
    std::uint32_t order = 0u;         // ids not ascending, or one id twice
    std::uint32_t kind = 0u;          // a kind that is none of NEBULA_WORLD_LIGHT_*
    std::uint32_t values = 0u;        // a colour, place or reach that is not finite
    std::uint32_t owner = 0u;         // a light set's owner names a world light the list does not hold
    std::uint32_t stage = 0u;         // the stage light is known but its two lights are not in the list
    std::uint32_t changed_kind = 0u;  // an id that was another kind of light on the list before
    [[nodiscard]] std::uint32_t total() const noexcept { return order + kind + values + owner + stage + changed_kind; }
};

// The world light `id` of `list`, or null.
[[nodiscard]] const NebulaFrameWorldLight* find_world_light(const NebulaFrameList& list, std::uint64_t id) noexcept;

// `list` against the rules, and against the list `before` it (null: none).
[[nodiscard]] WorldLightFaults check_world_lights(const NebulaFrameList& list, const NebulaFrameList* before) noexcept;

// What the lists of a run held (--diagnostics rt-inputs).
struct WorldLightStats {
    std::uint64_t lists = 0u, lights = 0u, faults = 0u;
    std::uint32_t most = 0u;
    std::uint64_t by_kind[NEBULA_WORLD_LIGHT_KIND_COUNT]{};
    std::uint64_t keys_following = 0u;  // key lights whose place moves with the camera
    std::uint64_t lists_with_stage = 0u;

    void add(const NebulaFrameList& list, const NebulaFrameList* before) noexcept;
    // One line to the log, then the counts start again.
    void report() noexcept;
};

}  // namespace galaxy::render::core
