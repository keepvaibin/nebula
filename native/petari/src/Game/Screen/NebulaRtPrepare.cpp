#include "Game/Screen/NebulaRtPrepare.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>

#ifdef TARGET_PC
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include <cstring>
#include <nebula_game_api.h>
#include <nw4r/lyt/layout.h>
#include <revolution/os.h>

using namespace NebulaUi;

namespace {
    // Screen units (NebulaUi::readScreen), the settings menu's question window and look (NebulaSettingsMenu.cpp).
    const f32 cWidth = 360.0f;
    const f32 cPad = 26.0f;
    const f32 cTitleSize = 21.0f;
    const f32 cPercentSize = 15.5f;
    const f32 cBarGap = 22.0f;      // the title's letters to the bar
    const f32 cPercentGap = 12.0f;  // the bar to the percentage
    const f32 cBarHeight = 8.0f;
    const f32 cBarInset = 36.0f;    // the bar from the window's sides
    const u32 cText = 0x4A5274FF;
    const u32 cTrack = 0xCBD6EBFF;
    const u32 cFill = 0x6FA6EAFF;

    const s32 cAppearFrames = 18;
    const s32 cHoldFrames = 15;    // at 100 % before it goes
    const s32 cOutroFrames = 30;   // shrinking and fading to black
    const f32 cOutroScale = 0.9f;
    // The shown progress moves this share of the way to the host's each frame.
    const f32 cFollow = 0.12f;

    enum { PhaseNone = 0, PhaseOpening = 1, PhaseCompiling = 2 };
};  // namespace

class NebulaRtPrepareScreen : public LayoutActor {
public:
    NebulaRtPrepareScreen()
        : LayoutActor("NebulaRtPrepare", true), mBuilt(false), mRoot(nullptr), mWindow(nullptr), mShadow(nullptr), mPanel(nullptr),
          mTitle(nullptr), mPercent(nullptr), mTrack(nullptr), mFill(nullptr), mBarLeft(0.0f), mBarY(0.0f),
          mBarWidth(0.0f), mShown(0.0f), mPercentShown(-1), mFrame(0), mDoneFrame(-1), mGone(false) {
        mKits.mText = &mTextKit;
        mKits.mWindow = &mWindowKit;
        mKits.mButton = nullptr;
    }

    virtual void init(const JMapInfoIter& rIter) {
        initLayoutManager("SysInfoWindow", 0);
        LayoutManager* pManager = getLayoutManager();
        static const char cLayoutName[] = "NebulaRtPrepare";
        delete[] pManager->mLayoutName;
        pManager->mLayoutName = new char[sizeof(cLayoutName)];
        memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));
        mBuilt = build();
        if (!mBuilt) {
            JUTNativeLog("NebulaRtPrepare: a layout or pane is missing\n");
        }
        kill();
    }

    bool isBuilt() const { return mBuilt; }
    // On screen, or on its way.
    bool isGoing() const { return mFrame > 0 && !mGone; }

    // One frame of the preparation (`phase` and `progress` the host's).
    void step(s32 phase, f32 progress) {
        if (!mBuilt || mGone) {
            return;
        }
        if (mFrame == 0) {
            if (phase != PhaseCompiling) {
                return;
            }
            fill();
            appear();
        }
        mFrame++;
        if (phase == PhaseCompiling) {
            const f32 target = clamp(progress, 0.0f, 1.0f);
            if (target > mShown) {
                mShown += (target - mShown) * cFollow;
            }
        } else {
            // Done: the bar fills, holds at 100 %, then the window goes.
            mShown = clamp(mShown + (1.0f - mShown) * 0.25f + 0.01f, 0.0f, 1.0f);
            if (mShown >= 1.0f && mDoneFrame < 0) {
                mDoneFrame = mFrame;
            }
        }
        const s32 since = mDoneFrame < 0 ? 0 : mFrame - mDoneFrame;
        if (mDoneFrame >= 0 && since >= cHoldFrames + cOutroFrames) {
            mGone = true;
            kill();
            setSharp(SharpPrepare, false);
            return;
        }
        place(since);
        setSharp(SharpPrepare, true);
        calcAnim();
    }

    void drawScreen() const {
        if (mBuilt && isGoing() && !getLayoutManager()->mIsScreenHidden) {
            draw();
        }
    }

