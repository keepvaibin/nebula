#pragma once

// Monitors and their display modes, for the display settings (monitor, fullscreen resolution and refresh rate) and
// the internal-resolution cap. The monitor list is Windows-only; a display's mode and the two rules that order the modes
// and list their resolutions are every desktop host's (the SDL host reads its displays into the same rules,
// desktop_display.h).

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

struct DisplayModeInfo {
    unsigned width = 0;
    unsigned height = 0;
    unsigned refresh_hz = 0;

    [[nodiscard]] bool operator==(const DisplayModeInfo&) const = default;
};

// A display's modes in the order the settings list them: the largest picture first, then the wider, then the faster.
inline void sort_display_modes(std::vector<DisplayModeInfo>& modes) {
    std::sort(modes.begin(), modes.end(), [](const DisplayModeInfo& a, const DisplayModeInfo& b) {
        const std::uint64_t area_a = std::uint64_t{a.width} * a.height;
        const std::uint64_t area_b = std::uint64_t{b.width} * b.height;
        if (area_a != area_b) return area_a > area_b;
        if (a.width != b.width) return a.width > b.width;
        return a.refresh_hz > b.refresh_hz;
    });
}

// The distinct resolutions of a display's modes, in the modes' order (largest first once they are sorted).
[[nodiscard]] inline std::vector<std::pair<unsigned, unsigned>> distinct_resolutions(
    const std::vector<DisplayModeInfo>& modes) {
    std::vector<std::pair<unsigned, unsigned>> out;
    for (const DisplayModeInfo& mode : modes) {
        const std::pair<unsigned, unsigned> size{mode.width, mode.height};
        if (std::find(out.begin(), out.end(), size) == out.end()) {
            out.push_back(size);
        }
    }
    return out;
}

// The client size of a window on a display: the wanted size limited to the area a window may take (the work area) less its
// frame, and never below 320 x 240.
[[nodiscard]] inline std::pair<long, long> windowed_client_size(long work_width, long work_height, long frame_width,
                                                                long frame_height, unsigned width, unsigned height) {
    return {std::clamp<long>(static_cast<long>(width), 320, std::max(320L, work_width - frame_width)),
            std::clamp<long>(static_cast<long>(height), 240, std::max(240L, work_height - frame_height))};
}

#if defined(_WIN32)
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
