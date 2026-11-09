#pragma once

// Monitors and their display modes, for the display settings (monitor, fullscreen resolution and refresh rate) and
// the internal-resolution cap. The monitor list is Windows-only.

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::render {

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

// What the output the window is on reports of itself (IDXGIOutput6::GetDesc1 of the output with the window's monitor, read
// through a new factory: a factory made before a display change describes the old one).
struct OutputHdr {
    bool known = false;            // the output was found and described
    bool hdr = false;              // Windows has HDR on for it (colour space HDR10 / ST 2084)
    unsigned bits = 8u;
    unsigned peak_nits = 0;        // MaxLuminance
    unsigned full_frame_nits = 0;  // MaxFullFrameLuminance
    unsigned min_nits_10000 = 0;   // MinLuminance in units of 0.0001 nit
    int colour_space = 0;          // DXGI_COLOR_SPACE_TYPE
    HMONITOR monitor = nullptr;

    [[nodiscard]] bool operator==(const OutputHdr&) const = default;
};
[[nodiscard]] OutputHdr read_output_hdr(HWND window);

[[nodiscard]] std::string narrow_utf8(std::wstring_view text);
[[nodiscard]] std::wstring widen_utf8(std::string_view text);
#endif

}  // namespace galaxy::render
