#include "Game/Speaker/SpkWave.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Util/ByteView.hpp"

SpkWave::SpkWave() : mResource(nullptr) {
}

void SpkWave::setResource(void* pResource) {
    GameAudioExclusion crit;
    mResource = pResource;
}

s32 SpkWave::getWaveSize(s32 wave) const {
    if (mResource == nullptr) {
        return 0;
    }

    return getWaveData(wave)->mSize;
}

u32 SpkWave::getLoopStartPos(s32 wave) const {
    if (mResource == nullptr) {
        return 0;
    }

    return getWaveData(wave)->mLoopStartPos;
}

u32 SpkWave::getLoopEndPos(s32 wave) const {
    if (mResource == nullptr) {
        return 0;
    }

    return getWaveData(wave)->mLoopEndPos;
}

s16* SpkWave::getWave(s32 wave) const {
    if (mResource == nullptr) {
        return nullptr;
    }

    return getWaveData(wave)->mWave;
}

WaveData* SpkWave::getWaveData(s32 wave) const {
    return (WaveData*)((uintptr_t)mResource + readBytes< u32 >(static_cast< const u8* >(mResource) + wave * 4 + 8));
}
