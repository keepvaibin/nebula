#include "Game/Screen/MoviePlayerSimple.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/MovieDraw.hpp"
#include "Game/Screen/MovieStream.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setVolume_f1, "setVolume.f1", 127.0f);

namespace {
    const NebulaColor8 cBlack = {0, 0, 0, 255};

    NEW_NERVE(MoviePlayerSimpleOff, MoviePlayerSimple, SimpleOff);
    NEW_NERVE(MoviePlayerSimpleOpen, MoviePlayerSimple, Open);
    NEW_NERVE(MoviePlayerSimplePlaying, MoviePlayerSimple, Playing);
    NEW_NERVE(MoviePlayerSimpleSuspend, MoviePlayerSimple, SimpleSuspend);
};  // namespace

MoviePlayerSimple::MoviePlayerSimple()
    : LayoutActor("ムービープレイヤー", true), ArenaOwned(), mMovieName(""), mStream(nullptr), mLoop(false),
      mInputCheckedBefore(false) {
}

MoviePlayerSimple::~MoviePlayerSimple() {
    mStream->quit();
    delete mStream;
}

void MoviePlayerSimple::init(const TableIter& rIter) {
    MR::connectToSceneMovie(this);

    mStream = new MovieStream();

    initNerve(GET_NERVE_ANON(MoviePlayerSimpleOff));
    kill();
}

void MoviePlayerSimple::draw() const {
    const bool playing = isNerve(GET_NERVE_ANON(MoviePlayerSimplePlaying));
    if (!playing && !isNerve(GET_NERVE_ANON(MoviePlayerSimpleSuspend))) {
        MR::fillScreen(cBlack);
        return;
    }

    // As large as the screen holds the movie at its own shape, centred.
    const f32 screenWidth = MR::getScreenWidth();
    const f32 screenHeight = MR::getScreenHeight();
    const f32 screenAspect = MR::getScreenAspect();
    const f32 movieAspect = mStream->getDisplayAspect();
    f32 width = screenWidth;
    f32 height = screenHeight;
    if (movieAspect > screenAspect) {
        height = screenHeight * screenAspect / movieAspect;
    } else {
        width = screenWidth * movieAspect / screenAspect;
    }
    const f32 x = (screenWidth - width) * cNum_draw_f1;
    const f32 y = (screenHeight - height) * cNum_draw_f2;

    drawBands(x, y, width, height);
    MovieDraw::setLinearFilter(true);
    if (playing) {
        mStream->pickFrameAtPosition();
    }
    mStream->drawCurrentFrame(x, y, width, height);
}

// Black on the screen around the movie at x, y (width x height).
void MoviePlayerSimple::drawBands(f32 x, f32 y, f32 width, f32 height) const {
    const SiteMaterial material = MR::fillScreenSetup(cBlack);
    TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
    const f32 screenWidth = MR::getScreenWidth();
    const f32 screenHeight = MR::getScreenHeight();
    if (y > 0.0f) {
        MR::fillScreenArea(site, Vec2f(0.0f, 0.0f), Vec2f(screenWidth, y));
        MR::fillScreenArea(site, Vec2f(0.0f, y + height), Vec2f(screenWidth, screenHeight));
    }
    if (x > 0.0f) {
        MR::fillScreenArea(site, Vec2f(0.0f, y), Vec2f(x, y + height));
        MR::fillScreenArea(site, Vec2f(x + width, y), Vec2f(screenWidth, y + height));
    }
}

void MoviePlayerSimple::startMovie(const char* pMovieName, bool loop) {
    if (!MR::isFileExist(pMovieName, false)) {
        NEBULA_PANIC("%s: no such movie", pMovieName);
    }

    mMovieName = pMovieName;
    play(loop);
    appear();
    MR::requestMovementOn(this);
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
        return mStream->getPictureFrame();
    }

    return -1;
}

s32 MoviePlayerSimple::getTotalFrame() const {
    return mStream->getTotalFrame();
}

void MoviePlayerSimple::setVolume(f32 volume, s32 time) {
    mStream->setVolume(volume * cNum_setVolume_f1, time);
}

// Before the nerve feeds this frame's sound: the host fades the sound back in after the hold.
void MoviePlayerSimple::movement() {
    if (GameSystemFunction::isSceneResumed() && isMovieActive()) {
        mStream->setUnpauseFrameFlag();
    }

    LayoutActor::movement();
}

void MoviePlayerSimple::exeSimpleOff() {
}

void MoviePlayerSimple::exeOpen() {
    mStream->open(mMovieName);
    mStream->start(mLoop);
    setNerve(GET_NERVE_ANON(MoviePlayerSimplePlaying));
    appear();
}

// The movie's place is its sound's (fed ahead here), or the game's steps for one with none; draw shows the frame there.
void MoviePlayerSimple::exePlaying() {
    if (mStream->hasSound()) {
        mStream->feedSound(0);
    } else {
        mStream->stepSilent();
    }

    if (!mLoop && mStream->getPosition() >= mStream->getTotalFrame()) {
        stop();
        setNerve(GET_NERVE_ANON(MoviePlayerSimpleOff));
    }
}

void MoviePlayerSimple::exeSimpleSuspend() {
}

void MoviePlayerSimple::play(bool loop) {
    mLoop = loop;
    setNerve(GET_NERVE_ANON(MoviePlayerSimpleOpen));
    MR::startStarPointerModeDemo(this);
    mInputCheckedBefore = GameSystemFunction::setPermissionToCheckInput(false);
}

void MoviePlayerSimple::stop() {
    mStream->stop();
    mStream->close();
    MR::endStarPointerMode(this);
    GameSystemFunction::setPermissionToCheckInput(mInputCheckedBefore);
}
