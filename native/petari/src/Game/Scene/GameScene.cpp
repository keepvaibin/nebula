#include "Game/Scene/GameScene.hpp"
#include "Game/System/NebulaKernel.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#ifdef TARGET_PC
#include "Game/Util/GameLightSignal.hpp"
#include "Game/Util/MotionSignal.hpp"
#endif
#ifdef TARGET_PC
#include "Game/System/GameDataFunction.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameEventFlagChecker.hpp"
#include "Game/System/GameEventFlagStorage.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include <cstring>
#include "Game/Screen/NebulaSettingsMenu.hpp"
#include "Game/System/NebulaFrameGen.hpp"
#include <nebula_game_api.h>
#include <revolution/os.h>
#endif
#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/SleepControllerHolder.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/NPC/EventDirector.hpp"
#include "Game/NPC/NPCFunction.hpp"
#include "Game/NPC/TalkDirector.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Scene/GameScenePauseControl.hpp"
#include "Game/Scene/GameSceneScenarioOpeningCameraState.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneNameObjMovementController.hpp"
#include "Game/Screen/CometRetryButton.hpp"
#include "Game/Screen/GamePauseSequence.hpp"
#include "Game/Screen/GameStageClearSequence.hpp"
#include "Game/Screen/LensFlare.hpp"
#include "Game/Screen/MoviePlayingSequence.hpp"
#include "Game/Screen/ScreenAlphaCapture.hpp"
#include "Game/System/GalaxyMapController.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/JMapIdInfo.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#ifdef TARGET_PC
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/NPC/NebulaAchievementLuma.hpp"
#include "Game/Screen/NebulaAchievementBoard.hpp"
#include "Game/Screen/NebulaAchievementToast.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/Util/MessageUtil.hpp"
#endif
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include "Game/Util/RenderSignals.hpp"
#include "Game/System/NebulaHost.hpp"

namespace {
    CometRetryButton* getCometRetryButton() {
        return MR::getSceneObj< CometRetryButton >(SceneObj_CometRetryButton);
    }
};  // namespace

namespace NrvGameScene {
    NEW_NERVE(GameSceneScenarioOpeningCamera, GameScene, ScenarioOpeningCamera);
    NEW_NERVE(GameSceneScenarioStarter, GameScene, ScenarioStarter);
    NEW_NERVE(GameSceneAction, GameScene, SceneAction);
    NEW_NERVE(GameScenePauseMenu, GameScene, PauseMenu);
    NEW_NERVE(GameScenePowerStarGet, GameScene, PowerStarGet);
    NEW_NERVE(GameSceneGrandStarGet, GameScene, GrandStarGet);
    NEW_NERVE(GameSceneGameOver, GameScene, GameOver);
    NEW_NERVE(GameSceneCometRetryAfterMiss, GameScene, CometRetryAfterMiss);
    NEW_NERVE(GameSceneSaveAfterGameOver, GameScene, SaveAfterGameOver);
    NEW_NERVE(GameScenePlayMovie, GameScene, PlayMovie);
    NEW_NERVE(GameSceneTimeUp, GameScene, TimeUp);
    NEW_NERVE(GameSceneGalaxyMap, GameScene, GalaxyMap);
    NEW_NERVE(GameSceneStaffRoll, GameScene, StaffRoll);
#ifdef TARGET_PC
    NEW_NERVE(GameSceneNebulaBoard, GameScene, NebulaBoard);
#endif
};  // namespace NrvGameScene

GameScene::GameScene()
    : Scene("GameScene"), _14(0), mScenarioCamera(nullptr), mPauseCtrl(nullptr), mPauseSeq(nullptr), mStageClearSeq(nullptr), mDraw3D(true), _29(1) {
}

GameScene::~GameScene() {
    MR::destroySceneMessage();
    NPCFunction::deleteNPCData();
    MR::onStarPointerSceneOut();
}

