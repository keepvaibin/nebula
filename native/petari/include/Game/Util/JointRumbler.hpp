#pragma once

#include <nebula_geometry.hpp>

class LiveActor;
class RumbleCalculatorCosMultLinear;
class XjointTransform;

class JointRumbler {
public:
    enum Flag {
        FLAG_EXTRA_SCALE = 1 << 0,  // rumble the joint's extra scale, not its scale
        FLAG_LOCK_X = 1 << 1,
        FLAG_LOCK_Y = 1 << 2,
        FLAG_LOCK_Z = 1 << 3,
    };

    JointRumbler(LiveActor* pActor, const char* pJointName, f32 frequency, f32 amplitude, u32 duration, bool useExtraScale, s32 startDelay);

    void update();
    void start();
    void reset();
    bool isRumbling() const;
    void updateModelScale(f32 scale);
    void updateScale(Vec3f* pVec, f32 scale);

    inline bool testFlag(Flag flag) const {
        return static_cast< u32 >(mFlags & flag) == flag;
    }

    RumbleCalculatorCosMultLinear* mRumbleCalculator;
    XjointTransform* mJointTransform;
    s32 mStartDelay;
    s32 mDelayLeft;
    f32 mModelScale;
    u16 mFlags;
};
