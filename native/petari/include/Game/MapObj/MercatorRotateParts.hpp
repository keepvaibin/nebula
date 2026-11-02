#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/MapPartsAppearController.hpp"
#include "Game/MapObj/MapPartsRotator.hpp"

class MercatorRotateParts : public LiveActor {
public:
    MercatorRotateParts(const char*);

    virtual ~MercatorRotateParts();
    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    MapPartsRotator* mRotator;
    MapPartsAppearController* mAppearController;
    TVec3f mLocalTrans;
    TVec3f mLocalRotate;
};