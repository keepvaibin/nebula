#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudSoundId.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Boss/BossStinkBug.hpp"
#include "Game/Enemy/Kanina.hpp"
#include "Game/NPC/Rabbit.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundAnimator.hpp"
#include "Game/System/NebulaArena.hpp"

AudAnmSoundObject::AudAnmSoundObject(Vec3f* pPos, u8 numHandles, NebulaArena* pArena) : AudSoundObject(pPos, numHandles, pArena), SoundAnimator(this) {
}

void AudAnmSoundObject::update(f32 time) {
    if (mPos == nullptr) {
        return;
    }

    updateAnimSound(time, *mPos, nullptr);
}

bool AudAnmSoundObject::playsSound(const SoundAnimationSound* pSound, const Vec3f& rPos, f32 speed) {
    if (!SoundAnimator::playsSound(pSound, rPos, speed)) {
        return false;
    }

    return !isLimitedSound(getSoundID(pSound, rPos, speed));
}

u32 AudAnmSoundObject::getSoundID(const SoundAnimationSound* pSound, const Vec3f& rPos, f32 speed) {
    switch (pSound->getSoundID()) {
    case SE_EM_KANINA_STEP: {
        if (mSeVersion != Kanina::SeVersion_NORMAL) {
            return SE_EM_KANINA_STEP_W;
        }
        break;
    }
    case SE_SM_RABBIT_HOP: {
        if (mSeVersion != Rabbit::SeVersion_NORMAL) {
            return SE_SM_RABBIT_HOP_WATER;
        }
        break;
    }
    case SE_SM_RABBIT_JUMP: {
        if (mSeVersion != Rabbit::SeVersion_NORMAL) {
            return SE_SM_RABBIT_JUMP_WATER;
        }
        break;
    }
    case SE_BM_BOSS_BUG_FOOT_NOTE1: {
        if (mSeVersion == BossStinkBug::SeVersion_WATER || mSeVersion == BossStinkBug::SeVersion_ANGRY_WATER) {
            return SE_BM_BOSS_BUG_FOOT_NOTE1W;
        }
        break;
    }
    case SE_BM_BOSS_BUG_FOOT_NOTE_RUN: {
        if (mSeVersion == BossStinkBug::SeVersion_ANGRY || mSeVersion == BossStinkBug::SeVersion_ANGRY_WATER) {
            return SE_BM_BOSS_BUG_FOOT_NOTE_ANGRY;
        }
        break;
    }
    case SE_PM_STAR_POWER_GET_1: {
        return -1;
        break;
    }
    case SE_SV_KPVS3_PEACH_CALL_MARIO: {
        if (AudWrap::getSceneMgr()->isPlayerModeLuigi()) {
            return SE_SV_KPVS3_PEACH_CALL_LUIGI;
        }
        break;
    }
    case SE_PM_FALLDOWN_S:
    case SE_PM_FALLDOWN_M:
    case SE_PM_FOOTNOTE_L:
    case SE_PM_FOOTNOTE_R: {
        if (mSeVersion == Mario::SeVersion_DARK) {
            return -1;
        }
        break;
    }
    }

    return convertSoundIdFromSeVersion(pSound->getSoundID());
}

SoundHandle* AudAnmSoundObject::getFreeHandle(const SoundAnimationSound* pSound) {
    return SoundAnimator::getFreeHandle(pSound);
}

void AudAnmSoundObject::skip(f32 time) {
    if (mSoundAnimation == nullptr || mSoundAnimation->getNumSounds() == 0) {
        return;
    }

    f32 speed = time - mLifeTime;
    if (!mIsReversed) {
        // Past the loop's end: back to its start without sounding the rest.
        if (speed < 0.0f) {
            speed += mLoopEndFrame - mLoopStartFrame;
            mLifeTime = time;
            mLoopSoundIndex = mLoopStartSoundIndex;
            if (mTime < 0xFFFF) {
                mTime++;
            }
        }
        updateSoundLifeTime_(time, speed);
        while (mLoopSoundIndex < mSoundAnimation->getNumSounds() && mSoundAnimation->getSound(mLoopSoundIndex)->isNotingOn(time, false)) {
            mLoopSoundIndex++;
        }
    } else {
        if (speed > 0.0f) {
            speed -= mLoopEndFrame - mLoopStartFrame;
            mLifeTime = time;
            mLoopSoundIndex = mLoopEndSoundIndex - 1;
            if (mTime < 0xFFFF) {
                mTime++;
            }
        }
        updateSoundLifeTime_(time, speed);
        while (mLoopSoundIndex >= 0 && mSoundAnimation->getSound(mLoopSoundIndex)->isNotingOn(time, true)) {
            mLoopSoundIndex--;
        }
    }

    mLifeTime = time;
}

