// SPDX-License-Identifier: GPL-3.0-only
// d3d11::Context (d3d11/d3d11_context.h): the device, the frame fence, textures, upload memory and its address space; and
// the gpu:: functions gpu_types.h declares for every back end.
#include "galaxy/render/d3d11/d3d11_context.h"

#include "galaxy/launch_args.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/renderer_choice.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>
#include <utility>
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

bool starts_with_nocase(const std::string& text, const char* prefix) {
    const std::size_t n = std::strlen(prefix);
    return text.size() >= n && _strnicmp(text.c_str(), prefix, n) == 0;
}

struct OsVersionInfo {
    ULONG size;
    ULONG major, minor, build, platform;
    WCHAR service_pack[128];
};

// What the process runs on and every variable that could change its behaviour: the Windows version and all NEBULA_*
// variables, with the graphics layers' and compatibility ones.
void log_environment() {
    OsVersionInfo os{};
    os.size = sizeof(os);
    using RtlGetVersionFn = LONG(WINAPI*)(OsVersionInfo*);
    if (const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (const auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            fn(&os);
        }
    }
    std::cout << "[env] Windows " << os.major << '.' << os.minor << " build " << os.build << ' ' << narrow(os.service_pack)
              << (sizeof(void*) == 8u ? " x64" : " x86") << '\n';
    static constexpr const char* kShown[] = {"NEBULA_", "OS=", "PROCESSOR_ARCHITECTURE=", "PROCESSOR_IDENTIFIER=", "NUMBER_OF_PROCESSORS=",
                                             "__COMPAT_LAYER=", "DXGI", "D3D", "VK_", "MESA", "DXVK", "VKD3D", "SteamAppId",
                                             "SDL_", "ENABLE_", "DISABLE_"};
    int shown = 0;
    if (wchar_t* const block = GetEnvironmentStringsW()) {
        for (const wchar_t* entry = block; *entry != L'\0'; entry += std::wcslen(entry) + 1u) {
            if (*entry == L'=') {
                continue;  // the per-drive current directories
            }
            const std::string line = narrow(entry);
            for (const char* prefix : kShown) {
                if (starts_with_nocase(line, prefix)) {
                    std::cout << "[env] " << line << '\n';
                    ++shown;
                    break;
                }
            }
        }
        FreeEnvironmentStringsW(block);
    }
    std::cout << "[env] " << shown << " variable(s) shown\n";
}

enum class PathChoice { Auto, Level110, Level111 };

// NEBULA_D3D11_PATH: "11_0" never uses the 11.1 interfaces; "11_1" also asks for feature level 11_1 first (11_0 if the runtime
// refuses the list); anything else, or unset: auto, which makes a feature level 11_0 device and uses the 11.1 constant
// binding where the device reports it.
PathChoice path_choice(std::string& text) {
    char buffer[64]{};
    const DWORD length = GetEnvironmentVariableA("NEBULA_D3D11_PATH", buffer, static_cast<DWORD>(sizeof(buffer)));
    if (length == 0u || length >= sizeof(buffer)) {
        text = "(unset)";
        return PathChoice::Auto;
    }
    text = buffer;
    std::string lower = text;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "11_0" || lower == "11.0" || lower == "0") {
        return PathChoice::Level110;
    }
    if (lower == "11_1" || lower == "11.1" || lower == "1") {
        return PathChoice::Level111;
    }
    return PathChoice::Auto;
}

const char* choice_name(PathChoice choice) {
    return choice == PathChoice::Level110 ? "11_0" : choice == PathChoice::Level111 ? "11_1" : "auto";
}

// Everything the device says about itself, to the log; and the 11.1 constants path decided from it.
void probe_features(ID3D11Device* device, ID3D11DeviceContext* context, PathChoice choice, const DXGI_ADAPTER_DESC1& desc,
                    ComPtr<ID3D11Device1>& device1, ComPtr<ID3D11DeviceContext1>& context1, DeviceCaps& caps) {
    const auto yes = [](BOOL value) { return value != FALSE ? 1 : 0; };
    std::cout << "[gpu] adapter vendor=0x" << std::hex << desc.VendorId << " device=0x" << desc.DeviceId << " subsys=0x" << desc.SubSysId
              << " revision=0x" << desc.Revision << std::dec << " dedicated-video=" << desc.DedicatedVideoMemory / (1024u * 1024u)
              << "MB dedicated-system=" << desc.DedicatedSystemMemory / (1024u * 1024u)
              << "MB shared-system=" << desc.SharedSystemMemory / (1024u * 1024u) << "MB flags=0x" << std::hex << desc.Flags << std::dec << '\n';
    std::cout << "[gpu] device feature-level=0x" << std::hex << static_cast<unsigned>(device->GetFeatureLevel()) << " creation-flags=0x"
              << device->GetCreationFlags() << std::dec << '\n';
    {
        D3D11_FEATURE_DATA_THREADING threading{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_THREADING, &threading, sizeof(threading)))) {
            std::cout << "[gpu] threading concurrent-creates=" << yes(threading.DriverConcurrentCreates)
                      << " command-lists=" << yes(threading.DriverCommandLists) << '\n';
        }
        D3D11_FEATURE_DATA_DOUBLES doubles{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles)))) {
            std::cout << "[gpu] doubles=" << yes(doubles.DoublePrecisionFloatShaderOps) << '\n';
        }
        D3D11_FEATURE_DATA_ARCHITECTURE_INFO architecture{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_ARCHITECTURE_INFO, &architecture, sizeof(architecture)))) {
            std::cout << "[gpu] tile-based-deferred=" << yes(architecture.TileBasedDeferredRenderer) << '\n';
        }
        D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT precision{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, &precision, sizeof(precision)))) {
            std::cout << "[gpu] min-precision pixel=0x" << std::hex << precision.PixelShaderMinPrecision << " other=0x"
                      << precision.AllOtherShaderStagesMinPrecision << std::dec << '\n';
        } else {
            std::cout << "[gpu] min-precision: not reported (Direct3D 11.0 runtime or driver)\n";
        }
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    const bool have_options = SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)));
    if (have_options) {
        std::cout << "[gpu] d3d11-options logic-op=" << yes(options.OutputMergerLogicOp)
                  << " uav-only-forced-sample-count=" << yes(options.UAVOnlyRenderingForcedSampleCount)
                  << " discard-apis=" << yes(options.DiscardAPIsSeenByDriver)
                  << " update-copy-flags=" << yes(options.FlagsForUpdateAndCopySeenByDriver) << " clear-view=" << yes(options.ClearView)
                  << " copy-with-overlap=" << yes(options.CopyWithOverlap)
                  << " cb-partial-update=" << yes(options.ConstantBufferPartialUpdate)
                  << " cb-offsetting=" << yes(options.ConstantBufferOffsetting)
                  << " map-no-overwrite-cb=" << yes(options.MapNoOverwriteOnDynamicConstantBuffer)
                  << " map-no-overwrite-srv=" << yes(options.MapNoOverwriteOnDynamicBufferSRV)
                  << " msaa-rtv-sample-one=" << yes(options.MultisampleRTVWithForcedSampleCountOne)
                  << " sad4=" << yes(options.SAD4ShaderInstructions) << " ext-doubles=" << yes(options.ExtendedDoublesShaderInstructions)
                  << " ext-resource-sharing=" << yes(options.ExtendedResourceSharing) << '\n';
    } else {
        std::cout << "[gpu] d3d11-options: not reported (the runtime or driver has no Direct3D 11.1 options)\n";
    }
    {
        D3D11_FEATURE_DATA_D3D11_OPTIONS1 options1{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &options1, sizeof(options1)))) {
            std::cout << "[gpu] d3d11-options1 tiled-tier=" << static_cast<int>(options1.TiledResourcesTier)
                      << " min-max-filtering=" << yes(options1.MinMaxFiltering)
                      << " clear-view-depth-only=" << yes(options1.ClearViewAlsoSupportsDepthOnlyFormats)
                      << " map-on-default-buffers=" << yes(options1.MapOnDefaultBuffers) << '\n';
        }
        D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2{};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options2, sizeof(options2)))) {
            std::cout << "[gpu] d3d11-options2 ps-stencil-ref=" << yes(options2.PSSpecifiedStencilRefSupported)
                      << " typed-uav-additional-formats=" << yes(options2.TypedUAVLoadAdditionalFormats)
                      << " rovs=" << yes(options2.ROVsSupported) << " conservative-raster-tier=" << static_cast<int>(options2.ConservativeRasterizationTier)
                      << " map-on-default-textures=" << yes(options2.MapOnDefaultTextures)
                      << " standard-swizzle=" << yes(options2.StandardSwizzle)
                      << " unified-memory=" << yes(options2.UnifiedMemoryArchitecture) << '\n';
        }
    }
    // The 11.1 interfaces, on the same device (Windows 7 with the Platform Update has them at feature level 11_0).
    ComPtr<ID3D11Device1> found_device1;
    ComPtr<ID3D11DeviceContext1> found_context1;
    const HRESULT hr_device1 = device->QueryInterface(IID_PPV_ARGS(&found_device1));
    const HRESULT hr_context1 = context->QueryInterface(IID_PPV_ARGS(&found_context1));
    std::cout << "[gpu] ID3D11Device1 hr=0x" << std::hex << static_cast<unsigned>(hr_device1) << " ID3D11DeviceContext1 hr=0x"
              << static_cast<unsigned>(hr_context1) << std::dec << '\n';
    const bool interfaces = SUCCEEDED(hr_device1) && SUCCEEDED(hr_context1);
    const bool allowed = choice != PathChoice::Level110;
    const bool offsetting = allowed && interfaces && have_options && options.ConstantBufferOffsetting != FALSE;
    caps.offsetting = offsetting;
    caps.partial_update = offsetting && options.ConstantBufferPartialUpdate != FALSE;
    caps.no_overwrite_constants = offsetting && options.MapNoOverwriteOnDynamicConstantBuffer != FALSE;
    caps.path = offsetting ? "11_1" : "11_0";
    if (offsetting) {
        device1 = found_device1;
        context1 = found_context1;
    } else {
        device1.Reset();
        context1.Reset();
    }
    std::cout << "[gpu] constants path " << caps.path << " (NEBULA_D3D11_PATH choice " << choice_name(choice) << "): "
              << (!allowed          ? "the 11_0 path was asked for"
                  : !interfaces     ? "no ID3D11Device1 / ID3D11DeviceContext1"
                  : !have_options   ? "the device does not report its 11.1 options"
                  : !offsetting     ? "the device reports no ConstantBufferOffsetting"
                                    : "ConstantBufferOffsetting reported")
              << "; partial-update=" << (caps.partial_update ? 1 : 0) << " no-overwrite-constants=" << (caps.no_overwrite_constants ? 1 : 0) << '\n';
}

