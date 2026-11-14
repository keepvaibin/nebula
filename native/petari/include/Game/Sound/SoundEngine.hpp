#pragma once

#include "Game/Sound/RhythmSystem.hpp"
#include "Game/Sound/SeqTrack.hpp"
#include "Game/Sound/SoundBank.hpp"
#include "Game/Sound/SoundCommand.hpp"
#include "Game/Sound/SoundVoices.hpp"
#include "Game/Sound/SpeakerMixer.hpp"
#include <nebula_game_api.h>
#include <nebula_geometry.hpp>

class StreamPlayer;
class SoundEngine;

// One sound's sequence on the audio thread: its root track and the memory its tracks come from. The game thread makes it in memory
// of its own (create), hands it over with its first command and never touches it again but for the fields it reads; it frees the
// memory once mReleased says the audio thread has let go.
class SoundPlayer {
public:
    // `named`: the root's children whose own children the game names (a sequence's reserved children, 16 tracks each).
    static u32 bytes(u32 tracks, u32 mgrCount, const TrackRoom& rRoom, u32 named);
    static SoundPlayer* create(void* pMemory, u32 tracks, u32 mgrCount, const TrackRoom& rRoom, u32 named, u32 ownerSound,
                               u8 ownerRoute);
    // The words of root children: every index a program's track byte or a reserved child can give.
    static u32 rootWords(u32 named) {
        return ((named > 256 ? named : 256) + 31) / 32;
    }

    // Read by the game thread: the root's status, the tracks present (rootWords of root children, then one word of children
    // for each named child), and that the memory is free.
    NebulaAtomic< s32 > mStatus = SeqTrack::STATUS_FREE;
    NebulaAtomic< u32 >* mPresent = nullptr;
    u32 mRootWords = 0;
    u32 mNamed = 0;
    NebulaAtomic< bool > mReleased = false;

    // The audio thread's.
    TrackArena mArena;
    SeqTrack* mRoot = nullptr;
    // Each channel group's values from the game (the sound's, or its audible's channel); mHasSound: given.
    SoundParams* mMgrSound = nullptr;
    bool* mHasSound = nullptr;
    u32 mMgrCount = 0;
    // The values of the tracks the game names under the named children (a sequence's sound children), by child * 16 + grandchild.
    SoundParams* mTrackSound = nullptr;
    bool mListed = false;
    bool mReleaseAsked = false;
    SoundPlayer* mNext = nullptr;
};

// The audio thread's work after the voices each tick, in the order the first free place gives: the
// sequencers, each stream, the remote speakers.
struct SoundJob {
    enum Kind : u8 { JOB_TRACKS, JOB_STREAM, JOB_CALL };
    typedef void (*Call)(void*);

    u8 mKind = JOB_TRACKS;
    bool mListed = false;
    u32 mIndex = 0;
    void* mData = nullptr;
    Call mCall = nullptr;
    SoundJob* mNext = nullptr;
};

// A track as the game names it: the root, a child, or a child's child.
struct TrackPath {
    u8 mDepth;
    u32 mChild;
    u32 mGrandchild;
};

