#pragma once

#include "Game/Sound/SoundAudience.hpp"
#include "Game/Sound/StreamSound.hpp"
#include "Game/Sound/SoundGlobal.hpp"

class StreamManager : public SoundGlobal< StreamManager > {
public:
    StreamManager(bool setInstance);
    bool startSound(SoundID id, SoundHandle* handle, const Vec3f* posPtr);
    void freeDeadStream_();
    void calc();
    void stop();
    void stop(u32 fadeTime);
    void stopSoundID(SoundID id);
    void mixOut();
    StreamSound* newStream_();
    bool isActive() const {
        return mStreamList.getNum() != 0;
    }

    SoundAudience* getAudience() {
        return mAudience;
    }

    void setAudience(SoundAudience* audience) {
        mAudience = audience;
    }
    SoundMove* getParams() {
        return &mParams;
    }
    NebulaList< StreamSound >* getStreamList() {
        return &mStreamList;
    }


    void pause(bool paused) {
        mActivity.pause(paused);
    }

    void initParams() {
        mParams.init();
    }

    int getNumActiveStreams() const {
        return mStreamList.getNum();
    }

private:
    SoundActivity mActivity;
    SoundMove mParams;
    SoundAudience* mAudience;
    NebulaList< StreamSound > mStreamList;
};