// A texture or view that could not be made, said (the first few): a texture that is missing draws as the null one.
void log_texture_failure(const char* step, const char* name, DXGI_FORMAT format, std::uint32_t width, std::uint32_t height,
                         std::uint32_t bind, HRESULT hr) {
    static int said = 0;
    if (said++ >= 40) {
        return;
    }
    std::cout << "[gpu] texture " << (name != nullptr ? name : "?") << ' ' << width << 'x' << height << " format=" << static_cast<unsigned>(format)
              << " bind=0x" << std::hex << bind << ": " << step << " failed 0x" << static_cast<unsigned>(hr) << std::dec << '\n';
}

// Adds the time between its making and its end, in QueryPerformanceCounter ticks, to `sink`.
struct TickSpan {
    explicit TickSpan(std::uint64_t& sink_in) noexcept : sink(sink_in) {
        QueryPerformanceCounter(&start);
    }
    ~TickSpan() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        sink += static_cast<std::uint64_t>(now.QuadPart - start.QuadPart);
    }
    TickSpan(const TickSpan&) = delete;
    TickSpan& operator=(const TickSpan&) = delete;
    std::uint64_t& sink;
    LARGE_INTEGER start{};
};

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
    log_environment();
    std::string choice_text;
    const PathChoice choice = path_choice(choice_text);
    std::cout << "[gpu] NEBULA_D3D11_PATH=" << choice_text << " -> " << choice_name(choice) << '\n';
    // WDDM 1.1 (Windows 7): DXGI 1.1 is all there is, so the factory is an IDXGIFactory1 (CreateDXGIFactory1 is in
    // Windows Vista SP2 / 7's dxgi.dll) and the swap chain is the bit-block-transfer one (create_swap_chain).
    ComPtr<IDXGIFactory1> factory1;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory1)))) {
        failure_ = "No DXGI factory";
        return false;
    }
    factory_ = factory1;
    // Adapters in DXGI's plain order (EnumAdapters1: DXGI 1.1); software ones never, but by the start check.
    std::vector<ComPtr<IDXGIAdapter1>> adapters;
    {
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
                choices.push_back(GpuChoice{gpu_identity(d.VendorId, d.DeviceId, d.SubSysId), narrow(d.Description)});
            }
        }
        record_gpu_choices(std::move(choices));
    }
    // The chosen adapter first, when the settings name one (config.ini gpu=, as gpu_identity writes it: Direct3D 12's
    // and Vulkan's rule).
    if (!chosen_gpu.empty()) {
        const auto chosen = [&](const ComPtr<IDXGIAdapter1>& a) {
            DXGI_ADAPTER_DESC1 d{};
            return SUCCEEDED(a->GetDesc1(&d)) && gpu_identity(d.VendorId, d.DeviceId, d.SubSysId) == chosen_gpu;
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
        // Feature level 11_0 is asked for alone: a runtime without Direct3D 11.1 (Windows 7 without the Platform Update, and
        // every WDDM 1.1 driver) refuses a list that names 11_1 with E_INVALIDARG, and no level above 11_0 is used.
        static constexpr D3D_FEATURE_LEVEL kBase[] = {D3D_FEATURE_LEVEL_11_0};
        static constexpr D3D_FEATURE_LEVEL kBoth[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        UINT flags = debug_layer ? D3D11_CREATE_DEVICE_DEBUG : 0u;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level{};
        const auto make = [&](UINT make_flags) {
            HRESULT made = E_FAIL;
            if (choice == PathChoice::Level111) {
                made = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, make_flags, kBoth,
                                         static_cast<UINT>(std::size(kBoth)), D3D11_SDK_VERSION, &device, &level, &context);
                std::cout << "[gpu] " << name << ": D3D11CreateDevice with feature levels 11_1, 11_0: hr=0x" << std::hex
                          << static_cast<unsigned>(made) << std::dec << '\n';
            }
            if (FAILED(made)) {
                made = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, make_flags, kBase,
                                         static_cast<UINT>(std::size(kBase)), D3D11_SDK_VERSION, &device, &level, &context);
                std::cout << "[gpu] " << name << ": D3D11CreateDevice with feature level 11_0 (flags 0x" << std::hex << make_flags
                          << "): hr=0x" << static_cast<unsigned>(made) << std::dec << '\n';
            }
            return made;
        };
        HRESULT hr = make(flags);
        if (FAILED(hr) && debug_layer) {
            // The debug layer is not installed (Graphics Tools): the device without it.
            flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
            hr = make(flags);
        }
        if (FAILED(hr)) {
            continue;
        }
        // What the renderer needs (d3d11_refusal: feature level 11_0); the renderer check judges every card by the same rule.
        D3D11Facts facts;
        facts.device = true;
        facts.feature_level = static_cast<unsigned>(level);
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
        device_name_ = name;
        caps_ = DeviceCaps{};
        caps_.level = level;
        caps_.vendor = desc.VendorId;
        caps_.dedicated_bytes = desc.DedicatedVideoMemory;
        caps_.unified_memory = unified_memory(desc.DedicatedVideoMemory);
        caps_.flip_model = false;
        caps_.tearing = false;
        // The debug layer's messages, read back each frame (report_messages): Direct3D 11 has no callback.
        if ((flags & D3D11_CREATE_DEVICE_DEBUG) != 0u && SUCCEEDED(device.As(&info_queue_))) {
            info_queue_->SetMessageCountLimit(4096u);
        }
        std::cout << "[gpu] d3d11 debug-layer=" << (info_queue_ != nullptr ? 1 : 0) << '\n';
        probe_features(device.Get(), context.Get(), choice, desc, device1_, immediate1_, caps_);
        break;
    }
    if (device_ == nullptr && ::galaxy::launch_args().software_device) {
        // Said at the start, and no other renderer is tried in its place.
        const std::string missing = software_device_missing_line(RendererKind::kD3D11);
        std::cout << missing << '\n';
        failure_ = missing.substr(std::string("[gpu] ").size());
        factory_.Reset();
        return false;
    }
    if (device_ == nullptr) {
        note_no_device();
        failure_ = refused.empty() ? "No hardware Direct3D 11 adapter supports feature level 11_0"
                                   : "No hardware Direct3D 11 adapter can run the renderer (" + refused + ")";
        factory_.Reset();
        return false;
    }
    // The most samples every scene format takes.
    caps_.max_samples = 1u;
    for (const unsigned samples : {2u, 4u, 8u}) {
        bool all = true;
        for (const DXGI_FORMAT format : {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                         DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT_S8X24_UINT}) {
            UINT quality = 0u;
            all = all && SUCCEEDED(device_->CheckMultisampleQualityLevels(format, samples, &quality)) && quality > 0u;
        }
        if (all) {
            caps_.max_samples = samples;
        }
    }
    // Which formats the compute passes write (typed UAV stores) and read: Direct3D 11.0 guarantees only the 32-bit
    // single-channel ones, so a driver may refuse the others (a pass that needs one is then without its texture).
    for (const auto& [format, label] : {std::pair{DXGI_FORMAT_R32_FLOAT, "r32f"}, std::pair{DXGI_FORMAT_R16_FLOAT, "r16f"},
                                       std::pair{DXGI_FORMAT_R16G16_FLOAT, "rg16f"},
                                       std::pair{DXGI_FORMAT_R16G16B16A16_FLOAT, "rgba16f"},
                                       std::pair{DXGI_FORMAT_R8G8B8A8_UNORM, "rgba8"},
                                       std::pair{DXGI_FORMAT_R10G10B10A2_UNORM, "rgb10a2"}}) {
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{format, 0u};
        UINT basic = 0u;
        (void)device_->CheckFormatSupport(format, &basic);
        const bool have2 = SUCCEEDED(device_->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support)));
        std::cout << "[gpu] format " << label << ": uav=" << ((basic & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0u ? 1 : 0)
                  << " store=" << (have2 && (support.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0u ? 1 : 0)
                  << " load=" << (have2 && (support.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0u ? 1 : 0)
                  << " blend=" << ((basic & D3D11_FORMAT_SUPPORT_BLENDABLE) != 0u ? 1 : 0)
                  << " msaa=" << ((basic & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) != 0u ? 1 : 0) << '\n';
    }
    // The frame fence: ID3D11Fence where the runtime and driver have it (Direct3D 11.4), else event queries.
    {
        ComPtr<ID3D11Device5> device5;
        if (SUCCEEDED(device_.As(&device5)) && SUCCEEDED(device5->CreateFence(0u, D3D11_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
            fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        } else {
            fence_.Reset();
            fence_queries_.resize(8u);
            D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0u};
            for (ComPtr<ID3D11Query>& q : fence_queries_) {
                if (FAILED(device_->CreateQuery(&query, &q))) {
                    failure_ = "No event queries";
                    shutdown();
                    return false;
                }
            }
        }
    }
    submitted_ = completed_ = 0u;
    // The constant ring (d3d11_context.h): an arena in system memory, copied into each slot's own constant buffer when a
    // piece is bound. (WRITE_NO_OVERWRITE on a constant buffer, and bindings at an offset, are Direct3D 11.1's.)
    if (!make_constant_ring(kConstantRingBytes)) {
        failure_ = "No constant buffer for the draws' constants";
        shutdown();
        return false;
    }
    // The diagnostic switches in this process's environment, said so that a switch left set is not mistaken for the renderer.
    for (const char* name : {"NEBULA_D3D11_FULL_UPLOAD", "NEBULA_D3D11_NO_GAME_BLOOM"}) {
        if (GetEnvironmentVariableA(name, nullptr, 0u) != 0u) {
            std::cout << "[gpu] WARNING: diagnostic switch " << name << " is set"
                      << (std::strcmp(name, "NEBULA_D3D11_FULL_UPLOAD") == 0 ? " (whole-buffer uploads: slow)" : " (the game's glow is skipped)")
                      << '\n';
        }
    }
    std::cout << "[gpu] Direct3D 11 on " << device_name_ << ", feature level " << level_name(caps_.level)
              << (fence_ != nullptr ? ", fence" : ", event queries") << ", 11_0 (constants copied into slot buffers)\n";
    return true;
}

void Context::shutdown() noexcept {
    if (device_ != nullptr) {
        std::cout << "[draws] final: " << diag.draws << " draws in " << diag.flushes << " flushes, textures made " << diag.textures_made
                  << " (filled " << diag.textures_filled << "), views written " << diag.views_written << " released " << diag.views_released
                  << ", table slots " << diag.table_slots << " (missed " << diag.table_missed << "), tables run " << diag.tables_run << " (slots with a texture " << diag.run_real << ", null " << diag.run_null << "), places " << diag.places << " (failed "
                  << diag.place_failures << "), windows " << diag.windows << " (failed " << diag.window_failures << "), uploads " << diag.uploads
                  << " (" << diag.upload_bytes / 1024u << " KB)\n";
    }
    if (immediate_ != nullptr) {
        immediate_->ClearState();
        immediate_->Flush();
    }
    report_messages();
    ring_.Reset();
    ring_arena_.clear();
    ring_arena_.shrink_to_fit();
    dynamic_scratch_ = {};
    dynamic_scratch_bytes_ = {};
    copy_scratch_ = {};
    upload_scratch_ = {};
    upload_scratch_bytes_ = {};
    ring_bytes_ = 0u;
    ring_written_ = ring_done_ = 0u;
    ring_marks_.clear();
    info_queue_.Reset();
    fence_queries_.clear();
    fence_.Reset();
    if (fence_event_ != nullptr) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
    gpu_ring_.Reset();
    immediate1_.Reset();
    device1_.Reset();
    immediate_.Reset();
    device_.Reset();
    adapter_.Reset();
    factory_.Reset();
    submitted_ = completed_ = 0u;
}

unsigned Context::report_messages() noexcept {
    if (info_queue_ == nullptr) {
        return 0u;
    }
    // The tags the smoke scripts count (as [D3D12-LIVE-ERR] / [D3D12-LIVE-WARN] on Direct3D 12).
    const UINT64 count = info_queue_->GetNumStoredMessages();
    std::vector<std::byte> storage;
    for (UINT64 i = 0u; i < count; ++i) {
        SIZE_T bytes = 0u;
        if (FAILED(info_queue_->GetMessage(i, nullptr, &bytes)) || bytes == 0u) {
            continue;
        }
        storage.resize(bytes);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        if (FAILED(info_queue_->GetMessage(i, message, &bytes))) {
            continue;
        }
        const bool error = message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION ||
                           message->Severity == D3D11_MESSAGE_SEVERITY_ERROR;
        if (error || message->Severity == D3D11_MESSAGE_SEVERITY_WARNING) {
            std::fprintf(stderr, "[%s] id=%d %.*s\n", error ? "D3D11-LIVE-ERR" : "D3D11-LIVE-WARN",
                         static_cast<int>(message->ID), static_cast<int>(message->DescriptionByteLength),
                         message->pDescription);
        }
    }
    info_queue_->ClearStoredMessages();
    return static_cast<unsigned>(count);
}

std::uint64_t Context::signal() {
    const std::uint64_t value = ++submitted_;
    if (fence_ != nullptr) {
        ComPtr<ID3D11DeviceContext4> context4;
        if (SUCCEEDED(immediate_.As(&context4))) {
            context4->Signal(fence_.Get(), value);
        }
    } else if (!fence_queries_.empty()) {
        // A query's slot is used again only after its value was passed (wait keeps fewer in flight than slots).
        if (value > fence_queries_.size()) {
            wait(value - fence_queries_.size());
        }
        immediate_->End(fence_queries_[value % fence_queries_.size()].Get());
    }
    immediate_->Flush();
    return value;
}

std::uint64_t Context::completed_value() noexcept {
    if (fence_ != nullptr) {
        completed_ = std::max(completed_, fence_->GetCompletedValue());
        return completed_;
    }
    while (completed_ < submitted_) {
        BOOL done = FALSE;
        if (immediate_->GetData(fence_queries_[(completed_ + 1u) % fence_queries_.size()].Get(), &done, sizeof(done),
                                D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            done == FALSE) {
            break;
        }
        ++completed_;
    }
    return completed_;
}

bool Context::wait(std::uint64_t value) {
    const TickSpan span(diag.wait_ticks);
    if (value > submitted_) {
        value = submitted_;
    }
    if (completed_value() >= value) {
        return true;
    }
    if (fence_ != nullptr) {
        if (FAILED(fence_->SetEventOnCompletion(value, fence_event_))) {
            return false;
        }
        WaitForSingleObject(fence_event_, INFINITE);
        (void)completed_value();
        return true;
    }
    while (completed_ < value) {
        BOOL done = FALSE;
        const HRESULT hr =
            immediate_->GetData(fence_queries_[(completed_ + 1u) % fence_queries_.size()].Get(), &done, sizeof(done), 0u);
        if (FAILED(hr)) {
            return false;
        }
        if (hr == S_OK && done != FALSE) {
            ++completed_;
        } else {
            SwitchToThread();
        }
    }
    return true;
}

std::shared_ptr<Texture> Context::create_texture(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                                 DXGI_FORMAT format, std::uint32_t bind, const char* name,
                                                 std::uint32_t samples, const D3D11_SUBRESOURCE_DATA* initial) {
    if (device_ == nullptr || width == 0u || height == 0u) {
        return {};
    }
    const bool depth = format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT || format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
                       format == DXGI_FORMAT_D32_FLOAT;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = std::max(1u, levels);
    desc.ArraySize = 1u;
    // A sampled depth texture is made typeless so that it can have both views.
    desc.Format = depth && (bind & D3D11_BIND_SHADER_RESOURCE) != 0u
                      ? (format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? DXGI_FORMAT_R32G8X24_TYPELESS
                         : format == DXGI_FORMAT_D24_UNORM_S8_UINT  ? DXGI_FORMAT_R24G8_TYPELESS
                                                                    : DXGI_FORMAT_R32_TYPELESS)
                      : format;
    desc.SampleDesc.Count = std::max(1u, samples);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bind;
    auto texture = std::make_shared<Texture>();
    if (const HRESULT hr = device_->CreateTexture2D(&desc, initial, &texture->texture); FAILED(hr)) {
        log_texture_failure("CreateTexture2D", name, format, width, height, bind, hr);
        return {};
    }
    set_name(texture->texture.Get(), name);
    const bool ms = desc.SampleDesc.Count > 1u;
    if ((bind & D3D11_BIND_SHADER_RESOURCE) != 0u) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = !depth ? format
                      : format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS
                      : format == DXGI_FORMAT_D24_UNORM_S8_UINT    ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS
                                                                   : DXGI_FORMAT_R32_FLOAT;
        view.ViewDimension = ms ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = desc.MipLevels;
        if (const HRESULT hr = device_->CreateShaderResourceView(texture->texture.Get(), &view, &texture->view); FAILED(hr)) {
            log_texture_failure("shader resource view", name, format, width, height, bind, hr);
            return {};
        }
        if (depth && format != DXGI_FORMAT_D32_FLOAT) {
            view.Format = format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? DXGI_FORMAT_X32_TYPELESS_G8X24_UINT
                                                                     : DXGI_FORMAT_X24_TYPELESS_G8_UINT;
            device_->CreateShaderResourceView(texture->texture.Get(), &view, &texture->stencil);
        }
    }
    if ((bind & D3D11_BIND_RENDER_TARGET) != 0u) {
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = ms ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
        if (const HRESULT hr = device_->CreateRenderTargetView(texture->texture.Get(), &view, &texture->target); FAILED(hr)) {
            log_texture_failure("render target view", name, format, width, height, bind, hr);
            return {};
        }
    }
    if ((bind & D3D11_BIND_DEPTH_STENCIL) != 0u) {
        D3D11_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = ms ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
        if (const HRESULT hr = device_->CreateDepthStencilView(texture->texture.Get(), &view, &texture->depth); FAILED(hr)) {
            log_texture_failure("depth stencil view", name, format, width, height, bind, hr);
            return {};
        }
        if ((bind & D3D11_BIND_SHADER_RESOURCE) != 0u) {
            view.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
            device_->CreateDepthStencilView(texture->texture.Get(), &view, &texture->depth_read);
        }
    }
    if ((bind & D3D11_BIND_UNORDERED_ACCESS) != 0u) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        if (const HRESULT hr = device_->CreateUnorderedAccessView(texture->texture.Get(), &view, &texture->storage); FAILED(hr)) {
            log_texture_failure("unordered access view", name, format, width, height, bind, hr);
            return {};
        }
    }
    texture->format = format;
    texture->width = width;
    texture->height = height;
    texture->levels = desc.MipLevels;
    texture->samples = desc.SampleDesc.Count;
    // The allocation's size as the levels' bytes (Direct3D 11 does not report it).
    std::uint64_t bytes = 0u;
    const bool blocks = format == DXGI_FORMAT_BC1_UNORM;
    const std::uint32_t texel = format == DXGI_FORMAT_R16G16B16A16_FLOAT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT ? 8u
                                : format == DXGI_FORMAT_R8_UNORM                                                       ? 1u
                                : format == DXGI_FORMAT_R8G8_UNORM                                                     ? 2u
                                                                                                                       : 4u;
    for (std::uint32_t level = 0; level < desc.MipLevels; ++level) {
        const std::uint64_t w = std::max(1u, width >> level), h = std::max(1u, height >> level);
        bytes += blocks ? (w + 3u) / 4u * ((h + 3u) / 4u) * 8u : w * h * texel;
    }
    texture->allocation_bytes = bytes * desc.SampleDesc.Count;
    return texture;
}

std::shared_ptr<Buffer> Context::create_buffer(std::uint64_t size, const char*) {
    if (device_ == nullptr || size == 0u) {
        return {};
    }
    auto buffer = std::make_shared<Buffer>();
    buffer->size = round_up(size, 256u);
    buffer->shadow = static_cast<std::byte*>(
        VirtualAlloc(nullptr, buffer->size, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
    if (buffer->shadow == nullptr) {
        return {};
    }
    buffer->address = register_address(buffer.get());
    if (buffer->address == 0u) {
        return {};
    }
    std::lock_guard guard(buffers_mutex_);
    upload_buffers_.push_back(buffer.get());
    return buffer;
}

ID3D11Buffer* Buffer::bound(BufferUse use) {
    ComPtr<ID3D11Buffer>& made = gpu[static_cast<std::size_t>(use)];
    if (made == nullptr) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(size);
        desc.Usage = D3D11_USAGE_DEFAULT;
        // Constants: no bind flags. The buffer is only the source of the windows copied into the slots' constant buffers
        // (Context::bind_buffer_window), and a buffer that is not a constant buffer takes UpdateSubresource's box on
        // Direct3D 11.0 (a constant buffer must be written whole there).
        desc.BindFlags = use == BufferUse::Geometry    ? D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER
                         : use == BufferUse::Constants ? (Context::get().caps().offsetting ? D3D11_BIND_CONSTANT_BUFFER : 0u)
                                                       : D3D11_BIND_SHADER_RESOURCE;
        if (use == BufferUse::Rows) {
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = 16u;
        }
        D3D11_SUBRESOURCE_DATA data{shadow, 0u, 0u};
        if (FAILED(Context::get().device()->CreateBuffer(&desc, &data, &made))) {
            made.Reset();
        }
    }
    return made.Get();
}

ID3D11ShaderResourceView* Buffer::rows_view(std::uint64_t offset) {
    for (const auto& [at, view] : row_views) {
        if (at == offset) {
            return view.Get();
        }
    }
    ID3D11Buffer* rows = bound(BufferUse::Rows);
    if (rows == nullptr || offset >= size) {
        return nullptr;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = static_cast<UINT>(offset / 16u);
    desc.Buffer.NumElements = static_cast<UINT>((size - offset) / 16u);
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(Context::get().device()->CreateShaderResourceView(rows, &desc, &view))) {
        return nullptr;
    }
    if (row_views.size() >= kRowViews) {
        row_views.erase(row_views.begin());  // Direct3D 11 keeps a released view while bound or in flight
    }
    row_views.emplace_back(offset, view);
    return view.Get();
}

Buffer::~Buffer() {
    Context& context = Context::get();
    context.release_address(address);
    {
        std::lock_guard guard(context.buffers_mutex_);
        std::erase(context.upload_buffers_, this);
    }
    if (shadow != nullptr) {
        VirtualFree(shadow, 0u, MEM_RELEASE);
    }
}

void Context::upload_written() {
    const TickSpan span(diag.upload_ticks);
    std::unique_lock<std::mutex> guard(buffers_mutex_, std::defer_lock);
    {
        const TickSpan locking(diag.lock_ticks);
        guard.lock();
    }
    std::vector<void*>& pages = written_pages_;
    static const bool full_upload = GetEnvironmentVariableA("NEBULA_D3D11_FULL_UPLOAD", nullptr, 0u) != 0u;
    if (pages.size() < 4096u) {
        pages.resize(4096u);
    }
    for (Buffer* buffer : upload_buffers_) {
        bool any = false;
        for (const ComPtr<ID3D11Buffer>& made : buffer->gpu) {
            any = any || made != nullptr;
        }
        // No video-memory copy yet: nothing to bring up to date (Buffer::bound makes one from the whole system copy;
        // pages written before it are then copied once more, the same bytes) (P-064).
        if (!any) {
            continue;
        }
        ULONG_PTR count = pages.size();
        DWORD page_bytes = 4096u;
        // Every written page since the last call; read and reset in one call, so a write after it is seen next time.
        if (full_upload) {
            // Diagnostic (NEBULA_D3D11_FULL_UPLOAD=1): drain the written pages, then push the whole buffer if any was written.
            bool dirty = false;
            while (GetWriteWatch(WRITE_WATCH_FLAG_RESET, buffer->shadow, buffer->size, pages.data(), &count, &page_bytes) == 0u) {
                dirty = dirty || count != 0u;
                if (count < pages.size()) {
                    break;
                }
                count = pages.size();
            }
            if (dirty) {
                for (const ComPtr<ID3D11Buffer>& made : buffer->gpu) {
                    if (made != nullptr) {
                        immediate_->UpdateSubresource(made.Get(), 0u, nullptr, buffer->shadow, 0u, 0u);
                    }
                }
            }
            continue;
        }
        // The runs of consecutive written pages (byte ranges of the buffer).
        upload_runs_.clear();
        std::uint64_t total = 0u;
        ++diag.buffers_watched;
        diag.bytes_watched += buffer->size;
        while (true) {
            std::uint32_t watched;
            {
                const TickSpan watching(diag.watch_ticks);
                watched = GetWriteWatch(WRITE_WATCH_FLAG_RESET, buffer->shadow, buffer->size, pages.data(), &count, &page_bytes);
            }
            if (watched != 0u) {
                break;
            }
            std::size_t i = 0;
            while (i < count) {
                const auto* first = static_cast<const std::byte*>(pages[i]);
                std::size_t j = i + 1u;
                while (j < count && static_cast<const std::byte*>(pages[j]) == static_cast<const std::byte*>(pages[j - 1u]) + page_bytes) {
                    ++j;
                }
                const std::uint64_t begin = static_cast<std::uint64_t>(first - buffer->shadow);
                const std::uint64_t end = std::min<std::uint64_t>(begin + (j - i) * page_bytes, buffer->size);
                upload_runs_.emplace_back(begin, end);
                total += end - begin;
                i = j;
            }
            if (count < pages.size()) {
                break;
            }
            count = pages.size();
        }
        if (upload_runs_.empty()) {
            continue;
        }
        ++diag.uploads;
        diag.upload_bytes += total;
        // Direct3D 11.0's boxed UpdateSubresource of a buffer is not trusted (this path replaced it): the runs are packed
        // into a DYNAMIC buffer written with Map DISCARD (a new one for each upload, so no copy still to run reads what
        // is overwritten), and each run is copied from it into every video-memory copy with CopySubresourceRegion.
        ID3D11Buffer* const upload = upload_scratch(static_cast<std::size_t>(total));
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (upload == nullptr || FAILED(immediate_->Map(upload, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
            for (const ComPtr<ID3D11Buffer>& made : buffer->gpu) {
                if (made != nullptr) {
                    immediate_->UpdateSubresource(made.Get(), 0u, nullptr, buffer->shadow, 0u, 0u);
                }
            }
            continue;
        }
        std::uint64_t packed = 0u;
        for (const auto& [begin, end] : upload_runs_) {
            std::memcpy(static_cast<std::byte*>(mapped.pData) + packed, buffer->shadow + begin, static_cast<std::size_t>(end - begin));
            packed += end - begin;
        }
        immediate_->Unmap(upload, 0u);
        for (const ComPtr<ID3D11Buffer>& made : buffer->gpu) {
            if (made == nullptr) {
                continue;
            }
            packed = 0u;
            for (const auto& [begin, end] : upload_runs_) {
                const D3D11_BOX box{static_cast<UINT>(packed), 0u, 0u, static_cast<UINT>(packed + (end - begin)), 1u, 1u};
                immediate_->CopySubresourceRegion(made.Get(), 0u, static_cast<UINT>(begin), 0u, 0u, upload, 0u, &box);
                packed += end - begin;
            }
        }
    }
}

ID3D11Buffer* Context::upload_scratch(std::size_t bytes) noexcept {
    if (bytes == 0u) {
        return nullptr;
    }
    unsigned index = 0u;
    std::size_t want = std::size_t{16} << 10u;
    while (index + 1u < kUploadClasses && want < bytes) {
        ++index;
        want <<= 2u;
    }
    if (index + 1u == kUploadClasses) {
        // The last class grows by powers of two to the biggest upload.
        want = std::max<std::size_t>(want, upload_scratch_bytes_[index]);
        while (want < bytes) {
            want <<= 1u;
        }
    }
    if (upload_scratch_[index] == nullptr || upload_scratch_bytes_[index] < want) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(want);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;  // a dynamic buffer needs a bind flag; it is only ever copied from
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> made;
        if (FAILED(device_->CreateBuffer(&desc, nullptr, &made))) {
            return nullptr;
        }
        upload_scratch_[index] = std::move(made);
        upload_scratch_bytes_[index] = want;
    }
    return upload_scratch_[index].Get();
}

// The ring is an arena in system memory (d3d11_context.h); `ring_` is a one-constant buffer that only gives the places a
// non-null ConstantPlace::buffer to tell a piece from a refusal.
bool Context::make_constant_ring(std::size_t bytes) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = 16u;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Buffer> token;
    if (FAILED(device_->CreateBuffer(&desc, nullptr, &token))) {
        return false;
    }
    try {
        ring_arena_.assign(bytes, std::byte{0});
    } catch (const std::bad_alloc&) {
        return false;
    }
    ring_ = std::move(token);
    gpu_ring_.Reset();
    if (caps_.offsetting && immediate1_ != nullptr && (caps_.no_overwrite_constants || caps_.partial_update)) {
        // The 11.1 path: the arena's bytes are mirrored into a constant buffer bound at offsets.
        D3D11_BUFFER_DESC mirror{};
        mirror.ByteWidth = static_cast<UINT>(bytes);
        mirror.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        mirror.Usage = caps_.no_overwrite_constants ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
        mirror.CPUAccessFlags = caps_.no_overwrite_constants ? D3D11_CPU_ACCESS_WRITE : 0u;
        ComPtr<ID3D11Buffer> made;
        bool ok = SUCCEEDED(device_->CreateBuffer(&mirror, nullptr, &made));
        if (ok && caps_.no_overwrite_constants) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            ok = SUCCEEDED(immediate_->Map(made.Get(), 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped));  // the first Map discards
            if (ok) {
                immediate_->Unmap(made.Get(), 0u);
            }
        }
        if (ok) {
            gpu_ring_ = std::move(made);
        }
        std::cout << "[gpu] constant ring mirror: " << (gpu_ring_ != nullptr ? "made, " : "not made, ")
                  << (caps_.no_overwrite_constants ? "Map NO_OVERWRITE" : "UpdateSubresource1 NO_OVERWRITE") << ", " << bytes / 1024u << " KB\n";
    }
    ring_bytes_ = bytes;
    ++ring_generation_;
    ring_written_ = 0u;
    ring_done_ = 0u;
    ring_marks_.clear();
    return true;
}

std::byte* Context::begin_constants(std::size_t bytes, ConstantPlace& place) {
    place = {};
    if (ring_ == nullptr || ring_arena_.empty() || bytes == 0u) {
        return nullptr;
    }
    const std::size_t size = (bytes + 255u) & ~std::size_t{255u};
    if (size > ring_bytes_ / 4u) {
        return nullptr;  // the arena does not grow
    }
    // The piece in one run: a tail too short for it stays unused this time round.
    std::uint64_t start = ring_written_;
    if (start % ring_bytes_ + size > ring_bytes_) {
        start += ring_bytes_ - start % ring_bytes_;
    }
    // Last time round these bytes held pieces up to `needed`: the commands recorded while they were bound must have run.
    if (start + size > ring_bytes_) {
        const std::uint64_t needed = start + size - ring_bytes_;
        const auto retire = [this] {
            const std::uint64_t completed = completed_value();
            std::size_t passed = 0u;
            for (; passed < ring_marks_.size() && ring_marks_[passed].fence <= completed; ++passed) {
                ring_done_ = std::max(ring_done_, ring_marks_[passed].written);
            }
            ring_marks_.erase(ring_marks_.begin(), ring_marks_.begin() + static_cast<std::ptrdiff_t>(passed));
        };
        if (ring_done_ < needed) {
            retire();
        }
        if (ring_done_ < needed) {
            // The first mark past `needed`; its commands not yet submitted (this frame's own): submitted now.
            std::uint64_t fence = submitted_ + 1u;
            for (const RingMark& mark : ring_marks_) {
                if (mark.written >= needed) {
                    fence = mark.fence;
                    break;
                }
            }
            if (fence > submitted_) {
                fence = signal();
            }
            if (!wait(fence)) {
                return nullptr;
            }
            retire();
            if (ring_done_ < needed) {
                return nullptr;
            }
        }
    }
    const std::size_t at = static_cast<std::size_t>(start % ring_bytes_);
    ring_piece_at_ = at;
    ring_piece_bytes_ = size;
    ring_written_ = start + size;
    // The commands that bind the piece are recorded from here until the next signal: its value covers them.
    const std::uint64_t fence = submitted_ + 1u;
    if (!ring_marks_.empty() && ring_marks_.back().fence == fence) {
        ring_marks_.back().written = ring_written_;
    } else {
        ring_marks_.push_back({fence, ring_written_});
    }
    place.buffer = ring_.Get();
    place.first = static_cast<UINT>(at / 16u);
    place.count = static_cast<UINT>(size / 16u);
    return ring_arena_.data() + at;
}

void Context::end_constants() {
    // The 11_0 path: nothing to do, the arena is plain memory, read when the piece is bound. The 11.1 path mirrors the
    // piece into the constant buffer the draws bind at an offset (NO_OVERWRITE: no earlier piece's bytes are touched).
    if (gpu_ring_ != nullptr && ring_piece_bytes_ != 0u) {
        const std::size_t at = ring_piece_at_;
        const std::size_t bytes = ring_piece_bytes_;
        if (caps_.no_overwrite_constants) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(immediate_->Map(gpu_ring_.Get(), 0u, D3D11_MAP_WRITE_NO_OVERWRITE, 0u, &mapped))) {
                std::memcpy(static_cast<std::byte*>(mapped.pData) + at, ring_arena_.data() + at, bytes);
                immediate_->Unmap(gpu_ring_.Get(), 0u);
            }
        } else if (immediate1_ != nullptr) {
            const D3D11_BOX box{static_cast<UINT>(at), 0u, 0u, static_cast<UINT>(at + bytes), 1u, 1u};
            immediate1_->UpdateSubresource1(gpu_ring_.Get(), 0u, &box, ring_arena_.data() + at, 0u, 0u, D3D11_COPY_NO_OVERWRITE);
        }
    }
    ring_piece_bytes_ = 0u;
}

