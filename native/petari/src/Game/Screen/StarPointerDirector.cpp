#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/StarPointerController.hpp"
#include "Game/Screen/StarPointerGuidance.hpp"
#include "Game/Screen/StarPointerLayout.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StarPointerUtil.hpp"

StarPointerDirector::StarPointerDirector()
    : mIsUpdateTransHolder(), mIsAllowP1StarPieceShot(), mIsAllowP2StarPieceShot(), mControllers(), mStarPointerLayouts(), mTransHolder(), mPeekZ(),
      mGuidance(), mNozzleAimPos(0.0f, 0.0f, 0.0f) {
    mPeekZ = new StarPointerPeekZ();
    mTransHolder = new StarPointerTransformHolder();
    mControllers = new StarPointerController[StarPointerFunction::getNumStarPointer()];

    for (s32 channel = 0; channel < StarPointerFunction::getNumStarPointer(); channel++) {
        mControllers[channel].initAndSetPort(channel);
        mPeekZ->mInfos[channel] = &mControllers[channel].mInfo;
    }
}

void StarPointerDirector::update() {
    if (MR::isStarPointerModeHomeButton() || MR::isStarPointerModeErrorWindow()) {
        mIsSystemPointerMode = true;
        return;
    }

    if (mIsSystemPointerMode && mStarPointerLayouts != nullptr) {
        for (s32 channel = 0; channel < StarPointerFunction::getNumStarPointer(); channel++) {
            getStarPointerLayout(channel)->hideBlur();
        }
    }

    mIsSystemPointerMode = false;

    mTransHolder->movement();

    for (s32 channel = 0; channel < StarPointerFunction::getNumStarPointer(); channel++) {
        if (MR::isConnectedInput(channel)) {
            getStarPointerController(channel)->movement(mPeekZ->mProjectionParameters, mPeekZ->mViewportParameters);
        }

        if (mStarPointerLayouts != nullptr) {
            getStarPointerLayout(channel)->movement();
            getStarPointerLayout(channel)->calcAnim();
        }
    }

    if (mGuidance != nullptr) {
        mGuidance->movement();
        mGuidance->calcAnim();
    }
}

void StarPointerDirector::draw() {
    MotionSignal::drawObject(mDrawSlot, 0);
    if (mGuidance != nullptr) {
        mGuidance->draw();
    }

    if (mStarPointerLayouts != nullptr) {
        for (s32 channel = 0; channel < StarPointerFunction::getNumStarPointer(); channel++) {
            getStarPointerLayout(channel)->draw();
        }
    }
}

void StarPointerDirector::startHandPointer() {
    if (mStarPointerLayouts != nullptr) {
        mStarPointerLayouts[0].changeLayout(StarPointerKind_HandPointer);
    }
}

void StarPointerDirector::startHandPointerReactionWithCrossCursor() {
    if (mStarPointerLayouts != nullptr) {
        mStarPointerLayouts[0].changeLayout(StarPointerKind_HandPointerReactionWithCrossCursor);
    }
}

void StarPointerDirector::startFingerPointer() {
    if (mStarPointerLayouts != nullptr) {
        mStarPointerLayouts[0].changeLayout(StarPointerKind_FingerPointer);
    }
}

void StarPointerDirector::startStarPointer() {
    if (mStarPointerLayouts != nullptr) {
        mStarPointerLayouts[0].changeLayout(StarPointerKind_StarPointer);
    }
}

void StarPointerDirector::startStarPointerNozzle() {
    if (mStarPointerLayouts != nullptr) {
        mStarPointerLayouts[0].changeLayout(StarPointerKind_StarPointerNozzle);
    }
}

void StarPointerDirector::setGameSceneCameraMtx() {
    if (mIsUpdateTransHolder) {
        mTransHolder->mViewMtx.setInline(MR::getCameraViewMtx());
        mTransHolder->mProjMtx.setInline(MR::getCameraProjectionMtx());
        mTransHolder->mFovy = MR::getRenderFovy();
    }
}

StarPointerController* StarPointerDirector::getStarPointerController(s32 channel) const {
    return &mControllers[channel];
}

StarPointerLayout* StarPointerDirector::getStarPointerLayout(s32 channel) const {
    return &mStarPointerLayouts[channel];
}

void StarPointerDirector::createLayout() {
    mStarPointerLayouts = new StarPointerLayout[StarPointerFunction::getNumStarPointer()];

    for (u32 channel = 0; channel < StarPointerFunction::getNumStarPointer(); channel++) {
        mStarPointerLayouts[channel].initWithPort(channel);
        mStarPointerLayouts[channel].mDirector = this;
    }

    mGuidance = new StarPointerGuidance("スターポインタガイダンス");
    mGuidance->initWithoutIter();
}

namespace {
    static f32 mtx_identity[3][4] = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
};  // namespace

