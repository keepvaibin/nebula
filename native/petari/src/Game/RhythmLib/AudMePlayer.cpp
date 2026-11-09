#include "Game/RhythmLib/AudMePlayer.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/RhythmLib/AudChordInfo.hpp"
#include "Game/RhythmLib/AudMeHandles.hpp"
#include "Game/Sound/MeTrack.hpp"
#include "Game/System/NebulaArena.hpp"

void AudMeHandle::releaseMe() {
    if (isMeAttached()) {
        mMe->mHandle = nullptr;
        mMe = nullptr;
    }
}

AudMe::AudMe()
    : mHandle(), mLink(), mPos(), mMeId(-1), mStopTime(-1), mTimer(), mStartRequested(false), mTrackBound(),
      mStackBound(), mPlace(), mMgrVolume(1.0f), mVolume(1.0f), mOuterPan(0.5f), mOuterDolby(0.0f), mPriority(), mFadeTime() {
    initTimed();
}

AudMe::~AudMe() {
    release();
}

void AudMe::requestStart(u32 trackBound, u32 stackBound, u32 place) {
    mTrackBound = trackBound;
    mStackBound = stackBound;
    mPlace = place;
    mStartRequested = true;
}

void AudMe::start() {
    mLink.start(mTrackBound, mStackBound, AudMeMgr::getInstance()->programData(), mPlace, sound());
}

void AudMe::stop(s32 time) {
    if (time == -1) {
        if (mLink.exists()) {
            mLink.stopAtNoteEnd();
        }
        return;
    }

    if (time == -2) {
        doneStop();
        return;
    }

    if (time == 0) {
        time = mFadeTime;
    }

    if (time != 0) {
        setTimedParam(MoveParam_DistFilter, 0.0f, time);
        return;
    }

    doneStop();
}

void AudMe::release() {
    if (mLink.exists()) {
        if (mLink.status() != MeTrack::STATUS_FREE) {
            stop(0);
        }
        // stop(0) may have released it already (doneStop).
        mLink.release();
        mMeId = -1;
    }

    releaseHandle();
}

void AudMe::attachHandle(AudMeHandle* pHandle) {
    if (mHandle != nullptr) {
        releaseHandle();
    }

    mHandle = pHandle;
    mHandle->mMe = this;
}

void AudMe::releaseHandle() {
    if (mHandle == nullptr) {
        return;
    }

    mHandle->mMe = nullptr;
    mHandle = nullptr;
}

void AudMe::update() {
    if (mStartRequested) {
        start();
        mStartRequested = false;
    }

    if (mStopTime > 0) {
        mStopTime--;
        if (mStopTime <= 0) {
            stop(0);
            mStopTime = -1;
        }
    }

    if (mDistFilterParam.mValue <= 0.0f) {
        doneStop();
    }

    if (mLink.exists()) {
        updateTimedParams();
        mLink.frame(sound());

        if (mLink.status() == MeTrack::STATUS_FREE) {
            mLink.release();
        }
    }

    mTimer++;
}

void AudMe::setTimedParam(s32 param, f32 value, u32 time) {
    if (time == 0) {
        mMoveParams[param].mValue = value;
        mMoveParams[param].mDelta = 0.0f;
    } else {
        mMoveParams[param].mDelta = (value - mMoveParams[param].mValue) / time;
    }

    mMoveParams[param].mTarget = value;
}

void AudMe::initTimed() {
    mVolumeParam.mValue = 1.0f;
    mPitchParam.mValue = 1.0f;
    mPanParam.mValue = 0.5f;
    mFxMixParam.mValue = 0.0f;
    mDolbyParam.mValue = 0.0f;
    mDistFilterParam.mValue = 1.0f;

    for (s32 i = 0; i < TIMED_PARAMS; i++) {
        mMoveParams[i].mDelta = 0.0f;
        mMoveParams[i].mTarget = mMoveParams[i].mValue;
    }
}

void AudMe::updateTimedParams() {
    for (s32 i = 0; i < TIMED_PARAMS; i++) {
        if (mMoveParams[i].mDelta != 0.0f) {
            mMoveParams[i].mValue += mMoveParams[i].mDelta;
            if (mMoveParams[i].mDelta > 0.0f) {
                if (mMoveParams[i].mValue >= mMoveParams[i].mTarget) {
                    mMoveParams[i].mValue = mMoveParams[i].mTarget;
                    mMoveParams[i].mDelta = 0.0f;
                }
            } else {
                if (mMoveParams[i].mValue <= mMoveParams[i].mTarget) {
                    mMoveParams[i].mValue = mMoveParams[i].mTarget;
                    mMoveParams[i].mDelta = 0.0f;
                }
            }
        }
    }
}

SoundParams AudMe::sound() const {
    SoundParams sound;
    sound.mVolume = mMgrVolume * (mVolume * (mVolumeParam.mValue * mDistFilterParam.mValue));
    sound.mFxMix = mFxMixParam.mValue;
    sound.mPitch = mPitchParam.mValue;
    f32 pan = (mPanParam.mValue - 0.5f) + (mOuterPan - 0.5f);
    sound.mPan = pan + 0.5f;
    sound.mDolby = mDolbyParam.mValue + mOuterDolby;
    return sound;
}

void AudMe::setOuterPlayingParams(f32 volume, f32 pan, f32 dolby, s32 priority, s32 fadeTime) {
    mVolume = volume;
    mOuterPan = pan;
    mOuterDolby = dolby;
    mPriority = priority;
    mFadeTime = fadeTime;
}

