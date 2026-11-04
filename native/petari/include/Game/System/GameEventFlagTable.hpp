#pragma once

#include "Game/System/GameEventFlag.hpp"

class GameEventFlagIter {
public:
    bool isEnd() const;
    void goNext();
    const GameEventFlag* getFlag() const;
    bool isValid() const;

    s32 mIter;
};

class GameEventFlagTableInstance {
public:
    struct Key {
        u16 mHashCode;
        const GameEventFlag* mFlag;
    };

    GameEventFlagTableInstance();

    void initSortTable();
    const GameEventFlag* findFlag(const char*);

    Key* mSortTable;
    s32 mLength;
};

namespace GameEventFlagTable {
    GameEventFlagIter getBeginIter();
    GameEventFlagAccessor makeAccessor(const char*);
    s32 getTableSize();
    const GameEventFlag* getFlag(int);
    const GameEventFlag* findFlag(const char*);
    const char* getEventFlagNameSpecialPowerStar(const char*, s32);
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
    s32 calcSpecialPowerStarNum(const char*);
    s32 getStarPieceNumToOpenExclamationGalaxy(const char*);
    s32 calcGreenPowerStarNum();
    s32 getGalaxyDependedFlags(const char**, int, const char*);
};  // namespace GameEventFlagTable
