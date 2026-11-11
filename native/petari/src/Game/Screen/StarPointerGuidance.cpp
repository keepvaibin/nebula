#include "Game/System/NebulaText.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Screen/StarPointerGuidance.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Screen/NebulaMenuFocus.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const s32 hOutScreenTime = NebulaFrameClock::stepsOfSeconds(1);
    static const s32 hRequestTime = NebulaFrameClock::stepsOfMilliseconds(500);
    static const s32 hNotRequestEndTime = NebulaFrameClock::stepsOfMilliseconds(500);
    static const s32 hEndTimeOutHide = NebulaFrameClock::stepsOfSeconds(4);
};  // namespace

namespace NrvStarPointerGuidance {
    NEW_NERVE(HostTypeNrvAppear1P, StarPointerGuidance, Appear1P);
    NEW_NERVE(HostTypeNrvWait1P, StarPointerGuidance, Wait1P);
    NEW_NERVE(HostTypeNrvEnd1P, StarPointerGuidance, End1P);
    NEW_NERVE(HostTypeNrvAppearBlueStarGuide, StarPointerGuidance, AppearBlueStarGuide);
    NEW_NERVE(HostTypeNrvWaitBlueStarGuide, StarPointerGuidance, WaitBlueStarGuide);
    NEW_NERVE(HostTypeNrvEndBlueStarGuide, StarPointerGuidance, EndBlueStarGuide);
    NEW_NERVE(HostTypeNrvAppear2P, StarPointerGuidance, Appear2P);
    NEW_NERVE(HostTypeNrvWait2P, StarPointerGuidance, Wait2P);
    NEW_NERVE(HostTypeNrvEnd2P, StarPointerGuidance, End2P);
    NEW_NERVE(HostTypeNrvEndWait, StarPointerGuidance, EndWait);
};  // namespace NrvStarPointerGuidance

StarPointerGuidance::StarPointerGuidance(const char* pName)
    : LayoutActor(pName, true), mSpineFrame1P(nullptr), mSpineGuidance(nullptr), mSpineFrame2P(nullptr), mOutScreenTime(0), mRequestTime1P(0),
      mRequestTime2P(0), mGuidanceMessage(nullptr), mPrevGuidanceMessage(nullptr), mIsActiveRequest1P(false), mIsActiveRequest2P(false),
      mIsActive(true), mIsGuidanceDisabled(false), mTextLineNumGuidance(1), mTextLineNum1P(1), mTextLineNum2P(1) {
    lumacore::setClass(this, lumacore::kClassId< StarPointerGuidance >);
}

void StarPointerGuidance::init(const TableIter& /*rIter*/) {
    initLayoutManager("PointerGuidance", 1);
    MR::createAndAddPaneCtrl(this, "NPointerGuide", 1);
    MR::createAndAddPaneCtrl(this, "PointerFrame1", 1);
    MR::createAndAddPaneCtrl(this, "PointerFrame2", 1);
    mGuidePane = MR::getPane(this, "NPointerGuide");
    mFrame1PPane = MR::getPane(this, "PointerFrame1");
    mFrame2PPane = MR::getPane(this, "PointerFrame2");

    mSpineFrame1P = new Spine(this, GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait));
    mSpineGuidance = new Spine(this, GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait));
    mSpineFrame2P = new Spine(this, GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait));

    appear();
    MR::hidePaneRecursive(mGuidePane);
    MR::hidePaneRecursive(mFrame1PPane);
    MR::hidePaneRecursive(mFrame2PPane);

    mTextLineNum1P = MR::getTextLineNumMaxRecursive(this, "PointerFrame1");
    mTextLineNum2P = MR::getTextLineNumMaxRecursive(this, "PointerFrame2");
#if !defined(__ANDROID__)
    MR::Text16 prompt;
    NebulaUi::getText(prompt, "PointerSelect");
    MR::setTextBoxMessageRecursive(this, "Guide1", prompt.c_str());
    MR::setTextBoxMessageRecursive(this, "Guide2", prompt.c_str());
#endif
}

void StarPointerGuidance::control() {
    mSpineFrame1P->update();
    mSpineGuidance->update();
    mSpineFrame2P->update();

    if (!mIsActive) {
        mIsActiveRequest1P = false;
        mIsActiveRequest2P = false;
    }

    checkRequest2P();
    checkRequest1P();
}

