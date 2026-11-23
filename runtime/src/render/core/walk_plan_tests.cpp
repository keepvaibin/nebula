// SPDX-License-Identifier: GPL-3.0-only
// The draw walker's plans (render/core/walk_plan.h) over made-up lists: which target runs are drawn first and which scene
// depth clears wait for the next pass. No GPU: a lookup gives each pipeline's state word and
// each texture table's textures.
#include "render/core/walk_plan.h"
#include "render/core/walk_plan_shape.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
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
        // A run with a pass other than draws and clears (a native call inside it).
        List l;
        l.target(kTargetA, {{0u, 0u}});
        l.pass(NEBULA_PASS_NATIVE, NEBULA_LAYER_TARGET);
        check(targets_of(l).empty(), "a run with a native call stays in place");
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


// Lists recorded from tours (--diagnostics frame-plans, walk_plan_shape.h): each list's plans made again equal the plans
// written beside it. E4 rewrites the planners; a plan that changes on a real list is seen here, and the fixture is
// written again only with the change's reason in its commit (ledger P-044).
struct Recorded {
    std::vector<NebulaFramePass> passes;
    std::vector<NebulaFrameItem> items;
    std::vector<std::uint32_t> states;
    std::vector<std::vector<std::uint32_t>> reads;
    NebulaFrameList list{};
};

struct RecordedLookup {
    const Recorded& r;
    std::uint32_t state(std::uint32_t pipeline) const { return r.states[pipeline]; }
    template <class Fn>
    void textures(std::uint32_t table, Fn&& fn) const {
        for (const std::uint32_t texture : r.reads[table]) {
            fn(texture);
        }
    }
    static bool is_target(std::uint32_t texture) { return (texture & kTarget) != 0u; }
};

// One fixture file: its lists, each checked; the count of lists checked.
std::uint32_t test_recorded_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string line, wanted;
    Recorded r;
    std::uint32_t lists = 0u;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream words(line);
        std::string head;
        words >> head;
        if (head == "list") {
            r = Recorded{};
            wanted.clear();
            words >> r.list.game_frame;
        } else if (head == "p") {
            unsigned kind = 0, layer = 0, load = 0, clear = 0, count = 0;
            NebulaFramePass pass{};
            words >> kind >> layer >> load >> clear >> pass.target >> pass.over_frame_texture >> pass.over_frame_depth >> count;
            pass.kind = static_cast<std::uint8_t>(kind);
            pass.layer = static_cast<std::uint8_t>(layer);
            pass.load = static_cast<std::uint8_t>(load);
            pass.clear_channels = static_cast<std::uint8_t>(clear);
            pass.first_item = static_cast<std::uint32_t>(r.items.size());
            pass.item_count = count;
            r.passes.push_back(pass);
        } else if (head == "i") {
            std::uint32_t run = 0u, state = 0u, texture = 0u;
            words >> run >> state;
            std::vector<std::uint32_t> reads;
            while (words >> texture) {
                reads.push_back(texture);
            }
            for (std::uint32_t k = 0; k < run; ++k) {
                NebulaFrameItem item{};
                item.pipeline = static_cast<std::uint32_t>(r.states.size());
                item.textures = item.pipeline;
                r.states.push_back(state);
                r.reads.push_back(reads);
                r.items.push_back(item);
            }
        } else if (head == "plan") {
            wanted += line + "\n";
        } else if (head == "end") {
            std::uint32_t counted = 0u;
            for (const NebulaFramePass& pass : r.passes) {
                counted += pass.item_count;
            }
            check(counted == r.items.size(), "a recorded list's item runs add up to its passes' items");
            r.list.passes = r.passes.data();
            r.list.items = r.items.data();
            r.list.pass_count = static_cast<std::uint32_t>(r.passes.size());
            r.list.item_count = static_cast<std::uint32_t>(r.items.size());
            const RecordedLookup lookup{r};
            std::vector<EarlyTargets> targets;
            std::vector<std::uint32_t> clears;
            plan_early_targets(r.list, lookup, targets);
            plan_late_depth_clears(r.list, lookup, clears);
            std::string made;
            write_plans(targets, clears, made);
            if (made != wanted) {
                std::printf("FAIL: %s, list of frame %u: plans made\n%swritten\n%s", path.filename().string().c_str(),
                            r.list.game_frame, made.c_str(), wanted.c_str());
                ++failures;
            }
            ++lists;
        }
    }
    return lists;
}

void test_recorded_lists() {
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(WALK_PLAN_FIXTURES)) {
        if (entry.path().extension() == ".txt") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    std::uint32_t lists = 0u;
    for (const std::filesystem::path& file : files) {
        lists += test_recorded_file(file);
    }
    check(!files.empty() && lists >= 20u, "the recorded tours' lists are there (at least 20)");
    std::printf("walk plan tests: %u recorded lists from %u tours\n", lists, static_cast<unsigned>(files.size()));
}

}  // namespace

int main() {
    test_early_targets();
    test_late_depth_clears();
    test_recorded_lists();
    if (failures == 0) {
        std::printf("walk plan tests: all pass\n");
    }
    return failures == 0 ? 0 : 1;
}
