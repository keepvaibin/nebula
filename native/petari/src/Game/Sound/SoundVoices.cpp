#include "Game/Sound/SoundVoices.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include <cstring>
#include <nebula_geometry.hpp>

namespace {
    const f32 cCurveLinear[17] = {
        1.0f, 0.9375f, 0.875f, 0.8125f, 0.75f, 0.6875f, 0.625f, 0.5625f, 0.5f, 0.4375f, 0.375f, 0.3125f, 0.25f, 0.1875f, 0.125f, 0.0625f, 0.0f,
    };
    const f32 cCurveSampleCell[17] = {1.0f,          0.9704890251159668f,   0.7812740206718445f,  0.5462809801101685f,   0.39979198575019836f,
                                      0.28931498527526855f, 0.21210399270057678f, 0.15747599303722382f, 0.1126129999756813f,  0.08178959786891937f,
                                      0.057985201478004456f, 0.04364150017499924f, 0.03082370012998581f, 0.023712899535894394f, 0.015259300358593464f,
                                      0.00915555004030466f,  0.0f};
    const f32 cCurveSqRoot[17] = {1.0f,      0.8789060115814209f, 0.765625f, 0.6601560115814209f, 0.5625f,   0.4726560115814209f,
                                  0.390625f, 0.3164060115814209f, 0.25f,     0.1914059966802597f, 0.140625f, 0.09765619784593582f,
                                  0.0625f,   0.03515620157122612f, 0.015625f, 0.00390625f,        0.0f};
    const f32 cCurveSquare[17] = {
        1.0f,  0.9682459831237793f, 0.9354140162467957f, 0.9013879895210266f, 0.8660249710083008f,  0.8291559815406799f,
        0.790569007396698f, 0.75f, 0.7071070075035095f, 0.66143798828125f,   0.6123719811439514f,  0.55901700258255f,
        0.5f,  0.433012992143631f,  0.35355299711227417f, 0.25f,              0.0f,
    };
    const f32* const cCurves[4] = {cCurveLinear, cCurveSquare, cCurveSqRoot, cCurveSampleCell};

    // A full note's gain on its way to the mix.
    const f32 cNoteGain = 0.18310547f;
    // Envelope and LFO steps of one tick.
    const f32 cLfoStep = 1.000890625f;
    const f32 cEnvelopeStep = 1.5f;

    void effectOsc(const Oscillator& rOsc, OscEffect* pEffect) {
        f32 value = rOsc.getValue();
        switch (rOsc.mData->mTarget) {
        case Oscillator::TARGET_PITCH:
            pEffect->mPitch *= value;
            break;
        case Oscillator::TARGET_VOLUME:
            pEffect->mVolume *= value;
            break;
        case Oscillator::TARGET_PAN:
            value -= 0.5;
            pEffect->mPan += value;
            break;
        case Oscillator::TARGET_FXMIX:
            pEffect->mFxMix += value;
            break;
        case Oscillator::TARGET_DOLBY:
            pEffect->mDolby += value;
            break;
        case Oscillator::TARGET_VIBRATO:
            pEffect->mVibrato *= value;
            break;
        case Oscillator::TARGET_TREMOLO:
            pEffect->mTremolo *= value;
            break;
        default:
            break;
        }
    }

    f32 sumEffect(f32 sound, f32 effect, f32 channel) {
        return sound + effect + channel;
    }

    f32 sumPan(f32 sound, f32 effect, f32 channel) {
        return 0.5f + (sound - 0.5f) + (effect - 0.5f) + (channel - 0.5f);
    }

    // The volume setting of a sound id: its archive section in the high byte, its sound table category in the next.
    bool categoryOfSound(u32 id, u8* pCategory) {
        const u32 section = (id >> 24) & 0xff;
        const u32 category = (id >> 16) & 0xff;
        if (section == 0x01 || section == 0x02) {
            *pCategory = NEBULA_SOUND_MUSIC;
            return true;
        }
        if (section != 0) {
            return false;
        }
        switch (category) {
        case 0x01:  // player voice
        case 0x03:  // boss voice
        case 0x08:  // enemy voice
        case 0x0a:  // supporter voice
            *pCategory = NEBULA_SOUND_VOICE;
            return true;
        case 0x06:  // atmosphere
            *pCategory = NEBULA_SOUND_AMBIENCE;
            return true;
        case 0x07:  // in-engine cutscenes
            *pCategory = NEBULA_SOUND_CUTSCENE;
            return true;
        case 0x00:
        case 0x02:
        case 0x04:
        case 0x05:
        case 0x09:
        case 0x0b:
        case 0x0c:
        case 0x0d:
            *pCategory = NEBULA_SOUND_EFFECT;
            return true;
        default:
            return false;
        }
    }

