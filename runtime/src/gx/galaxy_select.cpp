// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/galaxy_select.h"

#include <atomic>
#include <cstdlib>
#include <mutex>

namespace galaxy::gx {
namespace {

std::atomic<std::uint64_t> g_travel{0u};  // stage + 1 in the low 32 bits, star in the high
std::atomic<bool> g_grand_stars{false};
std::atomic<bool> g_names_wanted{false};
std::atomic<bool> g_names_known{false};
std::mutex g_names_mutex;
std::vector<std::string> g_galaxy_names;
std::vector<std::vector<std::string>> g_star_names;

}  // namespace

bool galaxy_select_enabled() noexcept {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value), "GALAXY_GALAXY_SELECT") == 0 && length > 1u &&
               value[0] == '1';
    }();
    return enabled;
}

void request_galaxy_travel(std::size_t stage, std::uint32_t star) noexcept {
    if (stage >= kGalaxyStageCount || star == 0u || star > kGalaxyStages[stage].stars) {
        return;
    }
    g_travel.store((static_cast<std::uint64_t>(star) << 32u) | (stage + 1u), std::memory_order_release);
}

void request_grand_stars() noexcept {
    g_grand_stars.store(true, std::memory_order_release);
}

void want_galaxy_select_names() noexcept {
    if (!g_names_known.load(std::memory_order_acquire)) {
        g_names_wanted.store(true, std::memory_order_release);
    }
}

std::string galaxy_select_name(std::size_t stage) {
    const std::lock_guard<std::mutex> lock(g_names_mutex);
    return stage < g_galaxy_names.size() ? g_galaxy_names[stage] : std::string();
}

std::string galaxy_select_star_name(std::size_t stage, std::uint32_t star) {
    const std::lock_guard<std::mutex> lock(g_names_mutex);
    if (stage >= g_star_names.size() || star == 0u || star > g_star_names[stage].size()) {
        return {};
    }
    return g_star_names[stage][star - 1u];
}

}  // namespace galaxy::gx
