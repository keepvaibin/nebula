#include "Game/Screen/THPSimplePlayerWrapper.hpp"
#include "Game/System/NebulaKernel.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>
#include <cstring>
#ifdef TARGET_PC
#include <revolution/os.h>
#include "Game/System/NebulaClock.hpp"
#include "Game/System/NebulaSystemSettings.hpp"
#include "Game/System/NebulaDisc.hpp"
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The open movie's disc path, for the host's replacement videos (one movie plays at a time).
    char sNebulaMovieName[64];
}  // namespace
#endif

static s32 WorkBuffer[16] ATTRIBUTE_ALIGN(32);

namespace NrvTHPSimplePlayerWrapper {
    NEW_NERVE(HostTypeWait, THPSimplePlayerWrapper, Wait);
    NEW_NERVE(HostTypeReadHeader, THPSimplePlayerWrapper, ReadHeader);
    NEW_NERVE(HostTypeReadFrameComp, THPSimplePlayerWrapper, ReadFrameComp);
    NEW_NERVE(HostTypeReadVideoComp, THPSimplePlayerWrapper, ReadVideoComp);
    NEW_NERVE(HostTypeReadAudioComp, THPSimplePlayerWrapper, ReadAudioComp);
    NEW_NERVE(HostTypeReadPreLoad, THPSimplePlayerWrapper, ReadPreLoad);
};  // namespace NrvTHPSimplePlayerWrapper

namespace {
    void dvdCallBackFunc(s32 a1, NebulaDisc::Request* pRequest) {
        THPSimplePlayerWrapper* player = (THPSimplePlayerWrapper*)pRequest->mUser;
        player->dvdCallBack(a1);
    }
};  // namespace

THPSimplePlayerWrapper::THPSimplePlayerWrapper(const char* pName)
    : NerveExecutor(pName), _8(), _9(), _C(), _10(), mTHPWork(), mOpen(), mPreFetchState(), mAudioState(), mLoop(), mAudioExist(), mCurOffset(),
      mDvdError(), mReadProgress(), mNextDecodeIndex(), mReadIndex(), mReadSize(), mTotalReadFrame()
#if (VERSION == RMGK01)
      ,
      _310()
#endif
{
    mFile.mEntry = -1;
    mFile.mLength = 0;
    MR::zeroMemory(&mRequest, sizeof(mRequest));
    MR::zeroMemory(&mHeader, sizeof(mHeader));
    MR::zeroMemory(&mFrameComp, sizeof(mFrameComp));
    MR::zeroMemory(&mVideoInfo, sizeof(mVideoInfo));
    MR::zeroMemory(&mAudioInfo, sizeof(mAudioInfo));
    MR::zeroMemory(mReadBuffer, sizeof(mReadBuffer));

#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
    MR::zeroMemory(&mTextureSet, sizeof(mTextureSet));
#else
    MR::zeroMemory(&mTextureSet[0], sizeof(mTextureSet[0]));
    MR::zeroMemory(&mTextureSet[1], sizeof(mTextureSet[1]));
#endif

    initNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeWait));
}

bool THPSimplePlayerWrapper::init(s32 audio) {
    _8 = 1;
    return true;
}

void THPSimplePlayerWrapper::quit() {
    const NebulaHostServices* const host = nebula_host();
    host->movie_audio_control(host->user, NEBULA_MOVIE_AUDIO_CLOSE, 0, 0);
    _8 = 0;
}

bool THPSimplePlayerWrapper::open(const char* pName) {
    if (!tryDvdOpen(pName)) {
        return false;
    }
#ifdef TARGET_PC
    strncpy(sNebulaMovieName, pName, sizeof(sNebulaMovieName) - 1);
    sNebulaMovieName[sizeof(sNebulaMovieName) - 1] = '\0';
    __NebulaMovieFrame(sNebulaMovieName, -2, 0.0f, nullptr, nullptr, nullptr, 0, 0);
#endif

    setNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadHeader));
    return true;
}

