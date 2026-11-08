#pragma once

#include "Game/Sound/SoundParams.hpp"

struct SoundProperty {
    void init() {
        mVolume = 1.0f;
        mFxMix = 0.0f;
        mPitch = 1.0f;
    }

    f32 mVolume;
    f32 mFxMix;
    f32 mPitch;
};

struct SoundTransition {
    struct Ramp {
        void zero() {
            mStep = 0.0f;
            mRemainingSteps = 0;
            mTargetValue = 0.0f;
        }

        void set(f32 newValue, f32 currentValue, u32 maxSteps) {
            mRemainingSteps = maxSteps;
            mStep = (newValue - currentValue) / mRemainingSteps;
            mTargetValue = newValue;
        }

        f32 apply(f32 value) {
            // The last step lands on the target exactly.
            if (mRemainingSteps > 1) {
                mRemainingSteps--;
                value += mStep;
            } else if (mRemainingSteps == 1) {
                mRemainingSteps = 0;
                value = mTargetValue;
            }
            return value;
        }

        f32 mStep;
        f32 mTargetValue;
        u32 mRemainingSteps;
    };

    void init() {
        mVolume.zero();
        mPitch.zero();
        mFxMix.zero();
        mPan.zero();
        mDolby.zero();
    }

    void apply(SoundParams* params) {
        params->mVolume = mVolume.apply(params->mVolume);
        params->mPitch = mPitch.apply(params->mPitch);
        params->mFxMix = mFxMix.apply(params->mFxMix);
        params->mDolby = mDolby.apply(params->mDolby);
        params->mPan = mPan.apply(params->mPan);
    }

    Ramp mVolume;
    Ramp mPitch;
    Ramp mFxMix;
    Ramp mPan;
    Ramp mDolby;
};

struct SoundMove {
    SoundMove() : mParams() {
    }

    void init() {
        mParams.init();
        mTransition.init();
    }

    void calc() {
        mTransition.apply(&mParams);
    }

    void moveVolume(f32 newValue, u32 maxSteps);
    void movePitch(f32 newValue, u32 maxSteps);
    void moveFxMix(f32 newValue, u32 maxSteps);

    SoundParams mParams;
    SoundTransition mTransition;
};

struct SoundControls {
    SoundControls() : mMove() {
    }
    void mixOutAll(const SoundParams& inParams, SoundParams* outParams, f32);

    void init() {
        mMove.init();
        mProperty.init();
    }

    SoundParams& getMoveParams() {
        return mMove.mParams;
    }

    SoundProperty mProperty;
    SoundMove mMove;
};
