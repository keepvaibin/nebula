#pragma once

extern "C" char nebula_game_region(void);

// The release the game is: North America (RMGE01), Europe (RMGP01) or Japan (RMGJ01), the cooked game's id (a renamed
// copy cooks as the release it holds). The releases differ in
// ScenarioSelectLayout's comet notice and ShockWaveGenerator's camera and clipping (Europe), the language table
// (Language.cpp) and the PAL60 notice, which has no television to suggest a mode to; the native game follows each.
namespace NebulaRegion {
    inline bool isPal() {
        return nebula_game_region() == 'P';
    }

    inline bool isJapan() {
        return nebula_game_region() == 'J';
    }
};  // namespace NebulaRegion
