#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

struct JASOscillator {
    struct Point {
        s16 _0;
        s16 _2;
        s16 _4;
    };

    struct EffectParams {
        EffectParams() {
            mVolume = 1.0f;
            mPitch = 1.0f;
            mPan = 0.5f;
            mFxMix = 0.0f;
            mDolby = 0.0f;
            _14 = 1.0f;
            _18 = 1.0f;
        }

        f32 mVolume;
        f32 mPitch;
        f32 mPan;
        f32 mFxMix;
        f32 mDolby;
        f32 _14;
        f32 _18;
    };

    struct Data {
        u32 mTarget;
        f32 _04;
        const Point* mTable;
        const Point* rel_table;
        f32 mScale;
        f32 _14;
    };

    enum Target {
        /* 0 */ TARGET_VOLUME,
        /* 1 */ TARGET_PITCH,
        /* 2 */ TARGET_PAN,
        /* 3 */ TARGET_FXMIX,
        /* 4 */ TARGET_DOLBY,
        /* 5 */ TARGET_5,
        /* 6 */ TARGET_6,
    };

    JASOscillator();

    void initStart(const Data*);
    void incCounter(f32 param_0);
    f32 getValue() const;
    void release();
    void update();
    void updateCurrentValue(f32 param_0);

    void setDirectRelease(u16 param_0) {
        mDirectRelease = param_0;
    }
    void stop() {
        _1C = 0;
    }
    bool isValid() const {
        return mData != NULL;
    }
    bool isStop() const {
        return _1C == 0;
    }
    bool isRelease() const {
        return _1C == 3 || _1C == 4;
    }
    u32 getTarget() const {
        return mData->mTarget;
    }

    const Data* mData;
    f32 _04;
    f32 _08;
    f32 _0C;
    f32 _10;
    u16 _14;
    u16 mDirectRelease;
    u8 _18;
    u16 _1A;
    int _1C;

    static const f32 sCurveTableLinear[17];
    static const f32 sCurveTableSampleCell[17];
    static const f32 sCurveTableSqRoot[17];
    static const f32 sCurveTableSquare[17];
};
