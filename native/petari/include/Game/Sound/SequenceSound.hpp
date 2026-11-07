#pragma once

#include "Game/Sound/PlayerLink.hpp"
#include "Game/Sound/SeqSource.hpp"
#include "JSystem/JSupport/JSUList.hpp"

class SequenceManager;
struct SoundChild;

// A sequence (music, or a scene's sequence): a player whose tracks the game numbers 0 to 31 (child id >> 4, its track id & 15).
class SequenceSound final : public Sound, public JSULink< SequenceSound > {
public:
    static const int NUM_CHILDREN = 32;

    virtual s32 getNumChild() const;
    virtual SoundChild* getChild(int index);
    virtual void releaseChild(int index);
    virtual SequenceSound* asSeq();
    virtual PlayerLink* getPlayer();
    virtual PlayerLink* getChildTrack(int index, TrackPath* pPath);
    virtual SoundTempo* getTempoMgr();
    virtual bool tryDie_();

    SequenceSound(SequenceManager* seqMgr, SoundStrategyMgr< SequenceSound >* soundStrategyMgr);
    virtual ~SequenceSound();
    void startID_(SoundID id, const Vec3f* posPtr, SoundAudience* audience, int category, int numTracks, const SeqData& rData);
    void playSeqData_(const SoundParams& params, SoundActivity activity);
    bool prepare_getSeqData_();
    bool prepare_(const SoundParams& params, SoundActivity activity);
    void managerCalc_();
    void die_();
    void mixOut_(const SoundParams& params, SoundActivity activity);
    void managerMixOut_(const SoundParams& params, SoundActivity activity);

    const SeqData& getSeqData() const {
        return mSeqData;
    }
    s32 getCategory() const {
        return mCategory;
    }

    PlayerLink mPlayer;
    SoundChild* mSoundChild[NUM_CHILDREN];
    SoundTempo mTempoMgr;
    SoundParams mSoundParams;
    SeqData mSeqData;
    s32 mCategory;
    SequenceManager* mManager;
    SoundStrategyMgr< SequenceSound >* mStrategyMgr;
    SoundStrategy< SequenceSound >* mSoundStrategy;
};
