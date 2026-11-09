// RendererD3D12: device and swap chain, scene targets and copies, presentation.
#include "galaxy/render/utility_shaders.h"
#include "galaxy/launch_args.h"
#include "galaxy/render/renderer_d3d12.h"
#include <nebula_frame_list.h>
#include "galaxy/render/host_window_win32.h"
#include "galaxy/crash_notes.h"
#include "galaxy/render/display_modes.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/render_api.h"
#include "galaxy/render/galaxy_select.h"
#include "barriers_d3d12.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/rt/rt_backend.h"
#include "galaxy/render/rt/rt_shaders.h"
#include "galaxy/render/rt_caps.h"
#include "galaxy/render/streamline_bridge.h"
#include "galaxy/render/xess_library.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/scope_exit.h"
#include "galaxy/game_paths.h"
#include "galaxy/frame_timeline.h"
#include "galaxy/windows_power.h"

#pragma warning(push, 0)
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <dwmapi.h>
#include <immintrin.h>
#pragma warning(pop)

#include <algorithm>
#include <cwchar>
#include <atomic>
#include <bit>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace galaxy::render {

#if defined(GALAXY_WITH_STREAMLINE)
constexpr bool kStreamlineBuilt = true;
#else
constexpr bool kStreamlineBuilt = false;
#endif

bool detail::ConversionRtvSlot::bind(
    ID3D12Device* device, ID3D12Resource* resource,
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
    if (device == nullptr || resource == nullptr || descriptor.ptr == 0u) {
        throw std::runtime_error("conversion RTV slot has no device/resource/descriptor");
    }
    if (resource_.Get() == resource && descriptor_.ptr == descriptor.ptr) {
        return false;
    }
    device->CreateRenderTargetView(resource, nullptr, descriptor);
    resource_ = resource;
    descriptor_ = descriptor;
    return true;
}

namespace {

// How long a GPU fence wait may take before the device is treated as lost.
constexpr std::uint32_t kFenceWaitTimeoutMs = 30'000u;

// Present-blit root signature; lives as long as the RendererD3D12.
static ComPtr<ID3D12RootSignature> s_blit_root_sig;
// scene target-copy conversion blit root signature.
static ComPtr<ID3D12RootSignature> s_conv_root_sig;
static ComPtr<ID3D12RootSignature> s_clear_root_sig;
#if defined(NEBULA_DEV)
static bool s_listing_live_objects = false;
#endif

#ifndef GALAXY_WITH_STREAMLINE_BRIDGE
#define GALAXY_WITH_STREAMLINE_BRIDGE 0
#endif
#if GALAXY_WITH_STREAMLINE_BRIDGE != 0 && !defined(GALAXY_STREAMLINE_BRIDGE_READY)
#error "GALAXY_WITH_STREAMLINE_BRIDGE requires the real Streamline bridge to define GALAXY_STREAMLINE_BRIDGE_READY"
#endif

// The tier as major x 10 + minor (D3D12_RAYTRACING_TIER_1_1 is 11), 0 without ray tracing. The tiers are
// enumerators, so later ones read the same way without naming them.
unsigned raytracing_tier_code(D3D12_RAYTRACING_TIER tier) {
    return tier >= D3D12_RAYTRACING_TIER_1_0 ? static_cast<unsigned>(tier) : 0u;
}

void copy_adapter_description(
    const DXGI_ADAPTER_DESC1& desc,
    std::array<char, 128>& out) {
    out.fill('\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        0,
        desc.Description,
        -1,
        out.data(),
        static_cast<int>(out.size()),
        nullptr,
        nullptr);
    if (written <= 0) {
        std::snprintf(out.data(), out.size(), "unknown");
    }
}

bool trace_present_stats_enabled() {
    return ::galaxy::diagnosing(::galaxy::kDiagnosePresentStats);
}

// Installed physical memory in MiB (0 if it cannot be queried). Logged with the
// adapter's figures: on a unified-memory adapter the graphics budget is the OS cap
// of half of system memory.
std::uint64_t host_ram_mb() {
    static const std::uint64_t megabytes = [] {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status) == FALSE) {
            return std::uint64_t{0};
        }
        return static_cast<std::uint64_t>(status.ullTotalPhys) / (1024ull * 1024ull);
    }();
    return megabytes;
}

static void d3d_check(HRESULT hr, const char* label) {
    if (FAILED(hr)) {
        std::ostringstream ss;
        ss << "[RendererD3D12] " << label << " HRESULT=0x"
           << std::hex << std::uppercase
           << std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(hr));
        throw std::runtime_error(ss.str());
    }
}

// Once-per-process device-removal triage: removal reason, DRED auto
// breadcrumbs (last executed op per command list), page-fault VA, and any
// stored debug-layer messages.
static void dump_device_removal(ID3D12Device* dev) {
    if (dev == nullptr) return;
    const HRESULT reason = dev->GetDeviceRemovedReason();
    if (reason == S_OK) return;

    static bool s_dumped = false;
    if (s_dumped) return;
    s_dumped = true;

    std::cerr << "[Renderer] DEVICE REMOVED reason=0x" << std::hex
              << std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(reason))
              << std::dec << '\n';

    ComPtr<ID3D12DeviceRemovedExtendedData> dred;
    if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&dred)))) {
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bo{};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bo))) {
            // Every list still in flight; finished ones only up to 4.
            int node_idx = 0;
            int finished = 0;
            for (const D3D12_AUTO_BREADCRUMB_NODE* n = bo.pHeadAutoBreadcrumbNode;
                 n != nullptr && node_idx < 256; n = n->pNext, ++node_idx) {
                const UINT32 last = n->pLastBreadcrumbValue
                    ? *n->pLastBreadcrumbValue : 0u;
                if (last >= n->BreadcrumbCount && ++finished > 4) {
                    continue;
                }
                char name[96] = "(unnamed)";
                if (n->pCommandListDebugNameW != nullptr) {
                    std::snprintf(name, sizeof(name), "%ls", n->pCommandListDebugNameW);
                } else if (n->pCommandListDebugNameA != nullptr) {
                    std::snprintf(name, sizeof(name), "%s", n->pCommandListDebugNameA);
                }
                std::cerr << "[DRED] node " << node_idx << " cmdlist " << name
                          << " executed " << last << "/" << n->BreadcrumbCount
                          << " ops; ops around stop:";
                // A list stopped in flight: its whole history (its markers, op 0, count its passes).
                const bool stopped = last < n->BreadcrumbCount && last != 0u;
                const UINT32 from = stopped ? 0u : (last > 6u) ? last - 6u : 0u;
                const UINT32 to = stopped ? n->BreadcrumbCount
                    : (last + 6u < n->BreadcrumbCount) ? last + 6u : n->BreadcrumbCount;
                for (UINT32 i = from; i < to; ++i) {
                    std::cerr << ' ' << static_cast<unsigned>(n->pCommandHistory[i])
                              << (i + 1u == last ? "<" : "");
                }
                std::cerr << '\n';
            }
        }
        ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 bo1{};
        if (SUCCEEDED(dred.As(&dred1)) && SUCCEEDED(dred1->GetAutoBreadcrumbsOutput1(&bo1))) {
            int node_idx = 0;
            for (const D3D12_AUTO_BREADCRUMB_NODE1* n = bo1.pHeadAutoBreadcrumbNode;
                 n != nullptr && node_idx < 256; n = n->pNext, ++node_idx) {
                const UINT32 last = n->pLastBreadcrumbValue ? *n->pLastBreadcrumbValue : 0u;
                if (last >= n->BreadcrumbCount || n->BreadcrumbContextsCount == 0u) {
                    continue;
                }
                // The markers before the stop (the last three) and the first after it.
                std::vector<std::wstring> before;
                std::wstring after;
                for (UINT c = 0; c < n->BreadcrumbContextsCount; ++c) {
                    const D3D12_DRED_BREADCRUMB_CONTEXT& context = n->pBreadcrumbContexts[c];
                    const std::wstring value = context.pContextString != nullptr ? context.pContextString : L"";
                    if (context.BreadcrumbIndex <= last) {
                        before.push_back(std::to_wstring(context.BreadcrumbIndex) + L":" + value);
                        if (before.size() > 3u) before.erase(before.begin());
                    } else if (after.empty()) {
                        after = std::to_wstring(context.BreadcrumbIndex) + L":" + value;
                    }
                }
                std::string line;
                for (const std::wstring& item : before) {
                    char buffer[160]{};
                    std::snprintf(buffer, sizeof(buffer), " [%ls]", item.c_str());
                    line += buffer;
                }
                char next[160]{};
                std::snprintf(next, sizeof(next), " next [%ls]", after.c_str());
                std::cerr << "[DRED] node " << node_idx << " stopped at op " << last << " after" << line << next << '\n';
            }
        }
        D3D12_DRED_PAGE_FAULT_OUTPUT pf{};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pf)) &&
            pf.PageFaultVA != 0) {
            std::cerr << "[DRED] page fault VA=0x" << std::hex
                      << pf.PageFaultVA << std::dec << '\n';
            const auto dump_allocations = [](
                const char* label,
                const D3D12_DRED_ALLOCATION_NODE* head) {
                int index = 0;
                for (const D3D12_DRED_ALLOCATION_NODE* node = head;
                     node != nullptr && index < 16;
                     node = node->pNext, ++index) {
                    const char* name =
                        node->ObjectNameA != nullptr
                            ? node->ObjectNameA
                            : "(unnamed)";
                    std::cerr << "[DRED] " << label << '[' << index
                              << "] type="
                              << static_cast<unsigned>(node->AllocationType)
                              << " name=" << name << '\n';
                }
                if (head == nullptr) {
                    std::cerr << "[DRED] " << label << " none\n";
                } else if (index == 16) {
                    std::cerr << "[DRED] " << label
                              << " truncated after 16 nodes\n";
                }
            };
            dump_allocations("existing", pf.pHeadExistingAllocationNode);
            dump_allocations("recently-freed", pf.pHeadRecentFreedAllocationNode);
        }
    }

    ComPtr<ID3D12InfoQueue> iq;
    if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(&iq)))) {
        // ERROR/CORRUPTION only: warnings (clear-value mismatches) would bury the illegal call.
        const UINT64 count = iq->GetNumStoredMessages();
        int printed = 0;
        for (UINT64 i = 0; i < count && printed < 20; ++i) {
            SIZE_T len = 0;
            iq->GetMessage(i, nullptr, &len);
            if (len == 0) continue;
            std::vector<char> buf(len);
            auto* msg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
            if (SUCCEEDED(iq->GetMessage(i, msg, &len)) &&
                (msg->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                 msg->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)) {
                std::cerr << "[D3D12-ERR] " << msg->pDescription << '\n';
                ++printed;
            }
        }
        if (printed == 0) {
            std::cerr << "[D3D12-ERR] no ERROR-severity messages stored ("
                      << count << " total)\n";
        }
    }
}

}  // namespace

void DescriptorRing::reset() noexcept {
    *this = DescriptorRing{};
}

namespace {
// Null SRVs at the shadow heap's end: what a table's unwritten slots are filled with.
constexpr unsigned kNullBlock = 16u;
}  // namespace

bool DescriptorRing::initialize(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
    unsigned descriptors_per_frame,
    unsigned frames_in_flight,
    unsigned persistent_descriptors,
    unsigned texture_descriptors,
    unsigned heap_limit) {
    const std::uint64_t ring = static_cast<std::uint64_t>(persistent_descriptors) +
        static_cast<std::uint64_t>(descriptors_per_frame) * frames_in_flight;
    const std::uint64_t total = ring + texture_descriptors;
    if (device == nullptr || frames_in_flight == 0u || ring == 0u || total > heap_limit) {
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = type;
    dhd.NumDescriptors = static_cast<UINT>(total);
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    // Publish a complete heap/stride/base tuple only on success. A failed
    // initialization must not pair an old heap with new cursor limits.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shadow;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> persistent_shadow;
    if (FAILED(device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&heap)))) {
        return false;
    }
    dhd.NumDescriptors = static_cast<UINT>(ring - persistent_descriptors + kNullBlock);
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&shadow)))) {
        return false;
    }
    dhd.NumDescriptors = std::max(persistent_descriptors, 1u);
    if (FAILED(device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&persistent_shadow)))) {
        return false;
    }
    const auto increment = device->GetDescriptorHandleIncrementSize(type);
    heap_ = std::move(heap);
    shadow_ = std::move(shadow);
    persistent_shadow_ = std::move(persistent_shadow);
    replaced_.clear();
    heap_limit_ = heap_limit;
    frame_slot_ = 0u;
    frame_high_water_ = 0u;
    device_ = device;
    type_ = type;
    increment_ = increment;
    texture_descriptors_ = texture_descriptors;
    persistent_descriptors_ = persistent_descriptors;
    persistent_cursor_ = 0u;
    descriptors_per_frame_ = descriptors_per_frame;
    frames_in_flight_ = frames_in_flight;
    frame_base_ = cursor_ = 0u;
    frame_ready_ = false;
    bound_list_ = nullptr;
    bound_heap_ = nullptr;
    // The null block: typed 2D null views, what any slot a pass leaves unwritten holds.
    D3D12_SHADER_RESOURCE_VIEW_DESC null_view{};
    null_view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    null_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null_view.Texture2D.MipLevels = 1u;
    for (unsigned i = 0; i < kNullBlock; ++i) {
        device_->CreateShaderResourceView(nullptr, &null_view, shadow_at(static_cast<unsigned>(ring) + i));
    }
    return true;
}

// A ring index in the CPU shadow: the persistent region's own heap, then the frame segments and the null block.
D3D12_CPU_DESCRIPTOR_HANDLE DescriptorRing::shadow_at(unsigned index) const noexcept {
    const bool persistent = index < persistent_descriptors_;
    auto cpu = (persistent ? persistent_shadow_ : shadow_)->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(persistent ? index : index - persistent_descriptors_) * increment_;
    return cpu;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorRing::persistent_cpu(std::uint32_t index) const noexcept {
    return shadow_at(index);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorRing::heap_cpu_at(unsigned index) const noexcept {
    auto cpu = heap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(texture_descriptors_ + index) * increment_;
    return cpu;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorRing::texture_cpu(std::uint32_t slot) const noexcept {
    auto cpu = heap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(slot) * increment_;
    return cpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorRing::texture_gpu_start() const noexcept {
    return heap_->GetGPUDescriptorHandleForHeapStart();
}

std::uint64_t DescriptorRing::heap_bytes() const noexcept {
    std::uint64_t bytes = heap_ ? std::uint64_t{heap_->GetDesc().NumDescriptors} * increment_ : 0u;
    for (const Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& replaced : replaced_) {
        if (replaced) {
            bytes += std::uint64_t{replaced->GetDesc().NumDescriptors} * increment_;
        }
    }
    return bytes;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorRing::gpu_at(std::uint32_t index) const noexcept {
    auto gpu = heap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<UINT64>(texture_descriptors_ + index) * increment_;
    return gpu;
}

// Slots [index, index + count) of the shadow take the null SRV.
void DescriptorRing::fill_null(unsigned index, unsigned count) {
    const unsigned null_first = ring_descriptors();
    while (count > 0u) {
        const unsigned run = std::min(count, kNullBlock);
        device_->CopyDescriptorsSimple(run, shadow_at(index), shadow_at(null_first), type_);
        index += run;
        count -= run;
    }
}

// Shadow slots [index, index + count) into the same ring slots of the heap.
void DescriptorRing::copy_in(unsigned index, unsigned count) {
    if (count > 0u) {
        device_->CopyDescriptorsSimple(count, heap_cpu_at(index), shadow_at(index), type_);
    }
}

void DescriptorRing::begin_frame(unsigned frame_slot) {
    if (!heap_ || frame_slot >= frames_in_flight_) {
        throw std::runtime_error("DescriptorRing invalid frame slot");
    }
    frame_slot_ = frame_slot;
    frame_base_ = persistent_descriptors_ + frame_slot * descriptors_per_frame_;
    cursor_     = frame_base_;
    frame_ready_ = true;
    bound_list_ = nullptr;
}

DescriptorRing::Persistent DescriptorRing::allocate_persistent(unsigned count) {
    if (!heap_) {
        throw std::runtime_error("DescriptorRing persistent descriptors before the heap");
    }
    if (count > persistent_descriptors_ - persistent_cursor_) {
        // Doubled (at least to what is needed): the persistent region never refuses a descriptor below the device's heap limit.
        const std::uint64_t needed = std::uint64_t{persistent_cursor_} + count;
        std::uint64_t grown = std::max<std::uint64_t>(std::uint64_t{persistent_descriptors_} * 2u, 16u);
        while (grown < needed) {
            grown *= 2u;
        }
        const std::uint64_t other = std::uint64_t{texture_descriptors_} + std::uint64_t{descriptors_per_frame_} * frames_in_flight_;
        const std::uint64_t room = heap_limit_ > other ? heap_limit_ - other : 0u;
        if (needed > room) {
            throw std::runtime_error("DescriptorRing persistent descriptors past the shader-visible heap limit");
        }
        grow_persistent(static_cast<unsigned>(std::min(grown, room)));
    }
    Persistent table{shadow_at(persistent_cursor_), persistent_cursor_};
    fill_null(persistent_cursor_, count);
    persistent_cursor_ += count;
    return table;
}

// A larger persistent region: a new heap (the textures, then `persistent` descriptors, then the frame segments) and a new
// persistent shadow holding the old one's descriptors. Commands already recorded read the old heap, which gets this frame's tables
// first and then goes to replaced_; the frame's tables so far (their shadow places do not move) are copied into the new heap
// at their new indices.
void DescriptorRing::grow_persistent(unsigned persistent) {
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = type_;
    dhd.NumDescriptors = texture_descriptors_ + persistent + descriptors_per_frame_ * frames_in_flight_;
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> next;
    if (FAILED(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&next)))) {
        // Nothing was replaced: the old heap stays whole (told once); the descriptor asked for cannot be given.
        static std::atomic<bool> told{false};
        if (!told.exchange(true)) {
            std::cerr << "[gpu] a descriptor heap of " << dhd.NumDescriptors << " descriptors could not be made; the heap keeps its size\n";
        }
        throw std::runtime_error("DescriptorRing persistent descriptors: the device refused a larger heap");
    }
    dhd.NumDescriptors = persistent;
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> next_shadow;
    d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&next_shadow)), "CreateDescriptorHeap persistent shadow");
    flush();  // what the replaced heap's commands read
    if (persistent_cursor_ != 0u) {
        // CPU-only to CPU-only: the persistent descriptors keep their indices.
        device_->CopyDescriptorsSimple(persistent_cursor_, next_shadow->GetCPUDescriptorHandleForHeapStart(),
                                       persistent_shadow_->GetCPUDescriptorHandleForHeapStart(), type_);
    }
    const unsigned used = frame_ready_ ? cursor_ - frame_base_ : 0u;
    replaced_.push_back(std::move(heap_));
    heap_ = std::move(next);
    persistent_shadow_ = std::move(next_shadow);
    persistent_descriptors_ = persistent;
    if (frame_ready_) {
        frame_base_ = persistent_descriptors_ + frame_slot_ * descriptors_per_frame_;
        cursor_ = frame_base_ + used;
    }
    copy_in(0u, persistent_cursor_);
    flush();
    bound_list_ = nullptr;
    bound_heap_ = nullptr;
}

void DescriptorRing::commit(const Persistent& table, unsigned count) {
    if (!heap_ || table.index + std::uint64_t{count} > persistent_cursor_) {
        throw std::runtime_error("DescriptorRing commit outside the allocated persistent descriptors");
    }
    copy_in(table.index, count);
}

// Larger frame segments: a new heap (the textures, the persistent region, then `per_frame` descriptors for each frame in flight)
// and a new shadow. Commands already recorded read the old heap, which gets this frame's tables first and then goes to
// replaced_; the tables of this frame so far move to the same places in their new segment, and the persistent descriptors keep
// their indices. A transient table's handle belongs to the heap that was current when it was allocated, as with grow_persistent.
void DescriptorRing::grow_frames(unsigned per_frame) {
    const std::uint64_t total = std::uint64_t{texture_descriptors_} + persistent_descriptors_ + std::uint64_t{per_frame} * frames_in_flight_;
    if (total > heap_limit_) {
        throw std::runtime_error("DescriptorRing frame segment past the shader-visible heap limit");
    }
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = type_;
    dhd.NumDescriptors = static_cast<UINT>(total);
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> next;
    if (FAILED(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&next)))) {
        static std::atomic<bool> told{false};
        if (!told.exchange(true)) {
            std::cerr << "[gpu] a descriptor heap of " << total << " descriptors could not be made; the frame segment keeps its size\n";
        }
        throw std::runtime_error("DescriptorRing frame segment: the device refused a larger heap");
    }
    dhd.NumDescriptors = per_frame * frames_in_flight_ + kNullBlock;
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> next_shadow;
    d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&next_shadow)), "CreateDescriptorHeap frame shadow");
    // The null block, after the segments.
    D3D12_SHADER_RESOURCE_VIEW_DESC null_view{};
    null_view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    null_view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null_view.Texture2D.MipLevels = 1u;
    const D3D12_CPU_DESCRIPTOR_HANDLE shadow_start = next_shadow->GetCPUDescriptorHandleForHeapStart();
    for (unsigned i = 0; i < kNullBlock; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE at = shadow_start;
        at.ptr += static_cast<SIZE_T>(per_frame * frames_in_flight_ + i) * increment_;
        device_->CreateShaderResourceView(nullptr, &null_view, at);
    }
    const unsigned used = frame_ready_ ? cursor_ - frame_base_ : 0u;
    flush();  // what the replaced heap's commands read
    if (used != 0u) {
        D3D12_CPU_DESCRIPTOR_HANDLE to = shadow_start;
        to.ptr += static_cast<SIZE_T>(frame_slot_) * per_frame * increment_;
        device_->CopyDescriptorsSimple(used, to, shadow_at(frame_base_), type_);
    }
    replaced_.push_back(std::move(heap_));
    heap_ = std::move(next);
    shadow_ = std::move(next_shadow);
    descriptors_per_frame_ = per_frame;
    if (frame_ready_) {
        frame_base_ = persistent_descriptors_ + frame_slot_ * descriptors_per_frame_;
        cursor_ = frame_base_ + used;
    }
    copy_in(0u, persistent_cursor_);
    flush();
    bound_list_ = nullptr;
    bound_heap_ = nullptr;
    if (replaced_fn_ != nullptr) {
        replaced_fn_(replaced_owner_);
    }
}

DescriptorRing::Table DescriptorRing::allocate(unsigned count) {
    if (!frame_ready_) {
        throw std::runtime_error("DescriptorRing no frame begun");
    }
    if (count > descriptors_per_frame_ - (cursor_ - frame_base_)) {
        // Doubled (at least to what is needed): a frame's tables never refuse a descriptor below the device's heap limit.
        const std::uint64_t needed = std::uint64_t{cursor_ - frame_base_} + count;
        std::uint64_t grown = std::max<std::uint64_t>(std::uint64_t{descriptors_per_frame_} * 2u, 256u);
        while (grown < needed) {
            grown *= 2u;
        }
        const std::uint64_t other = std::uint64_t{texture_descriptors_} + persistent_descriptors_;
        const std::uint64_t room = heap_limit_ > other ? (heap_limit_ - other) / frames_in_flight_ : 0u;
        if (needed > room) {
            throw std::runtime_error("DescriptorRing frame segment exhausted: past the shader-visible heap limit");
        }
        grow_frames(static_cast<unsigned>(std::min(grown, room)));
    }
    frame_high_water_ = std::max(frame_high_water_, cursor_ - frame_base_ + count);
    Table table{shadow_at(cursor_), heap_->GetGPUDescriptorHandleForHeapStart()};
    table.gpu.ptr += static_cast<UINT64>(texture_descriptors_ + cursor_) * increment_;
    fill_null(cursor_, count);
    cursor_ += count;
    return table;
}

void DescriptorRing::flush() {
    if (frame_ready_) {
        copy_in(frame_base_, cursor_ - frame_base_);
    }
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> DescriptorRing::resize_textures(unsigned texture_descriptors) {
    if (!heap_ || texture_descriptors <= texture_descriptors_) {
        return {};
    }
    const std::uint64_t total = std::uint64_t{texture_descriptors} + ring_descriptors();
    if (total > heap_limit_) {
        throw std::runtime_error("DescriptorRing texture region past the shader-visible heap limit");
    }
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = type_;
    dhd.NumDescriptors = static_cast<UINT>(total);
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> next;
    if (FAILED(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&next)))) {
        // The device refuses a larger heap: the old one stays, and the texture region keeps its size (told once).
        static std::atomic<bool> told{false};
        if (!told.exchange(true)) {
            std::cerr << "[gpu] a descriptor heap of " << total << " descriptors could not be made; the heap keeps its size\n";
        }
        return {};
    }
    // Commands already recorded read the replaced heap: it gets this frame's tables first.
    flush();
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> old = std::move(heap_);
    heap_ = std::move(next);
    texture_descriptors_ = texture_descriptors;
    copy_in(0u, persistent_cursor_);
    flush();
    bound_list_ = nullptr;
    bound_heap_ = nullptr;
    return old;
}

void DescriptorRing::bind(ID3D12GraphicsCommandList* list) {
    if (list == bound_list_ && heap_.Get() == bound_heap_) {
        return;
    }
    ID3D12DescriptorHeap* heaps[] = {heap_.Get(), sampler_heap_};
    list->SetDescriptorHeaps(sampler_heap_ != nullptr ? 2u : 1u, heaps);
    bound_list_ = list;
    bound_heap_ = heap_.Get();
}

// Upload rings are committed UPLOAD-heap resources mapped for the life of the process. Their whole size is in the
// working set from the start (memws.py: every page of them resident on NVIDIA's driver), so they start at what the
// game needs and grow on demand; [gpu-rings] logs the sizes and UploadRing tracks each ring's peak use. On an integrated adapter the memory is system memory;
// DXGI's DedicatedVideoMemory there is a fixed compatibility value, not VRAM (Intel support article
// 000020962: 128 MB), and the graphics share is capped at half of installed RAM. Exhaustion is
// fatal: UploadRing::allocate throws rather than dropping geometry.
namespace {

// The session's adapter, for unified-memory detection. Written once in initialize(),
// before any ring is created and before the render thread starts.
DXGI_ADAPTER_DESC1 g_selected_adapter_desc{};
bool g_selected_adapter_valid = false;

// Whether graphics memory is system memory (integrated graphics), judged by the memory
// figures rather than the vendor: at most 256 MiB dedicated (the compatibility value on
// such parts) with at least 8x that shared, so a low-end discrete part with a small real
// pool is not classed as unified.
[[nodiscard]] bool adapter_is_unified_memory() {
    if (!g_selected_adapter_valid) {
        return false;
    }
    const std::uint64_t dedicated = g_selected_adapter_desc.DedicatedVideoMemory;
    const std::uint64_t shared = g_selected_adapter_desc.SharedSystemMemory;
    constexpr std::uint64_t kNegligibleDedicatedBytes = 256ull * 1024ull * 1024ull;
    return dedicated <= kNegligibleDedicatedBytes &&
        shared >= dedicated * 8ull;
}

}  // namespace

// The upload rings' first buffers. A segmentation gives each frame an equal share and keeps one more share as
// spill, so a frame may take twice its budget; a ring whose frames take over half of it grows at the next frame
// boundary. The most a frame took over the 102-stop tour (mem_f2): vertex 2.2 MB, index 1.2, constant 1.4, matrix 0.7.
// A frame's segment of the descriptor ring: its tables are the renderer's own passes' (a few hundred descriptors at most, by
// design, not by content: draws read the texture region). It starts at this size and doubles if a frame ever fills it
// (DescriptorRing::grow_frames); frame_high_water() reports use.
static constexpr unsigned    kSrvPerFrame = 1024u;  // the first size of a frame's segment; it doubles when a frame fills it
// The descriptor ring's persistent descriptors, outside the rewound frame segments: the scene target conversion sources
// (the scene target's views, the output scene target's, the MSAA depth's, then the wide scene's).
static constexpr unsigned    kPersistentSrvDescriptors = 4u;  // the first size: the region doubles when it is full
// The texture region of the shader-visible heap (the draws' `Texture2D gTextures[]`): every texture view at its
// TextureStore index, the last slot the null view. It starts with at least kTextureSlotsFirst slots (twice the views made
// so far when that is more) and the heap is replaced by one with twice as many when a view index reaches the null slot, up
// to the device's heap limit less the ring's part (one million descriptors on resource binding tiers 1 and 2; tier 3 names none,
// and a heap the device refuses is told once and the region keeps its size).
static constexpr unsigned    kTextureSlotsFirst = 64u;
// The texture region's room: what the device's heap limit leaves beside the descriptor ring's part.
static unsigned texture_slots_room(const DescriptorRing& ring) noexcept { return ring.heap_limit() - ring.ring_descriptors(); }
// The texture region's room is what the device's heap limit (srv_ring_.heap_limit(): tier 1 and 2 one million, tier 3 more) leaves
// beside the ring's part: RendererD3D12::texture_slots_max().
// Sampler slots a frame may add: the heap starts again between frames when fewer than this are free.
static constexpr unsigned    kSamplerFrameRoom = 512u;

void RendererD3D12::trace_rings() const {
    const auto line = [](const char* name, const UploadRing& ring) {
        std::fprintf(stderr, "[memory] ring %s %.1f MB, a frame took at most %.1f MB\n", name, static_cast<double>(ring.capacity()) / (1024.0 * 1024.0),
                     static_cast<double>(ring.high_water()) / (1024.0 * 1024.0));
    };
    line("vertex", vertex_ring_);
    line("index", index_ring_);
    line("constant", constant_ring_);
    line("matrix", matrix_ring_);
}

void RendererD3D12::wait_for_gpu() {
    if (device_gone_) {
        return;
    }
    const std::uint64_t val = ++next_fence_value_;
    const HRESULT signal_hr = queue_->Signal(fence_.Get(), val);
    if (FAILED(signal_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(signal_hr, "Queue Signal");
    }
    wait_for_fence_value(val);
}

bool RendererD3D12::create_scene_targets(const FrameMap& map) {
    // The physical targets scale; game scene target/display picture addresses and dimensions keep the
    // frame's size .
    //
    // Both resources are created before anything is replaced: a failure
    // (out of video memory) leaves the current targets untouched. On success
    // the previous targets go with the GPU drained.
    const std::uint64_t width = map.width;
    const std::uint64_t height = map.height;
    if (width == 0u || height == 0u || width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        width > static_cast<std::uint64_t>(
            std::numeric_limits<LONG>::max()) ||
        height > static_cast<std::uint64_t>(
            std::numeric_limits<LONG>::max())) {
        std::cerr << "[Renderer] invalid physical scene target extent\n";
        return false;
    }
    const UINT w = static_cast<UINT>(width);
    const UINT scene_h = static_cast<UINT>(height);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Resource> color;
    {
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width              = w;
        rd.Height             = scene_h;
        rd.DepthOrArraySize   = 1;
        rd.MipLevels          = 1;
        rd.Format             = kSceneColorFormat;
        rd.SampleDesc.Count   = 1;
        rd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv{};
        cv.Format   = kSceneColorFormat;
        cv.Color[3] = 1.0f;
        const HRESULT hr = create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd,
            gpu::Use::ColorWrite, &cv, IID_PPV_ARGS(&color));
        if (FAILED(hr)) {
            std::cerr << "[Renderer] scene target color " << w << 'x' << scene_h
                      << " allocation failed hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec << '\n';
            return false;
        }
    }
    ComPtr<ID3D12Resource> depth;
    {
        D3D12_RESOURCE_DESC drd{};
        drd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        drd.Width              = w;
        drd.Height             = scene_h;
        drd.DepthOrArraySize   = 1;
        drd.MipLevels          = 1;
        drd.Format             = kSceneDepthResourceFormat;
        drd.SampleDesc.Count   = 1;
        drd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE dcv{};
        dcv.Format               = kSceneDepthFormat;
        dcv.DepthStencil.Depth   = 1.0f;
        const HRESULT hr = create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &drd,
            gpu::Use::DepthWrite, &dcv, IID_PPV_ARGS(&depth));
        if (FAILED(hr)) {
            std::cerr << "[Renderer] scene target depth " << w << 'x' << scene_h
                      << " allocation failed hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec << '\n';
            return false;
        }
    }
    if (!dsv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1;
        dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if (FAILED(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&dsv_heap_)))) {
            return false;
        }
    }

    // Commit. Callers have drained the GPU, so the descriptor slots below can
    // be rewritten (OMSetRenderTargets copied the old views when recorded).
    scene_color_ = std::move(color);
    scene_depth_ = std::move(depth);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kSceneDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(
        scene_depth_.Get(), &dsv, dsv_heap_->GetCPUDescriptorHandleForHeapStart());

    // scene target RTV at slot kSwapChainBuffers of the backbuffer RTV heap.
    {
        const UINT stride = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE h =
            rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(kSwapChainBuffers) * stride;
        device_->CreateRenderTargetView(scene_color_.Get(), nullptr, h);
        scene_rtv_ = h;
    }
    scene_dsv_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    // MSAA: draws and clears go to the multisampled targets; scene_color_ and scene_depth_ hold their resolves.
    if (!create_scene_msaa(scene_msaa_, scene_color_.Get(), scene_depth_.Get(), wanted_msaa_samples(), scene_rtv_, scene_dsv_)) {
        std::cerr << "[Renderer] MSAA targets unavailable; the scene target draws without MSAA\n";
        scene_msaa_.samples = 1u;
        const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        scene_rtv_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        scene_rtv_.ptr += static_cast<SIZE_T>(kSwapChainBuffers) * stride;
    } else if (scene_msaa_.samples > 1u) {
        std::cout << "[Renderer] scene target MSAA " << scene_msaa_.samples << "x\n";
    }
    return true;
}

unsigned RendererD3D12::wanted_msaa_samples() const {
    const unsigned asked = std::min(get_render_config().msaa_samples, 8u);
    if (asked <= 1u || !device_ || temporal_mode()) {
        return 1u;
    }
    return std::min(std::bit_floor(asked), msaa_max_samples());
}

