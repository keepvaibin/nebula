// SPDX-License-Identifier: GPL-3.0-only
// Members that keep a placement argument as given hold every value the argument's s32 gives (N-3034). MorphItemObjNeo
// kept Obj_arg1 (bind) in an s8: 256 kept its low byte, 0, and the item did not bind; 384 kept -128 and bound only
// when it appeared. A stage's arguments are its author's, a mod's stage among them.
#include "Game/MapObj/MorphItemObjNeo.hpp"

#include <limits>
#include <stdio.h>

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    template < class Member >
    constexpr bool holdsEveryArgument() {
        return std::numeric_limits< Member >::min() <= std::numeric_limits< s32 >::min() &&
               std::numeric_limits< Member >::max() >= std::numeric_limits< s32 >::max();
    }
}  // namespace

int main() {
    expect(holdsEveryArgument< decltype(MorphItemObjNeo::mIsBind) >(), "a power-up item's bind argument as given");
    if (sFailures == 0) {
        printf("native_placement_arg_width_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
