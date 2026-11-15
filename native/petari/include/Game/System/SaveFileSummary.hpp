#pragma once

#include "Game/System/NebulaCalendar.hpp"
#include <nebula_types.h>

class UserFile;

// One user file as a screen sums it up (save.game's offer screen, the settings' per-file import), read from its parts
// as Nebula.sav holds them.
struct SaveFileSummary {
    bool mExists;                     // created
    bool mLuigi;                      // its last character Luigi
    u32 mStars;                       // that character's power stars
    NebulaCalendar::SaveTime mSaved;  // its last save; 0 never
};

// User file `user` (1 to 6) from its parts, by SaveFileId::Kind, read with `pScratch` (whose data it replaces). False:
// its config, or its last character's game data, does not load.
bool readSaveFileSummary(UserFile* pScratch, int user, const u8* const ppParts[], const u32 pSizes[], SaveFileSummary* pOut);
