#pragma once

#include "Game/System/GameEventFlag.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/NameIndex.hpp"

class GameEventFlagIter {
public:
    bool isEnd() const;
    void goNext();
    const GameEventFlag* getFlag() const;
    bool isValid() const;

    s32 mIter;
};

// Indexes of the table as it stands when made (after every mod row is added).
class GameEventFlagTableInstance {
public:
    GameEventFlagTableInstance();

    const GameEventFlag* findFlag(const char*) const;
    // The first saved flag with this save hash; -1 for none.
    s32 findSaveIndex(u16 saveHash) const;
    // The first special star flag in table order of that galaxy and star id, whose name holds pNamePart (any when null).
    const GameEventFlag* findSpecialStar(const char* pGalaxyName, s32 starId, const char* pNamePart) const;

    MR::NameIndex mNames;            // flag name -> table index
    MR::NameIndex mSaveHashes;       // keyed by save hash, saved flags only, in table order
    MR::NameIndex mSpecialGalaxies;  // galaxy name -> its first special star flag
    u32* mNextSpecial;               // per flag: the next special star flag of its galaxy, or cNoValue
};

namespace GameEventFlagTable {
    GameEventFlagIter getBeginIter();
    GameEventFlagAccessor makeAccessor(const char*);
    s32 getTableSize();
    const GameEventFlag* getFlag(int);
    const GameEventFlag* findFlag(const char*);
    const char* getEventFlagNameSpecialPowerStar(const char*, s32);
    // See GameEventFlagTableInstance::findSpecialStar.
    const GameEventFlag* findSpecialPowerStar(const char* pGalaxyName, s32 starId, const char* pNamePart);
    bool isPowerStarType(const char*, s32, const char*);
    s32 calcExclamationGalaxyNum();
    // Rows a mod adds: only before the table is first read. false: its name can't be told from a row already there.
    bool addGalaxy(const char* pGalaxyName);
    void setFlag(const char*, u8, u16, u16, const char*, const char*);
    bool addHungryLuma(const char* pGalaxyName, s32 slot, s32 starBits);
    const char* getExclamationGalaxyNameFromIndex(int);
    bool isExist(const char*);
    int getIndex(const GameEventFlag*);
    bool isDependedAnother(const char*, const char*);
    int getIndexFromHashCode(u16);
    // The 15-bit hash a save keeps for a flag of this name: of its Shift-JIS bytes, as saves have always held it.
    u16 getSaveHash(const char* pName);
    s32 calcSpecialPowerStarNum(const char*);
    s32 getStarPieceNumToOpenExclamationGalaxy(const char*);
    s32 calcGreenPowerStarNum();
    // A galaxy's non-empty open conditions (the table's OpenCondition0, 1, ...), in column order.
    void getGalaxyDependedFlags(MR::InlineVector< const char*, 3 >* pFlags, const char* pName);
};  // namespace GameEventFlagTable
