#include "Game/NPC/TalkState.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/TalkBalloon.hpp"
#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/NPC/TalkSupportPlayerWatcher.hpp"
#include "Game/Screen/IconAButton.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

TalkState::TalkState() : mMsgCtrl(), mBalloon() {
}

void TalkState::init(TalkMessageCtrl* pArg1, TalkBalloon* pBalloon) {
    mMsgCtrl = pArg1;
    mBalloon = pBalloon;
    mMessageID = mMsgCtrl->getMessageID();
}

u32 TalkState::getPageCount() const {
    return 0;
}

void TalkState::balloonOff() const {
    MR::hideScreen(mBalloon);
}

bool TalkState::isLostMessage(const TalkMessageCtrl* pArg1) const {
    if (pArg1 == nullptr) {
        return true;
    }

    if (pArg1 != mMsgCtrl) {
        return true;
    }

    return mMsgCtrl->getMessageID() != mMessageID;
};

bool TalkState::isSelfInterrupt(const TalkMessageCtrl* pArg1) const {
    if (pArg1 == nullptr) {
        return false;
    }

    if (pArg1 != mMsgCtrl) {
        return false;
    }

    return mMsgCtrl->getMessageID() != mMessageID;
}

TalkStateShort::TalkStateShort() : TalkState() {
}

bool TalkStateShort::prep(const TalkMessageCtrl* pArg1) {
    return !TalkState::isLostMessage(pArg1);
}

bool TalkStateShort::talk(const TalkMessageCtrl*) {
    return false;
}

void TalkStateShort::open() {
    mBalloon->open(mMsgCtrl);
    LiveActor* actor = mMsgCtrl->mHostActor;
    TalkMessageInfo* info = TalkFunction::getMessageInfo(mMsgCtrl);
    MR::startTalkSound(info->mSoundId, actor);
}

void TalkStateShort::clos() {
    mBalloon->close();
}

TalkStateEvent::TalkStateEvent() {
    mPlayerWatcher = nullptr;
    mTermStep = 0;
    mSkipHoldStep = 0;
}

bool TalkStateEvent::prep(const TalkMessageCtrl* pArg1) {
    return !TalkState::isLostMessage(pArg1);
}

bool TalkStateEvent::test() {
    return mPlayerWatcher->isEnableTalkPlayerStateEvent();
}

void TalkStateEvent::open() {
    TalkMessageInfo* info = TalkFunction::getMessageInfo(mMsgCtrl);
    MR::startTalkSound(info->mSoundId, mMsgCtrl->mHostActor);

    mIsJumpHeld = 1;
    mPageCount = 0;
    mSkipHoldStep = 0;

    if (!MR::isTimeKeepDemoActive() || !info->isCameraNormal()) {
        mMsgCtrl->startCamera(-1);
    }

    mBalloon->open(mMsgCtrl);
}

void TalkStateEvent::clos() {
    mBalloon->close();
}

bool TalkStateEvent::talk(const TalkMessageCtrl* pArg1) {
    // A skipped cutscene (NebulaCutsceneSkip): each page, shown whole, turns as soon as it is up; a question ends
    // the skip with the question on screen.
    if (NebulaCutsceneSkip::isSkipping()) {
        if (!mBalloon->isTextAppearedAll()) {
            return false;
        }
        if (TalkFunction::isSelectTalk(mMsgCtrl) && !mBalloon->hasNextPage()) {
            NebulaCutsceneSkip::endForQuestion();
            return true;
        }
        mPageCount++;
        return !mBalloon->turnPage();
    }

    if (MR::isHeldJump(0)) {
        if (mSkipHoldStep++ > 10) {
            for (int i = 0; i < 2; i++) {
                mBalloon->skipMessage();
            }
        }
    } else {
        mSkipHoldStep = 0;
    }

    if (!mBalloon->isTextAppearedAll()) {
        return false;
    }

    bool unknownBool = false;

    if (mIsJumpHeld != 0) {
        mIsJumpHeld = MR::isHeldJump(0);
    } else if (MR::isHeldJump(0) && !MR::isPressedJump(0)) {
        mIsJumpHeld = true;
        unknownBool = true;
    }

    if (TalkFunction::isSelectTalk(mMsgCtrl) && !mBalloon->hasNextPage()) {
        NebulaCutsceneSkip::endForQuestion();
        return true;
    }

    if (unknownBool) {
        mPageCount++;
        return !mBalloon->turnPage();
    }

    return false;
}

bool TalkStateEvent::term(const TalkMessageCtrl* pArg1) {
    if (mTermStep++ > 30) {
        mTermStep = 0;
        return true;
    } else {
        return false;
    }
}

u32 TalkStateEvent::getPageCount() const {
    return mPageCount;
}

TalkStateNormal::TalkStateNormal() : TalkStateEvent() {
}

bool TalkStateNormal::test() {
    if (!mPlayerWatcher->isEnableTalkPlayerStateNormal() || !MR::isPressedJump(0)) {
        return false;
    }

    return TalkStateEvent::test();
}

void TalkStateNormal::clos() {
    mAButton->term();
    mBalloon->close();
}

