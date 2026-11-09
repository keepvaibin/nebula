#pragma once

#include "Game/Sound/MeLink.hpp"
#include "Game/Sound/SoundGlobal.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/NebulaList.hpp"

class AudMeHandle;
class NebulaArena;
struct MeProgramFile;
struct MeEntry;

// An ME's playing parameters as the importer cooks them (native_data.rs rhythm).
struct AudMePlayingParams {
    u8 mPriority;
    u8 mChordVolume;
    u8 mPan;
    u8 mDolby;
    u16 mFadeTime;
};
static_assert(sizeof(AudMePlayingParams) == 6, "the cook's ME record");

class AudMePlayingParamsHolder {
public:
    AudMePlayingParamsHolder() : mNumEntries(), mParams(), mBase(), mNameOffsets() {
    }

    void setResource(void*);

    u32 mNumEntries;
    const AudMePlayingParams* mParams;
    // The ME table file (host order, as the importer cooks it) and its name offsets from the file's start.
    const u8* mBase;
    const u32* mNameOffsets;

    const char* getName(u32 index) const {
        return reinterpret_cast< const char* >(mBase + mNameOffsets[index]);
    }
};

// One Me phrase as the game plays it: its fades and the values it sends its player each frame.
class AudMe : public NebulaListLink< AudMe > {
public:
    static const s32 TIMED_PARAMS = 6;

    enum MoveParam {
        MoveParam_Volume = 0,
        MoveParam_Pitch = 1,
        MoveParam_FxMix = 2,
        MoveParam_Pan = 3,
        MoveParam_Dolby = 4,
        MoveParam_DistFilter = 5,
    };

    struct MoveParam_ {
        f32 mValue;
        f32 mTarget;
        f32 mDelta;
    };

    AudMe();
    ~AudMe();

    void requestStart(u32 trackBound, u32 stackBound, u32 place);
    void start();
    void stop(s32);
    void release();
    void attachHandle(AudMeHandle*);
    void releaseHandle();
    void update();
    void setTimedParam(s32, f32, u32);
    void initTimed();
    void updateTimedParams();
    void setOuterPlayingParams(f32, f32, f32, s32, s32);
    void doneStop();
    void setPauseFlag(bool);
    bool isPlaying() const {
        return mLink.exists();
    }
    // A phrase started at `place` that has not ended.
    bool isRequested(u32 place) const;

    AudMeHandle* mHandle;
    MeLink mLink;
    const Vec3f* mPos;
    s32 mMeId;
    s32 mStopTime;
    u32 mTimer;
    union {
        struct {
            MoveParam_ mVolumeParam;
            MoveParam_ mPitchParam;
            MoveParam_ mFxMixParam;
            MoveParam_ mPanParam;
            MoveParam_ mDolbyParam;
            MoveParam_ mDistFilterParam;
        };
        MoveParam_ mMoveParams[TIMED_PARAMS];
    };
    bool mStartRequested;
    u32 mTrackBound;
    u32 mStackBound;
    u32 mPlace;
    f32 mMgrVolume;
    f32 mVolume;
    f32 mOuterPan;
    f32 mOuterDolby;
    s32 mPriority;
    s32 mFadeTime;

private:
    SoundParams sound() const;
};

class AudMeMgr : public SoundGlobal< AudMeMgr > {
public:
    AudMeMgr(NebulaArena* pArena);

    void setMeSeq(const void*);
    void setMeTable(void*);
    bool startMe(u32, AudMeHandle*, const Vec3f*);
    void freeDeadMe();
    void updateEachMe();
    void stopAll(s32);
    void setPauseFlagAll(bool);
    void update();
    const MeEntry* getEntry(u32);
    bool isRequestedMe(u32);
    void setOuterPlayingParam(s32, AudMe*);
    // The program bytes every phrase reads.
    const u8* programData() const;

    u8 getPriority(int index) {
        return mParams.mParams[index].mPriority;
    }

    NebulaArena* mArena;
    const MeProgramFile* mMeSeq;
    NebulaList< AudMe > mMeList;
    f32 mMeVolume;
    AudMePlayingParamsHolder mParams;
};
