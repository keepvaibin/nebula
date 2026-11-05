#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class WalkerStateBlowDamageParam {
public:
    int _0;
};

class WalkerStateBlowDamage : public ActorStateBase< LiveActor > {
public:
    WalkerStateBlowDamage(LiveActor* pHost, Vec3f* pDirection, WalkerStateBlowDamageParam* pBlowDamageParam);

    virtual void appear();

    void exeBlow();
    void exeBlowLand();

    Vec3f* mDirection;
    WalkerStateBlowDamageParam* mBlowDamageParam;
};
