#include "Game/Screen/NebulaAchievementToast.hpp"

#ifdef TARGET_PC
#include "Game/Screen/LayoutManager.hpp"
#include "Game/System/LayoutHolder.hpp"
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
#include <nw4r/lyt/material.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/picture.h>
#include <nw4r/lyt/textBox.h>
#include <nw4r/lyt/window.h>
#include <revolution/os.h>
#include <revolution/tpl.h>

namespace {
    enum {
        StepIdle,
        StepRise,
        StepHold,
        StepFall,
    };

    // In the 608x456 canvas's units (the screen's height is 456 at every resolution): the window's padding, the gap
    // between icon and text, the icon's height, the longest the text may run before it narrows, the window's scale,
    // and its distance from the screen's right and bottom edges.
    const f32 cPadding = 16.0f;
    const f32 cGap = 14.0f;
    const f32 cIconSize = 60.0f;
    const f32 cTextMax = 440.0f;
    const f32 cScale = 0.5f;
    const f32 cMargin = 10.0f;

    const s32 cRiseFrames = 24;
    const s32 cIconDelay = 8;   // the icon pops in once the window is mostly up
    const s32 cIconFrames = 16;
    const s32 cFallFrames = 16;
    const s32 cHoldFrames = 270;
    const s32 cHoldFramesQueued = 180;  // when another one waits

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

    // A text pane's boxes (its shadow and its text) at the top-left of their pane; the text's drawn size.
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
            // Measured in a wide box: the game's text boxes break lines at their width (the last name's).
            pText->mSize = nw4r::lyt::Size(2000.0f, 400.0f);
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

    f32 clamp01(f32 t) {
        return t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    }

    // Eases out past 1 and settles back (overshoot `s`: 1.70158 is about 10%).
    f32 easeOutBack(f32 t, f32 s) {
        const f32 u = t - 1.0f;
        return 1.0f + u * u * ((s + 1.0f) * u + s);
    }

    f32 easeInCubic(f32 t) {
        return t * t * t;
    }
};  // namespace

NebulaAchievementToast::NebulaAchievementToast()
    : LayoutActor("NebulaAchievementToast", true), mToast(nullptr), mWindow(nullptr), mText(nullptr), mGlyph(nullptr), mIcon(nullptr),
      mPicture(nullptr), mTextures(nullptr), mStep(StepIdle), mFrame(0), mHoldFrames(cHoldFrames), mWidth(0.0f), mHeight(0.0f) {
}

void NebulaAchievementToast::init(const JMapInfoIter& rIter) {
    initLayoutManager("InformationWindow", 2);

    // Its own name for the canvas rules (LayoutAspect): the window follows the right and bottom edges.
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaAchievementToast";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));

    // The centred window holds everything: its text, and an icon pane made here holding the upper window's text pane
    // (for a glyph) and a picture (for a texture).
    mToast = MR::getPane(this, "InfoWindowCenter");
    mWindow = MR::getPane(this, "InfoWinC");
    mText = MR::getPane(this, "ConfirmC");
    mGlyph = MR::getPane(this, "ConfirmU");
    if (mToast == nullptr || mWindow == nullptr || mText == nullptr || mGlyph == nullptr || mGlyph->mpParent == nullptr) {
        mToast = nullptr;
        return;
    }
    mIcon = nw4r::lyt::Layout::NewObj< nw4r::lyt::Pane >();
    if (mIcon != nullptr) {
        mIcon->SetName("NebulaIcon");
        mGlyph->mpParent->RemoveChild(mGlyph);
        mIcon->AppendChild(mGlyph);
        mToast->AppendChild(mIcon);
    } else {
        mToast = nullptr;
        return;
    }
    // (The Japanese text boxes are already gone, or stand in for the others in Japanese: LayoutManager.)
    if (nw4r::lyt::Pane* pUpper = MR::getPane(this, "InfoWindowUp")) {
        pUpper->SetVisible(false);
    }
    mToast->SetInfluencedAlpha(true);
    mIcon->SetInfluencedAlpha(true);
    mToast->SetVisible(false);

    // Over the HUD's corner the window reads as solid: its colour's alpha, 200 of 255 in the game's information
    // window, 235 here (its content and frame).
    if (nw4r::lyt::Window* pWindow = nw4r::ut::DynamicCast< nw4r::lyt::Window* >(mWindow)) {
        nw4r::lyt::Material* pMaterials[1 + 4] = {pWindow->GetContentMaterial()};
        u32 materialNum = 1;
        for (u32 i = 0; i < pWindow->mFrameNum && materialNum < 5; i++) {
            pMaterials[materialNum++] = pWindow->GetFrameMaterial(i);
        }
        for (u32 i = 0; i < materialNum; i++) {
            if (pMaterials[i] != nullptr) {
                GXColorS10 color = pMaterials[i]->GetTevColor(1);
                color.a = 235;
                pMaterials[i]->SetTevColor(1, color);
            }
        }
    }

    loadTextures();
    // The layout's pane list, counted when it was built, again for the moved and added panes.
    pManager->rebuildPaneInfo();
    LayoutActor::appear();
}

