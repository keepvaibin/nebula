#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class SpinDriverShootPath;
class SpinDriverPathDrawer;

class AstroDomeDemoStarter : public LiveActor {
public:
    AstroDomeDemoStarter(const char*);

    virtual void init(const TableIter& rIter);
    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)_94.mMtx;
    };
    virtual void calcAndSetBaseMtx();

    void startJumpOut();
    void movePlayer();
    void exeSpinDriverAppear();
    void exeSpinDriverStart();
    void exeSpinDriverShoot();
    void exeJumpOut();
    void exeWhiteOut();

    SpinDriverShootPath* _8C;
    SpinDriverPathDrawer* _90;
    Pos3f _94;
    Pos3f _C4;

    ActorCameraInfo* _F4;
};