    void initVoice(NebulaVoice* pVoice) {
        pVoice->paused = 0;
        pVoice->finished = 0;
        pVoice->forced_stop = 0;
        pVoice->active = 0;
        pVoice->panner = 0;
        pVoice->restart = 0;
        pVoice->category = NEBULA_SOUND_EFFECT;
        pVoice->named = 0;
        for (int i = 0; i < 8; i++) {
            pVoice->fir[i] = 0.0f;
        }
        pVoice->fir[0] = 32767.0f / 32768.0f;
        for (int i = 0; i < 4; i++) {
            pVoice->iir[i] = 0.0f;
        }
        pVoice->iir[0] = 32767.0f / 32768.0f;
    }

    // `skip`: samples to start into the wave (1: from its loop start). A wave stored as PCM16 in the game's data is music unless a sound
    // names the voice.
    void setVoiceWave(NebulaVoice* pVoice, const SoundNote& rNote, u32 skip) {
        const u8 kind = rNote.mFormat & SoundNote::FORMAT_MASK;
        pVoice->wave = rNote.mWave;
        pVoice->format = kind < SoundNote::FORMAT_SAMPLED_NUM ? NEBULA_AUDIO_PCM16 : NEBULA_AUDIO_SILENT;
        pVoice->category = kind == SoundNote::FORMAT_PCM16 ? NEBULA_SOUND_MUSIC : NEBULA_SOUND_EFFECT;
        pVoice->restart = 0;
        if (pVoice->format == NEBULA_AUDIO_SILENT) {
            return;
        }
        pVoice->loop = static_cast< u16 >(rNote.mLoop);
        if (pVoice->loop) {
            if (skip == 1) {
                skip = rNote.mLoopStart;
            }
            pVoice->loop_start = rNote.mLoopStart;
            pVoice->end = rNote.mLoopEnd;
        } else {
            pVoice->end = rNote.mSampleCount;
        }
        if (skip != 0 && pVoice->end > skip) {
            pVoice->restart = skip;
        }
    }

    // The tape's factor (Voices::setRateScale): 1 at real time.
    f32 sRateScale = 1.0f;

    // A constant oscillator's `rate` is its value, not a speed: the tape leaves it.
    void setVoiceRate(NebulaVoice* pVoice, f32 samples) {
        pVoice->rate = pVoice->format == NEBULA_AUDIO_OSCILLATOR && pVoice->oscillator == NEBULA_AUDIO_CONSTANT ? samples / 8000.0f
                                                                                                                 : samples * sRateScale;
    }
}  // namespace

f32 SoundMath::pow2(f32 x) {
    const f32 scaleFrac[] = {0.0f, 0.5f};
    const f32 twoToFrac[] = {1.0f, 0.70710677f};
    const f32 twoToX[] = {0.6931472f, 0.24022661f, 0.055502914f, 0.009625022f, 0.0013131053f, 1.8300806E-4f};
    s32 fracIndex = 0;
    s32 exponent = x >= 0.0f ? static_cast< s32 >(x - 0.5f) : static_cast< s32 >(x + 0.5f);
    const f32 rest = x - exponent;
    if (exponent > 128) {
        return __builtin_inff();
    }
    exponent += 127;
    exponent = static_cast< s32 >(static_cast< u32 >(exponent) << 23);
    f32 scale;
    memcpy(&scale, &exponent, sizeof(scale));
    if (rest < 0.0f) {
        fracIndex = 1;
    }
    const f32 frac = rest + scaleFrac[fracIndex];
    // 2^frac - 1 by its series.
    const f32 fracPowMinusOne = frac * (frac * (frac * (frac * (frac * (frac * twoToX[5] + twoToX[4]) + twoToX[3]) + twoToX[2]) + twoToX[1]) + twoToX[0]);
    return scale * (0.75f * twoToFrac[fracIndex] + ((0.25f + fracPowMinusOne) * twoToFrac[fracIndex]));
}

