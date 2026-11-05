#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LodCtrl;
class MapPartsRailMover;
class MapPartsRailPosture;

class BeeFlowerHover : public LiveActor {
public:
    BeeFlowerHover(const char*);

    virtual ~BeeFlowerHover();
    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeSoftTouch();
    void exeSoftTouchWait();
    void exeHardTouch();
    void exeRecover();

    Mtx34f _8C;
    f32 _BC;
    LodCtrl* mLodCtrlPlanet;
    MapPartsRailMover* mRailMover;
    MapPartsRailPosture* mRailPosture;
    Vec3f _CC;
};
