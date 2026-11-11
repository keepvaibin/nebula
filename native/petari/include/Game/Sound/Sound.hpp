#pragma once

#include "Game/Sound/SoundAudible.hpp"
#include "Game/Sound/SoundControls.hpp"
#include "Game/Sound/SoundEngine.hpp"
#include <stdint.h>

class Sound;
class PlayerLink;

// A sound's number, 0xSSGGWWWW: its section (effect, sequence, stream), group (an effect's category) and wave.
class SoundID {
public:
    enum SoundType {
        SOUND_SE = 0,
        SOUND_SEQ = 1,
        SOUND_STREAM = 2,
    };

    SoundID() {
    }

    SoundID(u32 id) : mValue(id) {
    }

    SoundID(u32 sectionID, u32 groupID, u32 waveID) {
        mValue = (u32)(u8)sectionID << 24 | (u32)(u8)groupID << 16 | (u16)waveID;
    }

    operator u32() const {
        return mValue;
    }

    bool isAnonymous() const {
        return mValue == 0xffffffff;
    }

    void setAnonymous() {
        mValue = 0xffffffff;
    }

    u8 getSectionID() const {
        return (u8)(mValue >> 24);
    }

    void setSectionID(u8 id) {
        mValue = (mValue & 0x00FFFFFF) | (u32)id << 24;
    }

    u8 getGroupID() const {
        return (u8)(mValue >> 16);
    }

    void setGroupID(u8 id) {
        mValue = (mValue & 0xFF00FFFF) | (u32)id << 16;
    }

    u16 getWaveID() const {
        return (u16)mValue;
    }

    void setWaveID(u16 id) {
        mValue = (mValue & 0xFFFF0000) | id;
    }

    void set(u8 sectionID, u8 groupID, u16 waveID) {
        setSectionID(sectionID);
        setGroupID(groupID);
        setWaveID(waveID);
    }

    u32 mValue;
};

class SoundHandle {
public:
    SoundHandle() : mSound(nullptr) {
    }

    ~SoundHandle() {
        releaseSound();
    }

    Sound* operator->() const {
        return mSound;
    }

    Sound* getSound() {
        return mSound;
    }

    bool isSoundAttached() const {
        return mSound != nullptr;
    }

    void releaseSound();

    Sound* mSound;
};

// Where a sound is in its life, and the flags its owner sets.
struct SoundStatus {
    s32 lockWhenPrepared();
    s32 unlockIfLocked();

    enum State {
        State_PREPARE = 0,
        State_LOCK_PREPARE = 1,
        State_STOP = 2,
        State_LOCK_READY = 3,
        State_READY = 4,
        State_PLAYING = 5,
        State_DEAD = 6,
    };

    void init() {
        mMute = false;
        mPaused = false;
        mComesBack = false;
        mHasLifeTime = false;
        mPauseWhenOut = false;
        mState = State_PREPARE;
        mStopping = false;
        mFadingOut = false;
        mCalcedOnce = false;
        mAnimationState = 0;
        mUserData = 0;
    }

    u8 getState() const {
        return mState;
    }

    u8 setState(u8 state) {
        return mState = state;
    }

    bool isAlive() const {
        return getState() != State_DEAD;
    }
    bool isDead() const {
        return getState() == State_DEAD;
    }
    void die() {
        setState(State_DEAD);
    }
    void stop() {
        setState(State_STOP);
    }
    bool isStopped() const {
        return getState() == State_STOP;
    }

    bool isPlaying() const {
        return getState() == State_PLAYING;
    }
    void setPlaying() {
        setState(State_PLAYING);
    }

    void setReadyLocked() {
        setState(State_LOCK_READY);
    }

    void setReady() {
        setState(State_READY);
    }

    bool isPrepared() const {
        return getState() >= State_LOCK_READY;
    }
    bool isMute() const {
        return mMute;
    }

    void pause(bool pause) {
        mPaused = pause;
    }
    bool isPaused() const {
        return mPaused;
    }

    void pauseWhenOut() {
        mPauseWhenOut = true;
    }

