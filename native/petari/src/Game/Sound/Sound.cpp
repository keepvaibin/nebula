#include "Game/Sound/Sound.hpp"
#include <nebula_module.h>
#include "Game/Sound/SoundAudience.hpp"
#include "Game/Sound/SoundHandles.hpp"
#include "Game/Sound/PlayerLink.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_mixOutAll_f1, "mixOutAll.f1", 0.5f);

void SoundHandle::releaseSound() {
    if (mSound != nullptr) {
        mSound->mHandle = nullptr;
        mSound = nullptr;
    }
}

void Sound::releaseHandle() {
    if (mHandle != nullptr) {
        mHandle->mSound = nullptr;
        mHandle = nullptr;
    }
}

void Sound::attachHandle(SoundHandle* handle) {
    if (handle->isSoundAttached()) {
        handle->getSound()->stop();
    }
    if (isHandleAttached()) {
        releaseHandle();
    }
    mHandle = handle;
    mHandle->mSound = this;
}

s32 SoundStatus::lockWhenPrepared() {
    if (getState() == State_PREPARE) {
        setState(State_LOCK_PREPARE);
        return 1;
    }
    return 0;
}

s32 SoundStatus::unlockIfLocked() {
    if (getState() == State_LOCK_READY) {
        setState(State_READY);
        return 1;
    }
    if (getState() == State_LOCK_PREPARE) {
        setState(State_PREPARE);
        return 1;
    }
    return 0;
}

void SoundControls::mixOutAll(const SoundParams& inParams, SoundParams* outParams, f32 fade) {
    outParams->mVolume = mMove.mParams.mVolume * (inParams.mVolume * mProperty.mVolume) * fade;
    outParams->mFxMix = mMove.mParams.mFxMix + (inParams.mFxMix + mProperty.mFxMix);
    outParams->mPitch = mMove.mParams.mPitch * (inParams.mPitch * mProperty.mPitch);
    outParams->mPan = (inParams.mPan + mMove.mParams.mPan) - cNum_mixOutAll_f1;
    outParams->mDolby = inParams.mDolby + mMove.mParams.mDolby;
}

Sound::Sound() : mParams() {
}

void Sound::startSound_(SoundID id, const Vec3f* posPtr, SoundAudience* audience) {
    mHandle = nullptr;
    mSoundID = id;
    mStatus.init();
    mParams.init();
    mFader.forceIn();
    mAudience = audience;
    mPrepareCount = 0;
    mCount = 0;

    if (posPtr != nullptr && mAudience != nullptr && mAudience->startAudible(&mAudibleData, *posPtr, mSoundID)) {
        mAudible = &mAudibleData;
    } else {
        mAudible = nullptr;
    }

    mPriority = 0;
}

void Sound::stop(u32 fadeTime) {
    if (fadeTime == 0) {
        stop();
    } else {
        mFader.fadeOut(fadeTime);
        removeLifeTime_();
        mStatus.mFadingOut = true;
        mStatus.mStopping = true;
    }
}

void Sound::stop() {
    stopSound_();
    releaseHandle();
}

EffectSound* Sound::asSe() {
    return nullptr;
}

SequenceSound* Sound::asSeq() {
    return nullptr;
}

StreamSound* Sound::asStream() {
    return nullptr;
}

void Sound::dieSound_() {
    if (mAudible != nullptr) {
        mAudible = nullptr;
        mAudience = nullptr;
    }
    mFader.forceOut();
    releaseHandle();
    die();
}

bool Sound::calcSound_() {
    setCalcedOnce(true);
    if (isStopping() && tryDie_()) {
        return false;
    }

    bool playing = isPlaying();
    if (playing) {
        mCount++;
        // An effect nothing can stop any more (no handle holds it, it has no lifetime) and that has played 30 s is an
        // orphaned loop: its owner went without stopping it, and it would play on through everything until the game
        // stopped every sound. It fades out as a stopped sound does, and names itself.
        if (mCount == 1800 && asSe() != nullptr) {
            if (mHandle == nullptr && !hasLifeTime() && !isStopping()) {
                nebula_log("audio: orphaned sound %08x stopped after 30 s\n", (u32)mSoundID);
                stop(30);
            } else {
                nebula_log("audio: sound %08x alive 30 s (lifetime %s, handle %s)\n", (u32)mSoundID,
                         hasLifeTime() ? "on" : "off", isHandleAttached() ? "held" : "none");
            }
        }
    }

    bool paused = mStatus.isPaused();
    if (!paused) {
        mFader.calc();
    }

    bool currentlyPlaying = playing && !paused;
    if (currentlyPlaying) {
        mParams.mMove.calc();
        if (mAudible != nullptr) {
            mAudible->calc();
        }
        if (hasLifeTime()) {
            if (mLifeTime == 0) {
                stopSound_();
            } else {
                mLifeTime--;
            }
        }
    }

    if (mAudience != nullptr && mAudible != nullptr) {
        mPriority = mAudience->calcPriority(mAudible);
        if (mPriority == -1 && !comesBack()) {
            stopSound_();
        }
    } else {
        mPriority = 0;
    }

    return currentlyPlaying;
}

void Sound::initGroups_(PlayerLink* pLink) {
    pLink->setMgrCount(static_cast< u32 >(mAudible->getChannelCount()));
    sendGroups_(pLink);
}

// The audible's channel values as the audience mixed them this frame, a group each in channel order.
void Sound::sendGroups_(PlayerLink* pLink) {
    for (s32 i = 0; i < mAudible->getChannelCount(); i++) {
        pLink->setMgrSound(static_cast< u32 >(i), &mAudible->getOuterParams(i));
    }
}

void Sound::increasePrepareCount_() {
    ++mPrepareCount;
}
