#pragma once

#include "Game/Sound/SoundData.hpp"
#include "Game/Sound/SoundVoices.hpp"

class SoundEngine;
class SoundPlayer;
class SeqTrack;
class RhythmParser;

// A track's notes for one audience channel: its mix values and the notes it holds by slot.
struct ChannelMgr {
    void init();
    void releaseAll();
    bool noteOff(u32 slot, u16 directRelease);
    void setPauseFlag(bool pause);

    SeqTrack* mTrack = nullptr;
    NoteParams mParams;
    // The sound's values for this channel (the player's), or none.
    const SoundParams* mSound = nullptr;
    SoundNote** mNotes = nullptr;
    u32 mSlots = 0;
};

// What one track's record holds room for: note slots a channel group, call and loop entries, and envelope points an oscillator
// (a program's SeqFile bounds; none for a player with no program).
struct TrackRoom {
    static TrackRoom of(const SeqFile* pFile) {
        return pFile != nullptr ? TrackRoom{pFile->mNoteSlots, pFile->mStackBound, pFile->mEnvelopePoints} : TrackRoom{1, 0, 0};
    }

    u32 mSlots;
    u32 mStack;
    u32 mEnvelope;
};

// The tracks of one player: records carved from the memory the game gave it, each a track, its channel groups and their slots.
class TrackArena {
public:
    static u32 recordBytes(u32 mgrCount, const TrackRoom& rRoom);
    void setup(void* pMemory, u32 count, u32 mgrCount, const TrackRoom& rRoom);
    SeqTrack* alloc(SoundEngine* pEngine);
    void free(SeqTrack* pTrack);
    const u8* begin() const {
        return mBase;
    }
    const u8* end() const {
        return mBase + mCount * mRecordBytes;
    }
    // Notes whose envelope reads a track here: the memory stays the player's until none is left.
    NoteHolder mHolder;
    u32 mgrCount() const {
        return mMgrCount;
    }

private:
    u8* mBase = nullptr;
    u32 mCount = 0;
    u32 mRecordBytes = 0;
    u32 mMgrCount = 0;
    TrackRoom mRoom = {};
    SeqTrack* mFree = nullptr;
};

// The filter coefficients of a cutoff index 0 to 128.
extern const s16 cIIRCutoff[129][4];

class SeqTrack {
public:
    enum Status : s32 { STATUS_FREE, STATUS_RUN, STATUS_STOPPED };
    static const u32 cTimedParams = 6;
    typedef f64 (*SecondsClock)(void*);

    struct MoveParam {
        f32 mValue;
        f32 mTarget;
        u32 mCount;
    };

    // In a record of TrackArena::recordBytes: pTail is the memory past the track.
    void construct(SoundEngine* pEngine, TrackArena* pArena, u8* pTail, u32 mgrCapacity, const TrackRoom& rRoom);
    SoundEngine* engine() const {
        return mEngine;
    }
    TrackArena* arena() const {
        return mArena;
    }
    void init();
    void setChannelMgrCount(u32 count);
    void inherit(const SeqTrack& rParent);
    void setSeqData(const SeqFile* pFile, u32 entryOp);
    void start() {
        mStatus = STATUS_RUN;
    }
    void close();
    SeqTrack* openChild(u32 index);
    void closeChild(u32 index);
    SeqTrack* child(u32 index) const;
    SeqTrack* parent() const {
        return mParent;
    }
    SeqTrack* firstChild() const {
        return mFirstChild;
    }
    SeqTrack* nextSibling() const {
        return mNextSibling;
    }
    u32 index() const {
        return mIndex;
    }
    // A new child of the parent's own at `index`, before the program opens it (SequenceSound's reserved tracks); null when the arena is full
    // or the index is taken.
    SeqTrack* reserveChild(u32 index);
    SeqTrack* rootTrack();

