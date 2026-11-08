#include "Game/AudioLib/AudTrackController.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundChild.hpp"
#include "Game/Sound/SoundControls.hpp"
#include "Game/Sound/PlayerLink.hpp"

AudTrackController::AudTrackController() : mTrackNo(-1), mHandle(nullptr), mAutoMute(false), mVolume(1.0f) {
}

void AudTrackController::start(SoundHandle* pHandle) {
    mHandle = pHandle;
    mAutoMute = false;
}

void AudTrackController::stop() {
    mHandle = nullptr;
    mAutoMute = false;
}

namespace {
    void muteTrack(SoundHandle* pHandle, s32 trackNo, bool isMute) {
        if (pHandle == nullptr) {
            return;
        }

        Sound* sound = pHandle->getSound();
        if (sound == nullptr || trackNo < 0) {
            return;
        }

        TrackPath path;
        PlayerLink* player = sound->getChildTrack(trackNo, &path);
        if (player != nullptr) {
            player->mute(path, isMute);
        }
    }
}  // namespace

void AudTrackController::mute() {
    ::muteTrack(mHandle, mTrackNo, true);
}

void AudTrackController::unmute() {
    ::muteTrack(mHandle, mTrackNo, false);
}

void AudTrackController::setMuteState(u8 state, s32 fadeTime, bool autoMute) {
    switch (state) {
    case AudFader::FadeState_FadeOut:
        mFader.set(0.0f, fadeTime);
        break;
    case AudFader::FadeState_FadeIn:
        unmute();
        mFader.set(1.0f, fadeTime);
        break;
    case AudFader::FadeState_Mute:
        mute();
        break;
    case AudFader::FadeState_Unmute:
        unmute();
        break;
    default:
        return;
    }

    mAutoMute = autoMute;
}

void AudTrackController::update() {
    if (mHandle != nullptr && mHandle->isSoundAttached()) {
        if (mTrackNo < 0) {
            return;
        }

        mFader.update();

        f32 volume = mVolume * mFader.getVolume();
        SoundMove* params = mHandle->getSound()->getChild(mTrackNo)->getMoveParams();
        if (params == nullptr) {
            return;
        }
        params->moveVolume(volume, 0);
    }

    muteIfVolumeZero();
}

void AudTrackController::muteIfVolumeZero() {
    if (mTrackNo < 0) {
        return;
    }

    if (!mAutoMute) {
        return;
    }

    if (mHandle == nullptr) {
        return;
    }

    Sound* sound = mHandle->getSound();
    if (sound == nullptr) {
        return;
    }

    SoundChild* child = sound->getChild(mTrackNo);
    if (child == nullptr) {
        return;
    }

    TrackPath path;
    PlayerLink* player = mHandle->getSound()->getChildTrack(mTrackNo, &path);
    if (player == nullptr) {
        return;
    }

    player->mute(path, child->mParams.mVolume == 0.0f);
}
