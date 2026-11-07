#include "Game/System/AudSystemWrapper.hpp"
#include "Game/AudioLib/AudMeNameConverter.hpp"
#include "Game/AudioLib/AudMicWrap.hpp"
#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudSoundNameConverter.hpp"
#include "Game/AudioLib/AudSpeakerWrap.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/CSSoundNameConverter.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"

#include "Game/System/NebulaArena.hpp"
#include "Game/System/AssetFolder.hpp"

AudSystemWrapper::AudSystemWrapper(NebulaArena* pSolidHeap, NebulaArena* pHeap) {
    volatile AudSystemWrapper* self = this;

    self->mIsResetDone = false;
    self->mSolidHeap = pSolidHeap;
    self->mAudSystem = nullptr;
    self->mHeap = pHeap;
    self->mJaiSeqRes = nullptr;
    self->mJaiChordRes = nullptr;
    self->mJaiMeRes = nullptr;
    self->mJaiRemixSeqRes = nullptr;
    self->mSpkRes = nullptr;
    self->_28 = false;
    mSpkHeap = NebulaArena::create(pSolidHeap);
}

void AudSystemWrapper::requestResourceForInitialize() {
    MR::mountArchive(sJaiSeqArc, mSolidHeap);
    MR::mountArchive(sJaiChordArc, mSolidHeap);
    MR::mountArchive(sJaiMeArc, mSolidHeap);
    MR::mountArchive(AudSpeakerWrap::getResName(), mSpkHeap);
    MR::mountArchive(sJaiRemixArc, mSolidHeap);
}

void AudSystemWrapper::createAudioSystem() {
    receiveResourceForInitialize();

    if (mIsResetDone) {
        return;
    }

    mAudSystem = AudNewAudSystem(mSolidHeap, mJaiSeqRes, mJaiChordRes, mJaiMeRes, mJaiRemixSeqRes);

    mAudSystem->setSpeakerResource(mSpkRes);
    createSoundNameConverter();
    AudMicWrap::setMicEnv();
}

void AudSystemWrapper::createSoundNameConverter() {
    AudSingletonHolder< AudSoundNameConverter >::init();
    AudSingletonHolder< AudMeNameConverter >::init();
    AudSingletonHolder< CSSoundNameConverter >::init();
}

void AudSystemWrapper::movement() {
    if (mAudSystem == nullptr) {
        return;
    }

    mAudSystem->frameWork();
}

void AudSystemWrapper::stopAllSound(u32 time) {
    mAudSystem->stop(time);
}

void AudSystemWrapper::setPlayerLuigi(bool isLuigi) {
    if (mAudSystem != nullptr && mAudSystem->mSceneMgr != nullptr) {
        mAudSystem->mSceneMgr->setPlayerLuigi(isLuigi);
    }
}

void AudSystemWrapper::prepareReset() {
    if (mAudSystem == nullptr) {
        mIsResetDone = true;
    } else {
        mAudSystem->preProcessToReset();
    }
}

void AudSystemWrapper::requestReset(bool stopThreads) {
    if (mAudSystem == nullptr) {
        mIsResetDone = true;
    } else {
        mAudSystem->resetAudio(10, stopThreads);
        mAudSystem->stop(10);
    }
}

bool AudSystemWrapper::isResetDone() {
    if (mIsResetDone) {
        return true;
    }

    if (mAudSystem == nullptr) {
        return true;
    }

    return mAudSystem->hasReset();
}

void AudSystemWrapper::resumeReset() {
    if (mIsResetDone) {
        mIsResetDone = false;
    }

    if (mAudSystem == nullptr) {
        return;
    }

    return mAudSystem->resumeReset();
}

void AudSystemWrapper::receiveResourceForInitialize() {
    mJaiSeqRes = MR::getMountedArchive(sJaiSeqArc);
    mJaiChordRes = MR::getMountedArchive(sJaiChordArc);
    mJaiMeRes = MR::getMountedArchive(sJaiMeArc);
    mSpkRes = MR::getMountedArchive(AudSpeakerWrap::getResName());
    mJaiRemixSeqRes = MR::getMountedArchive(sJaiRemixArc);
}
