#include "Game/Screen/NebulaAchievementToast.hpp"

#ifdef TARGET_PC
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/textBox.h>

namespace {
    enum {
        StepIdle,
        StepRise,
        StepHold,
        StepFall,
    };

    // In the 608x456 canvas's units: the window's padding, the gap between icon and text, the icon's size, the whole
    // window's scale, its distance from the canvas's right edge, and its bottom's height: above the HUD's coin and Star
    // Bit counters in that corner.
    const f32 cPadding = 18.0f;
    const f32 cGap = 14.0f;
    const f32 cIconSize = 64.0f;
    const f32 cScale = 0.55f;
    const f32 cMargin = 14.0f;
    const f32 cBottom = 76.0f;

    const s32 cRiseFrames = 18;
    const s32 cFallFrames = 14;
    const s32 cHoldFrames = 240;
    const s32 cHoldFramesQueued = 150;  // when another one waits

    // The game's message tags, as MessageData holds them natively: 0x1A, the tag's byte length and group as two
    // bytes, the tag id, then 16-bit parameters (an 8-bit one in the high byte).
    wchar_t* addFontSizeTag(wchar_t* pDst, u16 size) {
        pDst[0] = 0x1A;
        pDst[1] = static_cast< wchar_t >(6 | (4 << 8));
        pDst[2] = size;  // 0: three quarters, 1: the box's size
        return pDst + 3;
    }

    wchar_t* addColorTag(wchar_t* pDst, u8 color) {
        pDst[0] = 0x1A;
        pDst[1] = static_cast< wchar_t >(8 | (0xFF << 8));
        pDst[2] = 0;
        pDst[3] = static_cast< wchar_t >(color << 8);  // the game's text colours: 0 white, 4 gold
        return pDst + 4;
    }

    wchar_t* addString(wchar_t* pDst, const wchar_t* pSrc, s32 capacity) {
        while (*pSrc != 0 && capacity-- > 1) {
            *pDst++ = *pSrc++;
        }
        *pDst = 0;
        return pDst;
    }

    nw4r::lyt::TextBox* asTextBox(nw4r::lyt::Pane* pPane) {
        if (pPane == nullptr || !pPane->GetRuntimeTypeInfo()->IsDerivedFrom(&nw4r::lyt::TextBox::typeInfo)) {
            return nullptr;
        }
        return static_cast< nw4r::lyt::TextBox* >(pPane);
    }

    // A text pane's two boxes (its shadow and its text) at the top-left of their pane; the text's drawn size.
    void fitText(nw4r::lyt::Pane* pParent, f32 fontSize, f32* pWidth, f32* pHeight) {
        *pWidth = 0.0f;
        *pHeight = 0.0f;
        for (nw4r::lyt::PaneList::Iterator it = pParent->GetChildList().GetBeginIter(); it != pParent->GetChildList().GetEndIter(); ++it) {
            nw4r::lyt::TextBox* pText = asTextBox(&*it);
            if (pText == nullptr || !pText->IsVisible()) {
                continue;
            }
            if (fontSize > 0.0f) {
                pText->SetFontSize(nw4r::lyt::Size(fontSize, fontSize));
            }
            pText->SetTextPositionH(0);
            pText->SetTextPositionV(0);
            const nw4r::ut::Rect rect = pText->GetTextDrawRect();
            const f32 width = rect.right - rect.left;
            const f32 height = rect.bottom - rect.top;
            *pWidth = width > *pWidth ? width : *pWidth;
            *pHeight = height > *pHeight ? height : *pHeight;
        }
        for (nw4r::lyt::PaneList::Iterator it = pParent->GetChildList().GetBeginIter(); it != pParent->GetChildList().GetEndIter(); ++it) {
            if (asTextBox(&*it) != nullptr) {
                it->mSize.width = *pWidth + 4.0f;
                it->mSize.height = *pHeight + 4.0f;
            }
        }
    }

    f32 easeOut(f32 t) {
        const f32 u = 1.0f - t;
        return 1.0f - u * u * u;
    }
};  // namespace

NebulaAchievementToast::NebulaAchievementToast()
    : LayoutActor("NebulaAchievementToast", true), mToast(nullptr), mWindow(nullptr), mText(nullptr), mIcon(nullptr), mStep(StepIdle),
      mFrame(0), mHoldFrames(cHoldFrames), mWidth(0.0f), mHeight(0.0f) {
}

void NebulaAchievementToast::init(const JMapInfoIter& rIter) {
    initLayoutManager("InformationWindow", 2);

    // Its own name for the canvas rules (LayoutAspect): the window follows the right and bottom edges.
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaAchievementToast";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));

    // The centred window holds the text; the upper window's text pane moves into it for the icon.
    mToast = MR::getPane(this, "InfoWindowCenter");
    mWindow = MR::getPane(this, "InfoWinC");
    mText = MR::getPane(this, "ConfirmC");
    mIcon = MR::getPane(this, "ConfirmU");
    if (mToast != nullptr && mIcon != nullptr && mIcon->mpParent != nullptr) {
        mIcon->mpParent->RemoveChild(mIcon);
        mToast->AppendChild(mIcon);
    }
    // (The Japanese text boxes are already gone, or stand in for the others in Japanese: LayoutManager.)
    if (nw4r::lyt::Pane* pUpper = MR::getPane(this, "InfoWindowUp")) {
        pUpper->SetVisible(false);
    }
    if (mToast != nullptr) {
        mToast->SetInfluencedAlpha(true);
        mToast->SetVisible(false);
    }

    MR::connectToSceneLayoutDecoration(this);
    if (MR::isExistSceneObj(SceneObj_DemoDirector)) {
        MR::registerDemoSimpleCastAll(this);
    }
    LayoutActor::appear();
}

