#include "Game/NPC/TalkBalloon.hpp"
#include <nebula_game_api.h>
#include "Game/AudioLib/AudSystemVolumeController.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/NPC/TalkState.hpp"
#include "Game/NPC/TalkTextFormer.hpp"
#include "Game/Screen/IconAButton.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>

#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Ui/Pane.hpp"

namespace NrvTalkBalloonShort {
    NEW_NERVE(TalkBalloonShortNrvOpen, TalkBalloonShort, Open);
    NEW_NERVE(TalkBalloonShortNrvTalk, TalkBalloonShort, Talk);
    NEW_NERVE(TalkBalloonShortNrvClose, TalkBalloonShort, Close);
};  // namespace NrvTalkBalloonShort

namespace NrvTalkBalloonEvent {
    NEW_NERVE(TalkBalloonEventNrvWait, TalkBalloonEvent, Wait);
    NEW_NERVE(TalkBalloonEventNrvOpen, TalkBalloonEvent, Open);
    NEW_NERVE(TalkBalloonEventNrvTalk, TalkBalloonEvent, Talk);
    NEW_NERVE(TalkBalloonEventNrvClose, TalkBalloonEvent, Close);
};  // namespace NrvTalkBalloonEvent

TalkBalloon::TalkBalloon(const char* pName) : LayoutActor(pName, true), mMessageCtrl(), mTextFormer(), mHasBeak(), mIsBeakMoving() {
}

void TalkBalloon::create(const char* pLayoutName, bool hasBeak, bool isTalkLayout) {
    mHasBeak = hasBeak;

    if (isTalkLayout) {
        MR::connectToSceneTalkLayout(this);
    } else {
        MR::connectToSceneLayout(this);
    }

    if (pLayoutName != nullptr) {
        initLayoutManager(pLayoutName, 2);

        if (mHasBeak) {
            MR::createAndAddPaneCtrl(this, "Balloon", 1);
        }

        mTextFormer = new TalkTextFormer(this, "Text00");
        MR::hideScreen(this);
    }
}

void TalkBalloon::init(const TableIter& rIter) {
    kill();
}

void TalkBalloon::kill() {
    LayoutActor::kill();
    mMessageCtrl = nullptr;
}

void TalkBalloon::open(TalkMessageCtrl* pCtrl) {
    mMessageCtrl = pCtrl;
    MR::startAnim(this, "Appear", 0);
    if (mHasBeak) {
        MR::startPaneAnim(this, "Balloon", "Beak", 0);
    }
}

void TalkBalloon::close() {
    MR::startAnim(this, "End", 0);
}

namespace {
    f32 smaller(f32 a, f32 b) {
        return b >= a ? a : b;
    }

    f32 larger(f32 a, f32 b) {
        return b >= a ? b : a;
    }

    // Short balloons are reused in turn.
    const s32 sBalloonShortNum = 4;
}  // namespace

void TalkBalloon::updateBalloon() {
    mMessageCtrl->updateBalloonPos();
    if (mHasBeak) {
        Vec2f paneTrans;
        MR::copyPaneTrans(&paneTrans, this, "PicBeak");

        Vec2f v2(mMessageCtrl->mBalloonPos.x - paneTrans.x, mMessageCtrl->mBalloonPos.y - paneTrans.y);
        Vec2f v3(0.0f, 1.0f);

        MR::normalizeOrZero(&v2);

        f32 cosine = MR::acos(v2.x * v3.x + v2.y * v3.y) * _180_PI;

        f32 paneAnimFrameMax = MR::getPaneAnimFrameMax(this, "Balloon", 0);
        f32 paneAnimFrame = MR::getPaneAnimFrame(this, "Balloon", 0);

        if (v2.x * v3.y - v2.y * v3.x > 0.0f) {
            f32 halfMax = paneAnimFrameMax;
            halfMax *= 0.5f;
            paneAnimFrameMax = smaller(paneAnimFrameMax, halfMax + cosine);
        } else {
            f32 half = 0.5f;
            paneAnimFrameMax = larger(0.0f, paneAnimFrameMax * half - cosine);
        }

        if (!mIsBeakMoving) {
            if (MR::fabs(paneAnimFrame - paneAnimFrameMax) < 10.0f) {
                return;
            }

            mIsBeakMoving = true;
        }

        if (paneAnimFrame > paneAnimFrameMax) {
            paneAnimFrameMax += 1.0f;
        } else {
            paneAnimFrameMax -= 1.0f;
        }

        if (MR::abs(paneAnimFrame - paneAnimFrameMax) < 2.0f) {
            mIsBeakMoving = false;
        }

        MR::setPaneAnimFrameAndStop(this, "Balloon", paneAnimFrameMax, 0);
    }
}

