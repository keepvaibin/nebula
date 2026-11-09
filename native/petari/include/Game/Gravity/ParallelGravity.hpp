#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class ParallelGravity : public PlanetGravity {
public:
    enum RANGE_TYPE { RangeType_Sphere = 0, RangeType_Box = 1, RangeType_Cylinder = 2 };

    enum DISTANCE_CALC_TYPE { DistanceCalcType_Default = -1, DistanceCalcType_X = 0, DistanceCalcType_Y = 1, DistanceCalcType_Z = 2 };

    ParallelGravity();

    virtual void updateMtx(const Pos3f& rMtx) override;
    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const override;

    void setPlane(const Vec3f& rPlaneUp, const Vec3f& rPlanePos);
    void setRangeBox(const Pos3f& rMtx);
    void setRangeCylinder(f32 radius, f32 height);
    void setRangeType(RANGE_TYPE rangeType);
    void setBaseDistance(f32 val);
    void setDistanceCalcType(DISTANCE_CALC_TYPE distanceCalcType);
    bool isInSphereRange(const Vec3f& rPosition, f32* pScalar) const;
    bool isInBoxRange(const Vec3f& rPosition, f32* pScalar) const;
    bool isInCylinderRange(const Vec3f& rPosition, f32* pScalar) const;
    bool isInRange(const Vec3f& rPosition, f32* pScalar) const;

    Pos3f mLocalMtx;
    Pos3f mWorldMtx;
    f32 mExtentX;
    f32 mExtentY;
    f32 mExtentZ;
    Vec3f mPlanePosition;
    Vec3f mPlaneUpVec;
    Vec3f mWorldPlanePosition;
    Vec3f mWorldPlaneUpVec;
    f32 mCylinderHeight;
    f32 mCylinderRadius;
    f32 mBaseDistance;
    RANGE_TYPE mRangeType;
    DISTANCE_CALC_TYPE mDistanceCalcType;
};
