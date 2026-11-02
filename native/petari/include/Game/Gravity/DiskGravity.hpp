#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class DiskGravity : public PlanetGravity {
public:
    DiskGravity();

    virtual void updateMtx(const TPos3f& rMtx);
    virtual bool calcOwnGravityVector(TVec3f* pDest, f32* pScalar, const TVec3f& rPosition) const;

    void updateLocalParam();

    void setLocalPosition(const TVec3f& rLocalPos);
    void setLocalDirection(const TVec3f& rLocalDir);
    void setSideDirection(const TVec3f& rSideDir);
    void setRadius(f32 val);
    void setValidDegee(f32 val);
    void setBothSide(bool val);
    void setEnableEdgeGravity(bool val);

    // Position
    TVec3f mLocalPosition;
    TVec3f mWorldPosition;

    // Unit normal vector to the circular surface of the disk
    TVec3f mLocalNormal;
    TVec3f mWorldNormal;

    TVec3f mSideDirection;

    TVec3f mOppositeSideVecOrtho;
    TVec3f mWorldOppositeSideVecOrtho;

    // Radius of the circular face of the disk
    f32 mLocalRadius;
    f32 mWorldRadius;

    // Number of degrees of the circle gravity applies to
    f32 mValidDegree;
    f32 mValidCos;

    bool mEnableBothSide;
    bool mEnableEdgeGravity;
};
