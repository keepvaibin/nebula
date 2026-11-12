#include "Game/Scene/GameScene.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/NebulaLoadTrace.hpp"
#include <nebula_module.h>
#include "Game/Render/FrameList.hpp"
#include "Game/Util/GameLightSignal.hpp"
#include "Game/Util/MotionSignal.hpp"
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
#include <cstring>
#include "Game/Screen/NebulaSettingsMenu.hpp"
#include "Game/System/NebulaFrameGen.hpp"
#include <nebula_game_api.h>
#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/AreaObj/AreaForm.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/AreaObj/WaterArea.hpp"
#include "Game/Map/OceanBowl.hpp"
#include "Game/Map/OceanRing.hpp"
#include "Game/Map/OceanSphere.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/WaterPoint.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Map/LightFunction.hpp"
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
#include "Game/System/LumaMovie.hpp"
#include "Game/System/GalaxyMapController.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/System/NebulaModCalls.hpp"
#include "Game/NPC/NebulaAchievementLuma.hpp"
#include "Game/Screen/NebulaAchievementBoard.hpp"
#include "Game/Screen/NebulaAchievementToast.hpp"
#include "Game/Screen/NebulaModLogNotice.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Render/Model.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/LumaDraw.hpp"
#include "Game/System/LumaDrawInst.hpp"

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
    NEW_NERVE(GameSceneGalaxyMap, GameScene, GalaxyMap);
    NEW_NERVE(GameSceneStaffRoll, GameScene, StaffRoll);
    NEW_NERVE(GameSceneNebulaBoard, GameScene, NebulaBoard);
};  // namespace NrvGameScene

GameScene::GameScene()
    : Scene("GameScene"), mScenarioCamera(nullptr), mPauseCtrl(nullptr), mPauseSeq(nullptr), mStageClearSeq(nullptr), mDraw3D(true) {
}

GameScene::~GameScene() {
    NebulaMods::fire(NEBULA_MOD_EVENT_STAGE_END, 0, nullptr, MR::getCurrentStageName());
    MR::destroySceneMessage();
    NPCFunction::deleteNPCData();
    MR::onStarPointerSceneOut();
}

void GameScene::init() {
    MR::createSceneObj(SceneObj_SceneDataInitializer);
    initNerve(GET_NERVE(GameScene, GameSceneScenarioOpeningCamera));
    SceneFunction::initForNameObj();
    SceneFunction::initForLiveActor();
    NebulaLoadTrace::measure("initEffect()", [&] { initEffect(); });
    MR::createSceneObj(SceneObj_CameraContext);
    MR::createSceneObj(SceneObj_NameObjGroup);
    MR::createSceneObj(SceneObj_PlanetGravityManager);
    MR::createSceneObj(SceneObj_MarioHolder);
    MR::createSceneObj(SceneObj_AudCameraWatcher);
    MR::createSceneObj(SceneObj_AudEffectDirector);
    MR::createSceneObj(SceneObj_AudBgmConductor);
    MR::createSceneObj(SceneObj_EventSequencer);
    MR::createSceneObj(SceneObj_ScenePlayingResult);
    MR::createSceneObj(SceneObj_FurDrawManager);
    MR::createSceneObj(SceneObj_PlacementStateChecker);
    MR::createSceneObj(SceneObj_GroupCheckManager);
    MR::createSceneObj(SceneObj_CinemaFrame);
    MR::createSceneObj(SceneObj_PlanetMapCreator);

    if (MR::isStageEpilogueDemo()) {
        MR::createSceneObj(SceneObj_StaffRoll);
    }

    // A plugin can start a movie in any scene.
    if (NebulaMods::hasCode()) {
        MR::createSceneObj(SceneObj_MoviePlayerSimple);
    }

    if (MR::isStageEpilogueDemo() || GameSequenceFunction::isNeedMoviePlayerForStorySequenceEvent()) {
        MR::createMoviePlayingSequence();
    }

    NebulaLoadTrace::measure("SceneFunction::createStageDataHolder()", [&] { SceneFunction::createStageDataHolder(); });
    MR::holdSceneInitialize();
}

