#pragma once

#include "Game/Sound/SeqSource.hpp"
#include "Game/Sound/SoundGlobal.hpp"
#include "Game/System/NebulaList.hpp"

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

    SoundMove* getParams() {
        return &mMove;
    }
    bool isActive() const {
        return mSeqList.getNum() != 0;
    }
    int getNumActiveSeqs() const {
        return mSeqList.getNum();
    }
    void pause(bool paused) {
        mSoundActivity.pause(paused);
    }

private:
    SoundAudience* mAudience;
    SeqSource* seqDataMgr;
    SoundMove mMove;
    NebulaList< SequenceSound > mSeqList;
    int mNumTracks;
};
