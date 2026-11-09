#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class BigBubbleMoveLimitter : public NameObj {
public:
    enum LimitterType {
        LimitterType_Cylinder = 0,
        LimitterType_Plane = 1,
    };

    enum LimitterMarginType {
        LimitterMarginType_Center = -1,
        LimitterMarginType_Outside = 0,
        LimitterMarginType_Inside = 1,
    };

    BigBubbleMoveLimitter(const char*, s32);

    virtual void init(const TableIter&) override;

    bool limitPosition(Vec3f*, f32) const;
    bool limitVelocity(Vec3f*, const Vec3f&, f32) const;

    s32 getLimitterID() const {
        return mID;
    }

    inline f32 getLimitterOffset(f32 offset) const {
        switch (mLimitMarginType) {
        case LimitterMarginType_Outside:
            return offset;
        case LimitterMarginType_Inside:
            return -offset;
        default:
            return 0.0f;
        }
    }

    s32 mID;
    Vec3f mPosition;
    Vec3f mAxis;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    f32 mCylinderRadius;
    f32 mCylinderHeight;
    f32 mPlaneWidth;
    f32 mPlaneHeight;
    s32 mLimitMarginType;
    s32 mType;
};

namespace MR {
    NameObj* createBigBubbleMoveLimitterCylinder(const char*);
    NameObj* createBigBubbleMoveLimitterPlane(const char*);
};  // namespace MR