bool TalkStateNormal::term(const TalkMessageCtrl* pArg1) {
    if (pArg1 != nullptr && pArg1 != mMsgCtrl) {
        return true;
    }

    if (pArg1 != nullptr && TalkFunction::isShortTalk(pArg1)) {
        return true;
    }

    return TalkStateEvent::term(pArg1);
}

bool TalkStateNormal::prep(const TalkMessageCtrl* pArg1) {
    if (TalkStateNormal::isLostMessage(pArg1)) {
        mAButton->term();
        return false;
    }

    TalkStateNormal::updateButton();

    if (!mPlayerWatcher->isEnableTalkPlayerStateNormal()) {
        mAButton->term();
    } else if (!MR::isPressedJump(0)) {
        TalkMessageInfo* info = TalkFunction::getMessageInfo(mMsgCtrl);
        if (!mAButton->isOpen()) {
            if (info->isBalloonSign()) {
                mAButton->openWithRead();
            } else {
                mAButton->openWithTalk();
            }

            MR::startSystemSE("SE_SM_TALK_BUTTON_APPEAR");
        }
    } else {
        mAButton->term();
        MR::startSystemSE("SE_SY_TALK_START");
        MR::startCSSound("CS_CLICK_OPEN", nullptr, 0);
    }

    return true;
}

void TalkStateNormal::updateButton() {
    Vec3f camZ(MR::getCamZdir());
    Vec3f camY(MR::getCamYdir());
    Vec3f up;
    Vec3f centerPlayer(*MR::getPlayerCenterPos());
    MR::getPlayerUpVec(&up);

    f64 absolute = fabs(camZ.dot(up));  // TODO: look at fabs again
    f32 f1 = absolute;
    f32 f2 = camY.dot(up);
    f1 = f1 * f1;

    MR::calcNormalizedScreenPosition(&up, up * 1000.0f + centerPlayer);
    MR::calcNormalizedScreenPosition(&centerPlayer, centerPlayer);
    MR::normalize(up - centerPlayer, &up);
    Vec2f playerScreenPos;
    MR::calcScreenPosition(&playerScreenPos, *MR::getPlayerCenterPos());

    playerScreenPos.add(Vec2f(0.0f, -1.0f) * (40.0f * f1));
    playerScreenPos.add(Vec2f(up.x, up.y) * (60.0f * f2));

    mAButton->setTrans(playerScreenPos);
}

TalkStateCompose::TalkStateCompose() : TalkStateNormal() {
}

void TalkStateCompose::init(TalkMessageCtrl* pArg1, TalkBalloon* pArg2) {
    TalkState::init(pArg1, pArg2);
    mSecondBalloon->open(pArg1);
}

bool TalkStateCompose::test() {
    if (mMsgCtrl->isNearPlayer(mMsgCtrl->mTalkDistance)) {
        return TalkStateNormal::test();
    }

    return false;
}

void TalkStateCompose::open() {
    TalkStateEvent::open();
    mSecondBalloon->close();
}

bool TalkStateCompose::prep(const TalkMessageCtrl* pArg1) {
    bool unknownBool = true;

    if (TalkState::isLostMessage(pArg1)) {
        unknownBool = false;
    } else if (mMsgCtrl->isNearPlayer(mMsgCtrl->mTalkDistance)) {
        unknownBool = TalkStateNormal::prep(pArg1);
    } else {
        mAButton->term();
    }

    if (!unknownBool) {
        mAButton->term();
        mSecondBalloon->close();
    }

    return unknownBool;
}

TalkStateHolder::TalkStateHolder() {
    mPlayerWatcher = new TalkSupportPlayerWatcher();

    mAButton = new IconAButton(true, false);
    mAButton->initWithoutIter();

    mBalloonShort = new TalkBalloonShort("会話吹き出し[合成会話]");
    mBalloonShort->initWithoutIter();
    mBalloonShort->initInterval();
    mBalloonShort->kill();

    mTalkShort = new TalkStateShort();

    mTalkNormal = new TalkStateNormal();
    mTalkNormal->mAButton = mAButton;
    mTalkNormal->mPlayerWatcher = mPlayerWatcher;

    mTalkCompose = new TalkStateCompose();
    mTalkCompose->mAButton = mAButton;
    mTalkCompose->mPlayerWatcher = mPlayerWatcher;
    mTalkCompose->mSecondBalloon = mBalloonShort;

    mTalkEvent = new TalkStateEvent();
    mTalkEvent->mPlayerWatcher = mPlayerWatcher;

    mTalk = new TalkState();
}

void TalkStateHolder::update() {
    mPlayerWatcher->update();
}

void TalkStateHolder::pauseOff() {
    MR::requestMovementOn(mAButton);
}

TalkState* TalkStateHolder::getState(const TalkMessageCtrl* pArg1) {
    TalkMessageInfo* info = TalkFunction::getMessageInfo(pArg1);

    if (info->isNormalTalk()) {
        return mTalkNormal;
    }

    if (info->isShortTalk()) {
        return mTalkShort;
    }

    if (info->isEventTalk()) {
        return mTalkEvent;
    }

    if (info->isComposeTalk()) {
        return mTalkCompose;
    }

    return mTalk;
}
