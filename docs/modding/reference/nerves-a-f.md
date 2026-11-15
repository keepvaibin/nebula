---
title: Nerves, classes A to F
shortTitle: Nerves, classes A to F
intro: The nerves of the game's classes whose names start with A to F.
contentType: reference
---

## About this list

This page lists the nerves of the game's classes whose names start with A to F. Each section is one class, with its source file. The Nerve column names the nerve, the Code column names the function that runs every frame in it, and the On leaving column names the function that runs as an object leaves it. For how to use nerves, see [Nerves](nerves.md).

## Air

src/Game/Map/Air.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeIn` | `Air::exeIn` |  |
| `HostTypeOut` | `Air::exeOut` |  |

## AirBubble

src/Game/MapObj/AirBubble.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AirBubbleNrvWait` | `AirBubble::exeWait` |  |
| `AirBubbleNrvMove` | `AirBubble::exeMove` |  |
| `AirBubbleNrvBreak` | `AirBubble::exeBreak` |  |
| `AirBubbleNrvKillWait` | `AirBubble::exeKillWait` |  |

## AirBubbleGenerator

src/Game/MapObj/AirBubbleGenerator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AirBubbleGeneratorNrvWait` | `AirBubbleGenerator::exeWait` |  |
| `AirBubbleGeneratorNrvGenerate` | `AirBubbleGenerator::exeGenerate` |  |

## AnimationRandomPlayer

src/Game/LiveActor/AnimationRandomPlayer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `AnimationRandomPlayer::exeWait` |  |
| `HostTypePlay` | `AnimationRandomPlayer::exePlay` |  |

## AnimScaleController

src/Game/Enemy/AnimScaleController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AnimScaleControllerNrvAnim` | `AnimScaleController::exeAnim` |  |
| `AnimScaleControllerNrvStop` | `AnimScaleController::exeStop` |  |
| `AnimScaleControllerNrvPointerVibration` | `AnimScaleController::exePointerVibration` |  |
| `AnimScaleControllerNrvHitReaction` | `AnimScaleController::exeHitReaction` |  |
| `AnimScaleControllerNrvCrush` | `AnimScaleController::exeCrush` |  |

## AnimStampController

src/Game/Enemy/AnimStampController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostNrvAnim` | `AnimStampController::exeAnim` |  |
| `HostNrvAnimEnd` | `AnimStampController::exeAnimEnd` |  |

## AnmModelObj

src/Game/MapObj/AnmModelObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `AnmModelObj::exeWait` |  |
| `HostTypeMove` | `AnmModelObj::exeMove` |  |
| `HostTypeDone` | `AnmModelObj::exeDone` |  |

## ArrowSwitch

src/Game/MapObj/ArrowSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ArrowSwitchNrvWait` | `ArrowSwitch::exeWait` |  |
| `ArrowSwitchNrvRotate` | `ArrowSwitch::exeRotate` |  |
| `ArrowSwitchNrvLock` | `ArrowSwitch::exeLock` |  |

## ArrowSwitchMulti

src/Game/MapObj/ArrowSwitchMulti.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ArrowSwitchMultiNrvWait` | `ArrowSwitchMulti::exeWait` |  |
| `ArrowSwitchMultiNrvRotate` | `ArrowSwitchMulti::exeRotate` |  |

## AssemblyBlock

src/Game/MapObj/AssemblyBlock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AssemblyBlockNrvWait` | `AssemblyBlock::exeWait` |  |
| `AssemblyBlockNrvAssemble` | `AssemblyBlock::exeAssemble` |  |
| `AssemblyBlockNrvAssembleWait` | `AssemblyBlock::exeAssembleWait` |  |
| `AssemblyBlockNrvReturn` | `AssemblyBlock::exeReturn` |  |
| `AssemblyBlockNrvTimer` | `AssemblyBlock::exeTimer` |  |

## AstroCore

src/Game/MapObj/AstroCore.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroCoreNrvWait` | `AstroCore::exeWait` |  |
| `AstroCoreNrvGrow` | `AstroCore::exeGrow` |  |

## AstroCountDownPlate

src/Game/MapObj/AstroCountDownPlate.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroCountDownPlateNrvDead` | `AstroCountDownPlate::exeWait` |  |
| `AstroCountDownPlateNrvAlive` | `AstroCountDownPlate::exeWait` |  |
| `AstroCountDownPlateNrvRevival` | `AstroCountDownPlate::exeRevival` |  |
| `AstroCountDownPlateNrvCountToZero` | `AstroCountDownPlate::exeCountToZero` |  |

## AstroDome

src/Game/MapObj/AstroDome.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroDomeNrvWait` | `AstroDome::exeWait` |  |
| `AstroDomeNrvDisappear` | `AstroDome::exeDisappear` |  |
| `AstroDomeNrvAppear` | `AstroDome::exeAppear` |  |

## AstroDomeAsteroid

src/Game/MapObj/AstroDomeAsteroid.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroDomeAsteroidNrvWait` | `AstroDomeAsteroid::exeWait` |  |

## AstroDomeBlueStar

src/Game/MapObj/AstroDomeBlueStar.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroDomeBlueStarNrvAppear` | `AstroDomeBlueStar::exeAppear` |  |
| `AstroDomeBlueStarNrvWait` | `AstroDomeBlueStar::exeWait` |  |
| `AstroDomeBlueStarNrvPointing` | `AstroDomeBlueStar::exePointing` |  |
| `AstroDomeBlueStarNrvTryStartBind` | `AstroDomeBlueStar::exeTryStartBind` |  |
| `AstroDomeBlueStarNrvBindTraction` | `AstroDomeBlueStar::exeBindTraction` |  |
| `AstroDomeBlueStarNrvBindHold` | `AstroDomeBlueStar::exeBindHold` |  |
| `AstroDomeBlueStarNrvBindEnd` | `AstroDomeBlueStar::exeBindEnd` |  |
| `AstroDomeBlueStarNrvGalaxySelect` | `AstroDomeBlueStar::exeGalaxySelect` |  |
| `AstroDomeBlueStarNrvGalaxyConfirmStart` | `AstroDomeBlueStar::exeGalaxyConfirmStart` |  |
| `AstroDomeBlueStarNrvGalaxyConfirm` | `AstroDomeBlueStar::exeGalaxyConfirm` |  |
| `AstroDomeBlueStarNrvGalaxyConfirmCancel` | `AstroDomeBlueStar::exeGalaxyConfirmCancel` |  |

## AstroDomeCameraController

src/Game/MapObj/AstroDomeCameraController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroDomeCameraControllerNrvGalaxySelectStart` | `AstroDomeCameraController::exeGalaxySelectStart` |  |
| `AstroDomeCameraControllerNrvGalaxySelect` | `AstroDomeCameraController::exeGalaxySelect` |  |
| `AstroDomeCameraControllerNrvGalaxyConfirmStart` | `AstroDomeCameraController::exeGalaxyConfirmStart` |  |
| `AstroDomeCameraControllerNrvGalaxyConfirm` | `AstroDomeCameraController::exeGalaxyConfirm` |  |
| `AstroDomeCameraControllerNrvGalaxyConfirmCancel` | `AstroDomeCameraController::exeGalaxyConfirmCancel` |  |

## AstroDomeComet

src/Game/MapObj/AstroDomeComet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroDomeCometNrvSelect` | `AstroDomeComet::exeSelect` |  |
| `AstroDomeCometNrvConfirm` | `AstroDomeComet::exeConfirm` |  |

## AstroDomeDemoStarter

src/Game/Demo/AstroDomeDemoStarter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroDomeDemoStarterNrvSpinDriverAppear` | `AstroDomeDemoStarter::exeSpinDriverAppear` |  |
| `AstroDomeDemoStarterNrvSpinDriverStart` | `AstroDomeDemoStarter::exeSpinDriverStart` |  |
| `AstroDomeDemoStarterNrvSpinDriverShoot` | `AstroDomeDemoStarter::exeSpinDriverShoot` |  |
| `AstroDomeDemoStarterNrvJumpOut` | `AstroDomeDemoStarter::exeJumpOut` |  |
| `AstroDomeDemoStarterNrvWhiteOut` | `AstroDomeDemoStarter::exeWhiteOut` |  |

## AstroDomeGalaxySelector

src/Game/MapObj/AstroDomeGalaxySelector.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroDomeGalaxySelectorNrvGalaxySelectStart` | `AstroDomeGalaxySelector::exeGalaxySelectStart` |  |
| `AstroDomeGalaxySelectorNrvGalaxySelect` | `AstroDomeGalaxySelector::exeGalaxySelect` |  |
| `AstroDomeGalaxySelectorNrvGalaxySelectCancel` | `AstroDomeGalaxySelector::exeGalaxySelectCancel` |  |
| `AstroDomeGalaxySelectorNrvGalaxyConfirmStart` | `AstroDomeGalaxySelector::exeGalaxyConfirmStart` |  |
| `AstroDomeGalaxySelectorNrvGalaxyConfirm` | `AstroDomeGalaxySelector::exeGalaxyConfirm` |  |
| `AstroDomeGalaxySelectorNrvGalaxyConfirmCancel` | `AstroDomeGalaxySelector::exeGalaxyConfirmCancel` |  |
| `AstroDomeGalaxySelectorNrvDemoJumpOut` | `AstroDomeGalaxySelector::exeDemoJumpOut` |  |
| `AstroDomeGalaxySelectorNrvDemoDomeLecture` | `AstroDomeGalaxySelector::exeDemoDomeLecture` |  |
| `AstroDomeGalaxySelectorNrvGalaxyMoveLecture` | `AstroDomeGalaxySelector::exeGalaxyMoveLecture` |  |
| `AstroDomeGalaxySelectorNrvWaitStartDemo` | `AstroDomeGalaxySelector::exeWaitStartDemo` |  |

## AstroDomeSky

src/Game/MapObj/AstroDomeSky.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroDomeSkyNrvHide` | `AstroDomeSky::exeHide` |  |
| `AstroDomeSkyNrvAppear` | `AstroDomeSky::exeAppear` |  |
| `AstroDomeSkyNrvWait` | `AstroDomeSky::exeWait` |  |
| `AstroDomeSkyNrvRotateAppear` | `AstroDomeSky::exeRotateAppear` |  |
| `AstroDomeSkyNrvRotateDisappear` | `AstroDomeSky::exeRotateDisappear` |  |
| `AstroDomeSkyNrvDisappear` | `AstroDomeSky::exeDisappear` |  |
| `AstroDomeSkyNrvReturnDemoWait` | `AstroDomeSky::exeReturnDemoWait` |  |
| `AstroDomeSkyNrvJumpOutDemo` | `AstroDomeSky::exeJumpOutDemo` |  |

## AstroMapBoard

src/Game/MapObj/AstroMapBoard.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroMapBoardNrvWait` | `AstroMapBoard::exeWait` |  |

## AstroMapObj

src/Game/MapObj/AstroMapObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `AstroMapObjNrvDead` | `AstroMapObj::exeWait` |  |
| `AstroMapObjNrvAlive` | `AstroMapObj::exeWait` |  |
| `AstroMapObjNrvAliveAfterDemo` | `AstroMapObj::exeWait` |  |
| `AstroMapObjNrvRevival` | `AstroMapObj::exeRevival` |  |
| `AstroMapObjNrvOpen` | `AstroMapObj::exeOpen` |  |

## BallBeamer

src/Game/Enemy/BallBeamer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BallBeamerNrvWait` | `BallBeamer::exeWait` |  |
| `BallBeamerNrvAttack` | `BallBeamer::exeAttack` |  |
| `BallBeamerNrvInter` | `BallBeamer::exeInter` |  |

## Balloonfish

src/Game/Enemy/Balloonfish.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `Balloonfish::exeWait` |  |
| `HostTypeNrvDash` | `Balloonfish::exeDash` |  |
| `HostTypeNrvDashEnd` | `Balloonfish::exeDash` |  |
| `HostTypeNrvStarPointerBind` | `Balloonfish::exeStarPointerBind` | `Balloonfish::exeStarPointerBind` |

## BallOpener

src/Game/MapObj/BallOpener.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BallOpenerNrvWait` | `BallOpener::exeWait` |  |
| `BallOpenerNrvSetCenter` | `BallOpener::exeSetCenter` |  |
| `BallOpenerNrvOpen` | `BallOpener::exeOpen` |  |

## BallRail

src/Game/MapObj/BallRail.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BallRailNrvWait` | `BallRail::exeWait` |  |
| `BallRailNrvSetUp` | `BallRail::exeSetUp` |  |
| `BallRailNrvRun` | `BallRail::exeRun` |  |
| `BallRailNrvNoBind` | `BallRail::exeNoBind` |  |

## Banekiti

src/Game/MapObj/Banekiti.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BanekitiNrvWait` | `Banekiti::exeWait` |  |
| `BanekitiNrvRepel` | `Banekiti::exeRepel` |  |
| `BanekitiNrvPointerSwoon` | `Banekiti::exePointerSwoon` | `Banekiti::exePointerSwoon` |

## BasaBasa

src/Game/Enemy/BasaBasa.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BasaBasaNrvWait` | `BasaBasa::exeWait` |  |
| `BasaBasaNrvAirWait` | `BasaBasa::exeAirWait` |  |
| `BasaBasaNrvChaseStart` | `BasaBasa::exeChaseStart` |  |
| `BasaBasaNrvChase` | `BasaBasa::exeChase` |  |
| `BasaBasaNrvQuickTurnStart` | `BasaBasa::exeQuickTurnStart` |  |
| `BasaBasaNrvQuickTurn` | `BasaBasa::exeQuickTurn` |  |
| `BasaBasaNrvPunchDown` | `BasaBasa::exePunchDown` |  |
| `BasaBasaNrvTrampleDown` | `BasaBasa::exeTrampleDown` |  |
| `BasaBasaNrvAttackStart` | `BasaBasa::exeAttackStart` |  |
| `BasaBasaNrvAttack` | `BasaBasa::exeAttack` |  |
| `BasaBasaNrvAttackEnd` | `BasaBasa::exeAttackEnd` |  |
| `BasaBasaNrvAttackEndRecover` | `BasaBasa::exeAttackEndRecover` |  |
| `BasaBasaNrvHitBack` | `BasaBasa::exeHitBack` |  |
| `BasaBasaNrvHitBackEnd` | `BasaBasa::exeHitBackEnd` |  |
| `BasaBasaNrvComeHome` | `BasaBasa::exeComeHome` |  |
| `BasaBasaNrvAttachCelling` | `BasaBasa::exeAttachCelling` |  |
| `BasaBasaNrvPointerSwoon` | `BasaBasa::exePointerSwoon` | `BasaBasa::exePointerSwoon` |
| `BasaBasaNrvStun` | `BasaBasa::exeStun` |  |