// The icons' textures, from the game's own files, once per scene: a texture missing from a mod's files leaves its icon
// as a glyph.
void NebulaAchievementToast::loadTextures() {
    const s32 count = NebulaAchievements::getIconTextureNum();
    mTextures = new nw4r::lyt::TexMap*[count];
    for (s32 i = 0; i < count; i++) {
        mTextures[i] = nullptr;
        const NebulaAchievementIcon* pIcon = NebulaAchievements::getIconTexture(i);
        if (pIcon->mLayoutArchive) {
            LayoutHolder* pHolder = MR::createAndAddLayoutHolder(pIcon->mArchive);
            void* pTpl = pHolder != nullptr ? pHolder->GetResource('timg', pIcon->mTexture, nullptr) : nullptr;
            if (pTpl != nullptr) {
                mTextures[i] = new nw4r::lyt::TexMap();
                mTextures[i]->ReplaceImage(static_cast< TPLPalette* >(pTpl), 0);
            }
        } else if (MR::isExistResourceInArc(pIcon->mArchive, pIcon->mTexture)) {
            mTextures[i] = MR::createLytTexMap(pIcon->mArchive, pIcon->mTexture);
        }
        if (mTextures[i] == nullptr) {
            OSReport("NebulaAchievementToast: no %s in %s\n", pIcon->mTexture, pIcon->mArchive);
        }
    }
    if (mTextures[0] != nullptr) {
        mPicture = nw4r::lyt::Layout::NewObj< nw4r::lyt::Picture >(*mTextures[0]);
    }
    if (mPicture != nullptr) {
        mPicture->SetName("NebulaIconPic");
        for (u32 i = 0; i < 4; i++) {
            mPicture->SetVtxColor(i, nw4r::ut::Color(0xFFFFFFFFu));
        }
        mPicture->SetInfluencedAlpha(true);
        mPicture->SetVisible(false);
        mIcon->AppendChild(mPicture);
    }
}

void NebulaAchievementToast::update() {
    movement();
    calcAnim();
}

void NebulaAchievementToast::drawOnTop() const {
    if (mToast != nullptr && mStep != StepIdle) {
        draw();
    }
}

