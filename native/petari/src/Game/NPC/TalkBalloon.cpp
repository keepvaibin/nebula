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
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Ui/Pane.hpp"
#include "Game/System/LumaCore.hpp"

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
    lumacore::setClass(this, lumacore::kClassId< TalkBalloon >);
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

TalkBalloonShort::TalkBalloonShort(const char* pName) : TalkBalloon(pName), mInterval(), mBodyPlaces(), mBodyPlacesRead(false) {
    lumacore::setClass(this, lumacore::kClassId< TalkBalloonShort >);
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
    keepBodyOnScreen();
}

namespace {
    // A short balloon's body (its window and the window's shadow) and its text (and the text's shadow), as the
    // balloon layouts name them (TalkBalloonStretch, TalkBalloonLetter).
    const char* const cBodyPanes[] = {"WinBalloon", "ShaBalloon", "TxtText", "ShaText"};
    const s32 cBodyWindows = 2;

    // A pane's horizontal place in its layout's root pane and the scale its parents give it.
    void placeInRoot(const Ui::Pane* pPane, const Ui::Pane* pRoot, f32* pX, f32* pParentScale) {
        f32 x = pPane->mTranslate.x;
        f32 scale = 1.0f;
        for (const Ui::Pane* parent = pPane->GetParent(); parent != nullptr && parent != pRoot; parent = parent->GetParent()) {
            x = parent->mTranslate.x + parent->mScale.x * x;
            scale *= parent->mScale.x;
        }
        *pX = x;
        *pParentScale = scale;
    }
}  // namespace

// The balloon stands on its speaker's place on the screen; a speaker near an edge put half the body past it and cut
// the text (N-1022). The body and its text move along the line, inside the screen's margin, and the beak stays on the
// speaker: the body never moves so far that the beak leaves it. The layout's animations size the body and place the
// text's parent; none moves these panes along the line.
void TalkBalloonShort::keepBodyOnScreen() {
    LayoutManager* manager = getLayoutManager();
    Ui::Pane* panes[4];
    for (s32 i = 0; i < 4; i++) {
        panes[i] = manager->findPaneByName(cBodyPanes[i]);
        if (panes[i] == nullptr) {
            return;
        }
    }
    if (!mBodyPlacesRead) {
        for (s32 i = 0; i < 4; i++) {
            mBodyPlaces[i] = panes[i]->mTranslate.x;
        }
        mBodyPlacesRead = true;
    }
    for (s32 i = 0; i < 4; i++) {
        panes[i]->mTranslate.x = mBodyPlaces[i];
    }

    // The body's extent across the root, in layout units, at the size the animations gave it this frame.
    const Ui::Pane* root = MR::getRootPane(this);
    f32 left = 0.0f;
    f32 right = 0.0f;
    for (s32 i = 0; i < cBodyWindows; i++) {
        f32 x;
        f32 parentScale;
        placeInRoot(panes[i], root, &x, &parentScale);
        const f32 width = panes[i]->GetDrawSize().width * panes[i]->mScale.x * parentScale;
        const s32 horizontal = panes[i]->mBasePosition % 3;  // left, centre, right
        const f32 low = horizontal == 0 ? x : horizontal == 1 ? x - width * 0.5f : x - width;
        left = i == 0 ? low : (low < left ? low : left);
        right = i == 0 ? low + width : (low + width > right ? low + width : right);
    }

    // The screen inside the game's margin, in the same units; the root stands on the speaker.
    const f32 margin = static_cast< f32 >(MR::getScreenMargin());
    Vec2f low;
    Vec2f high;
    Vec2f speaker;
    MR::convertScreenPosToLayoutPos(&low, Vec2f(margin, 0.0f));
    MR::convertScreenPosToLayoutPos(&high, Vec2f(static_cast< f32 >(MR::getScreenWidth()) - margin, 0.0f));
    MR::convertScreenPosToLayoutPos(&speaker, Vec2f(mMessageCtrl->mBalloonPos.x, mMessageCtrl->mBalloonPos.y));

    f32 shift = 0.0f;
    if (right - left >= high.x - low.x) {
        shift = (low.x + high.x) * 0.5f - (speaker.x + (left + right) * 0.5f);
    } else if (speaker.x + right > high.x) {
        shift = high.x - (speaker.x + right);
    } else if (speaker.x + left < low.x) {
        shift = low.x - (speaker.x + left);
    }

    // The beak (at the root's line) keeps a beak's width of body on each side of it.
    if (const Ui::Pane* beak = manager->findPaneByName("PicBeak")) {
        f32 x;
        f32 parentScale;
        placeInRoot(beak, root, &x, &parentScale);
        const f32 inset = beak->GetDrawSize().width * beak->mScale.x * parentScale;
        const f32 most_left = x + inset - right;
        const f32 most_right = x - inset - left;
        shift = most_left > most_right ? 0.0f : (shift < most_left ? most_left : (shift > most_right ? most_right : shift));
    }
    if (shift == 0.0f) {
        return;
    }
    for (s32 i = 0; i < 4; i++) {
        f32 x;
        f32 parentScale;
        placeInRoot(panes[i], root, &x, &parentScale);
        panes[i]->mTranslate.x = mBodyPlaces[i] + shift / parentScale;
    }
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
    : TalkBalloon(pName), mFormMode(TalkTextForm_Event), mAButton() {
    lumacore::setClass(this, lumacore::kClassId< TalkBalloonEvent >);
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
    lumacore::setClass(this, lumacore::kClassId< TalkBalloonSign >);
    mFormMode = TalkTextForm_Whole;
}

void TalkBalloonSign::init(const TableIter& rIter) {
    TalkBalloon::create("SignBoardBalloon", false, true);
    MR::createAndAddPaneCtrl(this, "AButtonPosition", 1);
}

TalkBalloonInfo::TalkBalloonInfo(const char* pName) : TalkBalloon(pName) {
    lumacore::setClass(this, lumacore::kClassId< TalkBalloonInfo >);
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
    lumacore::setClass(this, lumacore::kClassId< TalkBalloonIcon >);
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
