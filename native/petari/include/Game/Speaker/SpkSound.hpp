#pragma once

#include "Game/Sound/SoundGlobal.hpp"
#include "Game/Sound/SpeakerMixer.hpp"
#include "Game/System/NebulaInput.hpp"
#include "Game/System/NebulaList.hpp"

class NebulaArena;
class SpkSound;

class SpkSoundHandle {
public:
    SpkSoundHandle() : mSound(nullptr) {
    }

    ~SpkSoundHandle() {
        releaseSound();
    };

    void releaseSound();

    inline SpkSound* getSound() {
        return mSound;
    }

    SpkSound* mSound;
};

// One speaker sound as the game plays it: started locked, heard from the next frame, stopped by a fade out or its release. The
// audio thread's SpeakerVoice mixes it; its state is the game's own until the audio thread has run the game's last change.
class SpkSound : public NebulaListLink< SpkSound > {
public:
    enum State {
        State_NONE = SpeakerVoice::STATE_NONE,
        State_LOCK = SpeakerVoice::STATE_LOCK,
        State_UNLOCK = SpeakerVoice::STATE_UNLOCK,
        State_STOP = SpeakerVoice::STATE_STOP,
        State_DEAD = SpeakerVoice::STATE_DEAD,
    };

    SpkSound();
    ~SpkSound();

    void stop(s32);
    void attachHandle(SpkSoundHandle*);
    void releaseHandle();
    bool start(s32, s32);
    void unlock();
    // A sound still locked goes before it is heard.
    void kill();
    s32 getState() const;

    SpkSoundHandle* mSoundHandle;
    s32 mWaveID;
    s32 mPadChannel;
    // In 40-sample blocks.
    u32 mReleaseTime;

private:
    void setState(s32 state);

    s32 mState;
    u64 mStateSerial;
    SpeakerVoice* mVoice;
    void* mMemory;
};

class SpkSoundHolder : SoundGlobal< SpkSoundHolder > {
public:
    SpkSoundHolder(NebulaArena* pArena);

    bool startSound(s32, s32, SpkSoundHandle*);
    void freeDeadSound(s32);
    void framework();

    NebulaArena* mArena;
    // Per controller (NebulaInputPlayers::count): its sounds, and the last one AudSpeakerWrap started.
    NebulaList< SpkSound >* mSoundList;
    SpkSoundHandle* mPadHandles;
};
