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

    virtual void init(const TableIter& rIter) override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual MtxPtr getBaseMtx() const override;
    virtual void control() override;

    Koopa* mKoopa;
    Vec3f mFront;
    f32 mRadius;
    f32 mAngle;
    Pos3f mBaseMtx;
    const MR::ActorMoveParam* mMoveParam;
};
