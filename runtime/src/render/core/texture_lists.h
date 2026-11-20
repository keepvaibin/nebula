// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Texture lists that live as long as their table (FRAME-MODEL 2.10). A table's (texture slot, sampler slot) pairs are
// written once into a chunk of a buffer the programs read, and again only when a pair changes; a list a table gives up
// (rewritten, or the table retired or dropped) is used again only once the GPU has finished every frame that could
// read it (the renderer's fence). Chunks never move, so a list's address stands while it is in use; the renderer
// makes them (make_texture_list_chunk) and frees them at shutdown. Header-only: texture_lists_tests.cpp.

#include <cstdint>
#include <cstring>
#include <deque>
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

    std::vector<Chunk> chunks_;
    std::uint32_t used_ = 0u;  // pairs used in the last chunk
    std::deque<Freed> free_[32];
    std::uint64_t writes_ = 0u;
};

}  // namespace galaxy::render::core