## BatteryInfo

src/Game/Screen/BatteryInfo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BatteryInfoEnought` | `BatteryInfo::exeEnought` |  |
| `BatteryInfoLowAppear` | `BatteryInfo::exeLowAppear` |  |
| `BatteryInfoLow` | `BatteryInfo::exeLow` |  |
| `BatteryInfoLowDisappear` | `BatteryInfo::exeLowDisappear` |  |
| `BatteryInfoLowHide` | `BatteryInfo::exeLowHide` |  |
| `BatteryInfoDisappear` | `BatteryInfo::exeDisappear` |  |
| `BatteryInfoCriticalAppear` | `BatteryInfo::exeCriticalAppear` |  |
| `BatteryInfoCritical` | `BatteryInfo::exeCritical` |  |
| `BatteryInfoNoneAppear` | `BatteryInfo::exeNoneAppear` |  |
| `BatteryInfoNone` | `BatteryInfo::exeNone` |  |
| `BatteryInfoNoneDisappear` | `BatteryInfo::exeNoneDisappear` |  |
| `BatteryInfoNoneHide` | `BatteryInfo::exeNoneHide` |  |

## BattleShipElevator

src/Game/MapObj/BattleShipElevator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BattleShipElevatorNrvWait` | `BattleShipElevator::exeWait` |  |
| `BattleShipElevatorNrvMove` | `BattleShipElevator::exeMove` |  |
| `BattleShipElevatorNrvEnd` | `BattleShipElevator::exeEnd` |  |

## BeamGoRoundPlanet

src/Game/MapObj/BeamGoRoundPlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BeamGoRoundPlanetNrvWait` | `BeamGoRoundPlanet::exeWait` |  |

## BeeFlowerHover

src/Game/MapObj/BeeFlowerHover.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BeeFlowerHoverNrvWait` | `BeeFlowerHover::exeWait` |  |
| `BeeFlowerHoverNrvSoftTouch` | `BeeFlowerHover::exeSoftTouch` |  |
| `BeeFlowerHoverNrvSoftTouchWait` | `BeeFlowerHover::exeSoftTouchWait` |  |
| `BeeFlowerHoverNrvHardTouch` | `BeeFlowerHover::exeHardTouch` |  |
| `BeeFlowerHoverNrvRecover` | `BeeFlowerHover::exeRecover` |  |

## BegomanAttackPermitter

src/Game/Enemy/BegomanBase.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `BegomanAttackPermitter::exeWait` |  |
| `HostTypeNrvReceive` | `BegomanAttackPermitter::exeReceive` |  |
| `HostTypeNrvPermit` | `BegomanAttackPermitter::exePermit` |  |

## BegomanBaby

src/Game/Enemy/BegomanBaby.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvNoCalcWait` | `BegomanBaby::exeNoCalcWait` | `BegomanBaby::exeNoCalcWait` |
| `HostTypeNrvWait` | `BegomanBaby::exeWait` |  |
| `HostTypeNrvSignAttack` | `BegomanBaby::exeSignAttack` |  |
| `HostTypeNrvPursue` | `BegomanBaby::exePursue` |  |
| `HostTypeNrvTurn` | `BegomanBaby::exeTurn` |  |
| `HostTypeNrvBrake` | `BegomanBaby::exeBrake` |  |
| `HostTypeNrvStepBack` | `BegomanBaby::exeStepBack` |  |
| `HostTypeNrvReturn` | `BegomanBaby::exeReturn` |  |
| `HostTypeNrvProvoke` | `BegomanBaby::exeProvoke` |  |
| `HostTypeNrvTrample` | `BegomanBaby::exeTrample` |  |
| `HostTypeNrvHitReaction` | `BegomanBaby::exeHitReaction` |  |
| `HostTypeNrvTired` | `BegomanBaby::exeTired` |  |
| `HostTypeNrvBlow` | `BegomanBaby::exeBlow` |  |
| `HostTypeNrvAfterLaunch` | `BegomanBaby::exeAfterLaunch` |  |
| `HostTypeNrvAfterLaunchOnGround` | `BegomanBaby::exeAfterLaunchOnGround` |  |
| `HostTypeNrvLaunchFromGuarder` | `BegomanBaby::exeLaunchFromGuarder` |  |
| `HostTypeNrvLaunchTurn` | `BegomanBaby::exeLaunchTurn` |  |
| `HostTypeNrvInWater` | `BegomanBaby::exeInWater` |  |
| `HostTypeNrvKeepDistance` | `BegomanBaby::exeKeepDistance` |  |
| `HostTypeNrvBindStarPointer` | `BegomanBaby::exeBindStarPointer` | `BegomanBaby::exeBindStarPointer` |

## BegomanLauncher

src/Game/Enemy/BegomanLauncher.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWaitAfterSwitchOn` | `BegomanLauncher::exeWaitAfterSwitchOn` |  |
| `HostTypeNrvWait` | `BegomanLauncher::exeWait` |  |
| `HostTypeNrvWaitForLaunch` | `BegomanLauncher::exeWaitForLaunch` |  |
| `HostTypeNrvLaunch` | `BegomanLauncher::exeLaunch` |  |
| `HostTypeNrvAppear` | `BegomanLauncher::exeAppear` |  |
| `HostTypeNrvDisappear` | `BegomanLauncher::exeDisappear` |  |

## BegomanSpike

src/Game/Enemy/BegomanSpike.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvNoCalcWait` | `BegomanSpike::exeNoCalcWait` | `BegomanSpike::exeNoCalcWait` |
| `HostTypeNrvWait` | `BegomanSpike::exeWait` | `BegomanSpike::exeWait` |
| `HostTypeNrvSignAttack` | `BegomanSpike::exeSignAttack` | `BegomanSpike::exeHandBlur` |
| `HostTypeNrvPursue` | `BegomanSpike::exePursue` | `BegomanSpike::exePursue` |
| `HostTypeNrvTurn` | `BegomanSpike::exeTurn` | `BegomanSpike::exeTurn` |
| `HostTypeNrvBrake` | `BegomanSpike::exeBrake` |  |
| `HostTypeNrvStepBack` | `BegomanSpike::exeStepBack` |  |
| `HostTypeNrvReturn` | `BegomanSpike::exeReturn` |  |
| `HostTypeNrvProvoke` | `BegomanSpike::exeProvoke` | `BegomanSpike::exeHandBlur` |
| `HostTypeNrvTrampleReaction` | `BegomanSpike::exeTrampleReaction` | `BegomanSpike::exeHandBlur` |
| `HostTypeNrvHitReaction` | `BegomanSpike::exeHitReaction` | `BegomanSpike::exeHandBlur` |
| `HostTypeNrvTired` | `BegomanSpike::exeTired` |  |
| `HostTypeNrvBlow` | `BegomanSpike::exeBlow` |  |
| `HostTypeNrvElectricDeath` | `BegomanSpike::exeElectricDeath` |  |
| `HostTypeNrvBrokenPiece` | `BegomanSpike::exeBrokenPiece` |  |
| `HostTypeNrvInWater` | `BegomanSpike::exeInWater` |  |
| `HostTypeNrvKeepDistance` | `BegomanSpike::exeKeepDistance` |  |
| `HostTypeNrvLaunch` | `BegomanSpike::exeLaunch` |  |
| `HostTypeNrvBindStarPointer` | `BegomanSpike::exeBindStarPointer` | `BegomanSpike::exeBindStarPointer` |

## BegomanSpring

src/Game/Enemy/BegomanSpring.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvNoCalcWait` | `BegomanSpring::exeNoCalcWait` | `BegomanSpring::exeNoCalcWait` |
| `HostTypeNrvWait` | `BegomanSpring::exeWait` | `BegomanSpring::exeWait` |
| `HostTypeNrvSignAttack` | `BegomanSpring::exeSignAttack` |  |
| `HostTypeNrvPursue` | `BegomanSpring::exePursue` | `BegomanSpring::exePursue` |
| `HostTypeNrvTurn` | `BegomanSpring::exeTurn` |  |
| `HostTypeNrvBrake` | `BegomanSpring::exeBrake` |  |
| `HostTypeNrvStepBack` | `BegomanSpring::exeStepBack` |  |
| `HostTypeNrvReturn` | `BegomanSpring::exeReturn` |  |
| `HostTypeNrvProvoke` | `BegomanSpring::exeProvoke` |  |
| `HostTypeNrvShake` | `BegomanSpring::exeShake` |  |
| `HostTypeNrvTrample` | `BegomanSpring::exeTrample` |  |
| `HostTypeNrvHitReaction` | `BegomanSpring::exeHitReaction` |  |
| `HostTypeNrvTired` | `BegomanSpring::exeTired` |  |
| `HostTypeNrvBlow` | `BegomanSpring::exeBlow` |  |
| `HostTypeNrvElectricDeath` | `BegomanSpring::exeElectricDeath` |  |
| `HostTypeNrvHide` | `BegomanSpring::exeHide` | `BegomanSpring::exeHide` |
| `HostTypeNrvHop` | `BegomanSpring::exeHop` |  |
| `HostTypeNrvInWater` | `BegomanSpring::exeInWater` |  |
| `HostTypeNrvKeepDistance` | `BegomanSpring::exeKeepDistance` |  |
| `HostTypeNrvBindStarPointer` | `BegomanSpring::exeBindStarPointer` | `BegomanSpring::exeBindStarPointer` |

## BegomanSpringHead

src/Game/Enemy/BegomanSpringHead.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `BegomanSpringHead::exeWait` |  |
| `HostTypeNrvHopStart` | `BegomanSpringHead::exeHopStart` |  |
| `HostTypeNrvHopWait` | `BegomanSpringHead::exeHopWait` |  |
| `HostTypeNrvHopEnd` | `BegomanSpringHead::exeHopEnd` |  |
| `HostTypeNrvHopJump` | `BegomanSpringHead::exeHopJump` |  |

## BenefitItemObj

include/Game/MapObj/BenefitItemObj.hpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `BenefitItemObj::exeWait` |  |
| `HostTypeNrvShoot` | `BenefitItemObj::exeShoot` |  |
| `HostTypeNrvCatch` | `BenefitItemObj::exeCatch` |  |
| `HostTypeNrvAppearGround` | `BenefitItemObj::exeAppearGround` |  |
| `HostTypeNrvPreEscape` | `BenefitItemObj::exePreEscape` |  |
| `HostTypeNrvEscape` | `BenefitItemObj::exeEscape` |  |

## BigBubble

src/Game/Ride/BigBubble.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BigBubbleNrvAppear` | `BigBubble::exeAppear` |  |
| `BigBubbleNrvWait` | `BigBubble::exeWait` |  |
| `BigBubbleNrvCapture` | `BigBubble::exeCapture` | `BigBubble::exeCapture` |
| `BigBubbleNrvBreak` | `BigBubble::exeBreak` |  |
| `BigBubbleNrvEscape` | `BigBubble::exeEscape` |  |
| `BigBubbleNrvGoal` | `BigBubble::exeGoal` |  |
| `BigBubbleNrvMerged` | `BigBubble::exeMerged` |  |

## BigBubbleGenerator

src/Game/MapObj/BigBubbleGenerator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BigBubbleGeneratorNrvNonActive` | `BigBubbleGenerator::exeNonActive` |  |
| `BigBubbleGeneratorNrvActive` | `BigBubbleGenerator::exeActive` |  |

## BigFan

src/Game/MapObj/BigFan.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BigFanNrvStop` | `BigFan::exeStop` |  |
| `BigFanNrvStart` | `BigFan::exeStart` |  |
| `BigFanNrvWait` | `BigFan::exeWait` |  |

## Birikyu

src/Game/Enemy/Birikyu.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeMove` | `Birikyu::exeMove` |  |
| `HostTypeMoveCircle` | `Birikyu::exeMoveCircle` |  |
| `HostTypeAttack` | `Birikyu::exeAttack` |  |
| `HostTypeAttackWait` | `Birikyu::exeAttackWait` |  |
| `HostTypeWaitAtEdge` | `Birikyu::exeWaitAtEdge` |  |
| `HostTypeStopPointing` | `Birikyu::exeStopPointing` |  |

## BlackHole

src/Game/MapObj/BlackHole.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BlackHoleNrvWait` | `BlackHole::exeWait` |  |
| `BlackHoleNrvDemo` | `BlackHole::exeDemo` |  |
| `BlackHoleNrvDisappear` | `BlackHole::exeDisappear` |  |

## BlinkController

src/Game/Map/FileSelectItem.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BlinkControllerNrvOpen` | `BlinkController::exeOpen` |  |
| `BlinkControllerNrvShut` | `BlinkController::exeShut` |  |
| `BlinkControllerNrvSleep` | `BlinkController::exeSleep` |  |
| `BlinkControllerNrvBlink` | `BlinkController::exeBlink` |  |

## BlueStarCupsulePlanet

src/Game/MapObj/BlueStarCupsulePlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BlueStarCupsulePlanetNrvWait` | `BlueStarCupsulePlanet::exeWait` |  |
| `BlueStarCupsulePlanetNrvPointable` | `BlueStarCupsulePlanet::exePointable` |  |
| `BlueStarCupsulePlanetNrvHitPointer` | `BlueStarCupsulePlanet::exeHitPointer` |  |
| `BlueStarCupsulePlanetNrvActive` | `BlueStarCupsulePlanet::exeActive` |  |

## BombBird

src/Game/Enemy/BombBird.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvFlyOnRail` | `BombBird::exeFlyOnRail` |  |
| `HostTypeNrvFlyOnRailSearch` | `BombBird::exeFlyOnRailSearch` |  |
| `HostTypeNrvFlyWithAttack` | `BombBird::exeFlyWithAttack` |  |
| `HostTypeNrvBlow` | `BombBird::exeBlow` |  |
| `HostTypeNrvEscape` | `BombBird::exeEscape` |  |
| `HostTypeNrvDamage` | `BombBird::exeDamage` |  |
| `HostTypeNrvFall` | `BombBird::exeFall` |  |
| `HostTypeNrvSwoonStart` | `BombBird::exeSwoonStart` |  |
| `HostTypeNrvSwoon` | `BombBird::exeSwoon` |  |
| `HostTypeNrvSwoonEnd` | `BombBird::exeSwoonEnd` |  |
| `HostTypeNrvTrample` | `BombBird::exeTrample` |  |
| `HostTypeNrvSinkDown` | `BombBird::exeSinkDown` |  |
| `HostTypeNrvBindStarPointer` | `BombBird::exeBindStarPointer` | `BombBird::exeBindStarPointer` |