namespace {
    // The projection is a 4x4 copy.
    static f32 proj_identity[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
};  // namespace

StarPointerTransformHolder::StarPointerTransformHolder() : mViewMtx(mtx_identity), mProjMtx(proj_identity) {
}

void StarPointerTransformHolder::movement() {
}

StarPointerPeekZ::StarPointerPeekZ() : mAsker(Frame::newProbeAsker()) {
    mInfos = new PointerInfo*[2];
}

void StarPointerPeekZ::setDrawSyncToken() {
    Frame::getProjectionv(mProjectionParameters);
    Frame::getViewportv(mViewportParameters);
    // The depth under each pointer: what was asked last, and the picture drawn so far at where they are now.
    NebulaScreenProbe probes[2];
    const u32 count = StarPointerFunction::getNumStarPointer();
    const u32 done = Frame::screenProbeResults(cReadbackChannel, mAsker, probes, count);
    for (u32 channel = 0; channel < count; channel++) {
        if (MR::isInRange(mInfos[channel]->mPos.x, 0.0f, MR::getScreenWidth() - 1) &&
            MR::isInRange(mInfos[channel]->mPos.y, 0.0f, MR::getScreenHeight() - 1)) {
            mInfos[channel]->mZDepth = channel < done && probes[channel].x >= 0.0f ? probes[channel].value : 0.0f;
            mInfos[channel]->mDrawReady = true;

            Vec2f pos;
            MR::convertScreenPosToFrame(&pos, mInfos[channel]->mPos);
            probes[channel].x = pos.x;
            probes[channel].y = pos.y;
        } else {
            probes[channel].x = -1.0f;
            probes[channel].y = -1.0f;
        }
    }
    Frame::probeScreen(cReadbackChannel, mAsker, probes, count);
}

namespace StarPointerFunction {
    // Moves rPos along rDir to the
    // first screen edge (inset by the margins) that stays in the screen.
    bool forceOutsideScreenEdge(Vec2f* pOut, const Vec2f& rPos, const Vec2f& rDir, f32 marginX, f32 marginY) {
        if (!isInScreen(rPos, marginX, marginY)) {
            return false;
        }

        Vec2f dir;
        dir.x = rDir.x;
        dir.y = rDir.y;
        MR::normalizeOrZero(&dir);

        if (dir.x != 0.0f) {
            f32 edge = (0.0f >= dir.x) ? marginX : (f32)MR::getScreenWidth() - marginX;
            f32 t = (edge - rPos.x) / dir.x;
            Vec2f pos;
            pos.x = dir.x * t + rPos.x;
            pos.y = dir.y * t + rPos.y;
            if (isInScreen(pos, marginX, marginY)) {
                pOut->x = pos.x;
                pOut->y = pos.y;
                return true;
            }
        }

        if (dir.y != 0.0f) {
            f32 edge = (0.0f >= dir.y) ? marginY : (f32)MR::getScreenHeight() - marginY;
            f32 t = (edge - rPos.y) / dir.y;
            Vec2f pos;
            pos.x = dir.x * t + rPos.x;
            pos.y = dir.y * t + rPos.y;
            if (isInScreen(pos, marginX, marginY)) {
                pOut->x = pos.x;
                pOut->y = pos.y;
                return true;
            }
        }

        return false;
    }

    bool isInScreen(const Vec2f& rPos, f32 marginX, f32 marginY) {
        if (rPos.x < marginX) {
            return false;
        }

        if (rPos.y < marginY) {
            return false;
        }

        if (MR::getScreenWidth() - marginX < rPos.x) {
            return false;
        }

        if (MR::getScreenHeight() - marginY < rPos.y) {
            return false;
        }

        return true;
    }

    bool forceInsideScreenEdge(Vec2f* pPos) {
        bool forced = false;

        f32 margin = 0.0f;
        f32 width = MR::getScreenWidth() - margin;
        f32 height = MR::getScreenHeight() - margin;

        if (pPos->x < margin) {
            pPos->x = margin;
            forced = true;
        } else if (width < pPos->x) {
            pPos->x = width;
            forced = true;
        }

        if (pPos->y < margin) {
            pPos->y = margin;
            forced = true;
        } else if (height < pPos->y) {
            pPos->y = height;
            forced = true;
        }

        return forced;
    }

    StarPointerDirector* getStarPointerDirector() {
        return SingletonHolder< GameSystem >::get()->mObjHolder->mStarPointerDirector;
    }

    s32 getNumStarPointer() {
        return 2;
    }

    const Vec2f& getNextPastPosition(s32 channel, s32 index) {
        return getStarPointerDirector()->getStarPointerController(channel)->mNextPastPosition[index];
    }

    s32 getNextPastPointNum(s32 channel) {
        return getStarPointerDirector()->getStarPointerController(channel)->mNextPastPointNum;
    }

    bool canShoot(s32 channel) {
        return getStarPointerDirector()->getStarPointerLayout(channel)->mShootDisabled == false;
    }
};  // namespace StarPointerFunction
