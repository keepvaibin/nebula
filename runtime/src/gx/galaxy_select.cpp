// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/galaxy_select.h"

#include <atomic>
#include <cstdlib>
#include <mutex>

namespace galaxy::gx {
namespace {

std::atomic<std::uint64_t> g_travel{0u};  // stage + 1 in the low 32 bits, star in the high
std::atomic<bool> g_names_wanted{false};
std::atomic<bool> g_names_known{false};
std::mutex g_names_mutex;
std::vector<std::string> g_galaxy_names;
std::vector<std::vector<std::string>> g_star_names;

}  // namespace

std::uint32_t galaxy_stage_stars(std::string_view id) noexcept {
    for (const GalaxyStage& stage : kGalaxyStages) {
        if (id == stage.id) {
            return stage.stars;
        }
    }
    return 0u;
}

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

bool take_galaxy_travel(std::size_t& stage, std::uint32_t& star) noexcept {
    const std::uint64_t travel = g_travel.exchange(0u, std::memory_order_acq_rel);
    if (travel == 0u) {
        return false;
    }
    stage = static_cast<std::size_t>((travel & 0xFFFFFFFFu) - 1u);
    star = static_cast<std::uint32_t>(travel >> 32u);
    return stage < kGalaxyStageCount;
}

void want_galaxy_select_names() noexcept {
    if (!g_names_known.load(std::memory_order_acquire)) {
        g_names_wanted.store(true, std::memory_order_release);
    }
}

bool galaxy_select_names_wanted() noexcept {
    return g_names_wanted.load(std::memory_order_acquire) && !g_names_known.load(std::memory_order_acquire);
}

void set_galaxy_select_names(std::vector<std::string> galaxies, std::vector<std::vector<std::string>> stars) {
    const std::lock_guard<std::mutex> lock(g_names_mutex);
    g_galaxy_names = std::move(galaxies);
    g_star_names = std::move(stars);
    g_names_known.store(true, std::memory_order_release);
    g_names_wanted.store(false, std::memory_order_release);
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

std::string galaxy_select_text(const std::vector<std::uint16_t>& units) {
    std::string text;
    for (const std::uint16_t unit : units) {
        char c = 0;
        if (unit >= 'a' && unit <= 'z') {
            c = static_cast<char>(unit - 'a' + 'A');
        } else if ((unit >= 'A' && unit <= 'Z') || (unit >= '0' && unit <= '9') || unit == ' ' || unit == '-' ||
                   unit == '.' || unit == ',' || unit == ':' || unit == '?' || unit == '(' || unit == ')' ||
                   unit == '+' || unit == '/' || unit == '%' || unit == '=') {
            c = static_cast<char>(unit);
        } else if (unit >= 0xC0u && unit <= 0xFFu) {
            // Latin-1 letters without their accents.
            static constexpr char kBase[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTSAAAAAAACEEEEIIIIDNOOOOO/OUUUUYTY";
            c = kBase[unit - 0xC0u];
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
        if (c != 0 && !(c == ' ' && (text.empty() || text.back() == ' '))) {
            text.push_back(c);
        }
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

}  // namespace galaxy::gx
