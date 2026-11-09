#include "Game/Speaker/SpkWave.hpp"
#include "Game/Util/ByteView.hpp"

SpkWave::SpkWave() : mResource(nullptr) {
}

void SpkWave::setResource(void* pResource) {
    mResource = pResource;
}

const WaveData* SpkWave::getWaveData(s32 wave) const {
    const u8* const pFile = static_cast< const u8* >(mResource);
    return reinterpret_cast< const WaveData* >(pFile + readBytes< u32 >(pFile + wave * 4 + 8));
}
