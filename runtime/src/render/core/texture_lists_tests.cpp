// SPDX-License-Identifier: GPL-3.0-only
// Texture lists that live as long as their table (texture_lists.h, FRAME-MODEL 2.10): a list is written once and
// stands; a place given up is not used again before the GPU's fence passes it; chunks never move. The walk writes each
// table once, a table naming a target too: the target's pictures are read through the target slots.

#include "render/core/texture_lists.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace {

using galaxy::render::core::TextureLists;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}

// Chunks of CPU memory standing in for the renderer's buffers; each chunk's address is its index times 2^32.
struct Maker {
    std::vector<std::unique_ptr<std::uint32_t[]>> memory;
    TextureLists::Chunk operator()(std::uint32_t pairs) {
        memory.push_back(std::make_unique<std::uint32_t[]>(std::size_t{pairs} * 2u));
        TextureLists::Chunk chunk;
        chunk.mapped = memory.back().get();
        chunk.address = std::uint64_t{memory.size()} << 32;
        chunk.pairs = pairs;
        return chunk;
    }
};

void test_written_once() {
    TextureLists lists;
    Maker make;
    const std::uint32_t pairs[6] = {1u, 10u, 2u, 20u, 3u, 30u};
    const TextureLists::Place place = lists.write(pairs, 3u, 0u, make);
    check(place.valid() && place.first == 0u && place.room == 4u, "a list of three takes a place of four");
    check(lists.writes() == 1u && lists.chunk_count() == 1u, "one write, one chunk");
    const std::uint32_t* at = make.memory[0].get() + std::size_t{place.first} * 2u;
    check(at[0] == 1u && at[1] == 10u && at[4] == 3u && at[5] == 30u, "the pairs as given");
    const TextureLists::Place second = lists.write(pairs, 1u, 0u, make);
    check(second.first == 4u && second.room == 1u, "the next list after the first's room");
    check(lists.address(place) == lists.address(second) && lists.address(place) == (std::uint64_t{1} << 32), "one chunk's address");
}

void test_fence() {
    TextureLists lists;
    Maker make;
    const std::uint32_t pairs[4] = {1u, 1u, 2u, 2u};
    const TextureLists::Place first = lists.write(pairs, 2u, 0u, make);
    lists.release(first, 5u);  // free once fence 5 has completed
    const TextureLists::Place early = lists.write(pairs, 2u, 4u, make);
    check(early.first != first.first, "a place given up is not used before its fence");
    const TextureLists::Place late = lists.write(pairs, 2u, 5u, make);
    check(late.first == first.first && late.chunk == first.chunk, "the place is used again once its fence passed");
    lists.release(early, 7u);
    lists.release(late, 9u);
    const TextureLists::Place next = lists.write(pairs, 2u, 8u, make);
    check(next.first == early.first, "the oldest place given up is used first");
    const TextureLists::Place other = lists.write(pairs, 1u, 100u, make);
    check(other.room == 1u && other.first != late.first, "a place is used again only by a list of its size");
}

void test_chunks() {
    TextureLists lists;
    Maker make;
    // Room for the longest list written below (a chunk's pairs and one more).
    std::vector<std::uint32_t> pairs((std::size_t{TextureLists::kChunkPairs} + 1u) * 2u, 7u);
    const TextureLists::Place a = lists.write(pairs.data(), TextureLists::kChunkPairs - 2u, 0u, make);
    const std::uint64_t address = lists.address(a);
    const TextureLists::Place b = lists.write(pairs.data(), 4u, 0u, make);
    check(lists.chunk_count() == 2u && b.chunk == 1u && b.first == 0u, "a list past a chunk's end opens a new chunk");
    check(lists.address(a) == address && lists.address(b) != address, "chunks never move");
    const TextureLists::Place big = lists.write(pairs.data(), TextureLists::kChunkPairs + 1u, 0u, make);
    check(big.valid() && big.room == 2u * TextureLists::kChunkPairs && lists.chunk_count() == 3u, "a list longer than a chunk gets a chunk of its own size");
    lists.clear();
    check(lists.chunk_count() == 0u, "cleared");
}

void test_write_in() {
    TextureLists lists;
    Maker make;
    std::vector<std::uint32_t> pairs(std::size_t{TextureLists::kChunkPairs} * 2u, 3u);
    const TextureLists::Place a = lists.write(pairs.data(), 2u, 0u, make);
    const TextureLists::Place in_last = lists.write_in(0u, pairs.data(), 2u, 0u);
    check(in_last.valid() && in_last.chunk == 0u && in_last.first == 2u, "write_in takes room at the end of the last chunk");
    const TextureLists::Place fill = lists.write(pairs.data(), TextureLists::kChunkPairs - 4u, 0u, make);
    check(fill.chunk == 1u, "a list longer than the room left opens a new chunk");
    lists.release(a, 1u);
    check(!lists.write_in(1u, pairs.data(), 2u, 1u).valid(), "a free place of another chunk is not taken");
    const TextureLists::Place again = lists.write_in(0u, pairs.data(), 2u, 1u);
    check(again.valid() && again.chunk == 0u && again.first == a.first, "a free place of the chunk asked for is taken");
    check(!lists.write_in(0u, pairs.data(), 2u, 1u).valid(), "a chunk that is not the last has no room past its end");
    check(!lists.write_in(7u, pairs.data(), 2u, 1u).valid(), "no such chunk");
    const TextureLists::Place fresh = lists.write_fresh(pairs.data(), 2u, 3u, make);
    check(fresh.valid() && fresh.chunk == lists.chunk_count() - 1u && fresh.first == 0u, "write_fresh opens a chunk of its own");
}

