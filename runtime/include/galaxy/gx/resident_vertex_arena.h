#pragma once

// ResidentVertexArena: allocator for GPU-resident decoded vertices.
//
// A decoded packet-run cache entry depends only on the cached display-list bytes, the
// VCD/VAT/array key and the validated indexed-array snapshot, so its vertices live in one block
// of a persistently mapped buffer that draws reference directly, instead of being copied into
// the upload ring on every replay. The arena tracks vertex offsets only and owns no GPU object.
//   * allocate() is first-fit from the lowest offset, so entries created in replay order stay
//     contiguous and adjacent runs can merge into one draw batch.
//   * A released block becomes free only after `frames_in_flight` more advance_frame() calls,
//     once the GPU can no longer be reading it.
//   * reset() forgets everything (device teardown) and bumps epoch(); offsets from an older
//     epoch are invalid.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <map>
#include <optional>

namespace galaxy::gx {

class ResidentVertexArena {
public:
    ResidentVertexArena() = default;

    void reset(std::uint32_t capacity_units, unsigned frames_in_flight) {
        free_.clear();
        retired_.clear();
        used_ = 0u;
        frames_in_flight_ = frames_in_flight == 0u ? 1u : frames_in_flight;
        serial_ = 0u;
        ++epoch_;
        if (capacity_units != 0u) {
            free_.emplace(0u, capacity_units);
        }
    }

    [[nodiscard]] std::optional<std::uint32_t> allocate(std::uint32_t units) {
        if (units == 0u) {
            return std::nullopt;
        }
        for (auto it = free_.begin(); it != free_.end(); ++it) {
            if (it->second < units) {
                continue;
            }
            const std::uint32_t offset = it->first;
            const std::uint32_t remaining = it->second - units;
            free_.erase(it);
            if (remaining != 0u) {
                free_.emplace(offset + units, remaining);
            }
            used_ += units;
            return offset;
        }
        return std::nullopt;
    }

    // `offset`/`units` must be a block this epoch returned from allocate().
    void release(std::uint32_t offset, std::uint32_t units) {
        if (units == 0u) {
            return;
        }
        retired_.push_back(Retired{offset, units, serial_});
    }

    // Retirement depth in advance_frame() calls. Interpolated redraws replay a game frame's
    // draws until its ring segment comes round again, so the caller then advances once per
    // game frame with the segment count.
    void set_frames_in_flight(unsigned frames_in_flight) noexcept {
        frames_in_flight_ = frames_in_flight == 0u ? 1u : frames_in_flight;
    }

    // Call once per renderer frame (or per game frame, above), after the
    // renderer waited for the GPU fence of the slot it is about to reuse.
    void advance_frame() {
        ++serial_;
        while (!retired_.empty() &&
               serial_ - retired_.front().serial >= frames_in_flight_) {
            const Retired block = retired_.front();
            retired_.pop_front();
            insert_free(block.offset, block.units);
        }
    }

    [[nodiscard]] std::uint32_t used_units() const noexcept { return used_; }
    [[nodiscard]] std::uint64_t epoch() const noexcept { return epoch_; }

    [[nodiscard]] std::size_t free_block_count() const noexcept {
        return free_.size();
    }
    [[nodiscard]] std::size_t retired_block_count() const noexcept {
        return retired_.size();
    }
private:
    struct Retired {
        std::uint32_t offset = 0;
        std::uint32_t units = 0;
        std::uint64_t serial = 0;
    };

    void insert_free(std::uint32_t offset, std::uint32_t units) {
        used_ -= std::min(used_, units);
        auto next = free_.lower_bound(offset);
        if (next != free_.begin()) {
            auto prev = std::prev(next);
            if (prev->first + prev->second == offset) {
                offset = prev->first;
                units += prev->second;
                free_.erase(prev);
            }
        }
        if (next != free_.end() && offset + units == next->first) {
            units += next->second;
            free_.erase(next);
        }
        free_.emplace(offset, units);
    }

    std::map<std::uint32_t, std::uint32_t> free_;  // offset -> units
    std::deque<Retired> retired_;
    std::uint32_t used_ = 0;
    unsigned frames_in_flight_ = 1u;
    std::uint64_t serial_ = 0;
    std::uint64_t epoch_ = 0;
};

}  // namespace galaxy::gx