void GameScene::init() {
    SceneFunction::createHioBasicNode(this);
    SceneFunction::startStageFileLoad();
    MR::requestChangeArchivePlayer(MR::isPlayerLuigi() == false);
    initNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera));
    SceneFunction::initForNameObj();
    SceneFunction::initForLiveActor();
    initEffect();
    MR::createSceneObj(SceneObj_CameraContext);
    MR::createSceneObj(SceneObj_NameObjGroup);
    MR::createSceneObj(SceneObj_PlanetGravityManager);
    MR::createSceneObj(SceneObj_MarioHolder);
    MR::createSceneObj(SceneObj_AudCameraWatcher);
    MR::createSceneObj(SceneObj_AudEffectDirector);
    MR::createSceneObj(SceneObj_AudBgmConductor);
    MR::createSceneObj(SceneObj_ResourceShare);
    MR::createSceneObj(SceneObj_EventSequencer);
    MR::createSceneObj(SceneObj_ScenePlayingResult);
    MR::createSceneObj(SceneObj_FurDrawManager);
    MR::createSceneObj(SceneObj_PlacementStateChecker);
    MR::createScreenAlphaSceneObj(0, 1.0f);
    MR::createSceneObj(SceneObj_GroupCheckManager);
    MR::createSceneObj(SceneObj_CinemaFrame);
    MR::createSceneObj(SceneObj_PlanetMapCreator);

    if (MR::isStageEpilogueDemo()) {
        MR::createSceneObj(SceneObj_StaffRoll);
    }

    if (MR::isStageEpilogueDemo() || GameSequenceFunction::isNeedMoviePlayerForStorySequenceEvent()) {
        MR::createMoviePlayingSequence();
    }

    SceneFunction::waitDoneStageFileLoad();
    MR::waitEndChangeArchivePlayer();
    SceneFunction::startActorFileLoadCommon();
    MR::suspendAsyncExecuteThread("\203\126\201\133\203\223\217\211\212\372\211\273" /* シーン初期化 */);

    if (!MR::isScenarioDecided()) {
        MR::receiveAllRequestedFile();
    } else {
        SceneFunction::initAfterScenarioSelected();
        NPCFunction::createNPCData();
        SceneFunction::startActorFileLoadScenario();
        MR::createSceneObj(SceneObj_EventDirector);
        MR::initSceneMessage();
        MR::createSceneObj(SceneObj_CameraDirector);
        MR::createSceneObj(SceneObj_GameSceneLayoutHolder);
        MR::createSceneObj(SceneObj_StarPieceDirector);
        MR::createSceneObj(SceneObj_SceneWipeHolder);
        MR::createSceneObj(SceneObj_NamePosHolder);
        MR::createSceneObj(SceneObj_InformationObserver);

        if (MR::isGalaxyAnyCometAppearInCurrentStage()) {
            MR::createSceneObj(SceneObj_CometRetryButton);
        }

        LightFunction::initLightData();
        initSequences();
        SceneFunction::startActorPlacement();
#ifdef TARGET_PC
        // Nebula's Achievement Luma joins the Observatory's own actors.
        NebulaAchievementLuma::tryCreate();
#endif
        GameSceneFunction::loadScenarioWaveData();
        MR::createStarPiece();
        MR::completeCameraParameters();
        MR::initStarPointerGameScene();
        MR::initEventSystemAfterPlacement();
        MR::endInitLiveActorSystemInfo();
        MR::setInitializeStateAfterPlacement();
        MR::callMethodAllSceneNameObj(&NameObj::initAfterPlacement);
        SleepControlFunc::initSyncSleepController();

        while (!GameSceneFunction::isLoadDoneScenarioWaveData()) {
#ifdef TARGET_PC
            NebulaKernel::poll();
#endif
        }
    }
}

#ifdef TARGET_PC
namespace {
    // The achievement board asked for by the Achievement Luma: it opens once the scene is in plain play, not paused,
    // in a demo or behind a wipe; a scene that starts drops the request.
    bool sBoardRequested = false;
    // Tests (GALAXY_TEST_EVENT_FLAGS=AchievementBoard): asked for in the first scene with a board, past the restart the
    // test's Grand Stars cause.
    bool sTestBoard = false;

    // Discord (__NebulaPresence): the galaxy and its star in the game's own words, and the Power Stars collected.
    void reportPresence() {
        const char* pStage = MR::getCurrentStageName();
        const s32 scenario = MR::getCurrentScenarioNo();
        const wchar_t* pGalaxy = MR::getGalaxyNameOnCurrentLanguage(pStage);
        const wchar_t* pStar = scenario > 0 ? MR::getScenarioNameOnCurrentLanguage(pStage, scenario) : nullptr;
        NebulaHost::presence(reinterpret_cast< const u16* >(pGalaxy), reinterpret_cast< const u16* >(pStar), MR::getPowerStarNum());
    }
};  // namespace
#endif

void GameScene::start() {
    AudWrap::getSceneMgr()->startScene();
#ifdef TARGET_PC
    sBoardRequested = false;
    reportPresence();
    NebulaAchievements::evaluate();
#endif

    if (MR::isGlobalTimerEnd()) {
        MR::forceCloseWipeCircle();
    } else if (!MR::hasRetryGalaxySequence()) {
        startStagePlayFirst();
    } else {
        startStagePlayRetry();
    }
}

