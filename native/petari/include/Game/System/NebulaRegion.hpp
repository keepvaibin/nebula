#pragma once

extern "C" char nebula_game_region(void);

// The release the game is: North America (RMGE01), Europe (RMGP01) or Japan (RMGJ01), from the host (sdk/region.c).
// Where the releases differ (Language.cpp's language table, ScenarioSelectLayout's comet notice, ShockWaveGenerator's
// camera and clipping), the native game follows the release it is.
namespace NebulaRegion {
    inline bool isEurope() {
        return nebula_game_region() == 'P';
    }

    inline bool isJapan() {
        return nebula_game_region() == 'J';
    }
};  // namespace NebulaRegion
