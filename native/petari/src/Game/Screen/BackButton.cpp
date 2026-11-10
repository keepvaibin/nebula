#include "Game/Screen/BackButton.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

BackButton::BackButton(const char* pName, bool connectToDecoration) : LayoutActor(pName, true), mPaneCtrl(), mIsSelected() {
    lumacore::setClass(this, lumacore::kClassId< BackButton >);
    if (connectToDecoration) {
        MR::connectToSceneLayoutDecoration(this);
    }
}

void BackButton::appear() {
    LayoutActor::appear();
    mPaneCtrl->appear();

    mIsSelected = false;
}

void BackButton::disappear() {
    mPaneCtrl->disappear();
}

bool BackButton::isHidden() const {
    return mPaneCtrl->isHidden();
}

bool BackButton::isAppearing() const {
    return mPaneCtrl->isAppearing();
}

bool BackButton::isDisappearing() const {
    return mPaneCtrl->isDisappearing();
}

bool BackButton::isPointing() const {
    return mPaneCtrl->isPointing();
}

BackButton::~BackButton() {
}

void BackButton::init(const TableIter& /*rIter*/) {
    initLayoutManager("BackButton", 1);
    MR::createAndAddPaneCtrl(this, "Back", 1);

    mPaneCtrl = new ButtonPaneController(this, "Back", "BoxButton", 0, true);
}

void BackButton::control() {
    if (mPaneCtrl->isPointingTrigger()) {
        MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
    }

    if (mPaneCtrl->trySelect()) {
        if (!mIsSelected) {
            MR::startSystemSE("SE_SY_GALAXY_DECIDE_CANCEL");
        }

        mIsSelected = true;
    }

    mPaneCtrl->update();
}
