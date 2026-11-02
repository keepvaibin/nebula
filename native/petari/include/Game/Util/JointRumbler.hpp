#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class LiveActor;
class RumbleCalculatorCosMultLinear;
class XjointTransform;

class JointRumbler {
public:
    enum Flag {
        FLAG_1 = 1 << 0,
        FLAG_LOCK_X = 1 << 1,
        FLAG_LOCK_Y = 1 << 2,
        FLAG_LOCK_Z = 1 << 3,
    };

    JointRumbler(LiveActor* pActor, const char* pJointName, f32, f32, u32, bool, s32);

    void update();
    void start();
    void reset();
    bool isRumbling() const;
    void updateModelScale(f32 scale);
    void updateScale(TVec3f* pVec, f32 scale);

    inline bool testFlag(Flag flag) const {
        return static_cast< u32 >(mFlags & flag) == flag;
    }

    RumbleCalculatorCosMultLinear* mRumbleCalculator;
    XjointTransform* mJointTransform;
    s32 _8;
    s32 _C;
    f32 mModelScale;
    u16 mFlags;
    bool _16[2];
};
