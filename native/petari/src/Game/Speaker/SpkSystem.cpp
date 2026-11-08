#include "Game/Speaker/SpkSystem.hpp"
#include "Game/Speaker/SpkSpeakerCtrl.hpp"
#include "Game/System/NebulaArena.hpp"

SpkSystem::SpkSystem(NebulaArena* pArena) : SoundGlobal(true), mData() {
    if (pArena == nullptr) {
        pArena = NebulaArena::current();
    }

    mArena = pArena;
    mSoundHolder = new (pArena, 0) SpkSoundHolder(pArena);
    SpkSpeakerCtrl::setup();
}

void SpkSystem::setResource(AssetFolder* pArchive, u16 tableIndex, u16 waveIndex) {
    mData = new (mArena, 0) SpkData(pArchive);
    mData->loadTable(tableIndex);
    mData->loadWave(waveIndex);
}

void SpkSystem::framework() {
    mSoundHolder->framework();
}

void SpkSystem::startSound(s32 padChannel, s32 waveID, SpkSoundHandle* pHandle) {
    mSoundHolder->startSound(padChannel, waveID, pHandle);
}

void SpkSystem::connect(s32 padChannel) {
    SpkSpeakerCtrl::connect(padChannel);
}

void SpkSystem::disconnect(s32 padChannel) {
    SpkSpeakerCtrl::disconnect(padChannel);
}

f32 SpkSystem::getDeviceVolume(s32 padChannel) {
    return SpkSpeakerCtrl::getDeviceVolume(padChannel);
}
