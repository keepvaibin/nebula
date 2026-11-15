---
title: Nerves, classes G to M
shortTitle: Nerves, classes G to M
intro: The nerves of the game's classes whose names start with G to M.
contentType: reference
---

## About this list

This page lists the nerves of the game's classes whose names start with G to M. Each section is one class, with its source file. The Nerve column names the nerve, the Code column names the function that runs every frame in it, and the On leaving column names the function that runs as an object leaves it. For how to use nerves, see [Nerves](nerves.md).

## GalaxyCometState

src/Game/System/GalaxyCometState.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GalaxyCometStateHide` | `GalaxyCometState::exeHide` |  |
| `GalaxyCometStateLand` | `GalaxyCometState::exeLand` |  |

## GalaxyConfirmLayout

src/Game/Screen/GalaxyConfirmLayout.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GalaxyConfirmLayoutNrvSelecting` | `GalaxyConfirmLayout::exeSelecting` |  |
| `GalaxyConfirmLayoutNrvSelectedBackButton` | `GalaxyConfirmLayout::exeSelectedBackButton` |  |
| `GalaxyConfirmLayoutNrvSelected` | `GalaxyConfirmLayout::exeSelected` |  |

## GalaxyMap

src/Game/Screen/GalaxyMap.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GalaxyMapIdle` | `GalaxyMap::exeIdle` |  |
| `GalaxyMapFocusIn` | `GalaxyMap::exeFocusIn` |  |
| `GalaxyMapFocusOut` | `GalaxyMap::exeFocusOut` |  |
| `GalaxyMapFadeinGalaxyMap` | `GalaxyMap::exeFadeinGalaxyMap` |  |
| `GalaxyMapFadeinAstroMap` | `GalaxyMap::exeFadeinAstroMap` |  |
| `GalaxyMapShowDetail` | `GalaxyMap::exeShowDetail` |  |

## GalaxyMapController

src/Game/Screen/GalaxyMapController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GalaxyMapControllerReadyModeMap` | `GalaxyMapController::exeReadyModeMap` |  |
| `GalaxyMapControllerFadeinModeMap` | `GalaxyMapController::exeFadeinModeMap` |  |
| `GalaxyMapControllerAppearGalaxyMap` | `GalaxyMapController::exeAppearGalaxyMap` |  |
| `GalaxyMapControllerDisplayGalaxyMap` | `GalaxyMapController::exeDisplayGalaxyMap` |  |
| `GalaxyMapControllerAppearAstroMap` | `GalaxyMapController::exeAppearAstroMap` |  |
| `GalaxyMapControllerDisplayAstroMap` | `GalaxyMapController::exeDisplayAstroMap` |  |
| `GalaxyMapControllerReadyPowerStarList` | `GalaxyMapController::exeReadyPowerStarList` |  |
| `GalaxyMapControllerAppearPowerStarList` | `GalaxyMapController::exeAppearPowerStarList` |  |
| `GalaxyMapControllerDisplayPowerStarList` | `GalaxyMapController::exeDisplayPowerStarList` |  |
| `GalaxyMapControllerDisappearPowerStarList` | `GalaxyMapController::exeDisappearPowerStarList` |  |
| `GalaxyMapControllerNewGalaxyDiscoverPhaseAstroMap` | `GalaxyMapController::exeNewGalaxyDiscoverPhaseAstroMap` |  |
| `GalaxyMapControllerNewGalaxyDiscoverPhaseAppearGalaxyMap` | `GalaxyMapController::exeNewGalaxyDiscoverPhaseAppearGalaxyMap` |  |
| `GalaxyMapControllerNewGalaxyDiscoverPhaseDisplayGalaxyMap` | `GalaxyMapController::exeNewGalaxyDiscoverPhaseDisplayGalaxyMap` |  |
| `GalaxyMapControllerNewTicoGalaxyDiscoverPhaseAstroMap` | `GalaxyMapController::exeNewTicoGalaxyDiscoverPhaseAstroMap` |  |
| `GalaxyMapControllerNewDomeDiscoverPhaseAstroMap` | `GalaxyMapController::exeNewDomeDiscoverPhaseAstroMap` |  |
| `GalaxyMapControllerTicoCometWaitWhite` | `GalaxyMapController::exeTicoCometWaitWhite` |  |
| `GalaxyMapControllerTicoCometPray` | `GalaxyMapController::exeTicoCometPray` |  |
| `GalaxyMapControllerChallengeGalaxyDiscoverPhaseAstroMap` | `GalaxyMapController::exeChallengeGalaxyDiscoverPhaseAstroMap` |  |
| `GalaxyMapControllerFadeout` | `GalaxyMapController::exeFadeout` |  |
| `GalaxyMapControllerReadyGame` | `GalaxyMapController::exeReadyGame` |  |

## GalaxyMapGalaxyDetail

src/Game/Screen/GalaxyMapGalaxyDetail.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GalaxyMapGalaxyDetailAppear` | `GalaxyMapGalaxyDetail::exeAppear` |  |
| `GalaxyMapGalaxyDetailDisplay` | `GalaxyMapGalaxyDetail::exeDisplay` |  |
| `GalaxyMapGalaxyDetailDisappear` | `GalaxyMapGalaxyDetail::exeDisappear` |  |

## GalaxyMapSelectButton

src/Game/Screen/GalaxyMapSelectButton.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GalaxyMapSelectButtonHide` | `GalaxyMapSelectButton::exeHide` |  |
| `GalaxyMapSelectButtonAstroMapWait` | `GalaxyMapSelectButton::exeAstroMapWait` |  |
| `GalaxyMapSelectButtonStarListWait` | `GalaxyMapSelectButton::exeStarListWait` |  |
| `GalaxyMapSelectButtonBetweenAstroMapAndGalaxyMapFadein` | `GalaxyMapSelectButton::exeBetweenAstroMapAndGalaxyMapFadein` |  |
| `GalaxyMapSelectButtonBetweenListToMapFadeout` | `GalaxyMapSelectButton::exeBetweenListToMapFadeout` |  |
| `GalaxyMapSelectButtonBetweenListToMapFadein` | `GalaxyMapSelectButton::exeBetweenListToMapFadein` |  |
| `GalaxyMapSelectButtonBetweenMapToListFadeout` | `GalaxyMapSelectButton::exeBetweenMapToListFadeout` |  |
| `GalaxyMapSelectButtonBetweenMapToListFadein` | `GalaxyMapSelectButton::exeBetweenMapToListFadein` |  |

## GalaxyMapTitle

src/Game/Screen/GalaxyMapTitle.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GalaxyMapTitleHide` | `GalaxyMapTitle::exeHide` |  |
| `GalaxyMapTitleDisplay` | `GalaxyMapTitle::exeDisplay` |  |
| `GalaxyMapTitleAppearToAnother` | `GalaxyMapTitle::exeAppearToAnother` |  |
| `GalaxyMapTitleDisappearToAnother` | `GalaxyMapTitle::exeDisappearToAnother` |  |

## GalaxyNamePlate

src/Game/Screen/GalaxyNamePlate.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GalaxyNamePlateNrvAppearReady` | `GalaxyNamePlate::exeAppearReady` |  |
| `GalaxyNamePlateNrvAppear` | `GalaxyNamePlate::exeAppear` |  |
| `GalaxyNamePlateNrvWait` | `GalaxyNamePlate::exeWait` |  |

## GalaxySelectBackButton

src/Game/Screen/GalaxySelectBackButton.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GalaxySelectBackButtonNrvAppear` | `GalaxySelectBackButton::exeAppear` |  |
| `GalaxySelectBackButtonNrvDisappear` | `GalaxySelectBackButton::exeDisappear` |  |
| `GalaxySelectBackButtonNrvDecide` | `GalaxySelectBackButton::exeDecide` |  |

## GalaxySelectInfo

src/Game/Screen/GalaxySelectInfo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GalaxySelectInfoNrvWait` | `GalaxySelectInfo::exeWait` |  |
| `GalaxySelectInfoNrvDisappear` | `GalaxySelectInfo::exeDisappear` |  |

## GamePauseSequence

src/Game/Screen/GamePauseSequence.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GamePauseSequenceDeactive` | `GamePauseSequence::exeDeactive` |  |
| `GamePauseSequenceActivePauseMenu` | `GamePauseSequence::exeActivePauseMenu` |  |
| `GamePauseSequenceSceneInformation` | `GamePauseSequence::exeSceneInformation` |  |

## GameScene

src/Game/Scene/GameScene.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GameSceneScenarioOpeningCamera` | `GameScene::exeScenarioOpeningCamera` |  |
| `GameSceneScenarioStarter` | `GameScene::exeScenarioStarter` |  |
| `GameSceneAction` | `GameScene::exeSceneAction` |  |
| `GameScenePauseMenu` | `GameScene::exePauseMenu` |  |
| `GameScenePowerStarGet` | `GameScene::exePowerStarGet` |  |
| `GameSceneGrandStarGet` | `GameScene::exeGrandStarGet` |  |
| `GameSceneGameOver` | `GameScene::exeGameOver` |  |
| `GameSceneCometRetryAfterMiss` | `GameScene::exeCometRetryAfterMiss` |  |
| `GameSceneSaveAfterGameOver` | `GameScene::exeSaveAfterGameOver` |  |
| `GameScenePlayMovie` | `GameScene::exePlayMovie` |  |
| `GameSceneGalaxyMap` | `GameScene::exeGalaxyMap` |  |
| `GameSceneStaffRoll` | `GameScene::exeStaffRoll` |  |
| `GameSceneNebulaBoard` | `GameScene::exeNebulaBoard` |  |

## GameScenePauseControl

src/Game/Scene/GameScenePauseControl.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GameScenePauseControlNormal` | `GameScenePauseControl::exeNormal` |  |

## GameSceneScenarioOpeningCameraState

src/Game/Scene/GameSceneScenarioOpeningCameraState.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GameSceneScenarioOpeningCameraStateWait` | `GameSceneScenarioOpeningCameraState::exeWait` |  |
| `GameSceneScenarioOpeningCameraStatePlay` | `GameSceneScenarioOpeningCameraState::exePlay` |  |

## GameSequenceProgress

src/Game/System/GameSequenceProgress.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GameSequenceProgressBooting` | `GameSequenceProgress::exeBooting` |  |
| `GameSequenceProgressBoot` | `GameSequenceProgress::exeBoot` |  |
| `GameSequenceProgressWaitGoToFirstScene` | `GameSequenceProgress::exeWaitGoToFirstScene` |  |
| `GameSequenceProgressNormal` | `GameSequenceProgress::exeNormal` |  |
| `GameSequenceProgressGalaxyMove` | `GameSequenceProgress::exeGalaxyMove` |  |
| `GameSequenceProgressResetProcessing` | `GameSequenceProgress::exeResetProcessing` |  |

## GameStageClearSequence

src/Game/Screen/GameStageClearSequence.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GameStageClearSequencePowerStarGetDemo` | `GameStageClearSequence::exePowerStarGetDemo` |  |
| `GameStageClearSequenceGrandStarGetDemo` | `GameStageClearSequence::exeGrandStarGetDemo` |  |

## GameSystem

src/Game/System/GameSystem.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GameSystemInitializeAudio` | `GameSystem::exeInitializeAudio` |  |
| `GameSystemInitializeBootScene` | `GameSystem::exeInitializeBootScene` |  |
| `GameSystemLoadSystemArchive` | `GameSystem::exeLoadSystemArchive` |  |
| `GameSystemNormal` | `GameSystem::exeNormal` |  |

## GameSystemErrorWatcher

src/Game/System/GameSystemErrorWatcher.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GameSystemErrorWatcherNoError` | `GameSystemErrorWatcher::exeNoError` |  |
| `GameSystemErrorWatcherErrorWindowIn` | `GameSystemErrorWatcher::exeErrorWindowIn` |  |
| `GameSystemErrorWatcherErrorWindowDisplay` | `GameSystemErrorWatcher::exeErrorWindowDisplay` |  |
| `GameSystemErrorWatcherErrorWindowOut` | `GameSystemErrorWatcher::exeErrorWindowOut` |  |

## GameSystemRestartProcess

src/Game/System/GameSystemRestartProcess.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GameSystemRestartProcessIdle` | `GameSystemRestartProcess::exeIdle` |  |
| `GameSystemRestartProcessFadeOut` | `GameSystemRestartProcess::exeFadeOut` |  |
| `GameSystemRestartProcessWaitPrepareFadein` | `GameSystemRestartProcess::exeWaitPrepareFadein` |  |
| `GameSystemRestartProcessFadein` | `GameSystemRestartProcess::exeFadein` |  |

## GameSystemSceneController

src/Game/System/GameSystemSceneController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GameSystemSceneControllerNotInitialized` | `GameSystemSceneController::exeNotInitialized` |  |
| `GameSystemSceneControllerNormal` | `GameSystemSceneController::exeNormal` |  |
| `GameSystemSceneControllerChoosePlayerVoices` | `GameSystemSceneController::exeChoosePlayerVoices` |  |
| `GameSystemSceneControllerInitializeScene` | `GameSystemSceneController::exeInitializeScene` |  |
| `GameSystemSceneControllerInvalidateSystemWipe` | `GameSystemSceneController::exeInvalidateSystemWipe` |  |
| `GameSystemSceneControllerWaitDrawDoneScene` | `GameSystemSceneController::exeWaitDrawDoneScene` |  |
| `GameSystemSceneControllerDestroyScene` | `GameSystemSceneController::exeDestroyScene` |  |
| `GameSystemSceneControllerReadyToStartScene` | `GameSystemSceneController::exeReadyToStartScene` |  |
| `GameSystemSceneControllerWaitDrawDoneSceneForDestroy` | `GameSystemSceneController::exeWaitDrawDoneScene` |  |
| `GameSystemSceneControllerDestroySceneForDestroy` | `GameSystemSceneController::exeDestroyScene` |  |
| `GameSystemSceneControllerDestroyed` | `GameSystemSceneController::exeDestroyed` |  |

## GCapture

