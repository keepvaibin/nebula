#include "Game/AudioLib/AudSpeakerWrap.hpp"
#include "Game/Speaker/SpkSound.hpp"
#include "Game/Speaker/SpkSystem.hpp"
#include "Game/Sound/SoundGlobal.hpp"
#include "Game/System/NebulaInput.hpp"

namespace {
    SpkSoundHandle sSoundHandle[PLAYER_MAX];
};  // namespace

const char* AudSpeakerWrap::getResName() {
    return "/AudioRes/SpkRes/SpkRes";
}

void AudSpeakerWrap::start(s32 waveID, s32 padChannel) {
    SpkSystem* system = SoundGlobal< SpkSystem >::getInstance();

    if (system == nullptr) {
        return;
    }

    if (padChannel < PLAYER_1) {
        for (s32 idx = 0; idx < PLAYER_MAX; idx++) {
            system->startSound(idx, waveID, &::sSoundHandle[idx]);
        }
    } else {
        system->startSound(padChannel, waveID, &::sSoundHandle[padChannel]);
    }
}

bool AudSpeakerWrap::isPlayable(s32 padChannel) {
    SpkSystem* system = SoundGlobal< SpkSystem >::getInstance();

    if (system == nullptr) {
        return false;
    }

    return system->getDeviceVolume(padChannel) != 0.0f;
}
