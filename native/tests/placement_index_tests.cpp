// SPDX-License-Identifier: GPL-3.0-only
// MR::placementIndexOr (Game/Util/PlacementIndex.hpp): a placement argument names a table's entry or takes the fallback.
// N-3032: ElectricRail's colour argument indexed the rail holder's four models as it was. N-3031: a view group cube with
// argument 0 unset (-1) wrote before the view group table.
#include "Game/Util/PlacementIndex.hpp"

#include <stdio.h>

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }
}  // namespace

int main() {
    // The rail colours: 0 yellow, 1 red; any other argument is yellow, as an unset one.
    expect(MR::placementIndexOr< s32 >(0, 2, 0) == 0 && MR::placementIndexOr< s32 >(1, 2, 0) == 1, "the game's two colours");
    expect(MR::placementIndexOr< s32 >(2, 2, 0) == 0, "a colour past the two is the unset one");
    expect(MR::placementIndexOr< s32 >(1000, 2, 0) == 0, "a large colour is the unset one");
    expect(MR::placementIndexOr< s32 >(-5, 2, 0) == 0, "a colour under 0 is the unset one");
    // A view group cube: a group of the table, or none.
    expect(MR::placementIndexOr< s32 >(3, 4, -1) == 3, "a cube's group in the table");
    expect(MR::placementIndexOr< s32 >(-1, 4, -1) == -1, "a cube with argument 0 unset names no group");
    expect(MR::placementIndexOr< s32 >(4, 4, -1) == -1, "a group past the table is none");
    // N-3033: a Tico's colour of its six lights, past them the first.
    expect(MR::placementIndexOr< s32 >(5, 6, 0) == 5 && MR::placementIndexOr< s32 >(6, 6, 0) == 0, "a Tico's colour of six");
    if (sFailures == 0) {
        printf("native_placement_index_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