void AudAnmSoundObject::setStartPos(f32 time) {
    if (mSoundAnimation == nullptr || mSoundAnimation->getNumSounds() == 0) {
        return;
    }

    mLifeTime = time;
    mLoopSoundIndex = mSoundAnimation->getStartSoundIndex(time);
    if (mIsReversed && mLoopSoundIndex > 0) {
        mLoopSoundIndex--;
    }
}

void AudAnmSoundObject::updateAnimSound(f32 time, const Vec3f& rPos, SoundStarter* pStarter) {
    SoundHandles* handles = getAnimatorHandles();
    for (s32 index = 0; index < handles->getNumHandles(); index++) {
        Sound* sound = handles->getSound(index);
        if (sound != nullptr && sound->getAnimationState() != 0) {
            sound->setPos(rPos);
        }
    }

    if (mSoundAnimation == nullptr || mSoundAnimation->getNumSounds() == 0) {
        return;
    }

    if (pStarter == nullptr) {
        pStarter = SoundStarter::getInstance();
    }

    f32 speed = time - mLifeTime;
    if (!mIsReversed) {
        if (speed < 0.0f) {
            speed += mLoopEndFrame - mLoopStartFrame;
            while (mLoopSoundIndex < mLoopEndSoundIndex) {
                startAnimSound(rPos, speed, pStarter);
            }
            mLifeTime = time;
            mLoopSoundIndex = mLoopStartSoundIndex;
            if (mTime < 0xFFFF) {
                mTime++;
            }
        }
        updateSoundLifeTime_(time, speed);
        while (mLoopSoundIndex < mSoundAnimation->getNumSounds() && mSoundAnimation->getSound(mLoopSoundIndex)->isNotingOn(time, false)) {
            startAnimSound(rPos, speed, pStarter);
        }
    } else {
        if (speed > 0.0f) {
            speed -= mLoopEndFrame - mLoopStartFrame;
            while (mLoopSoundIndex >= mLoopStartSoundIndex) {
                startAnimSound(rPos, speed, pStarter);
            }
            mLifeTime = time;
            mLoopSoundIndex = mLoopEndSoundIndex - 1;
            if (mTime < 0xFFFF) {
                mTime++;
            }
        }
        updateSoundLifeTime_(time, speed);
        while (mLoopSoundIndex >= 0 && mSoundAnimation->getSound(mLoopSoundIndex)->isNotingOn(time, true)) {
            startAnimSound(rPos, speed, pStarter);
        }
    }

    mLifeTime = time;
}

void AudAnmSoundObject::startAnimSound(const Vec3f& rPos, f32 speed, SoundStarter* pStarter) {
    const SoundAnimationSound* sound = mSoundAnimation->getSound(mLoopSoundIndex);
    if (mIsReversed) {
        mLoopSoundIndex--;
    } else {
        mLoopSoundIndex++;
    }

    SoundHandle* handle = getAnimatorHandles()->getHandleUserData((uintptr_t)sound);
    if (handle == nullptr) {
        handle = getFreeHandle(sound);
    }

    if (handle == nullptr) {
        return;
    }

    u32 soundID = getSoundID(sound, rPos, speed);
    if (soundID == -1) {
        return;
    }

    if (!playsSound(sound, rPos, speed)) {
        return;
    }

    if (!pStarter->startSound(soundID, handle, &rPos)) {
        return;
    }

    if (!handle->isSoundAttached()) {
        return;
    }

    handle->getSound()->setAnimationState(1);
    handle->getSound()->setUserData((uintptr_t)sound);

    setMapCodeToPort(handle, soundID);
    setCutoffToPort(handle, soundID);

    if (sound->setsLifeTime()) {
        handle->getSound()->setLifeTime(1, false);
    }

    modifySoundParams(&handle->getSound()->getAuxiliary().mParams, sound, speed);
    releaseHandleIfNecessary(handle, soundID);
}

bool AudAnmSoundObject::releaseHandleIfNecessary(SoundHandle* pHandle, u32 soundID) {
    if (pHandle == nullptr) {
        return false;
    }

    if (!pHandle->isSoundAttached()) {
        return false;
    }

    switch (soundID) {
    case SE_PV_LIBRARY_SURPRISE:
        pHandle->getSound()->releaseHandle();
        return true;
    }
    return false;
}
