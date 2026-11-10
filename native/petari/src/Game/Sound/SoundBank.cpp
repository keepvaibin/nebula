#include "Game/Sound/SoundBank.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_effects_f1, "effects.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_effects_f2, "effects.f2", 0.9999999f);
NEBULA_TUNABLE_NAMED(f64, cNum_effects_f3, "effects.f3", 0.5);
NEBULA_TUNABLE_NAMED(f32, cNum_prepareNote_f1, "prepareNote.f1", 523.0005f);

namespace {
    // The envelope of a drum and of a program with none: full at once, cut at release.
    const OscData cHoldOsc = {Oscillator::TARGET_VOLUME, 1.0f, nullptr, nullptr, 1.0f, 0.0f};

    // An oscillator note's envelope: held, released over ten ticks.
    const SeqPoint cOscRelease[2] = {{1, 10, 0}, {0xf, 0, 0}};
    const OscData cOscEnvelope = {Oscillator::TARGET_VOLUME, 1.0f, nullptr, cOscRelease, 1.0f, 0.0f};

    template < typename T >
    const T* after(const void* pBase, u32 bytes) {
        return reinterpret_cast< const T* >(static_cast< const u8* >(pBase) + bytes);
    }
}  // namespace

bool WaveBank::setup(const WaveBankFile* pFile, u32 size, s32* pCurrent, u32* pLive, const u8** pData, u32* pDataBytes) {
    if (size < sizeof(WaveBankFile) || pFile->mMagic != WaveBankFile::cMagic || pFile->mVersion != WaveBankFile::cVersion) {
        return false;
    }
    const u32 bytes = sizeof(WaveBankFile) + pFile->mGroupCount * sizeof(WaveGroup) + pFile->mWaveCount * sizeof(WaveEntry) + pFile->mNameBytes;
    if (bytes > size) {
        return false;
    }
    mFile = pFile;
    mGroups = after< WaveGroup >(pFile, sizeof(WaveBankFile));
    mWaves = after< WaveEntry >(mGroups, pFile->mGroupCount * sizeof(WaveGroup));
    mNames = after< char >(mWaves, pFile->mWaveCount * sizeof(WaveEntry));
    mCurrent = pCurrent;
    mLive = pLive;
    mData = pData;
    mDataBytes = pDataBytes;
    for (u32 i = 0; i < pFile->mIdCount; i++) {
        mCurrent[i] = -1;
    }
    for (u32 i = 0; i < pFile->mGroupCount; i++) {
        mLive[i] = 0;
        mData[i] = nullptr;
        mDataBytes[i] = 0;
    }
    return true;
}

void WaveBank::activate(u32 group, const u8* pData, u32 bytes) {
    if (group >= mFile->mGroupCount) {
        return;
    }
    mData[group] = pData;
    mDataBytes[group] = bytes;
    mLive[group] = 1;
    const WaveGroup& rGroup = mGroups[group];
    for (u32 i = 0; i < rGroup.mCount; i++) {
        const u32 wave = rGroup.mFirst + i;
        mCurrent[mWaves[wave].mId] = static_cast< s32 >(wave);
    }
}

void WaveBank::deactivate(u32 group) {
    if (group >= mFile->mGroupCount || mLive[group] == 0) {
        return;
    }
    mLive[group] = 0;
    const WaveGroup& rGroup = mGroups[group];
    for (u32 i = 0; i < rGroup.mCount; i++) {
        const u32 wave = rGroup.mFirst + i;
        const u32 id = mWaves[wave].mId;
        if (mCurrent[id] != static_cast< s32 >(wave)) {
            continue;
        }
        // The id falls back to another live group that holds it.
        mCurrent[id] = -1;
        for (u32 g = mFile->mGroupCount; g-- > 0;) {
            if (mLive[g] == 0) {
                continue;
            }
            for (u32 w = mGroups[g].mFirst; w < mGroups[g].mFirst + mGroups[g].mCount; w++) {
                if (mWaves[w].mId == id) {
                    mCurrent[id] = static_cast< s32 >(w);
                    break;
                }
            }
            if (mCurrent[id] >= 0) {
                break;
            }
        }
    }
}

const WaveEntry* WaveBank::find(u32 id, const u32** ppLive, const u8** ppWave, const u8** ppEnd) const {
    if (id >= mFile->mIdCount || mCurrent[id] < 0) {
        return nullptr;
    }
    const u32 wave = static_cast< u32 >(mCurrent[id]);
    u32 group = 0;
    while (group + 1 < mFile->mGroupCount && wave >= mGroups[group].mFirst + mGroups[group].mCount) {
        group++;
    }
    const u8* data = mData[group];
    if (data == nullptr) {
        return nullptr;
    }
    *ppLive = &mLive[group];
    *ppWave = data + mWaves[wave].mOffset;
    *ppEnd = data + mDataBytes[group];
    return &mWaves[wave];
}

