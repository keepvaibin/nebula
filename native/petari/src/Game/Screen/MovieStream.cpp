#include "Game/Screen/MovieStream.hpp"
#include "Game/Screen/MovieDraw.hpp"
#include "Game/System/NebulaClock.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/NebulaSystemSettings.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <cstring>
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The open movie's disc path, for the host's replacement videos (one movie plays at a time).
    char sMovieName[64];

    void movieFrame(const char* pName, s32 frame, f32 frameRate, const void* pY, const void* pU, const void* pV, u32 width, u32 height) {
        const NebulaHostServices* const host = nebula_host();
        host->movie_frame(host->user, pName, frame, frameRate, pY, pU, pV, width, height);
    }

    void movieAudioControl(u32 what, s32 value, s32 milliseconds) {
        const NebulaHostServices* const host = nebula_host();
        host->movie_audio_control(host->user, what, value, milliseconds);
    }
}  // namespace

MovieStream::MovieStream()
    : mOpen(false), mPlaying(false), mLoop(false), mData(nullptr), mCurOffset(0), mFrameSize(0), mFrame(0), mPlaneY(nullptr),
      mPlaneU(nullptr), mPlaneV(nullptr), mPlanesFrame(-1) {
    mFile.mEntry = -1;
    mFile.mLength = 0;
    MR::zeroMemory(&mHeader, sizeof(mHeader));
    MR::zeroMemory(&mVideoInfo, sizeof(mVideoInfo));
}

void MovieStream::quit() {
    movieAudioControl(NEBULA_MOVIE_AUDIO_CLOSE, 0, 0);
}

bool MovieStream::open(const char* pName) {
    if (mOpen || !GameAssets::open(pName, &mFile)) {
        return false;
    }

    strncpy(sMovieName, pName, sizeof(sMovieName) - 1);
    sMovieName[sizeof(sMovieName) - 1] = '\0';
    movieFrame(sMovieName, NEBULA_MOVIE_OPENING, 0.0f, nullptr, nullptr, nullptr, 0, 0);

    mData = static_cast< const u8* >(GameAssets::map(mFile.mEntry));
    if (mData == nullptr) {
        NEBULA_PANIC("%s cannot be loaded", sMovieName);
    }

    memcpy(&mHeader, mData, sizeof(mHeader));
    if (strncmp(mHeader.mMagic, "NMOV", 4) != 0) {
        NEBULA_PANIC("%s: a movie not cooked (the importer rewrites THP files)", sMovieName);
    }

    mVideoInfo.mWidth = mHeader.mWidth;
    mVideoInfo.mHeight = mHeader.mHeight;
    rewind();
    mPlanesFrame = -1;
    mPlaying = false;
    mLoop = false;
    mOpen = true;

    // The movie's sound plays from the host while the movie with it is open.
    if (mHeader.mAudioTracks != 0) {
        movieAudioControl(NEBULA_MOVIE_AUDIO_OPEN, NebulaSystemSettings::soundMode() == NebulaSystemSettings::SoundModeMono ? 1 : 0, 0);
    } else {
        movieAudioControl(NEBULA_MOVIE_AUDIO_CLOSE, 0, 0);
    }

    return true;
}

void MovieStream::rewind() {
    mCurOffset = sizeof(MovieFileHeader);
    mFrameSize = mHeader.mFirstFrameSize;
    mFrame = 0;
}

bool MovieStream::close() {
    if (!mOpen || mPlaying) {
        return false;
    }

    mOpen = false;
    movieAudioControl(NEBULA_MOVIE_AUDIO_CLOSE, 0, 0);
    movieFrame(nullptr, NEBULA_MOVIE_NONE, 0.0f, nullptr, nullptr, nullptr, 0, 0);
    return true;
}

u32 MovieStream::calcNeedMemory() const {
    if (!mOpen) {
        return 0;
    }

    const u32 pixels = mVideoInfo.mWidth * mVideoInfo.mHeight;
    return NebulaAlignUp32(pixels) + NebulaAlignUp32(pixels / 4) * 2;
}

