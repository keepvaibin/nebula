// SPDX-License-Identifier: GPL-3.0-only
// The renderer check (renderer_check.h): Direct3D 12, Direct3D 11 and Vulkan asked of every card the way each renderer
// asks when it chooses its device (renderer_d3d12.cpp, d3d11_context.cpp, vk_context.cpp), the answers judged by the
// same rules (renderer_choice.h). The APIs are loaded from the system folder only for the check, so no build of Nebula
// links an API it does not render with.

#include "galaxy/render/renderer_check.h"

#include "galaxy/render/display_modes.h"
#include "galaxy/render/render_config.h"
#include "galaxy/windows_paths.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11_1.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <mutex>
#include <system_error>
#include <string>
#include <thread>
#include <utility>

namespace galaxy::render {
namespace {

using Microsoft::WRL::ComPtr;

// A system library for the check's length.
class SystemLibrary {
public:
    explicit SystemLibrary(const wchar_t* name) noexcept : module_(LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {}
    ~SystemLibrary() {
        if (module_ != nullptr) FreeLibrary(module_);
    }
    SystemLibrary(const SystemLibrary&) = delete;
    SystemLibrary& operator=(const SystemLibrary&) = delete;
    template <typename Function>
    [[nodiscard]] Function find(const char* name) const noexcept {
        return module_ != nullptr ? reinterpret_cast<Function>(reinterpret_cast<void*>(GetProcAddress(module_, name))) : nullptr;
    }

private:
    HMODULE module_;
};

struct Card {
    ComPtr<IDXGIAdapter1> adapter;
    LUID luid{};
    unsigned vendor = 0u;
    unsigned device = 0u;
    CardRenderers renderers;
};

// Every hardware adapter in Windows' order for performance where DXGI 1.6 gives it, else its plain order, as the
// renderers take them; a card Windows lists twice (a virtual display adds an entry) once, in its first place.
std::vector<Card> hardware_cards(const SystemLibrary& dxgi) {
    std::vector<Card> cards;
    using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
    const auto create = dxgi.find<CreateFactory>("CreateDXGIFactory1");
    ComPtr<IDXGIFactory1> factory;
    if (create == nullptr || FAILED(create(IID_PPV_ARGS(&factory)))) return cards;
    ComPtr<IDXGIFactory6> factory6;
    const bool preference = SUCCEEDED(factory.As(&factory6));
    for (UINT index = 0u;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT found = preference ? factory6->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                                IID_PPV_ARGS(&adapter))
                                         : factory->EnumAdapters1(index, &adapter);
        if (FAILED(found)) break;
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u) continue;
        Card card;
        card.adapter = adapter;
        card.luid = description.AdapterLuid;
        card.vendor = description.VendorId;
        card.device = description.DeviceId;
        card.renderers.identity = gpu_identity(description.VendorId, description.DeviceId, description.SubSysId);
        card.renderers.name = narrow_utf8(description.Description);
        const bool listed = std::any_of(cards.begin(), cards.end(), [&](const Card& other) {
            return other.renderers.identity == card.renderers.identity;
        });
        if (!listed) cards.push_back(std::move(card));
    }
    return cards;
}

// renderer_d3d12.cpp's device choice: a device at feature level 11_0, resource binding tier 2 and shader model 6.0.
void check_d3d12(std::vector<Card>& cards) {
    const SystemLibrary d3d12(L"d3d12.dll");
    const auto create = d3d12.find<PFN_D3D12_CREATE_DEVICE>("D3D12CreateDevice");
    for (Card& card : cards) {
        if (create == nullptr) {
            card.renderers.set(RendererKind::kD3D12, RendererRefusal::kNoDriver);
            continue;
        }
        D3D12Facts facts;
        ComPtr<ID3D12Device> device;
        facts.device = SUCCEEDED(create(card.adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        if (facts.device) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
                facts.binding_tier = static_cast<unsigned>(options.ResourceBindingTier);
            }
            D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model)))) {
                facts.shader_model = static_cast<unsigned>(model.HighestShaderModel);
            }
        }
        card.renderers.set(RendererKind::kD3D12, d3d12_refusal(facts));
    }
}

