#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class CameraTargetMtx;

class SlingShooter : public LiveActor {
public:
    SlingShooter(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void calcAnim();
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeFree();
    void exeFreeInvalid();
    void exeLand();
    void exeWait();
    void exeAim();
    void exeAttack();
    void exeAttackSuccess();

    void calcBaseMtx(TPos3f*);

    bool updateWait();
    void updateHang();
    void updateActorMtx();

    bool tryCancel();
    bool tryRelease();
    bool tryAttackMap();

    void startBind(LiveActor*);
    void startCancelWait();
    void startCancelAim();

    void endBind();
    void endCommandStream();

    void emitNullEffect(HitSensor* pSender, HitSensor* pReceiver);
    void emitHitEffect(HitSensor* pSender, HitSensor* pReceiver);

    bool mIsKinopioCameraFocused;
    u32 _90;
    f32 mPlayerZ;
    bool mIsKinopioAttached;
    bool mIsKinopioTalkEnabled;
    bool mIsLastDamage;
    const TVec3f* mBasePos;
    const TVec3f* mNeutralPos;
    const TVec3f* mUp;
    TVec3f mPointerPos;
    s32 mPadChannel;
    bool mPassThroughEnable;
    TVec3f mAttackVelocity;
    LiveActor* mRider;
    TPos3f mBaseMtx;
    CameraTargetMtx* mCameraTargetMtx;
    ActorCameraInfo* mCameraInfo;
};
