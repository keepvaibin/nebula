#include "galaxy/host/scene_hint.h"

#include <atomic>

// Depends only on <atomic> so the reader (galaxy_gx) and writer
// (galaxy_native_host) can share this flag without linking each other's
// libraries. See scene_hint.h.

namespace galaxy::host {

namespace {
// Last DVD-read LayoutData archive: 0 = unknown, 1 = FileSelect.arc,
// 2 = any other.
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
