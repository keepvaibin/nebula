#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class AnimScaleParam;
class AnimScaleController;

class CocoSamboHead : public PartsModel {
public:
    CocoSamboHead(LiveActor*);

    virtual void init(const JMapInfoIter&);
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    bool isBodySensor(const HitSensor*) const;
    bool isNerveSwoon() const;
    void updateFrontVecToPlayer(const TVec3f&);
    void exeConnectedBody() {
    }

    void exeFall();
    void exeFallLand();
    void exeSwoon();
    void exeSwoonEnd();
    void exeBlow();

    TVec3f mFrontVec;
};

class CocoSambo : public LiveActor {
public:
    CocoSambo(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initMapToolInfo(const JMapInfoIter& rIter);
    void initSensor();
    bool isPointing();
    bool tryDpdPointing(const Nerve*);
    bool tryToFallHead(const HitSensor*, const HitSensor*);
    bool tryToPress();
    bool tryStarPieceReflect();
    bool tryToBlow();
    void dirToPlayer(f32);

    void exeAppear();
    void exeWait();
    void exeFallHead();
    void exeAttack();
    void exeAttackInterval();
    void exeHide();
    void exeHideWait();
    void exeFallHeadHide();
    void exeRecoverWait();
    void exeRecover();
    void exePressY();
    void exeBlow();
    void exeDpdPointing();
    void endDpdPointing();

    inline bool isNerveDying();
    inline void startBckThisAndHead(const char*);
    inline bool isNerveDown();
    inline bool isSensorTryToFallHead(HitSensor* pSensor);

    CocoSamboHead* mHead;
    TVec3f mHitEffectScale;
    TVec3f mFrontVec;
    TVec3f mUpVec;
    f32 mPlayerSearchDistance;
    AnimScaleParam* mAnimScaleParam;
    AnimScaleController* mAnimScaleController;
    const Nerve* mDpdPointingEndNerve;
    LiveActor** mPointingActorArray;
};