// The command kinds (SoundCommand::mKind) and their values.
enum SoundCommandKind : u16 {
    SOUND_OUTPUT_LEVEL,     // f32
    SOUND_OUTPUT_MODE,      // mArg
    SOUND_EFFECT_LINE,      // EffectLineCommand
    SOUND_RESET_INIT,       // the reset keeps the level now for a resume before any start
    SOUND_RESET_START,      // ResetCommand
    SOUND_RESET_RESUME,
    SOUND_WAVE_GROUP,       // WaveGroupCommand
    SOUND_PLAYER_INIT,      // player: its root back to new (a sound starting again)
    SOUND_PLAYER_RESERVE,   // player, mArg tracks: SequenceSound's children made before the program opens them
    SOUND_PLAYER_SEQ,       // player, SeqCommand
    SOUND_PLAYER_MGRS,      // player, mArg groups
    SOUND_PLAYER_SOUND,     // player, mArg group, MgrSoundCommand
    SOUND_PLAYER_START,     // player
    SOUND_PLAYER_STOP,      // player
    SOUND_PLAYER_TEMPO,     // player, f32 rate
    SOUND_PLAYER_MUTE,      // player, TrackCommand (mValue 1: mute)
    SOUND_PLAYER_PAUSE,     // player, mArg
    SOUND_PLAYER_PORT,      // player, TrackCommand (mPort, mValue)
    SOUND_PLAYER_CHILD_IIR, // player, mArg cutoff: every child of the root
    SOUND_PLAYER_RHYTHM,    // player, TrackCommand (mValue: parser index, -1 none)
    SOUND_PLAYER_TRACK_SOUND,   // player, TrackSoundCommand
    SOUND_PLAYER_RELEASE,   // player
    SOUND_STREAM_INIT,      // stream: its values back to new
    SOUND_STREAM_START,     // stream
    SOUND_STREAM_STOP,      // stream, mArg direct release
    SOUND_STREAM_PAUSE,     // stream, mArg
    SOUND_STREAM_MIX,       // stream, StreamMixCommand
    SOUND_STREAM_PREPARE,   // stream
    SOUND_STREAM_CANCEL,    // stream
    SOUND_STREAM_RELEASE,   // stream
    SOUND_STREAM_ROOM,      // stream, StreamPlayer::MessageRoom*
    SOUND_ME_START,         // Me player, MeStartCommand
    SOUND_ME_PARAMS,        // Me player, SoundParams, then the frame's mix update
    SOUND_ME_STOP_AT_NOTE_END,  // Me player
    SOUND_ME_STOP,          // Me player
    SOUND_ME_PAUSE,         // Me player, mArg
    SOUND_ME_RELEASE,       // Me player
    SOUND_RHYTHM_INIT,      // mValue parser (AudRhythmMeSystem::rejectSeq)
    SOUND_RHYTHM_ACTIVATE,  // mValue parser (AudRhythmMeSystem::setSeq)
    SOUND_RHYTHM_USE,       // mValue parser (AudRhythmMeSystem::setUsingRhythmParser)
    SOUND_TEMPO_INFO,       // mValue parser, TempoInfoCommand
    SOUND_TEMPO_REJECT,     // mValue parser
    SOUND_CHORD_LOAD,       // const void* table, mArg 1: keep the chord and scale numbers
    SOUND_CHORD_INVALIDATE,
    SOUND_SPEAKER_SETUP,    // SpeakerSetupCommand: the speaker's memory, and its job listed
    SOUND_SPEAKER_PAD,      // mArg controller, mValue 1: connected
    SOUND_SPEAKER_START,    // speaker voice, mArg controller, SpeakerStartCommand
    SOUND_SPEAKER_STATE,    // speaker voice, mValue SpeakerVoice::State
    SOUND_SPEAKER_STOP,     // speaker voice, mArg 0: fade out, 1: release; mValue blocks
    SOUND_SPEAKER_RELEASE,  // speaker voice
    SOUND_COMMANDS
};

struct EffectLineCommand {
    u8 mLine;
    bool mRestart;
    bool mHasConfig;
    // An effect line's settings as the game's effect settings write them.
    u8 mFilter;
    u16 mBusA;
    s16 mVolumeA;
    u16 mBusB;
    s16 mVolumeB;
    u32 mSubframes;
    s16 mFir[8];
};

struct ResetCommand {
    u32 mTime;
    bool mStopClock;
};

struct WaveGroupCommand {
    u32 mWaveBank;
    u32 mGroup;
    const u8* mData;
    u32 mBytes;
    bool mLive;
};

struct SeqCommand {
    const SeqFile* mFile;
    u32 mEntryOp;
};

struct MgrSoundCommand {
    bool mHas;
    SoundParams mSound;
};

struct TrackCommand {
    TrackPath mPath;
    u8 mPort;
    s32 mValue;
};

struct TrackSoundCommand {
    TrackPath mPath;
    bool mHas;
    SoundParams mSound;
};

struct StreamMixCommand {
    u8 mChannel;  // 0xff: every channel
    f32 mVolume;
    f32 mPan;
    f32 mFxMix;
    f32 mDolby;
    f32 mPitch;
};

struct MeStartCommand {
    const u8* mData;
    u32 mStart;
    SoundParams mSound;
};

struct TempoInfoCommand {
    StreamPlayer* mStream;
    f32 mBeatMul;
    u32 mIntroBeats;
    u32 mLoopBeats;
    u32 mLoopStartSamples;
    u32 mLoopEndSamples;
};

