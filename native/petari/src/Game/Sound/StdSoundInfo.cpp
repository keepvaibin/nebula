#include "Game/Sound/StdSoundInfo.hpp"
#include "Game/Sound/SoundChild.hpp"
#include "Game/Sound/SoundTable.hpp"
#include "Game/Sound/StreamSound.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_applySound_f1, "applySound.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_applyStream_f1, "applyStream.f1", 0.5f);

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

u16 StdSoundInfo::getAudibleSw(SoundID soundID) const {
    const SoundItem* data = item(soundID, DATA_SE);
    return data != nullptr ? data->mAudibleSw : 0xffff;
}

u32 StdSoundInfo::getPriority(SoundID soundID) const {
    const SoundItem* data = anyItem(soundID);
    return data != nullptr ? data->mPriority : 0;
}

void StdSoundInfo::applySound(SoundID soundID, Sound* pSound) const {
    const SoundItem* data = anyItem(soundID);
    if (data != nullptr) {
        pSound->mParams.mProperty.mVolume = data->mVolume * (1.0f / cNum_applySound_f1);
    }
}

// The channel control: two bits a channel, from the first (0 leaves it, 1 centre, 2 left, 3 right).
void StdSoundInfo::applyStream(SoundID soundID, StreamSound* pStream) const {
    applySound(soundID, pStream);

    const SoundItem* data = item(soundID, DATA_STREAM);
    if (data == nullptr) {
        return;
    }

    u16 bits = data->mChannelCtrl;
    int numChild = pStream->getNumChild();
    for (int idx = 0; idx < numChild && bits != 0; idx++, bits >>= 2) {
        u16 ctrl = bits & 0b11;
        if (ctrl == 0) {
            continue;
        }

        SoundChild* child = pStream->getChild(idx);
        if (child == nullptr) {
            continue;
        }

        switch (ctrl) {
        case 1:
            child->mMove.mParams.mPan = cNum_applyStream_f1;
            break;
        case 2:
            child->mMove.mParams.mPan = 0.0f;
            break;
        case 3:
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
