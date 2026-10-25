#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/IconAButton.hpp"
#include "Game/Screen/YesNoController.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <cstdio>
#ifdef TARGET_PC
#include "Game/Screen/LayoutManager.hpp"
#include "Game/System/NebulaFrameGen.hpp"
#include "Game/Util/DrawUtil.hpp"
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>
#include <revolution/os.h>
#endif

namespace {
    const MessageChangeFuncTableEntry cMessageChangeFuncTable[] = {
        {MR::setTextBoxSystemMessageRecursive},
        {MR::setTextBoxGameMessageRecursive},
    };
#ifdef TARGET_PC
    // The dim a closing or closed system window still shows, and when it last did (OSGetTime); the hand-overs so far.
    f32 sDimLeft = 0.0f;
    OSTime sDimLeftTime = 0;
    u32 sDimEpoch = 0;
    // A closed window's dim holds this many frames for the next window (the pause menu's question opens 9 frames
    // after the save's last window closes), then lifts over 10, as the windows' own end animations do.
    const s32 cDimLinger = 12;
    const f32 cDimStep = 0.1f;
    // A dim left longer ago than this belongs to an earlier screen.
    const s32 cDimHandOverMs = 100;
#endif
};  // namespace

namespace NrvSysInfoWindow {
    NEW_NERVE(SysInfoWindowNrvAppear, SysInfoWindow, Appear);
    NEW_NERVE(SysInfoWindowNrvWait, SysInfoWindow, Wait);
    NEW_NERVE(SysInfoWindowNrvDisappear, SysInfoWindow, Disappear);
};  // namespace NrvSysInfoWindow

SysInfoWindow::SysInfoWindow(SysInfoWindowType windowType, SysInfoExecuteType executeType)
    : LayoutActor("\203\126\203\130\203\145\203\200\227\160\203\103\203\223\203\164\203\110\203\201\201\133\203\126\203\207\203\223\203\105\203\102\203\223\203\150\203\105" /* システム用インフォメーションウィンドウ */, true), mWindowType(windowType), mType(Type_Key), mYesNoSelector(nullptr),
      mIconAButton(nullptr), mTextParentPaneName(nullptr), mWindowParentPaneName(nullptr), _38(executeType == ExecuteType_Children)
#ifdef TARGET_PC
      ,
      mIsBackAnswersNo(false), mDimPane(nullptr), mDim(0.0f), mDimFloor(0.0f), mDimLinger(0), mDimEpoch(0),
      mDimHandOver(DimHandOver_None), mDimHanding(false), mFadePane(nullptr), mPanelPane(nullptr), mShadowPane(nullptr)
#endif
{
    switch (windowType) {
    case WindowType_Normal:
        mTextParentPaneName = "InfoWindow";
        mWindowParentPaneName = "WinInfoWindow";
        break;
    case WindowType_Mini:
        mTextParentPaneName = "InfoWindowM";
        mWindowParentPaneName = "WinInfoWindowM";
        break;
    }
}

void SysInfoWindow::init(const JMapInfoIter& rIter) {
    initLayoutManager(getLayoutName(), 1);
    MR::createAndAddPaneCtrl(this, mTextParentPaneName, 1);
    MR::createAndAddPaneCtrl(this, mWindowParentPaneName, 1);

    if (mWindowType == WindowType_Normal) {
        MR::createAndAddPaneCtrl(this, "Left", 1);
        MR::createAndAddPaneCtrl(this, "Right", 1);
        mYesNoSelector = new YesNoController(this);
        MR::setTextBoxGameMessageRecursive(this, "Left", "System_Save00_No");
        MR::setTextBoxGameMessageRecursive(this, "Right", "System_Save00_Yes");
    }

    mIconAButton = MR::createAndSetupIconAButton(this, !_38, false);
#ifdef TARGET_PC
    mDimPane = MR::getPane(this, "BG");
    if (mWindowType == WindowType_Normal) {
        mFadePane = MR::getPane(this, mWindowParentPaneName);
        mPanelPane = MR::getPane(this, "WinInfo");
        mShadowPane = MR::getPane(this, "ShaInfo");
    }
#endif

    initNerve(GET_NERVE(SysInfoWindow, SysInfoWindowNrvAppear));
}

void SysInfoWindow::movement() {
    LayoutActor::movement();

    if (_38) {
        mIconAButton->movement();
    }
}

void SysInfoWindow::calcAnim() {
    LayoutActor::calcAnim();
#ifdef TARGET_PC
    if (!MR::isDead(this)) {
        shapeFade();
        NebulaFrameGen::noteFullscreenUi();
    }
    updateDim();
#endif

    if (_38) {
        mIconAButton->calcAnim();
    }
}

