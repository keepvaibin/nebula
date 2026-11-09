#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MapPartsSeesaw1AxisRotator;

class PhantomShipHandle : public LiveActor {
public:
    PhantomShipHandle(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    MapPartsSeesaw1AxisRotator* mRotator;
};
