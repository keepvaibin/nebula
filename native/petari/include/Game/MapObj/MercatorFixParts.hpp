#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MapPartsAppearController;

class MercatorFixParts : public LiveActor {
public:
    MercatorFixParts(const char*);

    virtual void init(const TableIter&) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    MapPartsAppearController* mAppearController;
};