## BombBirdBomb

src/Game/Enemy/BombBirdBomb.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvHold` | `BombBirdBomb::exeHold` |  |
| `HostTypeNrvWait` | `BombBirdBomb::exeWait` |  |
| `HostTypeNrvExplosion` | `BombBirdBomb::exeExplosion` |  |

## BombHei

src/Game/Enemy/BombHei.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvLaunch` | `BombHei::exeLaunch` |  |
| `HostTypeNrvStarting` | `BombHei::exeStarting` |  |
| `HostTypeNrvWait` | `BombHei::exeWait` |  |
| `HostTypeNrvWalk` | `BombHei::exeWalk` |  |
| `HostTypeNrvPursue` | `BombHei::exePursue` |  |
| `HostTypeNrvPursueFast` | `BombHei::exePursue` |  |
| `HostTypeNrvPursueHit` | `BombHei::exePursueHit` |  |
| `HostTypeNrvSpinHit` | `BombHei::exeSpinHit` | `BombHei::exeSpinHit` |
| `HostTypeNrvTrample` | `BombHei::exeTrample` |  |
| `HostTypeNrvPhysics` | `BombHei::exePhysics` |  |
| `HostTypeNrvStop` | `BombHei::exeStop` |  |
| `HostTypeNrvTaken` | `BombHei::exeTaken` | `BombHei::exeTaken` |
| `HostTypeNrvThrown` | `BombHei::exeThrown` | `BombHei::exeThrown` |
| `HostTypeNrvExplode` | `BombHei::exeExplode` |  |
| `HostTypeNrvBindStarPointer` | `BombHei::exeBindStarPointer` | `BombHei::exeBindStarPointer` |

## BombHeiLauncher

src/Game/Enemy/BombHeiLauncher.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvLauncherWait` | `BombHeiLauncher::exeLauncherWait` |  |
| `HostTypeNrvStop` | `BombHeiLauncher::exeStop` |  |

## BombTeresa

src/Game/Enemy/BombTeresa.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BombTeresaNrvAppear` | `BombTeresa::exeAppear` |  |
| `BombTeresaNrvShadowAppear` | `BombTeresa::exeShadowAppear` |  |
| `BombTeresaNrvBallAppear` | `BombTeresa::exeBallAppear` |  |
| `BombTeresaNrvWait` | `BombTeresa::exeWait` |  |
| `BombTeresaNrvWander` | `BombTeresa::exeWander` |  |
| `BombTeresaNrvChase` | `BombTeresa::exeChase` |  |
| `BombTeresaNrvAttackTongueFailed` | `BombTeresa::exeAttackTongueFailed` |  |
| `BombTeresaNrvDirectTackleSign` | `BombTeresa::exeDirectTackleSign` |  |
| `BombTeresaNrvDirectTackle` | `BombTeresa::exeDirectTackle` |  |
| `BombTeresaNrvDrift` | `BombTeresa::exeDrift` |  |
| `BombTeresaNrvDriftRelease` | `BombTeresa::exeDriftRelease` |  |
| `BombTeresaNrvBindStarPointer` | `BombTeresa::exeBindStarPointer` | `BombTeresa::exeBindStarPointer` |
| `BombTeresaNrvExplosion` | `BombTeresa::exeExplosion` |  |
| `BombTeresaNrvDisperse` | `BombTeresa::exeDisperse` |  |
| `BombTeresaNrvShock` | `BombTeresa::exeShock` |  |
| `BombTeresaNrvReadyRestart` | `BombTeresa::exeReadyRestart` |  |

## BombTimerLayout

src/Game/Screen/BombTimerLayout.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BombTimerLayoutNrvAppear` | `BombTimerLayout::exeAppear` |  |
| `BombTimerLayoutNrvWait` | `BombTimerLayout::exeWait` |  |
| `BombTimerLayoutNrvDanger` | `BombTimerLayout::exeDanger` |  |
| `BombTimerLayoutNrvEnd` | `BombTimerLayout::exeEnd` |  |

## BootScene

src/Game/Scene/BootScene.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BootSceneMountGameData` | `BootScene::exeMountGameData` |  |
| `BootSceneWaitPreparation` | `BootScene::exeWaitPreparation` |  |
| `BootSceneWaitReadDoneSystemArchive` | `BootScene::exeWaitReadDoneSystemArchive` |  |
| `BootSceneDeactive` | `BootScene::exeDeactive` |  |

## BossBegoman

src/Game/Boss/BossBegoman.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvPreDemoWait` | `BossBegoman::exePreDemoWait` | `BossBegoman::exePreDemoWait` |
| `HostTypeNrvFirstContactDemo` | `BossBegoman::exeFirstContactDemo` |  |
| `HostTypeNrvReady` | `BossBegoman::exeReady` |  |
| `HostTypeNrvNoCalcWait` | `BossBegoman::exeNoCalcWait` | `BossBegoman::exeNoCalcWait` |
| `HostTypeNrvWait` | `BossBegoman::exeWait` |  |
| `HostTypeNrvSignAttack` | `BossBegoman::exeSignAttack` |  |
| `HostTypeNrvPursue` | `BossBegoman::exePursue` |  |
| `HostTypeNrvTurn` | `BossBegoman::exeTurn` | `BossBegoman::exeTurn` |
| `HostTypeNrvOnWeak` | `BossBegoman::exeOnWeak` | `BossBegoman::exeOnWeak` |
| `HostTypeNrvOnWeakTurn` | `BossBegoman::exeOnWeak` |  |
| `HostTypeNrvBrake` | `BossBegoman::exeBrake` |  |
| `HostTypeNrvStepBack` | `BossBegoman::exeStepBack` |  |
| `HostTypeNrvStepBackOnWeak` | `BossBegoman::exeStepBack` |  |
| `HostTypeNrvReturn` | `BossBegoman::exeReturn` |  |
| `HostTypeNrvProvoke` | `BossBegoman::exeProvoke` |  |
| `HostTypeNrvTrampleReaction` | `BossBegoman::exeTrampleReaction` |  |
| `HostTypeNrvAware` | `BossBegoman::exeAware` |  |
| `HostTypeNrvHitReaction` | `BossBegoman::exeHitReaction` |  |
| `HostTypeNrvBlow` | `BossBegoman::exeBlow` |  |
| `HostTypeNrvElectricDeath` | `BossBegoman::exeElectricDeath` |  |
| `HostTypeNrvElectricReturn` | `BossBegoman::exeElectricReturn` |  |
| `HostTypeNrvJumpToInitPos` | `BossBegoman::exeJumpToInitPos` |  |
| `HostTypeNrvKeepDistance` | `BossBegoman::exeKeepDistance` |  |

## BossBegomanHead

src/Game/Boss/BossBegomanHead.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvDemoWait` | `BossBegomanHead::exeDemoWait` |  |
| `HostTypeNrvOpeningDemo` | `BossBegomanHead::exeOpeningDemo` |  |
| `HostTypeNrvOnWait` | `BossBegomanHead::exeOnWait` |  |
| `HostTypeNrvOffWait` | `BossBegomanHead::exeOffWait` |  |
| `HostTypeNrvSwitchOn` | `BossBegomanHead::exeSwitchOn` |  |
| `HostTypeNrvSwitchOff` | `BossBegomanHead::exeSwitchOff` |  |
| `HostTypeNrvTurn` | `BossBegomanHead::exeTurn` |  |
| `HostTypeNrvTurnEnd` | `BossBegomanHead::exeTurnEnd` |  |

## BossKameckBarrier

src/Game/Boss/BossKameckBarrier.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossKameckBarrierNrvWait` | `BossKameckBarrier::exeWait` |  |

## BossKameckBattleDemo

src/Game/Boss/BossKameckBattleDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossKameckBattleDemoNrvTryStartDemo` | `BossKameckBattleDemo::exeTryStartDemo` |  |
| `BossKameckBattleDemoNrvWaitAppearVs1` | `BossKameckBattleDemo::exeWaitAppearVs1` |  |
| `BossKameckBattleDemoNrvAppearVs1` | `BossKameckBattleDemo::exeAppearVs1` |  |
| `BossKameckBattleDemoNrvPowerUpVs1` | `BossKameckBattleDemo::exePowerUpVs1` |  |
| `BossKameckBattleDemoNrvDownVs1` | `BossKameckBattleDemo::exeDownVs1` |  |
| `BossKameckBattleDemoNrvWaitAppearVs2` | `BossKameckBattleDemo::exeWaitAppearVs2` |  |
| `BossKameckBattleDemoNrvAppearVs2` | `BossKameckBattleDemo::exeAppearVs2` |  |
| `BossKameckBattleDemoNrvPowerUpVs2` | `BossKameckBattleDemo::exePowerUpVs2` |  |
| `BossKameckBattleDemoNrvDownVs2` | `BossKameckBattleDemo::exeDownVs2` |  |

## BossKameckStateBattle

src/Game/Boss/BossKameckStateBattle.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossKameckStateBattleNrvWait` | `BossKameckStateBattle::exeWait` |  |
| `BossKameckStateBattleNrvMove` | `BossKameckStateBattle::exeMove` |  |
| `BossKameckStateBattleNrvHideMoveStart` | `BossKameckStateBattle::exeHideMoveStart` |  |
| `BossKameckStateBattleNrvHideMove` | `BossKameckStateBattle::exeHideMove` |  |
| `BossKameckStateBattleNrvHideMoveEnd` | `BossKameckStateBattle::exeHideMoveEnd` |  |
| `BossKameckStateBattleNrvSummonKameckWait` | `BossKameckStateBattle::exeSummonKameckWait` |  |
| `BossKameckStateBattleNrvSummonKameck` | `BossKameckStateBattle::exeSummonKameck` |  |
| `BossKameckStateBattleNrvAttackWait` | `BossKameckStateBattle::exeAttackWait` |  |
| `BossKameckStateBattleNrvAttack` | `BossKameckStateBattle::exeAttack` |  |
| `BossKameckStateBattleNrvDamage` | `BossKameckStateBattle::exeDamage` |  |
| `BossKameckStateBattleNrvRecover` | `BossKameckStateBattle::exeRecover` |  |
| `BossKameckStateBattleNrvGuard` | `BossKameckStateBattle::exeGuard` |  |

## BossKameckVs1

src/Game/Boss/BossKameckVs1.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossKameckVs1NrvOpeningDemo` | `BossKameckVs1::exeOpeningDemo` |  |
| `BossKameckVs1NrvBattleLv1` | `BossKameckVs1::exeBattleLv1` |  |
| `BossKameckVs1NrvBattleLv2` | `BossKameckVs1::exeBattleLv2` |  |
| `BossKameckVs1NrvPowerUpDemo` | `BossKameckVs1::exePowerUpDemo` |  |
| `BossKameckVs1NrvBattleLv3` | `BossKameckVs1::exeBattleLv3` |  |
| `BossKameckVs1NrvEndDemo` | `BossKameckVs1::exeEndDemo` |  |

## BossKameckVs2

src/Game/Boss/BossKameckVs2.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossKameckVs2NrvOpeningDemo` | `BossKameckVs2::exeOpeningDemo` |  |
| `BossKameckVs2NrvBattleLv1` | `BossKameckVs2::exeBattleLv1` |  |
| `BossKameckVs2NrvBattleLv2` | `BossKameckVs2::exeBattleLv2` |  |
| `BossKameckVs2NrvPowerUpDemo` | `BossKameckVs2::exePowerUpDemo` |  |
| `BossKameckVs2NrvBattleLv3` | `BossKameckVs2::exeBattleLv3` |  |
| `BossKameckVs2NrvBattleLv4` | `BossKameckVs2::exeBattleLv4` |  |
| `BossKameckVs2NrvEndDemo` | `BossKameckVs2::exeEndDemo` |  |

## BossStinkBugActionFlyHigh

src/Game/Boss/BossStinkBugActionFlyHigh.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossStinkBugActionFlyHighNrvFlyDash` | `BossStinkBugActionFlyHigh::exeFlyDash` |  |
| `BossStinkBugActionFlyHighNrvFly` | `BossStinkBugActionFlyHigh::exeFly` |  |
| `BossStinkBugActionFlyHighNrvShakeOffSign` | `BossStinkBugActionFlyHigh::exeShakeOffSign` |  |
| `BossStinkBugActionFlyHighNrvShakeOff` | `BossStinkBugActionFlyHigh::exeShakeOff` |  |
| `BossStinkBugActionFlyHighNrvShakeOffWait` | `BossStinkBugActionFlyHigh::exeShakeOffWait` |  |
| `BossStinkBugActionFlyHighNrvShakeOffEnd` | `BossStinkBugActionFlyHigh::exeShakeOffEnd` |  |
| `BossStinkBugActionFlyHighNrvRollSign` | `BossStinkBugActionFlyHigh::exeRollSign` |  |
| `BossStinkBugActionFlyHighNrvRoll` | `BossStinkBugActionFlyHigh::exeRoll` |  |
| `BossStinkBugActionFlyHighNrvRollWait` | `BossStinkBugActionFlyHigh::exeRollWait` |  |
| `BossStinkBugActionFlyHighNrvRollEnd` | `BossStinkBugActionFlyHigh::exeRollEnd` |  |
| `BossStinkBugActionFlyHighNrvTurnSign` | `BossStinkBugActionFlyHigh::exeTurnSign` |  |
| `BossStinkBugActionFlyHighNrvTurn` | `BossStinkBugActionFlyHigh::exeTurn` |  |
| `BossStinkBugActionFlyHighNrvFall` | `BossStinkBugActionFlyHigh::exeFall` |  |
| `BossStinkBugActionFlyHighNrvToGround` | `BossStinkBugActionFlyHigh::exeToGround` |  |
| `BossStinkBugActionFlyHighNrvLand` | `BossStinkBugActionFlyHigh::exeLand` |  |
| `BossStinkBugActionFlyHighNrvGroundRun` | `BossStinkBugActionFlyHigh::exeGroundRun` |  |
| `BossStinkBugActionFlyHighNrvToFly` | `BossStinkBugActionFlyHigh::exeToFly` |  |
| `BossStinkBugActionFlyHighNrvDamage` | `BossStinkBugActionFlyHigh::exeDamage` |  |

