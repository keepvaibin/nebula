// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/galaxy_select.h"

#include <atomic>
#include <mutex>
#include <vector>

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

void request_galaxy_travel(std::size_t stage, std::uint32_t star) noexcept {
    if (stage >= kGalaxyStageCount || star == 0u || star > kGalaxyStages[stage].stars) {
        return;
    }
    g_travel.store((static_cast<std::uint64_t>(star) << 32u) | (stage + 1u), std::memory_order_release);
}

void request_grand_stars() noexcept {
    g_grand_stars.store(true, std::memory_order_release);
}

bool take_galaxy_travel(std::size_t& stage, std::uint32_t& star) noexcept {
    const std::uint64_t travel = g_travel.exchange(0u, std::memory_order_acq_rel);
    if (travel == 0u) {
        return false;
    }
    stage = static_cast<std::size_t>((travel & 0xFFFFFFFFu) - 1u);
    star = static_cast<std::uint32_t>(travel >> 32u);
    return stage < kGalaxyStageCount;
}

bool take_grand_stars() noexcept {
    return g_grand_stars.exchange(false, std::memory_order_acq_rel);
}

void want_galaxy_select_names() noexcept {
    if (!g_names_known.load(std::memory_order_acquire)) {
        g_names_wanted.store(true, std::memory_order_release);
    }
}

bool take_galaxy_select_names_wanted() noexcept {
    return g_names_wanted.exchange(false, std::memory_order_acq_rel) && !g_names_known.load(std::memory_order_acquire);
}

void set_galaxy_select_name(std::size_t stage, std::uint32_t star, std::string name) {
    if (stage >= kGalaxyStageCount || star > kGalaxyStages[stage].stars) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_names_mutex);
    g_galaxy_names.resize(kGalaxyStageCount);
    g_star_names.resize(kGalaxyStageCount);
    if (star == 0u) {
        g_galaxy_names[stage] = std::move(name);
    } else {
        g_star_names[stage].resize(kGalaxyStages[stage].stars);
        g_star_names[stage][star - 1u] = std::move(name);
    }
    g_names_known.store(true, std::memory_order_release);
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
