// RendererD3D12: device and swap chain, EFB targets and copies, draws, presentation, and the
// replayed sub-frames of frame interpolation.
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/interp_eval.h"
#include "galaxy/gx/host_window_win32.h"
#include "galaxy/gx/rt_materials.h"
#include "galaxy/crash_notes.h"
#include "galaxy/gx/display_modes.h"

#include "galaxy/content_viewport.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/gx/game_asset_extract.h"
#include "galaxy/gx/galaxy_select.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/rt_prepare.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/streamline_bridge.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/motion_signals.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/scope_exit.h"
#include "galaxy/native_host.h"
#include "galaxy/frame_timeline.h"
#include "galaxy/windows_power.h"

#pragma warning(push, 0)
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
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

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace galaxy::gx {

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

// Present-blit root signature; lives as long as the RendererD3D12.
static ComPtr<ID3D12RootSignature> s_blit_root_sig;
// EFB-copy conversion blit root signature.
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
    if (getenv_s(&length, value, sizeof(value), "GALAXY_GPU_PREFERENCE") == 0 &&
        length > 1 && (value[0] == 'l' || value[0] == 'L' || value[0] == 'm' || value[0] == 'M')) {
        return DXGI_GPU_PREFERENCE_MINIMUM_POWER;
    }
    return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
}

bool trace_present_stats_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_TRACE_PRESENT_STATS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

// Frame GPU timestamps (two queries and a resolve per command list) are always on: Match Output's GPU budget reads
// them. GALAXY_GPU_TIMESTAMPS=0 turns them off, and the budget with them.
bool gpu_timestamps_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_GPU_TIMESTAMPS") == 0 &&
            length > 1) {
            return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
        }
        return true;
    }();
    return enabled;
}

bool allow_tearing_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_ALLOW_TEARING") != 0 ||
            length <= 1) {
            return get_render_config().allow_tearing;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool flip_discard_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_FLIP_DISCARD") != 0 ||
            length <= 1) {
            return get_render_config().flip_discard;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

unsigned swap_chain_frame_latency() {
    static const unsigned latency = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_SWAP_CHAIN_FRAME_LATENCY") != 0 ||
            length <= 1) {
            return std::clamp(
                get_render_config().swap_chain_frame_latency,
                1u,
                kFramesInFlight);
        }
        return std::clamp(
            static_cast<unsigned>(std::strtoul(value, nullptr, 10)),
            1u,
            kFramesInFlight);
    }();
    return latency;
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

bool frame_latency_wait_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_WAIT_FOR_FRAME_LATENCY") != 0 ||
            length <= 1) {
            return get_render_config().wait_for_frame_latency;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
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

static ComPtr<ID3DBlob> compile_hlsl(
    const char* src, std::size_t len, const char* target, const char* entry) {
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(
        src, len, nullptr, nullptr, nullptr, entry, target,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &code, &errors);
    if (FAILED(hr)) {
        const char* msg = errors
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "(no error blob)";
        throw std::runtime_error(std::string("D3DCompile(") + target + "): " + msg);
    }
    return code;
}

}  // namespace

bool validate_efb_conversion_shader(std::string& diagnostics) noexcept {
    try {
        const ComPtr<ID3DBlob> code = compile_hlsl(
            efb_conversion_shader_source().data(), efb_conversion_shader_source().size(), "ps_5_0", "main");
        diagnostics.clear();
        return code != nullptr;
    } catch (const std::exception& error) {
        diagnostics = error.what();
        return false;
    } catch (...) {
        diagnostics = "unknown D3DCompile failure";
        return false;
    }
}

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

// Upload rings are committed UPLOAD-heap resources mapped for the life of the process, so
// bytes_per_frame * frames_in_flight stays reserved: (64+16+8+32) MiB * 2 slots = 240 MiB, plus a
// shader-visible descriptor heap. An UPLOAD heap's untouched pages are never backed, so unused
// reservation costs address space and commit, not memory; [gpu-rings] logs the reservation and
// UploadRing tracks each ring's peak use. On an integrated adapter the memory is system memory;
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

// The adapter and its user-mode driver's version, as the ray tracing pipeline library records them (rt_prepare.h).
rt_prepare::Identity rt_identity(IDXGIAdapter1* adapter, const DXGI_ADAPTER_DESC1& desc) {
    char text[48]{};
    std::snprintf(text, sizeof(text), "%04x:%04x:%08x:%02x", static_cast<unsigned>(desc.VendorId & 0xFFFFu),
                  static_cast<unsigned>(desc.DeviceId & 0xFFFFu), static_cast<unsigned>(desc.SubSysId),
                  static_cast<unsigned>(desc.Revision & 0xFFu));
    rt_prepare::Identity identity{text, {}};
    LARGE_INTEGER umd{};
    if (adapter != nullptr && SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u", static_cast<unsigned>(HIWORD(umd.HighPart)),
                      static_cast<unsigned>(LOWORD(umd.HighPart)), static_cast<unsigned>(HIWORD(umd.LowPart)),
                      static_cast<unsigned>(LOWORD(umd.LowPart)));
        identity.driver = text;
    }
    return identity;
}

}  // namespace

std::uint64_t RendererD3D12::efb_copy_dest_budget_bytes() {
    return galaxy::gx::efb_copy_dest_budget_bytes();
}

// The upload rings' first buffers. A segmentation gives each frame an equal share and keeps one more share as
// spill, so a frame may take twice its budget; a ring whose frames take over half of it grows at the next frame
// boundary. Worst observed frame without the whole stage drawn: 117,361 vertices x 144 B = 16.1 MiB.
static constexpr std::size_t kVertexRingBytes = 192u * 1024u * 1024u;
static constexpr std::size_t kIndexRingBytes = 48u * 1024u * 1024u;
// Interpolated sub-frames add one VS constant block per scene record each.
static constexpr std::size_t kConstantRingBytes = 96u * 1024u * 1024u;
// A busy frame reloads the XF palette many times (per-object and skinned matrices via LOAD_INDX).
static constexpr std::size_t kMatrixRingBytes = 128u * 1024u * 1024u;
static constexpr unsigned    kSrvPerFrame = 32768u;
// The descriptor ring's persistent prefix, outside the rewound frame segments: the EFB
// conversion sources (the EFB's views, the output EFB's, the MSAA depth's, then the HDR scene
// EFBs', live and sub-frame: 10), the ray-traced scenes' texture views and the de-light views.
// TextureCache's own SRV indices are independent of this prefix.
static constexpr unsigned    kRtTextureViews = SceneRayTracer::kTextureViewScenes * SceneRayTracer::kTextureViewsPerScene;
static constexpr unsigned    kPersistentSrvDescriptors = 10u + kRtTextureViews + SceneRayTracer::kDelightViews;

void RendererD3D12::wait_for_gpu() {
    const std::uint64_t val = ++next_fence_value_;
    const HRESULT signal_hr = queue_->Signal(fence_.Get(), val);
    if (FAILED(signal_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(signal_hr, "Queue Signal");
    }
    wait_for_fence_value(val);
}

bool RendererD3D12::create_efb_targets(unsigned efb_scale) {
    // The physical targets scale; guest EFB/XFB addresses and dimensions keep the
    // Wii's logical size (as Aurora's lib/window.cpp and lib/gx/gx.cpp do).
    //
    // Both resources are created before anything is replaced: a failure
    // (out of video memory) leaves the current targets untouched. On success
    // the previous targets move to efb_carry_*, whose contents begin_frame
    // copies into the new ones.
    const std::uint64_t width =
        static_cast<std::uint64_t>(kEfbWidth) * efb_scale;
    const std::uint64_t height =
        static_cast<std::uint64_t>(kEfbHeight) * efb_scale;
    if (efb_scale < 1u || efb_scale > kMaxEfbScale ||
        width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        width > static_cast<std::uint64_t>(
            std::numeric_limits<LONG>::max()) ||
        height > static_cast<std::uint64_t>(
            std::numeric_limits<LONG>::max())) {
        std::cerr << "[Renderer] invalid physical EFB extent\n";
        return false;
    }
    const UINT w = static_cast<UINT>(width);
    const UINT efb_h = static_cast<UINT>(height);

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Resource> color;
    {
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width              = w;
        rd.Height             = efb_h;
        rd.DepthOrArraySize   = 1;
        rd.MipLevels          = 1;
        rd.Format             = kEfbColorFormat;
        rd.SampleDesc.Count   = 1;
        rd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv{};
        cv.Format   = kEfbColorFormat;
        cv.Color[3] = 1.0f;
        const HRESULT hr = device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, IID_PPV_ARGS(&color));
        if (FAILED(hr)) {
            std::cerr << "[Renderer] EFB color " << w << 'x' << efb_h
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
        drd.Height             = efb_h;
        drd.DepthOrArraySize   = 1;
        drd.MipLevels          = 1;
        drd.Format             = kEfbDepthResourceFormat;
        drd.SampleDesc.Count   = 1;
        drd.Flags              = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE dcv{};
        dcv.Format               = kEfbDepthFormat;
        dcv.DepthStencil.Depth   = 1.0f;
        const HRESULT hr = device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &drd,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv, IID_PPV_ARGS(&depth));
        if (FAILED(hr)) {
            std::cerr << "[Renderer] EFB depth " << w << 'x' << efb_h
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
    // A picture the EFB still owes is its colour (carry_over_efb_contents carries it); the target the EFB continued on
    // goes with the GPU drained.
    efb_carry_color_readable_ = efb_owed_ != nullptr;
    efb_carry_color_ = efb_owed_ ? std::move(efb_owed_) : std::move(efb_color_);
    efb_carry_depth_ = std::move(efb_depth_);
    efb_color_ = std::move(color);
    efb_depth_ = std::move(depth);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kEfbDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(
        efb_depth_.Get(), &dsv, dsv_heap_->GetCPUDescriptorHandleForHeapStart());

    // EFB RTV at slot kSwapChainBuffers of the backbuffer RTV heap.
    {
        const UINT stride = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE h =
            rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(kSwapChainBuffers) * stride;
        device_->CreateRenderTargetView(efb_color_.Get(), nullptr, h);
        efb_rtv_ = h;
    }
    efb_dsv_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    // MSAA: draws and clears go to the multisampled targets; efb_color_ and efb_depth_ hold their resolves.
    if (!create_efb_msaa(efb_msaa_, efb_depth_.Get(), efb_scale, wanted_msaa_samples(), efb_rtv_, efb_dsv_)) {
        std::cerr << "[Renderer] MSAA targets unavailable; the EFB draws without MSAA\n";
        efb_msaa_.samples = 1u;
        const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        efb_rtv_ = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        efb_rtv_.ptr += static_cast<SIZE_T>(kSwapChainBuffers) * stride;
    } else if (efb_msaa_.samples > 1u) {
        std::cout << "[Renderer] EFB MSAA " << efb_msaa_.samples << "x\n";
    }
    return true;
}

unsigned RendererD3D12::wanted_msaa_samples() const {
    const unsigned asked = std::min(get_render_config().msaa_samples, 8u);
    if (asked <= 1u || !device_ || temporal_mode() || rt_active()) {
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
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS color{kEfbColorFormat, samples};
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS depth{kEfbDepthFormat, samples};
        if (SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &color, sizeof(color))) &&
            SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &depth, sizeof(depth))) &&
            color.NumQualityLevels > 0u && depth.NumQualityLevels > 0u) {
            msaa_max_samples_ = samples;
            break;
        }
    }
    return msaa_max_samples_;
}

bool RendererD3D12::create_efb_msaa(EfbMsaa& msaa, ID3D12Resource* depth, unsigned scale, unsigned samples,
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
    rd.Width = kEfbWidth * scale;
    rd.Height = kEfbHeight * scale;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kEfbColorFormat;
    rd.SampleDesc.Count = samples;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = kEfbColorFormat;
    cv.Color[3] = 1.0f;
    if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                &cv, IID_PPV_ARGS(&msaa.color)))) {
        return false;
    }
    rd.Format = kEfbDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kEfbDepthFormat;
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
    color_view.Format = kEfbColorFormat;
    color_view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    device_->CreateRenderTargetView(msaa.color.Get(), &color_view, rtv);
    const UINT dsv_stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    dsv = msaa.dsv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC depth_view{};
    depth_view.Format = kEfbDepthFormat;
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

// The multisampled depth's view for the depth resolve (made at the first resolve: the EFB's first targets come before
// the descriptor ring).
void RendererD3D12::write_msaa_depth_view(EfbMsaa& msaa) {
    if (!msaa.depth_srv) {
        msaa.depth_srv = srv_ring_.allocate_persistent(1u);
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = kEfbDepthSrvFormat;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device_->CreateShaderResourceView(msaa.depth.Get(), &view, msaa.depth_srv->cpu);
}

namespace {

// The depth resolve: each pixel's first sample (the console's single sample sits at the pixel centre).
constexpr char kDepthResolvePs[] = R"HLSL(
Texture2DMS<float> depth_samples : register(t0);
float main(float4 pos : SV_Position) : SV_Depth {
    return depth_samples.Load(int2(pos.xy), 0);
}
)HLSL";

}  // namespace

void RendererD3D12::resolve_efb(bool depth, bool color_wanted) {
    settle_owed_efb();
    EfbMsaa& msaa = efb_msaa_;
    const bool color = color_wanted && msaa.color_dirty;
    depth = depth && msaa.depth_dirty;
    if (msaa.samples <= 1u || (!color && !depth)) {
        return;
    }
    const GpuPassScope pass{*this, GpuPass::Resolve};
    record_efb_resolve(command_list_.Get(), color, depth);
    msaa.color_dirty = msaa.color_dirty && !color;
    if (depth) {
        // The EFB's own targets and the GX state again, as the next draw expects them.
        command_list_->SetGraphicsRootSignature(root_signature_.Get());
        command_list_->OMSetRenderTargets(1, &efb_rtv_, FALSE, &efb_dsv_);
        bound_rt_aux_ = 0u;
        invalidate_gx_bindings();
        bound_viewport_valid_ = false;
        bound_scissor_valid_ = false;
        bound_scissor_input_valid_ = false;
        msaa.depth_dirty = false;
    }
}

void RendererD3D12::resolve_efb_on(ID3D12GraphicsCommandList* list, bool color, bool depth) {
    if (list == command_list_.Get()) {
        resolve_efb(depth);
        return;
    }
    color = color && efb_msaa_.color_dirty;
    depth = depth && efb_msaa_.depth_dirty;
    if (color || depth) {
        record_efb_resolve(list, color, depth);
    }
}

void RendererD3D12::record_efb_resolve(ID3D12GraphicsCommandList* list, bool color, bool depth) {
    EfbMsaa& msaa = efb_msaa_;
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
        barriers[1].Transition.pResource = efb_color_.Get();
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
        list->ResourceBarrier(2u, barriers);
        list->ResolveSubresource(efb_color_.Get(), 0u, msaa.color.Get(), 0u, kEfbColorFormat);
        for (auto& barrier : barriers) {
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        }
        list->ResourceBarrier(2u, barriers);
    }
    if (depth) {
        if (!depth_resolve_pso_) {
            const ComPtr<ID3DBlob> vs =
                compile_hlsl(blit_vertex_shader_source().data(), blit_vertex_shader_source().size(), "vs_5_1", "main");
            const ComPtr<ID3DBlob> ps = compile_hlsl(kDepthResolvePs, sizeof(kDepthResolvePs) - 1u, "ps_5_1", "main");
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
            pd.DSVFormat = kEfbDepthFormat;
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
        const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(kEfbWidth * efb_scale_),
                                      static_cast<float>(kEfbHeight * efb_scale_), 0.0f, 1.0f};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(kEfbWidth * efb_scale_),
                                 static_cast<LONG>(kEfbHeight * efb_scale_)};
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

// After an internal-resolution change the new EFB starts with the previous targets' contents,
// resampled. The game expects the EFB it left at the end of the last frame (normally the display
// copy's clear, whose alpha its shadow and glow passes read); zero-filled targets would show as a
// dark picture with stray geometry.
void RendererD3D12::carry_over_efb_contents() {
    settle_owed_efb();
    if (!efb_carry_color_ || !efb_carry_depth_ || efb_msaa_.samples > 1u) {
        if (efb_msaa_.samples > 1u && efb_carry_color_) {
            const auto far_values = make_efb_clear_values(0u, 0u, 0x00FFFFFFu, 0u);
            const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            command_list_->ClearRenderTargetView(efb_rtv_, black, 0u, nullptr);
            command_list_->ClearDepthStencilView(efb_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                                 far_values.reversed_depth, 0u, 0u, nullptr);
            efb_msaa_.color_dirty = efb_msaa_.depth_dirty = true;
            retired_efb_copy_textures_[frame_slot_].push_back(std::move(efb_carry_color_));
            retired_efb_copy_textures_[frame_slot_].push_back(std::move(efb_carry_depth_));
        }
        efb_carry_color_.Reset();
        efb_carry_depth_.Reset();
        efb_carry_color_readable_ = false;
        return;
    }
    if (!efb_carry_depth_pso_) {
        const ComPtr<ID3DBlob> vs = compile_hlsl(blit_vertex_shader_source().data(), blit_vertex_shader_source().size(), "vs_5_1", "main");
        const ComPtr<ID3DBlob> ps =
            compile_hlsl(depth_carry_pixel_shader_source().data(), depth_carry_pixel_shader_source().size(), "ps_5_1", "main");
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
        pd.DSVFormat = kEfbDepthFormat;
        pd.SampleDesc.Count = 1;
        if (FAILED(device_->CreateGraphicsPipelineState(
                &pd, IID_PPV_ARGS(&efb_carry_depth_pso_)))) {
            efb_carry_depth_pso_.Reset();
        }
    }

    const DescriptorRing::Table table = srv_ring_.allocate(2);
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kEfbColorFormat;
    device_->CreateShaderResourceView(efb_carry_color_.Get(), &view, table.cpu);
    D3D12_CPU_DESCRIPTOR_HANDLE depth_cpu = table.cpu;
    depth_cpu.ptr += stride;
    D3D12_GPU_DESCRIPTOR_HANDLE depth_gpu = table.gpu;
    depth_gpu.ptr += stride;
    view.Format = kEfbDepthSrvFormat;
    device_->CreateShaderResourceView(efb_carry_depth_.Get(), &view, depth_cpu);

    D3D12_RESOURCE_BARRIER to_read[2]{};
    for (auto& barrier : to_read) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    to_read[0].Transition.pResource = efb_carry_depth_.Get();
    to_read[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    to_read[1].Transition.pResource = efb_carry_color_.Get();
    to_read[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_list_->ResourceBarrier(efb_carry_color_readable_ ? 1u : 2u, to_read);
    efb_carry_color_readable_ = false;

    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(kEfbWidth * efb_scale_),
        static_cast<float>(kEfbHeight * efb_scale_), 0.0f, 1.0f};
    const D3D12_RECT scissor{
        0, 0, static_cast<LONG>(kEfbWidth * efb_scale_),
        static_cast<LONG>(kEfbHeight * efb_scale_)};
    command_list_->SetGraphicsRootSignature(s_blit_root_sig.Get());
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->OMSetRenderTargets(1, &efb_rtv_, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    command_list_->SetPipelineState(blit_pipeline_.Get());
    command_list_->SetGraphicsRootDescriptorTable(0, table.gpu);
    command_list_->DrawInstanced(3, 1, 0, 0);
    if (efb_carry_depth_pso_) {
        command_list_->OMSetRenderTargets(0, nullptr, FALSE, &efb_dsv_);
        bound_rt_aux_ = 0u;
        command_list_->SetPipelineState(efb_carry_depth_pso_.Get());
        command_list_->SetGraphicsRootDescriptorTable(0, depth_gpu);
        command_list_->DrawInstanced(3, 1, 0, 0);
    } else {
        const auto far_values = make_efb_clear_values(0u, 0u, 0x00FFFFFFu, 0u);
        command_list_->ClearDepthStencilView(
            efb_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            far_values.reversed_depth, 0u, 0u, nullptr);
    }
    // The old targets are read by this frame's command list.
    retired_efb_copy_textures_[frame_slot_].push_back(std::move(efb_carry_color_));
    retired_efb_copy_textures_[frame_slot_].push_back(std::move(efb_carry_depth_));
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
}

HRESULT create_gx_root_signature(
    ID3D12Device* device,
    ID3D12RootSignature** root_signature) noexcept {
    if (device == nullptr || root_signature == nullptr) {
        return E_INVALIDARG;
    }
    *root_signature = nullptr;

    // GX root signature:
    //   [0] Root CBV b0,space0  VS+GS - GxVsConstants
    //   [1] Root CBV b1,space0  PS - GxPsConstants
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

    D3D12_ROOT_PARAMETER params[8]{};
    params[0].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace  = 0;
    params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType             = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace  = 0;
    params[1].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

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
    // The matrix palette also holds the XF lights, which per-pixel lighting
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

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 8;
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
        create_gx_root_signature(
            device_.Get(), root_signature_.ReleaseAndGetAddressOf()),
        "Create GX root signature");
    return true;
}

// Each frame slot owns a sampler heap. Slots 0-7 hold its default table;
// dynamic tables are reclaimed only after that slot's submission fence.

D3D12_SAMPLER_DESC build_sampler_desc_from_key(
    std::uint32_t key,
    const RenderConfig& cfg) noexcept {
    auto to_d3d_wrap = [](std::uint32_t gx) -> D3D12_TEXTURE_ADDRESS_MODE {
        switch (gx) {
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

    // TexMinFilter: 0 Near, 1 NearMipNear, 2 NearMipLinear,
    //               4 Linear, 5 LinearMipNear, 6 LinearMipLinear.
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

// Packed sampler key layout (must match pack_sampler_key in gx_backend.cpp):
//   bits 0-1  wrap_s (GX TexWrap)        bits 2-3  wrap_t
//   bit  4    mag linear                 bits 5-7  min filter (TexMinFilter)
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

// EFB-copy conversion pipeline: root signature (16 root constants, one SRV
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
        const ComPtr<ID3DBlob> vs =
            compile_hlsl(blit_vertex_shader_source().data(), blit_vertex_shader_source().size(), "vs_5_1", "main");
        const ComPtr<ID3DBlob> ps =
            compile_hlsl(efb_conversion_shader_source().data(), efb_conversion_shader_source().size(), "ps_5_1", "main");

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
        pd.RTVFormats[0]         = kEfbColorFormat;
        pd.SampleDesc.Count      = 1;
        d3d_check(device_->CreateGraphicsPipelineState(
            &pd, IID_PPV_ARGS(&conv_pipeline_)), "CreateConvPSO");
        pd.RTVFormats[0] = DXGI_FORMAT_R8_UNORM;
        d3d_check(device_->CreateGraphicsPipelineState(
            &pd, IID_PPV_ARGS(&conv_pipeline_r8_)), "CreateConvPSO R8");
        const ComPtr<ID3DBlob> filter =
            compile_hlsl(display_filter_shader_source().data(), display_filter_shader_source().size(), "ps_5_1", "main");
        pd.PS = {filter->GetBufferPointer(), filter->GetBufferSize()};
        pd.RTVFormats[0] = kEfbColorFormat;
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

    refresh_efb_conversion_source_views();
    return true;
}

void RendererD3D12::refresh_efb_conversion_source_views() {
    if (!efb_color_ || !efb_depth_) {
        throw std::runtime_error("EFB conversion source resource unavailable");
    }
    if (!conv_efb_source_table_) {
        conv_efb_source_table_ = srv_ring_.allocate_persistent(2u);
    }
    write_efb_conversion_views(*conv_efb_source_table_);
}

void RendererD3D12::write_efb_conversion_views(const DescriptorRing::Table& table) {
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kEfbColorFormat;
    device_->CreateShaderResourceView(efb_color_.Get(), &view, table.cpu);
    auto depth_cpu = table.cpu;
    depth_cpu.ptr += stride;
    view.Format = kEfbDepthSrvFormat;
    device_->CreateShaderResourceView(efb_depth_.Get(), &view, depth_cpu);
    conv_efb_color_srv_ = table.gpu;
    auto depth_gpu = table.gpu;
    depth_gpu.ptr += stride;
    conv_efb_depth_srv_ = depth_gpu;
}

bool RendererD3D12::can_hand_over_display_copy(const EfbCopyParams& params, const XfbTexture& dest) const {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value), "GALAXY_NATIVE_XFB") != 0 || length <= 1u || value[0] != '0';
    }();
    // GALAXY_TEST_NATIVE_XFB_AB=1: handed over in every other presented frame, copied in the others.
    static const bool test_ab = [] {
        char value[8]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value), "GALAXY_TEST_NATIVE_XFB_AB") == 0 && length > 1u &&
               value[0] == '1';
    }();
    if (!enabled || (test_ab && (presented_frames_ & 1u) != 0u) || !efb_color_ || efb_owed_ ||
        efb_carry_color_ || replay_recording_ || replay_playing_ || ring_mode_ == RingMode::Interp ||
        frame_generation_running() || output_efb_active_ || efb_msaa_.samples > 1u || efb_hdr_ || scene_temporal_ ||
        rt_active() || !post_process_.ready() || !post_process_.reads_held_pictures()) {
        return false;
    }
    const unsigned scale = efb_scale_;
    const bool identity_filter =
        (params.filter_upper() == 0u && params.filter_middle() == 64u && params.filter_lower() == 0u) ||
        (params.filter_upper() == 0u && params.filter_middle() == 0u && params.filter_lower() == 0u);
    return identity_filter && (params.gamma & 3u) == 0u && !params.intensity && !params.half_scale &&
           (!params.scale_y || params.y_scale_raw == 256u) && params.src_x == 0u && params.src_y == 0u &&
           params.src_width == kEfbWidth && params.src_height != 0u && params.src_height <= kEfbHeight &&
           dest.width == kEfbWidth * scale && dest.height == params.src_height * scale;
}

