// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The draw walker's plans for a list, made once before its first walk (draw_walker.cpp RenderCore::walk_list): target
// passes drawn before the rest, and scene depth clears left for the next pass to make. Each reads
// the list and only what a Lookup says of it, so a test plans a list with no GPU (walk_plan_tests.cpp):
//   std::uint32_t state(std::uint32_t pipeline) const   the pipeline's state word (NEBULA_STATE_*, nebula_model.h)
//   void textures(std::uint32_t table, Fn fn) const     fn(texture) for each texture the table names
//   bool is_target(std::uint32_t texture) const         the texture is a target a pass draws into

#include <nebula_frame_list.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace galaxy::render::core {

// Target passes drawn before the list's other passes: passes [first, end), and the SCENE_BEGIN they sit after in the
// list, or none.
struct EarlyTargets {
    std::uint32_t first = 0u, end = 0u, scene = NEBULA_NONE32;
};

// A target pass that clears and draws (load) in place: when nothing before it in the list wrote its target or reads it,
// and nothing it reads was written before it, its run is drawn first, before the frame's own passes.
template <class Lookup>
void plan_early_targets(const NebulaFrameList& list, const Lookup& lookup, std::vector<EarlyTargets>& out) {
    out.clear();
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
            lookup.textures(list.items[i].textures, [&](std::uint32_t texture) {
                if (lookup.is_target(texture) && !has(into, texture)) {
                    into.push_back(texture);
                }
            });
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
                out.push_back(EarlyTargets{p, end, scene});
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

// A clear of the scene's depth that no later pass of the list reads or writes (the HUD's, before 2D draws that leave
// depth alone) is only seen by what comes after the list: the renderer may clear it as the next pass opens instead
// (defer_scene_depth_clear).
template <class Lookup>
void plan_late_depth_clears(const NebulaFrameList& list, const Lookup& lookup, std::vector<std::uint32_t>& out) {
    out.clear();
    const auto leaves_depth = [&](const NebulaFramePass& pass) {
        const std::uint32_t end = pass.first_item + pass.item_count;
        for (std::uint32_t i = pass.first_item; i < end; ++i) {
            const std::uint32_t state = lookup.state(list.items[i].pipeline);
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
            } else if (pass.kind != NEBULA_PASS_CLEAR && pass.kind != NEBULA_PASS_SHOW) {
                late = false;
            }
        }
        if (late) {
            out.push_back(p);
        }
    }
}

}  // namespace galaxy::render::core
