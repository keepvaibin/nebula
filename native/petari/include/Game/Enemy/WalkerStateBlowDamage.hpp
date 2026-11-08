#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class WalkerStateBlowDamage : public ActorStateBase< LiveActor > {
public:
    WalkerStateBlowDamage(LiveActor* pHost, Vec3f* pDirection);

    virtual void appear();

    void exeBlow();
    void exeBlowLand();

    Vec3f* mDirection;
};