gpu::TextureRef RendererD3D12::hand_over_efb_color(gpu::TextureRef fresh) {
    const GpuPassScope pass{*this, GpuPass::XfbCopy};
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
    D3D12_RESOURCE_BARRIER barriers[2]{};
    UINT barrier_count = 0;
    if (fresh) {
        const D3D12_RESOURCE_DESC got = fresh->GetDesc();
        if (got.Width != desc.Width || got.Height != desc.Height || got.Format != desc.Format ||
            got.Flags != desc.Flags || got.SampleDesc.Count != 1u) {
            throw std::runtime_error("handed-over EFB replacement has another description");
        }
        // Released XFBs are kept as shader resources.
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
        clear.Format = kEfbColorFormat;
        clear.Color[3] = 1.0f;
        d3d_check(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&fresh)),
                  "Create EFB colour for a handover");
    }
    // The picture becomes an XFB: a shader resource, as presentation reads XFBs.
    D3D12_RESOURCE_BARRIER& picture_barrier = barriers[barrier_count++];
    picture_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    picture_barrier.Transition.pResource = efb_color_.Get();
    picture_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    picture_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    picture_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(barrier_count, barriers);

    gpu::TextureRef picture = std::move(efb_color_);
    efb_color_ = std::move(fresh);
    // RTVs are copied when recorded: the EFB's slot takes the new target now. Its conversion views, which the GPU reads
    // when it runs, go to the next table in turn (a frame in flight still reads the one before).
    device_->CreateRenderTargetView(efb_color_.Get(), nullptr, efb_rtv_);
    std::optional<DescriptorRing::Table>& table = conv_efb_tables_[conv_efb_table_next_];
    conv_efb_table_next_ = (conv_efb_table_next_ + 1u) % static_cast<unsigned>(conv_efb_tables_.size());
    if (!table) {
        table = srv_ring_.allocate_persistent(2u);
    }
    write_efb_conversion_views(*table);
    efb_owed_ = picture;
    ++efb_handovers_;
    bind_efb(nullptr);
    return picture;
}

void RendererD3D12::restore_owed_efb(const D3D12_RECT* kept) {
    ComPtr<ID3D12Resource> owed = std::move(efb_owed_);
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
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
            ++efb_handovers_free_;
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
    b[1].Transition.pResource = efb_color_.Get();
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    command_list_->ResourceBarrier(2, b);
    if (kept == nullptr) {
        command_list_->CopyResource(efb_color_.Get(), owed.Get());
        ++efb_handovers_restored_;
    } else {
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = efb_color_.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = owed.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        for (UINT i = 0; i < box_count; ++i) {
            command_list_->CopyTextureRegion(&dst, boxes[i].left, boxes[i].top, 0, &src, &boxes[i]);
        }
        ++efb_handovers_banded_;
    }
    for (auto& barrier : b) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    command_list_->ResourceBarrier(2, b);
}

// The share of wall time the GPU may spend on the frame's command lists before Match Output steps down: the rest is
// headroom for heavier frames than the window's average, the compositor and other processes.
constexpr double kGpuBudgetBusy = 0.80;

void RendererD3D12::update_gpu_budget() {
    const auto now = std::chrono::steady_clock::now();
    if (gpu_budget_window_start_ == std::chrono::steady_clock::time_point{}) {
        gpu_budget_window_start_ = now;
        gpu_budget_ms_ = 0.0;
        return;
    }
    const double window_ms = std::chrono::duration<double, std::milli>(now - gpu_budget_window_start_).count();
    if (window_ms < 2000.0) {
        return;
    }
    const double busy = gpu_budget_ms_ / window_ms;
    gpu_budget_window_start_ = now;
    gpu_budget_ms_ = 0.0;
    const RenderConfig cfg = get_render_config();
    if (!cfg.gpu_budget || !cfg.internal_follow_window || efb_scale_ <= 1u || output_efb_active_) {
        gpu_budget_over_windows_ = 0u;
        return;
    }
    if (busy <= kGpuBudgetBusy) {
        gpu_budget_over_windows_ = 0u;
        return;
    }
    if (++gpu_budget_over_windows_ < 2u) {
        return;
    }
    gpu_budget_over_windows_ = 0u;
    gpu_budget_cap_ = efb_scale_ - 1u;
    follow_window_width_ = 0u;  // re-derive the scale under the cap at this frame boundary
    std::cerr << "[Renderer] GPU busy " << static_cast<int>(busy * 100.0 + 0.5) << "% at internal resolution "
              << efb_scale_ << "x; Match Output capped at " << gpu_budget_cap_ << "x\n";
}

void RendererD3D12::follow_window_internal_resolution() {
    // Dynamic aspect latches a new window shape at a later VI boundary than
    // the swap-chain resize, so the content aspect is part of the key.
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const double content_aspect =
        aspect ? aspect->content_aspect : galaxy::kWiiContentAspect;
    if (host_window::follow_window_rearm.load(std::memory_order_relaxed) &&
        host_window::follow_window_rearm.exchange(false, std::memory_order_acq_rel)) {
        follow_window_width_ = 0u;
    }
    if (follow_window_retry_ms_ != 0u && GetTickCount64() >= follow_window_retry_ms_) {
        follow_window_retry_ms_ = 0u;
        follow_window_width_ = 0u;
    }
    if (host_window::backbuffer_width == follow_window_width_ &&
        host_window::backbuffer_height == follow_window_height_ &&
        content_aspect == follow_window_aspect_) {
        return;
    }
    // An edge drag resizes every frame; settle first, then re-create once.
    if (host_window::move_size_active.load(std::memory_order_acquire)) {
        return;
    }
    follow_window_width_ = host_window::backbuffer_width;
    follow_window_height_ = host_window::backbuffer_height;
    follow_window_aspect_ = content_aspect;
    RenderConfig render = get_render_config();
    if (!render.internal_follow_window) {
        return;
    }
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        content_aspect);
    if (content.width <= 0 || content.height <= 0) {
        return;
    }
    // The launcher's Match Output rule: cover the picture on both axes.
    const double needed = std::max(content.width / static_cast<double>(kEfbWidth),
                                   content.height / 456.0);
    unsigned scale = std::clamp(
        static_cast<unsigned>(std::ceil(needed - 1e-6)), 1u, kMaxEfbScale);
    if (gpu_budget_cap_ != 0u) {
        scale = std::min(scale, gpu_budget_cap_);
    }
    if (scale != render.efb_scale) {
        render.efb_scale = scale;
        set_render_config(render);
        stage_next_start_efb_scale(scale);
    }
}

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

void RendererD3D12::apply_pending_efb_scale() {
    if (!device_ || !efb_color_ || output_efb_active_ || efb_hdr_) {
        return;
    }
    const RenderConfig render = get_render_config();
    // The INTERNAL RESOLUTION row sets the frame's resolution: the upscaler's
    // output, else the render resolution. A quality mode renders below it.
    const unsigned internal =
        std::min(std::clamp(render.efb_scale, 1u, kMaxEfbScale), host_window::display_efb_scale_cap());
    const unsigned wanted = host_window::upscaler_render_scale(render, internal);
    const unsigned live_internal = upscale_output_scale_ != 0u ? upscale_output_scale_ : efb_scale_;
    upscale_output_scale_ = internal;
    const unsigned samples = wanted_msaa_samples();
    if (wanted == efb_scale_ && samples == efb_msaa_.samples) {
        return;
    }
    // Keeps the current targets; the setting returns to the live internal resolution.
    const auto refuse = [&] {
        upscale_output_scale_ = live_internal;
        RenderConfig restored = get_render_config();
        restored.efb_scale = live_internal;
        set_render_config(restored);
        stage_next_start_efb_scale(live_internal);
        host_window::efb_scale_rejected.store(wanted, std::memory_order_release);
        return restored.internal_follow_window;
    };
    if (wanted > efb_scale_ && !efb_scale_fits_memory(wanted)) {
        std::cerr << "[Renderer] internal resolution " << wanted
                  << "x exceeds the video-memory budget; staying at "
                  << efb_scale_ << "x\n";
        if (refuse()) {
            follow_window_retry_ms_ = GetTickCount64() + 10'000u;
        }
        return;
    }
    // Between frames with nothing recording; the GPU must be done with the
    // old targets and every scaled copy destination before they go away.
    wait_for_gpu();
    efb_copy_dests_.clear();
    efb_copy_dests_bytes_ = 0u;
    efb_copy_dests_tick_ = 0u;
    for (auto& retired : retired_efb_copy_textures_) {
        retired.clear();
    }
    for (auto& retired : retired_segment_textures_) {
        retired.clear();
    }
    readback_buf_.Reset();
    readback_pending_ = false;
    readback_fence_value_ = 0u;
    const unsigned previous = efb_scale_;
    if (!create_efb_targets(wanted)) {
        (void)refuse();
        return;
    }
    const unsigned applied = wanted;
    efb_scale_ = applied;
    refresh_efb_conversion_source_views();
    invalidate_gx_bindings();
    bound_scissor_input_scale_ = 0u;
    std::cout << "[Renderer] internal resolution " << previous << "x -> "
              << applied << "x (" << kEfbWidth * applied << "x"
              << kEfbHeight * applied << ")";
    if (efb_msaa_.samples > 1u) {
        std::cout << " MSAA " << efb_msaa_.samples << 'x';
    }
    std::cout << '\n';
}

bool RendererD3D12::efb_scale_fits_memory(unsigned scale) const {
    if (!adapter3_) {
        return true;
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter3_->QueryVideoMemoryInfo(
            0u, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) || info.Budget == 0u) {
        return true;
    }
    // Scale-dependent residency: EFB color + depth (8 bytes per pixel; with ray
    // tracing the HDR scene EFB's 12 more) and, at most, the XFB pair plus one
    // full-picture copy (12 bytes per pixel of the 640x456 picture). Everything
    // else is independent of the scale.
    const double efb_bytes = ray_tracer_->ready() ? 20.0 : 8.0;
    const auto scaled_bytes = [efb_bytes](unsigned s) {
        const double area = static_cast<double>(s) * s;
        return area * (static_cast<double>(kEfbWidth) * kEfbHeight * efb_bytes +
                       static_cast<double>(kEfbWidth) * 456.0 * 12.0);
    };
    const double needed = scaled_bytes(scale) - scaled_bytes(efb_scale_);
    return static_cast<double>(info.CurrentUsage) + needed <=
           0.9 * static_cast<double>(info.Budget);
}

std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 2>
detail::create_efb_conversion_source_views(
    ID3D12Device* device, ID3D12Resource* color, ID3D12Resource* depth,
    DescriptorRing& ring) {
    if (device == nullptr || color == nullptr || depth == nullptr) {
        throw std::runtime_error("EFB conversion source resource unavailable");
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
    view.Format = kEfbColorFormat;
    device->CreateShaderResourceView(color, &view, table.cpu);
    view.Format = kEfbDepthSrvFormat;
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

    clear_vs_ = compile_hlsl(blit_vertex_shader_source().data(), blit_vertex_shader_source().size(), "vs_5_1", "main");
    clear_ps_ =
        compile_hlsl(clear_pixel_shader_source().data(), clear_pixel_shader_source().size(), "ps_5_1", "main");
    return clear_pipeline(true) != nullptr && clear_pipeline(false) != nullptr;
}

ID3D12PipelineState* RendererD3D12::clear_pipeline(bool color) {
    const std::uint8_t variant = efb_variant();
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
    pd.RTVFormats[0] = (variant & kEfbVariantHdr) != 0u ? kEfbHdrColorFormat : kEfbColorFormat;
    pd.SampleDesc.Count = 1u << (variant & kEfbVariantSamplesMask);
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = color
        ? D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE
        : D3D12_COLOR_WRITE_ENABLE_ALPHA;
    d3d_check(device_->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pipeline)),
              color ? "CreateClearRgbPSO" : "CreateClearAlphaPSO");
    return pipeline.Get();
}

