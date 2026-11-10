#include "Game/Screen/StarPointerLayout.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/Screen/StarPointerBlur.hpp"
#include "Game/Screen/StarPointerCommandStream.hpp"
#include "Game/Screen/StarPointerController.hpp"
#include "Game/Screen/StarPointerDirector.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/System/InputPointer.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <nebula_geometry.hpp>

#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaInput.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_rotateTest_f1, "rotateTest.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startAnimHandPaa_f1, "startAnimHandPaa.f1", 2.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_tearDownHide_setAnimRate_1, "tearDownHide.setAnimRate.1", 1.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_isChanceToRumble_1, "isChanceToRumble.1", 1);
NEBULA_TUNABLE_NAMED(s32, cStep_isChanceToRumble_2, "isChanceToRumble.2", 1);

namespace {
    // Two colours for each of the four pad channels (setPortAndColor): the blur takes both, the command stream the first.
    static NebulaColor8 sColorA[] = {
        {0x00, 0x32, 0xFF, 0xFF},
        {0xFF, 0xFF, 0x00, 0xFF},
        {0x55, 0xFF, 0x00, 0xFF},
        {0x7F, 0x7F, 0x00, 0xFF},
    };

    static NebulaColor8 sColorB[] = {
        {0x00, 0x0A, 0x32, 0xFF},
        {0x32, 0x32, 0x00, 0xFF},
        {0x00, 0xFF, 0x00, 0xFF},
        {0x7F, 0x7F, 0x00, 0xFF},
    };

    NEBULA_TUNABLE(s32, sTouchCount, 1);
    NEBULA_TUNABLE(f32, sNormalRadius, 15);
    NEBULA_TUNABLE(f32, sHoldRadius, 45);
    NEBULA_TUNABLE(f32, sHandRadius, 20);

    NEBULA_TUNABLE(s32, hGripTime, 20);
    static const s32 hReleaseTime = NebulaFrameClock::stepsOfMilliseconds(500);
    static const s32 hHideRumbleTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sRotateMult, 900.0f);
    // static const f32 sRotateHandMult =
    NEBULA_TUNABLE(f32, hEndAnimRate, 2.0f);
    NEBULA_TUNABLE(s32, sBeginHideTime, 0);
    NEBULA_TUNABLE(f32, sDegreeMin, 0.0f);

    // A layout a mod changed may lack one of these panes (null): no pane, nothing to show, hide or turn.
    static void setPaneVisible(Ui::Pane* pPane, bool visible) {
        if (pPane != nullptr) {
            pPane->SetVisible(visible);
        }
    }

    static bool isPaneVisible(const Ui::Pane* pPane) {
        return pPane != nullptr && pPane->IsVisible();
    }
};  // namespace

namespace NrvStarPointerLayout {
    NEW_NERVE(HostTypeNrvWait, StarPointerLayout, Wait);
    NEW_NERVE_ONEND(HostTypeNrvOnReaction, StarPointerLayout, OnReaction, OnReaction);
    NEW_NERVE(HostTypeNrvGrip, StarPointerLayout, Grip);
    NEW_NERVE_ONEND(HostTypeNrvHold, StarPointerLayout, Hold, Hold);
    NEW_NERVE_ONEND(HostTypeNrvHoldTouch, StarPointerLayout, Hold, Hold);
    NEW_NERVE(HostTypeNrvRelease, StarPointerLayout, Release);
    NEW_NERVE(HostTypeNrvOutScreen, StarPointerLayout, OutScreen);
    NEW_NERVE_ONEND(HostTypeNrvNotConnected, StarPointerLayout, Hide, Hide);
    NEW_NERVE(HostTypeNrvHide, StarPointerLayout, Hide);
    NEW_NERVE(HostTypeNrvHideImmediately, StarPointerLayout, Hide);
    NEW_NERVE_ONEND(HostTypeNrvInvalidate, StarPointerLayout, Hide, Hide);
    NEW_NERVE_ONEND(HostTypeNrvInvalidateWait, StarPointerLayout, Hide, Hide);
};  // namespace NrvStarPointerLayout