void SysInfoWindow::draw() const {
#ifdef TARGET_PC
    if (mDimPane != nullptr) {
        // A dim handed over goes the frame the next window opens (this window's update may have run first).
        if (mDim > 0.0f && !mFlag.mIsHidden && (!mDimHanding || mDimEpoch == sDimEpoch)) {
            drawDim();
        }
        if (!MR::isDead(this)) {
            const bool shown = mDimPane->IsVisible();
            mDimPane->SetVisible(false);
            LayoutActor::draw();
            mDimPane->SetVisible(shown);
        }
    } else {
        LayoutActor::draw();
    }
#else
    LayoutActor::draw();
#endif

    if (_38) {
        mIconAButton->draw();
    }
}

void SysInfoWindow::appear() {
#ifdef TARGET_PC
    // The dim a window closing or just closed left takes this one's place until its own is as deep.
    mDimFloor = 0.0f;
    if (sDimLeft > 0.0f && OSTicksToMilliseconds(OSGetTime() - sDimLeftTime) < cDimHandOverMs) {
        mDimFloor = sDimLeft;
    }
    mDimEpoch = ++sDimEpoch;
    sDimLeft = 0.0f;
    mDimHandOver = DimHandOver_None;
    mDimHanding = false;
#endif
    const char* pAnimName = "ButtonAppear";

    MR::startPaneAnim(this, mTextParentPaneName, pAnimName, 0);
    MR::startPaneAnim(this, mWindowParentPaneName, pAnimName, 0);
    setNerve(GET_NERVE(SysInfoWindow, SysInfoWindowNrvAppear));
    LayoutActor::appear();
}

void SysInfoWindow::appear(const char* pMessageId, SysInfoType type, SysInfoTextPos textPos, SysInfoMessageType messageType) {
    mType = type;

    switch (type) {
    case Type_Key:
    case Type_Blocking:
        if (mWindowType != WindowType_Mini) {
            MR::hidePaneRecursive(this, "Left");
            MR::hidePaneRecursive(this, "Right");
        }
        break;
    case Type_YesNo:
        MR::showPaneRecursive(this, "Left");
        MR::showPaneRecursive(this, "Right");
        break;
    }

    switch (textPos) {
    case TextPos_Center:
        MR::setTextBoxVerticalPositionCenterRecursive(this, mTextParentPaneName);
        break;
    case TextPos_Bottom:
        MR::setTextBoxVerticalPositionBottomRecursive(this, mTextParentPaneName);
        break;
    }

    MessageChangeFunc* pMessageChangeFunc = ::cMessageChangeFuncTable[messageType].mFuncPtr;

    pMessageChangeFunc(this, mTextParentPaneName, pMessageId);

    if (type == Type_YesNo) {
        char yesMessageId[48];
        char noMessageId[48];
        snprintf(yesMessageId, sizeof(yesMessageId), "%s_Yes", pMessageId);
        snprintf(noMessageId, sizeof(noMessageId), "%s_No", pMessageId);
        pMessageChangeFunc(this, "Right", yesMessageId);
        pMessageChangeFunc(this, "Left", noMessageId);
    }

    appear();

    switch (type) {
    case Type_Key:
        mIconAButton->openWithoutMessage();
        break;
    case Type_Blocking:
        break;
    case Type_YesNo:
        mYesNoSelector->appear();
        break;
    }
}

void SysInfoWindow::disappear() {
    setNerve(GET_NERVE(SysInfoWindow, SysInfoWindowNrvDisappear));
}

void SysInfoWindow::kill() {
    LayoutActor::kill();
    mIconAButton->kill();
}

void SysInfoWindow::forceKill() {
    kill();
#ifdef TARGET_PC
    // Taken away at once: its dim goes with it.
    mDim = 0.0f;
    mDimHanding = false;
#endif

    if (mYesNoSelector != nullptr) {
        mYesNoSelector->kill();
    }
}

bool SysInfoWindow::isWait() const {
    return isNerve(GET_NERVE(SysInfoWindow, SysInfoWindowNrvWait));
}

bool SysInfoWindow::isSelectedYes() const {
    return mYesNoSelector->isSelectedYes();
}

bool SysInfoWindow::isDisappear() const {
    return isNerve(GET_NERVE(SysInfoWindow, SysInfoWindowNrvDisappear));
}

void SysInfoWindow::control() {
    if (mYesNoSelector != nullptr) {
        mYesNoSelector->update();
    }
}

#ifdef TARGET_PC
namespace {
    // Lines centred in a box centred in its pane, as the game's own questions are.
    class TextBoxRecursiveCenter : public TextBoxRecursiveOperation {
    public:
        virtual void execute(nw4r::lyt::TextBox* pTextBox) const {
            pTextBox->SetTextPositionH(1);
            pTextBox->SetTextPositionV(1);
            pTextBox->mBits.textAlignment = 2;
        }
    };
};  // namespace