bool RendererD3D12::initialize(int window_width, int window_height, unsigned efb_scale) {
    efb_scale_   = std::clamp(efb_scale, 1u, kMaxEfbScale);
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
        if (getenv_s(&len, buf, sizeof(buf), "GALAXY_D3D_DEBUG") == 0 &&
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
        // GALAXY_D3D_DRED=1: device-removed extended data (auto breadcrumbs, page faults) for dump_device_removal.
        {
            char dred_env[8]{};
            std::size_t dred_len = 0;
            if (getenv_s(&dred_len, dred_env, sizeof(dred_env), "GALAXY_D3D_DRED") == 0 && dred_len > 0 &&
                dred_env[0] != '0') {
                ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
                if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
                    dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                    dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                    // The passes' markers (SceneRayTracer::dred_marker) as breadcrumb contexts.
                    ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dred1;
                    if (SUCCEEDED(dred.As(&dred1))) {
                        dred1->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
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
            // TEV records and buffers read straight from the descriptor heap (shader model 6.6, resource binding
            // tier 3), on dedicated video memory.
            D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_6};
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            const bool offered =
                dxr_runtime_supported && options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1 &&
                SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model,
                                                       sizeof(shader_model))) &&
                shader_model.HighestShaderModel >= D3D_SHADER_MODEL_6_6 &&
                SUCCEEDED(device_->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) &&
                options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 && !adapter_is_unified_memory();
            host_window::ray_tracing_offered.store(offered, std::memory_order_relaxed);
            record_render_adapter(adapter_desc.Description, adapter_desc.VendorId, adapter_desc.DeviceId,
                                  adapter_desc.SubSysId, adapter_preference() == DXGI_GPU_PREFERENCE_MINIMUM_POWER,
                                  offered);
            if (offered) {
                rt_prepare::identify(rt_identity(adapter.Get(), adapter_desc));
                host_window::ray_tracing_prepared.store(rt_prepare::prepared(), std::memory_order_relaxed);
            }
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
            msaa_max_samples() > 1u ? "EFB targets multisampled; off with DLSS, FSR 3 or ray tracing"
                                    : "the GPU has no multisampled EFB formats");
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

        if (gpu_timestamps_enabled()) {
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
                return getenv_s(&length, value, sizeof(value), "GALAXY_GPU_PASSES") == 0 && length > 1u &&
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
                return getenv_s(&length, value, sizeof(value), "GALAXY_GPU_DRAWS") == 0 && length > 1u &&
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
        scd.SwapEffect   = flip_discard_enabled()
            ? DXGI_SWAP_EFFECT_FLIP_DISCARD
            : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scd.SampleDesc.Count = 1;
        scd.Scaling      = DXGI_SCALING_STRETCH;
        scd.AlphaMode    = DXGI_ALPHA_MODE_IGNORE;
        // ALLOW_MODE_SWITCH lets exclusive fullscreen switch the monitor to
        // the selected resolution and refresh rate.
        scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                    DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        if (allow_tearing_enabled()) {
            ComPtr<IDXGIFactory5> factory5;
            BOOL supported = FALSE;
            if (SUCCEEDED(factory.As(&factory5)) &&
                SUCCEEDED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                    &supported, sizeof(supported))) &&
                supported) {
                tearing_supported_ = true;
                scd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
            }
        }
        ComPtr<IDXGISwapChain1> sc1;
        // Through Streamline's factory when it runs: its presents go through it.
        d3d_check(streamline::swap_chain_factory(factory.Get())
                      ->CreateSwapChainForHwnd(queue_.Get(), window_, &scd, nullptr, nullptr, &sc1),
                  "CreateSwapChain");
        d3d_check(sc1.As(&swap_chain_), "SwapChain As");
        {
            ComPtr<IDXGISwapChain2> swap_chain2;
            d3d_check(swap_chain_.As(&swap_chain2), "SwapChain2 As");
            d3d_check(swap_chain2->SetMaximumFrameLatency(
                          swap_chain_frame_latency()),
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

        // RTV heap: kSwapChainBuffers backbuffers + 1 EFB slot.
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
        command_list_->SetName(L"GxFrameList");
        d3d_check(command_list_->Close(), "CommandList Close init");

        d3d_check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence");
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event_) throw std::runtime_error("CreateEvent failed");

        // A saved scale above what this display can show is clamped here.
        boot_step("queue and swap chain");
        efb_scale_ = std::min(efb_scale_, host_window::display_efb_scale_cap());
        if (!create_efb_targets(efb_scale_)) return false;

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
            const ComPtr<ID3DBlob> vs = compile_hlsl(blit_vertex_shader_source().data(), blit_vertex_shader_source().size(), "vs_5_1", "main");
            const ComPtr<ID3DBlob> ps = compile_hlsl(blit_pixel_shader_source().data(), blit_pixel_shader_source().size(), "ps_5_1", "main");

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

        efb_copy_heap_.initialize(device_.Get());
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
            if (!ambient_occlusion_.initialize(device_.Get())) {
                std::cerr << "[ambient-occlusion] unavailable\n";
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
        {
            const UINT increment = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            const DescriptorRing::Table views = srv_ring_.allocate_persistent(kRtTextureViews);
            rt_texture_views_ = {views.cpu, srv_ring_.index_of(views.gpu), increment};
            ray_tracer_->set_texture_views(rt_texture_views_.cpu, rt_texture_views_.first, rt_texture_views_.increment);
            const DescriptorRing::Table delight = srv_ring_.allocate_persistent(SceneRayTracer::kDelightViews);
            rt_delight_views_ = {delight.cpu, srv_ring_.index_of(delight.gpu), increment};
            ray_tracer_->set_delight_views(rt_delight_views_.cpu, rt_delight_views_.first, rt_delight_views_.increment);
        }

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

        // Ray tracing (config ray_tracing_enabled, GALAXY_RAY_TRACING=1) where offered.
        reconcile_ray_tracing();
        // The settings menu's pipeline compiles its shaders: here, not inside the frame that first opens it.
        ensure_native_settings_solid_pipeline_loaded();
    } catch (const std::exception& e) {
        std::cerr << "[Renderer] initialize failed: " << e.what() << "\n";
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

gpu::Table RendererD3D12::texture_table_for(
    const TextureHandle* handles, unsigned count, const TextureCache& cache, bool lit_table) {
    DescriptorRing::Table table = srv_ring_.allocate(count);
    D3D12_CPU_DESCRIPTOR_HANDLE sources[kMaxTextureMaps]{};
    const auto heap_start = cache.backend().srv_cpu_base();
    const UINT stride = cache.backend().srv_descriptor_stride();
    for (unsigned m = 0; m < count; ++m) {
        sources[m] = handles[m].resource != nullptr
            ? D3D12_CPU_DESCRIPTOR_HANDLE{heap_start.ptr + static_cast<SIZE_T>(handles[m].srv_index) * stride}
            : cache.backend().null_srv();
    }
    // A lit translucent draw's map 7: the ray tracer's light texture.
    if (lit_table && count > 7u) {
        sources[7] = rt_light_texture_srv();
    }
    // CPU-only sources, one contiguous frame-owned table.
    const UINT destination_size = count;
    device_->CopyDescriptors(1, &table.cpu, &destination_size, count, sources, nullptr,
                             D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // Interpolation logs keep tables that name EFB-copy textures: sub-frames repoint those slots.
    if (replay_recording()) [[unlikely]] {
        static_assert(kMaxTextureMaps == kReplayTableSlots);
        ID3D12Resource* copies[kMaxTextureMaps]{};
        bool names_copy = false;
        for (unsigned m = 0; m < count; ++m) {
            if (handles[m].resource != nullptr && handles[m].from_efb_copy) {
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
    retain_shutdown_failure("EFB diagnostic readback", [&] {
        debug_log_readback();
    });
    retain_shutdown_failure("backbuffer diagnostic readback", [&] {
        debug_log_backbuffer_readback();
    });
    retain_shutdown_failure("selected XFB diagnostic readback", [&] {
        debug_log_selected_xfb_readback();
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
    if (swap_chain_ && exclusive_fullscreen_active_) {
        swap_chain_->SetFullscreenState(FALSE, nullptr);
        exclusive_fullscreen_active_ = false;
        host_window::exclusive_fullscreen_active.store(false, std::memory_order_release);
    }
    s_blit_root_sig.Reset();
    post_process_.shutdown();
    ambient_occlusion_.shutdown();
    game_bloom_.shutdown();
    abandon_ray_tracer_start();
    ray_tracer_->shutdown();
    {
        const PlacedTextureAllocator::Stats heap = efb_copy_heap_.stats();
        std::cerr << "[efb-copy-heap] heaps=" << heap.heaps << " heap-bytes=" << heap.heap_bytes
                  << " used-bytes=" << heap.used_bytes << " placed=" << heap.placed
                  << " committed-fallbacks=" << heap.committed_fallbacks << '\n';
    }
    efb_copy_heap_.shutdown();
    blit_pipeline_.Reset();
    s_conv_root_sig.Reset();
    conv_pipeline_.Reset();
    conv_pipeline_r8_.Reset();
    display_filter_pipeline_.Reset();
    conv_scratch_rtv_slot_.reset();
    conv_rtv_heap_.Reset();
    conv_scratch_rtv_ = {};
    conv_efb_color_srv_ = {};
    conv_efb_depth_srv_ = {};
    conv_efb_source_table_.reset();
    next_fence_value_ = 0;
    peek_command_list_.Reset();
    peek_allocator_.Reset();
    pointer_depth_capture_fence_ = 0;
    peek_target_.Reset();
    pointer_depth_target_.Reset();
    pointer_depth_readback_.Reset();
    for (auto& readback : pointer_depth_ring_) readback.Reset();
    pointer_depth_ring_rgba_.fill(nullptr);
    pointer_depth_pending_rgba_ = nullptr;
    peek_readback_.Reset();
    peek_rtv_heap_.Reset();
    peek_rtv_ = {};
    peek_readback_row_pitch_ = 0;
    s_clear_root_sig.Reset();
    clear_rgb_pipelines_ = {};
    clear_alpha_pipelines_ = {};
    clear_vs_.Reset();
    clear_ps_.Reset();
    timestamp_query_heap_.Reset();
    timestamp_readback_.Reset();
    timestamp_pending_.fill(false);
    timestamp_frequency_ = 0;
    readback_buf_.Reset();
    readback_pending_ = false;
    readback_fence_value_ = 0;
    next_efb_capture_path_.clear();
    readback_capture_path_.clear();
    backbuffer_readback_buf_.Reset();
    backbuffer_readback_pending_ = false;
    backbuffer_readback_fence_value_ = 0;
    backbuffer_readback_width_ = backbuffer_readback_height_ =
        backbuffer_readback_row_pitch_ = 0;
    next_backbuffer_capture_path_.clear();
    backbuffer_readback_capture_path_.clear();
    selected_xfb_readback_buf_.Reset();
    selected_xfb_readback_source_.Reset();
    selected_xfb_readback_pending_ = false;
    selected_xfb_readback_fence_ = 0;
    selected_xfb_readback_width_ = selected_xfb_readback_height_ =
        selected_xfb_readback_pitch_ = 0;
    selected_xfb_readback_path_.clear();
    if (efb_copy_requests_ != 0u) {
        std::cerr << "[efb-copy-work] requests=" << efb_copy_requests_
                  << " shader-copies=" << efb_shader_copies_
                  << " identity-copies=" << efb_identity_copies_
                  << " identity-payload-bytes=" << efb_identity_payload_bytes_ << '\n';
    }
    efb_copy_requests_ = 0u;
    efb_shader_copies_ = 0u;
    efb_identity_copies_ = 0u;
    efb_identity_payload_bytes_ = 0u;
    efb_copy_dests_.clear();
    efb_copy_dests_bytes_ = 0u;
    efb_copy_dests_tick_ = 0u;
    for (auto& retired : retired_efb_copy_textures_) {
        retired.clear();
    }
    for (auto& retired : retired_segment_textures_) {
        retired.clear();
    }
    efb_color_.Reset();
    efb_depth_.Reset();
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
    invalidate_gx_bindings();
    last_latency_wait_us_ = last_frame_slot_wait_us_ =
        last_timestamp_read_us_ = last_begin_reset_us_ = 0;
    telemetry_window_start_qpc_ = last_present_qpc_ = 0;
    present_pacer_.reset();
    telemetry_first_serial_start_ = telemetry_xfb_copy_start_ = 0;
    has_presented_swap_chain_ = window_shown_ = false;
    telemetry_present_count_ = telemetry_adjacent_serial_transition_count_ = 0;
    telemetry_interval_count_ = 0;
    last_presented_xfb_serial_ = ~std::uint64_t{0};
    telemetry_interval_ms_ = telemetry_interval_max_ms_ = 0.0;
    telemetry_present_ms_ = telemetry_present_max_ms_ = 0.0;
    telemetry_pace_wait_ms_ = telemetry_pace_wait_max_ms_ = 0.0;
    telemetry_latency_wait_ms_ = telemetry_latency_wait_max_ms_ = 0.0;
    telemetry_gpu_ms_ = 0.0;
    telemetry_gpu_samples_ = telemetry_no_xfb_present_count_ =
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

bool RendererD3D12::enter_exclusive_fullscreen(
    const std::wstring& monitor_device, unsigned width, unsigned height,
    unsigned refresh_hz) {
    if (!swap_chain_ || !adapter3_) {
        return false;
    }
    ComPtr<IDXGIOutput> output;
    for (UINT index = 0u;; ++index) {
        ComPtr<IDXGIOutput> candidate;
        if (adapter3_->EnumOutputs(index, &candidate) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_OUTPUT_DESC desc{};
        if (candidate && SUCCEEDED(candidate->GetDesc(&desc)) &&
            _wcsicmp(desc.DeviceName, monitor_device.c_str()) == 0) {
            output = candidate;
            break;
        }
    }
    if (!output) {
        // The monitor is driven by another GPU (hybrid laptops): borderless
        // fullscreen on it is the closest equivalent.
        std::cerr << "[Renderer] exclusive fullscreen: monitor not on the rendering GPU;"
                     " using borderless\n";
        return false;
    }
    DXGI_MODE_DESC wanted{};
    wanted.Width = width;
    wanted.Height = height;
    wanted.RefreshRate = {refresh_hz, refresh_hz != 0u ? 1u : 0u};
    wanted.Format = kBackbufferFormat;
    DXGI_MODE_DESC closest{};
    if (width == 0u || height == 0u ||
        FAILED(output->FindClosestMatchingMode(&wanted, &closest, device_.Get()))) {
        DXGI_OUTPUT_DESC desc{};
        (void)output->GetDesc(&desc);
        closest = wanted;
        closest.Width = static_cast<UINT>(desc.DesktopCoordinates.right - desc.DesktopCoordinates.left);
        closest.Height = static_cast<UINT>(desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
        closest.RefreshRate = {0u, 0u};
    }
    wait_for_gpu();
    (void)swap_chain_->ResizeTarget(&closest);
    const HRESULT hr = swap_chain_->SetFullscreenState(TRUE, output.Get());
    if (FAILED(hr)) {
        std::cerr << "[Renderer] exclusive fullscreen unavailable hr=0x" << std::hex
                  << static_cast<unsigned long>(hr) << std::dec << "; using borderless\n";
        return false;
    }
    exclusive_fullscreen_active_ = true;
    host_window::exclusive_fullscreen_active.store(true, std::memory_order_release);
    resize_swap_chain(closest.Width, closest.Height, true);
    std::cout << "[Renderer] exclusive fullscreen " << closest.Width << 'x' << closest.Height
              << " @ " << (closest.RefreshRate.Denominator != 0u
                               ? closest.RefreshRate.Numerator / closest.RefreshRate.Denominator
                               : 0u)
              << " Hz\n";
    return true;
}

void RendererD3D12::leave_exclusive_fullscreen() {
    if (!swap_chain_ || !exclusive_fullscreen_active_) {
        return;
    }
    wait_for_gpu();
    (void)swap_chain_->SetFullscreenState(FALSE, nullptr);
    exclusive_fullscreen_active_ = false;
    host_window::exclusive_fullscreen_active.store(false, std::memory_order_release);
}

void RendererD3D12::reconcile_display_state() {
    if (!swap_chain_ || window_ == nullptr || host_window::hidden_window_mode_enabled()) {
        return;
    }
    const RenderConfig cfg = get_render_config();
    const unsigned mode = std::min(cfg.display_mode, kDisplayExclusive);
    AppliedDisplay& applied = applied_display_;
    const bool same_display = applied.valid && applied.mode == mode &&
        applied.monitor == cfg.monitor &&
        (mode != kDisplayExclusive ||
         (applied.fullscreen_width == cfg.fullscreen_width &&
          applied.fullscreen_height == cfg.fullscreen_height &&
          applied.fullscreen_refresh_hz == cfg.fullscreen_refresh_hz));
    if (same_display) {
        if (mode == kDisplayExclusive) {
            // Alt+Tab leaves exclusive fullscreen; return once the game is
            // in front again. Checked twice a second.
            if (++display_checks_ % 30u == 0u) {
                BOOL fullscreen = FALSE;
                if (SUCCEEDED(swap_chain_->GetFullscreenState(&fullscreen, nullptr)) &&
                    !fullscreen && GetForegroundWindow() == window_) {
                    exclusive_fullscreen_active_ = false;
                    host_window::exclusive_fullscreen_active.store(false, std::memory_order_release);
                    applied.valid = false;
                }
            }
            return;
        }
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
    if (exclusive_fullscreen_active_) {
        leave_exclusive_fullscreen();
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
    bool exclusive = false;
    if (mode == kDisplayExclusive) {
        exclusive = enter_exclusive_fullscreen(monitor->device, cfg.fullscreen_width,
                                               cfg.fullscreen_height, cfg.fullscreen_refresh_hz);
    }
    if (!exclusive) {
        // The placed window's client size (WM_SIZE can lag behind the
        // cross-thread placement).
        RECT client{};
        if (GetClientRect(window_, &client) && client.right > 0 && client.bottom > 0) {
            resize_swap_chain(static_cast<UINT>(client.right),
                              static_cast<UINT>(client.bottom), false);
        }
    }
    applied.valid = true;
    applied.mode = mode;
    applied.monitor = cfg.monitor;
    applied.fullscreen_width = cfg.fullscreen_width;
    applied.fullscreen_height = cfg.fullscreen_height;
    applied.fullscreen_refresh_hz = cfg.fullscreen_refresh_hz;
    applied.window_width = cfg.window_width;
    applied.window_height = cfg.window_height;
    std::cout << "[Renderer] display " << (mode == kDisplayWindowed ? "windowed"
                                           : mode == kDisplayBorderless ? "borderless"
                                                                        : "fullscreen")
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
    const auto tex = galaxy::gx::load_game_texture(
        host_window::native_settings_content_root, "files\\LayoutData\\FileSelect.arc",
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

        const ComPtr<ID3DBlob> vs = compile_hlsl(
            ui_textured_quad_vertex_shader_source().data(), ui_textured_quad_vertex_shader_source().size(), "vs_5_1", "main");
        const ComPtr<ID3DBlob> ps = compile_hlsl(
            ui_textured_quad_pixel_shader_source().data(), ui_textured_quad_pixel_shader_source().size(), "ps_5_1", "main");

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
    invalidate_gx_bindings();
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

        const ComPtr<ID3DBlob> vs = compile_hlsl(
            ui_solid_quad_vertex_shader_source().data(), ui_solid_quad_vertex_shader_source().size(), "vs_5_1", "main");
        const ComPtr<ID3DBlob> ps = compile_hlsl(
            ui_solid_quad_pixel_shader_source().data(), ui_solid_quad_pixel_shader_source().size(), "ps_5_1", "main");

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
    invalidate_gx_bindings();
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
    invalidate_gx_bindings();
}

void RendererD3D12::pump_messages() {
    // The window and its modal loops belong to window_owner_.
}

bool RendererD3D12::quit_requested() const noexcept {
    return host_window::quit_request.load(std::memory_order_acquire);
}

void RendererD3D12::invalidate_gx_bindings() {
    bound_pipeline_ = nullptr;
    bound_vs_constants_ = 0;
    bound_ps_constants_ = 0;
    bound_texture_table_ = {};
    bound_sampler_table_ = {};
    bound_matrix_palette_ = 0;
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
    UploadBuffer staging = gpu::create_upload_memory(device_.Get(), bytes, name);
    if (!staging || !ensure_pre_list()) {
        return staging;
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
        return staging;
    }
    local->SetName(L"GX resident decoded vertices (local)");
    resident_staging_ = staging.buffer;
    resident_local_ = local.Get();
    resident_copies_.clear();
    UploadBuffer resident;
    resident.gpu = local->GetGPUVirtualAddress();
    resident.cpu = staging.cpu;
    resident.buffer = std::move(local);
    return resident;
}

void RendererD3D12::copy_resident_vertices(std::uint64_t offset, std::uint64_t bytes) {
    if (!resident_staging_ || bytes == 0u) {
        return;
    }
    if (!resident_copies_.empty() && resident_copies_.back()[0] + resident_copies_.back()[1] == offset) {
        resident_copies_.back()[1] += bytes;
    } else {
        resident_copies_.push_back({offset, bytes});
    }
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
        const std::string source = ShaderGenerator::curve_sort_source();
        const ComPtr<ID3DBlob> classify = compile_hlsl(source.data(), source.size(), "cs_5_1", "classify");
        const ComPtr<ID3DBlob> offsets = compile_hlsl(source.data(), source.size(), "cs_5_1", "offsets");
        const ComPtr<ID3DBlob> scatter = compile_hlsl(source.data(), source.size(), "cs_5_1", "scatter");
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
        for (const auto& copy : resident_copies_) {
            pre_list_->CopyBufferRegion(resident_local_, copy[0], resident_staging_.Get(), copy[0], copy[1]);
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

void RendererD3D12::begin_frame(bool present_swap_chain) {
    // How long the frames a started tracer took over lasted (the hitch a tracer's first frames could cause).
    {
        const auto now = std::chrono::steady_clock::now();
        // GALAXY_TEST_HITCHES=1: every frame of 50 ms or more, with whether the ray tracer runs (a stall's place).
        static const bool hitches = [] {
            char value[8]{};
            std::size_t length = 0u;
            return getenv_s(&length, value, sizeof(value), "GALAXY_TEST_HITCHES") == 0 && length > 1u && value[0] == '1';
        }();
        if (hitches && frame_begun_.time_since_epoch().count() != 0) {
            const double ms = std::chrono::duration<double, std::milli>(now - frame_begun_).count();
            if (ms >= 50.0) {
                std::cerr << "[hitch] " << ms << " ms, ray tracing " << (ray_tracer_ && ray_tracer_->ready() ? "on" : "off")
                          << '\n';
            }
        }
        if (rt_takeover_frames_ != 0u) {
            std::cerr << "[rt] frame " << (11u - rt_takeover_frames_) << " after taking over: "
                      << std::chrono::duration<double, std::milli>(now - frame_begun_).count() << " ms\n";
            --rt_takeover_frames_;
        }
        frame_begun_ = now;
    }
    // Display and size changes apply here, between command-list recordings.
    // They wait for the GPU themselves, so they come before any per-slot state
    // below is touched.
    host_window::poll_render_config_file();
    reconcile_display_state();
    apply_pending_resize();
    update_gpu_budget();
    follow_window_internal_resolution();
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
    take_started_ray_tracer();
    apply_pending_efb_scale();
    prepare_temporal_upscaler();
    sync_post_process_settings();
    reconcile_ray_tracing();
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
                gpu_budget_ms_ += gpu_ms;
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
                    gpu_budget_ms_ += ms;
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

    ray_tracer_->begin_frame(frame_index_);

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
        // across present, peek and utility frames.
        if (matrix_segment_used_ || !matrix_segment_started_) {
            matrix_segment_ = (matrix_segment_ + 1u) % matrix_segments_;
            ++matrix_segment_serial_;
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
        for (auto& texture : retired_efb_copy_textures_[frame_slot_]) {
            keep.push_back(std::move(texture));
        }
    }
    retired_efb_copy_textures_[frame_slot_].clear();

    ensure_frame_values();
    // Bind both heaps for the entire frame.
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
    command_list_->SetDescriptorHeaps(2, heaps);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    if (efb_carry_color_ || efb_carry_depth_) {
        carry_over_efb_contents();
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
    if (frame_latency_wait_enabled() && frame_latency_waitable_ != nullptr) {
        const auto latency_wait_start_steady = collect_present_telemetry
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        constexpr DWORD kFrameLatencyWaitTimeoutMs = 17;
        const DWORD latency_result =
            WaitForSingleObjectEx(
                frame_latency_waitable_,
                kFrameLatencyWaitTimeoutMs,
                FALSE);
        if (latency_result != WAIT_OBJECT_0 &&
            latency_result != WAIT_TIMEOUT) {
            throw std::runtime_error("frame latency wait failed");
        }
        if (collect_present_telemetry) {
            last_latency_wait_us_ = elapsed_us(
                latency_wait_start_steady,
                std::chrono::steady_clock::now());
        }
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
        (vsync == 0 && tearing_supported_ && !exclusive_fullscreen_active_) ? DXGI_PRESENT_ALLOW_TEARING
                                           : 0u;
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
            telemetry_first_serial_start_ = xfb_present_stats().first_serial_presentations;
            telemetry_xfb_copy_start_ = xfb_copy_count();
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
            const auto progress = xfb_present_stats();
            const auto produced_copies = xfb_copy_count();
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
                << " xfb-production-hz="
                << static_cast<double>(produced_copies - telemetry_xfb_copy_start_) / elapsed_seconds
                << " first-serial-total=" << progress.first_serial_presentations
                << " xfb-copy-total=" << produced_copies
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
                << " tearing-enabled=" << (tearing_supported_ ? 1 : 0)
                << " remote-session="
                << (GetSystemMetrics(SM_REMOTESESSION) != 0 ? 1 : 0)
                << " gpu-timing=" << (!gpu_timestamps_enabled() ? "disabled" :
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
                static constexpr const char* kPassNames[] = {"scene", "resolve", "efb-copy", "xfb-copy", "bloom",
                                                             "scene-aa", "scene-ao", "upscale", "rt", "present",
                                                             "curve-sort"};
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
                << " efb-handovers=" << efb_handovers_ << '/' << efb_handovers_free_ << '/' << efb_handovers_banded_
                << '/' << efb_handovers_restored_
                << " no-xfb-presents="
                << telemetry_no_xfb_present_count_
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
            telemetry_xfb_copy_start_ = produced_copies;
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
            telemetry_no_xfb_present_count_ = 0;
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
    // Ray tracing: this chunk's traced draws read upload rings the next frame begin rewinds.
    if (ray_tracer_->scene_open()) {
        ray_tracer_->flush_geometry(command_list_.Get(), rt_sources(), &RendererD3D12::rt_allocate, this);
        bound_pipeline_ = nullptr;
    }
    if (ray_tracer_->water_open()) {
        ray_tracer_->flush_water(command_list_.Get(), rt_sources(), &RendererD3D12::rt_allocate, this);
        bound_pipeline_ = nullptr;
    }
    if (ray_tracer_->late_open()) {
        ray_tracer_->flush_late(command_list_.Get(), rt_sources(), &RendererD3D12::rt_allocate, this);
        bound_pipeline_ = nullptr;
    }
    if (ray_tracer_->sky_open()) {
        ray_tracer_->flush_sky(command_list_.Get(), rt_sources(), &RendererD3D12::rt_allocate, this);
        bound_pipeline_ = nullptr;
    }
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
    ray_tracer_->note_submission(submitted_fence);
    if (ring_mode_ == RingMode::Interp) {
        ring_segment_fence_[ring_segment_] = submitted_fence;
    } else if (matrix_segments_ != kFramesInFlight) {
        matrix_segment_used_ = matrix_ring_.cursor() != matrix_begin_cursor_;
        matrix_segment_fence_[matrix_segment_] = submitted_fence;
    }
    if (pointer_depth_in_frame_pending_) {
        pointer_depth_capture_fence_ = submitted_fence;
        pointer_depth_in_frame_pending_ = false;
    }
    for (AsyncPeekField& field : async_peek_fields_) {
        if (field.fence == kAsyncPeekInFrame) {
            field.fence = submitted_fence;
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
    if (selected_xfb_readback_pending_ && selected_xfb_readback_fence_ == 0u) {
        selected_xfb_readback_fence_ = submitted_fence;
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
            "device removed while polling frame PE completion");
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
            "device removed before frame-specific GPU PE completion");
    }
    if (completed_before >= fence_value) {
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(kFramePeCompletionTimeoutMs);
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
                "frame-specific GPU PE completion wait timed out");
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
            "frame-specific GPU PE completion wait failed: result=" +
            std::to_string(wait_result) +
            " win32=" + std::to_string(wait_error));
    }
}

bool RendererD3D12::temporal_mode() const {
    const unsigned upscaler = get_render_config().upscaler;
    return (upscaler == 2u && fsr3_available()) || (upscaler == 3u && streamline::dlss_supported());
}

unsigned RendererD3D12::scene_display_scale() const {
    if (temporal_ != nullptr && temporal_mode() && temporal_output_w_ > kEfbWidth * efb_scale_) {
        return temporal_output_w_ / kEfbWidth;
    }
    return efb_scale_;
}

bool RendererD3D12::ensure_motion_target() {
    if (motion_target_ && motion_target_scale_ == efb_scale_) {
        return true;
    }
    if (motion_target_) {
        // A render-scale change inside a scene: this frame's command list
        // may still name them.
        retired_efb_copy_textures_[frame_slot_].push_back(std::move(motion_target_));
        retired_efb_copy_textures_[frame_slot_].push_back(std::move(reactive_target_));
        retired_efb_copy_textures_[frame_slot_].push_back(std::move(layer_color_));
        retired_efb_copy_textures_[frame_slot_].push_back(std::move(layer_trans_));
    }
    motion_target_.Reset();
    reactive_target_.Reset();
    layer_color_.Reset();
    layer_trans_.Reset();
    layer_window_ = false;
    motion_target_scale_ = 0u;
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
    rd.Width = kEfbWidth * efb_scale_;
    rd.Height = kEfbHeight * efb_scale_;
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
    layer_color_->SetName(L"GX effects layer colour");
    layer_trans_->SetName(L"GX effects layer transmittance");
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
    motion_target_scale_ = efb_scale_;
    return true;
}

static constexpr char kMotionDebugPs[] =
    "Texture2D<float2> mv:register(t0);"
    "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
    "float4 main(V v):SV_Target{"
    " float2 m=mv.Load(int3(int2(v.p.xy),0));"
    " return float4(saturate(0.5+m.x/16.0),saturate(0.5+m.y/16.0),0.5,1.0);}";

void RendererD3D12::record_motion_debug_view() {
    settle_owed_efb();
    // Its pipeline draws to the RGBA8 EFB.
    if (!motion_target_ || efb_hdr_) {
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
        const ComPtr<ID3DBlob> vs = compile_hlsl(blit_vertex_shader_source().data(), blit_vertex_shader_source().size(), "vs_5_1", "main");
        const ComPtr<ID3DBlob> ps =
            compile_hlsl(kMotionDebugPs, sizeof(kMotionDebugPs) - 1, "ps_5_1", "main");
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
        pd.RTVFormats[0] = kEfbColorFormat;
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
    command_list_->OMSetRenderTargets(1, &efb_rtv_, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    const D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(kEfbWidth * efb_scale_),
                            static_cast<float>(kEfbHeight * efb_scale_), 0.0f, 1.0f};
    const D3D12_RECT sci{0, 0, static_cast<LONG>(kEfbWidth * efb_scale_),
                         static_cast<LONG>(kEfbHeight * efb_scale_)};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sci);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bind_efb(nullptr);
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
    key.efb_scale = output_efb_active_ ? alt_efb_.scale : efb_scale_;  // the render EFB's
    key.output_scale = upscale_output_scale_;
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
    std::swap(replay_viewports_, play_viewports_);
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
    std::swap(play_viewports_, run_viewports_);
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
    blend_verdicts_->SetName(L"GX vertex blend verdicts and frame values");
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
        const ComPtr<ID3DBlob> cs =
            compile_hlsl(kBlendValidateCs, sizeof(kBlendValidateCs) - 1u, "cs_5_1", "main");
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
            const ComPtr<ID3DBlob> cs = compile_hlsl(kStripeGatherCs, sizeof(kStripeGatherCs) - 1u, "cs_5_1", "main");
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
    stripe_earlier_->SetName(L"GX stripe particle earlier vertices");
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

// A tracer started on a worker takes over at a frame's start (before the EFB follows it: no MSAA under ray tracing),
// with the inputs the scene gave the one it replaces; one the setting no longer wants, or wants at another quality,
// is dropped unused. One whose buffers were made for an EFB size the window or the INTERNAL RESOLUTION row has since
// changed (the start's first frames: following the window, 6x became 4x) goes back to the worker to make them for the
// current size, so the frame that takes it over still allocates nothing.
void RendererD3D12::take_started_ray_tracer() {
    if (!rt_starting_.valid() || rt_starting_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    std::unique_ptr<SceneRayTracer> started = rt_starting_.get();
    const D3D12_RESOURCE_DESC efb = efb_depth_ ? efb_depth_->GetDesc() : D3D12_RESOURCE_DESC{};
    const auto efb_width = static_cast<std::uint32_t>(efb.Width);
    const auto efb_height = static_cast<std::uint32_t>(efb.Height);
    if (started != nullptr && efb_width != 0u && !started->prepared_for(efb_width, efb_height)) {
        rt_starting_ = std::async(std::launch::async, [tracer = std::move(started), efb_width, efb_height]() mutable {
            galaxy::host::name_current_thread(L"Nebula RT Start");
            tracer->prepare_buffers(efb_width, efb_height);
            return std::move(tracer);
        });
        return;
    }
    rt_start_failed_ = started == nullptr;
    std::cerr << "[rt] started in "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rt_start_time_).count()
              << " ms on a worker" << (rt_start_failed_ ? " (failed)" : "") << '\n';
    const RenderConfig config = get_render_config();
    if (started != nullptr && config.ray_tracing_enabled &&
        host_window::ray_tracing_offered.load(std::memory_order_relaxed) &&
        started->quality() == std::min(config.rt_quality, 2u)) {
        started->adopt_inputs(*ray_tracer_);
        ray_tracer_ = std::move(started);
        rt_takeover_frames_ = 10u;
    }
}

void RendererD3D12::reconcile_ray_tracing() {
    const bool offered = host_window::ray_tracing_offered.load(std::memory_order_relaxed);
    const bool wanted = get_render_config().ray_tracing_enabled && offered;
    // REFLECTIONS switches live, at a frame's start.
    ray_tracer_->set_surface_reflections(rt_effect_on(get_render_config(), RtEffect::Reflections));
    if (rt_starting_.valid()) {
        return;
    }
    // The first call (initialize): Setup's request prepares the pipeline library with RAY TRACING off too (that tracer
    // is dropped once it is done); a request the GPU or the library leaves nothing to do for is withdrawn.
    const bool first = !rt_first_reconciled_;
    rt_first_reconciled_ = true;
    if (first && rt_prepare::preparation_requested()) {
        if (!offered || rt_prepare::prepared()) {
            rt_prepare::withdraw_request();
        } else if (!wanted && device_) {
            start_ray_tracer(true);
            return;
        }
    }
    // A quality change restarts the tracer (its rays, sample grid and pipelines are chosen when it starts): off this
    // frame, on again the next, as the setting's off and on.
    const bool requality =
        wanted && ray_tracer_->ready() && ray_tracer_->quality() != std::min(get_render_config().rt_quality, 2u);
    if (!requality && (wanted == ray_tracer_->ready() || (wanted && rt_start_failed_) || !device_)) {
        return;
    }
    if (!wanted || requality) {
        // Recorded sub-frame replays composite the tracer's results: none may run again. A scene left open in the
        // HDR scene EFB gives the EFB back as it was.
        if (efb_hdr_) {
            swap_efb_with(hdr_scene_efb_);
        }
        wait_for_gpu();
        for (ReplayList& entry : replay_lists_) {
            entry.sequence = 0u;
        }
        ray_tracer_->shutdown();
        std::cerr << (requality ? "[rt] quality changed: restarting\n" : "[rt] off\n");
        return;
    }
    // A start in the game compiles nothing: while the library is not prepared, ray tracing waits for the next start,
    // which prepares it before the title (the settings ask to restart).
    if (!first && !rt_prepare::prepared()) {
        if (!rt_waiting_restart_) {
            rt_waiting_restart_ = true;
            std::cerr << "[rt] waits for the next start: its pipelines are not prepared\n";
        }
        return;
    }
    start_ray_tracer(first);
}

// Starting a tracer builds its pipelines (from the ray tracing pipeline library, rt_prepare.h) and buffers: on a
// worker, so turning RAY TRACING on never holds the window; the frames draw without ray tracing until
// reconcile_ray_tracing takes it over. The device is free-threaded, and the worker touches only its own tracer. A start
// whose library is not prepared compiles every pipeline of every quality into it; the `first` (the renderer's, before
// the game starts) tells the host, which holds the game in the preparation window meanwhile.
void RendererD3D12::start_ray_tracer(bool first) {
    char value[8]{};
    std::size_t length = 0u;
    const bool validate =
        getenv_s(&length, value, sizeof(value), "GALAXY_RT_VALIDATE") == 0 && length > 1u && value[0] == '1';
    rt_start_time_ = std::chrono::steady_clock::now();
    // The EFB the tracer will trace (trace_scene's size): its buffers are made on the worker too.
    const D3D12_RESOURCE_DESC efb = efb_depth_ ? efb_depth_->GetDesc() : D3D12_RESOURCE_DESC{};
    const auto efb_width = static_cast<std::uint32_t>(efb.Width);
    const auto efb_height = static_cast<std::uint32_t>(efb.Height);
    if (first) {
        rt_prepare::set_first_start(rt_prepare::FirstStart::Opening);
    }
    rt_starting_ = std::async(
        std::launch::async,
        [device = device_, frequency = timestamp_frequency_, validate, views = rt_texture_views_,
         delight = rt_delight_views_, efb_width, efb_height, first]() -> std::unique_ptr<SceneRayTracer> {
            galaxy::host::name_current_thread(L"Nebula RT Start");
            // However the start ends, the host's wait for it does.
            const galaxy::ScopeExit first_done([first]() noexcept {
                if (first) rt_prepare::set_first_start(rt_prepare::FirstStart::None);
            });
            rt_prepare::open(device.Get());
            const bool preparing = !rt_prepare::prepared();
            rt_prepare::reset();
            if (first) {
                rt_prepare::set_first_start(preparing ? rt_prepare::FirstStart::Compiling : rt_prepare::FirstStart::None);
            }
            std::unique_ptr<SceneRayTracer> started;
            try {
                auto tracer = std::make_unique<SceneRayTracer>();
                tracer->set_texture_views(views.cpu, views.first, views.increment);
                tracer->set_delight_views(delight.cpu, delight.first, delight.increment);
                if (tracer->initialize(device.Get(), frequency, validate, kEfbHdrColorFormat, kEfbColorFormat)) {
                    tracer->prepare_buffers(efb_width, efb_height);
                    started = std::move(tracer);
                }
            } catch (const std::exception& e) {
                std::cerr << "[rt] start failed: " << e.what() << '\n';
            }
            const bool complete = started != nullptr && started->complete();
            rt_prepare::finish_start(preparing && complete);
            host_window::ray_tracing_prepared.store(rt_prepare::prepared(), std::memory_order_relaxed);
            if (complete) {
                rt_prepare::withdraw_request();
            }
            std::cerr << "[rt-prepare] start " << (complete ? "complete" : "incomplete") << ", "
                      << rt_prepare::compiled() << " pipelines compiled\n";
            return started;
        });
}

void RendererD3D12::abandon_ray_tracer_start() noexcept {
    if (rt_starting_.valid()) {
        rt_starting_.wait();
        rt_starting_ = {};
    }
}

RtSources RendererD3D12::rt_sources() const {
    RtSources sources{};
    sources.vertex_ring = {vertex_ring_.gpu_base(), vertex_ring_.total_bytes()};
    sources.resident = {resident_vertex_base_, resident_vertex_bytes_};
    sources.index_ring = {index_ring_.gpu_base(), index_ring_.total_bytes()};
    sources.constant_ring = {constant_ring_.gpu_base(), constant_ring_.total_bytes()};
    sources.matrix_ring = {matrix_ring_.gpu_base(), matrix_ring_.total_bytes()};
    return sources;
}

RtUpload RendererD3D12::rt_allocate(void* renderer, std::size_t bytes, std::size_t alignment) {
    const UploadRing::Allocation allocation =
        static_cast<RendererD3D12*>(renderer)->constant_ring_.allocate(bytes, alignment);
    return {allocation.cpu, allocation.gpu,
            static_cast<ID3D12Resource*>(static_cast<RendererD3D12*>(renderer)->constant_ring_.resource()),
            allocation.offset};
}

// A traced draw's buffers: vertices in the frame ring or the resident decoded-vertex buffer,
// R16/R32 indices in the index ring, VS constants in the constant ring and its palette in the
// matrix ring. Draws whose data lives elsewhere are not traced.
void RendererD3D12::rt_note_draw(const DrawCall& call, const RtDrawInfo& info) {
    // A curved surface's patches are its triangle list (the traced scene takes the flat triangles).
    const bool triangles = call.topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST ||
                           call.topology == D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
    if ((info.surface & kRtDrawWater) != 0u && rt_log_glows()) [[unlikely]] {
        static std::uint32_t logged = 0u;
        const std::uint32_t reason = !ray_tracer_->accepts(info) ? 1u : call.index_count < 3u ? 2u
                                     : !triangles ? 4u
                                     : call.vertex_view.StrideInBytes != kRtVertexStride ? 8u
                                     : call.vertex_view.SizeInBytes == 0u ? 16u : 32u;
        if ((logged & reason) == 0u) {
            logged |= reason;
            std::fprintf(stderr, "[rt-water-note] material=%u reason=%u topology=%d stride=%u indices=%u\n",
                         static_cast<unsigned>(info.rt_material), reason, static_cast<int>(call.topology),
                         call.vertex_view.StrideInBytes, call.index_count);
        }
    }
    if (!ray_tracer_->accepts(info) || call.index_count < 3u || !triangles ||
        call.vertex_view.StrideInBytes != kRtVertexStride || call.vertex_view.SizeInBytes == 0u) {
        return;
    }
    const auto offset_in = [](D3D12_GPU_VIRTUAL_ADDRESS va, std::uint64_t bytes, D3D12_GPU_VIRTUAL_ADDRESS base,
                              std::uint64_t size, std::uint32_t& offset) {
        if (base == 0u || va < base || va + bytes > base + size || va - base > 0xFFFFFFFFu) {
            return false;
        }
        offset = static_cast<std::uint32_t>(va - base);
        return true;
    };
    RtDraw draw{};
    draw.flags = info.surface;
    const D3D12_GPU_VIRTUAL_ADDRESS vertices = call.vertex_view.BufferLocation;
    if (!offset_in(vertices, call.vertex_view.SizeInBytes, vertex_ring_.gpu_base(), vertex_ring_.total_bytes(),
                   draw.vertex_offset)) {
        if (!offset_in(vertices, call.vertex_view.SizeInBytes, resident_vertex_base_, resident_vertex_bytes_,
                       draw.vertex_offset)) {
            return;
        }
        draw.flags |= kRtDrawResident;
    }
    draw.vertex_count = std::min<std::uint32_t>(call.vertex_view.SizeInBytes / call.vertex_view.StrideInBytes,
                                                kRtVertexCountMask) |
                        (info.texgen < 8u ? (info.texgen + 1u) << kRtTexGenShift : 0u) |
                        (info.texgen2 < 8u ? (info.texgen2 + 1u) << kRtTexGen2Shift : 0u);
    if (call.index_view.BufferLocation != 0u) {
        if (!offset_in(call.index_view.BufferLocation, call.index_view.SizeInBytes, index_ring_.gpu_base(),
                       index_ring_.total_bytes(), draw.index_offset)) {
            return;
        }
        draw.flags |= kRtDrawIndexed;
        if (call.index_view.Format == DXGI_FORMAT_R32_UINT) {
            draw.flags |= kRtDrawWideIndices;
        }
    }
    if (!offset_in(call.vs_constants, sizeof(GxVsConstants), constant_ring_.gpu_base(), constant_ring_.total_bytes(),
                   draw.constants_offset)) {
        return;
    }
    if (call.matrix_palette != 0u &&
        !offset_in(call.matrix_palette, 16u, matrix_ring_.gpu_base(), matrix_ring_.total_bytes(),
                   draw.palette_offset)) {
        return;
    }
    draw.triangle_count = call.index_count / 3u;
    ray_tracer_->add_draw(draw, info);
}

void RendererD3D12::rt_begin_scene() {
    // The scene's texture views take the block of the scene kTextureViewScenes back: its GPU work completes first
    // (submitted game frames ago, normally long done).
    const std::uint64_t views = ray_tracer_->texture_views_fence();
    if (views != 0u && views != ~0ull) {
        wait_for_fence_value(views);
    }
    ray_tracer_->begin_scene();
}

void RendererD3D12::rt_trace_scene() {
    settle_owed_efb();
    const GpuPassScope pass{*this, GpuPass::RayTracing};
    if (!ray_tracer_->scene_open()) {
        return;
    }
    const bool depth = efb_depth_ && conv_efb_depth_srv_.ptr != 0u;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = efb_depth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    if (depth) {
        command_list_->ResourceBarrier(1, &barrier);
    }
    const D3D12_RESOURCE_DESC desc = depth ? efb_depth_->GetDesc() : D3D12_RESOURCE_DESC{};
    const auto width = static_cast<std::uint32_t>(desc.Width);
    // The engine's G-buffer reads the EFB's colour (the texels' albedo) during the trace.
    const bool picture = depth && efb_color_ && ray_tracer_->engine_active();
    D3D12_RESOURCE_BARRIER picture_barrier = barrier;
    picture_barrier.Transition.pResource = efb_color_.Get();
    picture_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    picture_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (picture) {
        command_list_->ResourceBarrier(1, &picture_barrier);
    }
    ray_tracer_->set_engine_picture(picture ? efb_color_.Get() : nullptr);
    // And the raster's motion (every opaque draw of the scene writes its own, scene_motion_): the engine's histories
    // follow what moved by itself.
    const bool motion = picture && scene_motion_ && motion_target_ && motion_target_scale_ == efb_scale_;
    D3D12_RESOURCE_BARRIER motion_barrier = picture_barrier;
    motion_barrier.Transition.pResource = motion_target_.Get();
    if (motion) {
        command_list_->ResourceBarrier(1, &motion_barrier);
    }
    ray_tracer_->set_engine_motion(motion ? motion_target_.Get() : nullptr);
    static_cast<void>(ray_tracer_->trace_scene(command_list_.Get(), rt_sources(), &RendererD3D12::rt_allocate, this,
                                              depth ? conv_efb_depth_srv_ : D3D12_GPU_DESCRIPTOR_HANDLE{}, width,
                                              desc.Height));
    ray_tracer_->set_engine_picture(nullptr);
    ray_tracer_->set_engine_motion(nullptr);
    if (picture) {
        std::swap(picture_barrier.Transition.StateBefore, picture_barrier.Transition.StateAfter);
        command_list_->ResourceBarrier(1, &picture_barrier);
    }
    if (motion) {
        std::swap(motion_barrier.Transition.StateBefore, motion_barrier.Transition.StateAfter);
        command_list_->ResourceBarrier(1, &motion_barrier);
    }
    // The occlusion multiplies the opaque scene before the translucent phases draw; a recorded
    // log applies it again on each sub-frame's replay.
    const int slot = ray_tracer_->light_slot();
    if (depth && slot >= 0 && ray_tracer_->surface_reflections_active() && efb_color_ &&
        conv_efb_color_srv_.ptr != 0u) {
        // Reflected light is the opaque scene's colour (the conversion views: colour, then depth).
        D3D12_RESOURCE_BARRIER color = barrier;
        color.Transition.pResource = efb_color_.Get();
        color.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        color.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        command_list_->ResourceBarrier(1, &color);
        ray_tracer_->trace_reflections(command_list_.Get(), &RendererD3D12::rt_allocate, this, conv_efb_color_srv_,
                                      width, desc.Height);
        std::swap(color.Transition.StateBefore, color.Transition.StateAfter);
        command_list_->ResourceBarrier(1, &color);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE game_srv{};
    log_rt_seq("trace");
    if (depth && slot >= 0 && efb_hdr_) {
        game_srv = rt_composite_color();
        ray_tracer_->composite(command_list_.Get(), efb_rtv_, conv_efb_depth_srv_, game_srv, width,
                              desc.Height, slot, false, efb_scale_);
        ray_tracer_->write_light_texture(command_list_.Get(), slot);
        if (replay_recording_) {
            replay_ops_.push_back({ReplayKind::RtComposite, static_cast<std::uint32_t>(slot)});
        }
    }
    if (depth) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        command_list_->ResourceBarrier(1, &barrier);
    }
    // The frame before the translucent phases, for the water composite (the seabed under the water).
    rt_opaque_srv_ = depth && slot >= 0 ? rt_copy_color(rt_opaque_copy_, L"rt opaque scene")
                                        : D3D12_GPU_DESCRIPTOR_HANDLE{};
    // The picture before the translucent phases: what the glows add to it measures their light.
    if (depth && ray_tracer_->glow_open() && efb_color_ && conv_efb_color_srv_.ptr != 0u) {
        D3D12_RESOURCE_BARRIER views[2]{};
        for (D3D12_RESOURCE_BARRIER& view : views) {
            view.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            view.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            view.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        }
        views[0].Transition.pResource = efb_color_.Get();
        views[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        views[1].Transition.pResource = efb_depth_.Get();
        views[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        command_list_->ResourceBarrier(2, views);
        ray_tracer_->snapshot_glow_base(command_list_.Get(), conv_efb_color_srv_, game_srv, width, desc.Height);
        for (D3D12_RESOURCE_BARRIER& view : views) {
            std::swap(view.Transition.StateBefore, view.Transition.StateAfter);
        }
        command_list_->ResourceBarrier(2, views);
    }
    // The compute and composite pipelines replaced the GX bindings.
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bound_rt_aux_ = 0xFFu;
}

void RendererD3D12::rt_capture_picture() {
    settle_owed_efb();
    if (!ray_tracer_->lighting_active() || !efb_color_ || !efb_depth_ || conv_efb_color_srv_.ptr == 0u) {
        return;
    }
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    barriers[0].Transition.pResource = efb_color_.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.pResource = efb_depth_.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    command_list_->ResourceBarrier(2, barriers);
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
    // The conversion views hold the EFB colour, then its depth.
    ray_tracer_->capture_picture(command_list_.Get(), rt_sources(), &RendererD3D12::rt_allocate, this,
                                conv_efb_color_srv_, static_cast<std::uint32_t>(desc.Width), desc.Height,
                                rt_game_picture());
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    command_list_->ResourceBarrier(2, barriers);
    bound_pipeline_ = nullptr;
    // The camera turns the HDR scene EFB's light into the EFB's picture.
    end_hdr_scene();
}

void RendererD3D12::begin_hdr_scene() {
    settle_owed_efb();
    if (efb_hdr_ || output_efb_active_ || efb_msaa_.samples > 1u || !efb_color_ || !efb_depth_ ||
        conv_efb_color_srv_.ptr == 0u || !ray_tracer_->ready() || !ray_tracer_->camera_ready()) {
        return;
    }
    if (replay_recording_) {
        replay_ops_.push_back({ReplayKind::HdrBegin, 0u});
    }
    record_hdr_begin();
}

void RendererD3D12::end_hdr_scene() {
    if (!efb_hdr_) {
        return;
    }
    // The camera takes this frame's light slot, here and in its sub-frames' replay (the tracer has moved on by then).
    const int slot = ray_tracer_->light_slot();
    if (replay_recording_) {
        replay_ops_.push_back({ReplayKind::HdrEnd, static_cast<std::uint32_t>(slot + 1)});
    }
    record_hdr_end(slot);
}

// GALAXY_RT_LOG_SEQ=n: logs the ray tracing frame's steps and the EFB colour each meets, renderer frames n to n + 12.
void RendererD3D12::log_rt_seq(const char* step) {
    static const std::uint64_t first = [] {
        char value[24]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value), "GALAXY_RT_LOG_SEQ") == 0 && length > 1u
                   ? std::strtoull(value, nullptr, 10)
                   : 0ull;
    }();
    ray_tracer_->set_log_seq(first != 0u && frame_index_ >= first && frame_index_ <= first + 12u);
    if (ray_tracer_->log_seq()) {
        std::fprintf(stderr, "[rt-seq] frame=%llu %s efb=%p rtv=%llx hdr=%d replay=%d/%d output=%d\n",
                     static_cast<unsigned long long>(frame_index_), step, static_cast<void*>(efb_color_.Get()),
                     static_cast<unsigned long long>(efb_rtv_.ptr), efb_hdr_ ? 1 : 0, replay_recording_ ? 1 : 0,
                     replay_playing_ ? 1 : 0, output_efb_active_ ? 1 : 0);
    }
}

// The HDR scene EFB stands in: the EFB's colour so far widened into it (a draw: the formats differ), its depth copied;
// then the two swap (the set keeps the RGBA8 EFB meanwhile). Sub-frames use their own set (a later frame's scene may
// be open while they play).
void RendererD3D12::record_hdr_begin() {
    settle_owed_efb();
    log_rt_seq("hdr-begin");
    if (efb_hdr_) {
        return;
    }
    EfbTargetSet& set = replay_playing_ ? replay_hdr_efb_ : hdr_scene_efb_;
    if (!set.color || !set.hdr || set.scale != efb_scale_) {
        if (!create_efb_set(set, replay_playing_ ? replay_hdr_rtv_heap_ : hdr_scene_rtv_heap_,
                            replay_playing_ ? replay_hdr_dsv_heap_ : hdr_scene_dsv_heap_, efb_scale_, true, false, 1u,
                            true)) {
            static bool reported = false;
            if (!std::exchange(reported, true)) {
                std::cerr << "[rt] HDR scene EFB allocation failed: the scene stays in the RGBA8 EFB\n";
            }
            return;
        }
    }
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
    D3D12_RESOURCE_BARRIER barriers[3]{};
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    barriers[0].Transition.pResource = efb_color_.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.pResource = efb_depth_.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[2].Transition.pResource = set.depth.Get();
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    command_list_->ResourceBarrier(3u, barriers);
    const bool widened = ray_tracer_->camera_widen(command_list_.Get(), conv_efb_color_srv_, set.rtv,
                                                  static_cast<std::uint32_t>(desc.Width), desc.Height);
    command_list_->CopyResource(set.depth.Get(), efb_depth_.Get());
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    command_list_->ResourceBarrier(3u, barriers);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    bound_pipeline_ = nullptr;
    bound_rt_aux_ = 0xFFu;
    if (!widened) {
        invalidate_gx_bindings();
        return;
    }
    swap_efb_with(set);
}

// The camera writes the HDR scene EFB's picture into the RGBA8 EFB (held by the set), its depth is copied back, and
// the two swap again.
void RendererD3D12::record_hdr_end(int slot) {
    settle_owed_efb();
    log_rt_seq("hdr-end");
    if (!efb_hdr_) {
        return;
    }
    EfbTargetSet& set = replay_playing_ ? replay_hdr_efb_ : hdr_scene_efb_;
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
    const auto width = static_cast<std::uint32_t>(desc.Width);
    constexpr D3D12_RESOURCE_STATES kRead =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER barriers[3]{};
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    barriers[0].Transition.pResource = efb_color_.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = kRead;
    barriers[1].Transition.pResource = efb_depth_.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[2].Transition.pResource = set.depth.Get();
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    command_list_->ResourceBarrier(3u, barriers);
    ray_tracer_->camera_finish(command_list_.Get(), conv_efb_color_srv_, set.rtv, width, desc.Height, slot,
                               replay_playing_);
    command_list_->CopyResource(set.depth.Get(), efb_depth_.Get());
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    command_list_->ResourceBarrier(3u, barriers);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    bound_pipeline_ = nullptr;
    bound_rt_aux_ = 0xFFu;
    swap_efb_with(set);
}

D3D12_GPU_DESCRIPTOR_HANDLE RendererD3D12::rt_composite_color() {
    return rt_copy_color(rt_color_copy_, L"rt composite colour");
}

// A copy of the EFB colour (render target state before and after) in `copy`, made or remade at its
// size; its SRV (ALL_SHADER_RESOURCE).
D3D12_GPU_DESCRIPTOR_HANDLE RendererD3D12::rt_copy_color(Microsoft::WRL::ComPtr<ID3D12Resource>& target,
                                                         const wchar_t* name) {
    settle_owed_efb();
    if (!efb_color_) {
        return {};
    }
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
    if (!target || target->GetDesc().Width != desc.Width || target->GetDesc().Height != desc.Height ||
        target->GetDesc().Format != desc.Format) {
        if (target) {
            wait_for_gpu();
        }
        target.Reset();
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC copy = desc;
        copy.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &copy,
                                                    D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, nullptr,
                                                    IID_PPV_ARGS(&target)))) {
            return {};
        }
        target->SetName(name);
    }
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    barriers[0].Transition.pResource = efb_color_.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    // Read by the composite (pixel) and the exposure and capture passes (compute).
    barriers[1].Transition.pResource = target.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    command_list_->ResourceBarrier(2u, barriers);
    command_list_->CopyResource(target.Get(), efb_color_.Get());
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    command_list_->ResourceBarrier(2u, barriers);
    const DescriptorRing::Table table = srv_ring_.allocate(1);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM : desc.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    device_->CreateShaderResourceView(target.Get(), &view, table.cpu);
    return table.gpu;
}

D3D12_GPU_DESCRIPTOR_HANDLE RendererD3D12::rt_layer_table() {
    settle_owed_efb();
    if (!layer_color_ || !layer_trans_ || !efb_color_) {
        return {};
    }
    const D3D12_RESOURCE_DESC desc = layer_color_->GetDesc();
    const D3D12_RESOURCE_DESC efb = efb_color_->GetDesc();
    if (desc.Width != efb.Width || desc.Height != efb.Height) {
        return {};
    }
    const auto ensure_copy = [&](Microsoft::WRL::ComPtr<ID3D12Resource>& copy, const wchar_t* name) {
        if (copy && copy->GetDesc().Width == desc.Width && copy->GetDesc().Height == desc.Height) {
            return true;
        }
        if (copy) {
            wait_for_gpu();
        }
        copy.Reset();
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = desc;
        rd.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                                    IID_PPV_ARGS(&copy)))) {
            return false;
        }
        copy->SetName(name);
        return true;
    };
    if (!ensure_copy(rt_layer_color_copy_, L"rt effects layer colour") ||
        !ensure_copy(rt_layer_trans_copy_, L"rt effects layer transmittance")) {
        return {};
    }
    D3D12_RESOURCE_BARRIER barriers[4]{};
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    ID3D12Resource* const sources[2]{layer_color_.Get(), layer_trans_.Get()};
    ID3D12Resource* const copies[2]{rt_layer_color_copy_.Get(), rt_layer_trans_copy_.Get()};
    for (unsigned k = 0; k < 2u; ++k) {
        barriers[k].Transition.pResource = sources[k];
        barriers[k].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[k].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[2u + k].Transition.pResource = copies[k];
        barriers[2u + k].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[2u + k].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    }
    command_list_->ResourceBarrier(4u, barriers);
    command_list_->CopyResource(copies[0], sources[0]);
    command_list_->CopyResource(copies[1], sources[1]);
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    command_list_->ResourceBarrier(4u, barriers);
    const DescriptorRing::Table table = srv_ring_.allocate(2);
    const UINT step = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = kLayerFormat;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    D3D12_CPU_DESCRIPTOR_HANDLE at = table.cpu;
    device_->CreateShaderResourceView(copies[0], &view, at);
    at.ptr += step;
    device_->CreateShaderResourceView(copies[1], &view, at);
    return table.gpu;
}

// The copy rt_composite_color made of this scene's opaque picture (before the lighting), without a new
// copy; 0 without one of the EFB's size.
D3D12_GPU_DESCRIPTOR_HANDLE RendererD3D12::rt_game_picture() {
    settle_owed_efb();
    if (!efb_color_ || !rt_color_copy_) {
        return {};
    }
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
    const D3D12_RESOURCE_DESC copy = rt_color_copy_->GetDesc();
    if (copy.Width != desc.Width || copy.Height != desc.Height || copy.Format != desc.Format) {
        return {};
    }
    const DescriptorRing::Table table = srv_ring_.allocate(1);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM : desc.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    device_->CreateShaderResourceView(rt_color_copy_.Get(), &view, table.cpu);
    return table.gpu;
}

void RendererD3D12::rt_trace_water() {
    settle_owed_efb();
    log_rt_seq("water");
    if (!ray_tracer_->water_open()) {
        return;
    }
    if (!efb_color_ || !efb_depth_ || conv_efb_color_srv_.ptr == 0u || conv_efb_depth_srv_.ptr == 0u) {
        static_cast<void>(ray_tracer_->trace_water(nullptr, rt_sources(), &RendererD3D12::rt_allocate, this,
                                                  D3D12_GPU_DESCRIPTOR_HANDLE{}, 0u, 0u));
        return;
    }
    // The trace reads the EFB colour and depth (the conversion views: colour, then depth); the
    // composite draws into the colour at that depth.
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    barriers[0].Transition.pResource = efb_color_.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.pResource = efb_depth_.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[1].Transition.StateAfter =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(2, barriers);
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
    const auto width = static_cast<std::uint32_t>(desc.Width);
    const int slot = ray_tracer_->trace_water(command_list_.Get(), rt_sources(), &RendererD3D12::rt_allocate, this,
                                             conv_efb_color_srv_, width, desc.Height);
    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barriers[0]);
    if (slot >= 0 && efb_hdr_) {
        const D3D12_GPU_DESCRIPTOR_HANDLE scene = rt_copy_color(rt_water_copy_, L"rt water scene");
        ray_tracer_->composite_water(command_list_.Get(), efb_rtv_, conv_efb_depth_srv_, width, desc.Height, slot,
                                    false, scene, rt_opaque_srv_, layer_window_ ? rt_layer_table()
                                                                                : D3D12_GPU_DESCRIPTOR_HANDLE{});
        if (replay_recording_) {
            replay_ops_.push_back({ReplayKind::RtWater,
                                   static_cast<std::uint32_t>(slot) | (layer_window_ ? kReplayWaterLayer : 0u)});
        }
    }
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barriers[1]);
    // The waterfall mist over the water and the glass, under the effects (the window's layer).
    if (const int mist = ray_tracer_->mist_slot(); mist >= 0 && efb_hdr_) {
        record_rt_mist(mist, false, layer_window_);
        if (replay_recording_) {
            replay_ops_.push_back({ReplayKind::RtMist,
                                   static_cast<std::uint32_t>(mist) | (layer_window_ ? kReplayWaterLayer : 0u)});
        }
    }
    // The effects drawn after the composites are over them.
    layer_window_ = false;
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bound_rt_aux_ = 0xFFu;
    bound_pipeline_ = nullptr;
}

void RendererD3D12::record_rt_water(int slot, bool reproject, bool layer) {
    settle_owed_efb();
    if (!efb_depth_ || conv_efb_depth_srv_.ptr == 0u || !efb_hdr_) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = efb_depth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    const D3D12_RESOURCE_DESC desc = efb_depth_->GetDesc();
    const D3D12_GPU_DESCRIPTOR_HANDLE scene = rt_copy_color(rt_water_copy_, L"rt water scene");
    ray_tracer_->composite_water(command_list_.Get(), efb_rtv_, conv_efb_depth_srv_,
                                static_cast<std::uint32_t>(desc.Width), desc.Height, slot, reproject, scene,
                                rt_opaque_srv_, layer ? rt_layer_table() : D3D12_GPU_DESCRIPTOR_HANDLE{});
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bound_rt_aux_ = 0xFFu;
}

void RendererD3D12::record_rt_mist(int slot, bool reproject, bool layer) {
    settle_owed_efb();
    if (!efb_depth_ || conv_efb_depth_srv_.ptr == 0u || !efb_hdr_) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = efb_depth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    const D3D12_RESOURCE_DESC desc = efb_depth_->GetDesc();
    // The frame after the water's composites (the water copy's resource, its composites recorded before).
    const D3D12_GPU_DESCRIPTOR_HANDLE scene = rt_copy_color(rt_water_copy_, L"rt water scene");
    ray_tracer_->composite_mist(command_list_.Get(), efb_rtv_, conv_efb_depth_srv_,
                               static_cast<std::uint32_t>(desc.Width), desc.Height, static_cast<unsigned>(slot), reproject,
                               scene, layer ? rt_layer_table() : D3D12_GPU_DESCRIPTOR_HANDLE{});
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bound_rt_aux_ = 0xFFu;
}

void RendererD3D12::record_rt_composite(int slot, bool reproject) {
    settle_owed_efb();
    const GpuPassScope pass{*this, GpuPass::RayTracing};
    log_rt_seq("composite");
    if (!efb_depth_ || conv_efb_depth_srv_.ptr == 0u || !efb_hdr_) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = efb_depth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    const D3D12_RESOURCE_DESC desc = efb_depth_->GetDesc();
    ray_tracer_->composite(command_list_.Get(), efb_rtv_, conv_efb_depth_srv_, rt_composite_color(),
                          static_cast<std::uint32_t>(desc.Width), desc.Height, slot, reproject, efb_scale_);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    // The sub-frame's frame before its translucent phases (its water composite's seabed).
    rt_opaque_srv_ = rt_copy_color(rt_opaque_copy_, L"rt opaque scene");
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bound_rt_aux_ = 0xFFu;
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
                return (static_cast<std::uint64_t>(v.particle) << 32u) |
                       (static_cast<std::uint64_t>(v.strip) << 24u) | (static_cast<std::uint64_t>(v.side) << 16u) |
                       (v.age_lifetime & 0xFFFFu);
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
                const std::uint64_t key = key_of(v);
                const auto found = std::lower_bound(stripe_keys_.begin(), stripe_keys_.end(),
                                                    std::pair<std::uint64_t, std::uint32_t>{key, 0u});
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
    replay_viewports_.clear();
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
    replay_start_output_active_ = output_efb_active_;
    replay_source_slot_ = frame_slot_;
}

bool RendererD3D12::ensure_subframe_resources(unsigned xfb_width, unsigned xfb_height) {
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
    if (subframe_xfb_.texture && subframe_xfb_.width == xfb_width &&
        subframe_xfb_.height == xfb_height) {
        return true;
    }
    if (subframe_xfb_.texture) {
        wait_for_gpu();
    }
    subframe_xfb_ = XfbTexture{};
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = xfb_width;
    rd.Height = xfb_height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kEfbColorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr, IID_PPV_ARGS(&subframe_xfb_.texture)))) {
        return false;
    }
    subframe_xfb_.width = static_cast<std::uint16_t>(xfb_width);
    subframe_xfb_.height = static_cast<std::uint16_t>(xfb_height);
    subframe_xfb_.presentable = true;
    return true;
}

void RendererD3D12::execute_replay_copy(const ReplayCopy& copy, ID3D12Resource* target) {
    settle_owed_efb();
    if (copy.identity) {
        resolve_efb(false);
        detail::record_efb_identity_copy(command_list_.Get(), efb_color_.Get(),
                                         target != nullptr ? target : copy.dest, copy.box);
        return;
    }
    if (copy.mode == 2u) {
        record_conversion_blit(
            alt_efb_.color.Get(), copy.width, copy.height, {}, copy.params,
            false, false, true, 15u, copy.geometry[0], copy.geometry[1],
            copy.geometry[2], copy.geometry[3], copy.geometry[4]);
        return;
    }
    ID3D12Resource* const dest = copy.mode == 1u ? subframe_xfb_.texture.Get()
        : target != nullptr ? target : copy.dest;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = dest;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);
    record_conversion_blit(
        dest, copy.width, copy.height, allocate_conversion_rtv(dest), copy.params,
        copy.depth, copy.intensity, copy.keep_alpha, copy.format,
        copy.geometry[0], copy.geometry[1], copy.geometry[2], copy.geometry[3],
        copy.geometry[4]);
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
    entry.texture->SetName(snapshot ? L"GX sub-frame copy snapshot" : L"GX sub-frame copy target");
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
        for (const auto& [entry, pso] : {std::pair{"copy_main", std::addressof(interp_copy_pso_)},
                                          std::pair{"eval_main", std::addressof(interp_eval_pso_)}}) {
            const ComPtr<ID3DBlob> cs = compile_hlsl(kInterpEvalCs, sizeof(kInterpEvalCs) - 1u, "cs_5_1", entry);
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
    interp_output_->SetName(L"GX interpolated constants");
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
    // GxBackend::scene_depth_view gives a traced frame's (the draws add the jitter in clip space, x + jx w: the
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
    ray_tracer_->write_reprojection(command_list_.Get(), camera_t, own_known ? &own_view : nullptr);
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
    unsigned output_scale = 0u;  // the log switches to the output EFB at this scale
    for (const ReplayCopy& copy : run_copies_) {
        if (copy.mode == 1u) display = &copy;
        if (copy.mode == 2u) output_scale = copy.width / kEfbWidth;
    }
    if (display == nullptr || run_start_output_active_) {
        // Logs begin at the ring advance after a display copy, on the render
        // EFB; the private sets below assume it.
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
    interp_copied_.assign(static_cast<std::size_t>(layout.vs_count) + layout.ps_count, 0u);
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
        if (call.ps_record >= 0 && static_cast<std::size_t>(call.ps_record) < sequence.ps_slot.size()) {
            const std::int32_t slot = sequence.ps_slot[static_cast<std::size_t>(call.ps_record)];
            const std::size_t seen = layout.vs_count + static_cast<std::size_t>(slot);
            if (slot >= 0 && interp_copied_[seen] == 0u) {
                interp_copied_[seen] = 1u;
                if (!copy_block(call.ps_constants, interp_gpu::kPsBytes,
                                layout.ps_offset(static_cast<std::uint32_t>(slot)))) {
                    replay_failure_ = 9u;
                    return false;
                }
            }
        }
    }
    for (std::uint32_t q = 0; q < layout.palette_count && q < sequence.palette_source.size(); ++q) {
        const std::uint64_t source = static_cast<std::uint64_t>(sequence.palette_source[q]) * 16u;
        if (source + interp_gpu::kPaletteBytes > matrices_bytes) {
            replay_failure_ = 9u;
            return false;
        }
        interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Matrices),
                                  static_cast<std::uint32_t>(source), layout.palette_offset(q),
                                  interp_gpu::kPaletteWords});
        if (q < layout.prev_palette_count) {
            interp_copies_.push_back({static_cast<std::uint32_t>(interp_gpu::Source::Matrices),
                                      static_cast<std::uint32_t>(source), layout.prev_palette_offset(q),
                                      interp_gpu::kPrevPaletteWords});
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
    sequence_.output_scale = output_scale;
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
    settle_owed_efb();
    const ReplaySequence& sequence = sequence_.overrides;
    const interp_gpu::Layout& layout = sequence.layout;
    const unsigned output_scale = sequence_.output_scale;
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
        invalidate_gx_bindings();
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
        // Private EFB sets stand in for the live ones, so a later frame's partial
        // EFBs (its chunks interleave with the sub-frames) stay intact: one at the
        // render EFB's scale and, for a log that switches to the output EFB (HUD
        // at output resolution), one at that scale.
        const unsigned render_scale = output_efb_active_ ? alt_efb_.scale : efb_scale_;
        const unsigned render_samples = output_efb_active_ ? alt_efb_.msaa.samples : efb_msaa_.samples;
        if (((!replay_efb_.color || replay_efb_.scale != render_scale ||
              replay_efb_.msaa.samples != render_samples) &&
             !create_efb_set(replay_efb_, replay_efb_rtv_heap_, replay_efb_dsv_heap_, render_scale, true, false,
                             render_samples)) ||
            (output_scale != 0u &&
             (!replay_output_efb_.color || replay_output_efb_.scale != output_scale) &&
             !create_efb_set(replay_output_efb_, replay_output_efb_rtv_heap_,
                             replay_output_efb_dsv_heap_, output_scale, false, true))) {
            command_list_->Close();
            entry.sequence = 0u;
            replay_failure_ = 5u;
            return false;
        }
        if (output_scale != 0u) {
            // Its conversion-source views live in this game frame's descriptor
            // segment (the persistent prefix is sized for the live sets).
            write_efb_set_views(replay_output_efb_, srv_ring_.allocate(2u));
        }
        record_subframe_guard(true);

        // The live sets at rest (render EFB current), then the private ones in.
        const bool output_before = output_efb_active_;
        if (output_before) {
            swap_efb_sets();
        }
        swap_efb_with(replay_efb_);
        if (output_scale != 0u) {
            std::swap(alt_efb_, replay_output_efb_);
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
        bind_efb(nullptr);
        const auto far_values = make_efb_clear_values(0u, 0u, 0x00FFFFFFu, 0u);
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
                        call.matrix_palette = output + layout.palette_base();
                    }
                }
                if (call.ps_record >= 0 && static_cast<std::size_t>(call.ps_record) < sequence.ps_slot.size()) {
                    if (const std::int32_t slot = sequence.ps_slot[static_cast<std::size_t>(call.ps_record)];
                        slot >= 0) {
                        call.ps_constants = output + layout.ps_offset(static_cast<std::uint32_t>(slot));
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
            case ReplayKind::BindEfb: {
                const ReplayViewport& viewport = run_viewports_[op.index];
                bind_efb(viewport.valid ? viewport.values : nullptr);
                break;
            }
            case ReplayKind::Clear: {
                const ReplayClear& c = run_clears_[op.index];
                clear_efb(c.ar, c.gb, c.z24, c.color, c.alpha, c.depth, c.x, c.y, c.w, c.h, c.format);
                break;
            }
            case ReplayKind::Copy: {
                const ReplayCopy& copy = run_copies_[op.index];
                if (motion_swapped && copy.mode == 2u && !entry.split) {
                    // The output switch is the temporal upscale, recorded per
                    // presented frame (record_subframe_upscale); the rest of
                    // the log goes to the second list.
                    entry.split = true;
                    entry.upscale = {efb_color_.Get(), efb_depth_.Get(), motion_target_.Get(), reactive_target_.Get(),
                                     alt_efb_.color.Get(), kEfbWidth * efb_scale_, kEfbHeight * efb_scale_,
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
                    invalidate_gx_bindings();
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
            case ReplayKind::SwapEfbSets:
                swap_efb_sets();
                break;
            case ReplayKind::ClearDepth:
                command_list_->ClearDepthStencilView(
                    efb_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                    far_values.reversed_depth, 0u, 0u, nullptr);
                break;
            case ReplayKind::RtComposite:
                record_rt_composite(static_cast<int>(op.index), true);
                break;
            case ReplayKind::RtWater:
                record_rt_water(static_cast<int>(op.index & ~kReplayWaterLayer), true,
                                (op.index & kReplayWaterLayer) != 0u);
                break;
            case ReplayKind::RtMist:
                record_rt_mist(static_cast<int>(op.index & ~kReplayWaterLayer), true,
                               (op.index & kReplayWaterLayer) != 0u);
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
                record_hdr_end(static_cast<int>(op.index) - 1);
                break;
            }
        }
        // The log ends after its display copy, on the output EFB when it switched.
        if (output_efb_active_) {
            swap_efb_sets();
        }
        if (output_scale != 0u) {
            std::swap(alt_efb_, replay_output_efb_);
        }
        swap_efb_with(replay_efb_);
        if (output_before) {
            swap_efb_sets();
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
        invalidate_gx_bindings();

        entry.sequence = sequence_.serial;
        entry.blend = blend && blended != 0u;
        entry.redirect = redirect;
        entry.blended_draws = blended;
        entry.guarded = subframe_guarded_.size();
        entry.alloc_segment = ring_segment_;
    }
    invalidate_gx_bindings();
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
        invalidate_gx_bindings();
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
        invalidate_gx_bindings();
    }
    (void)present(&subframe_xfb_, subframe_capture_);
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
    if (selected_xfb_readback_pending_ && selected_xfb_readback_fence_ == 0u) {
        selected_xfb_readback_fence_ = fence;
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

// Motion and reactive targets of the sub-frames, at the replay EFB's scale
// (the scale current while a replay records).
bool RendererD3D12::ensure_replay_motion_targets() {
    if (replay_motion_.target && replay_motion_.scale == efb_scale_) {
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
    rd.Width = kEfbWidth * efb_scale_;
    rd.Height = kEfbHeight * efb_scale_;
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
    replay_motion_.target->SetName(L"GX sub-frame motion vectors");
    replay_motion_.reactive->SetName(L"GX sub-frame reactive mask");
    replay_motion_.layer_color->SetName(L"GX sub-frame effects layer colour");
    replay_motion_.layer_trans->SetName(L"GX sub-frame effects layer transmittance");
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
    replay_motion_.scale = efb_scale_;
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
// replay's EFB into its output EFB, or the plain resample when the upscaler
// could not record it.
void RendererD3D12::record_subframe_upscale(const ReplayList& entry, const SubframeTime& time) {
    const bool recording = replay_recording_;
    replay_recording_ = false;
    const bool output_before = output_efb_active_;
    if (output_before) {
        swap_efb_sets();
    }
    swap_efb_with(replay_efb_);
    std::swap(alt_efb_, replay_output_efb_);
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
        invalidate_gx_bindings();
    }
    temporal_history_valid_ = upscaled;
    if (!upscaled && entry.upscale.copy < run_copies_.size()) {
        execute_replay_copy(run_copies_[entry.upscale.copy]);
    }
    std::swap(alt_efb_, replay_output_efb_);
    swap_efb_with(replay_efb_);
    if (output_before) {
        swap_efb_sets();
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
    const unsigned render_scale = output_efb_active_ ? alt_efb_.scale : efb_scale_;
    const unsigned output_scale = upscale_output_scale_;
    if (!temporal_mode() || output_scale == 0u || render_scale > output_scale) {
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
    const unsigned rw = kEfbWidth * render_scale;
    const unsigned rh = kEfbHeight * render_scale;
    const unsigned ow = kEfbWidth * output_scale;
    const unsigned oh = kEfbHeight * output_scale;
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
    // The engine's histories follow what moved by itself through the raster's motion: without a temporal upscaler the
    // scene still writes it (MSAA is off with ray tracing).
    scene_motion_ = rt_active() && ray_tracer_->engine_active() && !output_efb_active_;
    const unsigned rw = kEfbWidth * efb_scale_;
    const unsigned ow = temporal_output_w_;
    // The context is replaced only by begin_frame (prepare_temporal_upscaler).
    if (!temporal_ || !temporal_mode() || output_efb_active_ || rw != temporal_render_w_ ||
        kEfbHeight * efb_scale_ != temporal_render_h_) {
        temporal_history_valid_ = false;
        return false;
    }
    temporal_interp_ = ring_mode_ == RingMode::Interp;
    if (temporal_interp_) {
        // Game frames feed the sub-frames: no jitter, and no upscale
        // (begin_output_efb). Each presented frame takes the next phase.
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

bool RendererD3D12::record_temporal_upscale(unsigned output_scale) {
    settle_owed_efb();
    const GpuPassScope pass{*this, GpuPass::Upscale};
    if (!scene_temporal_ || !temporal_ || !ensure_motion_target()) {
        return false;
    }
    TemporalUpscaleInputs in{};
    in.list = command_list_.Get();
    in.color = efb_color_.Get();
    in.depth = efb_depth_.Get();
    in.motion = motion_target_.Get();
    in.reactive = reactive_target_.Get();
    in.output = alt_efb_.color.Get();
    in.render_width = kEfbWidth * efb_scale_;
    in.render_height = kEfbHeight * efb_scale_;
    in.output_width = kEfbWidth * output_scale;
    in.output_height = kEfbHeight * output_scale;
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
    invalidate_gx_bindings();
    temporal_history_valid_ = recorded;
    return recorded;
}

unsigned RendererD3D12::output_efb_scale_for_window() const {
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const double content_aspect =
        aspect ? aspect->content_aspect : galaxy::kWiiContentAspect;
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        content_aspect);
    if (content.width <= 0 || content.height <= 0) {
        return 0u;
    }
    // The smallest integer scale whose 640x456 picture covers the window's
    // content rectangle on both axes (the launcher's Match Output rule).
    const double needed = std::max(content.width / static_cast<double>(kEfbWidth),
                                   content.height / 456.0);
    return std::clamp(static_cast<unsigned>(std::ceil(needed - 1e-6)), 1u, kMaxEfbScale);
}

bool RendererD3D12::ensure_output_efb(unsigned scale) {
    settle_owed_efb();
    if (alt_efb_.color && alt_efb_.scale == scale) {
        return true;
    }
    const UINT w = kEfbWidth * scale;
    const UINT h = kEfbHeight * scale;
    if (w > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
        h > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
        return false;
    }
    if (alt_efb_.color) {
        // A new output scale (window resize or internal resolution): frames in
        // flight and this frame's command list may still name the old targets.
        retired_efb_copy_textures_[frame_slot_].push_back(std::move(alt_efb_.color));
        retired_efb_copy_textures_[frame_slot_].push_back(std::move(alt_efb_.depth));
    }
    alt_efb_.color.Reset();
    alt_efb_.depth.Reset();
    alt_efb_.scale = 0u;
    if (!alt_efb_rtv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1;
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&alt_efb_rtv_heap_)),
                  "output EFB RTV heap");
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&alt_efb_dsv_heap_)),
                  "output EFB DSV heap");
        alt_efb_.rtv = alt_efb_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
        alt_efb_.dsv = alt_efb_dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = kEfbColorFormat;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
               D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = kEfbColorFormat;
    cv.Color[3] = 1.0f;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET,
            &cv, IID_PPV_ARGS(&alt_efb_.color)))) {
        return false;
    }
    rd.Format = kEfbDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kEfbDepthFormat;
    dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &dcv, IID_PPV_ARGS(&alt_efb_.depth)))) {
        alt_efb_.color.Reset();
        return false;
    }
    device_->CreateRenderTargetView(alt_efb_.color.Get(), nullptr, alt_efb_.rtv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kEfbDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(alt_efb_.depth.Get(), &dsv, alt_efb_.dsv);
    if (!alt_efb_.conv_table) {
        alt_efb_.conv_table = srv_ring_.allocate_persistent(2u);
    }
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = kEfbColorFormat;
    device_->CreateShaderResourceView(
        alt_efb_.color.Get(), &view, alt_efb_.conv_table->cpu);
    auto depth_cpu = alt_efb_.conv_table->cpu;
    depth_cpu.ptr += stride;
    view.Format = kEfbDepthSrvFormat;
    device_->CreateShaderResourceView(alt_efb_.depth.Get(), &view, depth_cpu);
    alt_efb_.conv_color_srv = alt_efb_.conv_table->gpu;
    alt_efb_.conv_depth_srv = alt_efb_.conv_table->gpu;
    alt_efb_.conv_depth_srv.ptr += stride;
    alt_efb_.scale = scale;
    std::cout << "[Renderer] output EFB " << scale << "x (" << w << "x" << h << ")\n";
    return true;
}

void RendererD3D12::write_efb_set_views(EfbTargetSet& set, const DescriptorRing::Table& table) {
    const UINT stride = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1u;
    view.Format = set.hdr ? kEfbHdrColorFormat : kEfbColorFormat;
    device_->CreateShaderResourceView(set.color.Get(), &view, table.cpu);
    auto depth_cpu = table.cpu;
    depth_cpu.ptr += stride;
    view.Format = kEfbDepthSrvFormat;
    device_->CreateShaderResourceView(set.depth.Get(), &view, depth_cpu);
    set.conv_table = table;
    set.conv_color_srv = table.gpu;
    set.conv_depth_srv = table.gpu;
    set.conv_depth_srv.ptr += stride;
}

bool RendererD3D12::create_efb_set(
    EfbTargetSet& set, ComPtr<ID3D12DescriptorHeap>& rtv_heap,
    ComPtr<ID3D12DescriptorHeap>& dsv_heap, unsigned scale, bool persistent_views,
    bool unordered_access, unsigned samples, bool hdr) {
    const UINT w = kEfbWidth * scale;
    const UINT h = kEfbHeight * scale;
    if (set.color) {
        wait_for_gpu();
    }
    set.color.Reset();
    set.depth.Reset();
    set.scale = 0u;
    if (!rtv_heap) {
        D3D12_DESCRIPTOR_HEAP_DESC dhd{};
        dhd.NumDescriptors = 1;
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&rtv_heap)), "EFB set RTV heap");
        dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        d3d_check(device_->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&dsv_heap)), "EFB set DSV heap");
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
    rd.Format = hdr ? kEfbHdrColorFormat : kEfbColorFormat;
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
    rd.Format = kEfbDepthResourceFormat;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv{};
    dcv.Format = kEfbDepthFormat;
    dcv.DepthStencil.Depth = 1.0f;
    if (FAILED(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &dcv, IID_PPV_ARGS(&set.depth)))) {
        set.color.Reset();
        return false;
    }
    device_->CreateRenderTargetView(set.color.Get(), nullptr, set.rtv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = kEfbDepthFormat;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device_->CreateDepthStencilView(set.depth.Get(), &dsv, set.dsv);
    if (!create_efb_msaa(set.msaa, set.depth.Get(), scale, samples, set.rtv, set.dsv)) {
        set.color.Reset();
        set.depth.Reset();
        return false;
    }
    if (persistent_views) {
        write_efb_set_views(set, set.conv_table ? *set.conv_table
                                                : srv_ring_.allocate_persistent(2u));
    } else {
        // The caller writes views into the frame's descriptor segment per use.
        set.conv_table.reset();
    }
    set.scale = scale;
    return true;
}

