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
        return const_cast< MtxPtr >(mBaseMtx.mMtx);
    };
    virtual void calcAndSetBaseMtx();

    void startJumpOut();
    void movePlayer();
    void exeSpinDriverAppear();
    void exeSpinDriverStart();
    void exeSpinDriverShoot();
    void exeJumpOut();
    void exeWhiteOut();

    SpinDriverShootPath* mShootPath;
    SpinDriverPathDrawer* mPathDrawer;
    Pos3f mBaseMtx;
    Pos3f mPlayerDemoMtx;

    ActorCameraInfo* mCameraInfo;
};
