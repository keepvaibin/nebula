#include "Game/Screen/TitleSequenceProduct.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaLauncher.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/TextString.hpp"
#include <Game/Ui/TextBox.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Util/TriggerChecker.hpp"
#include <Game/Ui/Layout.hpp>
#include <cstring>
#include <nebula_module.h>

namespace {
    const s32 sPressABAppearFrame = 25;

    // The version label, in screen units (NebulaUi::readScreen): its text's size, its right and bottom margins (the
    // capitals' bottom), its colour as red, green, blue, alpha, how opaque it is at most, and the share of the way its
    // opacity moves to the target each frame.
    const f32 cVersionSize = 15.0f;
    const f32 cVersionRight = 24.0f;
    const f32 cVersionBottom = 20.0f;
    const u32 cVersionColor = 0xDCE6FFFFu;
    const f32 cVersionOpacity = 0.7f;
    const f32 cVersionFollow = 0.15f;
    const u32 cVersionRoom = 24;  // units its text box holds

    // A prompt box the layout lacks (a mod's layout, or one that is not a text box) takes no buffer.
    void allocPromptBuffer(LayoutActor* pLayout, const char* pPaneName, u32 length) {
        Ui::TextBox* pTextBox = Ui::PaneCast< Ui::TextBox >(MR::getPane(pLayout, pPaneName));

        if (pTextBox != nullptr) {
            pTextBox->AllocStringBuffer(length);
        }
    }
};  // namespace

namespace TitleSequenceProductSub {
    LogoLayout::LogoLayout() : SimpleLayout("ロゴ", "TitleLogo", 2, -1) {
        initEffectKeeper(1, "TitleLogo", nullptr);
        kill();
    }
};  // namespace TitleSequenceProductSub

namespace {
    NEW_NERVE(TitleSequenceProductBgmPrepare, TitleSequenceProduct, BgmPrepare);
    NEW_NERVE(TitleSequenceProductLogoFadein, TitleSequenceProduct, LogoFadein);
    NEW_NERVE(TitleSequenceProductLogoWait, TitleSequenceProduct, LogoWait);
    NEW_NERVE(TitleSequenceProductLogoDisplay, TitleSequenceProduct, LogoDisplay);
    NEW_NERVE(TitleSequenceProductUpdateNotice, TitleSequenceProduct, UpdateNotice);
    NEW_NERVE(TitleSequenceProductDecide, TitleSequenceProduct, Decide);
    NEW_NERVE(TitleSequenceProductDead, TitleSequenceProduct, Dead);
};  // namespace

TitleSequenceProduct::TitleSequenceProduct()
    : NerveExecutor(""), mLogoLayout(nullptr), mPressStartLayout(nullptr), mAButtonChecker(nullptr),
      mBButtonChecker(nullptr), mVersionLayout(nullptr), mVersionKit(nullptr), mVersionRoot(nullptr), mVersionText(nullptr),
      mVersionAlpha(0.0f) {
    initNerve(GET_NERVE_ANON(TitleSequenceProductBgmPrepare));

    mLogoLayout = new TitleSequenceProductSub::LogoLayout();
    mPressStartLayout = new SimpleLayout("Press[A][B]", "PressStart", 1, -1);
    mPressStartLayout->initWithoutIter();
#if defined(__ANDROID__)
    // "Press A or tap the screen": one thumb starts the game. The game's own tag for A, whichever device shows it.
    MR::Text16 prompt;
    NebulaUi::getText(prompt, "PressATap");
    s32 at = 0;
    while (prompt.c_str()[at] != 0 && prompt.c_str()[at] != u'\u25A0') {
        at++;
    }
    if (prompt.c_str()[at] != 0) {
        s32 end = at + 1;
        while (prompt.c_str()[end] != 0) {
            end++;
        }
        // The tag takes three units where the square was one.
        char16_t* const pPrompt = prompt.grow(static_cast< u32 >(end) + 1, static_cast< u32 >(end) + 3);
        for (s32 i = end; i > at; i--) {
            pPrompt[i + 2] = pPrompt[i];
        }
        pPrompt[at] = 0x1A;
        pPrompt[at + 1] = static_cast< char16_t >(6 | (3 << 8));
        pPrompt[at + 2] = 0x0000;
    }
    // The layout's boxes hold the prompt they ship with, or the room it needs.
    const u32 promptLength = static_cast< u32 >(MR::textLength(prompt.c_str()));
    allocPromptBuffer(mPressStartLayout, "TxtStart", promptLength);
    allocPromptBuffer(mPressStartLayout, "ShaStart", promptLength);
    MR::setTextBoxMessageRecursive(mPressStartLayout, "PressAB", prompt.c_str());
#else
    MR::Text16 prompt;
    NebulaUi::getText(prompt, "PressAnyStart");
    const u32 promptLength = static_cast< u32 >(MR::textLength(prompt.c_str()));
    allocPromptBuffer(mPressStartLayout, "TxtStart", promptLength);
    allocPromptBuffer(mPressStartLayout, "ShaStart", promptLength);
    MR::setTextBoxMessageRecursive(mPressStartLayout, "PressAB", prompt.c_str());
#endif
    mAButtonChecker = new TriggerChecker();
    mBButtonChecker = new TriggerChecker();
    initVersion();
}

