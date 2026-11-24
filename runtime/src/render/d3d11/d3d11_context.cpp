// SPDX-License-Identifier: GPL-3.0-only
// d3d11::Context (d3d11/d3d11_context.h): the device, the frame fence, textures, upload memory and its address space; and
// the gpu:: functions gpu_types.h declares for every back end.
#include "galaxy/render/d3d11/d3d11_context.h"

#include "galaxy/launch_args.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_choice.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace galaxy::render::d3d11 {

namespace {

std::string narrow(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(bytes > 0 ? static_cast<std::size_t>(bytes - 1) : 0u, '\0');
    if (bytes > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), bytes, nullptr, nullptr);
    }
    return out;
}

const char* level_name(D3D_FEATURE_LEVEL level) {
    switch (level) {
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
    case D3D_FEATURE_LEVEL_10_1: return "10_1";
    case D3D_FEATURE_LEVEL_10_0: return "10_0";
    default: return "9_x";
    }
}

void set_name(ID3D11DeviceChild* object, const char* name) {
    if (object != nullptr && name != nullptr) {
        object->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen(name)), name);
    }
}

std::uint64_t round_up(std::uint64_t value, std::uint64_t step) {
    return (value + step - 1u) / step * step;
}

}  // namespace

Context& Context::get() noexcept {
    static Context context;
    return context;
}

bool Context::initialize(const std::string& chosen_gpu, bool debug_layer) {
    if (device_ != nullptr) {
        return true;
    }
    failure_.clear();
    // CreateDXGIFactory2 is Windows 8+; on Windows 7 (with Platform Update KB2670838) use CreateDXGIFactory1 and
    // QueryInterface for IDXGIFactory2. Without the Platform Update there is no IDXGIFactory2, and the renderer falls
    // back to the legacy bitblt swap chain (create_swap_chain tests caps_.flip_model, which is false then).
    ComPtr<IDXGIFactory1> factory1;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory1)))) {
        failure_ = "No DXGI factory";
        return false;
    }
    ComPtr<IDXGIFactory2> factory2;
    if (SUCCEEDED(factory1.As(&factory2))) {
        factory_ = factory2;
    } else {
        factory_.Reset();  // no 1.2 factory on Win 7 without KB2670838
    }
    // Adapters in the system's order for performance where DXGI 1.6 gives it, else its plain order; software ones never.
    std::vector<ComPtr<IDXGIAdapter1>> adapters;
    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(factory1.As(&factory6))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                             IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++i) {
            adapters.push_back(adapter);
            adapter.Reset();
        }
    } else {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory1->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            adapters.push_back(adapter);
            adapter.Reset();
        }
    }
    {
        // Every hardware adapter, in that order, for the System page's GRAPHICS CARD choice.
        std::vector<GpuChoice> choices;
        for (const ComPtr<IDXGIAdapter1>& adapter : adapters) {
            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(adapter->GetDesc1(&d)) && (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0u) {
                choices.push_back(GpuChoice{gpu_identity(d.VendorId, d.DeviceId, d.SubsysId), narrow(d.Description)});
            }
        }
        record_gpu_choices(std::move(choices));
    }
    // The chosen adapter first, when the settings name one (config.ini gpu=, as gpu_identity writes it: Direct3D 12's
    // and Vulkan's rule).
    if (!chosen_gpu.empty()) {
        const auto chosen = [&](const ComPtr<IDXGIAdapter1>& a) {
            DXGI_ADAPTER_DESC1 d{};
            return SUCCEEDED(a->GetDesc1(&d)) && gpu_identity(d.VendorId, d.DeviceId, d.SubsysId) == chosen_gpu;
        };
        const bool present = std::any_of(adapters.begin(), adapters.end(), chosen);
        std::stable_partition(adapters.begin(), adapters.end(), chosen);
        std::cout << "[gpu] chosen " << chosen_gpu << (present ? " first" : " not present; choosing automatically") << '\n';
    }
    std::string refused;
    for (const ComPtr<IDXGIAdapter1>& adapter : adapters) {
        DXGI_ADAPTER_DESC1 desc{};
        // A software adapter is taken only by the start check (launch_args.h start_check), after every hardware one, or by
        // --software-device, which takes no other (renderer_choice.h adapter_allowed).
        if (FAILED(adapter->GetDesc1(&desc)) ||
            !adapter_allowed((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u, ::galaxy::launch_args().software_device,
                             ::galaxy::launch_args().start_check != 0u)) {
            continue;
        }
        const std::string name = narrow(desc.Description);
        static constexpr D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                        D3D_FEATURE_LEVEL_10_0};
        UINT flags = debug_layer ? D3D11_CREATE_DEVICE_DEBUG : 0u;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level{};
        HRESULT hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, kLevels,
                                       static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION, &device, &level, &context);
        if (FAILED(hr) && debug_layer) {
            // The debug layer is not installed (Graphics Tools): the device without it.
            flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
            hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, kLevels,
                                   static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION, &device, &level, &context);
        }
        if (FAILED(hr)) {
            continue;
        }
        // What the renderer needs (d3d11_refusal: feature level 11_0, the 11.1 interfaces, constant buffers bound at an
        // offset and written in part, as the draws' rows live in shared buffers); the renderer check judges every card
        // by the same rule.
        D3D11Facts facts;
        facts.device = true;
        facts.feature_level = static_cast<unsigned>(level);
        ComPtr<ID3D11Device1> device1;
        ComPtr<ID3D11DeviceContext1> context1;
        facts.device1 = SUCCEEDED(device.As(&device1)) && SUCCEEDED(context.As(&context1));
        D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        facts.constant_buffer_offsets =
            facts.device1 && SUCCEEDED(device1->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) &&
            options.ConstantBufferOffsetting && options.ConstantBufferPartialUpdate;
        if (const RendererRefusal refusal = d3d11_refusal(facts); refusal != RendererRefusal::kNone) {
            refused = name + ": " + (refusal == RendererRefusal::kFeatureLevel
                                         ? "feature level " + std::string(level_name(level)) + ", 11_0 needed"
                                         : std::string(refusal_reason(refusal)));
            std::cout << "[gpu] passed over " << refused << '\n';
            continue;
        }
        adapter_ = adapter;
        device_ = device;
        immediate_ = context;
        if (facts.device1) {
            device1_ = device1;
            immediate1_ = context1;
        } else {
            device1_.Reset();
            immediate1_.Reset();
        }
        device_name_ = name;
        caps_ = DeviceCaps{};
        caps_.level = level;
        caps_.vendor = desc.VendorId;
        caps_.dedicated_bytes = desc.DedicatedVideoMemory;
        caps_.unified_memory = unified_memory(desc.DedicatedVideoMemory);
        caps_.have_d3d11
