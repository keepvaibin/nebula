#include "Game/Speaker/SpkSpeakerCtrl.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Speaker/SpkMixingBuffer.hpp"
#include "Game/Speaker/SpkSound.hpp"
#include "Game/System/GameThreads.hpp"
#include <JSystem/JAudio2/JASDriverIF.hpp>
#include <cstring>
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaInput.hpp"

SpeakerInfo sSpeakerInfo[PLAYER_MAX];
SpkSoundHandle sAdjustSoundHandle[PLAYER_MAX];
SpkMixingBuffer* sMixingBuffer;

namespace {
    // The speaker's 40 samples at 6 kHz last 6.67 ms: an update each 640/3 samples of the driver's 80 per sub-frame.
    u32 sSubFrameSamples;

    s32 onSubFrame(void*) {
        sSubFrameSamples += 240;
        while (sSubFrameSamples >= 640) {
            sSubFrameSamples -= 640;
            SpkSpeakerCtrl::updateSpeaker();
        }
        return 0;
    }

    SpeakerInfo& getSpeakerInfo(s32 idx) {
        return sSpeakerInfo[idx];
    }
};  // namespace

void SpkSpeakerCtrl::setMixingBuffer(SpkMixingBuffer* pMixingBuffer) {
    BOOL status = GameThreads::excludeAudio();
    sMixingBuffer = pMixingBuffer;
    GameThreads::restoreAudio(status);
}

void SpkSpeakerCtrl::setup() {
    for (int i = 0; i < PLAYER_MAX; i++) {
        // A controller can connect before this runs (on the audio init's thread): it stays connected.
        const bool connected = sSpeakerInfo[i].mIsConnected;
        sSpeakerInfo[i].mIsConnected = false;
        sSpeakerInfo[i].mIsPlaying = false;
        sSpeakerInfo[i].mIsUpdated = true;
        sSpeakerInfo[i].mIsMuted = false;
        sSpeakerInfo[i].mState = SpeakerInfo::State_ENABLE;
        sSpeakerInfo[i].mVolume = 64;
        sSpeakerInfo[i].mRadioSensitivityTimer = 0;
        sSpeakerInfo[i].mExtensionTimer = 0;
        SpkSpeakerCtrl::initReconnect(i);
        sSpeakerInfo[i].mUsingTimeOut = -1;
        if (connected) {
            SpkSpeakerCtrl::connect(i);
        }
    }

    JASDriver::registerSubFrameCallback(onSubFrame, nullptr);
}

void SpkSpeakerCtrl::connect(s32 padChannel) {
    GameAudioExclusion crit;
    sSpeakerInfo[padChannel].mIsConnected = true;
    sSpeakerInfo[padChannel].mIsPlaying = false;
    sSpeakerInfo[padChannel].mState = SpeakerInfo::State_ENABLE;
    SpkSpeakerCtrl::initReconnect(padChannel);
    sSpeakerInfo[padChannel].mUsingTimeOut = -1;
    SpkSpeakerCtrl::setSpeakerOn(padChannel);
}

void SpkSpeakerCtrl::disconnect(s32 padChannel) {
    GameAudioExclusion crit;
    sSpeakerInfo[padChannel].mIsConnected = false;
    sSpeakerInfo[padChannel].mIsPlaying = false;
    sSpeakerInfo[padChannel].mState = SpeakerInfo::State_ENABLE;
    SpkSpeakerCtrl::setSpeakerOff(padChannel);
    SpkSpeakerCtrl::initReconnect(padChannel);
    sSpeakerInfo[padChannel].mUsingTimeOut = -1;
}

// The speaker is the host's: it plays as soon as the game turns it on (no handshake with a remote), and its sound
// goes to the host as 16-bit samples (speaker_samples).
void SpkSpeakerCtrl::setSpeakerOn(s32 padChannel) {
    GameAudioExclusion crit;
    sSpeakerInfo[padChannel].mState = SpeakerInfo::State_ENABLE;
    SpkSpeakerCtrl::setSpeakerPlay(padChannel);
}

void SpkSpeakerCtrl::setSpeakerPlay(s32 padChannel) {
    GameAudioExclusion crit;
    sSpeakerInfo[padChannel].mIsPlaying = true;
    sSpeakerInfo[padChannel].mState = SpeakerInfo::State_ENABLE;
    sSpeakerInfo[padChannel].mUsingTimeOut = 8 * 60 * 60;
}

void SpkSpeakerCtrl::setSpeakerOff(s32 padChannel) {
    sSpeakerInfo[padChannel].mIsPlaying = false;
    sSpeakerInfo[padChannel].mState = SpeakerInfo::State_ENABLE;
    sSpeakerInfo[padChannel].mUsingTimeOut = -1;
}