## BossStinkBugActionFlyLow

src/Game/Boss/BossStinkBugActionFlyLow.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossStinkBugActionFlyLowNrvFly` | `BossStinkBugActionFlyLow::exeFly` |  |
| `BossStinkBugActionFlyLowNrvShakeOffSign` | `BossStinkBugActionFlyLow::exeShakeOffSign` |  |
| `BossStinkBugActionFlyLowNrvShakeOff` | `BossStinkBugActionFlyLow::exeShakeOff` |  |
| `BossStinkBugActionFlyLowNrvShakeOffWait` | `BossStinkBugActionFlyLow::exeShakeOffWait` |  |
| `BossStinkBugActionFlyLowNrvShakeOffEnd` | `BossStinkBugActionFlyLow::exeShakeOffEnd` |  |
| `BossStinkBugActionFlyLowNrvFall` | `BossStinkBugActionFlyLow::exeFall` |  |
| `BossStinkBugActionFlyLowNrvDamage` | `BossStinkBugActionFlyLow::exeDamage` |  |
| `BossStinkBugActionFlyLowNrvDash` | `BossStinkBugActionFlyLow::exeDash` |  |
| `BossStinkBugActionFlyLowNrvTurnSign` | `BossStinkBugActionFlyLow::exeTurnSign` |  |
| `BossStinkBugActionFlyLowNrvTurn` | `BossStinkBugActionFlyLow::exeTurn` |  |

## BossStinkBugActionGround

src/Game/Boss/BossStinkBugActionGround.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossStinkBugActionGroundNrvStart` | `BossStinkBugActionGround::exeStart` |  |
| `BossStinkBugActionGroundNrvWalk` | `BossStinkBugActionGround::exeWalk` |  |
| `BossStinkBugActionGroundNrvDamage` | `BossStinkBugActionGround::exeDamage` |  |
| `BossStinkBugActionGroundNrvFind` | `BossStinkBugActionGround::exeFind` |  |
| `BossStinkBugActionGroundNrvShakeOff` | `BossStinkBugActionGround::exeShakeOff` |  |
| `BossStinkBugActionGroundNrvShakeOffRun` | `BossStinkBugActionGround::exeShakeOffRun` |  |
| `BossStinkBugActionGroundNrvRun` | `BossStinkBugActionGround::exeRun` |  |
| `BossStinkBugActionGroundNrvAttackSuccess` | `BossStinkBugActionGround::exeAttackSuccess` | `BossStinkBugActionGround::exeAttackSuccess` |
| `BossStinkBugActionGroundNrvCoolDown` | `BossStinkBugActionGround::exeCoolDown` |  |
| `BossStinkBugActionGroundNrvTurn` | `BossStinkBugActionGround::exeTurn` |  |

## BossStinkBugActionSequencer

src/Game/Boss/BossStinkBugActionSequencer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossStinkBugActionSequencerNrvWaitStart` | `BossStinkBugActionSequencer::exeWaitStart` |  |
| `BossStinkBugActionSequencerNrvOpeningDemo` | `BossStinkBugActionSequencer::exeOpeningDemo` |  |
| `BossStinkBugActionSequencerNrvGround` | `BossStinkBugActionSequencer::exeGround` |  |
| `BossStinkBugActionSequencerNrvFlyDemo` | `BossStinkBugActionSequencer::exeFlyDemo` |  |
| `BossStinkBugActionSequencerNrvFlyLow` | `BossStinkBugActionSequencer::exeFlyLow` |  |
| `BossStinkBugActionSequencerNrvAngryDemo` | `BossStinkBugActionSequencer::exeAngryDemo` |  |
| `BossStinkBugActionSequencerNrvFlyHigh` | `BossStinkBugActionSequencer::exeFlyHigh` |  |
| `BossStinkBugActionSequencerNrvFinishDemo` | `BossStinkBugActionSequencer::exeFinishDemo` |  |
| `BossStinkBugActionSequencerNrvEnd` | `BossStinkBugActionSequencer::exeEnd` |  |

## BossStinkBugAngryDemo

src/Game/Boss/BossStinkBugAngryDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossStinkBugAngryDemoNrvTryStart` | `BossStinkBugAngryDemo::exeTryStart` |  |
| `BossStinkBugAngryDemoNrvDemo` | `BossStinkBugAngryDemo::exeDemo` |  |

## BossStinkBugBomb

src/Game/Boss/BossStinkBugBomb.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossStinkBugBombNrvWait` | `BossStinkBugBomb::exeWait` |  |
| `BossStinkBugBombNrvExplosion` | `BossStinkBugBomb::exeExplosion` |  |

## BossStinkBugFinishDemo

src/Game/Boss/BossStinkBugFinishDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossStinkBugFinishDemoNrvTryStart` | `BossStinkBugFinishDemo::exeTryStart` |  |
| `BossStinkBugFinishDemoNrvDemo` | `BossStinkBugFinishDemo::exeDemo` |  |
| `BossStinkBugFinishDemoNrvAppearPowerStar` | `BossStinkBugFinishDemo::exeAppearPowerStar` |  |

## BossStinkBugFlyDemo

src/Game/Boss/BossStinkBugFlyDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossStinkBugFlyDemoNrvTryStart` | `BossStinkBugFlyDemo::exeTryStart` |  |
| `BossStinkBugFlyDemoNrvDemo` | `BossStinkBugFlyDemo::exeDemo` |  |

## BossStinkBugOpeningDemo

src/Game/Boss/BossStinkBugOpeningDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BossStinkBugOpeningDemoNrvTryStart` | `BossStinkBugOpeningDemo::exeTryStart` |  |
| `BossStinkBugOpeningDemoNrvDemo` | `BossStinkBugOpeningDemo::exeDemo` |  |

## BreakableCage

src/Game/MapObj/BreakableCage.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BreakableCageNrvWait` | `BreakableCage::exeWait` |  |
| `BreakableCageNrvWaitStartDemoBreak` | `BreakableCage::exeWaitStartDemoBreak` |  |
| `BreakableCageNrvBreak` | `BreakableCage::exeBreak` |  |

## BrosButton

src/Game/Screen/BrosButton.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BrosButtonNrvAppear` | `BrosButton::exeAppear` |  |
| `BrosButtonNrvSelect` | `BrosButton::exeSelect` |  |
| `BrosButtonNrvDecide` | `BrosButton::exeDecide` |  |
| `BrosButtonNrvDisappear` | `BrosButton::exeDisappear` |  |

## BumpAppearPlanet

src/Game/MapObj/BumpAppearPlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `BumpAppearPlanetNrvWait` | `BumpAppearPlanet::exeWait` |  |
| `BumpAppearPlanetNrvBumpOut` | `BumpAppearPlanet::exeBumpOut` |  |
| `BumpAppearPlanetNrvBumpIn` | `BumpAppearPlanet::exeBumpIn` |  |

## Butler

src/Game/NPC/Butler.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ButlerNrvStarPieceReaction` | `Butler::exeStarPieceReaction` |  |
| `ButlerNrvDemo` | `Butler::exeDemo` |  |
| `ButlerNrvDemoDomeLecture2` | `Butler::exeDemoDomeLecture2` |  |
| `ButlerNrvDemoStarPiece2` | `Butler::exeDemoStarPiece2` |  |
| `ButlerNrvDemoShowGalaxyMap` | `Butler::exeDemoShowGalaxyMap` |  |
| `ButlerNrvWaitStartDemo` | `Butler::exeDemoWait` |  |

## ButlerExplain

src/Game/NPC/ButlerExplain.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ButlerExplainNrvDemo` | `ButlerExplain::exeDemo` |  |

## ButlerMap

src/Game/NPC/ButlerMap.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ButlerMapNrvTalk` | `ButlerMap::exeTalk` |  |
| `ButlerMapNrvShowGalaxyMap` | `ButlerMap::exeShowGalaxyMap` |  |
| `ButlerMapNrvLectureDemoShowMapBefore` | `ButlerMap::exeLectureDemoShowMapBefore` |  |
| `ButlerMapNrvLectureDemoShowMap` | `ButlerMap::exeLectureDemoShowMap` |  |
| `ButlerMapNrvLectureDemoShowMapAfter` | `ButlerMap::exeLectureDemoShowMapAfter` |  |
| `ButlerMapNrvStarPieceReaction` | `ButlerMap::exeStarPieceReaction` |  |

## ButlerStateStarPieceReaction

src/Game/NPC/ButlerStateStarPieceReaction.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ButlerStateStarPieceReactionNrvWait` | `ButlerStateStarPieceReaction::exeWait` |  |

## Butterfly

src/Game/Map/Butterfly.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `Butterfly::exeWait` |  |
| `HostTypeRunAway` | `Butterfly::exeRunAway` |  |
| `HostTypeHive` | `Butterfly::exeHive` |  |
| `HostTypePerchOn` | `Butterfly::exePerchOn` |  |
| `HostTypeGotoSleepingMario` | `Butterfly::exeGotoSleepingMario` |  |
| `HostTypeReadyToPerchOnSleepingMario` | `Butterfly::exeReadyToPerchOnSleepingMario` |  |
| `HostTypePerchOnSleepingMario` | `Butterfly::exePerchOnSleepingMario` |  |

## ButtonPaneController

src/Game/Screen/ButtonPaneController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ButtonPaneControllerNrvHidden` | `ButtonPaneController::exeHidden` |  |
| `ButtonPaneControllerNrvAppear` | `ButtonPaneController::exeAppear` |  |
| `ButtonPaneControllerNrvWait` | `ButtonPaneController::exeWait` |  |
| `ButtonPaneControllerNrvPointing` | `ButtonPaneController::exePointing` |  |
| `ButtonPaneControllerNrvNotPointing` | `ButtonPaneController::exeNotPointing` |  |
| `ButtonPaneControllerNrvDecided` | `ButtonPaneController::exeDecided` |  |
| `ButtonPaneControllerNrvDecidedWait` | `ButtonPaneController::exeDecidedWait` |  |
| `ButtonPaneControllerNrvDecidedToDisappear` | `ButtonPaneController::exeDecidedToDisappear` |  |
| `ButtonPaneControllerNrvDisappear` | `ButtonPaneController::exeDisappear` |  |

## CameraInfo

src/Game/Screen/CameraInfo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CameraInfoNrvHide` | `CameraInfo::exeHide` |  |
| `CameraInfoNrvAppear` | `CameraInfo::exeAppear` |  |
| `CameraInfoNrvNormal` | `CameraInfo::exeNormal` |  |
| `CameraInfoNrvFirstPerson` | `CameraInfo::exeFirstPerson` |  |
| `CameraInfoNrvEnd` | `CameraInfo::exeEnd` |  |

## Candlestand

src/Game/MapObj/Candlestand.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWaitFire` | `Candlestand::exeWaitFire` |  |
| `HostTypeFire` | `Candlestand::exeFire` |  |
| `HostTypeBurn` | `Candlestand::exeBurn` |  |
| `HostTypeAttack` | `Candlestand::exeAttack` |  |
| `HostTypeExtinguish` | `Candlestand::exeExtinguish` |  |
| `HostTypeFlicker` | `Candlestand::exeFlicker` |  |

## CannonFortressBreakStep

src/Game/MapObj/CannonFortressBreakStep.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CannonFortressBreakStepNrvWait` | `CannonFortressBreakStep::exeWait` |  |
| `CannonFortressBreakStepNrvFallStart` | `CannonFortressBreakStep::exeFallStart` |  |
| `CannonFortressBreakStepNrvFall` | `CannonFortressBreakStep::exeFall` |  |
| `CannonFortressBreakStepNrvBreak` | `CannonFortressBreakStep::exeBreak` |  |

## CapsuleCage

src/Game/MapObj/CapsuleCage.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CapsuleCageNrvWait` | `CapsuleCage::exeWait` |  |
| `CapsuleCageNrvStartCamera` | `CapsuleCage::exeStartCamera` |  |
| `CapsuleCageNrvOpen` | `CapsuleCage::exeOpen` |  |
| `CapsuleCageNrvEndCamera` | `CapsuleCage::exeEndCamera` |  |

## Caretaker

src/Game/NPC/CareTaker.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CaretakerNrvTakeOutStar` | `Caretaker::exeTakeOutStar` |  |
| `CaretakerNrvReaction` | `Caretaker::exeReaction` |  |
| `CaretakerNrvTalk` | `Caretaker::exeTalk` |  |
| `CaretakerNrvWait` | `Caretaker::exeWait` |  |
| `CaretakerNrvPreTalk` | `Caretaker::exePreTalk` |  |
| `CaretakerNrvPreWipeOut` | `Caretaker::exePreWipeOut` |  |
| `CaretakerNrvPreWipeIn` | `Caretaker::exePreWipeIn` |  |
| `CaretakerNrvPreWait` | `Caretaker::exePreWait` |  |
| `CaretakerNrvCountDown` | `Caretaker::exeCountDown` |  |
| `CaretakerNrvTidy` | `Caretaker::exeTidy` |  |
| `CaretakerNrvRunaway` | `Caretaker::exeRunaway` |  |
| `CaretakerNrvPstWipeOut` | `Caretaker::exePstWipeOut` |  |
| `CaretakerNrvPstWipeIn` | `Caretaker::exePstWipeIn` |  |
| `CaretakerNrvPstWait` | `Caretaker::exePstWait` |  |
| `CaretakerNrvPstTalk` | `Caretaker::exePstTalk` |  |
| `CaretakerNrvAppearedStar` | `Caretaker::exeAppearedStar` |  |

## CenterScreenBlur

