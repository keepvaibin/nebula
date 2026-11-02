#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NameObj/NameObjArchiveListCollector.hpp"

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

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initSensor();
    void initState();
    void initKeySwitch(const JMapInfoIter&);
    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);
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
    TQuat4f _AC;
    TVec3f _BC;
};