Context::ConstantPlace Context::put_constants(const void* data, std::size_t bytes) {
    ConstantPlace place;
    if (bytes > 65536u) {
        return place;
    }
    std::byte* const at = begin_constants(bytes, place);
    if (at == nullptr) {
        return {};
    }
    std::memcpy(at, data, bytes);
    std::memset(at + bytes, 0, static_cast<std::size_t>(place.count) * 16u - bytes);
    end_constants();
    return place;
}

ID3D11Buffer* Context::dynamic_scratch(unsigned index, std::size_t bytes) noexcept {
    if (index >= kScratchSlots || bytes == 0u || bytes > 65536u) {
        return nullptr;
    }
    std::size_t want = 256u;
    while (want < bytes) {
        want <<= 1u;
    }
    if (dynamic_scratch_[index] == nullptr || dynamic_scratch_bytes_[index] < want) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(want);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> made;
        if (FAILED(device_->CreateBuffer(&desc, nullptr, &made))) {
            return nullptr;
        }
        dynamic_scratch_[index] = std::move(made);
        dynamic_scratch_bytes_[index] = want;
    }
    return dynamic_scratch_[index].Get();
}

ID3D11Buffer* Context::copy_scratch(unsigned index) noexcept {
    if (index >= kScratchSlots) {
        return nullptr;
    }
    if (copy_scratch_[index] == nullptr) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = kMostBoundConstants * 16u;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> made;
        if (FAILED(device_->CreateBuffer(&desc, nullptr, &made))) {
            return nullptr;
        }
        copy_scratch_[index] = std::move(made);
    }
    return copy_scratch_[index].Get();
}

