#pragma once

#include "JSystem/JAudio2/JAIAudience.hpp"
#include "JSystem/JAudio2/JAIStream.hpp"
#include "JSystem/JAudio2/JASGlobal.hpp"

struct JAIStreamDataMgr;

class JAIStreamMgr : public JASGlobalInstance< JAIStreamMgr > {
public:
    JAIStreamMgr(bool setInstance);
    bool startSound(JAISoundID id, JAISoundHandle* handle, const TVec3f* posPtr);
    void freeDeadStream_();
    void calc();
    void stop();
    void stop(u32 fadeTime);
    void stopSoundID(JAISoundID id);
    void mixOut();
    JAIStream* newStream_();
    bool isActive() const {
        return mStreamList.getNumLinks() != 0;
    }

    JAIAudience* getAudience() {
        return mAudience;
    }

    void setAudience(JAIAudience* audience) {
        mAudience = audience;
    }
    JAISoundParamsMove* getParams() {
        return &mParams;
    }
    JSUList< JAIStream >* getStreamList() {
        return &mStreamList;
    }

    void setStreamDataMgr(JAIStreamDataMgr* streamDataMgr) {
        streamDataMgr_ = streamDataMgr;
    }


    void pause(bool paused) {
        mActivity.pause(paused);
    }

    void initParams() {
        mParams.init();
    }

    int getNumActiveStreams() const {
        return mStreamList.getNumLinks();
    }

private:
    JAISoundActivity mActivity;
    JAISoundParamsMove mParams;
    JAIAudience* mAudience;
    JSUList< JAIStream > mStreamList;
    JAIStreamDataMgr* streamDataMgr_;
    JAISoundStrategyMgr< JAIStream >* soundStrategyMgr;
};
