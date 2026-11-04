#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Screen/MoviePlayerSimple.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/MovieDraw.hpp"
#include "Game/Screen/MovieStream.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/HomeButtonStateNotifier.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/JKernel/JKRDisposer.hpp>
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaClock.hpp"

namespace {
    static const s16 sCinemaFrameHeightTop = 44;
    static const s16 sCinemaFrameHeightBottom = 412;
    static const NebulaColor8 sCinemaFrameColor = {0, 0, 0, 255};
};  // namespace

namespace {
    NEW_NERVE(MoviePlayerSimpleOff, MoviePlayerSimple, SimpleOff);
    NEW_NERVE(MoviePlayerSimpleOpen, MoviePlayerSimple, Open);
    NEW_NERVE(MoviePlayerSimplePlaying, MoviePlayerSimple, Playing);
    NEW_NERVE(MoviePlayerSimpleSuspend, MoviePlayerSimple, SimpleSuspend);
};  // namespace

MoviePlayerSimple::MoviePlayerSimple()
    : LayoutActor("\203\200\201\133\203\162\201\133\203\166\203\214\203\103\203\204\201\133" /* ムービープレイヤー */, true), JKRDisposer(), mMovie(nullptr), mStream(nullptr), _44(false), _45(false) {
    mMovie = new Movie();
    mMovie->mMovieName = "";
    mMovie->mBuffer = nullptr;
    mMovie->mCurrentFrame = -1;
    mMovie->_18 = false;
    mMovie->_19 = false;
    mMovie->mFrameRateDefault = 1;
    mMovie->_20 = 0;
    mMovie->_24 = 0;
}

MoviePlayerSimple::~MoviePlayerSimple() {
    SingletonHolder< GameSystem >::get()->mHomeButtonStateNotifier->unregisterMoviePlayerSimple(this);
    mStream->quit();
    delete mStream;
    NebulaArena::destroy(mHeap);
}

void MoviePlayerSimple::init(const JMapInfoIter& rIter) {
    MR::connectToSceneMovie(this);

    mStream = new MovieStream();

    mHeap = NebulaArena::create(MR::getCurrentHeap());

    initNerve(GET_NERVE_ANON(MoviePlayerSimpleOff));
    kill();
}

void MoviePlayerSimple::draw() const {
    if (!isNerve(GET_NERVE_ANON(MoviePlayerSimplePlaying)) && !isNerve(GET_NERVE_ANON(MoviePlayerSimpleSuspend))) {
        NebulaColor8 fillColor;
        fillColor.r = 0;
        fillColor.g = 0;
        fillColor.b = 0;
        fillColor.a = 255;

        MR::fillScreen(fillColor);
    } else {
        drawCinemaFrame();

        u32 frameBufferWidth;

        if (MR::isScreen16Per9()) {
            frameBufferWidth = MR::getScreenWidth();
        } else {
            frameBufferWidth = MR::getScreenWidth() * 4 / 3;  // 16:9 on a 4:3 picture
        }

        u32 ySize = mMovie->mVideoInfo.mHeight;
        u32 v4 = (MR::getScreenWidth() - static_cast< s32 >(frameBufferWidth)) / 2;
        u32 v5 = (MR::getScreenHeight() - ySize) / 2;
        MovieDraw::setLinearFilter(true);
        if (mStream->hasSound() && isNerve(GET_NERVE_ANON(MoviePlayerSimplePlaying))) {
            mStream->decodeHeardFrame();
        }
        mMovie->mCurrentFrame = mStream->drawCurrentFrame(v4, v5, frameBufferWidth, ySize);
    }
}

void MoviePlayerSimple::startMovie(const char* pMovieName, bool a2) {
    if (!MR::isFileExist(pMovieName, false)) {
        NEBULA_PANIC("Moive File NOT found.");
    }

    mMovie->mMovieName = pMovieName;
    mMovie->_18 = a2;
    mMovie->_24 = 0;

    play(a2);
    appear();
    MR::requestMovementOn(this);
    SingletonHolder< GameSystem >::get()->mHomeButtonStateNotifier->registerMoviePlayerSimple(this);
}

void MoviePlayerSimple::stopMovie() {
    stop();
    kill();
    setNerve(GET_NERVE_ANON(MoviePlayerSimpleOff));
}