StarPointerLayout::StarPointerLayout(const char* pName)
    : LayoutActor(pName, true), mPosition(0.0f, 0.0f), mPointerKind(StarPointerKind_NULL), mAnimType(AnimType_HandPaa), mTouchTimer(),
      mPadChannel(-1), mRadius(::sNormalRadius),
      mDirector(), mBlur(), mCommandStream(), mNumber(), mActor(), mNewTouchedID(), mTouchedID(), mStartTouch(), mStartDisableShoot(), mSingleTouch(),
      mShootDisabled(), mIsPointerValid(true), mIsAppear(true), mAppearTime() {
    lumacore::setClass(this, lumacore::kClassId< StarPointerLayout >);
}

void StarPointerLayout::initWithPort(s32 channel) {
    mPadChannel = channel;
    if (mPadChannel == PLAYER_2) {
        initLayoutManager("DPDPointerSub", 2);
    } else {
        initLayoutManager("DPDPointer", 1);
    }

    MR::createAndAddPaneCtrl(this, "StarPointer", 1);
    MR::createAndAddPaneCtrl(this, "HandPointer", 1);
    MR::createAndAddPaneCtrl(this, "StarFrame", 1);
    MR::createAndAddPaneCtrl(this, "Star", 1);
    if (mPadChannel == PLAYER_1) {
        MR::createAndAddPaneCtrl(this, "PicNozzle", 1);
    }
    MR::createAndAddGroupCtrl(this, "GroupRing", 1);
    mStarPointerPane = MR::getPane(this, "StarPointer");
    mHandPointerPane = MR::getPane(this, "HandPointer");
    // Null where the layout has no such pane (the second player's has neither): the uses are player 1's.
    mHandArrowPane = MR::getPane(this, "HandArrow");
    mPicNozzlePane = MR::getPane(this, "PicNozzle");

    initNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHideImmediately));

    mBlur = new StarPointerBlur(new GameTexture(MR::loadTexFromArc("StarPointerBlur", "Blur.bti")));

    mCommandStream = new StarPointerCommandStream(&mPosition);

    mBlur->setPortAndColor(mPadChannel, &::sColorA[mPadChannel], &::sColorB[mPadChannel]);
    mCommandStream->setPortAndColor(mPadChannel, &::sColorA[mPadChannel]);
    mCommandStream->initWithoutIter();

    mNumber = new StarPointerNumber(this, mPadChannel, "スターポインタナンバー");
    mNumber->initWithoutIter();
    mNumber->appear();

    changeLayout(StarPointerKind_StarPointer);
    hideAll();
    appear();
}

void StarPointerLayout::appear() {
    LayoutActor::appear();
    setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvWait));
}

void StarPointerLayout::kill() {
    LayoutActor::kill();
}

bool StarPointerLayout::startCommandStream(const LiveActor* pActor, const Vec3f* pPos, bool b) {
    if (mCommandStream->mWorldPos != nullptr) {
        return false;
    }

    mCommandStream->start(pPos, b);
    hideBlur();
    mActor = pActor;
    MR::startStarPointerModeCommandStream(this);
    return true;
}

void StarPointerLayout::endCommandStream(const LiveActor* /*pActor*/) {
    forceEndCommandStream();
    MR::endStarPointerMode(this);
}

void StarPointerLayout::forceEndCommandStream() {
    mCommandStream->hide();
    mCommandStream->clear();
    mBlur->show();
    mActor = nullptr;
}

bool StarPointerLayout::isCommandStream(const LiveActor* pActor) {
    return mCommandStream->mWorldPos != nullptr && mActor == pActor;
}

void StarPointerLayout::exeWait() {
    if (MR::isFirstStep(this)) {
        if (mPointerKind == StarPointerKind_StarPointer) {
            startAnimStarPointer("Wait");
        } else if (mPointerKind == StarPointerKind_HandPointer || mPointerKind == StarPointerKind_2 ||
                   mPointerKind == StarPointerKind_HandPointerReactionWithCrossCursor) {
            changeToHandPointer();
            startAnimHandPaa();
        } else if (mPointerKind == StarPointerKind_FingerPointer) {
            startAnimHandFinger();
        } else if (mPointerKind == StarPointerKind_5) {
            startAnimStarPointer("Wait");
        }

        if (mPointerKind == StarPointerKind_StarPointerNozzle) {
            ::setPaneVisible(mPicNozzlePane, true);
            MR::stopPaneAnim(this, "PicNozzle", 0);
            startAnimStarPointer("Wait");

        } else if (mPadChannel == PLAYER_1) {
            ::setPaneVisible(mPicNozzlePane, false);
        }
    }

    if (isTouch()) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOnReaction));
    } else {
        if (MR::isHeldJump(mPadChannel)) {
            setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvGrip));
        }
    }
}

