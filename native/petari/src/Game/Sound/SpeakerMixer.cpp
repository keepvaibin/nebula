#include "Game/Sound/SpeakerMixer.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/System/NebulaHost.hpp"
#include <cstring>

namespace {
    // The sounds' common volume (SpkSoundHolder: base 1, level 10 of 15, each controller 1).
    const f32 cHolderVolume = 1.0f * (10 / 15.0f) * 1.0f;
}  // namespace

void SpeakerEnvelope::setRelease(s32 blocks) {
    mReleaseRate = blocks > 0 ? 1.0f / blocks : 1.0f;
    mReleaseVolume = 1.0f;
}

void SpeakerEnvelope::setFadeOut(s32 blocks) {
    mFadeOutRate = blocks > 0 ? 1.0f / blocks : 1.0f;
    mFadeOutVolume = 1.0f;
}

f32 SpeakerEnvelope::calc(bool& rKill) {
    rKill = false;
    if (mReleaseRate != 0.0f) {
        mReleaseVolume -= mReleaseRate;
        if (mReleaseVolume <= 0.0f) {
            mReleaseVolume = 0.0f;
            mReleaseRate = 0.0f;
            rKill = true;
        }
    }

    if (mFadeOutRate != 0.0f) {
        mFadeOutVolume -= mFadeOutRate;
        if (mFadeOutVolume <= 0.0f) {
            mFadeOutVolume = 0.0f;
            mFadeOutRate = 0.0f;
            rKill = true;
        }
    }

    if (mFadeRate != 0.0f) {
        mFadeVolume += mFadeRate;
        if (mFadeRate >= 0.0f) {
            if (mFadeVolume >= mFadeTarget) {
                mFadeVolume = mFadeTarget;
                mFadeRate = 0.0f;
            }
        } else {
            if (mFadeVolume <= mFadeTarget) {
                mFadeVolume = mFadeTarget;
                mFadeRate = 0.0f;
            }
        }
    }

    return mReleaseVolume * mReleaseVolume * mFadeOutVolume * mFadeVolume * mBaseVolume;
}

// The blocks, then the sound lists, then the connections (zeroed memory: nothing connected, no sounds).
u32 SpeakerMixer::bytes(u32 pads) {
    return pads * (sizeof(s16) * cBlockSamples + sizeof(SpeakerVoice*) + sizeof(bool));
}

void SpeakerMixer::setup(void* pMemory, u32 pads) {
    u8* cursor = static_cast< u8* >(pMemory);
    mSamples = reinterpret_cast< s16(*)[cBlockSamples] >(cursor);
    cursor += pads * sizeof(s16) * cBlockSamples;
    mFirst = reinterpret_cast< SpeakerVoice** >(cursor);
    cursor += pads * sizeof(SpeakerVoice*);
    mConnected = reinterpret_cast< bool* >(cursor);
    mPads = pads;
}

void SpeakerMixer::apply(const SoundCommand& rCommand) {
    SpeakerVoice* const voice = static_cast< SpeakerVoice* >(rCommand.mTarget);
    const u32 pad = rCommand.mArg;
    switch (rCommand.mKind) {
    case SOUND_SPEAKER_PAD:
        if (pad < mPads) {
            mConnected[pad] = rCommand.mValue != 0;
        }
        break;
    case SOUND_SPEAKER_START: {
        if (pad >= mPads) {
            break;
        }
        const SpeakerStartCommand& rStart = rCommand.payload< SpeakerStartCommand >();
        voice->mWave = rStart.mWave;
        voice->mWavePtr = 0;
        voice->mWaveSize = rStart.mSize;
        voice->mLoopStart = rStart.mLoopStart;
        voice->mLoopEnd = rStart.mLoopEnd;
        voice->mVolume.mBaseVolume = rStart.mBaseVolume;
        voice->setState(SpeakerVoice::STATE_LOCK);
        voice->mNext = nullptr;
        SpeakerVoice** link = &mFirst[pad];
        while (*link != nullptr) {
            link = &(*link)->mNext;
        }
        *link = voice;
        break;
    }
    case SOUND_SPEAKER_STATE:
        voice->setState(static_cast< s32 >(rCommand.mValue));
        break;
    case SOUND_SPEAKER_STOP:
        voice->setState(SpeakerVoice::STATE_STOP);
        if (rCommand.mArg == 0) {
            voice->mVolume.setFadeOut(static_cast< s32 >(rCommand.mValue));
        } else {
            voice->mVolume.setRelease(static_cast< s32 >(rCommand.mValue));
        }
        break;
    case SOUND_SPEAKER_RELEASE:
        for (u32 i = 0; i < mPads; i++) {
            unlist(voice, i);
        }
        voice->mReleased = true;
        break;
    default:
        break;
    }
}

