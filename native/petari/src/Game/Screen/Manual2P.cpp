#include "Game/Screen/Manual2P.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/BackButton.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/Clip.hpp"
#include <cstdio>
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEW_NERVE(Manual2PNrvAppear, Manual2P, Appear);
    NEW_NERVE(Manual2PNrvWait, Manual2P, Wait);
    NEW_NERVE(Manual2PNrvScrollRight, Manual2P, ScrollRight);
    NEW_NERVE(Manual2PNrvScrollRightAfter, Manual2P, ScrollRightAfter);
    NEW_NERVE(Manual2PNrvScrollLeft, Manual2P, ScrollLeft);
    NEW_NERVE(Manual2PNrvScrollLeftAfter, Manual2P, ScrollLeftAfter);
    NEW_NERVE(Manual2PNrvDisappear, Manual2P, Disappear);
};  // namespace

namespace {
    s32 countMessage(const char* pName) {
        s32 count = 0;
        MR::FormattedName messageID;

        do {
            messageID.format("%s%03d", pName, count + 1);
            count++;
        } while (MR::isExistGameMessage(messageID.c_str()));

        return count;
    }
};  // namespace

Manual2P::Manual2P(const char* pName)
    : LayoutActor(pName, true), mPageIndex(), mPageNum(), mLeftPaneCtrl(), mRightPaneCtrl(), mCanScrollLeft(true), mCanScrollRight(true), mBackButton() {
    lumacore::setClass(this, lumacore::kClassId< Manual2P >);
}

void Manual2P::init(const TableIter& /*rIter*/) {
    initLayoutManager("P2Manual", 2);
    MR::invalidateParentAnim(this);
    MR::createAndAddPaneCtrl(this, "LeftButton", 2);

    mLeftPaneCtrl = new ButtonPaneController(this, "LeftButton", "PicLButton", 0, true);
    mLeftPaneCtrl->mDisappearsAfterDecide = 0;

    MR::createAndAddPaneCtrl(this, "RightButton", 2);

    mRightPaneCtrl = new ButtonPaneController(this, "RightButton", "PicRButton", 0, true);
    mRightPaneCtrl->mDisappearsAfterDecide = 0;

    mBackButton = new BackButton("マニュアルの戻るボタン", true);
    mBackButton->initWithoutIter();
    // The D-pad and stick turn the pages here (NebulaMenuFocus leaves these buttons to the pointer).
    mLeftPaneCtrl->mIsNebulaFocusable = false;
    mRightPaneCtrl->mIsNebulaFocusable = false;
    mBackButton->mPaneCtrl->mIsNebulaFocusable = false;

    initNerve(GET_NERVE_ANON(Manual2PNrvAppear));
    MR::connectToSceneLayout(this);

    mPageNum = ::countMessage("2PGuidance");
}

void Manual2P::appear() {
    LayoutActor::appear();
    setNerve(GET_NERVE_ANON(Manual2PNrvAppear));

    mPageIndex = 0;

    MR::startAnim(this, "Picture", 1);

    if (mPageNum > MR::getAnimCtrl(this, 1)->getEnd()) {
        mPageNum = MR::getAnimCtrl(this, 1)->getEnd();
    }
}

bool Manual2P::isClosed() const {
    return MR::isDead(this);
}

void Manual2P::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Appear", 0);
        mLeftPaneCtrl->appear();
        mRightPaneCtrl->appear();
        mBackButton->appear();
        reflectPageIndex();
    }

    if (MR::isAnimStopped(this, 0) && mLeftPaneCtrl->isWait() && mRightPaneCtrl->isWait()) {
        setNerve(GET_NERVE_ANON(Manual2PNrvWait));
    }
}

void Manual2P::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Wait", 0);
    }

    if (mCanScrollLeft) {
        mLeftPaneCtrl->trySelect();

        if (mLeftPaneCtrl->mIsSelected) {
            MR::startSystemSE("SE_SY_FILE_SEL_TIPS_PAGE");
            setNerve(GET_NERVE_ANON(Manual2PNrvScrollLeft));
            return;
        }

        if (MR::isMoveStickPressedLeft(PLAYER_1) || MR::isPressedLeft(PLAYER_1)) {
            mLeftPaneCtrl->mIsActive = false;

            MR::startSystemSE("SE_SY_FILE_SEL_TIPS_PAGE");
            setNerve(GET_NERVE_ANON(Manual2PNrvScrollLeft));
            return;
        }
    }

    if (mCanScrollRight) {
        mRightPaneCtrl->trySelect();

        if (mRightPaneCtrl->mIsSelected) {
            MR::startSystemSE("SE_SY_FILE_SEL_TIPS_PAGE");
            setNerve(GET_NERVE_ANON(Manual2PNrvScrollRight));
            return;
        }

        if (MR::isMoveStickPressedRight(PLAYER_1) || MR::isPressedRight(PLAYER_1)) {
            mRightPaneCtrl->mIsActive = false;

            MR::startSystemSE("SE_SY_FILE_SEL_TIPS_PAGE");
            setNerve(GET_NERVE_ANON(Manual2PNrvScrollRight));
            return;
        }
    }

    if (checkSelectedBackButton()) {
        setNerve(GET_NERVE_ANON(Manual2PNrvDisappear));
    }
}