src/Game/MapObj/GCapture.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GCaptureNrvWait` | `GCapture::exeWait` |  |
| `GCaptureNrvCapture` | `GCapture::exeCapture` | `GCapture::exeCapture` |
| `GCaptureNrvRecapture` | `GCapture::exeRecapture` | `GCapture::exeRecapture` |
| `GCaptureNrvTraction` | `GCapture::exeTraction` |  |
| `GCaptureNrvHold` | `GCapture::exeHold` |  |
| `GCaptureNrvBreak` | `GCapture::exeBreak` |  |
| `GCaptureNrvCoolDown` | `GCapture::exeCoolDown` |  |

## GCaptureTarget

src/Game/MapObj/GCaptureTarget.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GCaptureTargetNrvTryDemoAppear` | `GCaptureTarget::exeTryDemoAppear` |  |
| `GCaptureTargetNrvAppear` | `GCaptureTarget::exeAppear` |  |
| `GCaptureTargetNrvWait` | `GCaptureTarget::exeWait` |  |
| `GCaptureTargetNrvPointable` | `GCaptureTarget::exePointable` |  |
| `GCaptureTargetNrvHitPointer` | `GCaptureTarget::exeHitPointer` |  |
| `GCaptureTargetNrvActive` | `GCaptureTarget::exeActive` |  |

## GeneralMapParts

src/Game/MapObj/GeneralMapParts.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `GeneralMapParts::exeWait` |  |
| `HostTypeWaitForPlayerOn` | `GeneralMapParts::exeWaitForPlayerOn` |  |
| `HostTypeMoveStart` | `GeneralMapParts::exeMoveStart` |  |
| `HostTypeMove` | `GeneralMapParts::exeWait` |  |

## Gesso

src/Game/Enemy/Gesso.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GessoNrvComeFromBox` | `Gesso::exeComeFromBox` |  |
| `GessoNrvWait` | `Gesso::exeWait` |  |
| `GessoNrvSearch` | `Gesso::exeSearch` |  |
| `GessoNrvWalkCharge` | `Gesso::exeWalkCharge` |  |
| `GessoNrvWalk` | `Gesso::exeWalk` |  |
| `GessoNrvSink` | `Gesso::exeSink` |  |
| `GessoNrvLostPlayer` | `Gesso::exeLostPlayer` |  |
| `GessoNrvAttack` | `Gesso::exeAttack` |  |
| `GessoNrvPunchDown` | `Gesso::exePunchDown` |  |
| `GessoNrvComeBack` | `Gesso::exeComeBack` |  |
| `GessoNrvPointerSwoon` | `Gesso::exePointerSwoon` | `Gesso::exePointerSwoon` |
| `GessoNrvRotate` | `Gesso::exeRotate` |  |

## GhostPlayer

src/Game/Player/GhostPlayer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `GhostPlayer::exeWait` |  |
| `HostTypeNrvWinDemo` | `GhostPlayer::exeWinDemo` |  |
| `HostTypeNrvLostDemo` | `GhostPlayer::exeLostDemo` |  |
| `HostTypeNrvPreStartDemo0` | `GhostPlayer::exePreStartDemo0` |  |
| `HostTypeNrvPreStartDemo1` | `GhostPlayer::exePreStartDemo1` |  |
| `HostTypeNrvPreStartDemo2` | `GhostPlayer::exePreStartDemo2` |  |
| `HostTypeNrvStartDemo` | `GhostPlayer::exeWait` |  |
| `HostTypeNrvRun` | `GhostPlayer::exeWait` |  |

## GrandStarReturnDemoStarter

src/Game/Demo/GrandStarReturnDemoStarter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GrandStarReturnDemoStarterNrvMove` | `GrandStarReturnDemoStarter::exeMove` |  |
| `GrandStarReturnDemoStarterNrvFlyWait` | `GrandStarReturnDemoStarter::exeFlyWait` |  |
| `GrandStarReturnDemoStarterNrvRushToCore` | `GrandStarReturnDemoStarter::exeRushToCore` |  |
| `GrandStarReturnDemoStarterNrvRevival` | `GrandStarReturnDemoStarter::exeRevival` |  |
| `GrandStarReturnDemoStarterNrvStageResult` | `GrandStarReturnDemoStarter::exeStageResult` |  |
| `GrandStarReturnDemoStarterNrvFadeOut` | `GrandStarReturnDemoStarter::exeFadeOut` |  |
| `GrandStarReturnDemoStarterNrvWaitDemoEnd` | `GrandStarReturnDemoStarter::exeWaitDemoEnd` |  |

## GravityLight

src/Game/MapObj/GravityLight.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GravityLightNrvWait` | `GravityLight::exeWait` |  |

## GravityLightRoad

src/Game/MapObj/GravityLightRoad.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GravityLightRoadNrvWait` | `GravityLightRoad::exeWait` |  |
| `GravityLightRoadNrvDraw` | `GravityLightRoad::exeDraw` |  |
| `GravityLightRoadNrvDisappear` | `GravityLightRoad::exeDisappear` |  |

## GreenCaterpillarBig

src/Game/MapObj/GreenCaterpillarBig.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `GreenCaterpillarBigNrvHide` | `GreenCaterpillarBig::exeHide` |  |
| `GreenCaterpillarBigNrvWriggle` | `GreenCaterpillarBig::exeWriggle` |  |
| `GreenCaterpillarBigNrvRest` | `GreenCaterpillarBig::exeRest` |  |
| `GreenCaterpillarBigNrvEndAdjust` | `GreenCaterpillarBig::exeEndAdjust` |  |
| `GreenCaterpillarBigNrvEnd` | `GreenCaterpillarBig::exeEnd` |  |

## Halo

src/Game/Map/Halo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeAppear` | `Halo::exeAppear` |  |
| `HostTypeDisappear` | `Halo::exeDisappear` |  |

## HammerHeadPackun

src/Game/Enemy/HammerHeadPackun.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HammerHeadPackunNrvWait` | `HammerHeadPackun::exeWait` |  |
| `HammerHeadPackunNrvSearch` | `HammerHeadPackun::exeSearch` |  |
| `HammerHeadPackunNrvAttack` | `HammerHeadPackun::exeAttack` |  |
| `HammerHeadPackunNrvRest` | `HammerHeadPackun::exeRest` |  |
| `HammerHeadPackunNrvRecover` | `HammerHeadPackun::exeRecover` |  |
| `HammerHeadPackunNrvDamage` | `HammerHeadPackun::exeDamage` |  |
| `HammerHeadPackunNrvRise` | `HammerHeadPackun::exeRise` |  |
| `HammerHeadPackunNrvHit` | `HammerHeadPackun::exeHit` |  |
| `HammerHeadPackunNrvCrow` | `HammerHeadPackun::exeCrow` |  |
| `HammerHeadPackunNrvSmashKill` | `HammerHeadPackun::exeSmashKill` |  |
| `HammerHeadPackunNrvBlowKill` | `HammerHeadPackun::exeBlowKill` |  |
| `HammerHeadPackunNrvNumb` | `HammerHeadPackun::exeNumb` | `HammerHeadPackun::exeNumb` |

## Hanachan

src/Game/Enemy/Hanachan.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HanachanNrvHanachanWalk` | `Hanachan::exeWalk` |  |
| `HanachanNrvHanachanBecomeAngry` | `Hanachan::exeBecomeAngry` |  |
| `HanachanNrvHanachanAngryPursue` | `Hanachan::exeAngryPursue` |  |
| `HanachanNrvHanachanAngryEnd` | `Hanachan::exeAngryEnd` |  |
| `HanachanNrvHanachanWallHitEnd` | `Hanachan::exeWallHitEnd` |  |
| `HanachanNrvHanachanTrample` | `Hanachan::exeTrample` |  |
| `HanachanNrvHanachanOverturn` | `Hanachan::exeOverturn` |  |
| `HanachanNrvHanachanOverturnBound` | `Hanachan::exeOverturnBound` |  |
| `HanachanNrvHanachanOverturnWait` | `Hanachan::exeOverturnWait` |  |
| `HanachanNrvHanachanRecover` | `Hanachan::exeRecover` |  |
| `HanachanNrvHanachanHipDropped` | `Hanachan::exeHipDropped` |  |
| `HanachanNrvHanachanOverturnHipDropped` | `Hanachan::exeHipDropped` |  |
| `HanachanNrvHanachanBlow` | `Hanachan::exeBlow` |  |
| `HanachanNrvHanachanStarPointerBind` | `Hanachan::exeStarPointerBind` | `Hanachan::exeStarPointerBind` |
| `HanachanNrvHanachanStarPointerBindEnd` | `Hanachan::exeWallHitEnd` |  |
| `HanachanNrvHanachanStarPointerBindOverturn` | `Hanachan::exeStarPointerBind` | `Hanachan::exeStarPointerBind` |
| `HanachanNrvHanachanStarPointerBindEndOverturn` | `Hanachan::exeWallHitEnd` |  |

## HanachanParts

src/Game/Enemy/Hanachan.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HanachanPartsNrvWalk` | `HanachanParts::exeWalk` |  |
| `HanachanPartsNrvBecomeAngry` | `HanachanParts::exeBecomeAngry` | `HanachanParts::exeBecomeAngry` |
| `HanachanPartsNrvAngryPursue` | `HanachanParts::exeAngryPursue` | `HanachanParts::exeAngryPursue` |
| `HanachanPartsNrvAngryEnd` | `HanachanParts::exeAngryEnd` |  |
| `HanachanPartsNrvWallHitEnd` | `HanachanParts::exeWallHitEnd` |  |
| `HanachanPartsNrvTrample` | `HanachanParts::exeTrample` |  |
| `HanachanPartsNrvOverturn` | `HanachanParts::exeOverturn` |  |
| `HanachanPartsNrvOverturnBound` | `HanachanParts::exeOverturnBound` |  |
| `HanachanPartsNrvOverturnWait` | `HanachanParts::exeOverturnWait` |  |
| `HanachanPartsNrvRecover` | `HanachanParts::exeRecover` | `HanachanParts::exeRecover` |
| `HanachanPartsNrvHipDropped` | `HanachanParts::exeHipDropped` |  |
| `HanachanPartsNrvOverturnHipDropped` | `HanachanParts::exeHipDropped` |  |
| `HanachanPartsNrvBlow` | `HanachanParts::exeBlow` |  |
| `HanachanPartsNrvStarPointerBind` | `HanachanParts::exeStarPointerBind` | `HanachanParts::exeStarPointerBind` |

## HatchWaterPlanet

src/Game/MapObj/HatchWaterPlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HatchWaterPlanetNrvWait` | `HatchWaterPlanet::exeWait` |  |
| `HatchWaterPlanetNrvOpen` | `HatchWaterPlanet::exeOpen` |  |
| `HatchWaterPlanetNrvWaitAfterOpen` | `HatchWaterPlanet::exeWaitAfterOpen` |  |

## HeavensDoorDemoObj

src/Game/MapObj/HeavensDoorDemoObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HeavensDoorDemoObjNrvAppear` | `HeavensDoorDemoObj::exeAppear` |  |
| `HeavensDoorDemoObjNrvWait` | `HeavensDoorDemoObj::exeWait` |  |
| `HeavensDoorDemoObjNrvVanish` | `HeavensDoorDemoObj::exeVanish` |  |

## HipDropMoveObj

src/Game/MapObj/HipDropMoveObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `HipDropMoveObj::exeWait` |  |
| `HostTypeMove` | `HipDropMoveObj::exeMove` |  |
| `HostTypeDone` | `HipDropMoveObj::exeDone` |  |

## HipDropRock

src/Game/MapObj/HipDropRock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HipDropRockNrvWait` | `HipDropRock::exeWait` |  |
| `HipDropRockNrvBreak` | `HipDropRock::exeBreak` |  |
| `HipDropRockNrvWreck` | `HipDropRock::exeWreck` |  |

## HipDropSwitch

src/Game/MapObj/HipDropSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HipDropSwitchNrvOff` | `HipDropSwitch::exeOff` |  |
| `HipDropSwitchNrvSwitchDown` | `HipDropSwitch::exeSwitchDown` |  |
| `HipDropSwitchNrvOn` | `HipDropSwitch::exeOn` |  |

## HipDropTimerSwitch

src/Game/MapObj/HipDropTimerSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HipDropTimerSwitchNrvOff` | `HipDropTimerSwitch::exeOff` |  |
| `HipDropTimerSwitchNrvSwitchDown` | `HipDropTimerSwitch::exeSwitchDown` |  |
| `HipDropTimerSwitchNrvOn` | `HipDropTimerSwitch::exeOn` |  |
| `HipDropTimerSwitchNrvSwitchUp` | `HipDropTimerSwitch::exeSwitchUp` |  |

## HitWallTimerSwitch

src/Game/MapObj/HitWallTimerSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HitWallTimerSwitchNrvOff` | `HitWallTimerSwitch::exeOff` |  |
| `HitWallTimerSwitchNrvSwitchDown` | `HitWallTimerSwitch::exeSwitchDown` |  |
| `HitWallTimerSwitchNrvOn` | `HitWallTimerSwitch::exeOn` |  |
| `HitWallTimerSwitchNrvSwitchUp` | `HitWallTimerSwitch::exeSwitchUp` |  |

## HomeButtonLayout

src/Game/Screen/HomeButtonLayout.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HomeButtonLayoutDeactive` | `HomeButtonLayout::exeDeactive` |  |
| `HomeButtonLayoutActive` | `HomeButtonLayout::exeActive` |  |

## HomingKiller

src/Game/Enemy/HomingKiller.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HomingKillerNrvAppear` | `HomingKiller::exeAppear` |  |
| `HomingKillerNrvMoveStart` | `HomingKiller::exeMoveStart` |  |
| `HomingKillerNrvMove` | `HomingKiller::exeMove` |  |
| `HomingKillerNrvChaseStart` | `HomingKiller::exeChaseStart` |  |
| `HomingKillerNrvChase` | `HomingKiller::exeChase` |  |
| `HomingKillerNrvFreeze` | `HomingKiller::exeFreeze` |  |
| `HomingKillerNrvBreak` | `HomingKiller::exeBreak` |  |
| `HomingKillerNrvGoToTarget` | `HomingKiller::exeGoToTarget` |  |

## HomingKillerLauncher

src/Game/Enemy/HomingKiller.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HomingKillerLauncherNrvAppearKiller` | `HomingKillerLauncher::exeAppearKiller` |  |
| `HomingKillerLauncherNrvDeadKiller` | `HomingKillerLauncher::exeDeadKiller` |  |

## HoneyBee

src/Game/NPC/HoneyBee.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HoneyBeeNrvWait` | `HoneyBee::exeWait` |  |
| `HoneyBeeNrvReaction` | `HoneyBee::exeReaction` |  |
| `HoneyBeeNrvFly` | `HoneyBee::exeFly` |  |
| `HoneyBeeNrvDropLecture` | `HoneyBee::exeDropLecture` |  |
| `HoneyBeeNrvJumpLecture` | `HoneyBee::exeJumpLecture` |  |
| `HoneyBeeNrvFlyLectureA` | `HoneyBee::exeFlyLectureA` |  |
| `HoneyBeeNrvFlyLectureB` | `HoneyBee::exeFlyLectureB` |  |

