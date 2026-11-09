#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;
class PartsModel;

class LavaJamboSunPlanet : public LiveActor {
public:
    LavaJamboSunPlanet(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;

    void exeWait();
    void exeMove();

    PartsModel* mBloomModel;
    LodCtrl* mLodCtrl;
    f32 mMoveSpeed;
    Vec3f mClipCenter;
};