// "v" and the version (NEBULA_VERSION), in the message font, from the information window's text pane.
void TitleSequenceProduct::initVersion() {
    mVersionLayout = new SimpleLayout("NebulaVersion", "InformationWindow", 1, -1);
    mVersionLayout->initWithoutIter();
    LayoutManager* pManager = mVersionLayout->getLayoutManager();
    static const char cLayoutName[] = "NebulaVersion";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));
    mVersionKit = new NebulaPaneKit();
    NebulaUi::Kits kits = {mVersionKit, mVersionKit, nullptr};
    Ui::Pane* pRoot = pManager->mLayout != nullptr ? pManager->mLayout->mpRootPane : nullptr;
    if (pRoot != nullptr && mVersionKit->init(pManager->mLayoutHolder, "InformationWindow", pManager->mLayout)) {
        for (Ui::Pane* pChild : pRoot->GetChildList()) {
            pChild->SetVisible(false);
        }
        mVersionRoot = NebulaUi::makeGroup(pRoot, "VerRoot");
        mVersionText = mVersionRoot != nullptr ? NebulaUi::makeText(kits, mVersionRoot, "VerText", cVersionRoom) : nullptr;
    }
    if (mVersionText == nullptr) {
        // The game's own layout lacks a pane (a damaged store, a mod's layout): the title shows without it.
        nebula_log("TitleSequenceProduct: a layout or pane is missing; the title shows no version\n");
        mVersionLayout->kill();
        mVersionLayout = nullptr;
        return;
    }
    char16_t text[cVersionRoom];
    u32 length = 0;
    text[length++] = u'v';
    for (const char* p = NEBULA_VERSION; *p != 0 && length + 1 < cVersionRoom; p++) {
        text[length++] = static_cast< char16_t >(*p);
    }
    text[length] = 0;
    // Its right edge and the capitals' middle on its place (anchor 5: the middle of the right side).
    NebulaUi::setTextStyle(mVersionText, cVersionSize, cVersionColor, cVersionColor, 5);
    NebulaUi::setText(mVersionText, text);
    NebulaUi::setInfluencedAlphaRecursive(mVersionRoot, 0);
    mVersionRoot->mAlpha = 0;
    mVersionLayout->kill();
}

// The label's place at the screen's bottom right (the screen's shape may change) and its opacity toward `target`.
void TitleSequenceProduct::stepVersion(f32 target) {
    if (mVersionLayout == nullptr) {
        return;
    }
    mVersionAlpha += (target - mVersionAlpha) * cVersionFollow;
    if (mVersionAlpha < 0.004f) {
        mVersionAlpha = target > 0.0f ? mVersionAlpha : 0.0f;
    }
    const bool shown = mVersionAlpha > 0.0f;
    if (shown && MR::isDead(mVersionLayout)) {
        mVersionLayout->appear();
    } else if (!shown && !MR::isDead(mVersionLayout)) {
        mVersionLayout->kill();
    }
    if (!shown) {
        return;
    }
    f32 scaleX;
    f32 screenWidth;
    f32 screenHeight;
    NebulaUi::readScreen(&scaleX, &screenWidth, &screenHeight);
    mVersionRoot->mScale = Vec2f(scaleX, 1.0f);
    const f32 capsMiddle = -screenHeight * 0.5f + cVersionBottom + cVersionSize * NebulaUi::cCapHeight * 0.5f;
    mVersionText->mTranslate = Vec3f(screenWidth * 0.5f - cVersionRight, NebulaUi::textCentre(capsMiddle, cVersionSize), 0.0f);
    mVersionRoot->mAlpha = NebulaUi::toAlpha(mVersionAlpha * cVersionOpacity);
}

void TitleSequenceProduct::exeBgmPrepare() {
    if (MR::isFirstStep(this)) {
        MR::startStageBGM("STM_TITLE", true);
    }

    if (MR::isPreparedStageBgm()) {
        setNerve(GET_NERVE_ANON(TitleSequenceProductLogoFadein));
    }
}

void TitleSequenceProduct::exeLogoFadein() {
    if (MR::isFirstStep(this)) {
        mLogoLayout->appear();
        MR::startAnim(mLogoLayout, "Appear", 0);
        MR::unlockStageBGM();
    }

    if (MR::isAnimStopped(mLogoLayout, 0)) {
        setNerve(GET_NERVE_ANON(TitleSequenceProductLogoWait));
    }
}

