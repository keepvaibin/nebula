#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class HitResult {
public:
    HitResult();

    Vec3f mHitPos;
    Vec3f mPushedPos;
    Vec3f mNormal;
};

class TripodBossMovableArea {
public:
    TripodBossMovableArea();

    virtual bool collideSphere(HitResult* pResult, const Vec3f& rStart, f32 radius, const Vec3f& rMove) const;
    virtual void calcNearLandingPosition(Vec3f* pPos, const Vec3f& rPos) const;
    virtual void calcLandingNormal(Vec3f* pNorm, const Vec3f& rPos) const;
    virtual void calcLandingFront(Vec3f* pFront, const Vec3f& rPos) const;

    void setCenter(const Vec3f& rCenter);
    void setBaseAxis(const Vec3f& rAxis);
    void setFrontVector(const Vec3f& rFront);
    void setRadius(f32 radius);

    Vec3f mCenter;
    Vec3f mBaseAxis;  // up through the area
    Vec3f mFront;
    f32 mRadius;
    f32 mRangeMin;  // cosine range from mBaseAxis that landing positions keep to
    f32 mRangeMax;
};
