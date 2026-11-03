// RendererD3D12: device and swap chain, scene target targets and copies, draws, presentation, and the
// replayed sub-frames of frame interpolation.
#include "galaxy/render/utility_shaders.h"
#include "galaxy/run_options.h"
#include "galaxy/render/renderer_d3d12.h"
#include "galaxy/render/interp_eval.h"
#include "galaxy/render/host_window_win32.h"
#include "galaxy/render/rt_materials.h"
#include "galaxy/crash_notes.h"
#include "galaxy/render/display_modes.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/render_api.h"
#include "galaxy/render/game_asset_extract.h"
#include "galaxy/render/galaxy_select.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/curved_surfaces.h"
#include "galaxy/render/streamline_bridge.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/render/draw_constants.h"
#include "galaxy/motion_signals.h"
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

#ifndef GALAXY_WITH_STREAMLINE_BRIDGE
#define GALAXY_WITH_STREAMLINE_BRIDGE 0
#endif
#if GALAXY_WITH_STREAMLINE_BRIDGE != 0 && !defined(GALAXY_STREAMLINE_BRIDGE_READY)
#error "GALAXY_WITH_STREAMLINE_BRIDGE requires the real Streamline bridge to define GALAXY_STREAMLINE_BRIDGE_READY"
#endif

std::uint64_t elapsed_us(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - begin).count());
}

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

// GALAXY_GPU_PREFERENCE=low selects the minimum-power (integrated) adapter first.
DXGI_GPU_PREFERENCE adapter_preference() {
    char value[16]{};
    std::size_t length = 0;
    if (::galaxy::run_option_read(&length, value, sizeof(value), ::galaxy::run_options().gpu_preference) == 0 &&
        length > 1 && (value[0] == 'l' || value[0] == 'L' || value[0] == 'm' || value[0] == 'M')) {
        return DXGI_GPU_PREFERENCE_MINIMUM_POWER;
    }
    return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
}

bool trace_present_stats_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return ::galaxy::run_option_read(&length, value, sizeof(value), ::galaxy::run_options().trace_present_stats) == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
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

bool DescriptorRing::initialize(
    ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
    unsigned descriptors_per_frame,
    unsigned frames_in_flight,
    unsigned persistent_descriptors) {
    const std::uint64_t total = static_cast<std::uint64_t>(persistent_descriptors) +
        static_cast<std::uint64_t>(descriptors_per_frame) * frames_in_flight;
    if (device == nullptr || frames_in_flight == 0u || total == 0u ||
        total > std::numeric_limits<UINT>::max()) {
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = type;
    dhd.NumDescriptors = static_cast<UINT>(total);
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    // Publish a complete heap/stride/base tuple only on success. A failed
    // initialization must not pair an old heap with new cursor limits.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    if (FAILED(device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&heap)))) {
        return false;
    }
    const auto cpu_base = heap->GetCPUDescriptorHandleForHeapStart();
    const auto gpu_base = heap->GetGPUDescriptorHandleForHeapStart();
    const auto increment = device->GetDescriptorHandleIncrementSize(type);
    heap_ = std::move(heap);
    cpu_base_ = cpu_base;
    gpu_base_ = gpu_base;
    increment_ = increment;
    persistent_descriptors_ = persistent_descriptors;
    persistent_cursor_ = 0u;
    descriptors_per_frame_ = descriptors_per_frame;
    frames_in_flight_ = frames_in_flight;
    segment_base_ = cursor_ = 0u;
    frame_ready_ = false;
    return true;
}

void DescriptorRing::begin_frame(unsigned frame_slot) {
    if (!heap_ || frame_slot >= frames_in_flight_) {
        throw std::runtime_error("DescriptorRing invalid frame slot");
    }
    segment_base_ = persistent_descriptors_ +
        frame_slot * descriptors_per_frame_;
    cursor_       = segment_base_;
    frame_ready_ = true;
}

DescriptorRing::Table DescriptorRing::allocate_persistent(unsigned count) {
    if (!heap_) {
        throw std::runtime_error("DescriptorRing not initialized");
    }
    if (count > persistent_descriptors_ - persistent_cursor_) {
        throw std::runtime_error(
            "DescriptorRing persistent region exhausted: request " +
            std::to_string(count) + " descriptors at used " +
            std::to_string(persistent_cursor_) + "/" +
            std::to_string(persistent_descriptors_));
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_base_;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = gpu_base_;
    cpu.ptr += static_cast<SIZE_T>(persistent_cursor_) * increment_;
    gpu.ptr += static_cast<UINT64>(persistent_cursor_) * increment_;
    persistent_cursor_ += count;
    return Table{cpu, gpu};
}

DescriptorRing::Table DescriptorRing::allocate(unsigned count) {
    if (!frame_ready_) {
        throw std::runtime_error("DescriptorRing allocation before begin_frame");
    }
    if (count > descriptors_per_frame_ - (cursor_ - segment_base_)) {
        throw std::runtime_error(
            "DescriptorRing exhausted: request " + std::to_string(count) +
            " descriptors at frame-used " +
            std::to_string(cursor_ - segment_base_) + "/" +
            std::to_string(descriptors_per_frame_));
    }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = cpu_base_;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = gpu_base_;
    cpu.ptr += static_cast<SIZE_T>(cursor_) * increment_;
    gpu.ptr += static_cast<UINT64>  (cursor_) * increment_;
    cursor_ += count;
    return Table{cpu, gpu};
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
static constexpr std::size_t kVertexRingBytes = 32u * 1024u * 1024u;
static constexpr std::size_t kIndexRingBytes = 12u * 1024u * 1024u;
static constexpr std::size_t kConstantRingBytes = 12u * 1024u * 1024u;
static constexpr std::size_t kMatrixRingBytes = 8u * 1024u * 1024u;
static constexpr unsigned    kSrvPerFrame = 32768u;
// The descriptor ring's persistent prefix, outside the rewound frame segments: the scene target
// conversion sources (the scene target's views, the output scene target's, the MSAA depth's, then the HDR scene
// EFBs', live and sub-frame: 10). TextureCache's own SRV indices are independent of this prefix.
static constexpr unsigned    kPersistentSrvDescriptors = 128u;

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
    // the previous targets move to scene_carry_*, whose contents begin_frame
    // copies into the new ones.
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
        const HRESULT hr = device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, IID_PPV_ARGS(&color));
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
        const HRESULT hr = device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &drd,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv, IID_PPV_ARGS(&depth));
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
    // A picture the scene target still owes is its colour (carry_over_scene_contents carries it); the target the scene target continued on
    // goes with the GPU drained.
    scene_carry_color_readable_ = scene_owed_ != nullptr;
    scene_carry_color_ = scene_owed_ ? std::move(scene_owed_) : std::move(scene_color_);
    scene_carry_depth_ = std::move(scene_depth_);
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
    if (!create_scene_msaa(scene_msaa_, scene_depth_.Get(), wanted_msaa_samples(), scene_rtv_, scene_dsv_)) {
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

bool RendererD3D12::create_scene_msaa(SceneMsaa& msaa, ID3D12Resource* depth, unsigned samples,
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
    rd.Width = depth->GetDesc().Width;
    rd.Height = depth->GetDesc().Height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kSceneColorFormat;
    rd.SampleDesc.Count = samples;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = kSceneColorFormat;
    cv.Color[3] = 1.0f;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                &cv, IID_PPV_ARGS(&msaa.color)))) {
        return false;
    }
    rd.Format = kSceneDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kSceneDepthFormat;
    dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE,
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
    color_view.Format = kSceneColorFormat;
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
        msaa.depth_srv = srv_ring_.allocate_persistent(1u);
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = kSceneDepthSrvFormat;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device_->CreateShaderResourceView(msaa.depth.Get(), &view, msaa.depth_srv->cpu);
}

namespace {


}  // namespace

void RendererD3D12::resolve_scene(bool depth, bool color_wanted) {
    settle_owed_scene();
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
        bound_viewport_valid_ = false;
        bound_scissor_valid_ = false;
        bound_scissor_input_valid_ = false;
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
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for (auto& barrier : barriers) {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        barriers[0].Transition.pResource = msaa.color.Get();
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
        barriers[1].Transition.pResource = scene_color_.Get();
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
        list->ResourceBarrier(2u, barriers);
        list->ResolveSubresource(scene_color_.Get(), 0u, msaa.color.Get(), 0u,
                                 scene_hdr_ ? kSceneHdrColorFormat : kSceneColorFormat);
        for (auto& barrier : barriers) {
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        }
        list->ResourceBarrier(2u, barriers);
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
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource = msaa.depth.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1u, &barrier);
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
        list->SetGraphicsRootDescriptorTable(0, msaa.depth_srv->gpu);
        list->DrawInstanced(3, 1, 0, 0);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        list->ResourceBarrier(1u, &barrier);
    }
}

// After an internal-resolution change the new scene target starts with the previous targets' contents,
// resampled. The game expects the scene target it left at the end of the last frame (normally the display
// copy's clear, whose alpha its shadow and glow passes read); zero-filled targets would show as a
// dark picture with stray geometry.
void RendererD3D12::carry_over_scene_contents() {
    settle_owed_scene();
    if (!scene_carry_color_ || !scene_carry_depth_ || scene_msaa_.samples > 1u) {
        if (scene_msaa_.samples > 1u && scene_carry_color_) {
            const auto far_values = make_scene_clear_values(0u, 0u);
            const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            command_list_->ClearRenderTargetView(scene_rtv_, black, 0u, nullptr);
            command_list_->ClearDepthStencilView(scene_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                                 far_values.reversed_depth, 0u, 0u, nullptr);
            scene_msaa_.color_dirty = scene_msaa_.depth_dirty = true;
            retired_frame_pass_targets_[frame_slot_].push_back(std::move(scene_carry_color_));
            retired_frame_pass_targets_[frame_slot_].push_back(std::move(scene_carry_depth_));
        }
        scene_carry_color_.Reset();
        scene_carry_depth_.Reset();
        scene_carry_color_readable_ = false;
        return;
    }
    if (!scene_carry_depth_pso_) {
        const UtilityShaderBytes vs = utility_shader(UtilityShader::BlitVs);
        const UtilityShaderBytes ps = utility_shader(UtilityShader::DepthCarryPs);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = s_blit_root_sig.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = FALSE;
        pd.DepthStencilState.DepthEnable = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 0;
        pd.DSVFormat = kSceneDepthFormat;
        pd.SampleDesc.Count = 1;
        if (FAILED(device_->CreateGraphicsPipelineState(
                &pd, IID_PPV_ARGS(&scene_carry_depth_pso_)))) {
            scene_carry_depth_pso_.Reset();
        }
    }

    const DescriptorRing::Table table = srv_ring_.allocate(2);
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kSceneColorFormat;
    device_->CreateShaderResourceView(scene_carry_color_.Get(), &view, table.cpu);
    D3D12_CPU_DESCRIPTOR_HANDLE depth_cpu = table.cpu;
    depth_cpu.ptr += stride;
    D3D12_GPU_DESCRIPTOR_HANDLE depth_gpu = table.gpu;
    depth_gpu.ptr += stride;
    view.Format = kSceneDepthSrvFormat;
    device_->CreateShaderResourceView(scene_carry_depth_.Get(), &view, depth_cpu);

    D3D12_RESOURCE_BARRIER to_read[2]{};
    for (auto& barrier : to_read) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    to_read[0].Transition.pResource = scene_carry_depth_.Get();
    to_read[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    to_read[1].Transition.pResource = scene_carry_color_.Get();
    to_read[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list_->ResourceBarrier(scene_carry_color_readable_ ? 1u : 2u, to_read);
    scene_carry_color_readable_ = false;

    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(frame_.width),
        static_cast<float>(frame_.height), 0.0f, 1.0f};
    const D3D12_RECT scissor{
        0, 0, static_cast<LONG>(frame_.width),
        static_cast<LONG>(frame_.height)};
    command_list_->SetGraphicsRootSignature(s_blit_root_sig.Get());
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->OMSetRenderTargets(1, &scene_rtv_, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    command_list_->SetPipelineState(blit_pipeline_.Get());
    command_list_->SetGraphicsRootDescriptorTable(0, table.gpu);
    command_list_->DrawInstanced(3, 1, 0, 0);
    if (scene_carry_depth_pso_) {
        command_list_->OMSetRenderTargets(0, nullptr, FALSE, &scene_dsv_);
        bound_rt_aux_ = 0u;
        command_list_->SetPipelineState(scene_carry_depth_pso_.Get());
        command_list_->SetGraphicsRootDescriptorTable(0, depth_gpu);
        command_list_->DrawInstanced(3, 1, 0, 0);
    } else {
        const auto far_values = make_scene_clear_values(0u, 0u);
        command_list_->ClearDepthStencilView(
            scene_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            far_values.reversed_depth, 0u, 0u, nullptr);
    }
    // The old targets are read by this frame's command list.
    retired_frame_pass_targets_[frame_slot_].push_back(std::move(scene_carry_color_));
    retired_frame_pass_targets_[frame_slot_].push_back(std::move(scene_carry_depth_));
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
}

HRESULT create_draw_root_signature(
    ID3D12Device* device,
    ID3D12RootSignature** root_signature) noexcept {
    if (device == nullptr || root_signature == nullptr) {
        return E_INVALIDARG;
    }
    *root_signature = nullptr;

    // root signature:
    //   [0] Root CBV b0,space0  VS+GS - VertexConstants
    //   [1] Root CBV b1,space0  VS+PS - PixelConstants (a native material's constants: both stages)
    //   [2] Descriptor table   SRV t0..t7,space0  PS - textures
    //   [3] Descriptor table   Sampler s0..s7       PS
    //   [4] Root SRV  t0,space1 VS - matrix palette
    //   [5] Root SRV  t1,space1 VS - previous vertices (sub-frame blending)
    //   [6] Root constants b2   VS - vertex blend mode, t, verdict slot
    //   [7] Root SRV  t2,space1 VS - vertex blend verdicts

    D3D12_DESCRIPTOR_RANGE tex_range{};
    tex_range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    tex_range.NumDescriptors     = 8;
    tex_range.BaseShaderRegister = 0;
    tex_range.RegisterSpace      = 0;
    tex_range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE samp_range{};
    samp_range.RangeType          = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samp_range.NumDescriptors     = 8;
    samp_range.BaseShaderRegister = 0;
    samp_range.RegisterSpace      = 0;
    samp_range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[9]{};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace  = 0;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges   = &tex_range;
    params[2].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    params[3].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges   = &samp_range;
    params[3].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

    params[4].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[4].Descriptor.ShaderRegister = 0;
    params[4].Descriptor.RegisterSpace  = 1;
    // The matrix palette also holds the lights, which per-pixel lighting
    // reads in the pixel shader.
    params[4].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    params[5].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[5].Descriptor.ShaderRegister = 1;
    params[5].Descriptor.RegisterSpace  = 1;
    params[5].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    params[6].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[6].Constants.ShaderRegister = 2;
    params[6].Constants.RegisterSpace  = 0;
    params[6].Constants.Num32BitValues = 5;
    params[6].ShaderVisibility         = D3D12_SHADER_VISIBILITY_VERTEX;

    params[7].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[7].Descriptor.ShaderRegister = 2;
    params[7].Descriptor.RegisterSpace  = 1;
    params[7].ShaderVisibility          = D3D12_SHADER_VISIBILITY_VERTEX;

    // The rows the draw's row table (t0, space1) names: the matrix ring, or a replay's output.
    params[8].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[8].Descriptor.ShaderRegister = 3;
    params[8].Descriptor.RegisterSpace  = 1;
    params[8].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 9;
    rsd.pParameters   = params;
    // Curved surfaces' hull and domain shaders read the vertex constants (b0).
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
    std::uint32_t key,
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
    const std::uint32_t wrap_s = key & 0x3u;
    const std::uint32_t wrap_t = (key >> 2) & 0x3u;
    const bool mag_linear = ((key >> 4) & 0x1u) != 0u;
    const std::uint32_t minf = (key >> 5) & 0x7u;
    const float lod_bias = static_cast<float>(
        static_cast<std::int8_t>((key >> 8) & 0xFFu)) / 32.0f;
    const float min_lod =
        static_cast<float>((key >> 16) & 0xFFu) / 16.0f;
    const float max_lod =
        static_cast<float>((key >> 24) & 0xFFu) / 16.0f;

    // The minified filter (texture_sampler.h min_filter_code): 4 linear, plus 1 the nearest level or 2 between levels.
    const bool min_linear = (minf >= 4u);
    // 0 none, 1 point, 2 linear, 3 linear without anisotropy (pack_sampler_key's arbitrary levels).
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
    desc.MipLODBias = lod_bias + cfg_bias;
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
    default_sampler_valid_.fill(false);
    for (auto& tables : sampler_tables_) tables.reset();
    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    dhd.NumDescriptors = kSamplerHeapSlots;
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    for (auto& heap : sampler_heaps_) {
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&heap)),
                  "CreateDescriptorHeap Sampler");
    }
    reset_sampler_frame();
    return true;
}

void RendererD3D12::reset_sampler_frame() {
    sampler_heap_ = sampler_heaps_[sampler_slot_];

    const RenderConfig cfg = get_render_config();
    const std::uint32_t config_key = sampler_config_key(cfg);
    // Tables are retained only in the heap they were written in (another slot's
    // tables have no descriptors here), until the sampler configuration changes.
    sampler_tables_[sampler_slot_].reset(config_key);

    default_sampler_table_ = sampler_heap_->GetGPUDescriptorHandleForHeapStart();
    if (default_sampler_valid_[sampler_slot_] &&
        default_sampler_config_keys_[sampler_slot_] == config_key) {
        return;
    }

    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_CPU_DESCRIPTOR_HANDLE base =
        sampler_heap_->GetCPUDescriptorHandleForHeapStart();

    const float lod_bias = static_cast<float>(std::clamp(cfg.texture_lod_bias, 0u, 8u)) * 0.5f;
    const unsigned anisotropy =
        std::clamp(cfg.anisotropic_filtering, 1u, 16u);

    // Default s0-s7 fallback table at slots 0-7.
    D3D12_SAMPLER_DESC sd{};
    sd.Filter         = anisotropy > 1u
        ? D3D12_FILTER_ANISOTROPIC
        : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sd.AddressU = sd.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sd.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sd.MipLODBias     = lod_bias;
    sd.MaxAnisotropy  = anisotropy;
    sd.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sd.MinLOD         = 0.0f;
    sd.MaxLOD         = D3D12_FLOAT32_MAX;
    for (unsigned i = 0; i < 8; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE h = base;
        h.ptr += static_cast<SIZE_T>(i) * stride;
        device_->CreateSampler(&sd, h);
    }
    // Slots 0..7 are never allocated dynamically. Their descriptors survive
    // this slot's fence/reset, and need rewriting only for changed settings.
    default_sampler_config_keys_[sampler_slot_] = config_key;
    default_sampler_valid_[sampler_slot_] = true;
}

// Packed sampler key layout (must match pack_sampler_key in render_core.cpp):
//   bits 0-1  wrap_s (TexWrap)           bits 2-3  wrap_t
//   bit  4    mag linear                 bits 5-7  min filter (min_filter_code; 3 in the low two bits:
//                                        arbitrary levels, no anisotropy)
//   bits 8-15  lod_bias  (s8, 1/32 LOD units)
//   bits 16-23 min_lod_x16
//   bits 24-31 max_lod_x16
D3D12_GPU_DESCRIPTOR_HANDLE RendererD3D12::sampler_table_for(
    const std::uint32_t keys[8]) {
    const RenderConfig cfg = get_render_config();
    const auto allocation = sampler_tables_[sampler_slot_].get_or_allocate(make_sampler_table_key(keys, cfg));
    const unsigned table_base = allocation.index;
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    for (unsigned i = 0; allocation.created && i < 8; ++i) {
        const D3D12_SAMPLER_DESC dyn =
            build_sampler_desc_from_key(keys[i], cfg);

        D3D12_CPU_DESCRIPTOR_HANDLE h =
            sampler_heap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(table_base + i) * stride;
        device_->CreateSampler(&dyn, h);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE gpu =
        sampler_heap_->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += static_cast<UINT64>(table_base) * stride;

    return gpu;
}

// scene target-copy conversion pipeline: root signature (16 root constants, one SRV
// table, static linear and point clamp samplers) and the blit PSO.
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
        params[0].Constants.Num32BitValues = 16;
        params[0].ShaderVisibility         = D3D12_SHADER_VISIBILITY_PIXEL;

        params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges   = &range;
        params[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC ss[2]{};
        ss[0].Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        ss[0].AddressU = ss[0].AddressV = ss[0].AddressW =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss[0].ShaderRegister   = 0;
        ss[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        ss[1] = ss[0];
        ss[1].Filter         = D3D12_FILTER_MIN_MAG_MIP_POINT;
        ss[1].ShaderRegister = 1;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters     = 2;
        rsd.pParameters       = params;
        rsd.NumStaticSamplers = 2;
        rsd.pStaticSamplers   = ss;

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
        pd.RTVFormats[0] = kSceneColorFormat;
        const UtilityShaderBytes filter = utility_shader(UtilityShader::DisplayFilterPs);
        pd.PS = {filter->GetBufferPointer(), filter->GetBufferSize()};
        pd.RTVFormats[0] = kSceneColorFormat;
        d3d_check(device_->CreateGraphicsPipelineState(
            &pd, IID_PPV_ARGS(&display_filter_pipeline_)), "CreateDisplayFilterPSO");
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
        conv_scene_source_table_ = srv_ring_.allocate_persistent(2u);
    }
    write_scene_conversion_views(*conv_scene_source_table_);
}

void RendererD3D12::write_scene_conversion_views(const DescriptorRing::Table& table) {
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kSceneColorFormat;
    device_->CreateShaderResourceView(scene_color_.Get(), &view, table.cpu);
    auto depth_cpu = table.cpu;
    depth_cpu.ptr += stride;
    view.Format = kSceneDepthSrvFormat;
    device_->CreateShaderResourceView(scene_depth_.Get(), &view, depth_cpu);
    conv_scene_color_srv_ = table.gpu;
    auto depth_gpu = table.gpu;
    depth_gpu.ptr += stride;
    conv_scene_depth_srv_ = depth_gpu;
}

bool RendererD3D12::can_hand_over_display_copy(const FramePassRequest&, const DisplayTexture& dest) const {
    if (!scene_color_ || scene_owed_ ||
        scene_carry_color_ || replay_recording_ || replay_playing_ || ring_mode_ == RingMode::Interp ||
        frame_generation_running() || output_scene_active_ || scene_msaa_.samples > 1u || scene_hdr_ || scene_temporal_ ||
        !post_process_.ready() || !post_process_.reads_held_pictures()) {
        return false;
    }
    const FrameExtent extent = frame_pass_extent(frame_);
    // The display's vertical filter is applied when the frame is presented (present: filter_display_picture).
    return dest.width == extent.width && dest.height == extent.height;
}

bool RendererD3D12::can_take_scene() const noexcept {
    return scene_color_ && !scene_carry_color_ && !replay_recording_ && !replay_playing_ && ring_mode_ != RingMode::Interp &&
           !frame_generation_running() && !output_scene_active_ && !target_pass_active_ && scene_msaa_.samples <= 1u &&
           !scene_hdr_;
}

bool RendererD3D12::take_scene(std::uint64_t key, gpu::TextureRef fresh, TextureCache& textures, gpu::TextureRef& released) {
    settle_owed_scene();
    if (const auto it = pictures_.find(key); it != pictures_.end()) {
        released = it->second.viewed;
    }
    gpu::TextureRef picture = hand_over_scene_color(std::move(fresh));
    scene_owed_.Reset();
    const FrameExtent size = frame_pass_extent(frame_);
    return publish_picture(key, size.width, size.height, std::move(picture), 0u, 0u, textures);
}

void RendererD3D12::open_frame_for_pass(std::uint64_t color_key, std::uint8_t channels, std::uint64_t depth_key,
                                        TextureCache& textures) {
    resolve_scene(depth_key != 0u);
    D3D12_RESOURCE_BARRIER barriers[2]{};
    UINT count = 0u;
    for (const auto& [resource, before, wanted] :
         {std::tuple{scene_color_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, color_key != 0u},
          std::tuple{scene_depth_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, depth_key != 0u}}) {
        if (wanted) {
            D3D12_RESOURCE_BARRIER& b = barriers[count++];
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = resource;
            b.Transition.StateBefore = before;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
    }
    command_list_->ResourceBarrier(count, barriers);
    const FrameExtent size = frame_pass_extent(frame_);
    if (color_key != 0u) {
        (void)publish_picture(color_key, size.width, size.height, scene_color_, channels, 0u, textures);
    }
    if (depth_key != 0u) {
        (void)publish_picture(depth_key, size.width, size.height, scene_depth_, 0u, 0u, textures);
    }
    frame_pass_color_key_ = color_key;
    frame_pass_depth_key_ = depth_key;
}

void RendererD3D12::close_frame_for_pass(TextureCache& textures) {
    D3D12_RESOURCE_BARRIER barriers[2]{};
    UINT count = 0u;
    for (const auto& [resource, after, key] :
         {std::tuple{scene_color_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, frame_pass_color_key_},
          std::tuple{scene_depth_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, frame_pass_depth_key_}}) {
        if (key == 0u) {
            continue;
        }
        D3D12_RESOURCE_BARRIER& b = barriers[count++];
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = resource;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        if (const auto it = pictures_.find(key); it != pictures_.end()) {
            textures.retire_view(it->second.view, std::move(it->second.viewed));
            pictures_.erase(it);
        }
    }
    if (count != 0u) {
        command_list_->ResourceBarrier(count, barriers);
    }
    frame_pass_color_key_ = 0u;
    frame_pass_depth_key_ = 0u;
    invalidate_draw_bindings();
}

gpu::TextureRef RendererD3D12::hand_over_scene_color(gpu::TextureRef fresh) {
    const GpuPassScope pass{*this, GpuPass::DisplayCopy};
    const D3D12_RESOURCE_DESC desc = scene_color_->GetDesc();
    D3D12_RESOURCE_BARRIER barriers[2]{};
    UINT barrier_count = 0;
    if (fresh) {
        const D3D12_RESOURCE_DESC got = fresh->GetDesc();
        if (got.Width != desc.Width || got.Height != desc.Height || got.Format != desc.Format ||
            got.Flags != desc.Flags || got.SampleDesc.Count != 1u) {
            throw std::runtime_error("handed-over scene target replacement has another description");
        }
        // Released pictures are kept as shader resources.
        D3D12_RESOURCE_BARRIER& b = barriers[barrier_count++];
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = fresh.Get();
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    } else {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = kSceneColorFormat;
        clear.Color[3] = 1.0f;
        d3d_check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&fresh)),
                  "Create scene target colour for a handover");
    }
    // The picture becomes an display picture: a shader resource, as presentation reads pictures.
    D3D12_RESOURCE_BARRIER& picture_barrier = barriers[barrier_count++];
    picture_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    picture_barrier.Transition.pResource = scene_color_.Get();
    picture_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    picture_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    picture_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(barrier_count, barriers);

    gpu::TextureRef picture = std::move(scene_color_);
    scene_color_ = std::move(fresh);
    // RTVs are copied when recorded: the scene target's slot takes the new target now. Its conversion views, which the GPU reads
    // when it runs, go to the next table in turn (a frame in flight still reads the one before).
    device_->CreateRenderTargetView(scene_color_.Get(), nullptr, scene_rtv_);
    if (conv_scene_table_ < conv_scene_tables_.size()) {
        conv_scene_tables_[conv_scene_table_].free_from = frame_index_ + kFramesInFlight + 1u;
    }
    std::size_t table = 0u;
    while (table < conv_scene_tables_.size() && (table == conv_scene_table_ || frame_index_ < conv_scene_tables_[table].free_from)) {
        ++table;
    }
    if (table == conv_scene_tables_.size()) {
        conv_scene_tables_.push_back({srv_ring_.allocate_persistent(2u)});
    }
    conv_scene_table_ = table;
    write_scene_conversion_views(conv_scene_tables_[table].table);
    scene_owed_ = picture;
    ++scene_handovers_;
    bind_scene();
    return picture;
}