bool THPSimplePlayerWrapper::close() {
    if (mOpen) {
        if (mPreFetchState == 0) {
            if (mAudioExist) {
                if (mAudioState == 1) {
                    return false;
                }
            } else {
                mAudioState = 0;
            }

            if (!mReadProgress) {
                mOpen = 0;
                const NebulaHostServices* const host = nebula_host();
                host->movie_audio_control(host->user, NEBULA_MOVIE_AUDIO_CLOSE, 0, 0);
#ifdef TARGET_PC
                __NebulaMovieFrame(nullptr, -1, 0.0f, nullptr, nullptr, nullptr, 0, 0);
#endif
                return true;
            }
        }
    }

    return false;
}

s32 THPSimplePlayerWrapper::getUseTextureCount() const {
    return 2;
}

u32 THPSimplePlayerWrapper::calcNeedMemory() {
    if (!mOpen) {
        return 0;
    }

#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
    u32 size = NebulaAlignUp32(mHeader.bufSize) * 20;
    size += NebulaAlignUp32(mVideoInfo.xSize * mVideoInfo.ySize);
    size += NebulaAlignUp32(mVideoInfo.xSize * mVideoInfo.ySize / 4);
    size += NebulaAlignUp32(mVideoInfo.xSize * mVideoInfo.ySize / 4);
#else
    u32 size = NebulaAlignUp32(mHeader.bufSize) * 20;
    size += NebulaAlignUp32(mVideoInfo.xSize * mVideoInfo.ySize * 2);
    size += NebulaAlignUp32(mVideoInfo.xSize * mVideoInfo.ySize / 2 & 0x7FFFFFFE);
    size += NebulaAlignUp32(mVideoInfo.xSize * mVideoInfo.ySize / 2 & 0x7FFFFFFE);
#endif

    size += 0x1000;
    return size;
}

// FIXME: JPN AND USA
bool THPSimplePlayerWrapper::setBuffer(u8* pBuffer) {
    if (mOpen && !mPreFetchState) {
        if (mAudioState == 1) {
            return false;
        }

        u32 ysize = NebulaAlignUp32(mVideoInfo.xSize * mVideoInfo.ySize);
        u32 uvsize = NebulaAlignUp32(mVideoInfo.xSize * mVideoInfo.ySize / 4);

#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
        mTextureSet.ytexture = pBuffer;
        pBuffer += ysize;
        mTextureSet.utexture = pBuffer;
        pBuffer += uvsize;
        mTextureSet.vtexture = pBuffer;
        pBuffer += uvsize;
#else
        mTextureSet[0].ytexture = pBuffer;
        pBuffer += ysize;
        mTextureSet[0].utexture = pBuffer;
        pBuffer += uvsize;
        mTextureSet[0].vtexture = pBuffer;
        pBuffer += uvsize;
        mTextureSet[1].ytexture = pBuffer;
        pBuffer += ysize;
        mTextureSet[1].utexture = pBuffer;
        pBuffer += uvsize;
        mTextureSet[1].vtexture = pBuffer;
        pBuffer += uvsize;
#endif

        for (s32 i = 0; i < 20; i++) {
            mReadBuffer[i].ptr = pBuffer;
            pBuffer += NebulaAlignUp32(mHeader.bufSize);
            mReadBuffer[i].isValid = 0;
        }

        mTHPWork = (void*)pBuffer;
    }

    return true;
}

bool THPSimplePlayerWrapper::preLoad(s32 loop) {
    if (!mOpen || mPreFetchState) {
        return false;
    }

    mLoop = loop;
    _314 = 20;
    if (!mLoop && mHeader.numFrames < 0x14) {
        _314 = mHeader.numFrames;
    }

    setNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadPreLoad));
    return true;
}

// FIXME: JPN AND USA
bool THPSimplePlayerWrapper::loadStop() {
    if (mOpen && !mAudioState) {
        mPreFetchState = 0;

        if (mReadProgress) {
            NebulaDisc::cancel(&mRequest);
            mReadProgress = 0;
        }

        for (s32 i = 0; i < 0x14; i++) {
            mReadBuffer[i].isValid = 0;
        }

#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
        mTextureSet.frameNumber = -1;
#else
        mTextureSet[0].frameNumber = -1;
        mTextureSet[1].frameNumber = -1;
#endif

        mCurOffset = mHeader.movieDataOffsets;
        mReadSize = mHeader.firstFrameSize;
        mReadIndex = 0;
        mTotalReadFrame = 0;
        mDvdError = 0;
        mNextDecodeIndex = 0;
        const NebulaHostServices* const host = nebula_host();
        host->movie_audio_control(host->user, NEBULA_MOVIE_AUDIO_RESET, 0, 0);

#if (VERSION == RMGK01)
        _310 = 0;
#endif
        return true;
    }

    return false;
}

