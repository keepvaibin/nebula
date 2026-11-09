#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class TripodBossGuardWallPart : public LiveActor {
public:
    TripodBossGuardWallPart(const char* pName = "三脚ボスコア防壁部品");

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    void requestStartDemo();
    bool requestBreak();
    bool isEndDemo() const;
    void setHostMatrix(const Pos3f*);
    void setPlacementAngle(f32);
    void setStartTiming(s32);

    void exeNonActive();
    void exeDemo();
    void exeActive();
    void exeBreak();
    void exeRepair();

    const Pos3f* mHostMtx;
    f32 mPlacementAngle;
    s32 mStartTiming;
};
