#pragma once

#include <revolution/types.h>

class JKRHeap;
struct GalaxyCometTimePaper;
struct GameEventFlag;

// The data the game keeps in its executable, read from the cooked disc's NebulaData folder (the cooker extracts it
// from the player's own main.dol, B-05). Each file is read once, the first time it is asked for.
namespace NebulaDiscTables {
    // The galaxies' information and the story's progress (BCSV, as JMapInfo reads them).
    const void* getGalaxyInfoTable();
    const void* getStoryProgressTable();
    // The error-message archive, read into `pHeap` (an archive the game writes into as it mounts it).
    void* loadErrorMessageArchive(JKRHeap* pHeap);
    // Grand galaxy `galaxy`'s comet schedule and the event flags: the compiled ones, unless the disc's (a mod's main.dol)
    // differ from them.
    const GalaxyCometTimePaper* getCometTimePaper(s32 galaxy, const GalaxyCometTimePaper* pCompiled);
    const GameEventFlag* getEventFlags(const GameEventFlag* pCompiled);
};  // namespace NebulaDiscTables
