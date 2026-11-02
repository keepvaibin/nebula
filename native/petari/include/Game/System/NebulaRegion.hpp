#pragma once

extern "C" char nebula_game_region(void);

// The release the game is: North America (RMGE01), Europe (RMGP01) or Japan (RMGJ01), from the host (sdk/region.c).
// Where Petari's VERSION switches tell the three apart (Language.cpp's language table, ScenarioSelectLayout's comet
// notice, ShockWaveGenerator's camera and clipping, the date TitleSequenceProduct records for the PAL60 notice), the
// native game follows the game's.
namespace NebulaRegion {
    inline bool isPal() {
        return nebula_game_region() == 'P';
    }

    inline bool isJapan() {
        return nebula_game_region() == 'J';
    }
};  // namespace NebulaRegion
