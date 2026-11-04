#pragma once


class NebulaArena;

// Two tables of the game's data, files of the store's NebulaData folder. Each is read once, the first time it is asked for.
namespace NebulaDataTables {
    // The galaxies' information and the story's progress (BCSV, as JMapInfo reads them).
    const void* getGalaxyInfoTable();
    // A galaxy a mod adds: a row after the game's. Only before the table is first read.
    struct AddedGalaxy {
        const char* mName;
        const char* mPane;
        const char* mConditions[3];
        u8 mStarsToOpen;
        u8 mDome;
    };
    void addGalaxy(const AddedGalaxy& rGalaxy);
    s32 addedGalaxyCount();
    const void* getStoryProgressTable();
};  // namespace NebulaDataTables
