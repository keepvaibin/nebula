#include "Game/AudioLib/AudBgmVolumeController.hpp"
#include "Game/AudioLib/AudParams.hpp"

namespace {
    // Frames a held fader stays down after its last request.
    const s32 cHoldTime = 2;

    s32 getRecoverTime(s32 fader) {
        if (fader == AudBgmVolumeController::Fader_VolDown) {
            return AudParams::faderVolDownRecoverTime;
        }

        return AudParams::faderInterruptedRecoverTime;
    }
}  // namespace

AudBgmVolumeController::AudBgmVolumeController() {
    for (s32 i = 0; i < Fader_Num; i++) {
        mTimers[i] = -1;
    }
}

void AudBgmVolumeController::update() {
    for (s32 i = 0; i < Fader_Num; i++) {
        if (mTimers[i] > 0) {
            mTimers[i]--;
            if (mTimers[i] <= 0) {
                mFaders[i].set(1.0f, ::getRecoverTime(i));
                mTimers[i] = -1;
            }
        }
        mFaders[i].update();
    }
}

f32 AudBgmVolumeController::getVolume() const {
    f32 volume = 1.0f;
    for (s32 i = 0; i < Fader_Num; i++) {
        volume *= mFaders[i].mCurrentVolume;
    }
    return volume;
}

void AudBgmVolumeController::holdFade(Fader fader, f32 volume, s32 time) {
    if (mTimers[fader] < 0) {
        mFaders[fader].set(volume, time);
    }
    mTimers[fader] = ::cHoldTime;
}

void AudBgmVolumeController::volDown(bool immediate) {
    if (immediate) {
        mFaders[Fader_VolDown].set(1.0f, 1);
        mTimers[Fader_VolDown] = -1;
        return;
    }

    holdFade(Fader_VolDown, AudParams::faderVolDownVolume, AudParams::faderVolDownTime);
}

void AudBgmVolumeController::interruptedByOther() {
    holdFade(Fader_Interrupted, AudParams::faderInterruptedVolume, AudParams::faderInterruptedTime);
}

void AudBgmVolumeController::moveAuxVolume(f32 volume, s32 time) {
    mFaders[Fader_Aux].set(volume, time);
}

void AudBgmVolumeController::moveNoteFairyVolume(f32 volume, s32 time) {
    mFaders[Fader_NoteFairy].set(volume, time);
}