bool SoundBank::setup(const BankFile* pFile, u32 size, OscData* pOscs) {
    if (size < sizeof(BankFile) || pFile->mMagic != BankFile::cMagic || pFile->mVersion != BankFile::cVersion) {
        return false;
    }
    const u32 bytes = sizeof(BankFile) + pFile->mProgramCount * sizeof(BankProgram) + pFile->mKeyCount * sizeof(BankKey) +
                      pFile->mVeloCount * sizeof(BankVelo) + pFile->mPercCount * sizeof(BankPerc) + pFile->mEffectCount * sizeof(BankEffect) +
                      pFile->mOscCount * sizeof(BankOsc) + pFile->mPointBytes;
    if (bytes > size) {
        return false;
    }
    mFile = pFile;
    mPrograms = after< BankProgram >(pFile, sizeof(BankFile));
    mKeys = after< BankKey >(mPrograms, pFile->mProgramCount * sizeof(BankProgram));
    mVelos = after< BankVelo >(mKeys, pFile->mKeyCount * sizeof(BankKey));
    mPercs = after< BankPerc >(mVelos, pFile->mVeloCount * sizeof(BankVelo));
    mEffects = after< BankEffect >(mPercs, pFile->mPercCount * sizeof(BankPerc));
    const BankOsc* oscs = after< BankOsc >(mEffects, pFile->mEffectCount * sizeof(BankEffect));
    const u8* points = after< u8 >(oscs, pFile->mOscCount * sizeof(BankOsc));
    for (u32 i = 0; i < pFile->mOscCount; i++) {
        const BankOsc& rOsc = oscs[i];
        pOscs[i].mTarget = rOsc.mTarget;
        pOscs[i].mRate = rOsc.mRate;
        pOscs[i].mTable = rOsc.mTable >= 0 ? reinterpret_cast< const SeqPoint* >(points + rOsc.mTable) : nullptr;
        pOscs[i].mRelease = rOsc.mRelease >= 0 ? reinterpret_cast< const SeqPoint* >(points + rOsc.mRelease) : nullptr;
        pOscs[i].mScale = rOsc.mScale;
        pOscs[i].mOffset = rOsc.mOffset;
    }
    mOscs = pOscs;
    return true;
}

void SoundBank::effects(u32 first, u32 count, s32 key, s32 velocity, FastRandom* pRandom, InstParams* pOut) const {
    for (u32 i = 0; i < count; i++) {
        const BankEffect& rEffect = mEffects[first + i];
        f32 value;
        if (rEffect.mKind == BankEffect::RANDOM) {
            value = cNum_effects_f1 * pRandom->getRandF() - cNum_effects_f2;
            value *= rEffect.mRange;
            value += rEffect.mBase;
        } else {
            const f32 one = 1.0f;
            int at = 0;
            switch (rEffect.mSenseType) {
            case 1:
                at = velocity;
                break;
            case 2:
                at = key;
                break;
            }
            const int maxPoint = static_cast< int >(rEffect.mMaxPoint);
            const f32 start = rEffect.mBase;
            const f32 end = rEffect.mRange;
            if (maxPoint == 0x7f || maxPoint == 0) {
                value = start + (at * (end - start)) / 0x7f;
            } else if (at < maxPoint) {
                value = start + (one - start) * (static_cast< f32 >(at) / maxPoint);
            } else {
                value = (end - one) * (static_cast< f32 >(at - maxPoint) / (0x7f - maxPoint)) + one;
            }
        }
        switch (rEffect.mTarget) {
        case 0:
            pOut->mSound.mVolume *= value;
            break;
        case 1:
            pOut->mSound.mPitch *= value;
            break;
        case 2:
            pOut->mSound.mPan += value - cNum_effects_f3;
            break;
        case 3:
            pOut->mSound.mFxMix += value;
            break;
        case 4:
            pOut->mSound.mDolby += value;
            break;
        }
    }
}

bool SoundBank::velocity(u32 first, u32 count, s32 velocity, InstParams* pOut) const {
    for (u32 i = 0; i < count; i++) {
        const BankVelo& rVelo = mVelos[first + i];
        if (velocity <= rVelo.mHighVelocity) {
            pOut->mSound.mVolume *= rVelo.mVolume;
            pOut->mSound.mPitch *= rVelo.mPitch;
            pOut->mWave = rVelo.mWave;
            return true;
        }
    }
    return false;
}

