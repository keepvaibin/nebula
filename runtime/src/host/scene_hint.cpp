#include "galaxy/host/scene_hint.h"

#include <atomic>

// Deliberately tiny, dependency-free translation unit (only <atomic>): the
// reader (galaxy_gx, via renderer_d3d12.cpp's file-select UI button) and the
// writer (galaxy_native_host, via native_host.cpp's DVDLowRead-observing
// scene detection) must not have to pull in each other's heavy static
// libraries just to share one process-wide flag. See scene_hint.h for the
// full rationale.

namespace galaxy::host {

namespace {
// 0 = unknown, 1 = FileSelect.arc was the most recently DVD-read
// LayoutData archive, 2 = some other LayoutData archive was.
std::atomic<int> g_file_select_scene_hint{0};
}  // namespace

bool file_select_scene_active() noexcept {
    return g_file_select_scene_hint.load(std::memory_order_relaxed) == 1;
}

void set_file_select_scene_hint(int value) noexcept {
    g_file_select_scene_hint.store(value, std::memory_order_relaxed);
}

int exchange_file_select_scene_hint(int value) noexcept {
    return g_file_select_scene_hint.exchange(value, std::memory_order_relaxed);
}

}  // namespace galaxy::host
