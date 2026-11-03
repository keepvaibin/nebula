#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class RosettaChair : public LiveActor {
public:
    RosettaChair(const char*);

    virtual void init(const JMapInfoIter&);

    void setDefaultPose();
    void startDemo();

    TVec3f mDefaultPosition;
    TVec3f mDefaultRotation;
    TMtx34f mScaleMtx;
};