## HoneyQueen

src/Game/NPC/HoneyQueen.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HoneyQueenNrvWait` | `HoneyQueen::exeWait` |  |
| `HoneyQueenNrvReady` | `HoneyQueen::exeReady` |  |
| `HoneyQueenNrvDemo` | `HoneyQueen::exeDemo` |  |
| `HoneyQueenNrvFade` | `HoneyQueen::exeFade` |  |
| `HoneyQueenNrvTalk` | `HoneyQueen::exeTalk` |  |
| `HoneyQueenNrvItch` | `HoneyQueen::exeItch` |  |
| `HoneyQueenNrvEvent` | `HoneyQueen::exeEvent` |  |
| `HoneyQueenNrvAfter` | `HoneyQueen::exeAfter` |  |

## IceMerameraKing

src/Game/Enemy/IceMerameraKing.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvSearch` | `IceMerameraKing::exeSearch` |  |
| `HostTypeNrvBeginDemo` | `IceMerameraKing::exeThrow` | `IceMerameraKing::exeThrow` |
| `HostTypeNrvThrow` | `IceMerameraKing::exeThrow` | `IceMerameraKing::exeThrow` |
| `HostTypeNrvThrow2nd` | `IceMerameraKing::exeThrow` | `IceMerameraKing::exeThrow` |
| `HostTypeNrvExtinguish` | `IceMerameraKing::exeExtinguish` |  |
| `HostTypeNrvEscape` | `IceMerameraKing::exeEscape` | `IceMerameraKing::exeEscape` |
| `HostTypeNrvEscapeJump` | `IceMerameraKing::exeEscapeJump` |  |
| `HostTypeNrvDamage` | `IceMerameraKing::exeDamage` |  |
| `HostTypeNrvPreRecover` | `IceMerameraKing::exePreRecover` |  |
| `HostTypeNrvRecover` | `IceMerameraKing::exeRecover` |  |
| `HostTypeNrvPreAttack` | `IceMerameraKing::exePreAttack` |  |
| `HostTypeNrvAttack` | `IceMerameraKing::exeAttack` |  |
| `HostTypeNrvAttackAfter` | `IceMerameraKing::exeAttackAfter` |  |
| `HostTypeNrvAngryDemo1st` | `IceMerameraKing::exeAngryDemo` |  |
| `HostTypeNrvAngryDemo2nd` | `IceMerameraKing::exeAngryDemo` |  |
| `HostTypeNrvDeathDemoWait` | `IceMerameraKing::exeDeathDemoWait` |  |
| `HostTypeNrvDeathDemo` | `IceMerameraKing::exeDeathDemo` |  |

## IceStep

src/Game/MapObj/IceStep.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `IceStepNrvWait` | `IceStep::exeWait` |  |
| `IceStepNrvHit` | `IceStep::exeHit` |  |

## IceStepNoSlip

src/Game/MapObj/WaterLeakPipe.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `IceStepNoSlipNrvAppear` | `IceStepNoSlip::exeAppear` |  |
| `IceStepNoSlipNrvBreak` | `IceStepNoSlip::exeBreak` |  |

## IconAButton

src/Game/Screen/IconAButton.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `IconAButtonNrvOpen` | `IconAButton::exeOpen` |  |
| `IconAButtonNrvWait` | `IconAButton::exeWait` |  |
| `IconAButtonNrvTerm` | `IconAButton::exeTerm` |  |

## IconComet

src/Game/Screen/IconComet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `IconCometNrvWait` | `IconComet::exeWait` |  |

## InformationMessage

src/Game/Screen/InformationMessage.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `InformationMessageNrvAppear` | `InformationMessage::exeAppear` |  |
| `InformationMessageNrvWait` | `InformationMessage::exeWait` |  |
| `InformationMessageNrvDisappear` | `InformationMessage::exeDisappear` |  |

## InformationObserver

src/Game/Screen/InformationObserver.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `InformationObserverNrvWait` | `InformationObserver::exeWait` |  |
| `InformationObserverNrvDisp` | `InformationObserver::exeDisp` |  |

## IronCannonLauncher

src/Game/MapObj/IronCannonShell.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `IronCannonLauncherNrvRelax` | `IronCannonLauncher::exeRelax` |  |
| `IronCannonLauncherNrvWait` | `IronCannonLauncher::exeWait` |  |
| `IronCannonLauncherNrvShot` | `IronCannonLauncher::exeShot` |  |

## IronCannonLauncherPoint

src/Game/MapObj/IronCannonShell.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `IronCannonLauncherPointNrvWait` | `IronCannonLauncherPoint::exeWait` |  |
| `IronCannonLauncherPointNrvShot` | `IronCannonLauncherPoint::exeShot` |  |

## ItemAppearStone

src/Game/MapObj/ItemAppearStone.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `ItemAppearStone::exeWait` |  |

## ItemBlock

src/Game/MapObj/ItemBlock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ItemBlockNrvStandby` | `ItemBlock::exeStandby` |  |
| `ItemBlockNrvWait` | `ItemBlock::exeWait` |  |
| `ItemBlockNrvAppearItem` | `ItemBlock::exeAppearItem` |  |
| `ItemBlockNrvAppearItemSplash` | `ItemBlock::exeAppearItemSplash` |  |

## ItemBubble

src/Game/MapObj/ItemBubble.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ItemBubbleNrvWait` | `ItemBubble::exeWait` |  |
| `ItemBubbleNrvBreak` | `ItemBubble::exeBreak` |  |

## Jellyfish

src/Game/Enemy/Jellyfish.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `JellyfishNrvWait` | `Jellyfish::exeWait` |  |
| `JellyfishNrvWaitWithRightTurn` | `Jellyfish::exeWaitWithRightTurn` |  |
| `JellyfishNrvWaitWithLeftTurn` | `Jellyfish::exeWaitWithLeftTurn` |  |
| `JellyfishNrvFind` | `Jellyfish::exeFind` |  |
| `JellyfishNrvThreat` | `Jellyfish::exeThreat` |  |
| `JellyfishNrvThreatWithRightTurn` | `Jellyfish::exeThreatWithRightTurn` |  |
| `JellyfishNrvThreatWithLeftTurn` | `Jellyfish::exeThreatWithLeftTurn` |  |
| `JellyfishNrvDeath` | `Jellyfish::exeDeath` |  |
| `JellyfishNrvAttack` | `Jellyfish::exeAttack` |  |
| `JellyfishNrvRailGoal` | `Jellyfish::exeRailGoal` |  |
| `JellyfishNrvPointerSwoon` | `Jellyfish::exePointerSwoon` |  |

## JellyfishElectric

src/Game/Enemy/JellyfishElectric.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `JellyfishElectricNrvWait` | `JellyfishElectric::exeWait` |  |
| `JellyfishElectricNrvWaitWithRightTurn` | `JellyfishElectric::exeWaitWithRightTurn` |  |
| `JellyfishElectricNrvWaitWithLeftTurn` | `JellyfishElectric::exeWaitWithLeftTurn` |  |
| `JellyfishElectricNrvDamage` | `JellyfishElectric::exeDamage` |  |
| `JellyfishElectricNrvDeath` | `JellyfishElectric::exeDeath` |  |
| `JellyfishElectricNrvAttack` | `JellyfishElectric::exeAttack` |  |
| `JellyfishElectricNrvRailGoal` | `JellyfishElectric::exeRailGoal` |  |
| `JellyfishElectricNrvPointerSwoon` | `JellyfishElectric::exePointerSwoon` | `JellyfishElectric::exePointerSwoon` |

## JetTurtle

src/Game/MapObj/JetTurtle.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `JetTurtleNrvWait` | `JetTurtle::exeWait` | `JetTurtle::exeWait` |
| `JetTurtleNrvWait2` | `JetTurtle::exeWait` | `JetTurtle::exeWait` |
| `JetTurtleNrvThrowWait` | `JetTurtle::exeThrowWait` |  |
| `JetTurtleNrvThrowing` | `JetTurtle::exeThrowing` |  |
| `JetTurtleNrvTakenReserve` | `JetTurtle::exeWait` |  |
| `JetTurtleNrvTakenReserveD` | `JetTurtle::exeTakenReserve` |  |
| `JetTurtleNrvTakenStart` | `JetTurtle::exeTakenStart` |  |
| `JetTurtleNrvRestart` | `JetTurtle::exeRestart` |  |
| `JetTurtleNrvDrop` | `JetTurtle::exeDrop` |  |

## Jiraira

src/Game/Enemy/Jiraira.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `Jiraira::exeWait` |  |
| `HostTypeNrvStepped` | `Jiraira::exeStepped` |  |
| `HostTypeNrvPreRecover` | `Jiraira::exePreRecover` |  |
| `HostTypeNrvRecover` | `Jiraira::exeRecover` |  |
| `HostTypeNrvSteppedExplode` | `Jiraira::exeExplode` |  |
| `HostTypeNrvExplode` | `Jiraira::exeExplode` |  |

## JumpBeamer

src/Game/Enemy/JumpBeamer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `JumpBeamerNrvHide` | `JumpBeamer::exeHide` |  |
| `JumpBeamerNrvUp` | `JumpBeamer::exeUp` |  |
| `JumpBeamerNrvWait` | `JumpBeamer::exeWait` |  |
| `JumpBeamerNrvDown` | `JumpBeamer::exeDown` |  |
| `JumpBeamerNrvHopStart` | `JumpBeamer::exeHopStart` |  |
| `JumpBeamerNrvHopWait` | `JumpBeamer::exeHopWait` |  |
| `JumpBeamerNrvHopJump` | `JumpBeamer::exeHopJump` |  |
| `JumpBeamerNrvHopEnd` | `JumpBeamer::exeHopEnd` |  |
| `JumpBeamerNrvPreOpen` | `JumpBeamer::exePreOpen` |  |
| `JumpBeamerNrvOpen` | `JumpBeamer::exeOpen` |  |
| `JumpBeamerNrvClose` | `JumpBeamer::exeClose` |  |
| `JumpBeamerNrvInter` | `JumpBeamer::exeInter` |  |

## JumpBranch

src/Game/Ride/JumpBranch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `JumpBranchNrvWait` | `JumpBranch::exeWait` |  |
| `JumpBranchNrvWaitInvalid` | `JumpBranch::exeWaitInvalid` |  |
| `JumpBranchNrvBind` | `JumpBranch::exeBind` |  |

## JumpGuarder

src/Game/Enemy/JumpGuarder.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `JumpGuarderNrvHide` | `JumpGuarder::exeHide` |  |
| `JumpGuarderNrvUp` | `JumpGuarder::exeUp` |  |
| `JumpGuarderNrvWait` | `JumpGuarder::exeWait` |  |
| `JumpGuarderNrvDown` | `JumpGuarder::exeDown` |  |
| `JumpGuarderNrvHopStart` | `JumpGuarder::exeHopStart` |  |
| `JumpGuarderNrvHopWait` | `JumpGuarder::exeHopWait` |  |
| `JumpGuarderNrvHopJump` | `JumpGuarder::exeHopJump` |  |
| `JumpGuarderNrvHopEnd` | `JumpGuarder::exeHopEnd` |  |
| `JumpGuarderNrvPreOpen` | `JumpGuarder::exePreOpen` |  |
| `JumpGuarderNrvOpen` | `JumpGuarder::exeOpen` |  |
| `JumpGuarderNrvClose` | `JumpGuarder::exeClose` |  |
| `JumpGuarderNrvInter` | `JumpGuarder::exeInter` |  |

## JumpHole

src/Game/MapObj/JumpHole.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `JumpHoleNrvWait` | `JumpHole::exeWait` |  |
| `JumpHoleNrvSetCenter` | `JumpHole::exeSetCenter` |  |
| `JumpHoleNrvSetUp` | `JumpHole::exeSetUp` |  |
| `JumpHoleNrvRailMove` | `JumpHole::exeRailMove` |  |

## JumpSpider

src/Game/Enemy/JumpSpider.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `JumpSpider::exeWait` |  |
| `HostTypeNrvPreJump` | `JumpSpider::exePreJump` |  |
| `HostTypeNrvJump` | `JumpSpider::exeJump` |  |
| `HostTypeNrvJumpEnd` | `JumpSpider::exeJumpEnd` |  |
| `HostTypeNrvHitToPlayer` | `JumpSpider::exeHitToPlayer` |  |
| `HostTypeNrvStampFall` | `JumpSpider::exeStampFall` | `JumpSpider::exeStampFall` |
| `HostTypeNrvStampDeath` | `JumpSpider::exeStampDeath` | `JumpSpider::exeStampDeath` |
| `HostTypeNrvHitBlow` | `JumpSpider::exeHitBlow` | `JumpSpider::exeHitBlow` |
| `HostTypeNrvPointerAimed` | `JumpSpider::exePointerAimed` | `JumpSpider::exePointerAimed` |
| `HostTypeNrvPointerAimedEnd` | `JumpSpider::exeNoPowerStart` |  |
| `HostTypeNrvStarPieceHit` | `JumpSpider::exeNoPowerStart` |  |
| `HostTypeNrvPointerAimedLand` | `JumpSpider::exeNoPowerLand` |  |
| `HostTypeNrvStarPieceHitLand` | `JumpSpider::exeNoPowerLand` |  |
| `HostTypeNrvStarPieceHitWait` | `JumpSpider::exeNoPowerWait` |  |
| `HostTypeNrvNoPowerEnd` | `JumpSpider::exeNoPowerEnd` |  |

## JumpStand

src/Game/MapObj/JumpStand.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `JumpStandNrvWait` | `JumpStand::exeWait` |  |
| `JumpStandNrvTrampleBound` | `JumpStand::exeTrampleBound` |  |
| `JumpStandNrvHipDropBound` | `JumpStand::exeHipDropBound` |  |
| `JumpStandNrvStarPieceBound` | `JumpStand::exeStarPieceBound` |  |

## Kabokuri

src/Game/Enemy/Kabokuri.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KabokuriNrvWait` | `Kabokuri::exeWait` |  |
| `KabokuriNrvWalk` | `Kabokuri::exeWalk` |  |
| `KabokuriNrvDropFire` | `Kabokuri::exeDropFire` |  |
| `KabokuriNrvAttacksuccess` | `Kabokuri::exeAttacksuccess` |  |
| `KabokuriNrvTrampled` | `Kabokuri::exeTrampled` |  |
| `KabokuriNrvStarPieceHitted` | `Kabokuri::exeStarPieceHitted` |  |
| `KabokuriNrvBindStarPointer` | `Kabokuri::exeBindStarPointer` | `Kabokuri::exeBindStarPointer` |
| `KabokuriNrvHitAttacked` | `Kabokuri::exeHitAttacked` |  |
| `KabokuriNrvHipDropped` | `Kabokuri::exeHipDropped` |  |
| `KabokuriNrvBreak` | `Kabokuri::exeBreak` |  |

## KabokuriFire

src/Game/Enemy/KabokuriFire.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KabokuriFireNrvWait` | `KabokuriFire::exeWait` |  |

