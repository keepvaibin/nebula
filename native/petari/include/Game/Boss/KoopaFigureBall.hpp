#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class Koopa;

namespace MR {
    struct ActorMoveParam;
};  // namespace MR

class KoopaFigureBall : public LiveActor {
public:
    KoopaFigureBall(const char* pName, Koopa* pKoopa, f32, const MR::ActorMoveParam* pMoveParam);

    virtual ~KoopaFigureBall();
    virtual void init(const JMapInfoIter& rIter);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual MtxPtr getBaseMtx() const;
    virtual void control();

    Koopa* mKoopa;
    TVec3f mFront;
    f32 mRadius;
    f32 mAngle;
    TPos3f mBaseMtx;
    const MR::ActorMoveParam* mMoveParam;
};