void TalkBalloon::updateTalking() {
    updateBalloon();
    mTextFormer->updateTalkingAtTextSpeed();
}

bool TalkBalloon::isTextAppearedAll() {
    return mTextFormer->isTextAppearedAll();
}

bool TalkBalloon::turnPage() {
    return false;
}

bool TalkBalloon::hasNextPage() {
    return mTextFormer->hasNextPage();
}

void TalkBalloon::skipMessage() {
}

void TalkBalloon::pauseOff() {
    MR::requestMovementOn(this);
}

TalkBalloonShort::TalkBalloonShort(const char* pName) : TalkBalloon(pName), mInterval() {
    initNerve(GET_NERVE(TalkBalloonShort, TalkBalloonShortNrvOpen));
}

void TalkBalloonShort::init(const TableIter& rIter) {
    TalkBalloon::create("TalkBalloonStretch", true, false);
}

void TalkBalloonShort::open(TalkMessageCtrl* pCtrl) {
    TalkBalloon::open(pCtrl);
    MR::showScreen(this);
    LayoutActor::appear();
    TalkTextFormer* tempFormer = mTextFormer;

    const char16_t* message;
    if (TalkFunction::isComposeTalk(pCtrl)) {
        message = TalkFunction::getSubMessage(pCtrl);
    } else {
        message = TalkFunction::getMessage(pCtrl);
    }

    tempFormer->formMessage(message, TalkTextForm_Talk);
    tempFormer->setArg(pCtrl->mTagArg, 0);

    s32 numLine = MR::countMessageLine(message);

    switch (numLine) {
    case 1:
        MR::startAnim(this, "OneLine", 1);
        break;

    case 2:
        MR::startAnim(this, "TwoLine", 1);
        break;

    default:
        MR::startAnim(this, "TwoLine", 1);
        break;
    }

    MR::setAnimFrameAndStopAdjustTextWidth(this, "TxtText", 1);
    LayoutActor::setNerve(GET_NERVE(TalkBalloonShort, TalkBalloonShortNrvOpen));
}

void TalkBalloonShort::close() {
    TalkBalloon::close();
    setNerve(GET_NERVE(TalkBalloonShort, TalkBalloonShortNrvClose));
}

void TalkBalloonShort::updateBalloon() {
    TalkBalloon::updateBalloon();
    if (mMessageCtrl->mBalloonPos.z > 0.0f) {
        MR::hideScreen(this);
    } else {
        MR::showScreen(this);
    }

    setTrans(Vec2f(mMessageCtrl->mBalloonPos.x, mMessageCtrl->mBalloonPos.y));
}

void TalkBalloonShort::initInterval() {
    mInterval = 120;
}

void TalkBalloonShort::exeTalk() {
    updateTalking();
    if (mInterval != 0) {
        if (MR::isStep(this, mInterval)) {
            MR::startAnim(this, "End", 0);
        } else if (MR::isGreaterStep(this, mInterval + 30)) {
            open(mMessageCtrl);
        }
    }
}

void TalkBalloonShort::exeClose() {
    updateBalloon();
    if (MR::isAnimStopped(this, 0)) {
        kill();
    }
}

void TalkBalloonShort::exeOpen() {
    updateTalking();
    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE(TalkBalloonShort, TalkBalloonShortNrvTalk));
    }
}

