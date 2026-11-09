#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;
class ProjmapEffectMtxSetter;

class LavaGalaxyParts : public LiveActor {
public:
    LavaGalaxyParts(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void control() override;

    LodCtrl* mLodCtrl;
    ProjmapEffectMtxSetter* mMtxSetter;
};