unsigned RendererD3D12::msaa_max_samples() const {
    if (msaa_max_samples_ != 0u || !device_) {
        return std::max(msaa_max_samples_, 1u);
    }
    msaa_max_samples_ = 1u;
    for (unsigned samples = 8u; samples > 1u; samples /= 2u) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS color{kSceneColorFormat, samples};
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS depth{kSceneDepthFormat, samples};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &color, sizeof(color))) &&
            SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &depth, sizeof(depth))) &&
            color.NumQualityLevels > 0u && depth.NumQualityLevels > 0u) {
            msaa_max_samples_ = samples;
            break;
        }
    }
    return msaa_max_samples_;
}

bool RendererD3D12::create_scene_msaa(SceneMsaa& msaa, ID3D12Resource* color, ID3D12Resource* depth, unsigned samples,
                                    D3D12_CPU_DESCRIPTOR_HANDLE& rtv, D3D12_CPU_DESCRIPTOR_HANDLE& dsv) {
    msaa.color.Reset();
    msaa.depth.Reset();
    msaa.samples = 1u;
    msaa.color_dirty = msaa.depth_dirty = false;
    if (samples <= 1u) {
        return true;
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    const DXGI_FORMAT color_format = color->GetDesc().Format;
    rd.Width = depth->GetDesc().Width;
    rd.Height = depth->GetDesc().Height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = color_format;
    rd.SampleDesc.Count = samples;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = color_format;
    cv.Color[3] = 1.0f;
    if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ColorWrite,
                                                &cv, IID_PPV_ARGS(&msaa.color)))) {
        return false;
    }
    rd.Format = kSceneDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kSceneDepthFormat;
    dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::DepthWrite,
                                                &dcv, IID_PPV_ARGS(&msaa.depth)))) {
        msaa.color.Reset();
        return false;
    }
    if (!msaa.rtv_heap) {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.NumDescriptors = 1;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&msaa.rtv_heap)), "MSAA RTV heap");
        heap.NumDescriptors = 2;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        d3d_check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&msaa.dsv_heap)), "MSAA DSV heap");
    }
    rtv = msaa.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC color_view{};
    color_view.Format = color_format;
    color_view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    device_->CreateRenderTargetView(msaa.color.Get(), &color_view, rtv);
    const UINT dsv_stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    dsv = msaa.dsv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC depth_view{};
    depth_view.Format = kSceneDepthFormat;
    depth_view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
    device_->CreateDepthStencilView(msaa.depth.Get(), &depth_view, dsv);
    // The single-sample depth, the depth resolve's target.
    D3D12_CPU_DESCRIPTOR_HANDLE resolve_dsv = dsv;
    resolve_dsv.ptr += dsv_stride;
    depth_view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(depth, &depth_view, resolve_dsv);
    msaa.samples = samples;
    if (msaa.depth_srv) {
        write_msaa_depth_view(msaa);
    }
    return true;
}

// The multisampled depth's view for the depth resolve (made at the first resolve: the scene target's first targets come before
// the descriptor ring).
void RendererD3D12::write_msaa_depth_view(SceneMsaa& msaa) {
    if (!msaa.depth_srv) {
        msaa.depth_srv = allocate_persistent_descriptors(1u);
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = kSceneDepthSrvFormat;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device_->CreateShaderResourceView(msaa.depth.Get(), &view, srv_ring_.persistent_cpu(msaa.depth_srv->index));
    srv_ring_.commit(*msaa.depth_srv, 1u);
}

namespace {

}  // namespace

void RendererD3D12::resolve_scene(bool depth, bool color_wanted) {
    SceneMsaa& msaa = scene_msaa_;
    const bool color = color_wanted && msaa.color_dirty;
    depth = depth && msaa.depth_dirty;
    if (msaa.samples <= 1u || (!color && !depth)) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Resolve};
    record_scene_resolve(command_list_.Get(), color, depth);
    msaa.color_dirty = msaa.color_dirty && !color;
    if (depth) {
        // The scene target's own targets and the state again, as the next draw expects them.
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        command_list_->OMSetRenderTargets(1, &scene_rtv_, FALSE, &scene_dsv_);
        bound_rt_aux_ = 0u;
        invalidate_draw_bindings();
        msaa.depth_dirty = false;
    }
}

void RendererD3D12::resolve_scene_on(ID3D12GraphicsCommandList* list, bool color, bool depth) {
    if (list == command_list_.Get()) {
        resolve_scene(depth);
        return;
    }
    color = color && scene_msaa_.color_dirty;
    depth = depth && scene_msaa_.depth_dirty;
    if (color || depth) {
        record_scene_resolve(list, color, depth);
    }
}

void RendererD3D12::record_scene_resolve(ID3D12GraphicsCommandList* list, bool color, bool depth) {
    SceneMsaa& msaa = scene_msaa_;
    if (msaa.samples <= 1u) {
        return;
    }
    if (color) {
        BarriersD3D12 uses{list};
        uses.texture(msaa.color.Get(), gpu::Use::ColorWrite, gpu::Use::ResolveSource);
        uses.texture(scene_color_.Get(), gpu::Use::ColorWrite, gpu::Use::ResolveDest);
        uses.issue();
        list->ResolveSubresource(scene_color_.Get(), 0u, msaa.color.Get(), 0u, scene_color_->GetDesc().Format);
        uses.undo();
    }
    if (depth) {
        if (!depth_resolve_pso_) {
            const UtilityShaderBytes vs = utility_shader(UtilityShader::BlitVs);
            const UtilityShaderBytes ps = utility_shader(UtilityShader::DepthResolvePs);
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = s_blit_root_sig.Get();
            pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
            pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
            pd.SampleMask = UINT_MAX;
            pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pd.DepthStencilState.DepthEnable = TRUE;
            pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
            pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pd.NumRenderTargets = 0;
            pd.DSVFormat = kSceneDepthFormat;
            pd.SampleDesc.Count = 1;
            d3d_check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&depth_resolve_pso_)),
                      "depth resolve PSO");
        }
        if (!msaa.depth_srv) {
            write_msaa_depth_view(msaa);
        }
        BarriersD3D12 uses{list};
        uses.texture(msaa.depth.Get(), gpu::Use::DepthWrite, gpu::Use::PixelRead);
        uses.issue();
        D3D12_CPU_DESCRIPTOR_HANDLE resolve_dsv = msaa.dsv_heap->GetCPUDescriptorHandleForHeapStart();
        resolve_dsv.ptr += device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(frame_.width),
                                      static_cast<float>(frame_.height), 0.0f, 1.0f};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(frame_.width),
                                 static_cast<LONG>(frame_.height)};
        list->SetGraphicsRootSignature(s_blit_root_sig.Get());
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        list->OMSetRenderTargets(0, nullptr, FALSE, &resolve_dsv);
        list->SetPipelineState(depth_resolve_pso_.Get());
        srv_ring_.bind(list);
        list->SetGraphicsRootDescriptorTable(0, srv_ring_.gpu_at(msaa.depth_srv->index));
        list->DrawInstanced(3, 1, 0, 0);
        uses.undo();
    }
}

HRESULT create_draw_root_signature(
    ID3D12Device* device,
    ID3D12RootSignature** root_signature) noexcept {
    if (device == nullptr || root_signature == nullptr) {
        return E_INVALIDARG;
    }
    *root_signature = nullptr;

    // b0 view, b1 vertex material, b2 pixel material, b3 the draw's words; every texture (t0 space1) and sampler (s0
    // space1) as unbounded arrays from the heaps' starts (resource binding tier 2); t16 rows, t17 instances, t18 the
    // frame's texture lists.
    D3D12_DESCRIPTOR_RANGE tex_range{};
    tex_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_range.NumDescriptors = UINT_MAX;
    tex_range.RegisterSpace = 1u;
    D3D12_DESCRIPTOR_RANGE samp_range{};
    samp_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samp_range.NumDescriptors = UINT_MAX;
    samp_range.RegisterSpace = 1u;

    D3D12_ROOT_PARAMETER params[9]{};
    for (UINT i = 0; i < 3u; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[i].Descriptor.ShaderRegister = i;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[3].Constants.ShaderRegister = 3;
    params[3].Constants.Num32BitValues = gpu::kDrawRootWords;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[4].DescriptorTable.NumDescriptorRanges = 1;
    params[4].DescriptorTable.pDescriptorRanges = &tex_range;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[5].DescriptorTable.NumDescriptorRanges = 1;
    params[5].DescriptorTable.pDescriptorRanges = &samp_range;
    params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    for (UINT i = 0; i < 2u; ++i) {
        params[6u + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[6u + i].Descriptor.ShaderRegister = 16u + i;
        params[6u + i].ShaderVisibility = i == 0u ? D3D12_SHADER_VISIBILITY_ALL : D3D12_SHADER_VISIBILITY_VERTEX;
    }
    params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[8].Descriptor.ShaderRegister = 18u;
    params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 9;
    rsd.pParameters = params;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    const HRESULT serialize_hr = D3D12SerializeRootSignature(
        &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(serialize_hr)) {
        return serialize_hr;
    }
    return device->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(root_signature));
}

bool RendererD3D12::create_root_signature() {
    d3d_check(
        create_draw_root_signature(
            device_.Get(), root_signature_.ReleaseAndGetAddressOf()),
        "Create the root signature");
    return true;
}

// Each frame slot owns a sampler heap. Slots 0-7 hold its default table;
// dynamic tables are reclaimed only after that slot's submission fence.

D3D12_SAMPLER_DESC build_sampler_desc_from_key(
    const SamplerKey& key,
    const RenderConfig& cfg) noexcept {
    auto to_d3d_wrap = [](std::uint32_t wrap) -> D3D12_TEXTURE_ADDRESS_MODE {
        switch (wrap) {
        case 0:  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case 1:  return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        case 2:  return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        }
    };

    const float cfg_bias =
        static_cast<float>(std::clamp(cfg.texture_lod_bias, 0u, 8u)) * 0.5f;
    const unsigned anisotropy =
        std::clamp(cfg.anisotropic_filtering, 1u, 16u);
    const std::uint32_t wrap_s = key.filter & 3u;
    const std::uint32_t wrap_t = (key.filter >> 2u) & 3u;
    const bool mag_linear = (key.filter & 16u) != 0u;
    const std::uint32_t minf = (key.filter >> 5u) & 7u;
    const float lod_bias = key.lod_bias;
    const float min_lod = key.min_lod, max_lod = key.max_lod;

    // The minified filter (texture_sampler.h min_filter_code): 4 linear, plus 1 the nearest level or 2 between levels.
    const bool min_linear = (minf >= 4u);
    // 0 none, 1 point, 2 linear, 3 linear without anisotropy (make_sampler_key's arbitrary levels).
    const bool no_anisotropy = (minf & 0x3u) == 3u;
    const unsigned mip_mode = no_anisotropy ? 2u : minf & 0x3u;
    const D3D12_FILTER_TYPE ft_min = min_linear
        ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    const D3D12_FILTER_TYPE ft_mag = mag_linear
        ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;
    const D3D12_FILTER_TYPE ft_mip = (mip_mode == 2u)
        ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT;

    D3D12_SAMPLER_DESC desc{};
    const bool use_anisotropic =
        anisotropy > 1u && mip_mode != 0u && min_linear && mag_linear && !no_anisotropy;
    desc.Filter = use_anisotropic
        ? D3D12_FILTER_ANISOTROPIC
        : D3D12_ENCODE_BASIC_FILTER(
              ft_min, ft_mag, ft_mip, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
    desc.AddressU = to_d3d_wrap(wrap_s);
    desc.AddressV = to_d3d_wrap(wrap_t);
    desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.MipLODBias = std::clamp(lod_bias + cfg_bias, D3D12_MIP_LOD_BIAS_MIN, D3D12_MIP_LOD_BIAS_MAX);
    desc.MaxAnisotropy = use_anisotropic ? anisotropy : 1u;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    if (mip_mode == 0u) {
        desc.MinLOD = 0.0f;
        desc.MaxLOD = 0.0f;
    } else {
        desc.MinLOD = min_lod;
        desc.MaxLOD = (max_lod > min_lod) ? max_lod : min_lod;
    }
    return desc;
}

bool RendererD3D12::create_sampler_heap() {
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    dhd.NumDescriptors = kSamplerHeapSlots;
    dhd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&sampler_heap_)), "CreateDescriptorHeap Sampler");
    sampler_config_ = ~0u;
    fill_sampler_heap(get_render_config());
    return true;
}

// Every slot holds the default sampler, so a slot no draw named reads it and the heap is defined from its creation.
void RendererD3D12::fill_sampler_heap(const RenderConfig& cfg) {
    const unsigned anisotropy = std::clamp(cfg.anisotropic_filtering, 1u, 16u);
    D3D12_SAMPLER_DESC sd{};
    sd.Filter = anisotropy > 1u ? D3D12_FILTER_ANISOTROPIC : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = sd.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sd.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sd.MipLODBias = static_cast<float>(std::clamp(cfg.texture_lod_bias, 0u, 8u)) * 0.5f;
    sd.MaxAnisotropy = anisotropy;
    sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sd.MaxLOD = D3D12_FLOAT32_MAX;
    const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_CPU_DESCRIPTOR_HANDLE h = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
    for (unsigned i = 0; i < kSamplerHeapSlots; ++i, h.ptr += stride) {
        device_->CreateSampler(&sd, h);
    }
}

// Between frames, with nothing recorded: the sampler heap starts again when the settings change or fewer than
// kSamplerFrameRoom slots are free, every slot holding the default sampler, after the GPU is past the frames that read it.
void RendererD3D12::reset_sampler_frame() {
    const RenderConfig cfg = get_render_config();
    const std::uint32_t config_key = sampler_config_key(cfg);
    const bool full = sampler_slots_.used() + kSamplerFrameRoom > kSamplerHeapSlots;
    if (config_key == sampler_config_ && !full) {
        return;
    }
    if (sampler_config_ != ~0u) {
        wait_for_gpu();
    }
    sampler_config_ = config_key;
    sampler_slots_.reset();
    ++sampler_generation_;
    fill_sampler_heap(cfg);
}

void RendererD3D12::bind_draw_textures(gpu::Address lists) {
    if (mirrored_backend_ == nullptr) {
        throw std::runtime_error("[textures] a draw before the texture heap was made");
    }
    // One heap for the whole list: this binds it only on a list that has not got it (or after the heap was replaced).
    srv_ring_.bind(command_list_.Get());
    encoder_.set_texture_arrays(srv_ring_.texture_gpu_start(), sampler_heap_->GetGPUDescriptorHandleForHeapStart(), lists);
}

// The capture pipelines (root signature: SceneConversionShaderConstants, one SRV table, a static linear clamp sampler)
// and the screen probes' compute pipeline (constants, the depth's SRV table, the pixels' SRV, the values' UAV).
bool RendererD3D12::create_conversion_pipeline() {
    {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors     = 1;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = 0;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.RegisterSpace  = 0;
        params[0].Constants.Num32BitValues = kSceneConversionConstantCount;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC ss{};
        ss.Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss.ShaderRegister   = 0;
        ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters     = 2;
        rsd.pParameters       = params;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers   = &ss;

        ComPtr<ID3DBlob> blob, err;
        d3d_check(D3D12SerializeRootSignature(
            &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err),
            "SerializeConvRS");
        d3d_check(device_->CreateRootSignature(
            0, blob->GetBufferPointer(), blob->GetBufferSize(),
            IID_PPV_ARGS(&s_conv_root_sig)),
            "CreateConvRS");
    }

    // PSO (fullscreen triangle, no depth, RGBA8 target).
    {
        const UtilityShaderBytes vs = utility_shader(UtilityShader::BlitVs);
        const UtilityShaderBytes ps = utility_shader(UtilityShader::SceneConversionPs);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = s_conv_root_sig.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.DepthStencilState.DepthEnable   = FALSE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets      = 1;
        pd.RTVFormats[0]         = kSceneColorFormat;
        pd.SampleDesc.Count      = 1;
        d3d_check(device_->CreateGraphicsPipelineState(
            &pd, IID_PPV_ARGS(&conv_pipeline_)), "CreateConvPSO");
        const UtilityShaderBytes filter = utility_shader(UtilityShader::DisplayFilterPs);
        pd.PS = {filter->GetBufferPointer(), filter->GetBufferSize()};
        pd.RTVFormats[0] = kSceneColorFormat;
        d3d_check(device_->CreateGraphicsPipelineState(
            &pd, IID_PPV_ARGS(&display_filter_pipeline_)), "CreateDisplayFilterPSO");
    }

    // Screen probes: b0 the count, t0 the depth (a table), t1 the pixels and u0 the values (root views).
    {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER params[4]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = 1;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &range;
        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[2].Descriptor.ShaderRegister = 1;
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[3].Descriptor.ShaderRegister = 0;
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 4;
        rsd.pParameters = params;
        ComPtr<ID3DBlob> blob, err;
        d3d_check(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err), "SerializeProbeRS");
        d3d_check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                               IID_PPV_ARGS(&probe_root_signature_)),
                  "CreateProbeRS");
        const UtilityShaderBytes cs = utility_shader(UtilityShader::ScreenProbeCs);
        D3D12_COMPUTE_PIPELINE_STATE_DESC cd{};
        cd.pRootSignature = probe_root_signature_.Get();
        cd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
        d3d_check(device_->CreateComputePipelineState(&cd, IID_PPV_ARGS(&probe_pipeline_)), "CreateProbePSO");
    }

    // CPU-only RTV slot reused after each conversion bind is recorded.
    {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1u;
        dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(
            &dhd, IID_PPV_ARGS(&conv_rtv_heap_)), "ConvRTV heap");
        conv_scratch_rtv_ =
            conv_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        conv_scratch_rtv_slot_.reset();
    }

    refresh_scene_conversion_source_views();
    return true;
}

void RendererD3D12::refresh_scene_conversion_source_views() {
    if (!scene_color_ || !scene_depth_) {
        throw std::runtime_error("scene target conversion source resource unavailable");
    }
    if (!conv_scene_source_table_) {
        conv_scene_source_table_ = allocate_persistent_descriptors(2u);
    }
    write_scene_conversion_views(*conv_scene_source_table_);
}

void RendererD3D12::write_scene_conversion_views(const DescriptorRing::Persistent& table) {
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = scene_color_->GetDesc().Format;
    const D3D12_CPU_DESCRIPTOR_HANDLE color_cpu = srv_ring_.persistent_cpu(table.index);
    device_->CreateShaderResourceView(scene_color_.Get(), &view, color_cpu);
    auto depth_cpu = color_cpu;
    depth_cpu.ptr += stride;
    view.Format = kSceneDepthSrvFormat;
    device_->CreateShaderResourceView(scene_depth_.Get(), &view, depth_cpu);
    srv_ring_.commit(table, 2u);
    conv_scene_color_index_ = table.index;
    conv_scene_depth_index_ = table.index + 1u;
}

bool RendererD3D12::can_take_scene() const noexcept {
    return scene_color_ && !frame_generation_running() && !output_scene_active_ && !target_pass_active_ && scene_msaa_.samples <= 1u &&
           !scene_hdr_;
}

bool RendererD3D12::take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureStore& textures, gpu::TextureRef& released) {
    if (const auto it = pictures_.find(key); it != pictures_.end()) {
        released = it->second.viewed;
    }
    gpu::TextureRef picture = swap_scene_color(std::move(fresh));
    const FrameExtent size = frame_pass_extent(frame_);
    return publish_picture(key, size.width, size.height, std::move(picture), 0u, textures);
}

void RendererD3D12::open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key,
                                        TextureStore& textures) {
    resolve_scene(depth_key != 0u);
    BarriersD3D12 uses{command_list_.Get()};
    if (color_key != 0u) {
        uses.texture(scene_color_.Get(), gpu::Use::ColorWrite, gpu::Use::PixelRead);
    }
    if (depth_key != 0u) {
        uses.texture(scene_depth_.Get(), gpu::Use::DepthWrite, gpu::Use::PixelRead);
    }
    uses.issue();
    const FrameExtent size = frame_pass_extent(frame_);
    if (color_key != 0u) {
        (void)publish_picture(color_key, size.width, size.height, scene_color_, channels, textures);
    }
    if (depth_key != 0u) {
        (void)publish_picture(depth_key, size.width, size.height, scene_depth_, 0u, textures);
    }
    frame_pass_color_key_ = color_key;
    frame_pass_depth_key_ = depth_key;
}

void RendererD3D12::close_frame_for_pass(TextureStore& textures) {
    BarriersD3D12 uses{command_list_.Get()};
    for (const auto& [resource, use, key] :
         {std::tuple{scene_color_.Get(), gpu::Use::ColorWrite, frame_pass_color_key_},
          std::tuple{scene_depth_.Get(), gpu::Use::DepthWrite, frame_pass_depth_key_}}) {
        if (key == 0u) {
            continue;
        }
        uses.texture(resource, gpu::Use::PixelRead, use);
        if (const auto it = pictures_.find(key); it != pictures_.end()) {
            textures.retire_view(it->second.view, std::move(it->second.viewed));
            pictures_.erase(it);
        }
    }
    uses.issue();
    frame_pass_color_key_ = 0u;
    frame_pass_depth_key_ = 0u;
    invalidate_draw_bindings();
}

gpu::TextureRef RendererD3D12::take_frame_picture(gpu::TextureRef fresh) {
    if (scene_hdr_) {
        end_wide_scene();
    }
    resolve_scene(false);
    return swap_scene_color(std::move(fresh));
}

gpu::TextureRef RendererD3D12::swap_scene_color(gpu::TextureRef fresh) {
    const GpuPassScope pass{*this, GpuPass::DisplayCopy};
    const D3D12_RESOURCE_DESC desc = scene_color_->GetDesc();
    BarriersD3D12 uses{command_list_.Get()};
    std::erase_if(old_scene_targets_, [this](const OldSceneTarget& old) { return frame_index_ >= old.free_from; });
    if (fresh) {
        const D3D12_RESOURCE_DESC got = fresh->GetDesc();
        if (got.Width != desc.Width || got.Height != desc.Height || got.Format != desc.Format ||
            got.Flags != desc.Flags || got.SampleDesc.Count != 1u) {
            old_scene_targets_.push_back({std::move(fresh), frame_index_ + kFramesInFlight + 1u});
            fresh = nullptr;
        }
    }
    if (fresh) {
        // Released pictures are kept as shader resources.
        uses.texture(fresh.Get(), gpu::Use::PixelRead, gpu::Use::ColorWrite);
    } else {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = kSceneColorFormat;
        clear.Color[3] = 1.0f;
        d3d_check(create_texture(device_.Get(), &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                   gpu::Use::ColorWrite, &clear, IID_PPV_ARGS(&fresh)),
                  "Create scene target colour");
    }
    // The picture becomes an display picture: a shader resource, as presentation reads pictures.
    uses.texture(scene_color_.Get(), gpu::Use::ColorWrite, gpu::Use::PixelRead);
    uses.issue();

    gpu::TextureRef picture = std::move(scene_color_);
    scene_color_ = std::move(fresh);
    // RTVs are copied when recorded: the scene target's slot takes the new target now. Its conversion views, which the GPU reads
    // when it runs, go to the next table in turn (a frame in flight still reads the one before).
    if (scene_msaa_.samples <= 1u) {
        device_->CreateRenderTargetView(scene_color_.Get(), nullptr, scene_rtv_);
    }
    if (conv_scene_table_ < conv_scene_tables_.size()) {
        conv_scene_tables_[conv_scene_table_].free_from = frame_index_ + kFramesInFlight + 1u;
    }
    std::size_t table = 0u;
    while (table < conv_scene_tables_.size() && (table == conv_scene_table_ || frame_index_ < conv_scene_tables_[table].free_from)) {
        ++table;
    }
    if (table == conv_scene_tables_.size()) {
        conv_scene_tables_.push_back({allocate_persistent_descriptors(2u)});
    }
    conv_scene_table_ = table;
    write_scene_conversion_views(conv_scene_tables_[table].table);
    bind_scene();
    return picture;
}

// The share of wall time the GPU may spend on the frame's command lists before Match Output steps down: the rest is
// headroom for heavier frames than the window's average, the compositor and other processes.
constexpr double kGpuBudgetBusy = 0.80;

void RendererD3D12::sync_post_process_settings() {
    if (!post_process_.ready()) {
        return;
    }
    const PostProcessSettings next = post_process_settings_from(get_render_config());
    const PostProcessSettings& current = post_process_.settings();
    if (next.aa != current.aa || next.smaa_quality != current.smaa_quality ||
        next.upscaler != current.upscaler || next.sharpness != current.sharpness) {
        post_process_.apply_settings(next, frame_index_, kFramesInFlight);
    }
}

void RendererD3D12::apply_window_size() {
    if (!device_ || !scene_color_ || output_scene_active_ || scene_hdr_) {
        return;
    }
    // An edge drag resizes every frame: settle first, then re-create once.
    if (host_window::move_size_active.load(std::memory_order_acquire)) {
        return;
    }
    const RenderConfig render = get_render_config();
    // The picture's exact size on the screen, square pixels. A quality upscaler renders a whole scale below it.
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect);
    if (content.width <= 0 || content.height <= 0) {
        return;
    }
    FrameMap wanted(static_cast<unsigned>(content.width), static_cast<unsigned>(content.height));
    upscale_output_ = wanted;
    const auto config = render;
    const auto [render_width, render_height] = internal_extent(wanted.width, wanted.height, render_internal_height(config),
        D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION, host_window::upscaler_render_fraction(config, wanted.width, wanted.height));
    wanted = FrameMap(render_width, render_height);
    const unsigned samples = wanted_msaa_samples();
    if (wanted == frame_ && samples == scene_msaa_.samples) {
        return;
    }
    if (wanted.width * wanted.height > frame_.width * frame_.height && !scene_fits_memory(wanted)) {
        if (GetTickCount64() >= window_size_refused_ms_) {
            window_size_refused_ms_ = GetTickCount64() + 10'000u;
            std::cerr << "[Renderer] " << wanted.width << "x" << wanted.height
                      << " exceeds the video-memory budget; staying at " << frame_.width << "x" << frame_.height << '\n';
        }
        return;
    }
    // Between frames with nothing recording; the GPU must be done with the
    // old targets and every scaled copy destination before they go away.
    wait_for_gpu();
    frame_pass_targets_.clear();
    for (auto& retired : retired_targets_) {
        retired.clear();
    }
    readback_buf_.Reset();
    readback_pending_ = false;
    readback_fence_value_ = 0u;
    const FrameMap previous = frame_;
    if (!create_scene_targets(wanted)) {
        return;
    }
    frame_ = wanted;
    refresh_scene_conversion_source_views();
    invalidate_draw_bindings();
    std::cout << "[Renderer] internal resolution " << previous.width << "x" << previous.height << " -> "
              << frame_.width << "x" << frame_.height;
    if (scene_msaa_.samples > 1u) {
        std::cout << " MSAA " << scene_msaa_.samples << 'x';
    }
    std::cout << '\n';
}

bool RendererD3D12::scene_fits_memory(const FrameMap& map) const {
    if (!adapter3_) {
        return true;
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter3_->QueryVideoMemoryInfo(
            0u, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) || info.Budget == 0u) {
        return true;
    }
    // Size-dependent residency: scene colour (4 bytes per pixel) and depth (kSceneDepthBytes) and at most the display
    // picture pair plus one full-picture copy (12 bytes per pixel).
    const auto bytes = [](const FrameMap& m) { return (4.0 + kSceneDepthBytes + 12.0) * m.width * m.height; };
    const double needed = bytes(map) - bytes(frame_);
    return static_cast<double>(info.CurrentUsage) + needed <=
           0.9 * static_cast<double>(info.Budget);
}

void RendererD3D12::probe_occlusion() {
    if (!swap_chain_ || !occluded_.load(std::memory_order_relaxed)) {
        return;
    }
    // A test present draws nothing and moves no buffer: S_OK once the window shows, DXGI_STATUS_OCCLUDED while it does not.
    const HRESULT hr = swap_chain_->Present(0u, DXGI_PRESENT_TEST);
    if (hr != DXGI_STATUS_OCCLUDED) {
        occluded_.store(false, std::memory_order_relaxed);
    }
}

bool RendererD3D12::video_memory(std::uint64_t& usage, std::uint64_t& budget) const noexcept {
    usage = budget = 0u;
    if (!adapter3_) {
        return false;
    }
    // The use moves with every allocation and is read at most every 100 ms; the budget moves when the OS says (the event), and
    // that is read at once.
    const std::int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch()).count();
    const bool moved = budget_event_ != nullptr && WaitForSingleObject(budget_event_, 0u) == WAIT_OBJECT_0;
    if (moved || memory_budget_ == 0u || now - memory_read_ms_ >= 100) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (FAILED(adapter3_->QueryVideoMemoryInfo(0u, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) || info.Budget == 0u) {
            return false;
        }
        if (moved) {
            std::cout << "[vram] the OS moved the budget from " << (memory_budget_ >> 20u) << " MB to " << (info.Budget >> 20u)
                      << " MB\n";
        }
        memory_read_ms_ = now;
        memory_usage_ = info.CurrentUsage;
        memory_budget_ = info.Budget;
    }
    usage = memory_usage_;
    budget = memory_budget_;
    return true;
}

void RendererD3D12::release_budget_notification() noexcept {
    if (adapter3_ && budget_cookie_ != 0u) {
        adapter3_->UnregisterVideoMemoryBudgetChangeNotification(budget_cookie_);
    }
    budget_cookie_ = 0u;
    if (budget_event_ != nullptr) {
        CloseHandle(budget_event_);
        budget_event_ = nullptr;
    }
}

bool RendererD3D12::create_clear_pipeline() {
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;
    param.Constants.Num32BitValues = 4;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 1;
    rsd.pParameters = &param;

    ComPtr<ID3DBlob> blob, err;
    d3d_check(D3D12SerializeRootSignature(
        &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err),
        "SerializeClearRS");
    d3d_check(device_->CreateRootSignature(
        0, blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&s_clear_root_sig)), "CreateClearRS");

    clear_vs_ = utility_shader(UtilityShader::BlitVs);
    clear_ps_ =
        utility_shader(UtilityShader::ClearPs);
    return clear_pipeline(true) != nullptr && clear_pipeline(false) != nullptr;
}

ID3D12PipelineState* RendererD3D12::clear_pipeline(bool color) {
    const std::uint8_t variant = scene_variant();
    ComPtr<ID3D12PipelineState>& pipeline =
        (color ? clear_rgb_pipelines_ : clear_alpha_pipelines_)[variant];
    if (pipeline) {
        return pipeline.Get();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = s_clear_root_sig.Get();
    pd.VS = {clear_vs_->GetBufferPointer(), clear_vs_->GetBufferSize()};
    pd.PS = {clear_ps_->GetBufferPointer(), clear_ps_->GetBufferSize()};
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = (variant & kSceneVariantTarget) != 0u ? kTargetColorFormat
                       : (variant & kSceneVariantHdr) != 0u ? kSceneHdrColorFormat : kSceneColorFormat;
    pd.SampleDesc.Count = 1u << (variant & kSceneVariantSamplesMask);
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = color
        ? D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE
        : D3D12_COLOR_WRITE_ENABLE_ALPHA;
    d3d_check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pipeline)),
              color ? "CreateClearRgbPSO" : "CreateClearAlphaPSO");
    return pipeline.Get();
}

bool RendererD3D12::initialize(int window_width, int window_height, const FrameMap& frame) {
    frame_   = frame;
    frame_slot_ = 0;
    frame_index_ = 0;
    fence_values_.fill(0);
    timestamp_pending_.fill(false);
    window_shown_ = false;
    if (window_ != nullptr) {
        window_shown_ = true;
    } else {
        const host_window::OpenedWindow opened =
            host_window::open_game_window(*window_owner_, window_width, window_height);
        if (opened.window == nullptr) {
            return false;
        }
        window_ = opened.window;
        window_shown_ = opened.shown;
    }

    // --gpu-validation: 1 enables the D3D12 debug layer, 2 also GPU-based validation.
    const unsigned d3d_debug_level = ::galaxy::launch_args().gpu_validation;
    try {
        if (d3d_debug_level >= 1) {
            ComPtr<ID3D12Debug> d;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d)))) {
                d->EnableDebugLayer();
                if (d3d_debug_level >= 2) {
                    ComPtr<ID3D12Debug1> d1;
                    if (SUCCEEDED(d.As(&d1))) {
                        d1->SetEnableGPUBasedValidation(TRUE);
                    }
                }
            }
        }
#if defined(NEBULA_DEV)
        // Test builds: the removal data dump_device_removal prints (auto-breadcrumbs, page-fault allocations, breadcrumb
        // contexts), asked for before the device exists. Release builds leave the system's choice.
        {
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
                dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            } else {
                ComPtr<ID3D12DeviceRemovedExtendedDataSettings> basic;
                if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&basic)))) {
                    basic->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                    basic->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                }
            }
        }
