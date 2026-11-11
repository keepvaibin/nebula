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

std::uint64_t fake::scene_bytes(std::uint32_t instances) noexcept { return std::uint64_t{instances} * 100u + 1000u; }

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

void Backend::frame(unsigned) noexcept {}

void Backend::begin(void*, unsigned frame_slot, void*) noexcept {
    state_->frame_slot = frame_slot % kFramesInFlight;
    // A frame's commands follow the frame before's: what that built is whole.
    barrier();
    --g_log.barriers;
}

void Backend::build(const TriangleInput& input, BuildKind, const Structure& to, const Range& scratch, bool refit) {
    ++(refit ? g_log.refits : g_log.builds);
    if (input.micromap != 0u && (!state_->alive.contains(input.micromap) || input.micromap_buffer == kNoBuffer)) {
        ++g_log.faults;  // over a micromap that is not there, or without its indices
    }
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

BuildSizes Backend::scene_sizes(std::uint32_t instances) const {
    return {fake::scene_bytes(instances), std::uint64_t{instances} * 20u + 100u, 0u};
}

void Backend::build_scene(gpu::Address instances, std::uint32_t count, const Structure& to, const Range& scratch) {
    ++g_log.scenes;
    g_log.scene_instances = count;
    if (instances == 0u || count == 0u || !state_->alive.contains(to.name) || to.range.size < fake::scene_bytes(count) ||
        scratch.size < std::uint64_t{count} * 20u + 100u || scratch.page >= state_->pages.size() ||
        scratch.offset + scratch.size > state_->pages[scratch.page] || !state_->building.empty()) {
        ++g_log.faults;  // no room, or over structures no barrier finished
    }
    state_->building.insert(to.name);
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

std::uint32_t Backend::make_image(ImageFormat, std::uint32_t width, std::uint32_t height, const char*, std::uint32_t) {
    return width != 0u && height != 0u ? g_log.images++ : kNoImage;
}

void Backend::free_image(std::uint32_t) noexcept {}

void* Backend::image_to_copy(std::uint32_t) { return nullptr; }

void* Backend::image_to_sample(std::uint32_t) { return nullptr; }

void Backend::dispatch(RtShader, const PassBinds&, std::uint32_t, std::uint32_t) { ++g_log.passes; }

void Backend::end_counters() {}

void Backend::read_counters(unsigned, std::uint32_t out[kCounters]) const {
    for (unsigned i = 0; i < kCounters; ++i) {
        out[i] = 0u;
    }
}

void Backend::time_mark(unsigned, bool) {}
std::uint64_t Backend::timed_ticks(unsigned, unsigned) const { return 0u; }

std::uint32_t Backend::make_buffer(std::uint64_t bytes, const char*) { return bytes != 0u ? g_log.buffers++ : kNoBuffer; }

bool Backend::micromaps() const noexcept { return state_ != nullptr; }

BuildSizes Backend::micromap_sizes(const MicromapInput& input) const {
    return {std::uint64_t{input.triangles} * 64u + 100u, std::uint64_t{input.triangles} * 8u + 100u, 0u};
}

Structure Backend::make_micromap(const Range& range) {
    Structure out = make_structure(range, false);
    out.handle = out.name;
    --g_log.structures;
    ++g_log.micromaps;
    return out;
}

void Backend::free_micromap(Structure& micromap) noexcept {
    if (state_ != nullptr && micromap.name != 0u) {
        ++g_log.structures;
        --g_log.micromaps;
    }
    free_structure(micromap);
}

void Backend::build_micromap(const MicromapInput& input, const Structure& to, const Range& scratch) {
    ++g_log.micromap_builds;
    if (!state_->alive.contains(to.name) || input.buffer == kNoBuffer || scratch.size < std::uint64_t{input.triangles} * 8u + 100u ||
        input.triangles_at % kRangeAlign != 0u || input.indices_at % kRangeAlign != 0u) {
        ++g_log.faults;
    }
}

void Backend::free_buffer(std::uint32_t) noexcept {}

void Backend::buffer_for(std::uint32_t, bool) {}

BufferView Backend::view_of(gpu::Address at, std::uint64_t bytes, std::uint32_t& offset) const {
    offset = 0u;
    return {at, bytes};
}

}  // namespace galaxy::render::rt
