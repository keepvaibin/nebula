// SPDX-License-Identifier: GPL-3.0-only
// The draw walker's plans (render/core/walk_plan.h) over made-up lists: which target runs are drawn first, where probes
// are asked, which scene depth clears wait for the next pass. No GPU: a lookup gives each pipeline's state word and
// each texture table's textures.
#include "render/core/walk_plan.h"

#include <cstdio>
#include <vector>

using namespace galaxy::render::core;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr std::uint32_t kTarget = 0x80000000u;  // TextureStore::kTargetBit
constexpr std::uint32_t kTargetA = kTarget | 1u, kTargetB = kTarget | 2u, kTargetC = kTarget | 3u;

// State words (nebula_model.h): depth test bit 10, its compare bits 11-13 (7 always), depth write bit 14, stencil 20-21.
constexpr std::uint32_t kNoDepth = 0u;
constexpr std::uint32_t kDepthTest = (1u << 10u) | (3u << 11u);
constexpr std::uint32_t kDepthWrite = (1u << 10u) | (7u << 11u) | (1u << 14u);
constexpr std::uint32_t kStencil = 1u << 20u;
// Pipelines 0..3 have those states.
const std::uint32_t pipeline_states[] = {kNoDepth, kDepthTest, kDepthWrite, kStencil};

// Table t names the textures tables[t].
const std::vector<std::vector<std::uint32_t>> tables = {{}, {kTargetA}, {kTargetB}, {5u, kTargetC}, {7u}};

struct Lookup {
    std::uint32_t state(std::uint32_t pipeline) const { return pipeline_states[pipeline]; }
    template <class Fn>
    void textures(std::uint32_t table, Fn&& fn) const {
        for (const std::uint32_t texture : tables[table]) {
            fn(texture);
        }
    }
    static bool is_target(std::uint32_t texture) { return (texture & kTarget) != 0u; }
};

// A list being made: passes, and items each with a pipeline and a texture table.
struct List {
    std::vector<NebulaFramePass> passes;
    std::vector<NebulaFrameItem> items;
    NebulaFrameList list{};

    NebulaFramePass& pass(std::uint8_t kind, std::uint8_t layer = NEBULA_LAYER_SCREEN) {
        NebulaFramePass p{};
        p.kind = kind;
        p.layer = layer;
        passes.push_back(p);
        return passes.back();
    }
    // A draw pass of items, each {pipeline, table}.
    NebulaFramePass& draw(std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> drawn,
                          std::uint8_t layer = NEBULA_LAYER_SCREEN) {
        NebulaFramePass& p = pass(NEBULA_PASS_DRAW, layer);
        p.first_item = static_cast<std::uint32_t>(items.size());
        for (const auto& [pipeline, table] : drawn) {
            NebulaFrameItem item{};
            item.pipeline = pipeline;
            item.textures = table;
            items.push_back(item);
        }
        p.item_count = static_cast<std::uint32_t>(drawn.size());
        return p;
    }
    // A draw into a target that clears it first.
    NebulaFramePass& target(std::uint32_t texture, std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> drawn) {
        NebulaFramePass& p = draw(drawn, NEBULA_LAYER_TARGET);
        p.target = texture;
        p.load = 1u;
        return p;
    }
    NebulaFramePass& clear(std::uint32_t channels, std::uint8_t layer = NEBULA_LAYER_SCREEN) {
        NebulaFramePass& p = pass(NEBULA_PASS_CLEAR, layer);
        p.clear_channels = static_cast<std::uint8_t>(channels);
        return p;
    }
    const NebulaFrameList& done() {
        list.passes = passes.data();
        list.pass_count = static_cast<std::uint32_t>(passes.size());
        list.items = items.data();
        list.item_count = static_cast<std::uint32_t>(items.size());
        return list;
    }
};

std::vector<EarlyTargets> targets_of(List& l) {
    std::vector<EarlyTargets> out{EarlyTargets{9u, 9u, 9u}};  // a plan replaces what was there
    plan_early_targets(l.done(), Lookup{}, out);
    return out;
}

