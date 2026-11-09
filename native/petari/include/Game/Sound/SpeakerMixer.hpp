#pragma once

#include "Game/Sound/SoundCommand.hpp"
#include "Game/System/NebulaAtomic.hpp"

// A speaker sound's volume over its blocks: its release or fade out ends it.
struct SpeakerEnvelope {
    void setRelease(s32 blocks);
    void setFadeOut(s32 blocks);
    // The block's volume; `rKill` once the release or fade out is done.
    f32 calc(bool& rKill);

    f32 mReleaseVolume = 1.0f;
    f32 mReleaseRate = 0.0f;
    f32 mFadeOutVolume = 1.0f;
    f32 mFadeOutRate = 0.0f;
    f32 mFadeVolume = 1.0f;
    f32 mFadeTarget = 1.0f;
    f32 mFadeRate = 0.0f;
    f32 mBaseVolume = 1.0f;
};

struct SpeakerStartCommand {
    const s16* mWave;
    s32 mSize;
    s32 mLoopStart;
    s32 mLoopEnd;
    f32 mBaseVolume;
};

// One speaker sound's record, made by the game thread (SpkSound) and handed over with its start command. The game frees it once
// mReleased says the audio thread has let go.
class SpeakerVoice {
public:
    enum State : s32 { STATE_NONE, STATE_LOCK, STATE_UNLOCK, STATE_STOP, STATE_DEAD };

    // Read by the game thread: the state, which the wave's end or the envelope's makes STATE_DEAD.
    NebulaAtomic< s32 > mShownState = STATE_NONE;
    NebulaAtomic< bool > mReleased = false;

    // The audio thread's state and the game thread's view of it.
    void setState(s32 state) {
        mState = state;
        mShownState = state;
    }

    // The audio thread's.
    s32 mState = STATE_NONE;
    const s16* mWave = nullptr;
    s32 mWavePtr = 0;
    s32 mWaveSize = 0;
    s32 mLoopStart = -1;
    s32 mLoopEnd = -1;
    SpeakerEnvelope mVolume;
    SpeakerVoice* mNext = nullptr;
};

// The controllers' speaker sound on the audio thread: per controller its sounds, mixed 40 samples (6 kHz) at a time on the clock,
// and the blocks to the host (speaker_samples), which plays them where the speaker is.
class SpeakerMixer {
public:
    static const u32 cBlockSamples = 40;

    // The pads' state, in memory the game makes (SoundEngine::setupSpeaker) and sends with SOUND_SPEAKER_SETUP.
    static u32 bytes(u32 pads);
    void setup(void* pMemory, u32 pads);
    void apply(const SoundCommand& rCommand);
    // Each tick, as the engine's job (SOUND_SPEAKER_SETUP lists it).
    static void job(void* pSelf);

private:
    void block();
    void mix(u32 pad, const s16* pWave, s32 count, f32 volume, s32 offset);
    void update(SpeakerVoice* pVoice, u32 pad, f32 volume);
    void unlist(SpeakerVoice* pVoice, u32 pad);

    // 6 kHz on the 400 Hz clock: 15 samples a tick, a block each 40.
    u32 mSamplesDue = 0;
    // Per controller (mPads, 0 before setup): connected, its sounds in the order they started (the mix clamps after each),
    // and its block.
    u32 mPads = 0;
    bool* mConnected = nullptr;
    SpeakerVoice** mFirst = nullptr;
    s16 (*mSamples)[cBlockSamples] = nullptr;
};

struct SpeakerSetupCommand {
    void* mMemory;
    u32 mPads;
};
