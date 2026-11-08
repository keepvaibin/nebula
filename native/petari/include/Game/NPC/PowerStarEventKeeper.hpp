#pragma once

#include <nebula_geometry.hpp>

struct PowerStarInfo {
    bool mIsExist;
    bool mIsAppearAtStart;
    bool mIsKillInDemo;
    const char* mDemoName;
};

class PowerStarEventKeeper {
public:
    PowerStarEventKeeper();

    void initStarInfoTable();
    s32 findStarID(const char*) const;
    void declareStar(const char* pDemoName, s32 starId, bool killInDemo);
    bool isGreen(const char*, s32) const;
    bool isRed(const char*, s32) const;
    bool isGrand(const char*, s32) const;
    void initStarInfoTableAfterPlacement();
    void requestAppearPowerStar(const char*, s32, const Vec3f*, bool, bool);
    bool isExistPowerStarAtScenario(int) const;
    bool isActiveStarId(s32 starId) const;
    bool isSuccess(const char*, s32) const;

    inline PowerStarInfo* getStarInfo(u32 idx) const {
        return &mInfoTbl[idx];
    }

    inline s32 calcStarID(u32 scenarioNo) const {
        return (1 << (scenarioNo - 1));
    }

    u32 mStartScenario;
    s32 mPowerStarNum;
    PowerStarInfo* mInfoTbl;
};
