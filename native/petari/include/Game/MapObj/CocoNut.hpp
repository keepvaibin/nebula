#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class CocoNut : public LiveActor {
public:
    CocoNut(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void startClipped();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initMapToolInfo(const TableIter& rIter);
    void initModel();
    void initSensor();
    void initEffect();
    void updateRotate(f32 rate);
    void updateGravity();
    bool isInGroundGracePeriod() const {
        return mOffGroundStep < 10;
    }
    void processMove();
    bool isOnGround() const;
    f32 calcMoveSpeed() const;
    void setFrontVec(const Vec3f& rDir);
    bool getWallNormal(Vec3f* pNormal) const;

    bool sendMsgToBindedSensor();
    bool sendMsgEnemyAttackToBindedSensor(HitSensor* pSensor);

    bool tryHit(HitSensor* pOtherSensor, HitSensor* pMySensor);
    bool isPossibleToHit(const Vec3f& rPos, const Vec3f& rOtherPos, const Vec3f& rOtherVelocity) const;
    void calcHitSpeedAndFrontVec(f32* pMySpeed, f32* pOtherSpeed, Vec3f* pMyFront, Vec3f* pOtherFront, const Vec3f& rOtherPos,
                                 const Vec3f& rMyPos) const;
    void hit(const Vec3f& rFront, f32 speed);
    bool tryPushedFromActor(HitSensor* pOtherSensor, HitSensor* pMySensor);

    void reviseFrontVec();
    void statusToWait();

    bool isValidReceiveMsg(const HitSensor* pSensor) const;

    void statusToHide();
    void emitEffectSpinHit(const HitSensor* pOtherSensor, const HitSensor* pMySensor);

    void tryMoveEnd();

    bool tryDisappear();
    bool isContactWithOtherCocoNut() const;

    bool isValidPushedFromPlayer(const HitSensor* pMySensor, const HitSensor* pPlayerSensor) const;

    static const char* getModelName();
    static const char* getBreakEffectName();

    void exeWait();
    void exeWaitOnBind();
    void exeMove();
    void exeInWater();
    void exeBreak();
    void exeReplaceReady();

private:
    f32 mMoveSpeed;
    f32 mFallSpeed;
    Vec3f mFront;
    Pos3f mRollMtx;
    f32 mRadius;
    bool mIsBindOff;  // resting with binder and gravity off
    Pos3f mSmokeMtx;
    Pos3f mSpinHitMtx;
    s32 mOffGroundStep;
    bool mIsKicked;  // spun; cleared on landing
    Vec3f mSpawnPosition;
    bool mIsOnMoveArea;
    Vec3f mAreaVelocity;
    bool mSphericalShadow;  // Obj_arg0: sphere shadow, and rests on its binder instead of switching it off
    bool mRespawnWhenOutOfView;  // Obj_arg1
    bool mIsGroupClipping;       // in a clipping group: clipping stays on while rolling
    bool mContinueRolling;       // Obj_arg2: keeps rolling while touching another nut
};
