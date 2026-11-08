#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ArrowSwitchTarget;
class PlacementId;

class ArrowSwitchMulti : public LiveActor {
public:
    ArrowSwitchMulti(const char* pName);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void registerTarget(ArrowSwitchTarget*);
    bool requestPunch(HitSensor*, HitSensor*);
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