void Oscillator::initStart(const OscData* pData) {
    mData = pData;
    mTime = 0.0f;
    mValue = 0.0f;
    mFrom = 0.0f;
    mPoint = 0;
    mDirectRelease = 0;
    if (pData == nullptr) {
        mState = STOP;
        return;
    }
    if (mData->mTable == nullptr) {
        mState = HOLD;
        mValue = 1.0f;
        return;
    }
    mTo = mData->mTable[0].mValue / 32768.0f;
    mCurve = mData->mTable[0].mMode;
    mState = RUN;
}

void Oscillator::incCounter(f32 step) {
    if (mState == STOP || mState == HOLD) {
        return;
    }
    mTime += step * mData->mRate;
    update();
}

void Oscillator::release() {
    if (mData->mRelease == nullptr && mDirectRelease == 0) {
        mState = STOP;
        return;
    }
    if (mDirectRelease != 0) {
        mTime = 0.0f;
        mFrom = mValue;
        mTo = 0.0f;
        mPoint = 0;
        mCurve = (mDirectRelease >> 14) & 3;
        mState = DIRECT_RELEASE;
        update();
        return;
    }
    if (mData->mTable != mData->mRelease) {
        mTime = 0.0f;
        mFrom = mValue;
        mTo = mData->mRelease[0].mValue / 32768.0f;
        mPoint = 0;
        mCurve = mData->mRelease[0].mMode;
    }
    mState = RELEASE;
    update();
}

void Oscillator::update() {
    if (mState == DIRECT_RELEASE) {
        const s32 length = mDirectRelease & 0x3FFF;
        if (mTime < length) {
            updateCurrentValue(length);
        } else {
            mValue = mTo;
            mState = STOP;
        }
        return;
    }
    if (mState == HOLD) {
        return;
    }
    const SeqPoint* points = mState == RUN ? mData->mTable : mData->mRelease;
    if (points == nullptr) {
        mState = HOLD;
        return;
    }
    while (mTime >= points[mPoint].mTime) {
        mTime -= points[mPoint].mTime;
        mValue = mTo;
        mPoint++;
        mFrom = mValue;
        const SeqPoint& rNext = points[mPoint];
        switch (rNext.mMode) {
        case 0xf:
            mState = STOP;
            return;
        case 0xe:
            mState = HOLD;
            return;
        case 0xd:
            mPoint = rNext.mValue;
            break;
        default:
            mCurve = rNext.mMode;
            mTo = rNext.mValue / 32768.0f;
            break;
        }
    }
    updateCurrentValue(points[mPoint].mTime);
}

void Oscillator::updateCurrentValue(f32 length) {
    const f32* table = cCurves[mCurve];
    const f32 position = 16.0f * (mTime / length);
    const u32 index = static_cast< u32 >(position);
    const f32 fraction = position - index;
    const f32 curve = (1.0f - fraction) * table[index] + fraction * table[index + 1];
    mValue = mFrom * curve + mTo * (1.0f - curve);
}

f32 Lfo::getValue() const {
    if (mWait != 0) {
        return 0.0f;
    }
    if (mDepth == 0.0f) {
        return 0.0f;
    }
    return mDepth * Angle::sinLap(mPhase);
}

void Lfo::incCounter(f32 step) {
    if (mWait != 0) {
        mWait--;
        return;
    }
    mPhase += step * ((1.0f / 72.0f) * mPitch);
    if (mPhase >= 1.0f) {
        mPhase -= 1.0f;
    }
}

void Voices::setup(SoundEngine* pEngine, NebulaVoice* pVoices, u32 count, Slot* pSlots, SoundNote* pNotes) {
    mEngine = pEngine;
    mVoices = pVoices;
    mCount = count;
    mSlots = pSlots;
    mNotes = pNotes;
    mUsed = 0;
}