void StarPointerLayout::exeGrip() {
    if (MR::isFirstStep(this)) {
        if (mPadChannel == PLAYER_2 && isAppear()) {
            MR::startSystemSE("SE_SY_DPD_B_TRIG");
        }

        if (mPointerKind == StarPointerKind_StarPointer) {
            if (mPadChannel == PLAYER_2) {
                startAnimStarPointer("Grip");
            } else {
                if (mCommandStream->mWorldPos != nullptr) {
                    mCommandStream->show();
                    startAnimHandGuu();
                } else {
                    startAnimStarPointer("Wait");
                }
            }
        } else if (mPointerKind == StarPointerKind_HandPointer) {
            startAnimHandGuu();
        } else if (mPointerKind == StarPointerKind_2 || mPointerKind == StarPointerKind_HandPointerReactionWithCrossCursor) {
            if (mAnimType != AnimType_Finger) {
                startAnimHandGuu();
            }
        } else if (mPointerKind == StarPointerKind_FingerPointer) {
            startAnimHandFinger();
        } else if (mPointerKind == StarPointerKind_5) {
            startAnimHandPaa();
        }

        if (mPointerKind == StarPointerKind_StarPointerNozzle) {
            ::setPaneVisible(mPicNozzlePane, true);
            MR::startPaneAnim(this, "PicNozzle", "Touch", 0);
            startAnimStarPointer("Touch");
        } else {
            if (mPadChannel == PLAYER_1) {
                ::setPaneVisible(mPicNozzlePane, false);
            }
        }
    }

    if (mPointerKind == StarPointerKind_2 && mAnimType == AnimType_Finger && !isTouch()) {
        startAnimHandGuu();
    }

    if (!MR::isHeldJump(mPadChannel)) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvRelease));
        return;
    }

    if (MR::isGreaterStep(this, ::hGripTime) || !::isPaneVisible(mStarPointerPane)) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHold));
    }
}

void StarPointerLayout::exeHold() {
    if (MR::isFirstStep(this)) {
        if (mPointerKind == StarPointerKind_StarPointer) {
            if (mPadChannel == PLAYER_2) {
                startAnimStarPointer("Hold");
                mRadius = ::sHoldRadius;
            } else {
                if (mCommandStream->mWorldPos != nullptr) {
                    mCommandStream->show();
                    startAnimHandGuu();
                } else {
                    startAnimStarPointer("Wait");
                }
            }
        } else if (mPointerKind == StarPointerKind_HandPointer) {
            startAnimHandGuu();
            mRadius = ::sHandRadius;
        } else if (mPointerKind == StarPointerKind_2 || mPointerKind == StarPointerKind_HandPointerReactionWithCrossCursor) {
            if (mAnimType != AnimType_Finger) {
                startAnimHandGuu();
            }
            mRadius = ::sHandRadius;
        } else if (mPointerKind == StarPointerKind_FingerPointer) {
            startAnimHandFinger();
        } else if (mPointerKind == StarPointerKind_5) {
            startAnimHandPaa();
        }

        if (mPointerKind == StarPointerKind_StarPointerNozzle) {
            ::setPaneVisible(mPicNozzlePane, true);
            if (MR::isPaneAnimStopped(this, "PicNozzle", 0)) {
                MR::startPaneAnim(this, "PicNozzle", "Touch", 0);
            }
            startAnimStarPointer("Touch");
        } else {
            if (mPadChannel == PLAYER_1) {
                ::setPaneVisible(mPicNozzlePane, false);
            }
        }
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHold))) {
        if (isTouch()) {
            setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHoldTouch));
            return;
        }

        if (mPointerKind == StarPointerKind_2 ||
            (mPointerKind == StarPointerKind_HandPointerReactionWithCrossCursor && mAnimType == AnimType_Finger)) {
            startAnimHandGuu();
        }
    } else {
        if (!isTouch()) {
            setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHold));
            return;
        }
    }

    if (!MR::isHeldJump(mPadChannel)) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvRelease));
    }
}

