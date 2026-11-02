#pragma once

#include "Game/Enemy/BegomanBase.hpp"

class ActorCameraInfo;
class BegomanBaby;
class BegomanSpringHead;
class MultiEventCamera;

class BegomanSpring : public BegomanBase {
public:
    BegomanSpring(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void makeActorDead();
    virtual void kill();
    virtual void control();
    virtual void calcAnim();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual u32 getKind() const {
        return 2;
    }
    virtual bool onTouchElectric(const TVec3f& rPos, const TVec3f& rDirection);
    virtual bool setNerveReturn();
    virtual const Nerve* getNerveWait();

    void initEventCameras(const JMapInfoIter& rIter);
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
    TPos3f mHeadMatrix;
    TPos3f mWaterColumnMatrix;
    TVec3f mCameraStartPos;
    bool mIsCameraActive;
    ActorCameraInfo* mCameraInfo;
    s32 mType;
    BegomanBaby* mBaby;
    TVec3f mHidePos;
};