s32 THPSimplePlayerWrapper::decode(s32 audio) {
    bool isValid = mReadBuffer[mNextDecodeIndex].isValid == true;
    if (isValid) {
        NEBULA_BE(u32)* compSize = (NEBULA_BE(u32)*)mReadBuffer[mNextDecodeIndex].ptr + 2;
        u8* ptr = mReadBuffer[mNextDecodeIndex].ptr + mFrameComp.numComponents * 4 + 8;

        if (mAudioExist) {
            if (audio < 0 || audio >= mAudioInfo.sndNumTracks) {
                return 4;
            }

            const NebulaHostServices* const host = nebula_host();
            if (host->movie_audio(host->user, nullptr, 0) != 0) {
                for (s32 i = 0; i < mFrameComp.numComponents; i++) {
                    switch (mFrameComp.frameComp[i]) {
                    case 0:
                        if (!videoDecode(ptr, *compSize)) {
                            return 1;
                        }
                        break;
                    case 1:
                        // The host decodes and plays the frame's sound (the track's record).
                        host->movie_audio(host->user, ptr + (*compSize) * audio, *compSize);
                        break;
                    }

                    ptr += *compSize;
                    compSize++;
                }
            } else {
                return 3;
            }
        } else {
            for (s32 i = 0; i < mFrameComp.numComponents; i++) {
                switch (mFrameComp.frameComp[i]) {
                case 0:
                    if (!videoDecode(ptr, *compSize)) {
                        return 1;
                    }
                    break;
                }

                ptr += *compSize;
                compSize++;
            }
        }

        mReadBuffer[mNextDecodeIndex].isValid = 0;
        mNextDecodeIndex = getNextBuffer(mNextDecodeIndex);
        checkPrefetch();
        return 0;
    }

    return 2;
}

#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
s32 THPSimplePlayerWrapper::drawCurrentFrame(_GXRenderModeObj* rmode, u32 x, u32 y, u32 polyW, u32 polyH) {
    if (mTextureSet.frameNumber < 0) {
        return -1;
    }

#ifdef TARGET_PC
    __NebulaMovieFrame(sNebulaMovieName,
                       mHeader.numFrames > 0 ? mTextureSet.frameNumber % (s32)mHeader.numFrames : mTextureSet.frameNumber,
                       mHeader.frameRate, mTextureSet.ytexture, mTextureSet.utexture, mTextureSet.vtexture,
                       mVideoInfo.xSize, mVideoInfo.ySize);
#endif
    THPGXYuv2RgbSetup(rmode);
    THPGXYuv2RgbDraw(mTextureSet.ytexture, mTextureSet.utexture, mTextureSet.vtexture, x, y, mVideoInfo.xSize, mVideoInfo.ySize, polyW, polyH);
    THPGXRestore();

    return mTextureSet.frameNumber;
}
#else
s32 THPSimplePlayerWrapper::drawCurrentFrame(_GXRenderModeObj* rmode, u32 x, u32 y, u32 polyW, u32 polyH) {
    if (mTextureSet[_310].frameNumber < 0) {
        return -1;
    }

#ifdef TARGET_PC
    __NebulaMovieFrame(sNebulaMovieName,
                       mHeader.numFrames > 0 ? mTextureSet[_310].frameNumber % (s32)mHeader.numFrames
                                             : mTextureSet[_310].frameNumber,
                       mHeader.frameRate, mTextureSet[_310].ytexture, mTextureSet[_310].utexture,
                       mTextureSet[_310].vtexture, mVideoInfo.xSize, mVideoInfo.ySize);
#endif
    THPGXYuv2RgbSetup(rmode);
    THPGXYuv2RgbDraw(mTextureSet[_310].ytexture, mTextureSet[_310].utexture, mTextureSet[_310].vtexture, x, y, mVideoInfo.xSize, mVideoInfo.ySize,
                     polyW, polyH);
    THPGXRestore();
    s32 ret = mTextureSet[_310].frameNumber;
    _310 = _310 == 0;
    return ret;
}
#endif