bool MoviePlayerSimple::isMovieActive() const {
    if (MR::isDead(this)) {
        return false;
    }

    return isNerve(GET_NERVE_ANON(MoviePlayerSimpleOpen)) ||
           isNerve(GET_NERVE_ANON(MoviePlayerSimplePlaying)) || isNerve(GET_NERVE_ANON(MoviePlayerSimpleSuspend));
}

bool MoviePlayerSimple::isMoviePlaying() const {
    if (isMovieActive()) {
        return isNerve(GET_NERVE_ANON(MoviePlayerSimplePlaying));
    }

    return false;
}

s32 MoviePlayerSimple::getCurrentFrame() const {
    if (isMovieActive()) {
        return mMovie->mCurrentFrame;
    }

    return -1;
}

s32 MoviePlayerSimple::getTotalFrame() const {
    return mStream->getTotalFrame();
}

void MoviePlayerSimple::setFrameRateDefault() {
    f32 frameRate = mStream->getFrameRate();

    if (MR::isNearZero(frameRate - 59.94f)) {
        mMovie->mFrameRateDefault = 1;
    } else if (MR::isNearZero(frameRate - 29.97f)) {
        mMovie->mFrameRateDefault = 2;
    }
}

void MoviePlayerSimple::setVolume(f32 volume, s32 time) {
    mStream->setVolume(volume * 127.0f, time);
}

void MoviePlayerSimple::setUnpauseHomeButtonFlag() {
    mStream->setUnpauseFrameFlag();
}

void MoviePlayerSimple::exeSimpleOff() {
}

void MoviePlayerSimple::exeOpen() {
    mStream->open(mMovie->mMovieName);
    mMovie->mVideoInfo = mStream->getVideoInfo();
    mMovie->_20 = 0;
    mMovie->_19 = false;
    setFrameRateDefault();
    mMovie->mBuffer = new (mHeap, 0) u8[mStream->calcNeedMemory()];
    mStream->setBuffer(mMovie->mBuffer);
    mStream->start(_44);
    setNerve(GET_NERVE_ANON(MoviePlayerSimplePlaying));
    appear();
}

void MoviePlayerSimple::exePlaying() {
    // The sound keeps the time, whatever the game's frame rate or the screen's: draw shows the frame being heard.
    if (mStream->hasSound()) {
        mStream->feedSound(0);

        if (!mMovie->_18 && mStream->getSoundPosition() >= mStream->getTotalFrame()) {
            mMovie->_24 = 0;
            stop();
            setNerve(GET_NERVE_ANON(MoviePlayerSimpleOff));
        }
        return;
    }

    if (mMovie->mCurrentFrame + 1 == mStream->getTotalFrame()) {
        mMovie->_24 = 0;

        if (!mMovie->_18) {
            stop();
            setNerve(GET_NERVE_ANON(MoviePlayerSimpleOff));
            return;
        }
    }

    if (!mMovie->_19) {
        s32 result = mStream->decode();

        switch (result) {
        case 0:
            mMovie->_19 = true;
            break;
        case 2:
            mMovie->_24++;
            break;
        }
    }

    mMovie->_20++;

    if (mMovie->_19) {
        if (mMovie->mFrameRateDefault <= mMovie->_20) {
            mMovie->_20 = 0;
            mMovie->_19 = false;
        }
    }
}

void MoviePlayerSimple::exeSimpleSuspend() {
}

void MoviePlayerSimple::play(bool a1) {
    _44 = a1;
    setNerve(GET_NERVE_ANON(MoviePlayerSimpleOpen));
    MR::startStarPointerModeDemo(this);
    _45 = GameSystemFunction::setPermissionToCheckInput(false);
}

void MoviePlayerSimple::stop() {
    mStream->stop();
    mStream->close();
    delete[] mMovie->mBuffer;
    mMovie->mBuffer = nullptr;
    MR::endStarPointerMode(this);
    GameSystemFunction::setPermissionToCheckInput(_45);
}

void MoviePlayerSimple::drawCinemaFrame() const {
    MR::fillScreenSetup(::sCinemaFrameColor);

    s16 width = MR::getScreenWidth();
    TBox2s top(0, 0, width, ::sCinemaFrameHeightTop);
    MR::fillScreenArea(top.i, top.f);

    s16 height = MR::getScreenHeight();
    TBox2s bottom(0, ::sCinemaFrameHeightBottom, width, height);
    MR::fillScreenArea(bottom.i, bottom.f);
}
