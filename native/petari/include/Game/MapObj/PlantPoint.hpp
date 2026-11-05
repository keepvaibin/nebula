#pragma once

#include <nebula_geometry.hpp>

class PlantPoint {
public:
    PlantPoint(const Vec3f&, const Vec3f&, f32);

    virtual void setAxisAndPos(const Vec3f&, const Vec3f&, const Vec3f&, const Vec3f&);

    Vec3f mPosition;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    f32 mThickness;
};
