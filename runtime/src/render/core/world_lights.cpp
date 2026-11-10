// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/world_lights.h"

#include <cmath>
#include <cstdio>

namespace galaxy::render::core {

const NebulaFrameWorldLight* find_world_light(const NebulaFrameList& list, std::uint64_t id) noexcept {
    // Ascending by id: a binary search.
    std::uint32_t low = 0u, high = list.world_light_count;
    while (low < high) {
        const std::uint32_t middle = low + (high - low) / 2u;
        if (list.world_lights[middle].id < id) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low < list.world_light_count && list.world_lights[low].id == id ? &list.world_lights[low] : nullptr;
}

WorldLightFaults check_world_lights(const NebulaFrameList& list, const NebulaFrameList* before) noexcept {
    WorldLightFaults faults;
    for (std::uint32_t i = 0; i < list.world_light_count; ++i) {
        const NebulaFrameWorldLight& light = list.world_lights[i];
        if (i != 0u && list.world_lights[i - 1u].id >= light.id) {
            ++faults.order;
        }
        if (light.kind >= NEBULA_WORLD_LIGHT_KIND_COUNT) {
            ++faults.kind;
        }
        const float sum = light.color[0] + light.color[1] + light.color[2] + light.position[0] + light.position[1] + light.position[2] +
                          light.reach;
        if (!std::isfinite(sum)) {
            ++faults.values;
        }
    }
    if (faults.order == 0u) {
        // The searches below need the order.
        for (std::uint32_t s = 0; s < list.light_set_count; ++s) {
            const NebulaFrameLightOwner& owner = list.light_set_owners[s];
            if (owner.actor_light != 0u &&
                (find_world_light(list, owner.actor_light) == nullptr || find_world_light(list, owner.actor_light + 1u) == nullptr)) {
                ++faults.owner;
            }
            if (owner.point_light != 0u && find_world_light(list, owner.point_light) == nullptr) {
                ++faults.owner;
            }
        }
        if (list.stage_light_known != 0u &&
            (find_world_light(list, list.stage_light) == nullptr || find_world_light(list, list.stage_light + 1u) == nullptr)) {
            ++faults.stage;
        }
        if (before != nullptr) {
            // Both ascending: one pass over the two.
            std::uint32_t b = 0u;
            for (std::uint32_t i = 0; i < list.world_light_count; ++i) {
                const NebulaFrameWorldLight& light = list.world_lights[i];
                while (b < before->world_light_count && before->world_lights[b].id < light.id) {
                    ++b;
                }
                if (b < before->world_light_count && before->world_lights[b].id == light.id && before->world_lights[b].kind != light.kind) {
                    ++faults.changed_kind;
                }
            }
        }
    }
    return faults;
}

void WorldLightStats::add(const NebulaFrameList& list, const NebulaFrameList* before) noexcept {
    ++lists;
    lights += list.world_light_count;
    most = list.world_light_count > most ? list.world_light_count : most;
    lists_with_stage += list.stage_light_known != 0u ? 1u : 0u;
    for (std::uint32_t i = 0; i < list.world_light_count; ++i) {
        const NebulaFrameWorldLight& light = list.world_lights[i];
        if (light.kind < NEBULA_WORLD_LIGHT_KIND_COUNT) {
            ++by_kind[light.kind];
        }
        if (light.kind == NEBULA_WORLD_LIGHT_KEY && (light.flags & NEBULA_WORLD_LIGHT_FOLLOWS_CAMERA) != 0u) {
            ++keys_following;
        }
    }
    faults += check_world_lights(list, before).total();
}

void WorldLightStats::report() noexcept {
    if (lists == 0u) {
        return;
    }
    const double n = static_cast<double>(lists);
    const double keys = static_cast<double>(by_kind[NEBULA_WORLD_LIGHT_KEY]);
    std::fprintf(stderr,
                 "[rt-inputs] lights: lists %llu, a list holds %.1f (most %u): key %.1f fill %.1f point %.2f request %.1f sun %.2f core %.2f; "
                 "keys following the camera %.0f%%; lists with a stage light %.0f%%; faults %llu\n",
                 static_cast<unsigned long long>(lists), static_cast<double>(lights) / n, most, keys / n,
                 static_cast<double>(by_kind[NEBULA_WORLD_LIGHT_FILL]) / n, static_cast<double>(by_kind[NEBULA_WORLD_LIGHT_POINT]) / n,
                 static_cast<double>(by_kind[NEBULA_WORLD_LIGHT_REQUEST]) / n, static_cast<double>(by_kind[NEBULA_WORLD_LIGHT_SUN]) / n,
                 static_cast<double>(by_kind[NEBULA_WORLD_LIGHT_STAR_CORE]) / n,
                 keys > 0.0 ? 100.0 * static_cast<double>(keys_following) / keys : 0.0,
                 100.0 * static_cast<double>(lists_with_stage) / n, static_cast<unsigned long long>(faults));
    *this = WorldLightStats{};
}

}  // namespace galaxy::render::core
