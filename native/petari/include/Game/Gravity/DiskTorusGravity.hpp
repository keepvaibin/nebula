#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class DiskTorusGravity : public PlanetGravity {
public:
    DiskTorusGravity();

    virtual void updateMtx(const Pos3f& rMtx);
    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const;

    void setPosition(const Vec3f& rPosition);
    void setDirection(const Vec3f& rDirection);
    void setRadius(f32 val);
    void setDiskRadius(f32 val);
    void setEdgeType(s32 val);
    void setBothSide(bool val);

    Vec3f mLocalPosition;
    Vec3f mTranslation;
    Vec3f mLocalDirection;
    Vec3f mRotation;
    f32 mRadius;
    f32 mWorldRadius;
    f32 mDiskRadius;
    s32 mEdgeType;
    bool mEnableBothSide;
};