// What needs the scenario: the star the player chose (MR::tryResumeSceneInitialize).
bool GameScene::initAfterScenarioChosen() {
    if (!MR::isScenarioDecided()) return true;
    const auto* host = nebula_host();
    const uint64_t deadline = host->clock_ns(host->user) + 4'000'000u;
    do {
        switch (mInitializePhase) {
        case 0:
            SceneFunction::initAfterScenarioSelected();
            NPCFunction::createNPCData();
            MR::createSceneObj(SceneObj_EventDirector);
            MR::initSceneMessage();
            break;
        case 1:
            MR::createSceneObj(SceneObj_CameraDirector);
            MR::createSceneObj(SceneObj_GameSceneLayoutHolder);
            MR::createSceneObj(SceneObj_StarPieceDirector);
            MR::createSceneObj(SceneObj_SceneWipeHolder);
            MR::createSceneObj(SceneObj_NamePosHolder);
            MR::createSceneObj(SceneObj_InformationObserver);
            if (MR::isGalaxyAnyCometAppearInCurrentStage()) MR::createSceneObj(SceneObj_CometRetryButton);
            break;
        case 2:
            LightFunction::initLightData();
            initSequences();
            break;
        case 3:
            if (!SceneFunction::startActorPlacementStep(deadline)) return false;
            break;
        case 4:
            NebulaAchievementLuma::tryCreate();
            break;
        case 5:
            // The complete reserve still exists before play; each actor is an indivisible load operation.
            while (mReservePiecesLeft != 0u) {
                MR::getStarPieceDirector()->createStarPieces(1);
                --mReservePiecesLeft;
                if (host->clock_ns(host->user) >= deadline) return false;
            }
            break;
        case 6:
            MR::completeCameraParameters();
            MR::initStarPointerGameScene();
            MR::initEventSystemAfterPlacement();
            MR::endInitLiveActorSystemInfo();
            MR::setInitializeStateAfterPlacement();
            MR::callMethodAllSceneNameObj(&NameObj::initAfterPlacement);
            break;
        case 7:
            SleepControlFunc::initSyncSleepController();
            ++mInitializePhase;
            return true;
        default:
            return true;
        }
        ++mInitializePhase;
    } while (host->clock_ns(host->user) < deadline);
    return false;
}

namespace {
    // The settings' galaxy select's galaxy `index` (its whole stage name in *pId): a name that fills the buffer is asked
    // for again with twice the room. 0 past the last.
    int getGalaxySelectStage(u32 index, MR::FormattedName* pId, u32* pStars) {
        for (u32 capacity = 256;; capacity *= 2) {
            char* const pText = pId->room(capacity);
            if (NebulaHost::galaxySelectStage(index, pText, capacity, pStars) == 0) {
                return 0;
            }
            if (strlen(pText) < capacity - 1) {
                return 1;
            }
        }
    }

    // The achievement board asked for by the Achievement Luma: it opens once the scene is in plain play, not paused,
    // in a demo or behind a wipe; a scene that starts drops the request.
    bool sBoardRequested = false;

    // Discord (__NebulaPresence): the galaxy and its star in the game's own words, and the Power Stars collected.
    void reportPresence() {
        const char* pStage = MR::getCurrentStageName();
        const s32 scenario = MR::getCurrentScenarioNo();
        const char16_t* pGalaxy = MR::getGalaxyNameOnCurrentLanguage(pStage);
        const char16_t* pStar = scenario > 0 ? MR::getScenarioNameOnCurrentLanguage(pStage, scenario) : nullptr;
        NebulaHost::presence(reinterpret_cast< const u16* >(pGalaxy), reinterpret_cast< const u16* >(pStar), MR::getPowerStarNum());
    }
};  // namespace

void GameScene::start() {
    NebulaMods::fire(NEBULA_MOD_EVENT_STAGE_START, MR::getCurrentScenarioNo(), nullptr, MR::getCurrentStageName());
    AudWrap::getSceneMgr()->startScene();
    sBoardRequested = false;
    reportPresence();
    NebulaAchievements::evaluate();

    if (!MR::hasRetryGalaxySequence()) {
        startStagePlayFirst();
    } else {
        startStagePlayRetry();
    }
}

void GameScene::update() {
    NebulaMods::stageFrame();
    mPauseCtrl->updateNerve();
    updateNerve();
    NebulaLoadTrace::gameScene();
    NebulaAchievements::update();
    NebulaModCalls::update(isNerve(GET_NERVE(GameScene, GameSceneAction)));
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
    // The mod log's notice runs as the pop-up does: in play, cutscenes and menus alike.
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder)) {
        NebulaModLogNotice::update();
    }
}

