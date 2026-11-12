#pragma once

#include "Game/Sound/SoundData.hpp"
#include "Game/Sound/SoundParams.hpp"
#include <nebula_game_api.h>

// The audio thread's notes and the host mixer's voices they play on. Nothing here is touched by the game thread.

// An envelope or modulation curve's settings.
struct OscData {
    u32 mTarget;
    f32 mRate;
    const SeqPoint* mTable;
    const SeqPoint* mRelease;
    f32 mScale;
    f32 mOffset;
};

struct OscEffect {
    f32 mVolume = 1.0f;
    f32 mPitch = 1.0f;
    f32 mPan = 0.5f;
    f32 mFxMix = 0.0f;
    f32 mDolby = 0.0f;
    f32 mVibrato = 1.0f;
    f32 mTremolo = 1.0f;
};

struct Oscillator {
    enum Target : u32 { TARGET_VOLUME, TARGET_PITCH, TARGET_PAN, TARGET_FXMIX, TARGET_DOLBY, TARGET_VIBRATO, TARGET_TREMOLO };
    enum State : s32 { STOP, RUN, HOLD, RELEASE, DIRECT_RELEASE };

    void initStart(const OscData* pData);
    void incCounter(f32 step);
    f32 getValue() const {
        return mValue * mData->mScale + mData->mOffset;
    }
    void release();
    void update();
    void updateCurrentValue(f32 length);
    bool isValid() const {
        return mData != nullptr;
    }
    bool isStop() const {
        return mState == STOP;
    }
    bool isRelease() const {
        return mState == RELEASE || mState == DIRECT_RELEASE;
    }

    const OscData* mData = nullptr;
    f32 mTime = 0.0f;
    f32 mValue = 0.0f;
    f32 mFrom = 0.0f;
    f32 mTo = 0.0f;
    u16 mPoint = 0;
    u16 mDirectRelease = 0;
    u8 mCurve = 0;
    s32 mState = STOP;
};

struct Lfo {
    f32 getValue() const;
    void incCounter(f32 step);
    void resetCounter() {
        mWait = mDelay;
        mPhase = 0.0f;
    }

    f32 mPhase = 0.0f;
    f32 mDepth = 0.0f;
    f32 mPitch = 1.0f;
    u16 mDelay = 0;
    u16 mWait = 0;
};

// What a track's channel group gives its notes each update.
struct NoteParams {
    f32 mVolume = 1.0f;
    f32 mPitch = 1.0f;
    f32 mBend = 0.0f;
    f32 mPan = 0.5f;
    f32 mFxMix = 0.0f;
    f32 mDolby = 0.0f;
};

// Who hears a note's events (NoteEvent): the engine dispatches by kind.
enum NoteOwner : u8 { OWNER_NONE, OWNER_TRACK, OWNER_ME, OWNER_STREAM };
enum NoteEvent : u32 { NOTE_PLAY, NOTE_START, NOTE_STOP, NOTE_TIMER };

// Player memory a note's envelope reads while it plays (a track's own envelope): the player gets it back once no note holds it.
struct NoteHolder {
    u32 mHeld = 0;
    // OWNER_TRACK: a SoundPlayer's tracks; OWNER_ME: a MePlayer's.
    NoteOwner mKind = OWNER_NONE;
    void* mPlayer = nullptr;
};

struct SoundNote {
    enum Status : s32 { STATUS_STOP, STATUS_PLAY, STATUS_RELEASE };
    enum Kind : u32 { KIND_WAVE = 0, KIND_OSC = 2 };
    // mFormat's low three bits: the wave's format on the disc (the store's samples are all PCM16). Below FORMAT_SAMPLED_NUM
    // the wave plays; FORMAT_PCM16 marks music.
    enum Format : u8 { FORMAT_PCM16 = 3, FORMAT_SAMPLED_NUM = 4, FORMAT_MASK = 7 };

    void free() {
        mOwner = OWNER_NONE;
        mOwnerData = nullptr;
    }
    bool isDolbyMode() const {
        return mMixConfig[0] == 0xffff;
    }

    s32 mStatus = STATUS_STOP;
    bool mPause = false;
    NoteOwner mOwner = OWNER_NONE;
    s32 mSlot = -1;
    void* mOwnerData = nullptr;
    u32 mUpdateTimer = 0;
    Oscillator mOsc[2];
    Lfo mVibrato;
    Lfo mTremolo;
    u16 mMixConfig[6] = {0x150, 0x210, 0x352, 0x412, 0, 0};
    u16 mPriority = 0x13f;
    NoteParams mParams;
    SoundParams mSound;
    f32 mSourceRate = 0.0f;
    s16 mKey = 0;
    u16 mVelocity = 0x7f;
    f32 mKeySweep = 0.0f;
    f32 mKeySweepTarget = 0.0f;
    u32 mKeySweepCount = 0;
    u32 mSkipSamples = 0;
    u32 mKind = KIND_WAVE;
    // The wave: its settings, its first byte and its file's end, and its group's active flag (0: the note ends).
    u8 mFormat = 0;
    u8 mBaseKey = 60;
    u32 mLoop = 0;
    u32 mLoopStart = 0;
    u32 mLoopEnd = 0;
    u32 mSampleCount = 0;
    const u32* mWaveLive = nullptr;
    const u8* mWave = nullptr;
    const u8* mWaveEnd = nullptr;
    u32 mOscillatorWave = 0;
    // The player memory its envelope reads (a track's own envelope), held until the note ends.
    NoteHolder* mHolder = nullptr;
};

