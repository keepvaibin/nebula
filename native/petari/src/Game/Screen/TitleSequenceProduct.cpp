#include "Game/Screen/TitleSequenceProduct.hpp"
#include "Game/System/NebulaRegion.hpp"
#include "Game/System/NebulaText.hpp"
#include <nw4r/lyt/textBox.h>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Util/TriggerChecker.hpp"

namespace {
    static const s32 sPressABAppearFrame = 25;
};  // namespace

namespace TitleSequenceProductSub {
    LogoLayout::LogoLayout() : SimpleLayout("\203\215\203\123" /* ロゴ */, "TitleLogo", 2, -1) {
        initEffectKeeper(1, "TitleLogo", nullptr);
        kill();
    }
};  // namespace TitleSequenceProductSub

namespace {
    NEW_NERVE(TitleSequenceProductBgmPrepare, TitleSequenceProduct, BgmPrepare);
    NEW_NERVE(TitleSequenceProductLogoFadein, TitleSequenceProduct, LogoFadein);
    NEW_NERVE(TitleSequenceProductLogoWait, TitleSequenceProduct, LogoWait);
    NEW_NERVE(TitleSequenceProductLogoDisplay, TitleSequenceProduct, LogoDisplay);
    NEW_NERVE(TitleSequenceProductDecide, TitleSequenceProduct, Decide);
    NEW_NERVE(TitleSequenceProductDead, TitleSequenceProduct, Dead);
};  // namespace

TitleSequenceProduct::TitleSequenceProduct()
    : NerveExecutor(""), mLogoLayout(nullptr), mPressStartLayout(nullptr), mAButtonChecker(nullptr),
      mBButtonChecker(nullptr) {
    initNerve(GET_NERVE_ANON(TitleSequenceProductBgmPrepare));

    mLogoLayout = new TitleSequenceProductSub::LogoLayout();
    mPressStartLayout = new SimpleLayout("Press[A][B]", "PressStart", 1, -1);
    mPressStartLayout->initWithoutIter();
#if defined(__ANDROID__)
    // "Press A or tap the screen": one thumb starts the game. The game's own tag for A, whichever device shows it.
    static char16_t sPrompt[96];
    NebulaText::get(sPrompt, 96 - 3, "PressATap");
    s32 at = 0;
    while (sPrompt[at] != 0 && sPrompt[at] != u'\u25A0') {
        at++;
    }
    if (sPrompt[at] != 0) {
        s32 end = at + 1;
        while (sPrompt[end] != 0) {
            end++;
        }
        for (s32 i = end; i > at; i--) {
            sPrompt[i + 2] = sPrompt[i];
        }
        sPrompt[at] = 0x1A;
        sPrompt[at + 1] = static_cast< char16_t >(6 | (3 << 8));
        sPrompt[at + 2] = 0x0000;
    }
    // The layout's boxes hold the prompt they ship with.
    static_cast< nw4r::lyt::TextBox* >(MR::getPane(mPressStartLayout, "TxtStart"))->AllocStringBuffer(96);
    static_cast< nw4r::lyt::TextBox* >(MR::getPane(mPressStartLayout, "ShaStart"))->AllocStringBuffer(96);
    MR::setTextBoxMessageRecursive(mPressStartLayout, "PressAB", sPrompt);
#endif
    mAButtonChecker = new TriggerChecker();
    mBButtonChecker = new TriggerChecker();
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

    if (MR::isAnimStopped(mPressStartLayout, 0)) {
        MR::startAnim(mPressStartLayout, "Wait", 0);
    }

    mAButtonChecker->update(MR::isHeldJump(PLAYER_1));
    mBButtonChecker->update(MR::isHeldFire(PLAYER_1));

#if defined(__ANDROID__)
    if (mAButtonChecker->getLevel() || mBButtonChecker->getLevel()) {
#else
    if (mAButtonChecker->getLevel() && mBButtonChecker->getLevel()) {
#endif
        MR::stopStageBGM(75);
        MR::startSystemSE("SE_SY_GAME_START");
        MR::startCSSound("CS_CLICK_CLOSE", nullptr, 0);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        setNerve(GET_NERVE_ANON(TitleSequenceProductDecide));
    } else {
        updateButtonReaction(mAButtonChecker, "ReactionA");
        updateButtonReaction(mBButtonChecker, "ReactionB");
        updatePressStartReaction();
    }
}

void TitleSequenceProduct::exeDecide() {
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
