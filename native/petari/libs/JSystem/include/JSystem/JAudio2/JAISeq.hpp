#pragma once

#include "JSystem/JAudio2/JAISeqDataMgr.hpp"
#include "JSystem/JAudio2/JASTrack.hpp"
#include "JSystem/JSupport/JSUList.hpp"

class JAISeqMgr;
struct JAISoundChild;

class JAISeq final : public JAISound, public JSULink< JAISeq >, public JASPoolAllocObject< JAISeq > {
public:
    static const int MAX_OUTPUT_TRACK_CHILDREN = 2;
    static const int NUM_CHILDREN = MAX_OUTPUT_TRACK_CHILDREN * JASTrack::MAX_CHILDREN;

    class TInner {
    public:
        TInner() : mSeqData(nullptr, 0) {
        }

        JASTrack outputTrack;
        JAISoundChild* mSoundChild[NUM_CHILDREN];
        JAITempoMgr mTempoMgr;
        JASSoundParams mSoundParams;
        JAISeqData mSeqData;
        s32 mCategory;
        JAISeqMgr* seqMgr;
        JAISoundStrategyMgr< JAISeq >* strategyMgr;
    };

    virtual s32 getNumChild() const;
    virtual JAISoundChild* getChild(int index);
    virtual void releaseChild(int index);
    virtual JAISeq* asSeq();
    virtual JASTrack* getTrack();
    virtual JASTrack* getChildTrack(int);
    virtual JAITempoMgr* getTempoMgr();
    virtual bool JAISound_tryDie_();

    JAISeq(JAISeqMgr* seqMgr, JAISoundStrategyMgr< JAISeq >* soundStrategyMgr);
    void JAISeqMgr_startID_(JAISoundID id, const TVec3f* posPtr, JAIAudience* audience, int category, int);
    void playSeqData_(const JASSoundParams& params, JAISoundActivity activity);
    void reserveChildTracks_(int);
    void releaseChildTracks_();
    bool prepare_getSeqData_();
    bool prepare_(const JASSoundParams& params, JAISoundActivity activity);
    void JAISeqMgr_calc_();
    void die_();
    void mixOut_(const JASSoundParams& params, JAISoundActivity activity);
    void JAISeqMgr_mixOut_(const JASSoundParams& params, JAISoundActivity activity);

    const JAISeqData& getSeqData() const {
        return inner_.mSeqData;
    }
    s32 getCategory() const {
        return inner_.mCategory;
    }

    JASTrack* getOutputTrack() {
        return &inner_.outputTrack;
    }

    TInner inner_;
    JAISoundStrategy< JAISeq >* soundStrategy;
};