void StarPointerGuidance::checkRequest2P() {
#if defined(__ANDROID__)
    mIsActiveRequest2P = false;
    mRequestTime2P = 0;
    return;
#endif
    if (!mIsActiveRequest2P) {
        if (mSpineFrame2P->getCurrentNerve() == GET_NERVE(StarPointerGuidance, HostTypeNrvWait2P)) {
            mSpineFrame2P->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEnd2P));
        }

        mRequestTime2P = 0;
    } else {
        mRequestTime2P++;

        if (!MR::isStarPointerInScreen(PLAYER_2)) {
            if (mSpineFrame2P->getCurrentNerve() == GET_NERVE(StarPointerGuidance, HostTypeNrvEnd2P) ||
                mSpineFrame2P->getCurrentNerve() == GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait)) {
                mSpineFrame2P->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvAppear2P));

                if (mRequestTime2P == 1) {
                    MR::startCSSound2P("CS_CAN_PLAY_2P", nullptr);
                } else {
                    MR::startCSSound2P("CS_OUT_DISPLAY_2P", nullptr);
                }
            }
        } else if (mSpineFrame2P->getCurrentNerve() == GET_NERVE(StarPointerGuidance, HostTypeNrvWait2P)) {
            mSpineFrame2P->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEnd2P));
            MR::startCSSound2P("CS_IN_DISPLAY_2P", nullptr);
        }

        mIsActiveRequest2P = false;
    }
}

void StarPointerGuidance::checkRequest1P() {
    if (!mIsActiveRequest1P) {
        if (mRequestTime1P < 0) {
            mRequestTime1P--;
        } else {
            mRequestTime1P = -1;
        }

        mPrevGuidanceMessage = mGuidanceMessage;
        mGuidanceMessage = nullptr;
        if (mRequestTime1P > -::hNotRequestEndTime) {
            return;
        }

        if (mSpineFrame1P->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvAppear1P)) ||
            mSpineFrame1P->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvWait1P))) {
            mSpineFrame1P->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEnd1P));
        }

        if (mSpineGuidance->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvWaitBlueStarGuide))) {
            mSpineGuidance->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndBlueStarGuide));
        }

        return;
    }

    if (mRequestTime1P < 0) {
        mRequestTime1P = 0;
    }
    mRequestTime1P++;

    // A pad that holds the menu (Nebula's menu focus) needs no pointer: the frame asking for it goes.
#if defined(__ANDROID__)
    const bool isPointerWanted = false;  // touch selection has no off-screen pointing requirement
#else
    const bool isPointerWanted = !MR::isStarPointerInScreen(PLAYER_1) && !NebulaMenuFocus::isActive();
#endif
    if (isPointerWanted) {
        if (mOutScreenTime > ::hOutScreenTime || mRequestTime1P == 1) {
            if (mSpineGuidance->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvWaitBlueStarGuide))) {
                mSpineGuidance->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndBlueStarGuide));
            }

            if (mSpineFrame1P->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEnd1P)) ||
                mSpineFrame1P->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait))) {
                mSpineFrame1P->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvAppear1P));
                MR::startCSSound("CS_NOTICE_USE_DPD", "SE_SY_CS_NOTICE_USE_DPD", PLAYER_1);
            }
        } else {
            mOutScreenTime++;
        }

    } else {
        if (mGuidanceMessage != nullptr) {
            if (mGuidanceMessage != mPrevGuidanceMessage) {
                mRequestTime1P = 0;
            }

            if (mRequestTime1P > ::hRequestTime) {
                if (mSpineGuidance->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait))) {
                    mTextLineNumGuidance = MR::countMessageLine(mGuidanceMessage);
                    MR::setTextBoxMessageRecursive(this, "NPointerGuide", mGuidanceMessage);
                    mSpineGuidance->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvAppearBlueStarGuide));
                }
            }

        } else {
            if (mSpineGuidance->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvWaitBlueStarGuide))) {
                MR::clearTextBoxMessageRecursive(this, "NPointerGuide");
                mSpineGuidance->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndBlueStarGuide));
            }
        }

        if (mSpineFrame1P->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvAppear1P)) ||
            mSpineFrame1P->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvWait1P))) {
            mSpineFrame1P->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEnd1P));
        }

        mOutScreenTime = 0;
    }
    mIsActiveRequest1P = false;
    mPrevGuidanceMessage = mGuidanceMessage;
}

void StarPointerGuidance::exeAppear1P() {
    exeAppearCore(mSpineFrame1P, "PointerFrame1", mTextLineNum1P, GET_NERVE(StarPointerGuidance, HostTypeNrvWait1P));
}

void StarPointerGuidance::exeWait1P() {
    exeWaitCore(mSpineFrame1P, "PointerFrame1", mTextLineNum1P);
}

void StarPointerGuidance::exeEnd1P() {
    exeEndCore(mSpineFrame1P, "PointerFrame1", mTextLineNum1P);
}

void StarPointerGuidance::exeAppearBlueStarGuide() {
    exeAppearCore(mSpineGuidance, "NPointerGuide", mTextLineNumGuidance, GET_NERVE(StarPointerGuidance, HostTypeNrvWaitBlueStarGuide));
}

void StarPointerGuidance::exeWaitBlueStarGuide() {
    exeWaitCore(mSpineGuidance, "NPointerGuide", mTextLineNumGuidance);
}

