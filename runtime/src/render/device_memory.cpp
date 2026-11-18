// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/device_memory.h"

#include <atomic>

namespace galaxy::render {

namespace {
std::atomic<bool> s_unified{false};
}  // namespace

void set_device_unified_memory(bool unified) noexcept { s_unified.store(unified, std::memory_order_relaxed); }

bool device_unified_memory() noexcept { return s_unified.load(std::memory_order_relaxed); }

}  // namespace galaxy::render
