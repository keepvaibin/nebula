#pragma once

#include "Game/AudioLib/AudRemixSequencer.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"

class NebulaArena;

// In the remix sequence resource (host order, as the importer cooks it).
struct RemixNoteData {
    s32 mNoteNo;
    s32 mVelocity;
    s32 mLength;
    s32 mStartTick;
};

struct RemixNoteTrackData {
    s32 mTrackNo;
    RemixNoteData* mNotes;
};

struct RemixNoteGroupData {
    s32 mIndex;
    s32 mTrackCount;
    s32 mNoteCount;
    RemixNoteTrackData* mRemixTracks;
};

class AudRemixMgr {
public:
    AudRemixMgr(NebulaArena* pArena);
    void init();
    void update();
    void setRemixSeqResource(void* ptr);
    RemixNoteGroupData* getRemixNoteGroupDataFromMelodyNo(s32 melodyNo) const;

    NebulaArena* mArena;
    RemixNoteGroupData* mGroups;
    s32 mGroupCount;
    AudSoundObject* mSoundObj;
    AudRemixSequencer* mRemixSeq;
};
