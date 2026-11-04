#include "Game/RhythmLib/AudBgmTempoAdjuster.hpp"
#include "Game/AudioLib/AudParams.hpp"
#include <JSystem/JAudio2/JAIStream.hpp>
#include "Game/System/GameThreads.hpp"

AudBgmTempoAdjuster::AudBgmTempoAdjuster()
    : mStream(), mBeatMul(1.0f), mIntroBeats(), mLoopBeats(), mLoopStartSamples(), mLoopEndSamples(), mLoopSamples() {
    init();
}

void AudBgmTempoAdjuster::init() {
    mLoopCount = 0;
    mCurrentBeat = 0;
    mIsBeatNeedSync = false;
    mIsStreamNeedSync = false;
}

void AudBgmTempoAdjuster::setInfo(f32 beatMul, u32 introBeats, u32 loopBeats, u32 loopStartSamples, u32 loopEndSamples) {
    mBeatMul = beatMul;
    mIntroBeats = introBeats;
    mLoopBeats = loopBeats;
    mLoopStartSamples = loopStartSamples;
    mLoopEndSamples = loopEndSamples;
    mLoopSamples = loopEndSamples - loopStartSamples;
}

void AudBgmTempoAdjuster::setStream(JAIStream* pStream) {
    GameAudioExclusion crit;
    init();
    mStream = pStream;
    mLastPosition = pStream != nullptr ? static_cast< u32 >(pStream->inner_.mStream.mPosition) : 0;
}

void AudBgmTempoAdjuster::reject() {
    GameAudioExclusion crit;
    mStream = nullptr;
    init();
}

f32 AudBgmTempoAdjuster::beatProc(f32 tempo) {
    if (mStream == nullptr) {
        return 1.0f;
    }

    f32 currSample = mStream->inner_.mStream.mPosition;
    u32 loopCount = mStream->inner_.mStream.mLoopCount;

    bool newStreamLoop = false;
    if (loopCount != mLoopCount) {
        newStreamLoop = true;
        mLoopCount = loopCount;
    }

    bool newBeatLoop = false;
    if (mLoopBeats != 0 && mCurrentBeat >= mIntroBeats + mLoopBeats) {
        mCurrentBeat = mIntroBeats;
        newBeatLoop = true;
    }

    // (samples/beat) * mBeatMul * mCurrentBeat
    f32 beatSampleStart = (60.0f * AudParams::streamSampleRate / tempo) * mBeatMul * mCurrentBeat;

    if (mLoopSamples != 0.0f) {
        if (newBeatLoop && !newStreamLoop && !mIsStreamNeedSync) {
            currSample -= mLoopSamples;
        } else if (!newBeatLoop && newStreamLoop && !mIsBeatNeedSync) {
            currSample += mLoopSamples;
        }
    }

    f32 samples = currSample - beatSampleStart;  // how many samples out-of-sync
    if (newBeatLoop) {
        mIsBeatNeedSync = true;
    }
    if (newStreamLoop) {
        mIsStreamNeedSync = true;
    }

    if (mIsBeatNeedSync && mIsStreamNeedSync) {
        mIsBeatNeedSync = false;
        mIsStreamNeedSync = false;
    }

    mCurrentBeat++;

    return samples;
}

f32 AudBgmTempoAdjuster::samplesAdvanced(void* pSelf) {
    AudBgmTempoAdjuster* const self = static_cast< AudBgmTempoAdjuster* >(pSelf);
    if (self->mStream == nullptr) {
        return -1.0f;
    }
    const u32 position = self->mStream->inner_.mStream.mPosition;
    const u32 last = self->mLastPosition;
    self->mLastPosition = position;
    if (position >= last) {
        return static_cast< f32 >(position - last);
    }
    // Back at its loop's start, or a new stream from its first sample.
    const f32 looped = static_cast< f32 >(position) + self->mLoopSamples - static_cast< f32 >(last);
    return looped >= 0.0f ? looped : static_cast< f32 >(position);
}
