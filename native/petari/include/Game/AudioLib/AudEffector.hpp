#pragma once

#include <JSystem/JAudio2/JASVoice.hpp>

class JKRHeap;

class AudEffector {
public:
    /// @brief Creates a new `AudEffector`.
    AudEffector();

    void initFxLine(JKRHeap*);
    void setFxSend(f32);
    void setCutoff(s32);
    void setSpecialFx(s32);
    void initParams();

    static JASVoice::EffectConfig mFxLine[];

    /* 0x10 */ f32 mFxSend;
    /* 0x14 */ s32 mCutoff;
    /* 0x18 */ s32 mSpecialFx;
};