void GameScene::draw() const {
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
    // The settings' galaxy select (its ADVANCED page) names its galaxies and stars in the game's own words, asked once.
    if (NebulaHost::galaxySelectNamesWanted() != 0) {
        MR::FormattedName id;
        u32 stars = 0;
        for (u32 i = 0; ::getGalaxySelectStage(i, &id, &stars) != 0; ++i) {
            NebulaHost::galaxySelectName(i, 0, reinterpret_cast< const u16* >(MR::getGalaxyNameOnCurrentLanguage(id.c_str())));
            for (u32 star = 1; star <= stars; ++star) {
                NebulaHost::galaxySelectName(i, star,
                                         reinterpret_cast< const u16* >(MR::getScenarioNameOnCurrentLanguage(id.c_str(), star)));
            }
        }
    }
    // A travel the player asked for in the settings' galaxy select, through the game's own stage change; with the Grand
    // Stars, the Observatory they power.
    {
        char stage[64];
        int32_t star = 0;
        const int travel = NebulaHost::galaxyTravel(stage, sizeof(stage), &star);
        if (travel == NEBULA_GALAXY_TRAVEL_STAGE) {
            NebulaModCalls::changeStage(stage, star);
        } else if (travel == NEBULA_GALAXY_TRAVEL_GRAND_STARS) {
            NebulaModCalls::setGrandStars(star, stage);
        }
    }
    LightFunction::initLightRegisterAll();
    CategoryList::execute(MR::DrawType_MiiFaceIcon);
    CategoryList::execute(MR::DrawType_MiiFaceNew);
    drawMirror();
    draw3D();
    draw2D();
    MR::resetDrawState();
}

void GameScene::calcAnim() {
    if (isPlayMovie()) {
        SceneFunction::executeCalcAnimListOnPlayingMovie();
    } else {
        SceneFunction::executeCalcAnimList();
    }

    CategoryList::execute(MR::CalcAnimType_AnimParticleIgnorePause);
    SceneFunction::executeCalcViewAndEntryList();
    luma::draw::inst::frame();  // the mods' overrides onto this frame's materials and draw poses (one load with none)
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
    NebulaAchievements::onGrandStarGet();
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

bool GameScene::isExecPauseMenuOrGalaxyMap() const {
    return isNerve(GET_NERVE(GameScene, GameScenePauseMenu)) || isNerve(GET_NERVE(GameScene, GameSceneGalaxyMap));
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
        pCometRetryButton->appear();
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
    // The pools grow past this when a scene asks for more.
    SceneFunction::initEffectSystem(512);
}

void GameScene::drawMirror() const {
    if (isDrawMirror()) {
        Frame::mirror(true);
        ModelSys::mViewRole = NEBULA_CAMERA_MIRROR;
        NebulaMtxCopy(MR::getMirrorCameraViewMtx(), ModelSys::mViewMtx);
        MR::loadProjectionMtx();
        CategoryList::entryDrawBufferMirror();
        Frame::setPassAlpha(false);
        CategoryList::drawOpa(MR::DrawBufferType_MirrorMapObj);
        CategoryList::drawXlu(MR::DrawBufferType_MirrorMapObj);
        CategoryList::execute(MR::DrawType_CaptureScreenIndirect);
        Frame::mirror(false);
        // The scene's view, not the mirror's under the scene's role: a draw before draw3D's own load is in the scene.
        MR::loadViewMtx();
        MR::clearZBuffer();
        NebulaColor8 c = {0, 0, 255, 0};
        MR::fillScreen(c, true);
    }
}

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
    Frame::sceneBegin(sceneCamera);
    GameLightSignal::frame();
    CategoryList::execute(MR::DrawType_AstroDomeSkyClear);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    MR::setDefaultViewportAndScissor();
    Frame::setPassAlpha(true);
    luma::draw::point(LUMA_DRAW_WORLD_BEFORE);  // the mods' world.before (one load with none)
    CategoryList::drawOpa(MR::DrawBufferType_ClippedMapParts);
    CategoryList::execute(MR::DrawType_ClipArea);
    CategoryList::execute(MR::DrawType_FallOutFieldDraw);
    SceneFunction::executeDrawBufferListNormalOpaBeforeVolumeShadow();
    CategoryList::execute(MR::DrawType_ShadowVolume);
    // What is drawn after the volumes is not in their shadow.
    Frame::setPassStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    SceneFunction::executeDrawBufferListNormalOpaBeforeSilhouette();
    Frame::setPassStencil(NEBULA_STENCIL_OFF, NEBULA_STENCIL_KEEP);
    CategoryList::execute(MR::DrawType_PlayerSilhouette);
    SceneFunction::executeDrawSilhouetteAndFillShadow();
    SceneFunction::executeDrawAlphaShadow();
    MR::setLensFlareDrawSyncToken();
    Frame::setPassAlpha(false);
    SceneFunction::executeDrawBufferListNormalOpa();
    SceneFunction::executeDrawListOpa();
    MR::sceneOpaqueEnd();
    luma::draw::point(LUMA_DRAW_WORLD_OPAQUE_AFTER);
    SceneFunction::executeDrawBufferListNormalXlu();
    SceneFunction::executeDrawListXlu();
    CategoryList::execute(MR::DrawType_ShadowSurface);
    CategoryList::execute(MR::DrawType_EffectDraw3D);
    CategoryList::execute(MR::DrawType_EffectDrawForBloomEffect);
    CategoryList::execute(MR::DrawType_CenterScreenBlur);
    CategoryList::execute(MR::DrawType_CaptureScreenIndirect);
    SceneFunction::executeDrawAfterIndirect();
    CategoryList::execute(MR::DrawType_PlayerSearchLight);
    MR::setStarPointerDrawSyncToken();
    MR::setTalkDirectorDrawSyncToken();
    SceneFunction::executeDrawImageEffect();
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    CategoryList::execute(MR::DrawType_EffectDrawAfterImageEffect);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    MR::loadLightPlayer();
    CategoryList::execute(MR::DrawType_PlayerIndirect);
    luma::draw::point(LUMA_DRAW_WORLD_AFTER);
    Frame::sceneEnd();
    MR::clearZBuffer();
}

