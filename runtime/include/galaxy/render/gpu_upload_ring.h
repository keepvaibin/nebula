// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The per-frame upload rings both renderers allocate draw data from (vertices, indices, constants, matrix
// palettes), over upload memory from gpu::create_upload_memory.

#include "galaxy/render/gpu_types.h"

#include <algorithm>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::render {

// Linear per-frame allocator over one persistently mapped upload buffer, split into one segment per frame in
// flight so a frame never overwrites data the GPU still reads for an earlier one.
class UploadRing {
public:
    // Releases the buffer; the caller has retired every GPU reference to it. The place stays.
    void reset() noexcept;
    // Where the ring's memory lies from the next initialize on (and its chunks): kept across reset and release_into.
    void set_place(gpu::UploadPlace place) noexcept { place_ = place; }
    // The ring's buffer is in video memory (a GpuLocal ring on a device that offered it).
    [[nodiscard]] bool local() const noexcept { return local_; }
    struct Allocation {
        std::byte* cpu = nullptr;
        gpu::Address gpu = 0;
        std::size_t offset = 0;     // within `resource`, else the whole resource
        // The chunk it lies in (a ring made with `chunks`, when the segments and the spill were full), else null: the ring's
        // own buffer. A user that names a buffer and an offset takes both from here (resource_of).
        gpu::Resource* resource = nullptr;
    };
    [[nodiscard]] gpu::Resource* resource_of(const Allocation& at) const { return at.resource != nullptr ? at.resource : resource(); }

    // `capacity`: bytes reserved beyond bytes_per_frame * frames_in_flight, so later
    // resegment() calls up to it need no new buffer. `chunks`: a request that fits neither the frame's segment nor the spill
    // is placed in a chunk of its own (a new buffer, kept until that frame's slot comes round again) instead of throwing;
    // only for a ring whose users take absolute addresses (not the matrix ring, whose rows are indexed from gpu_base()).
    bool initialize(
        gpu::Device* device,
        const char* name,
        std::size_t bytes_per_frame,
        unsigned frames_in_flight,
        std::size_t capacity = 0,
        bool chunks = false);
    // The buffer and the chunks go to `out` for the caller to release once the GPU is past every frame that used them (the
    // ring is empty after; initialize makes the next).
    void release_into(std::vector<gpu::BufferRef>& out);
    // The bytes of chunks made since initialize (diagnostics).
    [[nodiscard]] std::size_t chunk_bytes_made() const { return chunk_bytes_made_; }
    // The bytes of chunks that exist now (the frame slots' that have not begun again), for the video memory breakdown.
    [[nodiscard]] std::size_t live_chunk_bytes() const {
        std::size_t bytes = 0u;
        for (const std::vector<Chunk>& slot : chunk_slots_) {
            for (const Chunk& chunk : slot) {
                bytes += chunk.size;
            }
        }
        return bytes;
    }
    // A new segmentation inside the existing buffer (the caller has retired every GPU
    // reference to its contents); false when it does not fit.
    bool resegment(std::size_t bytes_per_frame, unsigned frames_in_flight);
    void begin_frame(unsigned frame_slot);
    // Called before the frame starts: a new list can be larger than every
    // previous frame, so growth cannot depend only on yesterday's high water.
    void require_frame_bytes(std::size_t bytes) noexcept {
        requested_ = std::max(requested_, bytes);
        window_peak_ = std::max(window_peak_, bytes);
    }

    // Bump-allocates in the current frame's segment, then in the spill region behind the segments (the capacity
    // past them), which only a resegment or initialize rewinds: spilled() asks the caller to grow the ring at the
    // next frame boundary. `alignment` must be a nonzero power of two. Exhausting both goes to a chunk (a ring made with
    // `chunks`), else throws rather than dropping geometry. A rejected request consumes nothing.
    Allocation allocate(std::size_t size, std::size_t alignment);
    // allocate's placement without its throws: an empty Allocation (cpu null) where allocate would throw, consuming nothing.
    [[nodiscard]] Allocation try_allocate(std::size_t size, std::size_t alignment) noexcept;