bool THPSimplePlayerWrapper::getVideoInfo(THPVideoInfo* pInfo) const {
    if (!mOpen) {
        return false;
    }

    memcpy(pInfo, &mVideoInfo, sizeof(mVideoInfo));
    return true;
}

f32 THPSimplePlayerWrapper::getFrameRate() const {
    if (mOpen) {
        return mHeader.frameRate;
    }

    return 0.0f;
}

s32 THPSimplePlayerWrapper::getTotalFrame() const {
    if (mOpen) {
        return mHeader.numFrames;
    }

    return 0;
}

#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
// The host decodes the frame's JPEG data into the texture set's Y, U and V planes (thp_video_decode).
bool THPSimplePlayerWrapper::videoDecode(u8* pFile, u32 size) {
    const NebulaHostServices* const host = nebula_host();
    if (host->thp_video_decode(host->user, pFile, size, mTextureSet.ytexture, mTextureSet.utexture, mTextureSet.vtexture)) {
        return false;
    }

    mTextureSet.frameNumber = mReadBuffer[mNextDecodeIndex].frameNumber;
    return true;
}
#else
bool THPSimplePlayerWrapper::videoDecode(u8* pFile, u32 size) {
    const NebulaHostServices* const host = nebula_host();
    if (host->thp_video_decode(host->user, pFile, size, mTextureSet[_310].ytexture, mTextureSet[_310].utexture,
                               mTextureSet[_310].vtexture)) {
        return false;
    }

    mTextureSet[_310].frameNumber = mReadBuffer[mNextDecodeIndex].frameNumber;
    return true;
}
#endif

void THPSimplePlayerWrapper::readFrameAsync() {
    if (!mDvdError && mPreFetchState != 0) {
        if (mTotalReadFrame > mHeader.numFrames - 1) {
            if (mLoop == 1) {
                mTotalReadFrame = 0;
                mCurOffset = mHeader.movieDataOffsets;
                mReadSize = mHeader.firstFrameSize;
            } else {
                return;
            }
        }

        mReadProgress = 1;
        mRequest.mUser = this;

        if (!NebulaDisc::readAsync(&mFile, &mRequest, mReadBuffer[mReadIndex].ptr, mReadSize, mCurOffset, ::dvdCallBackFunc)) {
            mReadProgress = 0;
            mDvdError = 1;
        }
    }

    return;
}

void THPSimplePlayerWrapper::checkPrefetch() {
    BOOL in = NebulaKernel::disableInterrupts();
    bool isValid = mReadBuffer[mReadIndex].isValid == true;
    if (!isValid && !mReadProgress) {
        readFrameAsync();
    }

    NebulaKernel::restoreInterrupts(in);
}

void THPSimplePlayerWrapper::dvdCallBack(s32 result) {
    if (result == -1) {
        mDvdError = 1;
        return;
    } else if (result == -3) {
        return;
    }

    mReadProgress = 0;
    mReadBuffer[mReadIndex].frameNumber = mTotalReadFrame;
    mTotalReadFrame++;
    mReadBuffer[mReadIndex].isValid = 1;
    mCurOffset += mReadSize;
    mReadSize = *(NEBULA_BE(u32)*)mReadBuffer[mReadIndex].ptr;
    int index = mReadIndex;
    index = getNextBuffer(index);
    mReadIndex = index;

    bool valid = mReadBuffer[index].isValid == TRUE;

    if (!valid) {
        readFrameAsync();
    }
}

void THPSimplePlayerWrapper::readAsyncCallBack(s32 a1) {
    if (a1 < 0) {
        if (!isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadHeader)) && !isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadFrameComp)) &&
            !isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadVideoComp)) &&
            !isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadAudioComp))) {
            isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadPreLoad));
        }

        return;
    }

    if (isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadHeader))) {
        endReadHeader();
        setNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadFrameComp));
        return;
    }

    if (isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadFrameComp))) {
        _10 = 0;
        endReadFrameComp();
        checkComponentsInFrame(_10);
        return;
    }

    if (isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadVideoComp))) {
        endReadVideoComp();
        if (tryFinishDvdOpen()) {
            return;
        }

        checkComponentsInFrame(_10);
        return;
    }

    if (isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadAudioComp))) {
        endReadAudioComp();
        if (tryFinishDvdOpen()) {
            return;
        }

        checkComponentsInFrame(_10);
        return;
    }

    if (isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadPreLoad))) {
        endReadPreLoadOne();
        setNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadPreLoad));
    }
}

