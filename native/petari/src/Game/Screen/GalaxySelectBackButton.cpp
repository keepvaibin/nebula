#include "Game/Screen/GalaxySelectBackButton.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

namespace NrvGalaxySelectBackButton {
    NEW_NERVE(GalaxySelectBackButtonNrvAppear, GalaxySelectBackButton, Appear);
    NEW_NERVE(GalaxySelectBackButtonNrvDisappear, GalaxySelectBackButton, Disappear);
    NEW_NERVE(GalaxySelectBackButtonNrvDecide, GalaxySelectBackButton, Decide);
};  // namespace NrvGalaxySelectBackButton

GalaxySelectBackButton::GalaxySelectBackButton() : LayoutActor("\203\115\203\203\203\211\203\116\203\126\201\133\221\111\221\360\226\337\202\351\203\173\203\136\203\223" /* ギャラクシー選択戻るボタン */, true) {
}

void GalaxySelectBackButton::init(const JMapInfoIter&) {
    initLayoutManager("BackButton", 1);
    MR::connectToSceneLayout(this);

    mPaneCtrl = new ButtonPaneController(this, "Back", "BoxButton", 0, true);
    mPaneCtrl->_23 = true;

    initNerve(GET_NERVE(GalaxySelectBackButton, GalaxySelectBackButtonNrvAppear));
}

void GalaxySelectBackButton::appear() {
    LayoutActor::appear();
    setNerve(GET_NERVE(GalaxySelectBackButton, GalaxySelectBackButtonNrvAppear));
}

void GalaxySelectBackButton::disappear() {
    setNerve(GET_NERVE(GalaxySelectBackButton, GalaxySelectBackButtonNrvDisappear));
}

void GalaxySelectBackButton::decide() {
    setNerve(GET_NERVE(GalaxySelectBackButton, GalaxySelectBackButtonNrvDecide));
}

bool GalaxySelectBackButton::isPointing() const {
    return mPaneCtrl->isPointingPaneDirect();
}

bool GalaxySelectBackButton::tryPointing() {
    return mPaneCtrl->onPointing();
}

bool GalaxySelectBackButton::isAppearing() const {
    return mPaneCtrl->isAppearing();
}

bool GalaxySelectBackButton::isDisappearing() const {
    return mPaneCtrl->isDisappearing();
}

void GalaxySelectBackButton::control() {
    mPaneCtrl->update();
}

void GalaxySelectBackButton::exeAppear() {
    if (MR::isFirstStep(this)) {
        mPaneCtrl->appear();
    }
}

void GalaxySelectBackButton::exeDisappear() {
    if (MR::isFirstStep(this)) {
        mPaneCtrl->disappear();
    }

    if (mPaneCtrl->isHidden()) {
        kill();
    }
}

void GalaxySelectBackButton::exeDecide() {
    if (MR::isFirstStep(this)) {
        mPaneCtrl->decide();
    }

    if (mPaneCtrl->isHidden()) {
        kill();
    }
}