## Kameck

src/Game/Enemy/Kameck.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KameckNrvOpeningDemo` | `Kameck::exeOpeningDemo` |  |
| `KameckNrvDemoAppear` | `Kameck::exeDemoAppear` |  |
| `KameckNrvNonActive` | `Kameck::exeNonActive` |  |
| `KameckNrvAppear` | `Kameck::exeAppear` |  |
| `KameckNrvWait` | `Kameck::exeWait` |  |
| `KameckNrvHide` | `Kameck::exeHide` |  |
| `KameckNrvAttackWait` | `Kameck::exeAttackWait` |  |
| `KameckNrvAttack` | `Kameck::exeAttack` |  |
| `KameckNrvGuard` | `Kameck::exeGuard` |  |
| `KameckNrvHit` | `Kameck::exeHit` |  |
| `KameckNrvMoveHide` | `Kameck::exeMoveHide` |  |
| `KameckNrvMove` | `Kameck::exeMove` |  |
| `KameckNrvBindStarPointer` | `Kameck::exeBindStarPointer` | `Kameck::exeBindStarPointer` |
| `KameckNrvDown` | `Kameck::exeDown` |  |
| `KameckNrvPressDown` | `Kameck::exePressDown` |  |

## KameckBeam

src/Game/Enemy/KameckBeam.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KameckBeamNrvFollowWand` | `KameckBeam::exeFollowWand` |  |
| `KameckBeamNrvShoot` | `KameckBeam::exeShoot` |  |
| `KameckBeamNrvExplosion` | `KameckBeam::exeExplosion` |  |
| `KameckBeamNrvJetTurtle` | `KameckBeam::exeJetTurtle` |  |
| `KameckBeamNrvFire` | `KameckBeam::exeFire` |  |

## KameckFireBall

src/Game/Enemy/KameckFireBall.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KameckFireBallNrvWait` | `KameckFireBall::exeWait` |  |
| `KameckFireBallNrvSweep` | `KameckFireBall::exeSweep` |  |

## KameckTurtle

src/Game/Enemy/KameckTurtle.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KameckTurtleNrvRun` | `KameckTurtle::exeRun` |  |
| `KameckTurtleNrvWait` | `KameckTurtle::exeWait` |  |

## Kanina

src/Game/Enemy/Kanina.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeAppear` | `Kanina::exeAppear` |  |
| `HostTypeWait` | `Kanina::exeWait` |  |
| `HostTypeAttack` | `Kanina::exeAttack` |  |
| `HostTypeVauntAttackSuccess` | `Kanina::exeVauntAttackSuccess` |  |
| `HostTypeGuard` | `Kanina::exeGuard` |  |
| `HostTypeGuardEnd` | `Kanina::exeGuardEnd` |  |
| `HostTypeDamageFireBall` | `Kanina::exeDamageFireBall` |  |
| `HostTypeDamageHipDrop` | `Kanina::exeDamageHipDrop` |  |
| `HostTypeDamageTrampleInvincivle` | `Kanina::exeDamageHipDrop` |  |
| `HostTypeDig` | `Kanina::exeDig` |  |
| `HostTypeWaitUnderGround` | `Kanina::exeWaitUnderGround` |  |
| `HostTypeFindPlayer` | `Kanina::exeFindPlayer` |  |
| `HostTypeRunAwayBreak` | `Kanina::exeRunAwayBreak` |  |
| `HostTypeReboundEach` | `Kanina::exeReboundEach` |  |
| `HostTypeTurn` | `Kanina::exeTurn` |  |
| `HostTypeTurnEnd` | `Kanina::exeTurnEnd` |  |
| `HostTypePointing` | `Kanina::exePointing` | `Kanina::exePointing` |
| `HostTypePointingEnd` | `Kanina::exeGuardEnd` |  |
| `HostTypeWalk` | `Kanina::exeWalk` |  |
| `HostTypeRunAway` | `Kanina::exeRunAway` |  |
| `HostTypeRunAwayReboundDirection` | `Kanina::exeRunAwayReboundDirection` |  |
| `HostTypeHitWall` | `Kanina::exeHitWall` |  |

## Karikari

src/Game/Enemy/Karikari.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvFall` | `Karikari::exeFall` |  |
| `HostTypeNrvLand` | `Karikari::exeLand` |  |
| `HostTypeNrvNoCalcWait` | `Karikari::exeNoCalcWait` |  |
| `HostTypeNrvWait` | `Karikari::exeWait` |  |
| `HostTypeNrvPrePursue` | `Karikari::exePrePursue` |  |
| `HostTypeNrvPursue` | `Karikari::exePursue` |  |
| `HostTypeNrvWatchFor` | `Karikari::exeWatchFor` |  |
| `HostTypeNrvPreCling` | `Karikari::exePreCling` |  |
| `HostTypeNrvCling` | `Karikari::exeCling` |  |
| `HostTypeNrvRelease` | `Karikari::exeRelease` |  |
| `HostTypeNrvSpinAttacked` | `Karikari::exeSpinAttacked` |  |
| `HostTypeNrvPointerAttacked` | `Karikari::exePointerAttacked` |  |
| `HostTypeNrvFrozen` | `Karikari::exeFrozen` |  |
| `HostTypeNrvFrozenRecover` | `Karikari::exeFrozenRecover` |  |
| `HostTypeNrvBlowOut` | `Karikari::exeBlowOut` |  |
| `HostTypeNrvBlowOutStarPiece` | `Karikari::exeBlowOut` |  |
| `HostTypeNrvInTornado` | `Karikari::exeInTornado` |  |
| `HostTypeNrvPress` | `Karikari::exePress` |  |

## Karon

src/Game/Enemy/Karon.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvFixWait` | `Karon::exeFixWait` |  |
| `HostTypeNrvWait` | `Karon::exeWait` |  |
| `HostTypeNrvStarPieceHitWait` | `Karon::exeWait` |  |
| `HostTypeNrvWalk` | `Karon::exeWalk` |  |
| `HostTypeNrvTurn` | `Karon::exeTurn` |  |
| `HostTypeNrvWalkOnRail` | `Karon::exeWalkOnRail` |  |
| `HostTypeNrvSearch` | `Karon::exeSearch` |  |
| `HostTypeNrvPursue` | `Karon::exePursue` | `Karon::exePursue` |
| `HostTypeNrvBroken` | `Karon::exeBroken` |  |
| `HostTypeNrvStepBroken` | `Karon::exeStepBroken` |  |
| `HostTypeNrvRecover` | `Karon::exeRecover` |  |
| `HostTypeNrvHitReaction` | `Karon::exeHitReaction` |  |
| `HostTypeNrvStarPieceHit` | `Karon::exeHitReaction` |  |
| `HostTypeNrvDeath` | `Karon::exeDeath` |  |
| `HostTypeNrvSinkDown` | `Karon::exeSinkDown` |  |
| `HostTypeNrvBindStarPointer` | `Karon::exeBindStarPointer` | `Karon::exeBindStarPointer` |

## KeySwitch

src/Game/MapObj/KeySwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KeySwitchNrvDemoStart` | `KeySwitch::exeDemoStart` |  |
| `KeySwitchNrvAppear` | `KeySwitch::exeAppear` |  |
| `KeySwitchNrvWait` | `KeySwitch::exeWait` |  |

## KillerGunnerSingle

src/Game/MapObj/KillerGunnerSingle.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `KillerGunnerSingle::exeWait` |  |
| `HostTypeCharge` | `KillerGunnerSingle::exeCharge` |  |
| `HostTypeShoot` | `KillerGunnerSingle::exeShoot` |  |
| `HostTypeAttack` | `KillerGunnerSingle::exeAttack` |  |

## Kinopio

src/Game/NPC/Kinopio.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KinopioNrvReaction` | `Kinopio::exeReaction` |  |
| `KinopioNrvTakeOutStar` | `Kinopio::exeTakeOutStar` |  |
| `KinopioNrvFar` | `Kinopio::exeJump` |  |
| `KinopioNrvNear` | `Kinopio::exeJump` |  |
| `KinopioNrvWait` | `Kinopio::exeWait` |  |
| `KinopioNrvStop0` | `Kinopio::exeStop` |  |
| `KinopioNrvStop1` | `Kinopio::exeStop` |  |
| `KinopioNrvAction` | `Kinopio::exeAction` |  |
| `KinopioNrvAppear` | `Kinopio::exeAppear` |  |
| `KinopioNrvEscape` | `Kinopio::exeEscape` |  |
| `KinopioNrvStarWait` | `Kinopio::exeStarWait` |  |
| `KinopioNrvMount` | `Kinopio::exeMount` |  |
| `KinopioNrvEvent` | `Kinopio::exeEvent` |  |
| `KinopioNrvEventAndTalk` | `Kinopio::exeEventAndTalk` |  |
| `KinopioNrvEventTalking` | `Kinopio::exeEventAndTalk` |  |
| `KinopioNrvEventFall` | `Kinopio::exeEventFall` |  |

## Kiraira

src/Game/Enemy/Kiraira.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KirairaNrvWait` | `Kiraira::exeWait` |  |
| `KirairaNrvFaceToMario` | `Kiraira::exeFaceToMario` |  |
| `KirairaNrvFaceToMarioAndStare` | `Kiraira::exeFaceToMarioAndStare` |  |
| `KirairaNrvExplode` | `Kiraira::exeExplode` |  |
| `KirairaNrvBeExploded` | `Kiraira::exeBeExploded` |  |
| `KirairaNrvDead` | `Kiraira::exeDead` |  |
| `KirairaNrvRecoverSign` | `Kiraira::exeRecoverSign` |  |
| `KirairaNrvRecover` | `Kiraira::exeRecover` |  |

## KoopaBattleMain

src/Game/Boss/KoopaBattleMain.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaBattleMainNrvChasePlayer` | `KoopaBattleMain::exeChasePlayer` |  |
| `KoopaBattleMainNrvWander` | `KoopaBattleMain::exeWander` |  |
| `KoopaBattleMainNrvSearch` | `KoopaBattleMain::exeSearch` |  |
| `KoopaBattleMainNrvFind` | `KoopaBattleMain::exeFind` |  |
| `KoopaBattleMainNrvJumpAway` | `KoopaBattleMain::exeJumpAway` |  |
| `KoopaBattleMainNrvGuard` | `KoopaBattleMain::exeGuard` |  |
| `KoopaBattleMainNrvAttackFireShort` | `KoopaBattleMain::exeAttackFireShort` |  |
| `KoopaBattleMainNrvAttackFireLong` | `KoopaBattleMain::exeAttackFireLong` |  |
| `KoopaBattleMainNrvAttackShockWave` | `KoopaBattleMain::exeAttackShockWave` |  |
| `KoopaBattleMainNrvAttackHipDrop` | `KoopaBattleMain::exeAttackHipDrop` |  |
| `KoopaBattleMainNrvAttackSpin` | `KoopaBattleMain::exeAttackSpin` |  |
| `KoopaBattleMainNrvAttackRoll` | `KoopaBattleMain::exeAttackRoll` |  |
| `KoopaBattleMainNrvDamageEscape` | `KoopaBattleMain::exeDamageEscape` |  |

## KoopaBattleMapDamagePlate

src/Game/Map/KoopaBattleMapDamagePlate.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaBattleMapDamagePlateNrvWait` | `KoopaBattleMapDamagePlate::exeWait` |  |
| `KoopaBattleMapDamagePlateNrvBreak` | `KoopaBattleMapDamagePlate::exeBreak` |  |
| `KoopaBattleMapDamagePlateNrvRecover` | `KoopaBattleMapDamagePlate::exeRecover` |  |

## KoopaBattleMapPlanet

src/Game/Map/KoopaBattleMapPlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaBattleMapPlanetNrvWait` | `KoopaBattleMapPlanet::exeWait` |  |
| `KoopaBattleMapPlanetNrvDemoGrandStarAppear` | `KoopaBattleMapPlanet::exeDemoGrandStarAppear` |  |

## KoopaBattleMapStair

src/Game/Map/KoopaBattleMapStair.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaBattleMapStairNrvWaitSwitch` | `KoopaBattleMapStair::exeWaitSwitch` |  |
| `KoopaBattleMapStairNrvWaitKoopaFire` | `KoopaBattleMapStair::exeWaitKoopaFire` |  |
| `KoopaBattleMapStairNrvWaitFall` | `KoopaBattleMapStair::exeWaitFall` |  |
| `KoopaBattleMapStairNrvFall` | `KoopaBattleMapStair::exeFall` |  |
| `KoopaBattleMapStairNrvDisappear` | `KoopaBattleMapStair::exeDisappear` |  |

## KoopaBattleStairsVs1

src/Game/Boss/KoopaBattleStairsVs1.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaBattleStairsVs1NrvWaitDemo` | `KoopaBattleStairsVs1::exeWaitDemo` |  |
| `KoopaBattleStairsVs1NrvDemo` | `KoopaBattleStairsVs1::exeDemo` |  |
| `KoopaBattleStairsVs1NrvWait` | `KoopaBattleStairsVs1::exeWait` |  |
| `KoopaBattleStairsVs1NrvAttackFire` | `KoopaBattleStairsVs1::exeAttackFire` |  |
| `KoopaBattleStairsVs1NrvJumpToNextPosStart` | `KoopaBattleStairsVs1::exeJumpToNextPosStart` |  |
| `KoopaBattleStairsVs1NrvJumpToNextPosLoop` | `KoopaBattleStairsVs1::exeJumpToNextPosLoop` |  |
| `KoopaBattleStairsVs1NrvJumpToNextPosEnd` | `KoopaBattleStairsVs1::exeJumpToNextPosEnd` |  |

## KoopaBattleStairsVs3

src/Game/Boss/KoopaBattleStairsVs3.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaBattleStairsVs3NrvWaitDemo` | `KoopaBattleStairsVs3::exeWaitDemo` |  |
| `KoopaBattleStairsVs3NrvDemo` | `KoopaBattleStairsVs3::exeDemo` |  |
| `KoopaBattleStairsVs3NrvWait` | `KoopaBattleStairsVs3::exeWait` |  |