void RendererD3D12::swap_efb_with(EfbTargetSet& other) {
    settle_owed_efb();
    std::swap(efb_color_, other.color);
    std::swap(efb_depth_, other.depth);
    std::swap(efb_msaa_, other.msaa);
    std::swap(efb_scale_, other.scale);
    std::swap(efb_hdr_, other.hdr);
    std::swap(efb_rtv_, other.rtv);
    std::swap(efb_dsv_, other.dsv);
    std::swap(conv_efb_source_table_, other.conv_table);
    std::swap(conv_efb_color_srv_, other.conv_color_srv);
    std::swap(conv_efb_depth_srv_, other.conv_depth_srv);
    invalidate_gx_bindings();
    bound_scissor_input_scale_ = 0u;
}

void RendererD3D12::swap_efb_sets() {
    settle_owed_efb();
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::SwapEfbSets, 0u});
    }
    std::swap(efb_color_, alt_efb_.color);
    std::swap(efb_depth_, alt_efb_.depth);
    std::swap(efb_msaa_, alt_efb_.msaa);
    std::swap(efb_scale_, alt_efb_.scale);
    std::swap(efb_hdr_, alt_efb_.hdr);
    std::swap(efb_rtv_, alt_efb_.rtv);
    std::swap(efb_dsv_, alt_efb_.dsv);
    std::swap(conv_efb_source_table_, alt_efb_.conv_table);
    std::swap(conv_efb_color_srv_, alt_efb_.conv_color_srv);
    std::swap(conv_efb_depth_srv_, alt_efb_.conv_depth_srv);
    output_efb_active_ = !output_efb_active_;
    invalidate_gx_bindings();
    bound_scissor_input_scale_ = 0u;
    bind_efb(nullptr);
}