void GameScene::update() {
    mPauseCtrl->updateNerve();
    updateNerve();
#ifdef TARGET_PC
    NebulaAchievements::update();
    if (sTestBoard && MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mAchievementBoard != nullptr) {
        sTestBoard = false;
        requestShowNebulaBoard();
    }
    if (sBoardRequested && isNerve(GET_NERVE(GameScene, GameSceneAction)) && MR::isWipeOpen() && !MR::isWipeActive() &&
        !MR::isDemoActive()) {
        sBoardRequested = false;
        setNerve(GET_NERVE(GameScene, GameSceneNebulaBoard));
    }
    // Nebula's achievement pop-up shows as soon as one is earned, in play, cutscenes, menus and a star's get sequence
    // alike (the settings menu and the HOME Menu hold the whole scene, the pop-up with it).
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mAchievementToast != nullptr) {
        MR::getGameSceneLayoutHolder()->mAchievementToast->update();
    }
#endif

    bool isTimeUp = MR::isGlobalTimerEnd() && !isNerve(GET_NERVE(GameScene, GameSceneTimeUp)) && MR::isGreaterEqualStep(this, 2);

    if (isTimeUp) {
        setNerve(GET_NERVE(GameScene, GameSceneTimeUp));
    }
}

void GameScene::draw() const {
#ifdef TARGET_PC
    // DLSS Frame Generation while the scene shows play: its own play, its openings and star demos, and no menu,
    // map, staff roll, movie or settings over it, nor the title and file select.
    NebulaFrameGen::report(!MR::isStageFileSelect() && !MR::isActiveMoviePlayer() && !NebulaSettingsMenu::isShown() &&
                           (isNerve(GET_NERVE(GameScene, GameSceneAction)) ||
                            isNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera)) ||
                            isNerve(GET_NERVE(GameScene, GameSceneScenarioStarter)) ||
                            isNerve(GET_NERVE(GameScene, GameScenePowerStarGet)) ||
                            isNerve(GET_NERVE(GameScene, GameSceneGrandStarGet))));
    // Where the camera is in water, how deep: ray tracing lights the water it looks through.
    {
        const f32 depth = MR::isCameraInWater() ? WaterAreaFunction::getCameraWaterDepth() : -1.0f;
        NebulaHost::gameContext(NEBULA_CONTEXT_CAMERA_WATER, depth < 0.0f ? 0 : static_cast< s32 >(depth) + 1);
    }
    // Hidden test runs (the host's GALAXY_TEST_GALAXY_TOUR and GALAXY_TEST_TELEPORT): through the game's own calls.
    // A star taken in a test waits for play (the galaxy's opening camera and starter end in SceneAction, and would
    // replace the get demo); then what PowerStar::exeStageClearDemo does when the player takes it.
    static u32 sPendingStar;
    static s32 sPendingStarId;
    if (sPendingStar != 0 && isNerve(GET_NERVE(GameScene, GameSceneAction))) {
        MR::sendStageResultSequenceParam(sPendingStarId);
        sPendingStar == NEBULA_TEST_GET_GRAND_STAR ? MR::requestGrandStarGetDemo() : MR::requestPowerStarGetDemo();
        sPendingStar = 0;
    }
    // The settings' galaxy select (its ADVANCED page) names its galaxies and stars in the game's own words, asked once.
    if (NebulaHost::galaxySelectNamesWanted() != 0) {
        char id[64];
        u32 stars = 0;
        for (u32 i = 0; NebulaHost::galaxySelectStage(i, id, sizeof(id), &stars) != 0; ++i) {
            NebulaHost::galaxySelectName(i, 0, reinterpret_cast< const u16* >(MR::getGalaxyNameOnCurrentLanguage(id)));
            for (u32 star = 1; star <= stars; ++star) {
                NebulaHost::galaxySelectName(i, star,
                                         reinterpret_cast< const u16* >(MR::getScenarioNameOnCurrentLanguage(id, star)));
            }
        }
    }
    NebulaTestAction action;
    if (NebulaHost::testSceneFrame(&action)) {
        static char sStage[sizeof(action.stage)];  // the stage change keeps a pointer to the name
        if (action.kind == NEBULA_TEST_CHANGE_STAGE) {
            strcpy(sStage, action.stage);
            MR::requestChangeStageInGameMoving(sStage, action.scenario);
        } else if (action.kind == NEBULA_TEST_PLACE_PLAYER) {
            MR::setPlayerPos(TVec3f(action.position[0], action.position[1], action.position[2]));
        } else if (action.kind == NEBULA_TEST_KILL_PLAYER) {
            MR::forceKillPlayerByAbyss();
        } else if (action.kind == NEBULA_TEST_GET_STAR || action.kind == NEBULA_TEST_GET_GRAND_STAR) {
            sPendingStar = action.kind;
            sPendingStarId = action.scenario;
        } else if (action.kind == NEBULA_TEST_SET_PLAYER) {
            // GameDataHolder::isDataMario reads the file's name ("mario1" or "luigi1").
            snprintf(GameDataFunction::getCurrentGameDataHolder()->mName, sizeof(GameDataHolder::mName), "%s1",
                     action.scenario != 0 ? "luigi" : "mario");
        } else if (action.kind == NEBULA_TEST_SET_GRAND_STARS) {
            // Grand Star n is the first star of its galaxy (SpecialStarGrand1-7 derive from these).
            static const char* const kGrandStarGalaxies[] = {"HeavensDoorGalaxy",    "TriLegLv1Galaxy",
                                                             "KoopaBattleVs1Galaxy", "KoopaJrShipLv1Galaxy",
                                                             "KoopaBattleVs2Galaxy", "FloaterOtaKingGalaxy",
                                                             "KoopaBattleVs3Galaxy"};
            for (s32 i = 0; i < action.scenario && i < 7; ++i) {
                GameDataFunction::setGameFlagPowerStarSuccess(kGrandStarGalaxies[i], 1, true);
            }
            // The scene's domes and their parts take their state from the save as it is when they are built
            // (AstroMapObjFunction::isAlreadyRevival), so the stage starts again with these stars: set only at the
            // scene's second frame, every dome would stay a shell without its pillars, lamps and glass top, a state
            // play never reaches. The galaxy select names the Observatory; tests start the stage played again.
            if (action.stage[0] != '\0') {
                strcpy(sStage, action.stage);
                MR::requestChangeStageInGameMoving(sStage, 1);
            } else {
                strcpy(sStage, MR::getCurrentStageName());
                MR::requestChangeStageInGameMoving(sStage, MR::getCurrentScenarioNo());
            }
        } else if (action.kind == NEBULA_TEST_SET_EVENT_FLAG) {
            // A stored flag on, past its prerequisites (tryOn checks them; flags the game derives from others, such as
            // the storybook's chapters, follow those). name=value: an event value (LibraryOpenNewStarCount=0 with
            // Grand Star 3 opens the Library and its chapter 1); PictureBookAlreadyRead=n: the chapters Rosalina has
            // read (the Library's book lists those).
            static const char kRead[] = "PictureBookAlreadyRead=";
            const char* equals = strchr(action.stage, '=');
            static const char kAchievement[] = "Achievement:";
            if (strcmp(action.stage, "AchievementBoard") == 0) {
                // The Achievement Luma's board, as if it had been asked (in the Observatory), at the next update.
                sTestBoard = true;
            } else if (strncmp(action.stage, kAchievement, sizeof(kAchievement) - 1) == 0) {
                // Its pop-up, unlocked or not (pictures of every icon and name).
                NebulaAchievements::showForTest(action.stage + sizeof(kAchievement) - 1);
            } else if (strncmp(action.stage, kRead, sizeof(kRead) - 1) == 0) {
                MR::setPictureBookChapterAlreadyRead(atoi(action.stage + sizeof(kRead) - 1));
            } else if (strcmp(action.stage, "LuigiFile") == 0) {
                // The file played as a Luigi file from here (its name; the stage built next reads it).
                snprintf(GameDataFunction::getCurrentGameDataHolder()->mName, sizeof(GameDataHolder::mName), "luigi1");
            } else if (strcmp(action.stage, "SpinAndStarPointer") == 0) {
                // The story's grant of the spin and the pointer (the Gateway's Luma), which an early save lacks.
                MR::onGameEventFlagEnableToSpinAndStarPointer();
            } else if (equals != nullptr) {
                static char sName[sizeof(action.stage)];
                snprintf(sName, sizeof(sName), "%.*s", (int)(equals - action.stage), action.stage);
                GameDataFunction::setGameEventValue(sName, (u16)atoi(equals + 1));
            } else if (const GameEventFlag* pFlag = GameEventFlagTable::findFlag(action.stage)) {
                GameDataFunction::getCurrentGameDataHolder()->mEventFlagChecker->getChunk()->set(pFlag, true);
            } else {
                JUTNativeLog("test: no event flag %s\n", action.stage);
            }
        } else if (action.kind == NEBULA_TEST_LIST_ACTORS) {
            AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
            for (s32 i = 0; pGroup != nullptr && i < pGroup->getObjNum(); ++i) {
                LiveActor* pActor = pGroup->getActor(i);
                if (pActor == nullptr || pActor->mModelManager == nullptr || MR::isDead(pActor)) {
                    continue;
                }
                // With the animation it plays (none: no BCK started), its frame and rate; whether its model is shown;
                // whether the game clips it, and whether out of view it is kept unclipped for ray-traced light
                // (ClippingActorInfo::isWantedForLight).
                const char* pBck = MR::getPlayingBckName(pActor);
                const ClippingActorInfo* pInfo = MR::getClippingDirector()->mActorHolder->find(pActor);
                JUTNativeLog("actor %s %.0f %.0f %.0f bck=%s frame=%.1f rate=%.2f stopped=%d hidden=%d clipped=%d caster=%d\n",
                         MR::getModelResName(pActor), pActor->mPosition.x, pActor->mPosition.y, pActor->mPosition.z,
                         pBck != nullptr ? pBck : "none", pBck != nullptr ? MR::getBckFrame(pActor) : 0.0f,
                         pBck != nullptr ? MR::getBckRate(pActor) : 0.0f, pBck != nullptr && MR::isBckStopped(pActor) ? 1 : 0,
                         MR::isHiddenModel(pActor) ? 1 : 0, MR::isClipped(pActor) ? 1 : 0,
                         pInfo != nullptr && pInfo->mKeptForLight ? 1 : 0);
            }
        } else if (action.kind == NEBULA_TEST_KILL_ACTOR) {
            // Killed as their defeat ends (LiveActor::kill): what they leave appears, a KuriboChief's key.
            AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
            for (s32 i = 0; pGroup != nullptr && i < pGroup->getObjNum(); ++i) {
                LiveActor* pActor = pGroup->getActor(i);
                if (pActor != nullptr && pActor->mModelManager != nullptr && !MR::isDead(pActor) &&
                    MR::isEqualStringCase(MR::getModelResName(pActor), action.stage)) {
                    JUTNativeLog("test: kill %s %.0f %.0f %.0f\n", action.stage, pActor->mPosition.x, pActor->mPosition.y,
                             pActor->mPosition.z);
                    pActor->kill();
                }
            }
        } else if (action.kind == NEBULA_TEST_SHAKE_CAMERA) {
            // The shaker keeps its owner only to stop it; nothing stops this one.
            MR::shakeCameraInfinity(nullptr, 0.2f, 2.0f);
        } else if (action.kind == NEBULA_TEST_START_MOVIE) {
            if (MR::isExistSceneObj(SceneObj_MoviePlayingSequenceHolder)) {
                MR::startMovie(action.scenario);
            } else {
                JUTNativeLog("test: no movie sequence in this stage\n");
            }
        }
    }
