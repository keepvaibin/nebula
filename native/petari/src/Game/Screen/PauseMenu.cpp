#include "Game/Screen/PauseMenu.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/LuigiLetter.hpp"
#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Screen/NebulaPauseSettingsButton.hpp"
#include "Game/Screen/NebulaSettingsMenu.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 39.0f);

namespace {
    NEBULA_TUNABLE(s32, cBackSequenceFadeFrame, 90);

    bool isInvalidBackAstroDome() {
        return !MR::isOnGameEventFlagUseAstroDome() || (MR::isStageAstroLocation() && !MR::isEqualStageName("HeavensDoorGalaxy"));
    }

    bool isStageHideScenarioTitle() {
        return MR::isStageBeginPrologueEvent() || MR::isStageAstroLocation();
    }
};  // namespace

namespace NrvPauseMenu {
    NEW_NERVE(PauseMenuNrvSelecting, PauseMenu, Selecting);
    NEW_NERVE(PauseMenuNrvDecided, PauseMenu, Decided);
    NEW_NERVE(PauseMenuNrvDisappear, PauseMenu, Disappear);
    NEW_NERVE(PauseMenuNrvConfirm, PauseMenu, Confirm);
    NEW_NERVE(PauseMenuNrvGameDataSave, PauseMenu, GameDataSave);
    NEW_NERVE(PauseMenuNrvLuigiLetter, PauseMenu, LuigiLetter);
};  // namespace NrvPauseMenu

PauseMenu::PauseMenu()
    : LayoutActor("ポーズメニュー", true), mUpperButton(), mLowerButton(), mConfirmWindow(), mLuigiLetter(), mLetterButton(), mStageTitleOffset(0.0f, 39.0f),
      mCoinNumPos(0.0f, 0.0f), mStarPieceNumPos(0.0f, 0.0f), mWasDefaultLayoutActive(true) {
    mSettingsButton = nullptr;
}

void PauseMenu::init(const TableIter& /*rIter*/) {
    initLayoutManager("PauseMenu", 1);
    MR::createAndAddPaneCtrl(this, "BG", 1);
    MR::createAndAddPaneCtrl(this, "PicStagebase", 1);
    MR::createAndAddPaneCtrl(this, "PicStagebase1", 1);
    MR::createAndAddPaneCtrl(this, "Counter", 1);
    MR::createAndAddPaneCtrl(this, "Stars", 2);
    MR::createAndAddPaneCtrl(this, "Coin", 1);
    MR::createAndAddPaneCtrl(this, "Piece", 1);
    MR::setFollowPos(&mCoinNumPos, this, "Coin");
    MR::setFollowTypeReplace(this, "Coin");
    MR::setFollowPos(&mStarPieceNumPos, this, "Piece");
    MR::setFollowTypeReplace(this, "Piece");

    if (::isStageHideScenarioTitle()) {
        const char* pPaneName = "ShaStageTitle";

        MR::createAndAddPaneCtrl(this, pPaneName, 1);
        MR::setFollowPos(&mStageTitleOffset, this, pPaneName);
        MR::setFollowTypeAdd(this, pPaneName);
    }

    MR::connectToSceneLayoutOnPause(this);

    mUpperButton = new ButtonPaneController(this, "NBackUp", "BoxButton1", 0, true);
    mUpperButton->mDisappearsAfterDecide = false;

    mLowerButton = new ButtonPaneController(this, "NBackDown", "BoxButton2", 0, true);
    mLowerButton->mDisappearsAfterDecide = false;

    MR::setTextBoxGameMessageRecursive(this, "NBackDown", ::isInvalidBackAstroDome() ? "PauseMenu_ButtonEndGame" : "PauseMenu_ButtonBackAstroDome");

    if (MR::isEqualStageName("AstroDome")) {
        MR::setTextBoxMessageRecursive(this, "ShaStageTitle", MR::getCurrentScenarioNameOnCurrentLanguage());
    } else {
        MR::setTextBoxMessageRecursive(this, "ShaStageTitle", MR::getCurrentGalaxyNameOnCurrentLanguage());
    }

    if (::isStageHideScenarioTitle()) {
        MR::hidePaneRecursive(this, "ScenarioTitle");
    } else {
        MR::setTextBoxMessageRecursive(this, "ScenarioTitle", MR::getCurrentScenarioNameOnCurrentLanguage());
    }

    updateStarPane();

    mConfirmWindow = MR::createSysInfoWindow();

    MR::connectToSceneLayoutOnPause(mConfirmWindow);

    if (MR::getLuigiLetterGalaxyName()) {
        mLuigiLetter = MR::createLuigiLetterForMenu();
        mLuigiLetter->initWithoutIter();

        mLetterButton = new ButtonPaneController(this, "LetterButton", "BoxButton3", 0, true);
        mLetterButton->mDisappearsAfterDecide = false;
    } else {
        MR::hidePaneRecursive(this, "LetterButton");
    }

    mSettingsButton = new NebulaPauseSettingsButton();
    mSettingsButton->initWithoutIter();

    initNerve(GET_NERVE(PauseMenu, PauseMenuNrvSelecting));
}

