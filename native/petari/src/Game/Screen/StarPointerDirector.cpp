#include "Game/Screen/StarPointerDirector.hpp"
#ifdef TARGET_PC
#include "Game/Util/MotionSignal.hpp"
#endif
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/StarPointerController.hpp"
#include "Game/Screen/StarPointerGuidance.hpp"
#include "Game/Screen/StarPointerLayout.hpp"
#include "Game/System/DrawSyncManager.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
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
        mIsOSPointerMode = true;
        return;
    }

    if (mIsOSPointerMode && mStarPointerLayouts != nullptr) {
        for (s32 channel = 0; channel < StarPointerFunction::getNumStarPointer(); channel++) {
            getStarPointerLayout(channel)->hideBlur();
        }
    }

    mIsOSPointerMode = false;

    if (_18 < 0) {
        _18++;
    }

    mTransHolder->movement();

    for (s32 channel = 0; channel < StarPointerFunction::getNumStarPointer(); channel++) {
        if (MR::isConnectedWPad(channel)) {
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
#ifdef TARGET_PC
    MotionSignal::starPointerDirector(this);
#endif
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

    mGuidance = new StarPointerGuidance("\203\130\203\136\201\133\203\174\203\103\203\223\203\136\203\113\203\103\203\137\203\223\203\130" /* スターポインタガイダンス */);
    mGuidance->initWithoutIter();
}

namespace {
    static f32 mtx_identity[3][4] = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
};  // namespace

#ifdef TARGET_PC
namespace {
    // The projection is a 4x4 copy; the console read the 3x4 identity and the 16 bytes after it (a vtable).
    static f32 proj_identity[4][4] = {
        {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
};  // namespace

StarPointerTransformHolder::StarPointerTransformHolder() : mViewMtx(mtx_identity), mProjMtx(proj_identity) {
}
#else
StarPointerTransformHolder::StarPointerTransformHolder() : mViewMtx(mtx_identity), mProjMtx(mtx_identity) {
}
#endif

void StarPointerTransformHolder::movement() {
    f32 fovyRad = PI_180 * getFovy();
    mFocalLength = ((MR::getScreenHeight() * 0.5f) / MR::tan(fovyRad * 0.5f));
}

StarPointerPeekZ::StarPointerPeekZ() {
    mInfos = new DpdInfo*[2];
    mToken = DrawSyncManager::sInstance->setCallback(2, 1, this);
}

void StarPointerPeekZ::setDrawSyncToken() {
    GXGetProjectionv(mProjectionParameters);
    GXGetViewportv(mViewportParameters);
    DrawSyncManager::sInstance->pushBreakPoint();
    GXSetDrawSync(mToken);
}

void StarPointerPeekZ::drawSyncCallback(u16 token) {
    for (s32 channel = 0; channel < StarPointerFunction::getNumStarPointer(); channel++) {
        if (MR::isInRange(mInfos[channel]->mPos.x, 0.0f, MR::getScreenWidth() - 1) &&
            MR::isInRange(mInfos[channel]->mPos.y, 0.0f, MR::getScreenHeight() - 1)) {
            TVec2f pos;
            MR::convertScreenPosToFrameBufferPos(&pos, mInfos[channel]->mPos);
            GXPeekZ((s32)(pos.x), (s32)(pos.y), &mInfos[channel]->mZDepth);
            mInfos[channel]->mDrawReady = true;
        }
    }
}

namespace StarPointerFunction {
#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
    // No source in the decompilation; written from the RMGE01 assembly (0x80385C1C). Moves rPos along rDir to the
    // first screen edge (inset by the margins) that stays in the screen.
    bool forceOutsideScreenEdge(TVec2f* pOut, const TVec2f& rPos, const TVec2f& rDir, f32 marginX, f32 marginY) {
        if (!isInScreen(rPos, marginX, marginY)) {
            return false;
        }

        TVec2f dir;
        dir.x = rDir.x;
        dir.y = rDir.y;
        MR::normalizeOrZero(&dir);

        if (dir.x != 0.0f) {
            f32 edge = (0.0f >= dir.x) ? marginX : (f32)MR::getScreenWidth() - marginX;
            f32 t = (edge - rPos.x) / dir.x;
            TVec2f pos;
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
            TVec2f pos;
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

    bool isInScreen(const TVec2f& rPos, f32 marginX, f32 marginY) {
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
#endif

    bool isOnScreenEdge(const TVec2f& rPos, f32 marginX, f32 marginY) {
        if (rPos.x <= marginX) {
            return true;
        }

        if (rPos.y <= marginY) {
            return true;
        }

        if (MR::getScreenWidth() - marginX <= rPos.x) {
            return true;
        }

        if (MR::getScreenHeight() - marginY <= rPos.y) {
            return true;
        }

        return false;
    }

    bool isOnScreenEdge(s32 channel) {
        TVec2f pos;
        MR::convertPaneLocalPosToScreenPos(&pos, MR::getRootPane(getStarPointerDirector()->getStarPointerLayout(channel)), TVec2f(0.0f, 0.0f));
        return isOnScreenEdge(pos, 0.0f, 0.0f);
    }

    bool forceInsideScreenEdge(TVec2f* pPos) {
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

    s32 getPastPointNum(s32 channel) {
        return getStarPointerDirector()->getStarPointerController(channel)->mPastPointNum;
    }

    const TVec2f& getPastPosition(s32 channel, s32 index) {
        return getStarPointerDirector()->getStarPointerController(channel)->mPastPosition[index];
    }

    s32 getNextPastPointNum(s32 channel) {
        return getStarPointerDirector()->getStarPointerController(channel)->mNextPastPointNum;
    }

    bool canShoot(s32 channel) {
        return getStarPointerDirector()->getStarPointerLayout(channel)->mShootDisabled == false;
    }
};  // namespace StarPointerFunction
