// SPDX-License-Identifier: GPL-3.0-only
#include "render/rt/rt_geometry.h"

#include <algorithm>

namespace galaxy::render::rt {

namespace {

constexpr std::uint64_t aligned(std::uint64_t bytes) noexcept {
    return (std::max<std::uint64_t>(bytes, kRangeAlign) + kRangeAlign - 1u) & ~(kRangeAlign - 1u);
}

bool same_shape(const TriangleInput& a, const TriangleInput& b) noexcept {
    return a.vertex_count == b.vertex_count && a.index_count == b.index_count && a.index_bytes == b.index_bytes &&
           a.position_stride == b.position_stride && (a.indices != 0u) == (b.indices != 0u);
}

bool same_input(const TriangleInput& a, const TriangleInput& b) noexcept {
    return same_shape(a, b) && a.positions == b.positions && a.indices == b.indices && a.own_buffer == b.own_buffer;
}

bool nothing(const TriangleInput& input) noexcept {
    return input.triangles() == 0u || (input.positions == 0u && input.own_buffer == kNoBuffer);
}

}  // namespace

Range RangePages::take(std::uint64_t bytes) {
    bytes = aligned(bytes);
    constexpr std::uint32_t kNone = ~0u;
    std::uint32_t chosen = kNone;
    std::uint64_t chosen_offset = 0u;
    for (std::uint32_t i = 0; i < pages_.size(); ++i) {
        const Page& page = pages_[i];
        if (!page.here || page.free_bytes < bytes || (chosen != kNone && page.free_bytes >= pages_[chosen].free_bytes)) {
            continue;
        }
        auto fit = page.free.end();
        for (auto it = page.free.begin(); it != page.free.end(); ++it) {
            if (it->second >= bytes && (fit == page.free.end() || it->second < fit->second)) {
                fit = it;
            }
        }
        if (fit != page.free.end()) {
            chosen = i;
            chosen_offset = fit->first;
        }
    }
    if (chosen == kNone) {
        return {};
    }
    Page& page = pages_[chosen];
    const auto range = page.free.find(chosen_offset);
    const std::uint64_t size = range->second;
    page.free.erase(range);
    if (size > bytes) {
        page.free.emplace(chosen_offset + bytes, size - bytes);
    }
    page.free_bytes -= bytes;
    return {chosen, chosen_offset, bytes};
}

void RangePages::give_back(const Range& range) {
    if (range.size == 0u || range.page >= pages_.size() || !pages_[range.page].here) {
        return;
    }
    Page& page = pages_[range.page];
    std::uint64_t start = range.offset, length = range.size;
    const auto next = page.free.lower_bound(range.offset);
    if (next != page.free.begin()) {
        const auto previous = std::prev(next);
        if (previous->first + previous->second == start) {
            start = previous->first;
            length += previous->second;
            page.free.erase(previous);
        }
    }
    if (next != page.free.end() && range.offset + range.size == next->first) {
        length += next->second;
        page.free.erase(next);
    }
    page.free.emplace(start, length);
    page.free_bytes += range.size;
}

void RangePages::add_page(std::uint32_t page, std::uint64_t bytes) {
    if (pages_.size() <= page) {
        pages_.resize(page + 1u);
    }
    pages_[page] = Page{};
    pages_[page].bytes = pages_[page].free_bytes = bytes;
    pages_[page].free.emplace(0u, bytes);
    pages_[page].here = true;
}

std::uint32_t RangePages::empty_page() const noexcept {
    for (std::uint32_t i = 0; i < pages_.size(); ++i) {
        if (pages_[i].here && pages_[i].free_bytes == pages_[i].bytes) {
            return i;
        }
    }
    return ~0u;
}

void RangePages::drop_page(std::uint32_t page) {
    if (page < pages_.size()) {
        pages_[page] = Page{};
    }
}

std::uint64_t RangePages::page_bytes() const noexcept {
    std::uint64_t bytes = 0u;
    for (const Page& page : pages_) {
        bytes += page.here ? page.bytes : 0u;
    }
    return bytes;
}

std::uint64_t RangePages::used_bytes() const noexcept {
    std::uint64_t bytes = 0u;
    for (const Page& page : pages_) {
        bytes += page.here ? page.bytes - page.free_bytes : 0u;
    }
    return bytes;
}

std::uint32_t RangePages::pages() const noexcept {
    std::uint32_t count = 0u;
    for (const Page& page : pages_) {
        count += page.here ? 1u : 0u;
    }
    return count;
}

Range Geometry::take_structure(std::uint64_t bytes) {
    Range range = pages_.take(bytes);
    if (!range) {
        const std::uint64_t page_bytes = std::max(aligned(bytes), kStructurePage);
        const std::uint32_t page = backend_.make_page(page_bytes, false);
        if (page == ~0u) {
            return {};
        }
        pages_.add_page(page, page_bytes);
        range = pages_.take(bytes);
    }
    return range;
}

Range Geometry::take_scratch(std::uint64_t bytes) {
    bytes = aligned(bytes);
    if (scratch_page_ == ~0u || bytes > scratch_bytes_) {
        // A larger page: the one in use may still be read by what this frame recorded.
        const std::uint64_t page_bytes = std::max(bytes, kScratchPage);
        const std::uint32_t page = backend_.make_page(page_bytes, true);
        if (page == ~0u) {
            return {};
        }
        if (scratch_page_ != ~0u) {
            retired_scratch_.emplace_back(frame_index_, scratch_page_);
        }
        scratch_page_ = page;
        scratch_bytes_ = page_bytes;
        scratch_used_ = 0u;
    }
    if (scratch_used_ + bytes > scratch_bytes_) {
        // The page is used again from its start once what used it is done.
        if (built_since_barrier_) {
            backend_.barrier();
            built_since_barrier_ = false;
        }
        scratch_used_ = 0u;
    }
    const Range range{scratch_page_, scratch_used_, bytes};
    scratch_used_ += bytes;
    return range;
}

void Geometry::retire(Entry& entry) {
    if (entry.structure) {
        retired_.push_back({frame_index_, entry.structure});
        --stats_.structures;
        stats_.triangles -= entry.input.triangles();
        stats_.compacted -= entry.state == State::Compacted ? 1u : 0u;
    }
    if (entry.micromap) {
        retired_.push_back({frame_index_, entry.micromap, true});
        --stats_.micromaps;
        stats_.micromap_bytes -= entry.micromap.range.size;
    }
    if (entry.omm.buffer != kNoBuffer) {
        retired_buffers_.emplace_back(frame_index_, entry.omm.buffer);
    }
    entry.micromap = Structure{};
    entry.omm = MicromapInput{};
    entry.omm_tag = 0u;
    entry.input.micromap = 0u;
    entry.input.micromap_buffer = kNoBuffer;
    entry.structure = Structure{};
    entry.built_bytes = 0u;
    entry.state = State::Waiting;
}

void Geometry::release_retired() {
    std::erase_if(retired_, [this](Retired& retired) {
        if (retired.frame + kFramesToRelease > frame_index_) {
            return false;
        }
        const Range range = retired.structure.range;
        if (retired.micromap) {
            backend_.free_micromap(retired.structure);
        } else {
            backend_.free_structure(retired.structure);
        }
        pages_.give_back(range);
        return true;
    });
    std::erase_if(retired_buffers_, [this](const std::pair<std::uint64_t, std::uint32_t>& retired) {
        if (retired.first + kFramesToRelease > frame_index_) {
            return false;
        }
        backend_.free_buffer(retired.second);
        return true;
    });
    std::erase_if(retired_scratch_, [this](const std::pair<std::uint64_t, std::uint32_t>& retired) {
        if (retired.first + kFramesToRelease > frame_index_) {
            return false;
        }
        backend_.free_page(retired.second);
        return true;
    });
    // Empty pages go, but for one that stays for the next scene.
    while (pages_.pages() > 1u) {
        const std::uint32_t empty = pages_.empty_page();
        if (empty == ~0u) {
            break;
        }
        backend_.free_page(empty);
        pages_.drop_page(empty);
    }
}

void Geometry::want(std::uint64_t key, const TriangleInput& input) {
    if (nothing(input)) {
        return;
    }
    Entry& entry = entries_[key];
    if (entry.queued || (entry.structure && same_input(entry.input, input))) {
        return;
    }
    // A key given to other vertices: the structure it had is the old mesh's.
    retire(entry);
    entry.input = input;
    entry.kind = BuildKind::Static;
    entry.queued = true;
    queue_.push_back(key);
    ++stats_.waiting;
}

void Geometry::drawn_at(std::uint64_t key, float distance_sq) {
    if (drawn_waiting_.size() < 65536u && distance_sq >= 0.0f) {
        drawn_waiting_.emplace_back(distance_sq, key);
    }
}

// The queue with the meshes the last frame drew first, the nearest first; the others after them in their order.
void Geometry::order_queue() {
    if (drawn_waiting_.empty() || queue_.size() < 2u) {
        drawn_waiting_.clear();
        return;
    }
    std::sort(drawn_waiting_.begin(), drawn_waiting_.end());
    std::deque<std::uint64_t> ordered;
    std::unordered_map<std::uint64_t, bool> first;
    first.reserve(drawn_waiting_.size());
    for (const auto& [distance_sq, key] : drawn_waiting_) {
        const auto found = entries_.find(key);
        if (found != entries_.end() && found->second.queued && first.emplace(key, true).second) {
            ordered.push_back(key);
        }
    }
    drawn_waiting_.clear();
    if (ordered.empty()) {
        return;
    }
    for (const std::uint64_t key : queue_) {
        if (first.find(key) == first.end()) {
            ordered.push_back(key);  // a key queued twice (forgotten, wanted again) keeps both places, as before
        }
    }
    queue_.swap(ordered);
}

void Geometry::move(std::uint64_t key, const TriangleInput& input, bool rebuild) {
    if (nothing(input)) {
        return;
    }
    const auto [found, fresh] = entries_.try_emplace(key);
    Entry& entry = found->second;
    if (fresh) {
        moving_.push_back(key);
        entry.kind = BuildKind::Moving;
    } else if (entry.structure && !same_shape(entry.input, input)) {
        retire(entry);
    }
    entry.input = input;
    entry.moved = true;
    entry.named_frame = frame_index_;
    if (rebuild) {
        entry.refits = kRefitsToRebuild;
    }
}

void Geometry::forget(std::uint64_t key) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
        return;
    }
    stats_.waiting -= found->second.queued ? 1u : 0u;
    retire(found->second);
    if (found->second.kind == BuildKind::Moving) {
        std::erase(moving_, key);
    }
    entries_.erase(found);
}

