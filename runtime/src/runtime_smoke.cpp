#include "galaxy/native_api.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void log_message(void*, galaxy::LogLevelV1, const char* message) {
    std::cout << "[module] " << (message != nullptr ? message : "(null)") << '\n';
}

std::uint64_t time_base_ticks(void*) {
    constexpr std::uint64_t kWiiTimeBaseHz = 60'750'000;
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000;
    const auto whole_seconds =
        static_cast<std::uint64_t>(nanoseconds / kNanosecondsPerSecond);
    const auto remaining_nanoseconds =
        static_cast<std::uint64_t>(nanoseconds % kNanosecondsPerSecond);
    return whole_seconds * kWiiTimeBaseHz +
           remaining_nanoseconds * kWiiTimeBaseHz / kNanosecondsPerSecond;
}

void fatal_error(void*, std::uint32_t guest_pc, const char* message) {
    std::cerr << "Fatal guest error at 0x" << std::hex << guest_pc << ": "
              << (message != nullptr ? message : "(null)") << '\n';
}

void unresolved_guest_call(
    void*,
    std::uint32_t guest_address,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*) {
    std::cerr << "Unexpected guest call to 0x" << std::hex << guest_address << '\n';
    std::abort();
}

void unresolved_system_call(
    void*,
    std::uint32_t guest_pc,
    std::uint32_t,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*) {
    std::cerr << "Unexpected system call at 0x" << std::hex << guest_pc << '\n';
    std::abort();
}

void unresolved_program_trap(
    void*,
    std::uint32_t guest_pc,
    std::uint32_t,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*) {
    std::cerr << "Unexpected program trap at 0x" << std::hex << guest_pc << '\n';
    std::abort();
}

void unresolved_fpu_unavailable(
    void*,
    std::uint32_t guest_pc,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*) {
    std::cerr << "Unexpected FPU-unavailable exception at 0x" << std::hex
              << guest_pc << '\n';
    std::abort();
}

void unresolved_return_from_interrupt(
    void*,
    std::uint32_t guest_pc,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*) {
    std::cerr << "Unexpected return from interrupt at 0x" << std::hex << guest_pc << '\n';
    std::abort();
}

void unresolved_decrementer_write(
    void*,
    std::uint32_t guest_pc,
    std::uint32_t,
    std::uint64_t,
    std::uint32_t,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*) {
    std::cerr << "Unexpected decrementer write at 0x" << std::hex << guest_pc
              << '\n';
    std::abort();
}

void unresolved_branch_checkpoint(
    void*,
    std::uint32_t guest_pc,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*) {
    std::cerr << "Unexpected branch checkpoint at 0x" << std::hex << guest_pc
              << '\n';
    std::abort();
}