    bool comesBack() const {
        return mComesBack;
    }
    bool hasLifeTime() const {
        return mHasLifeTime;
    }
    bool isPauseWhenOut() const {
        return mPauseWhenOut;
    }

    bool isCalcedOnce() const {
        return mCalcedOnce;
    }
    void setCalcedOnce(bool calcedOnce) {
        mCalcedOnce = calcedOnce;
    }

    // SoundAnimator's mark (0 not animated).
    s32 getAnimationState() const {
        return mAnimationState;
    }

    void setAnimationState(u32 state) {
        mAnimationState = static_cast< u8 >(state & 3);
    }

    uintptr_t getUserData() const {
        return mUserData;
    }

    bool mMute;
    bool mPaused;
    bool mComesBack;
    bool mHasLifeTime;
    bool mPauseWhenOut;
    u8 mState;
    // Asked to stop; mFadingOut: once the fader is out.
    bool mStopping;
    bool mFadingOut;
    bool mCalcedOnce;
    u8 mAnimationState;
    uintptr_t mUserData;
};

struct SoundFader {
    void forceIn() {
        mValue = 1.0f;
        mTransition.zero();
    }

    void forceOut() {
        mValue = 0.0f;
        mTransition.zero();
    }

    void fadeOut(u32 maxSteps) {
        if (maxSteps != 0) {
            mTransition.set(0.0f, mValue, maxSteps);
        } else {
            forceOut();
        }
    }

    void fadeIn(u32 maxSteps) {
        if (maxSteps != 0) {
            mTransition.set(1.0f, mValue, maxSteps);
        } else {
            forceIn();
        }
    }

    void fadeInFromOut(u32 maxSteps) {
        mValue = 0.0f;
        fadeIn(maxSteps);
    }

    bool isOut() const {
        return (mTransition.mRemainingSteps == 0 && mValue < 0.01f);
    }

    void calc() {
        mValue = mTransition.apply(mValue);
    }
    f32 getIntensity() const {
        return mValue;
    }

    f32 mValue;
    SoundTransition::Ramp mTransition;
};

// A manager's mute and pause over all its sounds.
class SoundActivity {
public:
    void init() {
        mMuted = false;
        mPaused = false;
    }

    void mute(bool mute) {
        mMuted = mute;
    }

    bool isMute() const {
        return mMuted;
    }

    void pause(bool pause) {
        mPaused = pause;
    }

    bool isPaused() const {
        return mPaused;
    }

    bool mMuted;
    bool mPaused;
};

class SoundActivityOwner {
public:
    SoundActivity mSoundActivity;
};

class SoundTempo {
public:
    SoundTempo() {
        init();
    }
    void init() {
        setTempo(1.0f);
    }
    void setTempo(f32 tempo) {
        mTempo = tempo;
        mTransition.zero();
    }
    f32 getTempo() const {
        return mTempo;
    }
    void calc() {
        mTempo = mTransition.apply(mTempo);
    }

    f32 mTempo;
    SoundTransition::Ramp mTransition;
};

class SoundAudience;
class EffectSound;
class SequenceSound;
struct SoundChild;
class StreamSound;

class Sound {
public:
    Sound();
    virtual ~Sound() {
    }

    void releaseHandle();
    void attachHandle(SoundHandle* handle);

    void startSound_(SoundID id, const Vec3f* posPtr, SoundAudience* audience);
    void stop();
    void stop(u32 fadeTime);
    void dieSound_();
    void increasePrepareCount_();
    bool calcSound_();
    // The player's channel groups: one for each channel the audible has, each with that channel's values.
    void initGroups_(PlayerLink* pLink);
    void sendGroups_(PlayerLink* pLink);

    virtual s32 getNumChild() const = 0;
    virtual SoundChild* getChild(int) = 0;
    virtual void releaseChild(int) = 0;
    virtual EffectSound* asSe();
    virtual SequenceSound* asSeq();
    virtual StreamSound* asStream();
    // The sound's player (none for a stream).
    virtual PlayerLink* getPlayer() = 0;
    // The player and the path of the track the game numbers `index`, when that track is there now; null otherwise.
    virtual PlayerLink* getChildTrack(int index, TrackPath* pPath) = 0;
    virtual SoundTempo* getTempoMgr() = 0;
    virtual bool tryDie_() = 0;

