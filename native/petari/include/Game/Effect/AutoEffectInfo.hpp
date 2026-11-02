#pragma once

#include "Game/Util/Color.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class JMapInfoIter;

class AutoEffectInfo {
public:
    enum Flag {
        FLAG_FOLLOW_TRANS = 1 << 0,
        FLAG_FOLLOW_ROTATE = 1 << 1,
        FLAG_FOLLOW_SCALE = 1 << 2,
        FLAG_AFFECT_TRANS = 1 << 3,
        FLAG_AFFECT_ROTATE = 1 << 4,
        FLAG_AFFECT_SCALE = 1 << 5,
        FLAG_CONTINUE_ANIM_END = 1 << 6,
    };

    /// @brief Creates a new `AutoEffectInfo`.
    AutoEffectInfo();

    void init(const JMapInfoIter& rIter);
    const char* getName() const;

    const char* mGroupName;
    const char* mAnimName;
    const char* mUniqueName;
    const char* mEffectName;
    const char* mParentName;
    const char* mJointName;
    u16 mFlag;
    Color8 mPrmColor;
    bool mIsValidPrmColor;
    Color8 mEnvColor;
    bool mIsValidEnvColor;
    TVec3f mOffset;
    s32 mStartFrame;
    s32 mEndFrame;
    f32 mScaleValue;
    f32 mRateValue;
    f32 mLightAffectValue;
    s32 mDrawOrder;
};