src/Game/Screen/CenterScreenBlur.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CenterScreenBlurNrvFadeIn` | `CenterScreenBlur::exeFadeIn` |  |
| `CenterScreenBlurNrvKeep` | `CenterScreenBlur::exeKeep` |  |
| `CenterScreenBlurNrvFadeOut` | `CenterScreenBlur::exeFadeOut` |  |

## CharaState

src/Game/Screen/FileSelectInfo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CharaStateNrvMario` | `CharaState::exeMario` |  |
| `CharaStateNrvToLuigi` | `CharaState::exeToLuigi` |  |
| `CharaStateNrvLuigi` | `CharaState::exeLuigi` |  |
| `CharaStateNrvToMario` | `CharaState::exeToMario` |  |

## ChipBase

src/Game/MapObj/ChipBase.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ChipBaseNrvDeactive` | `ChipBase::exeDeactive` |  |
| `ChipBaseNrvWait` | `ChipBase::exeWait` |  |
| `ChipBaseNrvControled` | `ChipBase::exeControled` |  |
| `ChipBaseNrvFlashing` | `ChipBase::exeFlashing` |  |
| `ChipBaseNrvHide` | `ChipBase::exeHide` |  |
| `ChipBaseNrvGot` | `ChipBase::exeGot` |  |

## ChipCounter

src/Game/MapObj/ChipCounter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ChipCounterNrvHide` | `ChipCounter::exeHide` |  |
| `ChipCounterNrvFrameIn` | `ChipCounter::exeFrameIn` |  |
| `ChipCounterNrvShow` | `ChipCounter::exeShow` |  |
| `ChipCounterNrvFrameOut` | `ChipCounter::exeFrameOut` |  |
| `ChipCounterNrvTryDemo` | `ChipCounter::exeTryDemo` |  |
| `ChipCounterNrvComplete` | `ChipCounter::exeComplete` |  |
| `ChipCounterNrvCompleteOut` | `ChipCounter::exeCompleteOut` |  |

## CinemaFrame

src/Game/Screen/CinemaFrame.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CinemaFrameNrvScreen` | `CinemaFrame::exeScreen` |  |
| `CinemaFrameNrvFrame` | `CinemaFrame::exeFrame` |  |
| `CinemaFrameNrvBlank` | `CinemaFrame::exeBlank` |  |
| `CinemaFrameNrvScreenToFrame` | `CinemaFrame::exeScreenToFrame` |  |
| `CinemaFrameNrvFrameToBlank` | `CinemaFrame::exeFrameToBlank` |  |
| `CinemaFrameNrvBlankToFrame` | `CinemaFrame::exeBlankToFrame` |  |
| `CinemaFrameNrvFrameToScreen` | `CinemaFrame::exeFrameToScreen` |  |

## ClipAreaDrop

src/Game/MapObj/ClipAreaDrop.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ClipAreaDropNrvWait` | `ClipAreaDrop::exeWait` |  |

## ClipAreaDropLaser

src/Game/MapObj/ClipAreaDropLaser.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ClipAreaDropLaserNrvWait` | `ClipAreaDropLaser::exeWait` |  |
| `ClipAreaDropLaserNrvMove` | `ClipAreaDropLaser::exeMove` |  |

## CocoNut

src/Game/MapObj/CocoNut.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CocoNutNrvWait` | `CocoNut::exeWait` |  |
| `CocoNutNrvWaitOnBind` | `CocoNut::exeWaitOnBind` |  |
| `CocoNutNrvMove` | `CocoNut::exeMove` |  |
| `CocoNutNrvInWater` | `CocoNut::exeInWater` |  |
| `CocoNutNrvBreak` | `CocoNut::exeBreak` |  |
| `CocoNutNrvReplaceReady` | `CocoNut::exeReplaceReady` |  |

## CocoNutBall

src/Game/Enemy/CocoNutBall.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CocoNutBallNrvThrow` | `CocoNutBall::exeThrow` |  |
| `CocoNutBallNrvHitBackToHost` | `CocoNutBall::exeHitBackToHost` |  |
| `CocoNutBallNrvHitBackToPlayer` | `CocoNutBall::exeHitBackToPlayer` |  |
| `CocoNutBallNrvRebound` | `CocoNutBall::exeRebound` |  |
| `CocoNutBallNrvFreeze` | `CocoNutBall::exeFreeze` |  |
| `CocoNutBallNrvFreezeRelease` | `CocoNutBall::exeFreezeRelease` |  |

## CocoSambo

src/Game/Enemy/CocoSambo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CocoSamboNrvHideWait` | `CocoSambo::exeHideWait` |  |
| `CocoSamboNrvAppear` | `CocoSambo::exeAppear` |  |
| `CocoSamboNrvWait` | `CocoSambo::exeWait` |  |
| `CocoSamboNrvHide` | `CocoSambo::exeHide` |  |
| `CocoSamboNrvAttack` | `CocoSambo::exeAttack` |  |
| `CocoSamboNrvAttackInterval` | `CocoSambo::exeAttackInterval` |  |
| `CocoSamboNrvFallHead` | `CocoSambo::exeFallHead` |  |
| `CocoSamboNrvFallHeadHide` | `CocoSambo::exeFallHeadHide` |  |
| `CocoSamboNrvRecoverWait` | `CocoSambo::exeRecoverWait` |  |
| `CocoSamboNrvRecover` | `CocoSambo::exeRecover` |  |
| `CocoSamboNrvPressY` | `CocoSambo::exePressY` |  |
| `CocoSamboNrvBlow` | `CocoSambo::exeBlow` |  |
| `CocoSamboNrvPointerAiming` | `CocoSambo::exePointerAiming` | `CocoSambo::exePointerAiming` |

## CocoSamboHead

src/Game/Enemy/CocoSambo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CocoSamboHeadNrvHeadConnectedBody` | `CocoSamboHead::exeConnectedBody` |  |
| `CocoSamboHeadNrvHeadFall` | `CocoSamboHead::exeFall` |  |
| `CocoSamboHeadNrvHeadFallLand` | `CocoSamboHead::exeFallLand` |  |
| `CocoSamboHeadNrvHeadSwoon` | `CocoSamboHead::exeSwoon` |  |
| `CocoSamboHeadNrvHeadSwoonEnd` | `CocoSamboHead::exeSwoonEnd` |  |
| `CocoSamboHeadNrvHeadBlow` | `CocoSamboHead::exeBlow` |  |

## Coin

src/Game/MapObj/Coin.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CoinNrvNonActive` | `Coin::exeNonActive` |  |
| `CoinNrvFix` | `Coin::exeFix` |  |
| `CoinNrvFixHide` | `Coin::exeFixHide` |  |
| `CoinNrvFixTimer` | `Coin::exeFixTimer` |  |
| `CoinNrvControled` | `Coin::exeControled` |  |
| `CoinNrvSpinDrained` | `Coin::exeSpinDrained` |  |
| `CoinNrvMove` | `Coin::exeMove` |  |
| `CoinNrvHop` | `Coin::exeHop` |  |

## CoinBox

src/Game/MapObj/CoinBox.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CoinBoxNrvWait` | `CoinBox::exeWait` |  |
| `CoinBoxNrvHit` | `CoinBox::exeHit` |  |

## CoinCounter

src/Game/Screen/CoinCounter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CoinCounterNrvHide` | `CoinCounter::exeHide` |  |
| `CoinCounterNrvAppear` | `CoinCounter::exeAppear` |  |
| `CoinCounterNrvWait` | `CoinCounter::exeWait` |  |
| `CoinCounterNrvDisappear` | `CoinCounter::exeDisappear` |  |

## CoinGroup

src/Game/MapObj/CoinGroup.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CoinGroupNrvAppear` | `CoinGroup::exeAppear` |  |
| `CoinGroupNrvTryStartDemo` | `CoinGroup::exeTryStartDemo` |  |
| `CoinGroupNrvDemoAppear` | `CoinGroup::exeDemoAppear` |  |
| `CoinGroupNrvKill` | `CoinGroup::exeKill` |  |

## CoinSpot

src/Game/MapObj/CoinSpot.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CoinSpotNrvWait` | `CoinSpot::exeWait` |  |
| `CoinSpotNrvIsInTornado` | `CoinSpot::exeIsInTornado` |  |

## CollapsePlane

src/Game/MapObj/CollapsePlane.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CollapsePlaneNrvWait` | `CollapsePlane::exeWait` |  |
| `CollapsePlaneNrvCollapse` | `CollapsePlane::exeCollapse` |  |
| `CollapsePlaneNrvPointerStop` | `CollapsePlane::exePointerStop` |  |
| `CollapsePlaneNrvEnd` | `CollapsePlane::exeEnd` |  |

## CollectCounter

src/Game/MapObj/CollectCounter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CollectCounterNrvHide` | `CollectCounter::exeHide` |  |
| `CollectCounterNrvShow` | `CollectCounter::exeShow` |  |

## CollectTico

src/Game/NPC/CollectTico.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CollectTicoNrvWait` | `CollectTico::exeWait` |  |
| `CollectTicoNrvTryStartDemo` | `CollectTico::exeTryStartDemo` |  |
| `CollectTicoNrvCompleteDemo` | `CollectTico::exeCompleteDemo` |  |
| `CollectTicoNrvFlash` | `CollectTico::exeFlash` |  |
| `CollectTicoNrvAppearPowerStar` | `CollectTico::exeAppearPowerStar` |  |

## CometEventExecutorTimeLimit

src/Game/NPC/CometEventExecutorTimeLimit.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CometEventExecutorTimeLimitStartEvent` | `CometEventExecutorTimeLimit::exeStartEvent` |  |

## CometRetryButton

src/Game/Screen/CometRetryButton.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvMissLeftWait` | `CometRetryButton::exeMissLeftWait` |  |
| `HostTypeNrvWait` | `CometRetryButton::exeWait` |  |
| `HostTypeNrvDecided` | `CometRetryButton::exeDecided` |  |
| `HostTypeNrvEndRetry` | `CometRetryButton::exeEnd` |  |
| `HostTypeNrvEndGoBackAstroDome` | `CometRetryButton::exeEnd` |  |

## CounterLayoutAppearer

src/Game/Screen/CounterLayoutAppearer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CounterLayoutAppearerNrvHide` | `CounterLayoutAppearer::exeHide` |  |
| `CounterLayoutAppearerNrvAppear` | `CounterLayoutAppearer::exeAppear` |  |
| `CounterLayoutAppearerNrvShow` | `CounterLayoutAppearer::exeShow` |  |
| `CounterLayoutAppearerNrvDisappear` | `CounterLayoutAppearer::exeDisappear` |  |

## CounterLayoutController

src/Game/Screen/CounterLayoutController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CounterLayoutControllerNrvPlayerMoving` | `CounterLayoutController::exePlayerMoving` |  |
| `CounterLayoutControllerNrvPlayerNotMoving` | `CounterLayoutController::exePlayerNotMoving` |  |

## Creeper

src/Game/Ride/Creeper.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CreeperNrvFree` | `Creeper::exeFree` |  |
| `CreeperNrvFreeInvalid` | `Creeper::exeFreeInvalid` |  |
| `CreeperNrvHangStart` | `Creeper::exeHangStart` |  |
| `CreeperNrvHangUp` | `Creeper::exeHangUp` |  |
| `CreeperNrvHangDown` | `Creeper::exeHangDown` |  |

## CrystalCage

src/Game/MapObj/CrystalCage.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CrystalCageNrvWait` | `CrystalCage::exeWait` |  |
| `CrystalCageNrvBreak` | `CrystalCage::exeBreak` |  |
| `CrystalCageNrvBreakAfter` | `CrystalCage::exeBreakAfter` |  |

## CrystalCageMoving

src/Game/MapObj/CrystalCageMoving.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CrystalCageMovingNrvWaitBig` | `CrystalCageMoving::exeWaitBig` |  |
| `CrystalCageMovingNrvBreakBig` | `CrystalCageMoving::exeBreakBig` | `CrystalCageMoving::exeBreakBig` |
| `CrystalCageMovingNrvWaitSmall` | `CrystalCageMoving::exeWaitSmall` |  |
| `CrystalCageMovingNrvBreakSmall` | `CrystalCageMoving::exeBreakSmall` |  |
| `CrystalCageMovingNrvBreakAll` | `CrystalCageMoving::exeBreakAll` |  |
| `CrystalCageMovingNrvDemoTicoMove` | `CrystalCageMoving::exeDemoTicoMove` |  |
| `CrystalCageMovingNrvDemoTicoStop` | `CrystalCageMoving::exeDemoTicoStop` |  |
| `CrystalCageMovingNrvDemoTicoChange` | `CrystalCageMoving::exeDemoTicoChange` |  |

## CrystalSwitch

src/Game/MapObj/CrystalSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `CrystalSwitchNrvOff` | `CrystalSwitch::exeOff` |  |
| `CrystalSwitchNrvSwitchDown` | `CrystalSwitch::exeSwitchDown` |  |
| `CrystalSwitchNrvOn` | `CrystalSwitch::exeOn` |  |
| `CrystalSwitchNrvSwitchUp` | `CrystalSwitch::exeSwitchUp` |  |

## DeadLeaves

src/Game/MapObj/DeadLeaves.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DeadLeavesNrvWait` | `DeadLeaves::exeWait` |  |
| `DeadLeavesNrvSpin` | `DeadLeaves::exeSpin` |  |

## DemoKoopaJrShip

src/Game/Demo/DemoKoopaJrShip.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DemoKoopaJrShipNrvAppear` | `DemoKoopaJrShip::exeAppear` |  |
| `DemoKoopaJrShipNrvTalk` | `DemoKoopaJrShip::exeTalk` |  |
| `DemoKoopaJrShipNrvFlyAway` | `DemoKoopaJrShip::exeFlyAway` |  |

## DemoRabbit

src/Game/NPC/DemoRabbit.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DemoRabbitNrvAppear` | `DemoRabbit::exeAppear` |  |
| `DemoRabbitNrvDemo` | `DemoRabbit::exeDemo` |  |
| `DemoRabbitNrvTalk0` | `DemoRabbit::exeTalk` |  |
| `DemoRabbitNrvTalk1` | `DemoRabbit::exeTalk` |  |
| `DemoRabbitNrvWait` | `DemoRabbit::exeWait` |  |
| `DemoRabbitNrvGuide` | `DemoRabbit::exeGuide` |  |
| `DemoRabbitNrvGoal` | `DemoRabbit::exeGoal` |  |
| `DemoRabbitNrvRunaway` | `DemoRabbit::exeRunaway` |  |
| `DemoRabbitNrvChange` | `DemoRabbit::exeChange` |  |
| `DemoRabbitNrvStartBGM` | `DemoRabbit::exeStartBGM` |  |

## DemoStarter

src/Game/Util/NPCUtil.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DemoStarterNrvInit` | `DemoStarter::exeInit` |  |
| `DemoStarterNrvFade` | `DemoStarter::exeFade` |  |
| `DemoStarterNrvWait` | `DemoStarter::exeWait` |  |
| `DemoStarterNrvTerm` | `DemoStarter::exeTerm` |  |