std::vector<EarlyProbe> probes_of(List& l) {
    std::vector<EarlyProbe> out{EarlyProbe{9u, 9u}};
    plan_early_probes(l.done(), Lookup{}, out);
    return out;
}

std::vector<std::uint32_t> late_clears_of(List& l) {
    std::vector<std::uint32_t> out{9u};
    plan_late_depth_clears(l.done(), Lookup{}, out);
    return out;
}

void test_early_targets() {
    {
        List l;
        l.draw({{0u, 0u}});
        l.pass(NEBULA_PASS_SHOW);
        check(targets_of(l).empty(), "no target pass: nothing first");
    }
    {
        // A clean target run, drawn first; its run goes through the target's clear to the first screen pass.
        List l;
        l.pass(NEBULA_PASS_SCENE_BEGIN);
        l.target(kTargetA, {{0u, 0u}});
        l.clear(1u, NEBULA_LAYER_TARGET);
        l.draw({{0u, 1u}});  // the screen reads the target
        l.pass(NEBULA_PASS_SCENE_END);
        const auto plan = targets_of(l);
        check(plan.size() == 1u && plan[0].first == 1u && plan[0].end == 3u, "a clean target run is drawn first");
        check(plan.size() == 1u && plan[0].scene == 0u, "and knows the scene it sits in");
    }
    {
        // Two runs, the second reading the first's target: both first, in list order.
        List l;
        l.target(kTargetA, {{0u, 0u}});
        l.target(kTargetB, {{0u, 1u}});
        l.draw({{0u, 2u}});
        const auto plan = targets_of(l);
        check(plan.size() == 2u && plan[0].first == 0u && plan[1].first == 1u && plan[1].end == 2u,
              "a run reading an earlier first run is first too, after it");
        check(plan.size() == 2u && plan[0].scene == NEBULA_NONE32, "outside a scene");
    }
    {
        // A target a screen draw read before the run: drawn first, the read would see the new picture.
        List l;
        l.draw({{0u, 1u}});
        l.target(kTargetA, {{0u, 0u}});
        check(targets_of(l).empty(), "a target read before its run stays in place");
    }
    {
        // A run reading a picture copied from the frame before it.
        List l;
        l.pass(NEBULA_PASS_COPY_FRAME).target = kTargetB;
        l.target(kTargetA, {{0u, 2u}});
        check(targets_of(l).empty(), "a run reading a frame copy made before it stays in place");
    }
    {
        // A target written by a run that stays in place, then cleared and drawn again.
        List l;
        l.pass(NEBULA_PASS_COPY_FRAME).target = kTargetB;
        l.target(kTargetA, {{0u, 2u}});  // in place: reads B
        l.target(kTargetA, {{0u, 0u}});  // A again
        check(targets_of(l).empty(), "a target written in place before stays in place");
    }
    {
        // A run with a pass other than draws and clears (a probe inside it).
        List l;
        l.target(kTargetA, {{0u, 0u}});
        l.pass(NEBULA_PASS_PROBE, NEBULA_LAYER_TARGET);
        check(targets_of(l).empty(), "a run with a probe stays in place");
    }
    {
        // A target pass reading the open frame.
        List l;
        l.target(kTargetA, {{0u, 0u}}).over_frame_texture = kTargetC;
        check(targets_of(l).empty(), "a run over the open frame stays in place");
    }
    {
        // A run reading a texture that is no target (an image): free to go first.
        List l;
        l.target(kTargetA, {{0u, 4u}});
        check(targets_of(l).size() == 1u, "images do not hold a run in place");
    }
}