#endif
    MR::drawInit();
    LightFunction::initLightRegisterAll();
    CategoryList::execute(MR::DrawType_MiiFaceIcon);
    CategoryList::execute(MR::DrawType_MiiFaceNew);
    drawMirror();
    draw3D();
    draw2D();
    MR::reinitGX();
}

void GameScene::calcAnim() {
    if (isPlayMovie()) {
        SceneFunction::executeCalcAnimListOnPlayingMovie();
    } else {
        SceneFunction::executeCalcAnimList();
    }

    if (!isNerve(GET_NERVE(GameScene, GameSceneTimeUp))) {
        CategoryList::execute(MR::CalcAnimType_AnimParticleIgnorePause);
    }

    SceneFunction::executeCalcViewAndEntryList();
}

void GameScene::notifyEndScenarioStarter() {
    setNerve(GET_NERVE(GameScene, GameSceneAction));
}

void GameScene::requestPlayMovieDemo() {
    setNerve(GET_NERVE(GameScene, GameScenePlayMovie));
}

void GameScene::requestStartGameOverDemo() {
    if (!isNerve(GET_NERVE(GameScene, GameSceneGameOver))) {
        setNerve(GET_NERVE(GameScene, GameSceneGameOver));
    }
}

void GameScene::requestEndGameOverDemo() {
    setNerve(GET_NERVE(GameScene, GameSceneSaveAfterGameOver));
}