// d3d11_context.cpp's device choice: feature levels 11_1 to 10_0 asked, 11_0 needed, the 11.1 interfaces and constant
// buffer offsets.
void check_d3d11(std::vector<Card>& cards) {
    const SystemLibrary d3d11(L"d3d11.dll");
    const auto create = d3d11.find<PFN_D3D11_CREATE_DEVICE>("D3D11CreateDevice");
    static constexpr D3D_FEATURE_LEVEL kLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                                    D3D_FEATURE_LEVEL_10_0};
    for (Card& card : cards) {
        if (create == nullptr) {
            card.renderers.set(RendererKind::kD3D11, RendererRefusal::kNoDriver);
            continue;
        }
        D3D11Facts facts;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level{};
        facts.device = SUCCEEDED(create(card.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0u, kLevels,
                                        static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION, &device, &level, &context));
        if (facts.device) {
            facts.feature_level = static_cast<unsigned>(level);
            ComPtr<ID3D11Device1> device1;
            ComPtr<ID3D11DeviceContext1> context1;
            facts.device1 = SUCCEEDED(device.As(&device1)) && SUCCEEDED(context.As(&context1));
            D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
            facts.constant_buffer_offsets =
                facts.device1 && SUCCEEDED(device1->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options))) &&
                options.ConstantBufferOffsetting && options.ConstantBufferPartialUpdate;
        }
        card.renderers.set(RendererKind::kD3D11, d3d11_refusal(facts));
    }
}

bool has_extension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(),
                       [name](const VkExtensionProperties& extension) { return std::strcmp(extension.extensionName, name) == 0; });
}

