// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The per-frame upload rings both GX renderers allocate draw data from (vertices, indices, constants, matrix
// palettes), over upload memory from gpu::create_upload_memory.

#include "galaxy/gx/gpu_types.h"
#include "galaxy/gx/immutable_upload.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::gx {

// Linear per-frame allocator over one persistently mapped upload buffer, split into one segment per frame in
// flight so a frame never overwrites data the GPU still reads for an earlier one.
class UploadRing {
public:
    // Releases the buffer; the caller has retired every GPU reference to it.
    void reset() noexcept;
    struct Allocation {
        std::byte* cpu = nullptr;
        gpu::Address gpu = 0;
        std::size_t offset = 0;     // within the whole resource
    };

    // `capacity`: bytes reserved beyond bytes_per_frame * frames_in_flight, so later
    // resegment() calls up to it need no new buffer.
    bool initialize(
        gpu::Device* device,
        const char* name,
        std::size_t bytes_per_frame,
        unsigned frames_in_flight,
        std::size_t capacity = 0);
    // A new segmentation inside the existing buffer (the caller has retired every GPU
    // reference to its contents); false when it does not fit. Immutable uploads start over.
    bool resegment(std::size_t bytes_per_frame, unsigned frames_in_flight);
    void begin_frame(unsigned frame_slot);

    // Bump-allocates in the current frame's segment; `alignment` must be a nonzero power of two. Exhaustion
    // throws rather than dropping geometry. A rejected request consumes nothing.
    Allocation allocate(std::size_t size, std::size_t alignment);

    // Allocates `size` bytes and copies `source` into them, unless `token` records the same allocation in this
    // slot's previous use, whose bytes are still in place.
    Allocation upload_immutable(
        const void* source, std::size_t size, std::size_t alignment,
        ImmutableUploadToken& token);
    [[nodiscard]] std::uint64_t reused_bytes() const { return reused_bytes_; }
    [[nodiscard]] std::uint64_t copied_bytes() const { return copied_bytes_; }
    // Number of upload_immutable() calls this frame; separates a frame that never took that path from one that
    // copied every byte.
    [[nodiscard]] std::uint64_t immutable_upload_calls() const {
        return immutable_upload_calls_;
    }

    // The peak since the last call (diagnostics windows).
    [[nodiscard]] std::size_t take_peak_bytes_per_frame() noexcept {
        const std::size_t peak = peak_bytes_per_frame_;
        peak_bytes_per_frame_ = 0u;
        return peak;
    }
    [[nodiscard]] std::size_t total_bytes() const { return bytes_per_frame_ * frames_in_flight_; }
    // CPU view of a GPU address inside this ring; null outside it.
    [[nodiscard]] const std::byte* cpu_at(gpu::Address va) const {
        return va >= gpu_base_ && va < gpu_base_ + total_bytes() ? mapped_ + (va - gpu_base_) : nullptr;
    }
    [[nodiscard]] std::size_t cursor() const { return cursor_; }

    [[nodiscard]] gpu::Resource* resource() const { return gpu::raw(buffer_); }
    [[nodiscard]] gpu::Address gpu_base() const { return gpu_base_; }
    // The persistent mapping (the whole resource, all segments).
    [[nodiscard]] std::byte* mapped() const { return mapped_; }

private:
    gpu::BufferRef buffer_;
    const char* name_ = "";
    std::byte* mapped_ = nullptr;
    // Stable until initialize replaces the buffer.
    gpu::Address gpu_base_ = 0;
    std::size_t bytes_per_frame_ = 0;
    std::size_t capacity_ = 0;
    std::size_t segment_base_ = 0;
    std::size_t cursor_ = 0;
    unsigned frames_in_flight_ = 0;
    unsigned active_slot_ = 0;
    bool frame_ready_ = false;
    bool segment_allocation_pending_ = true;
    std::uint64_t resource_identity_ = 0;
    std::vector<std::uint64_t> slot_generations_;
    std::uint64_t reused_bytes_ = 0;
    std::uint64_t copied_bytes_ = 0;
    std::uint64_t immutable_upload_calls_ = 0;
    std::size_t peak_bytes_per_frame_ = 0;
};

// A mapped upload buffer that lives until it is released (GxBackend's resident decoded vertices).
using UploadBuffer = gpu::UploadMemory;

}  // namespace galaxy::gx