void StarPointerLayout::endHold() {
    tearDownHold();
}

void StarPointerLayout::tearDownHold() {
    if (mPointerKind == StarPointerKind_StarPointer && mPadChannel == PLAYER_2) {
        if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHold)) || isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHoldTouch))) {
            mRadius = ::sHoldRadius;
        } else {
            mRadius = ::sNormalRadius;
        }
    } else if (mPointerKind == StarPointerKind_HandPointer || mPointerKind == StarPointerKind_2) {
        mRadius = ::sHandRadius;
    }
}

void StarPointerLayout::exeRelease() {
    if (isTouch()) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOnReaction));
        return;
    }

    if (MR::isFirstStep(this)) {
        if (mPadChannel == PLAYER_2 && isAppear()) {
            MR::startSystemSE("SE_SY_DPD_B_RELEASE");
        }

        if (mPointerKind == StarPointerKind_StarPointer) {
            if (mPadChannel == PLAYER_2) {
                startAnimStarPointer("Release");
            } else {
                changeToStarPointer();
            }
        } else if (mPointerKind == StarPointerKind_HandPointer || mPointerKind == StarPointerKind_2 ||
                   mPointerKind == StarPointerKind_HandPointerReactionWithCrossCursor) {
            startAnimHandPaa();
        } else if (mPointerKind == StarPointerKind_FingerPointer) {
            startAnimHandFinger();
        } else if (mPointerKind == StarPointerKind_5) {
            startAnimStarPointer("Wait");
        }

        if (mPointerKind == StarPointerKind_StarPointerNozzle) {
            ::setPaneVisible(mPicNozzlePane, true);
            MR::stopPaneAnim(this, "PicNozzle", 0);
            startAnimStarPointer("Wait");
        } else if (mPadChannel == PLAYER_1) {
            ::setPaneVisible(mPicNozzlePane, false);
        }
    }

    if (MR::isHeldJump(mPadChannel)) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvGrip));
        return;
    }

    if (MR::isGreaterStep(this, ::hReleaseTime)) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvWait));
    }
}

void StarPointerLayout::exeOnReaction() {
    if (MR::isFirstStep(this)) {
        if (mPadChannel == PLAYER_1 && mPointerKind == StarPointerKind_StarPointer) {
            startAnimHandPaa();
        } else {
            if (mPointerKind == StarPointerKind_StarPointer) {
                startAnimStarPointer("Touch");
            } else if (mPointerKind == StarPointerKind_HandPointer) {
                startAnimHandPaa();
            } else if (mPointerKind == StarPointerKind_2 || mPointerKind == StarPointerKind_HandPointerReactionWithCrossCursor) {
                startAnimHandFinger();
            } else if (mPointerKind == StarPointerKind_FingerPointer) {
                startAnimHandFinger();
            } else if (mPointerKind == StarPointerKind_5) {
                startAnimStarPointer("Touch");
            }
        }

        if (mPointerKind == StarPointerKind_StarPointerNozzle) {
            ::setPaneVisible(mPicNozzlePane, true);
            MR::stopPaneAnim(this, "PicNozzle", 0);
            startAnimStarPointer("Wait");
        } else if (mPadChannel == PLAYER_1) {
            ::setPaneVisible(mPicNozzlePane, false);
        }
    }

    if (MR::isHeldJump(mPadChannel)) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvGrip));
        return;
    }

    if (!isTouch()) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvWait));
    }
}

void StarPointerLayout::endOnReaction() {
}

void StarPointerLayout::updateTouch() {
    mIsNewTouch = false;

    mSingleTouch = mStartSingleTouch;
    mStartSingleTouch = false;

    if (mStartTouch) {
        if (mTouchTimer == 0 || mNewTouchedID != mTouchedID) {
            mIsNewTouch = true;
            mTouchedID = mNewTouchedID;
        }
        mTouchTimer = ::sTouchCount;
        mStartTouch = false;
        return;
    }

    if (mTouchTimer > 0) {
        mTouchTimer--;
    }
}