TalkBalloonEvent::TalkBalloonEvent(const char* pName)
    : TalkBalloon(pName), mFormMode(TalkTextForm_Event), mAButton(), mPlaced(false), mHome(0.0f, 0.0f) {
    initNerve(GET_NERVE(TalkBalloonEvent, TalkBalloonEventNrvWait));
}

void TalkBalloonEvent::init(const TableIter& rIter) {
    create("TalkBalloonEvent", 1, 1);
    MR::createAndAddPaneCtrl(this, "AButtonPosition", 1);
}

void TalkBalloonEvent::open(TalkMessageCtrl* pCtrl) {
    mMessageCtrl = pCtrl;
    LayoutActor::appear();
    TalkMessageInfo* info = TalkFunction::getMessageInfo(pCtrl);
    mTextFormer->formMessage(info->mMessage, mFormMode);
    mTextFormer->setArg(pCtrl->mTagArg, 0);
    mAButton->kill();
    mAButton->setFollowActorPane(this, "AButtonPosition");
    updateBalloon();
    LayoutActor::setNerve(GET_NERVE(TalkBalloonEvent, TalkBalloonEventNrvWait));
}

void TalkBalloonEvent::close() {
    MR::startAnim(this, "End", 0);

    if (!TalkFunction::isSelectTalk(mMessageCtrl)) {
        MR::startSystemSE("SE_SY_TALK_OK");
    }

    if (!MR::isDead(mAButton)) {
        mAButton->term();
    }

    MR::recoverSoundVolumeSetting(30);
    MR::moveVolumeStageBGM(1.0f, 60);
    MR::moveVolumeSubBGM(1.0f, 60);
    LayoutActor::setNerve(GET_NERVE(TalkBalloonEvent, TalkBalloonEventNrvClose));
}

void TalkBalloonEvent::updateBalloon() {
    placeByPlugin();
    TalkBalloon::updateBalloon();
}

// A plugin's place for the box (place_talk_box): the text's middle on its point, the whole text inside the screen.
void TalkBalloonEvent::placeByPlugin() {
    Ui::Pane* pRoot = MR::getRootPane(this);
    f32 x = 0.0f;
    f32 y = 0.0f;
    if (!NebulaMods::talkBoxPlace(&x, &y)) {
        if (mPlaced) {
            pRoot->mTranslate.x = mHome.x;
            pRoot->mTranslate.y = mHome.y;
            mPlaced = false;
        }
        return;
    }
    if (!mPlaced) {
        mHome.set(pRoot->mTranslate.x, pRoot->mTranslate.y);
        mPlaced = true;
    }

    // The text's offset from the root, both as last drawn: the layout's own, the same wherever the root is.
    Vec2f root;
    Vec2f text;
    MR::copyPaneTrans(&root, this, nullptr);
    MR::copyPaneTrans(&text, this, "Text00");
    const Ui::Pane* pText = MR::getPane(this, "Text00");
    f32 halfWidth = 0.0f;
    f32 halfHeight = 0.0f;
    if (pText != nullptr) {
        Vec2f corner;
        MR::convertPaneLocalPosToScreenPos(&corner, pText, Vec2f(pText->mSize.width * 0.5f, pText->mSize.height * 0.5f));
        halfWidth = corner.x > text.x ? corner.x - text.x : text.x - corner.x;
        halfHeight = corner.y > text.y ? corner.y - text.y : text.y - corner.y;
    }
    const f32 width = MR::getScreenWidth();
    const f32 height = MR::getScreenHeight();
    x = x < halfWidth ? halfWidth : (x > width - halfWidth ? width - halfWidth : x);
    y = y < halfHeight ? halfHeight : (y > height - halfHeight ? height - halfHeight : y);
    setTrans(Vec2f(x - (text.x - root.x), y - (text.y - root.y)));
}

void TalkBalloonEvent::updateBeak() {
    if (!mHasBeak) {
        return;
    }

    mMessageCtrl->updateBalloonPos();
    TalkMessageCtrl* messageCtrl = mMessageCtrl;
    Vec2f v1(messageCtrl->mBalloonPos.x, messageCtrl->mBalloonPos.y);

    if (0.0f <= v1.x && v1.x < MR::getScreenWidth() && 120.0f <= v1.y &&
        v1.y < static_cast< s32 >(NebulaFrameClock::cScreenLines)) {
        MR::showPane(this, "ShaBeak");
        MR::showPane(this, "PicBeak");
    } else {
        MR::hidePane(this, "ShaBeak");
        MR::hidePane(this, "PicBeak");
    }
}

