#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class AnimScaleParam;
class AnimScaleController;

class CocoSamboHead : public PartsModel {
public:
    CocoSamboHead(LiveActor*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    bool isBodySensor(const HitSensor*) const;
    bool isNerveSwoon() const;
    void updateFrontVecToPlayer(const Vec3f& rGravity);
    void exeConnectedBody() {
    }

    void exeFall();
    void exeFallLand();
    void exeSwoon();
    void exeSwoonEnd();
    void exeBlow();

    Vec3f mFrontVec;
};

class CocoSambo : public LiveActor {
public:
    CocoSambo(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initMapToolInfo(const TableIter& rIter);
    void initSensor();
    bool isPointing();
    bool tryPointerAiming(const Nerve*);
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
    void exePointerAiming();
    void endPointerAiming();

    inline bool isNerveDying();
    inline void startBckThisAndHead(const char*);
    inline bool isNerveDown();
    inline bool isSensorTryToFallHead(HitSensor* pSensor);

    CocoSamboHead* mHead;
    Vec3f mHitEffectScale;
    Vec3f mFrontVec;
    Vec3f mUpVec;
    f32 mPlayerSearchDistance;
    AnimScaleParam* mAnimScaleParam;
    AnimScaleController* mAnimScaleController;
    const Nerve* mPointerAimingEndNerve;
    LiveActor** mPointingActorArray;
};
