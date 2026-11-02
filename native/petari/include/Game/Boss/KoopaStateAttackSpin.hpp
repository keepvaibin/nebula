#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class HitSensor;
class Koopa;

namespace MR {
    struct ActorMoveParam;
};  // namespace MR

class KoopaStateAttackSpin : public ActorStateBase< Koopa > {
public:
    KoopaStateAttackSpin(Koopa* pKoopa);

    virtual ~KoopaStateAttackSpin();
    virtual void init();
    virtual void appear();
    virtual void kill();

    bool attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    bool tryDamage(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWaitToStart();
    void exeRun();
    void exeStart();
    void exeSpin();
    void exeEnd();
    void exeEven();

    s32 mMaxAttacks;
    s32 mAttacks;
    const MR::ActorMoveParam* mMoveParam;
    s32 mSpinDelay;
};