// DLSS Frame Generation's HUD-less picture (fg_hudless_*): the output EFB right after the
// upscale, before the 2D layer draws into it.
void RendererD3D12::take_fg_hudless_scene() {
    settle_owed_efb();
    fg_hudless_scene_ = false;
    if (!frame_generation_running()) {
        return;
    }
    if (!fg_hudless_efb_.color || fg_hudless_efb_.scale != efb_scale_) {
        if (fg_hudless_efb_.color) {
            wait_for_gpu();
        }
        fg_hudless_efb_.color.Reset();
        fg_hudless_efb_.depth.Reset();
        if (!create_efb_set(fg_hudless_efb_, fg_hudless_rtv_heap_, fg_hudless_dsv_heap_, efb_scale_)) {
            return;
        }
    }
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
    barriers[0].Transition.pResource = efb_color_.Get();
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.pResource = fg_hudless_efb_.color.Get();
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    command_list_->ResourceBarrier(2u, barriers);
    command_list_->CopyResource(fg_hudless_efb_.color.Get(), efb_color_.Get());
    for (D3D12_RESOURCE_BARRIER& barrier : barriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    command_list_->ResourceBarrier(2u, barriers);
    fg_hudless_scene_ = true;
}

// The display copy of the taken scene: the same copy into the HUD-less XFB.
void RendererD3D12::copy_fg_hudless_xfb(const EfbCopyParams& params, const XfbTexture& dest) {
    const bool taken = fg_hudless_scene_;
    fg_hudless_scene_ = false;
    if (!taken || replay_recording_ || fg_hudless_efb_.scale != efb_scale_ || !dest.texture) {
        return;
    }
    const D3D12_RESOURCE_DESC desc = dest.texture->GetDesc();
    if (!fg_hudless_xfb_.texture || fg_hudless_xfb_.texture->GetDesc().Width != desc.Width ||
        fg_hudless_xfb_.texture->GetDesc().Height != desc.Height) {
        if (fg_hudless_xfb_.texture) {
            wait_for_gpu();
        }
        fg_hudless_xfb_.texture.Reset();
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (FAILED(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                                    IID_PPV_ARGS(&fg_hudless_xfb_.texture)))) {
            return;
        }
        fg_hudless_xfb_.texture->SetName(L"DLSS-G HUD-less XFB");
    }
    fg_hudless_xfb_.width = dest.width;
    fg_hudless_xfb_.height = dest.height;
    fg_hudless_xfb_.rtv = allocate_conversion_rtv(fg_hudless_xfb_.texture.Get());
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = fg_hudless_xfb_.texture.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);
    const EfbCopySamplingGeometry sampling =
        compute_xfb_sampling_geometry(params, dest.width, dest.height, efb_scale_);
    swap_efb_with(fg_hudless_efb_);
    record_conversion_blit(fg_hudless_xfb_.texture.Get(), dest.width, dest.height, fg_hudless_xfb_.rtv, params,
                           /*is_depth_copy=*/false, /*intensity=*/false, /*keep_alpha=*/true,
                           /*copy_format=*/15u, sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y,
                           sampling.filter_row_offset);
    swap_efb_with(fg_hudless_efb_);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);
    fg_hudless_xfb_addr_ = dest.guest_addr;
}

