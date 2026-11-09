#include "Game/Sound/SoundAnimator.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundHandles.hpp"

SoundAnimation* SoundAnimation::load(void* pFile, const char* pName) {
    if (pFile == nullptr) {
        return nullptr;
    }
    SoundAnimation* pAnimation = static_cast< SoundAnimation* >(pFile);
    const char* pMagic = pAnimation->mMagic;
    if (pMagic[0] != 'N' || pMagic[1] != 'B' || pMagic[2] != 'A' || pMagic[3] != 'S') {
        NEBULA_PANIC("SoundAnimation: %s: a sound animation the importer did not cook", pName);
    }
    return pAnimation;
}

u32 SoundAnimation::getStartSoundIndex(f32 time) const {
    int idx;
    for (idx = 0; idx < getNumSounds(); idx++) {
        if (getSound(idx)->mNoteOnTime >= time) {
            break;
        }
    }
    return idx;
}

u32 SoundAnimation::getEndSoundIndex(f32 time) const {
    int idx;
    for (idx = 0; idx < getNumSounds(); idx++) {
        if (getSound(idx)->mNoteOnTime > time) {
            break;
        }
    }
    return idx;
}

SoundAnimator::SoundAnimator(SoundHandles* pHandles) : mSoundAnimation() {
    mHandles = pHandles;
    mIsReversed = false;
}

void SoundAnimator::removeAnimation() {
    mSoundAnimation = nullptr;
}

void SoundAnimator::ageSounds_() {
    SoundHandles* handles = mHandles;
    for (int idx = 0; idx < handles->mNumHandles; idx++) {
        Sound* sound = handles->getHandle(idx)->getSound();
        if (sound == nullptr) {
            continue;
        }

        switch (sound->getAnimationState()) {
        case 0:
            break;
        case 1:
            if (sound->hasLifeTime()) {
                sound->stop();
                continue;
            }
            if (((SoundAnimationSound*)sound->getUserData())->stopsWhenAnimationChanges()) {
                sound->stop();
                continue;
            }
            sound->setAnimationState(2);
            break;
        case 2:
        case 3:
            break;
        }
    }
}

void SoundAnimator::startAnimation(const SoundAnimation* pAnimation, bool reversed, f32 loopStartFrame, f32 loopEndFrame) {
    ageSounds_();
    mSoundAnimation = pAnimation;
    if (mSoundAnimation == nullptr) {
        return;
    }

    mIsReversed = reversed;
    mTime = 0;

    if (mIsReversed) {
        mLoopSoundIndex = mSoundAnimation->getNumSounds() - 1;
        mLifeTime = FLOAT_MAX;
    } else {
        mLoopSoundIndex = 0;
        mLifeTime = 0.0f;
    }

    setLoopFrame(loopStartFrame, loopEndFrame);
}

void SoundAnimator::updateSoundLifeTime_(f32 time, f32 speed) {
    if (mSoundAnimation == nullptr) {
        return;
    }

    SoundHandles* handles = mHandles;

    for (int idx = 0; idx < handles->mNumHandles; idx++) {
        if (handles->getSound(idx) == nullptr) {
            continue;
        }

        switch (handles->getSound(idx)->getAnimationState()) {
        case 0:
            continue;
        case 1:
            break;
        case 2:
        case 3:
            continue;
        default:
            continue;
        }

        SoundAnimationSound* anim = (SoundAnimationSound*)handles->getSound(idx)->getUserData();

        bool speedZeroStop = speed == 0.0f && anim->stopsWhenSpeedIsZero();
        modifySoundParams(&handles->getSound(idx)->mParams.getMoveParams(), anim, speed);

        if (anim->setsLifeTime()) {
            if (speedZeroStop) {
                handles->getSound(idx)->pause(true);
                continue;
            }

            if (anim->isNoting(time)) {
                if (handles->getSound(idx)->mLifeTime < 1) {
                    handles->getSound(idx)->mLifeTime = 1;
                }

                handles->getSound(idx)->pause(false);
            } else {
                handles->getSound(idx)->stop();
            }
        } else {
            if (speedZeroStop) {
                handles->getSound(idx)->stop();
                continue;
            }

            if (anim->stopsWhenNoteOff() && anim->isNotingOff(time, mIsReversed)) {
                handles->getSound(idx)->stop();
            }
        }
    }
}

SoundHandle* SoundAnimator::getFreeHandle(const SoundAnimationSound* pAnimation) {
    for (int idx = 0; idx < mHandles->mNumHandles; idx++) {
        if (mHandles->getSound(idx) == nullptr) {
            return mHandles->getHandle(idx);
        }
    }
    return nullptr;
}

bool SoundAnimator::playsSound(const SoundAnimationSound* pAnimation, const Vec3f& rPos, f32 speed) {
    if (pAnimation->playsOnlyOnce()) {
        if (mTime != pAnimation->mPlayTime) {
            return false;
        }
    } else if (pAnimation->playsAtIntervals() && pAnimation->mPlayTime != (pAnimation->mRepeatInterval != 0 ? mTime % pAnimation->mRepeatInterval : mTime)) {
        return false;
    }

    if (mIsReversed) {
        if (pAnimation->playsOnlyForward()) {
            return false;
        }
    } else {
        if (pAnimation->playsOnlyReverse()) {
            return false;
        }
    }

    return true;
}

void SoundAnimator::modifySoundParams(SoundParams* pParams, const SoundAnimationSound* pAnimation, f32 time) {
    if (time > 3.0f) {
        time = 3.0f;
    }
    time = time < 0.0f ? -time - 1.0f : time - 1.0f;

    f32 volume = pAnimation->mBaseVolume;
    if (pAnimation->mVolumeDelta != 0) {
        volume += pAnimation->mVolumeDelta * 2.0f * time;
    }

    pParams->mVolume = volume * (1.0f / 127.0f);

    if (pAnimation->mPitchDelta != 0) {
        pParams->mPitch = pAnimation->mBasePitch + pAnimation->mPitchDelta * time * (1.0f / 32.0f);
    } else {
        pParams->mPitch = pAnimation->mBasePitch;
    }

    pParams->mPan = pAnimation->mBasePan * (1.0f / 127.0f);
}