void Manual2P::exeScrollRight() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "PageOut", 0);
    }

    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE_ANON(Manual2PNrvScrollRightAfter));
    }
}

void Manual2P::exeScrollRightAfter() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "PageIn", 0);

        mPageIndex++;

        reflectPageIndex();
    }

    if (MR::isAnimStopped(this, 0)) {
        mRightPaneCtrl->mIsActive = true;
        mRightPaneCtrl->forceToWait();
        setNerve(GET_NERVE_ANON(Manual2PNrvWait));
    }
}

void Manual2P::exeScrollLeft() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "PageIn", 0);
        MR::setAnimFrame(this, MR::getAnimCtrl(this, 0)->getEnd() - 1.0f, 0);
        MR::getAnimCtrl(this, 0)->setRate(-1.0f);
    }

    if (MR::getAnimCtrl(this, 0)->getFrame() + MR::getAnimCtrl(this, 0)->getRate() <= 0.0f) {
        setNerve(GET_NERVE_ANON(Manual2PNrvScrollLeftAfter));
    }
}

void Manual2P::exeScrollLeftAfter() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "PageOut", 0);
        MR::setAnimFrame(this, MR::getAnimCtrl(this, 0)->getEnd() - 1.0f, 0);
        MR::getAnimCtrl(this, 0)->setRate(-1.0f);

        mPageIndex--;

        reflectPageIndex();
    }

    if (MR::getAnimCtrl(this, 0)->getFrame() - MR::getAnimCtrl(this, 0)->getRate() <= 0.0f) {
        mLeftPaneCtrl->mIsActive = true;
        mLeftPaneCtrl->forceToWait();
        setNerve(GET_NERVE_ANON(Manual2PNrvWait));
    }
}

void Manual2P::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "End", 0);
        reflectPageIndex();
    }

    if (MR::isAnimStopped(this, 0) && mBackButton->isHidden()) {
        kill();
    }
}

void Manual2P::control() {
    mLeftPaneCtrl->update();
    mRightPaneCtrl->update();

    if (mLeftPaneCtrl->isPointingTrigger()) {
        MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
    }

    if (mRightPaneCtrl->isPointingTrigger()) {
        MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
    }
}

void Manual2P::reflectPageIndex() {
    MR::startAnim(this, "Picture", 1);

    if (mPageIndex < 0) {
        mPageIndex = 0;
    }

    if (mPageIndex >= mPageNum) {
        mPageIndex = mPageNum - 1;
    }

    MR::setAnimFrameAndStop(this, mPageIndex, 1);

    MR::FormattedName messageID("2PGuidance%03d", mPageIndex + 1);

    if (MR::isExistGameMessage(messageID.c_str())) {
        MR::setTextBoxGameMessageRecursive(this, "Text", messageID.c_str());
    }

    messageID.format("2PGuidanceTitle%03d", mPageIndex + 1);

    if (MR::isExistGameMessage(messageID.c_str())) {
        MR::setTextBoxGameMessageRecursive(this, "Title", messageID.c_str());
    }

    MR::setTextBoxFormatRecursive(this, "PageNumber", u"%d/%d", mPageIndex + 1, mPageNum);

    if (mPageIndex == 0) {
        ButtonPaneController* pPaneCtrl = mLeftPaneCtrl;
        MR::startPaneAnim(this, pPaneCtrl->mPaneName, "ButtonOff", 1);
        MR::setPaneAnimFrameAndStop(this, pPaneCtrl->mPaneName, 1.0f, 1);
        mCanScrollLeft = false;
    } else {
        ButtonPaneController* pPaneCtrl = mLeftPaneCtrl;
        MR::startPaneAnim(this, pPaneCtrl->mPaneName, "ButtonOff", 1);
        MR::setPaneAnimFrameAndStop(this, pPaneCtrl->mPaneName, 0.0f, 1);
        mCanScrollLeft = true;
    }

    if (mPageIndex == (mPageNum - 1)) {
        ButtonPaneController* pPaneCtrl = mRightPaneCtrl;
        MR::startPaneAnim(this, pPaneCtrl->mPaneName, "ButtonOff", 1);
        MR::setPaneAnimFrameAndStop(this, pPaneCtrl->mPaneName, 1.0f, 1);
        mCanScrollRight = false;
    } else {
        ButtonPaneController* pPaneCtrl = mRightPaneCtrl;
        MR::startPaneAnim(this, pPaneCtrl->mPaneName, "ButtonOff", 1);
        MR::setPaneAnimFrameAndStop(this, pPaneCtrl->mPaneName, 0.0f, 1);
        mCanScrollRight = true;
    }
}

bool Manual2P::checkSelectedBackButton() {
    if (mBackButton->isSelected()) {
        return true;
    }

    if (MR::testSystemTriggerB()) {
        MR::startSystemSE("SE_SY_GALAXY_DECIDE_CANCEL");
        mBackButton->disappear();

        return true;
    }

    return false;
}