## KoopaBattleVs3Lv1

src/Game/Boss/KoopaBattleVs3Lv1.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaBattleVs3Lv1NrvChaseRoll` | `KoopaBattleVs3Lv1::exeChaseRoll` |  |
| `KoopaBattleVs3Lv1NrvAttackFire` | `KoopaBattleVs3Lv1::exeAttackFire` |  |
| `KoopaBattleVs3Lv1NrvAttackShockWave` | `KoopaBattleVs3Lv1::exeAttackShockWave` |  |
| `KoopaBattleVs3Lv1NrvDamageReverse` | `KoopaBattleVs3Lv1::exeDamageReverse` |  |
| `KoopaBattleVs3Lv1NrvJumpAway` | `KoopaBattleVs3Lv1::exeJumpAway` |  |
| `KoopaBattleVs3Lv1NrvGuard` | `KoopaBattleVs3Lv1::exeGuard` |  |
| `KoopaBattleVs3Lv1NrvRecover` | `KoopaBattleVs3Lv1::exeRecover` |  |

## KoopaBattleVs3Lv2

src/Game/Boss/KoopaBattleVs3Lv2.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaBattleVs3Lv2NrvAttackRoll` | `KoopaBattleVs3Lv2::exeAttackRoll` |  |
| `KoopaBattleVs3Lv2NrvAttackFire` | `KoopaBattleVs3Lv2::exeAttackFire` |  |
| `KoopaBattleVs3Lv2NrvDamageReverse` | `KoopaBattleVs3Lv2::exeDamageReverse` |  |
| `KoopaBattleVs3Lv2NrvJumpAway` | `KoopaBattleVs3Lv2::exeJumpAway` |  |
| `KoopaBattleVs3Lv2NrvGuard` | `KoopaBattleVs3Lv2::exeGuard` |  |
| `KoopaBattleVs3Lv2NrvRecover` | `KoopaBattleVs3Lv2::exeRecover` |  |

## KoopaDemoFallToPlanetVs3Lv2

src/Game/Boss/KoopaDemoFallToPlanetVs3Lv2.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaDemoFallToPlanetVs3Lv2NrvWaitFall` | `KoopaDemoFallToPlanetVs3Lv2::exeWaitFall` |  |
| `KoopaDemoFallToPlanetVs3Lv2NrvDemoFall` | `KoopaDemoFallToPlanetVs3Lv2::exeDemoFall` |  |
| `KoopaDemoFallToPlanetVs3Lv2NrvFallToLand` | `KoopaDemoFallToPlanetVs3Lv2::exeFallToLand` |  |
| `KoopaDemoFallToPlanetVs3Lv2NrvLand` | `KoopaDemoFallToPlanetVs3Lv2::exeLand` |  |
| `KoopaDemoFallToPlanetVs3Lv2NrvWaitPlayer` | `KoopaDemoFallToPlanetVs3Lv2::exeWaitPlayer` |  |

## KoopaDemoFallToPlanetVs3Lv3

src/Game/Boss/KoopaDemoFallToPlanetVs3Lv3.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaDemoFallToPlanetVs3Lv3NrvWaitFallDemo` | `KoopaDemoFallToPlanetVs3Lv3::exeWaitFallDemo` |  |
| `KoopaDemoFallToPlanetVs3Lv3NrvFallOutside` | `KoopaDemoFallToPlanetVs3Lv3::exeFallOutside` |  |
| `KoopaDemoFallToPlanetVs3Lv3NrvFallInside` | `KoopaDemoFallToPlanetVs3Lv3::exeFallInside` |  |
| `KoopaDemoFallToPlanetVs3Lv3NrvLand` | `KoopaDemoFallToPlanetVs3Lv3::exeLand` |  |
| `KoopaDemoFallToPlanetVs3Lv3NrvWaitPlayer` | `KoopaDemoFallToPlanetVs3Lv3::exeWaitPlayer` |  |

## KoopaDemoJumpToPlanet

src/Game/Boss/KoopaDemoJumpToPlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaDemoJumpToPlanetNrvStart` | `KoopaDemoJumpToPlanet::exeStart` |  |
| `KoopaDemoJumpToPlanetNrvFall` | `KoopaDemoJumpToPlanet::exeFall` |  |
| `KoopaDemoJumpToPlanetNrvLand` | `KoopaDemoJumpToPlanet::exeLand` |  |
| `KoopaDemoJumpToPlanetNrvWaitPlayer` | `KoopaDemoJumpToPlanet::exeWaitPlayer` |  |

## KoopaDemoPowerUp

src/Game/Boss/KoopaDemoPowerUp.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaDemoPowerUpNrvWaitDemo` | `KoopaDemoPowerUp::exeWaitDemo` |  |
| `KoopaDemoPowerUpNrvDemo` | `KoopaDemoPowerUp::exeDemo` |  |

## KoopaFireShort

src/Game/Boss/KoopaFireShort.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaFireShortNrvFly` | `KoopaFireShort::exeFly` |  |
| `KoopaFireShortNrvDisappear` | `KoopaFireShort::exeDisappear` |  |
| `KoopaFireShortNrvErase2P` | `KoopaFireShort::exeErase2P` |  |

## KoopaFireStairs

src/Game/Boss/KoopaFireStairs.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaFireStairsNrvFly` | `KoopaFireStairs::exeFly` |  |
| `KoopaFireStairsNrvBreak` | `KoopaFireStairs::exeBreak` |  |

## KoopaJr

src/Game/NPC/KoopaJr.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `KoopaJr::exeWait` |  |
| `HostTypeReaction` | `KoopaJr::exeReaction` |  |
| `HostTypeReactionEnd` | `KoopaJr::exeReactionEnd` |  |
| `HostTypeShipBattleAppear` | `KoopaJr::exeShipBattleAppear` |  |
| `HostTypeShipBattlePowerUp` | `KoopaJr::exeShipBattlePowerUp` |  |
| `HostTypeShipBattleEscape` | `KoopaJr::exeShipBattleEscape` |  |
| `HostTypeShipBattleShipDamage` | `KoopaJr::exeShipBattleShipDamage` |  |
| `HostTypeShipBattleDemoTalkStart` | `KoopaJr::exeShipBattleDemoTalkStart` |  |
| `HostTypeShipBattleDemoTalkWait` | `KoopaJr::exeShipBattleDemoTalkWait` |  |

## KoopaJrShip

src/Game/Enemy/KoopaJrShip.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeAppear` | `KoopaJrShip::exeAppear` |  |
| `HostTypeMove` | `KoopaJrShip::exeMove` |  |
| `HostTypeMoveFrontAttack` | `KoopaJrShip::exeMoveFrontAttack` |  |
| `HostTypeShoot1` | `KoopaJrShip::exeShoot` |  |
| `HostTypeShoot2` | `KoopaJrShip::exeShoot` |  |
| `HostTypeShootMain` | `KoopaJrShip::exeShootMain` |  |
| `HostTypeStopAtEnd` | `KoopaJrShip::exeStopAtEnd` |  |
| `HostTypePowerUp` | `KoopaJrShip::exePowerUp` |  |
| `HostTypeDamage` | `KoopaJrShip::exeDamage` |  |
| `HostTypeBreakStart` | `KoopaJrShip::exeBreakStart` |  |
| `HostTypeBreak` | `KoopaJrShip::exeBreak` |  |
| `HostTypeBreakEnd` | `KoopaJrShip::exeBreakEnd` |  |
| `HostTypeTurnFront` | `KoopaJrShip::exeTurnFront` |  |

## KoopaJrShipCannonMainShell

src/Game/Enemy/KoopaJrShipCannonMainShell.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeFly` | `KoopaJrShipCannonMainShell::exeFly` |  |

## KoopaJrShipCannonShell

src/Game/Enemy/KoopaJrShipCannonShell.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeFly` | `KoopaJrShipCannonShell::exeFly` |  |
| `HostTypeDown` | `KoopaJrShipCannonShell::exeDown` |  |
| `HostTypeHipDropDown` | `KoopaJrShipCannonShell::exeDown` |  |
| `HostTypeFreeze` | `KoopaJrShipCannonShell::exeFreeze` |  |

## KoopaSequencerVs1

src/Game/Boss/KoopaSequencerVs1.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaSequencerVs1NrvBattleStairs` | `KoopaSequencerVs1::exeBattleStairs` |  |
| `KoopaSequencerVs1NrvDemoBattleStart` | `KoopaSequencerVs1::exeDemoBattleStart` |  |
| `KoopaSequencerVs1NrvDemoJumpToPlanet` | `KoopaSequencerVs1::exeDemoJumpToPlanet` |  |
| `KoopaSequencerVs1NrvBattle` | `KoopaSequencerVs1::exeBattle` |  |
| `KoopaSequencerVs1NrvWaitDemoBattleEnd` | `KoopaSequencerVs1::exeWaitDemoBattleEnd` |  |
| `KoopaSequencerVs1NrvDemoBattleEnd` | `KoopaSequencerVs1::exeDemoBattleEnd` |  |
| `KoopaSequencerVs1NrvWaitDemo` | `KoopaSequencerVs1::exeWaitDemo` |  |

## KoopaSequencerVs2

src/Game/Boss/KoopaSequencerVs2.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaSequencerVs2NrvWaitDemoBattleStart` | `KoopaSequencerVs2::exeWaitDemoBattleStart` |  |
| `KoopaSequencerVs2NrvDemoBattleStart` | `KoopaSequencerVs2::exeDemoBattleStart` |  |
| `KoopaSequencerVs2NrvDemoJumpToPlanet` | `KoopaSequencerVs2::exeDemoJumpToPlanet` |  |
| `KoopaSequencerVs2NrvBattle` | `KoopaSequencerVs2::exeBattle` |  |
| `KoopaSequencerVs2NrvWaitDemoDown` | `KoopaSequencerVs2::exeWaitDemoDown` |  |
| `KoopaSequencerVs2NrvDemoDown` | `KoopaSequencerVs2::exeDemoDown` |  |
| `KoopaSequencerVs2NrvWaitDemo` | `KoopaSequencerVs2::exeWaitDemo` |  |

## KoopaSequencerVs3

src/Game/Boss/KoopaSequencerVs3.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaSequencerVs3NrvWaitPlayer` | `KoopaSequencerVs3::exeWaitPlayer` |  |
| `KoopaSequencerVs3NrvBattleStairs` | `KoopaSequencerVs3::exeBattleStairs` |  |
| `KoopaSequencerVs3NrvDemoBattleStart` | `KoopaSequencerVs3::exeDemoBattleStart` |  |
| `KoopaSequencerVs3NrvDemoJumpToPlanet` | `KoopaSequencerVs3::exeDemoJumpToPlanet` |  |
| `KoopaSequencerVs3NrvBattleVs3Lv1` | `KoopaSequencerVs3::exeBattleVs3Lv1` |  |
| `KoopaSequencerVs3NrvFallToPlanetLv2` | `KoopaSequencerVs3::exeFallToPlanetLv2` |  |
| `KoopaSequencerVs3NrvBattleVs3Lv2` | `KoopaSequencerVs3::exeBattleVs3Lv2` |  |
| `KoopaSequencerVs3NrvFallToPlanetLv3` | `KoopaSequencerVs3::exeFallToPlanetLv3` |  |
| `KoopaSequencerVs3NrvBattleVs3Lv3` | `KoopaSequencerVs3::exeBattleVs3Lv3` |  |
| `KoopaSequencerVs3NrvDemoPowerUpFinal` | `KoopaSequencerVs3::exeDemoPowerUpFinal` |  |
| `KoopaSequencerVs3NrvBattleFinal` | `KoopaSequencerVs3::exeBattleFinal` |  |
| `KoopaSequencerVs3NrvWaitDemoDown` | `KoopaSequencerVs3::exeWaitDemoDown` |  |
| `KoopaSequencerVs3NrvDemoDown` | `KoopaSequencerVs3::exeDemoDown` |  |
| `KoopaSequencerVs3NrvDead` | `KoopaSequencerVs3::exeDead` |  |
| `KoopaSequencerVs3NrvWaitDemo` | `KoopaSequencerVs3::exeWaitDemo` |  |

## KoopaShockWave

src/Game/Boss/KoopaShockWave.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaShockWaveNrvWaveAttack` | `KoopaShockWave::exeWaveAttack` |  |

## KoopaStateAttackFireLong

src/Game/Boss/KoopaStateAttackFireLong.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaStateAttackFireLongNrvStart` | `KoopaStateAttackFireLong::exeStart` |  |
| `KoopaStateAttackFireLongNrvEmit` | `KoopaStateAttackFireLong::exeEmit` |  |

## KoopaStateAttackFireShort

src/Game/Boss/KoopaStateAttackFireShort.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaStateAttackFireShortNrvStart` | `KoopaStateAttackFireShort::exeStart` |  |
| `KoopaStateAttackFireShortNrvEmit` | `KoopaStateAttackFireShort::exeEmit` |  |

## KoopaStateAttackHipDrop

src/Game/Boss/KoopaStateAttackHipDrop.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaStateAttackHipDropNrvRun` | `KoopaStateAttackHipDrop::exeRun` |  |
| `KoopaStateAttackHipDropNrvJumpStart` | `KoopaStateAttackHipDrop::exeJumpStart` |  |
| `KoopaStateAttackHipDropNrvJumpUp` | `KoopaStateAttackHipDrop::exeJumpUp` |  |
| `KoopaStateAttackHipDropNrvJumpWaitAir` | `KoopaStateAttackHipDrop::exeJumpWaitAir` |  |
| `KoopaStateAttackHipDropNrvJumpAttackDown` | `KoopaStateAttackHipDrop::exeJumpAttackDown` |  |
| `KoopaStateAttackHipDropNrvLand` | `KoopaStateAttackHipDrop::exeLand` |  |
| `KoopaStateAttackHipDropNrvDamage` | `KoopaStateAttackHipDrop::exeDamage` |  |

## KoopaStateAttackRoll

