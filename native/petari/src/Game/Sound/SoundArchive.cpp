#include "Game/Sound/SoundArchive.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/Sound/SoundTable.hpp"
#include "Game/Sound/StdSoundInfo.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaArena.hpp"

namespace {
    const u8* part(const SoundArchiveFile* pFile, u32 offset) {
        return reinterpret_cast< const u8* >(pFile) + offset;
    }

    // A program as this version cooks it, whole within its bytes.
    const SeqFile* checkedProgram(const void* pData, u32 bytes) {
        const SeqFile* pFile = static_cast< const SeqFile* >(pData);
        if (bytes < sizeof(SeqFile) || pFile->mMagic != SeqFile::cMagic || pFile->mVersion != SeqFile::cVersion) {
            NEBULA_PANIC("a sequence not cooked by this version of the importer");
        }
        const u64 need = sizeof(SeqFile) + (u64(pFile->mGroupCount) + 1 + pFile->mEntryCount + pFile->mOpCount) * sizeof(u32) +
                         u64(pFile->mOpCount) * sizeof(SeqOp) + pFile->mDataBytes;
        if (need > bytes) {
            NEBULA_PANIC("a sequence of %u bytes that needs %u", bytes, static_cast< u32 >(need));
        }
        return pFile;
    }
}  // namespace

SoundArchive::SoundArchive(NebulaArena* pArena) : mArena(pArena) {
}

void SoundArchive::load(const char* pPath) {
    GameAssets::File file;
    if (!GameAssets::open(pPath, &file)) {
        return;
    }
    const SoundArchiveFile* pFile = static_cast< const SoundArchiveFile* >(GameAssets::map(file.mEntry));
    if (pFile == nullptr || file.mLength < sizeof(SoundArchiveFile) || pFile->mMagic != SoundArchiveFile::cMagic ||
        pFile->mVersion != SoundArchiveFile::cVersion) {
        NEBULA_PANIC("a sound archive not cooked by this version of the importer");
    }
    const u64 tables = sizeof(SoundArchiveFile) + u64(pFile->mSectionCount) * sizeof(SoundSection) +
                       u64(pFile->mGroupCount) * sizeof(SoundGroup) + u64(pFile->mItemCount) * sizeof(SoundItem) +
                       u64(pFile->mStaticCount) * sizeof(SoundStatic) + (u64(pFile->mBankCount) + pFile->mWaveBankCount) * sizeof(SoundPart) +
                       pFile->mNameBytes;
    if (tables > file.mLength) {
        NEBULA_PANIC("a sound archive of %u bytes whose tables need %u", file.mLength, static_cast< u32 >(tables));
    }
    mFile = pFile;

    if ((pFile->mFlags & SoundArchiveFile::cHasTable) != 0) {
        mSoundTable = new (mArena, 0) SoundTable(true);
        mSoundTable->init(pFile);
    }
    if ((pFile->mFlags & SoundArchiveFile::cHasNames) != 0) {
        mSoundNameTable = new (mArena, 0) SoundNameTable(true);
        mSoundNameTable->init(pFile);
    }

    const u8* at = part(pFile, sizeof(SoundArchiveFile) + pFile->mSectionCount * sizeof(SoundSection) + pFile->mGroupCount * sizeof(SoundGroup) +
                                   pFile->mItemCount * sizeof(SoundItem));
    mStatics = reinterpret_cast< const SoundStatic* >(at);
    const SoundPart* banks = reinterpret_cast< const SoundPart* >(mStatics + pFile->mStaticCount);
    const SoundPart* waveBanks = banks + pFile->mBankCount;
    for (u32 i = 0; i < pFile->mStaticCount; i++) {
        checkedProgram(part(pFile, mStatics[i].mFile), mStatics[i].mBytes);
    }
    if (pFile->mEffects != 0) {
        mEffects.mFile = checkedProgram(part(pFile, pFile->mEffects), pFile->mEffectBytes);
    }

    SoundEngine* engine = SoundEngine::get();
    const WaveBankFile** ppWaveBanks = new (mArena, 0) const WaveBankFile*[pFile->mWaveBankCount + 1];
    engine->setupWaveBanks(pFile->mWaveBankCount, mArena);
    for (u32 i = 0; i < pFile->mWaveBankCount; i++) {
        ppWaveBanks[i] = nullptr;
        if (waveBanks[i].mFile != 0) {
            ppWaveBanks[i] = reinterpret_cast< const WaveBankFile* >(part(pFile, waveBanks[i].mFile));
            engine->setupWaveBank(i, ppWaveBanks[i], waveBanks[i].mBytes, mArena);
        }
    }
    mWaveBanks = ppWaveBanks;
    for (u32 i = 0; i < pFile->mBankCount; i++) {
        if (banks[i].mFile != 0) {
            engine->setupBank(reinterpret_cast< const BankFile* >(part(pFile, banks[i].mFile)), banks[i].mBytes, mArena);
        }
    }
}

