#pragma once

#include "Game/AudioLib/AudBgmRhythmStrategy.hpp"
#include "Game/AudioLib/AudTrackController.hpp"
#include "Game/Sound/Sound.hpp"

class SoundID;
class AudBgmVolumeController;

class AudBgm {
public:
    AudBgm();

    virtual void init() = 0;
    virtual SoundHandle* start(u32, bool) = 0;
    virtual void stop(u32) = 0;
    virtual bool isPreparedPlay() = 0;
    virtual void playAfterPrepared() = 0;
    virtual void movement() = 0;
    virtual bool moveVolume(f32, u32) = 0;
    virtual bool moveVolumeForNoteFairy(f32, u32) = 0;
    virtual void changeTrackMuteState(s32, s32) = 0;
    virtual SoundHandle* getHandle() = 0;
    virtual SoundHandle* getRhythmHandle() = 0;
    virtual bool isSoundAttached() const = 0;
    virtual void pause(bool) = 0;
    virtual bool isStopping() const = 0;
    virtual bool isPaused() const = 0;
    virtual SoundID getSoundID() const = 0;
    virtual void setVolumeController(AudBgmVolumeController* pController) {
        mVolumeController = pController;
    }

    virtual AudBgmRhythmStrategy* getRhythmStrategy() {
        return &mRhythmStrategy;
    }

    virtual void sendToSyncStream() = 0;
    virtual void rejectFromSyncStream() = 0;
    virtual void resetAuxVolume();

    static const s32 mNumTracks = 16;

    AudBgmVolumeController* mVolumeController;
    AudBgmRhythmStrategy mRhythmStrategy;
};

class AudSingleBgm : public AudBgm {
public:
    AudSingleBgm();

    virtual void init();
    virtual SoundHandle* start(u32, bool);
    virtual void stop(u32);
    virtual bool isPreparedPlay();
    virtual void playAfterPrepared();
    virtual void movement();
    virtual bool moveVolume(f32, u32);
    virtual bool moveVolumeForNoteFairy(f32, u32);
    virtual void changeTrackMuteState(s32, s32);
    virtual SoundHandle* getHandle() {
        return &mHandle;
    }

    virtual SoundHandle* getRhythmHandle();
    virtual bool isSoundAttached() const {
        return mHandle.isSoundAttached();
    }

    virtual void pause(bool pause) {
        if (mHandle.isSoundAttached()) {
            mHandle->pause(pause);
        }
    }

    virtual bool isStopping() const {
        if (mHandle.isSoundAttached()) {
            return mHandle->isStopping();
        }

        return true;
    }

    virtual bool isPaused() const {
        if (mHandle.isSoundAttached()) {
            return mHandle->isPaused();
        }

        return false;
    }

    virtual SoundID getSoundID() const {
        if (!mHandle.isSoundAttached()) {
            return 0;
        }

        return mHandle->mSoundID;
    }

    virtual void sendToSyncStream() {
    }

    virtual void rejectFromSyncStream() {
    }

    void initTrackController();
    void startTrackControl();
    void stopTrackControl();
    void updateTrackControl();

    SoundHandle mHandle;
    SoundID mSoundID;
    AudTrackController mTrackController[mNumTracks];
};

class AudMultiBgm : public AudBgm {
public:
    AudMultiBgm();

    virtual void init();
    virtual SoundHandle* start(u32, bool);
    virtual void stop(u32);
    virtual bool isPreparedPlay();
    virtual void playAfterPrepared();
    virtual void movement();
    virtual bool moveVolume(f32, u32);
    virtual bool moveVolumeForNoteFairy(f32, u32);
    virtual void changeTrackMuteState(s32, s32);
    virtual SoundHandle* getHandle();
    virtual SoundHandle* getRhythmHandle();
    virtual bool isSoundAttached() const;
    virtual void pause(bool);
    virtual bool isStopping() const;
    virtual bool isPaused() const;
    virtual SoundID getSoundID() const;
    virtual void sendToSyncStream();
    virtual void rejectFromSyncStream();

    void initTrackController();
    void startTrackControl();
    void updateTrackControl();
    SoundHandle* prepare(u32);
    bool isPrepared();
    void unlock();
    void updateSyncProcess();
    void pauseSyncProcess();
    void setStreamVolume(f32, f32);

    static const s32 mNumFaders = 2;

    SoundHandle mHandle;
    SoundHandle mRhythmHandle;
    AudTrackController mTrackController[mNumTracks];
    AudFader mFader[mNumFaders];
    s32 mSyncState;
    s32 mBgmId;
    bool mIsLocked;
};
