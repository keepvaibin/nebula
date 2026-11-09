#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class CameraTargetDemoActor;
class FixedPosition;
template < typename T >
class JointControlDelegator;
struct JointControllerInfo;
class Meramera;
class SpinPullParticleCallBack;
class ThrowingIce;

class IceMerameraKing : public LiveActor {
public:
    IceMerameraKing(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeSearch();
    void exeThrow();
    void endThrow();
    void tearDownThrow();
    void exeExtinguish();
    void exeEscape();
    void endEscape();
    void exeEscapeJump();
    void exeDamage();
    void exePreRecover();
    void exeRecover();
    void exePreAttack();
    void exeAttack();
    void exeAttackAfter();
    void exeAngryDemo();
    void exeDeathDemoWait();
    void exeDeathDemo();
    void addVelocityToInitPos();
    bool isDeadAllIce();
    bool isEnableThrow();
    ThrowingIce* getDeadWeaponAndAppear();
    bool calcJoint(Pos3f*, const JointControllerInfo&);

    FixedPosition* mFixedPos;
    ThrowingIce* mThrowingIce;
    Meramera* mMeramera;
    SpinPullParticleCallBack* mSpinParticle;
    MR::Vector< MR::AssignableArray< ThrowingIce* > > mIce;
    LiveActor* mShockWave;
    LiveActor* mBreakModel;
    Vec3f mFront;
    Vec3f mUp;
    Vec3f mInitPos;
    Vec3f mInitRotation;
    s32 mThrowNum;
    s32 mWeaponIndex;
    u32 mEscapeStep;
    s32 mHitPoint;
    s32 mWeaponNum;
    Meramera** mModelArray;
    CameraTargetDemoActor* mCameraTarget;
    JointControlDelegator< IceMerameraKing >* mJointController;
    Quat4f mRollQuat;
    Vec3f mBinderOffset;
    f32 mExtinguishHeight;
    bool mIsPinchBgmOn;
    bool mIsPinchBgmAllowed;
};

class IceMerameraKingShockWave : public ModelObj {
public:
    IceMerameraKingShockWave();

    virtual void appear() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
};
