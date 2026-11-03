#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/Util/JointController.hpp"

class PressureMessenger : public LiveActor {
public:
    PressureMessenger(MsgSharedGroup*, const char*);

    virtual ~PressureMessenger(){};
    virtual void init(const JMapInfoIter&);

    void exeSync();

    MsgSharedGroup* mSharedGroup;
    s32 mWaitSyncTime;
};

class PressureBase : public LiveActor {
public:
    enum ShotType {
        ShotType_OnGravity = 0,
        ShotType_1 = 1,
        ShotType_Follow = 2,
        ShotType_AimTurn = 3,
    };

    PressureBase(const char*);

    virtual ~PressureBase(){};

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual void initBullet(const JMapInfoIter&);
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
    bool calcJointCannonV(TPos3f*, const JointControllerInfo&);
    bool isShotTypeOnGravity() const;
    bool isShotTypeFollow() const;

    JointControlDelegator< PressureBase >* mJointController;
    TVec3f mFront;
    f32 mRelaxAngle;
    f32 mBaseAngle;
    s32 mWaitTime;
    f32 mBallSpeed;
    s32 mShotType;
    bool mIsShortShot;
    PressureMessenger* mMessenger;
    MsgSharedGroup* mGroup;
    bool _BC;
};