#endif
        boot_step("window and display");
        UINT flags = 0;
        if (d3d_debug_level >= 1) {
            flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
        ComPtr<IDXGIFactory4> factory;
        d3d_check(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");

        // Every hardware adapter is tried in preference order and the first that
        // creates a D3D12 device is kept (entry zero may not). Legacy enumeration
        // covers systems without IDXGIFactory6 or a failed preferred enumeration.
        // A software adapter is never selected.
        ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 adapter_desc{};
        auto try_adapter = [&](IDXGIAdapter1* candidate) {
            DXGI_ADAPTER_DESC1 description{};
            if (candidate == nullptr ||
                FAILED(candidate->GetDesc1(&description)) ||
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u ||
                (same_card_only_ && (description.AdapterLuid.LowPart != card_.LowPart ||
                                     description.AdapterLuid.HighPart != card_.HighPart))) {
                return false;
            }
            ComPtr<ID3D12Device> candidate_device;
            if (FAILED(D3D12CreateDevice(
                    candidate, D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&candidate_device)))) {
                return false;
            }
            adapter = candidate;
            adapter_desc = description;
            device_ = std::move(candidate_device);
            return true;
        };
        // The GPU chosen in the settings (config.ini gpu) comes first, whatever Windows' GPU preference
        // says; that preference orders only the automatic choice below, which also covers a chosen GPU
        // that is missing or cannot create a device. Windows can list a GPU twice (a virtual display
        // adds an entry); searching in the automatic order takes the entry Windows prefers.
        ComPtr<IDXGIFactory6> preferred_factory;
        const bool preference_order = SUCCEEDED(factory.As(&preferred_factory));
        const auto enumerate = [&](UINT index, ComPtr<IDXGIAdapter1>& candidate) {
            return preference_order
                ? preferred_factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate))
                : factory->EnumAdapters1(index, &candidate);
        };
        const std::string chosen_gpu = get_render_config().gpu.data();
        // DLSS: Streamline, for an NVIDIA RTX GPU only (DLSS runs on nothing else), whenever it is installed, so DLSS
        // can be chosen in the settings without a restart. It must run before the queue and the swap chain exist (its
        // proxies make them); the device is handed to it after. It starts beside the device's creation for the GPU
        // the choice below takes (the chosen one, else the first in Windows' order), and after it when another is
        // taken in the end. Elsewhere it never loads, which spares a second of the start on a laptop.
        const auto rtx = [](const DXGI_ADAPTER_DESC1& description) {
            return description.VendorId == 0x10DEu && std::wcsstr(description.Description, L"RTX") != nullptr;
        };
        bool expect_rtx = false;
        {
            bool first = true;
            for (UINT index = 0u;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                DXGI_ADAPTER_DESC1 description{};
                if (FAILED(enumerate(index, candidate))) {
                    break;
                }
                if (FAILED(candidate->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u) {
                    continue;
                }
                const bool chosen = !chosen_gpu.empty() &&
                                    gpu_identity(description.VendorId, description.DeviceId, description.SubSysId) == chosen_gpu;
                if (chosen || first) {
                    expect_rtx = rtx(description);
                }
                first = false;
                if (chosen) {
                    break;
                }
            }
        }
        const bool frame_generation = get_render_config().frame_generation != 0u;
        std::thread streamline_start;
        if (expect_rtx) {
            streamline_start = std::thread([frame_generation] {
                (void)streamline::startup(streamline::default_directory(), frame_generation);
            });
        }
        if (!chosen_gpu.empty()) {
            for (UINT index = 0u;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                if (FAILED(enumerate(index, candidate))) {
                    break;
                }
                DXGI_ADAPTER_DESC1 description{};
                if (SUCCEEDED(candidate->GetDesc1(&description)) &&
                    gpu_identity(description.VendorId, description.DeviceId, description.SubSysId) == chosen_gpu &&
                    try_adapter(candidate.Get())) {
                    break;
                }
            }
            std::cout << "[gpu] chosen " << chosen_gpu
                      << (adapter ? " in use" : " not present or unusable; choosing automatically") << '\n';
        }
        if (!adapter && preference_order) {
            for (UINT index = 0u;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                const HRESULT result = preferred_factory->EnumAdapterByGpuPreference(
                    index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&candidate));
                if (result == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                if (FAILED(result)) {
                    break;
                }
                if (try_adapter(candidate.Get())) {
                    break;
                }
            }
        }
        if (!adapter) {
            for (UINT index = 0u;; ++index) {
                ComPtr<IDXGIAdapter1> candidate;
                const HRESULT result = factory->EnumAdapters1(index, &candidate);
                if (result == DXGI_ERROR_NOT_FOUND) {
                    break;
                }
                if (FAILED(result)) {
                    break;
                }
                if (try_adapter(candidate.Get())) {
                    break;
                }
            }
        }
        if (streamline_start.joinable()) {
            streamline_start.join();
        }
        if (!adapter || !device_) {
            throw std::runtime_error(
                "No hardware D3D12 adapter supports feature level 11_0");
        }
        if (!expect_rtx && rtx(adapter_desc)) {
            (void)streamline::startup(streamline::default_directory(), frame_generation);
        }
        boot_step("device and streamline");
        card_ = adapter_desc.AdapterLuid;
        // Published after every failure path, for adapter_is_unified_memory().
        g_selected_adapter_desc = adapter_desc;
        g_selected_adapter_valid = true;
        host_window::rtx_adapter.store(adapter_desc.VendorId == 0x10DEu &&
                                       std::wcsstr(adapter_desc.Description, L"RTX") != nullptr,
                                       std::memory_order_relaxed);
        (void)adapter.As(&adapter3_);
        release_budget_notification();
        if (adapter3_) {
            budget_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (budget_event_ != nullptr &&
                FAILED(adapter3_->RegisterVideoMemoryBudgetChangeNotificationEvent(budget_event_, &budget_cookie_))) {
                budget_cookie_ = 0u;
                release_budget_notification();
            }
        }
        {
            const std::string narrow = narrow_utf8(adapter_desc.Description);
            const bool is_software =
                (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            std::cout << "[gpu] adapter=\"" << narrow << "\""
                      << " vram-mb="
                      << (adapter_desc.DedicatedVideoMemory / (1024 * 1024))
                      // On integrated graphics vram-mb is a fixed compatibility value and the budget is shared-mb
                      // (capped by the OS at half of host RAM): a dedicated figure far below shared means unified memory.
                      << " shared-mb="
                      << (adapter_desc.SharedSystemMemory / (1024 * 1024))
                      << " host-ram-mb=" << host_ram_mb()
                      << " software=" << (is_software ? 1 : 0)
                      << " vendor=0x" << std::hex << adapter_desc.VendorId
                      << " device=0x" << adapter_desc.DeviceId << std::dec
                      << '\n';
            char gpu[640]{};
            std::snprintf(gpu, sizeof(gpu), "\"%s\" vram-mb=%llu vendor=0x%04X device=0x%04X", narrow.c_str(),
                          static_cast<unsigned long long>(adapter_desc.DedicatedVideoMemory / (1024 * 1024)),
                          static_cast<unsigned>(adapter_desc.VendorId), static_cast<unsigned>(adapter_desc.DeviceId));
            galaxy::crash_notes::set(galaxy::crash_notes::Fixed::Gpu, gpu);
        }

        // With the debug layer: ERROR/CORRUPTION messages go to stderr as they are produced.
        // A driver fail-fast (STATUS_STACK_BUFFER_OVERRUN in the UMD) ends the process before
        // the stored messages could be dumped at device removal.
        if (d3d_debug_level >= 1) {
            ComPtr<ID3D12InfoQueue1> iq1;
            if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&iq1)))) {
                DWORD cookie = 0;
                iq1->RegisterMessageCallback(
                    [](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity,
                       D3D12_MESSAGE_ID id, LPCSTR description, void*) {
#if defined(NEBULA_DEV)
                        if (s_listing_live_objects) {
                            std::cerr << "[D3D12-LIVE-OBJECT] " << description << '\n';
                            return;
                        }
#endif
                        if (severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                            severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                            // The callback runs inside the call the message is about: the executable's return addresses
                            // name it (llvm-symbolizer --obj=NebulaNative.exe --relative-address).
                            void* frames[24]{};
                            const USHORT count = CaptureStackBackTrace(1u, 24u, frames, nullptr);
                            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
                            std::cerr << "[D3D12-LIVE-ERR] id " << static_cast<int>(id) << ": " << description << " | at";
                            for (USHORT i = 0; i < count; ++i) {
                                HMODULE module = nullptr;
                                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                       static_cast<LPCWSTR>(frames[i]), &module) &&
                                    reinterpret_cast<std::uintptr_t>(module) == base) {
                                    std::cerr << " 0x" << std::hex << (reinterpret_cast<std::uintptr_t>(frames[i]) - base) << std::dec;
                                }
                            }
                            std::cerr << '\n';
                            std::cerr.flush();
                        }
                    },
                    D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                    nullptr,
                    &cookie);
            }
        }

        D3D_FEATURE_LEVEL requested_feature_levels[] = {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels{};
        feature_levels.NumFeatureLevels =
            static_cast<UINT>(std::size(requested_feature_levels));
        feature_levels.pFeatureLevelsRequested = requested_feature_levels;
        feature_levels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        if (FAILED(device_->CheckFeatureSupport(
                D3D12_FEATURE_FEATURE_LEVELS,
                &feature_levels,
                sizeof(feature_levels)))) {
            feature_levels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        }
        {
            // Enhanced barriers when the runtime and the driver have them (OPTIONS12), legacy transitions otherwise.
            D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
            if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12))) &&
                options12.EnhancedBarriersSupported && SUCCEEDED(device_.As(&barrier_device_))) {
                BarrierPath::device = barrier_device_.Get();
            } else {
                barrier_device_.Reset();
                BarrierPath::device = nullptr;
            }
            std::cerr << "[d3d12] barriers: " << (BarrierPath::enhanced() ? "enhanced" : "legacy") << '\n';
        }
        ComPtr<ID3D12Device5> device5;
        const bool dxr_device5_supported = SUCCEEDED(device_.As(&device5));
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        const bool dxr_runtime_supported =
            dxr_device5_supported &&
            SUCCEEDED(device_->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5,
                &options5,
                sizeof(options5))) &&
            options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
        {
            // What the tracer needs of the device (rt_caps.h): inline ray queries (DXR 1.1 with the shader model its
            // shaders are compiled for, 6.5) and every texture in one table (resource binding tier 3).
            D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_5};
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            RtCaps rt{};
            rt.acceleration_structures = dxr_runtime_supported;
            rt.ray_queries = dxr_runtime_supported && options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 &&
                             SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model,
                                                                    sizeof(shader_model))) &&
                             shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_5;
            rt.bindless = SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) &&
                          options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3;
            rt.micromaps = static_cast<int>(options5.RaytracingTier) >= 12;  // D3D12_RAYTRACING_TIER_1_2
            D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
            rt.unified_memory = SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture))) &&
                                architecture.UMA != FALSE;
            rt.shaders = rt_shaders_present();
            rt.enabled = true;  // Direct3D 12 enables nothing per device
            set_rt_caps(rt);
            log_rt_caps("d3d12", rt, get_render_config().ray_tracing_enabled);
            if (get_render_config().ray_tracing_enabled && rt_capable(rt)) {
                (void)rt::shaders_load(device_.Get());
            }
            const bool offered = kRayTracingRowOffered && rt_capable(rt);
            host_window::ray_tracing_offered.store(offered, std::memory_order_relaxed);
            std::array<char, 128> description{};
            copy_adapter_description(adapter_desc, description);
            record_render_adapter(description.data(), adapter_desc.VendorId, adapter_desc.DeviceId,
                                  adapter_desc.SubSysId, offered);
        }
        RenderFeatureCaps caps{};
        caps.d3d12_device_created = true;
        caps.d3d_feature_level =
            static_cast<unsigned>(feature_levels.MaxSupportedFeatureLevel);
        caps.adapter_vendor_id = adapter_desc.VendorId;
        caps.adapter_device_id = adapter_desc.DeviceId;
        caps.adapter_subsys_id = adapter_desc.SubSysId;
        caps.adapter_revision = adapter_desc.Revision;
        caps.adapter_luid_low =
            static_cast<std::uint32_t>(adapter_desc.AdapterLuid.LowPart);
        caps.adapter_luid_high =
            static_cast<std::uint32_t>(adapter_desc.AdapterLuid.HighPart);
        copy_adapter_description(adapter_desc, caps.adapter_description);
        caps.dxr_device5_supported = dxr_device5_supported;
        caps.dxr_raytracing_tier =
            raytracing_tier_code(options5.RaytracingTier);
        const bool dlss_ready = streamline::active() &&
                                streamline::attach_device(device_.Get(), adapter_desc.AdapterLuid);
        boot_step("streamline device");
        caps.streamline_bridge_built = kStreamlineBuilt;
        caps.msaa = make_render_feature_status(
            true,
            msaa_max_samples() > 1u,
            true,
            true,
            msaa_max_samples() > 1u ? "scene target targets multisampled; off with DLSS, FSR 3 or ray tracing"
                                    : "the GPU has no multisampled scene target formats");
        caps.taa = make_render_feature_status(
            false,
            false,
            false,
            false,
            "motion vectors, jitter, and history resources are not built");
        caps.dxr = make_render_feature_status(
            true,
            dxr_runtime_supported,
            host_window::ray_tracing_offered.load(std::memory_order_relaxed),
            host_window::ray_tracing_offered.load(std::memory_order_relaxed),
            host_window::ray_tracing_offered.load(std::memory_order_relaxed) ? "ray tracing (GRAPHICS: RAY TRACING)"
            : rt_refusal(rt_caps()) != nullptr                                ? rt_refusal(rt_caps())
                                                                              : "ray tracing is not offered yet");
        caps.streamline = make_render_feature_status(
            caps.streamline_bridge_built,
            streamline::active(),
            streamline::active(),
            false,
            !caps.streamline_bridge_built ? "NVIDIA Streamline bridge is not built into this runtime"
            : streamline::active()        ? "NVIDIA Streamline is running"
                                          : "NVIDIA Streamline is not installed or not signed");
        caps.reflex = make_render_feature_status(
            false,
            false,
            false,
            false,
            "Streamline Reflex integration is not built");
        caps.dlss_super_resolution = make_render_feature_status(
            caps.streamline_bridge_built,
            dlss_ready,
            dlss_ready,
            dlss_ready,
            dlss_ready ? "DLSS super resolution is available" : "DLSS needs Streamline and an NVIDIA RTX GPU");
        caps.dlss_frame_generation = make_render_feature_status(
            false,
            false,
            false,
            false,
            "DLFG requires Streamline, Reflex, motion vectors, and UI tagging");
        set_render_feature_caps(caps);

        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        // Streamline tracks queues through its device proxy.
        d3d_check(streamline::queue_device(device_.Get())->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue_)),
                  "CreateCommandQueue");
        d3d_check(
            queue_->GetTimestampFrequency(&timestamp_frequency_),
            "GetTimestampFrequency");
        QueryPerformanceFrequency(&qpc_frequency_);

        if (true) {
            D3D12_QUERY_HEAP_DESC qhd{};
            qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            qhd.Count = kFramesInFlight * 2u;
            d3d_check(
                device_->CreateQueryHeap(
                    &qhd, IID_PPV_ARGS(&timestamp_query_heap_)),
                "Create timestamp query heap");

            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width =
                kFramesInFlight * 2u * sizeof(std::uint64_t);
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            d3d_check(
                device_->CreateCommittedResource(
                    &hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(&timestamp_readback_)),
                "Create timestamp readback");
            const bool passes = ::galaxy::diagnosing(::galaxy::kDiagnoseGpuPasses);
            diagnostic_passes_ = passes;
            if (passes) {
                qhd.Count = kFramesInFlight * kGpuPassSlots;
                d3d_check(device_->CreateQueryHeap(&qhd, IID_PPV_ARGS(&pass_query_heap_)), "Create pass query heap");
                rd.Width = kFramesInFlight * kGpuPassSlots * sizeof(std::uint64_t);
                d3d_check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                           IID_PPV_ARGS(&pass_readback_)),
                          "Create pass readback");
                D3D12_QUERY_HEAP_DESC sqd{};
                sqd.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
                sqd.Count = kFramesInFlight;
                d3d_check(device_->CreateQueryHeap(&sqd, IID_PPV_ARGS(&stats_query_heap_)),
                          "Create statistics query heap");
                rd.Width = kFramesInFlight * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
                d3d_check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                           IID_PPV_ARGS(&stats_readback_)),
                          "Create statistics readback");
            }
        }

        dxgi_factory_ = factory;
        create_swap_chain(fsr_generation_wanted());
        factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
        // Borderless fullscreen is set by the first frame's display check (reconcile_display_state), once the swap
        // chain's buffers exist; exclusive fullscreen is never entered (Alt+Enter is off above).

        // RTV heap: kSwapChainBuffers backbuffers + 1 scene target slot.
        {
            D3D12_DESCRIPTOR_HEAP_DESC dhd{};
            dhd.NumDescriptors = kSwapChainBuffers + 1;
            dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&rtv_heap_)), "RTV heap");
            const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
            for (unsigned i = 0; i < kSwapChainBuffers; ++i) {
                d3d_check(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])), "GetBuffer");
                device_->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, h);
                h.ptr += stride;
            }
            (void)stride;
        }

        for (unsigned i = 0; i < kFramesInFlight; ++i)
            d3d_check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators_[i])), "CreateCommandAllocator");
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(), nullptr, IID_PPV_ARGS(&command_list_)), "CreateCommandList");
        command_list_->SetName(L"FrameList");
        d3d_check(command_list_->Close(), "CommandList Close init");

        d3d_check(device_->CreateFence(next_fence_value_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event_) throw std::runtime_error("CreateEvent failed");

        boot_step("queue and swap chain");
        if (!create_scene_targets(frame_)) return false;

        if (!create_root_signature()) return false;

        // Blit root signature: one SRV table + static linear-clamp sampler.
        {
            D3D12_STATIC_SAMPLER_DESC ss{};
            ss.Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            ss.ShaderRegister   = 0;
            ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors     = 1;
            range.BaseShaderRegister = 0;
            range.OffsetInDescriptorsFromTableStart = 0;

            D3D12_ROOT_PARAMETER param{};
            param.ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges   = &range;
            param.ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rsd{};
            rsd.NumParameters     = 1;
            rsd.pParameters       = &param;
            rsd.NumStaticSamplers = 1;
            rsd.pStaticSamplers   = &ss;

            ComPtr<ID3DBlob> blob, err;
            d3d_check(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err), "SerializeBlitRS");
            d3d_check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&s_blit_root_sig)), "CreateBlitRS");
        }

        {
            const UtilityShaderBytes vs = utility_shader(UtilityShader::BlitVs);
            const UtilityShaderBytes ps = utility_shader(UtilityShader::BlitPs);

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
            pd.pRootSignature = s_blit_root_sig.Get();
            pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
            pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
            pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            pd.SampleMask = UINT_MAX;
            pd.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
            pd.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
            pd.RasterizerState.DepthClipEnable = TRUE;
            pd.DepthStencilState.DepthEnable   = FALSE;
            pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pd.NumRenderTargets      = 1;
            pd.RTVFormats[0]         = kBackbufferFormat;
            pd.SampleDesc.Count      = 1;
            d3d_check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&blit_pipeline_)), "CreateBlitPSO");
        }

        frame_pass_heap_.initialize(device_.Get());
        boot_step("targets, root signature, blit");

        // Presentation post-processing (SMAA, FSR 1, RCAS, box resample).
        {
            const RenderConfig cfg = get_render_config();
            const PostProcessSettings post = post_process_settings_from(cfg);
            PostProcessStatus off{};
            off.live_aa = static_cast<std::uint8_t>(post.aa);
            off.live_quality = post.smaa_quality;
            if (!cfg.post_process) {
                std::cerr << "[post-process] off; presenting with the plain blit\n";
                std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "CONFIG OFF");
                set_post_process_status(off);
            } else if (!post_process_.initialize(device_.Get(), queue_.Get(), post)) {
                std::cerr << "[post-process] unavailable; presenting with the plain blit\n";
                std::snprintf(off.skip_reason.data(), off.skip_reason.size(), "INIT FAILED");
                set_post_process_status(off);
            } else {
                set_post_process_status(post_process_.status());
                std::cerr << "[post-process] aa=" << cfg.aa_mode << " smaa-quality=" << cfg.smaa_quality
                          << " upscaler=" << cfg.upscaler << " sharpness=" << cfg.sharpness << '\n';
            }
            set_curved_surfaces_offered(true);
            if (!ambient_occlusion_.initialize(device_.Get())) {
                std::cerr << "[ambient-occlusion] unavailable\n";
            }
            if (!camera_pass_.initialize(device_.Get())) {
                std::cerr << "[scene-camera] unavailable: the scene stays in the scene target\n";
            }
            if (!game_bloom_.initialize(device_.Get())) {
                std::cerr << "[bloom] unavailable: the game's own chain\n";
            }
        }
        boot_step("post-process and ambient occlusion");

        if (!create_sampler_heap()) return false;

        // The vertex, index and constant rings take a request past their segment and spill in a chunk; the matrix ring's rows are
        // indexed from its base, so it only grows between frames.
        const auto ring = [&](UploadRing& target, const char* name, std::size_t bytes, unsigned segments, bool chunks) {
            (void)bytes;
            return target.initialize(device_.Get(), name, UploadRing::budget(kFirstRingBytes, segments), segments, kFirstRingBytes, chunks);
        };
        // The vertex and constant rings are read by the GPU every frame and written once, in order: video memory the CPU maps,
        // where the adapter has it (gpu_upload_ring.cpp); the others stay in upload heaps.
        vertex_ring_.set_place(gpu::UploadPlace::GpuLocal);
        constant_ring_.set_place(gpu::UploadPlace::GpuLocal);
        if (!ring(vertex_ring_, "vertex", kVertexRingBytes, kRingSegments, true) ||
            !ring(index_ring_, "index", kIndexRingBytes, kFramesInFlight, true) ||
            !ring(constant_ring_, "constant", kConstantRingBytes, kFramesInFlight, true) ||
            !ring(matrix_ring_, "matrix", kMatrixRingBytes, kRingSegments, false)) {
            return false;
        }
        // The shader-visible heap's limit from the device: resource binding tiers 1 and 2 hold one million descriptors, tier 3
        // names no limit (D3D12_FEATURE_DATA_D3D12_OPTIONS::ResourceBindingTier).
        unsigned heap_limit = DescriptorRing::kMaxHeapDescriptors;
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) &&
                options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3) {
                heap_limit = std::numeric_limits<unsigned>::max();  // no named limit: heaps double until the device refuses one
            }
            std::cerr << "[gpu] descriptor heap: resource binding tier " << static_cast<int>(options.ResourceBindingTier)
                      << (heap_limit == DescriptorRing::kMaxHeapDescriptors ? ", at most 1000000 descriptors\n"
                                                                            : ", no named limit\n");
        }
        if (!srv_ring_.initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvPerFrame, kFramesInFlight,
                                  kPersistentSrvDescriptors, kTextureSlotsFirst, heap_limit)) {
            return false;
        }
        srv_ring_.set_sampler_heap(sampler_heap_.Get());
        srv_ring_.on_frames_grown([](void* renderer) { static_cast<RendererD3D12*>(renderer)->texture_heap_replaced(); }, this);
        retired_heaps_.clear();
        null_texture_slot_ = 0u;
        mirrored_backend_ = nullptr;
        mirrored_store_ = nullptr;

        // Conversion sources occupy the descriptor ring's immutable prefix.
        if (!create_conversion_pipeline()) return false;
        if (!create_clear_pipeline()) return false;

        // Upload-ring buffers, logged next to the [gpu] adapter line.
        std::cerr << "[gpu-rings]"
                  << " vertex-mb=" << (kVertexRingBytes >> 20u)
                  << " index-mb=" << (kIndexRingBytes >> 20u)
                  << " constant-mb=" << (kConstantRingBytes >> 20u)
                  << " matrix-mb=" << (kMatrixRingBytes >> 20u)
                  << " unified-memory=" << (adapter_is_unified_memory() ? 1 : 0)
                  << " vertex-ring=" << (vertex_ring_.local() ? "video" : "host")
                  << " constant-ring=" << (constant_ring_.local() ? "video" : "host")
                  << " frames-in-flight=" << kFramesInFlight
                  << " total-mb="
                  << ((kVertexRingBytes + kIndexRingBytes + kConstantRingBytes + kMatrixRingBytes) >> 20u)
                  << '\n';
    } catch (const std::exception& e) {
        std::cerr << "[Renderer] initialize failed: " << e.what() << "\n";
        init_error_ = e.what();
        return false;
    }
    return true;
}

UploadBuffer RendererD3D12::create_upload_buffer(std::size_t bytes, const char* name) {
    return gpu::create_upload_memory(device_.Get(), bytes, name);
}

void RendererD3D12::attach_texture_uploads(TextureStore& cache) {
    cache.backend().set_upload_list(command_list_.Get());
    if (mirrored_backend_ != &cache.backend()) {
        // New textures are written on the copy queue where the adapter has memory of its own (gpu_texture_backend.h).
        cache.backend().enable_async_uploads(kFramesInFlight, frame_slot_);
        // Every view goes into the texture region from now on: room for twice the views made so far (at least
        // kTextureSlotsFirst), the null view in its last slot.
        const std::uint64_t wanted = std::max<std::uint64_t>(kTextureSlotsFirst, 2u * std::uint64_t{cache.backend().views_made()} + 1u);
        mirrored_store_ = &cache;
        mirrored_backend_ = &cache.backend();
        install_texture_heap(cache, static_cast<std::uint32_t>(std::min<std::uint64_t>(wanted, texture_slots_room(srv_ring_))));
    }
}

// The texture region takes `slots` descriptors (the last the null view). A larger count replaces the ring's heap
// (DescriptorRing::resize_textures: persistent descriptors and this frame's tables move with it) and every view made so
// far is copied in from the texture backend's CPU views. Commands already recorded keep the old heap, which is freed once
// the GPU is past this frame. The null slot moved, so the walker writes its texture lists again (sampler_generation_), and
// the encoder takes nothing as bound (a new heap can have the old one's address).
void RendererD3D12::install_texture_heap(TextureStore& cache, std::uint32_t slots) {
    const bool grown = slots > srv_ring_.texture_slots();
    if (grown) {
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> replaced = srv_ring_.resize_textures(slots);
        if (replaced) {
            retired_heaps_.push_back({std::move(replaced), next_fence_value_ + 1u});
        }
    }
    const std::uint32_t count = srv_ring_.texture_slots();
    device_->CopyDescriptorsSimple(1u, srv_ring_.texture_cpu(count - 1u), cache.backend().null_srv(),
                                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cache.backend().mirror_into(
        srv_ring_.texture_cpu(0u), count - 1u,
        [](void* renderer, std::uint32_t index) { static_cast<RendererD3D12*>(renderer)->grow_texture_heap(index); }, this);
    if (grown || null_texture_slot_ != count - 1u) {
        ++sampler_generation_;
        invalidate_draw_bindings();
    }
    null_texture_slot_ = count - 1u;
}

// Persistent descriptors from the ring, which doubles its region when it is full: the heap it builds has an empty texture
// region, so the views made so far are copied into it from the texture backend's CPU views (install_texture_heap) and the
// encoder takes nothing as bound.
DescriptorRing::Persistent RendererD3D12::allocate_persistent_descriptors(unsigned count) {
    ID3D12DescriptorHeap* const before = srv_ring_.heap();
    DescriptorRing::Persistent table = srv_ring_.allocate_persistent(count);
    if (srv_ring_.heap() != before) {
        if (mirrored_store_ != nullptr) {
            install_texture_heap(*mirrored_store_, srv_ring_.texture_slots());
        }
        ++sampler_generation_;
        invalidate_draw_bindings();
    }
    return table;
}

// A frame took more transient descriptors than its segment held (DescriptorRing::allocate): the ring's new heap has an
// empty texture region, so the views made so far are copied into it and the texture backend writes its next views
// there, as after the persistent region's growth. Commands already recorded keep the heap they were recorded with.
void RendererD3D12::texture_heap_replaced() {
    if (mirrored_store_ != nullptr) {
        install_texture_heap(*mirrored_store_, srv_ring_.texture_slots());
    }
    ++sampler_generation_;
    invalidate_draw_bindings();
}

// A view index reached the null slot (TextureBackend::publish_view): a heap twice as large, or as large as the views
// need, up to the device's heap limit less the ring's part.
void RendererD3D12::grow_texture_heap(std::uint32_t index) {
    if (mirrored_store_ == nullptr) {
        return;
    }
    const std::uint64_t needed = std::max<std::uint64_t>(std::uint64_t{index} + 2u, std::uint64_t{mirrored_store_->backend().views_made()} + 1u);
    std::uint64_t slots = std::max<std::uint64_t>(srv_ring_.texture_slots(), kTextureSlotsFirst);
    while (slots < needed) {
        slots *= 2u;
    }
    slots = std::min<std::uint64_t>(slots, texture_slots_room(srv_ring_));
    if (slots < needed || slots <= srv_ring_.texture_slots()) {
        return;  // publish_view reports the limit
    }
    install_texture_heap(*mirrored_store_, static_cast<std::uint32_t>(slots));
}

std::uint32_t RendererD3D12::texture_slot(const TextureHandle& handle) const noexcept {
    return handle.resource != nullptr ? handle.srv_index : null_texture_slot_;
}

std::uint32_t RendererD3D12::sampler_slot(const SamplerKey& key) {
    const RenderConfig cfg = get_render_config();
    const auto allocation = sampler_slots_.get_or_allocate(make_sampler_slot_key(key, cfg));
    if (allocation.index >= kSamplerHeapSlots) {
        throw std::runtime_error("[samplers] more new samplers in one frame than the sampler heap holds");
    }
    if (allocation.created) {
        const D3D12_SAMPLER_DESC desc = build_sampler_desc_from_key(key, cfg);
        D3D12_CPU_DESCRIPTOR_HANDLE slot = sampler_heap_->GetCPUDescriptorHandleForHeapStart();
        slot.ptr += static_cast<SIZE_T>(allocation.index) *
            device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        device_->CreateSampler(&desc, slot);
    }
    return allocation.index;
}

void RendererD3D12::write_texture_list(const std::uint32_t* pairs, std::uint32_t count, gpu::Address& lists,
                                       std::uint32_t& first) {
    TextureListBuffer& buffer = texture_lists_[frame_slot_];
    if (buffer.resource == nullptr || texture_lists_used_ + count > buffer.capacity) {
        if (buffer.resource != nullptr) {
            retired_texture_lists_.push_back({std::move(buffer.resource), next_fence_value_ + 1u});
        }
        const std::uint32_t capacity = std::max(4096u, 2u * std::max(buffer.capacity, count));
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = UINT64{capacity} * 8u;
        desc.Height = 1u;
        desc.DepthOrArraySize = 1u;
        desc.MipLevels = 1u;
        desc.SampleDesc.Count = 1u;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d3d_check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   nullptr, IID_PPV_ARGS(&buffer.resource)),
                  "create texture lists");
        const D3D12_RANGE none{0u, 0u};
        void* mapped = nullptr;
        d3d_check(buffer.resource->Map(0u, &none, &mapped), "map texture lists");
        buffer.mapped = static_cast<std::uint32_t*>(mapped);
        buffer.capacity = capacity;
        texture_lists_used_ = 0u;
    }
    std::memcpy(buffer.mapped + std::size_t{texture_lists_used_} * 2u, pairs, std::size_t{count} * 8u);
    lists = buffer.resource->GetGPUVirtualAddress();
    first = texture_lists_used_;
    texture_lists_used_ += count;
}