void GameScene::requestEndMissDemo() {
    if (MR::isExistSceneObj(SceneObj_CometRetryButton)) {
        setNerve(GET_NERVE(GameScene, GameSceneCometRetryAfterMiss));
    } else {
        MR::requestChangeStageAfterMiss();
    }
}

void GameScene::requestPowerStarGetDemo() {
    setNerve(GET_NERVE(GameScene, GameScenePowerStarGet));
}

void GameScene::requestGrandStarGetDemo() {
#ifdef TARGET_PC
    NebulaAchievements::onGrandStarGet();
#endif
    setNerve(GET_NERVE(GameScene, GameSceneGrandStarGet));
}

void GameScene::setNerveAfterPauseMenu() {
    setNerve(GET_NERVE(GameScene, GameSceneAction));
}

bool GameScene::isExecScenarioOpeningCamera() const {
    return isNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera));
}

bool GameScene::isExecScenarioStarter() const {
    return isNerve(GET_NERVE(GameScene, GameSceneScenarioStarter));
}

bool GameScene::isExecStageClearDemo() const {
    return isNerve(GET_NERVE(GameScene, GameScenePowerStarGet)) || isNerve(GET_NERVE(GameScene, GameSceneGrandStarGet));
}

void GameScene::exeScenarioOpeningCamera() {
    mScenarioCamera->update();
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();

    if (mScenarioCamera->isDone()) {
        if (!MR::isBeginScenarioStarter()) {
            MR::activateDefaultGameLayout();
            MR::tryFrameToScreenCinemaFrame();
            MR::stopSubBGM(0);
            MR::stopStageBGM(0);
            MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
            setNerve(GET_NERVE(GameScene, GameSceneAction));
        } else {
            setNerve(GET_NERVE(GameScene, GameSceneScenarioStarter));
        }
    }
}