s32 Voices::lowestSlot(int priority) {
    s16 bestPriority = 0xff;
    s32 best = -1;
    u32 bestAge = 0;
    for (u32 i = 0; i < mUsed; i++) {
        const s16 slotPriority = mSlots[i].mPriority;
        if (slotPriority < 0) {
            return static_cast< s32 >(i);
        }
        if (slotPriority <= priority && slotPriority <= bestPriority) {
            if (slotPriority != bestPriority || mSlots[i].mAge > bestAge) {
                bestAge = mSlots[i].mAge;
                best = static_cast< s32 >(i);
                bestPriority = slotPriority;
            }
        }
    }
    if (mUsed < mCount) {
        return static_cast< s32 >(mUsed++);
    }
    return best;
}

// The slot's note gone: whoever owns it hears it stop.
void Voices::endNote(s32 slot) {
    Slot& rSlot = mSlots[slot];
    rSlot.mHasNote = false;
    SoundNote& rNote = mNotes[slot];
    rNote.mSlot = -1;
    if (rNote.mOwner != OWNER_NONE) {
        mEngine->noteEvent(&rNote, NOTE_STOP, nullptr);
    }
    rNote.mOwner = OWNER_NONE;
    rNote.mOwnerData = nullptr;
    if (rNote.mHolder != nullptr) {
        mEngine->releaseHolder(rNote.mHolder);
        rNote.mHolder = nullptr;
    }
}

void Voices::drop(s32 slot) {
    Slot& rSlot = mSlots[slot];
    if (rSlot.mHasNote) {
        endNote(slot);
    }
    rSlot.mPriority = -1;
    rSlot.mFlags &= ~1u;
    if (rSlot.mStatus == SLOT_ACTIVE) {
        rSlot.mFlags |= 2;
        rSlot.mStatus = SLOT_DROP;
    }
}

SoundNote* Voices::play(const SoundNote& rPrepared, bool force) {
    const u8 priority = rPrepared.mPriority & 0xff;
    const s32 slot = lowestSlot(priority);
    if (slot < 0) {
        // The note never played: its owner hears it stop.
        if (rPrepared.mOwner != OWNER_NONE) {
            SoundNote dead = rPrepared;
            mEngine->noteEvent(&dead, NOTE_STOP, nullptr);
        }
        return nullptr;
    }
    Slot& rSlot = mSlots[slot];
    if (force) {
        rSlot.mStatus = SLOT_INACTIVE;
    }
    drop(slot);
    rSlot.mPriority = priority;
    rSlot.mAge = 0;
    rSlot.mHasNote = true;
    SoundNote& rNote = mNotes[slot];
    rNote = rPrepared;
    rNote.mSlot = slot;
    rSlot.mFlags |= 1;
    rNote.mStatus = SoundNote::STATUS_PLAY;
    return &rNote;
}

void Voices::releaseNote(SoundNote* pNote, u16 directRelease) {
    if (pNote->mStatus != SoundNote::STATUS_PLAY) {
        return;
    }
    if (directRelease != 0) {
        pNote->mOsc[0].mDirectRelease = directRelease;
    }
    for (u32 i = 0; i < 2; i++) {
        if (pNote->mOsc[i].isValid()) {
            pNote->mOsc[i].release();
        }
    }
    mSlots[pNote->mSlot].mPriority = pNote->mPriority >> 8;
    pNote->mStatus = SoundNote::STATUS_RELEASE;
}

s32 Voices::noteCallback(s32 slot, u32 type) {
    switch (type) {
    case SLOT_PLAY:
        return update(slot);
    case SLOT_START:
        return initialUpdate(slot);
    default:
        mSlots[slot].mHasNote = false;
        endNote(slot);
        return -1;
    }
}