void RendererD3D12::free_target(std::uint32_t id, TextureStore& textures) {
    for (auto it = pictures_.begin(); it != pictures_.end();) {
        if (static_cast<std::uint32_t>(it->first) == id) {
            textures.retire_view(it->second.view, std::move(it->second.viewed));
            it = pictures_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = frame_pass_targets_.begin(); it != frame_pass_targets_.end();) {
        if (static_cast<std::uint32_t>(it->first) == id) {
            retired_targets_[frame_slot_].push_back(std::move(it->second.texture));
            it = frame_pass_targets_.erase(it);
        } else {
            ++it;
        }
    }
    // A target pass's set for this texture: ids are not used again, so no pass draws into it after this (a target slot used to
    // stay until a later target pass swept it, and a scene with none kept the last scene's, at the frame's scale).
    for (auto it = target_slots_.begin(); it != target_slots_.end();) {
        if (static_cast<std::uint32_t>(it->first) == id && !(target_pass_active_ && it->first == target_key_)) {
            retire_target_slot(it->second);
            it = target_slots_.erase(it);
        } else {
            ++it;
        }
    }
}

std::uint64_t RendererD3D12::allocation_bytes(ID3D12Resource* resource) const {
    if (resource == nullptr || !device_) {
        return 0u;
    }
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    return device_->GetResourceAllocationInfo(0u, 1u, &desc).SizeInBytes;
}

// `[vram] breakdown`: every 600 frames with --diagnostics memory (RenderCore's table log), the OS's reading of the process's local video memory and what the
// renderer holds of it by kind, each figure a resource's allocation size or a heap's size. Memory in upload heaps (host
// memory on a discrete adapter, video memory for a ring that got a GPU upload heap) is listed apart: `rings local` and the
// device-local kinds are subtracted from the OS figure to leave `other` (pipelines, the post chain's, ambient occlusion's
// and bloom's own targets, the driver's and the compositor's).
void RendererD3D12::log_video_memory(const VideoMemoryOutside& outside) {
    const auto mb = [](std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    (void)video_memory(usage, budget);

    const auto set_bytes = [this](const SceneTargetSet& set) {
        return allocation_bytes(set.color.Get()) + allocation_bytes(set.depth.Get()) + allocation_bytes(set.msaa.color.Get()) +
               allocation_bytes(set.msaa.depth.Get());
    };
    // The scene targets: the live set, the output-resolution one, the open target pass's, the wide and HUD-less ones, and the
    // motion, reactive and effects-layer targets.
    std::uint64_t scene = allocation_bytes(scene_color_.Get()) + allocation_bytes(scene_depth_.Get()) +
                          allocation_bytes(scene_msaa_.color.Get()) + allocation_bytes(scene_msaa_.depth.Get());
    scene += set_bytes(alt_scene_) + set_bytes(target_scene_) + set_bytes(hdr_scene_scene_) + set_bytes(fg_hudless_scene_target_);
    scene += allocation_bytes(fg_hudless_display_.texture.Get()) + allocation_bytes(fg_hudless_target_.Get()) +
             allocation_bytes(motion_target_.Get()) + allocation_bytes(reactive_target_.Get()) +
             allocation_bytes(layer_color_.Get()) + allocation_bytes(layer_trans_.Get()) +
             allocation_bytes(hdr_capture_target_.Get());
    for (const OldSceneTarget& old : old_scene_targets_) {
        scene += allocation_bytes(old.texture.Get());
    }
    std::uint64_t slots = 0u;
    for (const auto& entry : target_slots_) {
        slots += set_bytes(entry.second.set);
    }
    // Resources waiting out their frames: textures count as device-local, buffers (retired rings and chunks) are listed apart.
    std::uint64_t retired_textures = 0u;
    std::uint64_t retired_buffers = 0u;
    for (const RetiredResourceList& list : retired_targets_) {
        for (const Microsoft::WRL::ComPtr<ID3D12Resource>& resource : list) {
            if (resource) {
                (resource->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? retired_buffers : retired_textures) +=
                    allocation_bytes(resource.Get());
            }
        }
    }
    for (const RetiredBuffer& retired : retired_texture_lists_) {
        retired_buffers += allocation_bytes(retired.resource.Get());
    }
    std::uint64_t ring_local = 0u;
    std::uint64_t ring_host = 0u;
    std::uint64_t chunk_local = 0u;
    std::uint64_t chunk_host = 0u;
    for (const UploadRing* ring : {&vertex_ring_, &index_ring_, &constant_ring_, &matrix_ring_}) {
        (ring->local() ? ring_local : ring_host) += ring->capacity();
        (ring->local() ? chunk_local : chunk_host) += ring->live_chunk_bytes();
    }
    const PlacedTextureAllocator::Stats frame_pass_heap = frame_pass_heap_.stats();
    std::uint64_t descriptor_heaps = srv_ring_.heap_bytes();
    const std::uint64_t descriptor_stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (const RetiredHeap& retired : retired_heaps_) {
        if (retired.heap) {
            descriptor_heaps += std::uint64_t{retired.heap->GetDesc().NumDescriptors} * descriptor_stride;
        }
    }
    const std::uint64_t accounted = outside.texture_heap_bytes + frame_pass_heap.heap_bytes +
                                    scene + slots + outside.picture_bytes + retired_textures + ring_local + chunk_local +
                                    descriptor_heaps + outside.mesh_local_bytes;
    std::fprintf(stderr,
                 "[vram] breakdown os-usage=%.0f budget=%.0f MB | textures heaps=%llu %.0f used=%.1f committed=%llu | "
                 "scene-targets=%.1f target-slots=%zu %.1f frame-pass=%zu in %.0f (heap) pictures=%u %.1f retired=%.1f | "
                 "rings local=%.0f chunks=%.0f | descriptor-heaps=%.1f | accounted=%.0f other=%.0f | "
                 "host: staging=%.0f mesh-pages=%u (empty %u) %.0f (%.0f in video memory) live %.1f slots=%u %.1f rings=%.0f chunks=%.0f retired-buffers=%.1f | "
                 "pipelines=%llu\n",
                 mb(usage), mb(budget), static_cast<unsigned long long>(outside.texture_heaps), mb(outside.texture_heap_bytes),
                 mb(outside.texture_used_bytes), static_cast<unsigned long long>(outside.texture_committed), mb(scene),
                 target_slots_.size(), mb(slots), frame_pass_targets_.size(), mb(frame_pass_heap.heap_bytes), outside.picture_count,
                 mb(outside.picture_bytes), mb(retired_textures), mb(ring_local), mb(chunk_local), mb(descriptor_heaps),
                 mb(accounted), usage > accounted ? mb(usage - accounted) : 0.0, mb(outside.staging_bytes), outside.mesh_pages,
                 outside.mesh_empty_pages, mb(outside.mesh_page_bytes), mb(outside.mesh_local_bytes), mb(outside.mesh_live_bytes),
                 outside.slots,
                 mb(outside.slot_bytes), mb(ring_host), mb(chunk_host), mb(retired_buffers),
                 static_cast<unsigned long long>(outside.pipelines));
}

#if defined(NEBULA_DEV)
void RendererD3D12::list_live_objects(Kept& kept) {
    if (kept.debug_device) {
        s_listing_live_objects = true;
        kept.debug_device->ReportLiveDeviceObjects(D3D12_RLDO_SUMMARY | D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
        s_listing_live_objects = false;
        kept.debug_device.Reset();
    }
}
#endif

RendererD3D12::Kept RendererD3D12::take_apart_lost_device() {
    keep_window_ = true;
    device_gone_ = true;
    readback_pending_ = backbuffer_readback_pending_ = selected_display_readback_pending_ = false;
    try {
        shutdown();
    } catch (const std::exception& error) {
        std::cerr << "[Renderer] the lost device's shutdown: " << error.what() << "\n";
    }
    Kept kept{window_, card_};
#if defined(NEBULA_DEV)
    kept.debug_device = std::move(debug_device_);
#endif
    return kept;
}

void RendererD3D12::shutdown() {
    native_scene_gpu_ema_ms_ = 0.0;
    frame_scene_drawn_.fill(false);
    if (fsr_swap_chain_ && swap_chain_) {
        wait_for_gpu();
        fsr_generation_.destroy(swap_chain_.Get());
    }
    std::exception_ptr shutdown_failure;
    const auto retain_shutdown_failure =
        [&](const char* stage, auto&& action) {
            try {
                action();
            } catch (const std::exception& error) {
                std::cerr << "[Renderer] shutdown " << stage
                          << " failed: " << error.what() << "\n";
                if (shutdown_failure == nullptr) {
                    shutdown_failure = std::current_exception();
                }
            } catch (...) {
                std::cerr << "[Renderer] shutdown " << stage
                          << " failed with an unknown exception\n";
                if (shutdown_failure == nullptr) {
                    shutdown_failure = std::current_exception();
                }
            }
        };

    retain_shutdown_failure("settings flush", [&] {
        host_window::save_native_settings_now();
    });
    if (queue_ && fence_) {
        retain_shutdown_failure("GPU drain", [&] { wait_for_gpu(); });
    }
    // A diagnostic copy can be the last submitted list: its data goes to the
    // background PPM writers before any D3D resource is released.
    retain_shutdown_failure("scene target diagnostic readback", [&] {
        debug_log_readback();
    });
    retain_shutdown_failure("backbuffer diagnostic readback", [&] {
        debug_log_backbuffer_readback();
    });
    retain_shutdown_failure("selected display picture diagnostic readback", [&] {
        debug_log_selected_display_readback();
    });
    retain_shutdown_failure("diagnostic writer drain", [&] {
        reap_debug_capture_writers(true);
    });
    // Streamline shuts down before the swap chain and device it wraps go.
    retain_shutdown_failure("Streamline", [&] {
        temporal_.reset();
        temporal_pending_ = {};
        streamline::shutdown();
    });
    if (fence_event_) { CloseHandle(fence_event_); fence_event_ = nullptr; }
    release_budget_notification();
    host_window::pending_resize_extent.store(0u, std::memory_order_relaxed);
    s_blit_root_sig.Reset();
    post_process_.shutdown();
    ambient_occlusion_.shutdown();
    camera_pass_.shutdown();
    game_bloom_.shutdown();
    {
        const PlacedTextureAllocator::Stats heap = frame_pass_heap_.stats();
        std::cerr << "[frame-read-heap] heaps=" << heap.heaps << " heap-bytes=" << heap.heap_bytes
                  << " used-bytes=" << heap.used_bytes << " placed=" << heap.placed
                  << " committed-fallbacks=" << heap.committed_fallbacks << '\n';
    }
    frame_pass_heap_.shutdown();
    blit_pipeline_.Reset();
    s_conv_root_sig.Reset();
    conv_pipeline_.Reset();
    display_filter_pipeline_.Reset();
    conv_scratch_rtv_slot_.reset();
    conv_rtv_heap_.Reset();
    conv_scratch_rtv_ = {};
    conv_scene_color_index_ = conv_scene_depth_index_ = DescriptorRing::kNoPersistent;
    conv_scene_source_table_.reset();
    conv_scene_tables_.clear();
    conv_scene_table_ = ~std::size_t{0};
    probe_pipeline_.Reset();
    probe_root_signature_.Reset();
    probe_slots_ = {};
    s_clear_root_sig.Reset();
    clear_rgb_pipelines_ = {};
    clear_alpha_pipelines_ = {};
    clear_vs_ = {};
    clear_ps_ = {};
    timestamp_query_heap_.Reset();
    timestamp_readback_.Reset();
    timestamp_pending_.fill(false);
    timestamp_frequency_ = 0;
    readback_buf_.Reset();
    readback_pending_ = false;
    readback_fence_value_ = 0;
    next_scene_capture_path_.clear();
    readback_capture_path_.clear();
    backbuffer_readback_buf_.Reset();
    hdr_capture_target_.Reset();
    hdr_capture_rtv_heap_.Reset();
    hdr_active_ = false;
    hdr_failed_ = false;
    hdr_output_ = PostOutput{};
    display_read_ = false;
    display_reading_ = OutputHdr{};
    set_display_hdr(DisplayHdr{});
    backbuffer_readback_pending_ = false;
    backbuffer_readback_fence_value_ = 0;
    backbuffer_readback_width_ = backbuffer_readback_height_ =
        backbuffer_readback_row_pitch_ = 0;
    next_backbuffer_capture_path_.clear();
    backbuffer_readback_capture_path_.clear();
    selected_display_readback_buf_.Reset();
    selected_display_readback_source_.Reset();
    selected_display_readback_pending_ = false;
    selected_display_readback_fence_ = 0;
    selected_display_readback_width_ = selected_display_readback_height_ =
        selected_display_readback_pitch_ = 0;
    selected_display_readback_path_.clear();
    if (frame_pass_requests_ != 0u) {
        std::cerr << "[frame-pass-work] requests=" << frame_pass_requests_
                  << " passes=" << frame_passes_drawn_ << '\n';
    }
    frame_pass_requests_ = 0u;
    frame_passes_drawn_ = 0u;
    frame_pass_targets_.clear();
    for (auto& retired : retired_targets_) {
        retired.clear();
    }
    scene_color_.Reset();
    scene_depth_.Reset();
    for (auto& b : backbuffers_) b.Reset();
    rtv_heap_.Reset(); dsv_heap_.Reset(); sampler_heap_.Reset(); root_signature_.Reset();
    sampler_slots_.reset();
    for (TextureListBuffer& lists : texture_lists_) {
        lists = TextureListBuffer{};
    }
    retired_texture_lists_.clear();
    retired_heaps_.clear();
    mirrored_backend_ = nullptr;
    mirrored_store_ = nullptr;
    release_latency_wait();
    swap_chain_.Reset(); command_list_.Reset();
    for (auto& a : allocators_) a.Reset();
    vertex_ring_.reset();
    index_ring_.reset();
    constant_ring_.reset();
    matrix_ring_.reset();
    srv_ring_.reset();
    invalidate_draw_bindings();
    telemetry_window_start_qpc_ = last_present_qpc_ = 0;
    present_pacer_.reset();
    telemetry_new_pictures_start_ = telemetry_pictures_made_start_ = 0;
    window_shown_ = false;
    telemetry_present_count_ = 0;
    telemetry_interval_count_ = 0;
    telemetry_interval_ms_ = telemetry_interval_max_ms_ = 0.0;
    telemetry_present_ms_ = telemetry_present_max_ms_ = 0.0;
    telemetry_pace_wait_ms_ = telemetry_pace_wait_max_ms_ = 0.0;
    telemetry_gpu_ms_ = 0.0;
    telemetry_gpu_samples_ = 0;
#if defined(NEBULA_DEV)
    if (device_) {
        (void)device_.As(&debug_device_);
    }
#endif
    BarrierPath::device = nullptr;
    barrier_device_.Reset();
    fence_.Reset(); queue_.Reset(); device_.Reset();
    if (!keep_window_) {
        host_window::close_game_window(*window_owner_);
        window_ = nullptr;
    }
    if (shutdown_failure != nullptr) {
        std::rethrow_exception(shutdown_failure);
    }
}

// FSR 3 frame generation: chosen, with an upscaler that gives it depth and motion, and not DLSS, whose presents are Streamline's.
bool RendererD3D12::fsr_generation_wanted() const {
    const RenderConfig cfg = get_render_config();
    return fsr_frame_generation_compiled() && cfg.frame_generation == 2u && (cfg.upscaler == 2u || cfg.upscaler == 4u);
}

bool RendererD3D12::create_swap_chain(bool fsr) {
    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.BufferCount  = kSwapChainBuffers;
    scd.Width        = host_window::backbuffer_width;
    scd.Height       = host_window::backbuffer_height;
    scd.Format       = kBackbufferFormat;  // HDR10 is switched to by reconcile_hdr_output (ResizeBuffers)
    hdr_active_      = false;
    scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;
    scd.Scaling      = DXGI_SCALING_STRETCH;
    scd.AlphaMode    = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    // Tearing where the system allows it (not under AMD's swap chain, which wraps a chain made without it), so VSync off
    // presents at once: the flag must be on the swap chain for a present to carry DXGI_PRESENT_ALLOW_TEARING.
    BOOL tearing = FALSE;
    ComPtr<IDXGIFactory5> factory5;
    if (!fsr && SUCCEEDED(dxgi_factory_.As(&factory5)) &&
        SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing) {
        scd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    tearing_allowed_ = (scd.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0u;
    {
        // Once, and again only when it changes (a swap chain made again).
        static int logged_tearing = -1;
        if (logged_tearing != (tearing_allowed_ ? 1 : 0)) {
            logged_tearing = tearing_allowed_ ? 1 : 0;
            std::cerr << "[present] flip-model swap chain, " << (tearing_allowed_ ? "VSync off presents with ALLOW_TEARING (no wait for the display)"
                                                                                   : "VSync off still waits for the compositor (tearing not offered here)")
                      << ", VSync on: Present(1..4) paced by the latency object" << '\n';
        }
    }
    ComPtr<IDXGISwapChain1> sc1;
    ID3D12CommandQueue* const native = fsr ? streamline::native_queue(queue_.Get()) : queue_.Get();
    // Through Streamline's factory when it runs and the swap chain is not AMD's: its presents go through it.
    d3d_check((fsr ? dxgi_factory_ : streamline::swap_chain_factory(dxgi_factory_.Get()))
                  ->CreateSwapChainForHwnd(native, window_, &scd, nullptr, nullptr, &sc1),
              "CreateSwapChain");
    d3d_check(sc1.As(&swap_chain_), "SwapChain As");
    sc1.Reset();
    fsr_swap_chain_ = false;
    if (fsr) {
        if (FsrFrameGeneration::replace_swap_chain(native, swap_chain_)) {
            fsr_swap_chain_ = true;
        } else {
            swap_chain_.Reset();
            d3d_check(streamline::swap_chain_factory(dxgi_factory_.Get())
                          ->CreateSwapChainForHwnd(queue_.Get(), window_, &scd, nullptr, nullptr, &sc1),
                      "CreateSwapChain");
            d3d_check(sc1.As(&swap_chain_), "SwapChain As");
        }
    }
    ComPtr<IDXGISwapChain2> swap_chain2;
    d3d_check(swap_chain_.As(&swap_chain2), "SwapChain2 As");
    d3d_check(swap_chain2->SetMaximumFrameLatency(1u), "SetMaximumFrameLatency");
    release_latency_wait();
    latency_wait_ = swap_chain2->GetFrameLatencyWaitableObject();
    return true;
}

void RendererD3D12::release_latency_wait() noexcept {
    if (latency_wait_ != nullptr) {
        CloseHandle(latency_wait_);
        latency_wait_ = nullptr;
    }
}

// The swap chain made again when frame generation changes it (RenderConfig::frame_generation, applied between frames).
void RendererD3D12::recreate_swap_chain(bool fsr) {
    wait_for_gpu();
    if (fsr_swap_chain_) {
        fsr_generation_.destroy(swap_chain_.Get());
    }
    BOOL fullscreen = FALSE;
    if (SUCCEEDED(swap_chain_->GetFullscreenState(&fullscreen, nullptr)) && fullscreen) {
        (void)swap_chain_->SetFullscreenState(FALSE, nullptr);
        applied_display_.valid = false;
    }
    for (auto& buffer : backbuffers_) {
        buffer.Reset();
    }
    release_latency_wait();
    swap_chain_.Reset();
    create_swap_chain(fsr);
    const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (unsigned i = 0; i < kSwapChainBuffers; ++i) {
        d3d_check(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])), "GetBuffer");
        device_->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, h);
        h.ptr += stride;
    }
}

void RendererD3D12::apply_pending_resize() {
    const auto extent = host_window::pending_resize_extent.exchange(0u, std::memory_order_acq_rel);
    if (extent == 0u) return;
    resize_swap_chain(static_cast<UINT>(extent >> 32u), static_cast<UINT>(extent), false);
}

void RendererD3D12::resize_swap_chain(UINT new_width, UINT new_height, bool force) {
    if (!swap_chain_ || !rtv_heap_ || new_width == 0u || new_height == 0u) {
        return;
    }
    if (!force && new_width == host_window::backbuffer_width && new_height == host_window::backbuffer_height) {
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 current_desc{};
    if (FAILED(swap_chain_->GetDesc1(&current_desc))) {
        std::cerr << "[Renderer] resize: GetDesc1 failed; keeping prior "
                     "backbuffer size\n";
        return;
    }

    // ResizeBuffers needs every backbuffer reference released and the GPU done
    // with them. Only the swap chain's resources change: present() blits into
    // whatever backbuffer size is current.
    wait_for_gpu();
    if (fsr_swap_chain_) {
        fsr_generation_.destroy(swap_chain_.Get());
    }
    for (auto& buffer : backbuffers_) {
        buffer.Reset();
    }

    const HRESULT resize_hr = swap_chain_->ResizeBuffers(
        kSwapChainBuffers,
        new_width,
        new_height,
        current_desc.Format,
        current_desc.Flags);
    if (FAILED(resize_hr)) {
        dump_device_removal(device_.Get());
        std::cerr << "[Renderer] resize: ResizeBuffers failed hr=0x"
                  << std::hex << static_cast<unsigned long>(resize_hr)
                  << std::dec << "; swap chain left at prior size\n";
        // The backbuffers are already released, so a failed ResizeBuffers is fatal.
        d3d_check(resize_hr, "ResizeBuffers");
        return;
    }

    const UINT stride =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (unsigned i = 0; i < kSwapChainBuffers; ++i) {
        d3d_check(
            swap_chain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])),
            "resize GetBuffer");
        device_->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, h);
        h.ptr += stride;
    }

    host_window::backbuffer_width = new_width;
    host_window::backbuffer_height = new_height;
    // The frame-latency wait gates on the previous buffers' present history.
    if (trace_present_stats_enabled()) {
        std::cerr << "[Renderer] resized backbuffer to " << new_width << "x"
                  << new_height << '\n';
    }
}

void RendererD3D12::reconcile_display_state() {
    if (!swap_chain_ || window_ == nullptr || host_window::hidden_window_mode_enabled()) {
        return;
    }
    const RenderConfig cfg = get_render_config();
    const unsigned mode = std::min(cfg.display_mode, kDisplayBorderless);
    AppliedDisplay& applied = applied_display_;
    const bool same_display = !host_window::display_changed.exchange(false, std::memory_order_acq_rel) && applied.valid &&
                              applied.mode == mode && applied.monitor == cfg.monitor;
    if (same_display) {
        if (mode != kDisplayWindowed ||
            (applied.window_width == cfg.window_width &&
             applied.window_height == cfg.window_height) ||
            host_window::move_size_active.load(std::memory_order_acquire)) {
            return;
        }
    }

    const std::vector<MonitorInfo> monitors = enumerate_monitors();
    const MonitorInfo* monitor = find_monitor(monitors, widen_utf8(cfg.monitor.data()));
    if (monitor == nullptr) {
        return;
    }
    host_window::WindowPlacement placement{};
    if (mode == kDisplayWindowed) {
        placement.style = host_window::kWindowStyle;
        placement.rect = host_window::windowed_rect_on_monitor(*monitor, cfg.window_width, cfg.window_height);
        placement.borderless = false;
    } else {
        placement.style = WS_POPUP;
        placement.rect = monitor->bounds;
        placement.borderless = true;
    }
    DWORD_PTR result = 0u;
    if (SendMessageTimeoutW(window_, host_window::kPlaceWindowMessage, 0u,
                            reinterpret_cast<LPARAM>(&placement),
                            SMTO_NORMAL, 2000u, &result) == 0) {
        std::cerr << "[Renderer] display change: the window did not respond\n";
        return;
    }
    // The placed window's client size (WM_SIZE can lag behind the
    // cross-thread placement).
    RECT client{};
    if (GetClientRect(window_, &client) && client.right > 0 && client.bottom > 0) {
        resize_swap_chain(static_cast<UINT>(client.right),
                          static_cast<UINT>(client.bottom), false);
    }
    applied.valid = true;
    applied.mode = mode;
    applied.monitor = cfg.monitor;
    applied.window_width = cfg.window_width;
    applied.window_height = cfg.window_height;
    std::cout << "[Renderer] display " << (mode == kDisplayWindowed ? "windowed" : "borderless")
              << ' ' << narrow_utf8(monitor->device) << ' ' << host_window::backbuffer_width << 'x'
              << host_window::backbuffer_height << '\n';
}

void RendererD3D12::pump_messages() {
    // The window and its modal loops belong to window_owner_.
}

bool RendererD3D12::quit_requested() const noexcept {
    return host_window::quit_request.load(std::memory_order_acquire);
}

void RendererD3D12::invalidate_draw_bindings() {
    encoder_.reset();
}

void RendererD3D12::set_render_targets(ID3D12GraphicsCommandList* list, unsigned aux) const {
    if (aux == 3u) {
        const D3D12_CPU_DESCRIPTOR_HANDLE targets[4]{scene_rtv_, reactive_rtv_, layer_color_rtv_, layer_trans_rtv_};
        list->OMSetRenderTargets(4, targets, FALSE, &scene_dsv_);
    } else if (aux != 0u) {
        const D3D12_CPU_DESCRIPTOR_HANDLE targets[2]{scene_rtv_, aux == 1u ? motion_rtv_ : reactive_rtv_};
        list->OMSetRenderTargets(2, targets, FALSE, &scene_dsv_);
    } else {
        list->OMSetRenderTargets(1, &scene_rtv_, FALSE, &scene_dsv_);
    }
}

void RendererD3D12::set_draw_aux(unsigned aux) {
    if (aux == bound_rt_aux_) {
        return;
    }
    set_render_targets(command_list_.Get(), aux);
    bound_rt_aux_ = static_cast<std::uint8_t>(aux);
}

gpu::TextureArrays RendererD3D12::draw_texture_arrays(gpu::Address lists) {
    if (mirrored_backend_ == nullptr) {
        throw std::runtime_error("[textures] a draw before the texture heap was made");
    }
    gpu::TextureArrays arrays;
    arrays.textures = srv_ring_.texture_gpu_start();
    arrays.samplers = sampler_heap_->GetGPUDescriptorHandleForHeapStart();
    arrays.lists = lists;
    arrays.heap = srv_ring_.heap();
    return arrays;
}

namespace {
// The heaps a list of draws reads: the texture heap and the sampler heap.
void set_draw_heaps(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* textures, ID3D12DescriptorHeap* samplers) {
    ID3D12DescriptorHeap* heaps[] = {textures, samplers};
    list->SetDescriptorHeaps(samplers != nullptr ? 2u : 1u, heaps);
}
}  // namespace

// A chunk of a recorded pass replayed on the frame's own list, as the walker would have drawn it.
void RendererD3D12::replay_draws(std::uint8_t aux, const gpu::DrawOp* ops, std::size_t count) {
    set_draw_aux(aux);
    gpu::replay_ops(encoder_, ops, count, [this](const gpu::TextureArrays& arrays) {
        ID3D12DescriptorHeap* const heap = const_cast<ID3D12DescriptorHeap*>(static_cast<const ID3D12DescriptorHeap*>(arrays.heap));
        if (heap == srv_ring_.heap()) {
            srv_ring_.bind(command_list_.Get());
        } else {
            // The heap was replaced while the pass was walked: the tables are the old one's.
            set_draw_heaps(command_list_.Get(), heap, sampler_heap_.Get());
            srv_ring_.forget_bound();
        }
        encoder_.set_texture_arrays(arrays.textures, arrays.samplers, arrays.lists);
    });
}

bool RendererD3D12::parallel_record_begin(const std::uint8_t* chunk_aux, unsigned chunk_count) {
    constexpr unsigned kMaxRecordLists = 256u;
    std::vector<RecordList>& lists = record_lists_[frame_slot_];
    // A pipeline statistics query begins and ends on one list: the diagnostics that ask for it keep the pass on one.
    if (device_gone_ || chunk_count == 0u || stats_query_heap_ || record_used_[frame_slot_] + chunk_count > kMaxRecordLists) {
        return false;
    }
    record_base_ = record_used_[frame_slot_];
    while (lists.size() < record_base_ + chunk_count) {
        RecordList made;
        d3d_check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&made.allocator)),
                  "CreateCommandAllocator (chunk)");
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, made.allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&made.list)),
                  "CreateCommandList (chunk)");
        d3d_check(made.list->Close(), "CommandList Close (chunk)");
        lists.push_back(std::move(made));
    }
    record_used_[frame_slot_] += chunk_count;
    record_aux_.assign(chunk_aux, chunk_aux + chunk_count);
    // What the frame's list holds so far runs first.
    const HRESULT close_hr = command_list_->Close();
    if (FAILED(close_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(close_hr, "CommandList Close (before a recorded pass)");
    }
    frame_lists_.push_back(command_list_.Get());
    return true;
}

void RendererD3D12::parallel_record_chunk(unsigned chunk, const gpu::DrawOp* ops, std::size_t count) {
    RecordList& record = record_lists_[frame_slot_][record_base_ + chunk];
    ID3D12GraphicsCommandList* const list = record.list.Get();
    d3d_check(list->Reset(record.allocator.Get(), nullptr), "CommandList Reset (chunk)");
    // The state the frame's list had at the pass: its root signature, the draw targets of the chunk, and (below) the heaps.
    list->SetGraphicsRootSignature(root_signature_.Get());
    set_render_targets(list, record_aux_[chunk]);
    gpu::Encoder encoder;
    encoder.attach(list);
    ID3D12DescriptorHeap* heap = nullptr;
    gpu::replay_ops(encoder, ops, count, [&](const gpu::TextureArrays& arrays) {
        ID3D12DescriptorHeap* const wanted = const_cast<ID3D12DescriptorHeap*>(static_cast<const ID3D12DescriptorHeap*>(arrays.heap));
        if (wanted != heap) {
            set_draw_heaps(list, wanted, sampler_heap_.Get());
            heap = wanted;
        }
        encoder.set_texture_arrays(arrays.textures, arrays.samplers, arrays.lists);
    });
    d3d_check(list->Close(), "CommandList Close (chunk)");
}

void RendererD3D12::parallel_record_end() {
    const std::vector<RecordList>& lists = record_lists_[frame_slot_];
    for (std::size_t c = 0; c < record_aux_.size(); ++c) {
        frame_lists_.push_back(lists[record_base_ + c].list.Get());
    }
    // The frame goes on in another list: opened on the frame's allocator after the one just closed.
    if (segment_lists_.size() <= segments_used_) {
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> made;
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[frame_slot_].Get(), nullptr,
                                             IID_PPV_ARGS(&made)),
                  "CreateCommandList (frame continues)");
        d3d_check(made->Close(), "CommandList Close (frame continues)");
        segment_lists_.push_back(std::move(made));
    }
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& next = segment_lists_[segments_used_++];
    d3d_check(next->Reset(allocators_[frame_slot_].Get(), nullptr), "CommandList Reset (frame continues)");
    std::swap(command_list_, next);  // `next` now keeps the closed list alive until the frame is submitted
    const unsigned last_aux = record_aux_.empty() ? 0u : record_aux_.back();
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    set_render_targets(command_list_.Get(), last_aux);
    bound_rt_aux_ = static_cast<std::uint8_t>(last_aux);
    srv_ring_.forget_bound();
    encoder_.attach(command_list_.Get());
    if (mirrored_store_ != nullptr) {
        mirrored_store_->backend().set_upload_list(command_list_.Get());
    }
}

gpu::Address RendererD3D12::zero_stream(std::uint32_t bytes) {
    if (bytes > zero_stream_bytes_) {
        // Frames in flight read the old one: it goes with them.
        if (zero_stream_.buffer) {
            retired_targets_[frame_slot_].push_back(std::move(zero_stream_.buffer));
        }
        zero_stream_bytes_ = std::max(bytes, 1u << 16u);
        zero_stream_ = gpu::create_upload_memory(device_.Get(), zero_stream_bytes_, "zero stream");
        std::memset(zero_stream_.cpu, 0, zero_stream_bytes_);
    }
    return zero_stream_.gpu;
}

namespace {
constexpr const char* kPassTimeNames[] = {"scene", "resolve", "frame read", "display copy", "bloom", "scene aa", "scene ao",
                                          "upscale", "ray tracing", "present", "camera"};
}  // namespace

std::size_t RendererD3D12::gpu_pass_times(GpuPassTime* out, std::size_t capacity) const noexcept {
    std::size_t count = 0u;
    for (std::size_t pass = 0u; pass < std::size(kPassTimeNames) && count < capacity; ++pass) {
        if (const float ms = pass_ema_ms_[pass].load(std::memory_order_relaxed); ms > 0.0f) {
            out[count++] = {kPassTimeNames[pass], ms};
        }
    }
    return count;
}

// The pass timestamps' query heap and readback, made when the settings page asks for them (the diagnostics make theirs at start).
void RendererD3D12::ensure_pass_queries() {
    if (pass_query_heap_ || !device_) {
        return;
    }
    D3D12_QUERY_HEAP_DESC qhd{};
    qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    qhd.Count = kFramesInFlight * kGpuPassSlots;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = kFramesInFlight * kGpuPassSlots * sizeof(std::uint64_t);
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    if (FAILED(device_->CreateQueryHeap(&qhd, IID_PPV_ARGS(&heap))) ||
        FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                IID_PPV_ARGS(&readback)))) {
        gpu_timing_wanted_.store(false, std::memory_order_relaxed);  // this device cannot: the page shows it off
        return;
    }
    pass_readback_ = std::move(readback);
    pass_query_heap_ = std::move(heap);
}

void RendererD3D12::gpu_pass(GpuPass pass) {
    if (pass == current_pass_) {
        return;
    }
    current_pass_ = pass;
    // The pass by name for PIX and RenderDoc.
    if (pass_list_open_ && pass != GpuPass::Count) {
        const char* const name = kPassTimeNames[static_cast<std::size_t>(pass)];
        command_list_->SetMarker(1u, name, static_cast<UINT>(std::strlen(name) + 1u));
    }
    unsigned& marks = pass_mark_count_[frame_slot_];
    if (!pass_timing_on_ || !pass_query_heap_ || !pass_list_open_ || marks + 1u >= kGpuPassSlots) {
        return;
    }
    command_list_->EndQuery(pass_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_slot_ * kGpuPassSlots + marks);
    pass_marks_[frame_slot_][marks++] = pass;
}

void RendererD3D12::lose_device() {
    ComPtr<ID3D12Device5> device;
    if (SUCCEEDED(device_.As(&device))) {
        device->RemoveDevice();
    }
}

