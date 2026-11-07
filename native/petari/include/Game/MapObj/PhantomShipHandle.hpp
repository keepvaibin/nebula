#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MapPartsSeesaw1AxisRotator;

class PhantomShipHandle : public LiveActor {
public:
    PhantomShipHandle(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    MapPartsSeesaw1AxisRotator* mRotator;
};
