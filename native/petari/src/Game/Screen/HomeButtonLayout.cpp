#include "Game/Screen/HomeButtonLayout.hpp"
#include "Game/Screen/NebulaHomeMenu.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Speaker/SpkSystem.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/WPadHolder.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/JUtility/JUTVideo.hpp>

namespace NrvHomeButtonLayout {
    NEW_NERVE(HomeButtonLayoutDeactive, HomeButtonLayout, Deactive);
    NEW_NERVE(HomeButtonLayoutActive, HomeButtonLayout, Active);
};  // namespace NrvHomeButtonLayout

HomeButtonLayout::HomeButtonLayout() : LayoutActor("HOME\203\173\203\136\203\223" /* HOMEボタン */, false), mIsInitialized(false), _24(), _25() {
}

void HomeButtonLayout::init(const JMapInfoIter& rIter) {
    // The console loaded the HOME button library's layout, sounds, messages and settings from the disc here; the
    // host keeps the menu (NebulaHomeMenu).
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
        _24 = false;
    }

    if (GameSystemFunction::isResetProcessing()) {
        return;
    }

    if (GameSystemFunction::isDisplayStrapRemineder()) {
        return;
    }

    if (tryCorePadTriggerHome()) {
        setNerve(GET_NERVE(HomeButtonLayout, HomeButtonLayoutActive));
    }
}

void HomeButtonLayout::exeActive() {
    if (MR::isFirstStep(this)) {
        AudWrap::getSystem()->enterHomeButtonMenu();
#ifdef TARGET_PC
        // Natively the game draws the menu (NebulaSettingsMenu), under the game's own pointer.
#else
        MR::startStarPointerModeHomeButton(this);
#endif
        GameSystemFunction::onHomeButtonMenuBeginAllRumble();
        MR::setWPadHolderModeHomeButton();

        if (_24) {
            GameSystemFunction::stopControllerLeaveWatcher();
        }

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
        GameSystemFunction::requestGoWiiMenu(true);
        GameSystemFunction::onHomeButtonMenuEndAllRumble();
        MR::setStarPointerModeBase();
        break;
    case NebulaHomeMenu::cChoiceReset:
        if (!_25) {
            AudWrap::getSystem()->preProcessToReset();
        }

        bVar1 = true;
        GameSystemFunction::requestResetGameSystem(true);
        GameSystemFunction::onHomeButtonMenuEndAllRumble();
        MR::setStarPointerModeBase();
        break;
    }

    if (!bVar1) {
        AudWrap::getSystem()->exitHomeButtonMenu();
        SpkSystem::reconnect(-1);
    }

    if (MR::isStarPointerModeHomeButton()) {
        MR::endStarPointerMode(this);
    }

    MR::setWPadHolderModeGame();

    if (_24) {
        GameSystemFunction::startControllerLeaveWatcher();
    }

    _25 = false;

    setNerve(GET_NERVE(HomeButtonLayout, HomeButtonLayoutDeactive));
}

void HomeButtonLayout::draw() const {
    if (MR::isDead(this) || MR::isHiddenLayout(this)) {
        return;
    }

    J3DNativeState::setNumChans(1);
    J3DNativeState::setNumTexGens(0);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_PASSCLR);

    Mtx44 projectionMtx;
    f32 screenHalfHeight = static_cast< s32 >(JUTGetVideoManager()->getEfbHeight()) / 2.0f;
    f32 screenHalfWidth = MR::getScreenWidth() / 2.0f;
    NebulaMtxOrtho(projectionMtx, screenHalfHeight, -screenHalfHeight, -screenHalfWidth, screenHalfWidth, 0.0f, 500.0f);
    J3DNativeView::setProjection(projectionMtx, GX_ORTHOGRAPHIC);

    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setZMode(GX_FALSE, GX_NEVER, GX_FALSE);
    // The library drew the menu here; the game draws it with its settings menu (NebulaSettingsMenu).
}

void HomeButtonLayout::forceToDeactive() {
    if (!mIsInitialized) {
        return;
    }

    NebulaHomeMenu::close();
    _25 = true;
}

bool HomeButtonLayout::isActive() const {
    if (MR::isDead(this)) {
        return false;
    }

    return isNerve(GET_NERVE(HomeButtonLayout, HomeButtonLayoutActive)) && !MR::isNewNerve(this);
}


bool HomeButtonLayout::tryCorePadTriggerHome() {
    for (s32 i = 0; i < MR::getWPadMaxCount(); i++) {
        if (MR::testCorePadTriggerHome(i)) {
            return true;
        }
    }

    return false;
}
