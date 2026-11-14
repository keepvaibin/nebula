// SPDX-License-Identifier: GPL-3.0-only
// UploadRing, and gpu::create_upload_memory for D3D12.
#include "galaxy/render/gpu_upload_ring.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>


#if !defined(NEBULA_GPU_VULKAN) && !defined(NEBULA_GPU_METAL)
#include "galaxy/windows_api.h"
#include <d3d12.h>
#endif

namespace galaxy::render {

namespace {
std::atomic<std::uint64_t> g_upload_ring_identity{1};
}  // namespace

void UploadRing::reset() noexcept {
    const gpu::UploadPlace place = place_;
    const std::size_t floor_capacity = floor_capacity_;
    *this = UploadRing{};
    place_ = place;
    floor_capacity_ = floor_capacity;
}

void UploadRing::release_into(std::vector<gpu::BufferRef>& out) {
    if (buffer_) {
        out.push_back(std::move(buffer_));
    }
    for (std::vector<Chunk>& slot : chunk_slots_) {
        for (Chunk& chunk : slot) {
            if (chunk.memory.buffer) {
                out.push_back(std::move(chunk.memory.buffer));
            }
        }
    }
    reset();
}

bool UploadRing::initialize(
    gpu::Device* device, const char* name, std::size_t bytes_per_frame,
    unsigned frames_in_flight, std::size_t capacity, bool chunks) {
    if (device == nullptr || bytes_per_frame == 0u || frames_in_flight == 0u ||
        bytes_per_frame > std::numeric_limits<std::size_t>::max() / frames_in_flight) {
        return false;
    }
    // Everything that can fail runs before the current buffer is replaced.
    const std::size_t total = std::max(bytes_per_frame * frames_in_flight, capacity);
    gpu::UploadMemory memory = gpu::create_upload_memory(device, total, name, place_);
    if (!memory) {
        return false;
    }
    std::vector<std::uint64_t> generations(frames_in_flight, 0u);
    const gpu::Address gpu_base = memory.gpu;
    // GPU address ranges, so a DRED page-fault address can be attributed.
    std::cout << "[ring] name=" << (name != nullptr ? name : "") << " gpu=0x"
              << std::hex << gpu_base << "-0x" << (gpu_base + total) << std::dec
              << " bytes=" << total << " memory=" << (memory.local ? "video (CPU-mapped)" : "host") << '\n';
    local_ = memory.local;
    device_ = device;
    chunks_ = chunks;
    chunk_slots_.clear();
    chunk_slots_.resize(chunks ? frames_in_flight : 0u);
    chunk_bytes_made_ = 0u;
    buffer_ = std::move(memory.buffer);
    mapped_ = memory.cpu;
    gpu_base_ = gpu_base;
    name_ = name != nullptr ? name : "";
    bytes_per_frame_ = bytes_per_frame;
    frames_in_flight_ = frames_in_flight;
    capacity_ = total;
    resource_identity_ = g_upload_ring_identity.fetch_add(1, std::memory_order_relaxed);
    slot_generations_ = std::move(generations);
    segment_base_ = cursor_ = 0u;
    spill_cursor_ = spill_base();
    ++epoch_;
    frame_spill_ = 0u;
    high_water_ = 0u;
    requested_ = 0u;
    // The first capacity is the least the ring is ever shrunk to; a new buffer starts a new quiet window.
    if (floor_capacity_ == 0u) {
        floor_capacity_ = total;
    }
    window_peak_ = shrink_peak_ = 0u;
    window_frames_ = 0u;
    shrink_ready_ = false;
    active_slot_ = 0u;
    frame_ready_ = false;
    segment_allocation_pending_ = true;
    peak_bytes_per_frame_ = 0u;
    return true;
}

bool UploadRing::resegment(std::size_t bytes_per_frame, unsigned frames_in_flight) {
    if (!buffer_ || mapped_ == nullptr || bytes_per_frame == 0u || frames_in_flight == 0u ||
        bytes_per_frame > capacity_ / frames_in_flight) {
        return false;
    }
    bytes_per_frame_ = bytes_per_frame;
    frames_in_flight_ = frames_in_flight;
    resource_identity_ = g_upload_ring_identity.fetch_add(1, std::memory_order_relaxed);
    slot_generations_.assign(frames_in_flight, 0u);
    segment_base_ = cursor_ = 0u;
    spill_cursor_ = spill_base();
    ++epoch_;
    frame_spill_ = 0u;
    active_slot_ = 0u;
    frame_ready_ = false;
    segment_allocation_pending_ = true;
    peak_bytes_per_frame_ = 0u;
    return true;
}

void UploadRing::begin_frame(unsigned frame_slot) {
    if (mapped_ == nullptr || frame_slot >= frames_in_flight_) {
        throw std::runtime_error("UploadRing frame slot is out of range");
    }
    // The frame before: what it took (segment, spill and chunks) joins the quiet window. A window of kQuietFrames frames whose
    // peak stayed within a quarter of the budget lets the ring shrink (wants_shrink); a busier one does not.
    if (frame_ready_) {
        window_peak_ = std::max(window_peak_, cursor_ - segment_base_ + frame_spill_);
        if (++window_frames_ >= kQuietFrames) {
            shrink_ready_ = capacity_ > floor_capacity_ && window_peak_ <= bytes_per_frame_ / 4u;
            shrink_peak_ = window_peak_;
            window_peak_ = 0u;
            window_frames_ = 0u;
        }
    }
    active_slot_ = frame_slot;
    if (frame_slot < chunk_slots_.size()) {
        chunk_slots_[frame_slot].clear();  // that slot's last frame has retired: its chunks go
    }
    ++epoch_;
    segment_allocation_pending_ = true;
    segment_base_ = static_cast<std::size_t>(frame_slot) * bytes_per_frame_;
    cursor_       = segment_base_;
    frame_spill_ = 0u;
    frame_ready_ = true;
}

// A request past the segment and the spill: a chunk of this frame slot (the newest if it has room, else a new buffer at least
// twice the one before, and at least the request), counted into the frame's use so the ring grows at the next boundary.
UploadRing::Allocation UploadRing::allocate_in_chunk(std::size_t size, std::size_t alignment, std::size_t& wanted) {
    std::vector<Chunk>& chunks = chunk_slots_[active_slot_];
    const auto place = [&](Chunk& chunk) -> Allocation {
        const std::size_t at = chunk.used + ((std::size_t{0} - chunk.used) & (alignment - 1u));
        if (at > chunk.size || size > chunk.size - at) {
            return {};
        }
        chunk.used = at + size;
        return Allocation{chunk.memory.cpu + at, chunk.memory.gpu + static_cast<gpu::Address>(at), at, gpu::raw(chunk.memory.buffer)};
    };
    if (!chunks.empty()) {
        if (const Allocation placed = place(chunks.back()); placed.cpu != nullptr) {
            return placed;
        }
    }
    constexpr std::size_t kFirstChunk = std::size_t{4} << 20u;
    wanted = std::max({size + alignment, kFirstChunk, chunks.empty() ? std::size_t{0} : chunks.back().size * 2u});
    Chunk chunk;
    chunk.memory = gpu::create_upload_memory(device_, wanted, name_, place_);
    if (!chunk.memory) {
        return {};
    }
    chunk.size = wanted;
    chunk_bytes_made_ += wanted;
    std::cout << "[ring] name=" << name_ << " chunk bytes=" << wanted << " (a frame took more than its segment and the spill)\n";
    chunks.push_back(std::move(chunk));
    return place(chunks.back());
}

UploadRing::Allocation UploadRing::allocate(std::size_t size, std::size_t alignment) {
    Allocation out;
    std::size_t wanted = 0u;
    switch (take(size, alignment, out, wanted)) {
    case Miss::None:
        return out;
    case Miss::NotReady:
        throw std::runtime_error("UploadRing allocation before begin_frame");
    case Miss::Alignment:
        throw std::runtime_error("UploadRing alignment is not a nonzero power of two");
    case Miss::NoChunk:
        throw std::runtime_error(std::string("UploadRing '") + name_ + "' out of upload memory for a chunk of " +
                                 std::to_string(wanted) + " B");
    case Miss::Exhausted:
        break;
    }
    throw std::runtime_error(
        std::string("UploadRing '") + name_ + "' exhausted: request " +
        std::to_string(size) + " B at frame-used " +
        std::to_string(cursor_ - segment_base_ + frame_spill_) + "/" +
        std::to_string(bytes_per_frame_) + " B and spill " +
        std::to_string(capacity_ - spill_base()) + " B");
}

UploadRing::Allocation UploadRing::try_allocate(std::size_t size, std::size_t alignment) noexcept {
    try {
        Allocation out;
        std::size_t wanted = 0u;
        return take(size, alignment, out, wanted) == Miss::None ? out : Allocation{};
    } catch (...) {
        return {};
    }
}

UploadRing::Miss UploadRing::take(std::size_t size, std::size_t alignment, Allocation& out, std::size_t& wanted) {
    if (!frame_ready_) {
        return Miss::NotReady;
    }
    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        return Miss::Alignment;
    }
    // Padding by unsigned negation cannot overflow; padding and payload are checked before any address is formed.
    const auto fits = [&](std::size_t at, std::size_t end) {
        const std::size_t padding = (std::size_t{0} - at) & (alignment - 1u);
        const std::size_t remaining = end - at;
        return padding <= remaining && size <= remaining - padding;
    };
    std::size_t aligned = 0u;
    gpu::Resource* chunk_resource = nullptr;
    Allocation chunked;
    if (fits(cursor_, segment_base_ + bytes_per_frame_)) [[likely]] {
        aligned = cursor_ + ((std::size_t{0} - cursor_) & (alignment - 1u));
        cursor_ = aligned + size;
    } else if (fits(spill_cursor_, capacity_)) {
        aligned = spill_cursor_ + ((std::size_t{0} - spill_cursor_) & (alignment - 1u));
        frame_spill_ += aligned + size - spill_cursor_;
        spill_cursor_ = aligned + size;
    } else if (chunks_ && device_ != nullptr && active_slot_ < chunk_slots_.size()) {
        chunked = allocate_in_chunk(size, alignment, wanted);
        if (chunked.cpu == nullptr) {
            return Miss::NoChunk;
        }
        chunk_resource = chunked.resource;
        frame_spill_ += size + alignment;
    } else {
        return Miss::Exhausted;
    }
    // Bytes this frame has handed out: the floor for its budget.
    const std::size_t frame_used = cursor_ - segment_base_ + frame_spill_;
    if (frame_used > peak_bytes_per_frame_) {
        peak_bytes_per_frame_ = frame_used;
        high_water_ = std::max(high_water_, frame_used);
    }
    // A slot's generation advances on its first nonzero allocation only: fence-only chunks begin the frame but
    // write no bytes.
    if (size != 0 && segment_allocation_pending_) {
        ++slot_generations_[active_slot_];
        segment_allocation_pending_ = false;
    }
    if (chunk_resource != nullptr) {
        out = chunked;
        return Miss::None;
    }
    out = Allocation{
        mapped_ + aligned,
        gpu_base_ + static_cast<gpu::Address>(aligned),
        aligned,
        nullptr
    };
    return Miss::None;
}

}  // namespace galaxy::render

