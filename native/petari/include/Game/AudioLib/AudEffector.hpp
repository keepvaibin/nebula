#pragma once

#include <JSystem/JAudio2/JASVoice.hpp>

class NebulaArena;

class AudEffector {
public:
    /// @brief Creates a new `AudEffector`.
    AudEffector();

    void initFxLine(NebulaArena*);
    void setFxSend(f32);
    void setCutoff(s32);
    void setSpecialFx(s32);
    void initParams();

    static JASVoice::EffectConfig mFxLine[];

    /* 0x10 */ f32 mFxSend;
    /* 0x14 */ s32 mCutoff;
    /* 0x18 */ s32 mSpecialFx;
};
