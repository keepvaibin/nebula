#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include <nebula_geometry.hpp>

class BossKameck;
class BossKameckBattlePattern;
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
    void setBattlePattern(BossKameckBattlePattern* pBattlePattern);
    bool requestDamage(HitSensor*, HitSensor*);
    bool requestGuard(HitSensor*, HitSensor*);
    bool tryAttackWait();
    bool trySummonKameck();
    bool tryChargeBeam();
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
    BossKameckBattlePattern* mBattlePattern;
    KameckBeam* mBeam;
    KameckBeamEventListener* mBeamEventListener;
    Vec3f mMoveTarget;
    s32 mMoveRailIndex;
    s32 mMoveNumMax;
    s32 mMoveNum;
    s32 mBeamNumAlone;
    bool mIsFinal;
    bool mIsVs2;
};
