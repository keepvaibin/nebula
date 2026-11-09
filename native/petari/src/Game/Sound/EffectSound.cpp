#include "Game/Sound/EffectSound.hpp"
#include "Game/Sound/EffectManager.hpp"
#include "Game/Sound/SoundAudience.hpp"

EffectSound::EffectSound(EffectManager* seMgr, u32 priority) {
    mManager = seMgr;
    mProperPriority = priority;
    mTrackStarted = false;
}

EffectSound::~EffectSound() {
    mPlayer.release();
}

// The groups' values reach the player once its track has started (they were the track's buffers from then).
void EffectSound::mixOut_(const SoundParams& params) {
    mPlayer.setTempoRate(mTempoMgr.getTempo());
    SoundParams mixParams;
    mParams.mixOutAll(params, &mixParams, mFader.getIntensity());

    if (mAudible != nullptr) {
        for (s32 i = 0; i < mAudible->getChannelCount(); i++) {
            mAudience->mixChannelOut(mixParams, mAudible, i);
        }
        if (mTrackStarted) {
            sendGroups_(&mPlayer);
        }
    } else {
        mSoundParams = mixParams;
        if (mTrackStarted) {
            mPlayer.setMgrSound(0, &mSoundParams);
        }
    }
}

void EffectSound::stopTrack_() {
    if (mPlayer.status() == SeqTrack::STATUS_RUN) {
        mPlayer.stop();
    }
}

void EffectSound::startTrack_(const SoundParams& params) {
    if (mPlayer.status() == SeqTrack::STATUS_STOPPED) {
        mPlayer.init();
    }

    if (mAudible != nullptr) {
        initGroups_(&mPlayer);
    } else {
        mParams.mixOutAll(params, &mSoundParams, mFader.getIntensity());
        mPlayer.setMgrCount(1);
        mPlayer.setMgrSound(0, &mSoundParams);
    }

    mTrackStarted = true;
    mPlayer.setSeq(mSeqData.mFile, mSeqData.mEntryOp);
    mPlayer.start();
}

void EffectSound::categoryMixOut_(bool active, const SoundParams& params, SoundActivity activity) {
    mixOut_(params);
    if (isStopping()) {
        stopTrack_();
        return;
    }

    if (active) {
        if (isPlaying()) {
            if (mTrackStarted) {
                switch (mPlayer.status()) {
                case SeqTrack::STATUS_STOPPED:
                    if (comesBack()) {
                        startTrack_(params);
                    } else {
                        stopSound_();
                    }
                    break;
                case SeqTrack::STATUS_RUN:
                    mPlayer.pause(isPaused() || activity.isPaused());
                    mPlayer.mute(PlayerLink::root(), isMute() || activity.isMute());
                    break;
                }
            } else {
                startTrack_(params);
            }
        }
        return;
    }

    if (comesBack()) {
        if (isPauseWhenOut()) {
            mPlayer.pause(true);
        } else {
            stopTrack_();
        }
    } else {
        stopTrack_();
        stopSound_();
    }
}

void EffectSound::categoryCalc_() {
    prepare_();

    if (calcSound_()) {
        mTempoMgr.calc();
        if (mManager->mSeCalc != nullptr) {
            mManager->mSeCalc(this);
        }
    }

    if (isStopping() || mPriority == -1) {
        mCategoryPriority = -1;
    } else {
        mCategoryPriority = mProperPriority + mPriority;
    }
}

bool EffectSound::tryDie_() {
    switch (mPlayer.status()) {
    case SeqTrack::STATUS_FREE:
    case SeqTrack::STATUS_STOPPED:
        dieSound_();
        return true;
    case SeqTrack::STATUS_RUN:
        break;
    }
    return false;
}

// The player is made with the sound (the track was part of it): what the game writes to it before it starts holds.
void EffectSound::startID_(SoundID id, const Vec3f* posPtr, SoundAudience* audience) {
    startSound_(id, posPtr, audience);
    const SeqFile* pFile = mManager->getSeqDataMgr() != nullptr ? mManager->getSeqDataMgr()->program() : nullptr;
    mPlayer.create(pFile != nullptr ? pFile->mTrackBound : 1, mAudible != nullptr ? mAudible->getChannelCount() : 1,
                   TrackRoom::of(pFile), 2, (u32)id, 1);
    mTrackStarted = false;
}

s32 EffectSound::getNumChild() const {
    return 0;
}

SoundChild* EffectSound::getChild(int) {
    return nullptr;
}

void EffectSound::releaseChild(int) {
}

bool EffectSound::prepare_getSeqData_() {
    if (!mManager->getSeqDataMgr()->getSeqData(getID(), &mSeqData)) {
        mStatus.stop();
        return false;
    }
    return true;
}

void EffectSound::prepare_() {
    switch (mStatus.getState()) {
    case SoundStatus::State_DEAD:
        break;
    case SoundStatus::State_STOP:
        stopSound_();
        break;
    case SoundStatus::State_PREPARE:
        if (prepare_getSeqData_()) {
            mStatus.setPlaying();
        }
        break;
    case SoundStatus::State_LOCK_PREPARE:
        if (prepare_getSeqData_()) {
            mStatus.setReadyLocked();
        }
        break;
    case SoundStatus::State_READY:
        mStatus.setPlaying();
        break;
    case SoundStatus::State_LOCK_READY:
    case SoundStatus::State_PLAYING:
        break;
    }
}

PlayerLink* EffectSound::getPlayer() {
    return &mPlayer;
}

PlayerLink* EffectSound::getChildTrack(int index, TrackPath* pPath) {
    *pPath = PlayerLink::child(static_cast< u32 >(index));
    return mPlayer.present(*pPath) ? &mPlayer : nullptr;
}

EffectSound* EffectSound::asSe() {
    return this;
}

SoundTempo* EffectSound::getTempoMgr() {
    return &mTempoMgr;
}