void StarPointerGuidance::exeEndBlueStarGuide() {
    exeEndCore(mSpineGuidance, "NPointerGuide", mTextLineNumGuidance);
}

void StarPointerGuidance::exeAppear2P() {
    exeAppearCore(mSpineFrame2P, "PointerFrame2", mTextLineNum2P, GET_NERVE(StarPointerGuidance, HostTypeNrvWait2P));
}

void StarPointerGuidance::exeWait2P() {
    exeWaitCore(mSpineFrame2P, "PointerFrame2", mTextLineNum2P);
}

void StarPointerGuidance::exeEnd2P() {
    exeEndCore(mSpineFrame2P, "PointerFrame2", mTextLineNum2P);
}

void StarPointerGuidance::exeEndWait() {
}

s32 StarPointerGuidance::exeAppearCore(Spine* pSpine, const char* pPaneName, u32 numLines, const Nerve* pNerve) {
    if (pSpine->mStep == 0) {
        MR::showPaneRecursive(this, pPaneName);

        if (numLines >= 2) {
            MR::startPaneAnim(this, pPaneName, "AppearTwoLine", 0);
        } else {
            MR::startPaneAnim(this, pPaneName, "AppearOneLine", 0);
        }

        return 1;
    }

    if (MR::isPaneAnimStopped(this, pPaneName, 0)) {
        pSpine->setNerve(pNerve);

        return 2;
    }

    return 0;
}

s32 StarPointerGuidance::exeWaitCore(Spine* pSpine, const char* pPaneName, u32 numLines) {
    if (pSpine->mStep == 0) {
        mIsGuidanceDisabled = false;

        MR::startPaneAnim(this, pPaneName, "Wait", 0);

        return 1;
    }

    if (mIsTimeOutEnabled && pSpine == mSpineGuidance && mSpineGuidance->mStep == ::hEndTimeOutHide) {
        mIsGuidanceDisabled = true;

        if (numLines >= 2) {
            MR::startPaneAnim(this, pPaneName, "EndTwoLine", 0);
        } else {
            MR::startPaneAnim(this, pPaneName, "EndOneLine", 0);
        }
    }

    return 0;
}

s32 StarPointerGuidance::exeEndCore(Spine* pSpine, const char* pPaneName, u32 numLines) {
    if (pSpine->mStep == 0) {
        if (pSpine == mSpineGuidance && mIsGuidanceDisabled) {
            return 1;
        }

        if (numLines >= 2) {
            MR::startPaneAnim(this, pPaneName, "EndTwoLine", 0);
        } else {
            MR::startPaneAnim(this, pPaneName, "EndOneLine", 0);
        }

        return 1;
    }

    if (MR::isPaneAnimStopped(this, pPaneName, 0)) {
        MR::hidePaneRecursive(this, pPaneName);
        pSpine->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait));

        return 2;
    }

    return 0;
}

bool StarPointerGuidance::request1PGuidance(const char* pMessageId, bool enableTimeOut) {
    if (!MR::isConnectedInput(PLAYER_1) || (!MR::isStarPointerValid(PLAYER_1) && !MR::isStarPointerModeMarioLauncher())) {
        return false;
    }

    mGuidanceMessage = pMessageId != nullptr ? MR::getLayoutMessageDirect(pMessageId) : nullptr;
    mIsActiveRequest1P = true;
    mIsTimeOutEnabled = enableTimeOut;

    return true;
}

void StarPointerGuidance::tryResetTimeout() {
    if (!mIsTimeOutEnabled) {
        return;
    }

    if (!mIsGuidanceDisabled) {
        return;
    }

    mSpineGuidance->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvWaitBlueStarGuide));
}

bool StarPointerGuidance::isExistGuidanceOrFrame() {
    return isExistGuidance() || isExistFrame1P() || isExistFrame2P();
}

bool StarPointerGuidance::isExistGuidance() {
    if (mSpineGuidance->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait))) {
        return false;
    }

    return !mIsGuidanceDisabled;
}

bool StarPointerGuidance::isExistFrame1P() {
    return !mSpineFrame1P->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait));
}

bool StarPointerGuidance::isExistFrame2P() {
    return !mSpineFrame2P->isCurrentNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait));
}

void StarPointerGuidance::deactive() {
    mIsActive = false;

    MR::hidePaneRecursive(mFrame1PPane);
    MR::hidePaneRecursive(mGuidePane);
    MR::hidePaneRecursive(mFrame2PPane);
    mSpineFrame1P->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait));
    mSpineGuidance->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait));
    mSpineFrame2P->setNerve(GET_NERVE(StarPointerGuidance, HostTypeNrvEndWait));

    mPrevGuidanceMessage = nullptr;
    mGuidanceMessage = nullptr;
    mIsActiveRequest1P = false;
    mIsActiveRequest2P = false;
}
