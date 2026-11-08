#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class FlashingCtrl;

class ShadowClipActor : public LiveActor {
public:
    virtual void endClipped();

    LiveActor* mHost;
};

class BenefitItemObj : public LiveActor {
public:
    /// @brief Creates a new `BenefitItemObj`.
    /// @param pName A pointer to the null-terminated name of the object.
    /// @param pModelName A pointer to the null-terminated name of the model.
    BenefitItemObj(const char* pName, const char* pModelName);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual void appearGround();
    virtual void appearThrowUp();
    virtual void appearThrowUpQuestionBox();
    virtual void initModelAndEfx();
    virtual void exeCatch();
    virtual void runBck(const char*);
    virtual void runEfx(const char*);
    virtual void stopEfx(const char*);
    virtual void doRotateY();
    virtual void initEscape();
    virtual void doEscape();

    void shoot(const Vec3f&, const Vec3f&, bool);
    void exeShoot();
    void exeWait();
    void exeAppearGround();
    void exePreEscape();
    void exeEscape();
    void setFollowMtx(MtxPtr);
    void calcAndSetBaseMtxInMovement();

    Vec3f mBlendFront;
    Vec3f mUp;
    s16 mEscapeLife;
    s16 mFlashPeriod;
    s16 mFlashHideTime;
    s16 mFlashCounter;
    s16 mHideCounter;
    Vec3f mEscapeVelocity;
    Vec3f mFallVelocity;
    MtxPtr mFollowMtx;
    u8 mIsDisabled;
    u8 mAppearType;
    u8 mEscapeType;
    u8 mRotateFlags;
    u8 mShadowType;
    u8 mIsThrownHigh;
    u8 mHasAppearPicture;
    u8 mIsActive;
    u8 mIsWaitingAppear;
    u8 mIsItemMoving;
    u8 mIsOnRail;
    u8 mHasAppearCamera;
    u8 mIsInAir;
    u8 mIsDemoPending;
    f32 mRailSpeed;
    f32 mRotateY;
    Mtx34f mModelMtx;
    const char* mModelName;
    FlashingCtrl* mFlashCtrl;
    ActorCameraInfo* mCameraInfo;
    u32 mCameraFrames;
    LiveActor* mHitSensorActor;
    u16 mSensorInvalidStep;
};

namespace NrvBenefitItemObj {
    NEW_NERVE(HostTypeNrvWait, BenefitItemObj, Wait);
    NEW_NERVE(HostTypeNrvShoot, BenefitItemObj, Shoot);
    NEW_NERVE(HostTypeNrvCatch, BenefitItemObj, Catch);
    NEW_NERVE(HostTypeNrvAppearGround, BenefitItemObj, AppearGround);
    NEW_NERVE(HostTypeNrvPreEscape, BenefitItemObj, PreEscape);
    NEW_NERVE(HostTypeNrvEscape, BenefitItemObj, Escape);
};  // namespace NrvBenefitItemObj
