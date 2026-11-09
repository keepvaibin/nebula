#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class DashRing : public LiveActor {
public:
    DashRing(const char* pName) : LiveActor(pName) {
    }

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

    void calcAxis();
    void calcSubAxis();

    Vec3f mAxis;
    Vec3f mSubAxis;
    s32 mActiveTime;
    s32 mRevivalTime;
    // Read by MarioSwim and MarioFoo when the player passes.
    s32 mBoostTime;
    s32 mMaxDuration;
    f32 mSpeedScale;
    bool mDoTurn;
    Vec3f mInitialPosition;
};
