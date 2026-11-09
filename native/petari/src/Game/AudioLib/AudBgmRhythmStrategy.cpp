#include "Game/AudioLib/AudBgmRhythmStrategy.hpp"
#include "Game/AudioLib/AudBgm.hpp"
#include "Game/AudioLib/AudSoundInfo.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/RhythmLib/AudChordInfo.hpp"
#include "Game/RhythmLib/AudRhythmMeSystem.hpp"

bool AudBgmRhythmStrategy::set(AudBgm* pBgm, s32 bgmIndex) {
    AudSoundInfo* info = AudWrap::getSoundInfo();
    SoundHandle* handle = pBgm->getRhythmHandle();
    if (handle == nullptr) {
        return false;
    }

    const u32 soundID = handle->getSound()->getID();
    if (!handle->isSoundAttached() || !info->isRhythmBgm(soundID)) {
        return false;
    }

    AudWrap::getRhythmMeSystem()->setSeq(*handle, bgmIndex);
    mBgmIdx = bgmIndex;
    mBgm = pBgm;
    return true;
}

void AudBgmRhythmStrategy::reject() {
    if (mBgmIdx < 0) {
        return;
    }

    AudWrap::getRhythmMeSystem()->rejectSeq(mBgmIdx);
    if (isDominant()) {
        AudWrap::getSystem()->getChordInfo()->invalidate();
    }
    mBgm = nullptr;
    mBgmIdx = -1;
}

bool AudBgmRhythmStrategy::setDominant() {
    if (mBgmIdx < 0) {
        return false;
    }

    AudSoundInfo* info = AudWrap::getSoundInfo();
    SoundHandle* handle = mBgm->getRhythmHandle();
    if (handle == nullptr) {
        return false;
    }

    u32 soundID = handle->getSound()->getID();

    if (!info->isRhythmBgm(soundID)) {
        return false;
    }

    AudWrap::getRhythmMeSystem()->setUsingRhythmParser(mBgmIdx);

    u16 id = info->getChordResIdBgm(soundID);
    if (id != 0xFFFF) {
        AudWrap::getSystem()->getChordInfo()->loadChordInfo(id, false);
    } else {
        AudWrap::getSystem()->getChordInfo()->invalidate();
    }

    return true;
}

bool AudBgmRhythmStrategy::isDominant() const {
    return mBgmIdx >= 0 && mBgmIdx == AudWrap::getRhythmMeSystem()->getBgmIdx();
}