void SpeakerMixer::job(void* pSelf) {
    SpeakerMixer* const self = static_cast< SpeakerMixer* >(pSelf);
    self->mSamplesDue += 15;
    while (self->mSamplesDue >= cBlockSamples) {
        self->mSamplesDue -= cBlockSamples;
        self->block();
    }
}

// A controller's block goes out when it has a sound, even one not yet heard.
void SpeakerMixer::block() {
    for (u32 pad = 0; pad < mPads; pad++) {
        if (!mConnected[pad]) {
            continue;
        }
        std::memset(mSamples[pad], 0, sizeof(mSamples[pad]));
        if (mFirst[pad] == nullptr) {
            continue;
        }
        for (SpeakerVoice* voice = mFirst[pad]; voice != nullptr; voice = voice->mNext) {
            update(voice, pad, cHolderVolume);
        }
        const NebulaHostServices* const host = nebula_host();
        host->speaker_samples(host->user, pad, mSamples[pad], cBlockSamples);
    }
}

void SpeakerMixer::mix(u32 pad, const s16* pWave, s32 count, f32 volume, s32 offset) {
    for (s32 i = 0; i < count; i++) {
        s32 sample = mSamples[pad][i + offset];
        sample += static_cast< s32 >(pWave[i] * volume);
        if (sample > 0x7fff) {
            sample = 0x7fff;
        } else if (sample < -0x8000) {
            sample = -0x8000;
        }
        mSamples[pad][i + offset] = static_cast< s16 >(sample);
    }
}

// A block of the sound (SpkSound::update): from its place, round its loop, and its end kills it.
void SpeakerMixer::update(SpeakerVoice* pVoice, u32 pad, f32 baseVolume) {
    if (pVoice->mState != SpeakerVoice::STATE_UNLOCK && pVoice->mState != SpeakerVoice::STATE_STOP) {
        return;
    }

    bool kill = false;
    f32 volume = pVoice->mVolume.calc(kill);
    if (kill) {
        pVoice->setState(SpeakerVoice::STATE_DEAD);
    }
    volume *= baseVolume;

    const s32 block = static_cast< s32 >(cBlockSamples);
    if (!(pVoice->mLoopStart >= 0 && pVoice->mLoopEnd > pVoice->mLoopStart)) {
        s32 readSize = block;
        if (pVoice->mWavePtr + readSize >= pVoice->mWaveSize) {
            readSize = pVoice->mWaveSize - pVoice->mWavePtr;
        }
        mix(pad, pVoice->mWave + pVoice->mWavePtr, readSize, volume, 0);
        pVoice->mWavePtr += block;
        if (pVoice->mWavePtr >= pVoice->mWaveSize) {
            pVoice->setState(SpeakerVoice::STATE_DEAD);
        }
        return;
    }

    if (pVoice->mLoopEnd - pVoice->mWavePtr >= block) {
        mix(pad, pVoice->mWave + pVoice->mWavePtr, block, volume, 0);
        pVoice->mWavePtr += block;
        if (pVoice->mWavePtr >= pVoice->mLoopEnd) {
            pVoice->mWavePtr = pVoice->mLoopStart;
        }
        return;
    }

    s32 readOffset = 0;
    s32 readSize = block;
    while (readSize > 0) {
        s32 readAmount = pVoice->mLoopEnd - pVoice->mWavePtr;
        if (readAmount >= readSize) {
            readAmount = readSize;
        }
        readSize -= readAmount;
        mix(pad, pVoice->mWave + pVoice->mWavePtr, readAmount, volume, readOffset);
        readOffset += readAmount;
        pVoice->mWavePtr += readAmount;
        if (pVoice->mWavePtr >= pVoice->mLoopEnd) {
            pVoice->mWavePtr = pVoice->mLoopStart;
        }
    }
}

void SpeakerMixer::unlist(SpeakerVoice* pVoice, u32 pad) {
    for (SpeakerVoice** link = &mFirst[pad]; *link != nullptr; link = &(*link)->mNext) {
        if (*link == pVoice) {
            *link = pVoice->mNext;
            pVoice->mNext = nullptr;
            return;
        }
    }
}
