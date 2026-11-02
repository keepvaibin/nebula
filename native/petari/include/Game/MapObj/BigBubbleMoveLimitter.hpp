#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/TVec.hpp>

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

    virtual void init(const JMapInfoIter&);

    bool limitPosition(TVec3f*, f32) const;
    bool limitVelocity(TVec3f*, const TVec3f&, f32) const;

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
    TVec3f mPosition;
    TVec3f mAxis;
    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
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