bool StarPointerLayout::isTouch() {
    return mTouchTimer > 0;
}

void StarPointerLayout::exeOutScreen() {
    if (MR::isFirstStep(this)) {
        hideBlur();
        if (mPadChannel == PLAYER_1) {
            ::setPaneVisible(mPicNozzlePane, false);
        }
    }

    if (MR::isStarPointerInScreen(mPadChannel)) {
        if (MR::isHeldJump(mPadChannel)) {
            setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvGrip));
        } else {
            setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvWait));
        }
        return;
    }

    if (mCommandStream->mWorldPos != nullptr && MR::isHeldJump(mPadChannel)) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOutScreen));
        return;
    }

    if (MR::isGreaterStep(this, ::sBeginHideTime)) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHide));
    }
}

void StarPointerLayout::hideAll() {
    hideBlur();
    mCommandStream->hide();
    if (mPadChannel == PLAYER_1) {
        ::setPaneVisible(mPicNozzlePane, false);
        ::setPaneVisible(mHandArrowPane, false);
    }
    MR::hidePaneRecursive(mHandPointerPane);
    MR::hidePaneRecursive(mStarPointerPane);
}

void StarPointerLayout::exeHide() {
    if (MR::isFirstStep(this)) {
        if (mPadChannel == PLAYER_1) {
            ::setPaneVisible(mPicNozzlePane, false);
        }

        if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHideImmediately))) {
            MR::hidePaneRecursive(mHandPointerPane);
            MR::hidePaneRecursive(mStarPointerPane);
        } else if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvNotConnected)) || isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidate)) ||
                   isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidateWait))) {
            MR::hideLayout(this);
            MR::stopAnimFrame(this);
            MR::offCalcAnim(this);
        } else {
            if (mPointerKind == StarPointerKind_StarPointer || mPointerKind == StarPointerKind_5 ||
                mPointerKind == StarPointerKind_StarPointerNozzle) {
                startAnimStarPointer("End");
                MR::setAnimRate(this, ::hEndAnimRate, 0);
            } else if (mPointerKind == StarPointerKind_HandPointer || mPointerKind == StarPointerKind_2 ||
                       mPointerKind == StarPointerKind_HandPointerReactionWithCrossCursor || mPointerKind == StarPointerKind_FingerPointer) {
                ::setPaneVisible(mHandPointerPane, false);
            }
        }

        hideBlur();
        mCommandStream->hide();
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHide)) || isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHideImmediately))) {
        if (MR::isStarPointerInScreen(mPadChannel)) {
            setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvWait));
        }
    }
}

void StarPointerLayout::endHide() {
    tearDownHide();
}

void StarPointerLayout::tearDownHide() {
    if (mPointerKind == StarPointerKind_StarPointer || mPointerKind == StarPointerKind_5 || mPointerKind == StarPointerKind_StarPointerNozzle) {
        MR::setAnimRate(this, cNum_tearDownHide_setAnimRate_1, 0);
    } else if (mPointerKind == StarPointerKind_HandPointer || mPointerKind == StarPointerKind_2 || mPointerKind == StarPointerKind_FingerPointer) {
        ::setPaneVisible(mHandPointerPane, true);
    }
    MR::showLayout(this);
    MR::releaseAnimFrame(this);
    MR::onCalcAnim(this);
    calcAnim();
}

void StarPointerLayout::calcAnim() {
    LayoutActor::calcAnim();
    mCommandStream->calcAnim();
    mNumber->calcAnim();
}

void StarPointerLayout::draw() const {
    if (!isAppear() || NebulaMods::isHudHidden(NEBULA_HUD_POINTER)) {
        mBlur->hideAll();
        return;
    }

    if (mPadChannel == PLAYER_1 && MR::isStarPointer1PInvalid2PValidMode()) {
        mBlur->hideAll();
        return;
    }

    if (mPadChannel == PLAYER_1) {
        // A finger on a menu is its own pointer. Everything the pointer does still happens.
        const u32 touch = NebulaHost::option(NEBULA_OPTION_TOUCH_POINTER);
        if (touch == 3 || (touch == 2 && NebulaUi::isSharpScreenShown())) {
            mBlur->hideAll();
            return;
        }
    }

    MotionSignal::drawObject(mDrawSlot, mMotionEpoch);
    drawDecoration();
    LayoutActor::draw();
    mNumber->draw();
}