void SpkSpeakerCtrl::retryConnection(s32 padChannel) {
    switch (sSpeakerInfo[padChannel].mState) {
    case SpeakerInfo::State_ENABLE:
    case SpeakerInfo::State_3:
        break;
    case SpeakerInfo::State_ON:
        SpkSpeakerCtrl::setSpeakerOn(padChannel);
        break;
    case SpeakerInfo::State_PLAY:
        SpkSpeakerCtrl::setSpeakerPlay(padChannel);
        break;
    }
}

void SpkSpeakerCtrl::reconnect(s32 padChannel) {
    if (sSpeakerInfo[padChannel].mIsConnected) {
        sSpeakerInfo[padChannel].mReconnectState = SpeakerInfo::Reconnect_OFF;
    }
}

void SpkSpeakerCtrl::framework() {
    for (s32 i = 0; i < PLAYER_MAX; i++) {
        SpkSpeakerCtrl::continuousUsingProcess(i);
        SpkSpeakerCtrl::reconnectProcess(i);
        SpkSpeakerCtrl::retryConnection(i);
    }
}

void SpkSpeakerCtrl::reconnectProcess(s32 padChannel) {
    if (sSpeakerInfo[padChannel].mIsConnected) {
        switch (sSpeakerInfo[padChannel].mReconnectState) {
        case SpeakerInfo::Reconnect_NONE:
            break;
        case SpeakerInfo::Reconnect_OFF:
            SpkSpeakerCtrl::setSpeakerOff(padChannel);
            sSpeakerInfo[padChannel].mReconnectState = SpeakerInfo::Reconnect_WAIT;
            sSpeakerInfo[padChannel].mReconnectTime = 20;
            break;
        case SpeakerInfo::Reconnect_WAIT:
            sSpeakerInfo[padChannel].mReconnectTime--;

            if (sSpeakerInfo[padChannel].mReconnectTime <= 0) {
                sSpeakerInfo[padChannel].mReconnectTime = -1;
                sSpeakerInfo[padChannel].mReconnectState = SpeakerInfo::Reconnect_ON;
            }
            break;
        case SpeakerInfo::Reconnect_ON:
            SpkSpeakerCtrl::setSpeakerOn(padChannel);
            sSpeakerInfo[padChannel].mReconnectState = SpeakerInfo::Reconnect_NONE;
            break;
        }
    }
}

void SpkSpeakerCtrl::initReconnect(s32 padChannel) {
    sSpeakerInfo[padChannel].mReconnectState = SpeakerInfo::Reconnect_NONE;
    sSpeakerInfo[padChannel].mReconnectTime = -1;
}

void SpkSpeakerCtrl::continuousUsingProcess(s32 padChannel) {
    if (sSpeakerInfo[padChannel].mUsingTimeOut >= 0) {
        sSpeakerInfo[padChannel].mUsingTimeOut--;
        if (sSpeakerInfo[padChannel].mUsingTimeOut <= 0) {
            sSpeakerInfo[padChannel].mUsingTimeOut = -1;
            SpkSpeakerCtrl::reconnect(padChannel);
        }
    }
}

void SpkSpeakerCtrl::updateSpeaker() {
    for (s32 i = 0; i < PLAYER_MAX; i++) {
        if (sMixingBuffer == nullptr) {
            continue;
        }

        SpeakerInfo& inf = ::getSpeakerInfo(i);

        if (!inf.mIsPlaying) {
            continue;
        }

        if (inf.mIsMuted) {
            continue;
        }

        // 40 samples every 6.67 ms: 6 kHz.
        if (sMixingBuffer->update(i) && inf.mIsConnected) {
            const NebulaHostServices* host = nebula_host();
            host->speaker_samples(host->user, static_cast< u32 >(i), sMixingBuffer->getSamples(i), 40);
        }
    }
}

bool SpkSpeakerCtrl::isEnable(s32 padChannel) {
    return sSpeakerInfo[padChannel].mIsPlaying && sSpeakerInfo[padChannel].mState == SpeakerInfo::State_ENABLE && sMixingBuffer != nullptr;
}

void SpkSpeakerCtrl::extensionProcess(s32, s32) {
}

f32 SpkSpeakerCtrl::getDeviceVolume(s32 padChannel) {
    if (padChannel >= PLAYER_1 && !sSpeakerInfo[padChannel].mIsPlaying) {
        return 0.0f;
    }

    // The speaker volume at its default (0x58 of 127); the host's speaker gain is the player's.
    return 0x58 / 127.0f;
}