void PauseMenu::appear() {
    s32 coinNum = MR::getCoinNum();
    s32 starPieceNum = MR::getStarPieceNum();

    mWasDefaultLayoutActive = MR::isActiveDefaultGameLayout();

    if (mWasDefaultLayoutActive) {
        MR::deactivateDefaultGameLayout();
    }

    MR::setTextBoxNumberRecursive(this, "ShaStarNum", MR::getPowerStarNum());
    MR::setTextBoxNumberRecursive(this, "ShaMarioNum", MR::getPlayerLeft());
    MR::setTextBoxNumberRecursive(this, "ShaCoinNum", coinNum);
    MR::setTextBoxNumberRecursive(this, "ShaPieceNum", starPieceNum);
    MR::setTextBoxGameMessageRecursive(this, "ShaMario", "PlayerLeft");

    char16_t starPieceMessage[3];  // two picture font codes and the terminator

    starPieceMessage[0] = MR::isStageAstroLocation() ? 0x57 : 0x3B;
    starPieceMessage[1] = 0x40;
    starPieceMessage[2] = '\0';

    MR::setTextBoxMessageRecursive(this, "ShaPiece", starPieceMessage);
    startPaneAnimWithoutButton("Appear");
    mUpperButton->appear();

    if (mLowerButton != nullptr) {
        mLowerButton->appear();
    }

    // A stage with no letter galaxy made no letter button.
    if (MR::isOnLuigiHiding() && mLetterButton != nullptr) {
        mLetterButton->appear();
    }

    mSettingsButton->show();

    setNerve(GET_NERVE(PauseMenu, PauseMenuNrvSelecting));
    LayoutActor::appear();

    const char* pCoinPanePos = "CPosition1";
    const char* pPiecePanePos = "PPosition1";

    if (coinNum >= 100) {
        pCoinPanePos = "CPosition3";
    } else if (coinNum >= 10) {
        pCoinPanePos = "CPosition2";
    }

    if (starPieceNum >= 1000) {
        pPiecePanePos = "PPosition4";
    } else if (starPieceNum >= 100) {
        pPiecePanePos = "PPosition3";
    } else if (starPieceNum >= 10) {
        pPiecePanePos = "PPosition2";
    }

    MR::copyPaneTrans(&mCoinNumPos, this, pCoinPanePos);
    MR::copyPaneTrans(&mStarPieceNumPos, this, pPiecePanePos);
}

void PauseMenu::draw() const {
    LayoutActor::draw();
    mSettingsButton->draw();

    if (mLuigiLetter != nullptr) {
        mLuigiLetter->draw();
    }
}

void PauseMenu::kill() {
    LayoutActor::kill();
    mSettingsButton->kill();

    if (mWasDefaultLayoutActive) {
        MR::activateDefaultGameLayout();
    }
}

void PauseMenu::control() {
    mUpperButton->update();

    if (mLowerButton != nullptr) {
        mLowerButton->update();
    }

    if (mLetterButton != nullptr) {
        mLetterButton->update();
    }

    mStageTitleOffset.y = cNum_control_f1;
}

