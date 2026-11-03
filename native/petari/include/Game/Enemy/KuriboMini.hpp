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

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
    TQuat4f _A8;
    TVec3f _B8;
};
