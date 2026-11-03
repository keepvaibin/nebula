#include "Game/System/NebulaDataTables.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/NebulaArena.hpp"

// Files the cooker writes (crates/nebula-import/src/cook/dol.rs).
namespace {
    // A file of the folder read into the stationed arena, straight from the store: the file loader may not run yet.
    const u8* load(const char* pName) {
        char path[64];
        nebula_snprintf(path, sizeof(path), "/NebulaData/%s", pName);
        GameAssets::File file;
        if (!GameAssets::open(path, &file)) {
            NEBULA_PANIC("%s is missing: the game content was not cooked by this version", path);
        }
        u8* pData = static_cast< u8* >(NebulaArena::alloc(file.mLength, 32, MR::getStationedHeap()));
        if (pData == nullptr || GameAssets::read(&file, pData, file.mLength, 0) < static_cast< s32 >(file.mLength)) {
            NEBULA_PANIC("%s cannot be read", path);
        }
        return pData;
    }
}  // namespace

namespace NebulaDataTables {
    const void* getGalaxyInfoTable() {
        static const u8* sTable = nullptr;
        if (sTable == nullptr) {
            sTable = load("GalaxyInfoTable.bcsv");
        }
        return sTable;
    }

    const void* getStoryProgressTable() {
        static const u8* sTable = nullptr;
        if (sTable == nullptr) {
            sTable = load("StoryProgressTable.bcsv");
        }
        return sTable;
    }
};  // namespace NebulaDataTables
