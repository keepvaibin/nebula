#include "Game/Speaker/SpkSpeakerCtrl.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/System/NebulaInput.hpp"

namespace {
    bool sConnected[PLAYER_MAX];
    bool sSetup;

    // Before the sound system exists a connection is only kept; setup sends it.
    void sendPad(s32 padChannel) {
        SoundEngine* const engine = SoundEngine::get();
        if (engine == nullptr || !sSetup) {
            return;
        }
        SoundCommand& rCommand = engine->ring().next(SOUND_SPEAKER_PAD, nullptr);
        rCommand.mArg = static_cast< u16 >(padChannel);
        rCommand.mValue = sConnected[padChannel] ? 1 : 0;
        engine->ring().send();
    }
}  // namespace

void SpkSpeakerCtrl::setup() {
    SoundRing& rRing = SoundEngine::get()->ring();
    rRing.next(SOUND_SPEAKER_SETUP, nullptr);
    rRing.send();

    sSetup = true;
    for (s32 i = 0; i < PLAYER_MAX; i++) {
        sendPad(i);
    }
}

void SpkSpeakerCtrl::connect(s32 padChannel) {
    sConnected[padChannel] = true;
    sendPad(padChannel);
}

void SpkSpeakerCtrl::disconnect(s32 padChannel) {
    sConnected[padChannel] = false;
    sendPad(padChannel);
}

bool SpkSpeakerCtrl::isEnable(s32 padChannel) {
    return sConnected[padChannel] && sSetup;
}

f32 SpkSpeakerCtrl::getDeviceVolume(s32 padChannel) {
    if (padChannel >= PLAYER_1 && !sConnected[padChannel]) {
        return 0.0f;
    }

    // The speaker volume at its default (0x58 of 127); the host's speaker gain is the player's.
    return 0x58 / 127.0f;
}
