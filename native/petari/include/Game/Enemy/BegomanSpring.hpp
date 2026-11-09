#pragma once

#include "Game/Enemy/BegomanBase.hpp"

class ActorCameraInfo;
class BegomanBaby;
class BegomanSpringHead;
class MultiEventCamera;

class BegomanSpring : public BegomanBase {
public:
    BegomanSpring(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void makeActorDead() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAnim() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual u32 getKind() const override {
        return Kind_Spring;
    }
    virtual bool onTouchElectric(const Vec3f& rPos, const Vec3f& rDirection) override;
    virtual bool setNerveReturn() override;
    virtual const Nerve* getNerveWait() override;

    void initEventCameras(const TableIter& rIter);
    bool isNrvStarPointerBindable() const;
    bool isNrvBroken() const;
    void keepVelocityAlongGravity();
    void tryHopEndBefore(s32 endStep);
    void exeNoCalcWait();
    void exeWait();
    void exeSignAttack();
    void exePursue();
    void exeTurn();
    void exeBrake();
    void exeStepBack();
    void exeReturn();
    void exeProvoke();
    void exeShake();
    void exeTrample();
    void exeHitReaction();
    void exeTired();
    void exeBlow();
    void exeElectricDeath();
    void exeHide();
    void exeHop();
    void exeInWater();
    void exeKeepDistance();
    void exeBindStarPointer();
    void endNoCalcWait();
    void endWait();
    void endPursue();
    void endHide();
    void endBindStarPointer();
    void generateItem();
    void startCamera();
    void updateCamera();
    void endCamera();

    MultiEventCamera* mEventCamera;
    BegomanSpringHead* mHead;
    Pos3f mHeadMatrix;
    Pos3f mWaterColumnMatrix;
    Vec3f mCameraStartPos;
    bool mIsCameraActive;
    ActorCameraInfo* mCameraInfo;
    bool mIsHideType;
    BegomanBaby* mBaby;
    Vec3f mHidePos;
};
