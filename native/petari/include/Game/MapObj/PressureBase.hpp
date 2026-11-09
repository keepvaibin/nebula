#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/Util/JointController.hpp"

class PressureMessenger : public LiveActor {
public:
    PressureMessenger(MsgSharedGroup*, const char*);

    virtual void init(const TableIter&) override;

    void exeSync();

    MsgSharedGroup* mSharedGroup;
    s32 mWaitSyncTime;
};

class PressureBase : public LiveActor {
public:
    // PressureBase's arg 3 in the stage data.
    enum ShotType {
        ShotType_OnGravity = 0,
        ShotType_Straight = 1,  // no gravity, no follow, no aim
        ShotType_Follow = 2,
        ShotType_AimTurn = 3,
    };

    PressureBase(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual void initBullet(const TableIter&);
    virtual bool shotBullet(f32);

    void exeBound();
    void exeRelax();
    void exeSyncWait();
    void exeFirstWait();
    void exeWait();
    void exePrepareToShot();
    void exeShot();
    void startWait();
    void startRelax();
    bool calcJointCannonV(Pos3f*, const JointControllerInfo&);
    bool isShotTypeOnGravity() const;
    bool isShotTypeFollow() const;

    JointControlDelegator< PressureBase >* mJointController;
    Vec3f mFront;
    f32 mRelaxAngle;
    f32 mBaseAngle;
    s32 mWaitTime;
    f32 mBallSpeed;
    s32 mShotType;
    bool mIsShortShot;
    PressureMessenger* mMessenger;
    MsgSharedGroup* mGroup;
};
