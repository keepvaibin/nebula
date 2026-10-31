#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <revolution/thp.h>
#include "Game/System/NebulaDisc.hpp"

class THPSimplePlayerWrapper : public NerveExecutor {
public:
    THPSimplePlayerWrapper(const char*);

    bool init(s32);
    void quit();
    bool open(const char*);
    bool close();
    s32 getUseTextureCount() const;
    u32 calcNeedMemory();
    bool setBuffer(u8*);
    bool preLoad(s32);
    bool loadStop();
    s32 decode(s32);
    s32 drawCurrentFrame(GXRenderModeObj*, u32, u32, u32, u32);
    bool getVideoInfo(THPVideoInfo*) const;
    f32 getFrameRate() const;
    s32 getTotalFrame() const;
    bool videoDecode(u8*, u32);  // with the component's size: THP's entropy data is not byte-stuffed
    void readFrameAsync();
    void checkPrefetch();
    void dvdCallBack(s32);
    void readAsyncCallBack(s32);
    s32 getNextBuffer(u32) const NO_INLINE;
    bool tryDvdOpen(const char*);
    void setupParams();
    void exeWait();
    void exeReadHeader();
    void exeReadFrameComp();
    void exeReadVideoComp();
    void exeReadAudioComp();
    void endReadHeader();
    void endReadFrameComp();
    void endReadVideoComp();
    void endReadAudioComp();
    void exeReadPreLoad();
    void endReadPreLoadOne();
    bool checkComponentsInFrame(s32);
    bool tryFinishDvdOpen();
    bool isAudioProcessValid();
    void resetAudioParams();
    bool setVolume(s32, s32);
    bool isPreLoading() const;
    void setUnpauseFrameFlag();

    // TODO: make proper member names
#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
    /* 0x008 */ u8 _8;
    /* 0x009 */ u8 _9;
    /* 0x00C */ u32 _C;
    /* 0x010 */ u32 _10;
    NebulaDisc::File mFile;
    NebulaDisc::Request mRequest;
    /* 0x050 */ THPHeader mHeader;
    /* 0x080 */ THPFrameCompInfo mFrameComp;
    /* 0x094 */ THPVideoInfo mVideoInfo;
    /* 0x0A0 */ THPAudioInfo mAudioInfo;
    /* 0x0B0 */ void* mTHPWork;
    /* 0x0B4 */ u8 mOpen;
    /* 0x0B5 */ u8 mPreFetchState;
    /* 0x0B6 */ u8 mAudioState;
    /* 0x0B7 */ u8 mLoop;
    /* 0x0B8 */ u8 mAudioExist;
    /* 0x0BC */ u32 mCurOffset;
    /* 0x0C0 */ u8 mDvdError;
    /* 0x0C4 */ u32 mReadProgress;
    /* 0x0C8 */ s32 mNextDecodeIndex;
    /* 0x0CC */ volatile s32 mReadIndex;
    /* 0x0D0 */ s32 mReadSize;
    /* 0x0D4 */ s32 mTotalReadFrame;
    /* 0x0E8 */ THPReadBuffer mReadBuffer[20];
    /* 0x1D8 */ THPTextureSet mTextureSet;
    /* 0x300 */ s32 _314;
#else
    /* 0x008 */ u8 _8;
    /* 0x009 */ u8 _9;
    /* 0x00C */ u32 _C;
    /* 0x010 */ u32 _10;
    NebulaDisc::File mFile;
    NebulaDisc::Request mRequest;
    /* 0x050 */ THPHeader mHeader;
    /* 0x080 */ THPFrameCompInfo mFrameComp;
    /* 0x094 */ THPVideoInfo mVideoInfo;
    /* 0x0A0 */ THPAudioInfo mAudioInfo;
    /* 0x0B0 */ void* mTHPWork;
    /* 0x0B4 */ u8 mOpen;
    /* 0x0B5 */ u8 mPreFetchState;
    /* 0x0B6 */ u8 mAudioState;
    /* 0x0B7 */ u8 mLoop;
    /* 0x0B8 */ u8 mAudioExist;
    /* 0x0BC */ u32 mCurOffset;
    /* 0x0C0 */ u8 mDvdError;
    /* 0x0C4 */ u32 mReadProgress;
    /* 0x0C8 */ s32 mNextDecodeIndex;
    /* 0x0CC */ volatile s32 mReadIndex;
    /* 0x0D0 */ s32 mReadSize;
    /* 0x0D4 */ s32 mTotalReadFrame;
    /* 0x0E8 */ THPReadBuffer mReadBuffer[20];
    /* 0x1D8 */ THPTextureSet mTextureSet[2];
    /* 0x310 */ u8 _310;
    /* 0x314 */ s32 _314;
#endif
};
