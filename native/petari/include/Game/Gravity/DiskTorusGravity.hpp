#pragma once

#include "Game/Gravity/PlanetGravity.hpp"

class DiskTorusGravity : public PlanetGravity {
public:
    // Which rim edges pull (placement arg 1).
    enum EdgeType {
        EdgeType_DisableBoth = 0,
        EdgeType_DisableOuter = 1,
        EdgeType_DisableInner = 2,
        EdgeType_EnableBoth = 3,
    };

    DiskTorusGravity();

    virtual void updateMtx(const Pos3f& rMtx) override;
    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const override;

    void setPosition(const Vec3f& rPosition);
    void setDirection(const Vec3f& rDirection);
    void setRadius(f32 radius);
    void setDiskRadius(f32 diskRadius);
    void setEdgeType(s32 edgeType);
    void setBothSide(bool isBothSide);

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
