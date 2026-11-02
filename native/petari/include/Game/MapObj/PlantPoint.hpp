#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class PlantPoint {
public:
    PlantPoint(const TVec3f&, const TVec3f&, f32);

    virtual void setAxisAndPos(const TVec3f&, const TVec3f&, const TVec3f&, const TVec3f&);

    TVec3f mPosition;
    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
    f32 mThickness;
};
