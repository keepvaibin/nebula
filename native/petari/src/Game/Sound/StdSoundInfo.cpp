#include "Game/Sound/StdSoundInfo.hpp"
#include "Game/Sound/SoundChild.hpp"
#include "Game/Sound/SoundTable.hpp"
#include "Game/Sound/StreamSound.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaMods.hpp"

namespace {
    // The item of a sound of this type (the type's top four bits), else null. getTypeID is the item's mType.
    const SoundItem* item(SoundID soundID, u32 type) {
        const SoundItem* data = SoundGlobal< SoundTable >::getInstance()->getData(soundID);
        return data != nullptr && (data->mType & 0xF0) == type ? data : nullptr;
    }

    // An item of any type (they all have bit 0x40).
    const SoundItem* anyItem(SoundID soundID) {
        const SoundItem* data = SoundGlobal< SoundTable >::getInstance()->getData(soundID);
        return data != nullptr && (data->mType & 0x40) != 0 ? data : nullptr;
    }
}  // namespace

u16 StdSoundInfo::getBgmSeqResourceID(SoundID soundID) const {
    const SoundItem* data = item(soundID, DATA_BGM);
    return data != nullptr ? data->mResource : 0xffff;
}

// The table's all-ones switch: every flag, a doppler of fifteen fifteenths, half the far distance.
const SoundAudibleSwitches StdSoundInfo::cAllAudible = {
    SoundAudibleSwitches::VOLUME | SoundAudibleSwitches::FX_MIX | SoundAudibleSwitches::PAN | SoundAudibleSwitches::DOLBY |
        SoundAudibleSwitches::DISTANCE_PRIORITY | SoundAudibleSwitches::FAR_DROPS,
    {},
    15.0f * (1.0f / 15.0f),
    0.5f,
};

const SoundAudibleSwitches& StdSoundInfo::getAudibleSwitches(SoundID soundID) const {
    const SoundItem* data = item(soundID, DATA_SE);
    return data != nullptr ? data->mAudible : cAllAudible;
}

u32 StdSoundInfo::getPriority(SoundID soundID) const {
    const SoundItem* data = anyItem(soundID);
    return data != nullptr ? data->mPriority : 0;
}

void StdSoundInfo::applySound(SoundID soundID, Sound* pSound) const {
    const SoundItem* data = anyItem(soundID);
    if (data != nullptr) {
        pSound->mParams.mProperty.mVolume = data->mVolume * (1.0f / 255.0f);
    }
}

// Each of the first eight channels where the table places it.
void StdSoundInfo::applyStream(SoundID soundID, StreamSound* pStream) const {
    applySound(soundID, pStream);

    const SoundItem* data = item(soundID, DATA_STREAM);
    if (data == nullptr) {
        return;
    }

    const int count = pStream->getNumChild();
    for (int idx = 0; idx < count && idx < 8; idx++) {
        const u8 place = data->mChannelPlace[idx];
        if (place == CHANNEL_KEPT) {
            continue;
        }

        SoundChild* child = pStream->getChild(idx);
        if (child == nullptr) {
            continue;
        }

        switch (place) {
        case CHANNEL_CENTRE:
            child->mMove.mParams.mPan = 0.5f;
            break;
        case CHANNEL_LEFT:
            child->mMove.mParams.mPan = 0.0f;
            break;
        case CHANNEL_RIGHT:
            child->mMove.mParams.mPan = 1.0f;
            break;
        }
    }
}

const char* StdSoundInfo::getStreamFilePath(SoundID soundID) const {
    const SoundItem* data = item(soundID, DATA_STREAM);
    return data != nullptr ? SoundGlobal< SoundTable >::getInstance()->getStreamPath(data) : nullptr;
}

s32 StdSoundInfo::getStreamFileEntry(SoundID soundID) const {
    if (soundID.getWaveID() >= NebulaMods::cFirstTrackWave) {
        return NebulaMods::trackFile(soundID.getWaveID());
    }

    const char* path = getStreamFilePath(soundID);
    if (path == nullptr) {
        return -1;
    }
    return GameAssets::find(path);
}