    // The peak since the last call (diagnostics windows).
    [[nodiscard]] std::size_t take_peak_bytes_per_frame() noexcept {
        const std::size_t peak = peak_bytes_per_frame_;
        peak_bytes_per_frame_ = 0u;
        return peak;
    }
    // A segmentation's per-frame budget: `capacity` in `segments` equal shares and one more share of spill, so a
    // frame may take twice its budget.
    [[nodiscard]] static std::size_t budget(std::size_t capacity, unsigned segments) {
        return capacity / (segments + 1u) & ~std::size_t{0xFFFFu};
    }
    // A frame took over half the budget (or spilled): grow at the next frame boundary, to grown_capacity().
    [[nodiscard]] bool wants_growth() const { return mapped_ != nullptr && std::max(high_water_, requested_) > bytes_per_frame_ / 2u; }
    // The capacity whose budget for `segments` is twice the high-water mark: the current one when it suffices, 0
    // when the host address space cannot represent the allocation.
    [[nodiscard]] std::size_t grown_capacity(unsigned segments) const {
        const std::size_t needed = std::max(high_water_, requested_);
        if (needed > std::numeric_limits<std::size_t>::max() / 2u || capacity_ == 0u) { return 0u; }
        std::size_t capacity = capacity_;
        while (budget(capacity, segments) < needed * 2u) {
            if (capacity > std::numeric_limits<std::size_t>::max() / 2u) {
                return 0u;
            }
            capacity *= 2u;
        }
        return capacity;
    }
    // After kQuietFrames frames in a row in which no frame used more than a quarter of the budget (and none was asked to), the
    // ring can give memory back: shrunk_capacity() is the smallest capacity, a halving of this one at a time and never under the
    // capacity the ring first had, whose budget is still twice the quiet window's peak. The caller makes the ring again at that
    // size at a frame boundary, as for growth. The window starts over with every initialize.
    static constexpr unsigned kQuietFrames = 600u;
    [[nodiscard]] bool wants_shrink() const { return mapped_ != nullptr && shrink_ready_ && !wants_growth(); }
    [[nodiscard]] std::size_t shrunk_capacity(unsigned segments) const {
        std::size_t capacity = capacity_;
        while (capacity / 2u >= floor_capacity_ && budget(capacity / 2u, segments) >= shrink_peak_ * 2u &&
               budget(capacity / 2u, segments) != 0u) {
            capacity /= 2u;
        }
        return capacity;
    }
    // A frame took spill bytes since the last segmentation.
    [[nodiscard]] bool spilled() const { return spill_cursor_ != spill_base(); }
    // The most bytes one frame took, segment and spill, since initialize.
    [[nodiscard]] std::size_t high_water() const { return high_water_; }
    [[nodiscard]] std::size_t bytes_per_frame() const { return bytes_per_frame_; }
    [[nodiscard]] std::size_t capacity() const { return capacity_; }
    // The addressable bytes from gpu_base(): segments and spill.
    [[nodiscard]] std::size_t total_bytes() const { return capacity_; }
    [[nodiscard]] std::size_t cursor() const { return cursor_; }
    // Counts the rewinds (begin_frame, resegment, initialize): memory allocated before the latest one may be reused
    // once its frame retires, so a user that keeps allocations together (the matrix arena's rows and their table)
    // starts over when it changes.
    [[nodiscard]] std::uint64_t epoch() const { return epoch_; }

    [[nodiscard]] gpu::Resource* resource() const { return gpu::raw(buffer_); }
    [[nodiscard]] gpu::Address gpu_base() const { return gpu_base_; }
    [[nodiscard]] const std::byte* cpu_base() const { return mapped_; }
    // The persistent mapping (the whole resource, all segments).
    [[nodiscard]] std::byte* mapped() const { return mapped_; }

private:
    [[nodiscard]] std::size_t spill_base() const { return bytes_per_frame_ * frames_in_flight_; }

    struct Chunk {
        gpu::UploadMemory memory;
        std::size_t size = 0;
        std::size_t used = 0;
    };
    // Empty when no chunk could be made (`wanted`: the bytes asked of the device).
    [[nodiscard]] Allocation allocate_in_chunk(std::size_t size, std::size_t alignment, std::size_t& wanted);
    enum class Miss : unsigned char { None, NotReady, Alignment, Exhausted, NoChunk };
    // The placement both allocate and try_allocate make; `out` is set only with Miss::None.
    [[nodiscard]] Miss take(std::size_t size, std::size_t alignment, Allocation& out, std::size_t& wanted);

    gpu::Device* device_ = nullptr;
    gpu::UploadPlace place_ = gpu::UploadPlace::Host;
    bool local_ = false;
    bool chunks_ = false;
    std::vector<std::vector<Chunk>> chunk_slots_;  // by frame slot; released when the slot begins again
    std::size_t chunk_bytes_made_ = 0;
    gpu::BufferRef buffer_;
    const char* name_ = "";
    std::byte* mapped_ = nullptr;
    // Stable until initialize replaces the buffer.
    gpu::Address gpu_base_ = 0;
    std::size_t bytes_per_frame_ = 0;
    std::size_t capacity_ = 0;
    std::size_t segment_base_ = 0;
    std::uint64_t epoch_ = 0;
    std::size_t cursor_ = 0;
    std::size_t spill_cursor_ = 0;
    // Spill bytes the current frame took.
    std::size_t frame_spill_ = 0;
    std::size_t high_water_ = 0;
    std::size_t requested_ = 0;
    // The quiet window (kQuietFrames frames): its peak use or request, how many frames it has, whether the last full one was quiet
    // enough to shrink after (and its peak then), and the capacity the ring first had (kept across reset).
    std::size_t window_peak_ = 0;
    std::size_t shrink_peak_ = 0;
    unsigned window_frames_ = 0;
    bool shrink_ready_ = false;
    std::size_t floor_capacity_ = 0;
    unsigned frames_in_flight_ = 0;
    unsigned active_slot_ = 0;
    bool frame_ready_ = false;
    bool segment_allocation_pending_ = true;
    std::uint64_t resource_identity_ = 0;
    std::vector<std::uint64_t> slot_generations_;
    std::size_t peak_bytes_per_frame_ = 0;
};

// A mapped upload buffer that lives until it is released (RenderCore's resident decoded vertices).
using UploadBuffer = gpu::UploadMemory;

}  // namespace galaxy::render