## DesertLandMoveSwitch

src/Game/MapObj/DesertLandMoveSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `DesertLandMoveSwitch::exeWait` |  |
| `HostTypeSwitchDown` | `DesertLandMoveSwitch::exeSwitchDown` |  |
| `HostTypeOn` | `DesertLandMoveSwitch::exeOn` |  |
| `HostTypeReturn` | `DesertLandMoveSwitch::exeReturn` |  |

## DesertMovingLand

src/Game/MapObj/DesertMovingLand.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWaitTop` | `DesertMovingLand::exeWait` |  |
| `HostTypeWaitBottom` | `DesertMovingLand::exeWait` |  |
| `HostTypeMoveDown` | `DesertMovingLand::exeMoveDown` |  |
| `HostTypeMoveUp` | `DesertMovingLand::exeMoveUp` |  |
| `HostTypeMoveDownSign` | `DesertMovingLand::exeMoveSign` |  |
| `HostTypeMoveUpSign` | `DesertMovingLand::exeMoveSign` |  |
| `HostTypeStop` | `DesertMovingLand::exeStop` |  |

## DharmaSambo

src/Game/Enemy/DharmaSambo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWaitUnderGround` | `DharmaSambo::exeWaitUnderGround` |  |
| `HostTypeNrvAppear` | `DharmaSambo::exeAppear` |  |
| `HostTypeNrvAppearImmediately` | `DharmaSambo::exeAppear` |  |
| `HostTypeNrvPursue` | `DharmaSambo::exePursue` | `DharmaSambo::exePursue` |
| `HostTypeNrvAttack` | `DharmaSambo::exeAttack` |  |
| `HostTypeNrvHide` | `DharmaSambo::exeHide` |  |
| `HostTypeNrvHideAppear` | `DharmaSambo::exeHideAppear` |  |
| `HostTypeNrvSpinHitted` | `DharmaSambo::exeSpinHitted` |  |
| `HostTypeNrvStarPointerBindEnd` | `DharmaSambo::exeSpinHitted` |  |
| `HostTypeNrvKilled` | `DharmaSambo::exeKilled` |  |
| `HostTypeNrvStarPointerBind` | `DharmaSambo::exeStarPointerBind` | `DharmaSambo::exeStarPointerBind` |
| `HostTypeNrvEscapeRun` | `DharmaSambo::exeEscapeRun` |  |
| `HostTypeNrvEscapeLand` | `DharmaSambo::exeEscapeLand` |  |
| `HostTypeNrvEscapeWaitRun` | `DharmaSambo::exeEscapeRun` |  |
| `HostTypeNrvEscapeWaitLand` | `DharmaSambo::exeEscapeLand` |  |
| `HostTypeNrvTrampleDeath` | `DharmaSambo::exeTrampleDeath` |  |

## DharmaSamboParts

src/Game/Enemy/DharmaSambo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvDoNothing` | `DharmaSamboParts::exeDoNothing` |  |
| `HostTypeNrvBlow` | `DharmaSamboParts::exeBlow` |  |
| `HostTypeNrvPartsStarPointerBind` | `DharmaSamboParts::exeStarPointerBind` | `DharmaSamboParts::exeStarPointerBind` |

## DinoPackunBall

src/Game/Boss/DinoPackunBall.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunBallNrvWait` | `DinoPackunBall::exeWait` |  |
| `DinoPackunBallNrvShoot` | `DinoPackunBall::exeShoot` |  |
| `DinoPackunBallNrvCharge` | `DinoPackunBall::exeCharge` |  |
| `DinoPackunBallNrvReverse` | `DinoPackunBall::exeReverse` |  |
| `DinoPackunBallNrvRebound` | `DinoPackunBall::exeRebound` |  |
| `DinoPackunBallNrvLock` | `DinoPackunBall::exeLock` |  |

## DinoPackunBattleEgg

src/Game/Boss/DinoPackunBattleEgg.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunBattleEggNrvTurn` | `DinoPackunBattleEgg::exeTurn` |  |
| `DinoPackunBattleEggNrvWalk` | `DinoPackunBattleEgg::exeWalk` |  |
| `DinoPackunBattleEggNrvDamage` | `DinoPackunBattleEgg::exeDamage` |  |

## DinoPackunBattleEggVs2

src/Game/Boss/DinoPackunBattleEggVs2.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunBattleEggVs2NrvTurn` | `DinoPackunBattleEggVs2::exeTurn` |  |
| `DinoPackunBattleEggVs2NrvWalk` | `DinoPackunBattleEggVs2::exeWalk` |  |
| `DinoPackunBattleEggVs2NrvDamage` | `DinoPackunBattleEggVs2::exeDamage` |  |

## DinoPackunBattleVs1Lv1

src/Game/Boss/DinoPackunBattleVs1Lv1.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunBattleVs1Lv1NrvStart` | `DinoPackunBattleVs1Lv1::exeStart` |  |
| `DinoPackunBattleVs1Lv1NrvTurn` | `DinoPackunBattleVs1Lv1::exeTurn` |  |
| `DinoPackunBattleVs1Lv1NrvWalk` | `DinoPackunBattleVs1Lv1::exeWalk` |  |
| `DinoPackunBattleVs1Lv1NrvFind` | `DinoPackunBattleVs1Lv1::exeFind` |  |
| `DinoPackunBattleVs1Lv1NrvChase` | `DinoPackunBattleVs1Lv1::exeChase` |  |
| `DinoPackunBattleVs1Lv1NrvCoolDown` | `DinoPackunBattleVs1Lv1::exeCoolDown` |  |
| `DinoPackunBattleVs1Lv1NrvAttackHit` | `DinoPackunBattleVs1Lv1::exeAttackHit` |  |
| `DinoPackunBattleVs1Lv1NrvDamage` | `DinoPackunBattleVs1Lv1::exeDamage` |  |

## DinoPackunBattleVs1Lv2

src/Game/Boss/DinoPackunBattleVs1Lv2.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunBattleVs1Lv2NrvStart` | `DinoPackunBattleVs1Lv2::exeStart` |  |
| `DinoPackunBattleVs1Lv2NrvTurn` | `DinoPackunBattleVs1Lv2::exeTurn` |  |
| `DinoPackunBattleVs1Lv2NrvWalk` | `DinoPackunBattleVs1Lv2::exeWalk` |  |
| `DinoPackunBattleVs1Lv2NrvFind` | `DinoPackunBattleVs1Lv2::exeFind` |  |
| `DinoPackunBattleVs1Lv2NrvChase` | `DinoPackunBattleVs1Lv2::exeChase` |  |
| `DinoPackunBattleVs1Lv2NrvCoolDown` | `DinoPackunBattleVs1Lv2::exeCoolDown` |  |
| `DinoPackunBattleVs1Lv2NrvAttackHit` | `DinoPackunBattleVs1Lv2::exeAttackHit` |  |
| `DinoPackunBattleVs1Lv2NrvDamage` | `DinoPackunBattleVs1Lv2::exeDamage` |  |

## DinoPackunBattleVs2Lv1

src/Game/Boss/DinoPackunBattleVs2Lv1.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunBattleVs2Lv1NrvTurn` | `DinoPackunBattleVs2Lv1::exeTurn` |  |
| `DinoPackunBattleVs2Lv1NrvWalk` | `DinoPackunBattleVs2Lv1::exeWalk` | `DinoPackunBattleVs2Lv1::exeWalk` |
| `DinoPackunBattleVs2Lv1NrvFindCrazy` | `DinoPackunBattleVs2Lv1::exeFindCrazy` |  |
| `DinoPackunBattleVs2Lv1NrvCrazy` | `DinoPackunBattleVs2Lv1::exeCrazy` | `DinoPackunBattleVs2Lv1::exeCrazy` |
| `DinoPackunBattleVs2Lv1NrvChase` | `DinoPackunBattleVs2Lv1::exeChase` | `DinoPackunBattleVs2Lv1::exeChase` |
| `DinoPackunBattleVs2Lv1NrvAwake` | `DinoPackunBattleVs2Lv1::exeAwake` | `DinoPackunBattleVs2Lv1::exeAwake` |
| `DinoPackunBattleVs2Lv1NrvAttackHit` | `DinoPackunBattleVs2Lv1::exeAttackHit` |  |
| `DinoPackunBattleVs2Lv1NrvDamage` | `DinoPackunBattleVs2Lv1::exeDamage` |  |

## DinoPackunDemo

src/Game/Boss/DinoPackunDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunDemoNrvTryStartDemo` | `DinoPackunDemo::exeTryStartDemo` |  |
| `DinoPackunDemoNrvOpeningDemo` | `DinoPackunDemo::exeOpeningDemo` |  |
| `DinoPackunDemoNrvCryDemo` | `DinoPackunDemo::exeCryDemo` |  |
| `DinoPackunDemoNrvAngryDemo` | `DinoPackunDemo::exeAngryDemo` |  |
| `DinoPackunDemoNrvDownDemo` | `DinoPackunDemo::exeDownDemo` |  |
| `DinoPackunDemoNrvAppearPowerStar` | `DinoPackunDemo::exeAppearPowerStar` |  |

## DinoPackunDemoPosition

src/Game/Boss/DinoPackunDemoPosition.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunDemoPositionNrvOpeningDemo` | `DinoPackunDemoPosition::exeOpeningDemo` |  |

## DinoPackunEggShell

src/Game/Boss/DinoPackunEggShell.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunEggShellNrvWait` | `DinoPackunEggShell::exeWait` |  |

## DinoPackunFire

src/Game/Boss/DinoPackunFire.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunFireNrvShot` | `DinoPackunFire::exeShot` |  |
| `DinoPackunFireNrvGround` | `DinoPackunFire::exeGround` |  |

## DinoPackunStateAwake

src/Game/Boss/DinoPackunStateAwake.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunStateAwakeNrvFind` | `DinoPackunStateAwake::exeFind` |  |

## DinoPackunStateDamage

src/Game/Boss/DinoPackunStateDamage.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunStateDamageNrvPunched` | `DinoPackunStateDamage::exePunched` |  |
| `DinoPackunStateDamageNrvTryPulledDemo` | `DinoPackunStateDamage::exeTryPulledDemo` |  |
| `DinoPackunStateDamageNrvPulled` | `DinoPackunStateDamage::exePulled` |  |
| `DinoPackunStateDamageNrvEggBroken` | `DinoPackunStateDamage::exeEggBroken` |  |
| `DinoPackunStateDamageNrvDamage` | `DinoPackunStateDamage::exeDamage` |  |
| `DinoPackunStateDamageNrvLastDamage` | `DinoPackunStateDamage::exeLastDamage` |  |

## DinoPackunStateFire

src/Game/Boss/DinoPackunStateFire.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunStateFireNrvNormal` | `DinoPackunStateFire::exeNormal` |  |
| `DinoPackunStateFireNrvFireSign` | `DinoPackunStateFire::exeFireSign` |  |
| `DinoPackunStateFireNrvFire` | `DinoPackunStateFire::exeFire` |  |
| `DinoPackunStateFireNrvCool` | `DinoPackunStateFire::exeCool` |  |

## DinoPackunTailPart

src/Game/Boss/DinoPackunTailPart.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunTailPartNrvWait` | `DinoPackunTailPart::exeWait` |  |
| `DinoPackunTailPartNrvLockPosition` | `DinoPackunTailPart::exeLockPosition` |  |

## DinoPackunTrackFire

src/Game/Boss/DinoPackunTrackFire.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunTrackFireNrvWait` | `DinoPackunTrackFire::exeWait` |  |
| `DinoPackunTrackFireNrvGround` | `DinoPackunTrackFire::exeGround` |  |

## DinoPackunVs1

src/Game/Boss/DinoPackunVs1.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunVs1NrvWaitStart` | `DinoPackunVs1::exeWaitStart` |  |
| `DinoPackunVs1NrvOpeningDemo` | `DinoPackunVs1::exeOpeningDemo` |  |
| `DinoPackunVs1NrvBattleEgg` | `DinoPackunVs1::exeBattleEgg` |  |
| `DinoPackunVs1NrvCryDemo` | `DinoPackunVs1::exeCryDemo` |  |
| `DinoPackunVs1NrvBattleLv1` | `DinoPackunVs1::exeBattleLv1` |  |
| `DinoPackunVs1NrvAngryDemo` | `DinoPackunVs1::exeAngryDemo` |  |
| `DinoPackunVs1NrvBattleLv2` | `DinoPackunVs1::exeBattleLv2` |  |
| `DinoPackunVs1NrvBattleLv3` | `DinoPackunVs1::exeBattleLv3` |  |
| `DinoPackunVs1NrvDownDemo` | `DinoPackunVs1::exeDownDemo` |  |

## DinoPackunVs2

src/Game/Boss/DinoPackunVs2.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DinoPackunVs2NrvWaitStart` | `DinoPackunVs2::exeWaitStart` |  |
| `DinoPackunVs2NrvOpeningDemo` | `DinoPackunVs2::exeOpeningDemo` |  |
| `DinoPackunVs2NrvBattleEgg` | `DinoPackunVs2::exeBattleEgg` |  |
| `DinoPackunVs2NrvCryDemo` | `DinoPackunVs2::exeCryDemo` |  |
| `DinoPackunVs2NrvBattleLv1` | `DinoPackunVs2::exeBattleLv1` |  |
| `DinoPackunVs2NrvAngryDemo` | `DinoPackunVs2::exeAngryDemo` |  |
| `DinoPackunVs2NrvBattleLv2` | `DinoPackunVs2::exeBattleLv2` |  |
| `DinoPackunVs2NrvBattleLv3` | `DinoPackunVs2::exeBattleLv3` |  |
| `DinoPackunVs2NrvDownDemo` | `DinoPackunVs2::exeDownDemo` |  |

## DisplayInformationForResult

