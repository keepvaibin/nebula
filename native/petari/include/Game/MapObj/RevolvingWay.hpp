#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class RevolvingWay : public LiveActor {
public:
    RevolvingWay(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void calcAndSetBaseMtx() override;

    void exeWait();
    void addAccelMoment();

    Quat4f mRotateQuat;
    // Rotation per step, from pointer strokes; decays each step.
    Vec3f mRotateMoment;
    f32 mRadius;
};
