#include "Game/Screen/NebulaPauseSettingsButton.hpp"

#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>
#include <Game/Ui/Layout.hpp>
#include <Game/Ui/Pane.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    // The middle of the pause menu's lower band, between its counters' two rows (4 and -32 about their pane at -147).
    const f32 cBandMiddle = -161.0f;
    // The button at the size of the counters beside it.
    const f32 cScale = 0.62f;
};  // namespace

NebulaPauseSettingsButton::NebulaPauseSettingsButton() : LayoutActor("NebulaPauseSettings", true), mButton(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< NebulaPauseSettingsButton >);
}

void NebulaPauseSettingsButton::init(const TableIter& /*rIter*/) {
    initLayoutManager("PauseMenu", 1);

    // Its own name for the canvas rules (LayoutAspect): it stays in the middle at every width.
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaPauseSettings";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));

    // The menu's lower button alone, moved down into the band.
    Ui::Pane* pRoot = pManager->mLayout->mpRootPane;
    for (Ui::Pane* pChild : pRoot->GetChildList()) {
        pChild->SetVisible(MR::isEqualString(pChild->mName, "BackButton"));
    }
    MR::hidePane(this, "NBackUp");
    Ui::Pane* pGroup = MR::getPane(this, "BackButton");
    Ui::Pane* pLower = MR::getPane(this, "BackDown");
    if (pGroup != nullptr && pLower != nullptr) {
        pGroup->mScale = Vec2f(cScale, cScale);
        pGroup->mTranslate.y = cBandMiddle - pLower->mTranslate.y * cScale;
    }
    MR::Text16 label;
    NebulaUi::getText(label, "SettingsTitle");
    MR::setTextBoxMessageRecursive(this, "NBackDown", label.c_str());

    mButton = new ButtonPaneController(this, "NBackDown", "BoxButton2", 0, true);
    mButton->mDisappearsAfterDecide = false;
    // PauseMenu::draw draws it with the menu's own layout: under the windows that open over the menu (its questions,
    // the save's) and under Luigi's letter, as the menu's own buttons are.
    MR::connectToSceneLayoutOnPauseMovementCalcAnim(this);
    kill();
}

void NebulaPauseSettingsButton::control() {
    mButton->update();
}

void NebulaPauseSettingsButton::show() {
    LayoutActor::appear();
    mButton->appear();
}

void NebulaPauseSettingsButton::hide() {
    if (!mButton->isHidden()) {
        mButton->disappear();
    }
}
