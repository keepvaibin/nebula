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

// A movie in the mapped store. Its sound is read ahead into the host's mixer; its picture is the frame being heard, decoded
// into the Y, U and V planes when that frame comes up. A movie with no sound takes its frames in turn from decode().
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
    s32 decode();
    bool hasSound() const;
    // Keeps the mixer's queue full enough that a stall in the game does not reach the sound.
    void feedSound(s32 track);
    // Where playing has reached, in frames from the start through any loops; the frame count once the sound has gone out.
    f64 getSoundPosition() const;
    // Puts the frame nearest that place in the planes. A next frame comes in turn; only a whole frame late are any passed over.
    s32 decodeHeardFrame();
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
    void rewindSound();
    // The frame at the picture's place, undecoded: moves on to the next.
    const MovieFrameHeader* stepPicture();

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
    u32 mSoundOffset;
    u32 mSoundFrameSize;
    s32 mSoundFrame;
    s32 mSoundFed;
    s32 mPicturesTaken;
    u8* mPlaneY;
    u8* mPlaneU;
    u8* mPlaneV;
    s32 mPlanesFrame;
};
