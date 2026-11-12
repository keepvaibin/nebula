#include "Game/System/GameSequenceProgress.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/PlayerMissLeft.hpp"
#include "Game/System/FindingLuigiEventScheduler.hpp"
#include "Game/System/GalaxyCometScheduler.hpp"
#include "Game/System/GalaxyMoveArgument.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/LuigiLeftSupplier.hpp"
#include "Game/System/StarPointerOnOffController.hpp"
#include "Game/System/StorySequenceExecutor.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    const s32 sTimingPlayingTicoSE = 180;
};  // namespace

namespace {
    NEW_NERVE(GameSequenceProgressBooting, GameSequenceProgress, Booting);
    NEW_NERVE(GameSequenceProgressBoot, GameSequenceProgress, Boot);
    NEW_NERVE(GameSequenceProgressWaitGoToFirstScene, GameSequenceProgress, WaitGoToFirstScene);
    NEW_NERVE(GameSequenceProgressNormal, GameSequenceProgress, Normal);
    NEW_NERVE(GameSequenceProgressGalaxyMove, GameSequenceProgress, GalaxyMove);
    NEW_NERVE(GameSequenceProgressResetProcessing, GameSequenceProgress, ResetProcessing);
};  // namespace

GameSequenceProgress::GameSequenceProgress()
    : NerveExecutor("シーケンス進行"), mStarPointerOnOffController(), mStorySequenceExecutor(), mFindingLuigiEventScheduler(),
      mGalaxyCometScheduler(), mLuigiLeftSupplier(), mPlayerMissLeft(), mMinFrame(), mIsPlayTicoSound(), mIsCancelScenarioSelect(),
      mIsForceWipe(true), mOpenWipeDelay() {
    initNerve(GET_NERVE_ANON(GameSequenceProgressBooting));

    mStarPointerOnOffController = new StarPointerOnOffController();
    mStorySequenceExecutor = new StorySequenceExecutor();
    mFindingLuigiEventScheduler = new FindingLuigiEventScheduler();
    mLuigiLeftSupplier = new LuigiLeftSupplier();
}

void GameSequenceProgress::initAfterResourceLoaded() {
    mGalaxyCometScheduler = new GalaxyCometScheduler();

    mPlayerMissLeft = new PlayerMissLeft("ミス時のプレイヤー残機表示");
    mPlayerMissLeft->initWithoutIter();

    mFindingLuigiEventScheduler->initAfterResourceLoaded();
}

void GameSequenceProgress::update() {
    // The scene a start began is drawn from the next frame on (startScene runs a frame before the controller shows it,
    // and that frame draws the black intermission scene): the wipe that covered the load opens with the scene's first
    // frame, not the one before it, so a closed white wipe does not drop to one black frame.
    if (mOpenWipeDelay != 0 && --mOpenWipeDelay == 0) {
        MR::forceOpenSystemWipeFade();
    }

    updateNerve();
    mStarPointerOnOffController->update();
    mStorySequenceExecutor->update();

    if (mGalaxyCometScheduler != nullptr) {
        mGalaxyCometScheduler->update();
    }

    if (!isNerve(GET_NERVE_ANON(GameSequenceProgressResetProcessing)) && GameSystemFunction::isResetProcessing()) {
        setNerve(GET_NERVE_ANON(GameSequenceProgressResetProcessing));
    }

    if (mPlayerMissLeft != nullptr) {
        mPlayerMissLeft->movement();
        mPlayerMissLeft->calcAnim();
    }
}

void GameSequenceProgress::draw() {
    if (mPlayerMissLeft != nullptr) {
        mPlayerMissLeft->draw();
    }
}

void GameSequenceProgress::startScene() {
    mMinFrame = 0;
    mIsPlayTicoSound = false;

    if (MR::isCurrentScene(SceneKind_Boot)) {
        mStarPointerOnOffController->setStateToBase(this);
    } else if (MR::isStageFileSelect()) {
        mStarPointerOnOffController->setStateToBase(this);
    } else {
        mStarPointerOnOffController->setStateToBase(this);
        MR::startStarPointerModeGame(this);
    }

    GameSystemFunction::setPermissionToCheckInput(isScenePermittedUpdateInputStatus());

    if (MR::isCurrentScene(SceneKind_Game)) {
        mStorySequenceExecutor->setNerveSceneStart();

        if (MR::isStageFileSelect()) {
            MR::forceOpenWipeFade();
        }

        if (mIsForceWipe) {
            mOpenWipeDelay = 1;
        }

        if (!GameDataFunction::isPassedStoryEvent("スピン権利")) {
            MR::setPlayerSwingPermission(false);
        }
    }

    if (mPlayerMissLeft != nullptr) {
        mPlayerMissLeft->kill();
    }
}

