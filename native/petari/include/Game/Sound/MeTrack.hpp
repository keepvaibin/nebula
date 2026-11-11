#pragma once

#include "Game/Sound/SoundData.hpp"
#include "Game/Sound/SoundParams.hpp"
#include "Game/Sound/SoundVoices.hpp"
#include "Game/System/NebulaAtomic.hpp"

class RhythmSystem;
class MeTrack;
class MePlayer;

// A cooked Me program file (crates/nebula-import/src/cook/sound.rs me_program): the phrases' authored bytecode, read in
// place, and per entry its start, the tracks it can have alive at once and the call and loop entries a track holds at once.
struct MeProgramFile {
    static const u32 cMagic = 0x53454d4e;  // "NMES"
    static const u32 cVersion = 2;

    u32 mMagic;
    u32 mVersion;
    u32 mEntryCount;
    u32 mDataBytes;
    // Then: MeEntry entries[mEntryCount], u8 data[mDataBytes] (the program's addresses are offsets into it).
};
static_assert(sizeof(MeProgramFile) == 16, "NMES header");

struct MeEntry {
    u32 mStart;
    u32 mTrackBound;
    u32 mStackBound;  // call and loop entries a track holds at once
};
static_assert(sizeof(MeEntry) == 12, "NMES entry");

// A Me track's mix values and its note. A Me track plays one note at a time: its start's slot 1,
// the only slot it plays; its timer is never set, so no note ends by it.
struct MeChannelMgr {
    void releaseNote(u16 directRelease);
    void setPauseFlag(bool pause);

    MeTrack* mTrack = nullptr;
    // The game's values for the phrase (AudMe::updateTimedParams), and what the note gets.
    SoundParams mSound;
    NoteParams mParams;
    SoundNote* mNote = nullptr;
};

// The tracks of one phrase, in the memory the game gave its player.
class MeArena {
public:
    static u32 recordBytes(u32 stack);
    void setup(void* pMemory, u32 count, u32 stack);
    MeTrack* alloc(RhythmSystem* pSystem, MePlayer* pPlayer);
    void free(MeTrack* pTrack);
    const void* begin() const;
    const void* end() const;

    NoteHolder mHolder;

private:
    u8* mBase = nullptr;
    u32 mCount = 0;
    u32 mRecordBytes = 0;
    u32 mStack = 0;
    MeTrack* mFree = nullptr;
};

// One phrase track on the audio thread, with its program's reader: it steps on the music's beat
// notes, picks its notes from the chord and scale, and plays them on the voices.
class MeTrack {
public:
    enum Status : s32 { STATUS_FREE, STATUS_READY, STATUS_RUN, STATUS_START };
    void construct(RhythmSystem* pSystem, MePlayer* pPlayer, MeArena* pArena, u32* pStack, u16* pLoopCounts, u32 stackRoom);
    void setSeqData(const u8* pData, u32 start);
    void setStatus(s32 status);
    void stopSeq();
    void suspendSeq();
    void stopAtCurrentNoteEnd();
    // A beat note of the music: <0 the phrase ended.
    s32 rhythmProc(s32 type);
    void setPauseFlag(bool pause);
    void gframeProc();
    // Its notes off and its children closed, as the track's destructor did.
    void close();
    void setSound(const SoundParams& rSound) {
        mMgr.mSound = rSound;
    }
    static void noteEvent(u32 event, SoundNote* pNote, NebulaVoice* pVoice, MeChannelMgr* pMgr);

    s32 mStatus = STATUS_FREE;
    u32 mMeId = 0;
    // The players' list of playing phrases.
    MePlayer* mPlayer = nullptr;

private:
    struct TrackInfo {
        u8 mVelocity = 100;
        u16 mBankNumber = 0;
        u16 mProgNumber = 0xf0;
        u8 mBendSense = 12;
        u16 mRelease = 0;
        u8 mPlayPriority = 64;
        u8 mReleasePriority = 0;
        u32 mNoteReg = 0;
        u8 mNoteRangeLower = 0;
        u8 mNoteRangeUpper = 12 * 10;
        u8 mNoteRangeStart = 12 * 5;
    };