void RendererD3D12::restore_owed_scene(const D3D12_RECT* kept) {
    ComPtr<ID3D12Resource> owed = std::move(scene_owed_);
    const D3D12_RESOURCE_DESC desc = scene_color_->GetDesc();
    const LONG width = static_cast<LONG>(desc.Width);
    const LONG height = static_cast<LONG>(desc.Height);
    // The bands around `kept` (all of the picture without it).
    D3D12_BOX boxes[4]{};
    UINT box_count = 0;
    const auto add = [&](LONG left, LONG top, LONG right, LONG bottom) {
        if (right > left && bottom > top) {
            boxes[box_count++] = D3D12_BOX{static_cast<UINT>(left), static_cast<UINT>(top), 0u,
                                           static_cast<UINT>(right), static_cast<UINT>(bottom), 1u};
        }
    };
    if (kept != nullptr) {
        const LONG left = std::clamp(kept->left, 0L, width);
        const LONG right = std::clamp(kept->right, left, width);
        const LONG top = std::clamp(kept->top, 0L, height);
        const LONG bottom = std::clamp(kept->bottom, top, height);
        add(0, 0, width, top);
        add(0, bottom, width, height);
        add(0, top, left, bottom);
        add(right, top, width, bottom);
        if (box_count == 0u) {
            ++scene_handovers_free_;
            return;
        }
    }
    D3D12_RESOURCE_BARRIER b[2]{};
    for (unsigned i = 0; i < 2u; ++i) {
        b[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    b[0].Transition.pResource = owed.Get();
    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b[1].Transition.pResource = scene_color_.Get();
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    command_list_->ResourceBarrier(2, b);
    if (kept == nullptr) {
        command_list_->CopyResource(scene_color_.Get(), owed.Get());
        ++scene_handovers_restored_;
    } else {
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = scene_color_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = owed.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        for (UINT i = 0; i < box_count; ++i) {
            command_list_->CopyTextureRegion(&dst, boxes[i].left, boxes[i].top, 0, &src, &boxes[i]);
        }
        ++scene_handovers_banded_;
    }
    for (auto& barrier : b) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    command_list_->ResourceBarrier(2, b);
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
    const float render_fraction = host_window::upscaler_render_fraction(render, wanted.width, wanted.height);
    if (render_fraction < 1.0f) {
        wanted = FrameMap(std::max(static_cast<unsigned>(std::lround(wanted.width * render_fraction)), 1u),
                          std::max(static_cast<unsigned>(std::lround(wanted.height * render_fraction)), 1u));
    }
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
    for (auto& retired : retired_frame_pass_targets_) {
        retired.clear();
    }
    for (auto& retired : retired_segment_textures_) {
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
    bound_scissor_input_map_ = FrameMap{};
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
    // Size-dependent residency: scene colour and depth (8 bytes per pixel) and at most the display picture pair plus
    // one full-picture copy (12 bytes per pixel).
    const auto bytes = [](const FrameMap& m) { return (8.0 + 12.0) * m.width * m.height; };
    const double needed = bytes(map) - bytes(frame_);
    return static_cast<double>(info.CurrentUsage) + needed <=
           0.9 * static_cast<double>(info.Budget);
}

std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>
detail::create_scene_conversion_source_views(
    ID3D12Device* device, ID3D12Resource* color, ID3D12Resource* depth,
    DescriptorRing& ring) {
    if (device == nullptr || color == nullptr || depth == nullptr) {
        throw std::runtime_error("scene target conversion source resource unavailable");
    }
    // Reserve both views together: exhaustion must not leave a partial pair.
    const auto table = ring.allocate_persistent(2u);
    const UINT stride = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto depth_cpu = table.cpu;
    depth_cpu.ptr += stride;
    auto depth_gpu = table.gpu;
    depth_gpu.ptr += stride;

    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kSceneColorFormat;
    device->CreateShaderResourceView(color, &view, table.cpu);
    view.Format = kSceneDepthSrvFormat;
    device->CreateShaderResourceView(depth, &view, depth_cpu);
    return {table.gpu, depth_gpu};
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
    pd.RTVFormats[0] = (variant & kSceneVariantHdr) != 0u ? kSceneHdrColorFormat : kSceneColorFormat;
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
    next_fence_value_ = 0;
    fence_values_.fill(0);
    timestamp_pending_.fill(false);
    has_presented_swap_chain_ = false;
    window_shown_ = false;
    const host_window::OpenedWindow opened =
        host_window::open_game_window(window_owner_, window_width, window_height);
    if (opened.window == nullptr) {
        return false;
    }
    window_ = opened.window;
    window_shown_ = opened.shown;

    // GALAXY_D3D_DEBUG: 1 enables the D3D12 debug layer, 2 also GPU-based validation.
    unsigned d3d_debug_level = 0;
    {
        char buf[16]{};
        std::size_t len = 0;
        if (::galaxy::run_option_read(&len, buf, sizeof(buf), ::galaxy::run_options().d3d_debug) == 0 &&
            len > 0) {
            d3d_debug_level =
                static_cast<unsigned>(std::strtoul(buf, nullptr, 10));
        }
    }
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
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u) {
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
                ? preferred_factory->EnumAdapterByGpuPreference(index, adapter_preference(), IID_PPV_ARGS(&candidate))
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
                    index, adapter_preference(),
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
        // Published after every failure path, for adapter_is_unified_memory().
        g_selected_adapter_desc = adapter_desc;
        g_selected_adapter_valid = true;
        host_window::rtx_adapter.store(adapter_desc.VendorId == 0x10DEu &&
                                       std::wcsstr(adapter_desc.Description, L"RTX") != nullptr,
                                       std::memory_order_relaxed);
        (void)adapter.As(&adapter3_);
        {
            char narrow[128]{};
            WideCharToMultiByte(
                CP_UTF8, 0, adapter_desc.Description, -1, narrow,
                static_cast<int>(sizeof(narrow) - 1), nullptr, nullptr);
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
            char gpu[256]{};
            std::snprintf(gpu, sizeof(gpu), "\"%s\" vram-mb=%llu vendor=0x%04X device=0x%04X", narrow,
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
                       D3D12_MESSAGE_ID, LPCSTR description, void*) {
                        if (severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                            severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                            std::cerr << "[D3D12-LIVE-ERR] " << description
                                      << '\n';
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
            // What the ray tracer starts with (SceneRayTracer::create_lighting_pipelines): DXR 1.1, and its textures,
            // material records and buffers read straight from the descriptor heap (shader model 6.6, resource binding
            // tier 3), on dedicated video memory.
            D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_6};
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            const bool offered =
                !kRayTracingParked && dxr_runtime_supported && options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 &&
                SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model,
                                                       sizeof(shader_model))) &&
                shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_6 &&
                SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) &&
                options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 && !adapter_is_unified_memory();
            host_window::ray_tracing_offered.store(offered, std::memory_order_relaxed);
            record_render_adapter(adapter_desc.Description, adapter_desc.VendorId, adapter_desc.DeviceId,
                                  adapter_desc.SubSysId, adapter_preference() == DXGI_GPU_PREFERENCE_MINIMUM_POWER,
                                  offered);
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
            host_window::ray_tracing_offered.load(std::memory_order_relaxed)
                ? "ray-traced ambient occlusion (GRAPHICS: RAY TRACING)"
            : dxr_runtime_supported ? "needs DXR 1.1, shader model 6.6, binding tier 3 and dedicated memory"
                                    : "D3D12 raytracing tier is unsupported");
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
            const bool passes = [] {
                char value[8]{};
                std::size_t length = 0u;
                return ::galaxy::run_option_read(&length, value, sizeof(value), ::galaxy::run_options().gpu_passes) == 0 && length > 1u &&
                       value[0] == '1';
            }();
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
            const bool draws = [] {
                char value[8]{};
                std::size_t length = 0u;
                return ::galaxy::run_option_read(&length, value, sizeof(value), ::galaxy::run_options().gpu_draws) == 0 && length > 1u &&
                       value[0] == '1';
            }();
            if (draws) {
                qhd.Count = kFramesInFlight * kGpuDrawSlots;
                d3d_check(device_->CreateQueryHeap(&qhd, IID_PPV_ARGS(&draw_query_heap_)), "Create draw query heap");
                rd.Width = kFramesInFlight * kGpuDrawSlots * sizeof(std::uint64_t);
                d3d_check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                           D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                           IID_PPV_ARGS(&draw_readback_)),
                          "Create draw readback");
            }
        }

        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.BufferCount  = kSwapChainBuffers;
        scd.Width        = host_window::backbuffer_width;
        scd.Height       = host_window::backbuffer_height;
        scd.Format       = kBackbufferFormat;
        scd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd.SampleDesc.Count = 1;
        scd.Scaling      = DXGI_SCALING_STRETCH;
        scd.AlphaMode    = DXGI_ALPHA_MODE_IGNORE;
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        ComPtr<IDXGISwapChain1> sc1;
        // Through Streamline's factory when it runs: its presents go through it.
        d3d_check(streamline::swap_chain_factory(factory.Get())
                      ->CreateSwapChainForHwnd(queue_.Get(), window_, &scd, nullptr, nullptr, &sc1),
                  "CreateSwapChain");
        d3d_check(sc1.As(&swap_chain_), "SwapChain As");
        {
            ComPtr<IDXGISwapChain2> swap_chain2;
            d3d_check(swap_chain_.As(&swap_chain2), "SwapChain2 As");
            d3d_check(swap_chain2->SetMaximumFrameLatency(1u),
                      "SetMaximumFrameLatency");
            frame_latency_waitable_ =
                swap_chain2->GetFrameLatencyWaitableObject();
            if (frame_latency_waitable_ == nullptr) {
                throw std::runtime_error(
                    "GetFrameLatencyWaitableObject failed");
            }
        }
        factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
        // Exclusive fullscreen is entered by the first frame's display check
        // (reconcile_display_state), once the swap chain's buffers exist.

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
        }

        for (unsigned i = 0; i < kFramesInFlight; ++i)
            d3d_check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators_[i])), "CreateCommandAllocator");
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(), nullptr, IID_PPV_ARGS(&command_list_)), "CreateCommandList");
        command_list_->SetName(L"FrameList");
        d3d_check(command_list_->Close(), "CommandList Close init");

        d3d_check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
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

        // Every segmentation (interpolation, temporal) shares one buffer per ring, so a frame-rate or upscaler
        // change only re-segments it: no 300-400 MB of upload memory created mid-game.
        const auto ring = [&](UploadRing& target, const char* name, std::size_t bytes) {
            return target.initialize(device_.Get(), name, UploadRing::budget(bytes, kFramesInFlight),
                                     kFramesInFlight, bytes);
        };
        if (!ring(vertex_ring_, "vertex", kVertexRingBytes) || !ring(index_ring_, "index", kIndexRingBytes) ||
            !ring(constant_ring_, "constant", kConstantRingBytes) || !ring(matrix_ring_, "matrix", kMatrixRingBytes)) {
            return false;
        }
        if (!srv_ring_.initialize(device_.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvPerFrame, kInterpSegments, kPersistentSrvDescriptors)) return false;

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
                  << " frames-in-flight=" << kFramesInFlight
                  << " total-mb="
                  << ((kVertexRingBytes + kIndexRingBytes + kConstantRingBytes + kMatrixRingBytes) >> 20u)
                  << '\n';

        // The settings menu's pipeline compiles its shaders: here, not inside the frame that first opens it.
        ensure_native_settings_solid_pipeline_loaded();
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

void RendererD3D12::attach_texture_uploads(TextureCache& cache) {
    cache.backend().set_upload_list(command_list_.Get());
}

gpu::Table RendererD3D12::texture_table_for(const TextureHandle* handles, unsigned count, const TextureCache& cache) {
    DescriptorRing::Table table = srv_ring_.allocate(count);
    D3D12_CPU_DESCRIPTOR_HANDLE sources[kMaxTextureMaps]{};
    const auto heap_start = cache.backend().srv_cpu_base();
    const UINT stride = cache.backend().srv_descriptor_stride();
    for (unsigned m = 0; m < count; ++m) {
        sources[m] = handles[m].resource != nullptr
            ? D3D12_CPU_DESCRIPTOR_HANDLE{heap_start.ptr + static_cast<SIZE_T>(handles[m].srv_index) * stride}
            : cache.backend().null_srv();
    }
    // CPU-only sources, one contiguous frame-owned table.
    const UINT destination_size = count;
    device_->CopyDescriptors(1, &table.cpu, &destination_size, count, sources, nullptr,
                             D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // Interpolation logs keep tables that name scene target-copy textures: sub-frames repoint those slots.
    if (replay_recording()) [[unlikely]] {
        static_assert(kMaxTextureMaps == kReplayTableSlots);
        ID3D12Resource* copies[kMaxTextureMaps]{};
        bool names_copy = false;
        for (unsigned m = 0; m < count; ++m) {
            if (handles[m].resource != nullptr && handles[m].from_capture) {
                copies[m] = handles[m].resource;
                names_copy = true;
            }
        }
        if (names_copy) {
            note_replay_texture_table(table.gpu, sources, copies);
        }
    }
    return table.gpu;
}

void RendererD3D12::shutdown() {
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
    ui_texture_pipeline_.Reset();
    ui_solid_pipeline_.Reset();
    ui_solid_root_sig_.Reset();
    ui_solid_pipeline_ready_ = false;
    ui_solid_pipeline_load_attempted_ = false;
    ui_texture_root_sig_.Reset();
    ui_texture_srv_heap_.Reset();
    ui_corner_texture_.Reset();
    ui_corner_upload_.Reset();
    ui_corner_texture_ready_ = false;
    ui_corner_texture_load_attempted_ = false;
    host_window::pending_resize_extent.store(0u, std::memory_order_relaxed);
    if (frame_latency_waitable_) {
        CloseHandle(frame_latency_waitable_);
        frame_latency_waitable_ = nullptr;
    }
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
    conv_scene_color_srv_ = {};
    conv_scene_depth_srv_ = {};
    conv_scene_source_table_.reset();
    conv_scene_tables_.clear();
    conv_scene_table_ = ~std::size_t{0};
    next_fence_value_ = 0;
    probe_target_.Reset();
    probe_rtv_heap_.Reset();
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
    for (auto& retired : retired_frame_pass_targets_) {
        retired.clear();
    }
    for (auto& retired : retired_segment_textures_) {
        retired.clear();
    }
    scene_color_.Reset();
    scene_depth_.Reset();
    for (auto& b : backbuffers_) b.Reset();
    rtv_heap_.Reset(); dsv_heap_.Reset(); sampler_heap_.Reset(); root_signature_.Reset();
    for (auto& heap : sampler_heaps_) heap.Reset();
    for (auto& tables : sampler_tables_) tables.reset();
    default_sampler_table_ = {};
    default_sampler_valid_.fill(false);
    swap_chain_.Reset(); command_list_.Reset();
    for (auto& a : allocators_) a.Reset();
    vertex_ring_.reset();
    index_ring_.reset();
    constant_ring_.reset();
    matrix_ring_.reset();
    srv_ring_.reset();
    invalidate_draw_bindings();
    last_latency_wait_us_ = last_frame_slot_wait_us_ =
        last_timestamp_read_us_ = last_begin_reset_us_ = 0;
    telemetry_window_start_qpc_ = last_present_qpc_ = 0;
    present_pacer_.reset();
    telemetry_first_serial_start_ = telemetry_display_copy_start_ = 0;
    has_presented_swap_chain_ = window_shown_ = false;
    telemetry_present_count_ = telemetry_adjacent_serial_transition_count_ = 0;
    telemetry_interval_count_ = 0;
    last_presented_display_serial_ = ~std::uint64_t{0};
    telemetry_interval_ms_ = telemetry_interval_max_ms_ = 0.0;
    telemetry_present_ms_ = telemetry_present_max_ms_ = 0.0;
    telemetry_pace_wait_ms_ = telemetry_pace_wait_max_ms_ = 0.0;
    telemetry_latency_wait_ms_ = telemetry_latency_wait_max_ms_ = 0.0;
    telemetry_gpu_ms_ = 0.0;
    telemetry_gpu_samples_ = telemetry_no_display_present_count_ =
        telemetry_backbuffer_clear_count_ = 0;
    fence_.Reset(); queue_.Reset(); device_.Reset();
    host_window::close_game_window(window_owner_);
    window_ = nullptr;
    if (shutdown_failure != nullptr) {
        std::rethrow_exception(shutdown_failure);
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
    has_presented_swap_chain_ = false;
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

void RendererD3D12::ensure_native_settings_corner_texture_loaded() {
    if (ui_corner_texture_load_attempted_) {
        return;
    }
    ui_corner_texture_load_attempted_ = true;

    // Read from the game's files. Any failure (another game revision, a missing
    // asset, an unsupported format) leaves the overlay's solid-color corners.
    if (host_window::native_settings_content_root.empty()) {
        return;
    }
    const auto tex = galaxy::render::load_game_texture(
        host_window::native_settings_content_root, "LayoutData/FileSelect.arc",
        "myfileselwinframe.tpl");
    if (!tex.has_value() || tex->width == 0 || tex->height == 0) {
        return;
    }

    try {
        D3D12_HEAP_PROPERTIES default_heap{};
        default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC tex_desc{};
        tex_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex_desc.Width = tex->width;
        tex_desc.Height = tex->height;
        tex_desc.DepthOrArraySize = 1;
        tex_desc.MipLevels = 1;
        tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        d3d_check(
            device_->CreateCommittedResource(
                &default_heap, D3D12_HEAP_FLAG_NONE, &tex_desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&ui_corner_texture_)),
            "create UI corner texture");

        const UINT row_pitch = static_cast<UINT>(
            (tex->width * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
            ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u));
        const UINT64 upload_size =
            static_cast<UINT64>(row_pitch) * tex->height;

        D3D12_HEAP_PROPERTIES upload_heap{};
        upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buf_desc{};
        buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Width = upload_size;
        buf_desc.Height = 1;
        buf_desc.DepthOrArraySize = 1;
        buf_desc.MipLevels = 1;
        buf_desc.Format = DXGI_FORMAT_UNKNOWN;
        buf_desc.SampleDesc.Count = 1;
        buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        // This renderer owns staging until its GPU drain during shutdown.
        d3d_check(
            device_->CreateCommittedResource(
                &upload_heap, D3D12_HEAP_FLAG_NONE, &buf_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&ui_corner_upload_)),
            "create UI corner texture upload buffer");

        void* mapped = nullptr;
        const D3D12_RANGE no_read{0, 0};
        d3d_check(
            ui_corner_upload_->Map(0, &no_read, &mapped),
            "map UI corner texture upload buffer");
        auto* dst_base = static_cast<std::uint8_t*>(mapped);
        for (unsigned y = 0; y < tex->height; ++y) {
            std::memcpy(
                dst_base + static_cast<std::size_t>(y) * row_pitch,
                tex->rgba8.data() + static_cast<std::size_t>(y) * tex->width * 4u,
                static_cast<std::size_t>(tex->width) * 4u);
        }
        ui_corner_upload_->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = ui_corner_texture_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = ui_corner_upload_.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = tex->width;
        src.PlacedFootprint.Footprint.Height = tex->height;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = row_pitch;
        // Recorded into the frame's open command list (present() calls this
        // while recording), so the frame fence covers the copy.
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = ui_corner_texture_.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        // SRV heap (one descriptor), root signature and pipeline.
        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
        srv_heap_desc.NumDescriptors = 1;
        srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        d3d_check(
            device_->CreateDescriptorHeap(
                &srv_heap_desc, IID_PPV_ARGS(&ui_texture_srv_heap_)),
            "create UI texture SRV heap");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping =
            D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(
            ui_corner_texture_.Get(), &srv_desc,
            ui_texture_srv_heap_->GetCPUDescriptorHandleForHeapStart());

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_DESCRIPTOR_RANGE srv_range{};
        srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srv_range.NumDescriptors = 1;
        srv_range.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.ShaderRegister = 0;
        params[0].Constants.Num32BitValues = 14;  // rect+uv+tint+screen
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &srv_range;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 2;
        rsd.pParameters = params;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers = &sampler;

        ComPtr<ID3DBlob> rs_blob, rs_err;
        d3d_check(
            D3D12SerializeRootSignature(
                &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err),
            "serialize UI texture root signature");
        d3d_check(
            device_->CreateRootSignature(
                0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                IID_PPV_ARGS(&ui_texture_root_sig_)),
            "create UI texture root signature");

        const UtilityShaderBytes vs = utility_shader(UtilityShader::UiTexturedVs);
        const UtilityShaderBytes ps = utility_shader(UtilityShader::UiTexturedPs);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = ui_texture_root_sig_.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.DepthStencilState.DepthEnable = FALSE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = kBackbufferFormat;
        pd.SampleDesc.Count = 1;
        d3d_check(
            device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&ui_texture_pipeline_)),
            "create UI texture PSO");

        // Recorded after everything that can throw, so the catch below never
        // releases a resource the command list uses.
        command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        command_list_->ResourceBarrier(1, &barrier);

        ui_corner_texture_width_ = tex->width;
        ui_corner_texture_height_ = tex->height;
        ui_corner_texture_ready_ = true;
    } catch (const std::exception& e) {
        // Not fatal: the overlay keeps its solid-color corners.
        std::cerr << "[Renderer] native settings corner texture unavailable: "
                  << e.what() << '\n';
        ui_corner_texture_.Reset();
        ui_corner_upload_.Reset();
        ui_texture_srv_heap_.Reset();
        ui_texture_root_sig_.Reset();
        ui_texture_pipeline_.Reset();
        ui_corner_texture_ready_ = false;
    }
}

void RendererD3D12::draw_ui_textured_quad(
    int x, int y, int w, int h,
    float u0, float v0, float u1, float v1,
    const std::array<float, 4>& tint) {
    if (!ui_corner_texture_ready_) {
        return;
    }
    const float constants[14] = {
        static_cast<float>(x), static_cast<float>(y),
        static_cast<float>(w), static_cast<float>(h),
        u0, v0, u1, v1,
        tint[0], tint[1], tint[2], tint[3],
        static_cast<float>(host_window::backbuffer_width), static_cast<float>(host_window::backbuffer_height),
    };
    ID3D12DescriptorHeap* heaps[] = {ui_texture_srv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootSignature(ui_texture_root_sig_.Get());
    command_list_->SetPipelineState(ui_texture_pipeline_.Get());
    command_list_->SetGraphicsRoot32BitConstants(0, 14, constants, 0);
    command_list_->SetGraphicsRootDescriptorTable(
        1, ui_texture_srv_heap_->GetGPUDescriptorHandleForHeapStart());
    command_list_->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    command_list_->DrawInstanced(4, 1, 0, 0);
    invalidate_draw_bindings();
}

void RendererD3D12::ensure_native_settings_solid_pipeline_loaded() {
    if (ui_solid_pipeline_load_attempted_) {
        return;
    }
    ui_solid_pipeline_load_attempted_ = true;

    try {
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.Constants.ShaderRegister = 0;
        param.Constants.Num32BitValues = 13; // rect/color/screen + glyph mask/mode
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 1;
        rsd.pParameters = &param;

        ComPtr<ID3DBlob> rs_blob, rs_err;
        d3d_check(
            D3D12SerializeRootSignature(
                &rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err),
            "serialize UI solid root signature");
        d3d_check(
            device_->CreateRootSignature(
                0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                IID_PPV_ARGS(&ui_solid_root_sig_)),
            "create UI solid root signature");

        const UtilityShaderBytes vs = utility_shader(UtilityShader::UiSolidVs);
        const UtilityShaderBytes ps = utility_shader(UtilityShader::UiSolidPs);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = ui_solid_root_sig_.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.DepthStencilState.DepthEnable = FALSE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = kBackbufferFormat;
        pd.SampleDesc.Count = 1;
        d3d_check(
            device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&ui_solid_pipeline_)),
            "create UI solid PSO");

        ui_solid_pipeline_ready_ = true;
    } catch (const std::exception& e) {
        std::cerr << "[Renderer] native settings solid UI pipeline unavailable: "
                  << e.what() << '\n';
        ui_solid_root_sig_.Reset();
        ui_solid_pipeline_.Reset();
        ui_solid_pipeline_ready_ = false;
    }
}

void RendererD3D12::draw_ui_solid_quad(
    int x, int y, int w, int h,
    const std::array<float, 4>& color) {
    ensure_native_settings_solid_pipeline_loaded();
    if (!ui_solid_pipeline_ready_ || w <= 0 || h <= 0) {
        return;
    }
    const int left = std::clamp(x, 0, static_cast<int>(host_window::backbuffer_width));
    const int top = std::clamp(y, 0, static_cast<int>(host_window::backbuffer_height));
    const int right = std::clamp(x + w, 0, static_cast<int>(host_window::backbuffer_width));
    const int bottom = std::clamp(y + h, 0, static_cast<int>(host_window::backbuffer_height));
    if (right <= left || bottom <= top) {
        return;
    }
    const UiSolidQuadConstants constants{
        {left, top, right - left, bottom - top},
        {color[0], color[1], color[2], color[3]},
        {host_window::backbuffer_width, host_window::backbuffer_height},
        {0u, 0u}, 0u,
    };
    command_list_->SetGraphicsRootSignature(ui_solid_root_sig_.Get());
    command_list_->SetPipelineState(ui_solid_pipeline_.Get());
    command_list_->SetGraphicsRoot32BitConstants(0, 13, &constants, 0);
    command_list_->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    command_list_->DrawInstanced(4, 1, 0, 0);
    invalidate_draw_bindings();
}

void RendererD3D12::draw_ui_glyph(
    int x, int y, int scale, std::uint64_t bitmap,
    const std::array<float, 4>& color) {
    if (scale <= 0 || bitmap == 0u || host_window::backbuffer_width == 0u ||
        host_window::backbuffer_height == 0u) return;
    ensure_native_settings_solid_pipeline_loaded();
    if (!ui_solid_pipeline_ready_) return;
    const UiSolidQuadConstants constants{
        {x, y, scale, scale},
        {color[0], color[1], color[2], color[3]},
        {host_window::backbuffer_width, host_window::backbuffer_height},
        {static_cast<std::uint32_t>(bitmap),
         static_cast<std::uint32_t>(bitmap >> 32u)}, 1u,
    };
    command_list_->SetGraphicsRootSignature(ui_solid_root_sig_.Get());
    command_list_->SetPipelineState(ui_solid_pipeline_.Get());
    command_list_->SetGraphicsRoot32BitConstants(0, 13, &constants, 0);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    // One instance per cell, row-major; unlit cells have degenerate geometry.
    command_list_->DrawInstanced(4, 35, 0, 0);
    invalidate_draw_bindings();
}

void RendererD3D12::pump_messages() {
    // The window and its modal loops belong to window_owner_.
}

bool RendererD3D12::quit_requested() const noexcept {
    return host_window::quit_request.load(std::memory_order_acquire);
}

void RendererD3D12::invalidate_draw_bindings() {
    bound_pipeline_ = nullptr;
    bound_vs_constants_ = 0;
    bound_ps_constants_ = 0;
    bound_texture_table_ = {};
    bound_sampler_table_ = {};
    bound_matrix_palette_ = 0;
    bound_matrix_rows_ = 0;
    bound_vertex_blend_valid_ = false;
    bound_topology_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    bound_vertex_view_ = {};
    bound_vertex_view_valid_ = false;
    bound_index_view_ = {};
    bound_index_view_valid_ = false;
    bound_scissor_valid_ = false;
    bound_scissor_input_valid_ = false;
    bound_viewport_valid_ = false;
}

void RendererD3D12::wait_for_frame_latency_before_present() {
    if (has_presented_swap_chain_) {
        wait_for_frame_latency_if_needed(trace_present_stats_enabled());
    }
}

bool RendererD3D12::ensure_pre_list() {
    if (pre_list_) {
        return true;
    }
    if (pre_list_failed_ || !device_) {
        return false;
    }
    pre_list_failed_ = true;
    for (unsigned slot = 0; slot < kFramesInFlight; ++slot) {
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&pre_allocators_[slot])))) {
            return false;
        }
    }
    ComPtr<ID3D12GraphicsCommandList> list;
    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, pre_allocators_[0].Get(), nullptr,
                                          IID_PPV_ARGS(&list))) ||
        FAILED(list->Close())) {
        return false;
    }
    pre_list_ = std::move(list);
    pre_list_failed_ = false;
    return true;
}