// The audio thread: commands, the voices' update and mix, then the sequencers and streams, each 2.5 ms tick.
class SoundEngine {
public:
    static const u32 cTicksPerSecond = 400;
    static const u32 cTicksPerStep = 7;
    static const u32 cMaxCatchUp = 28;
    static const u32 cVoices = 4096;
    // Bank numbers are bytes in the programs (a track's bank register).
    static const u32 cBankNumbers = 256;

    static SoundEngine* get() {
        return sEngine;
    }

    // Game thread, before the clock starts.
    static SoundEngine* create(NebulaArena* pArena);
    void setupBank(const BankFile* pFile, u32 size, NebulaArena* pArena);
    void setupWaveBank(u32 index, const WaveBankFile* pFile, u32 size, NebulaArena* pArena);
    void setupWaveBanks(u32 count, NebulaArena* pArena);
    void startClock();
    SoundRing& ring() {
        return mRing;
    }
    // Game thread: the remote speakers' mix for `pads` controllers (SpkSpeakerCtrl::setup), in memory sent with the command.
    void setupSpeaker(u32 pads);
    // The commands run, and the reset's end (read by the game thread).
    u64 applied() const {
        return mRing.applied();
    }
    bool resetDone() const {
        return mResetDone;
    }

    // The host's clock thread (nebula_game_sound_frame).
    void frame();

    // The audio thread's parts.
    Voices& voices() {
        return mVoices;
    }
    RhythmSystem& rhythm() {
        return mRhythm;
    }
    FastRandom& seqRandom() {
        return mSeqRandom;
    }
    u32 outputMode() const {
        return mOutputMode;
    }
    bool prepareNote(u32 bankNumber, s32 program, u8 key, u8 velocity, u16 priority, SoundNote* pNote);
    void noteEvent(SoundNote* pNote, u32 event, NebulaVoice* pVoice);
    u8 noteSound(const SoundNote& rNote, u32* pId) const;
    void mixVoices(u32 used);
    void releaseHolder(NoteHolder* pHolder);
    void addJob(SoundJob* pJob);
    void removeJob(SoundJob* pJob);
    void reportTrackBound();
    void reportBadTarget(u32 offset);

private:
    SoundEngine() = default;
    void tick();
    void apply(const SoundCommand& rCommand);
    void applyPlayer(SoundPlayer* pPlayer, const SoundCommand& rCommand);
    void runTracks();
    // The jobs by place (the tracks, the streams; with `speaker` the remote speaker's mix too).
    void runJobs(bool speaker);
    void setOutputLevel(f32 level);
    void setEffectLine(const EffectLineCommand& rLine);
    void resetStep();
    void publish(SoundPlayer* pPlayer);
    void tryRelease(SoundPlayer* pPlayer);
    SeqTrack* track(SoundPlayer* pPlayer, const TrackPath& rPath);

    static SoundEngine* sEngine;

    SoundRing mRing;
    Voices mVoices;
    RhythmSystem mRhythm;
    SpeakerMixer mSpeaker;
    SoundJob mSpeakerJob;
    NebulaVoice* mHostVoices = nullptr;
    NebulaEffectLine* mEffects = nullptr;
    bool mEffectStarted[4] = {};
    f32 mOutputLevel = 0.0f;
    f32 mMixerGain = 4.0f;
    u32 mOutputMode = 1;
    u32 mFramesAsked = 0;
    // The world clock's tape (services/time.md 7): the rate followed now (thousandths) and the thousandths of a logic
    // tick carried to the next output tick. The audio clock's thread only.
    u32 mTapeRate = 1000;
    u32 mTapeAcc = 0;

    // The banks by number, their oscillators, and the wave banks with their tables.
    SoundBank mBanks[cBankNumbers];
    bool mHasBank[cBankNumbers] = {};
    WaveBank* mWaveBanks = nullptr;
    u32 mWaveBankCount = 0;
    FastRandom mInstRandom = FastRandom(0);
    FastRandom mSeqRandom = FastRandom(0);

    // The playing roots in start order, and the jobs by place.
    SoundPlayer* mFirstRoot = nullptr;
    SoundPlayer* mLastRoot = nullptr;
    SoundJob mTracksJob;
    SoundJob* mFirstJob = nullptr;

    // The reset's fade: ticks left, the level it started from.
    bool mResetting = false;
    bool mResetStopClock = false;
    u32 mResetTime = 0;
    f32 mResetLevel = 0.0f;
    NebulaAtomic< bool > mResetDone = true;
};
