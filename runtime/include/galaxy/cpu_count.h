// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// How many logical processors the process can use, for sizing worker pools. On Windows std::thread::hardware_concurrency()
// reports one processor group (at most 64 processors); GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) counts every group the
// machine has. At least 1.

#include "galaxy/windows_api.h"

#include <algorithm>
#include <thread>

namespace galaxy {

[[nodiscard]] inline unsigned logical_cores() noexcept {
#if defined(_WIN32)
    const DWORD all_groups = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (all_groups != 0u) {
        return static_cast<unsigned>(all_groups);
    }
#endif
    return std::max(1u, std::thread::hardware_concurrency());
}

}  // namespace galaxy
