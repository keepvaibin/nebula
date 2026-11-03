#pragma once

#include "Game/RhythmLib/AudBgmTempoAdjuster.hpp"
#include <JSystem/JAudio2/JASSeqParser.hpp>

struct JASTrack;

class AudRhythmSeqParser : public JASSeqParser {
public:
    static const int MAX_CHANNELS = 8;

    struct NoteInfo {
        bool mIsOn;
        u32 mNoteOff;
        u32 mPitch;
        u32 mVelocity;
        s64 mTime;
    };

    typedef void (*NoteOnFunc)(u32, u32);
    typedef void (*NoteOffFunc)();

    struct NoteOnInfo {
        s32 mChannel;
        s32 _4;
        NoteOnFunc mNoteOnFunc;
        NoteOffFunc mNoteOffFunc;
    };

    enum RhythmType {
        RhythmType_0 = 0,
        RhythmType_1 = 1,
        RhythmType_2 = 2,
        RhythmType_3 = 3,
        RhythmType_4 = 4,
        RhythmType_5 = 5,
    };

    AudRhythmSeqParser();

    virtual s32 parse(JASTrack*);
    virtual bool execNoteOnMidi(JASTrack*, u32, u32, u32);
    virtual void execNoteOff(JASTrack*, u32);

    void noteOnProcess(u32, u32, u32, u32);
    void noteOffProcess(u32);

    bool checkNoteOn(u32, u32&, u32&, u32&, s64&, bool);
    bool checkNoteOnKey(u32, u32&, u32&, s64&, u32&);
    f32 getTempo() const;
    f32 getModifiedTempo() const;
    s32 getRhythmType(u32);
    u16 getTempoData(JASTrack*);
    u8 getChordNumberData(JASTrack*);
    u8 getScaleNumberData(JASTrack*);
    s32 getNextNoteOff(JASTrack*, u32);
    u32 passRegCommand(JASTrack*, int);
    u32 passCommand(JASTrack*, u8, u16);
    bool callTmp(JASTrack*, u32);
    bool retTmp(JASTrack*);

    void initNoteInfo(u32);
    void init();
    void activate();
    void setNoteInfo(u32, u32, u32, u32);
    bool registChordNumToInfo();
    bool registScaleNumToInfo();
    void flushChordScale();

    AudBgmTempoAdjuster* getTempoAdjuster() {
        return &mTempoAdjuster;
    }

    f32 mTempo;
    f32 mModifiedTempo;
    u8 mChordId;
    u8 mScaleId;
    NoteInfo mNoteInfo[8];
    u8 mRhythmType0;
    u8 mRhythmType1;
    u8 mRhythmType2;
    u8 mRhythmType3;
    u8 mRhythmType4;
    AudBgmTempoAdjuster mTempoAdjuster;
    u32 mCallDepth;
    u8* mCallStack[8];
    NoteOnInfo mNoteOnInfo[8];
    s32 _1A4;
    bool mIsUsed;
    bool mIsActive;
    u32 _1AC;
};
