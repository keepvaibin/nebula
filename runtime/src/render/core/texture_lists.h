// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Texture lists that live as long as their table (FRAME-MODEL 2.10). A table's (texture slot, sampler slot) pairs are
// written once into a chunk of a buffer the programs read, and again only when a pair changes; a list a table gives up
// (rewritten, or the table retired or dropped) is used again only once the GPU has finished every frame that could
// read it (the renderer's fence). Chunks never move, so a list's address stands while it is in use; the renderer
// makes them (make_texture_list_chunk) and frees them at shutdown. A list names a target (a picture the frame draws,
// which moves from image to image) by its number, which stands while the target lives; the program reads the target's
// picture through the frame's target slots (TargetSlots), so a list naming a target is written once too. walk_list is
// the walk's part (RenderCore::walk_tables). Header-only: texture_lists_tests.cpp.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

namespace galaxy::render::core {

class TextureLists {
public:
    // A chunk the renderer made: `pairs` pairs of mapped memory and the address the programs take.
    struct Chunk {
        std::uint32_t* mapped = nullptr;
        std::uint64_t address = 0u;
        std::uint32_t pairs = 0u;
    };
    // A list's place: its chunk, its first pair, its room (a power of two, at least its count).
    struct Place {
        static constexpr std::uint32_t kNone = ~0u;
        std::uint32_t chunk = kNone;
        std::uint32_t first = 0u;
        std::uint32_t room = 0u;
        [[nodiscard]] bool valid() const noexcept { return chunk != kNone; }
    };
    static constexpr std::uint32_t kChunkPairs = 1u << 16;  // 512 KiB a chunk

    // Writes count pairs (count >= 1) into a free place and returns it. completed_fence: the renderer's completed
    // fence value, under which places given up are free again. make(pairs) makes a chunk of at least that many pairs.
    template <class Make>
    Place write(const std::uint32_t* pairs, std::uint32_t count, std::uint64_t completed_fence, Make&& make) {
        const std::uint32_t size_class = class_of(count);
        Place place = take_free(size_class, completed_fence);
        if (!place.valid()) {
            const std::uint32_t room = 1u << size_class;
            if (chunks_.empty() || chunks_.back().pairs - used_ < room) {
                const std::uint32_t want = room > kChunkPairs ? room : kChunkPairs;
                const Chunk chunk = make(want);
                if (chunk.mapped == nullptr || chunk.pairs < room) {
                    return Place{};
                }
                chunks_.push_back(chunk);
                used_ = 0u;
            }
            place.chunk = static_cast<std::uint32_t>(chunks_.size() - 1u);
            place.first = used_;
            place.room = room;
            used_ += room;
        }
        std::memcpy(chunks_[place.chunk].mapped + std::size_t{place.first} * 2u, pairs, std::size_t{count} * 8u);
        ++writes_;
        return place;
    }

    // As write, in chunk `chunk` only: a place given up there, or room at its end when it is the last chunk; an invalid
    // place when it has neither.
    Place write_in(std::uint32_t chunk, const std::uint32_t* pairs, std::uint32_t count, std::uint64_t completed_fence) {
        if (chunk >= chunks_.size()) {
            return Place{};
        }
        const std::uint32_t size_class = class_of(count);
        Place place = take_free_in(size_class, chunk, completed_fence);
        if (!place.valid()) {
            const std::uint32_t room = 1u << size_class;
            if (chunk + 1u != chunks_.size() || chunks_.back().pairs - used_ < room) {
                return Place{};
            }
            place.chunk = chunk;
            place.first = used_;
            place.room = room;
            used_ += room;
        }
        std::memcpy(chunks_[place.chunk].mapped + std::size_t{place.first} * 2u, pairs, std::size_t{count} * 8u);
        ++writes_;
        return place;
    }

    // As write, in a new chunk with room for `beside` pairs more after the list (a list moved beside its target slots).
    template <class Make>
    Place write_fresh(const std::uint32_t* pairs, std::uint32_t count, std::uint32_t beside, Make&& make) {
        const std::uint32_t room = 1u << class_of(count);
        const std::uint32_t need = room + (1u << class_of(beside));
        const Chunk chunk = make(need > kChunkPairs ? need : kChunkPairs);
        if (chunk.mapped == nullptr || chunk.pairs < need) {
            return Place{};
        }
        chunks_.push_back(chunk);
        used_ = 0u;
        return write_in(static_cast<std::uint32_t>(chunks_.size() - 1u), pairs, count, 0u);
    }

    // Gives a place up: free again once the completed fence reaches free_at (the renderer's submitted value + 1).
    void release(const Place& place, std::uint64_t free_at) {
        if (place.valid()) {
            free_[class_of(place.room)].push_back(Freed{place, free_at});
        }
    }

