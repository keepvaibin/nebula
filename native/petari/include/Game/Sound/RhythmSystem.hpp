#pragma once

#include "Game/Sound/ChordInfo.hpp"
#include "Game/Sound/MeTrack.hpp"
#include "Game/Sound/SeqTrack.hpp"
#include "Game/Sound/SoundCommand.hpp"
#include <nebula_geometry.hpp>

class NebulaArena;
class SoundEngine;
class StreamPlayer;

// A music's beat sequence and its stream as one clock: the sequence's place is worked out from the
// stream's sample. The first beat note is the stream's first sample; a loop in the stream is that many beats.
class TempoAdjuster {
public:
    void setInfo(f32 beatMul, u32 introBeats, u32 loopBeats, u32 loopStartSamples, u32 loopEndSamples);
    void setStream(StreamPlayer* pStream);
    void reject();
    // The stream goes away: the sequence keeps its own clock.
    void forget(const StreamPlayer* pStream) {
        if (mStream == pStream) {
            mStream = nullptr;
        }
    }
    void setTempo(f32 tempo) {
        mTempo = tempo;
    }
    // True once after setStream: the next beat note is the stream's start.
    bool takeNewStream();
    // Where the sequence should be, in seconds; below 0 with no stream.
    static f64 sequenceSeconds(void* pSelf);

private:
    StreamPlayer* mStream = nullptr;
    f32 mTempo = 120.0f;
    f32 mBeatMul = 1.0f;
    u32 mIntroBeats = 0;
    u32 mLoopBeats = 0;
    u32 mLoopStartSamples = 0;
    u32 mLoopEndSamples = 0;
    bool mIsNewStream = false;
};

class RhythmSystem;

// A music track's beat reader: it hears the track's notes, tells the Me phrases of each, follows the tempo and
// sets the chord and scale the program names.
class RhythmSeqParser : public RhythmParser {
public:
    bool noteOn(SeqTrack* pTrack, u32 slot, u32 key, u32 velocity, u32 next) override;
    void noteOff(SeqTrack* pTrack, u32 slot) override;
    void command(SeqTrack* pTrack, const SeqOp& rOp) override;

    void init();
    void flushChordScale();

    RhythmSystem* mSystem = nullptr;
    TempoAdjuster mTempoAdjuster;
    f32 mTempo = 120.0f;
    u8 mChordId = 0;
    u8 mScaleId = 0;
    bool mIsUsed = true;
    bool mIsActive = false;

private:
    // The beat note keys: their kinds 0-4, else -1.
    static s32 rhythmType(u32 key);
    bool registChord();
    bool registScale();

    // The look ahead from a note on to its note off on the program's bytes (getNextNoteOff): the ticks between, or -1.
    struct Scan {
        const u8* mData;
        u32 mBytes;
        u32 mCursor;
        bool mFailed;
        u32 mCallDepth;
        u32* mCallStack;  // the track's scanStack(), mCallRoom entries
        u32 mCallRoom;
        u8 read8();
        u32 read16();
        u32 read24();
        u32 readMidiValue();
    };
    s32 nextNoteOff(SeqTrack* pTrack, u32 slot, u32 next);
    u32 passRegCommand(SeqTrack* pTrack, Scan& rScan, int numArgs);
    u32 passCommand(SeqTrack* pTrack, Scan& rScan, u8 code, u16 widths);
    static void passPrintf(SeqTrack* pTrack, Scan& rScan);
};

// The audio thread's music logic: the beat readers, the Me phrases they drive, and the chord and scale both use. The game talks to
// it by command and reads its atomics.
class RhythmSystem {
public:
    // Game thread, before the clock starts.
    void setup(SoundEngine* pEngine, u32 parsers, NebulaArena* pArena);

    // Read by the game thread: the chord and scale in use (ChordInfo::isAvailable) and their numbers (a table loaded keeping them).
    NebulaAtomic< bool > mChordAvailable = false;
    NebulaAtomic< s32 > mChordIndex = 0;
    NebulaAtomic< s32 > mScaleIndex = 0;

    // The audio thread's parts.
    void apply(const SoundCommand& rCommand);
    RhythmParser* parser(u32 index) {
        return index < mParserCount ? &mParsers[index] : nullptr;
    }
    SoundEngine* engine() {
        return mEngine;
    }
    ChordInfo& chord() {
        return mChord;
    }
    void publishChord();
    // A beat note of kind `type` for every playing phrase, the last started first.
    void meBeat(s32 type);
    // Another playing phrase with this Me id.
    MeTrack* sameMe(const MeTrack* pTrack, u32 meId);
    void tryRelease(MePlayer* pPlayer);
    void forgetStream(const StreamPlayer* pStream) {
        for (u32 i = 0; i < mParserCount; i++) {
            mParsers[i].mTempoAdjuster.forget(pStream);
        }
    }
    u32 getRandom(u32 range) {
        return mMeRandom.getRand(range);
    }

    // The phrases' note registers, each byte a register number may name.
    u8 mRegs[256] = {};

private:
    void unlist(MePlayer* pPlayer);

    SoundEngine* mEngine = nullptr;
    RhythmSeqParser* mParsers = nullptr;
    u32 mParserCount = 0;
    ChordInfo mChord;
    // Playing phrases, the last started first.
    MePlayer* mFirstMe = nullptr;
    bool mMeStarted = false;
    FastRandom mMeRandom = FastRandom(0);
};
