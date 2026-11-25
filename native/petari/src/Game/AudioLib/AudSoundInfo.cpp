#include "Game/AudioLib/AudSoundInfo.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundTable.hpp"

namespace {
    // The sound's item and its type's top four bits; type 0 when there is no item.
    const SoundItem* item(SoundID soundID, u32* pType) {
        const SoundTable* table = SoundGlobal< SoundTable >::getInstance();
        const SoundItem* data = table->getData(soundID);
        *pType = data != nullptr ? table->getTypeID(soundID) & 0xF0u : 0;
        return data;
    }
}  // namespace

SoundSwitches AudSoundInfo::getSwitches(SoundID soundID) const {
    u32 type;
    const SoundItem* data = item(soundID, &type);
    return type == DATA_BGM || type == DATA_SE ? data->mSwitches : SoundSwitches{};
}

bool AudSoundInfo::isLimitNumPlaying(SoundID soundID) const {
    s32 numOfPlaying = AudWrap::getSystem()->getNumOfPlaying(soundID);
    const s32 most = getSwitches(soundID).mMostPlaying;
    if (most == 0) {
        return false;
    }
    return most <= numOfPlaying;
}

bool AudSoundInfo::isLimitCountPlaying(SoundID soundID) const {
    const u32 gap = getSwitches(soundID).mStartGap;
    if (gap == 0) {
        return false;
    }
    return AudWrap::getSystem()->getPlayCountMin(soundID) < gap;
}

u16 AudSoundInfo::getChordResIdBgm(SoundID soundID) const {
    u32 type;
    const SoundItem* data = item(soundID, &type);
    return type == DATA_BGM ? data->mChordResource : 0xffff;
}

bool AudSoundInfo::isRhythmBgm(SoundID soundID) const {
    u32 type;
    item(soundID, &type);
    return type == DATA_BGM && (getSwitches(soundID).mFlags & SoundSwitches::RHYTHM) != 0;
}

bool AudSoundInfo::isFanfareSe(SoundID soundID) const {
    u32 type;
    item(soundID, &type);
    return type == DATA_SE && (getSwitches(soundID).mFlags & SoundSwitches::FANFARE) != 0;
}

bool AudSoundInfo::isUseFxMix(SoundID soundID) const {
    u32 type;
    const SoundItem* data = item(soundID, &type);
    return type == DATA_SE && (data->mSwitches.mFlags & SoundSwitches::FX_MIX) != 0;
}

bool AudSoundInfo::isUseLpf(SoundID soundID) const {
    u32 type;
    const SoundItem* data = item(soundID, &type);
    return type == DATA_SE && (data->mSwitches.mFlags & SoundSwitches::LOW_PASS) != 0;
}
