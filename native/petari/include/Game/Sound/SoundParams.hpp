#pragma once

#include <nebula_types.h>

// A sound's mix values as the game sets them and the sequencer combines them.
struct SoundParams {
    f32 mVolume = 1.0f;
    f32 mFxMix = 0.0f;
    f32 mPitch = 1.0f;
    f32 mPan = 0.5f;
    f32 mDolby = 0.0f;

    void init() {
        mVolume = 1.0f;
        mPitch = 1.0f;
        mFxMix = 0.0f;
        mPan = 0.5f;
        mDolby = 0.0f;
    }

    void combine(const SoundParams& a, const SoundParams& b) {
        mVolume = a.mVolume * b.mVolume;
        mPitch = a.mPitch * b.mPitch;
        mFxMix = a.mFxMix + b.mFxMix;
        mPan = a.mPan + b.mPan - 0.5f;
        mDolby = a.mDolby + b.mDolby;
    }
};