#if !defined(NEBULA_GPU_VULKAN) && !defined(NEBULA_GPU_METAL)
namespace galaxy::render::gpu {

bool device_removed(Device* device) noexcept {
    return device != nullptr && device->GetDeviceRemovedReason() != S_OK;
}

namespace {

// GPU upload heaps (OPTIONS16): video memory the CPU maps through a resizable BAR. A discrete adapter's alone: on one that
// shares the system's memory (UMA) an upload heap is already memory the GPU reads at full speed, so the BAR saves nothing.
bool gpu_upload_heaps(Device* device) {
    D3D12_FEATURE_DATA_ARCHITECTURE architecture{};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture))) ||
        architecture.UMA == TRUE) {
        return false;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options{};
    return SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options, sizeof(options))) &&
           options.GPUUploadHeapSupported == TRUE;
}

}  // namespace

UploadMemory create_upload_memory(Device* device, std::size_t bytes, const char* name, UploadPlace place) {
    UploadMemory out;
    if (device == nullptr || bytes == 0u) {
        return out;
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = static_cast<UINT64>(bytes);
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    BufferRef buffer;
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    bool local = false;
    if (place == UploadPlace::GpuLocal && gpu_upload_heaps(device)) {
        // In video memory the CPU writes: created in the common state (a buffer there is promoted to the read state a draw
        // needs), else as an upload heap's buffer is; a heap that refuses either leaves the upload heap below.
        D3D12_HEAP_PROPERTIES local_hp{};
        local_hp.Type = D3D12_HEAP_TYPE_GPU_UPLOAD;
        for (const D3D12_RESOURCE_STATES state : {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_GENERIC_READ}) {
            if (SUCCEEDED(device->CreateCommittedResource(&local_hp, D3D12_HEAP_FLAG_NONE, &rd, state, nullptr,
                                                          IID_PPV_ARGS(&buffer))) &&
                SUCCEEDED(buffer->Map(0, &no_read, &mapped)) && mapped != nullptr) {
                local = true;
                break;
            }
            buffer.Reset();
            mapped = nullptr;
        }
    }
    if (!local &&
        (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                nullptr, IID_PPV_ARGS(&buffer))) ||
         FAILED(buffer->Map(0, &no_read, &mapped)) || mapped == nullptr)) {
        return out;
    }
    if (name != nullptr) {
        buffer->SetName(std::wstring(name, name + std::strlen(name)).c_str());
    }
    out.cpu = static_cast<std::byte*>(mapped);
    out.gpu = buffer->GetGPUVirtualAddress();
    out.buffer = std::move(buffer);
    out.local = local;
    return out;
}

}  // namespace galaxy::render::gpu
#endif
