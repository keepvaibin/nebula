#pragma once

#include "Game/Sound/SeqSource.hpp"
#include "Game/Sound/SoundGlobal.hpp"
#include "JSystem/JSupport/JSUList.hpp"

class SoundAudience;
class SequenceSound;

class SequenceManager : public SoundGlobal< SequenceManager >, public SoundActivityOwner {
public:
    SequenceManager(bool setInstance);
    void freeDeadSeq_();
    bool startSound(SoundID id, SoundHandle* handle, const Vec3f* posPtr);
    void calc();
    void stop();
    void stop(u32 fadeTime);
    void stopSoundID(SoundID id);
    void mixOut();
    SequenceSound* beginStartSeq_();

    void initParams() {
        mMove.init();
    }

    bool endStartSeq_(SequenceSound* seq, SoundHandle* handle);


    void setAudience(SoundAudience* audience) {
        mAudience = audience;
    }
    SoundAudience* getAudience() {
        return mAudience;
    }

    SeqSource* getSeqDataMgr() {
        return seqDataMgr;
    }

    void setSeqDataMgr(SeqSource* seqDataMgr_) {
        seqDataMgr = seqDataMgr_;
    }

    SoundStrategyMgr< SequenceSound >* getSoundStrategyMgr() {
        return soundStrategyMgr;
    }

    SoundMove* getParams() {
        return &mMove;
    }
    bool isActive() const {
        return mSeqList.getNumLinks() != 0;
    }
    int getNumActiveSeqs() const {
        return mSeqList.getNumLinks();
    }
    void pause(bool paused) {
        mSoundActivity.pause(paused);
    }

private:
    SoundAudience* mAudience;
    SeqSource* seqDataMgr;
    SoundStrategyMgr< SequenceSound >* soundStrategyMgr;
    SoundMove mMove;
    JSUList< SequenceSound > mSeqList;
    int mNumTracks;
};
