#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/MapObj/SpinDriverCamera.hpp"
#include "Game/MapObj/SpinDriverOperateRing.hpp"
#include "Game/MapObj/SpinDriverPathDrawer.hpp"
#include "Game/MapObj/SpinDriverShootPath.hpp"

class Nerve;

// SuperSpinDriver::mColor: also the btp frame. Green needs the green driver event and shows an empty model until then.
enum SuperSpinDriverColor {
    SuperSpinDriverColor_Yellow,
    SuperSpinDriverColor_Green,
    SuperSpinDriverColor_Pink,
};

class SuperSpinDriver : public LiveActor {
public:
    SuperSpinDriver(const char*, s32 color);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void makeActorAppeared() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initParamFromPlacement(const TableIter&);
    void initGravityAxis();
    void initSensor();
    void initEmptyModel();
    void initEventCamera(const TableIter&);
    void initShootPath(const TableIter&);
    void initShootPathDraw(const TableIter&);
    void initColor();
    void initOperateRing();
    void initAppearState(const TableIter&);
    bool canBind(HitSensor*, HitSensor*) const;
    bool tryBind(HitSensor*, HitSensor*);
    bool tryEndCapture();
    bool tryForceCancel();
    bool tryShootStart();
    bool tryShoot();
    bool tryEndShoot();
    bool tryEndCoolDown();
    bool trySwitchOff();
    void requestAppear();
    void requestEmptyAppear();
    void requestActive();
    void requestHide();
    void requestShow();

    void exeTryDemo();
    void exeEmptyNonActive();
    void exeEmptyAppear();
    void exeEmptyWait();
    void exeNonActive();
    void exeAppear();
    void exeWait();
    void exeCapture();
    void exeShootStart();
    void exeShoot();
    void exeCoolDown();
    void endShoot();

    void updateShootMotion();
    void cancelBind();
    void endBind();
    void updateBindActorMatrix();
    void updateBindActorPoseToShoot(f32 rate);
    void turnBindHead(const Vec3f& rUp, f32 rate);
    void moveBindPosToCenter();
    void startPathDraw();
    void endPathDraw();
    void updatePathDraw(f32);
    void updateOperateRate();
    void updateBindPosition(f32);
    void calcShootMotionTime();
    void addSwingSignRotateY();
    void onUse();
    void offUse();
    bool isNeedEmptyModel() const;
    bool isRightToUse() const;
    bool isNerveEnableBind() const;
    bool isSwingOr2PTrigger() const;
    bool isFarPlayer() const;

private:
    bool isPullPlayer(HitSensor*, HitSensor*) const;
    bool tryReleaseBind();
    void requestAppearNerve(const Nerve*);
    void startAppearCamera();
    void tryEndAppear(const Nerve* pNextNerve);

public:
    LiveActor* mBindActor;
    SpinDriverShootPath* mShootPath;
    SpinDriverCamera* mSpinDriverCamera;
    SpinDriverOperateRing* mOperateRing;
    SpinDriverPathDrawer* mPathDrawer;
    ModelObj* mEmptyModel;
    Quat4f mInitBindQuat;
    Quat4f mBindQuat;
    Vec3f mBindPos;
    Vec3f mBindVelocity;
    Vec3f mShootPathDirection;
    Vec3f mFront;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mShootPathPosition;
    Vec3f mCaptureStartPos;
    Vec3f mClippingCenter;
    f32 mClippingRadius;
    f32 mSwingAngle;
    f32 mSwingAngleMax;
    f32 mFrontAngle;
    f32 mSpinSpeed;
    f32 mRollAngle;
    f32 mShadowLength;
    s32 mFlightTime;
    s32 mFlyLoopStep;
    s32 mFlyEndStep;
    s32 mFallStep;
    // The bound player's SpaceFlyStart / SpaceFlyEnd lengths, read at bind.
    s32 mSpaceFlyStartFrame;
    s32 mSpaceFlyEndFrame;
    s32 mDrawPathRangeIdx;
    f32 mPlayerLandRotation;
    u32 mAlreadyDoneFlag;
    s32 mAlreadyDoneFlagIdx;
    s32 mColor;
    bool mIsPlayerAway;
    s32 mCoolDown;
    bool mIsCoolingDown;
    bool mIsPullPlayer;
    bool mIsDisableJingle;
};

namespace MR {
    NameObj* createSuperSpinDriverYellow(const char*);
    NameObj* createSuperSpinDriverGreen(const char*);
    NameObj* createSuperSpinDriverPink(const char*);
};  // namespace MR