void test_target_numbers() {
    galaxy::render::core::TargetSlots targets;
    const std::uint32_t a = targets.number(0x80000010u);
    const std::uint32_t b = targets.number(0x80000011u);
    check(a == 0u && b == 1u && targets.number(0x80000010u) == a, "a target keeps its number");
    targets.forget(0x80000010u);
    check(targets.number(0x80000012u) == a && targets.count() == 2u, "a forgotten target's number goes to the next");
}

// The walk-level counting test (FRAME-MODEL 2.10, R2.1): galaxy::render::core::walk_list with a host that counts what
// the renderer and the store are asked, and the lists' writes.
struct Entry {
    std::uint32_t texture = 0u;
};
struct Table {
    std::vector<Entry> list;
    std::uint32_t count = 0u;
    std::vector<std::uint32_t> sampler_slots;
    std::uint64_t textures_revision = ~0ull;
    std::uint32_t samplers_generation = 0u;
    TextureLists::Place list_place{};
    std::vector<std::uint32_t> list_pairs;
    bool has_target = false;
    std::uint64_t walk_stamp = 0u;
    std::uint64_t lists = 0u;
    std::uint32_t list_first = 0u;
};
Table table_of(std::vector<std::uint32_t> textures) {
    Table table;
    for (const std::uint32_t texture : textures) {
        table.list.push_back(Entry{texture});
    }
    table.count = static_cast<std::uint32_t>(textures.size());
    return table;
}
struct Picture {
    std::uint32_t slot = 0u;
    bool resource = false;
};
constexpr std::uint32_t kNullSlot = 999u;
struct CountingHost {
    using Handle = Picture;
    Maker& maker;
    std::uint64_t walk_serial = 1u;
    std::uint64_t state_now = 1u;
    std::uint32_t generation_now = 0u;
    std::uint64_t revision_now = 0u;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pictures;  // target, its picture's slot
    unsigned lookups = 0u, samplers = 0u, target_lookups = 0u;
    [[nodiscard]] std::uint64_t walk() const { return walk_serial; }
    [[nodiscard]] std::uint64_t state() const { return state_now; }
    [[nodiscard]] std::uint32_t generation() const { return generation_now; }
    [[nodiscard]] std::uint64_t revision() const { return revision_now; }
    [[nodiscard]] static bool is_target(std::uint32_t texture) { return (texture & 0x80000000u) != 0u; }
    Picture handle(std::uint32_t texture) {
        ++lookups;
        return Picture{texture + 100u, true};
    }
    [[nodiscard]] std::uint32_t texture_slot(const Picture& picture) const { return picture.resource ? picture.slot : kNullSlot; }
    std::uint32_t sampler_slot(const Entry&, const Picture& picture) {
        ++samplers;
        return picture.resource ? 5u : 6u;
    }
    std::uint32_t target_slot(std::uint32_t target) {
        ++target_lookups;
        for (const auto& [id, slot] : pictures) {
            if (id == target) {
                return slot;
            }
        }
        return kNullSlot;
    }
    [[nodiscard]] std::uint64_t completed() const { return 0u; }
    [[nodiscard]] std::uint64_t submitted() const { return 0u; }
    TextureLists::Chunk make(std::uint32_t pairs) { return maker(pairs); }
};

// The pair `index` of a place as the program reads it.
std::uint32_t texture_at(const Maker& make, const TextureLists::Place& place, std::uint32_t index) {
    return make.memory[place.chunk][(std::size_t{place.first} + index) * 2u];
}

