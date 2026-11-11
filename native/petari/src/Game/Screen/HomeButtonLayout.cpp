#include "Game/Screen/HomeButtonLayout.hpp"
#include "Game/Screen/NebulaHomeMenu.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvHomeButtonLayout {
    NEW_NERVE(HomeButtonLayoutDeactive, HomeButtonLayout, Deactive);
    NEW_NERVE(HomeButtonLayoutActive, HomeButtonLayout, Active);
};  // namespace NrvHomeButtonLayout

HomeButtonLayout::HomeButtonLayout() : LayoutActor("HOMEボタン", false), mIsInitialized(false), mForcedClosed(false) {
    lumacore::setClass(this, lumacore::kClassId< HomeButtonLayout >);
}

void HomeButtonLayout::init(const TableIter& /*rIter*/) {
    // The host keeps the menu (NebulaHomeMenu).
    mIsInitialized = true;
    initNerve(GET_NERVE(HomeButtonLayout, HomeButtonLayoutDeactive));
    appear();

    mFlag.mIsHidden = true;
    mFlag.mIsOffCalcAnim = true;
}

void HomeButtonLayout::movement() {
    if (MR::isDead(this)) {
        return;
    }

    LayoutActor::movement();
}

void HomeButtonLayout::exeDeactive() {
    if (MR::isFirstStep(this)) {
        mFlag.mIsHidden = true;
        mFlag.mIsOffCalcAnim = true;
    }

    if (GameSystemFunction::isResetProcessing()) {
        return;
    }

    if (GameSystemFunction::isBeforeTitle()) {
        return;
    }

    if (tryPressedHome()) {
        setNerve(GET_NERVE(HomeButtonLayout, HomeButtonLayoutActive));
    }
}

void HomeButtonLayout::exeActive() {
    if (MR::isFirstStep(this)) {
        AudWrap::getSystem()->enterHomeButtonMenu();
        // Natively the game draws the menu (NebulaSettingsMenu), under the game's own pointer.
        GameSystemFunction::onHomeButtonMenuBeginAllRumble();
        MR::setPlayerInputsModeHome();

        mFlag.mIsHidden = false;
        mFlag.mIsOffCalcAnim = false;

        NebulaHomeMenu::open(MR::isScreen16Per9());
    }

    const NebulaHomeMenu::Choice choice = NebulaHomeMenu::update();
    if (choice == NebulaHomeMenu::cChoiceNone) {
        return;
    }

    bool bVar1 = false;

    switch (choice) {
    case NebulaHomeMenu::cChoiceClose:
        GameSystemFunction::onHomeButtonMenuCloseAllRumble();
        break;
    case NebulaHomeMenu::cChoiceLeaveGame:
        AudWrap::getSystem()->preProcessToReset();
        bVar1 = true;
        GameSystemFunction::requestQuitGame(true);
        GameSystemFunction::onHomeButtonMenuEndAllRumble();
        MR::setStarPointerModeBase();
        break;
    case NebulaHomeMenu::cChoiceReset:
        if (!mForcedClosed) {
            AudWrap::getSystem()->preProcessToReset();
        }

        bVar1 = true;
        GameSystemFunction::requestResetGameSystem(true);
        GameSystemFunction::onHomeButtonMenuEndAllRumble();
        MR::setStarPointerModeBase();
        break;
    default:
        break;
    }

    if (!bVar1) {
        AudWrap::getSystem()->exitHomeButtonMenu();
    }

    if (MR::isStarPointerModeHomeButton()) {
        MR::endStarPointerMode(this);
    }

    MR::setPlayerInputsModeGame();

    mForcedClosed = false;

    setNerve(GET_NERVE(HomeButtonLayout, HomeButtonLayoutDeactive));
}

void HomeButtonLayout::forceToDeactive() {
    if (!mIsInitialized) {
        return;
    }

    NebulaHomeMenu::close();
    mForcedClosed = true;
}

bool HomeButtonLayout::isActive() const {
    if (MR::isDead(this)) {
        return false;
    }

    return isNerve(GET_NERVE(HomeButtonLayout, HomeButtonLayoutActive)) && !MR::isNewNerve(this);
}


bool HomeButtonLayout::tryPressedHome() {
    for (u32 i = 0; i < MR::getPlayerMaxCount(); i++) {
        if (MR::isPressedHome(i)) {
            return true;
        }
    }

    return false;
}
