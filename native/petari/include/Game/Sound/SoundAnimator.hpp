#pragma once

#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundHandles.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

struct SoundParams;
class SoundHandles;

// One sound of a sound animation as the importer cooks it (crates/nebula-import cook/native_data.rs bas, NBAS).
class SoundAnimationSound {
public:
    enum Flag : u32 {
        cForwardOnly = 0x1,
        cReverseOnly = 0x2,
        cStopOnAnimationChange = 0x4,
        cOnce = 0x8,
        cStopOnNoteOff = 0x10,
        cStopOnSpeedZero = 0x20,
        cLifeTime = 0x40,
        cAtIntervals = 0x80,
    };

    bool playsOnlyForward() const {
        return (mFlags & cForwardOnly) != 0;
    }

    bool playsOnlyReverse() const {
        return (mFlags & cReverseOnly) != 0;
    }

    bool stopsWhenAnimationChanges() const {
        return (mFlags & cStopOnAnimationChange) != 0;
    }

    bool playsOnlyOnce() const {
        return (mFlags & cOnce) != 0;
    }

    bool stopsWhenNoteOff() const {
        return (mFlags & cStopOnNoteOff) != 0;
    }

    bool stopsWhenSpeedIsZero() const {
        return (mFlags & cStopOnSpeedZero) != 0;
    }

    bool setsLifeTime() const {
        return (mFlags & cLifeTime) != 0;
    }

    bool playsAtIntervals() const {
        return (mFlags & cAtIntervals) != 0;
    }

    bool isNoting(f32 time) const {
        if (mNoteOnTime == mNoteOffTime) {
            return true;
        }

        if (mNoteOnTime > mNoteOffTime) {
            if (mNoteOffTime <= time && time < mNoteOnTime) {
                return true;
            }
        } else {
            if (mNoteOnTime <= time && time < mNoteOffTime) {
                return true;
            }
        }

        return false;
    }

    bool isNotingOn(f32 time, bool reversed) const {
        if (setsLifeTime()) {
            return isNoting(time);
        }

        if (reversed) {
            return time <= mNoteOnTime;
        } else {
            return time >= mNoteOnTime;
        }
    }

    bool isNotingOff(f32 time, bool reversed) const {
        if (setsLifeTime()) {
            return !isNoting(time);
        }

        if (mNoteOnTime == mNoteOffTime) {
            return false;
        }

        if (reversed) {
            return time <= mNoteOffTime;
        } else {
            return time >= mNoteOffTime;
        }
    }

    SoundID getSoundID() const {
        return mSoundID;
    }

    SoundID mSoundID;
    f32 mNoteOnTime;
    f32 mNoteOffTime;
    f32 mBasePitch;
    u32 mFlags;  // Flag bits
    u8 mBaseVolume;
    s8 mPitchDelta;
    u8 mPlayTime;
    u8 mBasePan;
    s8 mVolumeDelta;
    u8 mRepeatInterval;
};

static_assert(sizeof(SoundAnimationSound) == 28, "the cooked NBAS record");

// A sound animation as the importer cooks it (NBAS): this header, then mNumSounds sounds.
class SoundAnimation {
public:
    // pFile, checked once when its archive is mounted (a file the importer did not cook is fatal).
    static SoundAnimation* load(void* pFile, const char* pName);

    u32 getStartSoundIndex(f32) const;
    u32 getEndSoundIndex(f32) const;
    s32 getNumSounds() const {
        return static_cast< s32 >(mNumSounds);
    }

    const SoundAnimationSound* getSound(int index) const {
        return reinterpret_cast< const SoundAnimationSound* >(this + 1) + index;
    }

    char mMagic[4];  // "NBAS"
    u32 mNumSounds;
};

static_assert(sizeof(SoundAnimation) == 8, "the cooked NBAS header");

class SoundAnimator {
public:
    SoundAnimator(SoundHandles*);

    SoundHandle* getFreeHandle(const SoundAnimationSound* pAnimation);
    // Whether the sound plays at this frame and in this direction.
    bool playsSound(const SoundAnimationSound* pAnimation, const Vec3f& rPos, f32 speed);
    // The speed counts up to 3.
    void modifySoundParams(SoundParams* pParams, const SoundAnimationSound* pAnimation, f32 time);

    void ageSounds_();
    void setLoopFrame(f32 loopStartFrame, f32 loopEndFrame) {
        mLoopStartFrame = loopStartFrame;
        mLoopStartSoundIndex = mSoundAnimation->getStartSoundIndex(loopStartFrame);
        mLoopEndFrame = loopEndFrame;
        mLoopEndSoundIndex = mSoundAnimation->getEndSoundIndex(loopEndFrame);
    }

    void startAnimation(const SoundAnimation* pAnimation, bool reversed, f32 loopStartFrame, f32 loopEndFrame);
    void removeAnimation();
    void updateSoundLifeTime_(f32 time, f32 speed);

    Sound* getSound(int index) {
        return mHandles->getHandle(index)->getSound();
    }

    s32 getNumHandles() const {
        return mHandles->mNumHandles;
    }

    SoundHandles* mHandles;
    const SoundAnimation* mSoundAnimation;
    bool mIsReversed;
    s32 mLoopSoundIndex;
    f32 mLifeTime;
    s32 mLoopStartSoundIndex;
    s32 mLoopEndSoundIndex;
    f32 mLoopStartFrame;
    f32 mLoopEndFrame;
    s32 mTime;
};
