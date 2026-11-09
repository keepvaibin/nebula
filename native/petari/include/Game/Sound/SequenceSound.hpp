#pragma once

#include "Game/Sound/PlayerLink.hpp"
#include "Game/Sound/SeqSource.hpp"
#include "Game/System/NebulaList.hpp"

class SequenceManager;
struct SoundChild;

// A sequence (music, or a scene's sequence): a player whose tracks the game numbers (child id >> 4, its track id & 15), at
// least 0 to 31.
class SequenceSound final : public Sound, public NebulaListLink< SequenceSound > {
public:
    static const int cInlineChildren = 32;

    virtual s32 getNumChild() const;
    virtual SoundChild* getChild(int index);
    virtual void releaseChild(int index);
    virtual SequenceSound* asSeq();
    virtual PlayerLink* getPlayer();
    virtual PlayerLink* getChildTrack(int index, TrackPath* pPath);
    virtual SoundTempo* getTempoMgr();
    virtual bool tryDie_();

    explicit SequenceSound(SequenceManager* seqMgr);
    virtual ~SequenceSound();
    void startID_(SoundID id, const Vec3f* posPtr, SoundAudience* audience, int category, int numTracks, const SeqData& rData);
    void playSeqData_(const SoundParams& params, SoundActivity activity);
    bool prepare_getSeqData_();
    bool prepare_(const SoundParams& params, SoundActivity activity);
    void managerCalc_();
    void die_();
    void mixOut_(const SoundParams& params, SoundActivity activity);
    void managerMixOut_(const SoundParams& params, SoundActivity activity);
    void growChildren(s32 count);

    const SeqData& getSeqData() const {
        return mSeqData;
    }
    s32 getCategory() const {
        return mCategory;
    }

    PlayerLink mPlayer;
    // The sound children by track id: inline, then a SoundMemory block once an id passes it; mNumChild: the ids this start
    // reserves (16 to each named child).
    SoundChild* mInlineChild[cInlineChildren];
    SoundChild** mSoundChild;
    s32 mChildRoom;
    s32 mNumChild;
    SoundTempo mTempoMgr;
    SoundParams mSoundParams;
    SeqData mSeqData;
    s32 mCategory;
    SequenceManager* mManager;
};
