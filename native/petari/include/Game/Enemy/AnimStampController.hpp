#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <nebula_geometry.hpp>

class AnimStampController : public NerveExecutor {
public:
    AnimStampController();

    void init(s32 animStep, f32 baseY, f32 baseXZ, f32 amplitudeY, f32 freqY, f32 amplitudeXZ, f32 freqXZ);
    void startAnim(const Vec3f&);
    void exeAnim();
    void exeAnimEnd();
    bool isAnimEnd() const;

    Vec3f mDir;
    Pos3f mMtx;
    s32 mAnimStep;
    f32 mBaseY;
    f32 mBaseXZ;
    f32 mAmplitudeY;
    f32 mFreqY;
    f32 mAmplitudeXZ;
    f32 mFreqXZ;
};