src/Game/Boss/KoopaStateAttackRoll.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaStateAttackRollNrvWaitToStart` | `KoopaStateAttackRoll::exeWaitToStart` |  |
| `KoopaStateAttackRollNrvStart` | `KoopaStateAttackRoll::exeStart` |  |
| `KoopaStateAttackRollNrvRollAir` | `KoopaStateAttackRoll::exeRollAir` |  |
| `KoopaStateAttackRollNrvRollGround` | `KoopaStateAttackRoll::exeRollGround` |  |
| `KoopaStateAttackRollNrvEndAir` | `KoopaStateAttackRoll::exeEndAir` |  |
| `KoopaStateAttackRollNrvEndLand` | `KoopaStateAttackRoll::exeEndLand` |  |

## KoopaStateAttackShockWave

src/Game/Boss/KoopaStateAttackShockWave.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaStateAttackShockWaveNrvJumpStart` | `KoopaStateAttackShockWave::exeJumpStart` |  |
| `KoopaStateAttackShockWaveNrvJump` | `KoopaStateAttackShockWave::exeJump` |  |
| `KoopaStateAttackShockWaveNrvJumpFast` | `KoopaStateAttackShockWave::exeJump` |  |
| `KoopaStateAttackShockWaveNrvLand` | `KoopaStateAttackShockWave::exeLand` |  |

## KoopaStateAttackSpin

src/Game/Boss/KoopaStateAttackSpin.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaStateAttackSpinNrvWaitToStart` | `KoopaStateAttackSpin::exeWaitToStart` |  |
| `KoopaStateAttackSpinNrvRun` | `KoopaStateAttackSpin::exeRun` |  |
| `KoopaStateAttackSpinNrvStart` | `KoopaStateAttackSpin::exeStart` |  |
| `KoopaStateAttackSpinNrvSpin` | `KoopaStateAttackSpin::exeSpin` |  |
| `KoopaStateAttackSpinNrvEnd` | `KoopaStateAttackSpin::exeEnd` |  |
| `KoopaStateAttackSpinNrvEven` | `KoopaStateAttackSpin::exeEven` |  |

## KoopaStateChaseRoll

src/Game/Boss/KoopaStateChaseRoll.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaStateChaseRollNrvWaitToStart` | `KoopaStateChaseRoll::exeWaitToStart` |  |
| `KoopaStateChaseRollNrvStart` | `KoopaStateChaseRoll::exeStart` |  |
| `KoopaStateChaseRollNrvRollAir` | `KoopaStateChaseRoll::exeRollAir` |  |
| `KoopaStateChaseRollNrvRollGround` | `KoopaStateChaseRoll::exeRollGround` |  |
| `KoopaStateChaseRollNrvEndAir` | `KoopaStateChaseRoll::exeEndAir` |  |
| `KoopaStateChaseRollNrvEndLand` | `KoopaStateChaseRoll::exeEndLand` |  |

## KoopaStateDamageEscape

src/Game/Boss/KoopaStateDamageEscape.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaStateDamageEscapeNrvEscapeStart` | `KoopaStateDamageEscape::exeEscapeStart` |  |
| `KoopaStateDamageEscapeNrvEscapeStartLoop` | `KoopaStateDamageEscape::exeEscapeStartLoop` |  |
| `KoopaStateDamageEscapeNrvEscapeStartLand` | `KoopaStateDamageEscape::exeEscapeStartLand` |  |
| `KoopaStateDamageEscapeNrvEscapeRun` | `KoopaStateDamageEscape::exeEscapeRun` |  |
| `KoopaStateDamageEscapeNrvEscapeFind` | `KoopaStateDamageEscape::exeEscapeFind` |  |
| `KoopaStateDamageEscapeNrvEscapeEnd` | `KoopaStateDamageEscape::exeEscapeEnd` |  |
| `KoopaStateDamageEscapeNrvDamageTailRunStart` | `KoopaStateDamageEscape::exeDamageTailRunStart` |  |
| `KoopaStateDamageEscapeNrvDamageTailRunStartFinal` | `KoopaStateDamageEscape::exeDamageTailRunStart` |  |
| `KoopaStateDamageEscapeNrvDamageTailRun` | `KoopaStateDamageEscape::exeDamageTailRun` |  |
| `KoopaStateDamageEscapeNrvDamageTailRunFinal` | `KoopaStateDamageEscape::exeDamageTailRun` |  |
| `KoopaStateDamageEscapeNrvDamageTailRunEnd` | `KoopaStateDamageEscape::exeDamageTailRunEnd` |  |
| `KoopaStateDamageEscapeNrvDamageTailRunEndFinal` | `KoopaStateDamageEscape::exeDamageTailRunEnd` |  |
| `KoopaStateDamageEscapeNrvDown` | `KoopaStateDamageEscape::exeDown` |  |
| `KoopaStateDamageEscapeNrvDownLand` | `KoopaStateDamageEscape::exeDownLand` |  |
| `KoopaStateDamageEscapeNrvDownEnd` | `KoopaStateDamageEscape::exeDownEnd` |  |

## KoopaStateGuard

src/Game/Boss/KoopaStateGuard.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaStateGuardNrvGuardFace` | `KoopaStateGuard::exeGuardFace` |  |
| `KoopaStateGuardNrvGuardBody` | `KoopaStateGuard::exeGuardBody` |  |
| `KoopaStateGuardNrvGuardTail` | `KoopaStateGuard::exeGuardTail` |  |

## KoopaStateJumpAway

src/Game/Boss/KoopaStateJumpAway.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaStateJumpAwayNrvJumpStart` | `KoopaStateJumpAway::exeJumpStart` |  |
| `KoopaStateJumpAwayNrvJump` | `KoopaStateJumpAway::exeJump` |  |
| `KoopaStateJumpAwayNrvLand` | `KoopaStateJumpAway::exeLand` |  |

## KoopaSubSequenceBattle

src/Game/Boss/KoopaSubSequenceBattle.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KoopaSubSequenceBattleNrvBattleLv1` | `KoopaSubSequenceBattle::exeBattleLv1` |  |
| `KoopaSubSequenceBattleNrvDemoStartLv2` | `KoopaSubSequenceBattle::exeDemoStartLv2` |  |
| `KoopaSubSequenceBattleNrvBattleLv2` | `KoopaSubSequenceBattle::exeBattleLv2` |  |
| `KoopaSubSequenceBattleNrvDemoStartLv3` | `KoopaSubSequenceBattle::exeDemoStartLv3` |  |
| `KoopaSubSequenceBattleNrvBattleLv3` | `KoopaSubSequenceBattle::exeBattleLv3` |  |

## KoteBug

src/Game/Enemy/KoteBug.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvNoCalcWait` | `KoteBug::exeNoCalcWait` | `KoteBug::exeNoCalcWait` |
| `HostTypeNrvWait` | `KoteBug::exeWait` | `KoteBug::exeWait` |
| `HostTypeNrvSearch` | `KoteBug::exeSearch` |  |
| `HostTypeNrvEscapeSearch` | `KoteBug::exeEscapeSearch` |  |
| `HostTypeNrvPreEscape` | `KoteBug::exePreEscape` |  |
| `HostTypeNrvFlyStart` | `KoteBug::exeFlyStart` |  |
| `HostTypeNrvFlyPursue` | `KoteBug::exeFlyPursue` |  |
| `HostTypeNrvFlyEscape` | `KoteBug::exeFlyPursue` |  |
| `HostTypeNrvFlyPursueLast` | `KoteBug::exeFlyPursueLast` |  |
| `HostTypeNrvOverturnFall` | `KoteBug::exeOverturnFall` |  |
| `HostTypeNrvOverturn` | `KoteBug::exeOverturn` |  |
| `HostTypeNrvPreRecover` | `KoteBug::exePreRecover` |  |
| `HostTypeNrvRecover` | `KoteBug::exeRecover` |  |
| `HostTypeNrvHitToPlayer` | `KoteBug::exeHitToPlayer` |  |
| `HostTypeNrvBlow` | `KoteBug::exeBlow` |  |
| `HostTypeNrvStampDeath` | `KoteBug::exeStampDeath` |  |
| `HostTypeNrvBindStarPointer` | `KoteBug::exeBindStarPointer` | `KoteBug::exeBindStarPointer` |

## Kuribo

src/Game/Enemy/Kuribo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KuriboNrvNonActive` | `Kuribo::exeNonActive` | `Kuribo::exeNonActive` |
| `KuriboNrvAppearFromBox` | `Kuribo::exeAppearFromBox` |  |
| `KuriboNrvBlow` | `Kuribo::exeBlow` |  |
| `KuriboNrvBlowLand` | `Kuribo::exeBlowLand` |  |
| `KuriboNrvWander` | `Kuribo::exeWander` |  |
| `KuriboNrvFindPlayer` | `Kuribo::exeFindPlayer` |  |
| `KuriboNrvChase` | `Kuribo::exeChase` |  |
| `KuriboNrvAttackSuccess` | `Kuribo::exeAttackSuccess` |  |
| `KuriboNrvBindStarPointer` | `Kuribo::exeBindStarPointer` | `Kuribo::exeBindStarPointer` |
| `KuriboNrvStagger` | `Kuribo::exeStagger` |  |
| `KuriboNrvFlatDown` | `Kuribo::exeFlatDown` |  |
| `KuriboNrvHipDropDown` | `Kuribo::exeHipDropDown` |  |
| `KuriboNrvPressDown` | `Kuribo::exePressDown` |  |
| `KuriboNrvBlowDown` | `Kuribo::exeBlowDown` |  |

## KuriboChief

src/Game/Enemy/KuriboChief.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KuriboChiefNrvWander` | `KuriboChief::exeWander` |  |
| `KuriboChiefNrvFindPlayer` | `KuriboChief::exeFindPlayer` |  |
| `KuriboChiefNrvChase` | `KuriboChief::exeChase` |  |
| `KuriboChiefNrvAttackSuccess` | `KuriboChief::exeAttackSuccess` |  |
| `KuriboChiefNrvStagger` | `KuriboChief::exeStagger` |  |
| `KuriboChiefNrvTrample` | `KuriboChief::exeTrample` |  |
| `KuriboChiefNrvBindStarPointer` | `KuriboChief::exeBindStarPointer` | `KuriboChief::exeBindStarPointer` |
| `KuriboChiefNrvBlowDown` | `KuriboChief::exeBlowDown` |  |
| `KuriboChiefNrvBlowDownLand` | `KuriboChief::exeBlowDownLand` |  |

## KuriboMini

src/Game/Enemy/KuriboMini.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `KuriboMiniNrvWander` | `KuriboMini::exeWander` |  |
| `KuriboMiniNrvFindPlayer` | `KuriboMini::exeFindPlayer` |  |
| `KuriboMiniNrvChase` | `KuriboMini::exeChase` |  |
| `KuriboMiniNrvStagger` | `KuriboMini::exeStagger` |  |
| `KuriboMiniNrvBindStarPointer` | `KuriboMini::exeBindStarPointer` | `KuriboMini::exeBindStarPointer` |
| `KuriboMiniNrvAttackSuccess` | `KuriboMini::exeAttackSuccess` |  |
| `KuriboMiniNrvHipDropDown` | `KuriboMini::exeHipDropDown` |  |
| `KuriboMiniNrvPressDown` | `KuriboMini::exePressDown` |  |
| `KuriboMiniNrvFlatDown` | `KuriboMini::exeFlatDown` |  |
| `KuriboMiniNrvBlowDown` | `KuriboMini::exeBlowDown` |  |

## LargeChain

src/Game/MapObj/LargeChain.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `LargeChainNrvWait` | `LargeChain::exeWait` |  |
| `LargeChainNrvBreak` | `LargeChain::exeBreak` |  |

## LavaBallRisingPlanetLava

src/Game/MapObj/LavaBallRisingPlanetLava.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `LavaBallRisingPlanetLavaNrvWaitSmall` | `LavaBallRisingPlanetLava::exeWait` |  |
| `LavaBallRisingPlanetLavaNrvWaitBig` | `LavaBallRisingPlanetLava::exeWait` |  |
| `LavaBallRisingPlanetLavaNrvScaleUp` | `LavaBallRisingPlanetLava::exeScaleUp` |  |
| `LavaBallRisingPlanetLavaNrvScaleDown` | `LavaBallRisingPlanetLava::exeScaleDown` |  |

## LavaFloater

src/Game/MapObj/LavaFloater.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeFloat` | `LavaFloater::exeFloat` |  |
| `HostTypeSink` | `LavaFloater::exeSink` |  |

## LavaGeyser

src/Game/MapObj/LavaGeyser.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWaitSwitch` | `LavaGeyser::exeWaitSwitch` |  |
| `HostTypeNrvWait` | `LavaGeyser::exeWait` |  |
| `HostTypeNrvSign` | `LavaGeyser::exeSign` |  |
| `HostTypeNrvShootUp` | `LavaGeyser::exeShootUp` |  |
| `HostTypeNrvShootKeep` | `LavaGeyser::exeShootKeep` |  |
| `HostTypeNrvShootDown` | `LavaGeyser::exeShootDown` |  |

## LavaHomeSeesawRotator

src/Game/MapObj/LavaHomeSeesawRotator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `LavaHomeSeesawRotator::exeWait` |  |
| `HostTypeMoveStart` | `LavaHomeSeesawRotator::exeMoveStart` |  |
| `HostTypeMove` | `LavaHomeSeesawRotator::exeMove` |  |
| `HostTypeStay` | `LavaHomeSeesawRotator::exeStay` |  |
| `HostTypeHipDrop` | `LavaHomeSeesawRotator::exeHipDrop` |  |

## LavaHomeVolcanoFlow

src/Game/MapObj/LavaHomeVolcanoFlow.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `LavaHomeVolcanoFlow::exeWait` |  |
| `HostTypeMove` | `LavaHomeVolcanoFlow::exeMove` |  |
| `HostTypeDone` | `LavaHomeVolcanoFlow::exeWait` |  |

## LavaJamboSunPlanet

src/Game/MapObj/LavaJamboSunPlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `LavaJamboSunPlanet::exeWait` |  |
| `HostTypePause` | `LavaJamboSunPlanet::exeWait` |  |
| `HostTypeMove` | `LavaJamboSunPlanet::exeMove` |  |

## LavaProminence

src/Game/MapObj/LavaProminence.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWaitSwitch` | `LavaProminence::exeWaitSwitch` |  |
| `HostTypeNrvWait` | `LavaProminence::exeWait` |  |
| `HostTypeNrvSign` | `LavaProminence::exeSign` |  |
| `HostTypeNrvMoveStartExtra` | `LavaProminence::exeMoveStartExtra` |  |
| `HostTypeNrvMoveStart` | `LavaProminence::exeMoveStart` |  |
| `HostTypeNrvMoveLoop` | `LavaProminence::exeMoveLoop` |  |
| `HostTypeNrvMoveEnd` | `LavaProminence::exeMoveEnd` |  |
| `HostTypeNrvMoveEndExtra` | `LavaProminence::exeMoveEndExtra` |  |

## LavaShellTower

src/Game/Map/LavaShellTower.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `LavaShellTower::exeWait` |  |
| `HostTypeDemo` | `LavaShellTower::exeDemo` |  |
| `HostTypeDone` | `LavaShellTower::exeDone` |  |