    void init();
    void inherit(const MeTrack& rParent);
    MeTrack* newChild(u32 index);
    bool deleteChild(u32 index);
    MeTrack* child(u32 index) const;
    bool hasChildren() const {
        return mFirstChild != nullptr;
    }
    void noteOffAll(u16 directRelease);
    bool noteOn(u32 key, u32 velocity);
    void startNote();
    void overwriteOsc(SoundNote* pNote);
    void setOscAdsr(s16 attack, s16 decay, s16 sustainTime, s16 sustainLevel, u16 release);
    void updateChannelParams();
    // numNotes is the phrase's count byte, so a list of cNoteListRoom always holds it.
    static const s32 cNoteListRoom = 0x100;
    void setBaseNoteFromChord(const u8* pChordData, u8 numNotes, bool random);
    void setBaseNoteFromScale(const u8* pScaleData, u8 numNotes, bool random);
    void setBaseNoteFromChromatic(const u8* pChromData, u8 numNotes, bool random);
    void adjustNote(s32 direction, s32 type);
    void modifyNote(s32 direction, s32 type, s32 steps);
    u32 getRandom(u32 range);
    void makeRandomSearchList(s32* pList, s32 numItems, bool ordered);
    void adjustInNoteRange(u8& rNote) const;
    void adjustInStartNoteRange(u8& rNote) const;
    MeTrack* brother(s32 index) const;

    // The sequence control: a step's work until it waits; <0 the program ended, 1 a note's end.
    s32 seqProc(s32 type);
    s32 parse();
    void wait(s32 type, s32 time) {
        mWaitType = type;
        mWaitTime = time;
    }
    void autoWait();
    u32 read8() {
        return mData[mCursor++];
    }
    u32 read16();
    u32 read24();
    bool call(u32 address);
    bool ret();
    bool loopStart(u16 count);
    bool loopEnd();

    RhythmSystem* mSystem = nullptr;
    MeArena* mArena = nullptr;

    const u8* mData = nullptr;
    u32 mCursor = 0;
    u32 mStackDepth = 0;
    // Call and loop entries, in the track's record: room for the phrase's mStackBound.
    u32* mStack = nullptr;
    u16* mLoopCounts = nullptr;
    u32 mStackRoom = 0;
    s32 mWaitType = -1;
    s32 mWaitTime = 0;
    s32 mProgress = -1;
    bool mIsLocked = false;
    bool mCtrlStopAfterNote = false;

    MeTrack* mParent = nullptr;
    // Children in index order.
    MeTrack* mFirstChild = nullptr;
    MeTrack* mNextSibling = nullptr;
    s32 mBrotherNo = -1;
    MeChannelMgr mMgr;
    bool mIsPaused = false;
    bool mIsProcStopped = false;
    OscData mOscParam[2] = {};
    SeqPoint mOscPoint[4] = {};
    f32 mVolume = 1.0f;
    f32 mPitchBend = 0.0f;
    f32 mFxMix = 0.0f;
    f32 mPan = 0.5f;
    f32 mDolby = 0.0f;
    u8 mBaseNote = 60;
    u8 mBaseNotePrev = 60;
    bool mIsInStart = true;
    bool mStopAfterNote = false;
    bool mNoStartRange = false;
    bool mJumpFlag = false;
    TrackInfo mTrackInfo;
    // The arena's free list.
    MeTrack* mNextFree = nullptr;

    friend class MeArena;
    friend struct MeChannelMgr;
};

// A phrase's memory, made by the game thread (AudMe) and handed over with its start command: the record the game reads and the
// tracks. The game frees it once mReleased says the audio thread has let go.
class MePlayer {
public:
    static u32 bytes(u32 tracks, u32 stack);
    static MePlayer* create(void* pMemory, u32 tracks, u32 stack);

    // Read by the game thread: the main track's status.
    NebulaAtomic< s32 > mStatus = MeTrack::STATUS_FREE;
    NebulaAtomic< bool > mReleased = false;

    // The audio thread's.
    MeArena mArena;
    MeTrack* mRoot = nullptr;
    bool mListed = false;
    bool mReleaseAsked = false;
    MePlayer* mNext = nullptr;
};
