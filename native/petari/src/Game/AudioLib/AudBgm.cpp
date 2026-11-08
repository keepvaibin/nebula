#include "Game/AudioLib/AudBgm.hpp"
#include "Game/AudioLib/AudBgmMgr.hpp"
#include "Game/AudioLib/AudBgmSetting.hpp"
#include "Game/AudioLib/AudBgmVolumeController.hpp"
#include "Game/AudioLib/AudFader.hpp"
#include "Game/AudioLib/AudSoundId.hpp"
#include "Game/AudioLib/AudSoundInfo.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/RhythmLib/AudRhythmMeSystem.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundChild.hpp"
#include "Game/Sound/StreamSound.hpp"

namespace {
    // Each track to the table's state for the step: two tracks a byte, the even one in the high nibble, bit 3 muting on its
    // own and bits 0-2 the fade state. A song with no state for the step fades every track in, but a rhythm song's track 0.
    void setTrackMuteStates(AudTrackController* pTracks, s32 trackNum, const AudBgmSetting::MuteState* pState, bool isRhythm, s32 time) {
        if (pState == nullptr) {
            for (s32 i = 0; i < trackNum; i++) {
                if (i != 0 || !isRhythm) {
                    pTracks[i].setMuteState(AudFader::FadeState_FadeIn, time, false);
                }
            }
            return;
        }

        for (s32 i = 0; i < trackNum; i++) {
            u8 state = pState->mMuteStates[i / 2];
            if (i % 2 == 0) {
                state >>= 4;
            }
            pTracks[i].setMuteState(state & 0x7, time, (state & 0x8) > 0);
        }
    }
}  // namespace

AudBgm::AudBgm() : mVolumeController() {
}

void AudBgm::resetAuxVolume() {
    if (mVolumeController == nullptr) {
        return;
    }

    mVolumeController->moveAuxVolume(1.0f, 0);
    mVolumeController->moveNoteFairyVolume(1.0f, 0);
}

AudSingleBgm::AudSingleBgm() : AudBgm(), mHandle(), mSoundID(-1) {
    init();
}

void AudSingleBgm::init() {
    mSoundID.setAnonymous();
    initTrackController();
}

void AudSingleBgm::movement() {
    updateTrackControl();
    if (!mHandle.isSoundAttached()) {
        resetAuxVolume();
        return;
    }

    if (mVolumeController != nullptr) {
        mHandle->getAuxiliary().moveVolume(mVolumeController->getVolume(), 0);
    }
}

SoundHandle* AudSingleBgm::start(u32 soundID, bool lock) {
    SoundID id = soundID;
    mSoundID = soundID;

    if (mHandle.isSoundAttached()) {
        mHandle->stop();
    }

    resetAuxVolume();

    if (id.getSectionID() == SoundID::SOUND_STREAM) {
        AudWrap::getSystem()->getStreamMgr().startSound(id, &mHandle, nullptr);
    } else {
        AudWrap::getSystem()->getSeqMgr().startSound(id, &mHandle, nullptr);
        if (mHandle.isSoundAttached()) {
            startTrackControl();
            changeTrackMuteState(0, 0);
        }
    }

    if (mHandle.isSoundAttached()) {
        if (mVolumeController != nullptr) {
            mHandle->getAuxiliary().moveVolume(mVolumeController->getVolume(), 0);
        }

        if (lock) {
            mHandle->lockWhenPrepared();
        }
    }

    return &mHandle;
}

void AudSingleBgm::stop(u32 time) {
    if (!mHandle.isSoundAttached()) {
        return;
    }

    mHandle->stop(time);

    if (mSoundID.getSectionID() != SoundID::SOUND_STREAM) {
        stopTrackControl();
    }
}

bool AudSingleBgm::isPreparedPlay() {
    if (mHandle.isSoundAttached()) {
        return mHandle->isPrepared();
    }

    return false;
}

void AudSingleBgm::playAfterPrepared() {
    if (mHandle.isSoundAttached()) {
        mHandle->unlockIfLocked();
    }
}

