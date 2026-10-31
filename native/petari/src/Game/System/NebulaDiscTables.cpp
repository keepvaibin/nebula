#include "Game/System/NebulaDiscTables.hpp"
#include "Game/System/GalaxyCometScheduler.hpp"
#include "Game/System/GameEventFlag.hpp"
#include "Game/System/NebulaDisc.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <JSystem/JUtility/JUTException.hpp>
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include <cstring>

// The cooker's files (crates/nebula-import/src/cook/dol.rs). The two native tables are little-endian: entries, then
// their strings, each named by its offset from the file's start (0xFFFFFFFF: none).
namespace {
    const u32 cNoString = 0xFFFFFFFF;
    // RMGE01's six Grand galaxies' schedules hold 17 or 25 papers; the event flags are 188.
    const s32 cCometTables = 6;
    const s32 cCometPapersMax = 26;
    const u32 cEventFlags = 0xEB0 / 0x14;

    // A file of the folder read into `pHeap` (straight from the disc: the file loader may not run yet); null when the
    // disc has none of that name.
    u8* load(const char* pName, bool required, JKRHeap* pHeap) {
        char path[64];
        snprintf(path, sizeof(path), "/NebulaData/%s", pName);
        NebulaDisc::File file;
        if (!NebulaDisc::open(path, &file)) {
            if (required) {
                JUTException::panic_f(__FILE__, __LINE__, "%s is missing: the game content was not cooked by this version", path);
            }
            return nullptr;
        }
        const s32 size = static_cast< s32 >((file.mLength + 31) & ~31u);
        u8* pData = static_cast< u8* >(JKRHeap::alloc(size, 32, pHeap));
        if (pData == nullptr || NebulaDisc::read(&file, pData, size, 0) < static_cast< s32 >(file.mLength)) {
            JUTException::panic_f(__FILE__, __LINE__, "%s cannot be read", path);
        }
        return pData;
    }

    const u8* load(const char* pName, bool required) {
        return load(pName, required, MR::getStationedHeapNapa());
    }

    u32 word(const u8* p) {
        u32 v;
        memcpy(&v, p, sizeof(v));
        return v;
    }

    const char* text(const u8* pFile, u32 offset) {
        return offset == cNoString ? nullptr : reinterpret_cast< const char* >(pFile + offset);
    }

    bool sameText(const char* a, const char* b) {
        return (a == nullptr || b == nullptr) ? a == b : strcmp(a, b) == 0;
    }
}  // namespace

namespace NebulaDiscTables {
    const void* getGalaxyInfoTable() {
        static const u8* sTable = load("GalaxyInfoTable.bcsv", true);
        return sTable;
    }

    const void* getStoryProgressTable() {
        static const u8* sTable = load("StoryProgressTable.bcsv", true);
        return sTable;
    }

    void* loadErrorMessageArchive(JKRHeap* pHeap) {
        return load("ErrorMessageArchive.arc", true, pHeap);
    }

    const GalaxyCometTimePaper* getCometTimePaper(s32 galaxy, const GalaxyCometTimePaper* pCompiled) {
        static const u8* sFile = load("GalaxyCometSchedules.bin", false);
        static GalaxyCometTimePaper sPapers[cCometTables][cCometPapersMax];
        if (sFile == nullptr || galaxy < 0 || galaxy >= cCometTables || word(sFile) != cCometTables) {
            return pCompiled;
        }
        const u8* p = sFile + 4;
        for (s32 t = 0; t < galaxy; t++) {
            p += 4 + word(p) * 8;
        }
        const u32 entries = word(p);
        if (entries > cCometPapersMax) {
            return pCompiled;
        }
        // The compiled schedule up to its end mark against the disc's.
        bool same = true;
        bool ended = false;
        for (u32 i = 0; i < entries; i++) {
            GalaxyCometTimePaper& rPaper = sPapers[galaxy][i];
            rPaper.mGalaxyName = text(sFile, word(p + 4 + i * 8));
            rPaper.mCategory = static_cast< s32 >(word(p + 8 + i * 8));
            if (!ended) {
                same = same && sameText(rPaper.mGalaxyName, pCompiled[i].mGalaxyName) && rPaper.mCategory == pCompiled[i].mCategory;
                ended = pCompiled[i].mGalaxyName == nullptr;
            }
        }
        if (same) {
            return pCompiled;
        }
        JUTNativeLog("main.dol: Grand Galaxy %d's comet schedule differs from the game's (a mod's): main.dol's used\n", galaxy + 1);
        return sPapers[galaxy];
    }

    const GameEventFlag* getEventFlags(const GameEventFlag* pCompiled) {
        static const u8* sFile = load("GameEventFlags.bin", false);
        static GameEventFlag sFlags[cEventFlags];
        if (sFile == nullptr || word(sFile) != cEventFlags) {
            return pCompiled;
        }
        bool same = true;
        for (u32 i = 0; i < cEventFlags; i++) {
            const u8* e = sFile + 4 + i * 20;
            GameEventFlag& rFlag = sFlags[i];
            rFlag.mName = text(sFile, word(e));
            rFlag.mType = e[4];
            rFlag.mSaveFlag = e[5];
            rFlag.mCondition1 = e[6];
            rFlag.mCondition2 = e[7];
            rFlag._8 = word(e + 8);
            rFlag.mCondition3 = text(sFile, word(e + 12));
            rFlag.mCondition4 = text(sFile, word(e + 16));
            const GameEventFlag& rMine = pCompiled[i];
            same = same && sameText(rFlag.mName, rMine.mName) && rFlag.mType == rMine.mType && rFlag.mSaveFlag == rMine.mSaveFlag &&
                   rFlag.mCondition1 == rMine.mCondition1 && rFlag.mCondition2 == rMine.mCondition2 &&
                   sameText(rFlag.mCondition3, rMine.mCondition3) && sameText(rFlag.mCondition4, rMine.mCondition4);
        }
        if (same) {
            return pCompiled;
        }
        JUTNativeLog("main.dol: the event flags differ from the game's (a mod's): main.dol's used\n");
        return sFlags;
    }
};  // namespace NebulaDiscTables