void AudMe::doneStop() {
    if (mLink.exists()) {
        mLink.stop();
        release();
    }

    releaseHandle();
    mStopTime = -1;
}

void AudMe::setPauseFlag(bool pause) {
    if (mLink.exists()) {
        mLink.pause(pause);
    }
}

bool AudMe::isRequested(u32 place) const {
    if (!mLink.exists() || mLink.startPlace() != place) {
        return false;
    }
    const s32 status = mLink.status();
    return status == MeTrack::STATUS_RUN || status == MeTrack::STATUS_READY;
}

AudMeMgr::AudMeMgr(NebulaArena* pArena) : SoundGlobal< AudMeMgr >(true), mArena(pArena), mMeSeq(), mMeVolume(1.0f) {
}

void AudMeMgr::setMeSeq(const void* pData) {
    const MeProgramFile* pFile = static_cast< const MeProgramFile* >(pData);
    mMeSeq = pFile != nullptr && pFile->mMagic == MeProgramFile::cMagic && pFile->mVersion == MeProgramFile::cVersion ? pFile : nullptr;
}

void AudMeMgr::setMeTable(void* pData) {
    mParams.setResource(pData);
}

bool AudMeMgr::startMe(u32 meId, AudMeHandle* pHandle, const Vec3f* pPos) {
    if (meId == -1) {
        return false;
    }

    if (mMeSeq == nullptr) {
        return false;
    }

    if (!AudWrap::getSystem()->getChordInfo()->isAvailable()) {
        return false;
    }

    if (pHandle != nullptr && pHandle->isMeAttached()) {
        if (pHandle->getMe()->mStopTime > -1) {
            pHandle->getMe()->stop(0);
        }
    }

    AudMe* me = new (mArena, 0) AudMe();

    if (pHandle != nullptr) {
        me->attachHandle(pHandle);
    }

    if (pPos != nullptr) {
        me->mPos = pPos;
    }

    const MeEntry* pEntry = getEntry(meId);
    if (pEntry == nullptr || isRequestedMe(pEntry->mStart)) {
        if (pHandle != nullptr) {
            pHandle->releaseMe();
        }
        delete me;
        return false;
    }

    me->mMeId = meId;
    setOuterPlayingParam(meId, me);
    me->requestStart(pEntry->mTrackBound, pEntry->mStackBound, pEntry->mStart);
    mMeList.append(me);
    me->mMgrVolume = mMeVolume;
    return true;
}

// A phrase that never started, or has ended, goes. A requested phrase starts in its first update.
void AudMeMgr::freeDeadMe() {
    for (NebulaListLink< AudMe >* it = mMeList.getFirst(); it != mMeList.getEnd();) {
        NebulaListLink< AudMe >* next = it->getNext();
        if (!it->getObject()->isPlaying()) {
            mMeList.remove(it);
            delete it->getObject();
        }

        it = next;
    }
}

void AudMeMgr::updateEachMe() {
    for (NebulaListLink< AudMe >* it = mMeList.getFirst(); it != mMeList.getEnd(); it = it->getNext()) {
        if (!AudWrap::getSystem()->getChordInfo()->isAvailable()) {
            it->getObject()->stop(0);
        }

        it->getObject()->update();
    }
}

void AudMeMgr::stopAll(s32 time) {
    for (NebulaListLink< AudMe >* it = mMeList.getFirst(); it != mMeList.getEnd(); it = it->getNext()) {
        it->getObject()->stop(time);
    }
}

void AudMeMgr::setPauseFlagAll(bool pause) {
    for (NebulaListLink< AudMe >* it = mMeList.getFirst(); it != mMeList.getEnd(); it = it->getNext()) {
        it->getObject()->setPauseFlag(pause);
    }
}

void AudMeMgr::update() {
    updateEachMe();
    freeDeadMe();
}

const MeEntry* AudMeMgr::getEntry(u32 meId) {
    if (mMeSeq == nullptr || meId >= mMeSeq->mEntryCount) {
        return nullptr;
    }

    return reinterpret_cast< const MeEntry* >(mMeSeq + 1) + meId;
}

const u8* AudMeMgr::programData() const {
    return reinterpret_cast< const u8* >(reinterpret_cast< const MeEntry* >(mMeSeq + 1) + mMeSeq->mEntryCount);
}

bool AudMeMgr::isRequestedMe(u32 place) {
    for (NebulaListLink< AudMe >* it = mMeList.getFirst(); it != mMeList.getEnd(); it = it->getNext()) {
        if (it->getObject()->isRequested(place)) {
            return true;
        }
    }

    return false;
}

void AudMeMgr::setOuterPlayingParam(s32 meId, AudMe* pMe) {
    const AudMePlayingParams* params = &mParams.mParams[meId];
    f32 vol = params->mChordVolume / 127.0f;
    vol *= vol;
    f32 pan = params->mPan / 127.0f;
    f32 dolby = params->mDolby / 127.0f;
    pMe->setOuterPlayingParams(vol, pan, dolby, params->mPriority, params->mFadeTime);
}

void AudMePlayingParamsHolder::setResource(void* pRes) {
    // The ME table as the importer cooks it (host order): entry count, entry and name table offsets; nothing written.
    const s32* cursor = static_cast< const s32* >(pRes);
    const s32 numEntries = cursor[0];
    const s32 entryOff = cursor[1];
    const s32 namesOff = cursor[2];

    mNumEntries = numEntries;
    mBase = static_cast< const u8* >(pRes);
    mParams = reinterpret_cast< const AudMePlayingParams* >(mBase + entryOff);
    mNameOffsets = reinterpret_cast< const u32* >(mBase + namesOff);
}
