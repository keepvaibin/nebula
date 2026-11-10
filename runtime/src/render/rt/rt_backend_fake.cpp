// SPDX-License-Identifier: GPL-3.0-only
#include "render/rt/rt_backend_fake.h"

#include "galaxy/render/rt/rt_backend.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace galaxy::render::rt {

namespace {

fake::Log g_log;

}  // namespace

fake::Log& fake::log() noexcept { return g_log; }

std::uint64_t fake::structure_bytes(std::uint32_t triangles) noexcept { return std::uint64_t{triangles} * 64u + 1000u; }

std::uint64_t fake::compacted_bytes(std::uint64_t bytes) noexcept { return bytes * 2u / 5u; }

bool shaders_load(gpu::Device*) { return true; }

struct Backend::State {
    std::vector<std::uint64_t> pages;                  // bytes, 0: freed
    std::unordered_set<std::uint64_t> alive;           // structure names
    std::unordered_set<std::uint64_t> finished;        // of them, built before a barrier
    std::unordered_set<std::uint64_t> building;        // built since the last barrier
    std::vector<std::pair<std::uint64_t, std::uint64_t>> scratch;  // (page << 48 | offset, size) since the last barrier
    std::uint64_t answers[kFramesInFlight][kSizeSlots]{};
    unsigned frame_slot = 0u;
};

Backend::Backend() = default;

Backend::~Backend() { stop(); }

bool Backend::started() const noexcept { return state_ != nullptr; }

bool Backend::start(gpu::Device*) {
    stop();
    state_ = new State{};
    g_log = fake::Log{};
    return true;
}

void Backend::stop() noexcept {
    delete state_;
    state_ = nullptr;
}

BuildSizes Backend::sizes(const TriangleInput& input, BuildKind) const {
    const std::uint32_t triangles = input.triangles();
    return {fake::structure_bytes(triangles), std::uint64_t{triangles} * 16u + 100u, std::uint64_t{triangles} * 4u + 100u};
}

std::uint32_t Backend::make_page(std::uint64_t bytes, bool) {
    state_->pages.push_back(bytes);
    ++g_log.pages;
    return static_cast<std::uint32_t>(state_->pages.size() - 1u);
}

void Backend::free_page(std::uint32_t page) noexcept {
    if (state_ != nullptr && page < state_->pages.size() && state_->pages[page] != 0u) {
        state_->pages[page] = 0u;
        --g_log.pages;
    }
}

Structure Backend::make_structure(const Range& range, bool) {
    Structure out;
    out.range = range;
    out.name = (std::uint64_t{range.page} + 1u) << 48u | range.offset;
    if (range.page >= state_->pages.size() || range.offset + range.size > state_->pages[range.page] || !state_->alive.insert(out.name).second) {
        ++g_log.faults;  // outside its page, or over a structure that is still there
    }
    ++g_log.structures;
    return out;
}

void Backend::free_structure(Structure& structure) noexcept {
    if (state_ != nullptr && structure.name != 0u) {
        g_log.faults += state_->alive.erase(structure.name) == 1u ? 0u : 1u;
        state_->finished.erase(structure.name);
        state_->building.erase(structure.name);
        --g_log.structures;
    }
    structure = Structure{};
}

void Backend::begin(void*, unsigned frame_slot) noexcept {
    state_->frame_slot = frame_slot % kFramesInFlight;
    // A frame's commands follow the frame before's: what that built is whole.
    barrier();
    --g_log.barriers;
}

void Backend::build(const TriangleInput& input, BuildKind, const Structure& to, const Range& scratch, bool refit) {
    ++(refit ? g_log.refits : g_log.builds);
    const std::uint64_t need = refit ? std::uint64_t{input.triangles()} * 4u + 100u : std::uint64_t{input.triangles()} * 16u + 100u;
    if (!state_->alive.contains(to.name) || scratch.size < need || scratch.page >= state_->pages.size() ||
        scratch.offset + scratch.size > state_->pages[scratch.page]) {
        ++g_log.faults;
    }
    if (refit && !state_->finished.contains(to.name)) {
        ++g_log.faults;  // a refit of what no barrier finished
    }
    const std::uint64_t at = std::uint64_t{scratch.page} << 48u | scratch.offset;
    for (const auto& [other, size] : state_->scratch) {
        if (at < other + size && other < at + scratch.size) {
            ++g_log.faults;  // two builds in one scratch range with no barrier between
        }
    }
    state_->scratch.emplace_back(at, scratch.size);
    if (!state_->building.insert(to.name).second) {
        ++g_log.faults;  // one structure built twice with no barrier between
    }
    state_->finished.erase(to.name);
}

void Backend::barrier() {
    ++g_log.barriers;
    state_->finished.insert(state_->building.begin(), state_->building.end());
    state_->building.clear();
    state_->scratch.clear();
}

void Backend::ask_compacted_size(const Structure& structure, std::uint32_t slot) {
    ++g_log.asks;
    if (slot >= kSizeSlots || !state_->finished.contains(structure.name)) {
        ++g_log.faults;
        return;
    }
    state_->answers[state_->frame_slot][slot] = fake::compacted_bytes(structure.range.size);
}

void Backend::end_size_asks() {}

std::uint64_t Backend::compacted_size(std::uint32_t slot, unsigned frame_slot) const {
    return state_->answers[frame_slot % kFramesInFlight][slot];
}

void Backend::compact(const Structure& from, const Structure& to) {
    ++g_log.compactions;
    if (!state_->finished.contains(from.name) || !state_->alive.contains(to.name) || to.range.size < fake::compacted_bytes(from.range.size)) {
        ++g_log.faults;
    }
    state_->building.insert(to.name);
}

}  // namespace galaxy::render::rt
