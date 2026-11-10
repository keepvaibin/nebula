#include "Game/Screen/GalaxyMapSelectButton.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEW_NERVE(GalaxyMapSelectButtonHide, GalaxyMapSelectButton, Hide);
    NEW_NERVE(GalaxyMapSelectButtonAstroMapWait, GalaxyMapSelectButton, AstroMapWait);
    NEW_NERVE(GalaxyMapSelectButtonStarListWait, GalaxyMapSelectButton, StarListWait);
    NEW_NERVE(GalaxyMapSelectButtonBetweenAstroMapAndGalaxyMapFadein, GalaxyMapSelectButton, BetweenAstroMapAndGalaxyMapFadein);
    NEW_NERVE(GalaxyMapSelectButtonBetweenListToMapFadeout, GalaxyMapSelectButton, BetweenListToMapFadeout);
    NEW_NERVE(GalaxyMapSelectButtonBetweenListToMapFadein, GalaxyMapSelectButton, BetweenListToMapFadein);
    NEW_NERVE(GalaxyMapSelectButtonBetweenMapToListFadeout, GalaxyMapSelectButton, BetweenMapToListFadeout);
    NEW_NERVE(GalaxyMapSelectButtonBetweenMapToListFadein, GalaxyMapSelectButton, BetweenMapToListFadein);
};  // namespace

GalaxyMapSelectButton::GalaxyMapSelectButton() : LayoutActor("選択用ボタン", true), mNextNerve(nullptr), mPaneCtrl(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< GalaxyMapSelectButton >);
}

void GalaxyMapSelectButton::init(const TableIter& /*rIter*/) {
    initLayoutManager("MapButton", 1);
    MR::createAndAddPaneCtrl(this, "MapList", 1);

    mPaneCtrl = new ButtonPaneController(this, "MapList", "BoxLButton", 0, true);
    mPaneCtrl->mDisappearsAfterDecide = false;

    initNerve(GET_NERVE_ANON(GalaxyMapSelectButtonHide));
    kill();
}

void GalaxyMapSelectButton::appear() {
    LayoutActor::appear();
    mPaneCtrl->forceToHide();
}

void GalaxyMapSelectButton::startAstroMap() {
    appear();

    mNextNerve = GET_NERVE_ANON(GalaxyMapSelectButtonAstroMapWait);

    setNerve(GET_NERVE_ANON(GalaxyMapSelectButtonBetweenAstroMapAndGalaxyMapFadein));
}

bool GalaxyMapSelectButton::isPointingAnything() const {
    return mPaneCtrl->isPointing();
}

bool GalaxyMapSelectButton::isDecidedList() const {
    return mPaneCtrl->mIsSelected != false && mPaneCtrl->isDecidedWait();
}

void GalaxyMapSelectButton::changeToStarList() {
    mNextNerve = GET_NERVE_ANON(GalaxyMapSelectButtonStarListWait);
}

void GalaxyMapSelectButton::exeHide() {
}

void GalaxyMapSelectButton::exeAstroMapWait() {
    if (mNextNerve != nullptr && mNextNerve == GET_NERVE_ANON(GalaxyMapSelectButtonStarListWait)) {
        setNerve(GET_NERVE_ANON(GalaxyMapSelectButtonBetweenMapToListFadeout));
    } else if (mPaneCtrl->trySelect()) {
        MR::startSystemSE("SE_SY_GALAMAP_DECIDE");
    }
}

void GalaxyMapSelectButton::exeStarListWait() {
    if (mNextNerve != nullptr && mNextNerve == GET_NERVE_ANON(GalaxyMapSelectButtonAstroMapWait)) {
        setNerve(GET_NERVE_ANON(GalaxyMapSelectButtonBetweenListToMapFadeout));
    }
}

void GalaxyMapSelectButton::exeBetweenAstroMapAndGalaxyMapFadein() {
    if (MR::isFirstStep(this)) {
        mPaneCtrl->appear();
    }

    if (mPaneCtrl->isAppearing()) {
        return;
    }

    setNerve(mNextNerve);

    mNextNerve = nullptr;
}

void GalaxyMapSelectButton::exeBetweenListToMapFadeout() {
    setNerve(GET_NERVE_ANON(GalaxyMapSelectButtonBetweenListToMapFadein));
}

void GalaxyMapSelectButton::exeBetweenListToMapFadein() {
    if (MR::isFirstStep(this)) {
        mPaneCtrl->appear();
    }

    if (mPaneCtrl->isAppearing()) {
        return;
    }

    setNerve(mNextNerve);

    mNextNerve = nullptr;
}

void GalaxyMapSelectButton::exeBetweenMapToListFadeout() {
    if (MR::isFirstStep(this)) {
        mPaneCtrl->disappear();
    }

    if (mPaneCtrl->isDisappearing()) {
        return;
    }

    mPaneCtrl->forceToHide();
    setNerve(GET_NERVE_ANON(GalaxyMapSelectButtonBetweenMapToListFadein));
}

void GalaxyMapSelectButton::exeBetweenMapToListFadein() {
    setNerve(mNextNerve);

    mNextNerve = nullptr;
}

void GalaxyMapSelectButton::control() {
    mPaneCtrl->update();

    if (mPaneCtrl->isPointingTrigger()) {
        MR::startSystemSE("SE_SY_GALAMAP_CURSOR_ON");
    }
}
