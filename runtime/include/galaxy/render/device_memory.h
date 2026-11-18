// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace galaxy::render {

// The GPU shares the system's memory (an integrated GPU): set by the renderer once its device exists (render thread),
// read from any thread. The first start's calibration and the benchmark's report read it.
void set_device_unified_memory(bool unified) noexcept;
[[nodiscard]] bool device_unified_memory() noexcept;

}  // namespace galaxy::render
