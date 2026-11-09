#pragma once

#include "Game/Boss/TripodBossFixPartsBase.hpp"

class TripodBossFixParts : public TripodBossFixPartsBase {
public:
    TripodBossFixParts(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void kill() override;

    virtual void activateTripodBoss() override;

    virtual void exeWaitOwn() {
    }

    void initSensor(const TableIter&);

    void exeNonActive();
    void exeStartDemo();
    void exeWait();
    void exeBreak();

    s32 mBreakType;
    s32 mBreakKillStep;
    bool mHasCollision;
    bool mHasBreakMovement;
    bool mIsMarioOn;
    bool mIsPlayingWarning;
};