void GameScene::exeCometRetryAfterMiss() {
    CometRetryButton* pCometRetryButton = ::getCometRetryButton();

    if (MR::isFirstStep(this)) {
        ::getCometRetryButton()->appear();
        MR::forceOpenWipeCircle();
    }

    pCometRetryButton->movement();
}

void GameScene::exeSaveAfterGameOver() {
    if (MR::isFirstStep(this)) {
        MR::incPlayerGameOverNum();
        GameSequenceFunction::startGameDataSaveSequence(true, true);
        MR::startStarPointerModePauseMenu(this);
    }

    if (!GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        MR::endStarPointerMode(this);
        MR::requestChangeSceneAfterGameOver();
    }
}

void GameScene::exePlayMovie() {
    if (!MR::isActiveMoviePlayer() && !MR::isMoviePlayingOnSequence()) {
        setNerve(GET_NERVE(GameScene, GameSceneAction));
        SceneFunction::movementStopSceneController();
        SceneFunction::executeMovementList();
    } else {
        SceneFunction::executeMovementListOnPlayingMovie();
    }
}

void GameScene::exeGalaxyMap() {
    if (MR::isFirstStep(this)) {
        MR::startStarPointerModePauseMenu(this);
    }

    if (!MR::isActiveGalaxyMapLayout()) {
        MR::endStarPointerMode(this);
        setNerve(GET_NERVE(GameScene, GameSceneAction));
    } else {
        CategoryList::execute(MR::MovementType_LayoutOnPause);
    }
}

void GameScene::initSequences() {
    GameStageClearSequence* pStageClearSeq = new GameStageClearSequence();
    pStageClearSeq->initWithoutIter();
    mStageClearSeq = pStageClearSeq;

    GamePauseSequence* pPauseSeq = new GamePauseSequence();
    pPauseSeq->initWithoutIter();
    mPauseSeq = pPauseSeq;

    mScenarioCamera = new GameSceneScenarioOpeningCameraState();
    mPauseCtrl = new GameScenePauseControl(this);
    mPauseCtrl->registerNervePauseMenu(GET_NERVE(GameScene, GameScenePauseMenu));

    mPauseSeq->initWindowMenu(MR::Functor(mPauseCtrl, &GameScenePauseControl::requestPauseMenuOff));
}

void GameScene::initEffect() {
    if (MR::isEqualStageName("CosmosGardenGalaxy")) {
        SceneFunction::initEffectSystem(5120, 384);
    } else if (MR::isEqualStageName("KoopaBattleVs1Galaxy")) {
        SceneFunction::initEffectSystem(6144, 512);
    } else if (MR::isEqualStageName("AstroGalaxy")) {
        SceneFunction::initEffectSystem(3072, 512);
    } else {
        SceneFunction::initEffectSystem(3072, 256);
    }
}

void GameScene::drawMirror() const {
    if (isDrawMirror()) {
#ifdef TARGET_PC
        MotionSignal::mirror(true);
#endif
        NebulaMtxCopy(MR::getMirrorCameraViewMtx(), j3dSys.mViewMtx);
        MR::loadProjectionMtx();
        CategoryList::entryDrawBufferMirror();
        J3DNativeState::setAlphaUpdate(GX_FALSE);
        J3DNativeState::setColorUpdate(GX_TRUE);
        CategoryList::drawOpa(MR::DrawBufferType_MirrorMapObj);
        CategoryList::drawXlu(MR::DrawBufferType_MirrorMapObj);
        CategoryList::execute(MR::DrawType_CaptureScreenIndirect);
#ifdef TARGET_PC
        MotionSignal::mirror(false);
#endif
        MR::clearZBuffer();
        GXColor c = {0, 0, 255, 0};
        MR::fillScreen(c);
    }
}

// inline
bool GameScene::isPlayMovie() const {
    return MR::isActiveMoviePlayer() || MR::isMoviePlayingOnSequence() || isNerve(GET_NERVE(GameScene, GameScenePlayMovie));
}

