#include "Game/AudioLib/AudSpeakerWrap.hpp"
#include "Game/Speaker/SpkSound.hpp"
#include "Game/Speaker/SpkSystem.hpp"
#include "Game/Sound/SoundGlobal.hpp"
#include "Game/System/NebulaInput.hpp"

const char* AudSpeakerWrap::getResName() {
    return "/AudioRes/SpkRes/SpkRes";
}

void AudSpeakerWrap::start(s32 waveID, s32 padChannel) {
    SpkSystem* system = SoundGlobal< SpkSystem >::getInstance();

    if (system == nullptr) {
        return;
    }

    if (padChannel < PLAYER_1) {
        for (s32 idx = 0; idx < static_cast< s32 >(NebulaInputPlayers::count()); idx++) {
            system->startSound(idx, waveID, system->padHandle(idx));
        }
    } else {
        system->startSound(padChannel, waveID, system->padHandle(padChannel));
    }
}

bool AudSpeakerWrap::isPlayable(s32 padChannel) {
    SpkSystem* system = SoundGlobal< SpkSystem >::getInstance();

    if (system == nullptr) {
        return false;
    }

    return system->getDeviceVolume(padChannel) != 0.0f;
}
