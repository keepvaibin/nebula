#pragma once

#include "Game/Boss/TripodBossFixPartsBase.hpp"

class TripodBossFixParts : public TripodBossFixPartsBase {
public:
    /// @brief Creates a new `TripodBossFixParts`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossFixParts(const char* pName);

    virtual void init(const TableIter&);
    virtual void kill();

    virtual void activateTripodBoss();

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
