#include "Game/Screen/MovieStream.hpp"
#include "Game/Screen/MovieDraw.hpp"
#include "Game/System/NebulaClock.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/NebulaSystemSettings.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <cstring>
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The open movie's disc path, for the host's replacement videos (one movie plays at a time).
    char sMovieName[64];
    MovieFileHeader sHeaderBuffer ATTRIBUTE_ALIGN(32);

    void movieFrame(const char* pName, s32 frame, f32 frameRate, const void* pY, const void* pU, const void* pV, u32 width, u32 height) {
        const NebulaHostServices* const host = nebula_host();
        host->movie_frame(host->user, pName, frame, frameRate, pY, pU, pV, width, height);
    }

    void movieAudioControl(u32 what, s32 value, s32 milliseconds) {
        const NebulaHostServices* const host = nebula_host();
        host->movie_audio_control(host->user, what, value, milliseconds);
    }

    void frameReadCallback(s32 result, GameAssets::Request* pRequest) {
        static_cast< MovieStream* >(pRequest->mUser)->onFrameRead(result);
    }

    void loadReadCallback(s32 result, GameAssets::Request* pRequest) {
        static_cast< MovieStream* >(pRequest->mUser)->onLoadRead(result);
    }
}  // namespace

namespace NrvMovieStream {
    NEW_NERVE(HostTypeWait, MovieStream, Wait);
    NEW_NERVE(HostTypeReadHeader, MovieStream, ReadHeader);
    NEW_NERVE(HostTypeReadPreLoad, MovieStream, ReadPreLoad);
};  // namespace NrvMovieStream

MovieStream::MovieStream(const char* pName)
    : NerveExecutor(pName), mHeaderRead(false), mOpen(false), mPreFetching(false), mLoop(false), mReadError(false), mReading(false), mCurOffset(0),
      mNextDecodeIndex(0), mReadIndex(0), mReadSize(0), mTotalReadFrame(0), mPreLoadLeft(0), mPlaneY(nullptr), mPlaneU(nullptr), mPlaneV(nullptr),
      mPlanesFrame(-1) {
    mFile.mEntry = -1;
    mFile.mLength = 0;
    MR::zeroMemory(&mRequest, sizeof(mRequest));
    MR::zeroMemory(&mHeader, sizeof(mHeader));
    MR::zeroMemory(&mVideoInfo, sizeof(mVideoInfo));
    MR::zeroMemory(mReadBuffer, sizeof(mReadBuffer));

    initNerve(GET_NERVE(MovieStream, HostTypeWait));
}

void MovieStream::quit() {
    movieAudioControl(NEBULA_MOVIE_AUDIO_CLOSE, 0, 0);
}

bool MovieStream::open(const char* pName) {
    if (mOpen || !GameAssets::open(pName, &mFile)) {
        return false;
    }

    mHeaderRead = false;
    strncpy(sMovieName, pName, sizeof(sMovieName) - 1);
    sMovieName[sizeof(sMovieName) - 1] = '\0';
    movieFrame(sMovieName, NEBULA_MOVIE_OPENING, 0.0f, nullptr, nullptr, nullptr, 0, 0);

    setNerve(GET_NERVE(MovieStream, HostTypeReadHeader));
    return true;
}