bool SoundBank::instParams(s32 program, s32 key, s32 velocity, FastRandom* pRandom, InstParams* pOut) const {
    if (program < 0 || static_cast< u32 >(program) >= mFile->mProgramCount) {
        return false;
    }
    const BankProgram& rProgram = mPrograms[program];
    switch (rProgram.mKind) {
    case BankProgram::INSTRUMENT: {
        pOut->mFixedKey = false;
        pOut->mOsc[0] = rProgram.mOsc[0] >= 0 ? &mOscs[rProgram.mOsc[0]] : nullptr;
        pOut->mOsc[1] = rProgram.mOsc[1] >= 0 ? &mOscs[rProgram.mOsc[1]] : nullptr;
        pOut->mOscCount = 2;
        pOut->mSound.mVolume = rProgram.mVolume;
        pOut->mSound.mPitch = rProgram.mPitch;
        effects(rProgram.mEffectFirst, rProgram.mEffectCount, key, velocity, pRandom, pOut);
        for (u32 i = 0; i < rProgram.mCount; i++) {
            const BankKey& rKey = mKeys[rProgram.mFirst + i];
            if (key <= rKey.mHighKey) {
                return this->velocity(rKey.mVeloFirst, rKey.mVeloCount, velocity, pOut);
            }
        }
        return false;
    }
    case BankProgram::DRUMS: {
        if (static_cast< u32 >(key) >= rProgram.mCount) {
            return false;
        }
        const BankPerc& rPerc = mPercs[rProgram.mFirst + key];
        if (rPerc.mPresent == 0) {
            return false;
        }
        pOut->mFixedKey = true;
        pOut->mSound.mVolume = rPerc.mVolume;
        pOut->mSound.mPitch = rPerc.mPitch;
        pOut->mSound.mPan = rPerc.mPan;
        pOut->mDirectRelease = static_cast< u16 >(rPerc.mRelease);
        pOut->mOsc[0] = &cHoldOsc;
        pOut->mOscCount = 1;
        effects(rPerc.mEffectFirst, rPerc.mEffectCount, key, velocity, pRandom, pOut);
        return this->velocity(rPerc.mVeloFirst, rPerc.mVeloCount, velocity, pOut);
    }
    default:
        return false;
    }
}

bool SoundBanks::prepareNote(const SoundBank* pBank, const WaveBank* pWaves, s32 program, u8 key, u8 velocity, u16 priority, FastRandom* pRandom,
                             Note* pNote) {
    if (program >= 0xf0) {
        pNote->mPriority = priority;
        pNote->mOscillatorWave = static_cast< u32 >(program - 0xf0);
        pNote->mKind = Note::KIND_OSC;
        pNote->mSourceRate = cNum_prepareNote_f1;
        pNote->mKey = static_cast< s16 >(key - pNote->mBaseKey);
        pNote->mVelocity = velocity;
        pNote->mOsc[0].initStart(&cOscEnvelope);
        return true;
    }
    if (pBank == nullptr) {
        return false;
    }
    InstParams params;
    if (!pBank->instParams(program, key, velocity, pRandom, &params)) {
        return false;
    }
    if (pWaves == nullptr || params.mWave < 0) {
        return false;
    }
    const u32* live = nullptr;
    const u8* wave = nullptr;
    const u8* end = nullptr;
    const WaveEntry* entry = pWaves->find(static_cast< u32 >(params.mWave), &live, &wave, &end);
    if (entry == nullptr) {
        return false;
    }
    pNote->mPriority = priority;
    pNote->mFormat = entry->mFormat;
    pNote->mBaseKey = entry->mBaseKey;
    pNote->mLoop = entry->mLoop;
    pNote->mLoopStart = entry->mLoopStart;
    pNote->mLoopEnd = entry->mLoopEnd;
    pNote->mSampleCount = entry->mSampleCount;
    pNote->mWaveLive = live;
    pNote->mWave = wave;
    pNote->mWaveEnd = end;
    pNote->mKind = Note::KIND_WAVE;
    pNote->mSound.mPitch = params.mSound.mPitch;
    pNote->mSourceRate = entry->mSampleRate;
    if (!params.mFixedKey) {
        pNote->mKey = static_cast< s16 >(key - entry->mBaseKey);
    }
    pNote->mSound.mVolume = params.mSound.mVolume;
    pNote->mVelocity = velocity;
    pNote->mSound.mPan = params.mSound.mPan;
    pNote->mSound.mFxMix = params.mSound.mFxMix;
    pNote->mSound.mDolby = params.mSound.mDolby;
    for (u32 i = 0; i < params.mOscCount; i++) {
        pNote->mOsc[i].initStart(params.mOsc[i]);
    }
    pNote->mOsc[0].mDirectRelease = params.mDirectRelease;
    return true;
}