void test_early_probes() {
    {
        List l;
        l.pass(NEBULA_PASS_SCENE_END);
        l.pass(NEBULA_PASS_PROBE);
        const auto plan = probes_of(l);
        check(plan.size() == 1u && plan[0].after == 0u && plan[0].probe == 1u, "a probe right after the scene's end");
    }
    {
        // Frame copies and draws that leave depth alone lie between: still asked at the scene's end.
        List l;
        l.pass(NEBULA_PASS_SCENE_END);
        l.pass(NEBULA_PASS_COPY_FRAME);
        l.draw({{0u, 0u}, {1u, 0u}});  // no depth, and a test without a write
        l.clear(1u);                     // colour only
        l.pass(NEBULA_PASS_PROBE);
        const auto plan = probes_of(l);
        check(plan.size() == 1u && plan[0].after == 0u && plan[0].probe == 4u, "back past copies to the scene's end");
    }
    {
        // A depth write before the copy: asked after the copy.
        List l;
        l.pass(NEBULA_PASS_SCENE_END);
        l.draw({{2u, 0u}});
        l.pass(NEBULA_PASS_TAKE_FRAME);
        l.pass(NEBULA_PASS_PROBE);
        const auto plan = probes_of(l);
        check(plan.size() == 1u && plan[0].after == 2u, "after the nearest copy when depth changed before it");
    }
    {
        // A depth write with no copy after it: not early.
        List l;
        l.pass(NEBULA_PASS_SCENE_END);
        l.draw({{2u, 0u}});
        l.pass(NEBULA_PASS_PROBE);
        check(probes_of(l).empty(), "a depth write between: asked in place");
    }
    {
        // A depth clear on the screen between: not early; a target's depth clear is not the scene's.
        List l;
        l.pass(NEBULA_PASS_SCENE_END);
        l.clear(4u, NEBULA_LAYER_TARGET);
        l.draw({{2u, 0u}}, NEBULA_LAYER_TARGET);
        l.pass(NEBULA_PASS_PROBE);
        check(probes_of(l).size() == 1u, "a target's depth is not the scene's");
        List m;
        m.pass(NEBULA_PASS_SCENE_END);
        m.clear(4u);
        m.pass(NEBULA_PASS_PROBE);
        check(probes_of(m).empty(), "a scene depth clear between: asked in place");
    }
    {
        List l;
        l.pass(NEBULA_PASS_PROBE);
        check(probes_of(l).empty(), "a probe with nothing before it: in place");
    }
}

void test_late_depth_clears() {
    {
        // The HUD's clear before 2D draws that leave depth alone.
        List l;
        l.clear(4u | 1u);
        l.draw({{0u, 0u}, {0u, 4u}});
        l.pass(NEBULA_PASS_SHOW);
        const auto plan = late_clears_of(l);
        check(plan.size() == 1u && plan[0] == 0u, "a depth clear nothing after reads is late");
    }
    {
        List l;
        l.clear(4u);
        l.draw({{1u, 0u}});  // a depth test
        check(late_clears_of(l).empty(), "a later depth test reads it");
    }
    {
        List l;
        l.clear(4u);
        l.draw({{3u, 0u}});  // stencil
        check(late_clears_of(l).empty(), "a later stencil use keeps it in place");
    }
    {
        List l;
        l.clear(4u);
        l.target(kTargetA, {{0u, 0u}});
        check(late_clears_of(l).empty(), "a target pass after keeps it in place");
    }
    {
        List l;
        l.clear(4u);
        l.pass(NEBULA_PASS_PROBE);
        check(late_clears_of(l).empty(), "a probe after reads the depth");
    }
    {
        // Cleared again whole before anything reads it: both clears are late.
        List l;
        l.clear(4u);
        l.clear(4u);
        l.draw({{0u, 0u}});
        const auto plan = late_clears_of(l);
        check(plan.size() == 2u && plan[0] == 0u && plan[1] == 1u, "a clear cleared again before use is late");
    }
    {
        List l;
        l.clear(1u);
        l.clear(4u, NEBULA_LAYER_TARGET);
        check(late_clears_of(l).empty(), "colour clears and target clears are not scene depth clears");
    }
}

}  // namespace

int main() {
    test_early_targets();
    test_early_probes();
    test_late_depth_clears();
    if (failures == 0) {
        std::printf("walk plan tests: all pass\n");
    }
    return failures == 0 ? 0 : 1;
}