void test_walk_counts() {
    using galaxy::render::core::kTargetMap;
    using galaxy::render::core::walk_list;
    constexpr std::uint32_t kTarget = 0x80000005u;
    TextureLists lists;
    galaxy::render::core::TargetSlots targets;
    Maker make;
    CountingHost host{make};
    host.pictures.push_back({kTarget, 50u});
    std::vector<Picture> handles;
    std::vector<std::uint32_t> pairs;
    Table plain = table_of({1u, 2u});
    Table with_target = table_of({3u, kTarget});
    const auto table_writes = [&] { return lists.writes() - targets.writes(); };
    std::uint32_t slots = 0u;

    // Walk 1: each table named by two draws.
    check(walk_list(plain, host, lists, targets, handles, pairs, slots) && slots == 0u, "a list naming no target has no slots");
    check(walk_list(with_target, host, lists, targets, handles, pairs, slots), "a list naming a target walks");
    const std::uint32_t slots_walk1 = slots;
    check(walk_list(plain, host, lists, targets, handles, pairs, slots), "the second draw of a list");
    check(walk_list(with_target, host, lists, targets, handles, pairs, slots) && slots == slots_walk1,
          "the second draw of a list naming a target reads the same slots");
    check(table_writes() == 2u, "walk 1 writes each table once");
    check(targets.writes() == 1u, "walk 1 writes the target slots once");
    check(host.lookups == 3u, "walk 1 looks each texture up once");
    check(texture_at(make, with_target.list_place, 0u) == 103u &&
              texture_at(make, with_target.list_place, 1u) == (kTargetMap | 0u),
          "a target is named in its list by its number");
    check(with_target.list_place.chunk == plain.list_place.chunk, "one chunk");
    const TextureLists::Place first_slots{with_target.list_place.chunk, slots_walk1, 1u};
    check(texture_at(make, first_slots, 0u) == 50u, "the slots name the target's picture");

    // A picture moves to another image within walk 1: a new state of the slots, the list stands.
    host.pictures[0].second = 60u;
    ++host.state_now;
    check(walk_list(with_target, host, lists, targets, handles, pairs, slots) && slots != slots_walk1,
          "after a picture moved the draw reads new slots");
    check(table_writes() == 2u, "a picture that moved writes no list");
    check(targets.writes() == 2u, "the slots are written once for the new state");
    check(texture_at(make, TextureLists::Place{with_target.list_place.chunk, slots, 1u}, 0u) == 60u &&
              texture_at(make, first_slots, 0u) == 50u,
          "the new slots name the new picture; the draws before read the old");

    // Walk 2, the same lists: the textures are looked up again, nothing is written but the slots.
    host.walk_serial = 2u;
    ++host.state_now;
    check(walk_list(plain, host, lists, targets, handles, pairs, slots), "walk 2, the list");
    check(walk_list(with_target, host, lists, targets, handles, pairs, slots), "walk 2, the list naming a target");
    check(table_writes() == 2u, "the same lists walked again write no table");
    check(targets.writes() == 3u, "walk 2 writes the slots once");
    check(host.lookups == 6u, "each walk looks the textures up");
    check(host.samplers == 4u, "the samplers are made once, a target's too");

    // New textures in the store: samplers made again; the same slots, no list written.
    ++host.revision_now;
    host.walk_serial = 3u;
    check(walk_list(with_target, host, lists, targets, handles, pairs, slots), "walk 3");
    check(host.samplers == 6u && table_writes() == 2u, "the samplers follow the store; the pairs stood");

    // A target with no picture reads the null slot; a forgotten target's number reads it too.
    Table missing = table_of({0x80000006u});
    host.walk_serial = 4u;
    ++host.state_now;
    check(walk_list(missing, host, lists, targets, handles, pairs, slots), "walk 4");
    check(texture_at(make, TextureLists::Place{missing.list_place.chunk, slots, 2u}, 1u) == kNullSlot,
          "a target with no picture reads the null slot");
    targets.forget(kTarget);
    ++host.state_now;
    check(walk_list(missing, host, lists, targets, handles, pairs, slots), "walk 4 after a target was freed");
    check(texture_at(make, TextureLists::Place{missing.list_place.chunk, slots, 2u}, 0u) == kNullSlot,
          "a free number reads the null slot");
}

// A list whose chunk has no room left for the slots moves to a new chunk beside them.
void test_walk_slots_beside_the_list() {
    TextureLists lists;
    galaxy::render::core::TargetSlots targets;
    Maker make;
    CountingHost host{make};
    std::vector<Picture> handles;
    std::vector<std::uint32_t> pairs((std::size_t{TextureLists::kChunkPairs} / 2u) * 2u, 1u);
    std::vector<std::uint32_t> filler = pairs;
    (void)lists.write(filler.data(), TextureLists::kChunkPairs / 2u, 0u, make);
    Table table = table_of({0x80000001u});
    std::uint32_t slots = 0u;
    check(walk_list(table, host, lists, targets, handles, pairs, slots) && table.list_place.chunk == 0u, "the list in chunk 0");
    (void)lists.write(filler.data(), TextureLists::kChunkPairs / 2u - 2u, 0u, make);
    check(lists.chunk_count() == 2u, "chunk 0 is no longer the last");
    ++host.state_now;
    check(walk_list(table, host, lists, targets, handles, pairs, slots), "the list moves");
    check(table.list_place.chunk == 2u && lists.chunk_count() == 3u, "to a new chunk");
    check(texture_at(make, table.list_place, 0u) == (galaxy::render::core::kTargetMap | 0u) &&
              texture_at(make, TextureLists::Place{2u, slots, 1u}, 0u) == kNullSlot,
          "the list and its slots in one chunk");
}

}  // namespace

int main() {
    test_written_once();
    test_fence();
    test_chunks();
    test_write_in();
    test_target_numbers();
    test_walk_counts();
    test_walk_slots_beside_the_list();
    if (failures != 0) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    std::printf("texture lists: all passed\n");
    return 0;
}