    bool noteOn(u32 slot, u32 key, u32 velocity);
    bool gateOn(u32 key, u32 velocity, f32 seqTime, u32 flags);
    bool noteOff(u32 slot, u16 directRelease);
    void noteOffAll(u16 directRelease);
    bool checkNoteStop(u32 slot) const;
    void mute(bool mute);
    void pause(bool pause);
    void setTempo(u16 tempo);
    void setTempoRate(f32 rate);
    void setTimebase(u16 timebase);
    void setSecondsClock(SecondsClock clock, void* pData);
    void setIIR(const s16* pCoefficients);
    void setFIR(const u8* pBigEndian);
    void writePort(u32 port, u16 value);
    u16 readPort(u32 port);
    u16 port(u32 port) const {
        return mPorts[port];
    }
    s32 transposeTotal() const;
    bool isMute() const;
    void setParam(u32 param, f32 value, u32 time);
    void setMgrSound(u32 mgr, const SoundParams* pSound) {
        mMgrs[mgr].mSound = pSound;
    }
    u32 mgrCount() const {
        return mMgrCount;
    }

    // The tick's work of a playing root: its ticks as tempo and clock say, then the mix values down the tree. <0: it ended.
    s32 seqMain();
    // The engine's dispatch of a note event for a note this track holds.
    static void noteEvent(u32 event, SoundNote* pNote, NebulaVoice* pVoice, ChannelMgr* pMgr);

    // Read by the game's snapshot and the rhythm parser.
    // A register as the program reads it: 0x00-0x3F the registers, 0x40-0x4F the ports, 0x60 and up the track's fields.
    u32 readReg(u32 reg) const;
    void writeRegister(u32 reg, u32 value);
    u32 runningChildren() const;

    void setRhythmParser(RhythmParser* pParser) {
        mRhythm = pParser;
    }
    RhythmParser* rhythmParser() const {
        return mRhythm;
    }
    // A rhythm parser's look ahead leaves the program at a byte: the operation there, or the track ends.
    void jumpToOffset(u32 offset);
    const u8* seqData() const {
        return mData;
    }
    u32 seqBytes() const {
        return mFile != nullptr ? mFile->mDataBytes : 0;
    }
    // The byte offset of the next operation (the program's end past the last).
    u32 nextOffset() const;
    s32 stackTopOffset() const;
    // Room for a look ahead's own calls (RhythmSeqParser::nextNoteOff): as deep as the track's stack.
    u32* scanStack() const {
        return mScanStack;
    }
    u32 stackRoom() const {
        return mStackRoom;
    }

    s32 mStatus = STATUS_FREE;
    bool mPauseFlag = false;
    bool mIsMute = false;
    bool mIsDirectlyPlayed = true;
    bool mIsOwnedByParent = false;
    bool mReadyToPlay = false;
    bool mInvalidateSeq = true;
    bool mIsStopped = false;
    u16 mTempo = 120;
    u16 mTimebase = 48;
    f32 mTempoRate = 1.0f;
    s8 mTranspose = 0;
    u8 mPitch = 60;
    u16 mBankNumber = 0;
    u16 mProgNumber = 0xf0;
    // The sound whose root this is, for the host's volume categories: its id and route (1 effect, 2 sequence).
    u32 mOwnerSound = 0;
    u8 mOwnerRoute = 0;
    // The players' list of playing roots.
    SeqTrack* mNextRoot = nullptr;

private:
    s32 tickProc();
    s32 seqTick();
    void interrupt(u32 type);
    s32 findInterrupt();
    void checkInterrupt();
    void timerProcess();
    void updateTimedParam();
    void updateTrack(f32 pan);
    void updateTempo();
    void updateSeq(bool update, f32 pan);
    u32 seqTimeToDspTime(f32 seqTime);
    SoundNote* channelStart(ChannelMgr* pMgr, u32 key, u32 velocity, u32 updateTimer);
    void overwriteOsc(SoundNote* pNote);
    void updateChannel(SoundNote* pNote, NebulaVoice* pVoice);
    void setChannelPauseFlag(bool pause);
    void setOscTable(u32 osc, const u8* pBigEndian);
    void setOscAdsr(s16 attack, s16 decay, s16 sustain, s16 release, u16 directRelease);
    bool connectChild(u32 index, SeqTrack* pChild);
    void freeTrack();

