#pragma once

#include "Game/Sound/PlayerLink.hpp"
#include "Game/Sound/SeqSource.hpp"
#include "Game/System/NebulaList.hpp"

class EffectManager;

// A sound effect: one sequence entry from the effect collection, on its own player.
class EffectSound final : public NebulaListLink< EffectSound >, public Sound {
public:
    virtual s32 getNumChild() const;
    virtual SoundChild* getChild(int);
    virtual void releaseChild(int);
    virtual PlayerLink* getPlayer();
    virtual PlayerLink* getChildTrack(int index, TrackPath* pPath);
    virtual EffectSound* asSe();
    virtual SoundTempo* getTempoMgr();
    virtual bool tryDie_();

    EffectSound(EffectManager* seMgr, u32 priority);
    virtual ~EffectSound();
    void mixOut_(const SoundParams& params);
    void stopTrack_();
    void startTrack_(const SoundParams& params);
    void categoryMixOut_(bool active, const SoundParams& params, SoundActivity activity);
    void categoryCalc_();
    void startID_(SoundID id, const Vec3f* posPtr, SoundAudience* audience);
    bool prepare_getSeqData_();
    void prepare_();

    const SeqData* getSeqData() const {
        return &mSeqData;
    }
    u32 getProperPriority_() const {
        return mProperPriority;
    }
    u32 getPriority_() const {
        return mCategoryPriority;
    }
    bool isFarAway() const {
        return mCategoryPriority == -1;
    }

    u32 mProperPriority;
    // The priority the category sorts by (-1: out of hearing).
    u32 mCategoryPriority;
    SoundParams mSoundParams;
    PlayerLink mPlayer;
    SeqData mSeqData;
    bool mTrackStarted;
    EffectManager* mManager;
    SoundTempo mTempoMgr;
};