template <typename Function>
Function load_export(HMODULE module, const char* name) {
    auto* address = GetProcAddress(module, name);
    if (address == nullptr) {
        std::cerr << "Missing module export: " << name << '\n';
        return nullptr;
    }
    return reinterpret_cast<Function>(address);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const bool load_only = argc == 3 && std::wstring(argv[2]) == L"--load-only";
    const bool load_rso_only =
        argc == 3 && std::wstring(argv[2]) == L"--load-rso-only";
    if (argc != 2 && !load_only && !load_rso_only) {
        std::wcerr
            << L"Usage: galaxy_runtime_smoke <native-module.dll> "
               L"[--load-only|--load-rso-only]\n";
        return 2;
    }

    const std::filesystem::path module_path =
        std::filesystem::absolute(std::filesystem::path(argv[1]));
    constexpr DWORD kModuleSearchFlags =
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32;
    HMODULE module =
        LoadLibraryExW(module_path.c_str(), nullptr, kModuleSearchFlags);
    if (module == nullptr) {
        std::wcerr << L"Failed to load native module: " << module_path.c_str()
                   << L"  error=" << GetLastError() << L'\n';
        return 3;
    }

    if (load_rso_only) {
        const auto manifest_fn = load_export<galaxy::NativeRsoModuleManifestFn>(
            module, "galaxy_home_button_rso_manifest");
        const auto try_call_fn = load_export<galaxy::NativeRsoTryCallFn>(
            module, "galaxy_home_button_rso_try_call");
        const auto resume_fn = load_export<galaxy::NativeRsoResumeFn>(
            module, "galaxy_home_button_rso_resume");
        if (manifest_fn == nullptr || try_call_fn == nullptr ||
            resume_fn == nullptr) {
            FreeLibrary(module);
            return 4;
        }
        const galaxy::NativeRsoModuleManifestV1* manifest = manifest_fn();
        if (manifest == nullptr ||
            manifest->abi_version != galaxy::kNativeRsoModuleAbiVersion ||
            manifest->native_abi_version != galaxy::kNativeAbiVersion ||
            manifest->struct_size < sizeof(galaxy::NativeRsoModuleManifestV1)) {
            std::cerr << "Native RSO sidecar ABI mismatch\n";
            FreeLibrary(module);
            return 5;
        }
        std::cout << "Native RSO sidecar ABI smoke test passed for "
                  << manifest->game_id << '\n';
        FreeLibrary(module);
        return 0;
    }

    const auto manifest_fn =
        load_export<galaxy::ModuleManifestFn>(module, "galaxy_module_manifest");
    const auto init_fn = load_export<galaxy::ModuleInitFn>(module, "galaxy_module_init");
    const auto entry_fn = load_export<galaxy::ModuleEntryFn>(module, "galaxy_module_entry");
    const auto lookup_fn =
        load_export<galaxy::ModuleLookupFn>(module, "galaxy_lookup_function");
    if (manifest_fn == nullptr || init_fn == nullptr || entry_fn == nullptr ||
        lookup_fn == nullptr) {
        FreeLibrary(module);
        return 4;
    }

    const galaxy::GameModuleManifestV1* manifest = manifest_fn();
    if (manifest == nullptr || manifest->abi_version != galaxy::kNativeAbiVersion ||
        manifest->struct_size < sizeof(galaxy::GameModuleManifestV1)) {
        std::cerr << "Native module ABI mismatch"
                  << " expected-version=" << galaxy::kNativeAbiVersion
                  << " expected-manifest-size="
                  << sizeof(galaxy::GameModuleManifestV1);
        if (manifest == nullptr) {
            std::cerr << " actual-manifest=null";
        } else {
            std::cerr << " actual-version=" << manifest->abi_version
                      << " actual-manifest-size=" << manifest->struct_size;
        }
        std::cerr << '\n';
        FreeLibrary(module);
        return 5;
    }

    galaxy::NativeServicesV1 services{};
    services.log = &log_message;
    services.time_base_ticks = &time_base_ticks;
    services.fatal = &fatal_error;
    services.call_guest = &unresolved_guest_call;
    services.system_call = &unresolved_system_call;
    services.program_trap = &unresolved_program_trap;
    services.fpu_unavailable = &unresolved_fpu_unavailable;
    services.return_from_interrupt = &unresolved_return_from_interrupt;
    services.decrementer_written = &unresolved_decrementer_write;
    services.branch_checkpoint = &unresolved_branch_checkpoint;
    const std::atomic_uint32_t pending_event_mask{0u};
    services.pending_event_mask = &pending_event_mask;
    if (!init_fn(&services)) {
        std::cerr << "Native module initialization failed\n";
        FreeLibrary(module);
        return 6;
    }

    galaxy::PpcContext context{};
    // The smoke harness has no translated OS exception handler, so run with FP
    // enabled; an FP-unavailable exception would hit the fatal unresolved
    // exception callback. Lazy-FPU handling needs RMGE01's exception-7 handler.
    context.msr = galaxy::kMsrFloatingPointAvailable;
    galaxy::GuestMemoryV1 memory{};
    const bool is_test_module = std::string(manifest->game_id) == "TEST01";
    if (!load_only) {
        entry_fn(&context, &memory);
    }
    if (!load_only && is_test_module &&
        (context.gpr[3] != 0x47525831 || context.pc != 0x80004004)) {
        std::cerr << "Native module entry produced an unexpected state\n";
        FreeLibrary(module);
        return 7;
    }
    if (lookup_fn(manifest->guest_entry_point) == nullptr ||
        (is_test_module && lookup_fn(manifest->guest_entry_point + 4) != nullptr)) {
        std::cerr << "Native function lookup contract failed\n";
        FreeLibrary(module);
        return 8;
    }

    std::cout << "Native x64 module ABI smoke test passed for " << manifest->game_id << '\n';
    FreeLibrary(module);
    return 0;
}
