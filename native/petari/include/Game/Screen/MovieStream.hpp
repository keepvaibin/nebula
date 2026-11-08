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
    // The picture's shape as shown, width / height (its pixels need not be square).
    f32 mDisplayAspect;
    u8 mReserved[0x1C];
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

// A movie in the mapped store. Its sound is read ahead into the host's mixer; its picture is the frame at its place, decoded
// into the Y, U and V planes when that frame comes up. Its place is its sound's, or for a movie with none, the game's
// steps at the movie's frame rate.
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
    bool hasSound() const;
    // Keeps the mixer's queue full enough that a stall in the game does not reach the sound.
    void feedSound(s32 track);
    // A movie with no sound: its place moves on by one game step.
    void stepSilent();
    // Where playing has reached, in frames from the start through any loops; the frame count once the sound has gone out.
    f64 getPosition() const;
    // Puts the frame nearest that place in the planes. A next frame comes in turn; only a whole frame late are any passed over.
    s32 decodeFrameAtPosition();
    // The planes' frame drawn at x, y (width x height, screen units); the frame drawn, or -1 before the first.
    s32 drawCurrentFrame(f32 x, f32 y, f32 width, f32 height);
    // The frame last decoded (-1 before the first).
    s32 getPictureFrame() const {
        return mPlanesFrame;
    }
    f32 getDisplayAspect() const {
        return mHeader.mDisplayAspect;
    }
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
    f64 mSilentPosition;
    u8* mPlaneY;
    u8* mPlaneU;
    u8* mPlaneV;
    s32 mPlanesFrame;
    s32 mPlanesUploaded;
};
