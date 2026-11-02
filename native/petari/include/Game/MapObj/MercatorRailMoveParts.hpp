#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class MapPartsAppearController;
class MapPartsRailMover;
class MapPartsRailRotator;

class MercatorRailMoveParts : public LiveActor {
public:
    MercatorRailMoveParts(const char*);

    virtual ~MercatorRailMoveParts();
    virtual void init(const JMapInfoIter&);
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
    TMtx34f mRotateMtx;
    TVec3f mLocalTrans;
    TVec3f mLocalRotation;
    TVec3f _E0;
    bool mIsNotMoving;
};