## LavaSteam

src/Game/MapObj/LavaSteam.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `LavaSteam::exeWait` |  |
| `HostTypeWaitForSwitchOn` | `LavaSteam::exeWaitForSwitchOn` |  |
| `HostTypeSteam` | `LavaSteam::exeSteam` |  |
| `HostTypeSteamEnd` | `LavaSteam::exeSteamEnd` |  |

## LavaStrangeRock

src/Game/MapObj/LavaStrangeRock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `LavaStrangeRockNrvWait` | `LavaStrangeRock::exeWait` |  |

## LensFlareModel

src/Game/Screen/LensFlare.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `LensFlareModelNrvKill` | `LensFlareModel::exeKill` |  |
| `LensFlareModelNrvHide` | `LensFlareModel::exeHide` |  |
| `LensFlareModelNrvShow` | `LensFlareModel::exeShow` |  |
| `LensFlareModelNrvFadeIn` | `LensFlareModel::exeFadeIn` |  |
| `LensFlareModelNrvFadeOut` | `LensFlareModel::exeFadeOut` |  |

## LotusLeaf

src/Game/MapObj/LotusLeaf.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `LotusLeaf::exeWait` |  |
| `HostTypeShakeOnPlayer` | `LotusLeaf::exeShake` |  |
| `HostTypeShake` | `LotusLeaf::exeShake` |  |
| `HostTypeWaitPlayerOn` | `LotusLeaf::exeWaitPlayerOn` |  |

## LuigiLetter

src/Game/Screen/LuigiLetter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvAppear` | `LuigiLetter::exeAppear` |  |
| `HostTypeNrvWait` | `LuigiLetter::exeWait` |  |
| `HostTypeNrvEnd` | `LuigiLetter::exeEnd` |  |

## LuigiNPC

src/Game/NPC/LuigiNPC.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `LuigiNPCNrvWait` | `LuigiNPC::exeWait` |  |
| `LuigiNPCNrvReaction` | `LuigiNPC::exeReaction` |  |
| `LuigiNPCNrvTakeOutStar` | `LuigiNPC::exeTakeOutStar` |  |
| `LuigiNPCNrvAfraidWait` | `LuigiNPC::exeAfraidWait` |  |
| `LuigiNPCNrvAfraidSquat` | `LuigiNPC::exeAfraidSquat` |  |
| `LuigiNPCNrvAfraidRise` | `LuigiNPC::exeAfraidRise` |  |
| `LuigiNPCNrvArrestedWait` | `LuigiNPC::exeArrestedWait` |  |
| `LuigiNPCNrvArrestedJump` | `LuigiNPC::exeArrestedJump` |  |
| `LuigiNPCNrvOnTreeWait` | `LuigiNPC::exeOnTreeWait` |  |
| `LuigiNPCNrvOnTreeTouch` | `LuigiNPC::exeOnTreeTouch` |  |
| `LuigiNPCNrvOnTreeFall` | `LuigiNPC::exeOnTreeFall` |  |
| `LuigiNPCNrvOnTreeLand` | `LuigiNPC::exeOnTreeLand` |  |

## MagicBell

src/Game/MapObj/MagicBell.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MagicBellNrvWait` | `MagicBell::exeWait` |  |
| `MagicBellNrvRing` | `MagicBell::exeRing` |  |

## ManholeCover

src/Game/MapObj/ManholeCover.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `ManholeCover::exeWait` |  |
| `HostTypeOpen` | `ManholeCover::exeOpen` |  |
| `HostTypeRattle` | `ManholeCover::exeRattle` |  |

## Manual2P

src/Game/Screen/Manual2P.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `Manual2PNrvAppear` | `Manual2P::exeAppear` |  |
| `Manual2PNrvWait` | `Manual2P::exeWait` |  |
| `Manual2PNrvScrollRight` | `Manual2P::exeScrollRight` |  |
| `Manual2PNrvScrollRightAfter` | `Manual2P::exeScrollRightAfter` |  |
| `Manual2PNrvScrollLeft` | `Manual2P::exeScrollLeft` |  |
| `Manual2PNrvScrollLeftAfter` | `Manual2P::exeScrollLeftAfter` |  |
| `Manual2PNrvDisappear` | `Manual2P::exeDisappear` |  |

## MapObjActor

src/Game/MapObj/MapObjActor.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `MapObjActor::exeWait` |  |
| `HostTypeMove` | `MapObjActor::exeMove` |  |
| `HostTypeDone` | `MapObjActor::exeDone` |  |

## MapPartsAppearController

src/Game/MapObj/MapPartsAppearController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `MapPartsAppearController::exeWait` |  |
| `HostTypeDisappear` | `MapPartsAppearController::exeDisappear` |  |

## MapPartsFloatingForce

src/Game/MapObj/MapPartsFloatingForce.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `MapPartsFloatingForce::exeWait` |  |
| `HostTypeMove` | `MapPartsFloatingForce::exeMove` |  |
| `HostTypeMoveSpring` | `MapPartsFloatingForce::exeMoveSpring` |  |
| `HostTypeMoveReturn` | `MapPartsFloatingForce::exeMoveReturn` |  |

## MapPartsRailGuideDrawer

src/Game/MapObj/MapPartsRailGuideDrawer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeHideAll` | `MapPartsRailGuideDrawer::exeHideAll` |  |
| `HostTypeDrawAll` | `MapPartsRailGuideDrawer::exeDrawAll` |  |
| `HostTypeDrawForward` | `MapPartsRailGuideDrawer::exeDrawForward` |  |

## MapPartsRailMover

src/Game/MapObj/MapPartsRailMover.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `MapPartsRailMover::exeWait` |  |
| `HostTypeMove` | `MapPartsRailMover::exeMove` |  |
| `HostTypeMoveStart` | `MapPartsRailMover::exeMoveStart` |  |
| `HostTypeStopAtPointBeforeRotate` | `MapPartsRailMover::exeStopAtPoint` |  |
| `HostTypeStopAtPointAfterRotate` | `MapPartsRailMover::exeStopAtPoint` |  |
| `HostTypeStopAtEndBeforeRotate` | `MapPartsRailMover::exeStopAtEnd` |  |
| `HostTypeStopAtEndAfterRotate` | `MapPartsRailMover::exeStopAtEnd` |  |
| `HostTypeStopAtEndWithPlayerOn` | `MapPartsRailMover::exeStopAtEndWithPlayerOn` |  |
| `HostTypeWaitForRestartByPlayerOn` | `MapPartsRailMover::exeWaitForRestartByPlayerOn` |  |
| `HostTypeRotateAtPoint` | `MapPartsRailMover::exeRotateAtPoint` |  |
| `HostTypeRotateAtEndPoint` | `MapPartsRailMover::exeRotateAtEndPoint` |  |
| `HostTypeVanish` | `MapPartsRailMover::exeVanish` |  |

## MapPartsRailPosture

src/Game/MapObj/MapPartsRailPosture.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeDoNothing` | `MapPartsRailPosture::exeDoNothing` |  |
| `HostTypeMove` | `MapPartsRailPosture::exeMove` |  |

## MapPartsRailRotator

src/Game/MapObj/MapPartsRailRotator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `MapPartsRailRotator::exeWait` |  |
| `HostTypeRotateAtPoint` | `MapPartsRailRotator::exeRotate` |  |
| `HostTypeRotateBetweenPoints` | `MapPartsRailRotator::exeRotate` |  |
| `HostTypeDone` | `MapPartsRailRotator::exeDone` |  |

## MapPartsRotator

src/Game/MapObj/MapPartsRotator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNeverMove` | `MapPartsRotator::exeNeverMove` |  |
| `HostTypeWait` | `MapPartsRotator::exeWait` |  |
| `HostTypeRotateStart` | `MapPartsRotator::exeRotateStart` |  |
| `HostTypeRotate` | `MapPartsRotator::exeRotate` |  |
| `HostTypeStopAtEnd` | `MapPartsRotator::exeStopAtEnd` |  |

## MapPartsSeesaw1AxisRotator

src/Game/MapObj/MapPartsSeesaw1AxisRotator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `MapPartsSeesaw1AxisRotator::exeWait` |  |
| `HostTypeMoveStart` | `MapPartsSeesaw1AxisRotator::exeMove` |  |
| `HostTypeMove` | `MapPartsSeesaw1AxisRotator::exeMove` |  |
| `HostTypeStay` | `MapPartsSeesaw1AxisRotator::exeStay` |  |
| `HostTypeHipDrop` | `MapPartsSeesaw1AxisRotator::exeHipDrop` |  |

## MapPartsSeesaw2AxisRotator

src/Game/MapObj/MapPartsSeesaw2AxisRotator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `MapPartsSeesaw2AxisRotator::exeWait` |  |
| `HostTypeStay` | `MapPartsSeesaw2AxisRotator::exeStay` |  |
| `HostTypeMove` | `MapPartsSeesaw2AxisRotator::exeMove` |  |
| `HostTypeHipDrop` | `MapPartsSeesaw2AxisRotator::exeHipDrop` |  |

## MarblePlanet

src/Game/MapObj/MarblePlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MarblePlanetNrvWait` | `MarblePlanet::exeWait` |  |
| `MarblePlanetNrvScaleUpCore` | `MarblePlanet::exeScaleUpCore` |  |
| `MarblePlanetNrvBreakCore` | `MarblePlanet::exeBreakCore` |  |

## MarblePlanetElectron

src/Game/MapObj/MarblePlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MarblePlanetElectronNrvMove` | `MarblePlanetElectron::exeMove` |  |
| `MarblePlanetElectronNrvAttack` | `MarblePlanetElectron::exeAttack` |  |

## MarioActor

include/Game/Player/MarioActor.hpp

| Nerve | Code | On leaving |
|---|---|---|
| `MarioActorNrvWait` | `MarioActor::exeWait` |  |
| `MarioActorNrvGameOver` | `MarioActor::exeGameOver` |  |
| `MarioActorNrvGameOverAbyss` | `MarioActor::exeGameOverAbyss` |  |
| `MarioActorNrvGameOverAbyss2` | `MarioActor::exeGameOverAbyss` |  |
| `MarioActorNrvGameOverFire` | `MarioActor::exeGameOverFire` |  |
| `MarioActorNrvGameOverBlackHole` | `MarioActor::exeGameOverBlackHole2` |  |
| `MarioActorNrvGameOverNonStop` | `MarioActor::exeGameOverNonStop` |  |
| `MarioActorNrvGameOverSink` | `MarioActor::exeGameOverSink` |  |
| `MarioActorNrvTimeWait` | `MarioActor::exeTimeWait` |  |
| `MarioActorNrvNoRush` | `MarioActor::exeWait` |  |

## MarioLauncher

src/Game/MapObj/MarioLauncher.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MarioLauncherNrvWait` | `MarioLauncher::exeWait` |  |
| `MarioLauncherNrvReady` | `MarioLauncher::exeReady` |  |
| `MarioLauncherNrvPlayerIn` | `MarioLauncher::exePlayerIn` |  |
| `MarioLauncherNrvPrep` | `MarioLauncher::exePrep` |  |
| `MarioLauncherNrvAim` | `MarioLauncher::exeAim` |  |
| `MarioLauncherNrvFlight` | `MarioLauncher::exeFlight` |  |
| `MarioLauncherNrvKill` | `MarioLauncher::exeKill` |  |
| `MarioLauncherNrvLand` | `MarioLauncher::exeLand` |  |
| `MarioLauncherNrvReject` | `MarioLauncher::exeReject` |  |

## MarioLauncherAttractor

src/Game/MapObj/MarioLauncherAttractor.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MarioLauncherAttractorNrvWait` | `MarioLauncherAttractor::exeWait` |  |

## MechaKoopaPartsArm

src/Game/MapObj/MechaKoopaPartsArm.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MechaKoopaPartsArmNrvWaitFront` | `MechaKoopaPartsArm::exeWait` |  |
| `MechaKoopaPartsArmNrvWaitRear` | `MechaKoopaPartsArm::exeWait` |  |
| `MechaKoopaPartsArmNrvMoveStartFront` | `MechaKoopaPartsArm::exeMoveStart` | `MechaKoopaPartsArm::exeMoveStart` |
| `MechaKoopaPartsArmNrvMoveStartRear` | `MechaKoopaPartsArm::exeMoveStart` | `MechaKoopaPartsArm::exeMoveStart` |
| `MechaKoopaPartsArmNrvMoveFront` | `MechaKoopaPartsArm::exeMove` |  |
| `MechaKoopaPartsArmNrvMoveRear` | `MechaKoopaPartsArm::exeMove` |  |
| `MechaKoopaPartsArmNrvBreakStart` | `MechaKoopaPartsArm::exeBreakStart` |  |
| `MechaKoopaPartsArmNrvBreak` | `MechaKoopaPartsArm::exeBreak` |  |

## MechaKoopaPartsHead

