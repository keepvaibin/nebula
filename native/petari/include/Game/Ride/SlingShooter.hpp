#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class CameraTargetMtx;

class SlingShooter : public LiveActor {
public:
    SlingShooter(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void calcAnim() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void updateHitSensor(HitSensor* pSensor) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeFree();
    void exeFreeInvalid();
    void exeLand();
    void exeWait();
    void exeAim();
    void exeAttack();
    void exeAttackSuccess();

    void calcBaseMtx(Pos3f*);

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
    f32 mPlayerZ;
    bool mIsKinopioAttached;
    bool mIsKinopioTalkEnabled;
    bool mIsLastDamage;
    const Vec3f* mBasePos;
    const Vec3f* mNeutralPos;
    const Vec3f* mUp;
    Vec3f mPointerPos;
    s32 mPadChannel;
    bool mPassThroughEnable;
    Vec3f mAttackVelocity;
    LiveActor* mRider;
    Pos3f mBaseMtx;
    CameraTargetMtx* mCameraTargetMtx;
    ActorCameraInfo* mCameraInfo;
};