void NebulaAchievementToast::control() {
    if (mToast == nullptr || mWindow == nullptr || mText == nullptr || mIcon == nullptr) {
        return;
    }

    switch (mStep) {
    case StepIdle: {
        s32 icon = 0;
        // Into play only: no cutscene, the scene's wipe open and still (not the stage's opening circle or a scene
        // change), no system wipe.
        const bool free = !MR::isDemoActive() && !MR::isWipeActive() && MR::isWipeOpen() && !MR::isSystemWipeActive();
        const wchar_t* pName = free ? NebulaAchievements::getWaiting(&icon) : nullptr;
        if (pName != nullptr) {
            start(pName, icon);
        }
        break;
    }
    case StepRise: {
        mFrame++;
        const f32 t = static_cast< f32 >(mFrame) / cRiseFrames;
        place(easeOut(t), t * 2.0f);
        if (mFrame >= cRiseFrames) {
            // On screen whole: it counts as shown even if the scene ends now.
            NebulaAchievements::popWaiting();
            mHoldFrames = NebulaAchievements::isMoreWaiting() ? cHoldFramesQueued : cHoldFrames;
            mStep = StepHold;
            mFrame = 0;
        }
        break;
    }
    case StepHold:
        if (++mFrame >= mHoldFrames) {
            mStep = StepFall;
            mFrame = 0;
        }
        break;
    case StepFall: {
        mFrame++;
        const f32 t = static_cast< f32 >(mFrame) / cFallFrames;
        place(1.0f - t * t, (1.0f - t) * 2.0f);
        if (mFrame >= cFallFrames) {
            mToast->SetVisible(false);
            mStep = StepIdle;
            mFrame = 0;
        }
        break;
    }
    }
}

void NebulaAchievementToast::start(const wchar_t* pName, s32 icon) {
    wchar_t text[160];
    wchar_t* pText = addFontSizeTag(text, 0);
    pText = addColorTag(pText, 4);
    pText += NebulaText::get(pText, 48, "AchievementGet");
    pText = addString(pText, L"\n", 2);
    pText = addFontSizeTag(pText, 1);
    pText = addColorTag(pText, 0);
    addString(pText, pName, 64);
    MR::setTextBoxMessageRecursive(this, "ConfirmC", text);

    wchar_t picture[8];
    MR::addPictureFontTag(picture, icon);
    MR::setTextBoxMessageRecursive(this, "ConfirmU", picture);

    // The window around the icon and the two lines, each centred on the window's height.
    f32 textWidth, textHeight, iconWidth, iconHeight;
    fitText(mText, 0.0f, &textWidth, &textHeight);
    fitText(mIcon, cIconSize, &iconWidth, &iconHeight);
    const f32 content = textHeight > iconHeight ? textHeight : iconHeight;
    mWidth = cPadding + iconWidth + cGap + textWidth + cPadding;
    mHeight = cPadding + content + cPadding;
    mWindow->mSize.width = mWidth;
    mWindow->mSize.height = mHeight;
    mWindow->mTranslate.x = 0.0f;
    mWindow->mTranslate.y = 0.0f;
    mIcon->mTranslate.x = -mWidth * 0.5f + cPadding;
    mIcon->mTranslate.y = iconHeight * 0.5f;
    mText->mTranslate.x = -mWidth * 0.5f + cPadding + iconWidth + cGap;
    mText->mTranslate.y = textHeight * 0.5f;
    mToast->mScale.x = cScale;
    mToast->mScale.y = cScale;

    mStep = StepRise;
    mFrame = 0;
    place(0.0f, 0.0f);
    mToast->SetVisible(true);
    MR::startSystemSE("SE_SY_LETTER_APPEAR");
}

// `rise` 0: just below the canvas's bottom edge; 1: `cBottom` above it. The window's right edge stays `cMargin` in.
void NebulaAchievementToast::place(f32 rise, f32 alpha) {
    nw4r::lyt::Pane* pRoot = getLayoutManager()->mLayout->mpRootPane;
    const f32 width = mWidth * cScale;
    const f32 height = mHeight * cScale;
    const f32 bottom = -pRoot->mSize.height * 0.5f;
    mToast->mTranslate.x = pRoot->mSize.width * 0.5f - cMargin - width * 0.5f;
    mToast->mTranslate.y = bottom - height * 0.5f + rise * (height + cBottom);
    alpha = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
    mToast->mAlpha = static_cast< u8 >(255.0f * alpha + 0.5f);
}
#endif
