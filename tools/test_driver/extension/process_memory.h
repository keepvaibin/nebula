// SPDX-License-Identifier: GPL-3.0-only
// The memory the test run's process holds as its own, for the harness's process.memory fact (plan line
// `memory <seconds>`): what a soak run watches for growth.
#pragma once

#include <cstdint>
#include <optional>

namespace harness {

// Bytes the process holds that no other process shares, or none where the system does not say:
//   Windows: private bytes (the commit charge, PROCESS_MEMORY_COUNTERS_EX::PrivateUsage), the driver's too;
//   Linux and Android: resident anonymous memory (/proc/self/status RssAnon);
//   Apple: the physical footprint (TASK_VM_INFO phys_footprint), what the system's memory limit counts.
std::optional<std::uint64_t> process_private_bytes();

}  // namespace harness
