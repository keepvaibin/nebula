#pragma once

#include <nebula_game_api.h>
#include <nebula_types.h>

// What a benchmark step measures (NEBULA_BENCHMARK_PART_*), as the host's texts name it; null for a part this game
// does not know, which the run screen shows with no name rather than another part's.
namespace NebulaRunParts {
    constexpr const char* cNames[] = {"YOUR SETTINGS", "EACH EFFECT ALONE", "FIRST STAGE AGAIN"};
    constexpr u32 cCount = sizeof(cNames) / sizeof(cNames[0]);
    static_assert(cCount == NEBULA_BENCHMARK_PART_AGAIN + 1, "one name for each NEBULA_BENCHMARK_PART_*");

    constexpr const char* name(u32 part) {
        return part < cCount ? cNames[part] : nullptr;
    }
}  // namespace NebulaRunParts