bool TalkBalloonEvent::turnPage() {
    if (mTextFormer->nextPage()) {
        mTextFormer->setArg(mMessageCtrl->mTagArg, 0);
        MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");
        mAButton->term();

        return true;
    }

    return false;
}

void TalkBalloonEvent::skipMessage() {
    if (LayoutActor::isNerve(GET_NERVE(TalkBalloonEvent, TalkBalloonEventNrvTalk))) {
        mTextFormer->updateTalking();
    }
}

bool TalkBalloonEvent::isTextAppearedAll() {
    return mTextFormer->isTextAppearedAll() && LayoutActor::isNerve(GET_NERVE(TalkBalloonEvent, TalkBalloonEventNrvTalk));
}

void TalkBalloonEvent::exeWait() {
    if (MR::isTimeKeepDemoActive()) {
        LayoutActor::setNerve(GET_NERVE(TalkBalloonEvent, TalkBalloonEventNrvOpen));
    } else if (!MR::isFirstStep(this) && MR::isCameraInterpolatingNearlyEnd()) {
        LayoutActor::setNerve(GET_NERVE(TalkBalloonEvent, TalkBalloonEventNrvOpen));
    }
}

void TalkBalloonEvent::exeOpen() {
    if (MR::isFirstStep(this)) {
        updateBeak();
        MR::showScreen(this);
        TalkBalloon::open(mMessageCtrl);
        MR::startSystemSE("SE_SM_TALKBLN_OPEN");
        MR::setSoundVolumeSetting(AudSystemVolumeController::VolumeSet_Talk, 30);

        if (!MR::isPlayingStageBgmName("STM_KINOPIO_TANKEN_B")) {
            MR::moveVolumeStageBGM(0.5f, 30);
            MR::moveVolumeSubBGM(0.5f, 30);
        }
    }

    updateBalloon();

    if (MR::isAnimStopped(this, 0)) {
        MR::startAnim(this, "Wait", 0);
        LayoutActor::setNerve(GET_NERVE(TalkBalloonEvent, TalkBalloonEventNrvTalk));
    }
}

void TalkBalloonEvent::exeTalk() {
    updateTalking();
    if (mTextFormer->isTextAppearedAll() && MR::isDead(mAButton) && !TalkFunction::getMessageInfo(mMessageCtrl)->isBalloonFix()) {
        if (!TalkFunction::isSelectTalk(mMessageCtrl) || hasNextPage()) {
            mAButton->openWithoutMessage();
        }
    }
}

void TalkBalloonEvent::exeClose() {
    if (MR::isAnimStopped(this, 0)) {
        kill();
    }
}

TalkBalloonSign::TalkBalloonSign(const char* pName) : TalkBalloonEvent(pName) {
    mFormMode = TalkTextForm_Whole;
}

void TalkBalloonSign::init(const TableIter& rIter) {
    TalkBalloon::create("SignBoardBalloon", false, true);
    MR::createAndAddPaneCtrl(this, "AButtonPosition", 1);
}

TalkBalloonInfo::TalkBalloonInfo(const char* pName) : TalkBalloon(pName) {
}

void TalkBalloonInfo::open(TalkMessageCtrl* pCtrl) {
    TalkMessageInfo* info = TalkFunction::getMessageInfo(pCtrl);
    MR::appearInformationMessage(info->mMessage, true);
}

void TalkBalloonInfo::close() {
    MR::startSystemSE("SE_SY_TALK_OK");
    MR::disappearInformationMessage();
}

TalkBalloonIcon::TalkBalloonIcon(const char* pName) : TalkBalloonShort(pName) {
}

void TalkBalloonIcon::init(const TableIter& rIter) {
    TalkBalloon::create("TalkBalloonLetter", false, false);
}

