#pragma once


class JAISoundID;
struct RemixNoteData;
struct RemixNoteTrackData;

class AudRmxSeqNoteOnTimer {
public:
    AudRmxSeqNoteOnTimer();

    void initData();
    void setData(const RemixNoteTrackData*, const RemixNoteData*);
    bool update(f32);
    JAISoundID getFreeSeID();

    f32 _0;
    f32 _4;
    const RemixNoteData* _8;
    const RemixNoteTrackData* _C;
};

class AudRemixSequencer {
public:
    AudRemixSequencer();

    void initNoteOnBuff();
    void update();
    void setTempo(f32);
    AudRmxSeqNoteOnTimer* newNoteOnTimer();
    void addNoteData(const RemixNoteTrackData*, const RemixNoteData*);

    AudRmxSeqNoteOnTimer _0[0x20];
    f32 _200;
    f32 _204;
};
