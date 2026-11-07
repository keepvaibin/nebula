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

u16 AudSoundInfo::getSoundSw(SoundID soundID) const {
    u32 type;
    const SoundItem* data = item(soundID, &type);
    return type == DATA_BGM || type == DATA_SE ? data->mSoundSw : 0;
}

bool AudSoundInfo::isLimitNumPlaying(SoundID soundID) const {
    s32 numOfPlaying = AudWrap::getSystem()->getNumOfPlaying(soundID);
    u16 sw = (getSoundSw(soundID) >> 3) & 3;
    if (sw == 0) {
        return false;
    }
    return sw <= numOfPlaying;
}

bool AudSoundInfo::isLimitCountPlaying(SoundID soundID) const {
    u32 max;
    switch (getSoundSw(soundID) & 0x60) {
    case 0x20:
        max = 1;
        break;
    case 0x40:
        max = 5;
        break;
    case 0x60:
        max = 10;
        break;
    default:
        return false;
    }
    return AudWrap::getSystem()->getPlayCountMin(soundID) < max;
}

u16 AudSoundInfo::getChordResIdBgm(SoundID soundID) const {
    u32 type;
    const SoundItem* data = item(soundID, &type);
    return type == DATA_BGM ? data->mChordResource : 0xffff;
}

bool AudSoundInfo::isRhythmBgm(SoundID soundID) const {
    u32 type;
    item(soundID, &type);
    return type == DATA_BGM && (getSoundSw(soundID) & 1) != 0;
}

bool AudSoundInfo::isFanfareSe(SoundID soundID) const {
    u32 type;
    item(soundID, &type);
    return type == DATA_SE && ((getSoundSw(soundID) >> 8) & 1) != 0;
}

bool AudSoundInfo::isUseFxMix(SoundID soundID) const {
    u32 type;
    const SoundItem* data = item(soundID, &type);
    return type == DATA_SE && ((data->mSoundSw >> 9) & 1) != 0;
}

bool AudSoundInfo::isUseLpf(SoundID soundID) const {
    u32 type;
    const SoundItem* data = item(soundID, &type);
    return type == DATA_SE && ((data->mSoundSw >> 10) & 1) != 0;
}