void SoundArchive::setWaveGroup(u32 waveBank, u32 group, bool live) {
    const WaveBankFile* pBank = mFile != nullptr && waveBank < mFile->mWaveBankCount ? mWaveBanks[waveBank] : nullptr;
    if (pBank == nullptr || group >= pBank->mGroupCount) {
        return;
    }
    WaveGroupCommand command = {};
    command.mWaveBank = waveBank;
    command.mGroup = group;
    command.mLive = live;
    if (live) {
        const WaveGroup* groups = reinterpret_cast< const WaveGroup* >(pBank + 1);
        const char* names = reinterpret_cast< const char* >(reinterpret_cast< const WaveEntry* >(groups + pBank->mGroupCount) + pBank->mWaveCount);
        GameAssets::File file;
        if (!GameAssets::open(names + groups[group].mName, &file)) {
            return;
        }
        command.mData = static_cast< const u8* >(GameAssets::map(file.mEntry));
        command.mBytes = file.mLength;
    }
    SoundRing& rRing = SoundEngine::get()->ring();
    rRing.next(SOUND_WAVE_GROUP, nullptr).payload< WaveGroupCommand >() = command;
    rRing.send();
}

void SoundArchive::setAllWaves(u32 exceptWaveBank) {
    for (u32 bank = 0; mFile != nullptr && bank < mFile->mWaveBankCount; bank++) {
        const WaveBankFile* pBank = bank != exceptWaveBank ? mWaveBanks[bank] : nullptr;
        for (u32 group = 0; pBank != nullptr && group < pBank->mGroupCount; group++) {
            setWaveGroup(bank, group, true);
        }
    }
}

void SoundArchive::setWaveGroups(u32 waveBank, u32 groups, bool live) {
    for (u32 group = 0; group < 32; group++) {
        if ((groups & (1u << group)) != 0) {
            setWaveGroup(waveBank, group, live);
        }
    }
}

bool SoundArchive::getSeqData(SoundID id, SeqData* pOut) {
    for (u32 i = 0; mFile != nullptr && i < mFile->mStaticCount; i++) {
        if (mStatics[i].mSoundId == static_cast< u32 >(id)) {
            pOut->set(reinterpret_cast< const SeqFile* >(part(mFile, mStatics[i].mFile)), 0);
            return true;
        }
    }
    AssetFolder::Item item;
    if (mSeqArchive != nullptr && mSeqArchive->getItem(mSeqArchive->getEntry(), StdSoundInfo::getInstance()->getBgmSeqResourceID(id), &item)) {
        pOut->set(checkedProgram(item.mData, item.mSize), 0);
        return true;
    }
    return false;
}

bool SoundArchive::EffectSource::getSeqData(SoundID id, SeqData* pOut) {
    const u32* groupFirst = reinterpret_cast< const u32* >(mFile + 1);
    const u32* entries = groupFirst + mFile->mGroupCount + 1;
    const u32 group = id.getGroupID();
    if (group >= mFile->mGroupCount) {
        return false;
    }
    const u32 wave = id.getWaveID();
    if (wave >= groupFirst[group + 1] - groupFirst[group]) {
        return false;
    }
    pOut->set(mFile, entries[groupFirst[group] + wave]);
    return true;
}
