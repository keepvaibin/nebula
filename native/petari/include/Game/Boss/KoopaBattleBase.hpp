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

namespace MR {
    void moveAndTurnToPlayer(LiveActor* pActor, Vec3f* pVec, const MR::ActorMoveParam& rMoveParam);
};  // namespace MR

class KoopaBattleBase : public ActorStateBase< Koopa > {
public:
    KoopaBattleBase(const char* pName, Koopa* pKoopa);

    virtual ~KoopaBattleBase();
    virtual void init();
    virtual void kill();

    void updateChasePlayer(const MR::ActorMoveParam& rMoveParam);
    bool updateWander(const MR::ActorMoveParam& rMoveParam);
    bool updateSearch();
    bool updateFind();
    bool updateRecover(const Nerve* pNerve);

    KoopaStateDamageEscape* mStateDamageEscape;
    KoopaStateGuard* mStateGuard;
    s32 mWanderTime;
};