void GameScene::draw3D() const {
    if (!mDraw3D) {
        return;
    }

    if (isPlayMovie()) {
        return;
    }

    f32 sceneCamera[4][4];
    MotionSignal::sceneCamera(sceneCamera);
    NebulaHost::gameStage(MR::getCurrentStageName());
    NebulaRender::sceneMarker(NEBULA_SCENE_3D_BEGIN, sceneCamera);
    MotionSignal::sceneBegin();
    GameLightSignal::frame();
    CategoryList::execute(MR::DrawType_AstroDomeSkyClear);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    MR::setDefaultViewportAndScissor();
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    J3DNativeState::setDstAlpha(GX_TRUE, 0);
    CategoryList::drawOpa(MR::DrawBufferType_ClippedMapParts);
    CategoryList::execute(MR::DrawType_ClipArea);
    CategoryList::execute(MR::DrawType_FallOutFieldDraw);
    SceneFunction::executeDrawBufferListNormalOpaBeforeVolumeShadow();
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    CategoryList::execute(MR::DrawType_ShadowVolume);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setDstAlpha(GX_TRUE, 0);
    SceneFunction::executeDrawBufferListNormalOpaBeforeSilhouette();
    CategoryList::execute(MR::DrawType_0x28);
    SceneFunction::executeDrawSilhouetteAndFillShadow();
    SceneFunction::executeDrawAlphaShadow();
    CategoryList::execute(MR::DrawType_BrightSun);
    MR::setLensFlareDrawSyncToken();
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setDstAlpha(GX_FALSE, 0);
    SceneFunction::executeDrawBufferListNormalOpa();
    SceneFunction::executeDrawListOpa();
    CategoryList::drawOpa(MR::DrawBufferType_UNK_0x18);
    SceneFunction::executeDrawBufferListNormalXlu();
    SceneFunction::executeDrawListXlu();
    CategoryList::drawXlu(MR::DrawBufferType_UNK_0x18);
    CategoryList::execute(MR::DrawType_ShadowSurface);
    CategoryList::execute(MR::DrawType_EffectDraw3D);
    CategoryList::execute(MR::DrawType_EffectDrawForBloomEffect);
    CategoryList::execute(MR::DrawType_CenterScreenBlur);
    CategoryList::execute(MR::DrawType_CaptureScreenIndirect);
    SceneFunction::executeDrawAfterIndirect();
    CategoryList::execute(MR::DrawType_0x33);
    MR::setStarPointerDrawSyncToken();
    MR::setTalkDirectorDrawSyncToken();
    SceneFunction::executeDrawImageEffect();
    J3DNativeState::setDither(GX_TRUE);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    CategoryList::execute(MR::DrawType_EffectDrawAfterImageEffect);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    MR::loadLightPlayer();
    CategoryList::execute(MR::DrawType_0x24);
    CategoryList::execute(MR::DrawType_0x3B);
    NebulaRender::sceneMarker(NEBULA_SCENE_3D_END, nullptr);
    MR::clearZBuffer();
}

void GameScene::draw2D() const {
#ifdef TARGET_PC
    MotionSignal::scene2D(true);
#endif
    if (isPlayMovie()) {
        SceneFunction::executeDrawList2DMovie();
    } else {
        SceneFunction::executeDrawList2DNormal();
    }

    CategoryList::execute(MR::DrawType_CameraCover);
    CategoryList::execute(MR::DrawType_LayoutOnPause);
    mPauseSeq->draw();
    MR::drawInitFor2DModel();
    CategoryList::drawOpa(MR::DrawBufferType_0x26);
    CategoryList::drawXlu(MR::DrawBufferType_0x26);
    CategoryList::execute(MR::DrawType_WipeLayout);
#ifdef TARGET_PC
    // Nebula's achievement board over everything while it is open (its pop-up: drawOverlay).
    if (isNerve(GET_NERVE(GameScene, GameSceneNebulaBoard))) {
        MR::getGameSceneLayoutHolder()->mAchievementBoard->drawOnTop();
    }
    MotionSignal::scene2D(false);
#endif
}

#ifdef TARGET_PC
void GameScene::drawOverlay() const {
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mAchievementToast != nullptr) {
        MR::getGameSceneLayoutHolder()->mAchievementToast->drawOnTop();
    }
}
#endif

bool GameScene::isValidScenarioOpeningCamera() const {
    return MR::hasStartAnimCamera() != false;
}