SoundHandle* AudSingleBgm::getRhythmHandle() {
    if (mHandle.isSoundAttached()) {
        if (mHandle->getID().getSectionID() == SoundID::SOUND_SEQ) {
            return &mHandle;
        }
    }

    return nullptr;
}

bool AudSingleBgm::moveVolume(f32 volume, u32 time) {
    if (!mHandle.isSoundAttached()) {
        return false;
    }

    if (mVolumeController != nullptr) {
        mVolumeController->moveAuxVolume(volume, time);
        return true;
    }

    return false;
}

bool AudSingleBgm::moveVolumeForNoteFairy(f32 volume, u32 time) {
    if (!mHandle.isSoundAttached()) {
        return false;
    }

    if (mVolumeController != nullptr) {
        mVolumeController->moveNoteFairyVolume(volume, time);
        return true;
    }

    return false;
}

void AudSingleBgm::changeTrackMuteState(s32 track, s32 time) {
    if (mSoundID.getSectionID() == SoundID::SOUND_STREAM) {
        return;
    }

    const bool isRhythm = AudWrap::getSoundInfo()->isRhythmBgm(mSoundID);
    ::setTrackMuteStates(mTrackController, mNumTracks, AudBgmSetting::getMuteState(mSoundID, track), isRhythm, time);
}

void AudSingleBgm::initTrackController() {
    for (s32 i = 0; i < mNumTracks; i++) {
        mTrackController[i].mTrackNo = i;
    }
}

void AudSingleBgm::startTrackControl() {
    for (s32 i = 0; i < mNumTracks; i++) {
        mTrackController[i].start(&mHandle);
    }
}

void AudSingleBgm::stopTrackControl() {
    for (s32 i = 0; i < mNumTracks; i++) {
        mTrackController[i].stop();
    }
}

void AudSingleBgm::updateTrackControl() {
    if (mSoundID.getSectionID() == SoundID::SOUND_STREAM) {
        return;
    }

    for (s32 i = 0; i < mNumTracks; i++) {
        mTrackController[i].update();
    }
}

AudMultiBgm::AudMultiBgm() : AudBgm(), mHandle(), mRhythmHandle(), mSyncState(), mBgmId(-1), mIsLocked() {
    init();
}

void AudMultiBgm::init() {
    initTrackController();
    for (s32 i = 0; i < mNumFaders; i++) {
        mFader[i].set(1.0f, 0);
    }
}

SoundHandle* AudMultiBgm::start(u32 soundID, bool lock) {
    if (!isStopping()) {
        stop(0);
    }

    mIsLocked = lock;
    return prepare(soundID);
}

void AudMultiBgm::stop(u32 time) {
    if (mRhythmHandle.isSoundAttached()) {
        mRhythmHandle->stop(time);
    }

    if (mHandle.isSoundAttached()) {
        mHandle->stop(time);
    }

    mSyncState = 0;
    mBgmId = -1;
}

bool AudMultiBgm::isPreparedPlay() {
    return isPrepared();
}

void AudMultiBgm::playAfterPrepared() {
    mIsLocked = false;
}

void AudMultiBgm::sendToSyncStream() {
    if (!mRhythmHandle.isSoundAttached()) {
        return;
    }

    if (!mHandle.isSoundAttached()) {
        return;
    }

    AudRhythmMeSystem* meSystem = AudRhythmMeSystem::getInstance();
    s32 idx = mRhythmStrategy.mBgmIdx;
    if (idx >= 0) {
        f32 beatMul = AudBgmSetting::getBeatMulForMultiBgm(mBgmId);
        u32 introBeats = AudBgmSetting::getIntroBeatsForMultiBgm(mBgmId);
        u32 loopBeats = AudBgmSetting::getLoopBeatsForMultiBgm(mBgmId);
        u32 loopStartSamples = AudBgmSetting::getLoopStartSamplesForMultiBgm(mBgmId);
        u32 loopEndSamples = AudBgmSetting::getLoopEndSamplesForMultiBgm(mBgmId);
        StreamSound* stream = mHandle->asStream();

        meSystem->setTempoStream(idx, stream != nullptr ? stream->player() : nullptr, beatMul, introBeats, loopBeats, loopStartSamples,
                                 loopEndSamples);
    }
}

