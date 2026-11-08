#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/Util/Table.hpp"
#include <cstring>
#include "Game/System/NebulaDataTables.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/StringUtil.hpp"

// Files the cooker writes (crates/nebula-import/src/cook/dol.rs).
namespace {
    // A file of the folder read into the permanent arena, straight from the store, before any archive is held.
    const u8* load(const char* pName) {
        MR::FormattedName path("/NebulaData/%s", pName);
        GameAssets::File file;
        if (!GameAssets::open(path.c_str(), &file)) {
            NEBULA_PANIC("%s is missing: the game content was not cooked by this version", path.c_str());
        }
        u8* pData = static_cast< u8* >(NebulaArena::alloc(file.mLength, 32, Arenas::permanent()));
        if (pData == nullptr || GameAssets::read(&file, pData, file.mLength, 0) < static_cast< s32 >(file.mLength)) {
            NEBULA_PANIC("%s cannot be read", path.c_str());
        }
        return pData;
    }
}  // namespace

namespace {
    NebulaDataTables::AddedGalaxy* sAddedGalaxies = nullptr;
    s32 sAddedGalaxyCount = 0;

    const char* paneOf(const NebulaDataTables::AddedGalaxy& rGalaxy) {
        return rGalaxy.mPane != nullptr ? rGalaxy.mPane : "dummy";
    }

    // The table with a row for each galaxy the mods add: their cells after the table's own, their words after its text.
    const u8* withAddedGalaxies(const u8* pBase) {
        Table base;
        base.attach(pBase);
        const TableData* pOld = base.mData;
        u32 words = 0;
        for (s32 i = 0; i < sAddedGalaxyCount; i++) {
            const NebulaDataTables::AddedGalaxy& rGalaxy = sAddedGalaxies[i];
            words += strlen(rGalaxy.mName) + 1 + strlen(paneOf(rGalaxy)) + 1;
            for (int c = 0; c < 3; c++) {
                words += rGalaxy.mConditions[c] != nullptr ? strlen(rGalaxy.mConditions[c]) + 1 : 0;
            }
        }
        const u32 cellsEnd = pOld->mRowsAt + pOld->mRows * pOld->mColumns * sizeof(u32);
        const u32 added = sAddedGalaxyCount * pOld->mColumns * sizeof(u32);
        const u32 textAt = cellsEnd + added;
        u8* pNew = static_cast< u8* >(NebulaArena::alloc(textAt + pOld->mTextSize + words, 32, Arenas::permanent()));
        memcpy(pNew, pBase, cellsEnd);
        memset(pNew + cellsEnd, 0, added);  // a cell of 0: "" or 0
        memcpy(pNew + textAt, pBase + pOld->mTextAt, pOld->mTextSize);
        u32 textEnd = pOld->mTextSize;
        const auto addWord = [&](const char* pWord) -> u32 {
            if (pWord == nullptr || *pWord == '\0') {
                return 0;
            }
            const u32 at = textEnd;
            const size_t length = strlen(pWord) + 1;
            memcpy(pNew + textAt + textEnd, pWord, length);
            textEnd += static_cast< u32 >(length);
            return at;
        };
        u32* pCells = reinterpret_cast< u32* >(pNew + cellsEnd);
        const auto set = [&](s32 row, const char* pColumn, u32 value) {
            const s32 column = base.searchItemInfo(pColumn);
            if (column >= 0) {
                pCells[row * pOld->mColumns + column] = value;
            }
        };
        for (s32 i = 0; i < sAddedGalaxyCount; i++) {
            const NebulaDataTables::AddedGalaxy& rGalaxy = sAddedGalaxies[i];
            set(i, "name", addWord(rGalaxy.mName));
            set(i, "MapPaneName", addWord(paneOf(rGalaxy)));
            set(i, "OpenCondition0", addWord(rGalaxy.mConditions[0]));
            set(i, "OpenCondition1", addWord(rGalaxy.mConditions[1]));
            set(i, "OpenCondition2", addWord(rGalaxy.mConditions[2]));
            set(i, "PowerStarNum", rGalaxy.mStarsToOpen);
            set(i, "GrandGalaxyNo", rGalaxy.mDome);
        }
        TableData* pTable = reinterpret_cast< TableData* >(pNew);
        pTable->mRows += sAddedGalaxyCount;
        pTable->mTextAt = textAt;
        pTable->mTextSize = textEnd;
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

    const void* getGalaxyInfoTable() {
        static const u8* sTable = nullptr;
        if (sTable == nullptr) {
            sTable = load("GalaxyInfoTable.bcsv");
            if (sAddedGalaxyCount != 0) {
                sTable = withAddedGalaxies(sTable);
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
