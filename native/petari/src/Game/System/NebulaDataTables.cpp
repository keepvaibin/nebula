#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/Util/JMapInfo.hpp"
#include <cstring>
#include "Game/System/NebulaDataTables.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/NebulaArena.hpp"

// Files the cooker writes (crates/nebula-import/src/cook/dol.rs).
namespace {
    // A file of the folder read into the stationed arena, straight from the store: the file loader may not run yet.
    const u8* load(const char* pName, u32* pLength = nullptr) {
        char path[64];
        nebula_snprintf(path, sizeof(path), "/NebulaData/%s", pName);
        GameAssets::File file;
        if (!GameAssets::open(path, &file)) {
            NEBULA_PANIC("%s is missing: the game content was not cooked by this version", path);
        }
        u8* pData = static_cast< u8* >(NebulaArena::alloc(file.mLength, 32, MR::getPermanentHeap()));
        if (pData == nullptr || GameAssets::read(&file, pData, file.mLength, 0) < static_cast< s32 >(file.mLength)) {
            NEBULA_PANIC("%s cannot be read", path);
        }
        if (pLength != nullptr) {
            *pLength = file.mLength;
        }
        return pData;
    }
}  // namespace

namespace {
    NebulaDataTables::AddedGalaxy* sAddedGalaxies = nullptr;
    s32 sAddedGalaxyCount = 0;

    u32 columnHash(const char* pName) {
        u32 hash = 0;
        for (const char* p = pName; *p != '\0'; p++) {
            hash = hash * 31 + static_cast< u8 >(*p);
        }
        return hash;
    }

    const JMapItem* findColumn(const JMapData* pTable, const char* pName) {
        const u32 hash = columnHash(pName);
        for (s32 i = 0; i < pTable->mNumFields; i++) {
            if (pTable->mItems[i].mHash == hash) {
                return &pTable->mItems[i];
            }
        }
        return nullptr;
    }

    // The table with a row for each galaxy the mods add, their words after the table's own.
    const u8* withAddedGalaxies(const u8* pBase, u32 baseLength) {
        const JMapData* pOld = reinterpret_cast< const JMapData* >(pBase);
        const u32 rowsEnd = pOld->mDataOffset + pOld->mNumEntries * pOld->mEntrySize;
        u32 words = 1;  // the empty word
        for (s32 i = 0; i < sAddedGalaxyCount; i++) {
            const NebulaDataTables::AddedGalaxy& rGalaxy = sAddedGalaxies[i];
            words += strlen(rGalaxy.mName) + 1;
            words += rGalaxy.mPane != nullptr ? strlen(rGalaxy.mPane) + 1 : 0;
            for (int c = 0; c < 3; c++) {
                words += rGalaxy.mConditions[c] != nullptr ? strlen(rGalaxy.mConditions[c]) + 1 : 0;
            }
        }
        const u32 added = sAddedGalaxyCount * pOld->mEntrySize;
        u8* pNew = static_cast< u8* >(NebulaArena::alloc(baseLength + added + words, 32, MR::getPermanentHeap()));
        memcpy(pNew, pBase, rowsEnd);
        memset(pNew + rowsEnd, 0, added);
        memcpy(pNew + rowsEnd + added, pBase + rowsEnd, baseLength - rowsEnd);
        JMapData* pTable = reinterpret_cast< JMapData* >(pNew);
        char* pPool = reinterpret_cast< char* >(pNew + rowsEnd + added);
        u32 poolEnd = baseLength - rowsEnd;
        const auto addWord = [&](const char* pWord) {
            const u32 at = poolEnd;
            const size_t length = strlen(pWord) + 1;
            memcpy(pPool + poolEnd, pWord, length);
            poolEnd += static_cast< u32 >(length);
            return at;
        };
        const u32 empty = addWord("");
        const auto setWord = [&](u8* pRow, const char* pColumn, const char* pWord) {
            if (const JMapItem* pItem = findColumn(pTable, pColumn)) {
                const u32 at = pWord != nullptr ? addWord(pWord) : empty;
                memcpy(pRow + pItem->mOffsData, &at, sizeof(at));
            }
        };
        const auto setByte = [&](u8* pRow, const char* pColumn, u8 value) {
            if (const JMapItem* pItem = findColumn(pTable, pColumn)) {
                pRow[pItem->mOffsData] = value;
            }
        };
        for (s32 i = 0; i < sAddedGalaxyCount; i++) {
            const NebulaDataTables::AddedGalaxy& rGalaxy = sAddedGalaxies[i];
            u8* pRow = pNew + rowsEnd + i * pOld->mEntrySize;
            setWord(pRow, "name", rGalaxy.mName);
            setWord(pRow, "MapPaneName", rGalaxy.mPane != nullptr ? rGalaxy.mPane : "dummy");
            setWord(pRow, "OpenCondition0", rGalaxy.mConditions[0]);
            setWord(pRow, "OpenCondition1", rGalaxy.mConditions[1]);
            setWord(pRow, "OpenCondition2", rGalaxy.mConditions[2]);
            setByte(pRow, "PowerStarNum", rGalaxy.mStarsToOpen);
            setByte(pRow, "GrandGalaxyNo", rGalaxy.mDome);
        }
        pTable->mNumEntries += sAddedGalaxyCount;
        return pNew;
    }
}  // namespace

namespace NebulaDataTables {
    void addGalaxy(const AddedGalaxy& rGalaxy) {
        AddedGalaxy* pGalaxies = new AddedGalaxy[sAddedGalaxyCount + 1];
        for (s32 i = 0; i < sAddedGalaxyCount; i++) {
            pGalaxies[i] = sAddedGalaxies[i];
        }
        pGalaxies[sAddedGalaxyCount] = rGalaxy;
        delete[] sAddedGalaxies;
        sAddedGalaxies = pGalaxies;
        sAddedGalaxyCount++;
    }

    s32 addedGalaxyCount() {
        return sAddedGalaxyCount;
    }

    const void* getGalaxyInfoTable() {
        static const u8* sTable = nullptr;
        if (sTable == nullptr) {
            u32 length = 0;
            sTable = load("GalaxyInfoTable.bcsv", &length);
            if (sAddedGalaxyCount != 0) {
                sTable = withAddedGalaxies(sTable, length);
            }
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
