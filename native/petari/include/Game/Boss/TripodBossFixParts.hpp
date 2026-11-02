#pragma once

#include "Game/Boss/TripodBossFixPartsBase.hpp"

class TripodBossFixParts : public TripodBossFixPartsBase {
public:
    /// @brief Creates a new `TripodBossFixParts`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossFixParts(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void kill();

    virtual void activateTripodBoss();

    virtual void exeWaitOwn() {
    }

    void initSensor(const JMapInfoIter&);

    void exeNonActive();
    void exeStartDemo();
    void exeWait();
    void exeBreak();

    s32 _E4;
    s32 _E8;
    bool mHasCollision;
    bool _ED;
    bool _EE;
    bool mIsPlayingWarning;
};
