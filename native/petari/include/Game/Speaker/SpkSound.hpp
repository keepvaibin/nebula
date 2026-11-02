#pragma once

#include <JSystem/JAudio2/JASGlobal.hpp>
#include <JSystem/JAudio2/JASHeapCtrl.hpp>
#include <JSystem/JSupport/JSUList.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

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

class SpkSoundVolume {
public:
    SpkSoundVolume();

    void setRelease(s32);
    void setFadeOut(s32);
    f32 calc(bool&);

    f32 mReleaseVolume;
    f32 mReleaseRate;
    f32 mFadeOutVolume;
    f32 mFadeOutRate;
    f32 mFadeVolume;
    f32 mFadeTarget;
    f32 mFadeRate;
    f32 mBaseVolume;
};

class SpkSound : public JSULink< SpkSound >, public JASPoolAllocObject< SpkSound > {
public:
    enum State {
        State_0 = 0,
        State_LOCK = 1,    // TODO: probably better name
        State_UNLOCK = 2,  // TODO: probably better name
        State_STOP = 3,
        State_DEAD = 4,
    };

    SpkSound();
    ~SpkSound();

    void update(f32);
    void stop(s32);
    void attachHandle(SpkSoundHandle*);
    void releaseHandle();
    bool start(s32, s32);
    void unlock();
    bool setWaveData(s32);

    bool isLoopValid() const {
        return mLoopStartPos >= 0 && mLoopEndPos > mLoopStartPos;
    }

    SpkSoundHandle* mSoundHandle;
    s32 mWaveID;
    s16* mWave;
    s32 mWavePtr;
    s32 mPadChannel;
    u32 _24;
    u32 mReleaseTime;
    s32 mWaveSize;
    s32 mLoopStartPos;
    s32 mLoopEndPos;
    s32 mState;
    s32 _3C;
    SpkSoundVolume mVolume;
};

class SpkSoundHolder : JASGlobalInstance< SpkSoundHolder > {
public:
    SpkSoundHolder();

    bool startSound(s32, s32, SpkSoundHandle*);
    bool update(s32);
    bool updateEachSound(s32);
    void freeDeadSound(s32);
    void framework();

    f32 getBaseVolume() const {
        return mBaseVolume;
    }

    f32 get_44() const {
        return _44 / 15.0f;
    }

    JSUList< SpkSound > mSoundList[4];
    f32 mVolume[4];
    f32 mBaseVolume;
    s32 _44;
};