void GameSequenceProgress::endScene() {
    if (isNerve(GET_NERVE_ANON(GameSequenceProgressBoot))) {
        MR::requestChangeSceneAfterBoot();

        GameSystemFunction::setResetOperationApplicationReset();

        setNerve(GET_NERVE_ANON(GameSequenceProgressWaitGoToFirstScene));
    } else if (!isNerve(GET_NERVE_ANON(GameSequenceProgressWaitGoToFirstScene))) {
        MR::requestChangeSceneTitle();
    }
}

void GameSequenceProgress::requestChangeScene(SceneKind kind) {
    updateGameDataBeforeChangeScene();

    GameSystemSceneController* pSceneController = SingletonHolder< GameSystem >::get()->mSceneController;
    pSceneController->mNextSceneControlInfo.mScene = kind;
    pSceneController->requestChangeScene();

    mStarPointerOnOffController->setStateToBase(this);
    mStorySequenceExecutor->forceStop();
}

void GameSequenceProgress::requestGalaxyMove(const GalaxyMoveArgument& rMoveArg) {
    updateGameDataGalaxyVisitedFlag();
    GameSequenceFunction::storeSceneStartGameDataHolder();

    if (rMoveArg.mMoveType == GalaxyMoveArgument::Type_AfterStageClear) {
        GameSequenceFunction::updateGameDataAndSequenceAfterStageResultSequence();
        mFindingLuigiEventScheduler->updateOnStageResult(GameSequenceFunction::getClearedStageName(), GameSequenceFunction::getClearedPowerStarId());

        countDownGameEventValueFromNewPowerStar();
    }

    mFindingLuigiEventScheduler->update(rMoveArg);

    GalaxyMoveArgument moveArg(rMoveArg);
    mStorySequenceExecutor->moveGalaxy(&moveArg, isNerve(GET_NERVE_ANON(GameSequenceProgressResetProcessing)) || mIsCancelScenarioSelect);

    mGalaxyCometScheduler->syncWithFlags();

    updateGameDataBeforeChangeScene();
    setMinFrameBeforeStartNextStage(moveArg);

    GameSystemSceneController* pController = SingletonHolder< GameSystem >::get()->mSceneController;
    pController->mNextSceneControlInfo.mScene = SceneKind_Game;
    pController->mNextSceneControlInfo.setStage(moveArg.mStageName);
    pController->mNextSceneControlInfo.mScenarioNo = moveArg.mScenarioNo;
    pController->mNextSceneControlInfo.mSelectedScenarioNo = moveArg.mSelectedScenarioNo;
    pController->mNextSceneControlInfo.setStartIdInfo(moveArg.mIDInfo);
    pController->requestChangeScene();

    resetGameDataAfterChangeScene(moveArg);

    switch (moveArg.mMoveType) {
    case GalaxyMoveArgument::Type_ScenarioSelect:
        // The leaving scene's own wipe goes with it, and the intermission scene is black until the select screen
        // fades in: a wipe it left closed carries on as the system wipe, so no black frame shows between.
        MR::holdClosedSceneWipeOnSystemWipe();
        SingletonHolder< GameSystem >::get()->mSceneController->startScenarioSelectScene();
        MR::setStarPointerModeBase();

        break;
    case GalaxyMoveArgument::Type_Title:
        SingletonHolder< GameSystem >::get()->mSceneController->startScenarioSelectSceneBackground();
        mStarPointerOnOffController->setStateToTitle(this);
        break;
    case GalaxyMoveArgument::Type_AfterMiss:
        if (mPlayerMissLeft != nullptr && !isCometStar(moveArg)) {
            mPlayerMissLeft->appear();
        }
        [[fallthrough]];
    case GalaxyMoveArgument::Type_InGame:
    case GalaxyMoveArgument::Type_GoBackAstroDome:
    case GalaxyMoveArgument::Type_AfterStageClear:
    case GalaxyMoveArgument::Type_AfterLoadingGameData:
        SingletonHolder< GameSystem >::get()->mSceneController->startScenarioSelectSceneBackground();
        mStarPointerOnOffController->setStateToBase(this);

        if (moveArg.mMoveType == GalaxyMoveArgument::Type_AfterLoadingGameData) {
            mLuigiLeftSupplier->syncWithFlags();
        }

        break;
    }

    mIsForceWipe = true;

    if (moveArg.mMoveType == GalaxyMoveArgument::Type_ScenarioSelect || MR::isEqualString(moveArg.mStageName, "EpilogueDemoStage")) {
        mIsForceWipe = false;
    }

    setNerve(GET_NERVE_ANON(GameSequenceProgressGalaxyMove));
}

