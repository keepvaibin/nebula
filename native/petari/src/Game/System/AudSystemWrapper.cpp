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

AudSystemWrapper::AudSystemWrapper(NebulaArena* pArena) {
    mIsResetDone = false;
    mArena = pArena;
    mAudSystem = nullptr;
    mSeqArchive = nullptr;
    mChordArchive = nullptr;
    mMeArchive = nullptr;
    mRemixArchive = nullptr;
    mSpkRes = nullptr;
    mSpeakerArena = NebulaArena::create(pArena);
}

void AudSystemWrapper::createAudioSystem() {
    receiveResourceForInitialize();

    if (mIsResetDone) {
        return;
    }

    mAudSystem = AudNewAudSystem(mArena, mSeqArchive, mChordArchive, mMeArchive, mRemixArchive);

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

void AudSystemWrapper::requestReset(bool stopClock) {
    if (mAudSystem == nullptr) {
        mIsResetDone = true;
    } else {
        mAudSystem->resetAudio(10, stopClock);
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
    mSeqArchive = MR::getArchive(cSeqArchivePath, mArena);
    mChordArchive = MR::getArchive(cChordArchivePath, mArena);
    mMeArchive = MR::getArchive(cMeArchivePath, mArena);
    mSpkRes = MR::getArchive(AudSpeakerWrap::getResName(), mSpeakerArena);
    mRemixArchive = MR::getArchive(cRemixArchivePath, mArena);
}
