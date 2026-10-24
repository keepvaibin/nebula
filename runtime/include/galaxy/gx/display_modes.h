#pragma once

// Monitors and their display modes, for the display settings (monitor, fullscreen resolution and refresh rate) and
// the internal-resolution cap. The monitor list is Windows-only.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::gx {

// The internal resolution (EFB scale) whose 640x456 game picture covers a
// width x height output, clamped to 1..max_scale.
[[nodiscard]] inline unsigned efb_scale_covering(
    unsigned width, unsigned height, unsigned max_scale) noexcept {
    if (width == 0u || height == 0u || max_scale == 0u) {
        return std::max(max_scale, 1u);
    }
    const double needed = std::max(width / 640.0, height / 456.0);
    return std::clamp(static_cast<unsigned>(std::ceil(needed - 1e-6)), 1u, max_scale);
}

#if defined(_WIN32)
struct DisplayModeInfo {
    unsigned width = 0;
    unsigned height = 0;
    unsigned refresh_hz = 0;

    [[nodiscard]] bool operator==(const DisplayModeInfo&) const = default;
};

struct MonitorInfo {
    std::wstring device;   // GDI device name, e.g. \\.\DISPLAY2
    std::string name;      // monitor's own name (EDID), UTF-8; may be empty
    RECT bounds{};         // desktop coordinates, physical pixels
    RECT work_area{};
    DisplayModeInfo current{};
    bool primary = false;
    // 32-bit progressive modes, unique, largest first then highest refresh.
    std::vector<DisplayModeInfo> modes;
};

// Primary monitor first, then the others in desktop order (left to right).
[[nodiscard]] std::vector<MonitorInfo> enumerate_monitors();

// The monitor with this device name, else the primary, else the first; null when there is none.
[[nodiscard]] const MonitorInfo* find_monitor(
    const std::vector<MonitorInfo>& monitors, std::wstring_view device);

// Distinct resolutions of a monitor, largest first.
[[nodiscard]] std::vector<std::pair<unsigned, unsigned>> monitor_resolutions(
    const MonitorInfo& monitor);

[[nodiscard]] std::string narrow_utf8(std::wstring_view text);
[[nodiscard]] std::wstring widen_utf8(std::string_view text);
#endif

}  // namespace galaxy::gx
