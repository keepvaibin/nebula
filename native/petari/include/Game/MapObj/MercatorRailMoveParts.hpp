#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class MapPartsAppearController;
class MapPartsRailMover;
class MapPartsRailRotator;

class MercatorRailMoveParts : public LiveActor {
public:
    MercatorRailMoveParts(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void startMove();
    void endMove();
    void updatePose();

    MapPartsRailMover* mRailMover;
    MapPartsRailRotator* mRailRotator;
    MapPartsAppearController* mAppearController;
    Mtx34f mRotateMtx;
    Vec3f mLocalTrans;
    Vec3f mLocalRotation;
    bool mIsNotMoving;
};