#pragma once

#include "Game/System/NebulaAtomic.hpp"

#include "JSystem/JAudio2/JAISound.hpp"
#include "JSystem/JAudio2/JASStream.hpp"
#include "JSystem/JSupport/JSUList.hpp"

class JAIStreamMgr;

class JAIStream final : public JAISound, public JSULink< JAIStream >, public JASPoolAllocObject< JAIStream > {
public:
    struct TInner {
        JASStream mStream;
    };

    JAIStream(JAIStreamMgr* streamMgr, JAISoundStrategyMgr< JAIStream >* soundStrategyMgr);
    void JAIStreamMgr_startID_(JAISoundID id, s32 streamFileEntry, const TVec3f* posPtr, JAIAudience* audience, int category);
    bool prepare_prepareStream_();
    void prepare_();
    void prepare_startStream_();
    void JAIStreamMgr_mixOut_(const JASSoundParams& inParams, JAISoundActivity activity);
    void die_JAIStream_();
    bool JAISound_tryDie_();
    void JAIStreamMgr_calc_();
    s32 getNumChild() const;
    JAISoundChild* getChild(int index);
    void releaseChild(int index);
    JASTrack* getTrack();
    JASTrack* getChildTrack(int);
    JAIStream* asStream();
    JAITempoMgr* getTempoMgr();


    void stopStream(bool stop) {
        mIsStreamStopped = stop;
    }

    bool isPaused() const {
        return inner_.mStream.isPaused();
    }

    static const int NUM_CHILDREN = 6;

    TInner inner_;
    int mPrepareState;
    s32 mStreamFileEntry;
    int mCategory;
    JAISoundChild* children[NUM_CHILDREN];
    JAIStreamMgr* streamMgr;
    JAISoundStrategy< JAIStream >* soundStrategy;
    JAISoundStrategyMgr< JAIStream >* soundStrategyMgr;
    bool mIsPaused;
    NebulaAtomic< bool > mIsStreamStopped;
    NebulaAtomic< bool > mIsStreamStarted;
};