void MovieStream::setBuffer(u8* pBuffer) {
    if (!mOpen || mPlaying) {
        return;
    }

    const u32 pixels = mVideoInfo.mWidth * mVideoInfo.mHeight;
    mPlaneY = pBuffer;
    pBuffer += NebulaAlignUp32(pixels);
    mPlaneU = pBuffer;
    pBuffer += NebulaAlignUp32(pixels / 4);
    mPlaneV = pBuffer;
}

void MovieStream::start(bool loop) {
    if (mOpen) {
        mLoop = loop;
        mPlaying = true;
    }
}

void MovieStream::stop() {
    if (!mOpen) {
        return;
    }

    mPlaying = false;
    mPlanesFrame = -1;
    rewind();
    movieAudioControl(NEBULA_MOVIE_AUDIO_RESET, 0, 0);
}

// 0: decoded. 1: the video's data is bad. 2: no frame left. 3: the mixer's queue is full. 4: no such track.
s32 MovieStream::decode(s32 track) {
    if (!mPlaying) {
        return 2;
    }
    if (mFrame >= static_cast< s32 >(mHeader.mFrames)) {
        if (!mLoop) {
            return 2;
        }
        rewind();
    }

    const NebulaHostServices* const host = nebula_host();
    MovieFrameHeader frame[1];
    memcpy(frame, mData + mCurOffset, sizeof(frame));
    const u8* const video = mData + mCurOffset + sizeof(MovieFrameHeader);

    if (mHeader.mAudioTracks != 0) {
        if (track < 0 || track >= static_cast< s32 >(mHeader.mAudioTracks)) {
            return 4;
        }
        if (host->movie_audio(host->user, nullptr, 0) == 0) {
            return 3;
        }
    }

    if (host->movie_video_decode(host->user, video, frame->mVideoSize, mVideoInfo.mWidth, mVideoInfo.mHeight, mPlaneY, mPlaneU, mPlaneV) != 0) {
        return 1;
    }
    mPlanesFrame = mFrame;

    if (mHeader.mAudioTracks != 0) {
        // The host decodes and plays the frame's sound (the track's record).
        host->movie_audio(host->user, video + frame->mVideoSize + frame->mAudioSize * track, frame->mAudioSize);
    }

    mCurOffset += mFrameSize;
    mFrameSize = frame->mNextFrameSize;
    mFrame++;
    return 0;
}

bool MovieStream::hasSound() const {
    return mOpen && mHeader.mAudioTracks != 0;
}

s32 MovieStream::getSoundFramesWaiting() const {
    const NebulaHostServices* const host = nebula_host();
    return NEBULA_MOVIE_AUDIO_QUEUE_FRAMES - host->movie_audio(host->user, nullptr, 0);
}

s32 MovieStream::drawCurrentFrame(u32 x, u32 y, u32 polyW, u32 polyH) {
    if (mPlanesFrame < 0) {
        return -1;
    }

    movieFrame(sMovieName, mHeader.mFrames > 0 ? mPlanesFrame % static_cast< s32 >(mHeader.mFrames) : mPlanesFrame, mHeader.mFrameRate, mPlaneY, mPlaneU,
               mPlaneV, mVideoInfo.mWidth, mVideoInfo.mHeight);
    MovieDraw::drawFrame(NebulaFrameClock::cScreenUnitsAcross, NebulaFrameClock::cScreenUnitsDown, mPlaneY, mPlaneU, mPlaneV, x, y, mVideoInfo.mWidth,
                         mVideoInfo.mHeight, polyW, polyH);

    return mPlanesFrame;
}

f32 MovieStream::getFrameRate() const {
    return mOpen ? mHeader.mFrameRate : 0.0f;
}

s32 MovieStream::getTotalFrame() const {
    return mOpen ? mHeader.mFrames : 0;
}

void MovieStream::setVolume(s32 volume, s32 time) {
    if (!mOpen || mHeader.mAudioTracks == 0) {
        return;
    }

    volume = volume > 127 ? 127 : (volume < 0 ? 0 : volume);
    time = time > 60000 ? 60000 : (time < 0 ? 0 : time);
    movieAudioControl(NEBULA_MOVIE_AUDIO_VOLUME, volume, time);
}

void MovieStream::setUnpauseFrameFlag() {
    movieAudioControl(NEBULA_MOVIE_AUDIO_RESUME, 0, 0);
}