UploadBuffer RendererD3D12::create_resident_vertices(std::size_t bytes, const char* name) {
    const auto upload_only = [&] {
        UploadBuffer upload = gpu::create_upload_memory(device_.Get(), bytes, name);
        resident_upload_ = upload.buffer;
        resident_upload_mapped_ = upload.cpu;
        return upload;
    };
    if (!ensure_pre_list()) {
        return upload_only();
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> local;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                nullptr, IID_PPV_ARGS(&local)))) {
        return upload_only();
    }
    local->SetName(L"Resident decoded vertices (local)");
    resident_upload_.Reset();
    resident_upload_mapped_ = nullptr;
    resident_local_ = local.Get();
    resident_copies_.clear();
    UploadBuffer resident;
    resident.gpu = local->GetGPUVirtualAddress();
    resident.buffer = std::move(local);
    return resident;
}

std::byte* RendererD3D12::stage_resident_vertices(std::uint64_t offset, std::uint64_t bytes) {
    if (resident_upload_mapped_ != nullptr) {
        return resident_upload_mapped_ + offset;
    }
    ResidentStaging& staging = resident_staging_[frame_slot_];
    staging.staged = true;
    while (staging.current < staging.chunks.size() && staging.used + bytes > staging.chunks[staging.current].size) {
        ++staging.current;
        staging.used = 0u;
    }
    if (staging.current == staging.chunks.size()) {
        const std::uint64_t size = std::max<std::uint64_t>(bytes, kResidentStagingChunk);
        UploadBuffer memory = gpu::create_upload_memory(device_.Get(), static_cast<std::size_t>(size), "Resident vertices staging");
        if (!memory) {
            throw std::runtime_error("no memory for resident vertices");
        }
        staging.chunks.push_back({std::move(memory), size});
    }
    const UploadBuffer& chunk = staging.chunks[staging.current].memory;
    ID3D12Resource* const source = gpu::raw(chunk.buffer);
    if (!resident_copies_.empty() && resident_copies_.back().source == source &&
        resident_copies_.back().source_offset + resident_copies_.back().bytes == staging.used &&
        resident_copies_.back().offset + resident_copies_.back().bytes == offset) {
        resident_copies_.back().bytes += bytes;
    } else {
        resident_copies_.push_back({source, staging.used, offset, bytes});
    }
    std::byte* const out = chunk.cpu + staging.used;
    staging.used += bytes;
    return out;
}

bool RendererD3D12::ensure_curve_sort() {
    if (curve_sort_pso_) {
        return true;
    }
    if (curve_sort_failed_ || !device_) {
        return false;
    }
    curve_sort_failed_ = true;
    D3D12_ROOT_PARAMETER params[7]{};
    for (unsigned i = 0; i < 6u; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = i;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[6].Descriptor.ShaderRegister = 0;
    params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 7;
    rsd.pParameters = params;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&curve_sort_root_)))) {
        return false;
    }
    try {
        const UtilityShaderBytes classify = utility_shader(UtilityShader::CurveClassifyCs);
        const UtilityShaderBytes offsets = utility_shader(UtilityShader::CurveOffsetsCs);
        const UtilityShaderBytes scatter = utility_shader(UtilityShader::CurveScatterCs);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = curve_sort_root_.Get();
        pso.CS = {offsets->GetBufferPointer(), offsets->GetBufferSize()};
        if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&curve_offsets_pso_)))) {
            return false;
        }
        pso.CS = {scatter->GetBufferPointer(), scatter->GetBufferSize()};
        if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&curve_scatter_pso_)))) {
            return false;
        }
        pso.CS = {classify->GetBufferPointer(), classify->GetBufferSize()};
        if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&curve_sort_pso_)))) {
            return false;
        }
    } catch (const std::exception& error) {
        std::cerr << "[curve-sort] shader: " << error.what() << '\n';
        return false;
    }
    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC signature{};
    signature.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    signature.NumArgumentDescs = 1;
    signature.pArgumentDescs = &argument;
    if (FAILED(device_->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&curve_draw_signature_)))) {
        curve_sort_pso_.Reset();
        return false;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = kCurveSortBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    for (unsigned slot = 0; slot < kFramesInFlight; ++slot) {
        if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                                                   nullptr, IID_PPV_ARGS(&curve_sorted_[slot])))) {
            curve_sort_pso_.Reset();
            return false;
        }
    }
    if (!ensure_pre_list()) {
        curve_sort_pso_.Reset();
        return false;
    }
    if (timestamp_query_heap_) {
        D3D12_QUERY_HEAP_DESC qhd{};
        qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qhd.Count = kFramesInFlight * 2u;
        D3D12_HEAP_PROPERTIES readback{};
        readback.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd = desc;
        rd.Width = kFramesInFlight * 2u * sizeof(std::uint64_t);
        rd.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(device_->CreateQueryHeap(&qhd, IID_PPV_ARGS(&curve_sort_queries_))) ||
            FAILED(device_->CreateCommittedResource(&readback, D3D12_HEAP_FLAG_NONE, &rd,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&curve_sort_readback_)))) {
            curve_sort_queries_.Reset();
            curve_sort_readback_.Reset();
        }
    }
    curve_sort_failed_ = false;
    return true;
}

std::int32_t RendererD3D12::sort_curved(D3D12_GPU_VIRTUAL_ADDRESS vertices, D3D12_GPU_VIRTUAL_ADDRESS indices,
                                        std::uint32_t triangles, D3D12_GPU_VIRTUAL_ADDRESS constants,
                                        D3D12_GPU_VIRTUAL_ADDRESS palette, bool palette_rows) {
    const auto inside = [](D3D12_GPU_VIRTUAL_ADDRESS address, D3D12_GPU_VIRTUAL_ADDRESS base, std::uint64_t bytes) {
        return address >= base && address - base < bytes && address - base <= 0xFFFFFFFFu;
    };
    if (!pass_list_open_ || triangles == 0u || !ensure_curve_sort() ||
        !inside(vertices, resident_vertex_base_, resident_vertex_bytes_) ||
        !inside(indices, index_ring_.gpu_base(), index_ring_.total_bytes()) ||
        !inside(constants, constant_ring_.gpu_base(), constant_ring_.total_bytes()) ||
        (palette_rows && !inside(palette, matrix_ring_.gpu_base(), matrix_ring_.total_bytes()))) {
        return -1;
    }
    std::vector<CurveSortDraw>& draws = curve_sort_draws_[frame_slot_];
    if (curve_sort_vertices_[frame_slot_] != 0u && curve_sort_vertices_[frame_slot_] != resident_vertex_base_) {
        return -1;
    }
    // Its output (arguments, two lists), then its blocks' masks and offsets.
    const std::uint64_t blocks = (triangles + 63u) / 64u;
    const std::uint64_t output = 40u + std::uint64_t{24} * triangles;
    const std::uint64_t bytes = output + 12u * blocks;
    if (blocks > 1024u || curve_sorted_bytes_[frame_slot_] + bytes > kCurveSortBytes || draws.size() >= 65535u) {
        return -1;
    }
    curve_sort_vertices_[frame_slot_] = resident_vertex_base_;
    draws.push_back({static_cast<std::uint32_t>(vertices - resident_vertex_base_),
                     static_cast<std::uint32_t>(indices - index_ring_.gpu_base()), triangles,
                     static_cast<std::uint32_t>(constants - constant_ring_.gpu_base()),
                     palette_rows ? static_cast<std::uint32_t>(palette - matrix_ring_.gpu_base()) : 0u,
                     palette_rows ? 1u : 0u, static_cast<std::uint32_t>(curve_sorted_bytes_[frame_slot_]),
                     static_cast<std::uint32_t>(curve_sorted_bytes_[frame_slot_] + output)});
    curve_sorted_bytes_[frame_slot_] += bytes;
    return static_cast<std::int32_t>(draws.size() - 1u);
}

void RendererD3D12::record_pre_list() {
    const std::vector<CurveSortDraw>& draws = curve_sort_draws_[frame_slot_];
    d3d_check(pre_list_->Reset(pre_allocators_[frame_slot_].Get(), nullptr), "Pre-list Reset");
    pre_list_used_[frame_slot_] = true;
    if (!resident_copies_.empty()) {
        // Buffers start each list in the common state (decayed).
        D3D12_RESOURCE_BARRIER to_copy{};
        to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_copy.Transition.pResource = resident_local_;
        to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        pre_list_->ResourceBarrier(1, &to_copy);
        for (const ResidentCopy& copy : resident_copies_) {
            pre_list_->CopyBufferRegion(resident_local_, copy.offset, copy.source, copy.source_offset, copy.bytes);
        }
        std::swap(to_copy.Transition.StateBefore, to_copy.Transition.StateAfter);
        pre_list_->ResourceBarrier(1, &to_copy);
        resident_copies_.clear();
    }
    if (draws.empty()) {
        d3d_check(pre_list_->Close(), "Pre-list Close");
        pre_list_recorded_ = true;
        return;
    }
    pre_list_->SetPipelineState(curve_sort_pso_.Get());
    const UploadRing::Allocation records =
        constant_ring_.allocate(draws.size() * sizeof(CurveSortDraw), 256u);
    std::memcpy(records.cpu, draws.data(), draws.size() * sizeof(CurveSortDraw));
    curve_sort_blocks_.clear();
    for (std::uint32_t draw = 0; draw < draws.size(); ++draw) {
        for (std::uint32_t first = 0; first < draws[draw].triangles; first += 64u) {
            curve_sort_blocks_.push_back({draw, first});
        }
    }
    const UploadRing::Allocation blocks =
        constant_ring_.allocate(curve_sort_blocks_.size() * sizeof(curve_sort_blocks_[0]), 256u);
    std::memcpy(blocks.cpu, curve_sort_blocks_.data(), curve_sort_blocks_.size() * sizeof(curve_sort_blocks_[0]));
    ID3D12Resource* const sorted = curve_sorted_[frame_slot_].Get();
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = sorted;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    pre_list_->ResourceBarrier(1, &barrier);
    // Timed from here: the barrier waits for the list before to finish.
    if (curve_sort_queries_) {
        pre_list_->EndQuery(curve_sort_queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_slot_ * 2u);
    }
    pre_list_->SetComputeRootSignature(curve_sort_root_.Get());
    pre_list_->SetComputeRootShaderResourceView(0, records.gpu);
    pre_list_->SetComputeRootShaderResourceView(1, curve_sort_vertices_[frame_slot_]);
    pre_list_->SetComputeRootShaderResourceView(2, index_ring_.gpu_base());
    pre_list_->SetComputeRootShaderResourceView(3, constant_ring_.gpu_base());
    pre_list_->SetComputeRootShaderResourceView(4, matrix_ring_.gpu_base());
    pre_list_->SetComputeRootShaderResourceView(5, blocks.gpu);
    pre_list_->SetComputeRootUnorderedAccessView(6, sorted->GetGPUVirtualAddress());
    pre_list_->Dispatch(static_cast<UINT>(curve_sort_blocks_.size()), 1u, 1u);
    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uav.UAV.pResource = sorted;
    pre_list_->ResourceBarrier(1, &uav);
    pre_list_->SetPipelineState(curve_offsets_pso_.Get());
    pre_list_->Dispatch(static_cast<UINT>(draws.size()), 1u, 1u);
    pre_list_->ResourceBarrier(1, &uav);
    pre_list_->SetPipelineState(curve_scatter_pso_.Get());
    pre_list_->Dispatch(static_cast<UINT>(curve_sort_blocks_.size()), 1u, 1u);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    pre_list_->ResourceBarrier(1, &barrier);
    if (curve_sort_queries_) {
        pre_list_->EndQuery(curve_sort_queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_slot_ * 2u + 1u);
        pre_list_->ResolveQueryData(curve_sort_queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_slot_ * 2u, 2u,
                                      curve_sort_readback_.Get(),
                                      static_cast<UINT64>(frame_slot_) * 2u * sizeof(std::uint64_t));
    }
    d3d_check(pre_list_->Close(), "Pre-list Close");
    pre_list_recorded_ = true;
}

void RendererD3D12::gpu_pass(GpuPass pass) {
    if (pass == current_pass_) {
        return;
    }
    current_pass_ = pass;
    unsigned& marks = pass_mark_count_[frame_slot_];
    if (!pass_query_heap_ || !pass_list_open_ || marks + 1u >= kGpuPassSlots) {
        return;
    }
    command_list_->EndQuery(pass_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame_slot_ * kGpuPassSlots + marks);
    pass_marks_[frame_slot_][marks++] = pass;
}

void RendererD3D12::test_lose_device() {
    ComPtr<ID3D12Device5> device;
    if (SUCCEEDED(device_.As(&device))) {
        device->RemoveDevice();
    }
}

void RendererD3D12::begin_frame(bool present_swap_chain) {
    // How long the frames a started tracer took over lasted (the hitch a tracer's first frames could cause).
    {
        const auto now = std::chrono::steady_clock::now();
        frame_begun_ = now;
    }
    // Display and size changes apply here, between command-list recordings.
    // They wait for the GPU themselves, so they come before any per-slot state
    // below is touched.
    host_window::poll_render_config_file();
    reconcile_display_state();
    apply_pending_resize();
    if (!streamline_frame_open_) {
        // Frame generation runs with DLSS on a GPU that runs it, while it is chosen.
        const RenderConfig fg_cfg = get_render_config();
        set_frame_generation_running(streamline::frame_generation_supported() && fg_cfg.frame_generation != 0u &&
                                     fg_cfg.upscaler == 3u && streamline::dlss_supported());
        if (frame_generation_running()) {
            streamline::reflex_sleep();
        }
        streamline::marker(streamline::Marker::kSimulationStart);
        streamline_frame_open_ = true;
    }
    apply_window_size();
    prepare_temporal_upscaler();
    sync_post_process_settings();
    {
        if (temporal_interp_suspended_ && temporal_interp_key() != temporal_interp_key_) {
            // The settings changed: decided again.
            temporal_interp_suspended_ = false;
            host_window::temporal_interp_suspended.store(false, std::memory_order_relaxed);
        }
        const RingMode mode = interpolation_mode() ? RingMode::Interp
            : temporal_mode() ? RingMode::Temporal : RingMode::Normal;
        // A ring whose frames took over half their budget (or spilled) grows before the next frame: the whole
        // stage drawn for traced light, a busier scene than any before.
        const bool grow = !ring_growth_failed_ &&
            (vertex_ring_.wants_growth() || index_ring_.wants_growth() || constant_ring_.wants_growth() ||
             matrix_ring_.wants_growth());
        if (mode != ring_mode_ || grow) {
            wait_for_gpu();
            const unsigned segments = mode == RingMode::Interp ? kInterpSegments : kFramesInFlight;
            const unsigned matrix_segments = mode == RingMode::Normal ? kFramesInFlight
                : mode == RingMode::Interp ? kInterpSegments : kRingSegments;
            const auto place = [&](UploadRing& ring, const char* name, unsigned count) {
                const std::size_t had = ring.capacity();
                std::size_t capacity = ring_growth_failed_ ? had : ring.grown_capacity(count);
                if (capacity == 0u) {
                    ring_growth_failed_ = true;
                    capacity = had;
                }
                if (capacity == had) {
                    return ring.resegment(UploadRing::budget(capacity, count), count);
                }
                std::cerr << "[gpu-rings] grow " << name << " frame-high-water-kb=" << (ring.high_water() >> 10u)
                          << " budget-kb=" << (ring.bytes_per_frame() >> 10u) << " mb=" << (had >> 20u) << "->"
                          << (capacity >> 20u) << '\n';
                ring.reset();
                if (ring.initialize(device_.Get(), name, UploadRing::budget(capacity, count), count, capacity)) {
                    return true;
                }
                // Out of upload memory: the buffer it had, and no further attempts.
                ring_growth_failed_ = true;
                std::cerr << "[gpu-rings] grow " << name << " failed\n";
                return ring.initialize(device_.Get(), name, UploadRing::budget(had, count), count, had);
            };
            if (!place(vertex_ring_, "vertex", segments) ||
                !place(index_ring_, "index", segments) ||
                !place(constant_ring_, "constant", segments) ||
                !place(matrix_ring_, "matrix", matrix_segments)) {
                throw std::runtime_error("ring resize failed");
            }
            matrix_segments_ = matrix_segments;
            matrix_segment_ = 0u;
            matrix_segment_fence_.fill(0u);
            matrix_segment_used_ = false;
            matrix_segment_started_ = false;
            ring_segment_ = 0u;
            ring_segment_fence_.fill(0u);
            ring_advance_pending_ = true;
            replay_valid_ = false;
            replay_recording_ = false;
            // The logs name memory of the rings just rebuilt.
            run_valid_ = false;
            prev_run_valid_ = false;
            replay_textures_.clear();
            play_textures_.clear();
            run_textures_.clear();
            release_copy_snapshots(~0u);
            for (auto& retired : retired_segment_textures_) {
                retired.clear();
            }
            ring_mode_ = mode;
        }
    }
    frame_slot_ = static_cast<unsigned>(frame_index_ % kFramesInFlight);
    temporal_interp_ = ring_mode_ == RingMode::Interp && temporal_mode();
    if (ring_mode_ != RingMode::Interp) {
        if (replay_valid_ && frame_slot_ == replay_source_slot_) {
            // The recorded frame's ring segments are about to be rewound.
            replay_valid_ = false;
        }
        replay_recording_ = false;
    }
    last_latency_wait_us_ = 0;
    last_frame_slot_wait_us_ = 0;
    last_timestamp_read_us_ = 0;
    last_begin_reset_us_ = 0;
    const bool collect_begin_timing = trace_present_stats_enabled();
    if (present_swap_chain && has_presented_swap_chain_) {
        wait_for_frame_latency_if_needed(collect_begin_timing);
    }

    // Wait only on the slot about to be reused.
    const std::uint64_t wait_val = fence_values_[frame_slot_];
    if (wait_val != 0u) {
        const auto fence_wait_start = collect_begin_timing
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const bool timeline_slot_wait =
            galaxy::timeline::enabled() && completed_fence_value() < wait_val;
        if (timeline_slot_wait) {
            galaxy::timeline::record(galaxy::timeline::Event::SlotWaitBegin, wait_val);
        }
        wait_for_fence_value(wait_val);
        if (timeline_slot_wait) {
            galaxy::timeline::record(galaxy::timeline::Event::SlotWaitEnd, wait_val);
        }
        if (collect_begin_timing) {
            last_frame_slot_wait_us_ = elapsed_us(
                fence_wait_start,
                std::chrono::steady_clock::now());
        }
    }

    if (timestamp_pending_[frame_slot_] && timestamp_readback_ &&
        timestamp_frequency_ != 0) {
        const auto timestamp_read_start = collect_begin_timing
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
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
                telemetry_gpu_by_place_[timestamp_list_place_[frame_slot_]] += gpu_ms;
                ++telemetry_gpu_by_place_count_[timestamp_list_place_[frame_slot_]];
                ++telemetry_gpu_samples_;
            }
            const D3D12_RANGE no_write{0, 0};
            timestamp_readback_->Unmap(0, &no_write);
        }
        timestamp_pending_[frame_slot_] = false;
        if (collect_begin_timing) {
            last_timestamp_read_us_ = elapsed_us(
                timestamp_read_start,
                std::chrono::steady_clock::now());
        }
    }
    if (pass_readback_ && pass_mark_count_[frame_slot_] > 1u) {
        const unsigned marks = pass_mark_count_[frame_slot_];
        const SIZE_T offset = static_cast<SIZE_T>(frame_slot_) * kGpuPassSlots * sizeof(std::uint64_t);
        const D3D12_RANGE read_range{offset, offset + marks * sizeof(std::uint64_t)};
        void* mapped = nullptr;
        if (SUCCEEDED(pass_readback_->Map(0, &read_range, &mapped))) {
            const auto* ticks = reinterpret_cast<const std::uint64_t*>(static_cast<const std::byte*>(mapped) + offset);
            for (unsigned i = 0; i + 1u < marks; ++i) {
                if (ticks[i + 1u] >= ticks[i]) {
                    telemetry_pass_ms_[static_cast<std::size_t>(pass_marks_[frame_slot_][i])] +=
                        static_cast<double>(ticks[i + 1u] - ticks[i]) * 1000.0 /
                        static_cast<double>(timestamp_frequency_);
                }
            }
            const D3D12_RANGE no_write{0, 0};
            pass_readback_->Unmap(0, &no_write);
        }
    }
    pass_mark_count_[frame_slot_] = 0u;
    if (pre_list_used_[frame_slot_]) {
        if (curve_sort_readback_ && timestamp_frequency_ != 0u) {
            const SIZE_T offset = static_cast<SIZE_T>(frame_slot_) * 2u * sizeof(std::uint64_t);
            const D3D12_RANGE read_range{offset, offset + 2u * sizeof(std::uint64_t)};
            void* mapped = nullptr;
            if (SUCCEEDED(curve_sort_readback_->Map(0, &read_range, &mapped))) {
                const auto* ticks =
                    reinterpret_cast<const std::uint64_t*>(static_cast<const std::byte*>(mapped) + offset);
                if (ticks[1] >= ticks[0]) {
                    const double ms =
                        static_cast<double>(ticks[1] - ticks[0]) * 1000.0 / static_cast<double>(timestamp_frequency_);
                    telemetry_gpu_ms_ += ms;
                    telemetry_pass_ms_[static_cast<std::size_t>(GpuPass::CurveSort)] += ms;
                }
                const D3D12_RANGE no_write{0, 0};
                curve_sort_readback_->Unmap(0, &no_write);
            }
        }
        d3d_check(pre_allocators_[frame_slot_]->Reset(), "Curve sort allocator Reset");
        pre_list_used_[frame_slot_] = false;
    }
    curve_sort_draws_[frame_slot_].clear();
    curve_sorted_bytes_[frame_slot_] = 0u;
    curve_sort_vertices_[frame_slot_] = 0u;
    {
        ResidentStaging& staging = resident_staging_[frame_slot_];
        if (!staging.staged) {
            staging.chunks.clear();
        }
        staging.staged = false;
        staging.current = 0u;
        staging.used = 0u;
    }
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
    if (draw_readback_ && !draw_marks_[frame_slot_].empty()) {
        const std::vector<GpuDrawMark>& marks = draw_marks_[frame_slot_];
        const SIZE_T offset = static_cast<SIZE_T>(frame_slot_) * kGpuDrawSlots * sizeof(std::uint64_t);
        const D3D12_RANGE read_range{offset, offset + 2u * marks.size() * sizeof(std::uint64_t)};
        void* mapped = nullptr;
        if (SUCCEEDED(draw_readback_->Map(0, &read_range, &mapped))) {
            const auto* ticks = reinterpret_cast<const std::uint64_t*>(static_cast<const std::byte*>(mapped) + offset);
            for (std::size_t i = 0; i < marks.size(); ++i) {
                GpuDrawCost& cost = telemetry_draw_cost_[marks[i].pipeline];
                if (ticks[2u * i + 1u] >= ticks[2u * i]) {
                    cost.ms += static_cast<double>(ticks[2u * i + 1u] - ticks[2u * i]) * 1000.0 /
                               static_cast<double>(timestamp_frequency_);
                }
                ++cost.draws;
                cost.triangles += marks[i].triangles;
            }
            const D3D12_RANGE no_write{0, 0};
            draw_readback_->Unmap(0, &no_write);
        }
    }
    draw_marks_[frame_slot_].clear();


    const auto reset_start = collect_begin_timing
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const HRESULT allocator_reset_hr = allocators_[frame_slot_]->Reset();
    if (FAILED(allocator_reset_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(allocator_reset_hr, "CommandAllocator Reset");
    }
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
    pass_list_open_ = true;
    ++curve_sort_list_;
    if (stats_query_heap_) {
        command_list_->BeginQuery(stats_query_heap_.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, frame_slot_);
    }
    {
        const GpuPass pass = current_pass_;
        current_pass_ = GpuPass::Count;
        gpu_pass(pass);
    }

    if (ring_mode_ == RingMode::Interp) {
        // One segment per game frame: the frames (chunks) of a game frame
        // append to it, so its draws stay replayable until the segment
        // comes round again.
        if (ring_advance_pending_) {
            ring_advance_pending_ = false;
            ring_segment_ = (ring_segment_ + 1u) % kInterpSegments;
            if (replay_valid_ && ring_segment_ == replay_segment_) {
                replay_valid_ = false;
            }
            if (run_valid_ && ring_segment_ == run_segment_) {
                run_valid_ = false;
            }
            if (prev_run_valid_ && ring_segment_ == prev_run_segment_) {
                prev_run_valid_ = false;
            }
            if (ring_segment_fence_[ring_segment_] != 0u) {
                wait_for_fence_value(ring_segment_fence_[ring_segment_]);
            }
            retired_segment_textures_[ring_segment_].clear();
            // The segment's logs are gone (and their sub-frames complete).
            release_copy_snapshots(ring_segment_);
            vertex_ring_.begin_frame(ring_segment_);
            index_ring_.begin_frame(ring_segment_);
            constant_ring_.begin_frame(ring_segment_);
            matrix_ring_.begin_frame(ring_segment_);
            srv_ring_.begin_frame(ring_segment_);
            sampler_slot_ = ring_segment_;
            reset_sampler_frame();
            // A frame that never reached its display copy is dropped.
            begin_replay_record();
        }
    } else {
    vertex_ring_  .begin_frame(frame_slot_);
    index_ring_   .begin_frame(frame_slot_);
    constant_ring_.begin_frame(frame_slot_);
    if (matrix_segments_ == kFramesInFlight) {
        matrix_ring_.begin_frame(frame_slot_);
    } else {
        // Temporal mode: a frame that wrote no palette keeps appending to the
        // current segment, so the previous scene's palettes stay addressable
        // across present and utility frames.
        if (matrix_segment_used_ || !matrix_segment_started_) {
            matrix_segment_ = (matrix_segment_ + 1u) % matrix_segments_;
            if (matrix_segment_fence_[matrix_segment_] != 0u) {
                wait_for_fence_value(matrix_segment_fence_[matrix_segment_]);
            }
            matrix_ring_.begin_frame(matrix_segment_);
            matrix_segment_started_ = true;
        }
        matrix_begin_cursor_ = matrix_ring_.cursor();
    }
    srv_ring_     .begin_frame(frame_slot_);
    sampler_slot_ = frame_slot_;
    reset_sampler_frame();
    }
    bound_rt_aux_ = 0u;
    if (ring_mode_ == RingMode::Interp) {
        // A logged frame's sub-frames may still name these copy textures:
        // they live until the current game-frame segment comes round again.
        auto& keep = retired_segment_textures_[ring_segment_];
        for (auto& texture : retired_frame_pass_targets_[frame_slot_]) {
            keep.push_back(std::move(texture));
        }
    }
    retired_frame_pass_targets_[frame_slot_].clear();

    ensure_frame_values();
    // Bind both heaps for the entire frame.
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(2, heaps);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    if (scene_carry_color_ || scene_carry_depth_) {
        carry_over_scene_contents();
    }
    if (collect_begin_timing) {
        last_begin_reset_us_ = elapsed_us(
            reset_start,
            std::chrono::steady_clock::now());
    }
}

void RendererD3D12::wait_for_frame_latency_if_needed(
    bool collect_present_telemetry) {
    LARGE_INTEGER latency_wait_start{};
    LARGE_INTEGER latency_wait_end{};
    if (collect_present_telemetry) {
        QueryPerformanceCounter(&latency_wait_start);
    }
    if (collect_present_telemetry) {
        QueryPerformanceCounter(&latency_wait_end);
    }
    if (collect_present_telemetry && qpc_frequency_.QuadPart != 0) {
        const double wait_ms =
            static_cast<double>(
                latency_wait_end.QuadPart - latency_wait_start.QuadPart) *
            1000.0 / static_cast<double>(qpc_frequency_.QuadPart);
        telemetry_latency_wait_ms_ += wait_ms;
        telemetry_latency_wait_max_ms_ =
            std::max(telemetry_latency_wait_max_ms_, wait_ms);
    }
}

bool RendererD3D12::vblank_grid(std::int64_t& vblank, std::int64_t& period) const noexcept {
    return window_ != nullptr && host_window::vblank_grid(window_, vblank, period);
}