void AudMultiBgm::rejectFromSyncStream() {
    AudRhythmMeSystem* meSystem = AudRhythmMeSystem::getInstance();
    s32 idx = mRhythmStrategy.mBgmIdx;
    if (idx >= 0) {
        meSystem->rejectTempoStream(idx);
    }
}

void AudMultiBgm::movement() {
    updateSyncProcess();
    updateTrackControl();

    for (s32 i = 0; i < mNumFaders; i++) {
        mFader[i].update();
    }

    f32 volume = 1.0f;
    if (mVolumeController != nullptr) {
        volume = mVolumeController->getVolume();
    }

    if (!mHandle.isSoundAttached()) {
        if (!mRhythmHandle.isSoundAttached()) {
            resetAuxVolume();
            if (mVolumeController != nullptr) {
                for (s32 i = 0; i < mNumFaders; i++) {
                    mFader[i].set(1.0f, 0);
                }
            }
        }
    } else {
        f32 volA = volume * mFader[0].getVolume();
        f32 volB = volume * mFader[1].getVolume();
        setStreamVolume(volA, volB);
    }

    if (mRhythmHandle.isSoundAttached()) {
        mRhythmHandle->getAuxiliary().moveVolume(volume, 0);
    }
}

bool AudMultiBgm::moveVolume(f32 volume, u32 time) {
    if (!mHandle.isSoundAttached() && !mRhythmHandle.isSoundAttached()) {
        return false;
    }

    if (mVolumeController != nullptr) {
        mVolumeController->moveAuxVolume(volume, time);
        return true;
    }

    return false;
}

bool AudMultiBgm::moveVolumeForNoteFairy(f32 volume, u32 time) {
    if (!mHandle.isSoundAttached() && !mRhythmHandle.isSoundAttached()) {
        return false;
    }

    if (mVolumeController != nullptr) {
        mVolumeController->moveNoteFairyVolume(volume, time);
        return true;
    }

    return false;
}

void AudMultiBgm::changeTrackMuteState(s32 track, s32 time) {
    if (!mRhythmHandle.isSoundAttached()) {
        return;
    }

    SoundID soundID = mRhythmHandle->getID();

    if (soundID.getSectionID() == SoundID::SOUND_STREAM) {
        return;
    }

    const bool isRhythm = AudWrap::getSoundInfo()->isRhythmBgm(soundID);
    const AudBgmSetting::MuteState* muteState = AudBgmSetting::getMuteState(soundID, track);
    if (muteState == nullptr) {
        ::setTrackMuteStates(mTrackController, mNumTracks, nullptr, isRhythm, time);
        return;
    }

    // BGM_GALAXY_01's step 1 fades three times as slowly.
    if (soundID == BGM_GALAXY_01 && track == 1) {
        time *= 3;
    }

    ::setTrackMuteStates(mTrackController, mNumTracks, muteState, isRhythm, time);

    u8 volA = muteState->mVolumeA;
    if (volA != 0xFF) {
        mFader[0].set(volA / 254.0f, time);
    }

    u8 volB = muteState->mVolumeB;
    if (volB != 0xFF) {
        mFader[1].set(volB / 254.0f, time);
    }

    if (track != 0) {
        u16 extraChordNum = AudBgmSetting::getExtraChordNum(soundID, track);
        if (extraChordNum != 0xFFFF) {
            AudWrap::getSystem()->getChordInfo()->loadChordInfo(extraChordNum, true);
        }
    }
}

SoundHandle* AudMultiBgm::getHandle() {
    return &mHandle;
}

SoundHandle* AudMultiBgm::getRhythmHandle() {
    return &mRhythmHandle;
}

bool AudMultiBgm::isSoundAttached() const {
    return mHandle.isSoundAttached();
}

void AudMultiBgm::pause(bool pause) {
    if (mHandle.isSoundAttached()) {
        mHandle->pause(pause);
    }
}

bool AudMultiBgm::isStopping() const {
    if (mHandle.isSoundAttached()) {
        return mHandle->isStopping();
    }

    return true;
}

