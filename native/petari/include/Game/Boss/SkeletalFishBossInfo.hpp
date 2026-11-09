#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class SkeletalFishBoss;

class SkeletalFishBossInfo : public NameObj {
public:
    class GuardStatus {
    public:
        GuardStatus();

        Vec3f mGuardPosLevel;
        s32 mGuardWaitLevelID;
    };

    struct LevelStatus {
    public:
        inline GuardStatus* getGuardStatus(u32 idx) const {
            return &mStatusArray[idx];
        }

        s32 mEnergyLevel;
        f32 mSpeedLevel;
        s32 mRailIDLevel;
        f32 mGuardOffsLevel;
        s32 mGuardAppearNumLevel;
        SkeletalFishBossInfo::GuardStatus* mStatusArray;
    };

    SkeletalFishBossInfo(SkeletalFishBoss*, s32, s32, const char*);

    virtual void init(const TableIter&) override;

    LevelStatus* getLevelStatus(s32) const;
    void createLevelStatus();
    void loadLevelStatus(const TableIter&);
    void loadGuardStatus(const TableIter&, s32, s32);

    SkeletalFishBoss* mFishBoss;
    s32 mLevelNum;
    s32 mGuardNum;
    LevelStatus* mLevelStatusArray;
};