void Voices::updateSlot(s32 slot) {
    Slot& rSlot = mSlots[slot];
    NebulaVoice* voice = &mVoices[slot];
    // A note's callback below 0 ends it; a slot without a note always ends. An ended slot gives up its priority.
    auto stopNote = [&]() {
        if ((rSlot.mHasNote ? noteCallback(slot, SLOT_STOP) : -1) < 0) {
            rSlot.mPriority = -1;
        }
    };
    if (voice->finished != 0) {
        rSlot.mFlags &= ~2u;
        if (rSlot.mStatus == SLOT_ACTIVE) {
            stopNote();
        }
        rSlot.mStatus = SLOT_INACTIVE;
        voice->finished = 0;
        voice->active = 0;
    } else if (rSlot.mFlags & 2) {
        rSlot.mFlags &= ~2u;
        voice->forced_stop = 1;
    } else if (rSlot.mStatus != SLOT_DROP) {
        if ((rSlot.mFlags & 1) && rSlot.mStatus == SLOT_INACTIVE) {
            rSlot.mFlags &= ~1u;
            rSlot.mStatus = SLOT_ACTIVE;
            initVoice(voice);
            if ((rSlot.mHasNote ? noteCallback(slot, SLOT_START) : 0) < 0) {
                rSlot.mStatus = SLOT_INACTIVE;
                stopNote();
            } else {
                voice->starting = 1;
                voice->active = 1;
                nameSound(slot);
            }
        } else if (rSlot.mStatus != SLOT_INACTIVE) {
            if ((rSlot.mHasNote ? noteCallback(slot, SLOT_PLAY) : 0) < 0) {
                rSlot.mStatus = SLOT_INACTIVE;
                stopNote();
                voice->active = 0;
            } else {
                rSlot.mAge++;
                if (voice->named == 0) {
                    nameSound(slot);
                }
            }
        }
    }
}

void Voices::dropActive() {
    for (u32 i = 0; i < mUsed; i++) {
        if (mSlots[i].mStatus == SLOT_ACTIVE) {
            drop(static_cast< s32 >(i));
        }
    }
}

void Voices::forgetOwners(const void* pBegin, const void* pEnd) {
    const u8* const begin = static_cast< const u8* >(pBegin);
    const u8* const end = static_cast< const u8* >(pEnd);
    for (u32 i = 0; i < mUsed; i++) {
        SoundNote& rNote = mNotes[i];
        const u8* const owner = static_cast< const u8* >(rNote.mOwnerData);
        if (mSlots[i].mHasNote && rNote.mOwner != OWNER_NONE && owner >= begin && owner < end) {
            rNote.free();
        }
    }
}

void Voices::updateAll() {
    updateSlots();
    mix();
}

void Voices::updateSlots() {
    for (u32 i = 0; i < mUsed; i++) {
        updateSlot(static_cast< s32 >(i));
    }
}

void Voices::mix() {
    mEngine->mixVoices(mUsed);
}

void Voices::setRateScale(f32 scale) {
    sRateScale = scale;
}

void Voices::nameSound(s32 slot) {
    if (mSlots[slot].mStatus != SLOT_ACTIVE) {
        return;
    }
    NebulaVoice* voice = &mVoices[slot];
    u32 id = 0;
    const u8 route = mSlots[slot].mHasNote ? mEngine->noteSound(mNotes[slot], &id) : 0;
    voice->named = route != 0;
    if (route != 0) {
        categoryOfSound(id, &voice->category);
    }
}

