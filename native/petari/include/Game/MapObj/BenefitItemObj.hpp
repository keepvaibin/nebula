#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class FlashingCtrl;

class ShadowClipActor : public LiveActor {
public:
    virtual void endClipped() override;

    LiveActor* mHost;
};

class BenefitItemObj : public LiveActor {
public:
    // mAppearType.
    enum {
        AppearType_InPlace = 0,
        AppearType_Ground = 1,
        AppearType_ThrowUp = 2,
        AppearType_ThrowUpQuestionBox = 3
    };

    // mEscapeType: 1 and 3 run straight.
    enum {
        EscapeType_None = 0,
        EscapeType_FromPlayer = 2
    };

    // mShadowType bits; 0 is no shadow.
    enum {
        ShadowBit_PrivateGravity = 1,
        ShadowBit_EveryFrame = 2,
        ShadowBit_Once = 4
    };

    // mRotateFlags: any bit turns it while it waits; this one also while it escapes.
    enum {
        RotateBit_Escape = 4
    };

    BenefitItemObj(const char* pName, const char* pModelName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void makeActorAppeared() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
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
    s32 mEscapeLife;
    s32 mFlashPeriod;
    s32 mFlashHideTime;
    s32 mFlashCounter;
    s32 mHideCounter;
    Vec3f mEscapeVelocity;
    Vec3f mFallVelocity;
    MtxPtr mFollowMtx;
    bool mIsDisabled;
    u8 mAppearType;
    u8 mEscapeType;
    u8 mRotateFlags;
    u8 mShadowType;
    bool mIsThrownHigh;
    bool mHasAppearPicture;
    bool mIsActive;
    bool mIsWaitingAppear;
    bool mIsItemMoving;
    bool mIsOnRail;
    bool mHasAppearCamera;
    bool mIsInAir;
    bool mIsDemoPending;
    f32 mRailSpeed;
    f32 mRotateY;
    Mtx34f mModelMtx;
    const char* mModelName;
    FlashingCtrl* mFlashCtrl;
    ActorCameraInfo* mCameraInfo;
    u32 mCameraFrames;
    LiveActor* mHitSensorActor;
    s32 mSensorInvalidStep;
};

namespace NrvBenefitItemObj {
    NEW_NERVE(HostTypeNrvWait, BenefitItemObj, Wait);
    NEW_NERVE(HostTypeNrvShoot, BenefitItemObj, Shoot);
    NEW_NERVE(HostTypeNrvCatch, BenefitItemObj, Catch);
    NEW_NERVE(HostTypeNrvAppearGround, BenefitItemObj, AppearGround);
    NEW_NERVE(HostTypeNrvPreEscape, BenefitItemObj, PreEscape);
    NEW_NERVE(HostTypeNrvEscape, BenefitItemObj, Escape);
};  // namespace NrvBenefitItemObj
