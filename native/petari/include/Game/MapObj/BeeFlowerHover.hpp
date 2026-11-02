#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

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

    TMtx34f _8C;
    f32 _BC;
    LodCtrl* mLodCtrlPlanet;
    MapPartsRailMover* mRailMover;
    MapPartsRailPosture* mRailPosture;
    TVec3f _CC;
};