src/Game/Screen/StageResultInformer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DisplayInformationForResultAppear` | `DisplayInformationForResult::exeAppear` |  |
| `DisplayInformationForResultDisplay` | `DisplayInformationForResult::exeDisplay` |  |
| `DisplayInformationForResultDisappear` | `DisplayInformationForResult::exeDisappear` |  |
| `DisplayInformationForResultIdle` | `DisplayInformationForResult::exeIdle` |  |

## DodoryuBank

src/Game/Boss/Dodoryu.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DodoryuBankNrvBankAppear` | `DodoryuBank::exeAppear` |  |

## DodoryuDemoAngry

src/Game/Boss/DodoryuDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DodoryuDemoAngryNrvAngryDemoWait` | `DodoryuDemoAngry::exeDemoWait` |  |
| `DodoryuDemoAngryNrvAngryPlay` | `DodoryuDemoAngry::exePlay` |  |

## DodoryuDemoAppear

src/Game/Boss/DodoryuDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DodoryuDemoAppearNrvAppearDemoWait` | `DodoryuDemoAppear::exeDemoWait` |  |
| `DodoryuDemoAppearNrvAppearPlay` | `DodoryuDemoAppear::exePlay` |  |

## DodoryuDemoDown

src/Game/Boss/DodoryuDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DodoryuDemoDownNrvDownDemoWait` | `DodoryuDemoDown::exeDemoWait` |  |
| `DodoryuDemoDownNrvDownPlay` | `DodoryuDemoDown::exePlay` |  |

## DodoryuDemoOpening

src/Game/Boss/DodoryuDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DodoryuDemoOpeningNrvOpeningDemoWait` | `DodoryuDemoOpening::exeDemoWait` |  |
| `DodoryuDemoOpeningNrvOpeningUnderground` | `DodoryuDemoOpening::exeUnderground` |  |
| `DodoryuDemoOpeningNrvOpeningUndergroundHide` | `DodoryuDemoOpening::exeUndergroundHide` |  |
| `DodoryuDemoOpeningNrvOpeningAppear` | `DodoryuDemoOpening::exeAppear` |  |
| `DodoryuDemoOpeningNrvOpeningWalk` | `DodoryuDemoOpening::exeWalk` |  |
| `DodoryuDemoOpeningNrvOpeningHideReady` | `DodoryuDemoOpening::exeHideReady` |  |
| `DodoryuDemoOpeningNrvOpeningHide` | `DodoryuDemoOpening::exeHide` |  |

## DodoryuRabbit

src/Game/Boss/Dodoryu.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DodoryuRabbitNrvRabbitEscapeWaiting` | `DodoryuRabbit::exeEscapeWaiting` |  |
| `DodoryuRabbitNrvRabbitEscape` | `DodoryuRabbit::exeEscape` |  |
| `DodoryuRabbitNrvRabbitEscapeSlow` | `DodoryuRabbit::exeEscapeSlow` |  |
| `DodoryuRabbitNrvRabbitRest` | `DodoryuRabbit::exeRest` |  |
| `DodoryuRabbitNrvRabbitJump` | `DodoryuRabbit::exeJump` |  |
| `DodoryuRabbitNrvRabbitWait` | `DodoryuRabbit::exeWait` |  |
| `DodoryuRabbitNrvRabbitReturn` | `DodoryuRabbit::exeReturn` |  |
| `DodoryuRabbitNrvRabbitPleasure` | `DodoryuRabbit::exePleasure` |  |

## DodoryuStateLv1

src/Game/Boss/DodoryuStateLv1.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DodoryuStateLv1NrvHide` | `DodoryuStateLv1::exeHide` | `DodoryuStateLv1::exeHide` |
| `DodoryuStateLv1NrvAppearSign` | `DodoryuStateLv1::exeAppearSign` |  |
| `DodoryuStateLv1NrvAppearStart` | `DodoryuStateLv1::exeAppearStart` | `DodoryuStateLv1::exeAppearStart` |
| `DodoryuStateLv1NrvAppear` | `DodoryuStateLv1::exeAppear` | `DodoryuStateLv1::exeAppear` |
| `DodoryuStateLv1NrvAppearEnd` | `DodoryuStateLv1::exeAppearEnd` |  |
| `DodoryuStateLv1NrvJumpOut` | `DodoryuStateLv1::exeJumpOut` |  |
| `DodoryuStateLv1NrvDamage` | `DodoryuStateLv1::exeDamage` |  |
| `DodoryuStateLv1NrvDive` | `DodoryuStateLv1::exeDive` |  |
| `DodoryuStateLv1NrvSpinOut` | `DodoryuStateLv1::exeSpinOut` |  |

## DodoryuStateLv2

src/Game/Boss/DodoryuStateLv2.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DodoryuStateLv2NrvStart` | `DodoryuStateLv2::exeStart` | `DodoryuStateLv2::exeStart` |
| `DodoryuStateLv2NrvReadyChase` | `DodoryuStateLv2::exeReadyChase` | `DodoryuStateLv2::exeReadyChase` |
| `DodoryuStateLv2NrvChaseHide` | `DodoryuStateLv2::exeChaseHide` | `DodoryuStateLv2::exeChaseHide` |
| `DodoryuStateLv2NrvReadyAppear` | `DodoryuStateLv2::exeReadyAppear` | `DodoryuStateLv2::exeReadyAppear` |
| `DodoryuStateLv2NrvAppearSign` | `DodoryuStateLv2::exeAppearSign` |  |
| `DodoryuStateLv2NrvChaseAppearStart` | `DodoryuStateLv2::exeChaseAppearStart` | `DodoryuStateLv2::exeChaseAppearStart` |
| `DodoryuStateLv2NrvChaseAppear` | `DodoryuStateLv2::exeChaseAppear` | `DodoryuStateLv2::exeChaseAppear` |
| `DodoryuStateLv2NrvChaseAppearEnd` | `DodoryuStateLv2::exeChaseAppearEnd` | `DodoryuStateLv2::exeChaseAppearEnd` |
| `DodoryuStateLv2NrvJumpOut` | `DodoryuStateLv2::exeJumpOut` |  |
| `DodoryuStateLv2NrvEscape` | `DodoryuStateLv2::exeEscape` | `DodoryuStateLv2::exeEscape` |
| `DodoryuStateLv2NrvEscapeJump` | `DodoryuStateLv2::exeEscapeJump` | `DodoryuStateLv2::exeEscapeJump` |
| `DodoryuStateLv2NrvRecover` | `DodoryuStateLv2::exeRecover` |  |
| `DodoryuStateLv2NrvDive` | `DodoryuStateLv2::exeDive` | `DodoryuStateLv2::exeDive` |
| `DodoryuStateLv2NrvKnockDown` | `DodoryuStateLv2::exeKnockDown` |  |
| `DodoryuStateLv2NrvLaugh` | `DodoryuStateLv2::exeLaugh` |  |
| `DodoryuStateLv2NrvLaughEnd` | `DodoryuStateLv2::exeLaughEnd` | `DodoryuStateLv2::exeLaughEnd` |
| `DodoryuStateLv2NrvFindPos` | `DodoryuStateLv2::exeFindPos` |  |
| `DodoryuStateLv2NrvHideAttack` | `DodoryuStateLv2::exeHideAttack` |  |
| `DodoryuStateLv2NrvChaseMoreStart` | `DodoryuStateLv2::exeChaseMoreStart` |  |
| `DodoryuStateLv2NrvChaseMore` | `DodoryuStateLv2::exeChaseMore` | `DodoryuStateLv2::exeChaseMore` |
| `DodoryuStateLv2NrvChaseMoreEnd` | `DodoryuStateLv2::exeChaseMoreEnd` |  |

## DodoryuStateWait

src/Game/Boss/DodoryuStateWait.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DodoryuStateWaitNrvWait` | `DodoryuStateWait::exeWait` |  |

## Dossun

src/Game/Enemy/Dossun.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DossunNrvReady` | `Dossun::exeReady` |  |
| `DossunNrvUpper` | `Dossun::exeUpper` |  |
| `DossunNrvFallSign` | `Dossun::exeFallSign` |  |
| `DossunNrvFalling` | `Dossun::exeFalling` |  |
| `DossunNrvOnGround` | `Dossun::exeOnGround` |  |
| `DossunNrvRising` | `Dossun::exeRising` |  |

## DragonHeadFlower

src/Game/MapObj/DragonHeadFlower.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DragonHeadFlowerNrvWait` | `DragonHeadFlower::exeWait` |  |
| `DragonHeadFlowerNrvSetCenter` | `DragonHeadFlower::exeSetCenter` |  |
| `DragonHeadFlowerNrvLaunchReady` | `DragonHeadFlower::exeLaunchReady` |  |
| `DragonHeadFlowerNrvRailMove` | `DragonHeadFlower::exeRailMove` |  |

## DriftWood

src/Game/MapObj/DriftWood.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `DriftWoodNrvWait` | `DriftWood::exeWait` |  |
| `DriftWoodNrvWaitLand` | `DriftWood::exeWait` |  |
| `DriftWoodNrvVibrateLand` | `DriftWood::exeVibrate` |  |
| `DriftWoodNrvVibrateTakeOff` | `DriftWood::exeVibrate` |  |

## EarthenPipe

src/Game/MapObj/EarthenPipe.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `EarthenPipeNrvWait` | `EarthenPipe::exeWait` |  |
| `EarthenPipeNrvReady` | `EarthenPipe::exeReady` |  |
| `EarthenPipeNrvPlayerIn` | `EarthenPipe::exePlayerIn` |  |
| `EarthenPipeNrvTargetPipeShowUp` | `EarthenPipe::exeTargetPipeShowUp` |  |
| `EarthenPipeNrvPlayerOut` | `EarthenPipe::exePlayerOut` |  |
| `EarthenPipeNrvInvalid` | `EarthenPipe::exeInvalid` |  |
| `EarthenPipeNrvHide` | `EarthenPipe::exeHide` |  |
| `EarthenPipeNrvShow` | `EarthenPipe::exeShow` |  |
| `EarthenPipeNrvWaitToShowUp` | `EarthenPipe::exeWaitToShowUp` |  |
| `EarthenPipeNrvShowUp` | `EarthenPipe::exeShowUp` |  |
| `EarthenPipeNrvWaitToHideDown` | `EarthenPipe::exeWaitToHideDown` |  |
| `EarthenPipeNrvHideDown` | `EarthenPipe::exeHideDown` |  |

## ElectricBall

src/Game/MapObj/ElectricBall.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `ElectricBall::exeWait` |  |
| `HostTypeMove` | `ElectricBall::exeMove` |  |
| `HostTypeAttack` | `ElectricBall::exeAttack` |  |

## ElectricPressureBullet

src/Game/Enemy/ElectricPressureBullet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ElectricPressureBulletNrvFly` | `ElectricPressureBullet::exeFly` |  |

## ElectricRail

src/Game/MapObj/ElectricRail.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ElectricRailNrvWait` | `ElectricRail::exeWait` |  |
| `ElectricRailNrvDisappear` | `ElectricRail::exeDisappear` |  |
| `ElectricRailNrvDisappeared` | `ElectricRail::exeDisappeared` |  |

## ElectricRailMoving

src/Game/MapObj/ElectricRailMoving.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ElectricRailMovingNrvWait` | `ElectricRailMoving::exeWait` |  |
| `ElectricRailMovingNrvDisappear` | `ElectricRailMoving::exeDisappear` |  |
| `ElectricRailMovingNrvDisappeared` | `ElectricRailMoving::exeDisappeared` |  |

## ErrorMessageWindow

src/Game/Screen/ErrorMessageWindow.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ErrorMessageWindowHide` | `ErrorMessageWindow::exeHide` |  |
| `ErrorMessageWindowAppear` | `ErrorMessageWindow::exeAppear` |  |
| `ErrorMessageWindowDisplay` | `ErrorMessageWindow::exeDisplay` |  |
| `ErrorMessageWindowDisappear` | `ErrorMessageWindow::exeDisappear` |  |
| `ErrorMessageWindowDisappearBeforeAppear` | `ErrorMessageWindow::exeDisappearBeforeAppear` |  |

## ExterminationChecker

src/Game/MapObj/ExterminationChecker.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ExterminationCheckerNrvWatching` | `ExterminationChecker::exeWatching` |  |
| `ExterminationCheckerNrvTryStartDemoAppear` | `ExterminationChecker::exeTryStartDemoAppear` |  |
| `ExterminationCheckerNrvAppearStar` | `ExterminationChecker::exeAppearStar` |  |
| `ExterminationCheckerNrvAppearKeySwitch` | `ExterminationChecker::exeAppearKeySwitch` |  |

## EyeBeamer

src/Game/Enemy/EyeBeamer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `EyeBeamerNrvDemoStartWait` | `EyeBeamer::exeDemoStartWait` |  |
| `EyeBeamerNrvDemoWait` | `EyeBeamer::exeDemoWait` |  |
| `EyeBeamerNrvDemoTurn` | `EyeBeamer::exeDemoTurn` |  |
| `EyeBeamerNrvDemoGotoPatrol` | `EyeBeamer::exeDemoGotoPatrol` |  |
| `EyeBeamerNrvWait` | `EyeBeamer::exeWait` |  |
| `EyeBeamerNrvTurn` | `EyeBeamer::exeTurn` |  |
| `EyeBeamerNrvGotoPatrol` | `EyeBeamer::exeGotoPatrol` |  |
| `EyeBeamerNrvPatrol` | `EyeBeamer::exePatrol` |  |

## FadeStarter

src/Game/Util/NPCUtil.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FadeStarterNrvFade` | `FadeStarter::exeFade` |  |
| `FadeStarterNrvTerm` | `FadeStarter::exeTerm` |  |

## FallDownBridge

src/Game/MapObj/FallDownBridge.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FallDownBridgeNrvWait` | `FallDownBridge::exeWait` |  |
| `FallDownBridgeNrvQuake` | `FallDownBridge::exeQuake` |  |

## FallingSmallRock

src/Game/MapObj/FallingSmallRock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `FallingSmallRock::exeWait` |  |
| `HostTypeMove` | `FallingSmallRock::exeMove` |  |

## FileSelectButton

src/Game/Screen/FileSelectButton.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FileSelectButtonNrvSelect` | `FileSelectButton::exeSelect` |  |
| `FileSelectButtonNrvWait` | `FileSelectButton::exeWait` |  |
| `FileSelectButtonNrvDisappear` | `FileSelectButton::exeDisappear` |  |

