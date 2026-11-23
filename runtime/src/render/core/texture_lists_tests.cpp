// SPDX-License-Identifier: GPL-3.0-only
// Texture lists that live as long as their table (texture_lists.h, FRAME-MODEL 2.10): a list is written once and
// stands; a place given up is not used again before the GPU's fence passes it; chunks never move.

#include "render/core/texture_lists.h"

#include <cstdint>
#include <cstdio>
#include <memory>
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

}  // namespace

int main() {
    test_written_once();
    test_fence();
    test_chunks();
    if (failures != 0) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    std::printf("texture lists: all passed\n");
    return 0;
}