bool MovieStream::close() {
    if (!mOpen || mPreFetching || mReading) {
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
    return NebulaAlignUp32(mHeader.mLargestFrame) * cReadBuffers + NebulaAlignUp32(pixels) + NebulaAlignUp32(pixels / 4) * 2;
}

void MovieStream::setBuffer(u8* pBuffer) {
    if (!mOpen || mPreFetching) {
        return;
    }

    const u32 pixels = mVideoInfo.mWidth * mVideoInfo.mHeight;
    mPlaneY = pBuffer;
    pBuffer += NebulaAlignUp32(pixels);
    mPlaneU = pBuffer;
    pBuffer += NebulaAlignUp32(pixels / 4);
    mPlaneV = pBuffer;
    pBuffer += NebulaAlignUp32(pixels / 4);

    for (s32 i = 0; i < cReadBuffers; i++) {
        mReadBuffer[i].mData = pBuffer;
        mReadBuffer[i].mValid = false;
        pBuffer += NebulaAlignUp32(mHeader.mLargestFrame);
    }
}

bool MovieStream::preLoad(bool loop) {
    if (!mOpen || mPreFetching) {
        return false;
    }

    mLoop = loop;
    mPreLoadLeft = cReadBuffers;
    if (!mLoop && mHeader.mFrames < static_cast< u32 >(cReadBuffers)) {
        mPreLoadLeft = mHeader.mFrames;
    }

    setNerve(GET_NERVE(MovieStream, HostTypeReadPreLoad));
    return true;
}

void MovieStream::loadStop() {
    if (!mOpen) {
        return;
    }

    mPreFetching = false;

    if (mReading) {
        GameAssets::cancel(&mRequest);
        mReading = false;
    }

    for (s32 i = 0; i < cReadBuffers; i++) {
        mReadBuffer[i].mValid = false;
    }

    mPlanesFrame = -1;
    mCurOffset = sizeof(MovieFileHeader);
    mReadSize = mHeader.mFirstFrameSize;
    mReadIndex = 0;
    mTotalReadFrame = 0;
    mReadError = false;
    mNextDecodeIndex = 0;
    movieAudioControl(NEBULA_MOVIE_AUDIO_RESET, 0, 0);
}

// 0: decoded. 1: the video's data is bad. 2: the frame is not read yet. 3: the mixer's queue is full. 4: no such track.
s32 MovieStream::decode(s32 track) {
    ReadBuffer& buffer = mReadBuffer[mNextDecodeIndex];
    if (!buffer.mValid) {
        return 2;
    }

    const NebulaHostServices* const host = nebula_host();
    const MovieFrameHeader* const frame = reinterpret_cast< const MovieFrameHeader* >(buffer.mData);
    const u8* const video = buffer.mData + sizeof(MovieFrameHeader);

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
    mPlanesFrame = buffer.mFrameNumber;

    if (mHeader.mAudioTracks != 0) {
        // The host decodes and plays the frame's sound (the track's record).
        host->movie_audio(host->user, video + frame->mVideoSize + frame->mAudioSize * track, frame->mAudioSize);
    }

    buffer.mValid = false;
    mNextDecodeIndex = nextBuffer(mNextDecodeIndex);
    checkPrefetch();
    return 0;
}

s32 MovieStream::drawCurrentFrame(u32 x, u32 y, u32 polyW, u32 polyH) {
    if (mPlanesFrame < 0) {
        return -1;
    }

    movieFrame(sMovieName, mHeader.mFrames > 0 ? mPlanesFrame % static_cast< s32 >(mHeader.mFrames) : mPlanesFrame, mHeader.mFrameRate, mPlaneY, mPlaneU,
               mPlaneV, mVideoInfo.mWidth, mVideoInfo.mHeight);
    MovieDraw::drawFrame(NebulaFrameClock::cFrameWidth, NebulaFrameClock::cFrameHeight, mPlaneY, mPlaneU, mPlaneV, x, y, mVideoInfo.mWidth,
                         mVideoInfo.mHeight, polyW, polyH);

    return mPlanesFrame;
}

f32 MovieStream::getFrameRate() const {
    return mOpen ? mHeader.mFrameRate : 0.0f;
}

s32 MovieStream::getTotalFrame() const {
    return mOpen ? mHeader.mFrames : 0;
}

void MovieStream::startFrameRead() {
    if (mReadError || !mPreFetching) {
        return;
    }

    if (mTotalReadFrame > static_cast< s32 >(mHeader.mFrames) - 1) {
        if (!mLoop) {
            return;
        }
        mTotalReadFrame = 0;
        mCurOffset = sizeof(MovieFileHeader);
        mReadSize = mHeader.mFirstFrameSize;
    }

    mReading = true;
    mRequest.mUser = this;

    GameAssets::readAsync(&mFile, &mRequest, mReadBuffer[mReadIndex].mData, mReadSize, mCurOffset, frameReadCallback);
}

void MovieStream::checkPrefetch() {
    if (!mReadBuffer[mReadIndex].mValid && !mReading) {
        startFrameRead();
    }
}

void MovieStream::onFrameRead(s32 result) {
    if (result == GameAssets::cResultFailed) {
        mReadError = true;
        return;
    }
    if (result == GameAssets::cResultCanceled) {
        return;
    }

    mReading = false;
    ReadBuffer& buffer = mReadBuffer[mReadIndex];
    buffer.mFrameNumber = mTotalReadFrame;
    mTotalReadFrame++;
    buffer.mValid = true;
    mCurOffset += mReadSize;
    mReadSize = reinterpret_cast< const MovieFrameHeader* >(buffer.mData)->mNextFrameSize;
    mReadIndex = nextBuffer(mReadIndex);

    if (!mReadBuffer[mReadIndex].mValid) {
        startFrameRead();
    }
}

void MovieStream::onLoadRead(s32 result) {
    if (result < 0) {
        return;
    }

    if (isNerve(GET_NERVE(MovieStream, HostTypeReadHeader))) {
        onHeaderRead();
    } else if (isNerve(GET_NERVE(MovieStream, HostTypeReadPreLoad))) {
        onPreLoadRead();
        setNerve(GET_NERVE(MovieStream, HostTypeReadPreLoad));
    }
}

void MovieStream::exeWait() {
}

void MovieStream::exeReadHeader() {
    if (MR::isFirstStep(this)) {
        mRequest.mUser = this;
        GameAssets::readAsync(&mFile, &mRequest, &sHeaderBuffer, sizeof(sHeaderBuffer), 0, loadReadCallback);
    }
}

void MovieStream::onHeaderRead() {
    mHeader = sHeaderBuffer;
    if (strncmp(mHeader.mMagic, "NMOV", 4) != 0) {
        NEBULA_PANIC("%s: a movie not cooked (the importer rewrites THP files)", sMovieName);
    }

    mVideoInfo.mWidth = mHeader.mWidth;
    mVideoInfo.mHeight = mHeader.mHeight;
    mCurOffset = sizeof(MovieFileHeader);
    mReadSize = mHeader.mFirstFrameSize;
    mReadIndex = 0;
    mTotalReadFrame = 0;
    mReadError = false;
    mPlanesFrame = -1;
    mNextDecodeIndex = 0;
    mPreFetching = false;
    mLoop = false;
    mOpen = true;

    // The movie's sound plays from the host while the movie with it is open.
    if (mHeader.mAudioTracks != 0) {
        movieAudioControl(NEBULA_MOVIE_AUDIO_OPEN, NebulaSystemSettings::soundMode() == NebulaSystemSettings::SoundModeMono ? 1 : 0, 0);
    } else {
        movieAudioControl(NEBULA_MOVIE_AUDIO_CLOSE, 0, 0);
    }

    mHeaderRead = true;
    setNerve(GET_NERVE(MovieStream, HostTypeWait));
}

void MovieStream::exeReadPreLoad() {
    if (MR::isFirstStep(this)) {
        mRequest.mUser = this;
        GameAssets::readAsync(&mFile, &mRequest, mReadBuffer[mReadIndex].mData, mReadSize, mCurOffset, loadReadCallback);
    }

    if (mPreLoadLeft == 0) {
        mPreFetching = true;
        setNerve(GET_NERVE(MovieStream, HostTypeWait));
    }
}

void MovieStream::onPreLoadRead() {
    ReadBuffer& buffer = mReadBuffer[mReadIndex];
    mCurOffset += mReadSize;
    mReadSize = reinterpret_cast< const MovieFrameHeader* >(buffer.mData)->mNextFrameSize;
    buffer.mValid = true;
    buffer.mFrameNumber = mTotalReadFrame;
    mReadIndex = nextBuffer(mReadIndex);
    mTotalReadFrame++;

    if (mTotalReadFrame > static_cast< s32 >(mHeader.mFrames) - 1 && mLoop) {
        mTotalReadFrame = 0;
        mCurOffset = sizeof(MovieFileHeader);
        mReadSize = mHeader.mFirstFrameSize;
    }

    mPreLoadLeft--;
}

void MovieStream::setVolume(s32 volume, s32 time) {
    if (!mOpen || mHeader.mAudioTracks == 0) {
        return;
    }

    volume = volume > 127 ? 127 : (volume < 0 ? 0 : volume);
    time = time > 60000 ? 60000 : (time < 0 ? 0 : time);
    movieAudioControl(NEBULA_MOVIE_AUDIO_VOLUME, volume, time);
}

bool MovieStream::isPreLoading() const {
    return isNerve(GET_NERVE(MovieStream, HostTypeReadPreLoad));
}

void MovieStream::setUnpauseFrameFlag() {
    movieAudioControl(NEBULA_MOVIE_AUDIO_RESUME, 0, 0);
}
