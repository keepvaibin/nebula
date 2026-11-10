#include "Game/Screen/GalaxyConfirmLayout.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/BackButton.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvGalaxyConfirmLayout {
    NEW_NERVE(GalaxyConfirmLayoutNrvSelecting, GalaxyConfirmLayout, Selecting);
    NEW_NERVE(GalaxyConfirmLayoutNrvSelectedBackButton, GalaxyConfirmLayout, SelectedBackButton);
    NEW_NERVE(GalaxyConfirmLayoutNrvSelected, GalaxyConfirmLayout, Selected);
};  // namespace NrvGalaxyConfirmLayout

GalaxyConfirmLayout::GalaxyConfirmLayout() : LayoutActor("ギャラクシー確認レイアウト", true), mPaneCtrl(nullptr), mBackButton(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< GalaxyConfirmLayout >);
}

void GalaxyConfirmLayout::init(const TableIter& /*rIter*/) {
    initLayoutManager("GalaxySelect", 1);
    MR::connectToSceneLayout(this);

    mPaneCtrl = new ButtonPaneController(this, "StartButton", "BoxStartButton", 0, true);
    mBackButton = new BackButton("戻るボタン", true);
    mBackButton->initWithoutIter();

    initNerve(GET_NERVE(GalaxyConfirmLayout, GalaxyConfirmLayoutNrvSelecting));
}

void GalaxyConfirmLayout::appear() {
    mPaneCtrl->appear();
    mBackButton->appear();
    LayoutActor::appear();
    setNerve(GET_NERVE(GalaxyConfirmLayout, GalaxyConfirmLayoutNrvSelecting));
}

bool GalaxyConfirmLayout::isSelected() const {
    return isNerve(GET_NERVE(GalaxyConfirmLayout, GalaxyConfirmLayoutNrvSelected)) ||
           isNerve(GET_NERVE(GalaxyConfirmLayout, GalaxyConfirmLayoutNrvSelectedBackButton));
}

bool GalaxyConfirmLayout::isSelectedYes() const {
    return mPaneCtrl->mIsSelected;
}

void GalaxyConfirmLayout::requestMovementOn() {
    MR::requestMovementOn(this);
    MR::requestMovementOn(mBackButton);
}

void GalaxyConfirmLayout::control() {
    mPaneCtrl->update();
}

void GalaxyConfirmLayout::exeSelecting() {
    if (mBackButton->isSelected()) {
        setNerve(GET_NERVE(GalaxyConfirmLayout, GalaxyConfirmLayoutNrvSelectedBackButton));
    } else {
        if (mPaneCtrl->isPointingTrigger()) {
            MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
        }

        if (mPaneCtrl->trySelect()) {
            MR::startSystemSE("SE_SY_GALAXY_DECIDE_OK");
            mBackButton->disappear();
            setNerve(GET_NERVE(GalaxyConfirmLayout, GalaxyConfirmLayoutNrvSelected));
        } else if (!mBackButton->isAppearing() && !mPaneCtrl->isAppearing() && MR::isPressedFire(PLAYER_1)) {
            MR::startSystemSE("SE_SY_GALAXY_DECIDE_CANCEL");
            mPaneCtrl->disappear();
            mBackButton->disappear();
            setNerve(GET_NERVE(GalaxyConfirmLayout, GalaxyConfirmLayoutNrvSelected));
        }
    }
}

void GalaxyConfirmLayout::exeSelectedBackButton() {
    if (mBackButton->isDisappearing()) {
        mPaneCtrl->disappear();
        setNerve(GET_NERVE(GalaxyConfirmLayout, GalaxyConfirmLayoutNrvSelected));
    }
}

void GalaxyConfirmLayout::exeSelected() {
    if (mBackButton->isHidden() && mPaneCtrl->isHidden()) {
        kill();
    }
}
