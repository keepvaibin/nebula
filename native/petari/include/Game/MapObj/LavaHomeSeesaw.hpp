#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LavaHomeSeesawRotator;
class ProjmapEffectMtxSetter;

class LavaHomeSeesaw : public LiveActor {
public:
    LavaHomeSeesaw(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();

    LavaHomeSeesawRotator* mSeesawRotator;
    ProjmapEffectMtxSetter* mMtxSetter;
    Vec3f _94;
    f32 mYOffset;
};