private:
    bool build() {
        LayoutManager* pManager = getLayoutManager();
        Pane* pRoot = pManager->mLayout->mpRootPane;
        if (pRoot == nullptr || !mModalKit.init(pManager->mLayoutHolder) ||
            !mTextKit.init(MR::createAndAddLayoutHolder("InformationWindow.arc")) ||
            !mWindowKit.init(MR::createAndAddLayoutHolder("SysInfoWindowMini.arc"))) {
            return false;
        }
        // The question window's own panes stay hidden; the screen is built beside them.
        for (nw4r::lyt::PaneList::Iterator it = pRoot->GetChildList().GetBeginIter(); it != pRoot->GetChildList().GetEndIter(); ++it) {
            it->SetVisible(false);
        }
        mRoot = makeGroup(pRoot, "RpRoot");
        mWindow = mRoot != nullptr ? makeGroup(mRoot, "RpWindow") : nullptr;
        mShadow = mModalKit.makeWindow("ShaInfo", "RpShadow");
        mPanel = mModalKit.makeWindow("WinInfo", "RpPanel");
        if (mWindow == nullptr || mShadow == nullptr || mPanel == nullptr) {
            return false;
        }
        mShadow->mBasePosition = 4;
        mPanel->mBasePosition = 4;
        mWindow->AppendChild(mShadow);
        mWindow->AppendChild(mPanel);
        mTitle = makeText(mKits, mWindow, "RpTitle", 96);
        mTrack = makeWindow(mKits, mWindow, "RpTrack");
        mFill = makeWindow(mKits, mWindow, "RpFill");
        mPercent = makeText(mKits, mWindow, "RpPercent", 32);
        if (mTitle == nullptr || mTrack == nullptr || mFill == nullptr || mPercent == nullptr) {
            return false;
        }
        setTextStyle(mTitle, cTitleSize, cText, cText, 4);
        setTextStyle(mPercent, cPercentSize, cText, cText, 4);
        paint(mTrack, cTrack, cTrack);
        paint(mFill, cFill, cFill);
        setInfluencedAlphaRecursive(mRoot, 0);
        return true;
    }

    // The title in the played language and the window's shape around it.
    void fill() {
        wchar_t text[96];
        NebulaText::get(text, 96, "RtPrepareTitle");
        setText(mTitle, text);
        f32 width = 0.0f;
        f32 titleHeight = 0.0f;
        measure(mTitle, 4000.0f, &width, &titleHeight);
        const f32 height =
            cPad + cTitleSize * cCapHeight + cBarGap + cBarHeight + cPercentGap + cPercentSize * cCapHeight + cPad;
        mShadow->mSize = nw4r::lyt::Size(cWidth, height);
        mPanel->mSize = nw4r::lyt::Size(cWidth, height);
        mShadow->mTranslate = nw4r::math::VEC3(6.0f, -6.0f, 0.0f);
        // Top to bottom by the letters: the title's capitals, the bar, the percentage.
        f32 y = height * 0.5f - cPad;
        mTitle->mTranslate = nw4r::math::VEC3(0.0f, textCentre(y - cTitleSize * cCapHeight * 0.5f, cTitleSize), 0.0f);
        y -= cTitleSize * cCapHeight + cBarGap;
        mBarY = y - cBarHeight * 0.5f;
        mBarWidth = cWidth - cBarInset * 2.0f;
        mBarLeft = -mBarWidth * 0.5f;
        shape(mTrack, 0.0f, mBarY, mBarWidth, cBarHeight, cBarHeight * 0.5f);
        y -= cBarHeight + cPercentGap;
        mPercent->mTranslate = nw4r::math::VEC3(0.0f, textCentre(y - cPercentSize * cCapHeight * 0.5f, cPercentSize), 0.0f);
        mPercentShown = -1;
    }

    // `since`: frames since the bar reached 100 % (0 before).
    void place(s32 since) {
        f32 scaleX;
        f32 screenWidth;
        f32 screenHeight;
        readScreen(&scaleX, &screenWidth, &screenHeight);
        f32 alpha = clamp(static_cast< f32 >(mFrame) / cAppearFrames, 0.0f, 1.0f);
        f32 scale = 0.96f + 0.04f * easeOut(alpha);
        if (since > cHoldFrames) {
            const f32 t = clamp(static_cast< f32 >(since - cHoldFrames) / cOutroFrames, 0.0f, 1.0f);
            const f32 eased = t * t * (3.0f - 2.0f * t);
            alpha *= 1.0f - eased;
            scale *= 1.0f - (1.0f - cOutroScale) * eased;
        }
        mRoot->mScale = nw4r::math::VEC2(scaleX, 1.0f);
        mRoot->mAlpha = toAlpha(alpha);
        mWindow->mScale = nw4r::math::VEC2(scale, scale);
        const f32 fill = mBarWidth * mShown;
        const f32 shown = fill > cBarHeight ? fill : cBarHeight;
        shape(mFill, mBarLeft + shown * 0.5f, mBarY, shown, cBarHeight, cBarHeight * 0.5f);
        const s32 percent = static_cast< s32 >(mShown * 100.0f + 0.0001f);
        if (percent != mPercentShown) {
            mPercentShown = percent;
            wchar_t pattern[32];
            wchar_t text[32];
            NebulaText::get(pattern, 32, "RtPreparePercent");
            s32 length = 0;
            for (const wchar_t* p = pattern; *p != 0 && length < 24; p++) {
                if (p[0] == L'{' && p[1] == L'n' && p[2] == L'}') {
                    wchar_t digits[8];
                    s32 count = 0;
                    s32 value = percent;
                    do {
                        digits[count++] = static_cast< wchar_t >(L'0' + value % 10);
                        value /= 10;
                    } while (value > 0 && count < 8);
                    while (count > 0) {
                        text[length++] = digits[--count];
                    }
                    p += 2;
                    continue;
                }
                text[length++] = *p;
            }
            text[length] = 0;
            setText(mPercent, text);
            f32 width;
            f32 height;
            measure(mPercent, 4000.0f, &width, &height);
        }
    }

    bool mBuilt;
    NebulaPaneKit mModalKit;  // SysInfoWindow: the quit question's window and its shadow
    NebulaPaneKit mTextKit;
    NebulaPaneKit mWindowKit;
    Kits mKits;
    Pane* mRoot;
    Pane* mWindow;
    Window* mShadow;
    Window* mPanel;
    TextBox* mTitle;
    TextBox* mPercent;
    Window* mTrack;
    Window* mFill;
    f32 mBarLeft;
    f32 mBarY;
    f32 mBarWidth;
    f32 mShown;          // the progress shown, 0 to 1
    s32 mPercentShown;   // the percentage the text shows (-1: none yet)
    s32 mFrame;          // frames since it appeared (0: not yet)
    s32 mDoneFrame;      // the frame the bar reached 100 % (-1: not yet)
    bool mGone;
};

namespace {
    NebulaRtPrepareScreen* sScreen = nullptr;
    s32 sPhase = PhaseNone;
};  // namespace

namespace NebulaRtPrepare {
    void init() {
        sScreen = new NebulaRtPrepareScreen();
        sScreen->initWithoutIter();
    }

    void update() {
        sPhase = static_cast< s32 >(__NebulaOption(NEBULA_OPTION_RT_PREPARATION));
        if (sScreen == nullptr || !sScreen->isBuilt()) {
            return;
        }
        const f32 progress = static_cast< f32 >(__NebulaOption(NEBULA_OPTION_RT_PREPARE_PROGRESS)) / 10000.0f;
        sScreen->step(sPhase, progress);
    }

    void draw() {
        if (sScreen != nullptr) {
            sScreen->drawScreen();
        }
    }

    bool isHolding() {
        return sPhase != PhaseNone || (sScreen != nullptr && sScreen->isGoing());
    }
};  // namespace NebulaRtPrepare
#endif
