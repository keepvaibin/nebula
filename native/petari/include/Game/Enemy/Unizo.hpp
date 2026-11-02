#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class AnimScaleController;
class ModelObj;
class WalkerStateBindStarPointer;

class Unizo : public LiveActor {
public:
    enum Type { TypeSea, TypeLand, TypeShoal };

    Unizo(const char*);

    virtual ~Unizo();
    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void makeActorDead();
    virtual MtxPtr getBaseMtx() const;
    virtual void startClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initType(const JMapInfoIter&);
    void updateRotate();
    void updateVelocity();
    void udpateInfluence();
    void udpateBlink();
    void updateSurfaceEffect();
    void deleteEffect();
    void doJump();
    void doAttack(HitSensor*);
    void doJumpDown();
    void doFireDown(const TVec3f&);
    void doBreak();
    void doSpin();
    bool isBreakGround();
    bool isBreakNow() const;
    bool tryPointBind();
    bool isEnablePointBind() const;
    void appearBreakModel();
    void startWallHitSound();

    void exeWait();
    void exeJump();
    void exeChase();
    void exeAttack();
    void exeCollidePlayer();
    void exeCollideEnemy();
    void exeBreak();
    void exeFireDown();
    void exeJumpDown();
    void exePointing();
    void endPointing();

    s32 mType;
    TQuat4f mQuat;
    TPos3f mBaseMtx;
    TPos3f mRippleMtx;
    s32 mFrame;
    s32 mBlinkCount;
    s32 mBlinkFrame;
    s32 mSpinAnimTimer;
    f32 mRadiusScale;
    ModelObj* mBreakModel;
    AnimScaleController* mAnimScaleController;
    WalkerStateBindStarPointer* mBindStarPointerState;
    s32 mJumpCount;
    s32 _124;
    f32 mGravityRate;
    f32 mRollHeight;
    s32 mRollSoundTimer;
};