bool AudMultiBgm::isPaused() const {
    if (mHandle.isSoundAttached()) {
        return mHandle->isPaused();
    }

    return false;
}

SoundID AudMultiBgm::getSoundID() const {
    if (!mHandle.isSoundAttached()) {
        return 0;
    }

    return mHandle->getID();
}

void AudMultiBgm::initTrackController() {
    for (s32 i = 0; i < mNumTracks; i++) {
        mTrackController[i].mTrackNo = i;
    }
}

void AudMultiBgm::startTrackControl() {
    for (s32 i = 0; i < mNumTracks; i++) {
        mTrackController[i].start(&mRhythmHandle);
    }
}

void AudMultiBgm::updateTrackControl() {
    for (s32 i = 0; i < mNumTracks; i++) {
        mTrackController[i].update();
    }
}

SoundHandle* AudMultiBgm::prepare(u32 id) {
    u32 bgmId = id & ~(0x01010000);

    u32 seqID = AudBgmSetting::getSeqIdForMultiBgm(bgmId);
    u32 streamID = AudBgmSetting::getStreamIdForMultiBgm(bgmId);

    AudWrap::getSystem()->getSeqMgr().startSound(seqID, &mRhythmHandle, nullptr);
    if (mRhythmHandle.isSoundAttached()) {
        mRhythmHandle->lockWhenPrepared();

        if (mVolumeController != nullptr) {
            mRhythmHandle->getAuxiliary().moveVolume(mVolumeController->getVolume(), 0);
        }

        startTrackControl();
        changeTrackMuteState(AudFader::FadeState_FadeOut, 0);
    } else {
        return nullptr;
    }

    AudWrap::getSystem()->getStreamMgr().startSound(streamID, &mHandle, nullptr);
    if (mHandle.isSoundAttached()) {
        mHandle->lockWhenPrepared();

        if (mVolumeController != nullptr) {
            f32 volA = mVolumeController->getVolume() * mFader[0].getVolume();
            f32 volB = mVolumeController->getVolume() * mFader[1].getVolume();
            setStreamVolume(volA, volB);
        }
    } else {
        mRhythmHandle->stop();
        return nullptr;
    }

    mBgmId = bgmId;
    mSyncState = 1;
    return &mHandle;
}

bool AudMultiBgm::isPrepared() {
    bool rhythmHandlePrepared = true;
    bool handlePrepared = false;

    if (mRhythmHandle.isSoundAttached()) {
        rhythmHandlePrepared = mRhythmHandle->isPrepared();
    }

    if (mHandle.isSoundAttached()) {
        handlePrepared = mHandle->isPrepared();
    }

    return rhythmHandlePrepared && handlePrepared;
}

void AudMultiBgm::unlock() {
    if (mIsLocked) {
        return;
    }

    if (mRhythmHandle.isSoundAttached()) {
        mRhythmHandle->unlockIfLocked();
    }

    if (mHandle.isSoundAttached()) {
        mHandle->unlockIfLocked();
    }

    mSyncState = 2;
}

void AudMultiBgm::updateSyncProcess() {
    pauseSyncProcess();

    if (mSyncState == 1) {
        if (isPrepared()) {
            unlock();
        }
    }
}

void AudMultiBgm::pauseSyncProcess() {
    bool isPaused = false;
    if (mHandle.isSoundAttached()) {
        isPaused = mHandle->asStream()->isPaused();
    }

    if (isPaused) {
        if (mRhythmHandle.isSoundAttached()) {
            mRhythmHandle->pause(true);
        }
    } else {
        if (mRhythmHandle.isSoundAttached()) {
            mRhythmHandle->pause(false);
        }
    }
}

void AudMultiBgm::setStreamVolume(f32 volA, f32 volB) {
    mHandle->getChild(0)->getMoveParams()->moveVolume(volA, 0);
    mHandle->getChild(1)->getMoveParams()->moveVolume(volA, 0);
    mHandle->getChild(2)->getMoveParams()->moveVolume(volB, 0);
    mHandle->getChild(3)->getMoveParams()->moveVolume(volB, 0);
}
