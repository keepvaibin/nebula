#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class CubeGravity : public PlanetGravity {
public:
    CubeGravity();

    virtual void updateMtx(const Pos3f& rMtx);
    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const;

    void setCube(const Pos3f&);

    /*
     * Area as in region, not area as in Area = Base * h
     *
     * The region is made up of 3 components: x, y, z. For each component, a value of 0 indicates
     * that the position is beyond the negative face of that component, a value of 1 indicates that
     * the position is between the two faces for that component, and a value of 2 indicates that
     * the position is beyond the positive face of that component.
     *
     * The region is encoded as follows (in pseudocode):
     *      `area = area_x * 3^0 + area_y * 3^1 + area_z * 3^2`
     *
     * `calcGravityArea` returns the encoded region
     */
    int calcGravityArea(const Vec3f& rPosition) const;
    bool calcFaceGravity(const Vec3f& rPosition, s32, Vec3f* pDest, f32* pScalar) const;
    bool calcEdgeGravity(const Vec3f& rPosition, s32, Vec3f* pDest, f32* pScalar) const;
    bool calcCornerGravity(const Vec3f& rPosition, s32, Vec3f* pDest, f32* pScalar) const;

    Pos3f mCube;
    Pos3f mPosition;
    f32 lenX;
    f32 lenY;
    f32 lenZ;
    u8 mActiveFaces;
};
