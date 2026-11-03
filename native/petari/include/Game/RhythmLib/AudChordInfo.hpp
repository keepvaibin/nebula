#pragma once

#include <JSystem/JAudio2/JASGlobal.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class AssetFolder;

struct AudChordData {
    u8 mBass;
    u8 mChordTone[4];
    u8 mAddNote[3];
};

// In the chord table file (host order, as the importer cooks it): its note lists' offsets from the file's start.
struct AudScaleData {
    u32 mUpOffset;
    u32 mDownOffset;
};

class AudChordTable {
public:
    AudChordTable();

    bool setChordTable(s32 id, AssetFolder* pArchive);
    bool setChordTableResource(void* pRes);

    bool mLoaded;
    int mChordCount;
    int mScaleCount;
    // The file and its offset tables (from the file's start); the file is not written.
    const u8* mBase;
    const u32* mChordOffsets;
    const u32* mScaleOffsets;

    const AudChordData* getChord(s32 index) const {
        return reinterpret_cast< const AudChordData* >(mBase + mChordOffsets[index]);
    }
    const AudScaleData* getScale(s32 index) const {
        return reinterpret_cast< const AudScaleData* >(mBase + mScaleOffsets[index]);
    }
};

class AudChordInfo : public JASGlobalInstance< AudChordInfo > {
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

    AudChordInfo();

    void init(AssetFolder* pArchive);
    bool loadChordInfo(s16 id, bool keepCurrent);
    void invalidiate();
    bool setCurChord(s32 index);
    bool setCurScale(s32 index);

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

    u8 getFirst() {
        return mCurChord->mChordTone[0];
    }
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

    bool isAvailable() const {
        return mTable.mLoaded && mCurChord != nullptr && mCurScale != nullptr;
    }

    AssetFolder* mArchive;
    AudChordTable mTable;
    const AudChordData* mCurChord;
    const AudScaleData* mCurScale;
    const u8* mCurScaleUp;  // the current scale's note lists in the chord table file
    const u8* mCurScaleDown;
    u8 mChordNoteList[NUM_CHORD_NOTES];
    u8 mFlags;
    u32 _2C;
    s32 mCurChordIndex;
    s32 mCurScaleIndex;
    s16 mTableId;
};
