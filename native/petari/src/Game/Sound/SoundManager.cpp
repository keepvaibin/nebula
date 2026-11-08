#include "Game/Sound/SoundManager.hpp"
#include "Game/Sound/SoundAudience.hpp"
#include "Game/Sound/Sound.hpp"

SoundManager::SoundManager() : SoundStarter(true), mSeMgr(true), mSeqMgr(true), mStreamMgr(true) {
}

bool SoundManager::startSound(SoundID soundID, SoundHandle* pHandle, const Vec3f* pPos) {
    switch (soundID.getSectionID()) {
    case SoundID::SOUND_SE:
        return mSeMgr.startSound(soundID, pHandle, pPos);
    case SoundID::SOUND_SEQ:
        return mSeqMgr.startSound(soundID, pHandle, pPos);
    case SoundID::SOUND_STREAM:
        return mStreamMgr.startSound(soundID, pHandle, pPos);
    }

    return false;
}

void SoundManager::setAudience(SoundAudience* pAudience) {
    mSeMgr.setAudience(pAudience);
    mSeqMgr.setAudience(pAudience);
    mStreamMgr.setAudience(pAudience);
}

void SoundManager::calc() {
    mSeMgr.calc();
    mSeqMgr.calc();
    mStreamMgr.calc();
}

void SoundManager::mixOut() {
    mSeMgr.mixOut();
    mSeqMgr.mixOut();
    mStreamMgr.mixOut();
}

void SoundManager::stopSoundID(SoundID soundID) {
    mSeMgr.stopSoundID(soundID);
    mSeqMgr.stopSoundID(soundID);
    mStreamMgr.stopSoundID(soundID);
}

void SoundManager::stop(u32 fadeTime) {
    mSeMgr.stop(fadeTime);
    mSeqMgr.stop(fadeTime);
    mStreamMgr.stop(fadeTime);
}

void SoundManager::initParams() {
    mSeMgr.initParams();
    mSeqMgr.initParams();
    mStreamMgr.initParams();
}

int SoundManager::getNumActiveSounds() const {
    return mSeMgr.getNumActiveSe() + mSeqMgr.getNumActiveSeqs() + mStreamMgr.getNumActiveStreams();
}
