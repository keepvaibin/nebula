#include "Game/AudioLib/AudEffector.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/System/NebulaArena.hpp"

// Line, restart, has config; filter (FIR before the sends), send A's bus and volume, send B's, delay in subframes, FIR.
const EffectLineCommand AudEffector::cFxLine[4] = {
    {0, true, true, 1, 0, 0x7000, 1, 0x0000, 64, {0, 0x1000, 0, 0, 0, 0, 0x3FFF, 0}},
    {1, true, true, 1, 0, 0x0000, 1, 0x7000, 64, {0, 0x1000, 0, 0, 0, 0, 0x3FFF, 0}},
    {2, true, true, 1, 0, 0x7000, 1, 0x0000, 64, {0, 0x1000, 0, 0, 0, 0, 0x3FFF, 0}},
    {3, true, true, 1, 0, 0x0000, 1, 0x7000, 64, {0, 0x1000, 0, 0, 0, 0, 0x3FFF, 0}},
};

AudEffector::AudEffector() : mFxSend(), mCutoff(0x7F), mSpecialFx() {
}

void AudEffector::initFxLine() {
    SoundRing& rRing = SoundEngine::get()->ring();
    for (u32 i = 0; i < 4; i++) {
        rRing.next(SOUND_EFFECT_LINE, nullptr).payload< EffectLineCommand >() = cFxLine[i];
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
