#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class DiskTorusGravity : public PlanetGravity {
public:
    DiskTorusGravity();

    virtual void updateMtx(const TPos3f& rMtx);
    virtual bool calcOwnGravityVector(TVec3f* pDest, f32* pScalar, const TVec3f& rPosition) const;

    void setPosition(const TVec3f& rPosition);
    void setDirection(const TVec3f& rDirection);
    void setRadius(f32 val);
    void setDiskRadius(f32 val);
    void setEdgeType(s32 val);
    void setBothSide(bool val);

    TVec3f mLocalPosition;
    TVec3f mTranslation;
    TVec3f mLocalDirection;
    TVec3f mRotation;
    f32 mRadius;
    f32 mWorldRadius;
    f32 mDiskRadius;
    s32 mEdgeType;
    bool mEnableBothSide;
};
