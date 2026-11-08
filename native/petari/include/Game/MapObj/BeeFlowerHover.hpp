#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LodCtrl;
class MapPartsRailMover;
class MapPartsRailPosture;

class BeeFlowerHover : public LiveActor {
public:
    BeeFlowerHover(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
