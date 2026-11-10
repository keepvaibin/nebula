#pragma once

#include "Game/Sound/SoundData.hpp"
#include "Game/Sound/SoundVoices.hpp"

class FastRandom;

// An instrument's answer for one key and velocity: its wave, envelopes and mix values.
struct InstParams {
    SoundParams mSound;
    s32 mWave = 0;
    const OscData* mOsc[2] = {nullptr, nullptr};
    u32 mOscCount = 0;
    // Drums: the key does not change the pitch.
    bool mFixedKey = false;
    u16 mDirectRelease = 0;
};

// A wave bank's waves by id and the groups that hold them. A group is live while its file is mapped; a wave id belongs to the
// last group made live that holds it.
class WaveBank {
public:
    // `pCurrent` (mIdCount s32), `pLive` (mGroupCount u32) and `pData` (mGroupCount pointers): the caller's memory.
    bool setup(const WaveBankFile* pFile, u32 size, s32* pCurrent, u32* pLive, const u8** pData, u32* pDataBytes);
    void activate(u32 group, const u8* pData, u32 bytes);
    void deactivate(u32 group);
    u32 groupCount() const {
        return mFile->mGroupCount;
    }
    const char* groupFile(u32 group) const {
        return mNames + mGroups[group].mName;
    }
    // The wave of `id` in its live group: its entry, its group's live flag, its first byte and the end of its file.
    const WaveEntry* find(u32 id, const u32** ppLive, const u8** ppWave, const u8** ppEnd) const;

private:
    const WaveBankFile* mFile = nullptr;
    const WaveGroup* mGroups = nullptr;
    const WaveEntry* mWaves = nullptr;
    const char* mNames = nullptr;
    s32* mCurrent = nullptr;
    u32* mLive = nullptr;
    const u8** mData = nullptr;
    u32* mDataBytes = nullptr;
};

class SoundBank {
public:
    // `pOscs`: mOscCount records of the caller's memory, filled from the file.
    bool setup(const BankFile* pFile, u32 size, OscData* pOscs);
    bool instParams(s32 program, s32 key, s32 velocity, FastRandom* pRandom, InstParams* pOut) const;
    u32 number() const {
        return mFile->mBankNumber;
    }
    u32 waveBank() const {
        return mFile->mWaveBank;
    }

private:
    void effects(u32 first, u32 count, s32 key, s32 velocity, FastRandom* pRandom, InstParams* pOut) const;
    bool velocity(u32 first, u32 count, s32 velocity, InstParams* pOut) const;

    const BankFile* mFile = nullptr;
    const BankProgram* mPrograms = nullptr;
    const BankKey* mKeys = nullptr;
    const BankVelo* mVelos = nullptr;
    const BankPerc* mPercs = nullptr;
    const BankEffect* mEffects = nullptr;
    const OscData* mOscs = nullptr;
};

namespace SoundBanks {
    // A note of program `program` (0xf0 and up: an oscillator) from `pBank` (null: none loaded), ready for Voices::play.
    bool prepareNote(const SoundBank* pBank, const WaveBank* pWaves, s32 program, u8 key, u8 velocity, u16 priority, FastRandom* pRandom,
                     SoundNote* pNote);
}  // namespace SoundBanks
