#include "Game/Util/ScreenUtil.hpp"
#include "Game/Screen/MovieStream.hpp"
#include "Game/Screen/MovieDraw.hpp"
#include "Game/System/NebulaClock.hpp"
#include "Game/System/NebulaSystemSettings.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <cstring>
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The open movie's path in the game's data, for the host's replacement videos (one movie plays at a time).
    MR::GrowBuffer< char > sMovieName;

    void movieFrame(const char* pName, s32 frame, f32 frameRate, const void* pData, u64 size, u32 width, u32 height) {
        const NebulaHostServices* const host = nebula_host();
        host->movie_frame(host->user, pName, frame, frameRate, pData, size, width, height);
    }

    void movieAudioControl(u32 what, s32 value, s32 milliseconds) {
        const NebulaHostServices* const host = nebula_host();
        host->movie_audio_control(host->user, what, value, milliseconds);
    }
}  // namespace

MovieStream::MovieStream()
    : mOpen(false), mPlaying(false), mLoop(false), mData(nullptr), mFrame(0), mSoundOffset(0), mSoundFrameSize(0), mSoundFrame(0),
      mSoundFed(0), mPicturesTaken(0), mSilentPosition(0.0), mShownFrame(-1) {
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

    const u32 nameLength = static_cast< u32 >(strlen(pName)) + 1;
    char* const pMovieName = sMovieName.room(nameLength);
    memcpy(pMovieName, pName, nameLength);

    mData = static_cast< const u8* >(GameAssets::map(mFile.mEntry));
    if (mData == nullptr) {
        NEBULA_PANIC("%s cannot be loaded", pMovieName);
    }

    memcpy(&mHeader, mData, sizeof(mHeader));
    if (!MR::isEqualStringPrefix(mHeader.mMagic, "NMOV")) {
        NEBULA_PANIC("%s: a movie not cooked (the importer rewrites THP files)", pMovieName);
    }
    if (!(mHeader.mDisplayAspect > 0.0f) || mHeader.mWidth == 0 || mHeader.mHeight == 0) {
        NEBULA_PANIC("%s: a movie with no picture shape", pMovieName);
    }

    mVideoInfo.mWidth = mHeader.mWidth;
    mVideoInfo.mHeight = mHeader.mHeight;
    // The host starts decoding now, ahead of the first frame drawn.
    movieFrame(pMovieName, NEBULA_MOVIE_OPENING, mHeader.mFrameRate, mData, mFile.mLength, mVideoInfo.mWidth, mVideoInfo.mHeight);
    mFrame = 0;
    rewindSound();
    mSoundFed = 0;
    mPicturesTaken = 0;
    mSilentPosition = 0.0;
    mShownFrame = -1;
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

void MovieStream::rewindSound() {
    mSoundOffset = sizeof(MovieFileHeader);
    mSoundFrameSize = mHeader.mFirstFrameSize;
    mSoundFrame = 0;
}

bool MovieStream::close() {
    if (!mOpen || mPlaying) {
        return false;
    }

    mOpen = false;
    movieAudioControl(NEBULA_MOVIE_AUDIO_CLOSE, 0, 0);
    movieFrame(nullptr, NEBULA_MOVIE_NONE, 0.0f, nullptr, 0, 0, 0);
    return true;
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
    mShownFrame = -1;
    mFrame = 0;
    rewindSound();
    mSoundFed = 0;
    mPicturesTaken = 0;
    mSilentPosition = 0.0;
    movieAudioControl(NEBULA_MOVIE_AUDIO_RESET, 0, 0);
}

bool MovieStream::hasSound() const {
    return mOpen && mHeader.mAudioTracks != 0;
}

void MovieStream::feedSound(s32 track) {
    if (!mPlaying || track < 0 || track >= static_cast< s32 >(mHeader.mAudioTracks)) {
        return;
    }

    // A sixth of a second ahead, whatever the frame rate.
    const NebulaHostServices* const host = nebula_host();
    s32 ahead = static_cast< s32 >(mHeader.mFrameRate / 6.0f) + 1;
    if (ahead > NEBULA_MOVIE_AUDIO_QUEUE_FRAMES) {
        ahead = NEBULA_MOVIE_AUDIO_QUEUE_FRAMES;
    }

    while (NEBULA_MOVIE_AUDIO_QUEUE_FRAMES - host->movie_audio(host->user, nullptr, 0) < ahead) {
        if (mSoundFrame >= static_cast< s32 >(mHeader.mFrames)) {
            if (!mLoop) {
                return;
            }
            rewindSound();
        }

        MovieFrameHeader frame[1];
        memcpy(frame, mData + mSoundOffset, sizeof(frame));
        const u8* const video = mData + mSoundOffset + sizeof(MovieFrameHeader);

        if (host->movie_audio(host->user, video + frame->mVideoSize + frame->mAudioSize * track, frame->mAudioSize) == 0) {
            return;
        }

        mSoundOffset += mSoundFrameSize;
        mSoundFrameSize = frame->mNextFrameSize;
        mSoundFrame++;
        mSoundFed++;
    }
}

void MovieStream::stepSilent() {
    if (!mPlaying || hasSound()) {
        return;
    }
    // Through any loops, as the sound's place runs (the pictures start over by themselves).
    mSilentPosition += static_cast< f64 >(mHeader.mFrameRate) / NebulaFrameClock::cStepsPerSecond;
    if (!mLoop && mSilentPosition > mHeader.mFrames) {
        mSilentPosition = mHeader.mFrames;
    }
}

f64 MovieStream::getPosition() const {
    if (!hasSound()) {
        return mSilentPosition;
    }
    const NebulaHostServices* const host = nebula_host();
    return host->movie_audio_position(host->user);
}

void MovieStream::stepPicture() {
    if (mFrame >= static_cast< s32 >(mHeader.mFrames)) {
        mFrame = 0;
    }
    mFrame++;
    mPicturesTaken++;
}

// 0: the frame at the place is named (or the one on show still is). 2: not playing.
s32 MovieStream::pickFrameAtPosition() {
    if (!mPlaying) {
        return 2;
    }

    const f64 position = getPosition();
    if (position < mPicturesTaken - 0.5) {
        return 0;
    }

    // Every frame stands alone, so those passed over cost nothing.
    if (position >= mPicturesTaken + 1.0) {
        const s32 nearest = static_cast< s32 >(position + 0.5);
        while (mPicturesTaken < nearest) {
            stepPicture();
        }
    }

    const s32 number = mFrame >= static_cast< s32 >(mHeader.mFrames) ? 0 : mFrame;
    stepPicture();
    mShownFrame = number;
    return 0;
}

s32 MovieStream::drawCurrentFrame(f32 x, f32 y, f32 width, f32 height) {
    if (mShownFrame < 0) {
        return -1;
    }

    movieFrame(sMovieName.mItems, mHeader.mFrames > 0 ? mShownFrame % static_cast< s32 >(mHeader.mFrames) : mShownFrame, mHeader.mFrameRate,
               mData, mFile.mLength, mVideoInfo.mWidth, mVideoInfo.mHeight);
    MovieDraw::drawFrame(MR::getScreenWidth(), MR::getScreenHeight(), mVideoInfo.mWidth, mVideoInfo.mHeight, x, y, width, height);

    return mShownFrame;
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
