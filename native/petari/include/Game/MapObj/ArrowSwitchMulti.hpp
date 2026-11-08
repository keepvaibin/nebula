#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ArrowSwitchTarget;
class PlacementId;

class ArrowSwitchMulti : public LiveActor {
public:
    ArrowSwitchMulti(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

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

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();

    void onTarget();
    void offTarget();

    PlacementId* mIdInfo;
    StageSwitchCtrl* mStageSwitchCtrl;
    s32 mTargetIndex;
};
