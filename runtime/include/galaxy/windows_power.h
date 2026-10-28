// SPDX-License-Identifier: GPL-3.0-only
// Thread naming, the power-throttling opt-out and performance-core selection for the host's threads.
#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstddef>
#include <cstdio>

namespace galaxy::host {

// Names the calling thread for debuggers, profilers and minidumps, and logs the
// mapping once so a crash line's thread id identifies the thread's role.
inline void name_current_thread(const wchar_t* name) noexcept {
    (void)SetThreadDescription(GetCurrentThread(), name);
    char narrow[96]{};
    for (std::size_t i = 0; name[i] != L'\0' && i + 1u < sizeof(narrow); ++i) {
        narrow[i] = name[i] < 0x80 ? static_cast<char>(name[i]) : '?';
    }
    std::fprintf(stderr, "[thread] name=%s id=%lu\n", narrow,
                 static_cast<unsigned long>(GetCurrentThreadId()));
}

// Background work (the shape builder): the calling thread runs where the game's and the renderer's threads leave a
// core idle.
inline void lower_current_thread_priority() noexcept {
    (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
}

// Request execution-speed QoS for the process and calling thread, and retain
// the process's timer-resolution requests when Windows supports that policy.
// This does not select a core or override thermal/power limits. The timer flag
// requires a newer OS than execution-speed control, so retry the older policy
// if the combined request is rejected. Unsupported OS versions may reject both.
inline void opt_out_of_power_throttling_process() noexcept {
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    // PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION == 0x4.
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | 0x4u;
    state.StateMask = 0;
    if (!SetProcessInformation(
            GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state))) {
        state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        (void)SetProcessInformation(
            GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
    }
}

// On a hybrid CPU (performance plus efficiency cores), restrict the calling
// thread to the CPU set of the highest efficiency class so a critical-path
// thread is never scheduled onto an efficiency core. Returns the number of
// cores selected, or 0 when the machine is homogeneous or the query fails, in
// which case nothing changes. Uses the documented CPU-set API rather than a
// hard affinity mask so the scheduler keeps its freedom inside the set.
inline unsigned pin_current_thread_to_performance_cores() noexcept {
    using GetSystemCpuSetInformationFn = BOOL (WINAPI*)(
        PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);
    using SetThreadSelectedCpuSetsFn = BOOL (WINAPI*)(HANDLE, const ULONG*, ULONG);
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        return 0u;
    }
    const auto get_cpu_sets = reinterpret_cast<GetSystemCpuSetInformationFn>(
        GetProcAddress(kernel32, "GetSystemCpuSetInformation"));
    const auto set_cpu_sets = reinterpret_cast<SetThreadSelectedCpuSetsFn>(
        GetProcAddress(kernel32, "SetThreadSelectedCpuSets"));
    if (get_cpu_sets == nullptr || set_cpu_sets == nullptr) {
        return 0u;
    }
    ULONG required = 0;
    (void)get_cpu_sets(nullptr, 0, &required, GetCurrentProcess(), 0);
    if (required == 0 || required > (1u << 20)) {
        return 0u;
    }
    unsigned char* storage = static_cast<unsigned char*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required));
    if (storage == nullptr) {
        return 0u;
    }
    unsigned selected = 0u;
    if (get_cpu_sets(
            reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(storage), required,
            &required, GetCurrentProcess(), 0)) {
        unsigned char best_class = 0;
        unsigned char worst_class = 255;
        ULONG count = 0;
        for (ULONG offset = 0; offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= required;) {
            const auto* info =
                reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(storage + offset);
            if (info->Size == 0) {
                break;
            }
            if (info->Type == CpuSetInformation) {
                const unsigned char cls = info->CpuSet.EfficiencyClass;
                best_class = cls > best_class ? cls : best_class;
                worst_class = cls < worst_class ? cls : worst_class;
                ++count;
            }
            offset += info->Size;
        }
        // Homogeneous machines have one class; leave the scheduler alone.
        if (count != 0 && best_class != worst_class) {
            ULONG* ids = static_cast<ULONG*>(
                HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ULONG) * count));
            if (ids != nullptr) {
                ULONG id_count = 0;
                for (ULONG offset = 0;
                     offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= required;) {
                    const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(
                        storage + offset);
                    if (info->Size == 0) {
                        break;
                    }
                    // Parked is a transient power state, not a capability:
                    // the scheduler unparks cores under load, so every
                    // logical CPU of the class belongs in the set.
                    if (info->Type == CpuSetInformation &&
                        info->CpuSet.EfficiencyClass == best_class) {
                        ids[id_count++] = info->CpuSet.Id;
                    }
                    offset += info->Size;
                }
                // Fewer than four logical CPUs cannot host the simulation and
                // render threads plus their helpers without contention.
                if (id_count >= 4u && set_cpu_sets(GetCurrentThread(), ids, id_count)) {
                    selected = static_cast<unsigned>(id_count);
                }
                HeapFree(GetProcessHeap(), 0, ids);
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, storage);
    return selected;
}

}  // namespace galaxy::host
#else
#include <pthread.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#else
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <cstddef>

namespace galaxy::host {
// The thread's name in the platform's tools (top, perf, debuggers): its first 15 characters, as Linux keeps.
inline void name_current_thread(const wchar_t* name) noexcept {
    char narrow[16]{};
    for (std::size_t i = 0; name[i] != L'\0' && i + 1u < sizeof(narrow); ++i) {
        narrow[i] = name[i] < 0x80 ? static_cast<char>(name[i]) : '?';
    }
#if defined(__APPLE__)
    (void)pthread_setname_np(narrow);
#else
    (void)pthread_setname_np(pthread_self(), narrow);
#endif
}
inline void opt_out_of_power_throttling_process() noexcept {}
inline unsigned pin_current_thread_to_performance_cores() noexcept { return 0u; }
// Background work (the shape builder): the calling thread runs where the game's and the renderer's threads leave a
// core idle.
inline void lower_current_thread_priority() noexcept {
#if defined(__APPLE__)
    (void)pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#else
    (void)setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
#endif
}
}  // namespace galaxy::host
#endif
