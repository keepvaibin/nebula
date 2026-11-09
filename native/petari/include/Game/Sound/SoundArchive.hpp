#pragma once

#include "Game/Sound/SeqSource.hpp"
#include "Game/Sound/SoundData.hpp"

class AssetFolder;
class NebulaArena;
class SoundNameTable;
class SoundTable;

// What the sound system plays from: the cooked sound archive (tables, effect programs, banks, wave banks) mapped from the
// store for the process's life, and the music programs of the sequence archive. Its own lookups give the archive's static
// sequences and the music; the effect programs answer for the effect manager.
class SoundArchive : public SeqSource {
public:
    explicit SoundArchive(NebulaArena* pArena);

    NebulaArena* getArena() const {
        return mArena;
    }

    // The archive at pPath (none: no tables, no banks). Sets the engine's banks up: before its clock starts.
    void load(const char* pPath);

    void setSeqArchive(AssetFolder* pArchive) {
        mSeqArchive = pArchive;
    }

    // Every wave group of every wave bank but one, whose groups hold the same waves and are chosen (setWaveGroups).
    void setAllWaves(u32 exceptWaveBank);
    // The groups `groups` names, a bit each, of a wave bank: playable or not.
    void setWaveGroups(u32 waveBank, u32 groups, bool live);

    SeqSource* getEffectSource() {
        return mEffects.mFile != nullptr ? &mEffects : nullptr;
    }

    SoundTable* getSoundTable() const {
        return mSoundTable;
    }

    virtual bool getSeqData(SoundID id, SeqData* pOut);

private:
    // The effect programs: one program, an entry for each effect, the sound id's group and wave numbering them.
    struct EffectSource : public SeqSource {
        virtual bool getSeqData(SoundID id, SeqData* pOut);
        virtual const SeqFile* program() const {
            return mFile;
        }
        const SeqFile* mFile = nullptr;
    };

    void setWaveGroup(u32 waveBank, u32 group, bool live);

    NebulaArena* mArena;
    AssetFolder* mSeqArchive = nullptr;
    const SoundArchiveFile* mFile = nullptr;
    const SoundStatic* mStatics = nullptr;
    const WaveBankFile* const* mWaveBanks = nullptr;
    EffectSource mEffects;
    SoundTable* mSoundTable = nullptr;
    SoundNameTable* mSoundNameTable = nullptr;
};