void GameScene::startStagePlayFirst() {
    if (isValidScenarioOpeningCamera()) {
        setNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera));

        if (!MR::isBeginScenarioStarter()) {
            MR::stopSubBGM(0);
            MR::stopStageBGM(0);
            MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
        }
    } else if (MR::isBeginScenarioStarter()) {
        MR::forceToBlankCinemaFrame();
        MR::forceOpenWipeFade();
        setNerve(GET_NERVE(GameScene, GameSceneScenarioStarter));
    } else {
        if (MR::isStageBeginFadeWipe()) {
            MR::openWipeFade(180);
        } else if (MR::isStageBeginTitleWipe()) {
            MR::openWipeFade(30);
        } else if (MR::isStageBeginWithoutWipe()) {
            MR::forceOpenWipeFade();
        } else {
            MR::openWipeCircle();
        }

        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
        MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
        MR::executeOnWelcomeAndRetry();
        setNerve(GET_NERVE(GameScene, GameSceneAction));
    }
}

void GameScene::startStagePlayRetry() {
    MR::openWipeCircle();

    if (MR::isEqualStageName("SurfingLv1Galaxy") && MR::getPlayerRestartIdInfo()->_0 == 1) {
        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
    } else if (MR::isEqualStageName("SurfingLv2Galaxy") && MR::getPlayerRestartIdInfo()->_0 == 1) {
        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
    } else {
        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
        MR::startStageBGMFromStageName("Game", MR::getCurrentStageName(), MR::getCurrentScenarioNo());
    }

    MR::executeOnWelcomeAndRetry();
    setNerve(GET_NERVE(GameScene, GameSceneAction));
}

bool GameScene::isPermitToPauseMenu() const {
    return !MR::isStageDisablePauseMenu() && isNerve(GET_NERVE(GameScene, GameSceneAction)) && !MR::isDemoActive() && !MR::isWipeActive() &&
           !MR::isWipeBlank() && !MR::isPlayerDead() && !MR::isPlayerDamaging();
}

void GameScene::requestShowGalaxyMap() {
    setNerve(GET_NERVE(GameScene, GameSceneGalaxyMap));
}

#ifdef TARGET_PC
void GameScene::requestShowNebulaBoard() {
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mAchievementBoard != nullptr) {
        sBoardRequested = true;
    }
}

// As the galaxy map: the world held, the pointer in its menu mode, the board's own frame each frame.
void GameScene::exeNebulaBoard() {
    NebulaAchievementBoard* pBoard = MR::getGameSceneLayoutHolder()->mAchievementBoard;
    if (MR::isFirstStep(this)) {
        MR::startStarPointerModePauseMenu(this);
        pBoard->open();
    }
    pBoard->update();
    if (!pBoard->isOpen()) {
        MR::endStarPointerMode(this);
        setNerve(GET_NERVE(GameScene, GameSceneAction));
    } else {
        CategoryList::execute(MR::MovementType_LayoutOnPause);
    }
}
#endif

void GameScene::requestStaffRoll() {
    setNerve(GET_NERVE(GameScene, GameSceneStaffRoll));
}

bool GameScene::isDrawMirror() const {
    if (!MR::isExistMirrorCamera()) {
        return false;
    }

    if (isPlayMovie()) {
        return false;
    }

    return MR::isPlayerInAreaObj("MirrorArea");
}

void GameScene::stageClear() {
    if (MR::isFirstStep(this)) {
#ifdef TARGET_PC
        // The star just collected (PowerStar sent it with the get sequence's request) unlocks what it completes now,
        // while its get sequence plays, not when the hub records it.
        NebulaAchievements::evaluate();
#endif
        if (isNerve(GET_NERVE(GameScene, GameScenePowerStarGet))) {
            mStageClearSeq->startPowerStarGetDemo();
        } else if (isNerve(GET_NERVE(GameScene, GameSceneGrandStarGet))) {
            mStageClearSeq->startGrandStarGetDemo();
        }
    }

    mStageClearSeq->movement();
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}

void GameScene::exeStaffRoll() {
    MR::getSceneNameObjMovementController()->movement();
    CategoryList::execute(MR::MovementType_Layout);
    CategoryList::execute(MR::DrawType_LayoutDecoration);
    CategoryList::execute(MR::MovementType_WipeLayout);
}

void GameScene::exeTimeUp() {
    if (MR::isFirstStep(this)) {
        MR::startGlobalTimerTimeUp();
    }
}

void GameScene::exeGameOver() {
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}

void GameScene::exeGrandStarGet() {
    stageClear();
}

void GameScene::exePowerStarGet() {
    stageClear();
}

void GameScene::exePauseMenu() {
    if (MR::isFirstStep(this)) {
    }

    mPauseSeq->movement();
    CategoryList::execute(MR::MovementType_LayoutOnPause);
}

void GameScene::exeSceneAction() {
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}

void GameScene::exeScenarioStarter() {
    SceneFunction::movementStopSceneController();
    SceneFunction::executeMovementList();
}
