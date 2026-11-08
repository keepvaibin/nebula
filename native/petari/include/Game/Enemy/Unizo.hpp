#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class ModelObj;
class WalkerStateBindStarPointer;

class Unizo : public LiveActor {
public:
    enum Type { TypeSea, TypeLand, TypeShoal };

    Unizo(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void makeActorDead();
    virtual MtxPtr getBaseMtx() const;
    virtual void startClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initType();
    void updateRotate();
    void updateVelocity();
    void updateInfluence();
    void updateBlink();
    void updateSurfaceEffect();
    void deleteEffect();
    void doJump();
    void doAttack(HitSensor* pSensor);
    void doJumpDown();
    void doFireDown(const Vec3f& rDirection);
    void doBreak();
    void doSpin();
    void fallBroken();
    bool isBreakModelEnd() const;
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
    Quat4f mQuat;     // the roll
    Pos3f mBaseMtx;   // up against gravity, at mPosition
    Pos3f mRippleMtx;
    s32 mFrame;
    s32 mBlinkCount;  // blinks left after an enemy collision
    s32 mBlinkFrame;  // nerve step (mod 200) of the idle blink
    s32 mSpinAnimTimer;
    f32 mRadiusScale;
    ModelObj* mBreakModel;
    AnimScaleController* mAnimScaleController;
    WalkerStateBindStarPointer* mBindStarPointerState;
    s32 mJumpCount;  // set by appear, a jump or a player hit; cleared on the next ground touch
    f32 mGravityRate;
    f32 mRollHeight;
    s32 mRollSoundTimer;
};
