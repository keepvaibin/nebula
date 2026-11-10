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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateButton_f1, "updateButton.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateButton_f2, "updateButton.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateButton_f3, "updateButton.f3", 60.0f);

TalkState::TalkState() : mMsgCtrl(), mBalloon() {
}

void TalkState::init(TalkMessageCtrl* pMsgCtrl, TalkBalloon* pBalloon) {
    mMsgCtrl = pMsgCtrl;
    mBalloon = pBalloon;
    mMessageID = mMsgCtrl->getMessageID();
}

u32 TalkState::getPageCount() const {
    return 0;
}

void TalkState::balloonOff() const {
    MR::hideScreen(mBalloon);
}

bool TalkState::isLostMessage(const TalkMessageCtrl* pMsgCtrl) const {
    if (pMsgCtrl == nullptr) {
        return true;
    }

    if (pMsgCtrl != mMsgCtrl) {
        return true;
    }

    return mMsgCtrl->getMessageID() != mMessageID;
}

bool TalkState::isSelfInterrupt(const TalkMessageCtrl* pMsgCtrl) const {
    if (pMsgCtrl == nullptr) {
        return false;
    }

    if (pMsgCtrl != mMsgCtrl) {
        return false;
    }

    return mMsgCtrl->getMessageID() != mMessageID;
}

TalkStateShort::TalkStateShort() : TalkState() {
}

bool TalkStateShort::prep(const TalkMessageCtrl* pMsgCtrl) {
    return !TalkState::isLostMessage(pMsgCtrl);
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

bool TalkStateEvent::prep(const TalkMessageCtrl* pMsgCtrl) {
    return !TalkState::isLostMessage(pMsgCtrl);
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

bool TalkStateEvent::talk(const TalkMessageCtrl* /*pMsgCtrl*/) {
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

    // A page turns once per hold: the button must be let go after the text is up, then held again.
    bool isTurnPage = false;

    if (mIsJumpHeld != 0) {
        mIsJumpHeld = MR::isHeldJump(0);
    } else if (MR::isHeldJump(0) && !MR::isPressedJump(0)) {
        mIsJumpHeld = true;
        isTurnPage = true;
    }

    if (TalkFunction::isSelectTalk(mMsgCtrl) && !mBalloon->hasNextPage()) {
        NebulaCutsceneSkip::endForQuestion();
        return true;
    }

    if (isTurnPage) {
        mPageCount++;
        return !mBalloon->turnPage();
    }

    return false;
}

bool TalkStateEvent::term(const TalkMessageCtrl* /*pMsgCtrl*/) {
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

bool TalkStateNormal::term(const TalkMessageCtrl* pMsgCtrl) {
    if (pMsgCtrl != nullptr && pMsgCtrl != mMsgCtrl) {
        return true;
    }

    if (pMsgCtrl != nullptr && TalkFunction::isShortTalk(pMsgCtrl)) {
        return true;
    }

    return TalkStateEvent::term(pMsgCtrl);
}

bool TalkStateNormal::prep(const TalkMessageCtrl* pMsgCtrl) {
    if (TalkStateNormal::isLostMessage(pMsgCtrl)) {
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

    // The button sits over the player's head on screen: 40 straight up as far as up faces the camera, 60 along
    // up's screen direction as far as up is the camera's up.
    f32 facing = fabs(camZ.dot(up));
    f32 upright = camY.dot(up);
    facing = facing * facing;

    MR::calcNormalizedScreenPosition(&up, up * cNum_updateButton_f1 + centerPlayer);
    MR::calcNormalizedScreenPosition(&centerPlayer, centerPlayer);
    MR::normalize(up - centerPlayer, &up);
    Vec2f playerScreenPos;
    MR::calcScreenPosition(&playerScreenPos, *MR::getPlayerCenterPos());

    playerScreenPos.add(Vec2f(0.0f, -1.0f) * (cNum_updateButton_f2 * facing));
    playerScreenPos.add(Vec2f(up.x, up.y) * (cNum_updateButton_f3 * upright));

    mAButton->setTrans(playerScreenPos);
}

TalkStateCompose::TalkStateCompose() : TalkStateNormal() {
}

void TalkStateCompose::init(TalkMessageCtrl* pMsgCtrl, TalkBalloon* pBalloon) {
    TalkState::init(pMsgCtrl, pBalloon);
    mSecondBalloon->open(pMsgCtrl);
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

bool TalkStateCompose::prep(const TalkMessageCtrl* pMsgCtrl) {
    bool isKeep = true;

    if (TalkState::isLostMessage(pMsgCtrl)) {
        isKeep = false;
    } else if (mMsgCtrl->isNearPlayer(mMsgCtrl->mTalkDistance)) {
        isKeep = TalkStateNormal::prep(pMsgCtrl);
    } else {
        mAButton->term();
    }

    if (!isKeep) {
        mAButton->term();
        mSecondBalloon->close();
    }

    return isKeep;
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

TalkState* TalkStateHolder::getState(const TalkMessageCtrl* pMsgCtrl) {
    TalkMessageInfo* info = TalkFunction::getMessageInfo(pMsgCtrl);

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
