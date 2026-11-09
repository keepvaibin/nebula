#pragma once

#include "Game/LiveActor/LiveActor.hpp"
class SpinDriverCamera;
class SpinDriverShootPath;

// Without a rail it launches straight up at mJumpSpeed; with one it flies the rail's parabola in mShootTime steps.
class SpinDriver : public LiveActor {
public:
    SpinDriver(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initParamFromPlacement(const TableIter&);
    void initShootPath(const TableIter&);
    void initEventCamera(const TableIter&);
    bool tryStartShoot();
    bool tryEndCapture();
    bool tryShoot();
    bool tryEndShoot();
    bool tryForceCancel();
    bool tryEndCoolDown();
    bool trySwitchOff();

    void exeTryDemo();
    void exeNonActive();
    void exeAppear();
    void exeWait();
    void exeCapture();
    void exeShootStart();
    void exeShoot();
    void exeCoolDown();

    bool startBind(HitSensor*);
    void cancelBind();
    void updateBindPosition();
    void moveBindPosToCenter();
    void updateBindActorMatrix(f32 rate);
    void calcBindActorMatrix();
    void calcParabolicBindPose();
    void turnBindHead(const Vec3f& rUp, f32 rate);
    void calcShootMotionTime();
    void startCamera();
    void updateCamera();
    void cancelCamera();
    void endCamera();
    bool canStartBind() const;
    bool canBind(HitSensor*) const;

private:
    bool isSwingOr2PTrigger() const;
    bool isPullPlayer(const HitSensor*) const;
    bool tryReleaseBind();
    void startSpinAttackSound(LiveActor* pPlayer);
    void startWaitAnim();

public:
    LiveActor* mBindActor;
    SpinDriverShootPath* mShootPath;
    SpinDriverCamera* mSpinDriverCamera;
    Quat4f mInitBindQuat;
    Quat4f mBindQuat;
    Vec3f mBindPos;
    Vec3f mBindVelocity;
    Vec3f mFront;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mCaptureStartPos;
    f32 mJumpSpeed;
    f32 mSpinAngle;
    f32 mSpinSpeed;
    Vec3f mClippingCenter;
    f32 mClippingRadius;
    f32 mCaptureRate;
    f32 mShadowDropLength;
    s32 mShootTime;
    s32 mFlyShortStep;
    s32 mFlyEndStep;
    s32 mFallStep;
    // The bound player's SpaceFlyEnd length, read at bind.
    s32 mSpaceFlyEndFrame;
    f32 mInitPoseAngle;
    bool mCanBind;
    s32 mCoolDown;
    bool mIsCoolingDown;
    bool mIsPlayerAway;
    bool mIsAppearSilent;
};
