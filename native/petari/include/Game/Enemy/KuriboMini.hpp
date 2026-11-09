#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class ItemGenerator;
class WalkerStateWander;
class WalkerStateFindPlayer;
class WalkerStateChase;
class WalkerStateStagger;
class WalkerStateBindStarPointer;

class KuriboMini : public LiveActor {
public:
    KuriboMini(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initSensor();
    void initState();
    bool requestHipDropDown(HitSensor*, HitSensor*);
    bool requestFlatDown(HitSensor*, HitSensor*);
    bool requestPressDown();
    bool requestBlowDown(HitSensor*, HitSensor*);
    bool requestStagger(HitSensor*, HitSensor*);
    bool requestAttackSuccess();
    bool tryFind();
    bool tryPointBind();
    bool tryDeadMap();
    void exeWander();
    void exeFindPlayer();
    void exeChase();
    void exeStagger();
    void exeBindStarPointer();
    void endBindStarPointer();
    void exeAttackSuccess();
    void exeHipDropDown();
    void exeFlatDown();
    void exePressDown();
    void exeBlowDown();
    void calcPassiveMovement();
    bool isEnableAttack() const;
    bool isEnableKick() const;
    bool isDown() const;

    AnimScaleController* mScaleController;
    ItemGenerator* mItemGenerator;
    WalkerStateWander* mStateWander;
    WalkerStateFindPlayer* mStateFindPlayer;
    WalkerStateChase* mStateChase;
    WalkerStateStagger* mStateStagger;
    WalkerStateBindStarPointer* mStateBindStarPointer;
    Quat4f mRotateQuat;
    Vec3f mFront;
};