void RendererD3D12::refresh_display_rate() {
    // The vblanks' own rate when they are timed (the mode reports whole hertz: 239 or 240 for 239.76).
    std::int64_t vblank = 0;
    std::int64_t period = 0;
    if (vblank_grid(vblank, period)) {
        const double hz =
            std::round(static_cast<double>(qpc_frequency_.QuadPart) / static_cast<double>(period) * 100.0) / 100.0;
        if (std::abs(hz - display_refresh_hz_) >= 0.1) {
            std::cerr << "[display] refresh-hz=" << hz << " (vblanks)\n";
            display_refresh_hz_ = hz;
        }
        return;
    }
    if (swap_chain_ == nullptr) {
        return;
    }
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    DXGI_OUTPUT_DESC desc{};
    if (FAILED(swap_chain_->GetContainingOutput(&output)) ||
        FAILED(output->GetDesc(&desc))) {
        return;
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(desc.DeviceName, ENUM_CURRENT_SETTINGS, &mode) == FALSE ||
        mode.dmDisplayFrequency <= 1u) {
        return;
    }
    const double hz = static_cast<double>(mode.dmDisplayFrequency);
    if (hz != display_refresh_hz_) {
        std::cerr << "[display] refresh-hz=" << hz << '\n';
    }
    display_refresh_hz_ = hz;
}

UINT RendererD3D12::present_sync_interval(unsigned configured, float max_fps) {
    const bool game_frame = std::exchange(present_game_frame_, false);
    if (configured != 1u) {
        return configured;
    }
    if (display_refresh_presents_++ % 120u == 0u) {
        refresh_display_rate();
    }
    const double hz = display_refresh_hz_;
    if (hz <= 0.0) {
        return 1u;
    }
    // EnumDisplaySettings reports whole hertz: 119 for 119.88, 239 for 239.76. A game frame without sub-frames
    // holds for its whole 60 Hz frame (4 refreshes at 240 Hz) rather than the next refresh after it is ready, which
    // drifts against the display.
    const double rate = game_frame ? std::min(60.0, present_frame_rate(max_fps, configured, hz))
                                   : present_frame_rate(max_fps, configured, hz);
    const double multiple = std::round(hz / rate);
    if (multiple >= 2.0 && multiple <= 4.0 &&
        std::abs(hz - rate * multiple) <= 0.015 * hz) {
        return static_cast<UINT>(multiple);
    }
    return 1u;
}

void RendererD3D12::present_to_swap_chain() {
    const RenderConfig cfg = get_render_config();
    const bool generating = frame_generation_running();
    // With frame generation the sync interval is 0 or 1 (Streamline paces the generated frames).
    const UINT vsync = generating ? std::min(1u, cfg.vsync_interval)
                                  : present_sync_interval(std::min(2u, cfg.vsync_interval), cfg.max_fps);
    const UINT present_flags =
        0u;
    // Interpolation presents on its own slot clock.
    const double pace_wait_ms =
        ring_mode_ == RingMode::Interp ? 0.0 : present_pacer_.pace(cfg.vsync_interval, presented_max_fps(cfg));
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
                static_cast<long>(rate / 60.0 + 1e-3) - 1, 1, static_cast<long>(streamline::frame_generation_max())));
        }
        streamline::set_frame_generation(generated, streamline::frame_generation_dynamic(),
                                         cfg.max_fps > 0.0f ? cfg.max_fps : 0.0f);
    }
    streamline::marker(streamline::Marker::kPresentStart);
    const HRESULT present_hr = swap_chain_->Present(vsync, present_flags);
    streamline::marker(streamline::Marker::kPresentEnd);
    streamline_frame_open_ = false;
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
    has_presented_swap_chain_ = true;
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
        if (display_refresh_hz_ == 0.0) {
            refresh_display_rate();
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
            telemetry_first_serial_start_ = display_present_stats().first_serial_presentations;
            telemetry_display_copy_start_ = display_copy_count();
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
            const auto produced_copies = display_copy_count();
            const double present_fps =
                static_cast<double>(telemetry_present_count_) /
                elapsed_seconds;
            const double adjacent_serial_transition_hz =
                static_cast<double>(
                    telemetry_adjacent_serial_transition_count_) /
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
            const double latency_avg =
                telemetry_latency_wait_ms_ /
                static_cast<double>(telemetry_present_count_);
            const double gpu_avg =
                telemetry_gpu_samples_ != 0
                    ? telemetry_gpu_ms_ /
                          static_cast<double>(telemetry_gpu_samples_)
                    : 0.0;
            const double same_serial_or_replay_hz =
                std::max(
                    0.0, present_fps - adjacent_serial_transition_hz);
            const double same_serial_or_replay_percent =
                present_fps > 0.0
                    ? same_serial_or_replay_hz * 100.0 / present_fps
                    : 0.0;

            // cerr is unit-buffered: the record is assembled first so it is one write, not one per field.
            std::ostringstream message;
            message
                << "[present-stats] fps=" << present_fps
                << " new-serial-hz="
                << static_cast<double>(progress.first_serial_presentations - telemetry_first_serial_start_) / elapsed_seconds
                << " display-production-hz="
                << static_cast<double>(produced_copies - telemetry_display_copy_start_) / elapsed_seconds
                << " first-serial-total=" << progress.first_serial_presentations
                << " display-copy-total=" << produced_copies
                << " steady-ns=" << std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()
                << " adjacent-serial-transition-hz="
                << adjacent_serial_transition_hz
                << " same-serial-or-replay-hz="
                << same_serial_or_replay_hz
                << " same-serial-or-replay-pct="
                << same_serial_or_replay_percent
                << " frame-ms(avg/max)=" << interval_avg << "/"
                << telemetry_interval_max_ms_
                << " latency-wait-ms(avg/max)=" << latency_avg << "/"
                << telemetry_latency_wait_max_ms_
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
                                                             "curve-sort", "camera"};
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
            if (draw_query_heap_ && !telemetry_draw_cost_.empty()) {
                std::vector<std::pair<ID3D12PipelineState*, GpuDrawCost>> costs(telemetry_draw_cost_.begin(),
                                                                                telemetry_draw_cost_.end());
                std::sort(costs.begin(), costs.end(),
                          [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
                const double presents = static_cast<double>(std::max<std::uint64_t>(telemetry_present_count_, 1u));
                double total = 0.0;
                for (const auto& cost : costs) {
                    total += cost.second.ms;
                }
                std::ostringstream draws;
                draws << "[gpu-draws] total-ms=" << total / presents << " pipelines=" << costs.size() << '\n';
                for (std::size_t i = 0; i < costs.size() && i < 24u; ++i) {
                    draws << "[gpu-draws] pso=" << static_cast<const void*>(costs[i].first)
                          << " ms=" << costs[i].second.ms / presents
                          << " draws=" << static_cast<double>(costs[i].second.draws) / presents
                          << " tris=" << static_cast<double>(costs[i].second.triangles) / presents << '\n';
                }
                std::cerr << draws.str();
                telemetry_draw_cost_.clear();
            }
            message
                << " scene-handovers=" << scene_handovers_ << '/' << scene_handovers_free_ << '/' << scene_handovers_banded_
                << '/' << scene_handovers_restored_
                << " no-display-presents="
                << telemetry_no_display_present_count_
                << " backbuffer-clears="
                << telemetry_backbuffer_clear_count_
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
            telemetry_first_serial_start_ = progress.first_serial_presentations;
            telemetry_display_copy_start_ = produced_copies;
            telemetry_present_count_ = 0;
            telemetry_interval_count_ = 0;
            telemetry_adjacent_serial_transition_count_ = 0;
            telemetry_interval_ms_ = 0.0;
            telemetry_interval_max_ms_ = 0.0;
            telemetry_present_ms_ = 0.0;
            telemetry_present_max_ms_ = 0.0;
            telemetry_pace_wait_ms_ = 0.0;
            telemetry_pace_wait_max_ms_ = 0.0;
            telemetry_latency_wait_ms_ = 0.0;
            telemetry_latency_wait_max_ms_ = 0.0;
            telemetry_gpu_ms_ = 0.0;
            telemetry_gpu_by_place_.fill(0.0);
            telemetry_gpu_by_place_count_.fill(0u);
            telemetry_gpu_samples_ = 0;
            telemetry_no_display_present_count_ = 0;
            telemetry_backbuffer_clear_count_ = 0;
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
    void* submission_context) {
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
    if (draw_query_heap_ && !draw_marks_[frame_slot_].empty()) {
        command_list_->ResolveQueryData(draw_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                        frame_slot_ * kGpuDrawSlots,
                                        static_cast<UINT>(2u * draw_marks_[frame_slot_].size()), draw_readback_.Get(),
                                        static_cast<UINT64>(frame_slot_) * kGpuDrawSlots * sizeof(std::uint64_t));
    }
    const HRESULT close_hr = command_list_->Close();
    if (FAILED(close_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(close_hr, "CommandList Close");
    }
    pre_list_recorded_ = false;
    if (!resident_copies_.empty() || !curve_sort_draws_[frame_slot_].empty()) {
        record_pre_list();
    }
    ID3D12CommandList* lists[] = {pre_list_.Get(), command_list_.Get()};
    if (present_swap_chain) {
        streamline::marker(streamline::Marker::kSimulationEnd);
        streamline::marker(streamline::Marker::kRenderSubmitStart);
    }
    if (pre_list_recorded_) {
        queue_->ExecuteCommandLists(2, lists);
    } else {
        queue_->ExecuteCommandLists(1, lists + 1);
    }
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
    if (ring_mode_ == RingMode::Interp) {
        ring_segment_fence_[ring_segment_] = submitted_fence;
    } else if (matrix_segments_ != kFramesInFlight) {
        matrix_segment_used_ = matrix_ring_.cursor() != matrix_begin_cursor_;
        matrix_segment_fence_[matrix_segment_] = submitted_fence;
    }
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
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            dump_device_removal(device_.Get());
            throw std::runtime_error(
                "GPU fence wait timed out");
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
    return (upscaler == 2u && fsr3_available()) || (upscaler == 3u && streamline::dlss_supported());
}

bool RendererD3D12::ensure_motion_target() {
    if (motion_target_ && motion_target_map_ == frame_) {
        return true;
    }
    if (motion_target_) {
        // A render-scale change inside a scene: this frame's command list
        // may still name them.
        retired_frame_pass_targets_[frame_slot_].push_back(std::move(motion_target_));
        retired_frame_pass_targets_[frame_slot_].push_back(std::move(reactive_target_));
        retired_frame_pass_targets_[frame_slot_].push_back(std::move(layer_color_));
        retired_frame_pass_targets_[frame_slot_].push_back(std::move(layer_trans_));
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
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&motion_target_)))) {
        return false;
    }
    rd.Format = kReactiveMaskFormat;
    cv.Format = kReactiveMaskFormat;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&reactive_target_)))) {
        motion_target_.Reset();
        return false;
    }
    rd.Format = kLayerFormat;
    cv.Format = kLayerFormat;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                &cv, IID_PPV_ARGS(&layer_color_))) ||
        FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
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
    motion_target_map_ = frame_;
    return true;
}


void RendererD3D12::record_motion_debug_view() {
    settle_owed_scene();
    // Its pipeline draws to the RGBA8 scene target.
    if (!motion_target_ || scene_hdr_) {
        return;
    }
    if (!motion_debug_pipeline_) {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = &range;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 1;
        rsd.pParameters = &param;
        ComPtr<ID3DBlob> blob, err;
        d3d_check(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err),
                  "SerializeMotionDebugRS");
        d3d_check(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                               IID_PPV_ARGS(&motion_debug_root_)),
                  "CreateMotionDebugRS");
        const UtilityShaderBytes vs = utility_shader(UtilityShader::BlitVs);
        const UtilityShaderBytes ps = utility_shader(UtilityShader::MotionDebugPs);
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = motion_debug_root_.Get();
        pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 1;
        pd.RTVFormats[0] = kSceneColorFormat;
        pd.SampleDesc.Count = 1;
        pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d3d_check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&motion_debug_pipeline_)),
                  "CreateMotionDebugPSO");
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = motion_target_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    const DescriptorRing::Table table = srv_ring_.allocate(1u);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kMotionVectorFormat;
    device_->CreateShaderResourceView(motion_target_.Get(), &view, table.cpu);
    command_list_->SetGraphicsRootSignature(motion_debug_root_.Get());
    command_list_->SetPipelineState(motion_debug_pipeline_.Get());
    command_list_->SetGraphicsRootDescriptorTable(0, table.gpu);
    command_list_->OMSetRenderTargets(1, &scene_rtv_, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(frame_.width),
                            static_cast<float>(frame_.height), 0.0f, 1.0f};
    const D3D12_RECT sci{0, 0, static_cast<LONG>(frame_.width),
                         static_cast<LONG>(frame_.height)};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sci);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bind_scene();
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
    host_window::temporal_interp_suspended.store(true, std::memory_order_relaxed);
    std::cerr << "[interp] temporal upscaling of the replays off: " << ms_per_frame
              << " ms GPU per presented frame\n";
}

void RendererD3D12::note_stripe_vertices(std::span<const StripeVertex> vertices) {
    if (!replay_recording_ || vertices.empty()) {
        return;
    }
    replay_stripes_.draws.push_back({static_cast<std::uint32_t>(replay_draws_.size()),
                                     static_cast<std::uint32_t>(replay_stripes_.vertices.size()),
                                     static_cast<std::uint32_t>(vertices.size())});
    replay_stripes_.vertices.insert(replay_stripes_.vertices.end(), vertices.begin(), vertices.end());
}

void RendererD3D12::publish_replay_log() {
    std::swap(replay_stripes_, play_stripes_);
    std::swap(replay_ops_, play_ops_);
    std::swap(replay_draws_, play_draws_);
    std::swap(replay_clears_, play_clears_);
    std::swap(replay_copies_, play_copies_);
    std::swap(replay_blooms_, play_blooms_);
    std::swap(replay_textures_, play_textures_);
    play_start_output_active_ = replay_start_output_active_;
}

bool RendererD3D12::start_replay_playback() {
    if (!replay_valid_) {
        return false;
    }
    // The playing log becomes the one before (vertex blending reads it).
    prev_run_valid_ = run_valid_;
    if (run_valid_) {
        std::swap(prev_run_draws_, run_draws_);
        std::swap(prev_run_stripes_, run_stripes_);
        prev_run_segment_ = run_segment_;
        prev_run_serial_ = run_serial_;
    }
    std::swap(play_stripes_, run_stripes_);
    run_blend_vertices_.clear();
    std::swap(play_ops_, run_ops_);
    std::swap(play_draws_, run_draws_);
    std::swap(play_clears_, run_clears_);
    std::swap(play_copies_, run_copies_);
    std::swap(play_blooms_, run_blooms_);
    std::swap(play_textures_, run_textures_);
    run_start_output_active_ = play_start_output_active_;
    run_segment_ = replay_segment_;
    run_serial_ = play_serial_;
    run_valid_ = true;
    replay_valid_ = false;
    return true;
}

// Blend verdicts, then the presented frame's values (t, t of the frame before,
// jitter) at kFrameValuesElement and a zero element read by game frames. Made
// at the first frame, zeroed, so every draw can read it.
void RendererD3D12::ensure_frame_values() {
    if (blend_verdicts_ || !device_) {
        return;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<UINT64>(kZeroValuesElement + 1u) * 16u;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    d3d_check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&blend_verdicts_)),
              "frame values CreateCommittedResource");
    blend_verdicts_->SetName(L"vertex blend verdicts and frame values");
    const UploadRing::Allocation zeros = constant_ring_.allocate(static_cast<std::size_t>(desc.Width), 256u);
    std::memset(zeros.cpu, 0, static_cast<std::size_t>(desc.Width));
    command_list_->CopyBufferRegion(blend_verdicts_.Get(), 0u, constant_ring_.resource(), zeros.offset, desc.Width);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = blend_verdicts_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    blend_verdicts_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

bool RendererD3D12::ensure_blend_validation() {
    if (blend_validate_pso_) {
        return true;
    }
    if (blend_validate_failed_ || !device_) {
        return false;
    }
    blend_validate_failed_ = true;
    if (!blend_verdicts_) {
        return false;
    }
    D3D12_ROOT_PARAMETER params[5]{};
    for (unsigned i = 0; i < 3u; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[i].Descriptor.ShaderRegister = i;
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[3].Descriptor.ShaderRegister = 0;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[4].Constants.ShaderRegister = 0;
    params[4].Constants.Num32BitValues = 2;
    params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 5;
    rsd.pParameters = params;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&blend_validate_root_)))) {
        return false;
    }
    try {
        const UtilityShaderBytes cs = utility_shader(UtilityShader::BlendValidateCs);
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = blend_validate_root_.Get();
        pso.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
        if (FAILED(device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(&blend_validate_pso_)))) {
            return false;
        }
    } catch (const std::exception& error) {
        std::cerr << "[interp-blend] validation shader: " << error.what() << '\n';
        return false;
    }
    blend_validate_failed_ = false;
    return true;
}

bool RendererD3D12::ensure_stripe_gather(std::uint64_t bytes) {
    if (!stripe_gather_pso_) {
        if (stripe_gather_failed_ || !device_) {
            return false;
        }
        stripe_gather_failed_ = true;
        D3D12_ROOT_PARAMETER params[5]{};
        for (unsigned i = 0; i < 3u; ++i) {
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
            params[i].Descriptor.ShaderRegister = i;
            params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[4].Constants.Num32BitValues = 3u;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 5;
        rsd.pParameters = params;
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
            FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                IID_PPV_ARGS(&stripe_gather_root_)))) {
            return false;
        }
        try {
            const UtilityShaderBytes cs = utility_shader(UtilityShader::StripeGatherCs);
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = stripe_gather_root_.Get();
            desc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
            if (FAILED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(&stripe_gather_pso_)))) {
                return false;
            }
        } catch (const std::exception& error) {
            std::cerr << "[stripe-gather] shader: " << error.what() << '\n';
            return false;
        }
        stripe_gather_failed_ = false;
    }
    if (stripe_earlier_ && stripe_earlier_bytes_ >= bytes) {
        return true;
    }
    std::uint64_t size = std::max<std::uint64_t>(stripe_earlier_bytes_ * 2u, 1ull << 20u);
    while (size < bytes) {
        size *= 2u;
    }
    if (stripe_earlier_) {
        // Sub-frames in flight still read the old one.
        interp_retired_.push_back({std::move(stripe_earlier_), next_fence_value_});
        stripe_earlier_bytes_ = 0u;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr,
                                                IID_PPV_ARGS(&stripe_earlier_)))) {
        return false;
    }
    stripe_earlier_->SetName(L"stripe particle earlier vertices");
    stripe_earlier_bytes_ = size;
    stripe_earlier_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    return true;
}

void RendererD3D12::record_stripe_gather() {
    const std::size_t bytes = stripe_gather_.size() * sizeof(stripe_gather_[0]);
    const UploadRing::Allocation table = constant_ring_.allocate(bytes, 256u);
    std::memcpy(table.cpu, stripe_gather_.data(), bytes);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = stripe_earlier_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = stripe_earlier_state_;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    command_list_->ResourceBarrier(1, &barrier);
    command_list_->SetComputeRootSignature(stripe_gather_root_.Get());
    command_list_->SetPipelineState(stripe_gather_pso_.Get());
    const std::uint64_t ring_bytes = vertex_ring_.total_bytes();
    const bool resident = resident_vertex_base_ != 0u && resident_vertex_bytes_ >= 4u;
    const std::uint32_t limits[3]{
        static_cast<std::uint32_t>(std::min<std::uint64_t>(ring_bytes - 4u, 0xFFFFFFF0u)),
        static_cast<std::uint32_t>(std::min<std::uint64_t>((resident ? resident_vertex_bytes_ : ring_bytes) - 4u,
                                                           0xFFFFFFF0u)),
        static_cast<std::uint32_t>(stripe_gather_.size())};
    command_list_->SetComputeRootShaderResourceView(0, vertex_ring_.gpu_base());
    command_list_->SetComputeRootShaderResourceView(1, resident ? resident_vertex_base_ : vertex_ring_.gpu_base());
    command_list_->SetComputeRootShaderResourceView(2, table.gpu);
    command_list_->SetComputeRootUnorderedAccessView(3, stripe_earlier_->GetGPUVirtualAddress());
    command_list_->SetComputeRoot32BitConstants(4, 3u, limits, 0u);
    const auto threads = static_cast<std::uint64_t>(stripe_gather_.size()) * 36u;
    command_list_->Dispatch(static_cast<UINT>((threads + 63u) / 64u), 1u, 1u);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    stripe_earlier_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    bound_pipeline_ = nullptr;
}

void RendererD3D12::record_blend_validation() {
    if (!blend_validate_pending_) {
        return;
    }
    blend_validate_pending_ = false;
    if (!stripe_gather_.empty()) {
        record_stripe_gather();
    }
    if (blend_pairs_.empty()) {
        return;
    }
    const std::size_t bytes = blend_pairs_.size() * sizeof(blend_pairs_[0]);
    const UploadRing::Allocation table = constant_ring_.allocate(bytes, 256u);
    std::memcpy(table.cpu, blend_pairs_.data(), bytes);
    if (blend_verdicts_state_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = blend_verdicts_.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = blend_verdicts_state_;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        command_list_->ResourceBarrier(1, &barrier);
    }
    command_list_->SetComputeRootSignature(blend_validate_root_.Get());
    command_list_->SetPipelineState(blend_validate_pso_.Get());
    const std::uint64_t ring_bytes = vertex_ring_.total_bytes();
    const bool resident = resident_vertex_base_ != 0u && resident_vertex_bytes_ >= 12u;
    const std::uint32_t limits[2]{
        static_cast<std::uint32_t>(std::min<std::uint64_t>(ring_bytes - 12u, 0xFFFFFFF0u)),
        static_cast<std::uint32_t>(std::min<std::uint64_t>((resident ? resident_vertex_bytes_ : ring_bytes) - 12u,
                                                           0xFFFFFFF0u))};
    command_list_->SetComputeRootShaderResourceView(0, vertex_ring_.gpu_base());
    command_list_->SetComputeRootShaderResourceView(1, resident ? resident_vertex_base_ : vertex_ring_.gpu_base());
    command_list_->SetComputeRootShaderResourceView(2, table.gpu);
    command_list_->SetComputeRootUnorderedAccessView(3, blend_verdicts_->GetGPUVirtualAddress());
    command_list_->SetComputeRoot32BitConstants(4, 2u, limits, 0u);
    command_list_->Dispatch(static_cast<UINT>(blend_pairs_.size()), 1u, 1u);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = blend_verdicts_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    blend_verdicts_state_ = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    bound_pipeline_ = nullptr;
}

void RendererD3D12::prepare_vertex_blend(std::span<const std::int32_t> previous_record) {
    run_blend_vertices_.assign(run_draws_.size(), BlendSource{});
    blend_pairs_.clear();
    stripe_gather_.clear();
    blend_validate_pending_ = false;
    std::int32_t ps_records = 0;
    for (const DrawCall& call : run_draws_) {
        ps_records = std::max(ps_records, call.ps_record + 1);
    }
    run_ps_previous_.assign(static_cast<std::size_t>(ps_records), -1);
    if (!prev_run_valid_ || prev_run_serial_ + 1u != run_serial_ || previous_record.empty()) {
        return;
    }
    // Where a draw's vertices are: the frame vertex ring (0) or the resident
    // decoded-vertex buffer (1), and the byte offset there.
    const D3D12_GPU_VIRTUAL_ADDRESS ring_base = vertex_ring_.gpu_base();
    const std::uint64_t ring_bytes =
        vertex_ring_.resource() != nullptr ? vertex_ring_.resource()->GetDesc().Width : 0u;
    const auto locate = [&](D3D12_GPU_VIRTUAL_ADDRESS va, std::uint32_t bytes, std::uint32_t& offset) {
        if (va >= ring_base && va + bytes <= ring_base + ring_bytes) {
            offset = static_cast<std::uint32_t>(va - ring_base);
            return 0;
        }
        if (resident_vertex_base_ != 0u && va >= resident_vertex_base_ &&
            va + bytes <= resident_vertex_base_ + resident_vertex_bytes_) {
            offset = static_cast<std::uint32_t>(va - resident_vertex_base_);
            return 1;
        }
        return -1;
    };
    // The draws of each previous record, in log order (counting sort).
    std::int32_t records = 0;
    for (const DrawCall& call : prev_run_draws_) {
        records = std::max(records, call.replay_record + 1);
    }
    std::vector<std::uint32_t>& scratch = blend_scratch_;
    const std::size_t count = static_cast<std::size_t>(records);
    scratch.assign(2u * count + 1u + prev_run_draws_.size(), 0u);
    std::uint32_t* const first = scratch.data();              // [records + 1]
    std::uint32_t* const fill = first + count + 1u;           // [records]
    std::uint32_t* const order = fill + count;                // [draws]
    for (const DrawCall& call : prev_run_draws_) {
        if (call.replay_record >= 0) ++first[call.replay_record + 1];
    }
    for (std::size_t r = 0; r < count; ++r) first[r + 1u] += first[r];
    for (std::size_t d = 0; d < prev_run_draws_.size(); ++d) {
        const std::int32_t r = prev_run_draws_[d].replay_record;
        if (r >= 0) order[first[r] + fill[r]++] = static_cast<std::uint32_t>(d);
    }
    // Reuse `fill` as each previous record's cursor: a record's draws pair in
    // order, skipping up to three unmatched draws (one drawn in only one of
    // the frames) to the next one with as many vertices.
    std::fill(fill, fill + count, 0u);
    // Stripe draws of both logs, by draw index.
    stripe_of_draw_.assign(run_draws_.size(), -1);
    for (std::size_t i = 0; i < run_stripes_.draws.size(); ++i) {
        const StripeDraw& stripe = run_stripes_.draws[i];
        if (stripe.draw < stripe_of_draw_.size()) stripe_of_draw_[stripe.draw] = static_cast<std::int32_t>(i);
    }
    stripe_of_prev_draw_.assign(prev_run_draws_.size(), -1);
    for (std::size_t i = 0; i < prev_run_stripes_.draws.size(); ++i) {
        const StripeDraw& stripe = prev_run_stripes_.draws[i];
        if (stripe.draw < stripe_of_prev_draw_.size()) {
            stripe_of_prev_draw_[stripe.draw] = static_cast<std::int32_t>(i);
        }
    }
    const bool pairs_checked = ensure_blend_validation();
    // Per stripe draw: its draw and first gathered vertex (addresses once the
    // gather buffer is sized).
    std::vector<std::array<std::uint32_t, 2>> stripe_blends;
    for (std::size_t d = 0; d < run_draws_.size(); ++d) {
        const DrawCall& call = run_draws_[d];
        const std::int32_t r = call.replay_record;
        if (r < 0 || static_cast<std::size_t>(r) >= previous_record.size()) continue;
        const std::int32_t m = previous_record[static_cast<std::size_t>(r)];
        if (m < 0 || m >= records) continue;
        std::uint32_t at = first[m] + fill[m];
        const std::uint32_t end = std::min(first[m + 1], at + 4u);
        const std::int32_t stripe = stripe_of_draw_[d];
        if (stripe >= 0) {
            // A stripe pairs with its record's next stripe draw whatever the
            // particle count, and then particle by particle.
            while (at < end && stripe_of_prev_draw_[order[at]] < 0) {
                ++at;
            }
        } else {
            while (at < end && prev_run_draws_[order[at]].vertex_view.SizeInBytes != call.vertex_view.SizeInBytes) {
                ++at;
            }
        }
        if (at >= end) {
            continue;
        }
        fill[m] = at + 1u - first[m];
        const DrawCall& before = prev_run_draws_[order[at]];
        if (call.ps_record >= 0 && before.ps_record >= 0) {
            std::int32_t& pair = run_ps_previous_[static_cast<std::size_t>(call.ps_record)];
            pair = pair == -1 || pair == before.ps_record ? before.ps_record : -2;
        }
        std::uint32_t current_offset = 0u, previous_offset = 0u;
        const int current_buffer =
            locate(call.vertex_view.BufferLocation, call.vertex_view.SizeInBytes, current_offset);
        const int previous_buffer =
            locate(before.vertex_view.BufferLocation, before.vertex_view.SizeInBytes, previous_offset);
        // A strip of the same length slides along itself (index pairing, as
        // any pair); one that grew or shrank pairs particle by particle.
        if (stripe >= 0 && before.vertex_view.SizeInBytes != call.vertex_view.SizeInBytes) {
            const std::uint32_t vertices = call.vertex_view.StrideInBytes == 144u
                ? call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes : 0u;
            const std::uint32_t previous_vertices = before.vertex_view.StrideInBytes == 144u
                ? before.vertex_view.SizeInBytes / before.vertex_view.StrideInBytes : 0u;
            if (vertices == 0u || current_buffer < 0 || previous_buffer < 0 ||
                stripe_gather_.size() + vertices > (1u << 20u)) {
                continue;
            }
            // Previous particles by (particle, pass, side, lifetime); a match
            // continues only if its age moved on by at most one frame.
            const StripeDraw& prev_draw =
                prev_run_stripes_.draws[static_cast<std::size_t>(stripe_of_prev_draw_[order[at]])];
            const auto key_of = [](const StripeVertex& v) {
                return std::pair<std::uint64_t, std::uint32_t>{
                    v.particle, (static_cast<std::uint32_t>(v.strip) << 24u) |
                                    (static_cast<std::uint32_t>(v.side) << 16u) | (v.age_lifetime & 0xFFFFu)};
            };
            stripe_keys_.clear();
            for (std::uint32_t i = 0; i < prev_draw.count; ++i) {
                const StripeVertex& v = prev_run_stripes_.vertices[prev_draw.first + i];
                if (v.vertex < previous_vertices) stripe_keys_.emplace_back(key_of(v), prev_draw.first + i);
            }
            std::sort(stripe_keys_.begin(), stripe_keys_.end());
            const auto base = static_cast<std::uint32_t>(stripe_gather_.size());
            for (std::uint32_t i = 0; i < vertices; ++i) {
                stripe_gather_.push_back({current_offset + i * 144u, static_cast<std::uint32_t>(current_buffer)});
            }
            stripe_matched_.assign(vertices, -1);
            const StripeDraw& draw = run_stripes_.draws[static_cast<std::size_t>(stripe)];
            for (std::uint32_t i = 0; i < draw.count; ++i) {
                const StripeVertex& v = run_stripes_.vertices[draw.first + i];
                if (v.vertex >= vertices) continue;
                const auto key = key_of(v);
                const auto found = std::lower_bound(stripe_keys_.begin(), stripe_keys_.end(),
                                                    std::pair{key, std::uint32_t{0}});
                if (found == stripe_keys_.end() || found->first != key) continue;
                const StripeVertex& was = prev_run_stripes_.vertices[found->second];
                const auto steps = static_cast<std::uint16_t>((v.age_lifetime >> 16u) - (was.age_lifetime >> 16u));
                if (steps > 1u) continue;
                stripe_gather_[base + v.vertex] = {previous_offset + was.vertex * 144u,
                                                   static_cast<std::uint32_t>(previous_buffer)};
                stripe_matched_[v.vertex] = static_cast<std::int32_t>(was.vertex);
            }
            // A new particle grows out of where its nearest surviving
            // neighbour (same side of the strip) was.
            for (std::uint32_t i = 0; i < vertices; ++i) {
                if (stripe_matched_[i] >= 0) continue;
                for (std::uint32_t step = 2u; step <= 16u; step += 2u) {
                    const std::int64_t below = static_cast<std::int64_t>(i) - step;
                    const std::uint32_t above = i + step;
                    std::int32_t from = -1;
                    if (above < vertices && stripe_matched_[above] >= 0) {
                        from = stripe_matched_[above];
                    } else if (below >= 0 && stripe_matched_[static_cast<std::size_t>(below)] >= 0) {
                        from = stripe_matched_[static_cast<std::size_t>(below)];
                    }
                    if (from >= 0) {
                        stripe_gather_[base + i] = {previous_offset + static_cast<std::uint32_t>(from) * 144u,
                                                    static_cast<std::uint32_t>(previous_buffer)};
                        break;
                    }
                }
            }
            stripe_blends.push_back({static_cast<std::uint32_t>(d), base});
            continue;
        }
        if (pairs_checked && before.vertex_view.BufferLocation != call.vertex_view.BufferLocation &&
            before.vertex_view.StrideInBytes == call.vertex_view.StrideInBytes &&
            call.vertex_view.SizeInBytes != 0u && current_buffer >= 0 && previous_buffer >= 0 &&
            blend_pairs_.size() < kStripeVerdict) {
            const auto slot = static_cast<std::uint32_t>(blend_pairs_.size());
            blend_pairs_.push_back({current_offset, previous_offset,
                                    call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes,
                                    static_cast<std::uint32_t>(current_buffer) |
                                        (static_cast<std::uint32_t>(previous_buffer) << 1u)});
            run_blend_vertices_[d] = {before.vertex_view.BufferLocation, slot};
        }
    }
    if (!stripe_gather_.empty()) {
        if (ensure_stripe_gather(static_cast<std::uint64_t>(stripe_gather_.size()) * 144u)) {
            const D3D12_GPU_VIRTUAL_ADDRESS earlier = stripe_earlier_->GetGPUVirtualAddress();
            for (const auto& [draw, base] : stripe_blends) {
                run_blend_vertices_[draw] = {earlier + static_cast<std::uint64_t>(base) * 144u, kStripeVerdict};
            }
        } else {
            stripe_gather_.clear();
        }
    }
    blend_validate_pending_ = !blend_pairs_.empty() || !stripe_gather_.empty();
}

