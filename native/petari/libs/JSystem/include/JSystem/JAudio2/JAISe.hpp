#pragma once

#include "JSystem/JAudio2/JAISeqDataMgr.hpp"
#include "JSystem/JAudio2/JASTrack.hpp"

class JAISeMgr;

class JAISe final : public JSULink< JAISe >, public JAISound, public JASPoolAllocObject< JAISe > {
public:
    class TInner {
    public:
        TInner() : mSeqData(nullptr, 0) {
        }

        u32 mProperPriority;
        u32 mPriority;
        JASSoundParams mSoundParams;
        JASTrack track;
        JAISeqData mSeqData;
        bool _26C;
        JAISeMgr* seMgr;
        JAISoundStrategyMgr< JAISe >* mSoundStrategyMgr;
        JAISoundStrategy< JAISe >* mSoundStrategy;
        JAITempoMgr mTempoMgr;
    };

    virtual s32 getNumChild() const;
    virtual JAISoundChild* getChild(int);
    virtual void releaseChild(int);
    virtual JASTrack* getTrack();
    virtual JASTrack* getChildTrack(int);
    virtual JAISe* asSe();
    virtual JAITempoMgr* getTempoMgr();
    virtual bool JAISound_tryDie_();

    JAISe(JAISeMgr* seMgr, JAISoundStrategyMgr< JAISe >* soundStrategyMgr, u32 priority);
    void mixOut_(const JASSoundParams& params);
    void stopTrack_();
    void startTrack_(const JASSoundParams& params);
    void JAISeCategoryMgr_mixOut_(bool, const JASSoundParams& params, JAISoundActivity activity);
    void JAISeCategoryMgr_calc_();
    void JAISeMgr_startID_(JAISoundID id, const TVec3f* posPtr, JAIAudience* audience);
    bool prepare_getSeqData_();
    void prepare_();

    const JAISeqData* getSeqData() const {
        return &inner_.mSeqData;
    }
    u32 JAISeCategoryMgr_getProperPriority_() const {
        return inner_.mProperPriority;
    }
    u32 JAISeCategoryMgr_getPriority_() const {
        return inner_.mPriority;
    }
    bool isFarAway() const {
        return inner_.mPriority == -1;
    }

    TInner inner_;
};