void Context::bind_scratch(unsigned stages, UINT slot, ID3D11Buffer* buffer) noexcept {
    if ((stages & kVertexConstants) != 0u) {
        immediate_->VSSetConstantBuffers(slot, 1u, &buffer);
    }
    if ((stages & kPixelConstants) != 0u) {
        immediate_->PSSetConstantBuffers(slot, 1u, &buffer);
    }
}

void Context::bind_place(unsigned stages, UINT slot, const ConstantPlace& place) noexcept {
    const TickSpan span(diag.bind_ticks);
    ++diag.places;
    if (gpu_ring_ != nullptr && immediate1_ != nullptr && slot < kComputeScratch && place.buffer != nullptr && place.count != 0u) {
        ID3D11Buffer* const ring = gpu_ring_.Get();
        const UINT first = place.first;
        const UINT count = std::min<UINT>(place.count, 4096u);
        if ((stages & kVertexConstants) != 0u) {
            immediate1_->VSSetConstantBuffers1(slot, 1u, &ring, &first, &count);
        }
        if ((stages & kPixelConstants) != 0u) {
            immediate1_->PSSetConstantBuffers1(slot, 1u, &ring, &first, &count);
        }
        ++diag.offset_binds;
        return;
    }
    if (slot >= kComputeScratch || place.buffer == nullptr || place.count == 0u) {
        ++diag.place_failures;
        bind_scratch(stages, slot, nullptr);
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(std::min<UINT>(place.count, 4096u)) * 16u;
    ID3D11Buffer* const scratch = dynamic_scratch(slot, bytes);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (scratch == nullptr || FAILED(immediate_->Map(scratch, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
        ++diag.place_failures;
        bind_scratch(stages, slot, nullptr);
        return;
    }
    auto* const out = static_cast<std::byte*>(mapped.pData);
    std::memcpy(out, ring_arena_.data() + static_cast<std::size_t>(place.first) * 16u, bytes);
    if (dynamic_scratch_bytes_[slot] > bytes) {
        std::memset(out + bytes, 0, dynamic_scratch_bytes_[slot] - bytes);
    }
    immediate_->Unmap(scratch, 0u);
    bind_scratch(stages, slot, scratch);
}

void Context::bind_compute_place(const ConstantPlace& place, bool clear_after) noexcept {
    ID3D11Buffer* const none = nullptr;
    immediate_->CSSetConstantBuffers(0u, 1u, &none);
    if (place.buffer == nullptr || place.count == 0u || clear_after) {
        return;
    }
    if (gpu_ring_ != nullptr && immediate1_ != nullptr) {
        ID3D11Buffer* const ring = gpu_ring_.Get();
        const UINT first = place.first;
        const UINT count = std::min<UINT>(place.count, 4096u);
        immediate1_->CSSetConstantBuffers1(0u, 1u, &ring, &first, &count);
        ++diag.offset_binds;
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(std::min<UINT>(place.count, 4096u)) * 16u;
    ID3D11Buffer* const scratch = dynamic_scratch(kComputeScratch, bytes);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (scratch == nullptr || FAILED(immediate_->Map(scratch, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped))) {
        return;
    }
    auto* const out = static_cast<std::byte*>(mapped.pData);
    std::memcpy(out, ring_arena_.data() + static_cast<std::size_t>(place.first) * 16u, bytes);
    if (dynamic_scratch_bytes_[kComputeScratch] > bytes) {
        std::memset(out + bytes, 0, dynamic_scratch_bytes_[kComputeScratch] - bytes);
    }
    immediate_->Unmap(scratch, 0u);
    immediate_->CSSetConstantBuffers(0u, 1u, &scratch);
}

void Context::bind_buffer_window(unsigned stages, UINT slot, ID3D11Buffer* source, UINT first, UINT count) noexcept {
    const TickSpan span(diag.bind_ticks);
    ++diag.windows;
    if (caps_.offsetting && immediate1_ != nullptr && slot < kComputeScratch && source != nullptr && (first & 15u) == 0u &&
        (count & ~15u) != 0u) {
        // The 11.1 path: the window as it is, 16 constants at a time (the buffer was made with the constant bind flag).
        const UINT window_count = std::min<UINT>(count & ~15u, 4096u);
        if ((stages & kVertexConstants) != 0u) {
            immediate1_->VSSetConstantBuffers1(slot, 1u, &source, &first, &window_count);
        }
        if ((stages & kPixelConstants) != 0u) {
            immediate1_->PSSetConstantBuffers1(slot, 1u, &source, &first, &window_count);
        }
        ++diag.offset_binds;
        return;
    }
    ID3D11Buffer* const scratch = slot < kComputeScratch && source != nullptr && count != 0u ? copy_scratch(slot) : nullptr;
    if (scratch == nullptr) {
        ++diag.window_failures;
        bind_scratch(stages, slot, nullptr);
        return;
    }
    const UINT n = std::min(count, kMostBoundConstants);
    const D3D11_BOX box{first * 16u, 0u, 0u, (first + n) * 16u, 1u, 1u};
    immediate_->CopySubresourceRegion(scratch, 0u, 0u, 0u, 0u, source, 0u, &box);
    bind_scratch(stages, slot, scratch);
}

void Context::verify_against_shadow(Buffer* buffer, BufferUse use, std::uint64_t offset, std::uint64_t bytes, const char* label) {
    static const bool enabled = GetEnvironmentVariableA("NEBULA_D3D11_VERIFY", nullptr, 0u) != 0u;
    if (!enabled || buffer == nullptr || buffer->shadow == nullptr || offset >= buffer->size) {
        return;
    }
    struct Tally {
        std::uint64_t checked = 0u, bad = 0u;
    };
    static std::vector<std::pair<std::string, Tally>> tallies;
    Tally* tally = nullptr;
    for (auto& [name, found] : tallies) {
        if (name == label) {
            tally = &found;
        }
    }
    if (tally == nullptr) {
        tallies.emplace_back(label, Tally{});
        tally = &tallies.back().second;
    }
    if (tally->checked >= 400u) {
        return;  // enough of each kind
    }
    ID3D11Buffer* const gpu = buffer->bound(use);
    bytes = std::min<std::uint64_t>({bytes, buffer->size - offset, 4096u});
    if (gpu == nullptr || bytes == 0u) {
        return;
    }
    static ComPtr<ID3D11Buffer> staging;
    if (staging == nullptr) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 4096u;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device_->CreateBuffer(&desc, nullptr, &staging))) {
            return;
        }
    }
    const D3D11_BOX box{static_cast<UINT>(offset), 0u, 0u, static_cast<UINT>(offset + bytes), 1u, 1u};
    immediate_->CopySubresourceRegion(staging.Get(), 0u, 0u, 0u, 0u, gpu, 0u, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(immediate_->Map(staging.Get(), 0u, D3D11_MAP_READ, 0u, &mapped))) {
        return;
    }
    const auto* video = static_cast<const std::uint8_t*>(mapped.pData);
    const std::uint8_t* system = buffer->shadow + offset;
    std::uint64_t first_difference = bytes;
    std::uint64_t different = 0u;
    for (std::uint64_t i = 0; i < bytes; ++i) {
        if (video[i] != system[i]) {
            first_difference = std::min(first_difference, i);
            ++different;
        }
    }
    ++tally->checked;
    if (different != 0u) {
        ++tally->bad;
        static int said = 0;
        if (said++ < 24) {
            std::cout << "[verify] " << label << " offset " << offset << " of " << buffer->size << ": " << different << " of " << bytes
                      << " bytes differ, first at +" << first_difference << " video=";
            for (std::uint64_t i = first_difference; i < std::min<std::uint64_t>(first_difference + 8u, bytes); ++i) {
                char hex[4];
                std::snprintf(hex, sizeof(hex), "%02x", video[i]);
                std::cout << hex;
            }
            std::cout << " system=";
            for (std::uint64_t i = first_difference; i < std::min<std::uint64_t>(first_difference + 8u, bytes); ++i) {
                char hex[4];
                std::snprintf(hex, sizeof(hex), "%02x", system[i]);
                std::cout << hex;
            }
            std::cout << '\n';
        }
    }
    immediate_->Unmap(staging.Get(), 0u);
    if (tally->checked % 100u == 0u) {
        std::cout << "[verify] " << label << ": " << tally->bad << " of " << tally->checked << " checks differed\n";
    }
}

std::uint64_t Context::register_address(Buffer* buffer) {
    std::lock_guard guard(address_mutex_);
    std::uint32_t id = 0u;
    if (!free_address_ids_.empty()) {
        id = free_address_ids_.back();
        free_address_ids_.pop_back();
    } else if (next_address_id_ < kAddressIds) {
        id = next_address_id_++;
    } else {
        return 0u;
    }
    std::atomic<AddressPage*>& slot = address_pages_[id >> kAddressPageBits];
    AddressPage* page = slot.load(std::memory_order_acquire);
    if (page == nullptr) {
        page = new AddressPage();
        slot.store(page, std::memory_order_release);
    }
    page->slots[id & (kAddressPageSize - 1u)].store(buffer, std::memory_order_release);
    return std::uint64_t{id} << kAddressShift;
}

void Context::release_address(std::uint64_t address) noexcept {
    const std::uint64_t id = address >> kAddressShift;
    if (id == 0u) {
        return;
    }
    std::lock_guard guard(address_mutex_);
    AddressPage* page = address_pages_[id >> kAddressPageBits].load(std::memory_order_acquire);
    if (page != nullptr) {
        page->slots[id & (kAddressPageSize - 1u)].store(nullptr, std::memory_order_release);
    }
    free_address_ids_.push_back(static_cast<std::uint32_t>(id));
}

}  // namespace galaxy::render::d3d11

namespace galaxy::render::gpu {

TextureRef texture_ref(TextureObject* texture) {
    return texture != nullptr ? texture->shared_from_this() : TextureRef{};
}

TextureRef create_render_texture(Device*, std::uint32_t width, std::uint32_t height) {
    return d3d11::Context::get().create_texture(width, height, 1u, DXGI_FORMAT_R8G8B8A8_UNORM,
                                                D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, "render texture");
}

UploadMemory create_upload_memory(Device*, std::size_t bytes, const char* name, UploadPlace) {
    UploadMemory memory;
    std::shared_ptr<d3d11::Buffer> buffer = d3d11::Context::get().create_buffer(bytes, name);
    if (buffer == nullptr) {
        return memory;
    }
    memory.cpu = buffer->shadow;
    memory.gpu = buffer->address;
    memory.local = false;
    memory.buffer = std::move(buffer);
    return memory;
}

bool device_removed(Device* device) noexcept {
    return device != nullptr && device->GetDeviceRemovedReason() != S_OK;
}

}  // namespace galaxy::render::gpu
