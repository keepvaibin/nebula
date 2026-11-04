#include "Game/RhythmLib/AudBgmTempoAdjuster.hpp"
#include "Game/AudioLib/AudParams.hpp"
#include <JSystem/JAudio2/JAIStream.hpp>
#include "Game/System/GameThreads.hpp"

AudBgmTempoAdjuster::AudBgmTempoAdjuster()
    : mStream(nullptr), mTempo(120.0f), mBeatMul(1.0f), mIntroBeats(0), mLoopBeats(0), mLoopStartSamples(0), mLoopEndSamples(0),
      mIsNewStream(false) {
}

void AudBgmTempoAdjuster::setInfo(f32 beatMul, u32 introBeats, u32 loopBeats, u32 loopStartSamples, u32 loopEndSamples) {
    mBeatMul = beatMul;
    mIntroBeats = introBeats;
    mLoopBeats = loopBeats;
    mLoopStartSamples = loopStartSamples;
    mLoopEndSamples = loopEndSamples;
}

void AudBgmTempoAdjuster::setStream(JAIStream* pStream) {
    GameAudioExclusion crit;
    mStream = pStream;
    mIsNewStream = pStream != nullptr;
}

void AudBgmTempoAdjuster::reject() {
    GameAudioExclusion crit;
    mStream = nullptr;
    mIsNewStream = false;
}

bool AudBgmTempoAdjuster::takeNewStream() {
    const bool isNew = mIsNewStream;
    mIsNewStream = false;
    return isNew;
}

f64 AudBgmTempoAdjuster::sequenceSamples(void* pSelf) {
    const AudBgmTempoAdjuster* const self = static_cast< const AudBgmTempoAdjuster* >(pSelf);
    if (self->mStream == nullptr) {
        return -1.0;
    }
    const u32 position = self->mStream->inner_.mStream.mPosition;
    const u32 loops = self->mStream->inner_.mStream.mLoopCount;
    const u32 loopSamples = self->mLoopEndSamples - self->mLoopStartSamples;
    if (self->mLoopBeats == 0 || loopSamples == 0) {
        return position;
    }
    const f64 samplesPerBeat = 60.0 * AudParams::streamSampleRate / self->mTempo * self->mBeatMul;
    const f64 intro = self->mIntroBeats * samplesPerBeat;
    const f64 loop = self->mLoopBeats * samplesPerBeat;
    if (loops == 0 && position < self->mLoopStartSamples) {
        return position * intro / self->mLoopStartSamples;
    }
    return intro + loops * loop + (static_cast< f64 >(position) - self->mLoopStartSamples) * loop / loopSamples;
}
