#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class DiskGravity : public PlanetGravity {
public:
    DiskGravity();

    virtual void updateMtx(const Pos3f& rMtx) override;
    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const override;

    void updateLocalParam();

    void setLocalPosition(const Vec3f& rLocalPos);
    void setLocalDirection(const Vec3f& rLocalDir);
    void setSideDirection(const Vec3f& rSideDir);
    void setRadius(f32 radius);
    void setValidDegree(f32 validDegree);
    void setBothSide(bool isBothSide);
    void setEnableEdgeGravity(bool isEnable);

    // Position
    Vec3f mLocalPosition;
    Vec3f mWorldPosition;

    // Unit normal vector to the circular surface of the disk
    Vec3f mLocalNormal;
    Vec3f mWorldNormal;

    Vec3f mSideDirection;

    Vec3f mOppositeSideVecOrtho;
    Vec3f mWorldOppositeSideVecOrtho;

    // Radius of the circular face of the disk
    f32 mLocalRadius;
    f32 mWorldRadius;

    // Number of degrees of the circle gravity applies to
    f32 mValidDegree;
    f32 mValidCos;

    bool mEnableBothSide;
    bool mEnableEdgeGravity;
};
