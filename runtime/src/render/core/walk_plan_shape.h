// SPDX-License-Identifier: GPL-3.0-only
// A frame list's shape as the walk's plans read it (walk_plan.h), as text, with the plans made of it. Each pass: its
// kind, layer, load, depth clear, target, reads of the open frame and its item count; each run of like items: how many,
// the pipeline's state word and the target textures its table names. Nothing of the game's content (no mesh,
// constant, texture pixel or name). --diagnostics frame-plans writes it from a run (draw_walker.cpp); walk_plan_tests.cpp
// reads the lists of a tour back, makes the plans again and compares them with the plans written beside each list
// (ledger P-044, file 07 F2: the guard for E4's rewrite of the planners).
#pragma once

#include "render/core/walk_plan.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace galaxy::render::core {

// The plans as one line each: "plan targets" with first, end and scene of each run; "plan clears" with each clear's
// pass.
inline void write_plans(const std::vector<EarlyTargets>& targets, const std::vector<std::uint32_t>& clears,
                        std::string& out) {
    char word[48];
    out += "plan targets";
    for (const EarlyTargets& run : targets) {
        std::snprintf(word, sizeof(word), " %u %u %u", run.first, run.end, run.scene);
        out += word;
    }
    out += "\nplan clears";
    for (const std::uint32_t clear : clears) {
        std::snprintf(word, sizeof(word), " %u", clear);
        out += word;
    }
    out += '\n';
}

template <class Lookup>
void write_plan_shape(const NebulaFrameList& list, const Lookup& lookup, const std::vector<EarlyTargets>& targets,
                      const std::vector<std::uint32_t>& clears, std::string& out) {
    char word[160];
    std::snprintf(word, sizeof(word), "list %u %u\n", list.game_frame, list.pass_count);
    out += word;
    std::vector<std::uint32_t> reads, run_reads;
    const auto flush = [&](std::uint32_t run, std::uint32_t state) {
        if (run == 0u) {
            return;
        }
        std::snprintf(word, sizeof(word), "i %u %u", run, state);
        out += word;
        for (const std::uint32_t texture : run_reads) {
            std::snprintf(word, sizeof(word), " %u", texture);
            out += word;
        }
        out += '\n';
    };
    for (std::uint32_t p = 0; p < list.pass_count; ++p) {
        const NebulaFramePass& pass = list.passes[p];
        std::snprintf(word, sizeof(word), "p %u %u %u %u %u %u %u %u\n", pass.kind, pass.layer, pass.load,
                      pass.clear_channels, pass.target, pass.over_frame_texture, pass.over_frame_depth, pass.item_count);
        out += word;
        std::uint32_t run = 0u, run_state = 0u;
        for (std::uint32_t i = pass.first_item; i < pass.first_item + pass.item_count; ++i) {
            const std::uint32_t state = lookup.state(list.items[i].pipeline);
            reads.clear();
            lookup.textures(list.items[i].textures, [&](std::uint32_t texture) {
                if (lookup.is_target(texture)) {
                    reads.push_back(texture);
                }
            });
            if (run != 0u && state == run_state && reads == run_reads) {
                ++run;
                continue;
            }
            flush(run, run_state);
            run = 1u;
            run_state = state;
            run_reads = reads;
        }
        flush(run, run_state);
    }
    write_plans(targets, clears, out);
    out += "end\n";
}

}  // namespace galaxy::render::core