    // The interpreter.
    s32 step();
    u32 arg(const SeqOp& rOp, u32 i) const;
    u32 target(const SeqOp& rOp, u32 i, u32 value) const;
    s32 opAt(u32 offset) const;
    bool conditionCheck(u32 condition) const;
    void writeReg(u32 reg, u32 value);
    void execNoteOnGate(u32 key, u32 velocity, u32 time, u32 flags);
    bool execNoteOnMidi(u32 slot, u32 key, u32 velocity);
    void execNoteOff(u32 slot);
    bool call(u32 op);
    bool ret();
    bool loopStart(u32 count);
    bool loopEnd();

    SoundEngine* mEngine = nullptr;
    TrackArena* mArena = nullptr;
    RhythmParser* mRhythm = nullptr;

    // The program: its operations, their byte offsets, its bytes; the next operation (-1: none).
    const SeqFile* mFile = nullptr;
    const SeqOp* mOps = nullptr;
    const u32* mOffsets = nullptr;
    const u8* mData = nullptr;
    s32 mPc = -1;
    // Call and loop entries, in the track's record: room for the file's mStackBound.
    s32* mStack = nullptr;
    u16* mLoopCounts = nullptr;
    u32* mScanStack = nullptr;
    u32 mStackRoom = 0;
    u32 mStackDepth = 0;
    s32 mTimer = 0;
    // The operation an interrupt returns to (-1: none running).
    s32 mSwap = -1;
    u32 mIntrTable = 0;
    u16 mIntrPending = 0;
    u16 mIntrMask = 0;
    u8 mIntTimer = 0;
    bool mWaitNote = false;
    u32 mIntrCount = 0;
    u32 mIntrPeriod = 0;

    u16 mRegs[14] = {};
    u16 mPorts[16] = {};
    u16 mImport = 0;
    u16 mExport = 0;

    MoveParam mMoveParam[cTimedParams] = {};
    OscData mOscParam[2] = {};
    SeqPoint mOscPoint[4] = {};
    // The two oscillators' envelopes (setOscTable), in the record: mEnvelopeRoom points each.
    SeqPoint* mOscTable[2] = {};
    u32 mEnvelopeRoom = 0;

    SeqTrack* mParent = nullptr;
    // Children in index order.
    SeqTrack* mFirstChild = nullptr;
    SeqTrack* mNextSibling = nullptr;
    u32 mIndex = 0;

    ChannelMgr* mMgrs = nullptr;
    u32 mMgrCapacity = 0;
    u32 mMgrCount = 1;

    f32 mPlaytime = 1.0f;
    f32 mSampleInterval = 1.0f;
    f32 mVibDepth = 0.0f;
    f32 mVibPitch = 1.0f;
    f32 mTremDepth = 0.0f;
    f32 mTremPitch = 1.0f;
    u16 mVibDelay = 0;
    u16 mTremDelay = 0;
    s16 mFIR[8] = {};
    s16 mIIR[8] = {};
    u8 mFilterMode = 0;
    f32 mPanPower = 1.0f;
    SecondsClock mSecondsClock = nullptr;
    void* mSecondsClockData = nullptr;
    f64 mClockTicks = 0.0;
    u32 mSkipSample = 0;
    u16 mDirectRelease = 0;
    u8 mBendSense = 0xc;
    u8 mNoteOnPrio = 0x40;
    u8 mReleasePrio = 0;
    u8 mGateRate = 100;
    u16 mMixConfig[6] = {};
};

// What a rhythm parser adds to a track's program: it hears the track's notes and some settings.
class RhythmParser {
public:
    // A note on of slot `slot`; `next` the byte offset after it. Returns noteOn's answer.
    virtual bool noteOn(SeqTrack* pTrack, u32 slot, u32 key, u32 velocity, u32 next) = 0;
    virtual void noteOff(SeqTrack* pTrack, u32 slot) = 0;
    // An operation written without the register prefix, before it runs.
    virtual void command(SeqTrack* pTrack, const SeqOp& rOp) = 0;
};
