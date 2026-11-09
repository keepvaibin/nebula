#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class ConeGravity : public PlanetGravity {
public:
    ConeGravity();

    virtual void updateMtx(const Pos3f& rMtx) override;
    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const override;

    void setLocalMatrix(const Pos3f& rMtx);
    void setEnableBottom(bool isEnable);
    void setTopCutRate(f32 topCutRate);

    /*
     * The x column represents the radius vector. The y column represents the central axis vector,
     * point from the center of the base of the cone to the apex. The translation column represents
     * the location of the center of the base of the cone, and the z column is unused.
     */
    Pos3f mLocalMtx;
    Pos3f mWorldMtx;

    f32 mValidDegree;  // set in ctor but never used
    f32 mValidCos;     // set in ctor but never used
    f32 mWorldRadius;
    bool mEnableBottom;
    f32 mTopCutRate;
};
