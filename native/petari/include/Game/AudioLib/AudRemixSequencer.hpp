#pragma once

#include "Game/Util/Array.hpp"

class SoundID;
struct RemixNoteData;
struct RemixNoteTrackData;

class AudRmxSeqNoteOnTimer {
public:
    AudRmxSeqNoteOnTimer();

    void initData();
    void setData(const RemixNoteTrackData*, const RemixNoteData*);
    bool update(f32 ticks);
    SoundID getFreeSeID();

    f32 mDelay;
    f32 mRemaining;
    const RemixNoteData* mNote;
    const RemixNoteTrackData* mTrack;
};

class AudRemixSequencer {
public:
    AudRemixSequencer();

    void initNoteOnBuff();
    void update();
    void setTempo(f32 tempo);
    AudRmxSeqNoteOnTimer* newNoteOnTimer();
    void addNoteData(const RemixNoteTrackData*, const RemixNoteData*);

    // Slots, a note taking the first free one; full, the pool doubles (it never drops a note).
    MR::AssignableArray< AudRmxSeqNoteOnTimer > mTimers;
    f32 mTempo;
    f32 mTicksPerFrame;
};