// The HUD-less XFB of the presented copy, presented as the frame is into a backbuffer-sized
// target that frame generation reads at the present; without one this frame, no HUD-less tag.
void RendererD3D12::present_fg_hudless(const XfbTexture& source, const D3D12_VIEWPORT& viewport,
                                       const D3D12_RECT& scissor) {
    if (!frame_generation_running()) {
        return;
    }
    const bool ready = fg_hudless_xfb_.texture && fg_hudless_xfb_addr_ != 0u &&
                       fg_hudless_xfb_addr_ == source.guest_addr && fg_hudless_xfb_.width == source.width &&
                       fg_hudless_xfb_.height == source.height;
    fg_hudless_xfb_addr_ = 0u;
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
        const D3D12_RESOURCE_DESC desc = fg_hudless_xfb_.texture->GetDesc();
        post_process_.record(command_list_.Get(), srv_ring_, fg_hudless_xfb_.texture.Get(),
                             static_cast<unsigned>(desc.Width), desc.Height, rtv, viewport, frame_index_,
                             kFramesInFlight);
    } else {
        const DescriptorRing::Table tbl = srv_ring_.allocate(1);
        D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        device_->CreateShaderResourceView(fg_hudless_xfb_.texture.Get(), &sd, tbl.cpu);
        command_list_->SetGraphicsRootSignature(s_blit_root_sig.Get());
        command_list_->SetPipelineState(blit_pipeline_.Get());
        command_list_->SetGraphicsRootDescriptorTable(0, tbl.gpu);
        command_list_->RSSetViewports(1, &viewport);
        command_list_->RSSetScissorRects(1, &scissor);
        command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list_->DrawInstanced(3, 1, 0, 0);
    }
    invalidate_gx_bindings();
    streamline::tag_hudless(command_list_.Get(), fg_hudless_target_.Get(), host_window::backbuffer_width, host_window::backbuffer_height);
    // Back to the backbuffer for the overlays.
    const UINT buf_idx = swap_chain_->GetCurrentBackBufferIndex();
    D3D12_CPU_DESCRIPTOR_HANDLE back_rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    back_rtv.ptr += static_cast<SIZE_T>(buf_idx) * device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    command_list_->OMSetRenderTargets(1, &back_rtv, FALSE, nullptr);
}

void RendererD3D12::record_scene_aa() {
    settle_owed_efb();
    const GpuPassScope pass{*this, GpuPass::SceneAa};
    if (!post_process_.ready() || output_efb_active_ || scene_temporal_ || !efb_color_ || efb_msaa_.samples > 1u ||
        post_process_.settings().aa == PostProcessSettings::Aa::kOff) {
        return;
    }
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::SceneAa, 0u});
    }
    // The 640x456 picture the display copy takes decides AUTO.
    const auto aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport content = galaxy::fit_content_viewport(
        static_cast<int>(host_window::backbuffer_width), static_cast<int>(host_window::backbuffer_height),
        aspect ? aspect->content_aspect : galaxy::kWiiContentAspect);
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
    static_cast<void>(post_process_.record_scene_aa(
        command_list_.Get(), srv_ring_, efb_color_.Get(), efb_rtv_, static_cast<unsigned>(desc.Width),
        desc.Height, kEfbWidth * efb_scale_, 456u * efb_scale_,
        static_cast<unsigned>(std::max(content.width, 1)), static_cast<unsigned>(std::max(content.height, 1)),
        frame_index_, kFramesInFlight));
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bound_rt_aux_ = 0xFFu;
}

// The EFB's depth, single-sample (MSAA resolves it, sample 0), read by the occlusion; the product lands on the
// EFB's own colour target, every sample of a multisampled one.
void RendererD3D12::record_scene_ao() {
    settle_owed_efb();
    const GpuPassScope pass{*this, GpuPass::SceneAo};
    if (!ambient_occlusion_.ready() || !efb_depth_ || !efb_color_) {
        return;
    }
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::SceneAo, 0u});
    }
    resolve_efb(true, false);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = efb_depth_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    command_list_->ResourceBarrier(1, &barrier);
    const D3D12_RESOURCE_DESC desc = efb_depth_->GetDesc();
    AmbientOcclusion::Inputs in{};
    in.depth = efb_depth_.Get();
    in.width = static_cast<unsigned>(desc.Width);
    in.height = desc.Height;
    in.efb_scale = efb_scale_;
    in.color_rtv = efb_rtv_;
    in.samples = efb_msaa_.samples;
    in.hdr = efb_hdr_;
    in.view = scene_ao_view_;
    static_cast<void>(ambient_occlusion_.record(command_list_.Get(), srv_ring_, in, frame_index_, kFramesInFlight));
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    if (efb_msaa_.samples > 1u) {
        efb_msaa_.color_dirty = true;
    }
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
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

// The game's bloom on the EFB's single-sample colour (MSAA resolves it first); the composite lands on every sample of
// a multisampled EFB. The game's frame is its 640 x 456 at the internal scale.
void RendererD3D12::record_game_bloom(const ReplayBloom& bloom) {
    settle_owed_efb();
    const GpuPassScope pass{*this, GpuPass::Bloom};
    if (!efb_color_) {
        return;
    }
    resolve_efb(false, true);
    const D3D12_RESOURCE_DESC desc = efb_color_->GetDesc();
    GameBloom::Inputs in{};
    in.efb = efb_color_.Get();
    in.rtv = efb_rtv_;
    in.samples = efb_msaa_.samples;
    in.hdr = efb_hdr_;
    in.scale = efb_scale_;
    in.logical_width = kEfbWidth;
    in.logical_height = kGameFrameHeight;
    in.frame_width = std::min(kEfbWidth * efb_scale_, static_cast<unsigned>(desc.Width));
    in.frame_height = std::min(kGameFrameHeight * efb_scale_, static_cast<unsigned>(desc.Height));
    if (bloom.phase == motion::kBloomKeep) {
        static_cast<void>(game_bloom_.keep(command_list_.Get(), in, frame_index_, kFramesInFlight));
        return;
    }
    if (bloom.phase == motion::kBloomRestore) {
        if (!game_bloom_.restore(command_list_.Get(), srv_ring_, in, frame_index_, kFramesInFlight)) {
            return;
        }
    } else {
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
    if (efb_msaa_.samples > 1u) {
        efb_msaa_.color_dirty = true;
    }
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bound_rt_aux_ = 0xFFu;
}

bool RendererD3D12::begin_output_efb() {
    const bool temporal = scene_temporal_;
    scene_temporal_ = false;
    // Nebula's screens with small text are drawn at the window's resolution whatever the HUD's (sharp_2d): at the
    // internal scale their letters would lose strokes on a wide or large window. Otherwise the game's layouts keep at
    // least the Wii's own density on a window wider than 16:9 or narrower than 4:3, never above the window's.
    const bool full = temporal || get_render_config().hud_output_resolution || sharp_2d_requested();
    const unsigned layout = full ? 0u : galaxy::layout_density_scale(galaxy::experimental_effective_host_aspect());
    if (output_efb_active_ || (!full && layout <= efb_scale_)) {
        return false;
    }
    // A temporal upscaler outputs the internal resolution; the 2D layer alone
    // goes to the window's.
    const unsigned scale = temporal ? temporal_output_w_ / kEfbWidth
                           : full   ? output_efb_scale_for_window()
                                    : std::min(output_efb_scale_for_window(), layout);
    if (scale == 0u || scale < efb_scale_ || (scale == efb_scale_ && !temporal) ||
        !ensure_output_efb(scale)) {
        temporal_history_valid_ = false;
        return false;
    }
    scene_temporal_ = temporal;
    const bool upscaled = temporal && !temporal_interp_ && record_temporal_upscale(scale);
    scene_temporal_ = false;
    if (upscaled) {
        swap_efb_sets();
        const auto far_values = make_efb_clear_values(0u, 0u, 0x00FFFFFFu, 0u);
        command_list_->ClearDepthStencilView(
            efb_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            far_values.reversed_depth, 0u, 0u, nullptr);
        take_fg_hudless_scene();
        return true;
    }
    // The whole scene EFB, resampled into the output target with the display
    // copy's identity settings (no filter, gamma 1.0, alpha kept).
    EfbCopyParams params{};
    params.src_width = static_cast<std::uint16_t>(kEfbWidth);
    params.src_height = static_cast<std::uint16_t>(kEfbHeight);
    params.y_scale_raw = 256u;
    params.filter0_raw = (21u << 12u) | (22u << 18u);
    params.filter1_raw = 21u;
    const unsigned w = kEfbWidth * scale;
    const unsigned h = kEfbHeight * scale;
    const EfbCopySamplingGeometry sampling =
        compute_xfb_sampling_geometry(params, w, h, efb_scale_);
    if (replay_recording_) [[unlikely]] {
        ReplayCopy copy{};
        copy.width = w;
        copy.height = h;
        copy.params = params;
        copy.keep_alpha = true;
        copy.format = 15u;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        copy.geometry[4] = sampling.filter_row_offset;
        copy.mode = 2u;
        replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
        replay_copies_.push_back(copy);
        const bool recording = replay_recording_;
        replay_recording_ = false;  // the blit's own EFB rebinding is part of the copy
        record_conversion_blit(
            alt_efb_.color.Get(), w, h, {}, params,
            /*is_depth_copy=*/false, /*intensity=*/false, /*keep_alpha=*/true,
            /*copy_format=*/15u,
            sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y,
            sampling.filter_row_offset);
        replay_recording_ = recording;
        swap_efb_sets();
        replay_ops_.push_back({ReplayKind::ClearDepth, 0u});
    } else {
        record_conversion_blit(
            alt_efb_.color.Get(), w, h, {}, params,
            /*is_depth_copy=*/false, /*intensity=*/false, /*keep_alpha=*/true,
            /*copy_format=*/15u,
            sampling.src_x, sampling.src_y, sampling.step_x, sampling.step_y,
            sampling.filter_row_offset);
        swap_efb_sets();
    }
    const auto far_values = make_efb_clear_values(0u, 0u, 0x00FFFFFFu, 0u);
    command_list_->ClearDepthStencilView(
        efb_dsv_, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        far_values.reversed_depth, 0u, 0u, nullptr);
    return true;
}

void RendererD3D12::end_output_efb() {
    if (output_efb_active_) {
        swap_efb_sets();
    }
}

bool RendererD3D12::copy_output_efb_to_texture(const EfbCopyParams& params, TextureCache& textures) {
    if (output_efb_active_ || !alt_efb_.color) {
        return copy_efb_to_texture(params, textures, false);
    }
    swap_efb_sets();
    const bool updated = copy_efb_to_texture(params, textures, false);
    swap_efb_sets();
    return updated;
}

void RendererD3D12::bind_efb(const float xf_vp[6]) {
    if (replay_recording_) [[unlikely]] {
        ReplayViewport viewport{};
        viewport.valid = xf_vp != nullptr;
        if (xf_vp != nullptr) std::memcpy(viewport.values, xf_vp, sizeof(viewport.values));
        replay_ops_.push_back({ReplayKind::BindEfb, static_cast<std::uint32_t>(replay_viewports_.size())});
        replay_viewports_.push_back(viewport);
    }
    command_list_->OMSetRenderTargets(1, &efb_rtv_, FALSE, &efb_dsv_);
    bound_rt_aux_ = 0u;

    float vp_x = 0.0f, vp_y = 0.0f;
    float vp_w = static_cast<float>(kEfbWidth  * efb_scale_);
    float vp_h = static_cast<float>(kEfbHeight * efb_scale_);

    if (xf_vp) {
        // GX XF viewport: [xs, xo, ys, yo, zs, zo]
        // x_center = xo - 342,  half_w = |xs|
        // y_center = yo - 342,  half_h = |ys|
        const float xs = xf_vp[0], xo = xf_vp[1];
        const float ys = xf_vp[2], yo = xf_vp[3];
        const float scale = static_cast<float>(efb_scale_);
        const float hw = std::abs(xs) * scale;
        const float hh = std::abs(ys) * scale;
        const float cx = (xo - 342.0f) * scale;
        const float cy = (yo - 342.0f) * scale;
        vp_x = cx - hw; vp_y = cy - hh;
        vp_w = hw * 2.0f; vp_h = hh * 2.0f;
    }

    D3D12_VIEWPORT vp{vp_x, vp_y, vp_w, vp_h, 0.0f, 1.0f};
    D3D12_RECT sci{0, 0, static_cast<LONG>(kEfbWidth * efb_scale_), static_cast<LONG>(kEfbHeight * efb_scale_)};
    command_list_->RSSetViewports(1, &vp);
    command_list_->RSSetScissorRects(1, &sci);
    bound_viewport_ = vp;
    bound_scissor_ = sci;
    // Full-EFB binding can overwrite a previously applied GX scissor even
    // though its raw draw input is unchanged on the next draw.
    bound_scissor_input_valid_ = false;
    bound_viewport_valid_ = true;
    bound_scissor_valid_ = true;
}

void RendererD3D12::clear_efb(
    std::uint32_t clear_ar, std::uint32_t clear_gb, std::uint32_t clear_z24,
    bool color_enable, bool alpha_enable, bool depth_enable,
    unsigned rect_x, unsigned rect_y, unsigned rect_w, unsigned rect_h,
    std::uint8_t pixel_format) {
    if (efb_msaa_.samples > 1u) {
        efb_msaa_.color_dirty = efb_msaa_.depth_dirty = true;
    }
    if (replay_recording_) [[unlikely]] {
        replay_ops_.push_back({ReplayKind::Clear, static_cast<std::uint32_t>(replay_clears_.size())});
        replay_clears_.push_back({clear_ar, clear_gb, clear_z24, color_enable, alpha_enable,
                                  depth_enable, rect_x, rect_y, rect_w, rect_h, pixel_format});
    } else if (ring_mode_ == RingMode::Interp && ring_advance_pending_ && !replay_playing_) [[unlikely]] {
        log_start_clear_ = {clear_ar, clear_gb, clear_z24, color_enable, alpha_enable,
                            depth_enable, rect_x, rect_y, rect_w, rect_h, pixel_format};
        log_start_clear_valid_ = true;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = efb_rtv_;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = efb_dsv_;

    // Scope the clear to the copy's src rect (scaled), per hardware.
    const LONG efb_w = static_cast<LONG>(kEfbWidth * efb_scale_);
    const LONG efb_h = static_cast<LONG>(kEfbHeight * efb_scale_);
    D3D12_RECT rect{0, 0, efb_w, efb_h};
    UINT num_rects = 0;
    const D3D12_RECT* rects = nullptr;
    if (rect_w != 0 && rect_h != 0) {
        const ScaledEfbRect scaled = compute_scaled_efb_rect(
            static_cast<std::int32_t>(rect_x),
            static_cast<std::int32_t>(rect_y),
            static_cast<std::int32_t>(rect_x + rect_w),
            static_cast<std::int32_t>(rect_y + rect_h),
            kEfbWidth,
            kEfbHeight,
            efb_scale_);
        rect = D3D12_RECT{
            scaled.left, scaled.top, scaled.right, scaled.bottom};
        if (rect.right <= rect.left || rect.bottom <= rect.top) {
            return;
        }
        num_rects = 1;
        rects = &rect;
    }

    if (efb_owed_ && pass_list_open_) [[unlikely]] {
        const bool rgba = color_enable && (alpha_enable || (pixel_format & ~kEfbClearExact) != 1u);
        restore_owed_efb(rgba ? &rect : nullptr);
    }
    const auto clear = make_efb_clear_values(clear_ar, clear_gb, clear_z24, pixel_format);
    if (color_enable || alpha_enable) {
        if ((pixel_format & ~kEfbClearExact) != 1u) alpha_enable = true;
        const float* col = clear.color.data();
        if (color_enable && alpha_enable) {
            command_list_->ClearRenderTargetView(rtv, col, num_rects, rects);
        } else {
            command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            bound_rt_aux_ = 0u;
            const D3D12_VIEWPORT vp{
                0.0f, 0.0f,
                static_cast<float>(efb_w), static_cast<float>(efb_h),
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
            invalidate_gx_bindings();
            bind_efb(nullptr);
        }
    }
    if (depth_enable) {
        // REVERSED-Z buffer: GX z24 (near=0, far=2^24) inverts into the D3D depth value.
        // GX_MAX_Z24 (0xFFFFFF = "clear to farthest") becomes about 0.0 here, which the flipped
        // GEQUAL-style funcs pass.
        command_list_->ClearDepthStencilView(
            dsv, D3D12_CLEAR_FLAG_DEPTH, clear.reversed_depth, 0, num_rects, rects);
    }
}

void RendererD3D12::draw(const DrawCall& call) {
    settle_owed_efb();
    if (efb_msaa_.samples > 1u) {
        efb_msaa_.color_dirty = efb_msaa_.depth_dirty = true;
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
            const D3D12_CPU_DESCRIPTOR_HANDLE targets[4]{efb_rtv_, reactive_rtv_, layer_color_rtv_, layer_trans_rtv_};
            command_list_->OMSetRenderTargets(4, targets, FALSE, &efb_dsv_);
        } else if (call.aux_target != 0u) {
            const D3D12_CPU_DESCRIPTOR_HANDLE targets[2]{
                efb_rtv_, call.aux_target == 1u ? motion_rtv_ : reactive_rtv_};
            command_list_->OMSetRenderTargets(2, targets, FALSE, &efb_dsv_);
        } else {
            command_list_->OMSetRenderTargets(1, &efb_rtv_, FALSE, &efb_dsv_);
        }
        bound_rt_aux_ = call.aux_target;
    }

    // Per-draw GX scissor, scaled to the EFB and clamped. An empty scissor
    // rasterizes nothing (also how CullMode::All is expressed). The raw input
    // is cached only after a nonempty one has been applied.
    if (!bound_scissor_valid_ || !bound_scissor_input_valid_ ||
        bound_scissor_input_scale_ != efb_scale_ ||
        call.scissor.left != bound_scissor_input_.left ||
        call.scissor.top != bound_scissor_input_.top ||
        call.scissor.right != bound_scissor_input_.right ||
        call.scissor.bottom != bound_scissor_input_.bottom) {
        const ScaledEfbRect scaled = compute_scaled_efb_rect(
            call.scissor.left,
            call.scissor.top,
            call.scissor.right,
            call.scissor.bottom,
            kEfbWidth,
            kEfbHeight,
            efb_scale_);
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
        bound_scissor_input_scale_ = efb_scale_;
        bound_scissor_input_valid_ = true;
    }

    // Per-draw XF viewport with its depth range: SMG clears depth by drawing fullscreen quads with
    // GXSetViewport near == far == 1.0, so such a quad writes z = FAR whatever its geometry z.
    // XF layout (hardware/Dolphin xfmem): wd, ht, zRange, xOrig, yOrig, farZ.
    // Depth range (Dolphin BPFunctions::SetScissorAndViewport, the REVERSED-Z form for backends
    // without reversed-range support):
    //   gx_min = (farZ - zRange) / 2^24,  gx_max = farZ / 2^24
    //   MinDepth = 1 - gx_max,  MaxDepth = 1 - gx_min
    // Composes with the negated projection z (fill_projection): GX near maps to the depth MaxDepth
    // side, GX far to the MinDepth side; depth funcs are flipped in pipeline_cache to match.
    if (call.has_viewport) {
        const ScaledGxViewport scaled =
            compute_scaled_gx_viewport(call.xf_viewport, efb_scale_);
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
    // A replayed display list rebinds the same vertex-ring allocation for
    // thousands of consecutive draws, so the view is compared before binding.
    // Every other writer of stream 0 (the UI helpers, the conversion blit, the
    // clear quad) is followed by invalidate_gx_bindings(), and begin_frame()
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

std::optional<D3D12_BOX> detail::efb_identity_copy_box(
    const EfbCopyParams& params, bool is_depth_copy, unsigned efb_scale,
    unsigned dest_width, unsigned dest_height) noexcept {
    const std::optional<EfbCopyRect> rect =
        efb_identity_copy_rect(params, is_depth_copy, efb_scale, dest_width, dest_height);
    if (!rect) {
        return std::nullopt;
    }
    return D3D12_BOX{rect->left, rect->top, 0u, rect->right, rect->bottom, 1u};
}

void detail::record_efb_identity_copy(
    ID3D12GraphicsCommandList* commands, ID3D12Resource* source,
    ID3D12Resource* destination, const D3D12_BOX& source_box) {
    D3D12_RESOURCE_BARRIER barriers[2]{};
    for (auto& barrier : barriers) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    barriers[0].Transition.pResource = source;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.pResource = destination;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    commands->ResourceBarrier(2u, barriers);
    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = source;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = destination;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commands->CopyTextureRegion(&dst, 0u, 0u, 0u, &src, &source_box);
    for (auto& barrier : barriers) {
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    }
    commands->ResourceBarrier(2u, barriers);
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

// Records the conversion blit (see kConvPs).  Handles the EFB source
// transitions; the caller transitions `dest` to RENDER_TARGET first and back
// after.  Restores the GX root signature + EFB binding before returning.
void RendererD3D12::record_conversion_blit(
    ID3D12Resource* dest,
    unsigned dest_w,
    unsigned dest_h,
    D3D12_CPU_DESCRIPTOR_HANDLE dest_rtv,
    const EfbCopyParams& params,
    bool is_depth_copy,
    bool intensity,
    bool keep_alpha,
    std::uint8_t copy_format,
    float src_x,
    float src_y,
    float step_x,
    float step_y,
    float filter_row_offset) {
    settle_owed_efb();
    resolve_efb(is_depth_copy);
    ID3D12Resource* src =
        is_depth_copy ? efb_depth_.Get() : efb_color_.Get();

    // EFB -> shader resource.
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = src;
    b.Transition.StateBefore = is_depth_copy
        ? D3D12_RESOURCE_STATE_DEPTH_WRITE
        : D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    // Both immutable EFB views already live in the bound heap. Depth reads
    // retain the R24/X8 view of the typeless depth resource.
    const D3D12_GPU_DESCRIPTOR_HANDLE src_srv =
        is_depth_copy ? conv_efb_depth_srv_ : conv_efb_color_srv_;

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
    // A display copy one EFB pixel to one XFB pixel without gamma: its filter alone (display_filter_shader_source).
    // GALAXY_TEST_DISPLAY_FILTER_AB=1: the general conversion in every other presented frame.
    static const bool filter_ab = [] {
        char value[8]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value), "GALAXY_TEST_DISPLAY_FILTER_AB") == 0 && length > 1u &&
               value[0] == '1';
    }();
    const bool display_filter = !(filter_ab && (presented_frames_ & 1u) != 0u) && copy_format == 15u && keep_alpha &&
                                !intensity && !is_depth_copy && (params.gamma & 3u) == 0u && step_x == 1.0f &&
                                step_y == 1.0f &&
                                display_filter_pipeline_ != nullptr && dest->GetDesc().Format == kEfbColorFormat;
    command_list_->SetPipelineState(display_filter ? display_filter_pipeline_.Get()
                                    : dest->GetDesc().Format == DXGI_FORMAT_R8_UNORM ? conv_pipeline_r8_.Get()
                                                                                     : conv_pipeline_.Get());

    const EfbConversionShaderConstants constants =
        make_efb_conversion_constants(
            params,
            is_depth_copy,
            intensity,
            keep_alpha,
            copy_format,
            src_x,
            src_y,
            step_x,
            step_y,
            filter_row_offset,
            efb_scale_,
            kEfbHeight);
    command_list_->SetGraphicsRoot32BitConstants(
        0, 16, constants.values.data(), 0);
    command_list_->SetGraphicsRootDescriptorTable(1, src_srv);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);

    // EFB back to its steady state.
    ++efb_shader_copies_;
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);

    // Restore the GX drawing state (root signature changed; EFB RTV/DSV and full
    // viewport/scissor re-bound; per-draw viewport/scissor follow).
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bind_efb(nullptr);
}

void RendererD3D12::copy_efb_to_xfb(const EfbCopyParams& params, XfbTexture& dest) {
    settle_owed_efb();
    const GpuPassScope pass{*this, GpuPass::XfbCopy};
    log_rt_seq("xfb");
    // The caller publishes a new XFB serial after this returns, so a missing
    // target throws rather than pass as a new frame.
    if (!dest.texture) {
        throw std::runtime_error("XFB copy has no destination texture");
    }
    // A scene still in the HDR scene EFB (no scene end before the display copy): the camera first.
    end_hdr_scene();
    ++efb_copy_requests_;
    // Another XFB or texture copy can overwrite the shared CPU RTV slot.
    // Check its current resource, even when this destination has copied before.
    dest.rtv = allocate_conversion_rtv(dest.texture.Get());

    // Dest XFB texture -> render target for the stretch blit. The y-scale height difference between
    // the src rect and the dest texture is the stretch; CopyTextureRegion cannot express it.
    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = dest.texture.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    const EfbCopySamplingGeometry sampling =
        compute_xfb_sampling_geometry(
            params, dest.width, dest.height, efb_scale_);
    const bool recording = replay_recording_;
    if (recording) [[unlikely]] {
        ReplayCopy copy{};
        copy.width = dest.width;
        copy.height = dest.height;
        copy.params = params;
        copy.keep_alpha = true;
        copy.format = 15u;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        copy.geometry[4] = sampling.filter_row_offset;
        copy.mode = 1u;
        replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
        replay_copies_.push_back(copy);
        replay_recording_ = false;
    }

    // Display copies never convert to intensity (Dolphin passes false on the
    // XFB path); gamma + copy filter (deflicker) do apply.
    record_conversion_blit(
        dest.texture.Get(), dest.width, dest.height, dest.rtv, params,
        /*is_depth_copy=*/false, /*intensity=*/false, /*keep_alpha=*/true,
        /*copy_format=*/15u,
        sampling.src_x, sampling.src_y,
        sampling.step_x, sampling.step_y,
        sampling.filter_row_offset);
    replay_recording_ = recording;

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);
    copy_fg_hudless_xfb(params, dest);

    if (ring_mode_ == RingMode::Interp) {
        // The display copy closes the game frame: its log is complete and the
        // next frame starts a new ring segment.
        if (recording) {
            replay_recording_ = false;
            // One 3D scene, or none in a frame recorded as its screen layer alone (the pause menu). An output-EFB
            // switch (HUD at output resolution) replays into the sub-frame's private output EFB.
            replay_valid_ = replay_scene_count_ == 1u || (replay_scene_count_ == 0u && replay_screen_frame_);
            replay_reject_ = replay_valid_                ? 0u
                : replay_scene_count_ == 0u               ? 1u
                                                          : 2u;
            if (replay_valid_) {
                publish_replay_log();
            }
            replay_segment_ = ring_segment_;
            replay_xfb_addr_ = dest.guest_addr;
            ++replay_serial_;
            if (replay_valid_) {
                play_serial_ = replay_serial_;
            }
        }
        ring_advance_pending_ = true;
    }
}