void RendererD3D12::begin_frame() {
    scene_drawn_ = false;
    // Display and size changes apply here, between command-list recordings.
    // They wait for the GPU themselves, so they come before any per-slot state
    // below is touched.
    host_window::poll_render_config_file();
    reconcile_display_state();
    apply_pending_resize();
    if (const bool fsr = fsr_generation_wanted(); fsr != fsr_swap_chain_ && !streamline_frame_open_) {
        recreate_swap_chain(fsr);
    }
    if (!streamline_frame_open_) {
        // Frame generation runs with DLSS on a GPU that runs it, or with AMD's swap chain, while it is chosen.
        const RenderConfig fg_cfg = get_render_config();
        set_frame_generation_running(fsr_swap_chain_ || (streamline::frame_generation_supported() && fg_cfg.frame_generation == 1u &&
                                                         fg_cfg.upscaler == 3u && streamline::dlss_supported()));
        if (frame_generation_running()) {
            streamline::reflex_sleep();
        }
        streamline::marker(streamline::Marker::kSimulationStart);
        streamline_frame_open_ = true;
    }
    reconcile_hdr_output();
    apply_window_size();
    prepare_temporal_upscaler();
    sync_post_process_settings();
    {
        if (temporal_interp_suspended_ && temporal_interp_key() != temporal_interp_key_) {
            // The settings changed: decided again.
            temporal_interp_suspended_ = false;
        }
        // A ring whose frames took over half their budget (or spilled) grows before the next frame; one whose last 600 frames
        // stayed under a quarter of it gives half of its memory back.
        if (!ring_growth_failed_ && (vertex_ring_.wants_growth() || index_ring_.wants_growth() || constant_ring_.wants_growth() ||
                                     matrix_ring_.wants_growth() || vertex_ring_.wants_shrink() || index_ring_.wants_shrink() ||
                                     constant_ring_.wants_shrink() || matrix_ring_.wants_shrink())) {
            // No wait for the GPU: a ring is made again as a new buffer (the same size when only its segments change), and the old
            // one and its chunks go to the newest frame in flight's retired list, released after that frame's fence, which is
            // past every earlier frame's.
            std::vector<gpu::BufferRef>& old = retired_targets_[(frame_index_ + kFramesInFlight - 1u) % kFramesInFlight];
            const auto place = [&](UploadRing& ring, const char* name, unsigned count, bool chunks) {
                const std::size_t had = ring.capacity();
                std::size_t capacity = ring_growth_failed_ ? had : ring.grown_capacity(count);
                if (capacity == 0u) {
                    ring_growth_failed_ = true;
                    capacity = had;
                } else if (ring.wants_shrink()) {
                    capacity = ring.shrunk_capacity(count);
                }
                if (capacity != had) {
                    std::cerr << "[gpu-rings] " << (capacity > had ? "grow " : "shrink ") << name
                              << " frame-high-water-kb=" << (ring.high_water() >> 10u)
                              << " budget-kb=" << (ring.bytes_per_frame() >> 10u) << " mb=" << (had >> 20u) << "->"
                              << (capacity >> 20u) << '\n';
                }
                ring.release_into(old);
                if (ring.initialize(device_.Get(), name, UploadRing::budget(capacity, count), count, capacity, chunks)) {
                    return true;
                }
                // Out of upload memory: the size it had, and no further attempts.
                ring_growth_failed_ = true;
                std::cerr << "[gpu-rings] grow " << name << " failed\n";
                return ring.initialize(device_.Get(), name, UploadRing::budget(had, count), count, had, chunks);
            };
            if (!place(vertex_ring_, "vertex", kRingSegments, true) || !place(index_ring_, "index", kFramesInFlight, true) ||
                !place(constant_ring_, "constant", kFramesInFlight, true) || !place(matrix_ring_, "matrix", kRingSegments, false)) {
                throw std::runtime_error("ring resize failed");
            }
            history_segment_ = 0u;
            history_segment_fence_.fill(0u);
            history_used_ = true;
        }
    }
    frame_slot_ = static_cast<unsigned>(frame_index_ % kFramesInFlight);
    // Wait only on the slot about to be reused.
    const std::uint64_t wait_val = fence_values_[frame_slot_];
    if (wait_val != 0u) {
        const bool timeline_slot_wait =
            galaxy::timeline::enabled() && completed_fence_value() < wait_val;
        if (timeline_slot_wait) {
            galaxy::timeline::record(galaxy::timeline::Event::SlotWaitBegin, wait_val);
        }
        wait_for_fence_value(wait_val);
        if (timeline_slot_wait) {
            galaxy::timeline::record(galaxy::timeline::Event::SlotWaitEnd, wait_val);
        }
    }

    if (timestamp_pending_[frame_slot_] && timestamp_readback_ &&
        timestamp_frequency_ != 0) {
        const SIZE_T offset =
            static_cast<SIZE_T>(frame_slot_) * 2u * sizeof(std::uint64_t);
        const D3D12_RANGE read_range{
            offset, offset + 2u * sizeof(std::uint64_t)};
        void* mapped = nullptr;
        if (SUCCEEDED(timestamp_readback_->Map(0, &read_range, &mapped))) {
            const auto* timestamps =
                reinterpret_cast<const std::uint64_t*>(
                    static_cast<const std::byte*>(mapped) + offset);
            if (galaxy::timeline::enabled() && timestamps[1] >= timestamps[0]) {
                // GPU ticks map onto QPC through a calibration pair refreshed
                // every 60 frames; the two clocks drift by parts per million.
                static std::uint64_t calibration_gpu = 0u;
                static std::uint64_t calibration_qpc = 0u;
                static unsigned calibration_age = 0u;
                if (calibration_qpc == 0u || ++calibration_age >= 60u) {
                    UINT64 gpu = 0u;
                    UINT64 cpu = 0u;
                    if (SUCCEEDED(queue_->GetClockCalibration(&gpu, &cpu))) {
                        calibration_gpu = gpu;
                        calibration_qpc = cpu;
                        calibration_age = 0u;
                    }
                }
                if (calibration_qpc != 0u && qpc_frequency_.QuadPart > 0) {
                    const double scale =
                        static_cast<double>(qpc_frequency_.QuadPart) /
                        static_cast<double>(timestamp_frequency_);
                    const auto to_qpc = [&](std::uint64_t gpu_ticks) {
                        const double delta = static_cast<double>(
                            static_cast<std::int64_t>(gpu_ticks - calibration_gpu));
                        return static_cast<std::uint64_t>(
                            static_cast<std::int64_t>(calibration_qpc) +
                            static_cast<std::int64_t>(delta * scale));
                    };
                    galaxy::timeline::record_at(galaxy::timeline::Event::GpuBegin,
                                            to_qpc(timestamps[0]), wait_val);
                    galaxy::timeline::record_at(galaxy::timeline::Event::GpuEnd,
                                            to_qpc(timestamps[1]), wait_val);
                }
            }
            if (timestamps[1] >= timestamps[0]) {
                const double gpu_ms =
                    static_cast<double>(timestamps[1] - timestamps[0]) *
                    1000.0 / static_cast<double>(timestamp_frequency_);
                telemetry_gpu_ms_ += gpu_ms;
                if (frame_scene_drawn_[frame_slot_]) {
                    native_scene_gpu_ema_ms_ = native_scene_gpu_ema_ms_ == 0.0 ? gpu_ms :
                        native_scene_gpu_ema_ms_ + (gpu_ms - native_scene_gpu_ema_ms_) * 0.1;
                }
                telemetry_gpu_by_place_[timestamp_list_place_[frame_slot_]] += gpu_ms;
                if (frame_generated_[frame_slot_]) {
                    generated_gpu_ms_ += gpu_ms;
                    ++generated_gpu_samples_;
                    generated_gpu_ema_ms_ = generated_gpu_ema_ms_ == 0.0 ? gpu_ms : generated_gpu_ema_ms_ + (gpu_ms - generated_gpu_ema_ms_) * 0.1;
                }
                ++telemetry_gpu_by_place_count_[timestamp_list_place_[frame_slot_]];
                ++telemetry_gpu_samples_;
            }
            const D3D12_RANGE no_write{0, 0};
            timestamp_readback_->Unmap(0, &no_write);
        }
        timestamp_pending_[frame_slot_] = false;
    }
    if (pass_readback_ && pass_mark_count_[frame_slot_] > 1u) {
        const unsigned marks = pass_mark_count_[frame_slot_];
        const SIZE_T offset = static_cast<SIZE_T>(frame_slot_) * kGpuPassSlots * sizeof(std::uint64_t);
        const D3D12_RANGE read_range{offset, offset + marks * sizeof(std::uint64_t)};
        void* mapped = nullptr;
        if (SUCCEEDED(pass_readback_->Map(0, &read_range, &mapped))) {
            const auto* ticks = reinterpret_cast<const std::uint64_t*>(static_cast<const std::byte*>(mapped) + offset);
            std::array<double, static_cast<std::size_t>(GpuPass::Count)> frame_ms{};
            for (unsigned i = 0; i + 1u < marks; ++i) {
                if (ticks[i + 1u] >= ticks[i]) {
                    const double ms = static_cast<double>(ticks[i + 1u] - ticks[i]) * 1000.0 /
                                      static_cast<double>(timestamp_frequency_);
                    telemetry_pass_ms_[static_cast<std::size_t>(pass_marks_[frame_slot_][i])] += ms;
                    frame_ms[static_cast<std::size_t>(pass_marks_[frame_slot_][i])] += ms;
                }
            }
            // The page's running average of each pass over the frames that ran it.
            for (std::size_t pass = 0u; pass < frame_ms.size(); ++pass) {
                if (frame_ms[pass] > 0.0) {
                    const float before = pass_ema_ms_[pass].load(std::memory_order_relaxed);
                    const float value = static_cast<float>(frame_ms[pass]);
                    pass_ema_ms_[pass].store(before == 0.0f ? value : before + (value - before) * 0.1f, std::memory_order_relaxed);
                }
            }
            const D3D12_RANGE no_write{0, 0};
            pass_readback_->Unmap(0, &no_write);
        }
    }
    pass_mark_count_[frame_slot_] = 0u;
    if (stats_pending_[frame_slot_]) {
        const SIZE_T offset = static_cast<SIZE_T>(frame_slot_) * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
        const D3D12_RANGE read_range{offset, offset + sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS)};
        void* mapped = nullptr;
        if (SUCCEEDED(stats_readback_->Map(0, &read_range, &mapped))) {
            const auto& s = *reinterpret_cast<const D3D12_QUERY_DATA_PIPELINE_STATISTICS*>(
                static_cast<const std::byte*>(mapped) + offset);
            telemetry_stats_.IAVertices += s.IAVertices;
            telemetry_stats_.IAPrimitives += s.IAPrimitives;
            telemetry_stats_.VSInvocations += s.VSInvocations;
            telemetry_stats_.CInvocations += s.CInvocations;
            telemetry_stats_.CPrimitives += s.CPrimitives;
            telemetry_stats_.PSInvocations += s.PSInvocations;
            telemetry_stats_.HSInvocations += s.HSInvocations;
            telemetry_stats_.DSInvocations += s.DSInvocations;
            telemetry_stats_.CSInvocations += s.CSInvocations;
            const D3D12_RANGE no_write{0, 0};
            stats_readback_->Unmap(0, &no_write);
        }
        stats_pending_[frame_slot_] = false;
    }
    const HRESULT allocator_reset_hr = allocators_[frame_slot_]->Reset();
    if (FAILED(allocator_reset_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(allocator_reset_hr, "CommandAllocator Reset");
    }
    for (RecordList& record : record_lists_[frame_slot_]) {
        d3d_check(record.allocator->Reset(), "CommandAllocator Reset (chunk)");
    }
    record_used_[frame_slot_] = 0u;
    segments_used_ = 0u;
    frame_lists_.clear();
    const HRESULT command_reset_hr =
        command_list_->Reset(allocators_[frame_slot_].Get(), nullptr);
    if (FAILED(command_reset_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(command_reset_hr, "CommandList Reset");
    }
    if (timestamp_query_heap_) {
        command_list_->EndQuery(
            timestamp_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            frame_slot_ * 2u);
    }
    {
        // The page's timing is on for the frames begun while it is asked for (nothing is recorded or read otherwise).
        const bool timing = diagnostic_passes_ || gpu_timing_wanted_.load(std::memory_order_relaxed);
        if (timing && !pass_query_heap_) {
            ensure_pass_queries();
        }
        if (!timing && pass_timing_on_) {
            for (std::atomic<float>& average : pass_ema_ms_) {
                average.store(0.0f, std::memory_order_relaxed);
            }
        }
        pass_timing_on_ = timing && pass_query_heap_;
    }
    pass_list_open_ = true;
    if (stats_query_heap_) {
        command_list_->BeginQuery(stats_query_heap_.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, frame_slot_);
    }
    {
        const GpuPass pass = current_pass_;
        current_pass_ = GpuPass::Count;
        gpu_pass(pass);
    }

    // A frame that wrote neither keeps appending, so the last walk's stay readable across present-only frames.
    if (history_used_) {
        history_segment_ = (history_segment_ + 1u) % kRingSegments;
        if (history_segment_fence_[history_segment_] != 0u) {
            wait_for_fence_value(history_segment_fence_[history_segment_]);
        }
        vertex_ring_.begin_frame(history_segment_);
        matrix_ring_.begin_frame(history_segment_);
    }
    history_begin_vertex_ = vertex_ring_.cursor();
    history_begin_matrix_ = matrix_ring_.cursor();
    index_ring_.begin_frame(frame_slot_);
    constant_ring_.begin_frame(frame_slot_);
    srv_ring_.begin_frame(frame_slot_);
    texture_lists_used_ = 0u;
    {
        const std::uint64_t completed = completed_fence_value();
        std::erase_if(retired_texture_lists_, [&](const RetiredBuffer& retired) { return retired.fence <= completed; });
        std::erase_if(retired_heaps_, [&](const RetiredHeap& retired) { return retired.fence <= completed; });
    }
    reset_sampler_frame();
    bound_rt_aux_ = 0u;
    retired_targets_[frame_slot_].clear();
    game_bloom_.trim_idle(frame_index_, kFramesInFlight);
    ambient_occlusion_.trim_idle(frame_index_, kFramesInFlight);
    trim_idle_effect_targets();
    if (frame_index_ % 120u == 0u) {
        sweep_target_slots();
    }

    // Bind both heaps for the entire frame: the one list-wide SetDescriptorHeaps unless a heap is replaced.
    srv_ring_.bind(command_list_.Get());
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    encoder_.attach(command_list_.Get());
}

bool RendererD3D12::vblank_grid(std::int64_t& vblank, std::int64_t& period) const noexcept {
    return window_ != nullptr && host_window::vblank_grid(window_, vblank, period);
}

void RendererD3D12::refresh_display_rate() {
    // The mode's rate (whole hertz: 239 or 240 for 239.76), and the vblanks' own when they are timed.
    double mode_hz = 0.0;
    if (swap_chain_ != nullptr) {
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        DXGI_OUTPUT_DESC desc{};
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (SUCCEEDED(swap_chain_->GetContainingOutput(&output)) && output != nullptr && SUCCEEDED(output->GetDesc(&desc)) &&
            EnumDisplaySettingsW(desc.DeviceName, ENUM_CURRENT_SETTINGS, &mode) != FALSE && mode.dmDisplayFrequency > 1u) {
            mode_hz = static_cast<double>(mode.dmDisplayFrequency);
        }
    }
    std::int64_t vblank = 0;
    std::int64_t period = 0;
    const bool timed = vblank_grid(vblank, period);
    const double measured_hz =
        timed ? std::round(static_cast<double>(qpc_frequency_.QuadPart) / static_cast<double>(period) * 100.0) / 100.0 : 0.0;
    // A flip-model chain that may tear is what a variable refresh display needs to engage (DXGI_FEATURE_PRESENT_ALLOW_TEARING).
    const bool variable = tearing_allowed_ && next_variable_refresh(variable_refresh_, mode_hz, measured_hz);
    if (variable != variable_refresh_ || (variable && std::abs(variable_refresh_cap(mode_hz) - display_refresh_hz_) >= 0.1)) {
        if (variable) {
            std::cerr << "[pacing] vrr " << mode_hz << " Hz cap " << variable_refresh_cap(mode_hz) << '\n';
        } else {
            std::cerr << "[pacing] vrr off\n";
        }
    }
    variable_refresh_ = variable;
    const double hz = variable ? variable_refresh_cap(mode_hz) : (timed ? measured_hz : mode_hz);
    if (hz <= 1.0) {
        return;
    }
    if (!variable && std::abs(hz - display_refresh_hz_) >= 0.1) {
        std::cerr << "[display] refresh-hz=" << hz << (timed ? " (vblanks)\n" : "\n");
    }
    display_refresh_hz_ = hz;
}

// The output the window is on, read at the first frame and again after a move to another monitor, a density change or a
// display mode change (Windows turning HDR on or off for the output is one): its colour space (HDR10 when Windows has HDR on
// for it), bits and luminances, through a new factory (read_output_hdr), so the HDR state is the current one. The refresh rate,
// which sets the pacing period, is read by refresh_display_rate right after. The change is logged once and published
// (publish_display_hdr); reconcile_hdr_output switches the swap chain at the next frame boundary.
void RendererD3D12::read_display_output() {
    if (swap_chain_ == nullptr || window_ == nullptr) {
        return;
    }
    const OutputHdr now = read_output_hdr(window_);
    if (!display_read_ || !(now == display_reading_)) {
        hdr_failed_ = false;  // another display, or its HDR mode changed: a switch that failed may work now
        if (now.known) {
            std::cerr << "[display] output " << (now.hdr ? "HDR" : "SDR") << ", colour space " << now.colour_space << ", " << now.bits
                      << " bits, peak " << now.peak_nits << " nits, full frame " << now.full_frame_nits << " nits, black "
                      << static_cast<double>(now.min_nits_10000) / 10000.0 << " nits" << '\n';
        } else {
            std::cerr << "[display] output not described by DXGI: taken as SDR\n";
        }
    }
    display_reading_ = now;
    display_read_ = true;
    publish_display_hdr();
}

// What the settings page and the logs know of HDR: the display is in HDR mode and the post chain, which writes the last
// pass, is here to encode for it.
void RendererD3D12::publish_display_hdr() {
    DisplayHdr hdr{};
    hdr.available = display_reading_.hdr && swap_chain_ != nullptr && post_process_.ready();
    hdr.active = hdr_active_;
    hdr.peak_nits = display_reading_.known && display_reading_.hdr ? display_reading_.peak_nits : 0u;
    hdr.full_frame_nits = display_reading_.known && display_reading_.hdr ? display_reading_.full_frame_nits : 0u;
    hdr.min_nits_10000 = display_reading_.known && display_reading_.hdr ? display_reading_.min_nits_10000 : 0u;
    set_display_hdr(hdr);
}

// Frame boundary: the swap chain is HDR10 while HDR is on in the settings, the display is in HDR mode and nothing needs the
// 8-bit chain (AMD's frame generation chain and DLSS frame generation take the picture as SDR). The paper white and the
// display's peak only change the last write's constants (and the metadata), not the chain.
void RendererD3D12::reconcile_hdr_output() {
    if (swap_chain_ == nullptr || rtv_heap_ == nullptr) {
        return;
    }
    if (!display_read_) {
        read_display_output();
    }
    const RenderConfig cfg = get_render_config();
    const DisplayHdr display = get_display_hdr();
    const bool wanted = hdr_output_wanted(cfg, display) && !fsr_swap_chain_ && !frame_generation_running() && post_process_.ready();
    const PostOutput levels = post_output_for(cfg, display);
    if (!wanted) {
        hdr_failed_ = false;
    }
    if (wanted != hdr_active_) {
        if ((wanted && hdr_failed_) || !set_swap_chain_hdr(wanted, levels)) {
            return;
        }
    } else if (wanted && (levels.paper_white_nits != hdr_output_.paper_white_nits || levels.peak_nits != hdr_output_.peak_nits)) {
        apply_hdr_metadata(levels);
        hdr_output_ = levels;
        std::cerr << "[hdr] on peak " << levels.peak_nits << " nits paper " << levels.paper_white_nits << '\n';
    }
    if (display.active != hdr_active_) {
        publish_display_hdr();
    }
}

bool RendererD3D12::set_swap_chain_hdr(bool hdr, const PostOutput& levels) {
    Microsoft::WRL::ComPtr<IDXGISwapChain3> chain3;
    DXGI_SWAP_CHAIN_DESC1 current{};
    if (FAILED(swap_chain_.As(&chain3)) || FAILED(swap_chain_->GetDesc1(&current))) {
        return false;
    }
    if (hdr) {
        // The output must take the colour space for presenting, and the post chain's last writes exist for the new format.
        UINT support = 0u;
        if (FAILED(chain3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &support)) ||
            (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0u) {
            std::cerr << "[hdr] the swap chain cannot present HDR10 here: staying SDR\n";
            hdr_output_ = PostOutput{};
            hdr_failed_ = true;
            return false;
        }
        if (!post_process_.prepare_hdr()) {
            hdr_failed_ = true;
            return false;
        }
    }
    wait_for_gpu();
    for (auto& buffer : backbuffers_) {
        buffer.Reset();
    }
    const HRESULT resized = swap_chain_->ResizeBuffers(kSwapChainBuffers, current.Width, current.Height,
                                                       hdr ? DXGI_FORMAT_R10G10B10A2_UNORM : kBackbufferFormat, current.Flags);
    if (FAILED(resized)) {
        // The buffers are released already: this is as fatal as a failed resize.
        dump_device_removal(device_.Get());
        d3d_check(resized, "ResizeBuffers (HDR)");
        return false;
    }
    const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (unsigned i = 0; i < kSwapChainBuffers; ++i) {
        d3d_check(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&backbuffers_[i])), "HDR GetBuffer");
        device_->CreateRenderTargetView(backbuffers_[i].Get(), nullptr, h);
        h.ptr += stride;
    }
    if (FAILED(chain3->SetColorSpace1(hdr ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709))) {
        std::cerr << "[hdr] SetColorSpace1 failed\n";
    }
    if (hdr) {
        apply_hdr_metadata(levels);
    } else {
        (void)swap_chain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0u, nullptr);
    }
    hdr_active_ = hdr;
    hdr_output_ = hdr ? levels : PostOutput{};
    if (hdr) {
        std::cerr << "[hdr] on peak " << levels.peak_nits << " nits paper " << levels.paper_white_nits << '\n';
    } else {
        std::cerr << "[hdr] off\n";
    }
    publish_display_hdr();
    return true;
}

void RendererD3D12::apply_hdr_metadata(const PostOutput& levels) {
    const HdrMetadata m = hdr10_metadata(get_display_hdr(), levels.paper_white_nits);
    const auto chromaticity = [](float value) { return static_cast<UINT16>(std::lround(value * 50000.0f)); };
    DXGI_HDR_METADATA_HDR10 meta{};
    meta.RedPrimary[0] = chromaticity(m.red[0]);
    meta.RedPrimary[1] = chromaticity(m.red[1]);
    meta.GreenPrimary[0] = chromaticity(m.green[0]);
    meta.GreenPrimary[1] = chromaticity(m.green[1]);
    meta.BluePrimary[0] = chromaticity(m.blue[0]);
    meta.BluePrimary[1] = chromaticity(m.blue[1]);
    meta.WhitePoint[0] = chromaticity(m.white[0]);
    meta.WhitePoint[1] = chromaticity(m.white[1]);
    meta.MaxMasteringLuminance = static_cast<UINT>(std::lround(m.max_luminance));
    meta.MinMasteringLuminance = static_cast<UINT>(std::lround(m.min_luminance * 10000.0f));
    meta.MaxContentLightLevel = static_cast<UINT16>(std::min(std::lround(m.max_content_light), 65535L));
    meta.MaxFrameAverageLightLevel = static_cast<UINT16>(std::min(std::lround(m.max_frame_average_light), 65535L));
    if (FAILED(swap_chain_->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(meta), &meta))) {
        std::cerr << "[hdr] SetHDRMetaData failed\n";
    }
}

// The frame's picture as the SDR window would show it, for a capture (screenshots and test pictures stay SDR whatever the
// swap chain is): the post chain's last write run again into an 8-bit target of the swap chain's size.
ID3D12Resource* RendererD3D12::hdr_capture_picture(const DisplayTexture& source, const D3D12_VIEWPORT& viewport) {
    hdr_capture_used_frame_ = frame_index_;
    if (!hdr_capture_target_ || hdr_capture_target_->GetDesc().Width != host_window::backbuffer_width ||
        hdr_capture_target_->GetDesc().Height != host_window::backbuffer_height) {
        if (hdr_capture_target_) {
            wait_for_gpu();
        }
        hdr_capture_target_.Reset();
        if (!hdr_capture_rtv_heap_) {
            D3D12_DESCRIPTOR_HEAP_DESC dhd{};
            dhd.NumDescriptors = 1;
            dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            if (FAILED(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&hdr_capture_rtv_heap_)))) {
                return nullptr;
            }
        }
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = host_window::backbuffer_width;
        rd.Height = host_window::backbuffer_height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = kBackbufferFormat;
        rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv{};
        cv.Format = kBackbufferFormat;
        cv.Color[3] = 1.0f;
        if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ColorWrite, &cv,
                                  IID_PPV_ARGS(&hdr_capture_target_)))) {
            return nullptr;
        }
        hdr_capture_target_->SetName(L"HDR capture (SDR picture)");
        device_->CreateRenderTargetView(hdr_capture_target_.Get(), nullptr, hdr_capture_rtv_heap_->GetCPUDescriptorHandleForHeapStart());
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = hdr_capture_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    constexpr float kBlack[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    command_list_->ClearRenderTargetView(rtv, kBlack, 0, nullptr);
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    srv_ring_.bind(command_list_.Get());
    post_process_.record(command_list_.Get(), srv_ring_, source.texture.Get(), source.width, source.height, rtv, viewport,
                         frame_index_, kFramesInFlight);
    invalidate_draw_bindings();
    return hdr_capture_target_.Get();
}

UINT RendererD3D12::present_sync_interval(unsigned configured, float max_fps) {
    const bool game_frame = std::exchange(present_game_frame_, false);
    if (configured != 1u || variable_refresh_) {
        return configured;  // a variable refresh display shows a frame at once: whole refreshes mean nothing
    }
    const double hz = display_refresh_hz_;
    if (hz <= 0.0) {
        return 1u;
    }
    // EnumDisplaySettings reports whole hertz: 119 for 119.88, 239 for 239.76. A game frame without generated frames
    // holds for its whole 60 Hz frame (4 refreshes at 240 Hz) rather than the next refresh after it is ready, which
    // drifts against the display.
    const double rate = game_frame ? std::min(kGameStepRate, present_frame_rate(max_fps, configured, hz))
                                   : present_frame_rate(max_fps, configured, hz);
    const double multiple = std::round(hz / rate);
    if (multiple >= 2.0 && multiple <= 4.0 &&
        std::abs(hz - rate * multiple) <= 0.015 * hz) {
        return static_cast<UINT>(multiple);
    }
    return 1u;
}

void RendererD3D12::present_to_swap_chain() {
    // A covered window (the last present said DXGI_STATUS_OCCLUDED): the frame is drawn and submitted as always, but nothing is
    // presented; the render thread's probe (probe_occlusion, every 100 ms) clears the state when the window shows again. Not
    // under frame generation, whose presents belong to its library.
    if (occluded_.load(std::memory_order_relaxed) && !frame_generation_running() && !fsr_swap_chain_) {
        present_game_frame_ = false;
        streamline_frame_open_ = false;
        return;
    }
    // Automatic-rate interpolation needs the display clock even with VSync and diagnostics off.
    // Also at once when the window has moved to another monitor (or its density or the display mode changed), whose rate,
    // colour space and HDR state may differ: read again, and only what differs is acted on.
    const bool display_moved = host_window::monitor_changed.exchange(false, std::memory_order_acq_rel);
    if (display_moved) {
        read_display_output();
    }
    if (display_moved || display_refresh_presents_++ % 120u == 0u) {
        refresh_display_rate();
    }
    const RenderConfig cfg = get_render_config();
    const bool generating = frame_generation_running();
    // With frame generation the sync interval is 0 or 1 (Streamline paces the generated frames).
    const UINT vsync = generating ? std::min(1u, cfg.vsync_interval)
                                  : present_sync_interval(std::min(2u, cfg.vsync_interval), cfg.max_fps);
    const UINT present_flags =
        vsync == 0u && tearing_allowed_ && !generating && !fsr_swap_chain_ ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    // Interpolation presents on its own slot clock.
    // On a variable refresh display the pacer holds presents under the cap whatever the VSync setting (display_refresh_hz_ is the
    // cap there); a frame under the cap goes out as soon as it is ready.
    const float variable_rate = variable_refresh_ ? static_cast<float>(display_refresh_hz_) : 0.0f;
    const float paced_fps = variable_refresh_ ? (presented_max_fps(cfg) > 0.0f ? std::min(presented_max_fps(cfg), variable_rate) : variable_rate)
                                               : presented_max_fps(cfg);
    const double pace_wait_ms =
        interpolation_mode() ? 0.0 : present_pacer_.pace(variable_refresh_ ? 0u : cfg.vsync_interval, paced_fps);
    if (trace_present_stats_enabled()) {
        telemetry_pace_wait_ms_ += pace_wait_ms;
        telemetry_pace_wait_max_ms_ = std::max(telemetry_pace_wait_max_ms_, pace_wait_ms);
    }
    const bool collect_present_telemetry = trace_present_stats_enabled();
    LARGE_INTEGER present_start{};
    LARGE_INTEGER present_end{};
    if (collect_present_telemetry) {
        QueryPerformanceCounter(&present_start);
    }
    static std::uint64_t timeline_present_ordinal = 0u;
    const bool timeline_present = galaxy::timeline::enabled();
    if (timeline_present) {
        galaxy::timeline::record(galaxy::timeline::Event::PresentBegin,
                             ++timeline_present_ordinal, vsync);
    }
    {
        // The target rate: dynamic multi frame generation reaches it, a fixed multiplier is the
        // largest multiple of 60 at or below it.
        // Only while the game shows play (no menu, window or full-screen page: render_config.h), as NVIDIA's guide
        // asks; otherwise its own frames alone.
        unsigned generated = 0u;
        if (generating && frame_generation_allowed()) {
            const double rate = present_frame_rate(cfg.max_fps, cfg.vsync_interval, display_refresh_hz_);
            generated = static_cast<unsigned>(std::clamp<long>(
                static_cast<long>(rate / kGameStepRate + 1e-3) - 1, 1, static_cast<long>(streamline::frame_generation_max())));
        }
        streamline::set_frame_generation(generated, streamline::frame_generation_dynamic(),
                                         cfg.max_fps > 0.0f ? cfg.max_fps : 0.0f);
    }
    if (fsr_swap_chain_) {
        fsr_generation_.configure(swap_chain_.Get(), generating && frame_generation_allowed(), fg_hudless_ready_ ? fg_hudless_target_.Get() : nullptr,
                                  fsr_rect_[0], fsr_rect_[1], fsr_rect_[2], fsr_rect_[3]);
        fg_hudless_ready_ = false;
    }
    streamline::marker(streamline::Marker::kPresentStart);
    // The display's own signal that a frame may be queued, awaited asleep (never a poll) and bounded, so a display that stopped
    // never holds the render thread. Not when presenting at once with no wait for the display (vsync off), and not on a chain
    // that frame generation replaced.
    if (latency_wait_ != nullptr && vsync != 0u && !generating && !fsr_swap_chain_ &&
        WaitForSingleObjectEx(latency_wait_, 100u, TRUE) == WAIT_TIMEOUT && ++latency_timeouts_ <= 4u) {
        std::cerr << "[present] the frame latency object did not signal within 100 ms\n";
    }
    const HRESULT present_hr = swap_chain_->Present(vsync, present_flags);
    if (generating && collect_present_telemetry) {
        // What frame generation sends to the display per frame here.
        static UINT last_count = 0u;
        static unsigned frames = 0u;
        if (++frames == 600u) {
            UINT count = 0u;
            if (SUCCEEDED(swap_chain_->GetLastPresentCount(&count))) {
                std::cerr << "[frame-generation] swap chain presents per frame " << static_cast<double>(count - last_count) / 600.0 << '\n';
                last_count = count;
            }
            frames = 0u;
        }
    }
    streamline::marker(streamline::Marker::kPresentEnd);
    streamline_frame_open_ = false;
    occluded_.store(present_hr == DXGI_STATUS_OCCLUDED, std::memory_order_relaxed);
    if (timeline_present) {
        galaxy::timeline::record(galaxy::timeline::Event::PresentEnd,
                             timeline_present_ordinal);
        // The OS's own record of which refresh showed which present.
        DXGI_FRAME_STATISTICS statistics{};
        // An occluded or not yet displayed swap chain reports a zero
        // sync time; that is no timeline instant.
        if (SUCCEEDED(swap_chain_->GetFrameStatistics(&statistics)) &&
            statistics.SyncQPCTime.QuadPart != 0) {
            galaxy::timeline::record_at(
                galaxy::timeline::Event::DisplayStats,
                static_cast<std::uint64_t>(statistics.SyncQPCTime.QuadPart),
                (static_cast<std::uint64_t>(statistics.PresentCount) << 32u) |
                    statistics.PresentRefreshCount,
                statistics.SyncRefreshCount);
        }
        if (timeline_present_ordinal % 30u == 1u) {
            using DwmTimingFn = HRESULT(WINAPI*)(HWND, DWM_TIMING_INFO*);
            static const DwmTimingFn dwm_timing = [] {
                const HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
                return dwm == nullptr ? nullptr
                    : reinterpret_cast<DwmTimingFn>(
                          GetProcAddress(dwm, "DwmGetCompositionTimingInfo"));
            }();
            DWM_TIMING_INFO timing{};
            timing.cbSize = sizeof(timing);
            if (dwm_timing != nullptr && SUCCEEDED(dwm_timing(nullptr, &timing)) &&
                timing.qpcVBlank != 0u) {
                galaxy::timeline::record_at(galaxy::timeline::Event::VBlank, timing.qpcVBlank,
                                        timing.qpcRefreshPeriod);
            }
        }
    }
    if (FAILED(present_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(present_hr, "Present");
    }
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        swap_present_qpc_ = now.QuadPart;
        ++measured_present_count_;
        if (measured_present_window_qpc_ == 0) {
            measured_present_window_qpc_ = now.QuadPart;
            measured_present_count_ = 0u;
        } else if (qpc_frequency_.QuadPart > 0 &&
                   now.QuadPart - measured_present_window_qpc_ >= qpc_frequency_.QuadPart) {
            const double hz = static_cast<double>(measured_present_count_) *
                static_cast<double>(qpc_frequency_.QuadPart) /
                static_cast<double>(now.QuadPart - measured_present_window_qpc_);
            measured_present_hz_.store(hz, std::memory_order_relaxed);
            host_window::measured_present_hz.store(hz, std::memory_order_relaxed);
            measured_present_window_qpc_ = now.QuadPart;
            measured_present_count_ = 0u;
        }
    }
    if (collect_present_telemetry) {
        QueryPerformanceCounter(&present_end);
        DXGI_FRAME_STATISTICS shown{};
        if (SUCCEEDED(swap_chain_->GetFrameStatistics(&shown)) &&
            shown.PresentCount != last_stats_present_count_) {
            if (last_stats_present_count_ != 0u &&
                shown.PresentCount == last_stats_present_count_ + 1u) {
                const UINT refreshes =
                    shown.PresentRefreshCount - last_stats_present_refresh_count_;
                ++telemetry_shown_refreshes_[std::min<UINT>(refreshes, 4u)];
            }
            last_stats_present_count_ = shown.PresentCount;
            last_stats_present_refresh_count_ = shown.PresentRefreshCount;
        }
    }

    if (collect_present_telemetry && qpc_frequency_.QuadPart != 0) {
        const double present_ms =
            static_cast<double>(
                present_end.QuadPart - present_start.QuadPart) *
            1000.0 / static_cast<double>(qpc_frequency_.QuadPart);
        telemetry_present_ms_ += present_ms;
        telemetry_present_max_ms_ =
            std::max(telemetry_present_max_ms_, present_ms);

        if (telemetry_window_start_qpc_ == 0) {
            telemetry_window_start_qpc_ = present_end.QuadPart;
            telemetry_new_pictures_start_ = display_present_stats().new_pictures_shown;
            telemetry_pictures_made_start_ = pictures_made();
        }
        if (last_present_qpc_ != 0) {
            const double interval_ms =
                static_cast<double>(
                    present_end.QuadPart - last_present_qpc_) *
                1000.0 / static_cast<double>(qpc_frequency_.QuadPart);
            telemetry_interval_ms_ += interval_ms;
            ++telemetry_interval_count_;
            telemetry_interval_max_ms_ =
                std::max(telemetry_interval_max_ms_, interval_ms);
        }
        last_present_qpc_ = present_end.QuadPart;
        ++telemetry_present_count_;

        const double elapsed_seconds =
            static_cast<double>(
                present_end.QuadPart - telemetry_window_start_qpc_) /
            static_cast<double>(qpc_frequency_.QuadPart);
        if (elapsed_seconds >= 2.0) {
            const auto progress = display_present_stats();
            const auto produced_copies = pictures_made();
            const double present_fps =
                static_cast<double>(telemetry_present_count_) /
                elapsed_seconds;
            const double interval_avg =
                telemetry_interval_count_ != 0u
                    ? telemetry_interval_ms_ /
                          static_cast<double>(telemetry_interval_count_)
                    : 0.0;
            const double present_avg =
                telemetry_present_ms_ /
                static_cast<double>(telemetry_present_count_);
            const double pace_wait_avg =
                telemetry_pace_wait_ms_ /
                static_cast<double>(telemetry_present_count_);
            const double gpu_avg =
                telemetry_gpu_samples_ != 0
                    ? telemetry_gpu_ms_ /
                          static_cast<double>(telemetry_gpu_samples_)
                    : 0.0;
            // cerr is unit-buffered: the record is assembled first so it is one write, not one per field.
            std::ostringstream message;
            message
                << "[present-stats] fps=" << present_fps
                << " new-picture-hz="
                << static_cast<double>(progress.new_pictures_shown - telemetry_new_pictures_start_) / elapsed_seconds
                << " pictures-made-hz="
                << static_cast<double>(produced_copies - telemetry_pictures_made_start_) / elapsed_seconds
                << " new-pictures-total=" << progress.new_pictures_shown
                << " pictures-made-total=" << produced_copies
                << " steady-ns=" << std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()
                << " frame-ms(avg/max)=" << interval_avg << "/"
                << telemetry_interval_max_ms_
                << " pace-wait-ms(avg/max)=" << pace_wait_avg << "/"
                << telemetry_pace_wait_max_ms_
                << " present-ms(avg/max)=" << present_avg << "/"
                << telemetry_present_max_ms_
                << " vsync=" << vsync
                << " present-flags=" << present_flags
                << " remote-session="
                << (GetSystemMetrics(SM_REMOTESESSION) != 0 ? 1 : 0)
                << " gpu-timing=" << (false ? "disabled" :
                    telemetry_gpu_samples_ == 0u ? "pending" : "measured")
                << " gpu-ms(avg)=";
            if (telemetry_gpu_samples_ != 0u) message << gpu_avg;
            else message << "unavailable";
            message
                << " gpu-samples=" << telemetry_gpu_samples_
                << " gpu-total-ms=" << telemetry_gpu_ms_
                << " gpu-by-list-ms=";
            for (unsigned place = 0; place < kGpuListPlaces; ++place) {
                message << (place == 0 ? "" : "/")
                        << (telemetry_gpu_by_place_count_[place] != 0u
                                ? telemetry_gpu_by_place_[place] / telemetry_gpu_by_place_count_[place]
                                : 0.0);
            }
            if (pass_query_heap_) {
                static constexpr const char* kPassNames[] = {"scene", "resolve", "frame-read", "display-copy", "bloom",
                                                             "scene-aa", "scene-ao", "upscale", "rt", "present",
                                                             "camera"};
                static_assert(std::size(kPassNames) == static_cast<std::size_t>(GpuPass::Count));
                message << " gpu-pass-ms=";
                const double presents = static_cast<double>(std::max<std::uint64_t>(telemetry_present_count_, 1u));
                for (std::size_t pass = 0; pass < std::size(kPassNames); ++pass) {
                    message << (pass == 0 ? "" : ",") << kPassNames[pass] << ':' << telemetry_pass_ms_[pass] / presents;
                }
                telemetry_pass_ms_.fill(0.0);
                const D3D12_QUERY_DATA_PIPELINE_STATISTICS& st = telemetry_stats_;
                message << " gpu-stats=vs:" << static_cast<double>(st.VSInvocations) / presents
                        << ",hs:" << static_cast<double>(st.HSInvocations) / presents
                        << ",ds:" << static_cast<double>(st.DSInvocations) / presents
                        << ",prims:" << static_cast<double>(st.CPrimitives) / presents
                        << ",ps:" << static_cast<double>(st.PSInvocations) / presents
                        << ",cs:" << static_cast<double>(st.CSInvocations) / presents;
                telemetry_stats_ = {};
            }
            message
                << " post-frames-total=" << post_process_.status().frames
                << " aa-wanted-frames-total=" << post_process_.status().aa_wanted_frames
                << " smaa-frames-total=" << post_process_.status().smaa_frames
                << " fxaa-frames-total=" << post_process_.status().fxaa_frames
                << " aa-skipped-frames-total=" << post_process_.status().aa_skipped_frames
                << " refresh-hz=" << display_refresh_hz_
                << " shown-refreshes(0/1/2/3/4+)="
                << telemetry_shown_refreshes_[0] << '/'
                << telemetry_shown_refreshes_[1] << '/'
                << telemetry_shown_refreshes_[2] << '/'
                << telemetry_shown_refreshes_[3] << '/'
                << telemetry_shown_refreshes_[4]
                << '\n';
            std::cerr << message.str();
            telemetry_shown_refreshes_ = {};

            telemetry_window_start_qpc_ = present_end.QuadPart;
            telemetry_new_pictures_start_ = progress.new_pictures_shown;
            telemetry_pictures_made_start_ = produced_copies;
            telemetry_present_count_ = 0;
            telemetry_interval_count_ = 0;
            telemetry_interval_ms_ = 0.0;
            telemetry_interval_max_ms_ = 0.0;
            telemetry_present_ms_ = 0.0;
            telemetry_present_max_ms_ = 0.0;
            telemetry_pace_wait_ms_ = 0.0;
            telemetry_pace_wait_max_ms_ = 0.0;
            telemetry_gpu_ms_ = 0.0;
            telemetry_gpu_by_place_.fill(0.0);
            telemetry_gpu_by_place_count_.fill(0u);
            telemetry_gpu_samples_ = 0;
        }
    }

    if (!window_shown_ && window_ != nullptr &&
        !host_window::hidden_window_mode_enabled()) {
        window_shown_ = true;
        ShowWindowAsync(window_, SW_SHOWNORMAL);
    }
}

