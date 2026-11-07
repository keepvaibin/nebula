#include "Game/Sound/SequenceSound.hpp"
#include "Game/Sound/SequenceManager.hpp"
#include "Game/Sound/SoundAudience.hpp"
#include "Game/Sound/SoundChild.hpp"
SequenceSound::SequenceSound(SequenceManager* seqMgr, SoundStrategyMgr< SequenceSound >* soundStrategyMgr) : JSULink< SequenceSound >(this) {
    mManager = seqMgr;
    mStrategyMgr = soundStrategyMgr;
    mSoundParams.init();
    mSoundStrategy = nullptr;
    for (int i = 0; i < NUM_CHILDREN; i++) {
        mSoundChild[i] = nullptr;
    }
}

SequenceSound::~SequenceSound() {
    mPlayer.release();
}

// The player is made with the sound: the tracks its program keeps alive at once and the reserved ones (two children, and
// `numTracks` numbered tracks under them).
void SequenceSound::startID_(SoundID id, const Vec3f* posPtr, SoundAudience* audience, int category, int numTracks, const SeqData& rData) {
    mCategory = category;
    const SeqFile* pFile = rData.mFile;
    const u32 reserved = 2 + static_cast< u32 >(numTracks < 0 ? 0 : numTracks > 32 ? 32 : numTracks);
    const u32 mgrCount = posPtr != nullptr && audience != nullptr ? static_cast< u32 >(audience->getMaxChannels()) : 1;
    mPlayer.create((pFile != nullptr ? pFile->mTrackBound : 1) + reserved, mgrCount, pFile != nullptr ? pFile->mNoteSlots : 1, (u32)id, 2);
    mPlayer.reserve(static_cast< u32 >(numTracks));
    startSound_(id, posPtr, audience);

    if (mStrategyMgr) {
        mSoundStrategy = mStrategyMgr->newStrategy(id);
    } else {
        mSoundStrategy = nullptr;
    }
}

void SequenceSound::playSeqData_(const SoundParams& params, SoundActivity activity) {
    mPlayer.setSeq(mSeqData.mFile, mSeqData.mEntryOp);
    if (mAudible) {
        initGroups_(&mPlayer);
    } else {
        mPlayer.setMgrCount(1);
        mPlayer.setMgrSound(0, &mSoundParams);
    }

    mixOut_(params, activity);
    mPlayer.start();
}

bool SequenceSound::prepare_getSeqData_() {
    if (getID().isAnonymous()) {
        return true;
    }

    if (!mManager->getSeqDataMgr()->getSeqData(getID(), &mSeqData)) {
        mStatus.stop();
        return false;
    }
    return true;
}

bool SequenceSound::prepare_(const SoundParams& params, SoundActivity activity) {
    switch (mStatus.getState()) {
    case SoundStatus::State_DEAD:
        break;
    case SoundStatus::State_STOP:
        stopSound_();
        break;
    case SoundStatus::State_PREPARE:
        if (prepare_getSeqData_()) {
            mStatus.setPlaying();
            playSeqData_(params, activity);
            return true;
        }

        break;
    case SoundStatus::State_LOCK_PREPARE:
        if (prepare_getSeqData_()) {
            mStatus.setReadyLocked();
        }

        return false;
    case SoundStatus::State_READY:
        mStatus.setPlaying();
        playSeqData_(params, activity);
        return true;
    case SoundStatus::State_PLAYING:
        return true;
    }

    return false;
}

void SequenceSound::managerCalc_() {
    const s32 status = mPlayer.status();
    if (status == SeqTrack::STATUS_STOPPED || (status == SeqTrack::STATUS_FREE && mStatus.isStopped())) {
        die_();
        return;
    }

    if (calcSound_()) {
        mTempoMgr.calc();
        for (int i = 0; i < NUM_CHILDREN; i++) {
            if (mSoundChild[i]) {
                mSoundChild[i]->calc();
            }
        }

        if (mSoundStrategy) {
            mSoundStrategy->calc(this);
        }
    }
}

void SequenceSound::die_() {
    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (mSoundChild[i]) {
            delete mSoundChild[i];
            mSoundChild[i] = nullptr;
        }
    }

    if (mSoundStrategy) {
        mStrategyMgr->deleteStrategy(mSoundStrategy);
        mSoundStrategy = nullptr;
    }

    mPlayer.release();
    dieSound_();
}

bool SequenceSound::tryDie_() {
    const s32 status = mPlayer.status();
    if (status == SeqTrack::STATUS_FREE || status == SeqTrack::STATUS_STOPPED) {
        die_();
        return true;
    }

    mFader.forceOut();
    mStatus.mStopping = true;
    mPlayer.stop();
    return false;
}

void SequenceSound::mixOut_(const SoundParams& params, SoundActivity activity) {
    mPlayer.setTempoRate(mTempoMgr.getTempo());
    mPlayer.mute(PlayerLink::root(), activity.isMute() || mStatus.isMute());
    mPlayer.pause(activity.isPaused() || mStatus.isPaused());

    SoundParams outParams;
    mParams.mixOutAll(params, &outParams, mFader.getIntensity());

    if (mSoundStrategy) {
        mSoundStrategy->mix(this, &outParams);
    }

    if (mAudible) {
        int maxChannels = mAudience->getMaxChannels();
        for (int i = 0; i < maxChannels; i++) {
            mAudience->mixChannelOut(outParams, mAudible, i);
        }
        sendGroups_(&mPlayer);
    } else {
        mSoundParams = outParams;
        mPlayer.setMgrSound(0, &mSoundParams);
        for (int i = 0; i < NUM_CHILDREN; i++) {
            if (mSoundChild[i]) {
                TrackPath path;
                if (getChildTrack(i, &path) != nullptr) {
                    mSoundChild[i]->mixOut(&mPlayer, path);
                }
            }
        }
    }
}

void SequenceSound::managerMixOut_(const SoundParams& params, SoundActivity activity) {
    if (prepare_(params, activity) && mPlayer.status() == SeqTrack::STATUS_RUN) {
        mixOut_(params, activity);
    }
}

s32 SequenceSound::getNumChild() const {
    return NUM_CHILDREN;
}

SoundChild* SequenceSound::getChild(int index) {
    if (mSoundChild[index] == nullptr) {
        mSoundChild[index] = new SoundChild();
    }
    return mSoundChild[index];
}

void SequenceSound::releaseChild(int index) {
    if (mPlayer.status() == SeqTrack::STATUS_RUN && mSoundChild[index]) {
        TrackPath path;
        if (getChildTrack(index, &path) != nullptr) {
            mPlayer.setTrackSound(path, nullptr);
        }

        delete mSoundChild[index];
        mSoundChild[index] = nullptr;
    }
}

PlayerLink* SequenceSound::getPlayer() {
    return mPlayer.exists() ? &mPlayer : nullptr;
}

// The game's track number: the root's child id >> 4, its child id & 15.
PlayerLink* SequenceSound::getChildTrack(int id, TrackPath* pPath) {
    const u32 index = u32(id) >> 4;
    *pPath = PlayerLink::grandchild(index, u32(id) & 15);
    return mPlayer.present(*pPath) ? &mPlayer : nullptr;
}

SequenceSound* SequenceSound::asSeq() {
    return this;
}

SoundTempo* SequenceSound::getTempoMgr() {
    return &mTempoMgr;
}