void PauseMenu::updateStarPane() {
    GalaxyStatusAccessor accessor = MR::makeCurrentGalaxyStatusAccessor();
    const char* starPaneNames[7] = {"ShaStarA", "ShaStarB", "ShaStarC", "ShaStarD", "ShaStarE", "ShaStarF", "ShaStarG"};
    const char* starPicPaneNames[7] = {"PicStarA", "PicStarB", "PicStarC", "PicStarD", "PicStarE", "PicStarF", "PicStarG"};

    for (s32 i = 0; i < ARRAY_SIZEU(starPaneNames); i++) {
        MR::hidePaneRecursive(this, starPaneNames[i]);
    }

    if (::isStageHideScenarioTitle()) {
        return;
    }

    s32 extraStarCount = 0;
    bool isInvalidAstroDome = !MR::isOnGameEventFlagUseAstroDome();

    for (s32 i = 0; i < ARRAY_SIZEU(starPaneNames); i++) {
        if (i < accessor.getPowerStarNum()) {
            if (MR::hasPowerStarInCurrentStage(i + 1)) {
                if (i < accessor.getNormalScenarioNum()) {
                    MR::showPaneRecursive(this, starPaneNames[i]);
                } else if (accessor.getNormalScenarioNum() + extraStarCount < static_cast< s32 >(ARRAY_SIZEU(starPaneNames))) {
                    MR::showPaneRecursive(this, starPaneNames[accessor.getNormalScenarioNum() + extraStarCount]);
                    extraStarCount++;
                }
            } else if (i < accessor.getNormalScenarioNum()) {
                MR::showPaneRecursive(this, starPaneNames[i]);
                MR::hidePaneRecursive(this, starPicPaneNames[i]);
            }
        }

        if (isInvalidAstroDome) {
            break;
        }
    }

    MR::startPaneAnim(this, "Stars", "Star", 1);

    f32 frame = isInvalidAstroDome ? 0.0f : accessor.getNormalScenarioNum() + extraStarCount - 1;
    MR::setPaneAnimFrameAndStop(this, "Stars", frame, 1);
}

void PauseMenu::startPaneAnimWithoutButton(const char* pAnimName) {
    MR::startPaneAnim(this, "BG", pAnimName, 0);
    MR::startPaneAnim(this, "PicStagebase", pAnimName, 0);
    MR::startPaneAnim(this, "PicStagebase1", pAnimName, 0);
    MR::startPaneAnim(this, "Stars", pAnimName, 0);
    MR::startPaneAnim(this, "Counter", pAnimName, 0);
}

bool PauseMenu::isPaneAnimStoppedWithoutButton() const {
    return MR::isPaneAnimStopped(this, "BG", 0) && MR::isPaneAnimStopped(this, "PicStagebase", 0) &&
           MR::isPaneAnimStopped(this, "PicStagebase1", 0) && MR::isPaneAnimStopped(this, "Stars", 0) && MR::isPaneAnimStopped(this, "Counter", 0);
}

void PauseMenu::forceToWaitAllButton() {
    mUpperButton->forceToWait();

    if (mLowerButton != nullptr) {
        mLowerButton->forceToWait();
    }

    bool b = mLetterButton == nullptr || mLetterButton->isHidden();

    if (!b) {
        mLetterButton->forceToWait();
    }

    mSettingsButton->getButton()->forceToWait();
}

void PauseMenu::exeSelecting() {
    bool isPointingTrigger = mUpperButton->isPointingTrigger() || (mLowerButton != nullptr && mLowerButton->isPointingTrigger()) ||
                             (!(mLetterButton == nullptr || mLetterButton->isHidden()) && mLetterButton->isPointingTrigger());
    isPointingTrigger = isPointingTrigger || mSettingsButton->getButton()->isPointingTrigger();

    if (isPointingTrigger) {
        MR::startSystemSE("SE_SY_SELECT_PAUSE_ITEM");
    }

    bool isLetterHidden;

    if (mUpperButton->trySelect() || (mLowerButton != nullptr && mLowerButton->trySelect()) || (!(isLetterHidden = (mLetterButton == nullptr || mLetterButton->isHidden())) && mLetterButton->trySelect())) {
        setNerve(GET_NERVE(PauseMenu, PauseMenuNrvDecided));
        return;
    }

    if (mSettingsButton->getButton()->trySelect()) {
        setNerve(GET_NERVE(PauseMenu, PauseMenuNrvDecided));
        return;
    }

    bool isAnimStopped = isPaneAnimStoppedWithoutButton() && !mUpperButton->isAppearing() && (mLowerButton == nullptr || !mLowerButton->isAppearing()) &&
                         ((mLetterButton == nullptr || mLetterButton->isHidden()) || !mLetterButton->isAppearing());

    if (isAnimStopped && (MR::isPressedPause(0) || MR::isPressedMap(0))) {
        MR::startSystemSE("SE_SY_PAUSE_OFF");
        MR::startCSSound("CS_CLICK_CLOSE", nullptr, 0);
        setNerve(GET_NERVE(PauseMenu, PauseMenuNrvDisappear));
    }
}

