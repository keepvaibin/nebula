// SPDX-License-Identifier: GPL-3.0-only
// The process's own memory (process_memory.h).
#include "process_memory.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <cstdio>
#include <cstring>
#endif

namespace harness {

std::optional<std::uint64_t> process_private_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters))) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(counters.PrivateUsage);
#elif defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(info.phys_footprint);
#else
    std::FILE* status = std::fopen("/proc/self/status", "r");
    if (status == nullptr) {
        return std::nullopt;
    }
    char line[256];
    std::optional<std::uint64_t> bytes;
    while (std::fgets(line, sizeof(line), status) != nullptr) {
        unsigned long long kb = 0;
        if (std::strncmp(line, "RssAnon:", 8) == 0 && std::sscanf(line + 8, "%llu", &kb) == 1) {
            bytes = static_cast<std::uint64_t>(kb) * 1024u;
            break;
        }
    }
    std::fclose(status);
    return bytes;
#endif
}

}  // namespace harness