class SoundEngine;

// The host's voices and the slots that give them to notes; each slot's note lives with it.
class Voices {
public:
    enum SlotStatus : s32 { SLOT_ACTIVE, SLOT_INACTIVE, SLOT_DROP };
    enum SlotEvent : u32 { SLOT_PLAY, SLOT_START, SLOT_STOP };

    struct Slot {
        s32 mStatus = SLOT_INACTIVE;
        s16 mPriority = -1;
        u32 mFlags = 0;
        u32 mAge = 0;
        bool mHasNote = false;
    };

    void setup(SoundEngine* pEngine, NebulaVoice* pVoices, u32 count, Slot* pSlots, SoundNote* pNotes);
    // A note to its slot: the lowest priority slot is taken, its note dropped. Null when none is free.
    SoundNote* play(const SoundNote& rPrepared, bool force);
    // Its envelopes to their release (directRelease nonzero: that release instead), its slot to the release priority.
    void releaseNote(SoundNote* pNote, u16 directRelease);
    void updateAll();
    // updateAll in its two halves: every slot's update (envelopes, pitch, volumes), then one tick of the mix.
    void updateSlots();
    void mix();
    // The world clock's tape (services-time 7): every voice's playback rate times `scale` (1 at real time). The audio
    // clock's thread only; takes effect at each slot's next update.
    static void setRateScale(f32 scale);
    // Every playing slot's note gone (the reset's end).
    void dropActive();
    // Notes whose owner lies in [pBegin, pEnd) hear nothing more (a player's memory goes back to the game).
    void forgetOwners(const void* pBegin, const void* pEnd);
    void setSlotPriority(s32 slot, u8 priority) {
        mSlots[slot].mPriority = priority;
    }
    NebulaVoice* voice(s32 slot) {
        return &mVoices[slot];
    }
    u32 used() const {
        return mUsed;
    }

private:
    s32 lowestSlot(int priority);
    void drop(s32 slot);
    void updateSlot(s32 slot);
    s32 noteCallback(s32 slot, u32 type);
    void endNote(s32 slot);
    s32 initialUpdate(s32 slot);
    s32 update(s32 slot);
    void effectorParams(SoundNote& rNote, NebulaVoice* pVoice, f32* pVolumes, const OscEffect& rEffect);
    void mixerVolumes(const SoundNote& rNote, f32 volume, f32 pan, f32 fxmix, f32 dolby, f32* pVolumes);
    void nameSound(s32 slot);

    SoundEngine* mEngine = nullptr;
    NebulaVoice* mVoices = nullptr;
    Slot* mSlots = nullptr;
    SoundNote* mNotes = nullptr;
    u32 mCount = 0;
    // Slots past this have never been used: nothing walks them.
    u32 mUsed = 0;
};

// A voice's filters as a track sets them.
namespace VoiceFilter {
    inline void setMode(NebulaVoice* pVoice, u16 mode) {
        const u8 iirOn = mode & 0x20;
        u8 taps = mode & 0x1f;
        if (taps > (iirOn ? 0x14 : 0x18)) {
            taps = iirOn ? 0x14 : 0x18;
        }
        pVoice->fir_taps = taps;
        pVoice->iir_on = iirOn != 0;
    }
    inline void setIIR(NebulaVoice* pVoice, const s16* pCoefficients) {
        for (int i = 0; i < 4; i++) {
            pVoice->iir[i] = pCoefficients[i] / 32768.0f;
        }
    }
    inline void setFIR8(NebulaVoice* pVoice, const s16* pCoefficients) {
        for (int i = 0; i < 8; i++) {
            pVoice->fir[i] = pCoefficients[i] / 32768.0f;
        }
    }
}  // namespace VoiceFilter

namespace SoundMath {
    // 2 to the power x by the curve the game's pitches were tuned to.
    f32 pow2(f32 x);
    inline f32 clamp01(f32 x) {
        if (x <= 0.0f) {
            return 0.0f;
        }
        if (x >= 1.0f) {
            return 1.0f;
        }
        return x;
    }
}  // namespace SoundMath