void PauseMenu::exeDecided() {
    if (mUpperButton->mIsSelected) {
        if (mUpperButton->isTimingForSelectedSe()) {
            MR::startSystemSE("SE_SY_PAUSE_OFF");
            MR::startCSSound("CS_CLICK_CLOSE", nullptr, 0);
        }

        if (mUpperButton->isDecidedWait()) {
            setNerve(GET_NERVE(PauseMenu, PauseMenuNrvDisappear));
        }
    } else if (mLowerButton != nullptr && mLowerButton->mIsSelected) {
        if (mLowerButton->isTimingForSelectedSe()) {
            MR::startSystemSE("SE_SY_PAUSE_OFF");
            MR::startCSSound("CS_CLICK_CLOSE", nullptr, 0);
        }

        if (mLowerButton->isDecidedWait()) {
            if (::isInvalidBackAstroDome()) {
                setNerve(GET_NERVE(PauseMenu, PauseMenuNrvGameDataSave));
            } else {
                setNerve(GET_NERVE(PauseMenu, PauseMenuNrvConfirm));
            }
        }
    }
    // Settings: the menu drops in over the pause menu, which waits under it with its buttons ready.
    else if (mSettingsButton->getButton()->mIsSelected) {
        if (mSettingsButton->getButton()->isDecidedWait()) {
            NebulaSettingsMenu::requestOpen();
            forceToWaitAllButton();
            setNerve(GET_NERVE(PauseMenu, PauseMenuNrvSelecting));
        }
    }
    else {
        bool isLetterHidden = mLetterButton == nullptr || mLetterButton->isHidden();

        if (!isLetterHidden && mLetterButton->mIsSelected) {
            if (mLetterButton->isTimingForSelectedSe()) {
                MR::startSystemSE("SE_SY_FILE_SEL_UPPER_DECIDE");
                MR::startCSSound("CS_CLICK_CLOSE", nullptr, 0);
            }

            if (mLetterButton->isDecidedWait()) {
                setNerve(GET_NERVE(PauseMenu, PauseMenuNrvLuigiLetter));
            }
        }
    }
}

void PauseMenu::exeDisappear() {
    if (MR::isFirstStep(this)) {
        startPaneAnimWithoutButton("End");
        mUpperButton->disappear();

        if (mLowerButton != nullptr) {
            mLowerButton->disappear();
        }

        bool b = mLetterButton == nullptr || mLetterButton->isHidden();

        if (!b) {
            mLetterButton->disappear();
        }

        mSettingsButton->hide();
    }

    if (isPaneAnimStoppedWithoutButton() && mUpperButton->isHidden() && (mLowerButton == nullptr || mLowerButton->isHidden())) {
        bool b = mLetterButton == nullptr || mLetterButton->isHidden();

        if (b) {
            kill();
        }
    }
}

void PauseMenu::exeConfirm() {
    if (MR::isFirstStep(this)) {
        const char* pMessageId = ::isInvalidBackAstroDome() ? "PauseMenu_ConfirmEndGame" : "PauseMenu_ConfirmBackAstroDome";

        mConfirmWindow->appear(pMessageId, SysInfoWindow::Type_YesNo, SysInfoWindow::TextPos_Center, SysInfoWindow::MessageType_System);
        // Yes fades the screen out from a capture of it: the dim stays in that capture instead of lifting first.
        mConfirmWindow->setDimHandOver(SysInfoWindow::DimHandOver_Yes);
    }

    if (MR::isDead(mConfirmWindow)) {
        if (mConfirmWindow->isSelectedYes()) {
            MR::closeSystemWipeFadeWithCaptureScreen(::cBackSequenceFadeFrame);

            if (::isInvalidBackAstroDome()) {
                MR::requestChangeSceneTitle();
            } else {
                MR::requestChangeStageGoBackAstroDome();
            }
        } else {
            forceToWaitAllButton();
            setNerve(GET_NERVE(PauseMenu, PauseMenuNrvSelecting));
        }
    }
}

void PauseMenu::exeGameDataSave() {
    if (MR::isFirstStep(this)) {
        GameSequenceFunction::startGameDataSaveSequence(false, true);
        // Its question follows the save (exeConfirm).
        GameSequenceFunction::setGameDataSaveWindowFollows();
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    if (GameSequenceFunction::isSuccessSaveDataHandleSequence()) {
        setNerve(GET_NERVE(PauseMenu, PauseMenuNrvConfirm));
    } else {
        forceToWaitAllButton();
        setNerve(GET_NERVE(PauseMenu, PauseMenuNrvSelecting));
    }
}

void PauseMenu::exeLuigiLetter() {
    if (MR::isFirstStep(this)) {
        mLuigiLetter->appear();
    }

    if (MR::isDead(mLuigiLetter)) {
        forceToWaitAllButton();
        setNerve(GET_NERVE(PauseMenu, PauseMenuNrvSelecting));
    }
}