void RendererD3D12::begin_replay_record() {
    replay_scene_count_ = 0u;
    replay_screen_frame_ = false;
    replay_stripes_.clear();
    replay_ops_.clear();
    replay_draws_.clear();
    replay_clears_.clear();
    replay_copies_.clear();
    replay_blooms_.clear();
    replay_textures_.clear();
    if (log_start_clear_valid_) {
        log_start_clear_valid_ = false;
        replay_ops_.push_back({ReplayKind::Clear, 0u});
        replay_clears_.push_back(log_start_clear_);
    }
    if (ring_mode_ != RingMode::Interp) {
        replay_valid_ = false;
    }
    replay_recording_ = true;
    replay_start_output_active_ = output_scene_active_;
    replay_source_slot_ = frame_slot_;
}

bool RendererD3D12::ensure_subframe_resources(unsigned display_picture_width, unsigned display_picture_height) {
    if (!subframe_allocators_[0]) {
        for (auto& allocator : subframe_allocators_) {
            if (FAILED(device_->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))) {
                return false;
            }
        }
        D3D12_QUERY_HEAP_DESC qd{};
        qd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qd.Count = kSubframeAllocators * 4u;
        if (timestamp_frequency_ != 0u) {
            (void)device_->CreateQueryHeap(&qd, IID_PPV_ARGS(&subframe_query_heap_));
        }
        if (subframe_query_heap_) {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = kSubframeAllocators * 4u * sizeof(std::uint64_t);
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device_->CreateCommittedResource(
                    &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr, IID_PPV_ARGS(&subframe_readback_)))) {
                subframe_query_heap_.Reset();
            }
        }
    }
    if (subframe_display_.texture && subframe_display_.width == display_picture_width &&
        subframe_display_.height == display_picture_height) {
        return true;
    }
    if (subframe_display_.texture) {
        wait_for_gpu();
    }
    subframe_display_ = DisplayTexture{};
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = display_picture_width;
    rd.Height = display_picture_height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kSceneColorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr, IID_PPV_ARGS(&subframe_display_.texture)))) {
        return false;
    }
    subframe_display_.width = static_cast<std::uint16_t>(display_picture_width);
    subframe_display_.height = static_cast<std::uint16_t>(display_picture_height);
    subframe_display_.presentable = true;
    return true;
}

void RendererD3D12::execute_replay_copy(const ReplayCopy& copy, ID3D12Resource* target) {
    settle_owed_scene();
    if (copy.mode == 2u) {
        record_conversion_blit(alt_scene_.color.Get(), copy.width, copy.height, {}, kCopyLayoutDisplay, copy.geometry[0], copy.geometry[1], copy.geometry[2], copy.geometry[3]);
        return;
    }
    ID3D12Resource* const dest = copy.mode == 1u ? subframe_display_.texture.Get()
        : target != nullptr ? target : copy.dest;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = dest;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);
    record_conversion_blit(dest, copy.width, copy.height, allocate_conversion_rtv(dest), copy.format, copy.geometry[0], copy.geometry[1], copy.geometry[2], copy.geometry[3]);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);
}

void RendererD3D12::note_copy_write(ID3D12Resource* dest) {
    if (replay_recording_ &&
        std::find(replay_textures_.written.begin(), replay_textures_.written.end(), dest) ==
            replay_textures_.written.end()) {
        replay_textures_.written.push_back(dest);
    }
}

ID3D12Resource* RendererD3D12::acquire_subframe_scratch(ID3D12Resource* dest) {
    const D3D12_RESOURCE_DESC want = dest->GetDesc();
    for (SubframeScratch& scratch : subframe_scratch_) {
        const D3D12_RESOURCE_DESC& have = scratch.desc;
        if (scratch.used != subframe_counter_ && have.Width == want.Width &&
            have.Height == want.Height && have.Format == want.Format &&
            have.MipLevels == want.MipLevels &&
            have.DepthOrArraySize == want.DepthOrArraySize) {
            scratch.used = subframe_counter_;
            return scratch.texture.Get();
        }
    }
    D3D12_RESOURCE_DESC desc = want;
    desc.Alignment = 0u;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    SubframeScratch scratch{};
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&scratch.texture)))) {
        return nullptr;
    }
    scratch.desc = want;
    scratch.used = subframe_counter_;
    subframe_scratch_.push_back(std::move(scratch));
    return subframe_scratch_.back().texture.Get();
}

// save: guarded textures -> scratch; otherwise scratch -> guarded textures.
// Textures rest in PIXEL_SHADER_RESOURCE, scratch in COPY_DEST.
void RendererD3D12::record_subframe_guard(bool save) {
    if (subframe_guarded_.empty()) {
        return;
    }
    const auto transition = [this](ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
                                    D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = resource;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        subframe_barriers_.push_back(b);
    };
    const auto flush = [this] {
        command_list_->ResourceBarrier(static_cast<UINT>(subframe_barriers_.size()),
                                       subframe_barriers_.data());
        subframe_barriers_.clear();
    };
    constexpr auto kTexture = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const auto texture_copy_state = save ? D3D12_RESOURCE_STATE_COPY_SOURCE
                                         : D3D12_RESOURCE_STATE_COPY_DEST;
    subframe_barriers_.clear();
    for (const auto& [dest, scratch] : subframe_guarded_) {
        transition(dest, kTexture, texture_copy_state);
        if (!save) {
            transition(scratch, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
        }
    }
    flush();
    for (const auto& [dest, scratch] : subframe_guarded_) {
        if (save) {
            command_list_->CopyResource(scratch, dest);
        } else {
            command_list_->CopyResource(dest, scratch);
        }
    }
    for (const auto& [dest, scratch] : subframe_guarded_) {
        transition(dest, texture_copy_state, kTexture);
        if (!save) {
            transition(scratch, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        }
    }
    flush();
}

bool RendererD3D12::ensure_replay_store(ReplayTextures& log) {
    if (srv_increment_ == 0u) {
        srv_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    if (log.store) {
        return true;
    }
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = kReplayStoreDescriptors;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&log.store)))) {
        log.store.Reset();
        return false;
    }
    log.store_base = log.store->GetCPUDescriptorHandleForHeapStart();
    return true;
}

void RendererD3D12::note_replay_texture_table(D3D12_GPU_DESCRIPTOR_HANDLE table,
                                              const D3D12_CPU_DESCRIPTOR_HANDLE* sources,
                                              ID3D12Resource* const* copies) {
    if (!replay_recording_ || ring_mode_ != RingMode::Interp || table.ptr == 0u) {
        return;
    }
    ReplayTextures& log = replay_textures_;
    if (log.incomplete || log.by_gpu.contains(table.ptr)) {
        return;
    }
    if (log.store_used + kReplayTableSlots > kReplayStoreDescriptors || !ensure_replay_store(log)) {
        log.incomplete = true;
        return;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE destination{
        log.store_base.ptr + static_cast<SIZE_T>(log.store_used) * srv_increment_};
    const UINT destination_size = kReplayTableSlots;
    device_->CopyDescriptors(1u, &destination, &destination_size, kReplayTableSlots, sources, nullptr,
                             D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ReplayTextureTable entry{};
    entry.gpu = table.ptr;
    entry.store = log.store_used;
    for (unsigned m = 0; m < kReplayTableSlots; ++m) {
        entry.copies[m] = copies[m];
    }
    log.by_gpu.emplace(table.ptr, static_cast<std::uint32_t>(log.tables.size()));
    log.tables.push_back(entry);
    log.store_used += kReplayTableSlots;
}

D3D12_CPU_DESCRIPTOR_HANDLE RendererD3D12::copy_texture_srv(std::uint32_t index) const {
    return {copy_srv_base_.ptr + static_cast<SIZE_T>(copy_pool_[index].srv) * srv_increment_};
}

std::int32_t RendererD3D12::acquire_copy_texture(ID3D12Resource* like, bool snapshot) {
    const D3D12_RESOURCE_DESC want = like->GetDesc();
    for (std::size_t i = 0; i < copy_pool_.size(); ++i) {
        CopyTexture& entry = copy_pool_[i];
        const D3D12_RESOURCE_DESC& have = entry.desc;
        if (entry.snapshot != snapshot || have.Width != want.Width || have.Height != want.Height ||
            have.Format != want.Format || have.MipLevels != want.MipLevels ||
            have.DepthOrArraySize != want.DepthOrArraySize || have.Flags != want.Flags) {
            continue;
        }
        // Targets serve one destination per sub-frame; snapshots one log
        // until its ring segment comes round again.
        if (snapshot ? entry.segment != ~0u : entry.used == subframe_counter_) {
            continue;
        }
        if (snapshot) {
            entry.segment = ring_segment_;
        } else {
            entry.used = subframe_counter_;
        }
        return static_cast<std::int32_t>(i);
    }
    if (srv_increment_ == 0u) {
        srv_increment_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    if (!copy_srv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = kCopySrvCapacity;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&copy_srv_heap_)))) {
            copy_srv_heap_.Reset();
            return -1;
        }
        copy_srv_base_ = copy_srv_heap_->GetCPUDescriptorHandleForHeapStart();
    }
    if (copy_srv_used_ >= kCopySrvCapacity) {
        return -1;
    }
    CopyTexture entry{};
    D3D12_RESOURCE_DESC desc = want;
    desc.Alignment = 0u;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                                IID_PPV_ARGS(&entry.texture)))) {
        return -1;
    }
    entry.texture->SetName(snapshot ? L"sub-frame copy snapshot" : L"sub-frame copy target");
    entry.desc = want;
    entry.srv = copy_srv_used_++;
    entry.snapshot = snapshot;
    if (snapshot) {
        entry.segment = ring_segment_;
    } else {
        entry.used = subframe_counter_;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = want.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = want.MipLevels;
    copy_pool_.push_back(std::move(entry));
    const auto index = static_cast<std::uint32_t>(copy_pool_.size() - 1u);
    device_->CreateShaderResourceView(copy_pool_[index].texture.Get(), &view, copy_texture_srv(index));
    return static_cast<std::int32_t>(index);
}

