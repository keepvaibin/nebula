#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "JSystem/JGeometry/TVec.hpp"

class LavaHomeSeesawRotator;
class ProjmapEffectMtxSetter;

class LavaHomeSeesaw : public LiveActor {
public:
    LavaHomeSeesaw(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();

    LavaHomeSeesawRotator* mSeesawRotator;
    ProjmapEffectMtxSetter* mMtxSetter;
    TVec3f _94;
    f32 mYOffset;
};
