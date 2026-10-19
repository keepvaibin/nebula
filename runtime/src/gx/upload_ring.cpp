// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/upload_ring.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#if !defined(NEBULA_GPU_VULKAN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#endif

namespace galaxy::gx {

// ---------------------------------------------------------------------------
// UploadRing
// ---------------------------------------------------------------------------

namespace {
std::atomic<std::uint64_t> g_upload_ring_identity{1};
}  // namespace

void UploadRing::reset() noexcept {
    *this = UploadRing{};
}

bool UploadRing::initialize(
    gpu::Device* device, const char* name, std::size_t bytes_per_frame,
    unsigned frames_in_flight, std::size_t capacity) {
    if (device == nullptr || bytes_per_frame == 0u || frames_in_flight == 0u ||
        bytes_per_frame > std::numeric_limits<std::size_t>::max() / frames_in_flight) {
        return false;
    }
    // Prepare all fallible state before replacing the retained mapped buffer.
    const std::size_t total = std::max(bytes_per_frame * frames_in_flight, capacity);
    gpu::UploadMemory memory = gpu::create_upload_memory(device, total, name);
    if (!memory) {
        return false;
    }
    std::vector<std::uint64_t> generations(frames_in_flight, 0u);
    const gpu::Address gpu_base = memory.gpu;
    // GPU address ranges, so a DRED page-fault address can be attributed.
    std::cout << "[ring] name=" << (name != nullptr ? name : "") << " gpu=0x"
              << std::hex << gpu_base << "-0x" << (gpu_base + total) << std::dec
              << " bytes=" << total << '\n';
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
    active_slot_ = 0u;
    frame_ready_ = false;
    segment_allocation_pending_ = true;
    reused_bytes_ = copied_bytes_ = 0u;
    immutable_upload_calls_ = 0u;
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
    // A new identity: no immutable-upload token of the old segmentation matches.
    resource_identity_ = g_upload_ring_identity.fetch_add(1, std::memory_order_relaxed);
    slot_generations_.assign(frames_in_flight, 0u);
    segment_base_ = cursor_ = 0u;
    active_slot_ = 0u;
    frame_ready_ = false;
    segment_allocation_pending_ = true;
    reused_bytes_ = copied_bytes_ = 0u;
    immutable_upload_calls_ = 0u;
    peak_bytes_per_frame_ = 0u;
    return true;
}

void UploadRing::begin_frame(unsigned frame_slot) {
    if (mapped_ == nullptr || frame_slot >= frames_in_flight_) {
        throw std::runtime_error("UploadRing frame slot is out of range");
    }
    active_slot_ = frame_slot;
    segment_allocation_pending_ = true;
    reused_bytes_ = 0;
    copied_bytes_ = 0;
    immutable_upload_calls_ = 0;
    segment_base_ = static_cast<std::size_t>(frame_slot) * bytes_per_frame_;
    cursor_       = segment_base_;
    frame_ready_ = true;
}

UploadRing::Allocation UploadRing::allocate(std::size_t size, std::size_t alignment) {
    if (!frame_ready_) {
        throw std::runtime_error("UploadRing allocation before begin_frame");
    }
    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        throw std::runtime_error("UploadRing alignment is not a nonzero power of two");
    }
    // Unsigned negation gives the exact padding without cursor+alignment
    // overflow. Prove both padding and payload fit before forming addresses.
    const std::size_t padding = (std::size_t{0} - cursor_) & (alignment - 1u);
    const std::size_t remaining = bytes_per_frame_ - (cursor_ - segment_base_);
    if (padding > remaining || size > remaining - padding)
        throw std::runtime_error(
            std::string("UploadRing '") + name_ + "' exhausted: request " +
            std::to_string(size) + " B at frame-used " +
            std::to_string(cursor_ - segment_base_) + "/" +
            std::to_string(bytes_per_frame_) + " B");
    const std::size_t aligned = cursor_ + padding;
    cursor_ = aligned + size;
    // Peak within the segment, i.e. the bytes of this frame's segment that were
    // actually handed out. This is the only sound floor for shrinking
    // `bytes_per_frame_`, and it is what separates the ring's reserved size from
    // the physical memory it really backs.
    const std::size_t frame_used = cursor_ - segment_base_;
    if (frame_used > peak_bytes_per_frame_) {
        peak_bytes_per_frame_ = frame_used;
    }
    // Fence-only chunks reset the frame slot but never touch vertex bytes.
    // Count only segment uses that can actually replace their contents.
    if (size != 0 && segment_allocation_pending_) {
        ++slot_generations_[active_slot_];
        segment_allocation_pending_ = false;
    }
    return Allocation{
        mapped_ + aligned,
        gpu_base_ + static_cast<gpu::Address>(aligned),
        aligned
    };
}

UploadRing::Allocation UploadRing::upload_immutable(
    const void* source, std::size_t size, std::size_t alignment,
    ImmutableUploadToken& token) {
    const Allocation result = allocate(size, alignment);
    // Counted before any early classification, so the field counts every entry into the
    // immutable path. The two byte counters below are read against it.
    ++immutable_upload_calls_;
    if (token.slots.size() != frames_in_flight_) {
        token.slots.assign(frames_in_flight_, {});
    }
    auto& previous = token.slots[active_slot_];
    const std::uint64_t generation = slot_generations_[active_slot_];
    // Monotonic allocation prevents earlier writes in this segment from
    // overlapping this allocation. Requiring the previous segment generation
    // excludes intervening frames that might have overwritten the old bytes.
    const bool reuse = generation > 1 &&
        previous.resource_identity == resource_identity_ &&
        previous.generation == generation - 1 &&
        previous.offset == result.offset && previous.size == size;
    if (reuse) {
        reused_bytes_ += size;
    } else {
        std::memcpy(result.cpu, source, size);
        copied_bytes_ += size;
    }
    previous = {resource_identity_, generation, result.offset, size};
    return result;
}

}  // namespace galaxy::gx

#if !defined(NEBULA_GPU_VULKAN)
namespace galaxy::gx::gpu {

UploadMemory create_upload_memory(Device* device, std::size_t bytes, const char* name) {
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
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
                                               nullptr, IID_PPV_ARGS(&buffer))) ||
        FAILED(buffer->Map(0, &no_read, &mapped)) || mapped == nullptr) {
        return out;
    }
    if (name != nullptr) {
        buffer->SetName(std::wstring(name, name + std::strlen(name)).c_str());
    }
    out.cpu = static_cast<std::byte*>(mapped);
    out.gpu = buffer->GetGPUVirtualAddress();
    out.buffer = std::move(buffer);
    return out;
}

}  // namespace galaxy::gx::gpu
#endif
