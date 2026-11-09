#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class SegmentGravity : public PlanetGravity {
public:
    SegmentGravity();

    virtual void updateMtx(const Pos3f& rMtx) override;
    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const override;

    void setGravityPoint(u32 index, const Vec3f& rGravityPoint);
    void setSideVector(const Vec3f& rSideVec);
    void setValidSideDegree(f32 validSideDegree);
    void setEdgeValid(u32 index, bool isValid);
    void updateLocalParam();

    // Represents the centers of each base of the cylinder
    Vec3f mGravityPoints[2];
    Vec3f mWorldGravityPoints[2];

    Vec3f mSideVector;
    Vec3f mOppositeSideVecOrtho;
    Vec3f mWorldOppositeSideVecOrtho;

    Vec3f mAxis;
    f32 mAxisLength;

    f32 mValidSideDegree;
    f32 mValidSideCos;
    f32 mHemisphereExtension[2];
    bool mEdges[2];
};