src/Game/MapObj/MechaKoopaPartsHead.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MechaKoopaPartsHeadNrvWait` | `MechaKoopaPartsHead::exeWait` |  |
| `MechaKoopaPartsHeadNrvDemoBreak` | `MechaKoopaPartsHead::exeDemoBreak` |  |
| `MechaKoopaPartsHeadNrvDemoWhiteFadeOut` | `MechaKoopaPartsHead::exeDemoWhiteFadeOut` |  |
| `MechaKoopaPartsHeadNrvDemoWhiteWait` | `MechaKoopaPartsHead::exeDemoWhiteWait` |  |
| `MechaKoopaPartsHeadNrvDemoWhiteFadeIn` | `MechaKoopaPartsHead::exeDemoWhiteFadeIn` |  |
| `MechaKoopaPartsHeadNrvDemoAppearStar` | `MechaKoopaPartsHead::exeDemoAppearStar` |  |

## MechanicKoopaMini

src/Game/Enemy/MechanicKoopaMini.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `MechanicKoopaMini::exeWait` |  |
| `HostTypeNrvWalkOnRail` | `MechanicKoopaMini::exeWalkOnRail` |  |
| `HostTypeNrvTurn` | `MechanicKoopaMini::exeTurn` |  |
| `HostTypeNrvJumpStart` | `MechanicKoopaMini::exeJumpStart` |  |
| `HostTypeNrvJumpEnd` | `MechanicKoopaMini::exeJumpEnd` |  |
| `HostTypeNrvFind` | `MechanicKoopaMini::exeFind` |  |
| `HostTypeNrvPursue` | `MechanicKoopaMini::exePursue` |  |
| `HostTypeNrvPreFireAttack` | `MechanicKoopaMini::exePreFireAttack` |  |
| `HostTypeNrvFireAttack` | `MechanicKoopaMini::exeFireAttack` | `MechanicKoopaMini::exeFireAttack` |
| `HostTypeNrvFireAttackEnd` | `MechanicKoopaMini::exeFireAttackEnd` |  |
| `HostTypeNrvAttackHit` | `MechanicKoopaMini::exeAttackHit` |  |
| `HostTypeNrvSpinHit` | `MechanicKoopaMini::exeSpinHit` |  |
| `HostTypeNrvTrample` | `MechanicKoopaMini::exeTrample` |  |
| `HostTypeNrvHipDropped` | `MechanicKoopaMini::exeHipDropped` |  |
| `HostTypeNrvBindStarPointer` | `MechanicKoopaMini::exeBindStarPointer` | `MechanicKoopaMini::exeBindStarPointer` |

## Meramera

src/Game/Enemy/Meramera.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MerameraNrvWait` | `Meramera::exeWait` |  |
| `MerameraNrvWalk` | `Meramera::exeWalk` |  |
| `MerameraNrvChaseStart` | `Meramera::exeChaseStart` |  |
| `MerameraNrvChaseDash` | `Meramera::exeChaseDash` |  |
| `MerameraNrvChaseTurn` | `Meramera::exeChaseTurn` |  |
| `MerameraNrvDamage` | `Meramera::exeDamage` |  |
| `MerameraNrvShrink` | `Meramera::exeShrink` |  |
| `MerameraNrvRunaway` | `Meramera::exeRunaway` | `Meramera::exeRunaway` |
| `MerameraNrvStartDiving` | `Meramera::exeStartDiving` | `Meramera::exeRunaway` |
| `MerameraNrvDiving` | `Meramera::exeDiving` |  |
| `MerameraNrvSink` | `Meramera::exeSink` |  |
| `MerameraNrvFloat` | `Meramera::exeFloat` |  |
| `MerameraNrvIgnition` | `Meramera::exeIgnition` |  |
| `MerameraNrvIgnitionForce` | `Meramera::exeIgnitionForce` |  |
| `MerameraNrvAttackSuccess` | `Meramera::exeAttackSuccess` |  |
| `MerameraNrvBindStarPointer` | `Meramera::exeBindStarPointer` | `Meramera::exeBindStarPointer` |
| `MerameraNrvBindStarPointerNoFire` | `Meramera::exeBindStarPointerNoFire` | `Meramera::exeBindStarPointer` |
| `MerameraNrvDown` | `Meramera::exeDown` |  |
| `MerameraNrvFlatDown` | `Meramera::exeFlatDown` |  |
| `MerameraNrvReadyRestart` | `Meramera::exeReadyRestart` |  |

## Metbo

src/Game/Enemy/Metbo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MetboNrvNonActive` | `Metbo::exeNonActive` | `Metbo::exeNonActive` |
| `MetboNrvWait` | `Metbo::exeWait` |  |
| `MetboNrvWalkAround` | `Metbo::exeWalkAround` |  |
| `MetboNrvSearch` | `Metbo::exeSearch` |  |
| `MetboNrvChaseStart` | `Metbo::exeChaseStart` |  |
| `MetboNrvChase` | `Metbo::exeChase` | `Metbo::exeChase` |
| `MetboNrvGuard` | `Metbo::exeGuard` |  |
| `MetboNrvTrampleDown` | `Metbo::exeTrampleDown` |  |
| `MetboNrvBlowDown` | `Metbo::exeBlowDown` |  |
| `MetboNrvAttack` | `Metbo::exeAttack` |  |
| `MetboNrvRest` | `Metbo::exeRest` |  |
| `MetboNrvSwoonStart` | `Metbo::exeSwoonStart` |  |
| `MetboNrvSwoonLand` | `Metbo::exeSwoonLand` |  |
| `MetboNrvSwoonSpin` | `Metbo::exeSwoonSpin` |  |
| `MetboNrvSwoonWait` | `Metbo::exeSwoonWait` |  |
| `MetboNrvRecover` | `Metbo::exeRecover` |  |
| `MetboNrvPointerSwoon` | `Metbo::exePointerSwoon` | `Metbo::exePointerSwoon` |

## MeteoContainer

src/Game/MapObj/MeteoContainer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MeteoContainerNrvWait` | `MeteoContainer::exeWait` |  |
| `MeteoContainerNrvDestroy` | `MeteoContainer::exeDestroy` |  |

## MeteorStrike

src/Game/MapObj/MeteorStrike.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MeteorStrikeNrvMove` | `MeteorStrike::exeMove` |  |
| `MeteorStrikeNrvBreak` | `MeteorStrike::exeBreak` |  |

## MeteorStrikeLauncher

src/Game/MapObj/MeteorStrikeLauncher.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MeteorStrikeLauncherNrvCreate` | `MeteorStrikeLauncher::exeCreate` |  |
| `MeteorStrikeLauncherNrvInterval` | `MeteorStrikeLauncher::exeInterval` |  |

## MeterLayout

src/Game/Screen/MeterLayout.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MeterLayoutNrvHide` | `MeterLayout::exeHide` |  |
| `MeterLayoutNrvAppear` | `MeterLayout::exeAppear` |  |
| `MeterLayoutNrvWait` | `MeterLayout::exeWait` |  |
| `MeterLayoutNrvWaitStart` | `MeterLayout::exeWaitStart` |  |
| `MeterLayoutNrvPowerUp` | `MeterLayout::exePowerUp` |  |
| `MeterLayoutNrvMeterMove` | `MeterLayout::exeMeterMove` |  |
| `MeterLayoutNrvCounterMove` | `MeterLayout::exeCounterMove` |  |
| `MeterLayoutNrvRecover` | `MeterLayout::exeRecover` |  |
| `MeterLayoutNrvDamage` | `MeterLayout::exeDamage` |  |
| `MeterLayoutNrvBreakMeter` | `MeterLayout::exeBreakMeter` |  |
| `MeterLayoutNrvZeroMeter` | `MeterLayout::exeZeroMeter` |  |
| `MeterLayoutNrvZeroMeterBreak` | `MeterLayout::exeZeroMeterBreak` |  |

## MiiConfirmIcon

src/Game/Screen/MiiConfirmIcon.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MiiConfirmIconNrvAppear` | `MiiConfirmIcon::exeAppear` |  |
| `MiiConfirmIconNrvWait` | `MiiConfirmIcon::exeWait` |  |
| `MiiConfirmIconNrvDisappear` | `MiiConfirmIcon::exeDisappear` |  |

## MiiSelect

src/Game/Screen/MiiSelect.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MiiSelectNrvAppear` | `MiiSelect::exeAppear` |  |
| `MiiSelectNrvWait` | `MiiSelect::exeWait` |  |
| `MiiSelectNrvScrollRight` | `MiiSelect::exeScrollRight` |  |
| `MiiSelectNrvScrollLeft` | `MiiSelect::exeScrollLeft` |  |
| `MiiSelectNrvSelected` | `MiiSelect::exeSelected` |  |
| `MiiSelectNrvDisappear` | `MiiSelect::exeDisappear` |  |
| `MiiSelectNrvDummySelected` | `MiiSelect::exeDummySelected` |  |

## MiiSelectIcon

src/Game/Screen/MiiSelectIcon.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MiiSelectIconNrvCreate` | `MiiSelectIcon::exeCreate` |  |
| `MiiSelectIconNrvWait` | `MiiSelectIcon::exeWait` |  |
| `MiiSelectIconNrvSelected` | `MiiSelectIcon::exeSelected` |  |
| `MiiSelectIconNrvInvalid` | `MiiSelectIcon::exeInvalid` |  |

## MiniatureGalaxy

src/Game/MapObj/MiniatureGalaxy.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MiniatureGalaxyNrvWait` | `MiniatureGalaxy::exeWait` |  |
| `MiniatureGalaxyNrvSelected` | `MiniatureGalaxy::exeSelected` |  |
| `MiniatureGalaxyNrvNotSelected` | `MiniatureGalaxy::exeNotSelected` |  |
| `MiniatureGalaxyNrvConfirmed` | `MiniatureGalaxy::exeConfirmed` |  |
| `MiniatureGalaxyNrvOpen` | `MiniatureGalaxy::exeOpen` |  |

## MissLayout

src/Game/Screen/MissLayout.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MissLayoutNrvWait` | `MissLayout::exeWait` |  |
| `MissLayoutNrvActive` | `MissLayout::exeActive` |  |
| `MissLayoutNrvEnd` | `MissLayout::exeEnd` |  |

## Mogu

src/Game/Enemy/Mogu.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvHideWait` | `Mogu::exeHideWait` |  |
| `HostTypeNrvHide` | `Mogu::exeHide` |  |
| `HostTypeNrvAppear` | `Mogu::exeAppear` |  |
| `HostTypeNrvSearch` | `Mogu::exeSearch` |  |
| `HostTypeNrvTurn` | `Mogu::exeSearch` |  |
| `HostTypeNrvThrow` | `Mogu::exeThrow` | `Mogu::exeThrow` |
| `HostTypeNrvSwoonStart` | `Mogu::exeSwoonStart` |  |
| `HostTypeNrvSwoonEnd` | `Mogu::exeSwoonEnd` |  |
| `HostTypeNrvSwoon` | `Mogu::exeSwoon` |  |
| `HostTypeNrvHipDropReaction` | `Mogu::exeHipDropReaction` |  |
| `HostTypeNrvStampDeath` | `Mogu::exeStampDeath` |  |
| `HostTypeNrvHitBlow` | `Mogu::exeHitBlow` |  |

## Mogucchi

src/Game/Enemy/Mogucchi.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MogucchiNrvStroll` | `Mogucchi::exeStroll` | `Mogucchi::exeStroll` |
| `MogucchiNrvAppearDown` | `Mogucchi::exeAppearDown` |  |
| `MogucchiNrvDown` | `Mogucchi::exeDown` |  |
| `MogucchiNrvDive` | `Mogucchi::exeDive` |  |
| `MogucchiNrvScatter` | `Mogucchi::exeScatter` |  |
| `MogucchiNrvDie` | `Mogucchi::exeDie` |  |

## MogucchiHill

src/Game/Enemy/MogucchiHill.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MogucchiHillNrvMove` | `MogucchiHill::exeMove` |  |
| `MogucchiHillNrvWait` | `MogucchiHill::exeWait` |  |

## MogucchiHillPiece

src/Game/Enemy/MogucchiHill.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MogucchiHillPieceNrvPieceAppear` | `MogucchiHillPiece::exeAppear` |  |
| `MogucchiHillPieceNrvPieceWait` | `MogucchiHillPiece::exeWait` |  |
| `MogucchiHillPieceNrvPieceCrumble` | `MogucchiHillPiece::exeCrumble` |  |

## MogucchiShooter

src/Game/Enemy/MogucchiShooter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MogucchiShooterNrvWait` | `MogucchiShooter::exeWait` |  |
| `MogucchiShooterNrvShot` | `MogucchiShooter::exeShot` |  |
| `MogucchiShooterNrvTire` | `MogucchiShooter::exeTire` |  |
| `MogucchiShooterNrvDeathPanic` | `MogucchiShooter::exeDeathPanic` |  |
| `MogucchiShooterNrvExplosion` | `MogucchiShooter::exeExplosion` |  |
| `MogucchiShooterNrvLaugh` | `MogucchiShooter::exeLaugh` |  |
| `MogucchiShooterNrvShock` | `MogucchiShooter::exeShock` |  |
| `MogucchiShooterNrvPanic` | `MogucchiShooter::exePanic` |  |
| `MogucchiShooterNrvHitShock` | `MogucchiShooter::exeHitShock` |  |
| `MogucchiShooterNrvHitPanic` | `MogucchiShooter::exeHitPanic` |  |
| `MogucchiShooterNrvAnger` | `MogucchiShooter::exeAnger` |  |
| `MogucchiShooterNrvStormStart` | `MogucchiShooter::exeStormStart` |  |
| `MogucchiShooterNrvStorm` | `MogucchiShooter::exeStorm` |  |

## MoguStone

src/Game/Enemy/MoguStone.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MoguStoneNrvTaken` | `MoguStone::exeTaken` |  |
| `MoguStoneNrvThrow` | `MoguStone::exeThrow` |  |
| `MoguStoneNrvFall` | `MoguStone::exeFall` |  |

## MorphItemObjNeo

include/Game/MapObj/MorphItemObjNeo.hpp

| Nerve | Code | On leaving |
|---|---|---|
| `MorphItemObjNeoNrvWait` | `MorphItemObjNeo::exeWait` |  |
| `MorphItemObjNeoNrvAppear` | `MorphItemObjNeo::exeAppear` |  |
| `MorphItemObjNeoNrvSwitchAppear` | `MorphItemObjNeo::exeSwitchAppear` |  |
| `MorphItemObjNeoNrvWait2` | `MorphItemObjNeo::exeWait` |  |
| `MorphItemObjNeoNrvFly` | `MorphItemObjNeo::exeFly` |  |
| `MorphItemObjNeoNrvDemo` | `MorphItemObjNeo::exeWait` |  |

## MoviePlayerSimple

src/Game/Screen/MoviePlayerSimple.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `MoviePlayerSimpleOff` | `MoviePlayerSimple::exeSimpleOff` |  |
| `MoviePlayerSimpleOpen` | `MoviePlayerSimple::exeOpen` |  |
| `MoviePlayerSimplePlaying` | `MoviePlayerSimple::exePlaying` |  |
| `MoviePlayerSimpleSuspend` | `MoviePlayerSimple::exeSimpleSuspend` |  |

## MoviePlayingSequence

src/Game/Screen/MoviePlayingSequence.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `MoviePlayingSequence::exeWait` |  |
| `HostTypePlayWait` | `MoviePlayingSequence::exePlayWait` |  |
| `HostTypePlayStart` | `MoviePlayingSequence::exePlayStart` |  |
| `HostTypePlay` | `MoviePlayingSequence::exePlay` |  |
| `HostTypeCloseWipeOnPlaying` | `MoviePlayingSequence::exeCloseWipeOnPlaying` |  |
| `HostTypeEndWait` | `MoviePlayingSequence::exeEndWait` |  |

## MovieSubtitles

src/Game/Screen/MovieSubtitles.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `MovieSubtitles::exeWait` |  |
| `HostTypeTalk` | `MovieSubtitles::exeTalk` |  |
| `HostTypeTalkWait` | `MovieSubtitles::exeTalkWait` |  |

## Further reading

* [Nerves](nerves.md)
