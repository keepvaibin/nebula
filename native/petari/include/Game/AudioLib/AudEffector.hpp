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

    f32 mFxSend;
    s32 mCutoff;
    s32 mSpecialFx;
};
