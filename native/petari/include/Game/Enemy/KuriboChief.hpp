#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateWander;
class WalkerStateFindPlayer;
class WalkerStateChase;
class WalkerStateStagger;
class WalkerStateBindStarPointer;
class ItemGenerator;
class KeySwitch;

class KuriboChief : public LiveActor {
public:
    KuriboChief(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initSensor();
    void initState();
    void initKeySwitch(const TableIter&);
    bool requestStagger(HitSensor*, HitSensor*);
    bool requestBlowDown(HitSensor*, HitSensor*);
    bool tryFind();
    bool tryPointBind();
    void exeWander();
    void exeFindPlayer();
    void exeChase();
    void exeStagger();
    void exeTrample();
    void exeBindStarPointer();
    void endBindStarPointer();
    void exeAttackSuccess();
    void exeBlowDown();
    void exeBlowDownLand();
    bool isEnableAttack() const;
    bool isEnableKick() const;
    bool isDown() const;

    AnimScaleController* mScaleController;
    WalkerStateWander* mStateWander;
    WalkerStateFindPlayer* mStateFindPlayer;
    WalkerStateChase* mStateChase;
    WalkerStateStagger* mStateStagger;
    WalkerStateBindStarPointer* mStateBindStarPointer;
    ItemGenerator* mItemGenerator;
    KeySwitch* mKeySwitch;
    Quat4f mRotateQuat;
    Vec3f mFront;
};
