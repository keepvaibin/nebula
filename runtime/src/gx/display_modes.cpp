// Monitor and display-mode enumeration for the display settings (display_modes.h).
#include "galaxy/gx/display_modes.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace galaxy::gx {

namespace {

// GDI device name -> the monitor's own (EDID) name, for active outputs.
std::map<std::wstring, std::wstring> monitor_names() {
    std::map<std::wstring, std::wstring> names;
    UINT32 path_count = 0u;
    UINT32 mode_count = 0u;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) !=
        ERROR_SUCCESS) {
        return names;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(),
                           &mode_count, modes.data(), nullptr) != ERROR_SUCCESS) {
        return names;
    }
    paths.resize(path_count);
    for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS &&
            DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS &&
            target.monitorFriendlyDeviceName[0] != L'\0') {
            names[source.viewGdiDeviceName] = target.monitorFriendlyDeviceName;
        }
    }
    return names;
}

BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM user) {
    auto& list = *reinterpret_cast<std::vector<MonitorInfo>*>(user);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }
    MonitorInfo entry;
    entry.device = info.szDevice;
    entry.bounds = info.rcMonitor;
    entry.work_area = info.rcWork;
    entry.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0u;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        entry.current = {mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency};
    } else {
        entry.current = {
            static_cast<unsigned>(info.rcMonitor.right - info.rcMonitor.left),
            static_cast<unsigned>(info.rcMonitor.bottom - info.rcMonitor.top), 60u};
    }
    for (DWORD index = 0u;; ++index) {
        DEVMODEW candidate{};
        candidate.dmSize = sizeof(candidate);
        if (!EnumDisplaySettingsExW(info.szDevice, index, &candidate, 0u)) {
            break;
        }
        if (candidate.dmBitsPerPel != 32u ||
            (candidate.dmDisplayFlags & DM_INTERLACED) != 0u ||
            candidate.dmPelsWidth < 640u || candidate.dmPelsHeight < 480u) {
            continue;
        }
        const DisplayModeInfo found{
            candidate.dmPelsWidth, candidate.dmPelsHeight,
            std::max<unsigned>(1u, candidate.dmDisplayFrequency)};
        if (std::find(entry.modes.begin(), entry.modes.end(), found) == entry.modes.end()) {
            entry.modes.push_back(found);
        }
    }
    if (entry.modes.empty()) {
        entry.modes.push_back(entry.current);
    }
    std::sort(entry.modes.begin(), entry.modes.end(),
              [](const DisplayModeInfo& a, const DisplayModeInfo& b) {
                  const std::uint64_t area_a = std::uint64_t{a.width} * a.height;
                  const std::uint64_t area_b = std::uint64_t{b.width} * b.height;
                  if (area_a != area_b) return area_a > area_b;
                  if (a.width != b.width) return a.width > b.width;
                  return a.refresh_hz > b.refresh_hz;
              });
    list.push_back(std::move(entry));
    return TRUE;
}

}  // namespace

std::string narrow_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(bytes, 0)), '\0');
    if (bytes > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                            out.data(), bytes, nullptr, nullptr);
    }
    return out;
}

std::wstring widen_utf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int units = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(units, 0)), L'\0');
    if (units > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                            out.data(), units);
    }
    return out;
}

std::vector<MonitorInfo> enumerate_monitors() {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, &collect_monitor,
                        reinterpret_cast<LPARAM>(&monitors));
    const auto names = monitor_names();
    for (MonitorInfo& monitor : monitors) {
        if (const auto it = names.find(monitor.device); it != names.end()) {
            monitor.name = narrow_utf8(it->second);
        }
    }
    std::stable_sort(monitors.begin(), monitors.end(),
                     [](const MonitorInfo& a, const MonitorInfo& b) {
                         if (a.primary != b.primary) return a.primary;
                         if (a.bounds.left != b.bounds.left) {
                             return a.bounds.left < b.bounds.left;
                         }
                         return a.bounds.top < b.bounds.top;
                     });
    return monitors;
}

const MonitorInfo* find_monitor(
    const std::vector<MonitorInfo>& monitors, std::wstring_view device) {
    if (!device.empty()) {
        for (const MonitorInfo& monitor : monitors) {
            if (_wcsicmp(monitor.device.c_str(), std::wstring(device).c_str()) == 0) {
                return &monitor;
            }
        }
    }
    for (const MonitorInfo& monitor : monitors) {
        if (monitor.primary) {
            return &monitor;
        }
    }
    return monitors.empty() ? nullptr : &monitors.front();
}

std::vector<std::pair<unsigned, unsigned>> monitor_resolutions(const MonitorInfo& monitor) {
    std::vector<std::pair<unsigned, unsigned>> out;
    for (const DisplayModeInfo& mode : monitor.modes) {
        const std::pair<unsigned, unsigned> size{mode.width, mode.height};
        if (std::find(out.begin(), out.end(), size) == out.end()) {
            out.push_back(size);
        }
    }
    return out;
}

}  // namespace galaxy::gx