s32 Voices::initialUpdate(s32 slot) {
    SoundNote& rNote = mNotes[slot];
    NebulaVoice* voice = &mVoices[slot];
    if (rNote.isDolbyMode()) {
        if (voice->panner) {
            voice->panner_volume = voice->panner_target;
        } else {
            voice->panner_volume = 0;
            voice->panner = 1;
        }
    }
    if (rNote.mOwner != OWNER_NONE) {
        mEngine->noteEvent(&rNote, NOTE_START, voice);
    }
    if (rNote.mWaveLive != nullptr && *rNote.mWaveLive == 0) {
        mSlots[slot].mHasNote = false;
        endNote(slot);
        return -1;
    }
    switch (rNote.mKind) {
    case SoundNote::KIND_WAVE:
        setVoiceWave(voice, rNote, rNote.mSkipSamples);
        voice->wave_end = rNote.mWaveEnd;
        break;
    case SoundNote::KIND_OSC:
        voice->wave = nullptr;
        voice->format = NEBULA_AUDIO_OSCILLATOR;
        voice->category = NEBULA_SOUND_EFFECT;
        voice->oscillator = static_cast< uint16_t >(rNote.mOscillatorWave);
        break;
    }
    const u32 outputMode = mEngine->outputMode();
    for (u8 i = 0; i < 6; i++) {
        u8 bus = rNote.mMixConfig[i] >> 8;
        if (outputMode == 0) {
            switch (bus) {
            case 8:
                bus = 11;
                break;
            case 9:
                bus = 2;
                break;
            }
        } else if (outputMode == 1 && bus == 8) {
            bus = 11;
        }
        voice->sends[i].bus = bus < NEBULA_AUDIO_BUSES ? bus : static_cast< u32 >(NEBULA_AUDIO_BUS_NONE);
    }
    OscEffect effect;
    for (u32 i = 0; i < 2; i++) {
        if (rNote.mOsc[i].isValid()) {
            rNote.mOsc[i].update();
            effectOsc(rNote.mOsc[i], &effect);
        }
    }
    rNote.mVibrato.resetCounter();
    rNote.mTremolo.resetCounter();
    f32 volumes[6] = {};
    effectorParams(rNote, voice, volumes, effect);
    for (u8 i = 0; i < 6; i++) {
        voice->sends[i].current = volumes[i];
        voice->sends[i].target = volumes[i];
    }
    f32 pitch = SoundMath::pow2(rNote.mParams.mBend + (rNote.mKey + rNote.mKeySweep) / 12.0f + effect.mVibrato * rNote.mVibrato.getValue());
    pitch = rNote.mSound.mPitch * effect.mPitch * pitch * rNote.mParams.mPitch;
    if (pitch < 0.0f) {
        pitch = 0.0f;
    }
    setVoiceRate(voice, pitch * rNote.mSourceRate);
    voice->paused = rNote.mPause;
    return 0;
}

s32 Voices::update(s32 slot) {
    SoundNote& rNote = mNotes[slot];
    NebulaVoice* voice = &mVoices[slot];
    if (rNote.mOwner != OWNER_NONE) {
        mEngine->noteEvent(&rNote, NOTE_PLAY, voice);
    }
    if (rNote.mWaveLive != nullptr && *rNote.mWaveLive == 0) {
        mSlots[slot].mHasNote = false;
        endNote(slot);
        return -1;
    }
    voice->paused = rNote.mPause;
    OscEffect effect;
    if (rNote.mPause) {
        if (rNote.mOsc[0].isRelease()) {
            mSlots[slot].mHasNote = false;
            endNote(slot);
            return -1;
        }
    } else {
        rNote.mVibrato.incCounter(cLfoStep);
        rNote.mTremolo.incCounter(cLfoStep);
        if (rNote.mUpdateTimer != 0) {
            rNote.mUpdateTimer--;
            if (rNote.mUpdateTimer == 0 && rNote.mOwner != OWNER_NONE) {
                mEngine->noteEvent(&rNote, NOTE_TIMER, voice);
            }
        }
        for (u32 i = 0; i < 2; i++) {
            if (rNote.mOsc[i].isValid()) {
                rNote.mOsc[i].incCounter(cEnvelopeStep);
                effectOsc(rNote.mOsc[i], &effect);
                if (i == 0 && rNote.mOsc[i].isStop()) {
                    mSlots[slot].mHasNote = false;
                    endNote(slot);
                    return -1;
                }
            }
        }
    }
    f32 volumes[6] = {};
    effectorParams(rNote, voice, volumes, effect);
    for (u8 i = 0; i < 6; i++) {
        if (voice->forced_stop == 0) {
            voice->sends[i].target = volumes[i];
        }
    }
    f32 pitch = SoundMath::pow2(rNote.mParams.mBend + (rNote.mKey + rNote.mKeySweep) / 12.0f + effect.mVibrato * rNote.mVibrato.getValue());
    pitch = rNote.mSound.mPitch * effect.mPitch * pitch * rNote.mParams.mPitch;
    if (pitch < 0.0f) {
        pitch = 0.0f;
    }
    setVoiceRate(voice, pitch * rNote.mSourceRate);
    if (!rNote.mPause && rNote.mKeySweepCount != 0) {
        rNote.mKeySweep += (rNote.mKeySweepTarget - rNote.mKeySweep) / rNote.mKeySweepCount;
        rNote.mKeySweepCount--;
    }
    return 0;
}