void StarPointerLayout::drawDecoration() const {
    if (mPointerKind == StarPointerKind_HandPointer || mPointerKind == StarPointerKind_2 || mPointerKind == StarPointerKind_FingerPointer ||
        (mPointerKind == StarPointerKind_5 && MR::isHeldJump(mPadChannel))) {
        mBlur->hideAll();
        return;
    }

    MR::loadProjectionMtxFor2D();
    mBlur->draw();
    mCommandStream->draw();
}

void StarPointerLayout::control() {
    mAppearTime++;
    mNumber->movement();
    MR::convertPaneLocalPosToScreenPos(&mPosition, MR::getRootPane(this), Vec2f(0.0f, 0.0f));

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvNotConnected))) {
        if (MR::isConnectedInput(mPadChannel)) {
            hideAll();
            setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHideImmediately));
        }
        return;
    }

    if (!MR::isConnectedInput(mPadChannel)) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvNotConnected));
        return;
    }

    if (mPadChannel == PLAYER_1) {
        if (mPointerKind == StarPointerKind_HandPointerReactionWithCrossCursor &&
            (getAnimType() == AnimType_HandGuu || getAnimType() == AnimType_HandPaa) && !isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHide)) &&
            !isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHideImmediately)) && !isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOutScreen))) {
            ::setPaneVisible(mHandArrowPane, true);
        } else {
            ::setPaneVisible(mHandArrowPane, false);
        }
    }

    updateDecoration();
    rotateTest();
    updateTouch();

    mShootDisabled = mStartDisableShoot;
    mStartDisableShoot = false;

    if (!isAppear()) {
        if (!isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidate))) {
            setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidate));
        }
        setPosition();
        return;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidate)) || isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidateWait))) {
        if (MR::isStarPointerInScreen(mPadChannel)) {
            setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvWait));
        } else {
            hideAll();
            setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHideImmediately));
        }
        setPosition();
        return;
    }

    if (!MR::isStarPointerInScreen(mPadChannel)) {
        StarPointerController* controller = StarPointerFunction::getStarPointerDirector()->getStarPointerController(mPadChannel);
        if (!isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOutScreen)) && !isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHide)) &&
            !isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHideImmediately))) {
            if (controller->isOutScreenLong()) {
                if (mPointerKind == StarPointerKind_StarPointer) {
                    if (!::isPaneVisible(mStarPointerPane)) {
                        startAnimHandGuu();
                    } else {
                        startAnimStarPointer("Blink");
                    }
                } else if (mPointerKind == StarPointerKind_HandPointer) {
                    startAnimHandPaa();
                }
                setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOutScreen));

                MR::tryRumblePadWeak(this, mPadChannel);
            }
            
            return;
        } else {
            return;
        }
    }


    if (mPadChannel == PLAYER_2) {
        MR::startAnim(this, "Transparency", 1);

        if (MR::isStarPointer2PTransparencyMode() && !isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHide))) {
            mBlur->setPortAndColor(mPadChannel, &::sColorB[mPadChannel], &::sColorB[mPadChannel]);
            MR::setAnimFrameAndStop(this, 1.0f, 1);
        } else {
            mBlur->setPortAndColor(mPadChannel, &::sColorA[mPadChannel], &::sColorB[mPadChannel]);
            MR::setAnimFrameAndStop(this, 0.0f, 1);
        }
    }

    setPosition();
}

void StarPointerLayout::setPosition() {
    StarPointerController* controller = StarPointerFunction::getStarPointerDirector()->getStarPointerController(mPadChannel);
    // This frame's position (StarPointerController::updatePointerInfo has run). The 3D targeting keeps the previous frame's, which its depth peek belongs to.
    Vec2f pos = controller->mInfo.mPos;
    setTrans(pos);
    mNumber->setTrans(pos);
}