// Recording: before the first draw with this table, every copy texture it
// samples that this log has not written yet (an earlier frame's capture) is
// copied, so the log's sub-frames read it as this frame did.
void RendererD3D12::take_replay_snapshots(std::uint64_t table) {
    ReplayTextures& log = replay_textures_;
    const auto found = log.by_gpu.find(table);
    if (found == log.by_gpu.end()) {
        return;
    }
    ReplayTextureTable& entry = log.tables[found->second];
    if (entry.snapshots_checked || log.incomplete) {
        return;
    }
    entry.snapshots_checked = true;
    for (ID3D12Resource* source : entry.copies) {
        if (source == nullptr ||
            std::find(log.written.begin(), log.written.end(), source) != log.written.end()) {
            continue;
        }
        bool taken = false;
        for (const auto& snapshot : log.snapshots) {
            taken = taken || snapshot.first == source;
        }
        if (taken) {
            continue;
        }
        const std::int32_t index = acquire_copy_texture(source, true);
        if (index < 0) {
            log.incomplete = true;
            return;
        }
        ID3D12Resource* const copy = copy_pool_[static_cast<std::size_t>(index)].texture.Get();
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for (auto& barrier : barriers) {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        barriers[0].Transition.pResource = source;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[1].Transition.pResource = copy;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        command_list_->ResourceBarrier(2u, barriers);
        command_list_->CopyResource(copy, source);
        for (auto& barrier : barriers) {
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        }
        command_list_->ResourceBarrier(2u, barriers);
        log.snapshots.emplace_back(source, static_cast<std::uint32_t>(index));
        ++copy_redirect_stats_.snapshots;
    }
}

// Playback: the table with every slot that names a redirected copy texture
// pointed at its stand-in (a copy in the current descriptor segment).
D3D12_GPU_DESCRIPTOR_HANDLE RendererD3D12::patch_replay_table(D3D12_GPU_DESCRIPTOR_HANDLE table) {
    const auto found = run_textures_.by_gpu.find(table.ptr);
    if (found == run_textures_.by_gpu.end()) {
        return table;
    }
    const ReplayTextureTable& entry = run_textures_.tables[found->second];
    D3D12_CPU_DESCRIPTOR_HANDLE sources[kReplayTableSlots]{};
    bool redirected = false;
    for (unsigned m = 0; m < kReplayTableSlots; ++m) {
        sources[m].ptr = run_textures_.store_base.ptr +
            static_cast<SIZE_T>(entry.store + m) * srv_increment_;
        if (entry.copies[m] != nullptr) {
            if (const auto stand_in = redirect_.find(entry.copies[m]); stand_in != redirect_.end()) {
                sources[m] = copy_texture_srv(stand_in->second);
                redirected = true;
            }
        }
    }
    if (!redirected) {
        return table;
    }
    auto& cached = patched_tables_[table.ptr];
    if (cached.first == redirect_epoch_ + 1u) {
        return {cached.second};
    }
    const DescriptorRing::Table patched = srv_ring_.allocate(kReplayTableSlots);
    const UINT destination_size = kReplayTableSlots;
    device_->CopyDescriptors(1u, &patched.cpu, &destination_size, kReplayTableSlots, sources, nullptr,
                             D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cached = {redirect_epoch_ + 1u, patched.gpu.ptr};
    ++copy_redirect_stats_.patched_tables;
    return patched.gpu;
}

void RendererD3D12::release_copy_snapshots(unsigned segment) {
    for (CopyTexture& entry : copy_pool_) {
        if (entry.snapshot && (segment == ~0u || entry.segment == segment)) {
            entry.segment = ~0u;
        }
    }
}

constexpr D3D12_RESOURCE_STATES kInterpOutputRead =
    D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

bool RendererD3D12::ensure_interp_eval() {
    if (interp_eval_pso_) {
        return true;
    }
    if (interp_eval_failed_ || !device_) {
        return false;
    }
    interp_eval_failed_ = true;
    D3D12_ROOT_PARAMETER params[6]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = sizeof(InterpEvalFrame) / 4u;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (unsigned i = 0; i < 3u; ++i) {
        params[1u + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[1u + i].Descriptor.ShaderRegister = i;
        params[1u + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (unsigned i = 0; i < 2u; ++i) {
        params[4u + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[4u + i].Descriptor.ShaderRegister = i;
        params[4u + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 6;
    rsd.pParameters = params;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
        FAILED(device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                            IID_PPV_ARGS(&interp_eval_root_)))) {
        return false;
    }
    try {
        for (const auto& [entry, pso] : {std::pair{UtilityShader::InterpCopyCs, std::addressof(interp_copy_pso_)},
                                          std::pair{UtilityShader::InterpEvalCs, std::addressof(interp_eval_pso_)}}) {
            const UtilityShaderBytes cs = utility_shader(entry);
            D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = interp_eval_root_.Get();
            desc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
            if (FAILED(device_->CreateComputePipelineState(&desc, IID_PPV_ARGS(pso->ReleaseAndGetAddressOf())))) {
                interp_eval_pso_.Reset();
                return false;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "[interp-eval] shader: " << error.what() << '\n';
        interp_eval_pso_.Reset();
        return false;
    }
    interp_eval_failed_ = false;
    return true;
}

bool RendererD3D12::ensure_interp_output(std::uint64_t bytes) {
    bytes = std::max<std::uint64_t>(bytes, 256u);
    if (interp_output_ && interp_output_bytes_ >= bytes) {
        return true;
    }
    std::uint64_t size = std::max<std::uint64_t>(interp_output_bytes_ * 2u, 4ull << 20u);
    while (size < bytes) {
        size *= 2u;
    }
    if (interp_output_) {
        // Replays still in flight read the old one.
        interp_retired_.push_back({std::move(interp_output_), next_fence_value_});
        interp_output_bytes_ = 0u;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                IID_PPV_ARGS(&interp_output_)))) {
        return false;
    }
    interp_output_->SetName(L"interpolated constants");
    interp_output_bytes_ = size;
    interp_output_state_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    return true;
}

void RendererD3D12::transition_interp_output(D3D12_RESOURCE_STATES state) {
    if (interp_output_state_ == state) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = interp_output_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = interp_output_state_;
    barrier.Transition.StateAfter = state;
    command_list_->ResourceBarrier(1, &barrier);
    interp_output_state_ = state;
}

void RendererD3D12::transition_blend_verdicts(D3D12_RESOURCE_STATES state) {
    if (!blend_verdicts_ || blend_verdicts_state_ == state) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = blend_verdicts_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = blend_verdicts_state_;
    barrier.Transition.StateAfter = state;
    command_list_->ResourceBarrier(1, &barrier);
    blend_verdicts_state_ = state;
}

// The sequence's base copies (first sub-frame) and its ops at t, into the
// output read by the replay. The frame at t = 1 takes the copies again (its
// constants as the game frame recorded them), the stores (output addressing)
// and the ops at the frame before (its motion vectors).
void RendererD3D12::record_interp_eval(const SubframeTime& time, const Affine34& camera_t,
                                       const Affine34& camera_prev) {
    transition_interp_output(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition_blend_verdicts(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    // The ray-traced lighting of the replayed frame follows this sub-frame's camera, and the composites rebuild its
    // points with the view its depth is drawn with: its projection at t with this frame's jitter in it, as
    // RenderCore::scene_depth_view gives a traced frame's (the draws add the jitter in clip space, x + jx w: the
    // projection's B becomes B - jx, D becomes D - jy).
    RtView own_view{};
    const bool own_known = time.projection_known && time.viewport_width > 0.0f && time.viewport_height > 0.0f;
    if (own_known) {
        constexpr float kFarEps = 1.0f - 1e-7f;  // fill_vs_projection's
        const std::array<float, 6>& p = time.projection;
        own_view.projection = {p[0], p[1] - 2.0f * time.jitter_x / time.viewport_width, p[2],
                               p[3] + 2.0f * time.jitter_y / time.viewport_height};
        own_view.z_scale = -p[4] * kFarEps;
        own_view.z_offset = -p[5] * kFarEps;
        own_view.jitter = {time.jitter_x, time.jitter_y};
    }
    const bool last = time.t >= 1.0f;
    InterpEvalFrame frame{};
    std::memcpy(frame.camera, camera_t.data(), sizeof(frame.camera));
    std::memcpy(frame.camera_prev, camera_prev.data(), sizeof(frame.camera_prev));
    frame.mode = last ? 1u : 0u;
    frame.t = time.t;
    frame.t_prev = time.t_prev;
    frame.jitter[0] = time.jitter_x;
    frame.jitter[1] = time.jitter_y;
    frame.payload = sequence_.payload_offset;
    frame.program_last = sequence_.program_bytes - 16u;
    frame.output_last = static_cast<std::uint32_t>(interp_output_bytes_ - 4u);
    frame.constants_last = static_cast<std::uint32_t>(std::min<std::uint64_t>(constant_ring_.total_bytes() - 4u, 0xFFFFFFF0u));
    frame.matrices_last = static_cast<std::uint32_t>(std::min<std::uint64_t>(matrix_ring_.total_bytes() - 4u, 0xFFFFFFF0u));
    frame.frame_slot = kFrameValuesElement;
    command_list_->SetComputeRootSignature(interp_eval_root_.Get());
    command_list_->SetComputeRootShaderResourceView(1, sequence_.program);
    command_list_->SetComputeRootShaderResourceView(2, constant_ring_.gpu_base());
    command_list_->SetComputeRootShaderResourceView(3, matrix_ring_.gpu_base());
    command_list_->SetComputeRootUnorderedAccessView(4, interp_output_->GetGPUVirtualAddress());
    command_list_->SetComputeRootUnorderedAccessView(5, blend_verdicts_->GetGPUVirtualAddress());
    if ((sequence_.first || last) && sequence_.copies != 0u) {
        frame.count = sequence_.copies;
        frame.table = sequence_.copies_offset;
        command_list_->SetPipelineState(interp_copy_pso_.Get());
        command_list_->SetComputeRoot32BitConstants(0, sizeof(frame) / 4u, &frame, 0u);
        command_list_->Dispatch(sequence_.copies, 1u, 1u);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = interp_output_.Get();
        command_list_->ResourceBarrier(1, &barrier);
    }
    frame.count = sequence_.ops;
    frame.table = sequence_.ops_offset;
    command_list_->SetPipelineState(interp_eval_pso_.Get());
    command_list_->SetComputeRoot32BitConstants(0, sizeof(frame) / 4u, &frame, 0u);
    command_list_->Dispatch(std::max(1u, (sequence_.ops + 63u) / 64u), 1u, 1u);
    transition_interp_output(kInterpOutputRead);
    transition_blend_verdicts(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    bound_pipeline_ = nullptr;
}

bool RendererD3D12::begin_replay_sequence(const ReplaySequence& sequence) {
    sequence_.ready = false;
    if (ring_mode_ != RingMode::Interp && replay_valid_ && !run_valid_) {
        (void)start_replay_playback();
    }
    if (!run_valid_ || !device_ || !swap_chain_ || sequence.program == nullptr) {
        replay_failure_ = run_valid_ ? 2u : 1u;
        return false;
    }
    const ReplayCopy* display = nullptr;
    FrameMap output_map{};  // the log switches to the output scene target at this size
    for (const ReplayCopy& copy : run_copies_) {
        if (copy.mode == 1u) display = &copy;
        if (copy.mode == 2u) output_map = FrameMap(copy.width, copy.height);
    }
    if (display == nullptr || run_start_output_active_) {
        // Logs begin at the ring advance after a display copy, on the render
        // scene target; the private sets below assume it.
        replay_failure_ = display == nullptr ? 3u : 6u;
        return false;
    }
    if (!ensure_subframe_resources(display->width, display->height)) {
        replay_failure_ = 4u;
        return false;
    }
    if (!ensure_interp_eval()) {
        replay_failure_ = 7u;
        return false;
    }
    if (!ensure_interp_output(sequence.layout.bytes())) {
        replay_failure_ = 8u;
        return false;
    }
    const std::uint64_t completed = completed_fence_value();
    std::erase_if(interp_retired_, [completed](const RetiredResource& retired) {
        return retired.fence <= completed;
    });
    const auto record_start = std::chrono::steady_clock::now();

    // Base copies: each moving block as its draws read it this game frame.
    const interp_gpu::Layout& layout = sequence.layout;
    const D3D12_GPU_VIRTUAL_ADDRESS constants_base = constant_ring_.gpu_base();
    const std::uint64_t constants_bytes = constant_ring_.total_bytes();
    const std::uint64_t matrices_bytes = matrix_ring_.total_bytes();
    interp_copies_.clear();
    interp_copied_.assign(static_cast<std::size_t>(layout.vs_count), 0u);
    const auto copy_block = [&](D3D12_GPU_VIRTUAL_ADDRESS va, std::uint32_t bytes, std::uint32_t dst) {
        if (va < constants_base || va + bytes > constants_base + constants_bytes) {
            return false;
        }
        interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Constants),
                                  static_cast<std::uint32_t>(va - constants_base), dst, bytes / 4u});
        return true;
    };
    for (const DrawCall& call : run_draws_) {
        if (call.replay_record >= 0 && static_cast<std::size_t>(call.replay_record) < sequence.record_vs_slot.size()) {
            const std::int32_t slot = sequence.record_vs_slot[static_cast<std::size_t>(call.replay_record)];
            if (slot >= 0 && interp_copied_[static_cast<std::size_t>(slot)] == 0u) {
                interp_copied_[static_cast<std::size_t>(slot)] = 1u;
                if (!copy_block(call.vs_constants, interp_gpu::kVsBytes,
                                layout.vs_offset(static_cast<std::uint32_t>(slot)))) {
                    replay_failure_ = 9u;
                    return false;
                }
            }
        }
    }
    // Each palette slot takes its source draw's blocks (draw_matrices.h) at their rows; a motion vector's slot its
    // position and texture blocks.
    static constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 5> kBlocks{{
        {draw_rows::kPosition, draw_rows::kPositionRows}, {draw_rows::kTexture, draw_rows::kTextureRows},
        {draw_rows::kNormal, draw_rows::kNormalRows}, {draw_rows::kPost, draw_rows::kPostRows},
        {draw_rows::kLight, draw_rows::kLightRows}}};
    for (std::uint32_t q = 0; q < layout.palette_count && q < sequence.palette_source.size(); ++q) {
        const std::array<std::uint32_t, 5>& source = sequence.palette_source[q];
        for (std::size_t k = 0; k < kBlocks.size(); ++k) {
            if ((static_cast<std::uint64_t>(source[k]) + kBlocks[k].second) * 16u > matrices_bytes) {
                replay_failure_ = 9u;
                return false;
            }
        }
        const auto slot = [&](std::uint32_t offset, std::uint32_t rows, std::size_t blocks) {
            const std::uint32_t first = offset + rows * 4u;  // its rows, after the unused float4
            for (std::size_t k = 0; k < blocks; ++k) {
                interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Matrices), source[k] * 16u,
                                          first + kBlocks[k].first * 16u, kBlocks[k].second * 4u});
            }
        };
        slot(layout.palette_offset(q), interp_gpu::kPaletteRows, kBlocks.size());
        if (q < layout.prev_palette_count) {
            slot(layout.prev_palette_offset(q), interp_gpu::kPrevPaletteRows, 2u);
        }
    }

    // The program: copies, ops, payload (16 bytes of tail room for Load4).
    const interp_gpu::Program& program = *sequence.program;
    const std::size_t copies_bytes = interp_copies_.size() * sizeof(interp_gpu::Copy);
    const std::size_t ops_bytes = program.ops.size() * sizeof(interp_gpu::Op);
    const std::size_t payload_bytes = program.payload.size() * sizeof(std::uint32_t);
    const std::size_t table_bytes = copies_bytes + ops_bytes + payload_bytes + 16u;
    const UploadRing::Allocation table = constant_ring_.allocate(table_bytes, 256u);
    std::memcpy(table.cpu, interp_copies_.data(), copies_bytes);
    std::memcpy(table.cpu + copies_bytes, program.ops.data(), ops_bytes);
    std::memcpy(table.cpu + copies_bytes + ops_bytes, program.payload.data(), payload_bytes);
    std::memset(table.cpu + copies_bytes + ops_bytes + payload_bytes, 0, 16u);

    sequence_ = SequenceState{};
    sequence_.ready = true;
    sequence_.first = true;
    sequence_.serial = ++sequence_serial_;
    sequence_.output_map = output_map;
    sequence_.overrides = sequence;
    sequence_.alloc_segment = ring_segment_;
    sequence_.program = table.gpu;
    sequence_.program_bytes = static_cast<std::uint32_t>(table_bytes);
    sequence_.copies = static_cast<std::uint32_t>(interp_copies_.size());
    sequence_.ops = static_cast<std::uint32_t>(program.ops.size());
    sequence_.copies_offset = 0u;
    sequence_.ops_offset = static_cast<std::uint32_t>(copies_bytes);
    sequence_.payload_offset = static_cast<std::uint32_t>(copies_bytes + ops_bytes);
    sequence_record_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - record_start).count();
    ++sequence_record_samples_;
    return true;
}

// The playing log's replay into `entry`'s list, with the sequence's
// overrides. A list may run again only once its last execution completed
// (D3D12 rejects a resubmission in flight), so a sequence records another
// list when its sub-frames come faster than the GPU finishes them.
bool RendererD3D12::record_replay_list(ReplayList& entry) {
    settle_owed_scene();
    const ReplaySequence& sequence = sequence_.overrides;
    const interp_gpu::Layout& layout = sequence.layout;
    const FrameMap output_map = sequence_.output_map;
    const bool temporal = temporal_interp_ && temporal_ != nullptr;
    if (!entry.list) {
        d3d_check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&entry.allocator)),
                  "replay CreateCommandAllocator");
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, entry.allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&entry.list)),
                  "replay CreateCommandList");
        d3d_check(entry.list->Close(), "replay Close");
    }
    if (temporal && !entry.list_after) {
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, entry.allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&entry.list_after)),
                  "replay CreateCommandList");
        d3d_check(entry.list_after->Close(), "replay Close");
    }
    const auto record_start = std::chrono::steady_clock::now();
    d3d_check(entry.allocator->Reset(), "replay allocator Reset");
    d3d_check(entry.list->Reset(entry.allocator.Get(), nullptr), "replay Reset");
    entry.split = false;
    // The next game frame may be recording: the replay must not log into it.
    const bool recording = replay_recording_;
    replay_recording_ = false;
    // The list being recorded: the replay, then (temporal upscaling) the part
    // after the output switch.
    // std::addressof: a ComPtr's unary & yields a ComPtrRef that releases it on conversion.
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>* active = std::addressof(entry.list);
    struct Restore {
        RendererD3D12& renderer;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>*& active;
        bool recording;
        ~Restore() {
            renderer.replay_recording_ = recording;
            std::swap(renderer.command_list_, *active);
        }
    };
    std::swap(command_list_, entry.list);
    {
        const Restore restore{*this, active, recording};
        const bool interp_rings = ring_mode_ == RingMode::Interp;
        ID3D12DescriptorHeap* heaps[] = {
            srv_ring_.heap(),
            interp_rings ? sampler_heaps_[run_segment_].Get() : sampler_heap_.Get()};
        command_list_->SetDescriptorHeaps(2, heaps);
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        invalidate_draw_bindings();
        bound_rt_aux_ = 0u;

        ++subframe_counter_;
        subframe_guarded_.clear();
        // Copy redirection: the log's texture copies go to stand-ins, its
        // read-before-write copy textures read the recording frame's snapshots.
        const bool want_redirect = subframe_copy_redirect_ < 0 || subframe_copy_redirect_ == 1;
        const bool redirect = interp_rings && want_redirect && !run_textures_.incomplete;
        redirect_.clear();
        patched_tables_.clear();
        if (redirect) {
            for (const auto& [source, snapshot] : run_textures_.snapshots) {
                redirect_[source] = snapshot;
            }
        } else if (interp_rings && want_redirect) {
            ++copy_redirect_stats_.fallback_logs;
        }
        // Without redirection every copy texture the log writes is saved and
        // restored around the sub-frame: real frames read copies of earlier
        // frames (feedback blur, camera-cut cover), never a sub-frame's.
        for (const ReplayCopy& copy : run_copies_) {
            if (redirect || subframe_copy_redirect_ == 2) break;
            if (copy.mode != 0u || copy.dest == nullptr) continue;
            bool listed = false;
            for (const auto& guarded : subframe_guarded_) listed = listed || guarded.first == copy.dest;
            if (listed) continue;
            if (ID3D12Resource* scratch = acquire_subframe_scratch(copy.dest)) {
                subframe_guarded_.emplace_back(copy.dest, scratch);
            }
        }
        // Private scene target sets stand in for the live ones, so a later frame's partial
        // EFBs (its chunks interleave with the sub-frames) stay intact: one at the
        // render scene target's scale and, for a log that switches to the output scene target (HUD
        // at output resolution), one at that scale.
        const FrameMap render_map = output_scene_active_ ? alt_scene_.map : frame_;
        const unsigned render_samples = output_scene_active_ ? alt_scene_.msaa.samples : scene_msaa_.samples;
        if (((!replay_scene_.color || replay_scene_.map != render_map ||
              replay_scene_.msaa.samples != render_samples) &&
             !create_scene_set(replay_scene_, replay_scene_rtv_heap_, replay_scene_dsv_heap_, render_map, true, false,
                             render_samples)) ||
            (!output_map.empty() &&
             (!replay_output_scene_.color || replay_output_scene_.map != output_map) &&
             !create_scene_set(replay_output_scene_, replay_output_scene_rtv_heap_,
                             replay_output_scene_dsv_heap_, output_map, false, true))) {
            command_list_->Close();
            entry.sequence = 0u;
            replay_failure_ = 5u;
            return false;
        }
        if (!output_map.empty()) {
            // Its conversion-source views live in this game frame's descriptor
            // segment (the persistent prefix is sized for the live sets).
            write_scene_set_views(replay_output_scene_, srv_ring_.allocate(2u));
        }
        record_subframe_guard(true);

        // The live sets at rest (render scene target current), then the private ones in.
        const bool output_before = output_scene_active_;
        if (output_before) {
            swap_scene_sets();
        }
        swap_scene_with(replay_scene_);
        if (!output_map.empty()) {
            std::swap(alt_scene_, replay_output_scene_);
        }
        // Sub-frames write their motion vectors and reactive mask to their own
        // targets (the game frame's are live meanwhile).
        const bool motion_swapped = temporal && ensure_replay_motion_targets();
        if (motion_swapped) {
            swap_motion_targets();
            const float zero[4]{};
            command_list_->ClearRenderTargetView(motion_rtv_, zero, 0u, nullptr);
            command_list_->ClearRenderTargetView(reactive_rtv_, zero, 0u, nullptr);
        }
        replay_playing_ = true;
        bind_scene();
        const auto far_values = make_scene_clear_values(0u, 0u);
        // The log before's vertices are gone once its segment came round (a
        // later game frame records there while this sequence still plays).
        const bool blend = sequence.vertex_blend && prev_run_valid_ &&
                           (!blend_pairs_.empty() || !stripe_gather_.empty()) &&
                           run_blend_vertices_.size() == run_draws_.size();
        entry.blend_segment = prev_run_segment_;
        const D3D12_GPU_VIRTUAL_ADDRESS output = interp_output_->GetGPUVirtualAddress();
        std::uint64_t blended = 0u;
        for (const ReplayOp& op : run_ops_) {
            switch (op.kind) {
            case ReplayKind::Draw: {
                DrawCall call = run_draws_[op.index];
                if (call.replay_record >= 0 &&
                    static_cast<std::size_t>(call.replay_record) < sequence.record_vs_slot.size()) {
                    const auto record = static_cast<std::size_t>(call.replay_record);
                    if (const std::int32_t slot = sequence.record_vs_slot[record]; slot >= 0) {
                        call.vs_constants = output + layout.vs_offset(static_cast<std::uint32_t>(slot));
                    }
                    if (record < sequence.record_palette_slot.size() && sequence.record_palette_slot[record] >= 0) {
                        call.matrix_palette = output + layout.palette_offset(
                                                           static_cast<std::uint32_t>(sequence.record_palette_slot[record]));
                        call.matrix_rows = output + layout.palette_base();
                    }
                }
                if (blend && run_blend_vertices_[op.index].vertices != 0u) {
                    call.blend_vertices = run_blend_vertices_[op.index].vertices;
                    call.blend_slot = run_blend_vertices_[op.index].slot;
                    ++blended;
                }
                if (!redirect_.empty() && call.texture_table.ptr != 0u) {
                    call.texture_table = patch_replay_table(call.texture_table);
                }
                draw(call);
                break;
            }
            case ReplayKind::BindScene:
                bind_scene();
                break;
            case ReplayKind::Clear: {
                const ReplayClear& c = run_clears_[op.index];
                clear_scene(c.rgba, c.color, c.alpha, c.depth, c.x, c.y, c.w, c.h, c.format);
                break;
            }
            case ReplayKind::Copy: {
                const ReplayCopy& copy = run_copies_[op.index];
                if (motion_swapped && copy.mode == 2u && !entry.split) {
                    // The output switch is the temporal upscale, recorded per
                    // presented frame (record_subframe_upscale); the rest of
                    // the log goes to the second list.
                    entry.split = true;
                    entry.upscale = {scene_color_.Get(), scene_depth_.Get(), motion_target_.Get(), reactive_target_.Get(),
                                     alt_scene_.color.Get(), frame_.width, frame_.height,
                                     copy.width, copy.height, op.index};
                    d3d_check(command_list_->Close(), "replay Close");
                    std::swap(command_list_, *active);
                    active = std::addressof(entry.list_after);
                    d3d_check(entry.list_after->Reset(entry.allocator.Get(), nullptr), "replay Reset");
                    std::swap(command_list_, *active);
                    const bool interp_segment = ring_mode_ == RingMode::Interp;
                    ID3D12DescriptorHeap* after_heaps[] = {
                        srv_ring_.heap(),
                        interp_segment ? sampler_heaps_[run_segment_].Get() : sampler_heap_.Get()};
                    command_list_->SetDescriptorHeaps(2, after_heaps);
                    command_list_->SetGraphicsRootSignature(root_signature_.Get());
                    invalidate_draw_bindings();
                    bound_rt_aux_ = 0u;
                    break;
                }
                if (redirect && copy.mode == 0u && copy.dest != nullptr) {
                    // One stand-in per destination per sequence (never a snapshot).
                    std::int32_t stand_in = -1;
                    if (const auto current = redirect_.find(copy.dest);
                        current != redirect_.end() && !copy_pool_[current->second].snapshot) {
                        stand_in = static_cast<std::int32_t>(current->second);
                    } else {
                        stand_in = acquire_copy_texture(copy.dest, false);
                    }
                    if (stand_in >= 0) {
                        execute_replay_copy(copy, copy_pool_[static_cast<std::size_t>(stand_in)].texture.Get());
                        redirect_[copy.dest] = static_cast<std::uint32_t>(stand_in);
                        ++redirect_epoch_;
                        ++copy_redirect_stats_.redirected;
                        break;
                    }
                    ++copy_redirect_stats_.fallback_logs;
                }
                execute_replay_copy(copy);
                break;
            }
            case ReplayKind::SwapSceneSets:
                swap_scene_sets();
                break;
            case ReplayKind::ClearDepth:
                command_list_->ClearDepthStencilView(
                    scene_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                    far_values.reversed_depth, 0u, 0u, nullptr);
                break;
            case ReplayKind::LayerBegin:
                record_layer_begin();
                break;
            case ReplayKind::SceneAa:
                record_scene_aa();
                break;
            case ReplayKind::SceneAo:
                record_scene_ao();
                break;
            case ReplayKind::Bloom:
                record_game_bloom(run_blooms_[op.index]);
                break;
            case ReplayKind::HdrBegin:
                record_hdr_begin();
                break;
            case ReplayKind::HdrEnd:
                record_hdr_end();
                break;
            }
        }
        // The log ends after its display copy, on the output scene target when it switched.
        if (output_scene_active_) {
            swap_scene_sets();
        }
        if (!output_map.empty()) {
            std::swap(alt_scene_, replay_output_scene_);
        }
        swap_scene_with(replay_scene_);
        if (output_before) {
            swap_scene_sets();
        }
        if (motion_swapped) {
            swap_motion_targets();
        }
        replay_playing_ = false;
        record_subframe_guard(false);
        // Scratch textures unused for many sequences (a copy size changed) go;
        // the allocator fence waits retired their last use long ago.
        std::erase_if(subframe_scratch_, [this](const SubframeScratch& scratch) {
            return scratch.used + 1024u < subframe_counter_;
        });
        d3d_check(command_list_->Close(), "replay Close");
        invalidate_draw_bindings();

        entry.sequence = sequence_.serial;
        entry.blend = blend && blended != 0u;
        entry.redirect = redirect;
        entry.blended_draws = blended;
        entry.guarded = subframe_guarded_.size();
        entry.alloc_segment = ring_segment_;
    }
    invalidate_draw_bindings();
    sequence_record_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - record_start).count();
    ++replay_records_;
    return true;
}

bool RendererD3D12::present_replay_subframe(const SubframeTime& given, const Affine34& camera_t,
                                            const Affine34& camera_prev) {
    if (!sequence_.ready || !run_valid_ || !device_ || !swap_chain_) {
        replay_failure_ = sequence_.ready ? 2u : 10u;
        return false;
    }
    SubframeTime time = given;
    if (temporal_interp_ && temporal_ && temporal_render_w_ != 0u) {
        // Each presented frame takes the next jitter phase.
        const unsigned phases = std::max(temporal_->jitter_phase_count(temporal_render_w_, temporal_output_w_), 1u);
        temporal_->jitter_offset(static_cast<unsigned>(temporal_frame_ % phases), phases, time.jitter_x,
                                 time.jitter_y);
        ++temporal_frame_;
    }
    // A list of this sequence whose last execution completed; else a free one
    // (completed longest ago) recorded now. A list that blends with the log
    // before is not reused once that log's segment came round.
    const std::uint64_t completed = completed_fence_value();
    ReplayList* replay = nullptr;
    for (ReplayList& entry : replay_lists_) {
        if (entry.sequence == sequence_.serial && entry.fence <= completed &&
            (!entry.blend || (prev_run_valid_ && entry.blend_segment == prev_run_segment_))) {
            replay = &entry;
            break;
        }
    }
    if (replay == nullptr) {
        ReplayList* oldest = &replay_lists_[0];
        for (ReplayList& entry : replay_lists_) {
            if (entry.fence < oldest->fence) oldest = &entry;
        }
        if (oldest->fence > completed) {
            wait_for_fence_value(oldest->fence);
        }
        if (!record_replay_list(*oldest)) {
            return false;
        }
        replay = oldest;
    }
    if (has_presented_swap_chain_) {
        wait_for_frame_latency_if_needed(trace_present_stats_enabled());
    }
    const unsigned index = subframe_next_;
    subframe_next_ = (subframe_next_ + 1u) % kSubframeAllocators;
    if (subframe_fences_[index] != 0u) {
        wait_for_fence_value(subframe_fences_[index]);
    }
    if (subframe_timestamp_pending_[index] && subframe_readback_ && timestamp_frequency_ != 0u) {
        const SIZE_T offset = static_cast<SIZE_T>(index) * 4u * sizeof(std::uint64_t);
        const D3D12_RANGE range{offset, offset + 4u * sizeof(std::uint64_t)};
        void* mapped = nullptr;
        if (SUCCEEDED(subframe_readback_->Map(0, &range, &mapped))) {
            const auto* ticks = reinterpret_cast<const std::uint64_t*>(
                static_cast<const std::byte*>(mapped) + offset);
            const double to_ms = 1000.0 / static_cast<double>(timestamp_frequency_);
            if (ticks[3] >= ticks[2] && ticks[2] >= ticks[1] && ticks[1] >= ticks[0]) {
                subframe_phase_ms_[0] += static_cast<double>(ticks[1] - ticks[0]) * to_ms;
                subframe_phase_ms_[1] += static_cast<double>(ticks[2] - ticks[1]) * to_ms;
                subframe_phase_ms_[2] += static_cast<double>(ticks[3] - ticks[2]) * to_ms;
            }
            if (ticks[3] >= ticks[0]) {
                const double ms = static_cast<double>(ticks[3] - ticks[0]) * to_ms;
                subframe_gpu_ms_ += ms;
                ++subframe_gpu_samples_;
                subframe_gpu_ema_ms_ = subframe_gpu_ema_ms_ == 0.0
                    ? ms : subframe_gpu_ema_ms_ + (ms - subframe_gpu_ema_ms_) * 0.0625;
                subframe_path_gpu_ms_[subframe_timestamp_redirect_[index]] += ms;
                ++subframe_path_gpu_samples_[subframe_timestamp_redirect_[index]];
            }
            const D3D12_RANGE none{0, 0};
            subframe_readback_->Unmap(0, &none);
        }
        subframe_timestamp_pending_[index] = false;
    }
    const auto record_start = std::chrono::steady_clock::now();
    d3d_check(subframe_allocators_[index]->Reset(), "sub-frame allocator Reset");
    if (!subframe_eval_list_) {
        d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, subframe_allocators_[index].Get(),
                                             nullptr, IID_PPV_ARGS(&subframe_eval_list_)),
                  "sub-frame CreateCommandList");
    } else {
        d3d_check(subframe_eval_list_->Reset(subframe_allocators_[index].Get(), nullptr),
                  "sub-frame evaluation Reset");
    }
    {
        // The evaluation list: the blend check and base copies of a sequence's
        // first sub-frame, then the ops at t.
        struct Swap {
            RendererD3D12& renderer;
            ~Swap() { std::swap(renderer.command_list_, renderer.subframe_eval_list_); }
        };
        std::swap(command_list_, subframe_eval_list_);
        const Swap swap{*this};
        if (subframe_query_heap_) {
            command_list_->EndQuery(subframe_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 4u);
        }
        if (replay->blend && !sequence_.validated) {
            record_blend_validation();
            sequence_.validated = true;
        }
        record_interp_eval(time, camera_t, camera_prev);
        if (subframe_query_heap_) {
            command_list_->EndQuery(subframe_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 4u + 1u);
        }
        d3d_check(command_list_->Close(), "sub-frame evaluation Close");
    }
    if (replay->split) {
        // The temporal upscale of this presented frame, between the replay's two lists.
        if (!subframe_upscale_list_) {
            d3d_check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, subframe_allocators_[index].Get(),
                                                 nullptr, IID_PPV_ARGS(&subframe_upscale_list_)),
                      "sub-frame CreateCommandList");
        } else {
            d3d_check(subframe_upscale_list_->Reset(subframe_allocators_[index].Get(), nullptr),
                      "sub-frame upscale Reset");
        }
        struct Swap {
            RendererD3D12& renderer;
            ~Swap() { std::swap(renderer.command_list_, renderer.subframe_upscale_list_); }
        };
        std::swap(command_list_, subframe_upscale_list_);
        const Swap swap{*this};
        ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
        command_list_->SetDescriptorHeaps(2, heaps);
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        invalidate_draw_bindings();
        record_subframe_upscale(*replay, time);
        d3d_check(command_list_->Close(), "sub-frame upscale Close");
    }
    d3d_check(command_list_->Reset(subframe_allocators_[index].Get(), nullptr), "sub-frame CommandList Reset");
    if (subframe_query_heap_) {
        command_list_->EndQuery(subframe_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 4u + 2u);
    }
    {
        ID3D12DescriptorHeap* live[] = {srv_ring_.heap(), sampler_heap_.Get()};
        command_list_->SetDescriptorHeaps(2, live);
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        invalidate_draw_bindings();
    }
    (void)present(&subframe_display_, subframe_capture_);
    const bool readback = eval_readback_requested_ && eval_readback_fence_ == 0u;
    if (readback) {
        eval_readback_requested_ = false;
        if (!eval_readback_ || eval_readback_bytes_ < interp_output_bytes_) {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = interp_output_bytes_;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            eval_readback_.Reset();
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                        IID_PPV_ARGS(&eval_readback_)))) {
                eval_readback_.Reset();
            }
            eval_readback_bytes_ = eval_readback_ ? interp_output_bytes_ : 0u;
        }
        if (eval_readback_) {
            transition_interp_output(D3D12_RESOURCE_STATE_COPY_SOURCE);
            command_list_->CopyBufferRegion(eval_readback_.Get(), 0u, interp_output_.Get(), 0u,
                                            interp_output_bytes_);
            transition_interp_output(kInterpOutputRead);
            eval_readback_t_ = time.t;
        }
    }
    if (subframe_query_heap_) {
        command_list_->EndQuery(subframe_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 4u + 3u);
        command_list_->ResolveQueryData(subframe_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                        index * 4u, 4u, subframe_readback_.Get(),
                                        static_cast<UINT64>(index) * 4u * sizeof(std::uint64_t));
        subframe_timestamp_pending_[index] = true;
        subframe_timestamp_redirect_[index] =
            replay->redirect ? 1u : subframe_copy_redirect_ == 2 ? 2u : 0u;
    }
    d3d_check(command_list_->Close(), "sub-frame CommandList Close");
    if (replay->split) {
        ID3D12CommandList* lists[] = {subframe_eval_list_.Get(), replay->list.Get(), subframe_upscale_list_.Get(),
                                      replay->list_after.Get(), command_list_.Get()};
        queue_->ExecuteCommandLists(5, lists);
    } else {
        ID3D12CommandList* lists[] = {subframe_eval_list_.Get(), replay->list.Get(), command_list_.Get()};
        queue_->ExecuteCommandLists(3, lists);
    }
    const std::uint64_t fence = ++next_fence_value_;
    d3d_check(queue_->Signal(fence_.Get(), fence), "sub-frame Signal");
    subframe_fences_[index] = fence;
    replay->fence = fence;
    if (readback && eval_readback_) {
        eval_readback_fence_ = fence;
    }
    if (backbuffer_readback_pending_ && backbuffer_readback_fence_value_ == 0u) {
        backbuffer_readback_fence_value_ = fence;
    }
    if (selected_display_readback_pending_ && selected_display_readback_fence_ == 0u) {
        selected_display_readback_fence_ = fence;
    }
    if (ring_mode_ == RingMode::Interp) {
        // The replay reads the log's segment (and the log before's for blended
        // vertices) and the sequence's allocations; the present list allocates
        // from the current segment. Frame slots are not involved: a game frame's
        // chunks never wait for its sub-frames.
        for (const unsigned segment : {run_segment_, sequence_.alloc_segment, replay->alloc_segment, ring_segment_}) {
            ring_segment_fence_[segment] = std::max(ring_segment_fence_[segment], fence);
        }
        if (replay->blend) {
            ring_segment_fence_[replay->blend_segment] = std::max(ring_segment_fence_[replay->blend_segment], fence);
        }
    } else {
        // The replay read the recorded frame's slot rings and appended to the
        // current ones: neither may be rewound before it completes.
        fence_values_[replay_source_slot_] = std::max(fence_values_[replay_source_slot_], fence);
        fence_values_[(replay_source_slot_ + 1u) % kFramesInFlight] =
            std::max(fence_values_[(replay_source_slot_ + 1u) % kFramesInFlight], fence);
        fence_values_[frame_slot_] = std::max(fence_values_[frame_slot_], fence);
        if (matrix_segments_ != kFramesInFlight) {
            matrix_segment_fence_[matrix_segment_] = std::max(matrix_segment_fence_[matrix_segment_], fence);
        }
    }
    sequence_.first = false;
    vertex_blend_draws_ += replay->blended_draws;
    subframe_guarded_total_ += replay->guarded;
    subframe_record_ms_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - record_start).count();
    ++subframe_record_samples_;
    present_to_swap_chain();
    return true;
}

// Motion and reactive targets of the sub-frames, at the replay scene target's scale
// (the scale current while a replay records).
bool RendererD3D12::ensure_replay_motion_targets() {
    if (replay_motion_.target && replay_motion_.map == frame_) {
        return true;
    }
    if (replay_motion_.target) {
        // A scale change: lists recorded with the old targets are recorded again.
        wait_for_gpu();
        for (ReplayList& list : replay_lists_) {
            list.sequence = 0u;
        }
    }
    replay_motion_ = ReplayMotionTargets{};
    if (!replay_motion_rtv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 4;
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&replay_motion_rtv_heap_)))) {
            return false;
        }
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
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                &cv, IID_PPV_ARGS(&replay_motion_.target)))) {
        return false;
    }
    rd.Format = kReactiveMaskFormat;
    cv.Format = kReactiveMaskFormat;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                &cv, IID_PPV_ARGS(&replay_motion_.reactive)))) {
        replay_motion_ = ReplayMotionTargets{};
        return false;
    }
    rd.Format = kLayerFormat;
    cv.Format = kLayerFormat;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                &cv, IID_PPV_ARGS(&replay_motion_.layer_color))) ||
        FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                &cv, IID_PPV_ARGS(&replay_motion_.layer_trans)))) {
        replay_motion_ = ReplayMotionTargets{};
        return false;
    }
    replay_motion_.target->SetName(L"sub-frame motion vectors");
    replay_motion_.reactive->SetName(L"sub-frame reactive mask");
    replay_motion_.layer_color->SetName(L"sub-frame effects layer colour");
    replay_motion_.layer_trans->SetName(L"sub-frame effects layer transmittance");
    const UINT rtv_step = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    replay_motion_.rtv = replay_motion_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    replay_motion_.reactive_rtv = replay_motion_.rtv;
    replay_motion_.reactive_rtv.ptr += rtv_step;
    replay_motion_.layer_color_rtv = replay_motion_.reactive_rtv;
    replay_motion_.layer_color_rtv.ptr += rtv_step;
    replay_motion_.layer_trans_rtv = replay_motion_.layer_color_rtv;
    replay_motion_.layer_trans_rtv.ptr += rtv_step;
    device_->CreateRenderTargetView(replay_motion_.target.Get(), nullptr, replay_motion_.rtv);
    device_->CreateRenderTargetView(replay_motion_.reactive.Get(), nullptr, replay_motion_.reactive_rtv);
    device_->CreateRenderTargetView(replay_motion_.layer_color.Get(), nullptr, replay_motion_.layer_color_rtv);
    device_->CreateRenderTargetView(replay_motion_.layer_trans.Get(), nullptr, replay_motion_.layer_trans_rtv);
    replay_motion_.map = frame_;
    return true;
}