void Voices::effectorParams(SoundNote& rNote, NebulaVoice* pVoice, f32* pVolumes, const OscEffect& rEffect) {
    f32 pan = 0.5f;
    f32 dolby = 0.0f;
    switch (mEngine->outputMode()) {
    case 0:
        break;
    case 1:
        pan = sumPan(rNote.mSound.mPan, rEffect.mPan, rNote.mParams.mPan);
        break;
    case 2:
        pan = sumPan(rNote.mSound.mPan, rEffect.mPan, rNote.mParams.mPan);
        dolby = sumEffect(rNote.mSound.mDolby, rEffect.mDolby, rNote.mParams.mDolby);
        break;
    }
    f32 fxmix = sumEffect(rNote.mSound.mFxMix, rEffect.mFxMix, rNote.mParams.mFxMix);
    f32 volume = rNote.mVelocity / 127.0f;
    volume *= volume;
    volume *= rNote.mSound.mVolume * rEffect.mVolume * rNote.mParams.mVolume * (rEffect.mTremolo * rNote.mTremolo.getValue() + 1.0f);
    if (volume < 0.0f) {
        volume = 0.0f;
    }
    pan = SoundMath::clamp01(pan);
    fxmix = SoundMath::clamp01(fxmix);
    dolby = SoundMath::clamp01(dolby);
    if (rNote.isDolbyMode()) {
        if (mEngine->outputMode() == 0) {
            volume *= 0.707f;
        }
        volume = SoundMath::clamp01(volume);
        pVoice->pan = pan;
        pVoice->rear = dolby;
        pVoice->panner_send = fxmix;
        pVoice->panner_target = volume * cNoteGain;
        pVoice->panner = 1;
    } else {
        mixerVolumes(rNote, volume, pan, fxmix, dolby, pVolumes);
    }
}

void Voices::mixerVolumes(const SoundNote& rNote, f32 volumeIn, f32 pan, f32 fxmix, f32 dolby, f32* pVolumes) {
    const bool flat = mEngine->outputMode() == 0;
    for (u32 i = 0; i < 6; i++) {
        f32 volume = volumeIn;
        const u16 config = rNote.mMixConfig[i];
        const u8 bus = config >> 8;
        const u8 mode0 = (config >> 4) & 0xf;
        const u8 mode1 = config & 0xf;
        if (bus == 0) {
            pVolumes[i] = 0.0f;
            continue;
        }
        f32 scale = 0.0f;
        if (mode0 != 0) {
            switch (mode0) {
            case 1:
                scale = pan;
                break;
            case 2:
                scale = fxmix;
                break;
            case 3:
                scale = dolby;
                break;
            case 5:
                scale = 1.0f - pan;
                break;
            case 6:
                scale = 1.0f - fxmix;
                break;
            case 7:
                scale = 1.0f - dolby;
                break;
            }
            switch (mode0) {
            case 2:
            case 6:
                volume *= scale;
                break;
            default:
                volume *= flat ? scale : Angle::sinRadian(scale * FloatUtil::PI() * 0.5f);
                break;
            }
        }
        if (mode1 != 0) {
            switch (mode1) {
            case 1:
                scale = pan;
                break;
            case 2:
                scale = fxmix;
                break;
            case 3:
                scale = dolby;
                break;
            case 5:
                scale = 1.0f - pan;
                break;
            case 6:
                scale = 1.0f - fxmix;
                break;
            case 7:
                scale = 1.0f - dolby;
                break;
            }
            switch (mode1) {
            case 3:
            case 7:
                volume *= Angle::sinRadian((scale * 0.34776f + 0.32612f) * FloatUtil::PI() * 0.5f);
                break;
            case 2:
            case 6:
                volume *= scale;
                break;
            default:
                volume *= flat ? scale : Angle::sinRadian(scale * FloatUtil::PI() * 0.5f);
                break;
            }
        }
        volume = SoundMath::clamp01(volume);
        pVolumes[i] = volume * cNoteGain;
    }
}