void GameScene::draw2D() const {
    Frame::scene2D(true);
    luma::draw::point(LUMA_DRAW_HUD_BEFORE);  // the mods' hud.before, under the game's HUD (one load with none)
    if (isPlayMovie()) {
        SceneFunction::executeDrawList2DMovie();
        luma::movie::drawSubtitles();  // a mod's lines (<movie>.subs) over the movie (plan 3.10 layer 4)
    } else {
        SceneFunction::executeDrawList2DNormal();
    }

    CategoryList::execute(MR::DrawType_CameraCover);
    // The mods' hud.after: over the HUD, under the mod log's notice and the pause screens (plan 2.4).
    luma::draw::point(LUMA_DRAW_HUD_AFTER);
    // The mod log's notice over the game and under the pause screens (and the settings menu, drawn later), so it never
    // covers a choice the player must read (contract mod-log 4.1).
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder)) {
        NebulaModLogNotice::draw();
    }
    CategoryList::execute(MR::DrawType_LayoutOnPause);
    mPauseSeq->draw();
    MR::drawInitFor2DModel();
    CategoryList::execute(MR::DrawType_WipeLayout);
    // Nebula's achievement board over everything while it is open (its pop-up: drawOverlay).
    if (isNerve(GET_NERVE(GameScene, GameSceneNebulaBoard))) {
        MR::getGameSceneLayoutHolder()->mAchievementBoard->drawOnTop();
    }
    Frame::scene2D(false);
}

void GameScene::drawOverlay() const {
    if (MR::isExistSceneObj(SceneObj_GameSceneLayoutHolder) && MR::getGameSceneLayoutHolder()->mAchievementToast != nullptr) {
        MR::getGameSceneLayoutHolder()->mAchievementToast->drawOnTop();
    }
}

bool GameScene::isValidScenarioOpeningCamera() const {
    return MR::hasStartAnimCamera();
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

    if (MR::isEqualStageName("SurfingLv1Galaxy") && MR::getPlayerRestartIdInfo()->mId == 1) {
        MR::stopSubBGM(0);
        MR::stopStageBGM(0);
    } else if (MR::isEqualStageName("SurfingLv2Galaxy") && MR::getPlayerRestartIdInfo()->mId == 1) {
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
        // The star just collected (PowerStar sent it with the get sequence's request) unlocks what it completes now,
        // while its get sequence plays, not when the hub records it.
        NebulaAchievements::evaluate();
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
