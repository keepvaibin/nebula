#pragma once

#include "JSystem/JAudio2/JAISound.hpp"
#include "JSystem/JAudio2/JAISoundHandles.hpp"
#include "JSystem/JGeometry.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

struct JASSoundParams;
class JAISoundHandles;

class JAUSoundAnimationSound {
public:
    // TODO: why are some normal rlwinm and other extri and some dont matter?
    bool playsOnlyForward() const {
        return (mFlags & 1) != 0;  // 0x01
    }

    bool playsOnlyReverse() const {
        return (mFlags & 2) != 0;  // 0x02
    }

    bool stopsWhenAnimationChanges() const {
        return (mFlags & 4) != 0;  // 0x04
    }

    bool playsOnlyOnce() const {
        return (mFlags & 8) != 0;  // 0x08
    }

    bool stopsWhenNoteOff() const {
        return (mFlags & 0x10) != 0;  // 0x10
    }

    bool stopsWhenSpeedIsZero() const {
        return (mFlags & 0x20) != 0;  // 0x20
    }

    bool setsLifeTime() const {
        return mFlags & 0x40;  // 0x40
    }

    bool playsAtIntervals() const {
        return (mFlags & 0x80) != 0;  // 0x80
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

    JAISoundID getSoundID() const {
        return mSoundID;
    }

    // A BAS entry, read in place (host order, as the importer cooks it: native_data.rs bas).
    JAISoundID mSoundID;
    f32 mNoteOnTime;
    f32 mNoteOffTime;
    f32 mBasePitch;
     // u32 mFlags;
    // The flags are bits of the word (the accessors above test mFlags).
    u32 mFlags;
    u8 mBaseVolume;
    s8 mPitchDelta;
    u8 mPlayTime;
    u8 mBasePan;
    s8 mVolumeDelta;
    u8 mRepeatInterval;
    s8 _1A;
    u32 _1C;
};

class JAUSoundAnimation;

class JAUSoundAnimationControl {
public:
    virtual ~JAUSoundAnimationControl() = 0;
    virtual JAUSoundAnimationSound* getSound(const JAUSoundAnimation*, int) = 0;
    virtual u16 getNumSounds(const JAUSoundAnimation*) = 0;
};

class JAUSoundAnimation {
public:
    u32 getStartSoundIndex(f32) const;
    u32 getEndSoundIndex(f32) const;
    u16 getNumSounds() const {
        if (mControl != nullptr) {
            return mControl->getNumSounds(this);
        } else {
            return mNumSounds;
        }
    }

    const JAUSoundAnimationSound* getSound(int i_index) const {
        if (mControl != nullptr) {
            return mControl->getSound(this, i_index);
        } else {
            return &mSounds + i_index;
        }
    }

    u16 mNumSounds;
    // BAS files hold 0 here.
    u32 mControlSlot;
    static constexpr JAUSoundAnimationControl* mControl = nullptr;
    JAUSoundAnimationSound mSounds;
};

class JAUSoundAnimator {
public:
    JAUSoundAnimator(JAISoundHandles*);

    virtual JAISoundHandle* getFreeHandle(const JAUSoundAnimationSound* pAnimation);
    virtual u32 getSoundID(const JAUSoundAnimationSound* pAnimation, const TVec3f& rPos, f32);
    virtual bool playsSound(const JAUSoundAnimationSound* pAnimation, const TVec3f& rPos, f32);
    virtual void modifySoundParams(JASSoundParams* pParams, const JAUSoundAnimationSound* pAnimation, f32 time);

    void ageSounds_();
    void setLoopFrame(f32 loopStartFrame, f32 loopEndFrame) {
        mLoopStartFrame = loopStartFrame;
        mLoopStartSoundIndex = mSoundAnimation->getStartSoundIndex(loopStartFrame);
        mLoopEndFrame = loopEndFrame;
        mLoopEndSoundIndex = mSoundAnimation->getEndSoundIndex(loopEndFrame);
    }

    void startAnimation(const JAUSoundAnimation* pAnimation, bool reversed, f32 loopStartFrame, f32 loopEndFrame);
    void removeAnimation();
    void updateSoundLifeTime_(f32 time, f32 speed);

    JAISound* getSound(int index) {
        return mHandles->getHandle(index)->getSound();
    }

    s32 getNumHandles() const {
        return mHandles->mNumHandles;
    }

    JAISoundHandles* mHandles;
    const JAUSoundAnimation* mSoundAnimation;
    bool mIsReversed;
    s32 mLoopSoundIndex;
    f32 mLifeTime;
    s32 mLoopStartSoundIndex;
    s32 mLoopEndSoundIndex;
    f32 mLoopStartFrame;
    f32 mLoopEndFrame;
    s32 mTime;
};