bool RendererD3D12::copy_efb_to_texture(
    const EfbCopyParams& params, TextureCache& textures, bool is_depth_copy) {
    settle_owed_efb();
    const GpuPassScope pass{*this, GpuPass::EfbCopy};
    // SMG composites its visible frame from EFB-to-texture captures (the scene
    // is copied out at 640x456 and the final pass samples it). The conversion
    // blit applies intensity, gamma, the copy filter and half-scale, and serves
    // depth-source copies from efb_depth_ (SMG's focus/DOF effects).
    //
    // half_scale halves BOTH dimensions of the destination (Dolphin
    // TextureCacheBase scaleByHalf); the 2x step through the linear-filtered
    // source is the box filter.
    const UINT dw_unscaled = params.half_scale
        ? std::max<UINT>(1u, params.src_width / 2u)
        : params.src_width;
    const UINT dh_unscaled = params.half_scale
        ? std::max<UINT>(1u, params.src_height / 2u)
        : params.src_height;
    // Effect copies (is_effect_copy) are built at the game's own resolution.
    const bool native_bloom = efb_scale_ > 1u && is_effect_copy(dw_unscaled, dh_unscaled);
    const ScaledEfbCopyExtent extent = compute_scaled_efb_copy_extent(
        static_cast<std::uint16_t>(dw_unscaled),
        static_cast<std::uint16_t>(dh_unscaled),
        native_bloom ? 1u : efb_scale_);
    const UINT w = extent.width;
    const UINT h = extent.height;
    if (w == 0 || h == 0) {
        return false;
    }
    ++efb_copy_requests_;

    // Keyed by (addr, format, depth-source): SMG reuses one address with
    // different copy formats and each needs its own converted texture.
    const std::uint64_t dest_key =
        static_cast<std::uint64_t>(params.dest_addr) |
        (static_cast<std::uint64_t>(params.target_format) << 32) |
        (is_depth_copy ? (1ull << 40) : 0ull) |
        // One guest address alternates between early scaled masks and the
        // final native bloom composite. Cache both resources to avoid
        // reallocating on every pass/frame; the latest alias still wins.
        (native_bloom ? (1ull << 41) : 0ull) |
        // Interpolation: consecutive game frames copy into different
        // textures, so a sub-frame replaying one frame's copies never
        // overwrites what the next frame's chunks already copied.
        (ring_mode_ == RingMode::Interp ? (replay_serial_ & 1ull) << 42 : 0ull);

    // The destination map is bounded in bytes (efb_copy_dests_): the 192 MiB default holds about ten
    // full-extent copies at 4x, and most copies are much smaller masks and bloom workspaces. Least
    // recently used entries are evicted until the budget is met. A destination is a pure function of its
    // key, so an eviction costs at most a later re-creation (the `created` path below). `last_used` is
    // stamped first, so the entry this call uses is never a victim.
    static const std::uint64_t kEfbCopyDestBudgetBytes =
        efb_copy_dest_budget_bytes();
    EfbCopyDest& dest = efb_copy_dests_[dest_key];
    dest.last_used = ++efb_copy_dests_tick_;
    const bool created =
        !dest.texture || dest.width != w || dest.height != h;
    if (created) {
        if (dest.texture) {
            retired_efb_copy_textures_[frame_slot_].push_back(
                std::move(dest.texture));
            // The replaced texture leaves the budget now; the retirement list
            // releases it after kFramesInFlight frames.
            efb_copy_dests_bytes_ -= std::min(
                efb_copy_dests_bytes_, dest.bytes);
        }
        dest.bytes = 0u;
        dest.rtv = {};
        dest.rtv_valid = false;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = efb_copy_single_channel(params.target_format, is_depth_copy) ? DXGI_FORMAT_R8_UNORM
                                                                                            : kEfbColorFormat;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        std::uint64_t allocation_bytes = 0u;
        const HRESULT create_hr = efb_copy_heap_.create(
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
                message << "[efb-copy] device removed while allocating a "
                        << w << 'x' << h << " copy destination";
                throw std::runtime_error(message.str());
            }
            // Out of memory: skip this copy (its texture keeps its previous
            // contents) rather than ending the game.
            efb_copy_dests_bytes_ -= std::min(
                efb_copy_dests_bytes_, dest.bytes);
            dest.bytes = 0u;
            efb_copy_dests_.erase(dest_key);
            static std::atomic<unsigned> reported{0u};
            if (reported.fetch_add(1u, std::memory_order_relaxed) < 8u) {
                std::cerr << "[efb-copy] dest texture allocation failed hr=0x"
                          << std::hex << static_cast<unsigned long>(create_hr)
                          << " addr=0x" << params.dest_addr << std::dec
                          << " size=" << w << 'x' << h << "; copy skipped\n";
            }
            return false;
        }
        dest.width  = w;
        dest.height = h;
        // Charged at the resource's own allocation size, not w*h*bpp.
        dest.bytes = allocation_bytes;
        efb_copy_dests_bytes_ += dest.bytes;
    }
    // Trim against the updated allocation, not the destination's old extent.
    // Shrinking must not evict reusable neighbors; check growth in this call.
    if (kEfbCopyDestBudgetBytes != 0u &&
        efb_copy_dests_bytes_ > kEfbCopyDestBudgetBytes) {
        for (;;) {
            auto victim = efb_copy_dests_.end();
            for (auto it = efb_copy_dests_.begin(); it != efb_copy_dests_.end();
                 ++it) {
                if (it->first == dest_key) {
                    continue;
                }
                if (victim == efb_copy_dests_.end() ||
                    it->second.last_used < victim->second.last_used) {
                    victim = it;
                }
            }
            if (victim == efb_copy_dests_.end()) {
                break;  // only the entry in use remains
            }
            // Retired, not released: the slot's list is cleared at its next
            // begin_frame(), after the GPU work that may still read the victim.
            if (victim->second.texture) {
                retired_efb_copy_textures_[frame_slot_].push_back(
                    std::move(victim->second.texture));
            }
            efb_copy_dests_bytes_ -= std::min(
                efb_copy_dests_bytes_, victim->second.bytes);
            efb_copy_dests_.erase(victim);
            if (efb_copy_dests_bytes_ <= kEfbCopyDestBudgetBytes) {
                break;
            }
        }
    }
    // The HDR scene EFB's copies go through the conversion blit (clamped to the texture's range, as GX's).
    if (const auto source_box = efb_hdr_ ? std::optional<D3D12_BOX>{} : detail::efb_identity_copy_box(
            params, is_depth_copy, efb_scale_, w, h)) {
        if (replay_recording_) [[unlikely]] {
            ReplayCopy copy{};
            copy.dest = dest.texture.Get();
            copy.width = w;
            copy.height = h;
            copy.identity = true;
            copy.box = *source_box;
            replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
            replay_copies_.push_back(copy);
        }
        resolve_efb(false);
        detail::record_efb_identity_copy(command_list_.Get(), efb_color_.Get(),
            dest.texture.Get(), *source_box);
        if (ring_mode_ == RingMode::Interp) note_copy_write(dest.texture.Get());
        ++efb_identity_copies_;
        efb_identity_payload_bytes_ += static_cast<std::uint64_t>(w) * h * 4u;
        return textures.register_efb_copy(params.dest_addr, params, dest.texture);
    }

    // This descriptor shares its slot with XFB and other texture copies;
    // destination-local validity cannot prove its current contents.
    dest.rtv = allocate_conversion_rtv(dest.texture.Get());

    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = dest.texture.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    const EfbCopySamplingGeometry sampling =
        compute_efb_copy_sampling_geometry(params, w, h, efb_scale_);

    // Intensity copies to the RA4/RA8 (-> IA4/IA8) formats keep the source
    // alpha; pure-I formats replicate Y into alpha, matching the texture
    // decoder's I-format convention.
    const bool keep_alpha =
        params.target_format == 2u || params.target_format == 3u;

    const bool recording = replay_recording_;
    if (recording) [[unlikely]] {
        ReplayCopy copy{};
        copy.dest = dest.texture.Get();
        copy.width = w;
        copy.height = h;
        copy.params = params;
        copy.depth = is_depth_copy;
        copy.intensity = params.intensity;
        copy.keep_alpha = keep_alpha;
        copy.format = params.target_format;
        copy.geometry[0] = sampling.src_x;
        copy.geometry[1] = sampling.src_y;
        copy.geometry[2] = sampling.step_x;
        copy.geometry[3] = sampling.step_y;
        copy.geometry[4] = sampling.filter_row_offset;
        replay_ops_.push_back({ReplayKind::Copy, static_cast<std::uint32_t>(replay_copies_.size())});
        replay_copies_.push_back(copy);
        replay_recording_ = false;
    }
    record_conversion_blit(
        dest.texture.Get(), w, h, dest.rtv, params,
        is_depth_copy, params.intensity, keep_alpha,
        params.target_format,
        sampling.src_x, sampling.src_y,
        sampling.step_x, sampling.step_y,
        sampling.filter_row_offset);
    replay_recording_ = recording;
    if (ring_mode_ == RingMode::Interp) note_copy_write(dest.texture.Get());

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);

    // Refresh the guest-address alias every copy: with format-keyed dest
    // textures one address can alternate between resources, and the alias
    // must always point at the most recent copy (register_efb_copy is a
    // cheap no-op when the resource is unchanged).
    return textures.register_efb_copy(params.dest_addr, params, dest.texture);
}

bool RendererD3D12::ensure_peek_resources() {
    if (peek_target_ && peek_readback_ && peek_rtv_heap_ &&
        peek_allocator_ && peek_command_list_) {
        return true;
    }
    if (!device_ || !queue_ || !fence_ || !efb_color_ || !efb_depth_ ||
        !conv_pipeline_ || !s_conv_root_sig || !srv_ring_.heap() ||
        conv_efb_color_srv_.ptr == 0u || conv_efb_depth_srv_.ptr == 0u) {
        return false;
    }

    d3d_check(
        device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&peek_allocator_)),
        "GXPeek command allocator");
    d3d_check(
        device_->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            peek_allocator_.Get(),
            nullptr,
            IID_PPV_ARGS(&peek_command_list_)),
        "GXPeek command list");
    d3d_check(peek_command_list_->Close(), "GXPeek command list close");
    peek_command_list_->SetName(L"GxPeekList");

    D3D12_HEAP_PROPERTIES default_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC target_desc{};
    target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    target_desc.Width = 1;
    target_desc.Height = 1;
    target_desc.DepthOrArraySize = 1;
    target_desc.MipLevels = 1;
    target_desc.Format = kEfbColorFormat;
    target_desc.SampleDesc.Count = 1;
    target_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE target_clear{};
    target_clear.Format = kEfbColorFormat;
    target_clear.Color[3] = 1.0f;
    d3d_check(
        device_->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &target_desc,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            &target_clear,
            IID_PPV_ARGS(&peek_target_)),
        "GXPeek target");

    peek_readback_row_pitch_ = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readback_desc{};
    readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = peek_readback_row_pitch_;
    readback_desc.Height = 1;
    readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1;
    readback_desc.SampleDesc.Count = 1;
    readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d3d_check(
        device_->CreateCommittedResource(
            &readback_heap,
            D3D12_HEAP_FLAG_NONE,
            &readback_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&peek_readback_)),
        "GXPeek readback");

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.NumDescriptors = 1;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    d3d_check(
        device_->CreateDescriptorHeap(
            &rtv_heap_desc,
            IID_PPV_ARGS(&peek_rtv_heap_)),
        "GXPeek RTV heap");
    peek_rtv_ = peek_rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(peek_target_.Get(), nullptr, peek_rtv_);

    return true;
}

bool RendererD3D12::peek_efb(
    std::uint16_t x, std::uint16_t y, EfbPeekKind kind, std::uint32_t& value) {
    return read_efb_pixels(x, y, kind, value, {});
}

void RendererD3D12::create_first_use_resources() {
    (void)ensure_peek_resources();
    (void)ensure_pointer_depth_resources();
}

bool RendererD3D12::ensure_pointer_depth_resources() {
    if (pointer_depth_target_ && pointer_depth_readback_) {
        return true;
    }
    if (!ensure_peek_resources()) {
        return false;
    }
    constexpr UINT width = kEfbWidth;
    constexpr UINT height = kEfbHeight;
    constexpr UINT row_pitch = kEfbWidth * 4u;
    ComPtr<ID3D12Resource> target_candidate;
    ComPtr<ID3D12Resource> readback_candidate;
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = peek_target_->GetDesc();
    td.Width = width; td.Height = height;
    d3d_check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
        &td, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
        IID_PPV_ARGS(&target_candidate)), "pointer depth target");
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = peek_readback_->GetDesc();
    rd.Width = static_cast<UINT64>(row_pitch) * height;
    std::array<ComPtr<ID3D12Resource>, kPointerDepthRing> ring;
    for (auto& readback : ring) {
        d3d_check(device_->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
            &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&readback)), "pointer depth readback");
    }
    readback_candidate = ring[0];
    for (std::size_t i = 0; i < kPointerDepthRing; ++i) {
        pointer_depth_ring_rgba_[i] = pin_readback_mapping(ring[i].Get());
    }
    pointer_depth_ring_ = std::move(ring);
    pointer_depth_ring_next_ = 0;
    pointer_depth_target_ = std::move(target_candidate);
    pointer_depth_readback_ = std::move(readback_candidate);
    return true;
}

void RendererD3D12::select_pointer_depth_destination() {
    pointer_depth_readback_ = pointer_depth_ring_[pointer_depth_ring_next_];
    pointer_depth_pending_rgba_ = pointer_depth_ring_rgba_[pointer_depth_ring_next_];
    pointer_depth_ring_next_ = (pointer_depth_ring_next_ + 1u) % kPointerDepthRing;
}

// Mid-stream pointer depth field: the same conversion draw and readback copy
// as the utility path, recorded into the frame's command list so the frame's
// own submission carries it; nothing waits for it.
bool RendererD3D12::record_pointer_depth_capture_in_frame() {
    if (pointer_depth_in_frame_pending_ || pointer_depth_capture_fence_ != 0u) {
        return false;
    }
    if (!ensure_pointer_depth_resources()) {
        return false;
    }
    constexpr UINT width = kEfbWidth;
    constexpr UINT height = kEfbHeight;
    constexpr UINT row_pitch = kEfbWidth * 4u;
    select_pointer_depth_destination();
    resolve_efb(true);
    ID3D12Resource* const target = pointer_depth_target_.Get();
    ID3D12Resource* const readback = pointer_depth_readback_.Get();
    ID3D12Resource* const src = efb_depth_.Get();

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = src;
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(2, barriers);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = allocate_conversion_rtv(target);
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    bound_rt_aux_ = 0u;
    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);
    command_list_->SetGraphicsRootSignature(s_conv_root_sig.Get());
    command_list_->SetPipelineState(conv_pipeline_.Get());
    const EfbPeekSamplingGeometry sampling =
        compute_efb_peek_sampling_geometry(0u, 0u, efb_scale_);
    EfbConversionShaderConstants constants =
        make_efb_peek_conversion_constants(sampling, EfbPeekKind::Depth);
    constants.values[15] = -1.0f;
    command_list_->SetGraphicsRoot32BitConstants(0, 16, constants.values.data(), 0);
    command_list_->SetGraphicsRootDescriptorTable(1, conv_efb_depth_srv_);
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->DrawInstanced(3, 1, 0, 0);

    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    command_list_->ResourceBarrier(2, barriers);

    D3D12_TEXTURE_COPY_LOCATION src_copy{};
    src_copy.pResource = target;
    src_copy.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst_copy{};
    dst_copy.pResource = readback;
    dst_copy.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_copy.PlacedFootprint.Footprint.Format = kEfbColorFormat;
    dst_copy.PlacedFootprint.Footprint.Width = width;
    dst_copy.PlacedFootprint.Footprint.Height = height;
    dst_copy.PlacedFootprint.Footprint.Depth = 1;
    dst_copy.PlacedFootprint.Footprint.RowPitch = row_pitch;
    command_list_->CopyTextureRegion(&dst_copy, 0, 0, 0, &src_copy, nullptr);

    // Restore the GX drawing state exactly as record_conversion_blit does.
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bind_efb(nullptr);
    pointer_depth_in_frame_pending_ = true;
    return true;
}

bool RendererD3D12::capture_pointer_depth(std::span<std::uint32_t> pixels) {
    if (pixels.size() != kEfbWidth * kEfbHeight) {
        throw std::invalid_argument("pointer depth field requires 640x528 pixels");
    }
    std::uint32_t unused{};
    return read_efb_pixels(0u, 0u, EfbPeekKind::Depth, unused, pixels);
}

std::uint64_t RendererD3D12::submit_pointer_depth_capture() {
    std::uint32_t unused{};
    if (!read_efb_pixels(0u, 0u, EfbPeekKind::Depth, unused, {}, true)) {
        return 0u;
    }
    return pointer_depth_capture_fence_;
}

