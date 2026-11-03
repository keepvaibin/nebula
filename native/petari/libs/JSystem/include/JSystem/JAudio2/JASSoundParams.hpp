#pragma once


struct JASSoundParams {
    void clamp();
    void combine(JASSoundParams const&, JASSoundParams const&);

    void initVolume() {
        mVolume = 1.0f;
    }
    void initFxMix() {
        mFxMix = 0.0f;
    }
    void initPitch() {
        mPitch = 1.0f;
    }
    void initPan() {
        mPan = 0.5f;
    }
    void initDolby() {
        mDolby = 0.0f;
    }
    void init() {
        initVolume();
        initPitch();
        initFxMix();
        initPan();
        initDolby();
    }

    JASSoundParams() {
        init();
    }

    void clampVolume() {
        if (mVolume < 0.0f)
            mVolume = 0.0f;
        else if (mVolume > 1.0f)
            mVolume = 1.0f;
    }

    void clampFxMix(void) {
        if (mFxMix < 0.0f)
            mFxMix = 0.0f;
        else if (mFxMix > 1.0f)
            mFxMix = 1.0f;
    }

    void clampPitch() {
        if (mPitch < 0.0f)
            mPitch = 0.0f;
        else if (mPitch > 8.0f)
            mPitch = 8.0f;
    }

    void clampPan() {
        if (mPan < 0.0f)
            mPan = 0.0f;
        else if (mPan > 1.0f)
            mPan = 1.0f;
    }

    void clampDolby() {
        if (mDolby < 0.0f)
            mDolby = 0.0f;
        else if (mDolby > 1.0f)
            mDolby = 1.0f;
    }

    f32 getVolume() const {
        return mVolume;
    }

    f32 getFxMix() const {
        return mFxMix;
    }

    f32 getPitch() const {
        return mPitch;
    }

    f32 getPan() const {
        return mPan;
    }

    f32 getDolby() const {
        return mDolby;
    }

    f32 mVolume;
    f32 mFxMix;
    f32 mPitch;
    f32 mPan;
    f32 mDolby;
};