std::uint64_t RendererD3D12::end_frame(
    bool present_swap_chain,
    SubmissionCallback on_submitted,
    void* submission_context,
    bool generated) {
    if (timestamp_query_heap_ && timestamp_readback_) {
        command_list_->EndQuery(
            timestamp_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            frame_slot_ * 2u + 1u);
        command_list_->ResolveQueryData(
            timestamp_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
            frame_slot_ * 2u, 2u, timestamp_readback_.Get(),
            static_cast<UINT64>(frame_slot_) * 2u * sizeof(std::uint64_t));
    }
    if (pass_query_heap_ && pass_mark_count_[frame_slot_] != 0u) {
        unsigned& marks = pass_mark_count_[frame_slot_];
        command_list_->EndQuery(pass_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                frame_slot_ * kGpuPassSlots + marks);
        pass_marks_[frame_slot_][marks++] = GpuPass::Count;
        command_list_->ResolveQueryData(pass_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                        frame_slot_ * kGpuPassSlots, marks, pass_readback_.Get(),
                                        static_cast<UINT64>(frame_slot_) * kGpuPassSlots * sizeof(std::uint64_t));
    }
    pass_list_open_ = false;
    if (stats_query_heap_) {
        command_list_->EndQuery(stats_query_heap_.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, frame_slot_);
        command_list_->ResolveQueryData(stats_query_heap_.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, frame_slot_, 1u,
                                        stats_readback_.Get(),
                                        static_cast<UINT64>(frame_slot_) *
                                            sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
        stats_pending_[frame_slot_] = true;
    }
    // The frame's tables go into the heap before the GPU can read them.
    srv_ring_.flush();
    // Heaps the descriptor ring replaced while it grew: freed once this frame's submission has completed.
    for (Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>& replaced : srv_ring_.take_replaced()) {
        retired_heaps_.push_back({std::move(replaced), next_fence_value_ + 1u});
    }
    const HRESULT close_hr = command_list_->Close();
    if (FAILED(close_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(close_hr, "CommandList Close");
    }
    // The textures the copy queue wrote and this frame names are acquired first (the queue waits for the copy fence).
    submit_lists_.clear();
    if (mirrored_store_ != nullptr) {
        if (ID3D12CommandList* const acquire = mirrored_store_->backend().submit_uploads(queue_.Get())) {
            submit_lists_.push_back(acquire);
        }
    }
    // The lists a recorded pass cut the frame into (parallel_record_begin), then the frame's last.
    submit_lists_.insert(submit_lists_.end(), frame_lists_.begin(), frame_lists_.end());
    submit_lists_.push_back(command_list_.Get());
    if (present_swap_chain) {
        streamline::marker(streamline::Marker::kSimulationEnd);
        streamline::marker(streamline::Marker::kRenderSubmitStart);
    }
    queue_->ExecuteCommandLists(static_cast<UINT>(submit_lists_.size()), submit_lists_.data());
    if (present_swap_chain) {
        streamline::marker(streamline::Marker::kRenderSubmitEnd);
    }

    timestamp_pending_[frame_slot_] =
        timestamp_query_heap_ != nullptr && timestamp_readback_ != nullptr;
    timestamp_list_place_[frame_slot_] = static_cast<std::uint8_t>(std::min(list_in_frame_, kGpuListPlaces - 1u));
    list_in_frame_ = present_swap_chain ? 0u : list_in_frame_ + 1u;

    const std::uint64_t submitted_frame = frame_index_ + 1u;
    const std::uint64_t submitted_fence = ++next_fence_value_;
    fence_values_[frame_slot_] = submitted_fence;
    history_used_ = vertex_ring_.cursor() != history_begin_vertex_ || matrix_ring_.cursor() != history_begin_matrix_;
    history_segment_fence_[history_segment_] = submitted_fence;
    frame_generated_[frame_slot_] = generated;
    frame_scene_drawn_[frame_slot_] = scene_drawn_;
    for (ScreenProbeSlot& slot : probe_slots_) {
        if (slot.fence == kProbesInFrame) {
            slot.fence = submitted_fence;
        }
    }
    const HRESULT signal_hr = queue_->Signal(fence_.Get(), submitted_fence);
    if (FAILED(signal_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(signal_hr, "Queue Signal");
    }
    // Readbacks recorded on this list complete at this signal; nothing waits for them here.
    if (readback_pending_ && readback_fence_value_ == 0u) {
        readback_fence_value_ = submitted_fence;
    }
    if (backbuffer_readback_pending_ &&
        backbuffer_readback_fence_value_ == 0u) {
        backbuffer_readback_fence_value_ = submitted_fence;
    }
    if (selected_display_readback_pending_ && selected_display_readback_fence_ == 0u) {
        selected_display_readback_fence_ = submitted_fence;
    }
    // Reached only after a successful signal; presenting follows.
    if (on_submitted != nullptr) {
        on_submitted(submission_context, submitted_fence);
    }

    if (present_swap_chain) {
        present_to_swap_chain();
    }

    frame_index_ = submitted_frame;
    return submitted_fence;
}

std::uint64_t RendererD3D12::completed_fence_value() const {
    if (!fence_) {
        return 0u;
    }
    const std::uint64_t completed = fence_->GetCompletedValue();
    if (completed == ~std::uint64_t{0}) {
        dump_device_removal(device_.Get());
        throw std::runtime_error(
            "device removed while polling a GPU fence");
    }
    return completed;
}

void RendererD3D12::wait_for_fence_value(std::uint64_t fence_value) {
    if (device_gone_) {
        return;
    }
    if (!fence_ || fence_event_ == nullptr || fence_value == 0u) {
        throw std::runtime_error("invalid frame-specific GPU fence wait");
    }
    const std::uint64_t completed_before = fence_->GetCompletedValue();
    if (completed_before == ~std::uint64_t{0}) {
        dump_device_removal(device_.Get());
        throw std::runtime_error(
            "device removed before a GPU fence completed");
    }
    if (completed_before >= fence_value) {
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kFenceWaitTimeoutMs);
    const HRESULT event_hr =
        fence_->SetEventOnCompletion(fence_value, fence_event_);
    if (FAILED(event_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(event_hr, "frame PE SetEventOnCompletion");
    }
    // A previous timed-out registration can wake this reusable auto-reset
    // event later. Only the fence value authorizes resource reuse/readback.
    // Consume stale wakes without extending the original timeout budget.
    for (;;) {
        const std::uint64_t completed = completed_fence_value();
        if (completed >= fence_value) {
            return;
        }
        if (device_->GetDeviceRemovedReason() != S_OK) {
            dump_device_removal(device_.Get());
            throw std::runtime_error("device removed while waiting for a GPU fence");
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            throw std::runtime_error("device lost: a GPU fence never completed");
        }
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
            deadline - now).count();
        const DWORD wait_result = WaitForSingleObjectEx(
            fence_event_, static_cast<DWORD>(remaining), FALSE);
        if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_TIMEOUT) {
            // Recheck on both wake and timeout: completion can race the timeout.
            continue;
        }
        const DWORD wait_error = GetLastError();
        dump_device_removal(device_.Get());
        throw std::runtime_error(
            "GPU fence wait failed: result=" +
            std::to_string(wait_result) +
            " win32=" + std::to_string(wait_error));
    }
}

bool RendererD3D12::temporal_mode() const {
    const unsigned upscaler = get_render_config().upscaler;
    return (upscaler == 2u && fsr3_available()) || (upscaler == 3u && streamline::dlss_supported()) ||
           (upscaler == 4u && xess_library::available());
}

void RendererD3D12::trim_idle_effect_targets() {
    constexpr std::uint64_t kIdleFrames = 600u;
    // ComPtr's operator& yields a ComPtrRef, so each resource goes through a reference.
    const auto retire = [this](Microsoft::WRL::ComPtr<ID3D12Resource>& resource) {
        if (resource) {
            retired_targets_[frame_slot_].push_back(std::move(resource));
        }
    };
    if (frame_generation_running()) {
        fg_hudless_used_frame_ = frame_index_;
    }
    if (frame_index_ > fg_hudless_used_frame_ + kIdleFrames) {
        retire(fg_hudless_scene_target_.color);
        retire(fg_hudless_scene_target_.depth);
        retire(fg_hudless_scene_target_.msaa.color);
        retire(fg_hudless_scene_target_.msaa.depth);
        retire(fg_hudless_display_.texture);
        retire(fg_hudless_target_);
        fg_hudless_scene_target_.map = FrameMap{};
        fg_hudless_scene_ = fg_hudless_taken_ = fg_hudless_ready_ = false;
    }
    if (frame_index_ > hdr_capture_used_frame_ + kIdleFrames) {
        retire(hdr_capture_target_);
    }
    if (motion_target_ && !layer_window_ && frame_index_ > motion_used_frame_ + kIdleFrames) {
        retire(motion_target_);
        retire(reactive_target_);
        retire(layer_color_);
        retire(layer_trans_);
        motion_target_map_ = FrameMap{};
    }
    // The output scene target and the wide set are the live scene target while a swap holds them; between scenes they
    // are idle targets of their own.
    if (alt_scene_.color && !output_scene_active_ && !scene_hdr_ && frame_index_ > output_scene_used_frame_ + kIdleFrames) {
        retired_targets_[frame_slot_].push_back(std::move(alt_scene_.color));
        retired_targets_[frame_slot_].push_back(std::move(alt_scene_.depth));
        alt_scene_.map = FrameMap{};
    }
    if (hdr_scene_scene_.color && !scene_hdr_ && !output_scene_active_ && !target_pass_active_ &&
        frame_index_ > wide_scene_used_frame_ + kIdleFrames) {
        retired_targets_[frame_slot_].push_back(std::move(hdr_scene_scene_.color));
        if (hdr_scene_scene_.msaa.color) {
            retired_targets_[frame_slot_].push_back(std::move(hdr_scene_scene_.msaa.color));
        }
        hdr_scene_scene_.depth.Reset();  // the scene target's own depth, held by the scene target too
        hdr_scene_scene_.map = FrameMap{};
    }
}

bool RendererD3D12::ensure_motion_target() {
    motion_used_frame_ = frame_index_;
    if (motion_target_ && motion_target_map_ == frame_) {
        return true;
    }
    if (motion_target_) {
        // A render-scale change inside a scene: this frame's command list
        // may still name them.
        retired_targets_[frame_slot_].push_back(std::move(motion_target_));
        retired_targets_[frame_slot_].push_back(std::move(reactive_target_));
        retired_targets_[frame_slot_].push_back(std::move(layer_color_));
        retired_targets_[frame_slot_].push_back(std::move(layer_trans_));
    }
    motion_target_.Reset();
    reactive_target_.Reset();
    layer_color_.Reset();
    layer_trans_.Reset();
    layer_window_ = false;
    motion_target_map_ = FrameMap{};
    if (!motion_rtv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 4;
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&motion_rtv_heap_)),
                  "motion vector RTV heap");
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = frame_.width;
    rd.Height = frame_.height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kMotionVectorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = kMotionVectorFormat;
    if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ColorWrite,
            &cv, IID_PPV_ARGS(&motion_target_)))) {
        return false;
    }
    rd.Format = kReactiveMaskFormat;
    cv.Format = kReactiveMaskFormat;
    if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ColorWrite,
            &cv, IID_PPV_ARGS(&reactive_target_)))) {
        motion_target_.Reset();
        return false;
    }
    rd.Format = kLayerFormat;
    cv.Format = kLayerFormat;
    if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ColorWrite,
                                                &cv, IID_PPV_ARGS(&layer_color_))) ||
        FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ColorWrite,
                                                &cv, IID_PPV_ARGS(&layer_trans_)))) {
        motion_target_.Reset();
        reactive_target_.Reset();
        layer_color_.Reset();
        layer_trans_.Reset();
        return false;
    }
    layer_color_->SetName(L"effects layer colour");
    layer_trans_->SetName(L"effects layer transmittance");
    const UINT rtv_step = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    motion_rtv_ = motion_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    reactive_rtv_ = motion_rtv_;
    reactive_rtv_.ptr += rtv_step;
    layer_color_rtv_ = reactive_rtv_;
    layer_color_rtv_.ptr += rtv_step;
    layer_trans_rtv_ = layer_color_rtv_;
    layer_trans_rtv_.ptr += rtv_step;
    device_->CreateRenderTargetView(motion_target_.Get(), nullptr, motion_rtv_);
    device_->CreateRenderTargetView(reactive_target_.Get(), nullptr, reactive_rtv_);
    device_->CreateRenderTargetView(layer_color_.Get(), nullptr, layer_color_rtv_);
    device_->CreateRenderTargetView(layer_trans_.Get(), nullptr, layer_trans_rtv_);
    const float zero[4]{};
    command_list_->ClearRenderTargetView(motion_rtv_, zero, 0u, nullptr);
    command_list_->ClearRenderTargetView(reactive_rtv_, zero, 0u, nullptr);
    record_layer_begin();  // an empty layer until the first layer window, as that window starts
    motion_target_map_ = frame_;
    return true;
}


bool RendererD3D12::interpolation_mode() const {
    const RenderConfig cfg = get_render_config();
    const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, display_refresh_hz_);
    // With a temporal upscaler every presented frame is upscaled, its motion from the frame presented before (any
    // rate: the slots' t need not divide the game frame evenly), unless the GPU could not afford it.
    if (temporal_mode() && temporal_interp_suspended_) {
        return false;
    }
    return interpolated_frames_per_game_frame(rate) != 0u;
}

RendererD3D12::TemporalInterpKey RendererD3D12::temporal_interp_key() const {
    const RenderConfig cfg = get_render_config();
    TemporalInterpKey key{};
    key.upscaler = cfg.upscaler;
    key.quality = cfg.temporal_quality;
    key.scene_map = output_scene_active_ ? alt_scene_.map : frame_;  // the render scene target's
    key.output_map = upscale_output_;
    key.max_fps = presented_max_fps(cfg);
    key.vsync = cfg.vsync_interval;
    key.refresh_hz = display_refresh_hz_;
    return key;
}

void RendererD3D12::suspend_temporal_interpolation(double ms_per_frame) {
    if (temporal_interp_suspended_) {
        return;
    }
    temporal_interp_suspended_ = true;
    temporal_interp_key_ = temporal_interp_key();
    std::cerr << "[interp] temporal upscaling of generated frames off: " << ms_per_frame
              << " ms GPU per presented frame\n";
}

bool RendererD3D12::begin_motion_scene() {
    reactive_scene_ = false;
    layer_window_ = false;
    if (!scene_motion_ || !ensure_motion_target()) {
        return false;
    }
    const float zero[4]{};
    command_list_->ClearRenderTargetView(motion_rtv_, zero, 0u, nullptr);
    command_list_->ClearRenderTargetView(reactive_rtv_, zero, 0u, nullptr);
    reactive_scene_ = true;
    return true;
}

void RendererD3D12::begin_layer_window() {
    if (!reactive_scene_ || layer_window_ || !layer_color_ || !layer_trans_) {
        return;
    }
    record_layer_begin();
    layer_window_ = true;
}

// Nothing added yet, everything behind let through.
void RendererD3D12::record_layer_begin() {
    if (!layer_color_ || !layer_trans_) {
        return;
    }
    const float zero[4]{};
    const float one[4]{1.0f, 1.0f, 1.0f, 1.0f};
    command_list_->ClearRenderTargetView(layer_color_rtv_, zero, 0u, nullptr);
    command_list_->ClearRenderTargetView(layer_trans_rtv_, one, 0u, nullptr);
}

// Command lists recorded before this point may name the old context; none is
// open, so waiting for the GPU is enough before it goes.
void RendererD3D12::prepare_temporal_upscaler() {
    const FrameMap render_map = output_scene_active_ ? alt_scene_.map : frame_;
    const FrameMap output_map = upscale_output_;
    if (!temporal_mode() || output_map.empty() || render_map.width > output_map.width) {
        if (temporal_) {
            wait_for_gpu();
            temporal_.reset();
        }
        temporal_pending_ = {};
        temporal_render_w_ = temporal_render_h_ = 0u;
        temporal_output_w_ = temporal_output_h_ = 0u;
        temporal_history_valid_ = false;
        return;
    }
    const unsigned rw = render_map.width;
    const unsigned rh = render_map.height;
    const unsigned ow = output_map.width;
    const unsigned oh = output_map.height;
    const RenderConfig cfg = get_render_config();
    const unsigned kind = cfg.upscaler;
    const unsigned quality = std::min(cfg.temporal_quality, 5u);
    // A failed creation is not retried until its sizes or mode change.
    if (rw == temporal_render_w_ && rh == temporal_render_h_ && ow == temporal_output_w_ &&
        oh == temporal_output_h_ && kind == temporal_kind_ && (kind < 3u || quality == temporal_quality_)) {
        if (temporal_pending_.valid() &&
            temporal_pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            temporal_ = temporal_pending_.get();
            temporal_history_valid_ = false;
            if (temporal_) {
                std::cout << "[Renderer] " << temporal_->name() << " " << rw << "x" << rh
                          << " -> " << ow << "x" << oh << "\n";
            }
        }
        return;
    }
    if (temporal_) {
        wait_for_gpu();
        temporal_.reset();
    }
    // A creation still running for older sizes is waited for and dropped.
    temporal_pending_ = {};
    temporal_kind_ = kind;
    temporal_quality_ = quality;
    temporal_history_valid_ = false;
    temporal_render_w_ = rw;
    temporal_render_h_ = rh;
    temporal_output_w_ = ow;
    temporal_output_h_ = oh;
    if (kind == 4u) {
        // Until it is ready the scene renders at the render scale without the upscale.
        temporal_pending_ = std::async(std::launch::async, [device = device_, ow, oh, quality] {
            return create_xess_upscaler(device.Get(), ow, oh, quality);
        });
        return;
    }
    if (kind != 3u) {
        // Until it is ready the scene renders at the render scale without the upscale.
        temporal_pending_ = std::async(std::launch::async, [device = device_, rw, rh, ow, oh] {
            return create_fsr3_upscaler(device.Get(), rw, rh, ow, oh);
        });
        return;
    }
    temporal_ = create_dlss_upscaler(rw, rh, ow, oh, quality);
    if (temporal_) {
        std::cout << "[Renderer] " << temporal_->name() << " " << rw << "x" << rh
                  << " -> " << ow << "x" << oh << "\n";
    }
}

bool RendererD3D12::scene_temporal_begin(float& jitter_x, float& jitter_y) {
    jitter_x = 0.0f;
    jitter_y = 0.0f;
    scene_temporal_ = false;
    scene_motion_ = false;
    const unsigned rw = frame_.width;
    const unsigned ow = temporal_output_w_;
    // The context is replaced only by begin_frame (prepare_temporal_upscaler).
    if (!temporal_ || !temporal_mode() || output_scene_active_ || rw != temporal_render_w_ ||
        frame_.height != temporal_render_h_) {
        temporal_history_valid_ = false;
        return false;
    }
    const unsigned phases = std::max(temporal_->jitter_phase_count(rw, ow), 1u);
    temporal_->jitter_offset(static_cast<unsigned>(temporal_frame_ % phases), phases,
                             jitter_x, jitter_y);
    scene_jitter_index_ = static_cast<unsigned>(temporal_frame_ % phases);
    scene_jitter_phases_ = phases;
    ++temporal_frame_;
    scene_jitter_[0] = jitter_x;
    scene_jitter_[1] = jitter_y;
    scene_temporal_ = true;
    scene_motion_ = true;
    return true;
}

void RendererD3D12::set_scene_camera(float near_plane, float far_plane, float fov_y) noexcept {
    scene_camera_[0] = near_plane;
    scene_camera_[1] = far_plane;
    scene_camera_[2] = fov_y;
}

// The upscaler's context was created for exactly these sizes and the resources cover them. An
// internal-resolution change applied between a scene's start and its upscale (a render chunk
// boundary) would otherwise dispatch past the context's maximum (FidelityFX OUT_OF_RANGE) or
// outside the resources.
bool RendererD3D12::temporal_inputs_match(const TemporalUpscaleInputs& in) const {
    const auto covers = [](ID3D12Resource* resource, unsigned width, unsigned height) {
        if (resource == nullptr) {
            return true;
        }
        const D3D12_RESOURCE_DESC desc = resource->GetDesc();
        return desc.Width >= width && desc.Height >= height;
    };
    const bool match =
        in.render_width == temporal_render_w_ && in.render_height == temporal_render_h_ &&
        in.output_width == temporal_output_w_ && in.output_height == temporal_output_h_ &&
        covers(in.color, in.render_width, in.render_height) &&
        covers(in.depth, in.render_width, in.render_height) &&
        covers(in.motion, in.render_width, in.render_height) &&
        covers(in.reactive, in.render_width, in.render_height) &&
        covers(in.output, in.output_width, in.output_height);
    if (!match) {
        static std::uint64_t reported = 0u;
        if (reported++ < 8u) {
            std::cerr << "[temporal] skipped: " << in.render_width << 'x' << in.render_height << " -> "
                      << in.output_width << 'x' << in.output_height << " context " << temporal_render_w_ << 'x'
                      << temporal_render_h_ << " -> " << temporal_output_w_ << 'x' << temporal_output_h_ << '\n';
        }
    }
    return match;
}


bool RendererD3D12::record_temporal_upscale(const FrameMap& output) {
    const GpuPassScope pass{*this, GpuPass::Upscale};
    if (!scene_temporal_ || !temporal_ || !ensure_motion_target()) {
        return false;
    }
    TemporalUpscaleInputs in{};
    in.list = command_list_.Get();
    in.color = scene_color_.Get();
    in.depth = scene_depth_.Get();
    in.motion = motion_target_.Get();
    in.reactive = reactive_target_.Get();
    in.output = alt_scene_.color.Get();
    in.render_width = frame_.width;
    in.render_height = frame_.height;
    in.output_width = output.width;
    in.output_height = output.height;
    in.jitter_x = scene_jitter_[0];
    in.jitter_y = scene_jitter_[1];
    in.camera_near = scene_camera_[0];
    in.camera_far = scene_camera_[1];
    in.fov_y = scene_camera_[2];
    // The time since the last upscale, as measured.
    const std::int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    in.frame_ms = temporal_last_upscale_us_ != 0 ? static_cast<float>(now_us - temporal_last_upscale_us_) * 0.001f : 0.0f;
    temporal_last_upscale_us_ = now_us;
    in.reset = !temporal_history_valid_;
    in.view = scene_view_;
    in.view_known = scene_view_known_;
    const bool recorded = temporal_inputs_match(in) && temporal_->evaluate(in);
    if (recorded && fsr_swap_chain_) {
        const unsigned display_width = host_window::backbuffer_width;
        const unsigned display_height = host_window::backbuffer_height;
        bool ready = fsr_generation_.matches(in.render_width, in.render_height, display_width, display_height);
        if (!ready) {
            wait_for_gpu();
            fsr_generation_.destroy(swap_chain_.Get());
            ready = fsr_generation_.create(device_.Get(), in.render_width, in.render_height, display_width, display_height, kBackbufferFormat);
        }
        if (ready) {
            FsrFrameGeneration::Prepare prepare{};
            prepare.list = command_list_.Get();
            prepare.depth = in.depth;
            prepare.motion = in.motion;
            prepare.jitter_x = in.jitter_x;
            prepare.jitter_y = in.jitter_y;
            prepare.render_width = in.render_width;
            prepare.render_height = in.render_height;
            prepare.frame_ms = in.frame_ms;
            prepare.camera_near = in.camera_near;
            prepare.camera_far = in.camera_far;
            prepare.fov_y = in.fov_y;
            (void)fsr_generation_.prepare(prepare);
        }
    } else if (recorded && frame_generation_running()) {
        streamline::FrameGenerationInputs fg{};
        fg.list = command_list_.Get();
        fg.depth = in.depth;
        fg.motion = in.motion;
        fg.render_width = in.render_width;
        fg.render_height = in.render_height;
        (void)streamline::tag_frame_generation(fg);
    }
    // The upscaler binds its own heaps, root signature and pipelines.
    srv_ring_.forget_bound();
    srv_ring_.bind(command_list_.Get());
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    temporal_history_valid_ = recorded;
    return recorded;
}

FrameMap RendererD3D12::output_frame_for_window() const {
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const double content_aspect =
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect;
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        content_aspect);
    if (content.width <= 0 || content.height <= 0) {
        return FrameMap{};
    }
    return FrameMap(static_cast<unsigned>(content.width), static_cast<unsigned>(content.height));
}

bool RendererD3D12::ensure_output_scene(const FrameMap& map) {
    output_scene_used_frame_ = frame_index_;
    if (alt_scene_.color && alt_scene_.map == map) {
        return true;
    }
    const UINT w = map.width;
    const UINT h = map.height;
    if (w > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        h > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
        return false;
    }
    if (alt_scene_.color) {
        // A new output scale (window resize or internal resolution): frames in
        // flight and this frame's command list may still name the old targets.
        retired_targets_[frame_slot_].push_back(std::move(alt_scene_.color));
        retired_targets_[frame_slot_].push_back(std::move(alt_scene_.depth));
    }
    alt_scene_.color.Reset();
    alt_scene_.depth.Reset();
    alt_scene_.map = FrameMap{};
    if (!alt_scene_rtv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1;
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&alt_scene_rtv_heap_)),
                  "output scene target RTV heap");
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&alt_scene_dsv_heap_)),
                  "output scene target DSV heap");
        alt_scene_.rtv = alt_scene_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        alt_scene_.dsv = alt_scene_dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kSceneColorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = kSceneColorFormat;
    cv.Color[3] = 1.0f;
    if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ColorWrite,
            &cv, IID_PPV_ARGS(&alt_scene_.color)))) {
        return false;
    }
    rd.Format = kSceneDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kSceneDepthFormat;
    dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::DepthWrite,
            &dcv, IID_PPV_ARGS(&alt_scene_.depth)))) {
        alt_scene_.color.Reset();
        return false;
    }
    device_->CreateRenderTargetView(alt_scene_.color.Get(), nullptr, alt_scene_.rtv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kSceneDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(alt_scene_.depth.Get(), &dsv, alt_scene_.dsv);
    if (!alt_scene_.conv_table) {
        alt_scene_.conv_table = allocate_persistent_descriptors(2u);
    }
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kSceneColorFormat;
    const D3D12_CPU_DESCRIPTOR_HANDLE color_cpu = srv_ring_.persistent_cpu(alt_scene_.conv_table->index);
    device_->CreateShaderResourceView(alt_scene_.color.Get(), &view, color_cpu);
    auto depth_cpu = color_cpu;
    depth_cpu.ptr += stride;
    view.Format = kSceneDepthSrvFormat;
    device_->CreateShaderResourceView(alt_scene_.depth.Get(), &view, depth_cpu);
    srv_ring_.commit(*alt_scene_.conv_table, 2u);
    alt_scene_.conv_color_index = alt_scene_.conv_table->index;
    alt_scene_.conv_depth_index = alt_scene_.conv_table->index + 1u;
    alt_scene_.map = map;
    return true;
}

void RendererD3D12::write_scene_set_views(SceneTargetSet& set, const DescriptorRing::Persistent& table) {
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = set.color->GetDesc().Format;
    const D3D12_CPU_DESCRIPTOR_HANDLE color_cpu = srv_ring_.persistent_cpu(table.index);
    device_->CreateShaderResourceView(set.color.Get(), &view, color_cpu);
    auto depth_cpu = color_cpu;
    depth_cpu.ptr += stride;
    view.Format = kSceneDepthSrvFormat;
    device_->CreateShaderResourceView(set.depth.Get(), &view, depth_cpu);
    srv_ring_.commit(table, 2u);
    set.conv_table = table;
    set.conv_color_index = table.index;
    set.conv_depth_index = table.index + 1u;
}