    SoundID getID() const {
        return mSoundID;
    }
    u32 getAnimationState() const {
        return mStatus.getAnimationState();
    }
    bool isAnimated() const {
        return getAnimationState() != 0;
    }
    void setAnimationState(u32 state) {
        mStatus.setAnimationState(state);
    }
    uintptr_t getUserData() const {
        return mStatus.getUserData();
    }
    void setUserData(uintptr_t userData) {
        mStatus.mUserData = userData;
    }
    SoundAudible* getAudible() {
        return mAudible;
    }
    bool isHandleAttached() const {
        return mHandle != nullptr;
    }

    bool isAlive() const {
        return mStatus.isAlive();
    }
    bool isDead() const {
        return mStatus.isDead();
    }
    void die() {
        mStatus.die();
    }

    bool isPlaying() const {
        return mStatus.isPlaying();
    }
    bool isMute() const {
        return mStatus.isMute();
    }

    bool isPauseWhenOut() const {
        return mStatus.isPauseWhenOut();
    }

    bool comesBack() const {
        return mStatus.comesBack();
    }
    bool hasLifeTime() const {
        return mStatus.hasLifeTime();
    }

    void setCalcedOnce(bool calcedOnce) {
        mStatus.setCalcedOnce(calcedOnce);
    }
    bool isCalcedOnce() const {
        return mStatus.isCalcedOnce();
    }

    void removeLifeTime_() {
        mStatus.mComesBack = false;
        mStatus.mHasLifeTime = false;
    }

    bool isPrepared() const {
        return mStatus.isPrepared();
    }
    void unlockIfLocked() {
        mStatus.unlockIfLocked();
    }
    void lockWhenPrepared() {
        mStatus.lockWhenPrepared();
    }
    void stopSound_() {
        mStatus.mFadingOut = false;
        mStatus.mStopping = true;
    }

    bool isStopping() const {
        return mStatus.mStopping && (!mStatus.mFadingOut || mFader.isOut());
    }

    void pause(bool pause) {
        mStatus.pause(pause);
    }

    bool isPaused() const {
        return mStatus.isPaused();
    }

    void pauseWhenOut() {
        mStatus.pauseWhenOut();
    }

    void updateLifeTime(u32 lifeTime) {
        if (lifeTime > mLifeTime) {
            mLifeTime = lifeTime;
        }
    }

    void setLifeTime(u32 lifeTime, bool pauseWhenOut) {
        mLifeTime = lifeTime;
        setComesBack(pauseWhenOut);
        mStatus.mHasLifeTime = true;
    }

    void setComesBack(bool pauseWhenOut) {
        mStatus.mComesBack = true;
        if (pauseWhenOut) {
            mStatus.pauseWhenOut();
        }
    }

    bool setPos(const Vec3f& pos) {
        if (mAudible != nullptr) {
            mAudible->setPos(pos);
        }
        return mAudible != nullptr;
    }

    SoundFader* getFader() const {
        return (SoundFader*)&mFader;
    }
    void fadeIn(u32 maxSteps) {
        mFader.fadeInFromOut(maxSteps);
    }
    void fadeOut(u32 maxSteps) {
        mFader.fadeOut(maxSteps);
    }

    SoundProperty& getProperty() {
        return mParams.mProperty;
    }

    s32 getCount() const {
        return mCount;
    }

    SoundMove& getAuxiliary() {
        return mParams.mMove;
    }

    SoundHandle* mHandle;
    SoundAudible* mAudible;  // &mAudibleData while the sound is positional
    SoundAudible mAudibleData;
    SoundAudience* mAudience;
    u32 mLifeTime;
    s32 mPrepareCount;
    SoundID mSoundID;
    SoundStatus mStatus;
    SoundFader mFader;
    u32 mPriority;
    s32 mCount;
    SoundControls mParams;
};