// A Yes/No question of Nebula's own (the cutscene skip): its text as given, centred; the answers as init set them (the
// save prompt's Yes and No, in the played language).
void SysInfoWindow::appearYesNoWithText(const wchar_t* pText) {
    mType = Type_YesNo;
    MR::showPaneRecursive(this, "Left");
    MR::showPaneRecursive(this, "Right");
    MR::executeTextBoxRecursive(this, mTextParentPaneName, TextBoxRecursiveCenter());
    MR::setTextBoxMessageRecursive(this, mTextParentPaneName, pText);
    appear();
    mYesNoSelector->appear();
}

void SysInfoWindow::setBackAnswersNo(bool on) {
    mIsBackAnswersNo = on;
}

void SysInfoWindow::decideNo() {
    if (mYesNoSelector != nullptr) {
        mYesNoSelector->decideNo();
    }
}

// The BG pane's alpha as the layout would draw it: its own times its influencing parents', and for the question
// window shaped as its drop shadow is (shapeFade).
f32 SysInfoWindow::getOwnDim() const {
    f32 level = mDimPane->IsVisible() ? mDimPane->mAlpha / 255.0f : 0.0f;
    for (const nw4r::lyt::Pane* pPane = mDimPane->GetParent(); pPane != nullptr && level > 0.0f; pPane = pPane->GetParent()) {
        if (!pPane->IsVisible()) {
            level = 0.0f;
        } else if (pPane->IsInfluencedAlpha()) {
            level *= pPane->mAlpha / 255.0f;
        }
    }
    return mFadePane != nullptr ? level * level * level : level;
}

// The question window fades its panel, drop shadow and screen dim together, each pane on its own: half faded, the
// panel showed its own shadow and the dim through it and turned darker than both the screen before and the finished
// window (150 levels against 163 and 211). Here the panel is whole by 62% of the fade and the shadow and dim follow
// the fade's cube, so what the panel covers moves from the screen behind to the panel (2 levels below it at most,
// from 42), with every pane still drawn on its own each frame, as interpolated frames blend them.
void SysInfoWindow::shapeFade() {
    if (mFadePane == nullptr || mPanelPane == nullptr || mShadowPane == nullptr) {
        return;
    }
    const f32 fade = mFadePane->mGlbAlpha / 255.0f;
    const f32 panel = fade * 1.6f < 1.0f ? fade * 1.6f : 1.0f;
    const f32 shadow = fade * fade * fade;
    mPanelPane->mGlbAlpha = static_cast< u8 >(panel * (mPanelPane->mAlpha / 255.0f) * 255.0f + 0.5f);
    mShadowPane->mGlbAlpha = static_cast< u8 >(shadow * (mShadowPane->mAlpha / 255.0f) * 255.0f + 0.5f);
}

void SysInfoWindow::setDimHandOver(u8 when) {
    mDimHandOver = when;
}

// Each frame (calcAnim runs for a closed window too): an open window shows its own dim or the one it took over. A
// closing window handing its dim over holds it, and once closed holds it cDimLinger frames, then lifts it, unless the
// next window took it; any other closing window fades its dim with itself, as the game does.
void SysInfoWindow::updateDim() {
    if (mDimPane == nullptr) {
        return;
    }
    const bool open = !MR::isDead(this);
    const bool closing = open && isNerve(GET_NERVE(SysInfoWindow, SysInfoWindowNrvDisappear));
    if (open && (!closing || !mDimHanding)) {
        const f32 own = getOwnDim();
        mDim = closing || own > mDimFloor ? own : mDimFloor;
        mDimLinger = cDimLinger;
        return;
    }
    if (!mDimHanding || mDimEpoch != sDimEpoch) {
        mDim = 0.0f;
        return;
    }
    if (!open) {
        if (mDimLinger > 0) {
            mDimLinger--;
        } else {
            mDim = mDim > cDimStep ? mDim - cDimStep : 0.0f;
        }
    }
    if (mDim > 0.0f) {
        sDimLeft = mDim;
        sDimLeftTime = OSGetTime();
    }
}

// The BG pane alone at the window's dim, where the layout draws it (first, under everything else).
void SysInfoWindow::drawDim() const {
    LayoutManager* pManager = getLayoutManager();
    if (pManager == nullptr || pManager->mIsScreenHidden) {
        return;
    }
    MR::setupDrawForNW4RLayout(1.0f, true);
    const u8 alpha = mDimPane->mGlbAlpha;
    mDimPane->mGlbAlpha = static_cast< u8 >(mDim * 255.0f + 0.5f);
    mDimPane->Draw(pManager->mDrawInfo);
    mDimPane->mGlbAlpha = alpha;
}
#endif

