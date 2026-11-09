#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LodCtrl;
class MapPartsRailMover;
class MapPartsRailPosture;

class BeeFlowerHover : public LiveActor {
public:
    BeeFlowerHover(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeSoftTouch();
    void exeSoftTouchWait();
    void exeHardTouch();
    void exeRecover();

    Mtx34f mRotateMtx;
    f32 mRotateSpeed;
    LodCtrl* mLodCtrlPlanet;
    MapPartsRailMover* mRailMover;
    MapPartsRailPosture* mRailPosture;
    Vec3f mClipCenter;
};