void GameSequenceProgress::requestCancelScenarioSelect() {
    mIsCancelScenarioSelect = true;
}

GalaxyCometScheduler* GameSequenceProgress::getGalaxyCometScheduler() {
    return mGalaxyCometScheduler;
}

void GameSequenceProgress::exeBooting() {

    if (GameSequenceFunction::isReadyToStartScene()) {
        setNerve(GET_NERVE_ANON(GameSequenceProgressBoot));
    }
}

void GameSequenceProgress::exeBoot() {
    if (MR::isFirstStep(this)) {
        startScene();
        GameSequenceFunction::startScene();

        GameSystemFunction::tryToLoadSystemArchive();
    }
}

void GameSequenceProgress::exeWaitGoToFirstScene() {
    if (GameSequenceFunction::isReadyToStartScene()) {
        setNerve(GET_NERVE_ANON(GameSequenceProgressNormal));
    }
}

void GameSequenceProgress::exeNormal() {
    if (MR::isFirstStep(this)) {
        if (mIsCancelScenarioSelect) {
            requestGalaxyMove(GalaxyMoveArgument(GalaxyMoveArgument::Type_GoBackAstroDome, nullptr, 1, nullptr));
            mIsCancelScenarioSelect = false;
        } else {
            startScene();
            GameSequenceFunction::startScene();
        }
    } else if (GameSequenceFunction::isReadyToStartScene()) {
        setNerve(GET_NERVE_ANON(GameSequenceProgressNormal));
    }
}

void GameSequenceProgress::exeGalaxyMove() {
    if (mIsPlayTicoSound) {
        if (MR::isStep(this, ::sTimingPlayingTicoSE)) {
            MR::startSystemSE("SE_SY_TICO_WAKE_PLAYER");
        }
    }

    if (MR::isGreaterStep(this, mMinFrame) && GameSequenceFunction::isReadyToStartScene() && MR::isDead(mPlayerMissLeft)) {
        setNerve(GET_NERVE_ANON(GameSequenceProgressNormal));
    }
}

void GameSequenceProgress::exeResetProcessing() {
    if (MR::isFirstStep(this)) {
        mIsCancelScenarioSelect = false;
        mMinFrame = 0;
        mIsPlayTicoSound = false;

        mStorySequenceExecutor->forceStop();

        if (mPlayerMissLeft != nullptr) {
            mPlayerMissLeft->kill();
        }

    }

    if (!GameSystemFunction::isResetProcessing()) {
        MR::requestChangeSceneTitle();
        setNerve(GET_NERVE_ANON(GameSequenceProgressWaitGoToFirstScene));
    }
}

bool GameSequenceProgress::isScenePermittedUpdateInputStatus() {
    return !MR::isCurrentScene(SceneKind_Boot);
}

bool GameSequenceProgress::isCometStar(GalaxyMoveArgument moveArg) {
    GalaxyStatusAccessor accessor = MR::makeGalaxyStatusAccessor(moveArg.mStageName);
    return accessor.isCometStar(moveArg.mScenarioNo);
}

