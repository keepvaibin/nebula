#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class Koopa;

namespace MR {
    struct ActorMoveParam;
};  // namespace MR

class KoopaFigureBall : public LiveActor {
public:
    KoopaFigureBall(const char* pName, Koopa* pKoopa, f32, const MR::ActorMoveParam* pMoveParam);

    virtual void init(const TableIter& rIter);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual MtxPtr getBaseMtx() const;
    virtual void control();

    Koopa* mKoopa;
    Vec3f mFront;
    f32 mRadius;
    f32 mAngle;
    Pos3f mBaseMtx;
    const MR::ActorMoveParam* mMoveParam;
};
