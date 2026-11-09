#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class MapPartsAppearController;
class MapPartsRailMover;
class MapPartsRailRotator;

class MercatorRailMoveParts : public LiveActor {
public:
    MercatorRailMoveParts(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

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