bool RendererD3D12::try_complete_pointer_depth_capture_in_place(
    const std::uint8_t*& rgba) {
    rgba = nullptr;
    if (pointer_depth_capture_fence_ == 0u) {
        if (pointer_depth_in_frame_pending_) {
            return false;  // recorded in the open frame; fenced at end_frame
        }
        throw std::logic_error("pointer depth completion has no owned fence");
    }
    if (completed_fence_value() < pointer_depth_capture_fence_) {
        return false;
    }
    rgba = pointer_depth_pending_rgba_;
    pointer_depth_capture_fence_ = 0u;
    return true;
}

bool RendererD3D12::ensure_async_peek_field_resources(
    AsyncPeekField& field, EfbPeekKind kind) {
    if (field.allocator && field.command_list && field.target &&
        field.readbacks[0] && field.rtv_heap) {
        return true;
    }
    // The conversion pipeline, root signature and EFB views are shared with
    // the synchronous peek path; everything else here is private.
    if (!ensure_peek_resources()) {
        return false;
    }
    const bool is_depth = kind == EfbPeekKind::Depth;
    d3d_check(
        device_->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&field.allocator)),
        "async peek field command allocator");
    d3d_check(
        device_->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            field.allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&field.command_list)),
        "async peek field command list");
    d3d_check(field.command_list->Close(), "async peek field list close");
    field.command_list->SetName(
        is_depth ? L"GxAsyncDepthFieldList" : L"GxAsyncColorFieldList");

    D3D12_HEAP_PROPERTIES default_heap{};
    default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC target_desc{};
    target_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    target_desc.Width = kEfbWidth;
    target_desc.Height = kEfbHeight;
    target_desc.DepthOrArraySize = 1;
    target_desc.MipLevels = 1;
    target_desc.Format = kEfbColorFormat;
    target_desc.SampleDesc.Count = 1;
    target_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    target_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE target_clear{};
    target_clear.Format = kEfbColorFormat;
    target_clear.Color[3] = 1.0f;
    d3d_check(
        device_->CreateCommittedResource(
            &default_heap,
            D3D12_HEAP_FLAG_NONE,
            &target_desc,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            &target_clear,
            IID_PPV_ARGS(&field.target)),
        "async peek field target");

    D3D12_HEAP_PROPERTIES readback_heap{};
    readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readback_desc{};
    readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readback_desc.Width = static_cast<UINT64>(kEfbWidth) * 4u * kEfbHeight;
    readback_desc.Height = 1;
    readback_desc.DepthOrArraySize = 1;
    readback_desc.MipLevels = 1;
    readback_desc.SampleDesc.Count = 1;
    readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (std::size_t slot = 0; slot < kAsyncPeekFieldSlots; ++slot) {
        d3d_check(
            device_->CreateCommittedResource(
                &readback_heap,
                D3D12_HEAP_FLAG_NONE,
                &readback_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&field.readbacks[slot])),
            "async peek field readback");
        field.rgba[slot] = pin_readback_mapping(field.readbacks[slot].Get());
        if (field.rgba[slot] == nullptr) {
            throw std::runtime_error("async peek field readback could not be mapped");
        }
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.NumDescriptors = 1;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    d3d_check(
        device_->CreateDescriptorHeap(
            &rtv_heap_desc, IID_PPV_ARGS(&field.rtv_heap)),
        "async peek field RTV heap");
    field.rtv = field.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    device_->CreateRenderTargetView(field.target.Get(), nullptr, field.rtv);
    return true;
}

std::uint64_t RendererD3D12::submit_async_peek_field(
    std::size_t channel, EfbPeekKind kind, std::size_t slot) {
    AsyncPeekField& field = async_peek_fields_[channel];
    if (field.fence != 0u || slot >= kAsyncPeekFieldSlots) {
        return 0u;
    }
    if (!ensure_async_peek_field_resources(field, kind)) {
        return 0u;
    }
    ID3D12GraphicsCommandList* const list = field.command_list.Get();
    d3d_check(field.allocator->Reset(), "async peek field allocator reset");
    d3d_check(
        list->Reset(field.allocator.Get(), nullptr),
        "async peek field command list reset");
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap()};
    list->SetDescriptorHeaps(1, heaps);
    record_async_peek_field_commands(list, field, kind, slot);
    d3d_check(list->Close(), "async peek field command list close");
    ID3D12CommandList* lists[] = {list};
    queue_->ExecuteCommandLists(1, lists);
    const std::uint64_t fence_value = ++next_fence_value_;
    const HRESULT signal_hr = queue_->Signal(fence_.Get(), fence_value);
    if (FAILED(signal_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(signal_hr, "async peek field queue signal");
    }
    field.fence = fence_value;
    return fence_value;
}

bool RendererD3D12::record_async_peek_field_in_frame(
    std::size_t channel, EfbPeekKind kind, std::size_t slot) {
    AsyncPeekField& field = async_peek_fields_[channel];
    if (field.fence != 0u || slot >= kAsyncPeekFieldSlots ||
        !ensure_async_peek_field_resources(field, kind)) {
        return false;
    }
    record_async_peek_field_commands(command_list_.Get(), field, kind, slot);
    // Restore the GX drawing state exactly as record_conversion_blit does.
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    invalidate_gx_bindings();
    bind_efb(nullptr);
    field.fence = kAsyncPeekInFrame;  // fenced at end_frame
    return true;
}

void RendererD3D12::record_async_peek_field_commands(
    ID3D12GraphicsCommandList* list, AsyncPeekField& field, EfbPeekKind kind, std::size_t slot) {
    settle_owed_efb();
    const bool is_depth = kind == EfbPeekKind::Depth;
    resolve_efb_on(list, !is_depth, is_depth);
    constexpr UINT width = kEfbWidth;
    constexpr UINT height = kEfbHeight;
    constexpr UINT row_pitch = kEfbWidth * 4u;
    static_assert(row_pitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0u);
    ID3D12Resource* const target = field.target.Get();
    ID3D12Resource* const readback = field.readbacks[slot].Get();
    ID3D12Resource* const src = is_depth ? efb_depth_.Get() : efb_color_.Get();

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = src;
    barriers[0].Transition.StateBefore =
        is_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                 : D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(2, barriers);

    list->OMSetRenderTargets(1, &field.rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetGraphicsRootSignature(s_conv_root_sig.Get());
    list->SetPipelineState(conv_pipeline_.Get());
    const EfbPeekSamplingGeometry sampling =
        compute_efb_peek_sampling_geometry(0u, 0u, efb_scale_);
    EfbConversionShaderConstants constants =
        make_efb_peek_conversion_constants(sampling, kind);
    constants.values[15] = -1.0f;
    list->SetGraphicsRoot32BitConstants(0, 16, constants.values.data(), 0);
    list->SetGraphicsRootDescriptorTable(
        1, is_depth ? conv_efb_depth_srv_ : conv_efb_color_srv_);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);

    std::swap(barriers[0].Transition.StateBefore, barriers[0].Transition.StateAfter);
    std::swap(barriers[1].Transition.StateBefore, barriers[1].Transition.StateAfter);
    list->ResourceBarrier(2, barriers);

    D3D12_TEXTURE_COPY_LOCATION src_copy{};
    src_copy.pResource = target;
    src_copy.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst_copy{};
    dst_copy.pResource = readback;
    dst_copy.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_copy.PlacedFootprint.Footprint.Format = kEfbColorFormat;
    dst_copy.PlacedFootprint.Footprint.Width = width;
    dst_copy.PlacedFootprint.Footprint.Height = height;
    dst_copy.PlacedFootprint.Footprint.Depth = 1;
    dst_copy.PlacedFootprint.Footprint.RowPitch = row_pitch;
    list->CopyTextureRegion(&dst_copy, 0, 0, 0, &src_copy, nullptr);
}

bool RendererD3D12::try_complete_async_peek_field(std::size_t channel) {
    AsyncPeekField& field = async_peek_fields_[channel];
    if (field.fence == 0u) {
        return false;
    }
    if (completed_fence_value() < field.fence) {
        return false;
    }
    field.fence = 0u;
    return true;
}

const std::uint8_t* RendererD3D12::async_peek_field_rgba(
    std::size_t channel, std::size_t slot) const noexcept {
    return slot < kAsyncPeekFieldSlots
        ? async_peek_fields_[channel].rgba[slot] : nullptr;
}

bool RendererD3D12::read_efb_pixels(
    std::uint16_t x, std::uint16_t y, EfbPeekKind kind,
    std::uint32_t& value, std::span<std::uint32_t> depth_pixels,
    bool defer_pointer_depth) {
    settle_owed_efb();
    value = 0;
    if (pointer_depth_capture_fence_ != 0u) {
        throw std::logic_error("GXPeek resources still own an unpublished pointer field");
    }
    if (x >= kEfbWidth || y >= kEfbHeight || !ensure_peek_resources()) {
        return false;
    }

    const bool field = defer_pointer_depth || !depth_pixels.empty();
    const UINT width = field ? kEfbWidth : 1u;
    const UINT height = field ? kEfbHeight : 1u;
    const UINT row_pitch = field ? kEfbWidth * 4u : peek_readback_row_pitch_;
    if (field && !ensure_pointer_depth_resources()) {
        return false;
    }
    if (field) {
        select_pointer_depth_destination();
    }
    ID3D12Resource* target = field ? pointer_depth_target_.Get() : peek_target_.Get();
    ID3D12Resource* readback = field ? pointer_depth_readback_.Get() : peek_readback_.Get();
    // Every earlier peek completed at its fence before this one could start,
    // so the descriptor can be rewritten.
    device_->CreateRenderTargetView(target, nullptr, peek_rtv_);
    const bool is_depth = kind == EfbPeekKind::Depth;
    ID3D12Resource* src = is_depth ? efb_depth_.Get() : efb_color_.Get();

    d3d_check(peek_allocator_->Reset(), "GXPeek allocator reset");
    d3d_check(
        peek_command_list_->Reset(peek_allocator_.Get(), nullptr),
        "GXPeek command list reset");
    if (efb_msaa_.samples > 1u) {
        ID3D12DescriptorHeap* resolve_heaps[] = {srv_ring_.heap()};
        peek_command_list_->SetDescriptorHeaps(1, resolve_heaps);
        resolve_efb_on(peek_command_list_.Get(), !is_depth, is_depth);
    }

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = src;
    barriers[0].Transition.StateBefore =
        is_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE
                 : D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = target;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    peek_command_list_->ResourceBarrier(2, barriers);

    // Utility and GX lists use the same heap and immutable EFB source views.
    // Frame-ring resets cannot touch them, including during a deferred field.
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap()};
    peek_command_list_->SetDescriptorHeaps(1, heaps);
    peek_command_list_->OMSetRenderTargets(1, &peek_rtv_, FALSE, nullptr);
    const D3D12_VIEWPORT kViewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    const D3D12_RECT kScissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    peek_command_list_->RSSetViewports(1, &kViewport);
    peek_command_list_->RSSetScissorRects(1, &kScissor);
    peek_command_list_->SetGraphicsRootSignature(s_conv_root_sig.Get());
    peek_command_list_->SetPipelineState(conv_pipeline_.Get());

    const EfbPeekSamplingGeometry sampling =
        compute_efb_peek_sampling_geometry(x, y, efb_scale_);
    EfbConversionShaderConstants constants =
        make_efb_peek_conversion_constants(sampling, kind);
    if (field) constants.values[15] = -1.0f;
    peek_command_list_->SetGraphicsRoot32BitConstants(
        0, 16, constants.values.data(), 0);
    peek_command_list_->SetGraphicsRootDescriptorTable(1,
        is_depth ? conv_efb_depth_srv_ : conv_efb_color_srv_);
    peek_command_list_->IASetPrimitiveTopology(
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    peek_command_list_->DrawInstanced(3, 1, 0, 0);

    std::swap(
        barriers[0].Transition.StateBefore,
        barriers[0].Transition.StateAfter);
    std::swap(
        barriers[1].Transition.StateBefore,
        barriers[1].Transition.StateAfter);
    peek_command_list_->ResourceBarrier(2, barriers);

    D3D12_TEXTURE_COPY_LOCATION src_copy{};
    src_copy.pResource = target;
    src_copy.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst_copy{};
    dst_copy.pResource = readback;
    dst_copy.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_copy.PlacedFootprint.Footprint.Format = kEfbColorFormat;
    dst_copy.PlacedFootprint.Footprint.Width = width;
    dst_copy.PlacedFootprint.Footprint.Height = height;
    dst_copy.PlacedFootprint.Footprint.Depth = 1;
    dst_copy.PlacedFootprint.Footprint.RowPitch = row_pitch;
    peek_command_list_->CopyTextureRegion(
        &dst_copy, 0, 0, 0, &src_copy, nullptr);

    d3d_check(peek_command_list_->Close(), "GXPeek command list close");
    ID3D12CommandList* lists[] = {peek_command_list_.Get()};
    queue_->ExecuteCommandLists(1, lists);

    const std::uint64_t fence_value = ++next_fence_value_;
    const HRESULT signal_hr = queue_->Signal(fence_.Get(), fence_value);
    if (FAILED(signal_hr)) {
        dump_device_removal(device_.Get());
        d3d_check(signal_hr, "GXPeek queue signal");
    }
    if (defer_pointer_depth) {
        pointer_depth_capture_fence_ = fence_value;
        return true;
    }
    wait_for_fence_value(fence_value);

    copy_efb_readback_pixels(readback, width, height, row_pitch, is_depth,
        value, depth_pixels);
    return true;
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

void RendererD3D12::copy_efb_readback_pixels(
    ID3D12Resource* readback, UINT width, UINT height, UINT row_pitch,
    bool is_depth, std::uint32_t& value,
    std::span<std::uint32_t> depth_pixels) {
    void* mapped = nullptr;
    const D3D12_RANGE read_range{
        0, static_cast<SIZE_T>(row_pitch) * height};
    d3d_check(
        readback->Map(0, &read_range, &mapped),
        "GXPeek readback map");
    const auto* rgba = static_cast<const std::uint8_t*>(mapped);
    if (!depth_pixels.empty()) {
        // RGBA8 bytes to 0x00RRGGBB (depth) or 0xAARRGGBB (colour): a byte shuffle per four pixels.
        // The full 640x528 field is converted for every published peek field and pointer-depth capture.
        const __m128i order = is_depth
            ? _mm_setr_epi8(2, 1, 0, -128, 6, 5, 4, -128, 10, 9, 8, -128, 14, 13, 12, -128)
            : _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
        for (std::size_t row = 0; row < height; ++row) {
            const std::uint8_t* source = rgba + row * row_pitch;
            std::uint32_t* destination = depth_pixels.data() + row * width;
            std::size_t col = 0;
            for (; col + 4u <= width; col += 4u) {
                const __m128i quad = _mm_loadu_si128(
                    reinterpret_cast<const __m128i*>(source + col * 4u));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + col),
                                 _mm_shuffle_epi8(quad, order));
            }
            for (; col < width; ++col) {
                const std::uint8_t* pixel = source + col * 4u;
                const std::uint32_t rgb =
                    (static_cast<std::uint32_t>(pixel[0]) << 16u) |
                    (static_cast<std::uint32_t>(pixel[1]) << 8u) | pixel[2];
                destination[col] = is_depth
                    ? rgb
                    : (static_cast<std::uint32_t>(pixel[3]) << 24u) | rgb;
            }
        }
    }
    const std::uint32_t r = rgba[0];
    const std::uint32_t g = rgba[1];
    const std::uint32_t b = rgba[2];
    const std::uint32_t a = rgba[3];
    if (is_depth) {
        value = (r << 16) | (g << 8) | b;
    } else {
        value = (a << 24) | (r << 16) | (g << 8) | b;
    }
    const D3D12_RANGE no_write{0, 0};
    readback->Unmap(0, &no_write);
}

void RendererD3D12::set_debug_capture_paths(
    std::string efb_ppm_path,
    std::string backbuffer_ppm_path) {
    next_efb_capture_path_ = std::move(efb_ppm_path);
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

bool RendererD3D12::debug_copy_efb_to_readback() {
    settle_owed_efb();
    // The readback decodes RGBA8: the HDR scene EFB's dumps are taken after its scene's end.
    if (efb_hdr_) {
        return false;
    }
    resolve_efb(false);
    debug_log_readback();
    if (readback_pending_) {
        std::cerr << "[efb-dump] skipped=prior-readback-pending path="
                  << next_efb_capture_path_ << '\n';
        return false;
    }
    const UINT w = kEfbWidth * efb_scale_;
    const UINT h = kEfbHeight * efb_scale_;
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
            std::cerr << "[efb-dump] skipped=readback-allocation-failed path="
                      << next_efb_capture_path_ << '\n';
            return false;
        }
    }

    D3D12_RESOURCE_BARRIER b{};
    b.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource   = efb_color_.Get();
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &b);

    D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
    src.pResource = efb_color_.Get();
    src.Type      = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource = readback_buf_.Get();
    dst.Type      = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format   = kEfbColorFormat;
    dst.PlacedFootprint.Footprint.Width    = w;
    dst.PlacedFootprint.Footprint.Height   = h;
    dst.PlacedFootprint.Footprint.Depth    = 1;
    dst.PlacedFootprint.Footprint.RowPitch = row_pitch;
    command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &b);
    readback_pending_ = true;
    readback_fence_value_ = 0u;
    readback_capture_path_ = next_efb_capture_path_;
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

    const UINT w = kEfbWidth * efb_scale_;
    const UINT h = kEfbHeight * efb_scale_;
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
            "efb-dump",
            readback_capture_path_,
            base,
            w,
            h,
            row_pitch);
    }

    readback_buf_->Unmap(0, &no_write);
    mapped_active = false;
    readback_capture_path_.clear();
    std::cerr << "[efb-dump] nonblack=" << nonblack << "/" << samples
              << " alpha=" << alpha_hits
              << " mean=(" << rs / samples << "," << gs / samples << ","
              << bs / samples << ") max=("
              << static_cast<unsigned>(rmax) << ","
              << static_cast<unsigned>(gmax) << ","
              << static_cast<unsigned>(bmax) << ","
              << static_cast<unsigned>(amax) << ")\n";
}

bool RendererD3D12::present(
    const XfbTexture* source, bool capture_backbuffer, bool black) {
    const bool can_blit =
        source != nullptr && source->texture && blit_pipeline_ &&
        s_blit_root_sig;
    ++presented_frames_;
    if (!can_blit) {
        ++telemetry_no_xfb_present_count_;
        throw std::runtime_error(
            "present requested without a valid XFB blit source");
    }
    if (capture_backbuffer) {
        if (capture_waits_) {
            // Sequence captures: every present is captured, so the previous
            // readback is waited for instead of skipping this one.
            if (selected_xfb_readback_pending_ && selected_xfb_readback_fence_ != 0u) {
                wait_for_fence_value(selected_xfb_readback_fence_);
            }
            if (backbuffer_readback_pending_ && backbuffer_readback_fence_value_ != 0u) {
                wait_for_fence_value(backbuffer_readback_fence_value_);
            }
        }
        debug_log_backbuffer_readback();
        debug_log_selected_xfb_readback();
        if (backbuffer_readback_pending_ || selected_xfb_readback_pending_ ||
            next_backbuffer_capture_path_.empty()) {
            std::cerr << "[backbuffer-dump] skipped="
                      << (next_backbuffer_capture_path_.empty()
                              ? "no-output-path" : "prior-readback-pending")
                      << " path=" << next_backbuffer_capture_path_ << '\n';
            capture_backbuffer = false;
        } else {
            capture_backbuffer = debug_copy_selected_xfb_to_readback(*source);
        }
    }

    const auto ultrawide =
        galaxy::experimental_effective_host_aspect();
    // A frame copied before a live internal-resolution change is still a
    // full-width copy at the scale it was made with.
    const unsigned source_scale = source->width / kEfbWidth;
    if (ultrawide &&
        (source_scale == 0u ||
         source->width != kEfbWidth * source_scale ||
         source->height == 0u ||
         source->height > kEfbHeight * source_scale)) {
        throw std::runtime_error(
            "RMGE01 ultrawide aspect requires a full-width Wii XFB copy");
    }

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
        ultrawide ? ultrawide->content_aspect : galaxy::kWiiContentAspect,source->prerecorded_movie);
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
        const float kSafety[4] = {source->safety_surround, source->safety_surround,
                                 source->safety_surround, 1.0f};
        const float* clear_color = black ? kBlack
            : source->safety_surround > 0.0f ? kSafety : kBlack;
        command_list_->ClearRenderTargetView(
            back_rtv, clear_color, 0, nullptr);
        ++telemetry_backbuffer_clear_count_;
    }

    if (source->copy_serial != last_presented_xfb_serial_) {
        last_presented_xfb_serial_ = source->copy_serial;
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
    // the overlays and GX recording expect the sampler heap.
    ID3D12DescriptorHeap* heaps[] = {srv_ring_.heap(), sampler_heap_.Get()};
    const GpuPassScope present_pass{*this, GpuPass::Present};
    command_list_->SetDescriptorHeaps(2, heaps);
    if (black) {
        // The VI shows black (VISetBlack): the XFB is not scanned out.
    } else if (post_process_.ready()) {
        // The picture: an XFB copy's whole texture, or the top rows of a handed-over EFB.
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
        // One-slot SRV in the frame's ring for the XFB texture.
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
    invalidate_gx_bindings();
    if (!black) {
        present_fg_hudless(*source, vp, sci);
    }

    // The overlays are drawn in backbuffer pixels, not Wii content coordinates:
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

// Captures the presented XFB itself, in the same command list (and fence) as the
// final blit. The source is held until the copy completes; nothing waits here.
bool RendererD3D12::debug_copy_selected_xfb_to_readback(const XfbTexture& source) {
    debug_log_selected_xfb_readback();
    if (selected_xfb_readback_pending_ || next_backbuffer_capture_path_.empty()) {
        std::cerr << "[selected-xfb-capture] skipped="
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
        throw std::runtime_error("selected XFB capture has inconsistent extent/format");
    }
    const UINT pitch = (source.width * 4u + 255u) & ~255u;
    const UINT64 bytes = static_cast<UINT64>(pitch) * source.height;
    if (!selected_xfb_readback_buf_ || selected_xfb_readback_buf_->GetDesc().Width < bytes) {
        selected_xfb_readback_buf_.Reset();
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
            IID_PPV_ARGS(&selected_xfb_readback_buf_)), "Create selected XFB readback");
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
    dst.pResource = selected_xfb_readback_buf_.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width = source.width;
    dst.PlacedFootprint.Footprint.Height = source.height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = pitch;
    // The picture's rows (a handed-over EFB is taller).
    const D3D12_BOX picture{0u, 0u, 0u, source.width, source.height, 1u};
    command_list_->CopyTextureRegion(&dst, 0, 0, 0, &src, &picture);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);
    selected_xfb_readback_source_ = source.texture;
    selected_xfb_readback_width_ = source.width;
    selected_xfb_readback_height_ = source.height;
    selected_xfb_readback_pitch_ = pitch;
    selected_xfb_readback_path_ = next_backbuffer_capture_path_ + ".xfb.ppm";
    selected_xfb_readback_fence_ = 0u;
    selected_xfb_readback_pending_ = true;
    std::cerr << "[selected-xfb-capture] addr=0x" << std::hex << source.guest_addr
              << " resource=0x" << reinterpret_cast<std::uintptr_t>(source.texture.Get())
              << std::dec << " serial=" << source.copy_serial << " stamp=" << source.frame_stamp
              << " extent=" << source.width << 'x' << source.height
              << " path=" << selected_xfb_readback_path_ << '\n';
    return true;
}

void RendererD3D12::debug_log_selected_xfb_readback() {
    if (!selected_xfb_readback_pending_ || selected_xfb_readback_fence_ == 0u ||
        completed_fence_value() < selected_xfb_readback_fence_) return;
    const SIZE_T bytes = static_cast<SIZE_T>(selected_xfb_readback_pitch_) *
        selected_xfb_readback_height_;
    void* mapped = nullptr;
    const D3D12_RANGE range{0, bytes};
    d3d_check(selected_xfb_readback_buf_->Map(0, &range, &mapped), "Map selected XFB readback");
    try {
        queue_debug_ppm_capture("selected-xfb-dump", selected_xfb_readback_path_,
            static_cast<const std::uint8_t*>(mapped), selected_xfb_readback_width_,
            selected_xfb_readback_height_, selected_xfb_readback_pitch_);
    } catch (...) {
        const D3D12_RANGE no_write{0, 0};
        selected_xfb_readback_buf_->Unmap(0, &no_write);
        throw;
    }
    const D3D12_RANGE no_write{0, 0};
    selected_xfb_readback_buf_->Unmap(0, &no_write);
    selected_xfb_readback_pending_ = false;
    selected_xfb_readback_fence_ = 0u;
    selected_xfb_readback_source_.Reset();
    selected_xfb_readback_path_.clear();
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

}  // namespace galaxy::gx