void RendererD3D12::swap_motion_targets() {
    std::swap(motion_target_, replay_motion_.target);
    std::swap(reactive_target_, replay_motion_.reactive);
    std::swap(layer_color_, replay_motion_.layer_color);
    std::swap(layer_trans_, replay_motion_.layer_trans);
    std::swap(motion_rtv_, replay_motion_.rtv);
    std::swap(reactive_rtv_, replay_motion_.reactive_rtv);
    std::swap(layer_color_rtv_, replay_motion_.layer_color_rtv);
    std::swap(layer_trans_rtv_, replay_motion_.layer_trans_rtv);
    bound_rt_aux_ = 0xFFu;
}

// The output switch of one presented frame: the temporal upscale of the
// replay's scene target into its output scene target, or the plain resample when the upscaler
// could not record it.
void RendererD3D12::record_subframe_upscale(const ReplayList& entry, const SubframeTime& time) {
    const bool recording = replay_recording_;
    replay_recording_ = false;
    const bool output_before = output_scene_active_;
    if (output_before) {
        swap_scene_sets();
    }
    swap_scene_with(replay_scene_);
    std::swap(alt_scene_, replay_output_scene_);
    bool upscaled = false;
    if (temporal_) {
        TemporalUpscaleInputs in{};
        in.list = command_list_.Get();
        in.color = entry.upscale.color;
        in.depth = entry.upscale.depth;
        in.motion = entry.upscale.motion;
        in.reactive = entry.upscale.reactive;
        in.output = entry.upscale.output;
        in.render_width = entry.upscale.render_width;
        in.render_height = entry.upscale.render_height;
        in.output_width = entry.upscale.output_width;
        in.output_height = entry.upscale.output_height;
        in.jitter_x = time.jitter_x;
        in.jitter_y = time.jitter_y;
        in.camera_near = scene_camera_[0];
        in.camera_far = scene_camera_[1];
        in.fov_y = scene_camera_[2];
        in.view = scene_view_;
        in.view_known = scene_view_known_;
        const RenderConfig cfg = get_render_config();
        in.frame_ms = static_cast<float>(1000.0 / present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval,
                                                                      display_refresh_hz_));
        in.reset = time.reset || !temporal_history_valid_;
        upscaled = temporal_inputs_match(in) && temporal_->evaluate(in);
        ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
        command_list_->SetDescriptorHeaps(2, heaps);
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        invalidate_draw_bindings();
    }
    temporal_history_valid_ = upscaled;
    if (!upscaled && entry.upscale.copy < run_copies_.size()) {
        execute_replay_copy(run_copies_[entry.upscale.copy]);
    }
    std::swap(alt_scene_, replay_output_scene_);
    swap_scene_with(replay_scene_);
    if (output_before) {
        swap_scene_sets();
    }
    replay_recording_ = recording;
}

bool RendererD3D12::take_eval_readback(std::vector<std::uint32_t>& words, float& t) {
    if (eval_readback_fence_ == 0u || completed_fence_value() < eval_readback_fence_ || !eval_readback_) {
        return false;
    }
    eval_readback_fence_ = 0u;
    void* mapped = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(eval_readback_bytes_)};
    if (FAILED(eval_readback_->Map(0, &range, &mapped))) {
        return false;
    }
    words.resize(static_cast<std::size_t>(eval_readback_bytes_ / 4u));
    std::memcpy(words.data(), mapped, words.size() * 4u);
    const D3D12_RANGE none{0, 0};
    eval_readback_->Unmap(0, &none);
    t = eval_readback_t_;
    return true;
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
    if (replay_recording_) {
        replay_ops_.push_back({ReplayKind::LayerBegin, 0u});
    }
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
    const unsigned quality = std::min(cfg.temporal_quality, 4u);
    // A failed creation is not retried until its sizes or mode change.
    if (rw == temporal_render_w_ && rh == temporal_render_h_ && ow == temporal_output_w_ &&
        oh == temporal_output_h_ && kind == temporal_kind_ && (kind != 3u || quality == temporal_quality_)) {
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
    temporal_interp_ = ring_mode_ == RingMode::Interp;
    if (temporal_interp_) {
        // Game frames feed the sub-frames: no jitter, and no upscale
        // (begin_output_scene). Each presented frame takes the next phase.
        scene_jitter_[0] = 0.0f;
        scene_jitter_[1] = 0.0f;
        scene_temporal_ = true;
        scene_motion_ = true;
        return true;
    }
    const unsigned phases = std::max(temporal_->jitter_phase_count(rw, ow), 1u);
    temporal_->jitter_offset(static_cast<unsigned>(temporal_frame_ % phases), phases,
                             jitter_x, jitter_y);
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
    settle_owed_scene();
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
    in.frame_ms = 1000.0f / 60.0f;
    in.reset = !temporal_history_valid_;
    in.view = scene_view_;
    in.view_known = scene_view_known_;
    const bool recorded = temporal_inputs_match(in) && temporal_->evaluate(in);
    if (recorded && frame_generation_running()) {
        streamline::FrameGenerationInputs fg{};
        fg.list = command_list_.Get();
        fg.depth = in.depth;
        fg.motion = in.motion;
        fg.render_width = in.render_width;
        fg.render_height = in.render_height;
        (void)streamline::tag_frame_generation(fg);
    }
    // The upscaler binds its own heaps, root signature and pipelines.
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(2, heaps);
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
    settle_owed_scene();
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
        retired_frame_pass_targets_[frame_slot_].push_back(std::move(alt_scene_.color));
        retired_frame_pass_targets_[frame_slot_].push_back(std::move(alt_scene_.depth));
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
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&alt_scene_.color)))) {
        return false;
    }
    rd.Format = kSceneDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kSceneDepthFormat;
    dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE,
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
        alt_scene_.conv_table = srv_ring_.allocate_persistent(2u);
    }
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kSceneColorFormat;
    device_->CreateShaderResourceView(
        alt_scene_.color.Get(), &view, alt_scene_.conv_table->cpu);
    auto depth_cpu = alt_scene_.conv_table->cpu;
    depth_cpu.ptr += stride;
    view.Format = kSceneDepthSrvFormat;
    device_->CreateShaderResourceView(alt_scene_.depth.Get(), &view, depth_cpu);
    alt_scene_.conv_color_srv = alt_scene_.conv_table->gpu;
    alt_scene_.conv_depth_srv = alt_scene_.conv_table->gpu;
    alt_scene_.conv_depth_srv.ptr += stride;
    alt_scene_.map = map;
    return true;
}

void RendererD3D12::write_scene_set_views(SceneTargetSet& set, const DescriptorRing::Table& table) {
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = set.hdr ? kSceneHdrColorFormat : kSceneColorFormat;
    device_->CreateShaderResourceView(set.color.Get(), &view, table.cpu);
    auto depth_cpu = table.cpu;
    depth_cpu.ptr += stride;
    view.Format = kSceneDepthSrvFormat;
    device_->CreateShaderResourceView(set.depth.Get(), &view, depth_cpu);
    set.conv_table = table;
    set.conv_color_srv = table.gpu;
    set.conv_depth_srv = table.gpu;
    set.conv_depth_srv.ptr += stride;
}

bool RendererD3D12::create_scene_set(
    SceneTargetSet& set, ComPtr<ID3D12DescriptorHeap>& rtv_heap,
    ComPtr<ID3D12DescriptorHeap>& dsv_heap, const FrameMap& map, bool persistent_views,
    bool unordered_access, unsigned samples, bool hdr, unsigned width, unsigned height) {
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
    rd.Format = hdr ? kSceneHdrColorFormat : kSceneColorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
               (unordered_access ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
    D3D12_CLEAR_VALUE cv{};
    cv.Format = rd.Format;
    cv.Color[3] = 1.0f;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&set.color)))) {
        return false;
    }
    set.hdr = hdr;
    rd.Format = kSceneDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kSceneDepthFormat;
    dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &dcv, IID_PPV_ARGS(&set.depth)))) {
        set.color.Reset();
        return false;
    }
    device_->CreateRenderTargetView(set.color.Get(), nullptr, set.rtv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kSceneDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(set.depth.Get(), &dsv, set.dsv);
    if (!create_scene_msaa(set.msaa, set.depth.Get(), samples, set.rtv, set.dsv)) {
        set.color.Reset();
        set.depth.Reset();
        return false;
    }
    if (persistent_views) {
        write_scene_set_views(set, set.conv_table ? *set.conv_table
                                                : srv_ring_.allocate_persistent(2u));
    } else {
        // The caller writes views into the frame's descriptor segment per use.
        set.conv_table.reset();
    }
    set.map = map;
    return true;
}

void RendererD3D12::swap_scene_with(SceneTargetSet& other) {
    settle_owed_scene();
    std::swap(scene_color_, other.color);
    std::swap(scene_depth_, other.depth);
    std::swap(scene_msaa_, other.msaa);
    std::swap(frame_, other.map);
    std::swap(scene_hdr_, other.hdr);
    std::swap(scene_rtv_, other.rtv);
    std::swap(scene_dsv_, other.dsv);
    std::swap(conv_scene_source_table_, other.conv_table);
    std::swap(conv_scene_color_srv_, other.conv_color_srv);
    std::swap(conv_scene_depth_srv_, other.conv_depth_srv);
    invalidate_draw_bindings();
    bound_scissor_input_map_ = FrameMap{};
}

void RendererD3D12::swap_scene_sets() {
    settle_owed_scene();
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::SwapSceneSets, 0u});
    }
    std::swap(scene_color_, alt_scene_.color);
    std::swap(scene_depth_, alt_scene_.depth);
    std::swap(scene_msaa_, alt_scene_.msaa);
    std::swap(frame_, alt_scene_.map);
    std::swap(scene_hdr_, alt_scene_.hdr);
    std::swap(scene_rtv_, alt_scene_.rtv);
    std::swap(scene_dsv_, alt_scene_.dsv);
    std::swap(conv_scene_source_table_, alt_scene_.conv_table);
    std::swap(conv_scene_color_srv_, alt_scene_.conv_color_srv);
    std::swap(conv_scene_depth_srv_, alt_scene_.conv_depth_srv);
    output_scene_active_ = !output_scene_active_;
    invalidate_draw_bindings();
    bound_scissor_input_map_ = FrameMap{};
    bind_scene();
}

// The scene target and the target passes' set trade places (begin_target_pass): targets, scale and copy sources, the output
// scene target's state kept.
void RendererD3D12::swap_target_set() {
    settle_owed_scene();
    std::swap(scene_color_, target_scene_.color);
    std::swap(scene_depth_, target_scene_.depth);
    std::swap(scene_msaa_, target_scene_.msaa);
    std::swap(frame_, target_scene_.map);
    std::swap(scene_hdr_, target_scene_.hdr);
    std::swap(scene_rtv_, target_scene_.rtv);
    std::swap(scene_dsv_, target_scene_.dsv);
    std::swap(conv_scene_source_table_, target_scene_.conv_table);
    std::swap(conv_scene_color_srv_, target_scene_.conv_color_srv);
    std::swap(conv_scene_depth_srv_, target_scene_.conv_depth_srv);
    target_pass_active_ = !target_pass_active_;
    invalidate_draw_bindings();
    bound_scissor_input_map_ = FrameMap{};
    bind_scene();
}

bool RendererD3D12::begin_target_pass(std::uint64_t key, std::uint32_t width, std::uint32_t height) {
    if (target_pass_active_ || width == 0u || height == 0u) {
        return false;
    }
    TargetSlot& slot = target_slots_[key];
    const bool kept = slot.set.color && slot.set.map == frame_ && slot.width == width && slot.height == height;
    // A new set starts as a render target; the pass's first clear (RenderCore) fills it. Its copy views are written
    // per use (no persistent descriptors per key).
    if (!kept) {
        if (!create_scene_set(slot.set, slot.rtv_heap, slot.dsv_heap, frame_, false, false, 1u, false, width,
                            height)) {
            target_slots_.erase(key);
            return false;
        }
        slot.width = width;
        slot.height = height;
    } else {
        // Sampled since its last pass: drawn into again.
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = slot.set.color.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list_->ResourceBarrier(1, &barrier);
    }
    slot.used = frame_index_;
    target_key_ = key;
    std::swap(target_scene_, slot.set);
    swap_target_set();
    // The pass is the game frame's alone: sub-frames sample its picture, never draw it again.
    target_recording_ = replay_recording_;
    replay_recording_ = false;
    return true;
}

void RendererD3D12::end_target_pass(std::uint8_t channels, TextureCache& textures) {
    if (!target_pass_active_) {
        return;
    }
    swap_target_set();
    if (frame_pass_color_key_ != 0u || frame_pass_depth_key_ != 0u) {
        close_frame_for_pass(textures);
    }
    replay_recording_ = target_recording_;
    // The picture is sampled until the key's next pass.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target_scene_.color.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &barrier);
    TargetSlot& slot = target_slots_[target_key_];
    std::swap(slot.set, target_scene_);
    (void)publish_picture(target_key_, slot.width, slot.height, slot.set.color, channels, 0u, textures);
    // Keys unused for kTargetIdleFrames frames give back their render resources (their pictures stay).
    for (auto it = target_slots_.begin(); it != target_slots_.end();) {
        it = it->second.used + kTargetIdleFrames < frame_index_ ? target_slots_.erase(it) : std::next(it);
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
    out.from_capture = true;
    out.source_bytes = it->second.bytes != 0u ? it->second.bytes : static_cast<std::uint32_t>(it->second.width) * it->second.height * 4u;
    return true;
}

bool RendererD3D12::publish_picture(std::uint64_t key, std::uint32_t width, std::uint32_t height,
                                    gpu::TextureRef texture, std::uint8_t channels, std::uint32_t bytes,
                                    TextureCache& textures) {
    bool changed = false;
    if (bytes != 0u) {
        // A capture over memory another capture's picture names replaces it (the game reuses capture buffers).
        for (auto it = pictures_.begin(); it != pictures_.end();) {
            Picture& other = it->second;
            if (it->first != key && other.bytes != 0u && it->first < key + bytes && key < it->first + other.bytes) {
                textures.retire_view(other.view, std::move(other.viewed));
                                it = pictures_.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    Picture& picture = pictures_[key];
    if (picture.view != ~0u && picture.viewed == texture && picture.channels == channels && picture.width == width &&
        picture.height == height) {
        changed = changed || picture.bytes != bytes;
        picture.bytes = bytes;
        return changed;
    }
    if (picture.view != ~0u) {
        textures.retire_view(picture.view, std::move(picture.viewed));
    }
    picture.view = textures.allocate_view();
    picture.viewed = std::move(texture);
    picture.width = width;
    picture.height = height;
    picture.channels = channels;
    picture.bytes = bytes;
    const std::array<std::uint8_t, 4> swizzle = target_channel_swizzle(channels);
    // D3D12's component mapping: 0-3 the texture's R, G, B, A, 5 one.
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = picture.viewed.Get() == scene_depth_.Get() ? kSceneDepthSrvFormat : picture.viewed->GetDesc().Format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
        swizzle[0] == 4u ? 5u : swizzle[0], swizzle[1] == 4u ? 5u : swizzle[1], swizzle[2] == 4u ? 5u : swizzle[2],
        swizzle[3] == 4u ? 5u : swizzle[3]);
    desc.Texture2D.MipLevels = 1u;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = textures.backend().srv_cpu_base();
    cpu.ptr += static_cast<SIZE_T>(picture.view) * textures.backend().srv_descriptor_stride();
    device_->CreateShaderResourceView(picture.viewed.Get(), &desc, cpu);
    return true;
}

bool RendererD3D12::publish_frame_pass(const FramePassRequest& params, gpu::TextureRef texture, TextureCache& textures) {
    const FrameExtent size = frame_pass_extent(frame_);
    return publish_picture(params.key, size.width, size.height, std::move(texture), 0u, 0u, textures);
}

// DLSS Frame Generation's HUD-less picture (fg_hudless_*): the output scene target right after the
// upscale, before the 2D layer draws into it.
void RendererD3D12::take_fg_hudless_scene() {
    settle_owed_scene();
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
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    barriers[0].Transition.pResource = scene_color_.Get();
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.pResource = fg_hudless_scene_target_.color.Get();
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    command_list_->ResourceBarrier(2u, barriers);
    command_list_->CopyResource(fg_hudless_scene_target_.color.Get(), scene_color_.Get());
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    command_list_->ResourceBarrier(2u, barriers);
    fg_hudless_scene_ = true;
}

// The display copy of the taken scene: the same copy into the HUD-less display picture.
void RendererD3D12::copy_fg_hudless_display(const FramePassRequest& /*params*/, const DisplayTexture& dest) {
    const bool taken = fg_hudless_scene_;
    fg_hudless_scene_ = false;
    if (!taken || replay_recording_ || fg_hudless_scene_target_.map != frame_ || !dest.texture) {
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
        if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                                    IID_PPV_ARGS(&fg_hudless_display_.texture)))) {
            return;
        }
        fg_hudless_display_.texture->SetName(L"DLSS-G HUD-less display picture");
    }
    fg_hudless_display_.width = dest.width;
    fg_hudless_display_.height = dest.height;
    fg_hudless_display_.rtv = allocate_conversion_rtv(fg_hudless_display_.texture.Get());
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = fg_hudless_display_.texture.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);
    const FramePassGeometry sampling =
        frame_pass_geometry(dest.width, dest.height, frame_);
    swap_scene_with(fg_hudless_scene_target_);
    record_conversion_blit(fg_hudless_display_.texture.Get(), dest.width, dest.height, fg_hudless_display_.rtv, kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    swap_scene_with(fg_hudless_scene_target_);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);
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
        if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
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
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(2, heaps);
    if (post_process_.ready()) {
        const D3D12_RESOURCE_DESC desc = fg_hudless_display_.texture->GetDesc();
        post_process_.record(command_list_.Get(), srv_ring_, fg_hudless_display_.texture.Get(),
                             static_cast<unsigned>(desc.Width), desc.Height, rtv, viewport, frame_index_,
                             kFramesInFlight);
    } else {
        const DescriptorRing::Table tbl = srv_ring_.allocate(1);
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(fg_hudless_display_.texture.Get(), &sd, tbl.cpu);
        command_list_->SetGraphicsRootSignature(s_blit_root_sig.Get());
        command_list_->SetPipelineState(blit_pipeline_.Get());
        command_list_->SetGraphicsRootDescriptorTable(0, tbl.gpu);
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, &scissor);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->DrawInstanced(3, 1, 0, 0);
    }
    invalidate_draw_bindings();
    streamline::tag_hudless(command_list_.Get(), fg_hudless_target_.Get(), host_window::backbuffer_width, host_window::backbuffer_height);
    // Back to the backbuffer for the overlays.
    const UINT buf_idx = swap_chain_->GetCurrentBackBufferIndex();
    D3D12_CPU_DESCRIPTOR_HANDLE back_rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    back_rtv.ptr += static_cast<SIZE_T>(buf_idx) * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    command_list_->OMSetRenderTargets(1, &back_rtv, FALSE, nullptr);
}

void RendererD3D12::record_scene_aa() {
    settle_owed_scene();
    const GpuPassScope pass{*this, GpuPass::SceneAa};
    if (!post_process_.ready() || output_scene_active_ || scene_temporal_ || !scene_color_ ||
        post_process_.settings().aa == PostProcessSettings::Aa::kOff) {
        return;
    }
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::SceneAa, 0u});
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
        if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
                                                    IID_PPV_ARGS(&set.color)))) {
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device_->CreateRenderTargetView(set.color.Get(), nullptr, rtv);
        set.rtv = rtv;
        if (samples > 1u) {
            rd.SampleDesc.Count = samples;
            if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
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
        write_scene_set_views(set, set.conv_table ? *set.conv_table : srv_ring_.allocate_persistent(2u));
    }
    return true;
}

void RendererD3D12::begin_wide_scene(bool wanted) {
    if (!wanted || scene_hdr_ || !camera_pass_.ready() || !scene_color_ || output_scene_active_ || target_pass_active_) {
        return;
    }
    settle_owed_scene();
    SceneTargetSet& set = hdr_scene_scene_;
    if (!ensure_wide_set(set, hdr_scene_rtv_heap_)) {
        return;
    }
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::HdrBegin, 0u});
    }
    record_hdr_begin();
}

void RendererD3D12::record_hdr_begin() {
    const GpuPassScope pass{*this, GpuPass::Camera};
    SceneTargetSet& set = replay_playing_ ? replay_hdr_scene_ : hdr_scene_scene_;
    if (replay_playing_ && !ensure_wide_set(set, replay_hdr_rtv_heap_)) {
        return;
    }
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
    settle_owed_scene();
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::HdrEnd, 0u});
    }
    record_hdr_end();
}

void RendererD3D12::record_hdr_end() {
    if (!scene_hdr_) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Camera};
    resolve_scene(false, true);
    SceneTargetSet& set = replay_playing_ ? replay_hdr_scene_ : hdr_scene_scene_;
    swap_scene_with(set);
    scene_msaa_.depth_dirty = set.msaa.depth_dirty;
    const auto now = std::chrono::steady_clock::now();
    const float seconds = std::chrono::duration<float>(now - camera_metered_).count();
    camera_metered_ = now;
    const D3D12_RESOURCE_DESC desc = set.color->GetDesc();
    SceneCamera::Inputs in{};
    in.wide = set.color.Get();
    in.wide_ms = set.msaa.samples > 1u ? set.msaa.color.Get() : nullptr;
    in.rtv = scene_rtv_;
    in.width = static_cast<unsigned>(desc.Width);
    in.height = desc.Height;
    in.samples = set.msaa.samples;
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

// The scene target's depth, single-sample (MSAA resolves it, sample 0), read by the occlusion; the product lands on the
// scene target's own colour target, every sample of a multisampled one.
void RendererD3D12::record_scene_ao() {
    settle_owed_scene();
    const GpuPassScope pass{*this, GpuPass::SceneAo};
    if (!ambient_occlusion_.ready() || !scene_depth_ || !scene_color_) {
        return;
    }
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::SceneAo, 0u});
    }
    resolve_scene(true, false);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = scene_depth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    const D3D12_RESOURCE_DESC desc = scene_depth_->GetDesc();
    AmbientOcclusion::Inputs in{};
    in.depth = scene_depth_.Get();
    in.width = static_cast<unsigned>(desc.Width);
    in.height = desc.Height;
    in.color_rtv = scene_rtv_;
    in.samples = scene_msaa_.samples;
    in.hdr = scene_hdr_;
    in.view = scene_ao_view_;
    static_cast<void>(ambient_occlusion_.record(command_list_.Get(), srv_ring_, in, frame_index_, kFramesInFlight));
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    if (scene_msaa_.samples > 1u) {
        scene_msaa_.color_dirty = true;
    }
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bound_rt_aux_ = 0xFFu;
}

void RendererD3D12::game_bloom(std::uint16_t phase, std::uint32_t a, std::uint32_t b) {
    if (!game_bloom_.ready()) {
        return;
    }
    const ReplayBloom bloom{phase, a, b};
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::Bloom, static_cast<std::uint32_t>(replay_blooms_.size())});
        replay_blooms_.push_back(bloom);
    }
    record_game_bloom(bloom);
}

// The game's bloom on the scene target's single-sample colour (MSAA resolves it first); the composite lands on every sample of
// a multisampled scene target.
void RendererD3D12::record_game_bloom(const ReplayBloom& bloom) {
    settle_owed_scene();
    const GpuPassScope pass{*this, GpuPass::Bloom};
    if (!scene_color_) {
        return;
    }
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
    if (bloom.phase == motion::kBloomHold) {
        if (game_bloom_.ready() && scene_msaa_.samples <= 1u && !scene_hdr_ && !replay_recording_ && !replay_playing_ &&
            ring_mode_ != RingMode::Interp) {
            // The scene and the bloom's image trade places instead of the scene being copied. The game draws the
            // bloom's objects on black into the new target, over the same depth.
            gpu::TextureRef picture =
                hand_over_scene_color(game_bloom_.take_kept_image(command_list_.Get(), in, frame_index_, kFramesInFlight));
            scene_owed_.Reset();
            game_bloom_.keep_picture(std::move(picture), in);
            clear_scene(0u, true, true, false);
            return;
        }
        static_cast<void>(game_bloom_.keep(command_list_.Get(), in, frame_index_, kFramesInFlight));
        return;
    }
    {
        GameBloom::Params p{};
        p.threshold = bloom.a & 0xFFu;
        p.intensity = bloom.a >> 24u;
        if (bloom.phase == motion::kBloomNormal) {
            p.kind = GameBloom::Kind::Normal;
            p.tap1 = (bloom.a >> 8u) & 0xFFu;
            p.tap2 = (bloom.a >> 16u) & 0xFFu;
            p.ring_x = std::bit_cast<float>(bloom.b);
        } else {
            p.kind = GameBloom::Kind::Simple;
            p.channel = (bloom.a >> 8u) & 0xFFu;
            p.tap1 = (bloom.a >> 16u) & 0xFFu;
            p.ring_y = std::bit_cast<float>(bloom.b);
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

bool RendererD3D12::begin_output_scene() {
    const bool temporal = scene_temporal_;
    scene_temporal_ = false;
    // Nebula's screens with small text are drawn at the window's resolution whatever the HUD's (sharp_2d): at the
    // internal scale their letters would lose strokes on a wide or large window. Otherwise the game's layouts keep at
    // least the design's density on a window wider than 16:9 or narrower than 4:3, never above the window's.
    const bool full = temporal || get_render_config().hud_output_resolution || sharp_2d_requested();
    if (output_scene_active_ || !full) {
        return false;
    }
    // A temporal upscaler outputs the internal resolution; the 2D layer alone
    // goes to the window's.
    const FrameMap output = temporal ? FrameMap(temporal_output_w_, temporal_output_h_) : output_frame_for_window();
    if (output.empty() || output.width < frame_.width || (output == frame_ && !temporal) ||
        !ensure_output_scene(output)) {
        temporal_history_valid_ = false;
        return false;
    }
    scene_temporal_ = temporal;
    const bool upscaled = temporal && !temporal_interp_ && record_temporal_upscale(output);
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
    if (replay_recording_) [[unlikely]] {
        ReplayCopy copy{};
        copy.width = w;
        copy.height = h;
        copy.params = params;
        copy.format = kCopyLayoutDisplay;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        copy.mode = 2u;
        replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
        replay_copies_.push_back(copy);
        const bool recording = replay_recording_;
        replay_recording_ = false;  // the blit's own scene target rebinding is part of the copy
        record_conversion_blit(alt_scene_.color.Get(), w, h, {}, kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
        replay_recording_ = recording;
        swap_scene_sets();
        replay_ops_.push_back({ReplayKind::ClearDepth, 0u});
    } else {
        record_conversion_blit(alt_scene_.color.Get(), w, h, {}, kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
        swap_scene_sets();
    }
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

bool RendererD3D12::draw_frame_pass_from_output(const FramePassRequest& params, TextureCache& textures) {
    if (output_scene_active_ || !alt_scene_.color) {
        return draw_frame_pass(params, textures);
    }
    swap_scene_sets();
    const bool updated = draw_frame_pass(params, textures);
    swap_scene_sets();
    return updated;
}

void RendererD3D12::bind_scene() {
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::BindScene, 0u});
    }
    command_list_->OMSetRenderTargets(1, &scene_rtv_, FALSE, &scene_dsv_);
    bound_rt_aux_ = 0u;

    const float vp_x = 0.0f, vp_y = 0.0f;
    const float vp_w = static_cast<float>(frame_.width);
    const float vp_h = static_cast<float>(frame_.height);

    D3D12_VIEWPORT vp{vp_x, vp_y, vp_w, vp_h, 0.0f, 1.0f};
    D3D12_RECT sci{0, 0, static_cast<LONG>(frame_.width), static_cast<LONG>(frame_.height)};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sci);
    bound_viewport_ = vp;
    bound_scissor_ = sci;
    // Full-scene target binding can overwrite a previously applied game scissor even
    // though its raw draw input is unchanged on the next draw.
    bound_scissor_input_valid_ = false;
    bound_viewport_valid_ = true;
    bound_scissor_valid_ = true;
}

void RendererD3D12::clear_scene(
    std::uint32_t clear_rgba,
    bool color_enable, bool alpha_enable, bool depth_enable,
    unsigned rect_x, unsigned rect_y, unsigned rect_w, unsigned rect_h,
    std::uint8_t pixel_format) {
    if (scene_msaa_.samples > 1u) {
        scene_msaa_.color_dirty = scene_msaa_.depth_dirty = true;
    }
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::Clear, static_cast<std::uint32_t>(replay_clears_.size())});
        replay_clears_.push_back({clear_rgba, color_enable, alpha_enable,
                                  depth_enable, rect_x, rect_y, rect_w, rect_h, pixel_format});
    } else if (ring_mode_ == RingMode::Interp && ring_advance_pending_ && !replay_playing_) [[unlikely]] {
        log_start_clear_ = {clear_rgba, color_enable, alpha_enable,
                            depth_enable, rect_x, rect_y, rect_w, rect_h, pixel_format};
        log_start_clear_valid_ = true;
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

    if (scene_owed_ && pass_list_open_) [[unlikely]] {
        const bool rgba = color_enable && (alpha_enable || pixel_format != 1u);
        restore_owed_scene(rgba ? &rect : nullptr);
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
            dsv, D3D12_CLEAR_FLAG_DEPTH, clear.reversed_depth, 0, num_rects, rects);
    }
}