void TitleSequenceProduct::exeLogoWait() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(mLogoLayout, "Wait", 0);
        MR::emitEffect(mLogoLayout, "TitleLogoLight");
        MR::emitEffect(mLogoLayout, "TitleLogoLightB");
        MR::emitEffect(mLogoLayout, "TitleLogoLightC");
        MR::emitEffect(mLogoLayout, "TitleLogoLightD");
        MR::emitEffect(mLogoLayout, "TitleLogoLightE");
        MR::emitEffect(mLogoLayout, "TitleLogoLightF");
        MR::emitEffect(mLogoLayout, "TitleLogoLightG");
    }

    if (MR::isStep(this, ::sPressABAppearFrame)) {
        setNerve(GET_NERVE_ANON(TitleSequenceProductLogoDisplay));
    }
}

void TitleSequenceProduct::exeLogoDisplay() {
    if (MR::isFirstStep(this)) {
        mPressStartLayout->appear();
        MR::startAnim(mPressStartLayout, "Appear", 0);
    }
    stepVersion(1.0f);

    if (MR::isAnimStopped(mPressStartLayout, 0)) {
        MR::startAnim(mPressStartLayout, "Wait", 0);
    }

    mAButtonChecker->update(MR::isHeldJump(PLAYER_1));
    mBButtonChecker->update(MR::isHeldFire(PLAYER_1));

#if defined(__ANDROID__)
    if (mAButtonChecker->getLevel() || mBButtonChecker->getLevel()) {
#else
    if (MR::isHeldAnyWithoutHome(PLAYER_1)) {
#endif
        // An update the start's check found (Check for updates at start): its notice first, once a session.
        if (NebulaLauncher::openNotice()) {
            setNerve(GET_NERVE_ANON(TitleSequenceProductUpdateNotice));
        } else {
            decide();
        }
    } else {
        updateButtonReaction(mAButtonChecker, "ReactionA");
        updateButtonReaction(mBButtonChecker, "ReactionB");
        updatePressStartReaction();
    }
}

// The notice over the title: Later goes on to the game; Update leaves it (Windows: Nebula Setup installs it, then starts
// Nebula again) or installs it behind the game (Linux, macOS: the settings' System page restarts into it), which goes on.
void TitleSequenceProduct::exeUpdateNotice() {
    stepVersion(1.0f);
    NebulaLauncher::update();
    if (!NebulaLauncher::isHolding()) {
        decide();
    }
}

void TitleSequenceProduct::decide() {
    MR::stopStageBGM(75);
    MR::startSystemSE("SE_SY_GAME_START");
    MR::startCSSound("CS_CLICK_CLOSE", nullptr, 0);
    MR::tryRumblePadMiddle(this, PLAYER_1);
    setNerve(GET_NERVE_ANON(TitleSequenceProductDecide));
}

void TitleSequenceProduct::exeDecide() {
    stepVersion(0.0f);
    if (MR::isFirstStep(this)) {
        MR::startAnim(mLogoLayout, "Decide", 0);
        MR::deleteEffectAll(mLogoLayout);
        MR::startAnim(mPressStartLayout, "End", 0);
    }

    if (MR::isAnimStopped(mLogoLayout, 0) && MR::isAnimStopped(mPressStartLayout, 0)) {
        setNerve(GET_NERVE_ANON(TitleSequenceProductDead));
    }
}

void TitleSequenceProduct::exeDead() {
    if (MR::isFirstStep(this)) {
        mLogoLayout->kill();
        mPressStartLayout->kill();
        mVersionAlpha = 0.0f;
        stepVersion(0.0f);

    }
}

void TitleSequenceProduct::appear() {
    setNerve(GET_NERVE_ANON(TitleSequenceProductBgmPrepare));
}

void TitleSequenceProduct::kill() {
    setNerve(GET_NERVE_ANON(TitleSequenceProductDead));
}

bool TitleSequenceProduct::isActive() const {
    return !isNerve(GET_NERVE_ANON(TitleSequenceProductDead));
}

void TitleSequenceProduct::updateButtonReaction(TriggerChecker* pButtonChecker, const char* pAnimName) {
    if (pButtonChecker->getOnTrigger()) {
        MR::startAnim(mLogoLayout, pAnimName, 1);
        MR::setAnimFrameAndStop(mLogoLayout, 0.0f, 1);
    } else if (pButtonChecker->getOffTrigger()) {
        MR::startAnim(mLogoLayout, pAnimName, 1);
    }
}

void TitleSequenceProduct::updatePressStartReaction() {
    if (mAButtonChecker->getOnTrigger()) {
        MR::startAnim(mPressStartLayout, "ButtonReaction", 0);
    } else if (mBButtonChecker->getOnTrigger()) {
        MR::startAnim(mPressStartLayout, "ButtonReaction", 0);
    }
}