void GameSequenceProgress::updateGameDataBeforeChangeScene() {
    if (GameSequenceFunction::hasStageResultSequence() && GameSequenceFunction::isPowerStarAtResultSequence("KoopaBattleVs3Galaxy", 1)) {
        GameSequenceFunction::reflectStageResultSequenceCoin();

        GameDataFunction::addStockedStarPiece(GameSequenceFunction::getClearedStarPieceNum());

        GameSequenceFunction::resetStageResultSequenceParam();
    }
}

void GameSequenceProgress::resetGameDataAfterChangeScene(const GalaxyMoveArgument& rMoveArg) {
    s32 starPieceNum = GameDataFunction::getStarPieceNum();
    s32 last1upStarPieceNum = GameDataFunction::getLast1upStarPieceNum();

    switch (rMoveArg.mMoveType) {
    case GalaxyMoveArgument::Type_AfterLoadingGameData:
        mGalaxyCometScheduler->restoreStateFromGameData();
        [[fallthrough]];
    case GalaxyMoveArgument::Type_InGame:
    case GalaxyMoveArgument::Type_ScenarioSelect:
    case GalaxyMoveArgument::Type_GoBackAstroDome:
    case GalaxyMoveArgument::Type_Title:
        GameDataFunction::resetGameDataGoToGalaxyFirst();

        break;
    case GalaxyMoveArgument::Type_AfterMiss:
        GameDataFunction::resetGameDataGoToGalaxyRetry();

        break;
    }

    if (rMoveArg.isEqualStageScenario("HeavensDoorGalaxy", 1) && mStorySequenceExecutor->isEqualStageScenarioBefore("PeachCastleGardenGalaxy", 1) &&
        rMoveArg.mMoveType == GalaxyMoveArgument::Type_InGame) {
        GameDataFunction::addStarPiece(starPieceNum);
        GameDataFunction::setLast1upStarPieceNum(last1upStarPieceNum);
    }
}

void GameSequenceProgress::updateGameDataGalaxyVisitedFlag() {
    if (MR::isCurrentScene(SceneKind_Game) && !mIsCancelScenarioSelect && !isNerve(GET_NERVE_ANON(GameSequenceProgressResetProcessing))) {
        const char* pStageName = MR::getCurrentStageName();
        s32 scenarioNo = MR::getCurrentScenarioNo();
        GameDataFunction::onGalaxyScenarioFlagAlreadyVisited(pStageName, scenarioNo);
    }
}

void GameSequenceProgress::setMinFrameBeforeStartNextStage(const GalaxyMoveArgument& rMoveArg) {
    if (MR::isCurrentScene(SceneKind_Game) && MR::isEqualStageName("PeachCastleGardenGalaxy") &&
        MR::isEqualString(rMoveArg.mStageName, "HeavensDoorGalaxy") && rMoveArg.mScenarioNo == 1) {
        mMinFrame = NebulaFrameClock::stepsOfSeconds(5);
        mIsPlayTicoSound = true;
    }
}

bool GameSequenceProgress::isLuigiLeftSupply() {
    return mLuigiLeftSupplier->mIsLuigiLeft;
}

void GameSequenceProgress::offLuigiLeftSupply() {
    mLuigiLeftSupplier->mIsLuigiLeft = false;
}

void GameSequenceProgress::countDownGameEventValueFromNewPowerStar() {
    if (!GameSequenceFunction::hasStageResultSequence()) {
        return;
    }

    if (GameSequenceFunction::hasPowerStarYetAtResultSequence()) {
        return;
    }

    for (GameEventFlagIter iter = GameEventFlagTable::getBeginIter(); !iter.isEnd(); iter.goNext()) {
        GameEventFlagAccessor accessor(iter.getFlag());

        if (!accessor.isTypeEventValueIsZero()) {
            continue;
        }

        if (GameDataFunction::canOnGameEventFlag(accessor.getName())) {
            continue;
        }

        if (!GameDataFunction::isOnGameEventFlag(accessor.getRequirement())) {
            continue;
        }

        if (GameDataFunction::isOnJustGameEventFlag(accessor.getRequirement())) {
            continue;
        }

        u32 eventValue = GameDataFunction::getGameEventValue(accessor.getEventValueName());
        GameDataFunction::setGameEventValue(accessor.getEventValueName(), eventValue - 1);
    }
}