    [[nodiscard]] std::uint64_t address(const Place& place) const noexcept {
        return place.valid() ? chunks_[place.chunk].address : 0u;
    }
    [[nodiscard]] std::uint64_t writes() const noexcept { return writes_; }
    [[nodiscard]] std::size_t chunk_count() const noexcept { return chunks_.size(); }

    // The renderer freed its chunks (shutdown): every place is gone.
    void clear() {
        chunks_.clear();
        for (std::deque<Freed>& list : free_) {
            list.clear();
        }
        used_ = 0u;
    }

private:
    struct Freed {
        Place place;
        std::uint64_t free_at;
    };

    static std::uint32_t class_of(std::uint32_t count) noexcept {
        std::uint32_t size_class = 0u;
        while ((1u << size_class) < count) {
            ++size_class;
        }
        return size_class;
    }

    // Places are given up in fence order, so the oldest of a class is the first to be free.
    Place take_free(std::uint32_t size_class, std::uint64_t completed_fence) {
        std::deque<Freed>& list = free_[size_class];
        if (!list.empty() && list.front().free_at <= completed_fence) {
            const Place place = list.front().place;
            list.pop_front();
            return place;
        }
        return Place{};
    }

    // The oldest place of a class in `chunk` that is free.
    Place take_free_in(std::uint32_t size_class, std::uint32_t chunk, std::uint64_t completed_fence) {
        std::deque<Freed>& list = free_[size_class];
        for (auto it = list.begin(); it != list.end() && it->free_at <= completed_fence; ++it) {
            if (it->place.chunk == chunk) {
                const Place place = it->place;
                list.erase(it);
                return place;
            }
        }
        return Place{};
    }

    std::vector<Chunk> chunks_;
    std::uint32_t used_ = 0u;  // pairs used in the last chunk
    std::deque<Freed> free_[32];
    std::uint64_t writes_ = 0u;
};

// A list entry's texture slot (draw_tables .x) for a target: this bit and the target's number (program.hlsli MAP_AT).
inline constexpr std::uint32_t kTargetMap = 0x80000000u;

// The frame's target slots (FRAME-MODEL 2.10). Each target a list names has a number while it lives; the slots hold at
// each number the texture slot of the target's picture as it stands, and the program reads a target's picture through
// the slot at the slots' first pair plus its number (gpu::DrawRoot::maps). They are written once for each state of the
// pictures (a walk, a picture moved to another image, a generated frame's copy written), in each chunk a list naming a
// target sits in: a draw reads its list and the slots from one buffer.
class TargetSlots {
public:
    // The target's number, made at its first use.
    std::uint32_t number(std::uint32_t target) {
        const auto found = numbers_.find(target);
        if (found != numbers_.end()) {
            return found->second;
        }
        std::uint32_t n = 0u;
        if (!free_.empty()) {
            n = free_.back();
            free_.pop_back();
            ids_[n] = target;
        } else {
            n = static_cast<std::uint32_t>(ids_.size());
            ids_.push_back(target);
        }
        numbers_.emplace(target, n);
        ++changes_;
        return n;
    }

    // The target is gone, and the tables naming it were dropped before: its number is free for another.
    void forget(std::uint32_t target) {
        const auto found = numbers_.find(target);
        if (found == numbers_.end()) {
            return;
        }
        ids_[found->second] = 0u;
        free_.push_back(found->second);
        numbers_.erase(found);
        ++changes_;
    }

    // The slots of `state` in `chunk`, written when the state or the numbers changed since they were written there:
    // fill(target) gives a target's texture slot (target 0: a free number). Invalid when the chunk has no room.
    template <class Fill>
    TextureLists::Place slots_in(TextureLists& lists, std::uint32_t chunk, std::uint64_t state, std::uint64_t completed_fence,
                                 std::uint64_t free_at, Fill&& fill) {
        if (chunk >= written_.size()) {
            written_.resize(std::size_t{chunk} + 1u);
        }
        Written& at = written_[chunk];
        if (at.place.valid() && at.state == state && at.changes == changes_) {
            return at.place;
        }
        const auto count = static_cast<std::uint32_t>(ids_.size());
        pairs_.assign(std::size_t{count} * 2u, 0u);
        for (std::uint32_t n = 0; n < count; ++n) {
            pairs_[2u * n] = fill(ids_[n]);
        }
        lists.release(at.place, free_at);
        at.place = lists.write_in(chunk, pairs_.data(), count, completed_fence);
        at.state = state;
        at.changes = changes_;
        if (at.place.valid()) {
            ++writes_;
        }
        return at.place;
    }

