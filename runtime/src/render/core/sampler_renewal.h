// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A renderer's samplers start again beside the ones frames in flight read (FRAME-MODEL 2.1 rule 1, 2.15 item 7's form):
// between frames, with nothing recorded, when the settings they were made under changed, when the sampler array is nearly
// full, or when as many samplers were made as the device should keep. The renderer makes a new array (or a new default
// table for bound tables) beside the old, filled with the default sampler, and puts it in use; the old array and the
// samplers given up go to the retire ring and are destroyed after this frame slot's fence. Nothing waits for the GPU and
// nothing is drained inside a frame. The walker writes its lists and tables again (the generation).

#include "galaxy/render/renderer_common.h"
#include "render/core/retire_ring.h"

#include <cstddef>
#include <cstdint>

namespace galaxy::render::core {

// The state a renderer keeps for it: its slot cache (none for bound tables), the settings' key the samplers in use were
// made under, whether the first ones were made, and the generation the walker compares.
struct SamplerState {
    SamplerSlotCache* slots;  // null for bound tables
    std::uint32_t& config;
    bool& ready;
    std::uint32_t& generation;
};

// Renderer: samplers_made() (the distinct samplers it holds, of every settings), and make_samplers(ring, slot, config,
// all): makes the new array or default table beside the old for settings `config`, retires the old to `ring` at
// `slot`, and retires the samplers of other settings, or all of them when `all`. array_slots and room: the array's
// length and the slots a frame may add (unused for bound tables). crowd: the samplers made before they start again.
// True when they started again (and it was not the first time: `restarted`).
template <class Renderer>
bool renew_samplers(Renderer& renderer, RetireRing& ring, unsigned slot, SamplerState state, std::uint32_t config,
                    unsigned array_slots, unsigned room, std::size_t crowd, bool* restarted = nullptr) {
    const bool full = state.slots != nullptr && state.slots->used() + room > array_slots;
    const bool crowded = renderer.samplers_made() >= crowd;
    if (state.ready && config == state.config && !full && !crowded) {
        return false;
    }
    if (restarted != nullptr) {
        *restarted = state.ready;
    }
    renderer.make_samplers(ring, slot, config, crowded);
    if (state.slots != nullptr) {
        state.slots->reset();
    }
    state.ready = true;
    state.config = config;
    ++state.generation;
    return true;
}

}  // namespace galaxy::render::core