bool RendererD3D12::create_scene_set(
    SceneTargetSet& set, ComPtr<ID3D12DescriptorHeap>& rtv_heap,
    ComPtr<ID3D12DescriptorHeap>& dsv_heap, const FrameMap& map, bool persistent_views,
    bool unordered_access, unsigned samples, bool hdr, unsigned width, unsigned height, bool target) {
    const UINT w = static_cast<UINT>(std::lround(static_cast<double>(width) * map.sx));
    const UINT h = static_cast<UINT>(std::lround(static_cast<double>(height) * map.sy));
    if (set.color) {
        wait_for_gpu();
    }
    set.color.Reset();
    set.depth.Reset();
    set.map = FrameMap{};
    if (!rtv_heap) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1;
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&rtv_heap)), "scene target set RTV heap");
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&dsv_heap)), "scene target set DSV heap");
    }
    set.rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    set.dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = target ? kTargetColorFormat : hdr ? kSceneHdrColorFormat : kSceneColorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
               (unordered_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
    D3D12_CLEAR_VALUE cv{};
    cv.Format = rd.Format;
    cv.Color[3] = 1.0f;
    if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ColorWrite,
            &cv, IID_PPV_ARGS(&set.color)))) {
        return false;
    }
    set.hdr = hdr;
    rd.Format = kSceneDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kSceneDepthFormat;
    dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::DepthWrite,
            &dcv, IID_PPV_ARGS(&set.depth)))) {
        set.color.Reset();
        return false;
    }
    device_->CreateRenderTargetView(set.color.Get(), nullptr, set.rtv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kSceneDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(set.depth.Get(), &dsv, set.dsv);
    if (!create_scene_msaa(set.msaa, set.color.Get(), set.depth.Get(), samples, set.rtv, set.dsv)) {
        set.color.Reset();
        set.depth.Reset();
        return false;
    }
    if (persistent_views) {
        write_scene_set_views(set, set.conv_table ? *set.conv_table
                                                : allocate_persistent_descriptors(2u));
    } else {
        // The caller writes views into the frame's descriptor segment per use.
        set.conv_table.reset();
    }
    set.map = map;
    return true;
}

void RendererD3D12::swap_scene_with(SceneTargetSet& other) {
    std::swap(scene_color_, other.color);
    std::swap(scene_depth_, other.depth);
    std::swap(scene_msaa_, other.msaa);
    std::swap(frame_, other.map);
    std::swap(scene_hdr_, other.hdr);
    std::swap(scene_rtv_, other.rtv);
    std::swap(scene_dsv_, other.dsv);
    std::swap(conv_scene_source_table_, other.conv_table);
    std::swap(conv_scene_color_index_, other.conv_color_index);
    std::swap(conv_scene_depth_index_, other.conv_depth_index);
    invalidate_draw_bindings();
}

void RendererD3D12::swap_scene_sets() {
    std::swap(scene_color_, alt_scene_.color);
    std::swap(scene_depth_, alt_scene_.depth);
    std::swap(scene_msaa_, alt_scene_.msaa);
    std::swap(frame_, alt_scene_.map);
    std::swap(scene_hdr_, alt_scene_.hdr);
    std::swap(scene_rtv_, alt_scene_.rtv);
    std::swap(scene_dsv_, alt_scene_.dsv);
    std::swap(conv_scene_source_table_, alt_scene_.conv_table);
    std::swap(conv_scene_color_index_, alt_scene_.conv_color_index);
    std::swap(conv_scene_depth_index_, alt_scene_.conv_depth_index);
    output_scene_active_ = !output_scene_active_;
    invalidate_draw_bindings();
    bind_scene();
}

// The scene target and the target passes' set trade places (begin_target_pass): targets, scale and copy sources, the output
// scene target's state kept.
void RendererD3D12::swap_target_set() {
    std::swap(scene_color_, target_scene_.color);
    std::swap(scene_depth_, target_scene_.depth);
    std::swap(scene_msaa_, target_scene_.msaa);
    std::swap(frame_, target_scene_.map);
    std::swap(scene_hdr_, target_scene_.hdr);
    std::swap(scene_rtv_, target_scene_.rtv);
    std::swap(scene_dsv_, target_scene_.dsv);
    std::swap(conv_scene_source_table_, target_scene_.conv_table);
    std::swap(conv_scene_color_index_, target_scene_.conv_color_index);
    std::swap(conv_scene_depth_index_, target_scene_.conv_depth_index);
    target_pass_active_ = !target_pass_active_;
    invalidate_draw_bindings();
    bind_scene();
}

bool RendererD3D12::begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height) {
    if (target_pass_active_ || width == 0u || height == 0u) {
        return false;
    }
    TargetSlot& slot = target_slots_[key];
    const FrameMap map(width, height);
    const bool kept = slot.set.color && slot.set.map == map;
    // A new set starts as a render target; the pass's first clear (RenderCore) fills it. Its copy views are written
    // per use (no persistent descriptors per key).
    if (!kept) {
        if (!create_scene_set(slot.set, slot.rtv_heap, slot.dsv_heap, map, false, false, 1u, false, kFrameUnits,
                            kFrameUnits, true)) {
            target_slots_.erase(key);
            return false;
        }
        slot.width = width;
        slot.height = height;
    } else {
        // Sampled since its last pass: drawn into again.
        BarriersD3D12 uses{command_list_.Get()};
        uses.texture(slot.set.color.Get(), gpu::Use::PixelRead, gpu::Use::ColorWrite);
        uses.issue();
    }
    slot.used = frame_index_;
    target_key_ = key;
    std::swap(target_scene_, slot.set);
    swap_target_set();
    // Generated frames sample its picture, never draw it again.
    return true;
}

void RendererD3D12::end_target_pass(std::uint8_t channels, TextureStore& textures) {
    if (!target_pass_active_) {
        return;
    }
    swap_target_set();
    if (frame_pass_color_key_ != 0u || frame_pass_depth_key_ != 0u) {
        close_frame_for_pass(textures);
    }
    // The picture is sampled until the key's next pass.
    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(target_scene_.color.Get(), gpu::Use::ColorWrite, gpu::Use::PixelRead);
    uses.issue();
    TargetSlot& slot = target_slots_[target_key_];
    std::swap(slot.set, target_scene_);
    (void)publish_picture(target_key_, slot.width, slot.height, slot.set.color, channels, textures);
    // Keys unused for kTargetIdleFrames frames give back their render resources (their pictures stay).
    sweep_target_slots();
}

void RendererD3D12::retire_target_slot(TargetSlot& slot) {
    // ComPtr's operator& yields a ComPtrRef, so each resource goes through a reference.
    const auto retire = [this](Microsoft::WRL::ComPtr<ID3D12Resource>& resource) {
        if (resource) {
            retired_targets_[frame_slot_].push_back(std::move(resource));
        }
    };
    retire(slot.set.color);
    retire(slot.set.depth);
    retire(slot.set.msaa.color);
    retire(slot.set.msaa.depth);
}

void RendererD3D12::sweep_target_slots() {
    if (target_slots_.empty()) {
        return;
    }
    const std::uint64_t idle = target_idle_frames();
    for (auto it = target_slots_.begin(); it != target_slots_.end();) {
        if (it->second.used + idle < frame_index_ && !(target_pass_active_ && it->first == target_key_)) {
            retire_target_slot(it->second);
            it = target_slots_.erase(it);
        } else {
            ++it;
        }
    }
}

bool RendererD3D12::target_picture(std::uint64_t key, TextureHandle& out) const {
    if (pictures_.empty()) {
        return false;
    }
    const auto it = pictures_.find(key);
    if (it == pictures_.end()) {
        return false;
    }
    out = TextureHandle{};
    out.resource = gpu::raw(it->second.viewed);
    out.srv_index = it->second.view;
    out.width = static_cast<std::uint16_t>(it->second.viewed->GetDesc().Width);
    out.height = static_cast<std::uint16_t>(it->second.viewed->GetDesc().Height);
    out.mip_levels = 1u;
    return true;
}

bool RendererD3D12::publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height,
                                    gpu::TextureRef texture, std::uint8_t channels, TextureStore& textures) {
    Picture& picture = pictures_[key];
    if (picture.view != ~0u && picture.viewed == texture && picture.channels == channels && picture.width == width &&
        picture.height == height) {
        return false;
    }
    if (picture.view != ~0u) {
        textures.retire_view(picture.view, std::move(picture.viewed));
    }
    picture.view = textures.allocate_view();
    picture.viewed = std::move(texture);
    picture.width = width;
    picture.height = height;
    picture.channels = channels;
    const std::array<std::uint8_t, 4> swizzle = target_channel_swizzle(channels);
    // D3D12's component mapping: 0-3 the texture's R, G, B, A, 5 one.
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = picture.viewed.Get() == scene_depth_.Get() ? kSceneDepthSrvFormat : picture.viewed->GetDesc().Format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
        swizzle[0] == 4u ? 5u : swizzle[0], swizzle[1] == 4u ? 5u : swizzle[1], swizzle[2] == 4u ? 5u : swizzle[2],
        swizzle[3] == 4u ? 5u : swizzle[3]);
    desc.Texture2D.MipLevels = 1u;
    const auto cpu = textures.backend().srv_cpu(picture.view);
    device_->CreateShaderResourceView(picture.viewed.Get(), &desc, cpu);
    textures.backend().publish_view(picture.view);
    return true;
}

bool RendererD3D12::publish_frame_pass(const FramePassRequest& params, gpu::TextureRef texture, TextureStore& textures) {
    const FrameExtent size = frame_pass_extent(frame_);
    return publish_picture(params.key, size.width, size.height, std::move(texture), 0u, textures);
}

// DLSS Frame Generation's HUD-less picture (fg_hudless_*): the output scene target right after the
// upscale, before the 2D layer draws into it.
void RendererD3D12::take_fg_hudless_scene() {
    fg_hudless_scene_ = false;
    if (!frame_generation_running()) {
        return;
    }
    if (!fg_hudless_scene_target_.color || fg_hudless_scene_target_.map != frame_) {
        if (fg_hudless_scene_target_.color) {
            wait_for_gpu();
        }
        fg_hudless_scene_target_.color.Reset();
        fg_hudless_scene_target_.depth.Reset();
        if (!create_scene_set(fg_hudless_scene_target_, fg_hudless_rtv_heap_, fg_hudless_dsv_heap_, frame_)) {
            return;
        }
    }
    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(scene_color_.Get(), gpu::Use::ColorWrite, gpu::Use::CopySource);
    uses.texture(fg_hudless_scene_target_.color.Get(), gpu::Use::ColorWrite, gpu::Use::CopyDest);
    uses.issue();
    command_list_->CopyResource(fg_hudless_scene_target_.color.Get(), scene_color_.Get());
    uses.undo();
    fg_hudless_scene_ = true;
}

// The display copy of the taken scene: the same copy into the HUD-less display picture.
void RendererD3D12::copy_fg_hudless_display(const DisplayTexture& dest) {
    const bool taken = fg_hudless_scene_;
    fg_hudless_scene_ = false;
    if (!taken || fg_hudless_scene_target_.map != frame_ || !dest.texture) {
        return;
    }
    const D3D12_RESOURCE_DESC desc = dest.texture->GetDesc();
    if (!fg_hudless_display_.texture || fg_hudless_display_.texture->GetDesc().Width != desc.Width ||
        fg_hudless_display_.texture->GetDesc().Height != desc.Height) {
        if (fg_hudless_display_.texture) {
            wait_for_gpu();
        }
        fg_hudless_display_.texture.Reset();
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &desc,
                                                    gpu::Use::PixelRead, nullptr,
                                                    IID_PPV_ARGS(&fg_hudless_display_.texture)))) {
            return;
        }
        fg_hudless_display_.texture->SetName(L"DLSS-G HUD-less display picture");
    }
    fg_hudless_display_.width = dest.width;
    fg_hudless_display_.height = dest.height;
    fg_hudless_display_.rtv = allocate_conversion_rtv(fg_hudless_display_.texture.Get());
    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(fg_hudless_display_.texture.Get(), gpu::Use::PixelRead, gpu::Use::ColorWrite);
    uses.issue();
    const FramePassGeometry sampling =
        frame_pass_geometry(dest.width, dest.height, frame_);
    swap_scene_with(fg_hudless_scene_target_);
    record_conversion_blit(fg_hudless_display_.texture.Get(), dest.width, dest.height, fg_hudless_display_.rtv, kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    swap_scene_with(fg_hudless_scene_target_);
    uses.undo();
    fg_hudless_taken_ = true;
    fg_hudless_slot_ = dest.slot;
}

// The HUD-less display picture of the presented copy, presented as the frame is into a backbuffer-sized
// target that frame generation reads at the present; without one this frame, no HUD-less tag.
void RendererD3D12::present_fg_hudless(const DisplayTexture& source, const D3D12_VIEWPORT& viewport,
                                       const D3D12_RECT& scissor) {
    if (!frame_generation_running()) {
        return;
    }
    const bool ready = fg_hudless_display_.texture && fg_hudless_taken_ &&
                       fg_hudless_slot_ == source.slot && fg_hudless_display_.width == source.width &&
                       fg_hudless_display_.height == source.height;
    fg_hudless_taken_ = false;
    if (!ready) {
        streamline::tag_hudless(command_list_.Get(), nullptr, 0u, 0u);
        return;
    }
    if (!fg_hudless_target_ || fg_hudless_target_->GetDesc().Width != host_window::backbuffer_width ||
        fg_hudless_target_->GetDesc().Height != host_window::backbuffer_height) {
        if (fg_hudless_target_) {
            wait_for_gpu();
        }
        fg_hudless_target_.Reset();
        if (!fg_hudless_target_rtv_heap_) {
            D3D12_DESCRIPTOR_HEAP_DESC dhd{};
            dhd.NumDescriptors = 1;
            dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&fg_hudless_target_rtv_heap_)),
                      "HUD-less RTV heap");
        }
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = host_window::backbuffer_width;
        rd.Height = host_window::backbuffer_height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = kBackbufferFormat;
        rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv{};
        cv.Format = kBackbufferFormat;
        cv.Color[3] = 1.0f;
        if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                    gpu::Use::ColorWrite, &cv,
                                                    IID_PPV_ARGS(&fg_hudless_target_)))) {
            streamline::tag_hudless(command_list_.Get(), nullptr, 0u, 0u);
            return;
        }
        fg_hudless_target_->SetName(L"DLSS-G HUD-less");
        device_->CreateRenderTargetView(fg_hudless_target_.Get(), nullptr,
                                        fg_hudless_target_rtv_heap_->GetCPUDescriptorHandleForHeapStart());
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = fg_hudless_target_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    constexpr float kBlack[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    command_list_->ClearRenderTargetView(rtv, kBlack, 0, nullptr);
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    srv_ring_.bind(command_list_.Get());
    if (post_process_.ready()) {
        const D3D12_RESOURCE_DESC desc = fg_hudless_display_.texture->GetDesc();
        post_process_.record(command_list_.Get(), srv_ring_, fg_hudless_display_.texture.Get(),
                             static_cast<unsigned>(desc.Width), desc.Height, rtv, viewport, frame_index_,
                             kFramesInFlight);
    } else {
        const DescriptorRing::Table tbl = srv_ring_.allocate(1);
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = fg_hudless_display_.texture->GetDesc().Format;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(fg_hudless_display_.texture.Get(), &sd, tbl.cpu);
        command_list_->SetGraphicsRootSignature(s_blit_root_sig.Get());
        command_list_->SetPipelineState(blit_pipeline_.Get());
        srv_ring_.bind(command_list_.Get());
        command_list_->SetGraphicsRootDescriptorTable(0, tbl.gpu);
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, &scissor);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->DrawInstanced(3, 1, 0, 0);
    }
    invalidate_draw_bindings();
    streamline::tag_hudless(command_list_.Get(), fg_hudless_target_.Get(), host_window::backbuffer_width, host_window::backbuffer_height);
    fg_hudless_ready_ = true;
    // Back to the backbuffer for the overlays.
    const UINT buf_idx = swap_chain_->GetCurrentBackBufferIndex();
    D3D12_CPU_DESCRIPTOR_HANDLE back_rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    back_rtv.ptr += static_cast<SIZE_T>(buf_idx) * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    command_list_->OMSetRenderTargets(1, &back_rtv, FALSE, nullptr);
}

void RendererD3D12::record_scene_aa() {
    const GpuPassScope pass{*this, GpuPass::SceneAa};
    if (!post_process_.ready() || scene_temporal_ || !scene_color_ ||
        post_process_.settings().aa == PostProcessSettings::Aa::kOff) {
        return;
    }
    resolve_scene(false, true);
    // The picture the display copy takes decides AUTO.
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kDesignContentAspect);
    const D3D12_RESOURCE_DESC desc = scene_color_->GetDesc();
    static_cast<void>(post_process_.record_scene_aa(
        command_list_.Get(), srv_ring_, scene_color_.Get(), scene_rtv_, static_cast<unsigned>(desc.Width),
        desc.Height, frame_.width, frame_.height,
        static_cast<unsigned>(std::max(content.width, 1)), static_cast<unsigned>(std::max(content.height, 1)),
        frame_index_, kFramesInFlight, scene_msaa_.samples));
    if (scene_msaa_.samples > 1u) {
        scene_msaa_.color_dirty = true;
    }
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bound_rt_aux_ = 0xFFu;
}

// The wide colour targets for the live scene target's size and samples, over its depth. False when the device cannot
// make them: the scene then stays in the scene target.
bool RendererD3D12::ensure_wide_set(SceneTargetSet& set, ComPtr<ID3D12DescriptorHeap>& rtv_heap) {
    const D3D12_RESOURCE_DESC desc = scene_color_->GetDesc();
    const unsigned samples = scene_msaa_.samples;
    const bool same = set.color && set.color->GetDesc().Width == desc.Width &&
                      set.color->GetDesc().Height == desc.Height && set.msaa.samples == samples;
    if (!same) {
        if (set.color) {
            wait_for_gpu();
        }
        set.color.Reset();
        set.msaa.color.Reset();
        if (!rtv_heap) {
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.NumDescriptors = 2;
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            d3d_check(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtv_heap)), "wide scene RTV heap");
        }
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = desc.Width;
        rd.Height = desc.Height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = kSceneHdrColorFormat;
        rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv{};
        cv.Format = kSceneHdrColorFormat;
        cv.Color[3] = 1.0f;
        if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ColorWrite, &cv,
                                                    IID_PPV_ARGS(&set.color)))) {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device_->CreateRenderTargetView(set.color.Get(), nullptr, rtv);
        set.rtv = rtv;
        if (samples > 1u) {
            rd.SampleDesc.Count = samples;
            if (FAILED(create_texture(device_.Get(), &hp, D3D12_HEAP_FLAG_NONE, &rd, gpu::Use::ColorWrite,
                                                        &cv, IID_PPV_ARGS(&set.msaa.color)))) {
                set.color.Reset();
                return false;
            }
            rtv.ptr += device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            device_->CreateRenderTargetView(set.msaa.color.Get(), nullptr, rtv);
            set.rtv = rtv;
        }
        set.msaa.samples = samples;
        set.depth.Reset();
    }
    if (samples > 1u && !scene_msaa_.depth_srv) {
        write_msaa_depth_view(scene_msaa_);
    }
    set.hdr = true;
    set.map = frame_;
    set.dsv = scene_dsv_;
    set.msaa.depth = scene_msaa_.depth;
    set.msaa.dsv_heap = scene_msaa_.dsv_heap;
    set.msaa.depth_srv = scene_msaa_.depth_srv;
    set.msaa.color_dirty = false;
    set.msaa.depth_dirty = scene_msaa_.depth_dirty;
    if (set.depth != scene_depth_) {
        set.depth = scene_depth_;
        write_scene_set_views(set, set.conv_table ? *set.conv_table : allocate_persistent_descriptors(2u));
    }
    return true;
}

bool RendererD3D12::begin_wide_scene(bool wanted, const SceneClearValues* initial_clear) {
    if (!wanted || scene_hdr_ || !camera_pass_.ready() || !scene_color_ || output_scene_active_ || target_pass_active_) {
        return false;
    }
    SceneTargetSet& set = hdr_scene_scene_;
    wide_scene_used_frame_ = frame_index_;
    if (!ensure_wide_set(set, hdr_scene_rtv_heap_)) {
        return false;
    }
    if (initial_clear != nullptr) {
        const GpuPassScope pass{*this, GpuPass::Camera};
        swap_scene_with(set);
        command_list_->ClearRenderTargetView(scene_rtv_, initial_clear->color.data(), 0u, nullptr);
        command_list_->ClearDepthStencilView(scene_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                             initial_clear->reversed_depth, 0u, 0u, nullptr);
        scene_msaa_.color_dirty = scene_msaa_.samples > 1u;
        scene_msaa_.depth_dirty = scene_msaa_.samples > 1u;
        invalidate_draw_bindings();
        bound_rt_aux_ = 0xFFu;
        bind_scene();
        return true;
    }
    record_hdr_begin();
    return true;
}

void RendererD3D12::record_hdr_begin() {
    const GpuPassScope pass{*this, GpuPass::Camera};
    SceneTargetSet& set = hdr_scene_scene_;
    // What the scene target holds goes on under the scene.
    resolve_scene(false, true);
    const D3D12_RESOURCE_DESC desc = scene_color_->GetDesc();
    SceneCamera::WidenInputs in{};
    in.picture = scene_color_.Get();
    in.rtv = set.rtv;
    in.width = static_cast<unsigned>(desc.Width);
    in.height = desc.Height;
    in.samples = scene_msaa_.samples;
    static_cast<void>(camera_pass_.widen(command_list_.Get(), srv_ring_, in));
    set.msaa.color_dirty = scene_msaa_.samples > 1u;
    set.msaa.depth_dirty = scene_msaa_.depth_dirty;
    swap_scene_with(set);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    bound_rt_aux_ = 0xFFu;
    bind_scene();
}

void RendererD3D12::end_wide_scene() {
    if (!scene_hdr_) {
        return;
    }
    record_hdr_end();
}

void RendererD3D12::record_hdr_end() {
    if (!scene_hdr_) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Camera};
    resolve_scene(true, true);
    SceneTargetSet& set = hdr_scene_scene_;
    swap_scene_with(set);
    scene_msaa_.depth_dirty = set.msaa.depth_dirty;
    const bool resolve_output = set.msaa.samples > 1u && output_frame_for_window() == frame_ &&
                                ensure_output_scene(frame_);
    if (resolve_output) {
        swap_scene_sets();
        const auto far_values = make_scene_clear_values(0u, 0u);
        command_list_->ClearDepthStencilView(scene_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                             far_values.reversed_depth, 0u, 0u, nullptr);
    }
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now - camera_metered_).count();
    camera_metered_ = now;
    const D3D12_RESOURCE_DESC desc = set.color->GetDesc();
    SceneCamera::Inputs in{};
    in.wide = set.color.Get();
    in.wide_ms = set.msaa.samples > 1u ? set.msaa.color.Get() : nullptr;
    in.depth = set.depth.Get();
    in.rtv = scene_rtv_;
    in.width = static_cast<unsigned>(desc.Width);
    in.height = desc.Height;
    in.samples = set.msaa.samples;
    in.resolve = resolve_output;
    // After a load or a long pause the eye starts open.
    in.seconds = seconds > 1.0f ? -1.0f : seconds;
    static_cast<void>(camera_pass_.record(command_list_.Get(), srv_ring_, in));
    if (scene_msaa_.samples > 1u) {
        scene_msaa_.color_dirty = true;
    }
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bound_rt_aux_ = 0xFFu;
    bind_scene();
}

RendererD3D12::RtCommands RendererD3D12::rt_commands_begin() {
    rt_outer_pass_ = current_pass_;
    gpu_pass(GpuPass::RayTracing);
    return {command_list_.Get(), &srv_ring_};
}

// A pass leaves its own pipeline bound: the next draw binds the scene's again.
void RendererD3D12::rt_commands_end() {
    gpu_pass(rt_outer_pass_);
    invalidate_draw_bindings();
}

bool RendererD3D12::rt_depth_begin(rt::TextureView& depth, unsigned& width, unsigned& height, rt::ImageFormat& color) {
    if (!scene_depth_ || !scene_color_ || scene_msaa_.samples > 1u) {
        return false;
    }
    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(scene_depth_.Get(), gpu::Use::DepthWrite, gpu::Use::ShaderRead);
    uses.issue();
    const D3D12_RESOURCE_DESC desc = scene_depth_->GetDesc();
    depth = {scene_depth_.Get(), static_cast<std::uint32_t>(kSceneDepthSrvFormat)};
    width = static_cast<unsigned>(desc.Width);
    height = desc.Height;
    color = scene_color_->GetDesc().Format == kSceneHdrColorFormat ? rt::ImageFormat::Rgba16f : rt::ImageFormat::Rgb10a2;
    return true;
}

void RendererD3D12::rt_depth_end() {
    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(scene_depth_.Get(), gpu::Use::ShaderRead, gpu::Use::DepthWrite);
    uses.issue();
}

void RendererD3D12::rt_copy_to_scene(void* image) {
    auto* const source = static_cast<ID3D12Resource*>(image);
    if (source == nullptr || !scene_color_) {
        return;
    }
    const D3D12_RESOURCE_DESC from = source->GetDesc(), to = scene_color_->GetDesc();
    if (from.Width != to.Width || from.Height != to.Height || from.Format != to.Format || to.SampleDesc.Count != 1u) {
        return;
    }
    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(scene_color_.Get(), gpu::Use::ColorWrite, gpu::Use::CopyDest);
    uses.issue();
    command_list_->CopyResource(scene_color_.Get(), source);
    uses.undo();
}

// The scene target's depth, single-sample (MSAA resolves it, sample 0), read by the occlusion; the product lands on the
// scene target's own colour target, every sample of a multisampled one.
void RendererD3D12::record_scene_ao() {
    const GpuPassScope pass{*this, GpuPass::SceneAo};
    if (!ambient_occlusion_.ready() || !scene_depth_ || !scene_color_) {
        return;
    }
    resolve_scene(true, false);
    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(scene_depth_.Get(), gpu::Use::DepthWrite, gpu::Use::ShaderRead);
    uses.issue();
    const D3D12_RESOURCE_DESC desc = scene_depth_->GetDesc();
    AmbientOcclusion::Inputs in{};
    in.depth = scene_depth_.Get();
    in.width = static_cast<unsigned>(desc.Width);
    in.height = desc.Height;
    in.color_rtv = scene_rtv_;
    in.samples = scene_msaa_.samples;
    in.hdr = scene_hdr_;
    in.view = scene_ao_view_;
    in.ssao = get_render_config().ambient_occlusion_ssao;
    in.objects = &scene_ao_objects_;
    in.ring = &matrix_ring_;
    static_cast<void>(ambient_occlusion_.record(command_list_.Get(), srv_ring_, in, frame_index_, kFramesInFlight));
    uses.undo();
    if (scene_msaa_.samples > 1u) {
        scene_msaa_.color_dirty = true;
    }
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bound_rt_aux_ = 0xFFu;
}


// The game's bloom on the scene target's single-sample colour (MSAA resolves it first); the composite lands on every sample of
// a multisampled scene target.
void RendererD3D12::game_bloom(std::uint32_t kind, std::uint32_t a, std::uint32_t b) {
    if (!game_bloom_.ready() || !scene_color_) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Bloom};
    resolve_scene(false, true);
    const D3D12_RESOURCE_DESC desc = scene_color_->GetDesc();
    GameBloom::Inputs in{};
    in.scene_target = scene_color_.Get();
    in.rtv = scene_rtv_;
    in.samples = scene_msaa_.samples;
    in.hdr = scene_hdr_;
    in.scale = static_cast<float>(frame_.height / kAuthoredLines);
    in.frame_width = std::min(frame_.width, static_cast<unsigned>(desc.Width));
    in.frame_height = std::min(frame_.height, static_cast<unsigned>(desc.Height));
    if (kind == NEBULA_PASS_BLOOM_HOLD) {
        if (scene_msaa_.samples <= 1u && !scene_hdr_) {
            // The scene and the bloom's image trade places instead of the scene being copied. The game draws the
            // bloom's objects on black into the new target, over the same depth.
            gpu::TextureRef picture =
                swap_scene_color(game_bloom_.take_kept_image(command_list_.Get(), in, frame_index_, kFramesInFlight));
            game_bloom_.keep_picture(std::move(picture), in);
            clear_scene(0u, true, true, false);
            return;
        }
        static_cast<void>(game_bloom_.keep(command_list_.Get(), in, frame_index_, kFramesInFlight));
        return;
    }
    {
        GameBloom::Params p{};
        p.threshold = a & 0xFFu;
        p.intensity = a >> 24u;
        if (kind == NEBULA_PASS_BLOOM_NORMAL) {
            p.kind = GameBloom::Kind::Normal;
            p.tap1 = (a >> 8u) & 0xFFu;
            p.tap2 = (a >> 16u) & 0xFFu;
            p.ring_x = std::bit_cast<float>(b);
        } else {
            p.kind = GameBloom::Kind::Simple;
            p.channel = (a >> 8u) & 0xFFu;
            p.tap1 = (a >> 16u) & 0xFFu;
            p.ring_y = std::bit_cast<float>(b);
        }
        if (!(p.ring_x > 0.0f && p.ring_x < 16.0f) || !(p.ring_y > 0.0f && p.ring_y < 16.0f)) {
            p.ring_x = p.ring_y = 1.0f;
        }
        p.strength = static_cast<float>(std::min(get_render_config().bloom, kMaxBloom)) / 100.0f;
        if (!game_bloom_.apply(command_list_.Get(), srv_ring_, in, p, frame_index_, kFramesInFlight)) {
            return;
        }
    }
    if (scene_msaa_.samples > 1u) {
        scene_msaa_.color_dirty = true;
    }
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bound_rt_aux_ = 0xFFu;
}

bool RendererD3D12::begin_output_scene(bool resolve_msaa_only) {
    if (resolve_msaa_only && (output_scene_active_ || scene_temporal_ || scene_msaa_.samples <= 1u ||
                             output_frame_for_window() != frame_)) {
        return false;
    }
    const bool temporal = scene_temporal_;
    scene_temporal_ = false;
    // Nebula's screens with small text are drawn at the window's resolution whatever the HUD's (sharp_2d): at the
    // internal scale their letters would lose strokes on a wide or large window. Otherwise the game's layouts keep at
    // least the design's density on a window wider than 16:9 or narrower than 4:3, never above the window's.
    const bool full = resolve_msaa_only || temporal || (get_render_config().hud_output_resolution || render_internal_height(get_render_config()) != 0u) || sharp_2d_requested();
    if (output_scene_active_ || !full) {
        return false;
    }
    // A temporal upscaler outputs the internal resolution; the 2D layer alone
    // goes to the window's.
    const FrameMap output = temporal ? FrameMap(temporal_output_w_, temporal_output_h_) : output_frame_for_window();
    if (output.empty() || output.width < frame_.width || (output == frame_ && !temporal && !resolve_msaa_only) ||
        !ensure_output_scene(output)) {
        temporal_history_valid_ = false;
        return false;
    }
    scene_temporal_ = temporal;
    const bool upscaled = temporal && record_temporal_upscale(output);
    scene_temporal_ = false;
    if (upscaled) {
        swap_scene_sets();
        const auto far_values = make_scene_clear_values(0u, 0u);
        command_list_->ClearDepthStencilView(
            scene_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            far_values.reversed_depth, 0u, 0u, nullptr);
        take_fg_hudless_scene();
        return true;
    }
    // The whole scene scene target, resampled into the output target with the display
    // copy's identity settings (no filter, gamma 1.0, alpha kept).
    FramePassRequest params{};
    const unsigned w = output.width;
    const unsigned h = output.height;
    const FramePassGeometry sampling = frame_pass_geometry(w, h, frame_);
    record_conversion_blit(alt_scene_.color.Get(), w, h, {}, kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    swap_scene_sets();
    const auto far_values = make_scene_clear_values(0u, 0u);
    command_list_->ClearDepthStencilView(
        scene_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        far_values.reversed_depth, 0u, 0u, nullptr);
    return true;
}

void RendererD3D12::end_output_scene() {
    if (output_scene_active_) {
        swap_scene_sets();
    }
}

void RendererD3D12::bind_scene() {
    command_list_->OMSetRenderTargets(1, &scene_rtv_, FALSE, &scene_dsv_);
    bound_rt_aux_ = 0u;
    const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(frame_.width), static_cast<float>(frame_.height), 0.0f, 1.0f};
    const D3D12_RECT sci{0, 0, static_cast<LONG>(frame_.width), static_cast<LONG>(frame_.height)};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sci);
    encoder_.forget_rects();
}

