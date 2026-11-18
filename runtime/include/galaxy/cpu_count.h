// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// How many logical processors the process can use, for sizing worker pools: those its affinity allows when a launcher,
// the user or a container held it to some (on Linux and Android the calling thread's, which threads made after inherit),
// else every one the machine has. On Windows std::thread::hardware_concurrency() reports one processor group (at most 64
// processors); GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) counts every group the machine has. At least 1.

#include "galaxy/windows_api.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <thread>

#if defined(__linux__)
#include <sched.h>
#endif

namespace galaxy {

[[nodiscard]] inline unsigned logical_cores() noexcept {
#if defined(_WIN32)
    // A process held to part of its group. A process whose threads span groups gets masks of 0; the default is the
    // group's whole mask.
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) != 0 && process_mask != 0 &&
        process_mask != system_mask) {
        return static_cast<unsigned>(std::popcount(static_cast<std::uint64_t>(process_mask)));
    }
    const DWORD all_groups = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (all_groups != 0u) {
        return static_cast<unsigned>(all_groups);
    }
#elif defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) == 0) {
        const int count = CPU_COUNT(&allowed);
        if (count > 0) {
            return static_cast<unsigned>(count);
        }
    }
#endif
    return std::max(1u, std::thread::hardware_concurrency());
}

}  // namespace galaxy
