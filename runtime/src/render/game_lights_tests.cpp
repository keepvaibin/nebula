// SPDX-License-Identifier: GPL-3.0-only
// The game's light stores (game_lights.cpp): no cap on lit lights, a light updated in place, an entry unseen for 10 s
// reused, a black light not counted, and the nearest-light copy ignored while requests arrive.
#include "galaxy/render_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <vector>

namespace {

using galaxy::render::GameLight;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

constexpr std::uint64_t kAnyAgeMs = 600000u;
constexpr std::size_t kLights = 200u;
const std::array<float, 3> kWhite{1.0f, 1.0f, 1.0f};
const std::array<float, 3> kBlack{0.0f, 0.0f, 0.0f};

std::array<float, 3> place(std::uint64_t id) { return {static_cast<float>(id) * 1000.0f, 0.0f, 0.0f}; }

std::vector<GameLight> point_lights() {
    std::vector<GameLight> out(galaxy::render::game_point_light_count(kAnyAgeMs) + 8u);
    out.resize(galaxy::render::game_point_lights(out, kAnyAgeMs));
    return out;
}

// More lights than any fixed list held (the old store kept 32): every one of them lights, each with its own key.
bool lit_lights_are_all_kept() {
    bool ok = true;
    for (std::uint64_t id = 1u; id <= kLights; ++id) {
        galaxy::render::note_game_point_light(id, place(id), kWhite, 5.0f);
    }
    ok &= expect(galaxy::render::game_point_light_count(kAnyAgeMs) == kLights, "200 lit lights are counted");
    const std::vector<GameLight> lights = point_lights();
    ok &= expect(lights.size() == kLights, "200 lit lights are returned");
    std::set<std::uint32_t> keys;
    for (const GameLight& light : lights) {
        keys.insert(light.key);
    }
    ok &= expect(keys.size() == kLights && keys.count(0u) == 0u, "each light has its own non-zero key");
    std::vector<GameLight> few(10u);
    ok &= expect(galaxy::render::game_point_lights(few, kAnyAgeMs) == few.size(), "a short span gets as many as it holds");
    return ok;
}

// The same game light id moves in place: the entry and its key stay.
bool a_light_updates_in_place() {
    bool ok = true;
    const std::vector<GameLight> before = point_lights();
    galaxy::render::note_game_point_light(5u, {42.0f, 1.0f, 2.0f}, kWhite, 9.0f);
    const std::vector<GameLight> after = point_lights();
    ok &= expect(after.size() == before.size(), "a moved light adds no entry");
    bool found = false;
    for (std::size_t i = 0u; i < before.size() && i < after.size(); ++i) {
        if (before[i].position[0] == place(5u)[0]) {
            found = after[i].position[0] == 42.0f && after[i].key == before[i].key && after[i].half_distance == 9.0f;
        }
    }
    ok &= expect(found, "the moved light keeps its key");
    return ok;
}

// A world light has no id: lights near one another are one light, far ones are two.
bool world_lights_match_by_position() {
    bool ok = true;
    galaxy::render::note_game_world_light({0.0f, 0.0f, 0.0f}, kWhite);
    galaxy::render::note_game_world_light({10.0f, 5.0f, 0.0f}, kWhite);
    ok &= expect(galaxy::render::game_world_light_count(kAnyAgeMs) == 1u, "near world lights are one");
    galaxy::render::note_game_world_light({5000.0f, 0.0f, 0.0f}, kWhite);
    ok &= expect(galaxy::render::game_world_light_count(kAnyAgeMs) == 2u, "far world lights are two");
    return ok;
}

// An entry unseen for 10 s is free for another light; one only unseen for a while still holds its light.
bool an_unseen_entry_is_reused() {
    bool ok = true;
    const std::size_t held = galaxy::render::game_point_light_count(kAnyAgeMs);
    std::set<std::uint32_t> old_keys;
    for (const GameLight& light : point_lights()) {
        old_keys.insert(light.key);
    }
    galaxy::render::advance_game_lights_clock(600u);
    ok &= expect(galaxy::render::game_point_light_count(500u) == 0u, "lights unseen for 600 ms are not newer than 500 ms");
    ok &= expect(galaxy::render::game_point_light_count(kAnyAgeMs) == held, "they still hold their entries");
    galaxy::render::advance_game_lights_clock(10000u);
    galaxy::render::note_game_point_light(7777u, place(7777u), kWhite, 5.0f);
    // The new light took an entry unseen for 10 s: the stores did not grow.
    ok &= expect(galaxy::render::game_point_light_count(kAnyAgeMs) == held, "the new light reused an entry");
    std::vector<GameLight> recent(4u);
    const std::size_t recent_count = galaxy::render::game_point_lights(recent, 5000u);
    ok &= expect(recent_count == 1u && recent[0].position[0] == place(7777u)[0], "the new light is the only recent one");
    ok &= expect(recent_count == 1u && old_keys.count(recent[0].key) == 0u && recent[0].key != 0u,
                 "it has a key none of the old lights had");
    return ok;
}

// A black light is stored but does not light.
bool a_black_light_is_not_counted() {
    galaxy::render::note_game_point_light(1000u, place(1000u), kBlack, 5.0f);
    return expect(galaxy::render::game_point_light_count(5000u) == 1u, "a black light does not count");
}

// While requests arrive they carry every point light: the nearest-light copy is ignored.
bool requests_replace_the_nearest_copy() {
    bool ok = true;
    const std::size_t before = galaxy::render::game_point_light_count(5000u);
    galaxy::render::note_game_point_request(9001u, place(9001u), kWhite, 5.0f);
    galaxy::render::note_game_point_light(9002u, place(9002u), kWhite, 5.0f);
    ok &= expect(galaxy::render::game_point_light_count(5000u) == before + 1u, "only the request is noted");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= lit_lights_are_all_kept();
    ok &= a_light_updates_in_place();
    ok &= world_lights_match_by_position();
    ok &= an_unseen_entry_is_reused();
    ok &= a_black_light_is_not_counted();
    ok &= requests_replace_the_nearest_copy();
    if (!ok) {
        return 1;
    }
    std::cout << "game lights tests passed\n";
    return 0;
}