s32 THPSimplePlayerWrapper::getNextBuffer(u32 idx) const {
    return ((idx) + 1 >= 0x14 ? 0 : (idx) + 1);
}

bool THPSimplePlayerWrapper::tryDvdOpen(const char* pFileName) {
    if (!_8) {
        return false;
    }

    if (mOpen) {
        return false;
    }

    MR::zeroMemory(&mVideoInfo, sizeof(THPVideoInfo));
    MR::zeroMemory(&mAudioInfo, sizeof(THPAudioInfo));

    if (!NebulaDisc::open(pFileName, &mFile)) {
        return false;
    }

    _C = 0;
    _9 = 0;
    return true;
}

void THPSimplePlayerWrapper::setupParams() {
    mCurOffset = mHeader.movieDataOffsets;
    mReadSize = mHeader.firstFrameSize;
    mReadIndex = 0;
    mTotalReadFrame = 0;
    mDvdError = 0;
#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
    mTextureSet.frameNumber = -1;
#else
    mTextureSet[0].frameNumber = -1;
    mTextureSet[1].frameNumber = -1;
#endif
    mNextDecodeIndex = 0;
    mPreFetchState = 0;
    mAudioState = 0;
    mLoop = 0;
    mOpen = 1;
#if (VERSION == RMGK01)
    _310 = 0;
#endif
    resetAudioParams();
}

namespace {
    void readAsyncCallBackFunc(s32 a1, NebulaDisc::Request* pRequest) {
        THPSimplePlayerWrapper* player = (THPSimplePlayerWrapper*)pRequest->mUser;
        player->readAsyncCallBack(a1);
    }
};  // namespace

void THPSimplePlayerWrapper::exeWait() {
}

void THPSimplePlayerWrapper::exeReadHeader() {
    if (MR::isFirstStep(this)) {
        mRequest.mUser = this;
        NebulaDisc::readAsync(&mFile, &mRequest, WorkBuffer, 64, _C, ::readAsyncCallBackFunc);
    }
}

void THPSimplePlayerWrapper::exeReadFrameComp() {
    if (MR::isFirstStep(this)) {
        mRequest.mUser = this;
        _C = mHeader.compInfoDataOffsets;
        NebulaDisc::readAsync(&mFile, &mRequest, WorkBuffer, 32, _C, ::readAsyncCallBackFunc);
    }
}

void THPSimplePlayerWrapper::exeReadVideoComp() {
    if (MR::isFirstStep(this)) {
        mRequest.mUser = this;
        NebulaDisc::readAsync(&mFile, &mRequest, WorkBuffer, 32, _C, ::readAsyncCallBackFunc);
    }
}

void THPSimplePlayerWrapper::exeReadAudioComp() {
    if (MR::isFirstStep(this)) {
        mRequest.mUser = this;
        NebulaDisc::readAsync(&mFile, &mRequest, WorkBuffer, 32, _C, ::readAsyncCallBackFunc);
    }
}

#ifdef TARGET_PC
namespace {
    // THP headers are big-endian 32-bit words (copied into the player's host-order structs here).
    void swapWords(void* p, u32 size) {
        u32* words = static_cast< u32* >(p);
        for (u32 i = 0; i < size / sizeof(u32); i++) {
            words[i] = __builtin_bswap32(words[i]);
        }
    }
}  // namespace
#endif

void THPSimplePlayerWrapper::endReadHeader() {
    memcpy(&mHeader, WorkBuffer, sizeof(mHeader));
#ifdef TARGET_PC
    swapWords(&mHeader.version, sizeof(mHeader) - sizeof(mHeader.magic));
#endif
    if (strcmp(mHeader.magic, "THP")) {
        return;
    }

    if (mHeader.version != 0x11000) {
    }
}

void THPSimplePlayerWrapper::endReadFrameComp() {
    memcpy(&mFrameComp, WorkBuffer, sizeof(mFrameComp));
#ifdef TARGET_PC
    swapWords(&mFrameComp.numComponents, sizeof(mFrameComp.numComponents));
#endif
    mAudioExist = 0;
    _C += 0x14;
}

