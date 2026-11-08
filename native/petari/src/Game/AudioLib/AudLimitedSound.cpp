#include "Game/AudioLib/AudLimitedSound.hpp"

AudLimitedSoundInfo::AudLimitedSoundInfo() {
    init();
}

void AudLimitedSoundInfo::init() {
    mSoundID.setAnonymous();
    mDelay = 0;
}

void AudLimitedSoundInfo::set(SoundID soundID, s32 delay) {
    mSoundID = soundID;
    mDelay = delay;
}

void AudLimitedSoundInfo::update() {
    if (!isFree() && --mDelay <= 0) {
        init();
    }
}

bool AudLimitedSoundInfo::isFree() {
    return mSoundID.isAnonymous();
}
