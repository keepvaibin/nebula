// SPDX-License-Identifier: GPL-3.0-only
// MarioActor's model index (N-3020): mModels holds the game's six models, then one for each mod power-up, and mods
// register power-ups without a most (NebulaMods::powerUpCount, an s32). The index (mCurrModel, mDisplayModel and
// changeDisplayMode's mode) was a u8, so from the 251st power-up 6 + its slot wrapped to one of the game's models and
// the power-up showed the wrong body. The index must hold 6 + the most an s32 count gives.
#include "Game/Player/MarioActor.hpp"

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

    template < class Method >
    struct ParameterOf;
    template < class Class, class Result, class Parameter >
    struct ParameterOf< Result (Class::*)(Parameter) > {
        using Type = Parameter;
    };

    template < class Index >
    constexpr bool holdsEveryModel() {
        return static_cast< unsigned long long >(std::numeric_limits< Index >::max()) >=
               6ull + static_cast< unsigned long long >(std::numeric_limits< s32 >::max());
    }
}  // namespace

int main() {
    expect(holdsEveryModel< decltype(MarioActor::mCurrModel) >(), "mCurrModel holds 6 + every mod power-up's slot");
    expect(holdsEveryModel< decltype(MarioActor::mDisplayModel) >(), "mDisplayModel holds it too");
    expect(holdsEveryModel< ParameterOf< decltype(&MarioActor::changeDisplayMode) >::Type >(),
           "changeDisplayMode takes it");
    if (sFailures == 0) {
        printf("native_mario_model_index_tests: all passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
