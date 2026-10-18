#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class TripodBossGuardWallPart : public LiveActor {
public:
    /// @brief Creates a new `TripodBossGuardWallPart`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossGuardWallPart(const char* pName = "\216\117\213\162\203\173\203\130\203\122\203\101\226\150\225\307\225\224\225\151" /* 三脚ボスコア防壁部品 */);

    virtual void init(const JMapInfoIter&);
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    void requestStartDemo();
    bool requestBreak();
    bool isEndDemo() const;
    void setHostMatrix(const TPos3f*);
    void setPlacementAngle(f32);
    void setStartTiming(s32);

    void exeNonActive();
    void exeDemo();
    void exeActive();
    void exeBreak();
    void exeRepair();

    /* 0x8C */ const TPos3f* mHostMtx;
    /* 0x90 */ f32 mPlacementAngle;
    /* 0x94 */ s32 mStartTiming;
};
