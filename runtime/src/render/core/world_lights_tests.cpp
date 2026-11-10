// SPDX-License-Identifier: GPL-3.0-only
// The world lights' rules (core/world_lights.h): a list in order with its owners' lights passes; an id out of order or
// twice, an unknown kind, a value that is not finite, an owner or a stage light the list does not hold, and an id that
// changes kind between two lists are each found; a light keeps its id while its place and colour change.
#include "render/core/world_lights.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

namespace {

using galaxy::render::core::WorldLightFaults;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

NebulaFrameWorldLight light(std::uint64_t id, std::uint32_t kind, float x = 0.0f) {
    NebulaFrameWorldLight out{};
    out.id = id;
    out.kind = kind;
    out.color[0] = out.color[1] = out.color[2] = 1.0f;
    out.position[0] = x;
    return out;
}

// A list over the given lights and owners; the vectors outlive it.
NebulaFrameList list_of(const std::vector<NebulaFrameWorldLight>& lights, const std::vector<NebulaFrameLightOwner>& owners) {
    NebulaFrameList list{};
    list.world_lights = lights.data();
    list.world_light_count = static_cast<std::uint32_t>(lights.size());
    list.light_set_owners = owners.data();
    list.light_set_count = static_cast<std::uint32_t>(owners.size());
    return list;
}

NebulaFrameLightOwner owner(std::uint64_t actor, std::uint64_t point = 0u) {
    NebulaFrameLightOwner out{};
    out.actor_light = actor;
    out.point_light = point;
    return out;
}

const std::vector<NebulaFrameWorldLight> kGood = {
    light(1u, NEBULA_WORLD_LIGHT_SUN),          light(1000u, NEBULA_WORLD_LIGHT_KEY), light(1001u, NEBULA_WORLD_LIGHT_FILL),
    light(2000u, NEBULA_WORLD_LIGHT_KEY),       light(2001u, NEBULA_WORLD_LIGHT_FILL), light(3000u, NEBULA_WORLD_LIGHT_POINT),
    light(4000u, NEBULA_WORLD_LIGHT_REQUEST),   light(5000u, NEBULA_WORLD_LIGHT_STAR_CORE),
};

bool a_good_list_passes() {
    const std::vector<NebulaFrameLightOwner> owners = {owner(1000u, 3000u), owner(2000u), owner(0u)};
    NebulaFrameList list = list_of(kGood, owners);
    list.stage_light_known = 1u;
    list.stage_light = 2000u;
    bool ok = expect(galaxy::render::core::check_world_lights(list, nullptr).total() == 0u, "a list in order with its owners' lights passes");
    ok &= expect(galaxy::render::core::find_world_light(list, 3000u) == &kGood[5], "a light is found by its id");
    ok &= expect(galaxy::render::core::find_world_light(list, 3001u) == nullptr, "an id the list does not hold is not found");
    ok &= expect(galaxy::render::core::find_world_light(list, 0u) == nullptr && galaxy::render::core::find_world_light(list, 9999u) == nullptr,
                 "nor ids before the first and after the last");
    return ok;
}

bool each_fault_is_found() {
    bool ok = true;
    {
        std::vector<NebulaFrameWorldLight> lights = kGood;
        std::swap(lights[3], lights[4]);
        ok &= expect(galaxy::render::core::check_world_lights(list_of(lights, {}), nullptr).order != 0u, "ids out of order are found");
        lights = kGood;
        lights[4].id = lights[3].id;
        ok &= expect(galaxy::render::core::check_world_lights(list_of(lights, {}), nullptr).order != 0u, "an id twice is found");
    }
    {
        std::vector<NebulaFrameWorldLight> lights = kGood;
        lights[2].kind = NEBULA_WORLD_LIGHT_KIND_COUNT;
        ok &= expect(galaxy::render::core::check_world_lights(list_of(lights, {}), nullptr).kind == 1u, "an unknown kind is found");
        lights = kGood;
        lights[6].position[1] = std::numeric_limits<float>::quiet_NaN();
        lights[7].reach = std::numeric_limits<float>::infinity();
        ok &= expect(galaxy::render::core::check_world_lights(list_of(lights, {}), nullptr).values == 2u, "values that are not finite are found");
    }
    {
        const std::vector<NebulaFrameLightOwner> owners = {owner(1000u), owner(7000u), owner(2000u, 3001u), owner(4000u)};
        const WorldLightFaults faults = galaxy::render::core::check_world_lights(list_of(kGood, owners), nullptr);
        // 7000 is not in the list; 3001 is no point light of it; 4000 is there but 4001, its fill, is not.
        ok &= expect(faults.owner == 3u, "owners naming lights the list does not hold are found");
    }
    {
        NebulaFrameList list = list_of(kGood, {});
        list.stage_light_known = 1u;
        list.stage_light = 3000u;
        ok &= expect(galaxy::render::core::check_world_lights(list, nullptr).stage == 1u, "a stage light without its two lights is found");
        list.stage_light_known = 0u;
        ok &= expect(galaxy::render::core::check_world_lights(list, nullptr).stage == 0u, "a stage light that is not known is not looked for");
    }
    return ok;
}

bool an_id_is_one_light_from_list_to_list() {
    const NebulaFrameList before = list_of(kGood, {});
    // The same lights a frame on: one moved and dimmed (the same light still), one gone, one new.
    std::vector<NebulaFrameWorldLight> next = kGood;
    next[5].position[0] = 250.0f;
    next[5].color[0] = 0.25f;
    next.erase(next.begin() + 6);
    next.push_back(light(6000u, NEBULA_WORLD_LIGHT_REQUEST));
    bool ok = expect(galaxy::render::core::check_world_lights(list_of(next, {}), &before).total() == 0u,
                     "lights that move, leave and arrive keep the rules");
    next[1].kind = NEBULA_WORLD_LIGHT_POINT;
    ok &= expect(galaxy::render::core::check_world_lights(list_of(next, {}), &before).changed_kind == 1u,
                 "an id that becomes another kind of light is found");
    return ok;
}

bool the_counts_add_up() {
    galaxy::render::core::WorldLightStats stats;
    std::vector<NebulaFrameWorldLight> lights = kGood;
    lights[1].flags = NEBULA_WORLD_LIGHT_FOLLOWS_CAMERA;
    const NebulaFrameList list = list_of(lights, {});
    stats.add(list, nullptr);
    stats.add(list, &list);
    bool ok = expect(stats.lists == 2u && stats.lights == 16u && stats.most == 8u, "lists and lights are counted");
    ok &= expect(stats.by_kind[NEBULA_WORLD_LIGHT_KEY] == 4u && stats.by_kind[NEBULA_WORLD_LIGHT_SUN] == 2u, "by kind");
    ok &= expect(stats.keys_following == 2u && stats.faults == 0u, "keys following the camera, and no fault");
    return ok;
}

}  // namespace

int main() {
    bool ok = a_good_list_passes();
    ok &= each_fault_is_found();
    ok &= an_id_is_one_light_from_list_to_list();
    ok &= the_counts_add_up();
    if (ok) {
        std::cout << "world lights tests passed\n";
    }
    return ok ? 0 : 1;
}
