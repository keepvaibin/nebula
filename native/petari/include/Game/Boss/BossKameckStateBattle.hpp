#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class BossKameck;
class BossKameckBattlePattarn;
class BossKameckMoveRail;
class HitSensor;
class KameckBeam;
class KameckBeamEventListener;

class BossKameckStateBattle : public ActorStateBase< BossKameck > {
public:
    BossKameckStateBattle(BossKameck*);

    virtual void init();
    virtual void appear();
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void setMoveRail(BossKameckMoveRail*);
    void setBattlePattarn(BossKameckBattlePattarn*);
    bool requestDamage(HitSensor*, HitSensor*);
    bool requestGuard(HitSensor*, HitSensor*);
    bool tryAttackWait();
    bool trySummonKameck();
    bool tryChargeBram();
    void startMove();
    void exeWait();
    void exeMove();
    void exeHideMoveStart();
    void exeHideMove();
    void exeHideMoveEnd();
    void exeSummonKameckWait();
    void exeSummonKameck();
    void exeAttackWait();
    void exeAttack();
    void exeDamage();
    void exeRecover();
    void exeGuard();
    void selectStoppablePosition();
    void selectPosition();
    bool isEnableDamage() const;
    bool isEnableGuard() const;

    BossKameckMoveRail* mMoveRail;
    BossKameckBattlePattarn* mBattlePattarn;
    KameckBeam* mBeam;
    KameckBeamEventListener* mBeamEventListener;
    TVec3f _20;
    s32 _2C;
    s32 _30;
    s32 _34;
    s32 _38;
    bool mIsFinal;
    bool mIsVs2;
};
