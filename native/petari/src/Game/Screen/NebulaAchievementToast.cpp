#include "Game/Screen/NebulaAchievementToast.hpp"
#include <nebula_module.h>

#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Util/TextString.hpp"
#include <cstring>
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Material.hpp>
#include <Game/Ui/Pane.hpp>
#include <Game/Ui/Picture.hpp>
#include <Game/Ui/TextBox.hpp>
#include <Game/Ui/Window.hpp>
#include <Game/Ui/Font.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    enum {
        StepIdle,
        StepRise,
        StepHold,
        StepFall,
    };

    // In the canvas's units (the screen's height is 456 at every resolution): the window's padding, the gap between
    // icon and text, the icon's height, the space between the title line and the name, the longest the text may run
    // before it narrows, the window's scale, and its distance from the screen's right and bottom edges (the same on
    // screen for both).
    NEBULA_TUNABLE(f32, cPadding, 16.0f);
    NEBULA_TUNABLE(f32, cGap, 14.0f);
    NEBULA_TUNABLE(f32, cIconSize, 60.0f);
    NEBULA_TUNABLE(f32, cLineSpace, 8.0f);
    NEBULA_TUNABLE(f32, cTextMax, 440.0f);
    NEBULA_TUNABLE(f32, cScale, 0.65f);
    NEBULA_TUNABLE(f32, cMargin, 8.0f);

    NEBULA_TUNABLE(s32, cRiseFrames, 24);
    NEBULA_TUNABLE(s32, cIconDelay, 8);   // the icon pops in once the window is mostly up
    NEBULA_TUNABLE(s32, cIconFrames, 16);
    NEBULA_TUNABLE(s32, cFallFrames, 16);
    NEBULA_TUNABLE(s32, cHoldFrames, 270);
    NEBULA_TUNABLE(s32, cHoldFramesQueued, 180);  // when another one waits

    // The game's message tags, as MessageData holds them natively: 0x1A, the tag's byte length and group as two
    // bytes, the tag id, then 16-bit parameters (an 8-bit one in the high byte).
    char16_t* addFontSizeTag(char16_t* pDst, u16 size) {
        pDst[0] = 0x1A;
        pDst[1] = static_cast< char16_t >(6 | (4 << 8));
        pDst[2] = size;  // 0: three quarters, 1: the box's size
        return pDst + 3;
    }

    char16_t* addColorTag(char16_t* pDst, u8 color) {
        pDst[0] = 0x1A;
        pDst[1] = static_cast< char16_t >(8 | (0xFF << 8));
        pDst[2] = 0;
        pDst[3] = static_cast< char16_t >(color << 8);  // the game's text colours: 0 white, 4 gold
        return pDst + 4;
    }

    char16_t* addString(char16_t* pDst, const char16_t* pSrc) {
        while (*pSrc != 0) {
            *pDst++ = *pSrc++;
        }
        *pDst = 0;
        return pDst;
    }

    Ui::TextBox* asTextBox(Ui::Pane* pPane) {
        return Ui::PaneCast< Ui::TextBox >(pPane);
    }

    // A text pane's boxes (its shadow and its text) at the top-left of their pane; the text's drawn size.
    void fitText(Ui::Pane* pParent, f32 fontSize, f32 lineSpace, f32* pWidth, f32* pHeight) {
        *pWidth = 0.0f;
        *pHeight = 0.0f;
        // The Japanese boxes (InformationWindow's "JpJa" ones) sit by their top centre, 251 to the right; the others by
        // their top-left. Every language's at the top-left, the shadow kept at its offset from the text.
        Vec3f textAt(0.0f, 0.0f, 0.0f);
        for (Ui::Pane* pChild : pParent->GetChildList()) {
            if (asTextBox(pChild) != nullptr && MR::isEqualStringPrefix(pChild->mName, "Inf")) {
                textAt = pChild->mTranslate;
            }
        }
        for (Ui::Pane* pChild : pParent->GetChildList()) {
            if (asTextBox(pChild) != nullptr) {
                pChild->mBasePosition = 0;
                pChild->mTranslate.x -= textAt.x;
                pChild->mTranslate.y -= textAt.y;
            }
        }
        for (Ui::Pane* pChild : pParent->GetChildList()) {
            Ui::TextBox* pText = asTextBox(pChild);
            if (pText == nullptr || !pText->IsVisible()) {
                continue;
            }
            if (fontSize > 0.0f) {
                // A picture-font glyph is scaled by the box's own font (the message font, 27 wide for 33 high): its
                // width follows that font's proportions so the picture keeps its own.
                const Ui::Font* pFont = pText->mpFont;
                const f32 aspect = pFont != nullptr && pFont->GetHeight() > 0 ? static_cast< f32 >(pFont->GetWidth()) / pFont->GetHeight() : 1.0f;
                pText->SetFontSize(Ui::Size(fontSize * aspect, fontSize));
            }
            pText->SetTextPositionH(0);
            pText->SetTextPositionV(0);
            pText->mLineSpace = lineSpace;
            // Measured in a wide box: the game's text boxes break lines at their width (the last name's).
            pText->mSize = Ui::Size(2000.0f, 400.0f);
            const Ui::Rect rect = pText->GetTextDrawRect();
            const f32 width = rect.right - rect.left;
            const f32 height = rect.bottom - rect.top;
            *pWidth = width > *pWidth ? width : *pWidth;
            *pHeight = height > *pHeight ? height : *pHeight;
        }
        for (Ui::Pane* pChild : pParent->GetChildList()) {
            if (asTextBox(pChild) != nullptr) {
                pChild->mSize.width = *pWidth + 4.0f;
                pChild->mSize.height = *pHeight + 4.0f;
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
    lumacore::setClass(this, lumacore::kClassId< NebulaAchievementToast >);
}

void NebulaAchievementToast::init(const TableIter& /*rIter*/) {
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
    mIcon = mToast->GetLayout()->NewPane< Ui::Pane >();
    mIcon->SetName("NebulaIcon");
    mGlyph->mpParent->RemoveChild(mGlyph);
    mIcon->AppendChild(mGlyph);
    mToast->AppendChild(mIcon);
    // (The Japanese text boxes are already gone, or stand in for the others in Japanese: LayoutManager.)
    if (Ui::Pane* pUpper = MR::getPane(this, "InfoWindowUp")) {
        pUpper->SetVisible(false);
    }
    mToast->SetInfluencedAlpha(true);
    mIcon->SetInfluencedAlpha(true);
    mToast->SetVisible(false);

    // Over the HUD's corner the window reads as solid: its colour's alpha, 200 of 255 in the game's information
    // window, 235 here (its content and frame).
    if (Ui::Window* pWindow = Ui::PaneCast< Ui::Window >(mWindow)) {
        Ui::Material* pMaterials[1 + 4] = {pWindow->GetContentMaterial()};
        u32 materialNum = 1;
        for (u32 i = 0; i < pWindow->mFrameNum && materialNum < 5; i++) {
            pMaterials[materialNum++] = pWindow->GetFrameMaterial(i);
        }
        for (u32 i = 0; i < materialNum; i++) {
            if (pMaterials[i] != nullptr) {
                NebulaColor16 color = pMaterials[i]->GetStageColor(1);
                color.a = 235;
                pMaterials[i]->SetStageColor(1, color);
            }
        }
    }

    loadTextures();
    LayoutActor::appear();
}

// The icons' textures, from the game's own files, once per scene: a texture missing from a mod's files leaves its icon
// as a glyph.
void NebulaAchievementToast::loadTextures() {
    const s32 count = NebulaAchievements::getIconTextureNum();
    mTextures = new Ui::TexMap*[count];
    for (s32 i = 0; i < count; i++) {
        mTextures[i] = nullptr;
        const NebulaAchievementIcon* pIcon = NebulaAchievements::getIconTexture(i);
        if (pIcon->mLayoutArchive) {
            LayoutHolder* pHolder = MR::createAndAddLayoutHolder(pIcon->mArchive);
            void* pTpl = pHolder != nullptr ? pHolder->GetResource('timg', pIcon->mTexture, nullptr) : nullptr;
            if (pTpl != nullptr) {
                mTextures[i] = new Ui::TexMap();
                mTextures[i]->ReplaceImage(pTpl, 0);
            }
        } else if (MR::isExistResourceInArc(pIcon->mArchive, pIcon->mTexture)) {
            mTextures[i] = MR::createLytTexMap(pIcon->mArchive, pIcon->mTexture);
        }
        if (mTextures[i] == nullptr) {
            nebula_log("NebulaAchievementToast: no %s in %s\n", pIcon->mTexture, pIcon->mArchive);
        }
    }
    // No icon textures at all: no picture (the first slot does not exist).
    if (count > 0 && mTextures[0] != nullptr) {
        mPicture = mIcon->GetLayout()->NewPane< Ui::Picture >(*mTextures[0]);
    }
    if (mPicture != nullptr) {
        mPicture->SetName("NebulaIconPic");
        for (u32 i = 0; i < 4; i++) {
            mPicture->SetVtxColor(i, Color8(0xFFFFFFFFu));
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
        // At once, whatever the scene is doing: over cutscenes and their bars, menus and wipes.
        const NebulaAchievementIcon* pIcon = nullptr;
        const char16_t* pName = NebulaAchievements::getWaiting(&pIcon);
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
        Vec2f(*pCoords)[4] = mPicture->mTexCoordAry.mData;
        if (pCoords != nullptr && mPicture->mTexCoordAry.mNum > 0) {
            pCoords[0][0] = Vec2f(pIcon->mU0, pIcon->mV0);
            pCoords[0][1] = Vec2f(pIcon->mU1, pIcon->mV0);
            pCoords[0][2] = Vec2f(pIcon->mU0, pIcon->mV1);
            pCoords[0][3] = Vec2f(pIcon->mU1, pIcon->mV1);
        }
        // A texture with room around its picture (the faces) is drawn larger so every icon looks the same size.
        const f32 fill = pIcon->mFill > 0.0f ? pIcon->mFill : 1.0f;
        *pHeight = cIconSize * fill;
        *pWidth = cIconSize * fill * pIcon->mWidth / pIcon->mHeight;
        mPicture->mSize = Ui::Size(*pWidth, *pHeight);
        mPicture->mTranslate = Vec3f(0.0f, 0.0f, 0.0f);
        mPicture->SetVisible(true);
        mGlyph->SetVisible(false);
        return;
    }

    // A glyph (or a texture that is missing: the Power Star).
    char16_t picture[8];  // one picture font tag (3 units) and the terminator
    MR::addPictureFontTag(picture, pIcon != nullptr && pIcon->mGlyph != 0 ? pIcon->mGlyph : '7');
    MR::setTextBoxMessageRecursive(this, "ConfirmU", picture);
    fitText(mGlyph, cIconSize, 0.0f, pWidth, pHeight);
    mGlyph->mTranslate = Vec3f(-*pWidth * 0.5f, *pHeight * 0.5f, 0.0f);
    mGlyph->SetVisible(true);
    if (mPicture != nullptr) {
        mPicture->SetVisible(false);
    }
}

void NebulaAchievementToast::start(const char16_t* pName, const NebulaAchievementIcon* pIcon) {
    // The heading and the name, each after its size and colour tags, with the line break between and the terminator.
    MR::Text16 heading;
    const u32 headingLength = static_cast< u32 >(NebulaUi::getText(heading, "AchievementGet"));
    MR::Text16 text;
    char16_t* const pBegin = text.room(3 + 4 + headingLength + 1 + 3 + 4 + static_cast< u32 >(MR::textLength(pName)) + 1);
    char16_t* pText = addFontSizeTag(pBegin, 0);
    pText = addColorTag(pText, 4);
    pText = addString(pText, heading.c_str());
    pText = addString(pText, u"\n");
    pText = addFontSizeTag(pText, 1);
    pText = addColorTag(pText, 0);
    addString(pText, pName);
    MR::setTextBoxMessageRecursive(this, "ConfirmC", pBegin);

    f32 textWidth, textHeight, iconWidth, iconHeight;
    setIcon(pIcon, &iconWidth, &iconHeight);
    fitText(mText, 0.0f, cLineSpace, &textWidth, &textHeight);

    // A long name (some languages) narrows the text to `cTextMax`, keeping the window's height.
    const f32 textScale = textWidth > cTextMax ? cTextMax / textWidth : 1.0f;
    mText->mScale = Vec2f(textScale, 1.0f);
    textWidth *= textScale;

    // The window around the icon and the two lines, each centred on its height.
    const f32 iconBox = iconWidth > cIconSize ? iconWidth : cIconSize;
    const f32 content = textHeight > iconHeight ? textHeight : iconHeight;
    mWidth = cPadding + iconBox + cGap + textWidth + cPadding;
    mHeight = cPadding + content + cPadding;
    mWindow->mSize = Ui::Size(mWidth, mHeight);
    mWindow->mTranslate = Vec3f(0.0f, 0.0f, 0.0f);
    mIcon->mTranslate = Vec3f(-mWidth * 0.5f + cPadding + iconBox * 0.5f, 0.0f, 0.0f);
    mText->mTranslate = Vec3f(-mWidth * 0.5f + cPadding + iconBox + cGap, textHeight * 0.5f, 0.0f);
    mToast->mScale = Vec2f(cScale, cScale);

    mStep = StepRise;
    mFrame = 0;
    place(0.0f, 0.0f, 0.0f);
    mToast->SetVisible(true);
    MR::startSystemSE("SE_SY_LETTER_APPEAR");
}

// `rise` 0: just below the screen's bottom edge; 1: `cMargin` above it, as far in from the right edge. The canvas
// rules move the branch to the real edges at any aspect ratio. The game draws its 2D for a 16:9 picture: a pane that
// adjusts its location (this window, as the HUD's) is drawn three quarters as wide as its units, so its width and its
// horizontal margin count in those units.
void NebulaAchievementToast::place(f32 rise, f32 alpha, f32 iconScale) {
    Ui::Pane* pRoot = getLayoutManager()->mLayout->mpRootPane;
    const f32 adjust = mToast->IsLocationAdjust() && MR::isScreen16Per9() ? 0.75f : 1.0f;
    const f32 width = mWidth * cScale * adjust;
    const f32 height = mHeight * cScale;
    const f32 bottom = -pRoot->mSize.height * 0.5f;
    mToast->mTranslate.x = pRoot->mSize.width * 0.5f - cMargin * adjust - width * 0.5f;
    mToast->mTranslate.y = bottom - height * 0.5f + rise * (height + cMargin);
    mToast->mAlpha = static_cast< u8 >(255.0f * clamp01(alpha) + 0.5f);
    mIcon->mScale = Vec2f(iconScale, iconScale);
}
