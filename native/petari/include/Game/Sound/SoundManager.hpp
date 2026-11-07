#pragma once

#include "Game/Sound/EffectManager.hpp"
#include "Game/Sound/SequenceManager.hpp"
#include "Game/Sound/SoundStarter.hpp"
#include "Game/Sound/StreamManager.hpp"

class SoundAudience;
class SoundID;
class SoundHandle;

class SoundManager : public SoundStarter {
public:
    SoundManager();

    virtual bool startSound(SoundID id, SoundHandle* handlePtr, const Vec3f* posPtr);

    void setAudience(SoundAudience*);
    void calc();
    void mixOut();
    void stopSoundID(SoundID);
    void stop(u32 fadeTime);
    void initParams();

    int getNumActiveSounds() const;

    EffectManager& getSeMgr() {
        return mSeMgr;
    }

    SequenceManager& getSeqMgr() {
        return mSeqMgr;
    }

    StreamManager& getStreamMgr() {
        return mStreamMgr;
    }

    EffectManager mSeMgr;
    SequenceManager mSeqMgr;
    StreamManager mStreamMgr;
};