void Geometry::clear() noexcept {
    for (auto& [key, entry] : entries_) {
        backend_.free_structure(entry.structure);
        backend_.free_micromap(entry.micromap);
        if (entry.omm.buffer != kNoBuffer) {
            backend_.free_buffer(entry.omm.buffer);
        }
    }
    for (Retired& retired : retired_) {
        if (retired.micromap) {
            backend_.free_micromap(retired.structure);
        } else {
            backend_.free_structure(retired.structure);
        }
    }
    for (const auto& [frame, buffer] : retired_buffers_) {
        backend_.free_buffer(buffer);
    }
    retired_buffers_.clear();
    micromap_queue_.clear();
    entries_.clear();
    queue_.clear();
    drawn_waiting_.clear();
    moving_.clear();
    retired_.clear();
    retired_scratch_.clear();
    asked_.clear();
    to_ask_.clear();
    pages_ = RangePages{};  // the pages themselves go with the back end's stop
    scratch_page_ = ~0u;
    scratch_bytes_ = scratch_used_ = 0u;
    stats_ = GeometryStats{};
}

bool Geometry::build(Entry& entry, bool refit) {
    const BuildSizes sizes = backend_.sizes(entry.input, entry.kind);
    if (!refit && (!entry.structure || entry.built_bytes < sizes.structure)) {
        retire(entry);
        const Range range = take_structure(sizes.structure);
        if (!range) {
            return false;
        }
        entry.structure = backend_.make_structure(range, false);
        if (!entry.structure) {
            pages_.give_back(range);
            return false;
        }
        entry.built_bytes = range.size;
        ++stats_.structures;
        stats_.triangles += entry.input.triangles();
    }
    const Range scratch = take_scratch(refit ? sizes.update_scratch : sizes.build_scratch);
    if (!scratch) {
        return false;
    }
    backend_.build(entry.input, entry.kind, entry.structure, scratch, refit);
    built_since_barrier_ = true;
    entry.state = State::Built;
    entry.used_frame = frame_index_;
    return true;
}