void THPSimplePlayerWrapper::endReadVideoComp() {
    memcpy(&mVideoInfo, WorkBuffer, sizeof(mVideoInfo));
#ifdef TARGET_PC
    swapWords(&mVideoInfo, sizeof(mVideoInfo));
#endif
    _C += 12;
    _10++;
}

void THPSimplePlayerWrapper::endReadAudioComp() {
    memcpy(&mAudioInfo, WorkBuffer, sizeof(mAudioInfo));
#ifdef TARGET_PC
    swapWords(&mAudioInfo, sizeof(mAudioInfo));
#endif
    mAudioExist = 1;
    _C += 0x10;
    _10++;
}

void THPSimplePlayerWrapper::exeReadPreLoad() {
    if (MR::isFirstStep(this)) {
        NebulaDisc::readAsync(&mFile, &mRequest, mReadBuffer[mReadIndex].ptr, mReadSize, mCurOffset, ::readAsyncCallBackFunc);
    }

    if (!_314) {
        mPreFetchState = 1;
        setNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeWait));
    }
}

void THPSimplePlayerWrapper::endReadPreLoadOne() {
    mCurOffset += mReadSize;
    // Each frame starts with the next frame's size (big-endian).
    mReadSize = *(NEBULA_BE(s32)*)mReadBuffer[mReadIndex].ptr;
    mReadBuffer[mReadIndex].isValid = 1;
    mReadBuffer[mReadIndex].frameNumber = mTotalReadFrame;
    mReadIndex = getNextBuffer(mReadIndex);
    mTotalReadFrame++;

    if (mTotalReadFrame > mHeader.numFrames - 1 && mLoop == 1) {
        mTotalReadFrame = 0;
        mCurOffset = mHeader.movieDataOffsets;
        mReadSize = mHeader.firstFrameSize;
    }

    _314--;
}

bool THPSimplePlayerWrapper::checkComponentsInFrame(s32 comp) {
    if (comp < 0 || mFrameComp.numComponents <= comp) {
        return false;
    }

    switch (mFrameComp.frameComp[comp]) {
    case 0:
        setNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadVideoComp));
        return true;
    case 1:
        setNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadAudioComp));
        return true;
    }

    return false;
}

bool THPSimplePlayerWrapper::tryFinishDvdOpen() {
    if (_10 < mFrameComp.numComponents) {
        return false;
    }

    setupParams();
    _9 = 1;
    setNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeWait));
    return true;
}

// The movie's sound plays from the host while the movie with it is open.
void THPSimplePlayerWrapper::resetAudioParams() {
    const NebulaHostServices* const host = nebula_host();
    if (mAudioExist) {
        const bool mono = NebulaSystemSettings::soundMode() == NebulaSystemSettings::SoundModeMono;
        host->movie_audio_control(host->user, NEBULA_MOVIE_AUDIO_OPEN, mono ? 1 : 0, 0);
    } else {
        host->movie_audio_control(host->user, NEBULA_MOVIE_AUDIO_CLOSE, 0, 0);
    }
}

bool THPSimplePlayerWrapper::isAudioProcessValid() {
    return mOpen && mAudioExist;
}

bool THPSimplePlayerWrapper::setVolume(s32 volume, s32 time) {
    if (!isAudioProcessValid()) {
        return false;
    }

    if (volume > 127) {
        volume = 127;
    }

    if (volume < 0) {
        volume = 0;
    }

    if (time > 60000) {
        time = 60000;
    }

    if (time < 0) {
        time = 0;
    }

    const NebulaHostServices* const host = nebula_host();
    host->movie_audio_control(host->user, NEBULA_MOVIE_AUDIO_VOLUME, volume, time);
    return true;
}

bool THPSimplePlayerWrapper::isPreLoading() const {
    return isNerve(GET_NERVE(THPSimplePlayerWrapper, HostTypeReadPreLoad));
}

void THPSimplePlayerWrapper::setUnpauseFrameFlag() {
    const NebulaHostServices* const host = nebula_host();
    host->movie_audio_control(host->user, NEBULA_MOVIE_AUDIO_RESUME, 0, 0);
}
