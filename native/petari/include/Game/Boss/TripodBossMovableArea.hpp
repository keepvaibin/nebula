#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class HitResult {
public:
    HitResult();

    Vec3f _0;
    Vec3f _C;
    Vec3f _18;
};

class TripodBossMovableArea {
public:
    /// @brief Creates a new `TripodBossMovableArea`.
    TripodBossMovableArea();

    virtual bool collideSphere(HitResult*, const Vec3f&, f32, const Vec3f&) const;
    virtual void calcNearLandingPosition(Vec3f*, const Vec3f&) const;
    virtual void calcLandingNormal(Vec3f*, const Vec3f&) const;
    virtual void calcLandingFront(Vec3f*, const Vec3f&) const;

    void setCenter(const Vec3f&);
    void setBaseAxis(const Vec3f&);
    void setFrontVector(const Vec3f&);
    void setRadius(f32);

    Vec3f mCenter;
    Vec3f mBaseAxis;
    Vec3f mFront;
    f32 mRadius;
    f32 _2C;
    f32 _30;
};