    [[nodiscard]] std::uint32_t count() const noexcept { return static_cast<std::uint32_t>(ids_.size()); }
    [[nodiscard]] std::uint64_t writes() const noexcept { return writes_; }

    // The renderer freed its chunks (shutdown): the slots written are gone; the numbers stand with their targets.
    void clear_places() { written_.clear(); }

private:
    struct Written {
        TextureLists::Place place{};
        std::uint64_t state = 0u;
        std::uint64_t changes = 0u;
    };
    std::unordered_map<std::uint32_t, std::uint32_t> numbers_;
    std::vector<std::uint32_t> ids_;  // per number its target, 0 for a free number
    std::vector<std::uint32_t> free_;
    std::vector<Written> written_;     // per chunk
    std::vector<std::uint32_t> pairs_;
    std::uint64_t changes_ = 0u;
    std::uint64_t writes_ = 0u;
};

// One table's list as a walk names it (RenderCore::walk_tables; FRAME-MODEL 2.10). Once a walk, every texture is looked up
// (the look-up marks it in use for the video-memory budget and gives a movie its frame); the samplers are made again when
// the textures or the renderer's sampler generation moved (a target's is that of a one-level picture, Handle{}: made with
// the table); the list is written when a pair changed. A target is named by its number, so its list stands while its
// pictures move: `slots_first` is the first pair of this state's target slots in the list's chunk (gpu::DrawRoot::maps),
// 0 for a list that names no target. False when no list could be made.
// Host: a type Handle and walk(), state(), generation(), revision(), is_target(texture), handle(texture),
// texture_slot(handle), sampler_slot(entry, handle), target_slot(target), completed(), submitted(),
// make(pairs) -> TextureLists::Chunk. Table: core::TextureTable's list fields.
template <class Table, class Host>
bool walk_list(Table& table, Host& host, TextureLists& lists, TargetSlots& targets,
               std::vector<typename Host::Handle>& handles, std::vector<std::uint32_t>& pairs, std::uint32_t& slots_first) {
    using Handle = typename Host::Handle;
    const auto generation = host.generation();
    const std::uint64_t free_at = host.submitted() + 1u;
    const auto make = [&](std::uint32_t want) { return host.make(want); };
    // A list written earlier in this walk stands unless the generation moved since (the sampler array started again, or
    // the Vulkan image array grew and its null slot moved).
    if (table.walk_stamp != host.walk() || table.samplers_generation != generation || !table.list_place.valid()) {
        const std::uint32_t count = table.count;
        handles.assign(count, Handle{});
        bool has_target = false;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t texture = table.list[i].texture;
            if (texture == 0u) {
                continue;
            }
            if (host.is_target(texture)) {
                has_target = true;
            } else {
                handles[i] = host.handle(texture);
            }
        }
        if (table.sampler_slots.size() != count || table.textures_revision != host.revision() ||
            table.samplers_generation != generation) {
            table.sampler_slots.resize(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                table.sampler_slots[i] = host.sampler_slot(table.list[i], handles[i]);
            }
            table.textures_revision = host.revision();
            table.samplers_generation = generation;
        }
        pairs.resize(std::size_t{count} * 2u);
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t texture = table.list[i].texture;
            pairs[2u * i] = texture != 0u && host.is_target(texture) ? (kTargetMap | targets.number(texture))
                                                                     : host.texture_slot(handles[i]);
            pairs[2u * i + 1u] = table.sampler_slots[i];
        }
        if (!table.list_place.valid() || table.list_pairs != pairs) {
            lists.release(table.list_place, free_at);
            table.list_place = lists.write(pairs.data(), count, host.completed(), make);
            if (!table.list_place.valid()) {
                return false;
            }
            table.list_pairs = pairs;
        }
        table.has_target = has_target;
        table.walk_stamp = host.walk();
    }
    slots_first = 0u;
    if (table.has_target) {
        const auto fill = [&](std::uint32_t target) {
            return target == 0u ? host.texture_slot(Handle{}) : host.target_slot(target);
        };
        TextureLists::Place slots =
            targets.slots_in(lists, table.list_place.chunk, host.state(), host.completed(), free_at, fill);
        if (!slots.valid()) {
            // The list's chunk has no room for the slots: the list moves to a new chunk, beside them.
            lists.release(table.list_place, free_at);
            table.list_place = lists.write_fresh(table.list_pairs.data(), table.count, targets.count(), make);
            if (!table.list_place.valid()) {
                return false;
            }
            slots = targets.slots_in(lists, table.list_place.chunk, host.state(), host.completed(), free_at, fill);
            if (!slots.valid()) {
                return false;
            }
        }
        slots_first = slots.first;
    }
    table.lists = lists.address(table.list_place);
    table.list_first = table.list_place.first;
    return true;
}

}  // namespace galaxy::render::core