void StarPointerLayout::rotateTest() {
    Vec2f horiz;
    MR::getPlayerInput(mPadChannel)->mPointer->getHorizonVec(&horiz);
    f32 angle = MR::normalizeAbs(MR::toDegree(MR::atan2(horiz.y, horiz.x)), ::sDegreeMin, cNum_rotateTest_f1);

    if (mPointerKind == StarPointerKind_StarPointer) {
        angle *= ::sRotateMult;
        if (mStarPointerPane != nullptr) {
            mStarPointerPane->mRotate.z = -angle;
        }
    } else if (mPointerKind == StarPointerKind_StarPointerNozzle && mPadChannel == PLAYER_1) {
        Vec2f targetScreenPos;
        MR::calcScreenPosition(&targetScreenPos, StarPointerFunction::getStarPointerDirector()->mNozzleAimPos);
        Vec2f pointerPos = MR::getStarPointerScreenPositionOrEdge(mPadChannel);
        calcPoseNozzle(targetScreenPos, pointerPos);
    }
}

void StarPointerLayout::calcPoseNozzle(const Vec2f& rTargetPos, const Vec2f& rPointerPos) {
    Vec2f diff = rPointerPos;
    diff.sub(rTargetPos);

    if (MR::isScreen16Per9()) {
        // stripped
    }

    if (mPicNozzlePane != nullptr && !MR::isNearZero(diff)) {
        f32 dist;
        MR::separateScalarAndDirection(&dist, &diff, diff);

        mPicNozzlePane->mRotate.z = MR::toDegree(MR::atan2(diff.x, diff.y));
    }
}

void StarPointerLayout::changeLayout(StarPointerKind pointerKind) {
    if (pointerKind == mPointerKind) {
        return;
    }

    switch (pointerKind) {
    case StarPointerKind_StarPointer:
        if (mCommandStream->mWorldPos == nullptr) {
            mAnimType = AnimType_NULL;
        } else {
            mCommandStream->show();
        }
        mRadius = ::sNormalRadius;
        break;
    case StarPointerKind_HandPointer:
        mCommandStream->hide();
        mRadius = ::sHandRadius;
        break;
    case StarPointerKind_2:
        mCommandStream->hide();
        mRadius = ::sHandRadius;
        break;
    case StarPointerKind_HandPointerReactionWithCrossCursor:
        mCommandStream->hide();
        mRadius = ::sHandRadius;
        break;
    case StarPointerKind_FingerPointer:
        mCommandStream->hide();
        mRadius = ::sHandRadius;
        break;
    case StarPointerKind_5:
        mCommandStream->hide();
        mRadius = ::sHandRadius;
        break;
    case StarPointerKind_StarPointerNozzle:
        mCommandStream->hide();
        mRadius = ::sNormalRadius;
        break;
    default:
        break;
    }

    mPointerKind = pointerKind;

    if (!isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHide)) && !isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHideImmediately))) {
        calcAnim();
    }

    resetSameNerve();
}

bool StarPointerLayout::isChanceToRumble() const {
    if (mIsNewTouch) {
        return true;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOnReaction)) && MR::isStep(this, cStep_isChanceToRumble_1)) {
        return true;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvGrip)) && MR::isStep(this, cStep_isChanceToRumble_2)) {
        return true;
    }

    return false;
}

void StarPointerLayout::hideBlur() {
    mBlur->hideAll();
}

void StarPointerLayout::resetSameNerve() {
    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvWait))) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvWait));
        return;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOnReaction))) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOnReaction));
        return;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvGrip))) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvGrip));
        return;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHold))) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHold));
        return;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHoldTouch))) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHoldTouch));
        return;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvRelease))) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvRelease));
        return;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOutScreen))) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOutScreen));
        return;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvNotConnected))) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvNotConnected));
        return;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidate))) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidate));
        return;
    }

    if (isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidateWait))) {
        setNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidateWait));
        return;
    }
}

void StarPointerLayout::changeToStarPointer() {
    if (mBlur->mBlurPoints[0].mIsHidden) {
        mBlur->show();
    }

    MR::hidePaneRecursive(mHandPointerPane);
    MR::showPaneRecursive(mStarPointerPane);
}

