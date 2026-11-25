#include "Game/AudioLib/AudEffector.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/System/NebulaArena.hpp"

// Filter 1 (FIR before the sends), a delay of 64 subframes, send A to the left and send B to the right (0.875 on the one,
// 0 on the other), FIR weights 0.125 seven samples back and 16383 / 32768 two back. Each line starts over when set.
const EffectLineSettings AudEffector::cFxLine[4] = {
    {1, 64, NEBULA_AUDIO_BUS_LEFT, NEBULA_AUDIO_BUS_RIGHT, 0.875f, 0.0f, {0.0f, 0.125f, 0.0f, 0.0f, 0.0f, 0.0f, 0x3FFFp-15f, 0.0f}},
    {1, 64, NEBULA_AUDIO_BUS_LEFT, NEBULA_AUDIO_BUS_RIGHT, 0.0f, 0.875f, {0.0f, 0.125f, 0.0f, 0.0f, 0.0f, 0.0f, 0x3FFFp-15f, 0.0f}},
    {1, 64, NEBULA_AUDIO_BUS_LEFT, NEBULA_AUDIO_BUS_RIGHT, 0.875f, 0.0f, {0.0f, 0.125f, 0.0f, 0.0f, 0.0f, 0.0f, 0x3FFFp-15f, 0.0f}},
    {1, 64, NEBULA_AUDIO_BUS_LEFT, NEBULA_AUDIO_BUS_RIGHT, 0.0f, 0.875f, {0.0f, 0.125f, 0.0f, 0.0f, 0.0f, 0.0f, 0x3FFFp-15f, 0.0f}},
};

AudEffector::AudEffector() : mFxSend(), mCutoff(0x7F), mSpecialFx() {
}

void AudEffector::initFxLine() {
    SoundRing& rRing = SoundEngine::get()->ring();
    for (u32 i = 0; i < 4; i++) {
        rRing.next(SOUND_EFFECT_LINE, nullptr).payload< EffectLineCommand >() = {static_cast< u8 >(i), true, &cFxLine[i]};
        rRing.send();
    }
}

void AudEffector::setFxSend(f32 fxSend) {
    if (fxSend < 0.0f) {
        fxSend = 0.0f;
    }
    if (fxSend > 1.0f) {
        fxSend = 1.0f;
    }
    mFxSend = fxSend;
}

void AudEffector::setCutoff(s32 cutoff) {
    if (cutoff < 0 || cutoff > 0x7F) {
        cutoff = 0x7F;
    }
    mCutoff = cutoff;
}

void AudEffector::setSpecialFx(s32 specialFx) {
    mSpecialFx = specialFx;
}

void AudEffector::initParams() {
    mCutoff = 0x7F;
    mFxSend = 0.0f;
    mSpecialFx = 0;
}
