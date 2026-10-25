// SPDX-License-Identifier: GPL-3.0-only
// The game's lights as its hooks report them (gx_d3d12.h): the sun, the Comet Observatory's star core, point and
// world lights. Simulation thread writes, render thread reads.
#include "galaxy/gx_d3d12.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <span>

namespace galaxy::gx {
namespace {

std::mutex g_game_sun_mutex;
std::mutex g_star_core_mutex;
std::array<float, 3> g_star_core{};
float g_star_core_scale = 0.0f;
std::chrono::steady_clock::time_point g_star_core_time{};
std::array<float, 3> g_game_sun{};
float g_game_sun_distance = 0.0f;
std::chrono::steady_clock::time_point g_game_sun_time{};
bool g_game_sun_known = false;
std::uint64_t g_game_sun_updates = 0u;
std::array<float, 3> g_traced_sun{};
std::chrono::steady_clock::time_point g_traced_sun_time{};
bool g_traced_sun_present = false;

// The game's lights by position (a light that moves takes a new entry; old ones age out).
struct NotedLight {
    GameLight light{};
    std::chrono::steady_clock::time_point first{};   // since when it has lit without a gap
    std::chrono::steady_clock::time_point seen{};
    std::uint32_t id = 0u;
    bool used = false;
};
// A light fades in over its first kLightFadeIn and out over the kLightFadeOut before it ages out: one that starts or
// stops (a Luma the game stops updating far off) never pops. A gap longer than kLightGap starts it over.
constexpr std::chrono::duration<float, std::milli> kLightFadeIn{200.0f};
constexpr std::chrono::duration<float, std::milli> kLightFadeOut{300.0f};
constexpr std::chrono::milliseconds kLightGap{500};
std::mutex g_game_lights_mutex;
// The last point light request (note_game_point_request); guarded by g_game_lights_mutex.
std::chrono::steady_clock::time_point g_point_request_seen{};
std::array<NotedLight, 32> g_point_lights{};
std::array<NotedLight, 16> g_world_lights{};

std::mutex g_area_mutex;
std::array<GameAreaLight, 2> g_area_lights{};
std::array<std::chrono::steady_clock::time_point, 2> g_area_time{};
std::array<bool, 2> g_area_known{};

// A light is matched by its guest info (id) when it has one, else by position.
template <std::size_t N>
void note_light(std::array<NotedLight, N>& lights, const std::array<float, 3>& position,
                const std::array<float, 3>& color, float half_distance = 0.0f, std::uint32_t id = 0u) {
    const auto now = std::chrono::steady_clock::now();
    const std::lock_guard<std::mutex> lock(g_game_lights_mutex);
    NotedLight* slot = &lights[0];
    bool found = false;
    for (NotedLight& entry : lights) {
        const bool same = id != 0u ? entry.id == id
                                   : std::abs(entry.light.position[0] - position[0]) +
                                             std::abs(entry.light.position[1] - position[1]) +
                                             std::abs(entry.light.position[2] - position[2]) < 100.0f;
        if (entry.used && same) {
            slot = &entry;
            found = true;
            break;
        }
        if (!entry.used || (slot->used && entry.seen < slot->seen)) {
            slot = &entry;
        }
    }
    if (!found || now - slot->seen > kLightGap) {
        slot->first = now;
    }
    slot->light = {position, color, half_distance, 0u};
    slot->id = id;
    slot->seen = now;
    slot->used = true;
}

template <std::size_t N>
std::size_t active_lights(const std::array<NotedLight, N>& lights, std::span<GameLight> out, std::uint64_t max_age_ms) {
    const auto now = std::chrono::steady_clock::now();
    const std::lock_guard<std::mutex> lock(g_game_lights_mutex);
    std::size_t count = 0u;
    const std::chrono::milliseconds limit(max_age_ms);
    for (const NotedLight& entry : lights) {
        const auto& c = entry.light.color;
        const auto age = now - entry.seen;
        if (count < out.size() && entry.used && age <= limit && c[0] + c[1] + c[2] > 0.01f) {
            const float in = std::min((now - entry.first) / kLightFadeIn, 1.0f);
            const float fade = std::clamp((limit - age) / kLightFadeOut, 0.0f, 1.0f);
            GameLight& light = out[count++];
            light = entry.light;
            light.key = entry.id != 0u ? entry.id : 0x80000000u | static_cast<std::uint32_t>(&entry - lights.data());
            for (float& v : light.color) {
                v *= in * fade;
            }
        }
    }
    return count;
}

}  // namespace

void set_star_core(const std::array<float, 3>& position, float scale) noexcept {
    const std::lock_guard<std::mutex> lock(g_star_core_mutex);
    g_star_core = position;
    g_star_core_scale = scale;
    g_star_core_time = std::chrono::steady_clock::now();
}

bool star_core(std::array<float, 3>& position, float& scale, std::uint64_t max_age_ms) noexcept {
    const std::lock_guard<std::mutex> lock(g_star_core_mutex);
    if (g_star_core_scale <= 0.0f ||
        std::chrono::steady_clock::now() - g_star_core_time > std::chrono::milliseconds(max_age_ms)) {
        return false;
    }
    position = g_star_core;
    scale = g_star_core_scale;
    return true;
}

void set_game_sun(const std::array<float, 3>& direction, float distance) noexcept {
    const std::lock_guard<std::mutex> lock(g_game_sun_mutex);
    g_game_sun = direction;
    g_game_sun_distance = distance;
    g_game_sun_time = std::chrono::steady_clock::now();
    g_game_sun_known = true;
    ++g_game_sun_updates;
}

void set_traced_sun(const std::array<float, 3>& direction, bool present) noexcept {
    const std::lock_guard<std::mutex> lock(g_game_sun_mutex);
    g_traced_sun = direction;
    g_traced_sun_present = present;
    g_traced_sun_time = std::chrono::steady_clock::now();
}

bool traced_sun(std::array<float, 3>& direction, std::uint64_t max_age_ms) noexcept {
    const std::lock_guard<std::mutex> lock(g_game_sun_mutex);
    if (!g_traced_sun_present ||
        std::chrono::steady_clock::now() - g_traced_sun_time > std::chrono::milliseconds(max_age_ms)) {
        return false;
    }
    direction = g_traced_sun;
    return true;
}

std::uint64_t game_sun_updates() noexcept {
    const std::lock_guard<std::mutex> lock(g_game_sun_mutex);
    return g_game_sun_updates;
}

void note_game_point_light(std::uint32_t info, const std::array<float, 3>& position,
                           const std::array<float, 3>& color, float half_distance) noexcept {
    {
        // While requests arrive (note_game_point_request) they carry every point light, and this nearest-light copy
        // is ignored; a game module without the request site sends only this.
        const std::lock_guard<std::mutex> lock(g_game_lights_mutex);
        if (std::chrono::steady_clock::now() - g_point_request_seen < std::chrono::seconds(1)) {
            return;
        }
    }
    note_light(g_point_lights, position, color, half_distance, info);
}

void note_game_point_request(std::uint32_t actor, const std::array<float, 3>& position,
                             const std::array<float, 3>& color, float half_distance) noexcept {
    {
        const std::lock_guard<std::mutex> lock(g_game_lights_mutex);
        g_point_request_seen = std::chrono::steady_clock::now();
    }
    note_light(g_point_lights, position, color, half_distance, actor);
}

void note_game_world_light(const std::array<float, 3>& position, const std::array<float, 3>& color) noexcept {
    note_light(g_world_lights, position, color);
}

std::size_t game_point_lights(std::span<GameLight> out, std::uint64_t max_age_ms) noexcept {
    return active_lights(g_point_lights, out, max_age_ms);
}

std::size_t game_world_lights(std::span<GameLight> out, std::uint64_t max_age_ms) noexcept {
    return active_lights(g_world_lights, out, max_age_ms);
}

void set_game_area_light(GameAreaLightKind kind, const GameAreaLight& light) noexcept {
    const auto i = static_cast<std::size_t>(kind);
    const std::lock_guard<std::mutex> lock(g_area_mutex);
    g_area_lights[i] = light;
    g_area_time[i] = std::chrono::steady_clock::now();
    g_area_known[i] = true;
}

bool game_area_light(GameAreaLightKind kind, GameAreaLight& out, std::uint64_t max_age_ms) noexcept {
    const auto i = static_cast<std::size_t>(kind);
    const std::lock_guard<std::mutex> lock(g_area_mutex);
    if (!g_area_known[i] || std::chrono::steady_clock::now() - g_area_time[i] > std::chrono::milliseconds(max_age_ms)) {
        return false;
    }
    out = g_area_lights[i];
    return true;
}

bool game_sun(std::array<float, 3>& direction, std::uint64_t max_age_ms, float* distance) noexcept {
    const std::lock_guard<std::mutex> lock(g_game_sun_mutex);
    if (!g_game_sun_known ||
        std::chrono::steady_clock::now() - g_game_sun_time > std::chrono::milliseconds(max_age_ms)) {
        return false;
    }
    direction = g_game_sun;
    if (distance != nullptr) {
        *distance = g_game_sun_distance;
    }
    return true;
}

}  // namespace galaxy::gx