void NebulaAchievementToast::control() {
    if (mToast == nullptr) {
        return;
    }

    switch (mStep) {
    case StepIdle: {
        // Into play only (GameScene runs it in play): no cutscene, the scene's wipe open and still, no system wipe.
        const bool free = !MR::isDemoActive() && !MR::isWipeActive() && MR::isWipeOpen() && !MR::isSystemWipeActive();
        const NebulaAchievementIcon* pIcon = nullptr;
        const wchar_t* pName = free ? NebulaAchievements::getWaiting(&pIcon) : nullptr;
        if (pName != nullptr) {
            start(pName, pIcon);
        }
        break;
    }
    case StepRise: {
        mFrame++;
        const f32 t = clamp01(static_cast< f32 >(mFrame) / cRiseFrames);
        const f32 icon = clamp01(static_cast< f32 >(mFrame - cIconDelay) / cIconFrames);
        place(easeOutBack(t, 1.2f), t * 3.0f, mFrame > cIconDelay ? easeOutBack(icon, 2.6f) : 0.0f);
        if (mFrame >= cRiseFrames + cIconFrames) {
            // Up and settled: it counts as shown even if the scene ends now.
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
        const f32 t = clamp01(static_cast< f32 >(mFrame) / cFallFrames);
        place(1.0f - easeInCubic(t), (1.0f - t) * 2.5f, 1.0f);
        if (mFrame >= cFallFrames) {
            mToast->SetVisible(false);
            mStep = StepIdle;
            mFrame = 0;
        }
        break;
    }
    }
}

// The icon as a glyph of the picture font, or a picture of its texture; its drawn size.
void NebulaAchievementToast::setIcon(const NebulaAchievementIcon* pIcon, f32* pWidth, f32* pHeight) {
    const s32 texture = pIcon != nullptr && pIcon->mGlyph == 0 ? NebulaAchievements::findIconTexture(pIcon) : -1;
    if (texture >= 0 && mTextures[texture] != nullptr && mPicture != nullptr) {
        mPicture->GetMaterial()->SetTexture(0, *mTextures[texture]);
        nw4r::math::VEC2(*pCoords)[4] = mPicture->mTexCoordAry.mData;
        if (pCoords != nullptr && mPicture->mTexCoordAry.mNum > 0) {
            pCoords[0][0] = nw4r::math::VEC2(pIcon->mU0, pIcon->mV0);
            pCoords[0][1] = nw4r::math::VEC2(pIcon->mU1, pIcon->mV0);
            pCoords[0][2] = nw4r::math::VEC2(pIcon->mU0, pIcon->mV1);
            pCoords[0][3] = nw4r::math::VEC2(pIcon->mU1, pIcon->mV1);
        }
        // A texture with room around its picture (the faces) is drawn larger so every icon looks the same size.
        const f32 fill = pIcon->mFill > 0.0f ? pIcon->mFill : 1.0f;
        *pHeight = cIconSize * fill;
        *pWidth = cIconSize * fill * pIcon->mWidth / pIcon->mHeight;
        mPicture->mSize = nw4r::lyt::Size(*pWidth, *pHeight);
        mPicture->mTranslate = nw4r::math::VEC3(0.0f, 0.0f, 0.0f);
        mPicture->SetVisible(true);
        mGlyph->SetVisible(false);
        return;
    }

    // A glyph (or a texture that is missing: the Power Star).
    wchar_t picture[8];
    MR::addPictureFontTag(picture, pIcon != nullptr && pIcon->mGlyph != 0 ? pIcon->mGlyph : '7');
    MR::setTextBoxMessageRecursive(this, "ConfirmU", picture);
    fitText(mGlyph, cIconSize, pWidth, pHeight);
    mGlyph->mTranslate = nw4r::math::VEC3(-*pWidth * 0.5f, *pHeight * 0.5f, 0.0f);
    mGlyph->SetVisible(true);
    if (mPicture != nullptr) {
        mPicture->SetVisible(false);
    }
}

void NebulaAchievementToast::start(const wchar_t* pName, const NebulaAchievementIcon* pIcon) {
    wchar_t text[160];
    wchar_t* pText = addFontSizeTag(text, 0);
    pText = addColorTag(pText, 4);
    pText += NebulaText::get(pText, 48, "AchievementGet");
    pText = addString(pText, L"\n", 2);
    pText = addFontSizeTag(pText, 1);
    pText = addColorTag(pText, 0);
    addString(pText, pName, 64);
    MR::setTextBoxMessageRecursive(this, "ConfirmC", text);

    f32 textWidth, textHeight, iconWidth, iconHeight;
    setIcon(pIcon, &iconWidth, &iconHeight);
    fitText(mText, 0.0f, &textWidth, &textHeight);

    // A long name (some languages) narrows the text to `cTextMax`, keeping the window's height.
    const f32 textScale = textWidth > cTextMax ? cTextMax / textWidth : 1.0f;
    mText->mScale = nw4r::math::VEC2(textScale, 1.0f);
    textWidth *= textScale;

    // The window around the icon and the two lines, each centred on its height.
    const f32 iconBox = iconWidth > cIconSize ? iconWidth : cIconSize;
    const f32 content = textHeight > iconHeight ? textHeight : iconHeight;
    mWidth = cPadding + iconBox + cGap + textWidth + cPadding;
    mHeight = cPadding + content + cPadding;
    mWindow->mSize = nw4r::lyt::Size(mWidth, mHeight);
    mWindow->mTranslate = nw4r::math::VEC3(0.0f, 0.0f, 0.0f);
    mIcon->mTranslate = nw4r::math::VEC3(-mWidth * 0.5f + cPadding + iconBox * 0.5f, 0.0f, 0.0f);
    mText->mTranslate = nw4r::math::VEC3(-mWidth * 0.5f + cPadding + iconBox + cGap, textHeight * 0.5f, 0.0f);
    mToast->mScale = nw4r::math::VEC2(cScale, cScale);

    mStep = StepRise;
    mFrame = 0;
    place(0.0f, 0.0f, 0.0f);
    mToast->SetVisible(true);
    MR::startSystemSE("SE_SY_LETTER_APPEAR");
}

// `rise` 0: just below the screen's bottom edge; 1: `cMargin` above it, `cMargin` in from the right edge. The canvas
// rules move the branch to the real edges at any aspect ratio.
void NebulaAchievementToast::place(f32 rise, f32 alpha, f32 iconScale) {
    nw4r::lyt::Pane* pRoot = getLayoutManager()->mLayout->mpRootPane;
    const f32 width = mWidth * cScale;
    const f32 height = mHeight * cScale;
    const f32 bottom = -pRoot->mSize.height * 0.5f;
    mToast->mTranslate.x = pRoot->mSize.width * 0.5f - cMargin - width * 0.5f;
    mToast->mTranslate.y = bottom - height * 0.5f + rise * (height + cMargin);
    mToast->mAlpha = static_cast< u8 >(255.0f * clamp01(alpha) + 0.5f);
    mIcon->mScale = nw4r::math::VEC2(iconScale, iconScale);
}
#endif
