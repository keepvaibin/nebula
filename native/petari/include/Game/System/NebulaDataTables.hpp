#pragma once


class NebulaArena;

// Two tables of the game's data, files of the store's NebulaData folder. Each is read once, the first time it is asked for.
namespace NebulaDataTables {
    // The galaxies' information and the story's progress (BCSV, as JMapInfo reads them).
    const void* getGalaxyInfoTable();
    const void* getStoryProgressTable();
};  // namespace NebulaDataTables