void TalkBalloonIcon::open(TalkMessageCtrl* pCtrl) {
    TalkBalloon::open(pCtrl);
    MR::showScreen(this);
    LayoutActor::appear();

    const char16_t* message = TalkFunction::getMessage(pCtrl);
    TalkTextFormer* former = mTextFormer;

    former->formMessage(message, TalkTextForm_Whole);
    former->setArg(pCtrl->mTagArg, 0);
    LayoutActor::setNerve(GET_NERVE(TalkBalloonShort, TalkBalloonShortNrvOpen));
}

TalkBalloonHolder::TalkBalloonHolder() : mShortIndex() {
    mBalloonShortArray = new TalkBalloonShort*[::sBalloonShortNum];

    for (s32 i = 0; i < ::sBalloonShortNum; i++) {
        mBalloonShortArray[i] = new TalkBalloonShort("会話吹き出し[簡易会話]");
        mBalloonShortArray[i]->initWithoutIter();
        mBalloonShortArray[i]->kill();
    }

    mBalloonEvent = new TalkBalloonEvent("会話吹き出し[イベント]");
    mBalloonEvent->initWithoutIter();
    mBalloonEvent->kill();
    mBalloonInfo = new TalkBalloonInfo("会話吹き出し[インフォメーション]");
    mBalloonInfo->initWithoutIter();
    mBalloonInfo->kill();
    mBalloonSign = new TalkBalloonSign("会話吹き出し[看板]");
    mBalloonSign->initWithoutIter();
    mBalloonSign->kill();
    mBalloonIcon = new TalkBalloonIcon("会話吹き出し[アイコン]");
    mBalloonIcon->initWithoutIter();
    mBalloonIcon->kill();

    mAButton = new IconAButton(true, false);
    mAButton->initWithoutIter();

    mBalloonEvent->mAButton = mAButton;
    mBalloonSign->mAButton = mAButton;
}

TalkBalloon* TalkBalloonHolder::getBalloon(const TalkMessageCtrl* pArg) {
    TalkBalloon* balloon;
    const TalkMessageInfo* info = TalkFunction::getMessageInfo(pArg);

    if (info->isNormalTalk()) {
        balloon = mBalloonEvent;
    } else if (info->isShortTalk()) {
        mBalloonShortArray[mShortIndex]->mInterval = 0;
        balloon = mBalloonShortArray[mShortIndex];
    } else if (info->isEventTalk()) {
        balloon = mBalloonEvent;
    } else if (info->isComposeTalk()) {
        mBalloonShortArray[mShortIndex]->mInterval = 0;
        balloon = mBalloonEvent;
    } else {
        balloon = nullptr;
    }

    if (info->isBalloonInfo()) {
        balloon = mBalloonInfo;
    } else if (info->isBalloonSign()) {
        balloon = mBalloonSign;
    } else if (info->isBalloonFix()) {
        balloon = mBalloonEvent;
    } else if (info->isBalloonCall()) {
        mBalloonShortArray[mShortIndex]->mInterval = 120;
        balloon = mBalloonShortArray[mShortIndex];
    } else if (info->isBalloonIcon()) {
        balloon = mBalloonIcon;
    }

    if (balloon == mBalloonShortArray[mShortIndex]) {
        mShortIndex = (mShortIndex + 1) % ::sBalloonShortNum;
    }

    return balloon;
}

void TalkBalloonHolder::pauseOff() {
    mBalloonEvent->pauseOff();
    mBalloonInfo->pauseOff();
    mBalloonSign->pauseOff();
    mBalloonIcon->pauseOff();

    for (s32 i = 0; i < ::sBalloonShortNum; i++) {
        mBalloonShortArray[i]->pauseOff();
    }

    MR::requestMovementOn(mAButton);
}

void TalkBalloonHolder::balloonOff() {
    mAButton->kill();
}

void TalkBalloonHolder::update() {
}

bool TalkBalloonHolder::isActiveBalloonShort() const {
    for (s32 i = 0; i < ::sBalloonShortNum; i++) {
        if (!MR::isDead(mBalloonShortArray[i])) {
            return true;
        }
    }

    return false;
}
