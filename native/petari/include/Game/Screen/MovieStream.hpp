#pragma once


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

// A movie in the mapped store: decodes the next frame into its Y, U and V planes and hands the frame's sound to the host's mixer.
class MovieStream {
public:
    MovieStream();

    bool open(const char*);
    bool close();
    void quit();
    u32 calcNeedMemory() const;
    void setBuffer(u8*);
    void start(bool);
    void stop();
    s32 decode(s32);
    s32 drawCurrentFrame(u32, u32, u32, u32);
    const MovieVideoInfo& getVideoInfo() const {
        return mVideoInfo;
    }
    f32 getFrameRate() const;
    s32 getTotalFrame() const;
    void setVolume(s32, s32);
    void setUnpauseFrameFlag();

private:
    void rewind();

    bool mOpen;
    bool mPlaying;
    bool mLoop;
    GameAssets::File mFile;
    const u8* mData;
    MovieFileHeader mHeader;
    MovieVideoInfo mVideoInfo;
    u32 mCurOffset;
    u32 mFrameSize;
    s32 mFrame;
    u8* mPlaneY;
    u8* mPlaneU;
    u8* mPlaneV;
    s32 mPlanesFrame;
};
