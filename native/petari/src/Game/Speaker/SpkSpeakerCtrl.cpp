#include "Game/Speaker/SpkSpeakerCtrl.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaInput.hpp"

namespace {
    // Each controller's connection (NebulaInputPlayers::count of them), made at the first ask.
    bool* sConnected;
    bool sSetup;

    bool* connected() {
        if (sConnected == nullptr) {
            const u32 count = NebulaInputPlayers::count();
            sConnected = new (Arenas::permanent(), 0) bool[count];
            for (u32 i = 0; i < count; i++) {
                sConnected[i] = false;
            }
        }
        return sConnected;
    }

    // Before the sound system exists a connection is only kept; setup sends it.
    void sendPad(s32 padChannel) {
        SoundEngine* const engine = SoundEngine::get();
        if (engine == nullptr || !sSetup) {
            return;
        }
        SoundCommand& rCommand = engine->ring().next(SOUND_SPEAKER_PAD, nullptr);
        rCommand.mArg = static_cast< u16 >(padChannel);
        rCommand.mValue = connected()[padChannel] ? 1 : 0;
        engine->ring().send();
    }
}  // namespace

void SpkSpeakerCtrl::setup() {
    SoundEngine::get()->setupSpeaker(NebulaInputPlayers::count());

    sSetup = true;
    for (s32 i = 0; i < static_cast< s32 >(NebulaInputPlayers::count()); i++) {
        sendPad(i);
    }
}

void SpkSpeakerCtrl::connect(s32 padChannel) {
    connected()[padChannel] = true;
    sendPad(padChannel);
}

void SpkSpeakerCtrl::disconnect(s32 padChannel) {
    connected()[padChannel] = false;
    sendPad(padChannel);
}

bool SpkSpeakerCtrl::isEnable(s32 padChannel) {
    return connected()[padChannel] && sSetup;
}

f32 SpkSpeakerCtrl::getDeviceVolume(s32 padChannel) {
    if (padChannel >= PLAYER_1 && !connected()[padChannel]) {
        return 0.0f;
    }

    // The speaker volume at its default (0x58 of 127); the host's speaker gain is the player's.
    return 0x58 / 127.0f;
}