void RendererD3D12::draw(const DrawCall& call) {
    settle_owed_scene();
    if (scene_msaa_.samples > 1u) {
        scene_msaa_.color_dirty = scene_msaa_.depth_dirty = true;
    }
    if (replay_recording_) [[unlikely]] {
        if (call.texture_table.ptr != 0u && !replay_textures_.by_gpu.empty()) {
            take_replay_snapshots(call.texture_table.ptr);
        }
        replay_ops_.push_back({ReplayKind::Draw, static_cast<std::uint32_t>(replay_draws_.size())});
        replay_draws_.push_back(call);
    } else if (ring_mode_ == RingMode::Interp && ring_advance_pending_ && !replay_playing_) [[unlikely]] {
        ++log_gap_ops_;
    }
    const bool split = call.flat_pipeline != nullptr;
    gpu::Pipeline* const first_pipeline = split ? call.flat_pipeline : call.pipeline;
    if (first_pipeline != bound_pipeline_) {
        command_list_->SetPipelineState(first_pipeline);
        bound_pipeline_ = first_pipeline;
    }
    if (call.vs_constants &&
        call.vs_constants != bound_vs_constants_) {
        command_list_->SetGraphicsRootConstantBufferView(0, call.vs_constants);
        bound_vs_constants_ = call.vs_constants;
    }
    if (call.ps_constants &&
        call.ps_constants != bound_ps_constants_) {
        command_list_->SetGraphicsRootConstantBufferView(1, call.ps_constants);
        bound_ps_constants_ = call.ps_constants;
    }
    if (call.texture_table.ptr &&
        call.texture_table.ptr != bound_texture_table_.ptr) {
        command_list_->SetGraphicsRootDescriptorTable(2, call.texture_table);
        bound_texture_table_ = call.texture_table;
    }
    if (call.sampler_table.ptr &&
        call.sampler_table.ptr != bound_sampler_table_.ptr) {
        command_list_->SetGraphicsRootDescriptorTable(3, call.sampler_table);
        bound_sampler_table_ = call.sampler_table;
    }
    if (call.matrix_palette &&
        call.matrix_palette != bound_matrix_palette_) {
        command_list_->SetGraphicsRootShaderResourceView(4, call.matrix_palette);
        bound_matrix_palette_ = call.matrix_palette;
    }
    if (call.matrix_rows && call.matrix_rows != bound_matrix_rows_) {
        command_list_->SetGraphicsRootShaderResourceView(8, call.matrix_rows);
        bound_matrix_rows_ = call.matrix_rows;
    }
    if (!bound_vertex_blend_valid_ || call.blend_vertices != bound_blend_vertices_ ||
        call.blend_slot != bound_blend_slot_) {
        // Unblended draws bind valid addresses the shaders never read.
        command_list_->SetGraphicsRootShaderResourceView(
            5, call.blend_vertices != 0u ? call.blend_vertices : vertex_ring_.gpu_base());
        const std::uint32_t vertices = call.vertex_view.StrideInBytes != 0u
            ? call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes : 0u;
        const std::uint32_t blend[5]{call.blend_vertices != 0u ? 1u : 0u,
                                     replay_playing_ ? kFrameValuesElement : kZeroValuesElement,
                                     std::min(call.blend_slot, kMaxBlendPairs - 1u),
                                     call.blend_vertices != 0u && vertices != 0u ? vertices - 1u : 0u, 0u};
        command_list_->SetGraphicsRoot32BitConstants(6, 5u, blend, 0u);
        command_list_->SetGraphicsRootShaderResourceView(7, blend_verdicts_->GetGPUVirtualAddress());
        bound_blend_vertices_ = call.blend_vertices;
        bound_blend_slot_ = call.blend_slot;
        bound_vertex_blend_valid_ = true;
    }
    if (call.aux_target != bound_rt_aux_) [[unlikely]] {
        if (call.aux_target == 3u) {
            const D3D12_CPU_DESCRIPTOR_HANDLE targets[4]{scene_rtv_, reactive_rtv_, layer_color_rtv_, layer_trans_rtv_};
            command_list_->OMSetRenderTargets(4, targets, FALSE, &scene_dsv_);
        } else if (call.aux_target != 0u) {
            const D3D12_CPU_DESCRIPTOR_HANDLE targets[2]{
                scene_rtv_, call.aux_target == 1u ? motion_rtv_ : reactive_rtv_};
            command_list_->OMSetRenderTargets(2, targets, FALSE, &scene_dsv_);
        } else {
            command_list_->OMSetRenderTargets(1, &scene_rtv_, FALSE, &scene_dsv_);
        }
        bound_rt_aux_ = call.aux_target;
    }

    // Per-draw game scissor, scaled to the scene target and clamped. An empty scissor
    // rasterizes nothing (also how CullMode::All is expressed). The raw input
    // is cached only after a nonempty one has been applied.
    if (!bound_scissor_valid_ || !bound_scissor_input_valid_ ||
        bound_scissor_input_map_ != frame_ ||
        call.scissor.left != bound_scissor_input_.left ||
        call.scissor.top != bound_scissor_input_.top ||
        call.scissor.right != bound_scissor_input_.right ||
        call.scissor.bottom != bound_scissor_input_.bottom) {
        const ScaledSceneRect scaled = rect_pixels(
            call.scissor.left,
            call.scissor.top,
            call.scissor.right,
            call.scissor.bottom,
            frame_);
        if (scaled.empty()) {
            return;  // nothing can rasterize
        }
        const D3D12_RECT sci{
            scaled.left, scaled.top, scaled.right, scaled.bottom};
        if (!bound_scissor_valid_ ||
            sci.left != bound_scissor_.left ||
            sci.top != bound_scissor_.top ||
            sci.right != bound_scissor_.right ||
            sci.bottom != bound_scissor_.bottom) {
            command_list_->RSSetScissorRects(1, &sci);
            bound_scissor_ = sci;
            bound_scissor_valid_ = true;
        }
        bound_scissor_input_ = call.scissor;
        bound_scissor_input_map_ = frame_;
        bound_scissor_input_valid_ = true;
    }

    // Per-draw viewport with its depth range: the game clears depth by drawing fullscreen quads with
    // the state near == far == 1.0, so such a quad writes z = FAR whatever its geometry z.
    // Reversed: MinDepth = 1 - far, MaxDepth = 1 - near.
    // Composes with the negated projection z (fill_projection): game near maps to the depth MaxDepth
    // side, game far to the MinDepth side; depth funcs are flipped in pipeline_cache to match.
    if (call.has_viewport) {
        const ScaledViewport scaled =
            viewport_pixels(call.viewport, frame_);
        D3D12_VIEWPORT vp{
            scaled.x,
            scaled.y,
            scaled.width,
            scaled.height,
            scaled.min_depth,
            scaled.max_depth};
        if (scaled.valid()) {
            if (!bound_viewport_valid_ ||
                vp.TopLeftX != bound_viewport_.TopLeftX ||
                vp.TopLeftY != bound_viewport_.TopLeftY ||
                vp.Width != bound_viewport_.Width ||
                vp.Height != bound_viewport_.Height ||
                vp.MinDepth != bound_viewport_.MinDepth ||
                vp.MaxDepth != bound_viewport_.MaxDepth) {
                command_list_->RSSetViewports(1, &vp);
                bound_viewport_ = vp;
                bound_viewport_valid_ = true;
            }
        }
    }
    const gpu::Topology first_topology = split ? gpu::kTopologyTriangleList : call.topology;
    if (first_topology != bound_topology_) {
        command_list_->IASetPrimitiveTopology(first_topology);
        bound_topology_ = first_topology;
    }
    // A run of shapes rebinds the same vertex-ring allocation for
    // thousands of consecutive draws, so the view is compared before binding.
    // Every other writer of stream 0 (the UI helpers, the conversion blit, the
    // clear quad) is followed by invalidate_draw_bindings(), and begin_frame()
    // re-arms the cached view after rewinding the vertex ring.
    if (!bound_vertex_view_valid_ ||
        call.vertex_view.BufferLocation != bound_vertex_view_.BufferLocation ||
        call.vertex_view.SizeInBytes != bound_vertex_view_.SizeInBytes ||
        call.vertex_view.StrideInBytes != bound_vertex_view_.StrideInBytes) {
        command_list_->IASetVertexBuffers(0, 1, &call.vertex_view);
        bound_vertex_view_ = call.vertex_view;
        bound_vertex_view_valid_ = true;
    }
    const bool time_draw = draw_query_heap_ && pass_list_open_ && call.index_count > 0u &&
                           draw_marks_[frame_slot_].size() < kGpuDrawSlots / 2u;
    if (time_draw) {
        command_list_->EndQuery(draw_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                frame_slot_ * kGpuDrawSlots + 2u * static_cast<UINT>(draw_marks_[frame_slot_].size()));
    }
    if (call.index_count > 0) {
        if (call.index_view.BufferLocation) {
            // Compared before binding, as the vertex view above.
            if (!bound_index_view_valid_ ||
                call.index_view.BufferLocation !=
                    bound_index_view_.BufferLocation ||
                call.index_view.SizeInBytes != bound_index_view_.SizeInBytes ||
                call.index_view.Format != bound_index_view_.Format) {
                command_list_->IASetIndexBuffer(&call.index_view);
                bound_index_view_ = call.index_view;
                bound_index_view_valid_ = true;
            }
            if (split && call.curve_sort >= 0 && call.curve_sort_list == curve_sort_list_ && !replay_playing_ &&
                static_cast<std::size_t>(call.curve_sort) < curve_sort_draws_[frame_slot_].size()) {
                // The flat triangles, then the sort's lists: the whole triangles, then the patches, each drawn by
                // its arguments.
                if (call.flat_index_count != 0u) {
                    command_list_->DrawIndexedInstanced(call.flat_index_count, 1, 0, 0, 0);
                }
                const CurveSortDraw& sorted = curve_sort_draws_[frame_slot_][static_cast<std::size_t>(call.curve_sort)];
                ID3D12Resource* const buffer = curve_sorted_[frame_slot_].Get();
                const D3D12_GPU_VIRTUAL_ADDRESS lists = buffer->GetGPUVirtualAddress() + sorted.output + 40u;
                const UINT list_bytes = sorted.triangles * 12u;
                D3D12_INDEX_BUFFER_VIEW view{lists, list_bytes, DXGI_FORMAT_R32_UINT};
                command_list_->IASetIndexBuffer(&view);
                command_list_->ExecuteIndirect(curve_draw_signature_.Get(), 1u, buffer, sorted.output, nullptr, 0u);
                command_list_->SetPipelineState(call.pipeline);
                command_list_->IASetPrimitiveTopology(call.topology);
                view.BufferLocation = lists + list_bytes;
                command_list_->IASetIndexBuffer(&view);
                command_list_->ExecuteIndirect(curve_draw_signature_.Get(), 1u, buffer, sorted.output + 20u, nullptr,
                                               0u);
                bound_pipeline_ = call.pipeline;
                bound_topology_ = call.topology;
                bound_index_view_valid_ = false;
            } else if (split) {
                if (call.flat_index_count != 0u) {
                    command_list_->DrawIndexedInstanced(call.flat_index_count, 1, 0, 0, 0);
                }
                if (call.index_count > call.flat_index_count) {
                    command_list_->SetPipelineState(call.pipeline);
                    command_list_->IASetPrimitiveTopology(call.topology);
                    bound_pipeline_ = call.pipeline;
                    bound_topology_ = call.topology;
                    command_list_->DrawIndexedInstanced(call.index_count - call.flat_index_count, 1,
                                                        call.flat_index_count, 0, 0);
                }
            } else {
                command_list_->DrawIndexedInstanced(call.index_count, 1, 0, 0, 0);
            }
        } else {
            command_list_->DrawInstanced(call.index_count, 1, 0, 0);
        }
    }
    if (time_draw) {
        std::vector<GpuDrawMark>& marks = draw_marks_[frame_slot_];
        command_list_->EndQuery(draw_query_heap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                frame_slot_ * kGpuDrawSlots + 2u * static_cast<UINT>(marks.size()) + 1u);
        marks.push_back({call.pipeline, call.index_count / 3u});
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
    settle_owed_scene();
    resolve_scene(false);
    ID3D12Resource* src = scene_color_.Get();

    // scene target -> shader resource.
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = src;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    const D3D12_GPU_DESCRIPTOR_HANDLE src_srv = conv_scene_color_srv_;

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
        make_scene_conversion_constants(false, copy_format, src_x, src_y, step_x, step_y);
    command_list_->SetGraphicsRoot32BitConstants(
        0, 16, constants.values.data(), 0);
    command_list_->SetGraphicsRootDescriptorTable(1, src_srv);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);

    // scene target back to its steady state.
    ++frame_passes_drawn_;
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);

    // Restore the game drawing state (root signature changed; scene target RTV/DSV and full
    // viewport/scissor re-bound; per-draw viewport/scissor follow).
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bind_scene();
}

void RendererD3D12::copy_scene_to_display(const FramePassRequest& params, DisplayTexture& dest) {
    settle_owed_scene();
    const GpuPassScope pass{*this, GpuPass::DisplayCopy};
    // The caller publishes a new display picture serial after this returns, so a missing
    // target throws rather than pass as a new frame.
    if (!dest.texture) {
        throw std::runtime_error("display picture copy has no destination texture");
    }
    ++frame_pass_requests_;
    // Another display picture or texture copy can overwrite the shared CPU RTV slot.
    // Check its current resource, even when this destination has copied before.
    dest.rtv = allocate_conversion_rtv(dest.texture.Get());

    // Dest display picture texture -> render target for the stretch blit. The y-scale height difference between
    // the src rect and the dest texture is the stretch; CopyTextureRegion cannot express it.
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = dest.texture.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    const FramePassGeometry sampling =
        frame_pass_geometry(dest.width, dest.height, frame_);
    const bool recording = replay_recording_;
    if (recording) [[unlikely]] {
        ReplayCopy copy{};
        copy.width = dest.width;
        copy.height = dest.height;
        copy.params = params;
        copy.format = kCopyLayoutDisplay;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        copy.mode = 1u;
        replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
        replay_copies_.push_back(copy);
        replay_recording_ = false;
    }

    record_conversion_blit(dest.texture.Get(), dest.width, dest.height, dest.rtv, kCopyLayoutDisplay, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    replay_recording_ = recording;

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);
    copy_fg_hudless_display(params, dest);

    if (ring_mode_ == RingMode::Interp) {
        // The display copy closes the game frame: its log is complete and the
        // next frame starts a new ring segment.
        if (recording) {
            replay_recording_ = false;
            // One 3D scene, or none in a frame recorded as its screen layer alone (the pause menu). An output-scene target
            // switch (HUD at output resolution) replays into the sub-frame's private output scene target.
            replay_valid_ = replay_scene_count_ == 1u || (replay_scene_count_ == 0u && replay_screen_frame_);
            replay_reject_ = replay_valid_                ? 0u
                : replay_scene_count_ == 0u               ? 1u
                                                          : 2u;
            if (replay_valid_) {
                publish_replay_log();
            }
            replay_segment_ = ring_segment_;
            replay_picture_slot_ = dest.slot;
            ++replay_serial_;
            if (replay_valid_) {
                play_serial_ = replay_serial_;
            }
        }
        ring_advance_pending_ = true;
    }
}

bool RendererD3D12::draw_frame_pass(
    const FramePassRequest& params, TextureCache& textures) {
    settle_owed_scene();
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
        (static_cast<std::uint64_t>(params.channels) << 48) |  // addresses stay below 2^48
        // Interpolation: consecutive game frames copy into different
        // textures, so a sub-frame replaying one frame's copies never
        // overwrites what the next frame's chunks already copied.
        (ring_mode_ == RingMode::Interp ? (replay_serial_ & 1ull) << 58 : 0ull);

    FramePassTarget& dest = frame_pass_targets_[dest_key];
    const bool created =
        !dest.texture || dest.width != w || dest.height != h;
    if (created) {
        if (dest.texture) {
            retired_frame_pass_targets_[frame_slot_].push_back(
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
            rd, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, dest.texture,
            allocation_bytes);
        if (SUCCEEDED(create_hr)) {
            // A placed render target starts with undefined contents and must
            // be cleared, discarded or copied before any other use. The code
            // below expects the shader-resource state.
            command_list_->DiscardResource(dest.texture.Get(), nullptr);
            D3D12_RESOURCE_BARRIER to_srv{};
            to_srv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_srv.Transition.pResource = dest.texture.Get();
            to_srv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            to_srv.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            to_srv.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            command_list_->ResourceBarrier(1, &to_srv);
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

    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = dest.texture.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    const FramePassGeometry sampling =
        frame_pass_geometry(w, h, frame_);


    const bool recording = replay_recording_;
    if (recording) [[unlikely]] {
        ReplayCopy copy{};
        copy.dest = dest.texture.Get();
        copy.width = w;
        copy.height = h;
        copy.params = params;
        copy.format = params.channels;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
        replay_copies_.push_back(copy);
        replay_recording_ = false;
    }
    record_conversion_blit(dest.texture.Get(), w, h, dest.rtv, params.channels, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y);
    replay_recording_ = recording;
    if (ring_mode_ == RingMode::Interp) note_copy_write(dest.texture.Get());

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);

    // The key's picture is the latest copy: format-keyed destinations alternate at one address.
    return publish_frame_pass(params, dest.texture, textures);
}

void RendererD3D12::create_first_use_resources() {
    (void)ensure_screen_probe_resources();
}

bool RendererD3D12::ensure_screen_probe_resources() {
    if (probe_target_) {
        return true;
    }
    D3D12_HEAP_PROPERTIES default_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC target_desc{};
    target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    target_desc.Width = kScreenProbesMax;
    target_desc.Height = 1;
    target_desc.DepthOrArraySize = 1;
    target_desc.MipLevels = 1;
    target_desc.Format = kSceneColorFormat;
    target_desc.SampleDesc.Count = 1;
    target_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    d3d_check(
        device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE, &target_desc,
                                         D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&probe_target_)),
        "screen probe target");
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readback_desc{};
    readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = static_cast<UINT64>(kScreenProbesMax) * 4u;
    readback_desc.Height = 1;
    readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1;
    readback_desc.SampleDesc.Count = 1;
    readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (ScreenProbeSlot& slot : probe_slots_) {
        d3d_check(
            device_->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &readback_desc,
                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&slot.readback)),
            "screen probe readback");
        slot.rgba = pin_readback_mapping(slot.readback.Get());
        if (slot.rgba == nullptr) {
            throw std::runtime_error("screen probe readback could not be mapped");
        }
    }
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.NumDescriptors = 1;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    d3d_check(device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&probe_rtv_heap_)), "screen probe RTV heap");
    probe_rtv_ = probe_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(probe_target_.Get(), nullptr, probe_rtv_);
    return true;
}

bool RendererD3D12::record_screen_probes(std::size_t slot, ScreenProbeKind kind, std::span<const ScreenProbe> probes) {
    ScreenProbeSlot& out = probe_slots_[slot];
    if (out.fence != 0u || probes.size() > kScreenProbesMax || !ensure_screen_probe_resources()) {
        return false;
    }
    ID3D12GraphicsCommandList* const list = command_list_.Get();
    settle_owed_scene();
    const bool is_depth = kind == ScreenProbeKind::Depth;
    resolve_scene_on(list, !is_depth, is_depth);
    ID3D12Resource* const src = is_depth ? scene_depth_.Get() : scene_color_.Get();

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = src;
    barriers[0].Transition.StateBefore = is_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = probe_target_.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(2, barriers);

    list->OMSetRenderTargets(1, &probe_rtv_, FALSE, nullptr);
    list->SetGraphicsRootSignature(s_conv_root_sig.Get());
    list->SetPipelineState(conv_pipeline_.Get());
    list->SetGraphicsRootDescriptorTable(1, is_depth ? conv_scene_depth_srv_ : conv_scene_color_srv_);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (std::size_t i = 0; i < probes.size(); ++i) {
        if (probes[i].x < 0.0f || probes[i].y < 0.0f) {
            continue;
        }
        const D3D12_VIEWPORT viewport{static_cast<float>(i), 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        const D3D12_RECT scissor{static_cast<LONG>(i), 0, static_cast<LONG>(i) + 1, 1};
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        const SceneConversionShaderConstants constants = make_screen_probe_constants(probes[i].x, probes[i].y, frame_, is_depth);
        list->SetGraphicsRoot32BitConstants(0, 16, constants.values.data(), 0);
        list->DrawInstanced(3, 1, 0, 0);
    }

    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    list->ResourceBarrier(2, barriers);

    D3D12_TEXTURE_COPY_LOCATION src_copy{};
    src_copy.pResource = probe_target_.Get();
    src_copy.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst_copy{};
    dst_copy.pResource = out.readback.Get();
    dst_copy.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_copy.PlacedFootprint.Footprint.Format = kSceneColorFormat;
    dst_copy.PlacedFootprint.Footprint.Width = kScreenProbesMax;
    dst_copy.PlacedFootprint.Footprint.Height = 1;
    dst_copy.PlacedFootprint.Footprint.Depth = 1;
    dst_copy.PlacedFootprint.Footprint.RowPitch = kScreenProbesMax * 4u;
    static_assert(kScreenProbesMax * 4u % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0u);
    list->CopyTextureRegion(&dst_copy, 0, 0, 0, &src_copy, nullptr);

    // The drawing state as record_conversion_blit restores it.
    list->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_draw_bindings();
    bind_scene();
    out.fence = kProbesInFrame;  // fenced at end_frame
    return true;
}

const std::uint8_t* RendererD3D12::screen_probes_finished(std::size_t slot) noexcept {
    ScreenProbeSlot& out = probe_slots_[slot];
    if (out.fence == 0u || out.fence == kProbesInFrame || completed_fence_value() < out.fence) {
        return nullptr;
    }
    out.fence = 0u;
    return out.rgba;
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
    unsigned row_pitch) {
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
    for (unsigned y = 0u; y < height; ++y) {
        std::memcpy(
            rgba.data() + static_cast<std::size_t>(y) * source_row_bytes,
            source + static_cast<std::size_t>(y) * row_pitch,
            source_row_bytes);
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
    settle_owed_scene();
    // The readback decodes RGBA8: the HDR scene scene target's dumps are taken after its scene's end.
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

    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = scene_color_.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

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

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);
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
            const std::uint8_t r = row[x * 4 + 0];
            const std::uint8_t g = row[x * 4 + 1];
            const std::uint8_t bl = row[x * 4 + 2];
            const std::uint8_t a = row[x * 4 + 3];
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
            row_pitch);
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
        ++telemetry_no_display_present_count_;
        throw std::runtime_error(
            "present requested without a valid display picture blit source");
    }
    const DisplayTexture* source = frame;
    if (capture_backbuffer) {
        if (capture_waits_) {
            // Sequence captures: every present is captured, so the previous
            // readback is waited for instead of skipping this one.
            if (selected_display_readback_pending_ && selected_display_readback_fence_ != 0u) {
                wait_for_fence_value(selected_display_readback_fence_);
            }
            if (backbuffer_readback_pending_ && backbuffer_readback_fence_value_ != 0u) {
                wait_for_fence_value(backbuffer_readback_fence_value_);
            }
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

    // Transition backbuffer -> RENDER_TARGET.
    D3D12_RESOURCE_BARRIER bar{};
    bar.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource   = backbuffers_[buf_idx].Get();
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    bar.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &bar);

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
        ++telemetry_backbuffer_clear_count_;
    }

    if (source->copy_serial != last_presented_display_serial_) {
        last_presented_display_serial_ = source->copy_serial;
        ++telemetry_adjacent_serial_transition_count_;
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
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
    const GpuPassScope present_pass{*this, GpuPass::Present};
    command_list_->SetDescriptorHeaps(2, heaps);
    if (black) {
        // The frame shows black : the display picture is not scanned out.
    } else if (post_process_.ready()) {
        // The picture: an display picture copy's whole texture, or the top rows of a handed-over scene target.
        post_process_.record(
            command_list_.Get(), srv_ring_, source->texture.Get(), source->width, source->height,
            back_rtv, vp, frame_index_, kFramesInFlight);
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
        sd.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels       = 1;
        device_->CreateShaderResourceView(source->texture.Get(), &sd, tbl.cpu);
        command_list_->SetGraphicsRootSignature(s_blit_root_sig.Get());
        command_list_->SetPipelineState(blit_pipeline_.Get());
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

    // The overlays are drawn in backbuffer pixels, not game coordinates:
    // the full-surface raster state again, so captures below include the whole
    // composited surface (borders, game, overlays).
    const D3D12_VIEWPORT overlay_vp{
        0.0f, 0.0f, static_cast<float>(host_window::backbuffer_width),
        static_cast<float>(host_window::backbuffer_height), 0.0f, 1.0f};
    const D3D12_RECT overlay_sci{
        0, 0, static_cast<LONG>(host_window::backbuffer_width),
        static_cast<LONG>(host_window::backbuffer_height)};
    command_list_->RSSetViewports(1, &overlay_vp);
    command_list_->RSSetScissorRects(1, &overlay_sci);

    host_window::draw_native_settings_overlay(*this);
    host_window::draw_home_menu(*this);

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

        D3D12_RESOURCE_BARRIER capture_barrier{};
        capture_barrier.Type =
            D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        capture_barrier.Transition.pResource =
            backbuffers_[buf_idx].Get();
        capture_barrier.Transition.StateBefore =
            D3D12_RESOURCE_STATE_RENDER_TARGET;
        capture_barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_COPY_SOURCE;
        capture_barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list_->ResourceBarrier(1, &capture_barrier);

        D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
        src.pResource = backbuffers_[buf_idx].Get();
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

        std::swap(
            capture_barrier.Transition.StateBefore,
            capture_barrier.Transition.StateAfter);
        command_list_->ResourceBarrier(1, &capture_barrier);
        backbuffer_readback_pending_ = true;
        backbuffer_readback_fence_value_ = 0u;
        backbuffer_readback_width_ = w;
        backbuffer_readback_height_ = h;
        backbuffer_readback_row_pitch_ = row_pitch;
        backbuffer_readback_capture_path_ = next_backbuffer_capture_path_;
    }

    // Backbuffer -> PRESENT.
    std::swap(bar.Transition.StateBefore, bar.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &bar);
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
        desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
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
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = source.texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = source.texture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = selected_display_readback_buf_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = source.width;
    dst.PlacedFootprint.Footprint.Height = source.height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = pitch;
    // The picture's rows (a handed-over scene target is taller).
    const D3D12_BOX picture{0u, 0u, 0u, source.width, source.height, 1u};
    command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, &picture);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    selected_display_readback_source_ = source.texture;
    selected_display_readback_width_ = source.width;
    selected_display_readback_height_ = source.height;
    selected_display_readback_pitch_ = pitch;
    selected_display_readback_path_ = next_backbuffer_capture_path_ + ".display.ppm";
    selected_display_readback_fence_ = 0u;
    selected_display_readback_pending_ = true;
    std::cerr << "[selected-display-capture] slot=" << source.slot
              << " resource=0x" << std::hex << reinterpret_cast<std::uintptr_t>(source.texture.Get())
              << std::dec << " serial=" << source.copy_serial << " stamp=" << source.frame_stamp
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
            selected_display_readback_height_, selected_display_readback_pitch_);
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