void RendererD3D12::clear_scene(
    std::uint32_t clear_rgba,
    bool color_enable, bool alpha_enable, bool depth_enable,
    unsigned rect_x, unsigned rect_y, unsigned rect_w, unsigned rect_h,
    std::uint8_t pixel_format) {
    if (scene_msaa_.samples > 1u) {
        scene_msaa_.color_dirty |= color_enable || alpha_enable;
        scene_msaa_.depth_dirty |= depth_enable;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = scene_rtv_;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = scene_dsv_;

    // Scope the clear to the copy's src rect (scaled), per hardware.
    const LONG scene_w = static_cast<LONG>(frame_.width);
    const LONG scene_h = static_cast<LONG>(frame_.height);
    D3D12_RECT rect{0, 0, scene_w, scene_h};
    UINT num_rects = 0;
    const D3D12_RECT* rects = nullptr;
    if (rect_w != 0 && rect_h != 0) {
        const ScaledSceneRect scaled = rect_pixels(
            static_cast<std::int32_t>(rect_x),
            static_cast<std::int32_t>(rect_y),
            static_cast<std::int32_t>(rect_x + rect_w),
            static_cast<std::int32_t>(rect_y + rect_h),
            frame_);
        rect = D3D12_RECT{
            scaled.left, scaled.top, scaled.right, scaled.bottom};
        if (rect.right <= rect.left || rect.bottom <= rect.top) {
            return;
        }
        num_rects = 1;
        rects = &rect;
    }

    const auto clear = make_scene_clear_values(clear_rgba, pixel_format);
    if (color_enable || alpha_enable) {
        if (pixel_format != 1u) alpha_enable = true;
        const float* col = clear.color.data();
        if (color_enable && alpha_enable) {
            command_list_->ClearRenderTargetView(rtv, col, num_rects, rects);
        } else {
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            bound_rt_aux_ = 0u;
            const D3D12_VIEWPORT vp{
                0.0f, 0.0f,
                static_cast<float>(scene_w), static_cast<float>(scene_h),
                0.0f, 1.0f};
            command_list_->RSSetViewports(1, &vp);
            command_list_->RSSetScissorRects(1, &rect);
            command_list_->SetGraphicsRootSignature(s_clear_root_sig.Get());
            command_list_->SetPipelineState(clear_pipeline(color_enable));
            command_list_->SetGraphicsRoot32BitConstants(0, 4, col, 0);
            command_list_->IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            command_list_->DrawInstanced(3, 1, 0, 0);

            command_list_->SetGraphicsRootSignature(root_signature_.Get());
            invalidate_draw_bindings();
            bind_scene();
        }
    }
    if (depth_enable) {
        // Reversed depth: far is 0, which the flipped compares pass.
        command_list_->ClearDepthStencilView(
            dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, clear.reversed_depth, 0, num_rects, rects);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE RendererD3D12::allocate_conversion_rtv(
    ID3D12Resource* resource) {
    if (resource == nullptr || conv_rtv_heap_ == nullptr || conv_scratch_rtv_.ptr == 0u) {
        throw std::runtime_error(
            "[RendererD3D12] conversion RTV descriptor unavailable");
    }
    // OMSetRenderTargets snapshots this CPU descriptor at recording time.
    // Reuse only if the shared slot still describes this exact resource.
    static_cast<void>(conv_scratch_rtv_slot_.bind(
        device_.Get(), resource, conv_scratch_rtv_));
    return conv_scratch_rtv_;
}

// Records the conversion blit (see kConvPs).  Handles the scene target source
// transitions; the caller transitions `dest` to RENDER_TARGET first and back
// after.  Restores the root signature + scene target binding before returning.
void RendererD3D12::record_conversion_blit(
    ID3D12Resource* dest,
    unsigned dest_w,
    unsigned dest_h,
    D3D12_CPU_DESCRIPTOR_HANDLE dest_rtv,
    std::uint8_t copy_format,
    float src_x,
    float src_y,
    float step_x,
    float step_y) {
    resolve_scene(false);
    ID3D12Resource* src = scene_color_.Get();

    // scene target -> shader resource.
    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(src, gpu::Use::ColorWrite, gpu::Use::PixelRead);
    uses.issue();

    const D3D12_GPU_DESCRIPTOR_HANDLE src_srv = srv_ring_.gpu_at(conv_scene_color_index_);

    // CPU RTVs are consumed at recording time. Empty-handle callers use the
    // same shared slot writer, keeping its cached resource identity coherent.
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dest_rtv;
    if (rtv.ptr == 0u) {
        rtv = allocate_conversion_rtv(dest);
    }

    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    const D3D12_VIEWPORT vp{
        0.0f, 0.0f,
        static_cast<float>(dest_w), static_cast<float>(dest_h),
        0.0f, 1.0f};
    const D3D12_RECT sci{
        0, 0, static_cast<LONG>(dest_w), static_cast<LONG>(dest_h)};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sci);
    command_list_->SetGraphicsRootSignature(s_conv_root_sig.Get());
    // A display copy one scene target pixel to one display picture pixel without gamma: its filter alone (display_filter_shader_source).
    const bool display_filter = copy_format == kCopyLayoutDisplay && step_x == 1.0f &&
                                step_y == 1.0f &&
                                display_filter_pipeline_ != nullptr && dest->GetDesc().Format == kSceneColorFormat;
    command_list_->SetPipelineState(display_filter ? display_filter_pipeline_.Get() : conv_pipeline_.Get());

    const SceneConversionShaderConstants constants =
        make_scene_conversion_constants(copy_format, src_x, src_y, step_x, step_y);
    command_list_->SetGraphicsRoot32BitConstants(0, kSceneConversionConstantCount, &constants, 0);
    srv_ring_.bind(command_list_.Get());
    command_list_->SetGraphicsRootDescriptorTable(1, src_srv);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);

    // scene target back to its steady state.
    ++frame_passes_drawn_;
    uses.undo();

    // Restore the game drawing state (root signature changed; scene target RTV/DSV and full
    // viewport/scissor re-bound; per-draw viewport/scissor follow).
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bind_scene();
}

bool RendererD3D12::draw_frame_pass(
    const FramePassRequest& params, TextureStore& textures) {
    const GpuPassScope pass{*this, GpuPass::FramePass};
    const FrameExtent extent = frame_pass_extent(frame_);
    const UINT w = extent.width;
    const UINT h = extent.height;
    if (w == 0 || h == 0) {
        return false;
    }
    ++frame_pass_requests_;

    // Keyed by the texture and its channels.
    const std::uint64_t dest_key =
        static_cast<std::uint64_t>(params.key) |
        (static_cast<std::uint64_t>(params.channels) << 48);

    FramePassTarget& dest = frame_pass_targets_[dest_key];
    const bool created =
        !dest.texture || dest.width != w || dest.height != h;
    if (created) {
        if (dest.texture) {
            retired_targets_[frame_slot_].push_back(
                std::move(dest.texture));
        }
        dest.rtv = {};
        dest.rtv_valid = false;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = kSceneColorFormat;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        std::uint64_t allocation_bytes = 0u;
        const HRESULT create_hr = frame_pass_heap_.create(
            rd, gpu::Use::ColorWrite, nullptr, dest.texture,
            allocation_bytes);
        if (SUCCEEDED(create_hr)) {
            // A placed render target starts with undefined contents and must
            // be cleared, discarded or copied before any other use. The code
            // below expects the shader-resource state.
            command_list_->DiscardResource(dest.texture.Get(), nullptr);
            BarriersD3D12 uses{command_list_.Get()};
            uses.texture(dest.texture.Get(), gpu::Use::ColorWrite, gpu::Use::PixelRead);
            uses.issue();
        }
        if (FAILED(create_hr)) {
            dump_device_removal(device_.Get());
            if (device_->GetDeviceRemovedReason() != S_OK) {
                std::ostringstream message;
                message << "[frame-pass] device removed while allocating a "
                        << w << 'x' << h << " copy destination";
                throw std::runtime_error(message.str());
            }
            // Out of memory: skip this copy (its texture keeps its previous
            // contents) rather than ending the game.
            frame_pass_targets_.erase(dest_key);
            static std::atomic<unsigned> reported{0u};
            if (reported.fetch_add(1u, std::memory_order_relaxed) < 8u) {
                std::cerr << "[frame-pass] dest texture allocation failed hr=0x"
                          << std::hex << static_cast<unsigned long>(create_hr)
                          << " addr=0x" << params.key << std::dec
                          << " size=" << w << 'x' << h << "; copy skipped\n";
            }
            return false;
        }
        dest.width  = w;
        dest.height = h;
    }
    // This descriptor shares its slot with display picture and other texture copies;
    // destination-local validity cannot prove its current contents.
    dest.rtv = allocate_conversion_rtv(dest.texture.Get());

    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(dest.texture.Get(), gpu::Use::PixelRead, gpu::Use::ColorWrite);
    uses.issue();

    const FramePassGeometry sampling =
        frame_pass_geometry(w, h, frame_);

    record_conversion_blit(dest.texture.Get(), w, h, dest.rtv, params.channels, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);

    uses.undo();

    // The key's picture is the latest copy: format-keyed destinations alternate at one address.
    return publish_frame_pass(params, dest.texture, textures);
}

bool RendererD3D12::record_screen_probes(std::size_t slot, std::span<const ScreenProbe> probes, bool compared) {
    (void)compared;
    if (slot >= probe_slots_.size()) probe_slots_.resize(slot + 1u);
    ScreenProbeSlot& out = probe_slots_[slot];
    if (out.fence != 0u || probes.empty()) {
        return false;
    }
    const std::uint64_t bytes = probes.size() * sizeof(float);
    if (out.bytes < bytes) {
        D3D12_HEAP_PROPERTIES heap{};
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = bytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        d3d_check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr, IID_PPV_ARGS(&out.readback)),
                  "screen probe readback");
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        d3d_check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                   IID_PPV_ARGS(&out.values)),
                  "screen probe values");
        out.mapped = reinterpret_cast<const float*>(pin_readback_mapping(out.readback.Get()));
        if (out.mapped == nullptr) throw std::runtime_error("screen probe readback could not be mapped");
        out.bytes = bytes;
    }
    const UploadRing::Allocation pixels = constant_ring_.allocate(probes.size() * sizeof(ScreenProbePixel), 16u);
    for (std::size_t i = 0; i < probes.size(); ++i) {
        const ScreenProbePixel pixel = screen_probe_pixel(probes[i], frame_);
        std::memcpy(pixels.cpu + i * sizeof(ScreenProbePixel), &pixel, sizeof(pixel));
    }

    ID3D12GraphicsCommandList* const list = command_list_.Get();
    resolve_scene(true, false);  // depth alone
    BarriersD3D12 depth{list};
    depth.texture(scene_depth_.Get(), gpu::Use::DepthWrite, gpu::Use::ComputeRead);
    depth.issue();

    const std::uint32_t count = static_cast<std::uint32_t>(probes.size());
    list->SetComputeRootSignature(probe_root_signature_.Get());
    list->SetPipelineState(probe_pipeline_.Get());
    srv_ring_.bind(list);
    list->SetComputeRoot32BitConstants(0, 1, &count, 0);
    list->SetComputeRootDescriptorTable(1, srv_ring_.gpu_at(conv_scene_depth_index_));
    list->SetComputeRootShaderResourceView(2, pixels.gpu);
    list->SetComputeRootUnorderedAccessView(3, out.values->GetGPUVirtualAddress());
    list->Dispatch((count + kScreenProbeGroup - 1u) / kScreenProbeGroup, 1, 1);

    // The depth goes back with the values' change to the copy, in one call.
    BarriersD3D12 after{list};
    after.texture(scene_depth_.Get(), gpu::Use::ComputeRead, gpu::Use::DepthWrite);
    after.buffer(out.values.Get(), gpu::Use::ComputeWrite, gpu::Use::CopySource);
    after.issue();
    list->CopyBufferRegion(out.readback.Get(), 0, out.values.Get(), 0, bytes);
    BarriersD3D12 values{list};
    values.buffer(out.values.Get(), gpu::Use::CopySource, gpu::Use::ComputeWrite);
    values.issue();

    // The drawing state as the scene's draws expect it.
    list->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bind_scene();
    out.fence = kProbesInFrame;  // fenced at end_frame
    return true;
}

const float* RendererD3D12::screen_probes_finished(std::size_t slot) {
    ScreenProbeSlot& out = probe_slots_[slot];
    if (out.fence == 0u || out.fence == kProbesInFrame || completed_fence_value() < out.fence) {
        return nullptr;
    }
    out.fence = 0u;
    return out.mapped;
}

// A readback heap may stay mapped while the GPU writes it (D3D12 leaves the
// synchronization to the fences the callers already wait on). The reference
// taken here is never released; the resource's destruction ends the mapping.
const std::uint8_t* RendererD3D12::pin_readback_mapping(ID3D12Resource* readback) {
    if (readback == nullptr) return nullptr;
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(readback->Map(0, &no_read, &mapped))) return nullptr;
    return static_cast<const std::uint8_t*>(mapped);
}

void RendererD3D12::set_debug_capture_paths(
    std::string scene_ppm_path,
    std::string backbuffer_ppm_path) {
    next_scene_capture_path_ = std::move(scene_ppm_path);
    next_backbuffer_capture_path_ = std::move(backbuffer_ppm_path);
}

void RendererD3D12::reap_debug_capture_writers(bool wait_for_all) {
    for (auto it = debug_ppm_writers_.begin();
         it != debug_ppm_writers_.end();) {
        if (!wait_for_all &&
            it->completion.wait_for(std::chrono::seconds(0)) !=
                std::future_status::ready) {
            ++it;
            continue;
        }
        try {
            it->completion.get();
        } catch (const std::exception& error) {
            std::cerr << '[' << it->tag << "] failed path=" << it->path
                      << " error=" << error.what() << '\n';
            it = debug_ppm_writers_.erase(it);
            continue;
        }
        std::cerr << '[' << it->tag << "] wrote " << it->path << '\n';
        it = debug_ppm_writers_.erase(it);
    }
}

void RendererD3D12::queue_debug_ppm_capture(
    const char* tag,
    const std::string& path,
    const std::uint8_t* source,
    unsigned width,
    unsigned height,
    unsigned row_pitch,
    bool ten_bit) {
    constexpr std::size_t kMaximumPendingDebugCaptures = 8u;
    reap_debug_capture_writers(false);
    if (debug_ppm_writers_.size() >= kMaximumPendingDebugCaptures) {
        std::cerr << '[' << tag << "] skipped=writer-queue-full path="
                  << path << '\n';
        return;
    }
    if (source == nullptr || width == 0u || height == 0u ||
        row_pitch < width * 4u) {
        throw std::runtime_error("invalid mapped debug PPM capture");
    }
    const std::size_t source_row_bytes =
        static_cast<std::size_t>(width) * 4u;
    if (height > std::numeric_limits<std::size_t>::max() / source_row_bytes) {
        throw std::runtime_error("debug PPM capture size overflow");
    }
    std::vector<std::uint8_t> rgba(
        source_row_bytes * static_cast<std::size_t>(height));
    if (ten_bit) {
        ten_bit_to_rgba8(source, rgba.data(), width, height, row_pitch, static_cast<unsigned>(source_row_bytes));
    } else {
        for (unsigned y = 0u; y < height; ++y) {
            std::memcpy(
                rgba.data() + static_cast<std::size_t>(y) * source_row_bytes,
                source + static_cast<std::size_t>(y) * row_pitch,
                source_row_bytes);
        }
    }

    DebugPpmWriter writer{};
    writer.tag = tag;
    writer.path = path;
    writer.completion = std::async(
        std::launch::async,
        [path, width, height, rgba = std::move(rgba)]() {
            FILE* capture = nullptr;
            if (fopen_s(&capture, path.c_str(), "wb") != 0 ||
                capture == nullptr) {
                throw std::runtime_error("could not open output file");
            }
            const auto close_capture = [&capture]() noexcept {
                if (capture != nullptr) {
                    std::fclose(capture);
                    capture = nullptr;
                }
            };
            try {
                if (std::fprintf(capture, "P6\n%u %u\n255\n", width, height) <
                    0) {
                    throw std::runtime_error("could not write PPM header");
                }
                std::vector<std::uint8_t> rgb(
                    static_cast<std::size_t>(width) * 3u);
                for (unsigned y = 0u; y < height; ++y) {
                    const auto* source_row = rgba.data() +
                        static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(width) * 4u;
                    for (unsigned x = 0u; x < width; ++x) {
                        rgb[x * 3u + 0u] = source_row[x * 4u + 0u];
                        rgb[x * 3u + 1u] = source_row[x * 4u + 1u];
                        rgb[x * 3u + 2u] = source_row[x * 4u + 2u];
                    }
                    if (std::fwrite(rgb.data(), 1u, rgb.size(), capture) !=
                        rgb.size()) {
                        throw std::runtime_error("could not write PPM pixels");
                    }
                }
                if (std::fclose(capture) != 0) {
                    capture = nullptr;
                    throw std::runtime_error("could not finalize output file");
                }
                capture = nullptr;
            } catch (...) {
                close_capture();
                throw;
            }
        });
    debug_ppm_writers_.push_back(std::move(writer));
}

bool RendererD3D12::debug_copy_scene_to_readback() {
    // The readback decodes the 10-bit scene target (R10G10B10A2): the wide scene target's dumps are taken after its scene's end.
    if (scene_hdr_) {
        return false;
    }
    resolve_scene(false);
    debug_log_readback();
    if (readback_pending_) {
        std::cerr << "[scene-dump] skipped=prior-readback-pending path="
                  << next_scene_capture_path_ << '\n';
        return false;
    }
    const UINT w = frame_.width;
    const UINT h = frame_.height;
    const UINT row_pitch =
        (w * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);

    if (!readback_buf_) {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width            = static_cast<UINT64>(row_pitch) * h;
        rd.Height           = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device_->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&readback_buf_)))) {
            std::cerr << "[scene-dump] skipped=readback-allocation-failed path="
                      << next_scene_capture_path_ << '\n';
            return false;
        }
    }

    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(scene_color_.Get(), gpu::Use::ColorWrite, gpu::Use::CopySource);
    uses.issue();

    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = scene_color_.Get();
    src.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback_buf_.Get();
    dst.Type      = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format   = kSceneColorFormat;
    dst.PlacedFootprint.Footprint.Width    = w;
    dst.PlacedFootprint.Footprint.Height   = h;
    dst.PlacedFootprint.Footprint.Depth    = 1;
    dst.PlacedFootprint.Footprint.RowPitch = row_pitch;
    command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    uses.undo();
    readback_pending_ = true;
    readback_fence_value_ = 0u;
    readback_capture_path_ = next_scene_capture_path_;
    return true;
}

void RendererD3D12::debug_log_readback() {
    reap_debug_capture_writers(false);
    if (!readback_pending_ || !readback_buf_) {
        return;
    }
    if (readback_fence_value_ == 0u ||
        completed_fence_value() < readback_fence_value_) {
        return;
    }
    readback_pending_ = false;
    readback_fence_value_ = 0u;

    const UINT w = frame_.width;
    const UINT h = frame_.height;
    const UINT row_pitch =
        (w * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
        ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);

    void* mapped = nullptr;
    D3D12_RANGE range{0, static_cast<SIZE_T>(row_pitch) * h};
    if (FAILED(readback_buf_->Map(0, &range, &mapped))) {
        readback_capture_path_.clear();
        return;
    }
    bool mapped_active = true;
    const D3D12_RANGE no_write{0, 0};
    const ScopeExit unmap_readback([&]() noexcept {
        if (mapped_active) readback_buf_->Unmap(0, &no_write);
    });
    std::uint64_t nonblack = 0, alpha_hits = 0, rs = 0, gs = 0, bs = 0;
    std::uint8_t rmax = 0, gmax = 0, bmax = 0, amax = 0;
    const auto* base = static_cast<const std::uint8_t*>(mapped);
    for (UINT y = 0; y < h; y += 4) {           // sample every 4th row/col
        const std::uint8_t* row = base + static_cast<std::size_t>(y) * row_pitch;
        for (UINT x = 0; x < w; x += 4) {
            std::uint32_t word = 0u;
            std::memcpy(&word, row + x * 4u, 4u);
            const auto r = static_cast<std::uint8_t>((word & 1023u) >> 2u);
            const auto g = static_cast<std::uint8_t>((word >> 10u & 1023u) >> 2u);
            const auto bl = static_cast<std::uint8_t>((word >> 20u & 1023u) >> 2u);
            const auto a = static_cast<std::uint8_t>((word >> 30u) * 85u);
            rs += r; gs += g; bs += bl;
            rmax = std::max(rmax, r); gmax = std::max(gmax, g);
            bmax = std::max(bmax, bl); amax = std::max(amax, a);
            if (r > 8 || g > 8 || bl > 8) ++nonblack;
            if (a > 8) ++alpha_hits;
        }
    }
    const std::uint64_t samples =
        (static_cast<std::uint64_t>((h + 3) / 4)) * ((w + 3) / 4);

    if (!readback_capture_path_.empty()) {
        queue_debug_ppm_capture(
            "scene-dump",
            readback_capture_path_,
            base,
            w,
            h,
            row_pitch,
            true);
    }

    readback_buf_->Unmap(0, &no_write);
    mapped_active = false;
    readback_capture_path_.clear();
    std::cerr << "[scene-dump] nonblack=" << nonblack << "/" << samples
              << " alpha=" << alpha_hits
              << " mean=(" << rs / samples << "," << gs / samples << ","
              << bs / samples << ") max=("
              << static_cast<unsigned>(rmax) << ","
              << static_cast<unsigned>(gmax) << ","
              << static_cast<unsigned>(bmax) << ","
              << static_cast<unsigned>(amax) << ")\n";
}

bool RendererD3D12::present(
    const DisplayTexture* frame, bool capture_backbuffer, bool black) {
    const bool can_blit =
        frame != nullptr && frame->texture && blit_pipeline_ &&
        s_blit_root_sig;
    ++presented_frames_;
    if (!can_blit) {
        throw std::runtime_error(
            "present requested without a valid display picture blit source");
    }
    const DisplayTexture* source = frame;
    if (capture_backbuffer) {
        if (selected_display_readback_pending_ && selected_display_readback_fence_ != 0u) {
            wait_for_fence_value(selected_display_readback_fence_);
        }
        if (backbuffer_readback_pending_ && backbuffer_readback_fence_value_ != 0u) {
            wait_for_fence_value(backbuffer_readback_fence_value_);
        }
        debug_log_backbuffer_readback();
        debug_log_selected_display_readback();
        if (backbuffer_readback_pending_ || selected_display_readback_pending_ ||
            next_backbuffer_capture_path_.empty()) {
            std::cerr << "[backbuffer-dump] skipped="
                      << (next_backbuffer_capture_path_.empty()
                              ? "no-output-path" : "prior-readback-pending")
                      << " path=" << next_backbuffer_capture_path_ << '\n';
            capture_backbuffer = false;
        } else {
            capture_backbuffer = debug_copy_selected_display_to_readback(*source);
        }
    }

    const auto ultrawide =
        galaxy::experimental_effective_host_aspect();
    const UINT buf_idx = swap_chain_->GetCurrentBackBufferIndex();
    const UINT rtv_stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE back_rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    back_rtv.ptr += static_cast<SIZE_T>(buf_idx) * rtv_stride;

    // The back buffer is drawn, then presented.
    BarriersD3D12 back{command_list_.Get()};
    back.texture(backbuffers_[buf_idx].Get(), gpu::Use::Present, gpu::Use::ColorWrite);
    back.issue();

    command_list_->OMSetRenderTargets(1, &back_rtv, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    const auto content_aspect=galaxy::presentation_content_aspect(
        ultrawide ? ultrawide->content_aspect : galaxy::kDesignContentAspect,source->prerecorded_movie);
    galaxy::ContentViewport content_viewport = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width),static_cast<int>(host_window::backbuffer_height),
        content_aspect);
    if (content_viewport.width <= 0 || content_viewport.height <= 0) {
        // fit_content_viewport rejects degenerate extents (the swap chain can be
        // 1x1 mid-resize); the picture then fills the backbuffer.
        content_viewport = galaxy::ContentViewport{
            0, 0, static_cast<int>(host_window::backbuffer_width),
            static_cast<int>(host_window::backbuffer_height)};
    }
    (void)galaxy::g_presentation_content.publish(
        static_cast<int>(host_window::backbuffer_width),static_cast<int>(host_window::backbuffer_height),
        content_viewport);
    if (frame_generation_running()) {
        fsr_rect_[0] = static_cast<unsigned>(content_viewport.left);
        fsr_rect_[1] = static_cast<unsigned>(content_viewport.top);
        fsr_rect_[2] = static_cast<unsigned>(content_viewport.width);
        fsr_rect_[3] = static_cast<unsigned>(content_viewport.height);
        streamline::tag_backbuffer_region(command_list_.Get(), static_cast<unsigned>(content_viewport.left),
                                          static_cast<unsigned>(content_viewport.top),
                                          static_cast<unsigned>(content_viewport.width),
                                          static_cast<unsigned>(content_viewport.height));
    }
    const bool content_has_borders =
        content_viewport.left != 0 || content_viewport.top != 0 ||
        content_viewport.width != static_cast<int>(host_window::backbuffer_width) ||
        content_viewport.height != static_cast<int>(host_window::backbuffer_height);

    if (black || content_has_borders) {
        constexpr float kBlack[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        command_list_->ClearRenderTargetView(back_rtv, kBlack, 0, nullptr);
    }

    const D3D12_VIEWPORT vp{
        static_cast<float>(content_viewport.left),
        static_cast<float>(content_viewport.top),
        static_cast<float>(content_viewport.width),
        static_cast<float>(content_viewport.height),
        0.0f,
        1.0f};
    const D3D12_RECT sci{
        static_cast<LONG>(content_viewport.left),
        static_cast<LONG>(content_viewport.top),
        static_cast<LONG>(content_viewport.left + content_viewport.width),
        static_cast<LONG>(content_viewport.top + content_viewport.height)};

    // Both shader-visible heaps stay bound: the blit uses static samplers, but
    // the overlays and game recording expect the sampler heap.
    const GpuPassScope present_pass{*this, GpuPass::Present};
    srv_ring_.bind(command_list_.Get());
    if (black) {
        // The frame shows black : the display picture is not scanned out.
    } else if (post_process_.ready()) {
        // The picture: an display picture copy's whole texture, or the top rows of a handed-over scene target.
        post_process_.record(
            command_list_.Get(), srv_ring_, source->texture.Get(), source->width, source->height,
            back_rtv, vp, frame_index_, kFramesInFlight, hdr_active_ ? hdr_output_ : PostOutput{});
        // The overlay reads a copy: publish when what it shows changes.
        const PostProcessStatus& post = post_process_.status();
        static PostProcessStatus published{};
        if (post.last_run != published.last_run || post.last_wanted != published.last_wanted ||
            post.last_supersampled != published.last_supersampled || post.live_aa != published.live_aa ||
            post.live_quality != published.live_quality || published.frames == 0u ||
            post.aa_skipped_frames != published.aa_skipped_frames) {
            published = post;
            set_post_process_status(post);
        }
    } else {
        // One-slot SRV in the frame's ring for the display picture texture.
        const DescriptorRing::Table tbl = srv_ring_.allocate(1);
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format                    = source->texture->GetDesc().Format;
        sd.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels       = 1;
        device_->CreateShaderResourceView(source->texture.Get(), &sd, tbl.cpu);
        command_list_->SetGraphicsRootSignature(s_blit_root_sig.Get());
        command_list_->SetPipelineState(blit_pipeline_.Get());
        srv_ring_.bind(command_list_.Get());
        command_list_->SetGraphicsRootDescriptorTable(0, tbl.gpu);
        command_list_->RSSetViewports(1, &vp);
        command_list_->RSSetScissorRects(1, &sci);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->DrawInstanced(3, 1, 0, 0);
    }
    invalidate_draw_bindings();
    if (!black) {
        present_fg_hudless(*frame, vp, sci);
    }

    host_window::native_settings_present();

    // A capture is the SDR picture whatever the swap chain is: the HDR10 buffer itself is not a picture the gates compare.
    ID3D12Resource* capture_from = nullptr;
    if (capture_backbuffer) {
        capture_from = hdr_active_ ? hdr_capture_picture(*source, vp) : backbuffers_[buf_idx].Get();
        if (capture_from == nullptr) {
            std::cerr << "[backbuffer-dump] skipped=no-sdr-picture-for-hdr\n";
            capture_backbuffer = false;
        }
    }
    if (capture_backbuffer) {
        if (backbuffer_readback_pending_) {
            throw std::runtime_error(
                "backbuffer capture requested while a prior readback is pending");
        }
        const UINT w = host_window::backbuffer_width;
        const UINT h = host_window::backbuffer_height;
        const UINT row_pitch =
            (w * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
            ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
        const UINT64 required_bytes = static_cast<UINT64>(row_pitch) * h;
        if (!backbuffer_readback_buf_ ||
            backbuffer_readback_buf_->GetDesc().Width < required_bytes) {
            backbuffer_readback_buf_.Reset();
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = required_bytes;
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            d3d_check(
                device_->CreateCommittedResource(
                    &hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                    IID_PPV_ARGS(&backbuffer_readback_buf_)),
                "Create backbuffer readback");
        }

        BarriersD3D12 capture{command_list_.Get()};
        capture.texture(capture_from, gpu::Use::ColorWrite, gpu::Use::CopySource);
        capture.issue();

        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = capture_from;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = backbuffer_readback_buf_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = kBackbufferFormat;
        dst.PlacedFootprint.Footprint.Width = w;
        dst.PlacedFootprint.Footprint.Height = h;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = row_pitch;
        command_list_->CopyTextureRegion(
            &dst, 0, 0, 0, &src, nullptr);

        capture.undo();
        backbuffer_readback_pending_ = true;
        backbuffer_readback_fence_value_ = 0u;
        backbuffer_readback_width_ = w;
        backbuffer_readback_height_ = h;
        backbuffer_readback_row_pitch_ = row_pitch;
        backbuffer_readback_capture_path_ = next_backbuffer_capture_path_;
    }

    back.undo();
    return capture_backbuffer;
}

// Captures the presented display picture itself, in the same command list (and fence) as the
// final blit. The source is held until the copy completes; nothing waits here.
bool RendererD3D12::debug_copy_selected_display_to_readback(const DisplayTexture& source) {
    debug_log_selected_display_readback();
    if (selected_display_readback_pending_ || next_backbuffer_capture_path_.empty()) {
        std::cerr << "[selected-display-capture] skipped="
                  << (next_backbuffer_capture_path_.empty()
                          ? "no-output-path" : "prior-readback-pending")
                  << " path=" << next_backbuffer_capture_path_ << '\n';
        return false;
    }
    const auto desc = source.texture->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM) ||
        desc.Width != source.width || desc.Height < source.height ||
        source.width == 0u || source.height == 0u) {
        throw std::runtime_error("selected display picture capture has inconsistent extent/format");
    }
    const UINT pitch = (source.width * 4u + 255u) & ~255u;
    const UINT64 bytes = static_cast<UINT64>(pitch) * source.height;
    if (!selected_display_readback_buf_ || selected_display_readback_buf_->GetDesc().Width < bytes) {
        selected_display_readback_buf_.Reset();
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = bytes;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d3d_check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
            &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&selected_display_readback_buf_)), "Create selected display picture readback");
    }
    BarriersD3D12 uses{command_list_.Get()};
    uses.texture(source.texture.Get(), gpu::Use::PixelRead, gpu::Use::CopySource);
    uses.issue();
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = source.texture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = selected_display_readback_buf_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = desc.Format;
    selected_display_readback_ten_bit_ = desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM;
    dst.PlacedFootprint.Footprint.Width = source.width;
    dst.PlacedFootprint.Footprint.Height = source.height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = pitch;
    // The picture's rows (a handed-over scene target is taller).
    const D3D12_BOX picture{0u, 0u, 0u, source.width, source.height, 1u};
    command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, &picture);
    uses.undo();
    selected_display_readback_source_ = source.texture;
    selected_display_readback_width_ = source.width;
    selected_display_readback_height_ = source.height;
    selected_display_readback_pitch_ = pitch;
    selected_display_readback_path_ = next_backbuffer_capture_path_ + ".display.ppm";
    selected_display_readback_fence_ = 0u;
    selected_display_readback_pending_ = true;
    std::cerr << "[selected-display-capture] slot=" << source.slot
              << " resource=0x" << std::hex << reinterpret_cast<std::uintptr_t>(source.texture.Get())
              << std::dec << " serial=" << source.picture_number << " stamp=" << source.frame_stamp
              << " extent=" << source.width << 'x' << source.height
              << " path=" << selected_display_readback_path_ << '\n';
    return true;
}

void RendererD3D12::debug_log_selected_display_readback() {
    if (!selected_display_readback_pending_ || selected_display_readback_fence_ == 0u ||
        completed_fence_value() < selected_display_readback_fence_) return;
    const SIZE_T bytes = static_cast<SIZE_T>(selected_display_readback_pitch_) *
        selected_display_readback_height_;
    void* mapped = nullptr;
    const D3D12_RANGE range{0, bytes};
    d3d_check(selected_display_readback_buf_->Map(0, &range, &mapped), "Map selected display picture readback");
    try {
        queue_debug_ppm_capture("selected-display-dump", selected_display_readback_path_,
            static_cast<const std::uint8_t*>(mapped), selected_display_readback_width_,
            selected_display_readback_height_, selected_display_readback_pitch_,
            selected_display_readback_ten_bit_);
    } catch (...) {
        const D3D12_RANGE no_write{0, 0};
        selected_display_readback_buf_->Unmap(0, &no_write);
        throw;
    }
    const D3D12_RANGE no_write{0, 0};
    selected_display_readback_buf_->Unmap(0, &no_write);
    selected_display_readback_pending_ = false;
    selected_display_readback_fence_ = 0u;
    selected_display_readback_source_.Reset();
    selected_display_readback_path_.clear();
}

void RendererD3D12::debug_log_backbuffer_readback() {
    reap_debug_capture_writers(false);
    if (!backbuffer_readback_pending_ || !backbuffer_readback_buf_) {
        return;
    }
    if (backbuffer_readback_fence_value_ == 0u ||
        completed_fence_value() < backbuffer_readback_fence_value_) {
        return;
    }
    backbuffer_readback_pending_ = false;
    backbuffer_readback_fence_value_ = 0u;

    // A resize may have completed after this copy was recorded. Decode using
    // the captured footprint rather than the current swap-chain extent.
    const UINT w = backbuffer_readback_width_;
    const UINT h = backbuffer_readback_height_;
    const UINT row_pitch = backbuffer_readback_row_pitch_;
    if (w == 0u || h == 0u || row_pitch == 0u) {
        throw std::runtime_error(
            "pending backbuffer readback has no captured footprint");
    }
    const SIZE_T byte_count = static_cast<SIZE_T>(row_pitch) * h;
    void* mapped = nullptr;
    const D3D12_RANGE read_range{0, byte_count};
    if (FAILED(
            backbuffer_readback_buf_->Map(
                0, &read_range, &mapped))) {
        backbuffer_readback_capture_path_.clear();
        backbuffer_readback_width_ = 0;
        backbuffer_readback_height_ = 0;
        backbuffer_readback_row_pitch_ = 0;
        return;
    }

    bool mapped_active = true;
    const D3D12_RANGE no_write{0, 0};
    const ScopeExit unmap_readback([&]() noexcept {
        if (mapped_active) backbuffer_readback_buf_->Unmap(0, &no_write);
    });
    const auto* base = static_cast<const std::uint8_t*>(mapped);
    if (!backbuffer_readback_capture_path_.empty()) {
        queue_debug_ppm_capture(
            "backbuffer-dump",
            backbuffer_readback_capture_path_,
            base,
            w,
            h,
            row_pitch);
    }

    backbuffer_readback_capture_path_.clear();
    backbuffer_readback_width_ = 0;
    backbuffer_readback_height_ = 0;
    backbuffer_readback_row_pitch_ = 0;

    backbuffer_readback_buf_->Unmap(0, &no_write);
    mapped_active = false;
}

}  // namespace galaxy::render
