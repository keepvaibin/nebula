#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/MapPartsAppearController.hpp"
#include "Game/MapObj/MapPartsRotator.hpp"

class MercatorRotateParts : public LiveActor {
public:
    MercatorRotateParts(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    MapPartsRotator* mRotator;
    MapPartsAppearController* mAppearController;
    Vec3f mLocalTrans;
    Vec3f mLocalRotate;
};