// vk_context.cpp's device choice: the instance it makes (the loader's version up to 1.3, the physical-device queries'
// extension where listed), then per device swapchains, Vulkan 1.1 or 1.0 with what Nebula takes from 1.1, and a
// graphics queue that presents to a window. A device is matched to its card by LUID, else by vendor and device id.
void check_vulkan(std::vector<Card>& cards) {
    const auto refuse_all = [&cards](RendererRefusal refusal) {
        for (Card& card : cards) card.renderers.set(RendererKind::kVulkan, refusal);
    };
    const SystemLibrary loader(L"vulkan-1.dll");
    const auto get = loader.find<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    if (get == nullptr) {
        refuse_all(RendererRefusal::kNoDriver);
        return;
    }
    const auto enumerate_version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(get(nullptr, "vkEnumerateInstanceVersion"));
    const auto enumerate_instance_extensions =
        reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(get(nullptr, "vkEnumerateInstanceExtensionProperties"));
    const auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(get(nullptr, "vkCreateInstance"));
    if (enumerate_instance_extensions == nullptr || create_instance == nullptr) {
        refuse_all(RendererRefusal::kNoDriver);
        return;
    }
    std::uint32_t loader_version = VK_API_VERSION_1_0;
    if (enumerate_version != nullptr) (void)enumerate_version(&loader_version);
    std::uint32_t count = 0u;
    (void)enumerate_instance_extensions(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> instance_extensions(count);
    if (enumerate_instance_extensions(nullptr, &count, instance_extensions.data()) != VK_SUCCESS) instance_extensions.clear();
    instance_extensions.resize(std::min<std::size_t>(count, instance_extensions.size()));
    const bool properties2 = has_extension(instance_extensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    const bool window_surfaces = has_extension(instance_extensions, VK_KHR_SURFACE_EXTENSION_NAME) &&
                                 has_extension(instance_extensions, VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    std::vector<const char*> enabled;
    if (properties2) enabled.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    if (window_surfaces) {
        enabled.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        enabled.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Nebula";
    app.pEngineName = "Nebula";
    app.apiVersion = loader_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3
                     : loader_version >= VK_API_VERSION_1_1 ? loader_version
                                                            : VK_API_VERSION_1_0;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    create.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
    create.ppEnabledExtensionNames = enabled.data();
    VkInstance instance = VK_NULL_HANDLE;
    if (create_instance(&create, nullptr, &instance) != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        refuse_all(RendererRefusal::kNoDevice);
        return;
    }
    const auto find = [&](const char* name) { return get(instance, name); };
    const auto destroy_instance = reinterpret_cast<PFN_vkDestroyInstance>(find("vkDestroyInstance"));
    const auto enumerate_devices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(find("vkEnumeratePhysicalDevices"));
    const auto properties_of = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(find("vkGetPhysicalDeviceProperties"));
    const auto core_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        app.apiVersion >= VK_API_VERSION_1_1 ? find("vkGetPhysicalDeviceProperties2") : nullptr);
    const auto khr_properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2KHR>(
        properties2 ? find("vkGetPhysicalDeviceProperties2KHR") : nullptr);
    const auto device_extensions_of =
        reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(find("vkEnumerateDeviceExtensionProperties"));
    const auto queue_families_of =
        reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(find("vkGetPhysicalDeviceQueueFamilyProperties"));
    const auto presents_to_windows = reinterpret_cast<PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR>(
        window_surfaces ? find("vkGetPhysicalDeviceWin32PresentationSupportKHR") : nullptr);

    refuse_all(RendererRefusal::kNoDevice);  // a card no Vulkan device matches
    std::vector<VkPhysicalDevice> devices;
    if (enumerate_devices != nullptr && properties_of != nullptr && device_extensions_of != nullptr && queue_families_of != nullptr &&
        enumerate_devices(instance, &count, nullptr) == VK_SUCCESS) {
        devices.resize(count);
        if (enumerate_devices(instance, &count, devices.data()) < VK_SUCCESS) count = 0u;
        devices.resize(std::min<std::size_t>(count, devices.size()));
    }
    std::vector<bool> matched(cards.size(), false);
    for (const VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        properties_of(device, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) continue;
        // Its LUID: the core query serves a device of Vulkan 1.1 on a 1.1 instance, the extension's any device.
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 extended{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
        if (core_properties2 != nullptr && properties.apiVersion >= VK_API_VERSION_1_1) {
            core_properties2(device, &extended);
        } else if (khr_properties2 != nullptr) {
            khr_properties2(device, &extended);
        }
        std::size_t at = cards.size();
        for (std::size_t i = 0u; i < cards.size() && at == cards.size() && id.deviceLUIDValid == VK_TRUE; ++i) {
            if (!matched[i] && std::memcmp(&cards[i].luid, id.deviceLUID, sizeof(LUID)) == 0) at = i;
        }
        for (std::size_t i = 0u; i < cards.size() && at == cards.size(); ++i) {
            if (!matched[i] && cards[i].vendor == properties.vendorID && cards[i].device == properties.deviceID) at = i;
        }
        if (at == cards.size()) continue;
        matched[at] = true;

        std::uint32_t extension_count = 0u;
        (void)device_extensions_of(device, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> extensions(extension_count);
        if (device_extensions_of(device, nullptr, &extension_count, extensions.data()) < VK_SUCCESS) extension_count = 0u;
        extensions.resize(std::min<std::size_t>(extension_count, extensions.size()));
        VulkanFacts facts;
        facts.swapchain = has_extension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        facts.version = std::min(app.apiVersion, properties.apiVersion);
        facts.properties2 = properties2;
        facts.maintenance1 = has_extension(extensions, VK_KHR_MAINTENANCE1_EXTENSION_NAME);
        std::uint32_t families = 0u;
        queue_families_of(device, &families, nullptr);
        std::vector<VkQueueFamilyProperties> family(families);
        queue_families_of(device, &families, family.data());
        for (std::uint32_t i = 0u; i < families && presents_to_windows != nullptr && !facts.presenting_queue; ++i) {
            facts.presenting_queue = (family[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u && presents_to_windows(device, i) == VK_TRUE;
        }
        cards[at].renderers.set(RendererKind::kVulkan, vulkan_refusal(facts));
    }
    if (destroy_instance != nullptr) destroy_instance(instance, nullptr);
}

// Reads a check process's output until it ends or `limit_ms` passes; empty when it did not end in time.
std::string run_check_process(unsigned limit_ms) {
    const std::wstring self = windows::executable_path();
    if (self.empty()) return {};
    SECURITY_ATTRIBUTES inherited{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    // Room for the whole output, so the process never waits on this one to read.
    if (!CreatePipe(&read, &write, &inherited, 1u << 16u)) return {};
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0u);

    // Only the pipe's end is inherited.
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = write;
    startup.StartupInfo.hStdError = write;
    SIZE_T bytes = 0u;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> attributes(bytes);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    const bool listed = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes) &&
                        UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &write,
                                                  sizeof(write), nullptr, nullptr);
    std::wstring command = L"\"" + self + L"\" " + std::wstring(kRendererCheckSwitch, kRendererCheckSwitch + std::strlen(kRendererCheckSwitch));
    PROCESS_INFORMATION process{};
    const bool started = listed && CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, TRUE,
                                                  CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                                  nullptr, &startup.StartupInfo, &process);
    if (listed) DeleteProcThreadAttributeList(startup.lpAttributeList);
    CloseHandle(write);
    if (!started) {
        std::cout << "[renderer] the check did not start (" << GetLastError() << ")\n";
        CloseHandle(read);
        return {};
    }
    // It ends with this process: in a job that closes with this one's handle.
    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(job, process.hProcess);
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    const ULONGLONG began = GetTickCount64();
    const bool ended = WaitForSingleObject(process.hProcess, limit_ms) == WAIT_OBJECT_0;
    if (!ended) {
        TerminateProcess(process.hProcess, 1u);
        std::cout << "[renderer] the check did not end in " << limit_ms << " ms\n";
    }
    std::string text;
    if (ended) {
        char buffer[4096];
        DWORD got = 0u;
        while (ReadFile(read, buffer, sizeof(buffer), &got, nullptr) && got != 0u) text.append(buffer, got);
        DWORD code = 1u;
        GetExitCodeProcess(process.hProcess, &code);
        std::cout << "[renderer] the check ended in " << (GetTickCount64() - began) << " ms, code " << code << '\n';
    }
    CloseHandle(read);
    CloseHandle(process.hProcess);
    if (job != nullptr) CloseHandle(job);
    return text;
}

struct CheckState {
    std::mutex lock;
    bool started = false;
    RendererCheck result;
};
// Never destroyed: the worker may still run while the process exits.
CheckState& check_state() {
    static CheckState* const state = new CheckState();
    return *state;
}

}  // namespace

int run_renderer_check() {
    std::vector<CardRenderers> found;
    {
        const SystemLibrary dxgi(L"dxgi.dll");
        std::vector<Card> cards = hardware_cards(dxgi);
        check_d3d12(cards);
        check_d3d11(cards);
        check_vulkan(cards);
        for (const RendererKind kind : kRendererPreference) {
            if (!renderer_program(kind).empty()) continue;
            for (Card& card : cards) card.renderers.set(kind, RendererRefusal::kNotInstalled);
        }
        for (Card& card : cards) found.push_back(std::move(card.renderers));
    }
    const std::string text = format_renderer_check(found);
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0u;
    return out != nullptr && out != INVALID_HANDLE_VALUE &&
                   WriteFile(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size()
               ? 0
               : 1;
}

std::vector<CardRenderers> check_renderers_now(unsigned limit_ms) {
    return parse_renderer_check(run_check_process(limit_ms));
}

RendererCheck renderer_check() {
    CheckState& state = check_state();
    const std::lock_guard<std::mutex> lock(state.lock);
    if (!state.started) {
        state.started = true;
        try {
            std::thread([] {
                std::vector<CardRenderers> cards = check_renderers_now(15000u);
                CheckState& done = check_state();
                const std::lock_guard<std::mutex> finished(done.lock);
                done.result.cards = std::move(cards);
                done.result.done = true;
            }).detach();
        } catch (const std::system_error&) {
            state.result.done = true;  // no thread: nothing known
        }
    }
    return state.result;
}

std::filesystem::path renderer_program(RendererKind kind) {
    const wchar_t* base = kind == RendererKind::kD3D12   ? L"Nebula"
                          : kind == RendererKind::kD3D11 ? L"NebulaD11"
                          : kind == RendererKind::kVulkan ? L"NebulaVk"
                                                          : nullptr;
    const std::wstring self = windows::executable_path();
    const std::size_t slash = self.find_last_of(L"\\/");
    if (base == nullptr || slash == std::wstring::npos) return {};
    constexpr std::wstring_view kBaseline = L"-x86-64-v2.exe";
    const bool baseline = self.size() > kBaseline.size() &&
                          _wcsicmp(self.c_str() + self.size() - kBaseline.size(), kBaseline.data()) == 0;
    std::filesystem::path path = self.substr(0u, slash + 1u) + base + (baseline ? L"-x86-64-v2.exe" : L".exe");
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES ? path : std::filesystem::path();
}

}  // namespace galaxy::render
