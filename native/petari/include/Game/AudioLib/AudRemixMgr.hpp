#pragma once

#include "Game/AudioLib/AudRemixSequencer.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"

class NebulaArena;

// In the remix sequence resource (host order, as the importer cooks it).
struct RemixNoteData {
    s32 _0;
    s32 _4;
    s32 _8;
    s32 _C;
};

struct RemixNoteTrackData {
    s32 _0;
    RemixNoteData* _4;
};

struct RemixNoteGroupData {
    /* 0x0  */ s32 mIndex;
    /* 0x4  */ s32 mTrackCount;
    /* 0x8  */ s32 mNoteCount;
    u32* _C;
    RemixNoteTrackData* mRemixTracks;
};

class AudRemixMgr {
public:
    AudRemixMgr(NebulaArena* pHeap);
    void init();
    void update();
    void setRemixSeqResource(void* ptr);
    RemixNoteGroupData* getRemixNoteGroupDataFromMelodyNo(s32 melodyNo) const;

    /* 0x0  */ NebulaArena* mHeap;
    /* 0x4  */ RemixNoteGroupData* mGroups;
    /* 0x8  */ s32 mGroupCount;
    /* 0xC  */ AudSoundObject* mSoundObj;
    AudRemixSequencer* mRemixSeq;
};
