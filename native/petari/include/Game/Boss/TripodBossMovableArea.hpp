#pragma once

#include <JSystem/JGeometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class HitResult {
public:
    HitResult();

    TVec3f _0;
    TVec3f _C;
    TVec3f _18;
};

class TripodBossMovableArea {
public:
    /// @brief Creates a new `TripodBossMovableArea`.
    TripodBossMovableArea();

    virtual bool collideSphere(HitResult*, const TVec3f&, f32, const TVec3f&) const;
    virtual void calcNearLandingPosition(TVec3f*, const TVec3f&) const;
    virtual void calcLandingNormal(TVec3f*, const TVec3f&) const;
    virtual void calcLandingFront(TVec3f*, const TVec3f&) const;

    void setCenter(const TVec3f&);
    void setBaseAxis(const TVec3f&);
    void setFrontVector(const TVec3f&);
    void setRadius(f32);

    TVec3f mCenter;
    TVec3f mBaseAxis;
    TVec3f mFront;
    f32 mRadius;
    f32 _2C;
    f32 _30;
};