// 5.3 G-4: past the ceiling, the least recently named static structures go, down to seven eighths of it (what retired
// structures still hold is counted gone). A mesh seen again asks for its structure again (RenderCore::rt_want_form).
void Geometry::keep_under_ceiling() {
    std::uint64_t live = pages_.used_bytes();
    for (const Retired& retired : retired_) {
        live -= std::min(live, retired.structure.range.size);
    }
    if (live <= ceiling_) {
        return;
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> oldest;  // used frame, key
    for (const auto& [key, entry] : entries_) {
        if (entry.kind == BuildKind::Static && entry.structure && !entry.queued && entry.used_frame + kFramesToForget < frame_index_) {
            oldest.emplace_back(entry.used_frame, key);
        }
    }
    std::sort(oldest.begin(), oldest.end());
    const std::uint64_t goal = ceiling_ / 8u * 7u;
    for (const auto& [used, key] : oldest) {
        if (live <= goal) {
            break;
        }
        live -= std::min(live, entries_[key].structure.range.size);
        forget(key);
        ++stats_.released;
    }
}

void Geometry::frame(void* commands, unsigned frame_slot, std::uint64_t frame_index, std::uint64_t budget_triangles) {
    frame_index_ = frame_index;
    stats_.built_triangles = 0u;
    stats_.builds = stats_.compactions = 0u;
    release_retired();
    keep_under_ceiling();
    // The builds this frame slot's last frame timed (5.3 G-3: the caller learns the rate from them).
    const unsigned timed_slot = frame_slot % kFramesInFlight;
    stats_.timed_ticks = backend_.timed_ticks(kTimedBuilds, frame_slot);
    stats_.timed_triangles = stats_.timed_ticks != 0u ? timed_triangles_[timed_slot] : 0u;
    backend_.begin(commands, frame_slot);
    scratch_used_ = 0u;
    built_since_barrier_ = false;
    const bool timing = !queue_.empty();
    if (timing) {
        backend_.time_mark(kTimedBuilds, false);
    }

    // What moves and no frame has named for a while is forgotten.
    for (std::size_t i = 0; i < moving_.size();) {
        if (frame_index - entries_[moving_[i]].named_frame > kFramesToForget) {
            forget(moving_[i]);  // takes it out of moving_
        } else {
            ++i;
        }
    }

    // What stands still: what the last frame drew first, the nearest first; then the oldest; within the budget (one
    // build at least).
    order_queue();
    std::uint64_t used = 0u;
    bool any = false;
    while (!queue_.empty()) {
        const std::uint64_t key = queue_.front();
        const auto found = entries_.find(key);
        if (found == entries_.end() || !found->second.queued) {
            queue_.pop_front();
            continue;
        }
        Entry& entry = found->second;
        const std::uint64_t triangles = entry.input.triangles();
        if (budget_triangles != 0u && any && used + triangles > budget_triangles) {
            break;
        }
        if (!build(entry, false)) {
            break;  // no memory now: it stays first in the queue
        }
        queue_.pop_front();
        entry.queued = false;
        --stats_.waiting;
        to_ask_.push_back(key);
        used += triangles;
        any = true;
        ++stats_.builds;
        stats_.built_triangles += triangles;
    }
    // Then the micromaps whose input a pass wrote, each with its structure built again over it (5.3 G-2).
    while (!micromap_queue_.empty()) {
        const std::uint64_t key = micromap_queue_.front();
        const auto found = entries_.find(key);
        if (found == entries_.end() || found->second.omm.buffer == kNoBuffer || found->second.micromap || !found->second.structure ||
            found->second.queued) {
            micromap_queue_.pop_front();
            continue;
        }
        Entry& entry = found->second;
        const std::uint64_t triangles = entry.input.triangles();
        if (budget_triangles != 0u && any && used + triangles > budget_triangles) {
            break;
        }
        if (!build_with_micromap(entry)) {
            break;  // no memory now: it stays first
        }
        micromap_queue_.pop_front();
        to_ask_.push_back(key);
        used += triangles;
        any = true;
        ++stats_.builds;
        stats_.built_triangles += triangles;
    }
    if (budget_triangles != 0u) {
        stats_.most_built_triangles = std::max(stats_.most_built_triangles, used);
    }
    if (built_since_barrier_) {
        backend_.barrier();
        built_since_barrier_ = false;
    }
    if (timing) {
        backend_.time_mark(kTimedBuilds, true);
        timed_triangles_[timed_slot] = used;
    }

    // The sizes asked in this frame slot's last frame are readable now: the renderer waited for that frame before it
    // began this one, and the slot's next asks (below) overwrite them. Each structure they make smaller is copied into a
    // range of that size.
    bool copied = false;
    for (auto batch = asked_.begin(); batch != asked_.end();) {
        if (batch->frame_slot != frame_slot || batch->frame >= frame_index) {
            ++batch;
            continue;
        }
        for (std::uint32_t slot = 0; slot < batch->keys.size(); ++slot) {
            const auto found = entries_.find(batch->keys[slot]);
            if (found == entries_.end() || found->second.state != State::SizeAsked || found->second.asked_frame != batch->frame) {
                continue;
            }
            Entry& entry = found->second;
            const std::uint64_t bytes = aligned(backend_.compacted_size(slot, frame_slot));
            entry.state = State::Compacted;
            ++stats_.compacted;
            if (bytes <= kRangeAlign || bytes >= entry.structure.range.size) {
                continue;  // no answer, or nothing to gain: it stays as built
            }
            const Range range = take_structure(bytes);
            Structure smaller = range ? backend_.make_structure(range, false) : Structure{};
            if (!smaller) {
                pages_.give_back(range);
                continue;
            }
            backend_.compact(entry.structure, smaller);
            retired_.push_back({frame_index_, entry.structure});
            entry.structure = smaller;
            entry.built_bytes = range.size;
            copied = true;
            ++stats_.compactions;
        }
        batch = asked_.erase(batch);
    }
    if (copied) {
        backend_.barrier();
    }

    // The sizes of what was built: asked here, read when this frame slot comes round.
    if (!to_ask_.empty()) {
        Asked batch;
        batch.frame = frame_index;
        batch.frame_slot = frame_slot;
        while (!to_ask_.empty() && batch.keys.size() < kSizeSlots) {
            const std::uint64_t key = to_ask_.front();
            to_ask_.pop_front();
            const auto found = entries_.find(key);
            if (found == entries_.end() || found->second.state != State::Built || found->second.kind != BuildKind::Static) {
                continue;
            }
            backend_.ask_compacted_size(found->second.structure, static_cast<std::uint32_t>(batch.keys.size()));
            found->second.state = State::SizeAsked;
            found->second.asked_frame = frame_index;
            batch.keys.push_back(key);
        }
        if (!batch.keys.empty()) {
            asked_.push_back(std::move(batch));
        }
    }
    backend_.end_size_asks();

    stats_.structure_bytes = pages_.used_bytes();
    stats_.page_bytes = pages_.page_bytes() + scratch_bytes_;
}

void Geometry::refit() {
    stats_.refits = stats_.rebuilds = 0u;
    stats_.refit_triangles = 0u;
    for (const std::uint64_t key : moving_) {
        Entry& entry = entries_[key];
        if (!entry.moved) {
            continue;
        }
        entry.moved = false;
        const bool refit = entry.structure && entry.refits + 1u < kRefitsToRebuild;
        if (build(entry, refit)) {
            entry.refits = refit ? entry.refits + 1u : 0u;
            ++(refit ? stats_.refits : stats_.rebuilds);
            stats_.refit_triangles += entry.input.triangles();
        }
    }
    if (built_since_barrier_) {
        backend_.barrier();
        built_since_barrier_ = false;
    }
    stats_.structure_bytes = pages_.used_bytes();
    stats_.page_bytes = pages_.page_bytes() + scratch_bytes_;
}

// The micromap, then the structure over it in a range of its own; the old structure is retired only once both are
// recorded, so a frame names one or the other, never none.
bool Geometry::build_with_micromap(Entry& entry) {
    const BuildSizes micromap_sizes = backend_.micromap_sizes(entry.omm);
    if (micromap_sizes.structure == 0u) {
        return false;  // no size yet (no page or buffer for the query to name, l87): it waits first in the queue
    }
    const Range micromap_range = take_structure(micromap_sizes.structure);
    if (!micromap_range) {
        return false;
    }
    Structure micromap = backend_.make_micromap(micromap_range);
    if (!micromap) {
        pages_.give_back(micromap_range);
        return false;
    }
    const Range micromap_scratch = take_scratch(micromap_sizes.build_scratch);
    if (!micromap_scratch) {
        backend_.free_micromap(micromap);
        pages_.give_back(micromap_range);
        return false;
    }
    backend_.build_micromap(entry.omm, micromap, micromap_scratch);
    TriangleInput input = entry.input;
    input.micromap = micromap.handle;
    input.micromap_buffer = entry.omm.buffer;
    input.micromap_indices = entry.omm.indices_at;
    input.micromap_level = entry.omm.level;
    const BuildSizes sizes = backend_.sizes(input, BuildKind::Static);
    const Range range = take_structure(sizes.structure);
    Structure structure = range ? backend_.make_structure(range, false) : Structure{};
    const Range scratch = structure ? take_scratch(sizes.build_scratch) : Range{};
    if (!scratch) {
        // The micromap's build is recorded: it goes once no frame can still run it.
        if (structure) {
            backend_.free_structure(structure);
        }
        if (range) {
            pages_.give_back(range);
        }
        retired_.push_back({frame_index_, micromap, true});
        return false;
    }
    backend_.build(input, BuildKind::Static, structure, scratch, false);
    built_since_barrier_ = true;
    retired_.push_back({frame_index_, entry.structure});
    stats_.compacted -= entry.state == State::Compacted ? 1u : 0u;
    entry.structure = structure;
    entry.built_bytes = range.size;
    entry.input = input;
    entry.state = State::Built;
    entry.used_frame = frame_index_;
    entry.micromap = micromap;
    ++stats_.micromaps;
    ++stats_.micromaps_built;
    stats_.micromap_bytes += micromap.range.size;
    return true;
}

bool Geometry::takes_micromap(std::uint64_t key) const noexcept {
    const auto found = entries_.find(key);
    return found != entries_.end() && found->second.kind == BuildKind::Static && found->second.structure && !found->second.queued &&
           !found->second.micromap && found->second.omm.buffer == kNoBuffer;
}

bool Geometry::give_micromap(std::uint64_t key, const MicromapInput& input, std::uint32_t tag) {
    if (!takes_micromap(key) || input.buffer == kNoBuffer || input.triangles != entries_[key].input.triangles()) {
        return false;
    }
    Entry& entry = entries_[key];
    entry.omm = input;
    entry.omm_tag = tag;
    micromap_queue_.push_back(key);
    return true;
}

std::uint32_t Geometry::micromap_tag(std::uint64_t key) const noexcept {
    const auto found = entries_.find(key);
    return found != entries_.end() && found->second.micromap ? found->second.omm_tag : 0u;
}

std::uint64_t Geometry::name(std::uint64_t key) const noexcept {
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
        return 0u;
    }
    found->second.used_frame = frame_index_;
    return found->second.structure.name;
}

}  // namespace galaxy::render::rt
