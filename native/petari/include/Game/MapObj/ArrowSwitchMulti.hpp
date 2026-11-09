#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ArrowSwitchTarget;
class PlacementId;

class ArrowSwitchMulti : public LiveActor {
public:
    ArrowSwitchMulti(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void registerTarget(ArrowSwitchTarget* pTarget);
    bool requestPunch(HitSensor* pSender, HitSensor* pReceiver);
    void exeWait();
    void exeRotate();

    PlacementId* mIdInfo;
    ArrowSwitchTarget* mTargetArray[4];
    f32 mRotAngle;
    f32 mRotSpeed;
    u32 mActiveTargetIndex;
    s32 mRotYTargetIndex;
    bool mIsRotPlus;
};

class ArrowSwitchTarget : public NameObj {
public:
    ArrowSwitchTarget(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;

    void onTarget();
    void offTarget();

    PlacementId* mIdInfo;
    StageSwitchCtrl* mStageSwitchCtrl;
    s32 mTargetIndex;
};