void StarPointerLayout::changeToHandPointer() {
    hideBlur();
    MR::hidePaneRecursive(mStarPointerPane);
    MR::showPaneRecursive(mHandPointerPane);

    if (mPadChannel == PLAYER_1) {
        MR::showPaneRecursive(this, "PicPlayer1");
        MR::hidePaneRecursive(this, "PicPlayer2");
    } else {
        MR::showPaneRecursive(this, "PicPlayer2");
        MR::hidePaneRecursive(this, "PicPlayer1");
    }
}

void StarPointerLayout::startAnimStarPointer(const char* pAnimName) {
    changeToStarPointer();
    MR::startPaneAnim(this, "StarPointer", pAnimName, 0);
    mAnimType = AnimType_StarPointer;
}

void StarPointerLayout::startAnimHandPaa() {
    changeToHandPointer();
    MR::startPaneAnim(this, "HandPointer", "HandType", 0);
    MR::setPaneAnimFrameAndStop(this, "HandPointer", cNum_startAnimHandPaa_f1, 0);
    mAnimType = AnimType_HandPaa;
}

void StarPointerLayout::startAnimHandGuu() {
    changeToHandPointer();
    MR::startPaneAnim(this, "HandPointer", "HandType", 0);
    MR::setPaneAnimFrameAndStop(this, "HandPointer", 1.0f, 0);
    mAnimType = AnimType_HandGuu;
}

void StarPointerLayout::startAnimHandFinger() {
    changeToHandPointer();
    MR::startPaneAnim(this, "HandPointer", "HandType", 0);
    MR::setPaneAnimFrameAndStop(this, "HandPointer", 0.0f, 0);
    mAnimType = AnimType_Finger;
}

void StarPointerLayout::updateDecoration() {
    if (mPointerKind == StarPointerKind_HandPointer || mPointerKind == StarPointerKind_2) {
        return;
    }

    if (mCommandStream->mWorldPos != nullptr) {
        mCommandStream->movement();
    } else {
        mBlur->movement();
    }
}

StarPointerNumber::StarPointerNumber(StarPointerLayout* pParent, s32 padChannel, const char* pName)
    : LayoutActor(pName, true), mParent(pParent), mPadChannel(padChannel), mAnimType(AnimType_NULL), mIsAppear(false), mAppearTime(0) {
    lumacore::setClass(this, lumacore::kClassId< StarPointerNumber >);
    initLayoutManager("DPDPointerNumber", 1);
    startAnimPosition(0);
}

void StarPointerNumber::control() {
    if (MR::isConnectedInput(PLAYER_1) && MR::isConnectedInput(PLAYER_2)) {
        mIsAppear = true;
        mAppearTime++;
    } else {
        mIsAppear = false;
        mAppearTime = 0;
    }

    if (mAppearTime == ::hHideRumbleTime) {
        MR::tryRumblePadWeak(this, mPadChannel);
    }

    if (mAppearTime == 40) {
        MR::startCSSound("CS_CONT_CONNECT", "SE_SY_CS_CONT_CONNECT", mPadChannel);
    }

    if (!mIsAppear || isOffState()) {
        startAnimPosition(0);
        mAnimType = AnimType_NULL;
        return;
    }

    switch (mParent->mAnimType) {
    case AnimType_Finger:
        startAnimPosition(2);
        break;
    case AnimType_HandGuu:
        startAnimPosition(3);
        break;
    case AnimType_HandPaa:
        startAnimPosition(3);
        break;
    case AnimType_StarPointer:
        startAnimPosition(1);
        break;
    default:
        break;
    }

    mAnimType = mParent->mAnimType;
}

bool StarPointerNumber::isOffState() const {
    return mParent->isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvOutScreen)) ||
           mParent->isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvNotConnected)) ||
           mParent->isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHide)) ||
           mParent->isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvHideImmediately)) ||
           mParent->isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidate)) ||
           mParent->isNerve(GET_NERVE(StarPointerLayout, HostTypeNrvInvalidateWait));
}

void StarPointerNumber::startAnimPosition(u32 animType) {
    MR::startAnimAndSetFrameAndStop(this, "Position", animType + (mPadChannel == PLAYER_1 ? 0 : 10), 0);
}
