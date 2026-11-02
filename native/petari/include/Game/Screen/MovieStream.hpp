#pragma once

#include "Game/System/NebulaAtomic.hpp"

#include "Game/System/NerveExecutor.hpp"
#include "Game/System/GameAssets.hpp"

// A movie file as the importer cooks it (crates/nebula-import/src/cook/thp.rs), host order.
struct MovieFileHeader {
    char mMagic[4];  // "NMOV"
    u32 mFrames;
    f32 mFrameRate;
    u32 mLargestFrame;
    u32 mFirstFrameSize;
    u32 mWidth;
    u32 mHeight;
    u32 mAudioTracks;
    u8 mReserved[0x20];
};

struct MovieFrameHeader {
    u32 mNextFrameSize;
    u32 mVideoSize;
    u32 mAudioSize;  // one track's
    u32 mReserved;
};

struct MovieVideoInfo {
    u32 mWidth;
    u32 mHeight;
};

// Reads a movie's frames ahead from the disc, decodes the next one into its Y, U and V planes and hands its sound to
// the host's mixer.
class MovieStream : public NerveExecutor {
public:
    MovieStream(const char*);

    bool open(const char*);
    bool close();
    void quit();
    u32 calcNeedMemory() const;
    void setBuffer(u8*);
    bool preLoad(bool);
    void loadStop();
    s32 decode(s32);
    s32 drawCurrentFrame(u32, u32, u32, u32);
    bool isHeaderRead() const {
        return mHeaderRead;
    }
    const MovieVideoInfo& getVideoInfo() const {
        return mVideoInfo;
    }
    f32 getFrameRate() const;
    s32 getTotalFrame() const;
    void setVolume(s32, s32);
    bool isPreLoading() const;
    void setUnpauseFrameFlag();

    void onFrameRead(s32);
    void onLoadRead(s32);

    void exeWait();
    void exeReadHeader();
    void exeReadPreLoad();

private:
    struct ReadBuffer {
        u8* mData;
        s32 mFrameNumber;
        NebulaAtomic< bool > mValid;
    };

    static const s32 cReadBuffers = 20;

    void startFrameRead();
    void checkPrefetch();
    void onHeaderRead();
    void onPreLoadRead();
    static s32 nextBuffer(s32 index) {
        return index + 1 >= cReadBuffers ? 0 : index + 1;
    }

    bool mHeaderRead;
    bool mOpen;
    bool mPreFetching;
    bool mLoop;
    bool mReadError;
    bool mReading;
    GameAssets::File mFile;
    GameAssets::Request mRequest;
    MovieFileHeader mHeader;
    MovieVideoInfo mVideoInfo;
    u32 mCurOffset;
    s32 mNextDecodeIndex;
    NebulaAtomic< s32 > mReadIndex;
    s32 mReadSize;
    s32 mTotalReadFrame;
    s32 mPreLoadLeft;
    ReadBuffer mReadBuffer[cReadBuffers];
    u8* mPlaneY;
    u8* mPlaneU;
    u8* mPlaneV;
    s32 mPlanesFrame;
};
