#include "Game/Speaker/SpkSystem.hpp"
#include "Game/Speaker/SpkSpeakerCtrl.hpp"
#include "Game/System/NebulaArena.hpp"

SpkSystem::SpkSystem(NebulaArena* pHeap) : SoundGlobal(true), mData() {
    if (pHeap == nullptr) {
        pHeap = NebulaArena::getCurrentHeap();
    }

    mHeap = pHeap;
    mSoundHolder = new (pHeap, 0) SpkSoundHolder(pHeap);
    SpkSpeakerCtrl::setup();
}

void SpkSystem::setResource(AssetFolder* pArchive, u16 tableOffset, u16 waveOffset) {
    mData = new (mHeap, 0) SpkData(pArchive);
    mData->loadTable(tableOffset);
    mData->loadWave(waveOffset);
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
