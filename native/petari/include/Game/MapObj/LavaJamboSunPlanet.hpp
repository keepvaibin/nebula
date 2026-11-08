#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;
class PartsModel;

class LavaJamboSunPlanet : public LiveActor {
public:
    LavaJamboSunPlanet(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void control();

    void exeWait();
    void exeMove();

    PartsModel* mBloomModel;
    LodCtrl* mLodCtrl;
    f32 mMoveSpeed;
    Vec3f mClipCenter;
};
