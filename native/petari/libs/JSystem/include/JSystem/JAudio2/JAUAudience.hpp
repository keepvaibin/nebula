#pragma once


class JAUAudience3DSetting {
public:
    /// @brief Creates a new `JAUAudience3DSetting`.
    JAUAudience3DSetting() {
        init();
    }

    void init();

    f32 mFarDistance;
    f32 mNearDistance;
    f32 mMinVolume;
    f32 mFrontDolbyZ;
    f32 mBehindDolbyZ;
    f32 mMaxDolby;
    f32 mCenterDolby;
    f32 mMinDolby;
    f32 mMinFxMix;
    f32 mMaxFxMix;
    f32 mPanAmplitude;
    f32 mSonicSpeed;
    f32 mPitchDeltaRatioRange;
    u32 mPriorityDownRange;
};
