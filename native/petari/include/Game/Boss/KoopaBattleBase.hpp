#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include <nebula_geometry.hpp>

class Koopa;
class KoopaStateDamageEscape;
class KoopaStateGuard;
class LiveActor;

namespace MR {
    struct ActorMoveParam;
};  // namespace MR

class KoopaBattleBase : public ActorStateBase< Koopa > {
public:
    KoopaBattleBase(const char* pName, Koopa* pKoopa);

    virtual void init();
    virtual void kill();

    void updateChasePlayer(const MR::ActorMoveParam& rMoveParam);
    bool updateWander(const MR::ActorMoveParam& rMoveParam);
    bool updateSearch();
    bool updateFind();
    bool updateDamageReverse(const Nerve* pEndNerve, const Nerve* pRecoverNerve);
    bool updateRecover(const Nerve* pNerve);

    KoopaStateDamageEscape* mStateDamageEscape;
    KoopaStateGuard* mStateGuard;
    s32 mWanderTime;
};
