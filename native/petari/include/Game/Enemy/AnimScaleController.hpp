#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <nebula_geometry.hpp>

class AnimScaleParam {
public:
    AnimScaleParam();

    f32 mAnimSpring;
    f32 mAnimDamping;
    f32 mScaleMin;
    f32 mScaleMax;
    f32 mPointerTime;
    f32 mPointerBase;
    f32 mPointerAmplitude;
    f32 mPointerFreq;
    f32 mHitReactionVelocity;
    f32 mHitReactionSpring;
    f32 mHitReactionDamping;
    u32 mCrushStep;
    f32 mCrushEnd;
};

class AnimScaleController : public NerveExecutor {
public:
    AnimScaleController(AnimScaleParam*);

    void update();
    void updateScale(f32 spring, f32 damping);
    void startCrush();
    void startAnim();
    void startAndAddScaleVelocityY(f32 velocity);

    void setParamTight();
    void startHitReaction();
    void startPointerHitVibration();
    bool isHitReaction(s32 maxStep) const;

    void stopAndReset();
    void resetScale();

    bool tryStop();

    void exeAnim();
    inline void exeStop();
    void exePointerVibration();
    void exeHitReaction();
    void exeCrush();

    AnimScaleParam* mParam;
    Vec3f mScale;
    f32 mScaleVelocity;
};