## FileSelectCameraController

src/Game/Map/FileSelectCameraController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FileSelectCameraControllerNrvTitle` | `FileSelectCameraController::exeTitle` |  |
| `FileSelectCameraControllerNrvMoveToFarPoint` | `FileSelectCameraController::exeMoveToFarPoint` |  |
| `FileSelectCameraControllerNrvFarPoint` | `FileSelectCameraController::exeFarPoint` |  |
| `FileSelectCameraControllerNrvMoveToNearPoint` | `FileSelectCameraController::exeMoveToNearPoint` |  |
| `FileSelectCameraControllerNrvNearPoint` | `FileSelectCameraController::exeNearPoint` |  |

## FileSelectEffect

src/Game/Map/FileSelectEffect.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FileSelectEffectNrvAppear` | `FileSelectEffect::exeAppear` |  |
| `FileSelectEffectNrvWait` | `FileSelectEffect::exeWait` |  |
| `FileSelectEffectNrvDisappear` | `FileSelectEffect::exeDisappear` |  |

## FileSelectInfo

src/Game/Screen/FileSelectInfo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FileSelectInfoNrvAppear` | `FileSelectInfo::exeAppear` |  |
| `FileSelectInfoNrvDisplay` | `FileSelectInfo::exeDisplay` |  |
| `FileSelectInfoNrvDisappear` | `FileSelectInfo::exeDisappear` |  |

## FileSelectItem

src/Game/Map/FileSelectItem.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FileSelectItemNrvNewWait` | `FileSelectItem::exeNewWait` |  |
| `FileSelectItemNrvExistWait` | `FileSelectItem::exeExistWait` |  |
| `FileSelectItemNrvFormat` | `FileSelectItem::exeFormat` |  |
| `FileSelectItemNrvChangeFellow` | `FileSelectItem::exeChangeFellow` |  |
| `FileSelectItemNrvChangeMii` | `FileSelectItem::exeChangeMii` |  |

## FileSelectModel

src/Game/Map/FileSelectModel.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FileSelectModelNrvOpen` | `FileSelectModel::exeOpen` |  |
| `FileSelectModelNrvBlinkOnce` | `FileSelectModel::exeBlinkOnce` |  |
| `FileSelectModelNrvClose` | `FileSelectModel::exeClose` |  |
| `FileSelectModelNrvBlink` | `FileSelectModel::exeBlink` |  |

## FileSelectNumber

src/Game/Screen/FileSelectNumber.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FileSelectNumberNrvAppear` | `FileSelectNumber::exeAppear` |  |
| `FileSelectNumberNrvWait` | `FileSelectNumber::exeWait` |  |
| `FileSelectNumberNrvEnd` | `FileSelectNumber::exeEnd` |  |

## FileSelector

src/Game/Map/FileSelector.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FileSelectorNrvWaitBind` | `FileSelector::exeWaitBind` |  |
| `FileSelectorNrvTitle` | `FileSelector::exeTitle` |  |
| `FileSelectorNrvTitleEnd` | `FileSelector::exeTitleEnd` |  |
| `FileSelectorNrvMiiDataError` | `FileSelector::exeMiiDataError` |  |
| `FileSelectorNrvMiiDataWait` | `FileSelector::exeMiiDataWait` |  |
| `FileSelectorNrvMiiDataWaitEnd` | `FileSelector::exeMiiDataWaitEnd` |  |
| `FileSelectorNrvFileSelectStart` | `FileSelector::exeFileSelectStart` |  |
| `FileSelectorNrvFileSelect` | `FileSelector::exeFileSelect` |  |
| `FileSelectorNrvFileConfirmStart` | `FileSelector::exeFileConfirmStart` |  |
| `FileSelectorNrvFileConfirmMiiDeleteWarningStart` | `FileSelector::exeFileConfirmMiiDeleteWarningStart` |  |
| `FileSelectorNrvFileConfirmMiiDeleteWarning` | `FileSelector::exeFileConfirmMiiDeleteWarning` |  |
| `FileSelectorNrvFileConfirmMiiDeleteSave` | `FileSelector::exeFileConfirmMiiDeleteSave` |  |
| `FileSelectorNrvFileConfirm` | `FileSelector::exeFileConfirm` |  |
| `FileSelectorNrvDemoStartWait` | `FileSelector::exeDemoStartWait` |  |
| `FileSelectorNrvDemo` | `FileSelector::exeDemo` |  |
| `FileSelectorNrvCreateConfirmStart` | `FileSelector::exeCreateConfirmStart` |  |
| `FileSelectorNrvCreateConfirm` | `FileSelector::exeCreateConfirm` |  |
| `FileSelectorNrvCreate` | `FileSelector::exeCreate` |  |
| `FileSelectorNrvCopyWait` | `FileSelector::exeCopyWait` |  |
| `FileSelectorNrvCopySelect` | `FileSelector::exeCopySelect` |  |
| `FileSelectorNrvCopyConfirmStart` | `FileSelector::exeCopyConfirmStart` |  |
| `FileSelectorNrvCopyConfirm` | `FileSelector::exeCopyConfirm` |  |
| `FileSelectorNrvCopySave` | `FileSelector::exeCopySave` |  |
| `FileSelectorNrvCopySaveMii` | `FileSelector::exeCopySaveMii` |  |
| `FileSelectorNrvCopyDemo` | `FileSelector::exeCopyDemo` |  |
| `FileSelectorNrvCopyRejectStart` | `FileSelector::exeCopyRejectStart` |  |
| `FileSelectorNrvCopyReject` | `FileSelector::exeCopyReject` |  |
| `FileSelectorNrvMiiWait` | `FileSelector::exeMiiWait` |  |
| `FileSelectorNrvMiiTip` | `FileSelector::exeMiiTip` |  |
| `FileSelectorNrvMiiSelectStartFirst` | `FileSelector::exeMiiSelectStart` |  |
| `FileSelectorNrvMiiSelectStart` | `FileSelector::exeMiiSelectStart` |  |
| `FileSelectorNrvMiiSelect` | `FileSelector::exeMiiSelect` |  |
| `FileSelectorNrvMiiCancel` | `FileSelector::exeMiiCancel` |  |
| `FileSelectorNrvMiiConfirmWait` | `FileSelector::exeMiiConfirmWait` |  |
| `FileSelectorNrvMiiConfirm` | `FileSelector::exeMiiConfirm` |  |
| `FileSelectorNrvMiiCreateWait` | `FileSelector::exeMiiCreateWait` |  |
| `FileSelectorNrvMiiCreateDemo` | `FileSelector::exeMiiCreateDemo` |  |
| `FileSelectorNrvMiiCaution` | `FileSelector::exeMiiCaution` |  |
| `FileSelectorNrvMiiInfoStart` | `FileSelector::exeMiiInfoStart` |  |
| `FileSelectorNrvMiiInfo` | `FileSelector::exeMiiInfo` |  |
| `FileSelectorNrvDeleteConfirmStart` | `FileSelector::exeDeleteConfirmStart` |  |
| `FileSelectorNrvDeleteConfirm` | `FileSelector::exeDeleteConfirm` |  |
| `FileSelectorNrvDelete` | `FileSelector::exeDelete` |  |
| `FileSelectorNrvDeleteDemo` | `FileSelector::exeDeleteDemo` |  |
| `FileSelectorNrvFileBroken` | `FileSelector::exeFileBroken` |  |
| `FileSelectorNrvManualStart` | `FileSelector::exeManualStart` |  |
| `FileSelectorNrvManual` | `FileSelector::exeManual` |  |

## FileSelectSky

src/Game/Map/FileSelectSky.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FileSelectSkyNrvWait` | `FileSelectSky::exeWait` |  |

## FireBall

src/Game/Enemy/FireBall.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FireBallNrvThrow` | `FireBall::exeThrow` |  |
| `FireBallNrvReflect` | `FireBall::exeReflect` |  |

## FireBar

src/Game/MapObj/FireBar.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FireBarNrvWait` | `FireBar::exeWait` |  |

## FireBubble

src/Game/Enemy/FireBubble.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FireBubbleNrvAppear` | `FireBubble::exeAppear` |  |
| `FireBubbleNrvWait` | `FireBubble::exeWait` |  |
| `FireBubbleNrvChase` | `FireBubble::exeChase` |  |
| `FireBubbleNrvReflect` | `FireBubble::exeReflect` |  |
| `FireBubbleNrvReflectLand` | `FireBubble::exeReflectLand` |  |
| `FireBubbleNrvReflectWait` | `FireBubble::exeReflectWait` |  |
| `FireBubbleNrvDown` | `FireBubble::exeDown` |  |
| `FireBubbleNrvWaitToAppear` | `FireBubble::exeWaitToAppear` |  |

## FireMarioBall

src/Game/Player/FireMarioBall.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FireMarioBallNrvThrow` | `FireMarioBall::exeThrow` |  |

## FirePressureBullet

src/Game/MapObj/FirePressureBullet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FirePressureBulletNrvFly` | `FirePressureBullet::exeFly` |  |

## FirePressureRadiate

src/Game/MapObj/FirePressureRadiate.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FirePressureRadiateNrvRelax` | `FirePressureRadiate::exeRelax` |  |
| `FirePressureRadiateNrvSyncWait` | `FirePressureRadiate::exeSyncWait` |  |
| `FirePressureRadiateNrvWait` | `FirePressureRadiate::exeWait` |  |
| `FirePressureRadiateNrvPrepareToRadiate` | `FirePressureRadiate::exePrepareToRadiate` |  |
| `FirePressureRadiateNrvRadiate` | `FirePressureRadiate::exeRadiate` |  |
| `FirePressureRadiateNrvRadiateMargin` | `FirePressureRadiate::exeRadiateMargin` |  |

## Fish

src/Game/Map/FishGroup.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FishNrvWander` | `Fish::exeWander` |  |
| `FishNrvApproach` | `Fish::exeApproach` |  |
| `FishNrvEscape` | `Fish::exeEscape` |  |

## FlameGun

src/Game/MapObj/FlameGun.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvSwitchWait` | `FlameGun::exeSwitchWait` |  |
| `HostTypeNrvWait` | `FlameGun::exeWait` |  |
| `HostTypeNrvPreRadiate` | `FlameGun::exePreRadiate` |  |
| `HostTypeNrvRadiate` | `FlameGun::exeRadiate` |  |
| `HostTypeNrvRotate` | `FlameGun::exeRotate` |  |
| `HostTypeNrvRadiateOnly` | `FlameGun::exeRadiateOnly` |  |

## FlipPanel

src/Game/MapObj/FlipPanel.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FlipPanelNrvFrontLand` | `FlipPanel::exeFrontLand` |  |
| `FlipPanelNrvBackLand` | `FlipPanel::exeBackLand` |  |
| `FlipPanelNrvFront` | `FlipPanel::exeWait` |  |
| `FlipPanelNrvBack` | `FlipPanel::exeWait` |  |
| `FlipPanelNrvEndPrepare` | `FlipPanel::exeEndPrepare` |  |
| `FlipPanelNrvEnd` | `FlipPanel::exeEnd` |  |

## FlipPanelObserver

src/Game/MapObj/FlipPanel.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FlipPanelObserverNrvWait` | `FlipPanelObserver::exeWait` |  |
| `FlipPanelObserverNrvComplete` | `FlipPanelObserver::exeComplete` |  |
| `FlipPanelObserverNrvDemoWait` | `FlipPanelObserver::exeDemoWait` |  |

## FloaterFloatingForceTypeNormal

src/Game/MapObj/FloaterFloatingForceTypeNormal.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `FloaterFloatingForceTypeNormal::exeWait` |  |
| `HostTypeMoveFirst` | `FloaterFloatingForceTypeNormal::exeMoveFirst` |  |
| `HostTypeMoveOffPlayer` | `FloaterFloatingForceTypeNormal::exeMove` |  |
| `HostTypeMoveOnPlayer` | `FloaterFloatingForceTypeNormal::exeMove` |  |

## FloaterFloatingForceTypeSpring

src/Game/MapObj/FloaterFloatingForceTypeSpring.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `FloaterFloatingForceTypeSpring::exeWait` |  |
| `HostTypeMove` | `FloaterFloatingForceTypeSpring::exeMove` |  |
| `HostTypeMoveReturn` | `FloaterFloatingForceTypeSpring::exeMoveReturn` |  |

## Fluff

src/Game/Ride/Fluff.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FluffNrvFreeBloom` | `Fluff::exeFreeBloom` |  |
| `FluffNrvFreeWaitOnGround` | `Fluff::exeFreeWaitOnGround` |  |
| `FluffNrvFreeWaitAir` | `Fluff::exeFreeWaitAir` |  |
| `FluffNrvFreeWaitInvalid` | `Fluff::exeFreeWaitInvalid` |  |
| `FluffNrvRideStart` | `Fluff::exeRideStart` |  |
| `FluffNrvRideFly` | `Fluff::exeRideFly` |  |
| `FluffNrvRideFlyOnWind` | `Fluff::exeRideFlyOnWind` |  |
| `FluffNrvRideSpinUp` | `Fluff::exeRideSpinUp` |  |
| `FluffNrvRideSpinUpOnWind` | `Fluff::exeRideSpinUp` |  |

## FluffWindEffect

src/Game/Ride/FluffWind.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FluffWindEffectNrvBlowWind` | `FluffWindEffect::exeBlowWind` |  |

## Fountain

src/Game/MapObj/Fountain.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `Fountain::exeWait` |  |
| `HostTypeMove` | `Fountain::exeMove` |  |

## FountainBig

src/Game/MapObj/FountainBig.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FountainBigNrvWait` | `FountainBig::exeWait` |  |
| `FountainBigNrvSign` | `FountainBig::exeSign` |  |
| `FountainBigNrvSignStop` | `FountainBig::exeSignStop` |  |
| `FountainBigNrvSpout` | `FountainBig::exeSpout` |  |
| `FountainBigNrvSpoutEnd` | `FountainBig::exeSpoutEnd` |  |

## FullnessMeter

src/Game/Screen/FullnessMeter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `FullnessMeterNrvAppear` | `FullnessMeter::exeAppear` |  |
| `FullnessMeterNrvWait` | `FullnessMeter::exeWait` |  |
| `FullnessMeterNrvEnd` | `FullnessMeter::exeEnd` |  |

## Further reading

* [Nerves](nerves.md)
