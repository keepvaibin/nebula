#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class RosettaChair : public LiveActor {
public:
    RosettaChair(const char*);

    virtual void init(const TableIter&) override;

    void setDefaultPose();
    void startDemo();

    Vec3f mDefaultPosition;
    Vec3f mDefaultRotation;
    Mtx34f mScaleMtx;
};