const char* SysInfoWindow::getLayoutName() const {
    switch (mWindowType) {
    case WindowType_Normal:
        return "SysInfoWindow";
        break;
    case WindowType_Mini:
        return "SysInfoWindowMini";
        break;
    }

    return nullptr;
}

void SysInfoWindow::exeAppear() {
    MR::setNerveAtPaneAnimStopped(this, mTextParentPaneName, GET_NERVE(SysInfoWindow, SysInfoWindowNrvWait), 0);
}

void SysInfoWindow::exeWait() {
    if (MR::isFirstStep(this)) {
        const char* pAnimName = "ButtonWait";

        MR::startPaneAnim(this, mTextParentPaneName, pAnimName, 0);
        MR::startPaneAnim(this, mWindowParentPaneName, pAnimName, 0);
    }

    if (mType == Type_Key) {
        if (MR::testSystemPadTriggerDecide()) {
            MR::startSystemSE("SE_SY_TALK_OK");
            MR::startCSSound("CS_CLICK_CLOSE", nullptr, 0);
            setNerve(GET_NERVE(SysInfoWindow, SysInfoWindowNrvDisappear));
        }
    } else if (mYesNoSelector != nullptr) {
#ifdef TARGET_PC
        if (mIsBackAnswersNo && MR::testSystemTriggerB()) {
            mYesNoSelector->decideNo();
        }
#endif
        if (mYesNoSelector->isDisappearStart()) {
            setNerve(GET_NERVE(SysInfoWindow, SysInfoWindowNrvDisappear));
        }
    }
};

void SysInfoWindow::exeDisappear() {
    if (MR::isFirstStep(this)) {
        const char* pAnimName = "ButtonEnd";

        MR::startPaneAnim(this, mTextParentPaneName, pAnimName, 0);
        MR::startPaneAnim(this, mWindowParentPaneName, pAnimName, 0);

        if (mType == Type_Key) {
            mIconAButton->term();
        }
#ifdef TARGET_PC
        // A window the caller opens next (even this frame) takes over the dim this one shows now.
        const u8 answer = mType == Type_YesNo && mYesNoSelector != nullptr
                              ? (mYesNoSelector->isSelectedYes() ? DimHandOver_Yes : DimHandOver_No)
                              : DimHandOver_Always;
        mDimHanding = (mDimHandOver & answer) != 0 && mDimEpoch == sDimEpoch;
        if (mDimHanding && mDim > 0.0f) {
            sDimLeft = mDim;
            sDimLeftTime = OSGetTime();
        }
#endif
    }

    if (MR::isPaneAnimStopped(this, mTextParentPaneName, 0)) {
        if (mType == Type_Key || mType == Type_Blocking || mYesNoSelector != nullptr && !mYesNoSelector->_C) {
            kill();
        }
    }
}

void SysInfoWindow::setYesNoSelectorSE(const char* pCursorSE, const char* pYesSE, const char* pNoSE) {
    if (mYesNoSelector != nullptr) {
        mYesNoSelector->setSE(pCursorSE, pYesSE, pNoSE);
    }
}

void SysInfoWindow::resetYesNoSelectorSE() {
    if (mYesNoSelector != nullptr) {
        mYesNoSelector->setSE(nullptr, nullptr, nullptr);
    }
}

void SysInfoWindow::setTextBoxArgNumber(s32 arg, s32 param2) {
    MR::setTextBoxArgNumberRecursive(this, mTextParentPaneName, arg, param2);
}

void SysInfoWindow::setTextBoxArgString(const wchar_t* pArg, s32 param2) {
    MR::setTextBoxArgStringRecursive(this, mTextParentPaneName, pArg, param2);
}

namespace MR {
    SysInfoWindow* createSysInfoWindow() {
        SysInfoWindow* pWindow = new SysInfoWindow(SysInfoWindow::WindowType_Normal, SysInfoWindow::ExecuteType_Normal);

        pWindow->initWithoutIter();

        return pWindow;
    }

    SysInfoWindow* createSysInfoWindowExecuteWithChildren() {
        SysInfoWindow* pWindow = new SysInfoWindow(SysInfoWindow::WindowType_Normal, SysInfoWindow::ExecuteType_Children);

        pWindow->initWithoutIter();

        return pWindow;
    }

    SysInfoWindow* createSysInfoWindowMiniExecuteWithChildren() {
        SysInfoWindow* pWindow = new SysInfoWindow(SysInfoWindow::WindowType_Mini, SysInfoWindow::ExecuteType_Children);

        pWindow->initWithoutIter();

        return pWindow;
    }
};  // namespace MR
