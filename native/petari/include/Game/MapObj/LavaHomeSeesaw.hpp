#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LavaHomeSeesawRotator;
class ProjmapEffectMtxSetter;

class LavaHomeSeesaw : public LiveActor {
public:
    LavaHomeSeesaw(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    LavaHomeSeesawRotator* mSeesawRotator;
    ProjmapEffectMtxSetter* mMtxSetter;
    Vec3f mGroundPos;
    f32 mYOffset;
};
