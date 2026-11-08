#pragma once

#include <nebula_types.h>

// The music's current chord and scale on the audio thread: the Me phrases pick their notes from them and the rhythm parser
// changes them as the music plays. The game loads a table or drops it by command and reads only `available`.

struct ChordData {
    u8 mBass;
    u8 mChordTone[4];
    u8 mAddNote[3];
};

// In the cooked chord table (crates/nebula-import/src/cook/native_data.rs cit, host order): a scale's note lists, as offsets from
// the file's start.
struct ScaleData {
    u32 mUpOffset;
    u32 mDownOffset;
};

class ChordTable {
public:
    // A cooked chord table ("CITS" at 4), or false.
    bool setResource(const void* pFile);

    bool mLoaded = false;
    s32 mChordCount = 0;
    s32 mScaleCount = 0;
    const u8* mBase = nullptr;
    const u32* mChordOffsets = nullptr;
    const u32* mScaleOffsets = nullptr;

    const ChordData* getChord(s32 index) const {
        return reinterpret_cast< const ChordData* >(mBase + mChordOffsets[index]);
    }
    const ScaleData* getScale(s32 index) const {
        return reinterpret_cast< const ScaleData* >(mBase + mScaleOffsets[index]);
    }
};

class ChordInfo {
public:
    enum ScaleType {
        ScaleType_None = 0,
        ScaleType_Up = 1,
        ScaleType_Down = 2,
        ScaleType_UpDown = 3,
    };

    static const int OCTAVE = 12;
    static const int NULL_NOTE = 0x7F;
    static const int NUM_CHORD_NOTES = 8;

    // A table to use (null or not a chord table: none); keepCurrent keeps the chord and scale numbers.
    bool load(const void* pFile, bool keepCurrent);
    void invalidate();
    bool setCurChord(s32 index);
    bool setCurScale(s32 index);

    bool isAvailable() const {
        return mTable.mLoaded && mCurChord != nullptr && mCurScale != nullptr;
    }

    u8 getBassNote() const;
    u8 getRoot() const {
        return mCurChord->mChordTone[0];
    }
    u8 getChordTone(int idx) const {
        return mCurChord->mChordTone[idx];
    }
    u8 getAddNote(int idx) const {
        return mCurChord->mAddNote[idx];
    }
    s32 getScaleNoteUp(s32 idx) {
        return mCurScaleUp[idx];
    }
    s32 getScaleNoteDown(s32 idx) {
        return mCurScaleDown[idx];
    }

    u8 findInterval(bool isChordTone, u8 loStep, u8 hiStep, bool isSkipThird);
    u8 getThird();
    u8 getFifth();
    u8 getSeventh();
    u8 getSixth();
    u8 getNinth();
    u8 getEleventh();

    bool isBass(u8 note) {
        return note == getBassNote();
    }
    bool isRoot(u8 note) {
        return note == mCurChord->mChordTone[0];
    }
    bool isThird(u8 note) {
        return note == getThird();
    }
    bool isFifth(u8 note) {
        return note == getFifth();
    }
    bool isSixth(u8 note) {
        return note == getSixth();
    }
    bool isSeventh(u8 note) {
        return note == getSeventh();
    }
    bool isNinth(u8 note) {
        return note == getNinth();
    }
    bool isEleventh(u8 note) {
        return note == getEleventh();
    }

    bool isOnChord(u8 note, bool includeAdd, bool includeBass);
    u8 getNearestChordNoteDir(u8 note, bool up);
    u8 getNearestChordNote(u8 note);
    u8 getUpperNoteOnChord(u8 note, s32 steps);
    u8 getLowerNoteOnChord(u8 note, s32 steps);

    s32 getOnScaleType(u8 note);
    u8 getNearestScaleNoteDir(u8 note, bool up);
    u8 getNearestScaleNote(u8 note);
    u8 getUpperNoteOnScale(u8 note, s32 steps);
    u8 getLowerNoteOnScale(u8 note, s32 steps);

    void expandChordNote();
    s32 getChordNoteIndex(u8 pitchClass);
    s32 addIndexInChordNoteList(s32 idx, s32 count, s32& wrapCount);
    s32 subIndexInChordNoteList(s32 idx, s32 count, s32& wrapCount);
    s32 getScaleNoteIndex(u8 pitchClass, bool up);
    s32 addIndexInScaleNoteList(s32 idx, s32 count, s32& wrapCount);
    s32 subIndexInScaleNoteList(s32 idx, s32 count, s32& wrapCount);

    void initParams();

    ChordTable mTable;
    const ChordData* mCurChord = nullptr;
    const ScaleData* mCurScale = nullptr;
    const u8* mCurScaleUp = nullptr;
    const u8* mCurScaleDown = nullptr;
    u8 mChordNoteList[NUM_CHORD_NOTES] = {NULL_NOTE, NULL_NOTE, NULL_NOTE, NULL_NOTE, NULL_NOTE, NULL_NOTE, NULL_NOTE, NULL_NOTE};
    s32 mCurChordIndex = 0;
    s32 mCurScaleIndex = 0;
};
