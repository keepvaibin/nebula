#pragma once

#include <JSystem/JAudio2/JASGlobal.hpp>
#include <JSystem/JAudio2/JASHeapCtrl.hpp>
#include <JSystem/JGeometry/TVec.hpp>
#include <JSystem/JSupport/JSUList.hpp>

class AudMeHandle;
class AudMeTrack;

struct AudMePlayingParams {
    u8 mPriority;
    u8 mChordVolume;
    u8 _2;
    u8 _3;
    u16 _4;
    u8 _6;
    u8 _7;
};

class AudMePlayingParamsHolder {
public:
    AudMePlayingParamsHolder() : mNumEntries(), mParams(), mBase(), mNameOffsets() {
    }

    void setResource(void*);

    u32 mNumEntries;
    AudMePlayingParams* mParams;
    // The ME table file (host order, as the importer cooks it) and its name offsets from the file's start.
    const u8* mBase;
    const u32* mNameOffsets;

    const char* getName(u32 index) const {
        return reinterpret_cast< const char* >(mBase + mNameOffsets[index]);
    }
};

class AudMe : public JSULink< AudMe >, public JASPoolAllocObject< AudMe > {
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

    void requestStart(void*, u32);
    bool start(void*, u32);
    void stop(s32);
    void release();
    void attachHandle(AudMeHandle*);
    void releaseHandle();
    void update();
    void setTimedParam(s32, f32, u32);
    void initTimed();
    void updateTimedParams(bool);
    void setOuterPlayingParams(f32, f32, f32, s32, s32);
    void doneStop();
    void setPauseFlag(bool);

    AudMeHandle* mHandle;
    AudMeTrack* mTrack;
    u32 _18;
    const TVec3f* mPos;
    s32 mMeId;
    s32 _24;     // mStopTime
    u32 mTimer;  // mTimer
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
    s32 _74;
    void* mData;
    u32 _7C;
    f32 _80;
    f32 mVolume;
    f32 _88;
    f32 _8C;
    s32 mPriority;
    s32 _94;
};

struct AudMeTable {
    s32 mNumEntries;
    s32 mSeqStartPos[1];
};

class AudMeMgr : public JASGlobalInstance< AudMeMgr > {
public:
    AudMeMgr();

    void setMeSeq(void*);
    void setMeTable(void*);
    bool startMe(u32, AudMeHandle*, const TVec3f*);
    void freeDeadMe();
    void updateEachMe();
    void stopAll(s32);
    void setPauseFlagAll(bool);
    void update();
    s32 getSeqStartPos(u32);
    bool isRequestedMe(u32);
    void setOuterPlayingParam(s32, AudMe*);

    u8 getPriority(int index) {
        return mParams.mParams[index].mPriority;
    }

    AudMeTable* mMeSeq;
    JSUList< AudMe > mMeList;
    u8 _10[64];  // mNotes?
    f32 _50;
    AudMePlayingParamsHolder mParams;
};
