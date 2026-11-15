---
title: Nerves, classes N to Z
shortTitle: Nerves, classes N to Z
intro: The nerves of the game's classes whose names start with N to Z.
contentType: reference
---

## About this list

This page lists the nerves of the game's classes whose names start with N to Z. Each section is one class, with its source file. The Nerve column names the nerve, the Code column names the function that runs every frame in it, and the On leaving column names the function that runs as an object leaves it. For how to use nerves, see [Nerves](nerves.md).

## NebulaAchievementBoard

src/Game/Screen/NebulaAchievementBoard.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `NebulaAchievementBoardNrvAppear` | `NebulaAchievementBoard::exeAppear` |  |
| `NebulaAchievementBoardNrvWait` | `NebulaAchievementBoard::exeWait` |  |
| `NebulaAchievementBoardNrvDisappear` | `NebulaAchievementBoard::exeDisappear` |  |

## NebulaAchievementLuma

src/Game/NPC/NebulaAchievementLuma.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `NebulaAchievementLumaNrvWait` | `NebulaAchievementLuma::exeNebulaWait` |  |
| `NebulaAchievementLumaNrvOpen` | `NebulaAchievementLuma::exeNebulaOpen` |  |

## NeedlePlant

src/Game/MapObj/NeedlePlant.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `NeedlePlantNrvWait` | `NeedlePlant::exeWait` |  |
| `NeedlePlantNrvShake` | `NeedlePlant::exeShake` |  |

## NokonokoLand

src/Game/Enemy/NokonokoLand.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeAppearReady` | `NokonokoLand::exeAppearReady` |  |
| `HostTypeAppearStart` | `NokonokoLand::exeAppearStart` | `NokonokoLand::exeAppearStart` |
| `HostTypeAppear` | `NokonokoLand::exeAppear` |  |
| `HostTypeDead` | `NokonokoLand::exeDead` |  |
| `HostTypeFlyAway` | `NokonokoLand::exeFlyAway` |  |
| `HostTypeWalk` | `NokonokoLand::exeWalk` |  |
| `HostTypeLookAround` | `NokonokoLand::exeLookAround` |  |
| `HostTypeTurnStart` | `NokonokoLand::exeTurnStart` |  |
| `HostTypeTurn` | `NokonokoLand::exeTurnLoop` |  |
| `HostTypeTurnEnd` | `NokonokoLand::exeTurnEnd` |  |
| `HostTypeAttack` | `NokonokoLand::exeAttack` |  |
| `HostTypeAttackTurn` | `NokonokoLand::exeAttack` |  |
| `HostTypeRattle` | `NokonokoLand::exeRattle` |  |
| `HostTypeRattleTurn` | `NokonokoLand::exeRattle` |  |
| `HostTypeBindStarPointer` | `NokonokoLand::exeBindStarPointer` | `NokonokoLand::exeBindStarPointer` |
| `HostTypeBindStarPointerTurn` | `NokonokoLand::exeBindStarPointer` | `NokonokoLand::exeBindStarPointer` |
| `HostTypeShellStart` | `NokonokoLand::exeShellStartTrample` |  |
| `HostTypeShellWait` | `NokonokoLand::exeShellWait` |  |
| `HostTypeShellEnd` | `NokonokoLand::exeShellEnd` |  |
| `HostTypeShellStartPunch` | `NokonokoLand::exeShellStartPunch` |  |
| `HostTypeBecomeJetTurtle` | `NokonokoLand::exeBecomeJetTurtle` |  |

## Note

src/Game/MapObj/Note.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `NoteNrvWait` | `Note::exeWait` |  |
| `NoteNrvCountDown` | `Note::exeCountDown` |  |
| `NoteNrvFlyUp` | `Note::exeFlyUp` |  |

## NoteCounter

src/Game/Screen/NoteCounter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `NoteCounterNrvShow` | `NoteCounter::exeShow` |  |
| `NoteCounterNrvShowWait` | `NoteCounter::exeShowWait` |  |
| `NoteCounterNrvShowToHide` | `NoteCounter::exeShowToHide` |  |
| `NoteCounterNrvHideToShow` | `NoteCounter::exeHideToShow` |  |
| `NoteCounterNrvHide` | `NoteCounter::exeHide` |  |

## NoteFairy

src/Game/MapObj/NoteFairy.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `NoteFairyNrvHide` | `NoteFairy::exeHide` |  |
| `NoteFairyNrvStartAppearDemo` | `NoteFairy::exeStartAppearDemo` |  |
| `NoteFairyNrvRailMoveStart` | `NoteFairy::exeRailMoveStart` |  |
| `NoteFairyNrvDemoAppear` | `NoteFairy::exeDemoAppear` |  |
| `NoteFairyNrvAppearNoteBloom` | `NoteFairy::exeAppearNoteBloom` |  |
| `NoteFairyNrvAppearNote` | `NoteFairy::exeAppearNote` |  |
| `NoteFairyNrvEndWait` | `NoteFairy::exeEndWait` |  |
| `NoteFairyNrvSuccessDemo` | `NoteFairy::exeSuccessDemo` |  |
| `NoteFairyNrvSuccess` | `NoteFairy::exeSuccess` |  |

## NPCActor

src/Game/NPC/NPCActor.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `NPCActorNrvReaction` | `NPCActor::exeReaction` |  |
| `NPCActorNrvWait` | `NPCActor::exeWait` |  |
| `NPCActorNrvTalk` | `NPCActor::exeTalk` |  |
| `NPCActorNrvNull` | `NPCActor::exeNull` |  |

## OceanFloaterLandParts

src/Game/MapObj/OceanFloaterLandParts.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `OceanFloaterLandParts::exeWait` |  |
| `HostTypeMove` | `OceanFloaterLandParts::exeMove` |  |
| `HostTypeDone` | `OceanFloaterLandParts::exeDone` |  |

## OceanSmallTurtle

src/Game/MapObj/OceanSmallTurtle.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `OceanSmallTurtleNrvWait` | `OceanSmallTurtle::exeWait` |  |
| `OceanSmallTurtleNrvFloatSignal` | `OceanSmallTurtle::exeFloatSignal` |  |
| `OceanSmallTurtleNrvFloatUp` | `OceanSmallTurtle::exeFloatUp` |  |
| `OceanSmallTurtleNrvCatchRail` | `OceanSmallTurtle::exeCatchRail` |  |
| `OceanSmallTurtleNrvRailMove` | `OceanSmallTurtle::exeRailMove` |  |

## OceanSphere

src/Game/Map/OceanSphere.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `OceanSphereNrvRiseUp` | `OceanSphere::exeRiseUp` |  |
| `OceanSphereNrvWait` | `OceanSphere::exeWait` |  |

## OneUpBoard

src/Game/Screen/OneUpBoard.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `OneUpBoard::exeWait` |  |
| `HostTypeAppear` | `OneUpBoard::exeAppear` |  |

## Onimasu

src/Game/Enemy/Onimasu.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWaitForSwitchOn` | `Onimasu::exeWaitForSwitchOn` |  |
| `HostTypeWait` | `Onimasu::exeWait` |  |
| `HostTypeWaitForStamp` | `Onimasu::exeWait` |  |
| `HostTypeJump` | `Onimasu::exeMove` |  |
| `HostTypeStamp` | `Onimasu::exeStamp` |  |

## OtaKing

src/Game/Boss/OtaKing.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `OtaKingNrvWaitOnSwitch` | `OtaKing::exeWaitOnSwitch` |  |
| `OtaKingNrvAppearDemo` | `OtaKing::exeAppearDemo` |  |
| `OtaKingNrvWait` | `OtaKing::exeWait` |  |
| `OtaKingNrvThrowCocoNutWait` | `OtaKing::exeThrowCocoNutWait` |  |
| `OtaKingNrvThrowCocoNut` | `OtaKing::exeThrowCocoNut` |  |
| `OtaKingNrvThrowFireBallWait` | `OtaKing::exeThrowFireBallWait` |  |
| `OtaKingNrvThrowFireBall` | `OtaKing::exeThrowFireBall` |  |
| `OtaKingNrvDamage` | `OtaKing::exeDamage` |  |
| `OtaKingNrvPowerUp` | `OtaKing::exePowerUp` |  |
| `OtaKingNrvDown` | `OtaKing::exeDown` |  |
| `OtaKingNrvDownDemo` | `OtaKing::exeDownDemo` |  |
| `OtaKingNrvAppearStar` | `OtaKing::exeAppearStar` |  |
| `OtaKingNrvDead` | `OtaKing::exeDead` |  |
| `OtaKingNrvHitBackStart` | `OtaKing::exeHitBackStart` |  |
| `OtaKingNrvHitBack` | `OtaKing::exeHitBack` |  |
| `OtaKingNrvNearAttack` | `OtaKing::exeNearAttack` |  |
| `OtaKingNrvBubbleAttack` | `OtaKing::exeBubbleAttack` |  |
| `OtaKingNrvWaitStartDemo` | `OtaKing::exeWaitStartDemo` |  |

## OtaKingLongFoot

src/Game/Boss/OtaKingLongFoot.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `OtaKingLongFootNrvWait` | `OtaKingLongFoot::exeWait` |  |
| `OtaKingLongFootNrvDamage` | `OtaKingLongFoot::exeDamage` |  |
| `OtaKingLongFootNrvAppearDemo` | `OtaKingLongFoot::exeAppearDemo` |  |
| `OtaKingLongFootNrvDownDemo` | `OtaKingLongFoot::exeDownDemo` |  |

## OtaKingMagma

src/Game/Boss/OtaKingMagma.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `OtaKingMagmaNrvWait` | `OtaKingMagma::exeWait` |  |
| `OtaKingMagmaNrvAppearDemo` | `OtaKingMagma::exeAppearDemo` |  |
| `OtaKingMagmaNrvAttack` | `OtaKingMagma::exeAttack` |  |
| `OtaKingMagmaNrvFireAttack` | `OtaKingMagma::exeFireAttack` |  |
| `OtaKingMagmaNrvHitBack` | `OtaKingMagma::exeHitBack` |  |
| `OtaKingMagmaNrvDamage` | `OtaKingMagma::exeDamage` |  |
| `OtaKingMagmaNrvDown` | `OtaKingMagma::exeDown` |  |
| `OtaKingMagmaNrvEndWait` | `OtaKingMagma::exeEndWait` |  |

## OtaRock

src/Game/Enemy/OtaRock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `OtaRockNrvWait` | `OtaRock::exeWait` |  |
| `OtaRockNrvThrowCocoNut` | `OtaRock::exeThrowCocoNut` |  |
| `OtaRockNrvThrowFireBall` | `OtaRock::exeThrowFireBall` |  |
| `OtaRockNrvDown` | `OtaRock::exeDown` |  |
| `OtaRockNrvHide` | `OtaRock::exeHide` |  |
| `OtaRockNrvHideWait` | `OtaRock::exeHideWait` |  |
| `OtaRockNrvShow` | `OtaRock::exeShow` |  |

## PackunPetit

src/Game/Enemy/PackunPetit.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PackunPetitNrvNonActive` | `PackunPetit::exeNonActive` | `PackunPetit::exeNonActive` |
| `PackunPetitNrvWait` | `PackunPetit::exeWait` |  |
| `PackunPetitNrvFind` | `PackunPetit::exeFind` |  |
| `PackunPetitNrvThreat` | `PackunPetit::exeThreat` |  |
| `PackunPetitNrvLeftTurn` | `PackunPetit::exeTurn` |  |
| `PackunPetitNrvRightTurn` | `PackunPetit::exeTurn` |  |
| `PackunPetitNrvAttackStart` | `PackunPetit::exeAttackStart` |  |
| `PackunPetitNrvAttack` | `PackunPetit::exeAttack` |  |
| `PackunPetitNrvAttackBack` | `PackunPetit::exeAttackBack` |  |
| `PackunPetitNrvHitWaitForAttack` | `PackunPetit::exeHitWaitForAttack` |  |
| `PackunPetitNrvHit` | `PackunPetit::exeHit` |  |
| `PackunPetitNrvTrampleDown` | `PackunPetit::exeTrampleDown` |  |
| `PackunPetitNrvPunchDown` | `PackunPetit::exePunchDown` |  |
| `PackunPetitNrvSwoonStart` | `PackunPetit::exeSwoonStart` |  |
| `PackunPetitNrvSwoon` | `PackunPetit::exeSwoon` |  |
| `PackunPetitNrvSwoonToThreat` | `PackunPetit::exeSwoonToThreat` |  |
| `PackunPetitNrvPointerSwoon` | `PackunPetit::exePointerSwoon` | `PackunPetit::exePointerSwoon` |

## PalmIsland

src/Game/MapObj/PalmIsland.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PalmIslandNrvWait` | `PalmIsland::exeWait` |  |
| `PalmIslandNrvFloat` | `PalmIsland::exeFloat` |  |

## PauseMenu

src/Game/Screen/PauseMenu.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PauseMenuNrvSelecting` | `PauseMenu::exeSelecting` |  |
| `PauseMenuNrvDecided` | `PauseMenu::exeDecided` |  |
| `PauseMenuNrvDisappear` | `PauseMenu::exeDisappear` |  |
| `PauseMenuNrvConfirm` | `PauseMenu::exeConfirm` |  |
| `PauseMenuNrvGameDataSave` | `PauseMenu::exeGameDataSave` |  |
| `PauseMenuNrvLuigiLetter` | `PauseMenu::exeLuigiLetter` |  |

## Peach

src/Game/NPC/Peach.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PeachNrvWait` | `Peach::exeWait` |  |

## PeachCastleGardenPlanet

src/Game/MapObj/PeachCastleGardenPlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PeachCastleGardenPlanetNrvWait` | `PeachCastleGardenPlanet::exeWait` |  |
| `PeachCastleGardenPlanetNrvDamage` | `PeachCastleGardenPlanet::exeDamage` |  |

## PeachLetter

src/Game/Screen/PeachLetter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PeachLetterNrvAppear` | `PeachLetter::exeAppear` |  |
| `PeachLetterNrvWait` | `PeachLetter::exeWait` |  |
| `PeachLetterNrvEnd` | `PeachLetter::exeEnd` |  |

## Penguin

src/Game/NPC/Penguin.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PenguinNrvWait` | `Penguin::exeWait` |  |
| `PenguinNrvReaction` | `Penguin::exeReaction` |  |
| `PenguinNrvDive` | `Penguin::exeDive` |  |
| `PenguinNrvFlow` | `Penguin::exeFlow` |  |

## PenguinCoach

src/Game/NPC/PenguinCoach.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PenguinCoachNrvTakeOutStar` | `PenguinCoach::exeTakeOutStar` |  |
| `PenguinCoachNrvRace` | `PenguinCoach::exeRace` |  |
| `PenguinCoachNrvReaction` | `PenguinCoach::exeReaction` |  |
| `PenguinCoachNrvPrep` | `PenguinCoach::exePrep` |  |
| `PenguinCoachNrvWait` | `PenguinCoach::exeWait` |  |
| `PenguinCoachNrvFade` | `PenguinCoach::exeFade` |  |
| `PenguinCoachNrvTutorial` | `PenguinCoach::exeTutorial` |  |
| `PenguinCoachNrvPraise` | `PenguinCoach::exePraise` |  |

## PenguinMaster

src/Game/NPC/PenguinMaster.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PenguinMasterNrvWait` | `PenguinMaster::exeWait` |  |
| `PenguinMasterNrvReaction` | `PenguinMaster::exeReaction` |  |

## PenguinRacer

src/Game/NPC/PenguinRacer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PenguinRacerNrvWait` | `PenguinRacer::exeWait` |  |
| `PenguinRacerNrvReaction` | `PenguinRacer::exeReaction` |  |
| `PenguinRacerNrvRacePrep` | `PenguinRacer::exeRaceWait` |  |
| `PenguinRacerNrvRaceWalk` | `PenguinRacer::exeRaceWalk` |  |
| `PenguinRacerNrvRaceSwim` | `PenguinRacer::exeRaceSwim` |  |
| `PenguinRacerNrvRaceStoW` | `PenguinRacer::exeRaceStoW` |  |
| `PenguinRacerNrvRaceWtoS` | `PenguinRacer::exeRaceWtoS` |  |
| `PenguinRacerNrvRaceFall` | `PenguinRacer::exeRaceFall` |  |
| `PenguinRacerNrvRaceGoal` | `PenguinRacer::exeRaceGoal` |  |
| `PenguinRacerNrvRaceTurn` | `PenguinRacer::exeRaceTurn` |  |

## PenguinRacerLeader

src/Game/NPC/PenguinRacerLeader.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PenguinRacerLeaderNrvWait` | `PenguinRacerLeader::exeWait` |  |
| `PenguinRacerLeaderNrvPre` | `PenguinRacerLeader::exePre` |  |
| `PenguinRacerLeaderNrvReady` | `PenguinRacerLeader::exeReady` |  |
| `PenguinRacerLeaderNrvPost` | `PenguinRacerLeader::exePost` |  |
| `PenguinRacerLeaderNrvTalk` | `PenguinRacerLeader::exeTalk` |  |
| `PenguinRacerLeaderNrvTakeOutStar` | `PenguinRacerLeader::exeTakeOutStar` |  |

## PenguinSkater

src/Game/NPC/PenguinSkater.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PenguinSkaterNrvReaction` | `PenguinSkater::exeReaction` |  |
| `PenguinSkaterNrvTalk` | `PenguinSkater::exeTalk` |  |
| `PenguinSkaterNrvWait` | `PenguinSkater::exeWait` |  |
| `PenguinSkaterNrvDemo` | `PenguinSkater::exeDemo` |  |
| `PenguinSkaterNrvAway` | `PenguinSkater::exeAway` |  |
| `PenguinSkaterNrvTurn` | `PenguinSkater::exeTurn` |  |
| `PenguinSkaterNrvProvoke` | `PenguinSkater::exeProvoke` |  |
| `PenguinSkaterNrvCaught` | `PenguinSkater::exeCaught` |  |
| `PenguinSkaterNrvFadeOut` | `PenguinSkater::exeFadeOut` |  |
| `PenguinSkaterNrvFadeIn` | `PenguinSkater::exeFadeIn` |  |
| `PenguinSkaterNrvGiveUp` | `PenguinSkater::exeGiveUp` |  |
| `PenguinSkaterNrvSitDown` | `PenguinSkater::exeSitDown` |  |
| `PenguinSkaterNrvSwitch` | `PenguinSkater::exeSwitch` |  |

## PenguinStudent

src/Game/NPC/PenguinStudent.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PenguinStudentNrvSwim` | `PenguinStudent::exeSwim` |  |
| `PenguinStudentNrvLead` | `PenguinStudent::exeLead` |  |
| `PenguinStudentNrvSlow` | `PenguinStudent::exeSlow` |  |

## Petari

src/Game/Enemy/Petari.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PetariNrvLurk` | `Petari::exeLurk` |  |
| `PetariNrvJumpOut` | `Petari::exeJumpOut` |  |
| `PetariNrvWait` | `Petari::exeWait` | `Petari::exeWait` |
| `PetariNrvApproach` | `Petari::exeApproach` | `Petari::exeApproach` |
| `PetariNrvEscape` | `Petari::exeEscape` | `Petari::exeEscape` |
| `PetariNrvSwoonStart` | `Petari::exeSwoonStart` |  |
| `PetariNrvSwoon` | `Petari::exeSwoon` |  |
| `PetariNrvSwoonEnd` | `Petari::exeSwoonEnd` |  |
| `PetariNrvLand` | `Petari::exeLand` |  |
| `PetariNrvSmash` | `Petari::exeSmash` |  |
| `PetariNrvSpinOut` | `Petari::exeSpinOut` |  |
| `PetariNrvKickOut` | `Petari::exeKickOut` |  |
| `PetariNrvFreeze` | `Petari::exeFreeze` | `Petari::exeFreeze` |

## PhantomShipBoxFloater

src/Game/MapObj/PhantomShipBoxFloater.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `PhantomShipBoxFloater::exeWait` |  |
| `HostTypeMove` | `PhantomShipBoxFloater::exeMove` |  |
| `HostTypeMoveUnderWater` | `PhantomShipBoxFloater::exeMoveUnderWater` |  |

## PhantomShipBridge

src/Game/MapObj/PhantomShipBridge.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `PhantomShipBridge::exeWait` |  |
| `HostTypeMoveA` | `PhantomShipBridge::exeMoveA` |  |
| `HostTypeMoveB` | `PhantomShipBridge::exeMoveB` |  |

## PhantomTorch

src/Game/MapObj/PhantomTorch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `PhantomTorch::exeWait` |  |
| `HostTypeAttack` | `PhantomTorch::exeAttack` |  |
| `HostTypeExtinguish` | `PhantomTorch::exeExtinguish` |  |

## PicketSwitch

src/Game/MapObj/PicketSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PicketSwitchNrvWait` | `PicketSwitch::exeWait` |  |
| `PicketSwitchNrvFirstDrop` | `PicketSwitch::exeFirstDrop` |  |
| `PicketSwitchNrvLastDrop` | `PicketSwitch::exeLastDrop` |  |

## PictureBookCloseButton

src/Game/Screen/PictureBookCloseButton.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PictureBookCloseButtonNrvSelect` | `PictureBookCloseButton::exeSelect` |  |
| `PictureBookCloseButtonNrvSelected` | `PictureBookCloseButton::exeSelected` |  |
| `PictureBookCloseButtonNrvNotSelected` | `PictureBookCloseButton::exeNotSelected` |  |

## PictureBookLayout

src/Game/Screen/PictureBookLayout.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PictureBookLayoutNrvOpen` | `PictureBookLayout::exeOpen` |  |
| `PictureBookLayoutNrvContentsSelect` | `PictureBookLayout::exeContentsSelect` |  |
| `PictureBookLayoutNrvContentsFadeOut` | `PictureBookLayout::exeContentsFadeOut` |  |
| `PictureBookLayoutNrvFadeIn` | `PictureBookLayout::exeFadeIn` |  |
| `PictureBookLayoutNrvWaitNoText` | `PictureBookLayout::exeWaitNoText` |  |
| `PictureBookLayoutNrvFadeInText` | `PictureBookLayout::exeFadeInText` |  |
| `PictureBookLayoutNrvWaitWithText` | `PictureBookLayout::exeWaitWithText` |  |
| `PictureBookLayoutNrvFadeOutText` | `PictureBookLayout::exeFadeOutText` |  |
| `PictureBookLayoutNrvPageNext` | `PictureBookLayout::exePageNext` |  |
| `PictureBookLayoutNrvFadeOut` | `PictureBookLayout::exeFadeOut` |  |
| `PictureBookLayoutNrvClose` | `PictureBookLayout::exeClose` |  |

## Plant

src/Game/Ride/Plant.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PlantNrvWaitFar` | `Plant::exeWaitFar` |  |
| `PlantNrvSeedWait` | `Plant::exeSeedWait` |  |
| `PlantNrvWaitDemoWaitGrowUp` | `Plant::exeWaitDemoWaitGrowUp` |  |
| `PlantNrvDemoWaitGrowUp` | `Plant::exeDemoWaitGrowUp` |  |
| `PlantNrvGrowUp` | `Plant::exeGrowUp` |  |
| `PlantNrvHangWaitGrowUp` | `Plant::exeHangWaitGrowUp` |  |
| `PlantNrvHangUpGrowUp` | `Plant::exeHangUpGrowUp` |  |
| `PlantNrvGrowthStop` | `Plant::exeGrowthStop` |  |
| `PlantNrvGrowthWait` | `Plant::exeGrowthWait` |  |
| `PlantNrvGrowthWaitInvalid` | `Plant::exeGrowthWaitInvalid` |  |
| `PlantNrvHangStart` | `Plant::exeHangStart` |  |
| `PlantNrvHangUp` | `Plant::exeHangUp` |  |
| `PlantNrvHangDown` | `Plant::exeHangDown` |  |

## PlantMember

src/Game/MapObj/PlantGroup.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `PlantMember::exeWait` |  |
| `HostTypeNrvHint` | `PlantMember::exeHint` |  |
| `HostTypeNrvShakeWeak` | `PlantMember::exeShakeWeak` |  |
| `HostTypeNrvShakeMiddle` | `PlantMember::exeShakeMiddle` |  |
| `HostTypeNrvShakeStrong` | `PlantMember::exeShakeStrong` |  |

## PlayerActionGuidance

src/Game/Screen/PlayerActionGuidance.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PlayerActionGuidanceWaitFocusIn` | `PlayerActionGuidance::exeWaitFocusIn` |  |
| `PlayerActionGuidanceFadein` | `PlayerActionGuidance::exeFadein` |  |
| `PlayerActionGuidanceDisplay` | `PlayerActionGuidance::exeDisplay` |  |
| `PlayerActionGuidanceSuspend` | `PlayerActionGuidance::exeSuspend` |  |
| `PlayerActionGuidanceFadeout` | `PlayerActionGuidance::exeFadeout` |  |

## PlayerLeft

src/Game/Screen/PlayerLeft.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PlayerLeftNrvHide` | `PlayerLeft::exeHide` |  |
| `PlayerLeftNrvAppear` | `PlayerLeft::exeAppear` |  |
| `PlayerLeftNrvWait` | `PlayerLeft::exeWait` |  |
| `PlayerLeftNrvDisappear` | `PlayerLeft::exeDisappear` |  |

## PlayerMissLeft

src/Game/Screen/PlayerMissLeft.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvAppearWait` | `PlayerMissLeft::exeAppearWait` |  |
| `HostTypeNrvAppear` | `PlayerMissLeft::exeAppear` |  |
| `HostTypeNrvWait` | `PlayerMissLeft::exeWait` |  |
| `HostTypeNrvEnd` | `PlayerMissLeft::exeEnd` |  |

## Poihana

src/Game/Enemy/Poihana.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoihanaNrvNonActive` | `Poihana::exeNonActive` | `Poihana::exeNonActive` |
| `PoihanaNrvWait` | `Poihana::exeWait` |  |
| `PoihanaNrvWalkAround` | `Poihana::exeWalkAround` |  |
| `PoihanaNrvSleepStart` | `Poihana::exeSleepStart` |  |
| `PoihanaNrvSleep` | `Poihana::exeSleep` |  |
| `PoihanaNrvGetUp` | `Poihana::exeGetUp` |  |
| `PoihanaNrvSearch` | `Poihana::exeSearch` |  |
| `PoihanaNrvChasePlayer` | `Poihana::exeChasePlayer` |  |
| `PoihanaNrvShootUpCharge` | `Poihana::exeShootUpCharge` |  |
| `PoihanaNrvShootUp` | `Poihana::exeShootUp` | `Poihana::exeShootUp` |
| `PoihanaNrvGoBack` | `Poihana::exeGoBack` |  |
| `PoihanaNrvShock` | `Poihana::exeShock` |  |
| `PoihanaNrvSwoon` | `Poihana::exeSwoon` |  |
| `PoihanaNrvSwoonLand` | `Poihana::exeSwoonLand` |  |
| `PoihanaNrvRecover` | `Poihana::exeRecover` |  |
| `PoihanaNrvShake` | `Poihana::exeShake` |  |
| `PoihanaNrvDrown` | `Poihana::exeDrown` |  |
| `PoihanaNrvHide` | `Poihana::exeHide` |  |
| `PoihanaNrvAppear` | `Poihana::exeAppear` |  |
| `PoihanaNrvPointerSwoon` | `Poihana::exePointerSwoon` | `Poihana::exePointerSwoon` |

## Pole

src/Game/Ride/Pole.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoleNrvDemoAppear` | `Pole::exeDemoAppear` |  |
| `PoleNrvFree` | `Pole::exeFree` |  |
| `PoleNrvFreeInvalid` | `Pole::exeFreeInvalid` |  |
| `PoleNrvBindStart` | `Pole::exeBindStart` |  |
| `PoleNrvBindStartFast` | `Pole::exeBindStart` |  |
| `PoleNrvBindWait` | `Pole::exeBindWait` |  |
| `PoleNrvBindTurnLeft` | `Pole::exeBindTurn` |  |
| `PoleNrvBindTurnRight` | `Pole::exeBindTurn` |  |
| `PoleNrvBindTurnEnd` | `Pole::exeBindWait` |  |
| `PoleNrvBindClimbUp` | `Pole::exeBindClimbUp` |  |
| `PoleNrvBindFallDown` | `Pole::exeBindFallDown` |  |
| `PoleNrvBindHandstandStart` | `Pole::exeBindHandstandStart` |  |
| `PoleNrvBindHandstandWait` | `Pole::exeBindHandstandWait` |  |
| `PoleNrvBindHandstandEnd` | `Pole::exeBindHandstandEnd` |  |
| `PoleNrvBindHandstandTurn` | `Pole::exeBindHandstandTurn` |  |

## PoltaActionSequencer

src/Game/Boss/PoltaActionSequencer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaActionSequencerNrvWaitStart` | `PoltaActionSequencer::exeWaitStart` |  |
| `PoltaActionSequencerNrvOpeningDemo` | `PoltaActionSequencer::exeOpeningDemo` |  |
| `PoltaActionSequencerNrvBattleLv1` | `PoltaActionSequencer::exeBattleLv1` |  |
| `PoltaActionSequencerNrvPowerUpDemo` | `PoltaActionSequencer::exePowerUpDemo` |  |
| `PoltaActionSequencerNrvBattleLv2` | `PoltaActionSequencer::exeBattleLv2` |  |
| `PoltaActionSequencerNrvDownDemo` | `PoltaActionSequencer::exeDownDemo` |  |

## PoltaArm

src/Game/Boss/PoltaArm.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaArmNrvControlled` | `PoltaArm::exeControlled` |  |
| `PoltaArmNrvDamage` | `PoltaArm::exeDamage` |  |
| `PoltaArmNrvBreak` | `PoltaArm::exeBreak` |  |
| `PoltaArmNrvBreakBody` | `PoltaArm::exeBreakBody` |  |
| `PoltaArmNrvBroken` | `PoltaArm::exeBroken` |  |
| `PoltaArmNrvRepair` | `PoltaArm::exeRepair` | `PoltaArm::exeRepair` |
| `PoltaArmNrvWaitDamageEnd` | `PoltaArm::exeWaitDamageEnd` |  |
| `PoltaArmNrvWaitRepairEnd` | `PoltaArm::exeWaitRepairEnd` |  |

## PoltaBattleLv1

src/Game/Boss/PoltaBattleLv1.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaBattleLv1NrvWait` | `PoltaBattleLv1::exeWait` |  |
| `PoltaBattleLv1NrvGenerateGroundRock` | `PoltaBattleLv1::exeGenerateGroundRock` |  |
| `PoltaBattleLv1NrvGenerateRock` | `PoltaBattleLv1::exeGenerateRock` |  |
| `PoltaBattleLv1NrvDamageBody` | `PoltaBattleLv1::exeDamageBody` |  |
| `PoltaBattleLv1NrvBreakBody` | `PoltaBattleLv1::exeBreakBody` |  |
| `PoltaBattleLv1NrvStagger` | `PoltaBattleLv1::exeStagger` | `PoltaBattleLv1::exeStagger` |
| `PoltaBattleLv1NrvDamageCore` | `PoltaBattleLv1::exeDamageCore` |  |

## PoltaBattleLv2

src/Game/Boss/PoltaBattleLv2.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaBattleLv2NrvWait` | `PoltaBattleLv2::exeWait` |  |
| `PoltaBattleLv2NrvPunch` | `PoltaBattleLv2::exePunch` |  |
| `PoltaBattleLv2NrvAttackGround` | `PoltaBattleLv2::exeAttackGround` |  |
| `PoltaBattleLv2NrvGenerateGroundRock` | `PoltaBattleLv2::exeGenerateGroundRock` |  |
| `PoltaBattleLv2NrvGenerateRock` | `PoltaBattleLv2::exeGenerateRock` |  |
| `PoltaBattleLv2NrvDamageBody` | `PoltaBattleLv2::exeDamageBody` |  |
| `PoltaBattleLv2NrvBreakBody` | `PoltaBattleLv2::exeBreakBody` |  |
| `PoltaBattleLv2NrvStagger` | `PoltaBattleLv2::exeStagger` | `PoltaBattleLv2::exeStagger` |
| `PoltaBattleLv2NrvDamageCore` | `PoltaBattleLv2::exeDamageCore` |  |

## PoltaDemo

src/Game/Boss/PoltaDemo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaDemoNrvTryStartDemo` | `PoltaDemo::exeTryStartDemo` |  |
| `PoltaDemoNrvOpeningDemo` | `PoltaDemo::exeOpeningDemo` |  |
| `PoltaDemoNrvPowerUpDemo` | `PoltaDemo::exePowerUpDemo` |  |
| `PoltaDemoNrvDownDemo` | `PoltaDemo::exeDownDemo` |  |

## PoltaGroundRock

src/Game/Boss/PoltaGroundRock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaGroundRockNrvSign` | `PoltaGroundRock::exeSign` |  |
| `PoltaGroundRockNrvJut` | `PoltaGroundRock::exeJut` |  |
| `PoltaGroundRockNrvWait` | `PoltaGroundRock::exeWait` |  |
| `PoltaGroundRockNrvBreak` | `PoltaGroundRock::exeBreak` |  |

## PoltaRock

src/Game/Boss/PoltaRock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaRockNrvFloat` | `PoltaRock::exeFloat` |  |
| `PoltaRockNrvSign` | `PoltaRock::exeSign` |  |
| `PoltaRockNrvMove` | `PoltaRock::exeMove` |  |

## PoltaStateAttackGround

src/Game/Boss/PoltaStateAttackGround.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaStateAttackGroundNrvStart` | `PoltaStateAttackGround::exeStart` |  |
| `PoltaStateAttackGroundNrvAttack` | `PoltaStateAttackGround::exeAttack` |  |
| `PoltaStateAttackGroundNrvToWait` | `PoltaStateAttackGround::exeToWait` |  |

## PoltaStateGenerateRock

src/Game/Boss/PoltaStateGenerateRock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaStateGenerateRockNrvSign` | `PoltaStateGenerateRock::exeSign` |  |
| `PoltaStateGenerateRockNrvGenerate` | `PoltaStateGenerateRock::exeGenerate` |  |
| `PoltaStateGenerateRockNrvEnd` | `PoltaStateGenerateRock::exeEnd` |  |

## PoltaStateGroundRockAttack

src/Game/Boss/PoltaStateGroundRockAttack.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaStateGroundRockAttackNrvSign` | `PoltaStateGroundRockAttack::exeSign` |  |
| `PoltaStateGroundRockAttackNrvGenerate` | `PoltaStateGroundRockAttack::exeGenerate` |  |
| `PoltaStateGroundRockAttackNrvEnd` | `PoltaStateGroundRockAttack::exeEnd` |  |

## PoltaStatePunch

src/Game/Boss/PoltaStatePunch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaStatePunchNrvStart` | `PoltaStatePunch::exeStart` |  |
| `PoltaStatePunchNrvAttack` | `PoltaStatePunch::exeAttack` |  |
| `PoltaStatePunchNrvToWait` | `PoltaStatePunch::exeToWait` |  |

## PoltaStateStagger

src/Game/Boss/PoltaStateStagger.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PoltaStateStaggerNrvWait` | `PoltaStateStagger::exeWait` |  |

## PomponPlant

src/Game/MapObj/PomponPlant.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `PomponPlant::exeWait` |  |
| `HostTypeSwingPointer` | `PomponPlant::exeSwingPointer` |  |
| `HostTypeSwingPlayer` | `PomponPlant::exeSwingPlayer` |  |

## PowerStar

src/Game/MapObj/PowerStar.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PowerStarNrvWaitStartAppear` | `PowerStar::exeWaitStartAppear` |  |
| `PowerStarNrvAppearDemoRise` | `PowerStar::exeAppearDemoRise` |  |
| `PowerStarNrvAppearDemoMove` | `PowerStar::exeAppearDemoMove` |  |
| `PowerStarNrvAppearDemoKoopa` | `PowerStar::exeAppearDemoKoopa` |  |
| `PowerStarNrvWait` | `PowerStar::exeWait` |  |
| `PowerStarNrvWeak` | `PowerStar::exeWeak` |  |
| `PowerStarNrvWeakNoRotate` | `PowerStar::exeWeakNoRotate` |  |
| `PowerStarNrvWeakToWait` | `PowerStar::exeWeakToWait` |  |
| `PowerStarNrvStageClearDemo` | `PowerStar::exeStageClearDemo` |  |

## PowerStarHalo

src/Game/Map/Halo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWaitScenarioOpeningEnd` | `PowerStarHalo::exeWaitScenarioOpeningEnd` |  |

## PowerStarList

src/Game/Screen/PowerStarList.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PowerStarListNrvAppear` | `PowerStarList::exeAppear` |  |
| `PowerStarListNrvWait` | `PowerStarList::exeWait` |  |
| `PowerStarListNrvDisappear` | `PowerStarList::exeDisappear` |  |
| `PowerStarListNrvPageNext` | `PowerStarList::exePageNext` |  |
| `PowerStarListNrvPagePrev` | `PowerStarList::exePagePrev` |  |

## PressureBase

src/Game/MapObj/PressureBase.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PressureBaseNrvRelaxStart` | `PressureBase::exeBound` |  |
| `PressureBaseNrvWaitStart` | `PressureBase::exeBound` |  |
| `PressureBaseNrvRelax` | `PressureBase::exeRelax` |  |
| `PressureBaseNrvSyncWait` | `PressureBase::exeSyncWait` |  |
| `PressureBaseNrvFirstWait` | `PressureBase::exeFirstWait` |  |
| `PressureBaseNrvWait` | `PressureBase::exeWait` |  |
| `PressureBaseNrvPrepareToShot` | `PressureBase::exePrepareToShot` |  |
| `PressureBaseNrvShot` | `PressureBase::exeShot` |  |

## PressureMessenger

src/Game/MapObj/PressureBase.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PressureMessengerNrvSync` | `PressureMessenger::exeSync` |  |

## PrizeRing

src/Game/MapObj/PrizeRing.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PrizeRingStart` | `PrizeRing::exeStart` |  |
| `PrizeRingLoop` | `PrizeRing::exeLoop` |  |
| `PrizeRingTimeout` | `PrizeRing::exeTimeout` |  |
| `PrizeRingReadyToPass` | `PrizeRing::exeReadyToPass` |  |
| `PrizeRingPass` | `PrizeRing::exePass` |  |
| `PrizeRingReadyToKill` | `PrizeRing::exeReadyToKill` |  |

## PrologueDirector

src/Game/Demo/PrologueDirector.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PrologueDirectorNrvWait` | `PrologueDirector::exeWait` |  |
| `PrologueDirectorNrvPictureBook` | `PrologueDirector::exePictureBook` |  |
| `PrologueDirectorNrvPeachLetterStart` | `PrologueDirector::exePeachLetterStart` |  |
| `PrologueDirectorNrvPeachLetter` | `PrologueDirector::exePeachLetter` |  |
| `PrologueDirectorNrvPeachLetterWait` | `PrologueDirector::exePeachLetterWait` |  |
| `PrologueDirectorNrvPeachLetterEnd` | `PrologueDirector::exePeachLetterEnd` |  |
| `PrologueDirectorNrvBindWait` | `PrologueDirector::exeBindWait` |  |
| `PrologueDirectorNrvArrive` | `PrologueDirector::exeArrive` |  |
| `PrologueDirectorNrvGameStart` | `PrologueDirector::exeGameStart` |  |

## PrologueLetter

src/Game/Screen/PrologueLetter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PrologueLetterNrvAppear` | `PrologueLetter::exeAppear` |  |
| `PrologueLetterNrvWait` | `PrologueLetter::exeWait` |  |
| `PrologueLetterNrvDisappear` | `PrologueLetter::exeDisappear` |  |

## ProloguePictureBook

src/Game/Screen/ProloguePictureBook.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ProloguePictureBookActive` | `ProloguePictureBook::exeActive` |  |
| `ProloguePictureBookPlaying` | `ProloguePictureBook::exePlaying` |  |
| `ProloguePictureBookKeyWait` | `ProloguePictureBook::exeKeyWait` |  |
| `ProloguePictureBookEnd` | `ProloguePictureBook::exeEnd` |  |

## PTimerSwitch

src/Game/MapObj/PTimerSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PTimerSwitchNrvOff` | `PTimerSwitch::exeOff` |  |
| `PTimerSwitchNrvSwitchDown` | `PTimerSwitch::exeSwitchDown` |  |
| `PTimerSwitchNrvOn` | `PTimerSwitch::exeOn` |  |

## Pukupuku

src/Game/Enemy/Pukupuku.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PukupukuWait` | `Pukupuku::exeWait` |  |
| `PukupukuMoveWater` | `Pukupuku::exeMoveWater` |  |
| `PukupukuMoveWaterAfterJump` | `Pukupuku::exeMoveWaterAfterJump` |  |
| `PukupukuMoveWaterAfterJumpAfterPointing` | `Pukupuku::exeMoveWaterAfterJumpAfterPointing` |  |
| `PukupukuLanding` | `Pukupuku::exeLanding` |  |
| `PukupukuTrampled` | `Pukupuku::exeTrampled` |  |
| `PukupukuBlownOff` | `Pukupuku::exeBlownOff` |  |
| `PukupukuBindStarPointer` | `Pukupuku::exeBindStarPointer` |  |

## PukupukuStateLanding

src/Game/Enemy/Pukupuku.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PukupukuStateLandingLandingMoveLand` | `PukupukuStateLanding::exeLandingMoveLand` |  |
| `PukupukuStateLandingLandingJumpFromWater` | `PukupukuStateLanding::exeLandingJumpFromWater` |  |
| `PukupukuStateLandingLandingJumpFromLand` | `PukupukuStateLanding::exeLandingJumpFromLand` |  |

## PunchBox

src/Game/MapObj/PunchBox.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PunchBoxNrvWait` | `PunchBox::exeWait` |  |
| `PunchBoxNrvHit` | `PunchBox::exeHit` |  |

## PunchingKinoko

src/Game/MapObj/PunchingKinoko.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PunchingKinokoNrvWait` | `PunchingKinoko::exeWait` |  |
| `PunchingKinokoNrvSwing` | `PunchingKinoko::exeSwing` |  |
| `PunchingKinokoNrvPunched` | `PunchingKinoko::exePunched` |  |
| `PunchingKinokoNrvPunchedBrake` | `PunchingKinoko::exePunchedBrake` |  |
| `PunchingKinokoNrvHitted` | `PunchingKinoko::exeHitted` |  |
| `PunchingKinokoNrvPointSnaped` | `PunchingKinoko::exePointSnaped` |  |
| `PunchingKinokoNrvCrushed` | `PunchingKinoko::exeCrushed` |  |
| `PunchingKinokoNrvCrushedEnd` | `PunchingKinoko::exeCrushedEnd` |  |

## PurpleCoinCounter

src/Game/Screen/PurpleCoinCounter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `PurpleCoinCounterNrvAppear` | `PurpleCoinCounter::exeAppear` |  |
| `PurpleCoinCounterNrvWait` | `PurpleCoinCounter::exeWait` |  |

## QuakeEffectGenerator

src/Game/Map/QuakeEffectGenerator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `QuakeEffectGenerator::exeWait` |  |
| `HostTypeWaitInArea` | `QuakeEffectGenerator::exeWaitInArea` |  |
| `HostTypeQuaking` | `QuakeEffectGenerator::exeQuaking` |  |

## QuarterRollGravityRoomArrow

src/Game/MapObj/QuarterRollGravityRoomArrow.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `QuarterRollGravityRoomArrowNrvWait` | `QuarterRollGravityRoomArrow::exeWait` |  |
| `QuarterRollGravityRoomArrowNrvRollOnA` | `QuarterRollGravityRoomArrow::exeRollOnA` |  |
| `QuarterRollGravityRoomArrowNrvRollOffA` | `QuarterRollGravityRoomArrow::exeRollOffA` |  |
| `QuarterRollGravityRoomArrowNrvRollOnB` | `QuarterRollGravityRoomArrow::exeRollOnB` |  |
| `QuarterRollGravityRoomArrowNrvRollOffB` | `QuarterRollGravityRoomArrow::exeRollOffB` |  |

## QuestionBoxGalleryObj

src/Game/MapObj/QuestionBoxGalleryObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `QuestionBoxGalleryObjNrvOnStart` | `QuestionBoxGalleryObj::exeOnStart` |  |
| `QuestionBoxGalleryObjNrvOnWait` | `QuestionBoxGalleryObj::exeOnWait` |  |
| `QuestionBoxGalleryObjNrvOffStart` | `QuestionBoxGalleryObj::exeOffStart` |  |
| `QuestionBoxGalleryObjNrvOffWait` | `QuestionBoxGalleryObj::exeOffWait` |  |

## QuestionCoin

src/Game/MapObj/QuestionCoin.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `QuestionCoin::exeWait` |  |
| `HostTypeThrowActor` | `QuestionCoin::exeThrowActor` |  |
| `HostTypeCaught` | `QuestionCoin::exeCaught` |  |

## Rabbit

src/Game/NPC/Rabbit.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RabbitNrvAppear` | `Rabbit::exeAppear` |  |
| `RabbitNrvAppearLand` | `Rabbit::exeAppearLand` |  |
| `RabbitNrvWait` | `Rabbit::exeWait` |  |
| `RabbitNrvForwardLand` | `Rabbit::exeForwardLand` |  |
| `RabbitNrvPreJump` | `Rabbit::exePreJump` |  |
| `RabbitNrvMove` | `Rabbit::exeMove` |  |
| `RabbitNrvGoal` | `Rabbit::exeGoal` |  |
| `RabbitNrvFinish` | `Rabbit::exeFinish` |  |
| `RabbitNrvReaction` | `Rabbit::exeReaction` |  |
| `RabbitNrvTalk` | `Rabbit::exeTalk` |  |
| `RabbitNrvJumpV` | `Rabbit::exeJumpV` |  |
| `RabbitNrvJumpH` | `Rabbit::exeJumpH` |  |
| `RabbitNrvBackwardLand` | `Rabbit::exeBackwardLand` |  |
| `RabbitNrvPreJumpBack` | `Rabbit::exePreJumpBack` |  |
| `RabbitNrvNear` | `Rabbit::exeNear` |  |

## RabbitStateCaught

src/Game/NPC/RabbitStateCaught.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RabbitStateCaughtNrvTryStartDemo` | `RabbitStateCaught::exeTryStartDemo` |  |
| `RabbitStateCaughtNrvCaught` | `RabbitStateCaught::exeCaught` |  |
| `RabbitStateCaughtNrvCaughtLand` | `RabbitStateCaught::exeCaughtLand` |  |
| `RabbitStateCaughtNrvCaughtEvent` | `RabbitStateCaught::exeCaughtEvent` |  |

## RabbitStateWaitStart

src/Game/NPC/RabbitStateWaitStart.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RabbitStateWaitStartNrvWait` | `RabbitStateWaitStart::exeWait` |  |
| `RabbitStateWaitStartNrvSmallTurn` | `RabbitStateWaitStart::exeSmallTurn` |  |
| `RabbitStateWaitStartNrvHitReaction` | `RabbitStateWaitStart::exeHitReaction` |  |
| `RabbitStateWaitStartNrvTrample` | `RabbitStateWaitStart::exeTrample` |  |
| `RabbitStateWaitStartNrvSpin` | `RabbitStateWaitStart::exeSpin` |  |
| `RabbitStateWaitStartNrvPointing` | `RabbitStateWaitStart::exePointing` |  |
| `RabbitStateWaitStartNrvTalk` | `RabbitStateWaitStart::exeTalk` |  |

## RaceManager

src/Game/Map/RaceManager.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RaceManagerNrvWait` | `RaceManager::exeWait` |  |
| `RaceManagerNrvPrepWipe` | `RaceManager::exePrep` |  |
| `RaceManagerNrvPrepImme` | `RaceManager::exePrep` |  |
| `RaceManagerNrvPreWipeOut` | `RaceManager::exeWipeOut` |  |
| `RaceManagerNrvPreWipeWait` | `RaceManager::exeWipeWait` |  |
| `RaceManagerNrvPreWipeIn` | `RaceManager::exeWipeIn` |  |
| `RaceManagerNrvIntro` | `RaceManager::exeIntro` |  |
| `RaceManagerNrvCount` | `RaceManager::exeCount` |  |
| `RaceManagerNrvRace` | `RaceManager::exeRace` |  |
| `RaceManagerNrvDemo` | `RaceManager::exeDemo` |  |
| `RaceManagerNrvGoal` | `RaceManager::exeGoal` |  |
| `RaceManagerNrvRank` | `RaceManager::exeRank` |  |
| `RaceManagerNrvPstWipeOut` | `RaceManager::exeWipeOut` |  |
| `RaceManagerNrvPstWipeWait` | `RaceManager::exeWipeWait` |  |
| `RaceManagerNrvPstWipeIn` | `RaceManager::exeWipeIn` |  |

## RailBlock

src/Game/MapObj/RailBlock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RailBlockNrvWait` | `RailBlock::exeWait` |  |
| `RailBlockNrvMove` | `RailBlock::exeMove` |  |
| `RailBlockNrvTerminate` | `RailBlock::exeTerminate` |  |

## RailMoveObj

src/Game/MapObj/RailMoveObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `RailMoveObj::exeWait` |  |
| `HostTypeWaitForPlayerOn` | `RailMoveObj::exeWait` |  |
| `HostTypeMove` | `RailMoveObj::exeMove` |  |
| `HostTypeDone` | `RailMoveObj::exeWait` |  |

## RainCloud

src/Game/MapObj/RainCloud.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RainCloudNrvAppear` | `RainCloud::exeAppear` |  |
| `RainCloudNrvWait` | `RainCloud::exeWait` |  |
| `RainCloudNrvDisappear` | `RainCloud::exeDisappear` |  |
| `RainCloudNrvEnd` | `RainCloud::exeEnd` |  |
| `RainCloudNrvSoftTouch` | `RainCloud::exeSoftTouch` |  |
| `RainCloudNrvHardTouch` | `RainCloud::exeHardTouch` |  |

## ReverseGravityRoomPlanet

src/Game/MapObj/ReverseGravityRoomPlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ReverseGravityRoomPlanetNrvDownStart` | `ReverseGravityRoomPlanet::exeDownStart` |  |
| `ReverseGravityRoomPlanetNrvDownWait` | `ReverseGravityRoomPlanet::exeDownWait` |  |
| `ReverseGravityRoomPlanetNrvUpStart` | `ReverseGravityRoomPlanet::exeUpStart` |  |
| `ReverseGravityRoomPlanetNrvUpWait` | `ReverseGravityRoomPlanet::exeUpWait` |  |

## RevolvingWay

src/Game/MapObj/RevolvingWay.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RevolvingWayNrvWait` | `RevolvingWay::exeWait` |  |

## RingBeam

src/Game/Enemy/RingBeam.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RingBeamNrvSpread` | `RingBeam::exeSpread` |  |

## RingBeamer

src/Game/Enemy/RingBeamer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RingBeamerNrvWait` | `RingBeamer::exeWait` |  |
| `RingBeamerNrvAttack` | `RingBeamer::exeAttack` |  |
| `RingBeamerNrvInter` | `RingBeamer::exeInter` |  |

## Rock

src/Game/MapObj/Rock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RockNrvAppear` | `Rock::exeAppear` |  |
| `RockNrvAppearMoveInvalidBind` | `Rock::exeAppearMoveInvalidBind` |  |
| `RockNrvMove` | `Rock::exeMove` |  |
| `RockNrvMoveInvalidBind` | `Rock::exeMoveInvalidBind` |  |
| `RockNrvBreak` | `Rock::exeBreak` |  |
| `RockNrvFreeze` | `Rock::exeFreeze` |  |

## RockCreator

src/Game/MapObj/RockCreator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RockCreatorNrvActive` | `RockCreator::exeActive` |  |
| `RockCreatorNrvDeactive` | `RockCreator::exeDeactive` |  |

## Rosetta

src/Game/NPC/Rosetta.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RosettaNrvDemo` | `Rosetta::exeDemo` |  |
| `RosettaNrvReaction` | `Rosetta::exeReaction` |  |

## RosettaDemoAstroDomeExplain

src/Game/NPC/RosettaDemoAstroDome.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RosettaDemoAstroDomeExplainNrvExplainDemo` | `RosettaDemoAstroDomeExplain::exeDemo` |  |

## RosettaDemoAstroDomeFinalBattle

src/Game/NPC/RosettaDemoAstroDome.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RosettaDemoAstroDomeFinalBattleNrvFinalBattleDemo` | `RosettaDemoAstroDomeFinalBattle::exeDemo` |  |

## RosettaDemoAstroDomeTalk

src/Game/NPC/RosettaDemoAstroDome.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RosettaDemoAstroDomeTalkNrvTalkDemo` | `RosettaDemoAstroDomeTalk::exeDemo` |  |

## RosettaDemoEpilogue

src/Game/NPC/RosettaDemoEpilogue.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RosettaDemoEpilogueNrvDemo` | `RosettaDemoEpilogue::exeDemo` |  |

## RosettaDemoHeavensDoor1

src/Game/NPC/RosettaDemoHeavensDoor.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RosettaDemoHeavensDoor1NrvWait` | `RosettaDemoHeavensDoor1::exeWait` |  |
| `RosettaDemoHeavensDoor1NrvFade` | `RosettaDemoHeavensDoor1::exeFade` |  |
| `RosettaDemoHeavensDoor1NrvDemo` | `RosettaDemoHeavensDoor1::exeDemo` |  |

## RosettaDemoHeavensDoor2

src/Game/NPC/RosettaDemoHeavensDoor.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RosettaDemoHeavensDoor2NrvWait` | `RosettaDemoHeavensDoor2::exeWait` |  |
| `RosettaDemoHeavensDoor2NrvDemo` | `RosettaDemoHeavensDoor2::exeDemo` |  |

## RosettaPictureBook

src/Game/MapObj/RosettaPictureBook.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `RosettaPictureBook::exeWait` |  |
| `HostTypeNrvDemoWait` | `RosettaPictureBook::exeDemoWait` |  |
| `HostTypeNrvFadeOut` | `RosettaPictureBook::exeFadeOut` |  |
| `HostTypeNrvReading` | `RosettaPictureBook::exeReading` |  |
| `HostTypeNrvFadeIn` | `RosettaPictureBook::exeFadeIn` |  |

## RosettaReading

src/Game/NPC/RosettaReading.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RosettaReadingNrvPictureBookBefore` | `RosettaReading::exePictureBookBefore` |  |
| `RosettaReadingNrvPictureBook` | `RosettaReading::exePictureBook` |  |
| `RosettaReadingNrvPictureBookAfter` | `RosettaReading::exePictureBookAfter` |  |

## RotateMoveObj

src/Game/MapObj/RotateMoveObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWaitForPlayerOn` | `RotateMoveObj::exeWaitForPlayerOn` |  |
| `HostTypeWait` | `RotateMoveObj::exeWait` |  |
| `HostTypeMove` | `RotateMoveObj::exeMove` |  |
| `HostTypeStop` | `RotateMoveObj::exeStop` |  |

## RunawayRabbit

src/Game/NPC/RunawayRabbit.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RunawayRabbitNrvNoActive` | `RunawayRabbit::exeNoActive` |  |
| `RunawayRabbitNrvHide` | `RunawayRabbit::exeHide` |  |
| `RunawayRabbitNrvAppear` | `RunawayRabbit::exeAppear` |  |
| `RunawayRabbitNrvRunaway` | `RunawayRabbit::exeRunaway` |  |
| `RunawayRabbitNrvTryCaughtDemo` | `RunawayRabbit::exeTryCaughtDemo` |  |
| `RunawayRabbitNrvCaught` | `RunawayRabbit::exeCaught` |  |
| `RunawayRabbitNrvCaughtTalk` | `RunawayRabbit::exeCaughtTalk` |  |
| `RunawayRabbitNrvCaughtEnd` | `RunawayRabbit::exeCaughtEnd` |  |
| `RunawayRabbitNrvStop` | `RunawayRabbit::exeStop` |  |
| `RunawayRabbitNrvBlowDamage` | `RunawayRabbit::exeBlowDamage` |  |

## RunawayRabbitCollect

src/Game/NPC/RunawayRabbitCollect.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RunawayRabbitCollectNrvWait` | `RunawayRabbitCollect::exeWait` |  |
| `RunawayRabbitCollectNrvActive` | `RunawayRabbitCollect::exeActive` |  |

## RunawayTico

src/Game/NPC/RunawayTico.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `RunawayTicoNrvGuide0` | `RunawayTico::exeGuide0` |  |
| `RunawayTicoNrvGuide1` | `RunawayTico::exeGuide1` |  |
| `RunawayTicoNrvWhiteIn` | `RunawayTico::exeWhiteIn` |  |
| `RunawayTicoNrvWhiteOut` | `RunawayTico::exeWhiteOut` |  |
| `RunawayTicoNrvWait` | `RunawayTico::exeWait` |  |
| `RunawayTicoNrvAppear` | `RunawayTico::exeAppear` |  |
| `RunawayTicoNrvTalk` | `RunawayTico::exeTalk` |  |

## SamboHead

src/Game/Enemy/SamboHead.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWaitUnderGround` | `SamboHead::exeWaitUnderGround` |  |
| `HostTypeNrvHide` | `SamboHead::exeHide` |  |
| `HostTypeNrvAppear` | `SamboHead::exeAppear` |  |
| `HostTypeNrvAppearImmediately` | `SamboHead::exeAppear` |  |
| `HostTypeNrvWait` | `SamboHead::exeWait` |  |
| `HostTypeNrvPrePursue` | `SamboHead::exePrePursue` |  |
| `HostTypeNrvPursue` | `SamboHead::exePursue` |  |
| `HostTypeNrvPursueEnd` | `SamboHead::exePursueEnd` |  |
| `HostTypeNrvHitToPlayer` | `SamboHead::exeHitToPlayer` |  |
| `HostTypeNrvStarPieceHit` | `SamboHead::exeStarPieceHit` |  |
| `HostTypeNrvStampFall` | `SamboHead::exeStampFall` | `SamboHead::exeStampFall` |
| `HostTypeNrvStampDeath` | `SamboHead::exeStampDeath` | `SamboHead::exeStampDeath` |
| `HostTypeNrvHitBlow` | `SamboHead::exeHitBlow` | `SamboHead::exeHitBlow` |
| `HostTypeNrvBuryDeath` | `SamboHead::exeBuryDeath` |  |
| `HostTypeNrvBindStarPointer` | `SamboHead::exeBindStarPointer` | `SamboHead::exeBindStarPointer` |
| `HostTypeNrvBindStarPointerEnd` | `SamboHead::exeBindStarPointerEnd` |  |

## SandCapsuleInsidePlanet

src/Game/MapObj/SandCapsuleInsidePlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SandCapsuleInsidePlanetNrvWait` | `SandCapsuleInsidePlanet::exeWait` |  |

## SandCapsulePressGround

src/Game/MapObj/SandCapsulePressGround.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SandCapsulePressGroundNrvWaitBottom` | `SandCapsulePressGround::exeWait` |  |
| `SandCapsulePressGroundNrvWaitTop` | `SandCapsulePressGround::exeWait` |  |
| `SandCapsulePressGroundNrvMoveStartUp` | `SandCapsulePressGround::exeMoveStart` |  |
| `SandCapsulePressGroundNrvMoveStartDown` | `SandCapsulePressGround::exeMoveStart` |  |
| `SandCapsulePressGroundNrvMoveUp` | `SandCapsulePressGround::exeMove` |  |
| `SandCapsulePressGroundNrvMoveDown` | `SandCapsulePressGround::exeMove` |  |
| `SandCapsulePressGroundNrvMoveUpEnd` | `SandCapsulePressGround::exeMoveEnd` |  |
| `SandCapsulePressGroundNrvMoveDownEnd` | `SandCapsulePressGround::exeMoveEnd` |  |
| `SandCapsulePressGroundNrvDisappearDemo` | `SandCapsulePressGround::exeDisappearDemo` |  |
| `SandCapsulePressGroundNrvStop` | `SandCapsulePressGround::exeStop` |  |

## Sandstorm

src/Game/MapObj/Sandstorm.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SandstormNrvNonActive` | `Sandstorm::exeNonActive` |  |
| `SandstormNrvWait` | `Sandstorm::exeWait` |  |
| `SandstormNrvStorm` | `Sandstorm::exeStorm` |  |
| `SandstormNrvStormAdjust` | `Sandstorm::exeStormAdjust` |  |
| `SandstormNrvStormFix` | `Sandstorm::exeStormFix` |  |
| `SandstormNrvStormEnd` | `Sandstorm::exeStormEnd` |  |

## SandUpDownEffectObj

src/Game/MapObj/SandUpDownEffectObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SandUpDownEffectObjNrvWait` | `SandUpDownEffectObj::exeWait` |  |
| `SandUpDownEffectObjNrvFallingRock` | `SandUpDownEffectObj::exeFallingRock` |  |
| `SandUpDownEffectObjNrvFallingRockAndSmoke` | `SandUpDownEffectObj::exeFallingRockAndSmoke` |  |
| `SandUpDownEffectObjNrvBurySmokePrepare` | `SandUpDownEffectObj::exeBurySmokePrepare` |  |
| `SandUpDownEffectObjNrvBurySmoke` | `SandUpDownEffectObj::exeBurySmoke` |  |
| `SandUpDownEffectObjNrvBuryEnd` | `SandUpDownEffectObj::exeBuryEnd` |  |

## SandUpDownTriRock

src/Game/MapObj/SandUpDownTriRock.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SandUpDownTriRockNrvWait` | `SandUpDownTriRock::exeWait` |  |
| `SandUpDownTriRockNrvDown` | `SandUpDownTriRock::exeDown` |  |
| `SandUpDownTriRockNrvRest` | `SandUpDownTriRock::exeRest` |  |
| `SandUpDownTriRockNrvUp` | `SandUpDownTriRock::exeUp` |  |
| `SandUpDownTriRockNrvStop` | `SandUpDownTriRock::exeStop` |  |

## SaveDataHandleSequence

src/Game/System/SaveDataHandleSequence.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SaveDataHandleSequenceNoOperation` | `SaveDataHandleSequence::exeNoOperation` |  |
| `SaveDataHandleSequenceSaveConfirm` | `SaveDataHandleSequence::exeSaveConfirm` |  |
| `SaveDataHandleSequenceSave` | `SaveDataHandleSequence::exeSave` |  |
| `SaveDataHandleSequenceSaveWindowDisappear` | `SaveDataHandleSequence::exeSaveWindowDisappear` |  |
| `SaveDataHandleSequenceSaveDoneKeyWait` | `SaveDataHandleSequence::exeSaveDoneKeyWait` |  |
| `SaveDataHandleSequenceSaveAllWithoutKeyWait` | `SaveDataHandleSequence::exeSaveAllWithoutKeyWait` |  |
| `SaveDataHandleSequenceSaveAllWithoutKeyWaitDisappear` | `SaveDataHandleSequence::exeSaveAllWithoutKeyWaitDisappear` |  |
| `SaveDataHandleSequenceSaveAllWithoutWindow` | `SaveDataHandleSequence::exeSaveAllWithoutWindow` |  |
| `SaveDataHandleSequenceQuietSave` | `SaveDataHandleSequence::exeQuietSave` |  |
| `SaveDataHandleSequencePreLoad` | `SaveDataHandleSequence::exePreLoad` |  |
| `SaveDataHandleSequencePreLoadDone` | `SaveDataHandleSequence::exePreLoadDone` |  |
| `SaveDataHandleSequenceNoSaveConfirmRemind` | `SaveDataHandleSequence::exeNoSaveConfirmRemind` |  |
| `SaveDataHandleSequenceErrorHandling` | `SaveDataHandleSequence::exeErrorHandling` |  |

## SaveErrorSequence

src/Game/System/SaveErrorSequence.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SaveErrorSequenceIdle` | `SaveErrorSequence::exeIdle` |  |
| `SaveErrorSequenceError` | `SaveErrorSequence::exeError` |  |
| `SaveErrorSequenceDamagedConfirm` | `SaveErrorSequence::exeDamagedConfirm` |  |
| `SaveErrorSequenceNoSaveConfirm` | `SaveErrorSequence::exeNoSaveConfirm` |  |

## ScaleController

src/Game/Map/FileSelectItem.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ScaleControllerNrvToSmall` | `ScaleController::exeToSmall` |  |
| `ScaleControllerNrvSmall` | `ScaleController::exeSmall` |  |
| `ScaleControllerNrvToBig` | `ScaleController::exeToBig` |  |
| `ScaleControllerNrvBig` | `ScaleController::exeBig` |  |

## ScenarioSelectLayout

src/Game/Screen/ScenarioSelectLayout.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ScenarioSelectLayoutNrvAppearStar` | `ScenarioSelectLayout::exeAppearStar` |  |
| `ScenarioSelectLayoutNrvAppear` | `ScenarioSelectLayout::exeAppear` |  |
| `ScenarioSelectLayoutNrvWaitScenarioSelect` | `ScenarioSelectLayout::exeWaitScenarioSelect` |  |
| `ScenarioSelectLayoutNrvDecide` | `ScenarioSelectLayout::exeDecide` |  |
| `ScenarioSelectLayoutNrvAfterScenarioSelected` | `ScenarioSelectLayout::exeAfterScenarioSelected` |  |
| `ScenarioSelectLayoutNrvDisappear` | `ScenarioSelectLayout::exeDisappear` |  |
| `ScenarioSelectLayoutNrvCancel` | `ScenarioSelectLayout::exeCancel` |  |
| `ScenarioSelectLayoutNrvCancelFadeOut` | `ScenarioSelectLayout::exeCancelFadeOut` |  |
| `ScenarioSelectLayoutNrvAppearCometWarning` | `ScenarioSelectLayout::exeAppearCometWarning` |  |
| `ScenarioSelectLayoutNrvWaitCometWarning` | `ScenarioSelectLayout::exeWaitCometWarning` |  |
| `ScenarioSelectLayoutNrvDisappearCometWarning` | `ScenarioSelectLayout::exeDisappearCometWarning` |  |

## ScenarioSelectScene

src/Game/Scene/ScenarioSelectScene.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ScenarioSelectSceneNrvDeactive` | `ScenarioSelectScene::exeDeactive` |  |
| `ScenarioSelectSceneNrvInvalidScenarioSelect` | `ScenarioSelectScene::exeInvalidScenarioSelect` |  |
| `ScenarioSelectSceneNrvWaitStartScenarioSelect` | `ScenarioSelectScene::exeWaitStartScenarioSelect` |  |
| `ScenarioSelectSceneNrvStartScenarioSelect` | `ScenarioSelectScene::exeStartScenarioSelect` |  |
| `ScenarioSelectSceneNrvWaitScenarioSelect` | `ScenarioSelectScene::exeWaitScenarioSelect` |  |
| `ScenarioSelectSceneNrvWaitSceneLoad` | `ScenarioSelectScene::exeWaitSceneLoad` |  |
| `ScenarioSelectSceneNrvWaitInitializeEnd` | `ScenarioSelectScene::exeWaitInitializeEnd` |  |
| `ScenarioSelectSceneNrvWaitDisappearLayout` | `ScenarioSelectScene::exeWaitDisappearLayout` |  |
| `ScenarioSelectSceneNrvWaitSceneLoadIfRequestedReset` | `ScenarioSelectScene::exeWaitSceneLoadIfRequestedReset` |  |
| `ScenarioSelectSceneNrvWaitSceneLoadIfCanceledSelect` | `ScenarioSelectScene::exeWaitSceneLoadIfCanceledSelect` |  |

## ScenarioSelectStar

src/Game/Map/ScenarioSelectStar.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ScenarioSelectStarNrvAppear` | `ScenarioSelectStar::exeAppear` |  |
| `ScenarioSelectStarNrvNotPointing` | `ScenarioSelectStar::exeNotPointing` |  |
| `ScenarioSelectStarNrvPointing` | `ScenarioSelectStar::exePointing` |  |
| `ScenarioSelectStarNrvEndPointing` | `ScenarioSelectStar::exeEndPointing` |  |
| `ScenarioSelectStarNrvSelected` | `ScenarioSelectStar::exeSelected` |  |
| `ScenarioSelectStarNrvSelectedMove` | `ScenarioSelectStar::exeSelectedMove` |  |
| `ScenarioSelectStarNrvNotSelected` | `ScenarioSelectStar::exeNotSelected` |  |

## ScenarioStarter

src/Game/Demo/ScenarioStarter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ScenarioStarterWaitScenarioCameraEnd` | `ScenarioStarter::exeWaitScenarioCameraEnd` |  |
| `ScenarioStarterWaitToStart` | `ScenarioStarter::exeWaitToStart` |  |
| `ScenarioStarterCinemaFrameBlank` | `ScenarioStarter::exeCinemaFrameBlank` |  |
| `ScenarioStarterRailMove` | `ScenarioStarter::exeRailMove` |  |
| `ScenarioStarterRailMoveCanceled` | `ScenarioStarter::exeRailMoveCanceled` |  |
| `ScenarioStarterShowWelcomeLayout` | `ScenarioStarter::exeShowWelcomeLayout` |  |

## ScenarioTitle

src/Game/Screen/ScenarioTitle.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ScenarioTitleAppear` | `ScenarioTitle::exeAppear` |  |
| `ScenarioTitleWait` | `ScenarioTitle::exeWait` |  |
| `ScenarioTitleEnd` | `ScenarioTitle::exeEnd` |  |

## ScrewSwitch

src/Game/MapObj/ScrewSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ScrewSwitchNrvWait` | `ScrewSwitch::exeWait` |  |
| `ScrewSwitchNrvAdjust` | `ScrewSwitch::exeAdjust` |  |
| `ScrewSwitchNrvScrew` | `ScrewSwitch::exeScrew` |  |
| `ScrewSwitchNrvEnd` | `ScrewSwitch::exeEnd` |  |

## ScrewSwitchReverse

src/Game/MapObj/ScrewSwitchReverse.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ScrewSwitchReverseNrvWait` | `ScrewSwitchReverse::exeWait` |  |
| `ScrewSwitchReverseNrvAdjust` | `ScrewSwitchReverse::exeAdjust` |  |
| `ScrewSwitchReverseNrvScrew` | `ScrewSwitchReverse::exeScrew` |  |

## SeaBottomTriplePropeller

src/Game/MapObj/SeaBottomTriplePropeller.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SeaBottomTriplePropellerNrvWait` | `SeaBottomTriplePropeller::exeWait` |  |
| `SeaBottomTriplePropellerNrvBreak` | `SeaBottomTriplePropeller::exeBreak` |  |

## SeaGull

src/Game/Map/SeaGull.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SeaGullNrvHoverFront` | `SeaGull::exeHoverFront` |  |
| `SeaGullNrvHoverLeft` | `SeaGull::exeHoverLeft` |  |
| `SeaGullNrvHoverRight` | `SeaGull::exeHoverRight` |  |

## SearchBeamer

src/Game/Enemy/SearchBeamer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SearchBeamerNrvNonActive` | `SearchBeamer::exeNonActive` | `SearchBeamer::exeNonActive` |
| `SearchBeamerNrvCloseWaitFar` | `SearchBeamer::exeCloseWaitFar` |  |
| `SearchBeamerNrvCloseSearch` | `SearchBeamer::exeCloseSearch` |  |
| `SearchBeamerNrvCloseWaitNear` | `SearchBeamer::exeCloseWaitNear` |  |
| `SearchBeamerNrvOpenMouth` | `SearchBeamer::exeOpenMouth` |  |
| `SearchBeamerNrvBeamPrepare` | `SearchBeamer::exeBeamPrepare` |  |
| `SearchBeamerNrvBeamStart` | `SearchBeamer::exeBeamStart` |  |
| `SearchBeamerNrvBeamAim` | `SearchBeamer::exeBeamAim` |  |
| `SearchBeamerNrvCloseMouth` | `SearchBeamer::exeCloseMouth` |  |
| `SearchBeamerNrvStopStart` | `SearchBeamer::exeStopStart` |  |
| `SearchBeamerNrvStop` | `SearchBeamer::exeStop` |  |
| `SearchBeamerNrvStopForPlayerOff` | `SearchBeamer::exeStopForPlayerOff` |  |
| `SearchBeamerNrvRecover` | `SearchBeamer::exeRecover` |  |
| `SearchBeamerNrvPointerSwoon` | `SearchBeamer::exePointerSwoon` | `SearchBeamer::exePointerSwoon` |

## SeesawMoveNut

src/Game/MapObj/SeesawMoveNut.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SeesawMoveNutNrvHalfway` | `SeesawMoveNut::exeHalfway` |  |
| `SeesawMoveNutNrvEdge` | `SeesawMoveNut::exeEdge` |  |

## SelectAnimController

src/Game/Screen/FileSelectNumber.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SelectAnimControllerNrvSelectInStart` | `SelectAnimController::exeSelectInStart` |  |
| `SelectAnimControllerNrvSelectIn` | `SelectAnimController::exeSelectIn` |  |
| `SelectAnimControllerNrvSelectOutStart` | `SelectAnimController::exeSelectOutStart` |  |
| `SelectAnimControllerNrvSelectOut` | `SelectAnimController::exeSelectOut` |  |

## Shellfish

src/Game/MapObj/Shellfish.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ShellfishNrvWait` | `Shellfish::exeWait` |  |
| `ShellfishNrvOpen` | `Shellfish::exeOpen` |  |
| `ShellfishNrvOpenWait` | `Shellfish::exeOpenWait` |  |
| `ShellfishNrvCloseSignal` | `Shellfish::exeCloseSignal` |  |
| `ShellfishNrvClose` | `Shellfish::exeClose` |  |
| `ShellfishNrvShock` | `Shellfish::exeShock` |  |
| `ShellfishNrvPointerOpen` | `Shellfish::exePointerOpen` | `Shellfish::exePointerOpen` |

## ShockWaveGenerator

src/Game/MapObj/ShockWaveGenerator.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ShockWaveGeneratorNrvWait` | `ShockWaveGenerator::exeWait` |  |
| `ShockWaveGeneratorNrvDemoEcho` | `ShockWaveGenerator::exeDemoEcho` |  |
| `ShockWaveGeneratorNrvGenerate` | `ShockWaveGenerator::exeGenerate` |  |

## ShootingStar

src/Game/MapObj/ShootingStar.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvPreShooting` | `ShootingStar::exePreShooting` |  |
| `HostTypeNrvShooting` | `ShootingStar::exeShooting` |  |
| `HostTypeNrvWaitForNextShoot` | `ShootingStar::exeWaitForNextShoot` |  |

## SideSpikeMoveStep

src/Game/MapObj/SideSpikeMoveStep.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SideSpikeMoveStepNrvWait` | `SideSpikeMoveStep::exeWait` |  |
| `SideSpikeMoveStepNrvPointerStop` | `SideSpikeMoveStep::exePointerStop` | `SideSpikeMoveStep::exePointerStop` |
| `SideSpikeMoveStepNrvPointerStopSync` | `SideSpikeMoveStep::exePointerStopSync` | `SideSpikeMoveStep::exePointerStopSync` |

## SignBoard

src/Game/NPC/SignBoard.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SignBoardNrvWait` | `SignBoard::exeWait` |  |

## SimpleBreakableObj

src/Game/MapObj/SimpleBreakableObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `SimpleBreakableObj::exeWait` |  |
| `HostTypeTryStartDemo` | `SimpleBreakableObj::exeTryStartDemo` |  |
| `HostTypeDemo` | `SimpleBreakableObj::exeDemo` |  |

## SimpleEffectObj

src/Game/Effect/SimpleEffectObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `SimpleEffectObj::exeWait` |  |
| `HostTypeMove` | `SimpleEffectObj::exeMove` |  |

## SimpleFloaterObj

src/Game/MapObj/SimpleFloaterObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SimpleFloaterObjNrvWait` | `SimpleFloaterObj::exeWait` |  |

## SimpleTimerObj

src/Game/MapObj/SimpleTimerObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SimpleTimerObjNrvTimer` | `SimpleTimerObj::exeTimer` |  |

## SkeletalFishBaby

src/Game/Boss/SkeletalFishBaby.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SkeletalFishBabyNrvSwim` | `SkeletalFishBaby::exeSwim` |  |
| `SkeletalFishBabyNrvBind` | `SkeletalFishBaby::exeBind` | `SkeletalFishBaby::exeBind` |
| `SkeletalFishBabyNrvBreak` | `SkeletalFishBaby::exeBreak` |  |
| `SkeletalFishBabyNrvDead` | `SkeletalFishBaby::exeDead` |  |

## SkeletalFishBoss

src/Game/Boss/SkeletalFishBoss.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SkeletalFishBossNrvSwim` | `SkeletalFishBoss::exeSwim` |  |
| `SkeletalFishBossNrvOpen` | `SkeletalFishBoss::exeOpen` |  |
| `SkeletalFishBossNrvOpenWait` | `SkeletalFishBoss::exeOpenWait` |  |
| `SkeletalFishBossNrvClose` | `SkeletalFishBoss::exeClose` |  |
| `SkeletalFishBossNrvBite` | `SkeletalFishBoss::exeBite` |  |
| `SkeletalFishBossNrvDamage` | `SkeletalFishBoss::exeDamage` |  |
| `SkeletalFishBossNrvDown` | `SkeletalFishBoss::exeDown` |  |
| `SkeletalFishBossNrvDeadDamage` | `SkeletalFishBoss::exeDeadDamage` |  |
| `SkeletalFishBossNrvDead` | `SkeletalFishBoss::exeDead` |  |
| `SkeletalFishBossNrvAppearWait` | `SkeletalFishBoss::exeAppearWait` |  |
| `SkeletalFishBossNrvAppearDemo` | `SkeletalFishBoss::exeAppearDemo` |  |
| `SkeletalFishBossNrvPowerUpDemo` | `SkeletalFishBoss::exePowerUpDemo` |  |
| `SkeletalFishBossNrvDeadDemo` | `SkeletalFishBoss::exeDeadDemo` |  |
| `SkeletalFishBossNrvBreakDemo` | `SkeletalFishBoss::exeBreakDemo` |  |
| `SkeletalFishBossNrvDemoWait` | `SkeletalFishBoss::exeDemoWait` |  |

## SkeletalFishGuard

src/Game/Boss/SkeletalFishGuard.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SkeletalFishGuardNrvWait` | `SkeletalFishGuard::exeWait` |  |
| `SkeletalFishGuardNrvAppear` | `SkeletalFishGuard::exeAppear` |  |
| `SkeletalFishGuardNrvNormal` | `SkeletalFishGuard::exeNormal` |  |
| `SkeletalFishGuardNrvApart` | `SkeletalFishGuard::exeApart` |  |
| `SkeletalFishGuardNrvFollow` | `SkeletalFishGuard::exeFollow` |  |
| `SkeletalFishGuardNrvStraight` | `SkeletalFishGuard::exeStraight` |  |
| `SkeletalFishGuardNrvKill` | `SkeletalFishGuard::exeKill` |  |
| `SkeletalFishGuardNrvNumb` | `SkeletalFishGuard::exeNumb` | `SkeletalFishGuard::exeNumb` |

## Sky

src/Game/Map/Sky.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `Sky::exeWait` |  |
| `HostTypeChange` | `Sky::exeChange` |  |

## SlideState

src/Game/Screen/FileSelectInfo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SlideStateNrvNormalPos` | `SlideState::exeNormalPos` |  |
| `SlideStateNrvSliding` | `SlideState::exeSliding` |  |
| `SlideStateNrvSlidePos` | `SlideState::exeSlidePos` |  |
| `SlideStateNrvSlidingBack` | `SlideState::exeSlidingBack` |  |

## SlingShooter

src/Game/Ride/SlingShooter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SlingShooterNrvFree` | `SlingShooter::exeFree` |  |
| `SlingShooterNrvFreeInvalid` | `SlingShooter::exeFreeInvalid` |  |
| `SlingShooterNrvLand` | `SlingShooter::exeLand` |  |
| `SlingShooterNrvWait` | `SlingShooter::exeWait` |  |
| `SlingShooterNrvAim` | `SlingShooter::exeAim` |  |
| `SlingShooterNrvAttack` | `SlingShooter::exeAttack` |  |
| `SlingShooterNrvAttackSuccess` | `SlingShooter::exeAttackSuccess` |  |

## Snakehead

src/Game/Enemy/Snakehead.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SnakeheadNrvWaylay` | `Snakehead::exeWaylay` |  |
| `SnakeheadNrvWait` | `Snakehead::exeWait` |  |
| `SnakeheadNrvMoveForward` | `Snakehead::exeMoveForward` |  |
| `SnakeheadNrvRest` | `Snakehead::exeRest` |  |
| `SnakeheadNrvMoveBack` | `Snakehead::exeMoveBack` |  |
| `SnakeheadNrvTurtleDown` | `Snakehead::exeTurtleDown` |  |
| `SnakeheadNrvPointerSwoon` | `Snakehead::exePointerSwoon` | `Snakehead::exePointerSwoon` |

## SnowCapsulePlanet

src/Game/MapObj/SnowCapsulePlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SnowCapsulePlanetNrvCloseWait` | `SnowCapsulePlanet::exeCloseWait` |  |
| `SnowCapsulePlanetNrvOpenWait` | `SnowCapsulePlanet::exeOpenWait` |  |
| `SnowCapsulePlanetNrvOpen` | `SnowCapsulePlanet::exeOpen` |  |

## SnowMan

src/Game/MapObj/SnowMan.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SnowManNrvWait` | `SnowMan::exeWait` |  |
| `SnowManNrvWaitBody` | `SnowMan::exeWaitBody` |  |
| `SnowManNrvWaitHead` | `SnowMan::exeWaitHead` |  |
| `SnowManNrvMeltHead` | `SnowMan::exeMeltHead` |  |
| `SnowManNrvMeltBody` | `SnowMan::exeMeltBody` |  |
| `SnowManNrvDownHead` | `SnowMan::exeDownHead` |  |
| `SnowManNrvDownBody` | `SnowMan::exeDownBody` |  |

## SoundEmitter

src/Game/MapObj/SoundEmitter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SoundEmitterNrvStandBy` | `SoundEmitter::exeStandBy` |  |
| `SoundEmitterNrvPlaySound` | `SoundEmitter::exePlaySound` |  |

## SpaceCocoon

src/Game/Ride/SpaceCocoon.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SpaceCocoonNrvStop` | `SpaceCocoon::exeStop` |  |
| `SpaceCocoonNrvFree` | `SpaceCocoon::exeFree` |  |
| `SpaceCocoonNrvFreeInvalid` | `SpaceCocoon::exeFreeInvalid` |  |
| `SpaceCocoonNrvKinopioLand` | `SpaceCocoon::exeBindLand` |  |
| `SpaceCocoonNrvKinopioWait` | `SpaceCocoon::exeBindWait` |  |
| `SpaceCocoonNrvWaitKinopioAimDemo` | `SpaceCocoon::exeWaitKinopioAimDemo` |  |
| `SpaceCocoonNrvKinopioAim` | `SpaceCocoon::exeBindAim` |  |
| `SpaceCocoonNrvKinopioAttack` | `SpaceCocoon::exeBindAttack` |  |
| `SpaceCocoonNrvKinopioAttackSuccess` | `SpaceCocoon::exeBindAttackSuccess` |  |
| `SpaceCocoonNrvBindLand` | `SpaceCocoon::exeBindLand` |  |
| `SpaceCocoonNrvBindWait` | `SpaceCocoon::exeBindWait` |  |
| `SpaceCocoonNrvBindAim` | `SpaceCocoon::exeBindAim` |  |
| `SpaceCocoonNrvBindAttack` | `SpaceCocoon::exeBindAttack` |  |
| `SpaceCocoonNrvBindAttackSuccess` | `SpaceCocoon::exeBindAttackSuccess` |  |

## SpaceInner

src/Game/Map/SpaceInner.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeAppear` | `SpaceInner::exeAppear` |  |
| `HostTypeDisappear` | `SpaceInner::exeDisappear` |  |

## SpaceMine

src/Game/MapObj/SpaceMine.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `SpaceMine::exeWait` |  |
| `HostTypeAppear` | `SpaceMine::exeAppear` |  |

## SpaceShipStep

src/Game/MapObj/SpaceShipStep.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SpaceShipStepNrvWait` | `SpaceShipStep::exeWait` |  |
| `SpaceShipStepNrvMoveSign` | `SpaceShipStep::exeMoveSign` |  |
| `SpaceShipStepNrvMove` | `SpaceShipStep::exeMove` |  |
| `SpaceShipStepNrvBreak` | `SpaceShipStep::exeBreak` |  |

## SphereRailDash

src/Game/MapObj/SphereRailDash.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SphereRailDashNrvWait` | `SphereRailDash::exeWait` |  |
| `SphereRailDashNrvRailMove` | `SphereRailDash::exeRailMove` |  |

## SphereSelector

src/Game/Map/SphereSelector.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SphereSelectorNrvSelectStart` | `SphereSelector::exeSelectStart` |  |
| `SphereSelectorNrvSelectWait` | `SphereSelector::exeSelectWait` |  |
| `SphereSelectorNrvSelectCancel` | `SphereSelector::exeSelectCancel` |  |
| `SphereSelectorNrvConfirmStart` | `SphereSelector::exeConfirmStart` |  |
| `SphereSelectorNrvConfirmWait` | `SphereSelector::exeConfirmWait` |  |
| `SphereSelectorNrvConfirmCancel` | `SphereSelector::exeConfirmCancel` |  |
| `SphereSelectorNrvConfirmed` | `SphereSelector::exeConfirm` |  |

## SphereSelectorHandle

src/Game/Map/SphereSelectorHandle.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SphereSelectorHandleNrvWait` | `SphereSelectorHandle::exeWait` |  |
| `SphereSelectorHandleNrvHold` | `SphereSelectorHandle::exeHold` |  |
| `SphereSelectorHandleNrvSpin` | `SphereSelectorHandle::exeSpin` |  |
| `SphereSelectorHandleNrvDemoRotate` | `SphereSelectorHandle::exeDemoRotate` |  |
| `SphereSelectorHandleNrvDisappear` | `SphereSelectorHandle::exeDisappear` |  |
| `SphereSelectorHandleNrvInvalidRotate` | `SphereSelectorHandle::exeInvalidRotate` |  |
| `SphereSelectorHandleNrvGalaxyConfirmStart` | `SphereSelectorHandle::exeGalaxyConfirmStart` |  |
| `SphereSelectorHandleNrvGalaxyConfirmWait` | `SphereSelectorHandle::exeGalaxyConfirmWait` |  |
| `SphereSelectorHandleNrvGalaxyConfirmCancel` | `SphereSelectorHandle::exeGalaxyConfirmCancel` |  |
| `SphereSelectorHandleNrvIdleForFileSelect` | `SphereSelectorHandle::exeIdleForFileSelect` |  |
| `SphereSelectorHandleNrvIdleEndForFileSelect` | `SphereSelectorHandle::exeIdleEndForFileSelect` |  |

## SpiderCoin

src/Game/MapObj/SpiderCoin.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SpiderCoinNrvWait` | `SpiderCoin::exeWait` |  |
| `SpiderCoinNrvTouch` | `SpiderCoin::exeTouch` |  |
| `SpiderCoinNrvTouchAndApart` | `SpiderCoin::exeTouchAndApart` |  |

## SpinDriver

src/Game/MapObj/SpinDriver.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SpinDriverNrvTryDemo` | `SpinDriver::exeTryDemo` |  |
| `SpinDriverNrvNonActive` | `SpinDriver::exeNonActive` |  |
| `SpinDriverNrvAppear` | `SpinDriver::exeAppear` |  |
| `SpinDriverNrvWait` | `SpinDriver::exeWait` |  |
| `SpinDriverNrvCapture` | `SpinDriver::exeCapture` |  |
| `SpinDriverNrvShootStart` | `SpinDriver::exeShootStart` |  |
| `SpinDriverNrvShoot` | `SpinDriver::exeShoot` |  |
| `SpinDriverNrvCoolDown` | `SpinDriver::exeCoolDown` |  |

## SpinLeverSwitch

src/Game/MapObj/SpinLeverSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SpinLeverSwitchNrvWait` | `SpinLeverSwitch::exeWait` |  |
| `SpinLeverSwitchNrvSwitchOn` | `SpinLeverSwitch::exeSwitchOn` |  |
| `SpinLeverSwitchNrvEnd` | `SpinLeverSwitch::exeEnd` |  |

## SpinningBox

src/Game/MapObj/SpinningBox.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvFall` | `SpinningBox::exeFall` |  |
| `HostTypeNrvWait` | `SpinningBox::exeWait` | `SpinningBox::exeWait` |
| `HostTypeNrvSliding` | `SpinningBox::exeSliding` | `SpinningBox::exeSliding` |
| `HostTypeNrvSpinning` | `SpinningBox::exeSpinning` | `SpinningBox::exeSpinning` |
| `HostTypeNrvPointed` | `SpinningBox::exePointed` | `SpinningBox::exePointed` |

## SpringJetWater

src/Game/MapObj/SpringJetWater.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `SpringJetWater::exeWait` |  |
| `HostTypeJet` | `SpringJetWater::exeJet` |  |
| `HostTypeEnd` | `SpringJetWater::exeEnd` |  |

## SpringWaterFloaterSpot

src/Game/MapObj/SpringWaterFloaterSpot.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SpringWaterFloaterSpotNrvWait` | `SpringWaterFloaterSpot::exeWait` |  |
| `SpringWaterFloaterSpotNrvFloatSignal` | `SpringWaterFloaterSpot::exeFloatSignal` |  |
| `SpringWaterFloaterSpotNrvFloatUp` | `SpringWaterFloaterSpot::exeFloatUp` |  |
| `SpringWaterFloaterSpotNrvFloatEnd` | `SpringWaterFloaterSpot::exeFloatEnd` |  |

## StaffRoll

src/Game/Screen/StaffRoll.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StaffRollNrvPrepareDemo` | `StaffRoll::exePrepareDemo` |  |
| `StaffRollNrvPrepareBgm` | `StaffRoll::exePrepareBgm` |  |
| `StaffRollNrvTitleLogoAppear` | `StaffRoll::exeTitleLogoAppear` |  |
| `StaffRollNrvTitleLogoScroll` | `StaffRoll::exeTitleLogoScroll` |  |
| `StaffRollNrvLineScroll` | `StaffRoll::exeLineScroll` |  |
| `StaffRollNrvEndLogoScroll` | `StaffRoll::exeEndLogoScroll` |  |
| `StaffRollNrvAllRights` | `StaffRoll::exeAllRights` |  |
| `StaffRollNrvPrepareInfo` | `StaffRoll::exePrepareInfo` |  |
| `StaffRollNrvInfoPictureBookAppear` | `StaffRoll::exeInfoAppear` |  |
| `StaffRollNrvInfoCometAppear` | `StaffRoll::exeInfoAppear` |  |
| `StaffRollNrvInfoLuigiAppear` | `StaffRoll::exeInfoAppear` |  |
| `StaffRollNrvInfoPictureBookDisappear` | `StaffRoll::exeInfoDisappear` |  |
| `StaffRollNrvInfoCometDisappear` | `StaffRoll::exeInfoDisappear` |  |
| `StaffRollNrvInfoLuigiDisappear` | `StaffRoll::exeInfoDisappear` |  |
| `StaffRollNrvPauseForEndingMovie` | `StaffRoll::exePause` |  |
| `StaffRollNrvRestart` | `StaffRoll::exeRestart` |  |
| `StaffRollNrvPrepareSave` | `StaffRoll::exePrepareSave` |  |
| `StaffRollNrvSave` | `StaffRoll::exeSave` |  |
| `StaffRollNrvEnd` | `StaffRoll::exeEnd` |  |

## StaffRollLine

src/Game/Screen/StaffRoll.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StaffRollLineNrvScroll` | `StaffRollLine::exeScroll` |  |

## StaffRollPicture

src/Game/Screen/StaffRoll.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StaffRollPictureNrvWork` | `StaffRollPicture::exeWork` |  |

## StageResultInformer

src/Game/Screen/StageResultInformer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StageResultInformerAppearGetPowerStar` | `StageResultInformer::exeAppearGetPowerStar` |  |
| `StageResultInformerWaitBeforeCountUpPowerStar` | `StageResultInformer::exeWaitBeforeCountUpPowerStar` |  |
| `StageResultInformerCountUpPowerStar` | `StageResultInformer::exeCountUpPowerStar` |  |
| `StageResultInformerDisplayGetPowerStar` | `StageResultInformer::exeDisplayGetPowerStar` |  |
| `StageResultInformerDisappearGetPowerStar` | `StageResultInformer::exeDisappearGetPowerStar` |  |
| `StageResultInformerShowGalaxyComplete` | `StageResultInformer::exeShowGalaxyComplete` |  |
| `StageResultInformerAppearUpdateBestScore` | `StageResultInformer::exeAppearUpdateBestScore` |  |
| `StageResultInformerDisplayUpdateBestScore` | `StageResultInformer::exeDisplayUpdateBestScore` |  |
| `StageResultInformerDisappearUpdateBestScore` | `StageResultInformer::exeDisappearUpdateBestScore` |  |
| `StageResultInformerAppearGetStarPiece` | `StageResultInformer::exeAppearGetStarPiece` |  |
| `StageResultInformerDisplayGetStarPiece` | `StageResultInformer::exeDisplayGetStarPiece` |  |
| `StageResultInformerWaitBeforeDisappearGetStarPiece` | `StageResultInformer::exeWaitBeforeDisappearGetStarPiece` |  |
| `StageResultInformerDisappearGetStarPiece` | `StageResultInformer::exeDisappearGetStarPiece` |  |
| `StageResultInformerShowNewGalaxyDiscover` | `StageResultInformer::exeShowNewGalaxyDiscover` |  |
| `StageResultInformerShowGalaxyMap` | `StageResultInformer::exeShowGalaxyMap` |  |
| `StageResultInformerShowGetPictureBook` | `StageResultInformer::exeShowGetPictureBook` |  |
| `StageResultInformerShowTicoGalaxyAppear` | `StageResultInformer::exeShowTicoGalaxyAppear` |  |
| `StageResultInformerShowAstroMapForTico` | `StageResultInformer::exeShowAstroMapForTico` |  |

## StarCounter

src/Game/Screen/StarCounter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StarCounterNrvAppear` | `StarCounter::exeAppear` |  |
| `StarCounterNrvWait` | `StarCounter::exeWait` |  |
| `StarCounterNrvCountUp` | `StarCounter::exeCountUp` |  |
| `StarCounterNrvDisappear` | `StarCounter::exeDisappear` |  |

## StarPiece

src/Game/MapObj/StarPiece.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvFloating` | `StarPiece::exeFloating` |  |
| `HostTypeNrvHop` | `StarPiece::exeHop` |  |
| `HostTypeNrvFall` | `StarPiece::exeFall` |  |
| `HostTypeNrvFallAfterReflect` | `StarPiece::exeFall` |  |
| `HostTypeNrvToTarget` | `StarPiece::exeToTarget` |  |
| `HostTypeNrvSpinDrained` | `StarPiece::exeSpinDrained` |  |
| `HostTypeNrvToPlayerEnd` | `StarPiece::exeToPlayerEnd` |  |
| `HostTypeNrvThrow` | `StarPiece::exeThrow` | `StarPiece::exeThrow` |
| `HostTypeNrvThrowNoFall` | `StarPiece::exeThrow` | `StarPiece::exeThrow` |
| `HostTypeNrvThrowFall` | `StarPiece::exeThrowFall` | `StarPiece::exeThrow` |
| `HostTypeNrvFollowPlayer` | `StarPiece::exeFollowPlayer` |  |
| `HostTypeNrvRailMove` | `StarPiece::exeRailMove` |  |

## StarPieceCounter

src/Game/Screen/StarPieceCounter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StarPieceCounterNrvHide` | `StarPieceCounter::exeHide` |  |
| `StarPieceCounterNrvAppear` | `StarPieceCounter::exeAppear` |  |
| `StarPieceCounterNrvWait` | `StarPieceCounter::exeWait` |  |
| `StarPieceCounterNrvDisappear` | `StarPieceCounter::exeDisappear` |  |

## StarPieceFollowGroup

src/Game/MapObj/StarPieceFollowGroup.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvFollowToPlayer` | `StarPieceFollowGroup::exeFollowToPlayer` |  |
| `HostTypeNrvFollowEnd` | `StarPieceFollowGroup::exeFollowEnd` |  |

## StarPieceGroup

src/Game/MapObj/StarPieceGroup.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvGroup` | `StarPieceGroup::exeGroup` |  |
| `HostTypeNrvFlow` | `StarPieceGroup::exeFlow` |  |

## StarPieceMother

src/Game/MapObj/StarPieceMother.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvMoveOnRail` | `StarPieceMother::exeMoveOnRail` |  |
| `HostTypeNrvWait` | `StarPieceMother::exeWait` |  |

## StarPieceShooter

src/Game/MapObj/StarPieceDirector.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `StarPieceShooter::exeWait` |  |
| `HostTypeNrvLockOn` | `StarPieceShooter::exeLockOn` |  |

## StarPointerCommandStream

src/Game/Screen/StarPointerCommandStream.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `StarPointerCommandStream::exeWait` |  |
| `HostTypeNrvSignal` | `StarPointerCommandStream::exeSignal` |  |

## StarPointerGuidance

src/Game/Screen/StarPointerGuidance.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvAppear1P` | `StarPointerGuidance::exeAppear1P` |  |
| `HostTypeNrvWait1P` | `StarPointerGuidance::exeWait1P` |  |
| `HostTypeNrvEnd1P` | `StarPointerGuidance::exeEnd1P` |  |
| `HostTypeNrvAppearBlueStarGuide` | `StarPointerGuidance::exeAppearBlueStarGuide` |  |
| `HostTypeNrvWaitBlueStarGuide` | `StarPointerGuidance::exeWaitBlueStarGuide` |  |
| `HostTypeNrvEndBlueStarGuide` | `StarPointerGuidance::exeEndBlueStarGuide` |  |
| `HostTypeNrvAppear2P` | `StarPointerGuidance::exeAppear2P` |  |
| `HostTypeNrvWait2P` | `StarPointerGuidance::exeWait2P` |  |
| `HostTypeNrvEnd2P` | `StarPointerGuidance::exeEnd2P` |  |
| `HostTypeNrvEndWait` | `StarPointerGuidance::exeEndWait` |  |

## StarPointerLayout

src/Game/Screen/StarPointerLayout.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `StarPointerLayout::exeWait` |  |
| `HostTypeNrvOnReaction` | `StarPointerLayout::exeOnReaction` | `StarPointerLayout::exeOnReaction` |
| `HostTypeNrvGrip` | `StarPointerLayout::exeGrip` |  |
| `HostTypeNrvHold` | `StarPointerLayout::exeHold` | `StarPointerLayout::exeHold` |
| `HostTypeNrvHoldTouch` | `StarPointerLayout::exeHold` | `StarPointerLayout::exeHold` |
| `HostTypeNrvRelease` | `StarPointerLayout::exeRelease` |  |
| `HostTypeNrvOutScreen` | `StarPointerLayout::exeOutScreen` |  |
| `HostTypeNrvNotConnected` | `StarPointerLayout::exeHide` | `StarPointerLayout::exeHide` |
| `HostTypeNrvHide` | `StarPointerLayout::exeHide` |  |
| `HostTypeNrvHideImmediately` | `StarPointerLayout::exeHide` |  |
| `HostTypeNrvInvalidate` | `StarPointerLayout::exeHide` | `StarPointerLayout::exeHide` |
| `HostTypeNrvInvalidateWait` | `StarPointerLayout::exeHide` | `StarPointerLayout::exeHide` |

## StarReturnDemoStarter

src/Game/Demo/StarReturnDemoStarter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StarReturnDemoStarterNrvAppearWait` | `StarReturnDemoStarter::exeAppearWait` |  |
| `StarReturnDemoStarterNrvMove` | `StarReturnDemoStarter::exeMove` |  |
| `StarReturnDemoStarterNrvFlyWaitPowerStar` | `StarReturnDemoStarter::exeFlyWaitPowerStar` |  |
| `StarReturnDemoStarterNrvFlyWaitGrandStar` | `StarReturnDemoStarter::exeFlyWaitGrandStar` |  |
| `StarReturnDemoStarterNrvFall` | `StarReturnDemoStarter::exeFall` |  |
| `StarReturnDemoStarterNrvLand` | `StarReturnDemoStarter::exeLand` |  |
| `StarReturnDemoStarterNrvWait` | `StarReturnDemoStarter::exeWait` |  |
| `StarReturnDemoStarterNrvStageResult` | `StarReturnDemoStarter::exeStageResult` |  |
| `StarReturnDemoStarterNrvFadeOut` | `StarReturnDemoStarter::exeFadeOut` |  |
| `StarReturnDemoStarterNrvStageResultAfter` | `StarReturnDemoStarter::exeStageResultAfter` |  |

## StinkBugParent

src/Game/Enemy/StinkBugParent.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StinkBugParentNrvWait` | `StinkBugParent::exeWait` |  |
| `StinkBugParentNrvSearch` | `StinkBugParent::exeSearch` |  |
| `StinkBugParentNrvDashSign` | `StinkBugParent::exeDashSign` |  |
| `StinkBugParentNrvDashSignEnd` | `StinkBugParent::exeDashSignEnd` |  |
| `StinkBugParentNrvDash` | `StinkBugParent::exeDash` |  |
| `StinkBugParentNrvDashEnd` | `StinkBugParent::exeDashEnd` |  |
| `StinkBugParentNrvBack` | `StinkBugParent::exeBack` |  |
| `StinkBugParentNrvHipDropDown` | `StinkBugParent::exeHipDropDown` |  |
| `StinkBugParentNrvAttack` | `StinkBugParent::exeAttack` |  |
| `StinkBugParentNrvSpinReaction` | `StinkBugParent::exeSpinReaction` |  |
| `StinkBugParentNrvPanic` | `StinkBugParent::exePanic` |  |
| `StinkBugParentNrvShakeStart` | `StinkBugParent::exeShakeStart` |  |
| `StinkBugParentNrvShake` | `StinkBugParent::exeShake` |  |
| `StinkBugParentNrvRecover` | `StinkBugParent::exeRecover` |  |
| `StinkBugParentNrvDemoPrepare` | `StinkBugParent::exeDemoPrepare` |  |
| `StinkBugParentNrvDemoChildDown` | `StinkBugParent::exeDemoChildDown` |  |
| `StinkBugParentNrvDemoAnger` | `StinkBugParent::exeDemoAnger` |  |

## StinkBugSmall

src/Game/Enemy/StinkBugSmall.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StinkBugSmallNrvWait` | `StinkBugSmall::exeWait` |  |
| `StinkBugSmallNrvSearch` | `StinkBugSmall::exeSearch` |  |
| `StinkBugSmallNrvDashSign` | `StinkBugSmall::exeDashSign` |  |
| `StinkBugSmallNrvDashSignEnd` | `StinkBugSmall::exeDashSignEnd` |  |
| `StinkBugSmallNrvDash` | `StinkBugSmall::exeDash` |  |
| `StinkBugSmallNrvDashEnd` | `StinkBugSmall::exeDashEnd` |  |
| `StinkBugSmallNrvBack` | `StinkBugSmall::exeBack` |  |
| `StinkBugSmallNrvHipDropDown` | `StinkBugSmall::exeHipDropDown` |  |
| `StinkBugSmallNrvAttack` | `StinkBugSmall::exeAttack` |  |
| `StinkBugSmallNrvSpinReaction` | `StinkBugSmall::exeSpinReaction` |  |
| `StinkBugSmallNrvShakeStart` | `StinkBugSmall::exeShakeStart` |  |
| `StinkBugSmallNrvShake` | `StinkBugSmall::exeShake` |  |
| `StinkBugSmallNrvPanic` | `StinkBugSmall::exePanic` |  |
| `StinkBugSmallNrvRecover` | `StinkBugSmall::exeRecover` |  |
| `StinkBugSmallNrvPointerSwoon` | `StinkBugSmall::exePointerSwoon` | `StinkBugSmall::exePointerSwoon` |
| `StinkBugSmallNrvForceFall` | `StinkBugSmall::exeForceFall` |  |

## StopSceneStateControl

src/Game/Scene/SceneNameObjMovementController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StopSceneStateControlNone` | `StopSceneStateControl::exeNone` |  |
| `StopSceneStateControlStopped` | `StopSceneStateControl::exeStopped` |  |

## StorySequenceExecutor

src/Game/System/StorySequenceExecutor.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StorySequenceExecutorIdle` | `StorySequenceExecutor::exeIdle` |  |
| `StorySequenceExecutorWaitToSceneStart` | `StorySequenceExecutor::exeWaitToSceneStart` |  |
| `StorySequenceExecutorPlayDemoSequence` | `StorySequenceExecutor::exePlayDemoSequence` |  |
| `StorySequenceExecutorWaitTimeKeepDemoEnd` | `StorySequenceExecutor::exeWaitTimeKeepDemoEnd` |  |
| `StorySequenceExecutorWaitSaveEnd` | `StorySequenceExecutor::exeWaitSaveEnd` |  |
| `StorySequenceExecutorStartSaveAfterSequence` | `StorySequenceExecutor::exeStartSaveAfterSequence` |  |
| `StorySequenceExecutorWaitSaveEndAfterSequence` | `StorySequenceExecutor::exeWaitSaveEndAfterSequence` |  |
| `StorySequenceExecutorFadeinAfterSequence` | `StorySequenceExecutor::exeFadeinAfterSequence` |  |
| `StorySequenceExecutorStaffRollSequence` | `StorySequenceExecutor::exeStaffRollSequence` |  |

## StrayTico

src/Game/NPC/StrayTico.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `StrayTicoNrvWait` | `StrayTico::exeWait` |  |
| `StrayTicoNrvGlad` | `StrayTico::exeGlad` |  |
| `StrayTicoNrvChase` | `StrayTico::exeChase` |  |
| `StrayTicoNrvLauncherCamera` | `StrayTico::exeLauncherCamera` |  |
| `StrayTicoNrvCompleteDemo` | `StrayTico::exeCompleteDemo` |  |

## StringSpider

src/Game/Enemy/StringSpider.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `StringSpider::exeWait` | `StringSpider::exeWait` |
| `HostTypeNrvDownStart` | `StringSpider::exeDownStart` |  |
| `HostTypeNrvDownEnd` | `StringSpider::exeDownEnd` |  |
| `HostTypeNrvDown` | `StringSpider::exeDown` |  |
| `HostTypeNrvUp` | `StringSpider::exeUp` |  |
| `HostTypeNrvSearch` | `StringSpider::exeSearch` |  |
| `HostTypeNrvAttack` | `StringSpider::exeAttack` |  |
| `HostTypeNrvHit` | `StringSpider::exeHit` |  |
| `HostTypeNrvSpinned` | `StringSpider::exeSpinned` |  |
| `HostTypeNrvTrampleFall` | `StringSpider::exeTrampleFall` |  |
| `HostTypeNrvWallHitDeath` | `StringSpider::exeWallHitDeath` |  |
| `HostTypeNrvTrampleDeath` | `StringSpider::exeWallHitDeath` |  |
| `HostTypeNrvBlow` | `StringSpider::exeBlow` |  |
| `HostTypeNrvPointerGripped` | `StringSpider::exePointerGripped` | `StringSpider::exePointerGripped` |

## SubmarineSteam

src/Game/MapObj/SubmarineSteam.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWaitForSwitchOn` | `SubmarineSteam::exeWaitForSwitchOn` |  |
| `HostTypeSteam` | `SubmarineSteam::exeSteam` |  |

## SubmarineVolcanoBigColumn

src/Game/MapObj/SubmarineVolcanoBigColumn.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SubmarineVolcanoBigColumnNrvWait` | `SubmarineVolcanoBigColumn::exeWait` |  |
| `SubmarineVolcanoBigColumnNrvBreak` | `SubmarineVolcanoBigColumn::exeBreak` |  |

## SuddenDeathMeter

src/Game/Screen/SuddenDeathMeter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SuddenDeathMeterNrvAppear` | `SuddenDeathMeter::exeAppear` |  |
| `SuddenDeathMeterNrvWait` | `SuddenDeathMeter::exeWait` |  |
| `SuddenDeathMeterNrvPowerUp` | `SuddenDeathMeter::exePowerUp` |  |
| `SuddenDeathMeterNrvMeterMove` | `SuddenDeathMeter::exeMeterMove` |  |
| `SuddenDeathMeterNrvCounterMove` | `SuddenDeathMeter::exeCounterMove` |  |
| `SuddenDeathMeterNrvBreakMeter` | `SuddenDeathMeter::exeBreakMeter` |  |
| `SuddenDeathMeterNrvZeroMeter` | `SuddenDeathMeter::exeZeroMeter` |  |
| `SuddenDeathMeterNrvZeroMeterBreak` | `SuddenDeathMeter::exeZeroMeterBreak` |  |

## SunakazeKunTrap

src/Game/MapObj/Sandstorm.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SunakazeKunTrapNrvTrap` | `SunakazeKunTrap::exeTrap` |  |

## SuperSpinDriver

src/Game/MapObj/SuperSpinDriver.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SuperSpinDriverNrvTryDemo` | `SuperSpinDriver::exeTryDemo` |  |
| `SuperSpinDriverNrvEmptyNonActive` | `SuperSpinDriver::exeEmptyNonActive` |  |
| `SuperSpinDriverNrvEmptyAppear` | `SuperSpinDriver::exeEmptyAppear` |  |
| `SuperSpinDriverNrvEmptyWait` | `SuperSpinDriver::exeEmptyWait` |  |
| `SuperSpinDriverNrvNonActive` | `SuperSpinDriver::exeNonActive` |  |
| `SuperSpinDriverNrvAppear` | `SuperSpinDriver::exeAppear` |  |
| `SuperSpinDriverNrvWait` | `SuperSpinDriver::exeWait` |  |
| `SuperSpinDriverNrvCapture` | `SuperSpinDriver::exeCapture` |  |
| `SuperSpinDriverNrvShootStart` | `SuperSpinDriver::exeShootStart` |  |
| `SuperSpinDriverNrvShoot` | `SuperSpinDriver::exeShoot` | `SuperSpinDriver::exeShoot` |
| `SuperSpinDriverNrvCoolDown` | `SuperSpinDriver::exeCoolDown` |  |

## SurfingGuidance

src/Game/Screen/SurfingGuidance.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SurfingGuidanceFadeIn` | `SurfingGuidance::exeFadeIn` |  |
| `SurfingGuidanceLevelOffReady` | `SurfingGuidance::exeLevelOffReady` |  |
| `SurfingGuidanceLevelOffHold` | `SurfingGuidance::exeLevelOffHold` |  |
| `SurfingGuidanceLevelOffSuccess` | `SurfingGuidance::exeLevelOffSuccess` |  |
| `SurfingGuidanceTurnLeftReady` | `SurfingGuidance::exeTurnLeftReady` |  |
| `SurfingGuidanceTurnLeftHold` | `SurfingGuidance::exeTurnLeftHold` |  |
| `SurfingGuidanceTurnLeftSuccess` | `SurfingGuidance::exeTurnLeftSuccess` |  |
| `SurfingGuidanceTurnRightReady` | `SurfingGuidance::exeTurnRightReady` |  |
| `SurfingGuidanceTurnRightHold` | `SurfingGuidance::exeTurnRightHold` |  |
| `SurfingGuidanceTurnRightSuccess` | `SurfingGuidance::exeTurnRightSuccess` |  |
| `SurfingGuidanceFadeOut` | `SurfingGuidance::exeFadeOut` |  |

## SurfRay

src/Game/Ride/SurfRay.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SurfRayNrvWaitPlayer` | `SurfRay::exeWaitPlayer` |  |
| `SurfRayNrvTurnToWait` | `SurfRay::exeTurnToWait` |  |
| `SurfRayNrvWait` | `SurfRay::exeWait` |  |
| `SurfRayNrvRideAccel` | `SurfRay::exeRideAccel` |  |
| `SurfRayNrvRideAccelTwist` | `SurfRay::exeRideAccelTwist` |  |
| `SurfRayNrvRideFree` | `SurfRay::exeRideFree` |  |
| `SurfRayNrvRideFreeTwist` | `SurfRay::exeRideFreeTwist` |  |
| `SurfRayNrvRideJump` | `SurfRay::exeRideJump` |  |
| `SurfRayNrvRideJumpHigh` | `SurfRay::exeRideJumpHigh` |  |
| `SurfRayNrvRideFall` | `SurfRay::exeRideFall` |  |
| `SurfRayNrvRideLand` | `SurfRay::exeRideLand` |  |
| `SurfRayNrvTutorial` | `SurfRay::exeTutorial` |  |
| `SurfRayNrvWipeOut` | `SurfRay::exeWipeOut` |  |
| `SurfRayNrvWipeIn` | `SurfRay::exeWipeIn` |  |
| `SurfRayNrvReady` | `SurfRay::exeReady` |  |

## SurfRayTutorial

src/Game/Ride/SurfRayTutorial.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SurfRayTutorialNrvTutorialAllStart` | `SurfRayTutorial::exeTutorialAllStart` |  |
| `SurfRayTutorialNrvTutorialStraightStart` | `SurfRayTutorial::exeTutorialStraightStart` |  |
| `SurfRayTutorialNrvTutorialStraightFailure` | `SurfRayTutorial::exeTutorialStraightFailure` |  |
| `SurfRayTutorialNrvTutorialStraightUpkeep` | `SurfRayTutorial::exeTutorialStraightUpkeep` |  |
| `SurfRayTutorialNrvTutorialStraightSuccess` | `SurfRayTutorial::exeTutorialStraightSuccess` |  |
| `SurfRayTutorialNrvTutorialTurnLeftStart` | `SurfRayTutorial::exeTutorialTurnLeftStart` |  |
| `SurfRayTutorialNrvTutorialTurnLeftFailure` | `SurfRayTutorial::exeTutorialTurnLeftFailure` |  |
| `SurfRayTutorialNrvTutorialTurnLeftUpkeep` | `SurfRayTutorial::exeTutorialTurnLeftUpkeep` |  |
| `SurfRayTutorialNrvTutorialTurnLeftSuccess` | `SurfRayTutorial::exeTutorialTurnLeftSuccess` |  |
| `SurfRayTutorialNrvTutorialTurnRightStart` | `SurfRayTutorial::exeTutorialTurnRightStart` |  |
| `SurfRayTutorialNrvTutorialTurnRightFailure` | `SurfRayTutorial::exeTutorialTurnRightFailure` |  |
| `SurfRayTutorialNrvTutorialTurnRightUpkeep` | `SurfRayTutorial::exeTutorialTurnRightUpkeep` |  |
| `SurfRayTutorialNrvTutorialTurnRightSuccess` | `SurfRayTutorial::exeTutorialTurnRightSuccess` |  |
| `SurfRayTutorialNrvTutorialAllEnd` | `SurfRayTutorial::exeTutorialAllEnd` |  |

## SurprisedGalaxy

src/Game/MapObj/SurprisedGalaxy.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SurprisedGalaxyNrvExit` | `SurprisedGalaxy::exeExit` |  |
| `SurprisedGalaxyNrvWait` | `SurprisedGalaxy::exeWait` |  |

## SwingRope

src/Game/Ride/SwingRope.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SwingRopeNrvStop` | `SwingRope::exeStop` |  |
| `SwingRopeNrvFree` | `SwingRope::exeFree` |  |
| `SwingRopeNrvFreeInvalid` | `SwingRope::exeFreeInvalid` |  |
| `SwingRopeNrvBindSlideDownStart` | `SwingRope::exeBindSlideDownStart` |  |
| `SwingRopeNrvBindSlideDown` | `SwingRope::exeBindSlideDown` |  |
| `SwingRopeNrvBindStretch` | `SwingRope::exeBindStretch` |  |
| `SwingRopeNrvBindLoose` | `SwingRope::exeBindLoose` |  |

## SwitchBox

src/Game/MapObj/SwitchBox.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SwitchBoxNrvWait` | `SwitchBox::exeWait` |  |
| `SwitchBoxNrvHit` | `SwitchBox::exeHit` |  |

## Syati

src/Game/NPC/Syati.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SyatiWait` | `Syati::exeWait` |  |
| `SyatiFadeoutStartEvent` | `Syati::exeFadeoutBeforeTalk` |  |
| `SyatiWaitBlankStartEvent` | `Syati::exeWaitBlank` |  |
| `SyatiFadeinStartEvent` | `Syati::exeFadeinBeforeTalk` |  |
| `SyatiTalkStartMission` | `Syati::exeTalkStartMission` |  |
| `SyatiReadyToStart` | `Syati::exeReadyToStart` |  |
| `SyatiCountDown` | `Syati::exeCountDown` |  |
| `SyatiSwim` | `Syati::exeSwim` |  |
| `SyatiEmitRing` | `Syati::exeEmitRing` |  |
| `SyatiWaitStarAppeared` | `Syati::exeWaitStarAppeared` |  |
| `SyatiReachToEnd` | `Syati::exeReachToEnd` |  |
| `SyatiWaitAllRingDisappear` | `Syati::exeWaitAllRingDisappear` |  |
| `SyatiFadeoutRetryEvent` | `Syati::exeFadeoutBeforeTalk` |  |
| `SyatiWaitBlankRetryEvent` | `Syati::exeWaitBlank` |  |
| `SyatiFadeinRetryEvent` | `Syati::exeFadeinBeforeTalk` |  |
| `SyatiTalkRetryMission` | `Syati::exeTalkRetryMission` |  |
| `SyatiForceKill` | `Syati::exeForceKill` |  |
| `SyatiHideOnShore` | `Syati::exeHideOnShore` |  |
| `SyatiWaitOnShore` | `Syati::exeWaitOnShore` |  |
| `SyatiWaitTalkNormal` | `Syati::exeWaitTalkNormal` |  |
| `SyatiTalkNormal` | `Syati::exeTalkNormal` |  |
| `SyatiWaitDemoStart` | `Syati::exeStart` |  |

## SysInfoWindow

src/Game/Screen/SysInfoWindow.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `SysInfoWindowNrvAppear` | `SysInfoWindow::exeAppear` |  |
| `SysInfoWindowNrvWait` | `SysInfoWindow::exeWait` |  |
| `SysInfoWindowNrvDisappear` | `SysInfoWindow::exeDisappear` |  |

## TakeOutStar

src/Game/Util/NPCUtil.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TakeOutStarNrvAnim` | `TakeOutStar::exeAnim` |  |
| `TakeOutStarNrvDemo` | `TakeOutStar::exeDemo` |  |
| `TakeOutStarNrvTerm` | `TakeOutStar::exeTerm` |  |

## Takobo

src/Game/Enemy/Takobo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvWait` | `Takobo::exeWait` | `Takobo::exeWait` |
| `HostTypeNrvMove` | `Takobo::exeMove` | `Takobo::exeMove` |
| `HostTypeNrvPress` | `Takobo::exePress` |  |
| `HostTypeNrvAttack` | `Takobo::exeAttack` | `Takobo::exeAttack` |
| `HostTypeNrvHitPunch` | `Takobo::exeHitPunch` |  |
| `HostTypeNrvStunStart` | `Takobo::exeStunStart` |  |
| `HostTypeNrvStun` | `Takobo::exeStun` |  |
| `HostTypeNrvStunEnd` | `Takobo::exeStunEnd` |  |
| `HostTypeNrvHitReaction` | `Takobo::exeHitReaction` |  |
| `HostTypeNrvIce` | `Takobo::exeIce` |  |
| `HostTypeNrvPointerAimed` | `Takobo::exePointerAimed` | `Takobo::exePointerAimed` |

## TakoHei

src/Game/Enemy/TakoHei.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TakoHeiNrvNonActive` | `TakoHei::exeNonActive` |  |
| `TakoHeiNrvWait` | `TakoHei::exeWait` |  |
| `TakoHeiNrvWalk` | `TakoHei::exeWalk` |  |
| `TakoHeiNrvFindTurn` | `TakoHei::exeFindTurn` |  |
| `TakoHeiNrvFind` | `TakoHei::exeFind` |  |
| `TakoHeiNrvPursue` | `TakoHei::exePursue` |  |
| `TakoHeiNrvCoolDown` | `TakoHei::exeCoolDown` |  |
| `TakoHeiNrvAttackSign` | `TakoHei::exeAttackSign` |  |
| `TakoHeiNrvAttack` | `TakoHei::exeAttack` |  |
| `TakoHeiNrvAttackSuccess` | `TakoHei::exeAttackSuccess` |  |
| `TakoHeiNrvBindStarPointer` | `TakoHei::exeBindStarPointer` | `TakoHei::exeBindStarPointer` |
| `TakoHeiNrvSwoon` | `TakoHei::exeSwoon` |  |
| `TakoHeiNrvRecover` | `TakoHei::exeRecover` |  |
| `TakoHeiNrvPressDown` | `TakoHei::exePressDown` |  |
| `TakoHeiNrvHipDropDown` | `TakoHei::exeHipDropDown` |  |
| `TakoHeiNrvFlatDown` | `TakoHei::exeFlatDown` |  |
| `TakoHeiNrvPunchDown` | `TakoHei::exePunchDown` |  |

## TakoHeiInk

src/Game/Enemy/TakoHeiInk.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TakoHeiInkNrvWait` | `TakoHeiInk::exeWait` |  |

## TalkBalloonEvent

src/Game/NPC/TalkBalloon.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TalkBalloonEventNrvWait` | `TalkBalloonEvent::exeWait` |  |
| `TalkBalloonEventNrvOpen` | `TalkBalloonEvent::exeOpen` |  |
| `TalkBalloonEventNrvTalk` | `TalkBalloonEvent::exeTalk` |  |
| `TalkBalloonEventNrvClose` | `TalkBalloonEvent::exeClose` |  |

## TalkBalloonShort

src/Game/NPC/TalkBalloon.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TalkBalloonShortNrvOpen` | `TalkBalloonShort::exeOpen` |  |
| `TalkBalloonShortNrvTalk` | `TalkBalloonShort::exeTalk` |  |
| `TalkBalloonShortNrvClose` | `TalkBalloonShort::exeClose` |  |

## TalkDirector

src/Game/NPC/TalkDirector.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TalkDirectorNrvWait` | `TalkDirector::exeWait` |  |
| `TalkDirectorNrvPrep` | `TalkDirector::exePrep` |  |
| `TalkDirectorNrvTalk` | `TalkDirector::exeTalk` |  |
| `TalkDirectorNrvSlct` | `TalkDirector::exeSlct` |  |
| `TalkDirectorNrvNext` | `TalkDirector::exeNext` |  |
| `TalkDirectorNrvTerm` | `TalkDirector::exeTerm` |  |

## Tamakoro

src/Game/Ride/Tamakoro.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TamakoroNrvStandByTutorial` | `Tamakoro::exeStandByTutorial` |  |
| `TamakoroNrvStandByBind` | `Tamakoro::exeStandByBind` |  |
| `TamakoroNrvBindStart` | `Tamakoro::exeBindStart` |  |
| `TamakoroNrvBindStartLand` | `Tamakoro::exeBindStartLand` |  |
| `TamakoroNrvTutorial` | `Tamakoro::exeTutorial` |  |
| `TamakoroNrvWait` | `Tamakoro::exeWait` |  |
| `TamakoroNrvFall` | `Tamakoro::exeFall` |  |
| `TamakoroNrvJump` | `Tamakoro::exeJump` |  |
| `TamakoroNrvBumpWall` | `Tamakoro::exeBumpWall` |  |
| `TamakoroNrvLand` | `Tamakoro::exeLand` |  |
| `TamakoroNrvJumpHole` | `Tamakoro::exeJumpHole` |  |
| `TamakoroNrvJumpHoleSetUp` | `Tamakoro::exeJumpHoleSetUp` |  |
| `TamakoroNrvJumpHoleLaunch` | `Tamakoro::exeJumpHoleLaunch` | `Tamakoro::exeJumpHoleLaunch` |
| `TamakoroNrvDashRail` | `Tamakoro::exeDashRail` |  |
| `TamakoroNrvDashRailEnd` | `Tamakoro::exeDashRailEnd` |  |
| `TamakoroNrvRideRail` | `Tamakoro::exeRideRail` |  |
| `TamakoroNrvRideRailFastStart` | `Tamakoro::exeRideRailFastStart` |  |
| `TamakoroNrvRideRailFast` | `Tamakoro::exeRideRailFast` |  |
| `TamakoroNrvRideRailFastEnd` | `Tamakoro::exeRideRailFastEnd` |  |
| `TamakoroNrvBindEnd` | `Tamakoro::exeBindEnd` |  |

## TamakoroTutorial

src/Game/Ride/TamakoroTutorial.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeNrvFirst` | `TamakoroTutorial::exeFirst` |  |
| `HostTypeNrvFirstForceTalk` | `TamakoroTutorial::exeFirstForceTalk` |  |
| `HostTypeNrvWaitRideTalk` | `TamakoroTutorial::exeWaitRideTalk` |  |
| `HostTypeNrvWaitRide` | `TamakoroTutorial::exeWaitRide` |  |
| `HostTypeNrvWaitRaiseTalk` | `TamakoroTutorial::exeWaitRaiseTalk` |  |
| `HostTypeNrvWaitRaiseStable` | `TamakoroTutorial::exeWaitRaiseStable` |  |
| `HostTypeNrvRaiseOk` | `TamakoroTutorial::exeRaiseOk` |  |
| `HostTypeNrvRaiseOkTalk` | `TamakoroTutorial::exeRaiseOkTalk` |  |
| `HostTypeNrvFinalTalk` | `TamakoroTutorial::exeFinalTalk` |  |
| `HostTypeNrvWait` | `TamakoroTutorial::exeWait` |  |
| `HostTypeNrvDirectRide` | `TamakoroTutorial::exeWait` |  |
| `HostTypeNrvTrampleFront` | `TamakoroTutorial::exeTrample` |  |
| `HostTypeNrvTrampleBack` | `TamakoroTutorial::exeTrample` |  |
| `HostTypeNrvRecoverFront` | `TamakoroTutorial::exeRecover` |  |
| `HostTypeNrvRecoverBack` | `TamakoroTutorial::exeRecover` |  |

## Teresa

src/Game/Enemy/Teresa.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TeresaNrvAppearFromWall` | `Teresa::exeAppearFromWall` | `Teresa::exeAppearFromWall` |
| `TeresaNrvAppearFromGround` | `Teresa::exeAppearFromGround` | `Teresa::exeAppearFromGround` |
| `TeresaNrvWait` | `Teresa::exeWait` |  |
| `TeresaNrvWalk` | `Teresa::exeWalk` |  |
| `TeresaNrvRailWalk` | `Teresa::exeRailWalk` |  |
| `TeresaNrvRailTurn` | `Teresa::exeRailTurn` |  |
| `TeresaNrvLoveFind` | `Teresa::exeLoveFind` |  |
| `TeresaNrvLoveChase` | `Teresa::exeLoveChase` |  |
| `TeresaNrvLoveHit` | `Teresa::exeLoveHit` |  |
| `TeresaNrvLoveEnd` | `Teresa::exeLoveEnd` |  |
| `TeresaNrvChase` | `Teresa::exeChase` |  |
| `TeresaNrvShay` | `Teresa::exeShay` |  |
| `TeresaNrvAggressive` | `Teresa::exeAggressive` |  |
| `TeresaNrvAttackSuccess` | `Teresa::exeAttackSuccess` |  |
| `TeresaNrvDrift` | `Teresa::exeDrift` | `Teresa::exeDrift` |
| `TeresaNrvHideWall` | `Teresa::exeHideWall` |  |
| `TeresaNrvHideWater` | `Teresa::exeHideWater` |  |
| `TeresaNrvAscension` | `Teresa::exeAscension` |  |
| `TeresaNrvStop` | `Teresa::exeStop` |  |

## TeresaRacer

src/Game/NPC/TeresaRacer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TeresaRacerNrvWait` | `TeresaRacer::exeWait` |  |
| `TeresaRacerNrvPre` | `TeresaRacer::exePre` |  |
| `TeresaRacerNrvReady` | `TeresaRacer::exeReady` |  |
| `TeresaRacerNrvMove` | `TeresaRacer::exeMove` |  |
| `TeresaRacerNrvGoal` | `TeresaRacer::exeGoal` |  |
| `TeresaRacerNrvPost` | `TeresaRacer::exePost` |  |
| `TeresaRacerNrvTalk` | `TeresaRacer::exeTalk` |  |
| `TeresaRacerNrvTakeOutStar` | `TeresaRacer::exeTakeOutStar` |  |

## TeresaWater

src/Game/Enemy/TeresaWater.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TeresaWaterNrvMove` | `TeresaWater::exeMove` |  |
| `TeresaWaterNrvHit` | `TeresaWater::exeHit` |  |
| `TeresaWaterNrvStun` | `TeresaWater::exeStun` |  |
| `TeresaWaterNrvPointerSwoon` | `TeresaWater::exePointerSwoon` | `TeresaWater::exePointerSwoon` |

## Tico

src/Game/NPC/Tico.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TicoNrvNoReaction` | `Tico::exeNoReaction` |  |
| `TicoNrvReaction` | `Tico::exeReaction` |  |
| `TicoNrvDelight` | `Tico::exeDelight` |  |
| `TicoNrvAppear` | `Tico::exeAppear` |  |
| `TicoNrvWait` | `Tico::exeWait` |  |
| `TicoNrvTalk` | `Tico::exeTalk` |  |
| `TicoNrvMeta` | `Tico::exeMeta` |  |
| `TicoNrvBlue0` | `Tico::exeBlue0` |  |
| `TicoNrvBlue1` | `Tico::exeBlue1` |  |
| `TicoNrvRed0` | `Tico::exeRed0` |  |
| `TicoNrvRed1` | `Tico::exeRed1` |  |
| `TicoNrvRed2` | `Tico::exeRed2` |  |
| `TicoNrvSpin0` | `Tico::exeSpin0` |  |
| `TicoNrvGuide0` | `Tico::exeGuide0` |  |
| `TicoNrvGuide1` | `Tico::exeGuide1` |  |
| `TicoNrvGuide2` | `Tico::exeGuide2` |  |
| `TicoNrvGuide3` | `Tico::exeGuide3` |  |
| `TicoNrvLead0` | `Tico::exeLead0` |  |

## TicoComet

src/Game/NPC/TicoComet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TicoCometNrvDemoAnim` | `TicoComet::exeDemoAnim` |  |
| `TicoCometNrvDemoFade` | `TicoComet::exeDemoFade` |  |
| `TicoCometNrvDemoEnd` | `TicoComet::exeDemoEnd` |  |

## TicoDemoGetPower

src/Game/NPC/TicoDemoGetPower.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TicoDemoGetPowerNrvWait` | `TicoDemoGetPower::exeWait` |  |
| `TicoDemoGetPowerNrvDemo` | `TicoDemoGetPower::exeDemo` |  |

## TicoDomeLecture

src/Game/NPC/TicoDomeLecture.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TicoDomeLectureNrvWait` | `TicoDomeLecture::exeWait` |  |
| `TicoDomeLectureNrvMove` | `TicoDomeLecture::exeMove` |  |
| `TicoDomeLectureNrvMetamorphosis` | `TicoDomeLecture::exeMetamorphosis` |  |

## TicoEat

src/Game/NPC/TicoComet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TicoEatNrvEatPre` | `TicoEat::exeEatPre` |  |
| `TicoEatNrvEatNow` | `TicoEat::exeEatNow` |  |
| `TicoEatNrvEatPst` | `TicoEat::exeEatPst` |  |
| `TicoEatNrvEatEnd` | `TicoEat::exeEatEnd` |  |
| `TicoEatNrvReaction` | `TicoEat::exeReaction` |  |

## TicoFat

src/Game/NPC/TicoFat.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TicoFatNrvReaction` | `TicoFat::exeReaction` |  |
| `TicoFatNrvPrep` | `TicoFat::exePrep` |  |
| `TicoFatNrvWait` | `TicoFat::exeWait` |  |
| `TicoFatNrvPoint` | `TicoFat::exePoint` |  |
| `TicoFatNrvEat` | `TicoFat::exeEat` |  |
| `TicoFatNrvChem` | `TicoFat::exeChem` |  |
| `TicoFatNrvTest` | `TicoFat::exeTest` |  |
| `TicoFatNrvFullness` | `TicoFat::exeFullness` |  |
| `TicoFatNrvMeta` | `TicoFat::exeMeta` |  |
| `TicoFatNrvDemo` | `TicoFat::exeDemo` |  |
| `TicoFatNrvFly` | `TicoFat::exeFly` |  |
| `TicoFatNrvWipeOut` | `TicoFat::exeWipeOut` |  |
| `TicoFatNrvWipeIn` | `TicoFat::exeWipeIn` |  |
| `TicoFatNrvInfo` | `TicoFat::exeInfo` |  |
| `TicoFatNrvAfter` | `TicoFat::exeAfter` |  |

## TicoRail

src/Game/NPC/TicoRail.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TicoRailNrvWait` | `TicoRail::exeWait` |  |
| `TicoRailNrvLookAround` | `TicoRail::exeLookAround` |  |
| `TicoRailNrvMoveSign` | `TicoRail::exeMoveSign` |  |
| `TicoRailNrvMoveSignAndTurn` | `TicoRail::exeMoveSign` |  |
| `TicoRailNrvMove` | `TicoRail::exeMove` |  |
| `TicoRailNrvStop` | `TicoRail::exeStop` |  |
| `TicoRailNrvTalkStart` | `TicoRail::exeTalkStart` |  |
| `TicoRailNrvTalk` | `TicoRail::exeTalk` |  |
| `TicoRailNrvTalkCancel` | `TicoRail::exeTalkCancel` |  |
| `TicoRailNrvGoodBye` | `TicoRail::exeGoodBye` |  |

## TicoReading

src/Game/NPC/TicoReading.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TicoReadingNrvWait` | `TicoReading::exeWait` |  |

## TicoShop

src/Game/NPC/TicoShop.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TicoShopNrvDemo` | `TicoShop::exeDemo` |  |

## TicoStarRing

src/Game/NPC/TicoStarRing.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TicoStarRingNrvWait` | `TicoStarRing::exeWait` |  |

## TimeAppearObj

src/Game/MapObj/TimeAppearObj.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TimeAppearObjNrvHide` | `TimeAppearObj::exeHide` |  |
| `TimeAppearObjNrvEnd` | `TimeAppearObj::exeEnd` |  |

## TimeLimitLayout

src/Game/Screen/TimeLimitLayout.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TimeLimitLayoutAppear` | `TimeLimitLayout::exeAppear` |  |
| `TimeLimitLayoutCountDown` | `TimeLimitLayout::exeCountDown` |  |
| `TimeLimitLayoutScaleUp` | `TimeLimitLayout::exeScaleUp` |  |
| `TimeLimitLayoutScaleKeep` | `TimeLimitLayout::exeScaleKeep` |  |
| `TimeLimitLayoutScaleDown` | `TimeLimitLayout::exeScaleDown` |  |
| `TimeLimitLayoutFadeout` | `TimeLimitLayout::exeFadeout` |  |
| `TimeLimitLayoutTimeUpReady` | `TimeLimitLayout::exeTimeUpReady` |  |

## TimerMoveWall

src/Game/MapObj/TimerMoveWall.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TimerMoveWallNrvWait` | `TimerMoveWall::exeWait` |  |
| `TimerMoveWallNrvMoveFront` | `TimerMoveWall::exeMove` |  |
| `TimerMoveWallNrvMoveBack` | `TimerMoveWall::exeMove` |  |

## TitleSequenceProduct

src/Game/Screen/TitleSequenceProduct.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TitleSequenceProductBgmPrepare` | `TitleSequenceProduct::exeBgmPrepare` |  |
| `TitleSequenceProductLogoFadein` | `TitleSequenceProduct::exeLogoFadein` |  |
| `TitleSequenceProductLogoWait` | `TitleSequenceProduct::exeLogoWait` |  |
| `TitleSequenceProductLogoDisplay` | `TitleSequenceProduct::exeLogoDisplay` |  |
| `TitleSequenceProductDecide` | `TitleSequenceProduct::exeDecide` |  |
| `TitleSequenceProductDead` | `TitleSequenceProduct::exeDead` |  |

## TombSpider

src/Game/Boss/TombSpider.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TombSpiderNrvWaitPlayer` | `TombSpider::exeWaitPlayer` |  |
| `TombSpiderNrvDemoGateOpen` | `TombSpider::exeDemoGateOpen` |  |
| `TombSpiderNrvActionCocoon` | `TombSpider::exeActionCocoon` |  |
| `TombSpiderNrvDemoCocoonBreak` | `TombSpider::exeDemoCocoonBreak` |  |
| `TombSpiderNrvDemoBattle1stStart` | `TombSpider::exeDemoBattle1stStart` |  |
| `TombSpiderNrvDemoBattle1stStartJumpToPlayer` | `TombSpider::exeDemoBattle1stStartJumpToPlayer` |  |
| `TombSpiderNrvAction1st` | `TombSpider::exeAction1st` |  |
| `TombSpiderNrvDemoBattle1stEnd` | `TombSpider::exeDemoBattle1stEnd` |  |
| `TombSpiderNrvDemoBattle2ndStart` | `TombSpider::exeDemoBattle2ndStart` |  |
| `TombSpiderNrvAction2nd` | `TombSpider::exeAction2nd` |  |
| `TombSpiderNrvDemoDeath` | `TombSpider::exeDemoDeath` |  |
| `TombSpiderNrvWaitDemo` | `TombSpider::exeWaitDemo` |  |

## TombSpiderActionBattle

src/Game/Boss/TombSpiderActionBattle.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TombSpiderActionBattleNrvWait` | `TombSpiderActionBattle::exeWait` |  |
| `TombSpiderActionBattleNrvAttackStart` | `TombSpiderActionBattle::exeAttackStart` |  |
| `TombSpiderActionBattleNrvAttackLoop` | `TombSpiderActionBattle::exeAttackLoop` |  |
| `TombSpiderActionBattleNrvAttackEnd` | `TombSpiderActionBattle::exeAttackEnd` |  |
| `TombSpiderActionBattleNrvDamageEye` | `TombSpiderActionBattle::exeDamageEye` |  |
| `TombSpiderActionBattleNrvDamageHip` | `TombSpiderActionBattle::exeDamageHip` |  |
| `TombSpiderActionBattleNrvDamageGland` | `TombSpiderActionBattle::exeDamageGland` |  |
| `TombSpiderActionBattleNrvSwoon` | `TombSpiderActionBattle::exeSwoon` |  |
| `TombSpiderActionBattleNrvChanceStart` | `TombSpiderActionBattle::exeChanceStart` |  |
| `TombSpiderActionBattleNrvChanceWait` | `TombSpiderActionBattle::exeChanceWait` |  |
| `TombSpiderActionBattleNrvChanceDamage` | `TombSpiderActionBattle::exeChanceDamage` |  |
| `TombSpiderActionBattleNrvChanceDamageLast` | `TombSpiderActionBattle::exeChanceDamageLast` |  |
| `TombSpiderActionBattleNrvChanceEnd` | `TombSpiderActionBattle::exeChanceEnd` |  |
| `TombSpiderActionBattleNrvWaitDemo` | `TombSpiderActionBattle::exeWaitDemo` |  |

## TombSpiderActionCocoon

src/Game/Boss/TombSpiderActionCocoon.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TombSpiderActionCocoonNrvWait` | `TombSpiderActionCocoon::exeWait` |  |
| `TombSpiderActionCocoonNrvBreak` | `TombSpiderActionCocoon::exeBreak` |  |

## TombSpiderGland

src/Game/Boss/TombSpiderGland.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TombSpiderGlandNrvWait` | `TombSpiderGland::exeWait` |  |
| `TombSpiderGlandNrvActiveWait` | `TombSpiderGland::exeActiveWait` |  |
| `TombSpiderGlandNrvDamageStart` | `TombSpiderGland::exeDamageStart` |  |
| `TombSpiderGlandNrvDamageWait` | `TombSpiderGland::exeDamageWait` |  |

## TombSpiderStateSwoon

src/Game/Boss/TombSpiderStateSwoon.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TombSpiderStateSwoonNrvStart` | `TombSpiderStateSwoon::exeStart` |  |
| `TombSpiderStateSwoonNrvLoop` | `TombSpiderStateSwoon::exeLoop` |  |
| `TombSpiderStateSwoonNrvEnd` | `TombSpiderStateSwoon::exeEnd` |  |

## TombSpiderThreadAttacher

src/Game/Boss/TombSpiderThreadAttacher.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TombSpiderThreadAttacherNrvFree` | `TombSpiderThreadAttacher::exeFree` |  |
| `TombSpiderThreadAttacherNrvAttach` | `TombSpiderThreadAttacher::exeAttach` |  |

## TombSpiderVitalSpot

src/Game/Boss/TombSpiderVitalSpot.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TombSpiderVitalSpotNrvWait` | `TombSpiderVitalSpot::exeWait` |  |
| `TombSpiderVitalSpotNrvChanceWait` | `TombSpiderVitalSpot::exeChanceWait` |  |
| `TombSpiderVitalSpotNrvDamageStart` | `TombSpiderVitalSpot::exeDamageStart` |  |
| `TombSpiderVitalSpotNrvDamageWait` | `TombSpiderVitalSpot::exeDamageWait` |  |
| `TombSpiderVitalSpotNrvRecover` | `TombSpiderVitalSpot::exeRecover` |  |

## TrampleStar

src/Game/MapObj/TrampleStar.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TrampleStarNrvWait` | `TrampleStar::exeWait` |  |
| `TrampleStarNrvBindingCapture` | `TrampleStar::exeBindingCapture` |  |
| `TrampleStarNrvBindingShoot` | `TrampleStar::exeBindingShoot` |  |

## Trapeze

include/Game/Ride/Trapeze.hpp

| Nerve | Code | On leaving |
|---|---|---|
| `TrapezeNrvStop` | `Trapeze::exeStop` |  |
| `TrapezeNrvFree` | `Trapeze::exeFree` |  |
| `TrapezeNrvFreeInvalid` | `Trapeze::exeFreeInvalid` |  |
| `TrapezeNrvSwingWait` | `Trapeze::exeSwingWait` |  |
| `TrapezeNrvSwingSlideDownStart` | `Trapeze::exeSwingSlideDownStart` |  |
| `TrapezeNrvSwingSlideDown` | `Trapeze::exeSwingSlideDown` |  |
| `TrapezeNrvSwingFrontStart` | `Trapeze::exeSwingFrontStart` |  |
| `TrapezeNrvSwingFrontEnd` | `Trapeze::exeSwingFrontEnd` |  |
| `TrapezeNrvSwingBackStart` | `Trapeze::exeSwingBackStart` |  |
| `TrapezeNrvSwingBackEnd` | `Trapeze::exeSwingBackEnd` |  |
| `TrapezeNrvSlowSwingFrontStart` | `Trapeze::exeSwingFrontStart` |  |
| `TrapezeNrvSlowSwingFrontEnd` | `Trapeze::exeSwingFrontEnd` |  |
| `TrapezeNrvSlowSwingBackStart` | `Trapeze::exeSwingBackStart` |  |
| `TrapezeNrvSlowSwingBackEnd` | `Trapeze::exeSwingBackEnd` |  |

## TreasureBoxCracked

src/Game/MapObj/TreasureBoxCracked.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TreasureBoxCrackedNrvWait` | `TreasureBoxCracked::exeWait` |  |
| `TreasureBoxCrackedNrvOpen` | `TreasureBoxCracked::exeOpen` |  |
| `TreasureBoxCrackedNrvItemBound` | `TreasureBoxCracked::exeItemBound` |  |
| `TreasureBoxCrackedNrvOpenWait` | `TreasureBoxCracked::exeOpenWait` |  |
| `TreasureBoxCrackedNrvAlwaysOpen` | `TreasureBoxCracked::exeAlwaysOpen` |  |

## TreasureSpot

src/Game/MapObj/TreasureSpot.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TreasureSpotNrvWait` | `TreasureSpot::exeWait` |  |
| `TreasureSpotNrvEnd` | `TreasureSpot::exeEnd` |  |
| `TreasureSpotNrvSpout` | `TreasureSpot::exeSpout` |  |

## TrickRabbit

src/Game/NPC/TrickRabbit.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TrickRabbitNrvWaitStart` | `TrickRabbit::exeWaitStart` |  |
| `TrickRabbitNrvTryStartDemoRunnaway` | `TrickRabbit::exeTryStartDemoRunnaway` |  |
| `TrickRabbitNrvDemoRunaway` | `TrickRabbit::exeDemoRunaway` |  |
| `TrickRabbitNrvDemoJumpStart` | `TrickRabbit::exeDemoJumpStart` |  |
| `TrickRabbitNrvDemoJump` | `TrickRabbit::exeDemoJump` | `TrickRabbit::exeDemoJump` |
| `TrickRabbitNrvDemoEnd` | `TrickRabbit::exeDemoEnd` |  |
| `TrickRabbitNrvWait` | `TrickRabbit::exeWait` | `TrickRabbit::exeWait` |
| `TrickRabbitNrvRunaway` | `TrickRabbit::exeRunaway` |  |
| `TrickRabbitNrvJumpStart` | `TrickRabbit::exeJumpStart` |  |
| `TrickRabbitNrvJump` | `TrickRabbit::exeJump` | `TrickRabbit::exeJump` |
| `TrickRabbitNrvBrakeTurn` | `TrickRabbit::exeBrakeTurn` |  |
| `TrickRabbitNrvFallDown` | `TrickRabbit::exeFallDown` |  |
| `TrickRabbitNrvFallDownLand` | `TrickRabbit::exeFallDownLand` |  |
| `TrickRabbitNrvGetUpFromFallDown` | `TrickRabbit::exeGetUpFromFallDown` |  |
| `TrickRabbitNrvComebackRouteStart` | `TrickRabbit::exeComebackRouteStart` |  |
| `TrickRabbitNrvComebackRoute` | `TrickRabbit::exeComebackRoute` | `TrickRabbit::exeComebackRoute` |
| `TrickRabbitNrvTumble` | `TrickRabbit::exeTumble` |  |
| `TrickRabbitNrvGetUp` | `TrickRabbit::exeGetUp` |  |
| `TrickRabbitNrvCaught` | `TrickRabbit::exeCaught` |  |
| `TrickRabbitNrvWaitPowerStarDemo` | `TrickRabbit::exeWaitPowerStarDemo` |  |
| `TrickRabbitNrvGiveUp` | `TrickRabbit::exeGiveUp` |  |

## TrickRabbitFreeRun

src/Game/NPC/TrickRabbitFreeRun.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TrickRabbitFreeRunNrvWaitStart` | `TrickRabbitFreeRun::exeWaitStart` |  |
| `TrickRabbitFreeRunNrvTryDemo` | `TrickRabbitFreeRun::exeTryDemo` |  |
| `TrickRabbitFreeRunNrvRunawayStart` | `TrickRabbitFreeRun::exeRunawayStart` |  |
| `TrickRabbitFreeRunNrvRunaway` | `TrickRabbitFreeRun::exeRunaway` |  |
| `TrickRabbitFreeRunNrvBlowDamage` | `TrickRabbitFreeRun::exeBlowDamage` |  |
| `TrickRabbitFreeRunNrvCaught` | `TrickRabbitFreeRun::exeCaught` |  |
| `TrickRabbitFreeRunNrvPowerStarDemo` | `TrickRabbitFreeRun::exePowerStarDemo` |  |
| `TrickRabbitFreeRunNrvGiveUp` | `TrickRabbitFreeRun::exeGiveUp` |  |

## TrickRabbitSnow

src/Game/NPC/TrickRabbitSnow.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TrickRabbitSnowNrvWaitSnow` | `TrickRabbitSnow::exeWaitSnow` |  |
| `TrickRabbitSnowNrvStartHideSnow` | `TrickRabbitSnow::exeStartHideSnow` |  |
| `TrickRabbitSnowNrvVanish` | `TrickRabbitSnow::exeVanish` |  |
| `TrickRabbitSnowNrvHideSnow` | `TrickRabbitSnow::exeHideSnow` |  |
| `TrickRabbitSnowNrvAppearFromSnow` | `TrickRabbitSnow::exeAppearFromSnow` |  |
| `TrickRabbitSnowNrvRunaway` | `TrickRabbitSnow::exeRunaway` |  |
| `TrickRabbitSnowNrvBlowDamage` | `TrickRabbitSnow::exeBlowDamage` |  |
| `TrickRabbitSnowNrvCaught` | `TrickRabbitSnow::exeCaught` |  |
| `TrickRabbitSnowNrvGiveUp` | `TrickRabbitSnow::exeGiveUp` |  |

## TrickRabbitSnowCollect

src/Game/NPC/TrickRabbitSnowCollect.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TrickRabbitSnowCollectNrvTryDemo` | `TrickRabbitSnowCollect::exeTryDemo` |  |
| `TrickRabbitSnowCollectNrvWait` | `TrickRabbitSnowCollect::exeWait` |  |
| `TrickRabbitSnowCollectNrvStartWipeOut` | `TrickRabbitSnowCollect::exeStartWipeOut` |  |
| `TrickRabbitSnowCollectNrvStartWipeIn` | `TrickRabbitSnowCollect::exeStartWipeIn` |  |
| `TrickRabbitSnowCollectNrvStartTalk` | `TrickRabbitSnowCollect::exeStartTalk` |  |
| `TrickRabbitSnowCollectNrvWaitHideSnow` | `TrickRabbitSnowCollect::exeWaitHideSnow` |  |
| `TrickRabbitSnowCollectNrvStart` | `TrickRabbitSnowCollect::exeStart` |  |
| `TrickRabbitSnowCollectNrvFailedWipeOut` | `TrickRabbitSnowCollect::exeFailedWipeOut` |  |
| `TrickRabbitSnowCollectNrvFailedWipeIn` | `TrickRabbitSnowCollect::exeFailedWipeIn` |  |
| `TrickRabbitSnowCollectNrvFailedTalk` | `TrickRabbitSnowCollect::exeFailedTalk` |  |
| `TrickRabbitSnowCollectNrvSuccessWipeOut` | `TrickRabbitSnowCollect::exeSuccessWipeOut` |  |
| `TrickRabbitSnowCollectNrvSuccessWipeIn` | `TrickRabbitSnowCollect::exeSuccessWipeIn` |  |
| `TrickRabbitSnowCollectNrvSuccessTalk` | `TrickRabbitSnowCollect::exeSuccessTalk` |  |
| `TrickRabbitSnowCollectNrvTakeOutStar` | `TrickRabbitSnowCollect::exeTakeOutStar` |  |
| `TrickRabbitSnowCollectNrvAppearPowerStar` | `TrickRabbitSnowCollect::exeAppearPowerStar` |  |
| `TrickRabbitSnowCollectNrvEnd` | `TrickRabbitSnowCollect::exeEnd` |  |

## TripodBoss

src/Game/Boss/TripodBoss.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossNrvTryStartDemo` | `TripodBoss::exeTryStartDemo` |  |
| `TripodBossNrvNonActive` | `TripodBoss::exeNonActive` |  |
| `TripodBossNrvWait` | `TripodBoss::exeWait` |  |
| `TripodBossNrvStep` | `TripodBoss::exeStep` |  |
| `TripodBossNrvWaitStep` | `TripodBoss::exeWaitStep` |  |
| `TripodBossNrvDamage` | `TripodBoss::exeDamage` |  |
| `TripodBossNrvChangeSequence` | `TripodBoss::exeChangeSequence` |  |
| `TripodBossNrvLeaveLegOutOfPlayer` | `TripodBoss::exeLeaveLegOutOfPlayer` |  |
| `TripodBossNrvStartDemo` | `TripodBoss::exeStartDemo` |  |
| `TripodBossNrvDamageDemo` | `TripodBoss::exeDamageDemo` |  |
| `TripodBossNrvPainDemo` | `TripodBoss::exePainDemo` |  |
| `TripodBossNrvBreakDownDemo` | `TripodBoss::exeBreakDownDemo` |  |
| `TripodBossNrvExplosionDemo` | `TripodBoss::exeExplosionDemo` |  |

## TripodBossBreakMovement

src/Game/Boss/TripodBossBreakMovement.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossBreakMovementNrvWait` | `TripodBossBreakMovement::exeWait` |  |
| `TripodBossBreakMovementNrvStartBreak` | `TripodBossBreakMovement::exeStartBreak` |  |
| `TripodBossBreakMovementNrvBreak` | `TripodBossBreakMovement::exeBreak` |  |

## TripodBossCoin

src/Game/Boss/TripodBossCoin.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossCoinNrvNonActive` | `TripodBossCoin::exeNonActive` |  |
| `TripodBossCoinNrvActive` | `TripodBossCoin::exeActive` |  |
| `TripodBossCoinNrvEnd` | `TripodBossCoin::exeEnd` |  |

## TripodBossCore

src/Game/Boss/TripodBossCore.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossCoreNrvNonActive` | `TripodBossCore::exeNonActive` |  |
| `TripodBossCoreNrvWait` | `TripodBossCore::exeWait` |  |
| `TripodBossCoreNrvDamageDemo` | `TripodBossCore::exeDamageDemo` |  |
| `TripodBossCoreNrvWarning` | `TripodBossCore::exeWarning` |  |
| `TripodBossCoreNrvBreak` | `TripodBossCore::exeBreak` |  |

## TripodBossFixParts

src/Game/Boss/TripodBossFixParts.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossFixPartsNrvNonActive` | `TripodBossFixParts::exeNonActive` |  |
| `TripodBossFixPartsNrvStartDemo` | `TripodBossFixParts::exeStartDemo` |  |
| `TripodBossFixPartsNrvWait` | `TripodBossFixParts::exeWait` |  |
| `TripodBossFixPartsNrvBreak` | `TripodBossFixParts::exeBreak` |  |

## TripodBossGuardWall

src/Game/Boss/TripodBossGuardWall.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossGuardWallNrvWait` | `TripodBossGuardWall::exeWait` |  |
| `TripodBossGuardWallNrvTryDemo` | `TripodBossGuardWall::exeTryDemo` |  |
| `TripodBossGuardWallNrvDemo` | `TripodBossGuardWall::exeDemo` |  |
| `TripodBossGuardWallNrvRotate` | `TripodBossGuardWall::exeRotate` |  |

## TripodBossGuardWallPart

src/Game/Boss/TripodBossGuardWallPart.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossGuardWallPartNrvNonActive` | `TripodBossGuardWallPart::exeNonActive` |  |
| `TripodBossGuardWallPartNrvDemo` | `TripodBossGuardWallPart::exeDemo` |  |
| `TripodBossGuardWallPartNrvActive` | `TripodBossGuardWallPart::exeActive` |  |
| `TripodBossGuardWallPartNrvBreak` | `TripodBossGuardWallPart::exeBreak` |  |
| `TripodBossGuardWallPartNrvRepair` | `TripodBossGuardWallPart::exeRepair` |  |

## TripodBossKillerGenerater

src/Game/Boss/TripodBossKillerGenerater.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossKillerGeneraterNrvNonActive` | `TripodBossKillerGenerater::exeNonActive` |  |
| `TripodBossKillerGeneraterNrvHide` | `TripodBossKillerGenerater::exeHide` |  |
| `TripodBossKillerGeneraterNrvShowDemo` | `TripodBossKillerGenerater::exeShowDemo` |  |
| `TripodBossKillerGeneraterNrvReady` | `TripodBossKillerGenerater::exeReady` |  |
| `TripodBossKillerGeneraterNrvShootSetting` | `TripodBossKillerGenerater::exeShootSetting` |  |
| `TripodBossKillerGeneraterNrvShoot` | `TripodBossKillerGenerater::exeShoot` |  |
| `TripodBossKillerGeneraterNrvCoolDown` | `TripodBossKillerGenerater::exeCoolDown` |  |
| `TripodBossKillerGeneraterNrvBreak` | `TripodBossKillerGenerater::exeBreak` |  |
| `TripodBossKillerGeneraterNrvStop` | `TripodBossKillerGenerater::exeStop` |  |
| `TripodBossKillerGeneraterNrvRestart` | `TripodBossKillerGenerater::exeRestart` |  |

## TripodBossKinokoOneUp

src/Game/Boss/TripodBossKinokoOneUp.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossKinokoOneUpNrvActive` | `TripodBossKinokoOneUp::exeActive` |  |
| `TripodBossKinokoOneUpNrvEnd` | `TripodBossKinokoOneUp::exeEnd` |  |

## TripodBossLeg

src/Game/Boss/TripodBossLeg.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossLegNrvHold` | `TripodBossLeg::exeHold` |  |
| `TripodBossLegNrvDemo` | `TripodBossLeg::exeDemo` |  |
| `TripodBossLegNrvMove` | `TripodBossLeg::exeMove` |  |
| `TripodBossLegNrvUp` | `TripodBossLeg::exeUp` |  |
| `TripodBossLegNrvLeaveOut` | `TripodBossLeg::exeLeaveOut` |  |
| `TripodBossLegNrvMoveToLandingPos` | `TripodBossLeg::exeMoveToLandingPos` |  |
| `TripodBossLegNrvStampSign` | `TripodBossLeg::exeStampSign` |  |
| `TripodBossLegNrvLanding` | `TripodBossLeg::exeLanding` |  |
| `TripodBossLegNrvDamageVibration` | `TripodBossLeg::exeDamageVibration` |  |
| `TripodBossLegNrvDamage` | `TripodBossLeg::exeDamage` |  |
| `TripodBossLegNrvBreak` | `TripodBossLeg::exeBreak` |  |

## TripodBossShell

src/Game/Boss/TripodBossShell.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TripodBossShellNrvNonActive` | `TripodBossShell::exeNonActive` |  |
| `TripodBossShellNrvWait` | `TripodBossShell::exeWait` |  |
| `TripodBossShellNrvBreak` | `TripodBossShell::exeBreak` |  |

## Tsukidashikun

src/Game/MapObj/Tsukidashikun.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `TsukidashikunNrvRelax` | `Tsukidashikun::exeRelax` |  |
| `TsukidashikunNrvWaitBack` | `Tsukidashikun::exeWait` |  |
| `TsukidashikunNrvWaitForward` | `Tsukidashikun::exeWait` |  |
| `TsukidashikunNrvSignBack` | `Tsukidashikun::exeSign` |  |
| `TsukidashikunNrvSignForward` | `Tsukidashikun::exeSign` |  |
| `TsukidashikunNrvMoveBack` | `Tsukidashikun::exeMove` |  |
| `TsukidashikunNrvMoveForward` | `Tsukidashikun::exeMove` |  |

## TypicalDoor

src/Game/MapObj/TypicalDoor.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeClose` | `TypicalDoor::exeClose` |  |
| `HostTypeCloseForDemo` | `TypicalDoor::exeClose` |  |
| `HostTypeOpen` | `TypicalDoor::exeOpen` |  |

## UFOBase

src/Game/MapObj/UFOBase.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `UFOBaseNrvWait` | `UFOBase::exeWait` |  |
| `UFOBaseNrvWaitForPlayerOn` | `UFOBase::exeWaitForPlayerOn` |  |
| `UFOBaseNrvMove` | `UFOBase::exeMove` |  |
| `UFOBaseNrvBreak` | `UFOBase::exeBreak` |  |

## UFOKinoko

src/Game/MapObj/UFOKinoko.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `UFOKinoko::exeWait` |  |
| `HostTypeMove` | `UFOKinoko::exeMove` |  |
| `HostTypeStop` | `UFOKinoko::exeStop` |  |

## Unizo

src/Game/Enemy/Unizo.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `UnizoNrvWait` | `Unizo::exeWait` |  |
| `UnizoNrvJump` | `Unizo::exeJump` |  |
| `UnizoNrvChase` | `Unizo::exeChase` |  |
| `UnizoNrvAttack` | `Unizo::exeAttack` |  |
| `UnizoNrvCollidePlayer` | `Unizo::exeCollidePlayer` |  |
| `UnizoNrvCollideEnemy` | `Unizo::exeCollideEnemy` |  |
| `UnizoNrvBreak` | `Unizo::exeBreak` |  |
| `UnizoNrvFireDown` | `Unizo::exeFireDown` |  |
| `UnizoNrvJumpDown` | `Unizo::exeJumpDown` |  |
| `UnizoNrvPointing` | `Unizo::exePointing` | `Unizo::exePointing` |

## UnizoLauncher

src/Game/Enemy/UnizoLauncher.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `UnizoLauncherNrvWait` | `UnizoLauncher::exeWait` |  |
| `UnizoLauncherNrvLaunch` | `UnizoLauncher::exeLaunch` |  |

## ValveSwitch

src/Game/MapObj/ValveSwitch.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `ValveSwitchNrvWait` | `ValveSwitch::exeWait` |  |
| `ValveSwitchNrvAdjust` | `ValveSwitch::exeAdjust` |  |
| `ValveSwitchNrvValve` | `ValveSwitch::exeValve` |  |
| `ValveSwitchNrvEnd` | `ValveSwitch::exeEnd` |  |

## WalkerStateBindStarPointer

src/Game/Enemy/WalkerStateBindStarPointer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WalkerStateBindStarPointerNrvBind` | `WalkerStateBindStarPointer::exeBind` |  |

## WalkerStateBlowDamage

src/Game/Enemy/WalkerStateBlowDamage.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WalkerStateBlowDamageNrvBlow` | `WalkerStateBlowDamage::exeBlow` |  |
| `WalkerStateBlowDamageNrvBlowLand` | `WalkerStateBlowDamage::exeBlowLand` |  |

## WalkerStateChase

src/Game/Enemy/WalkerStateChase.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WalkerStateChaseNrvStart` | `WalkerStateChase::exeStart` |  |
| `WalkerStateChaseNrvEnd` | `WalkerStateChase::exeEnd` |  |

## WalkerStateFindPlayer

src/Game/Enemy/WalkerStateFindPlayer.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WalkerStateFindPlayerNrvFind` | `WalkerStateFindPlayer::exeFind` |  |
| `WalkerStateFindPlayerNrvFindJumpStart` | `WalkerStateFindPlayer::exeFindJumpStart` |  |
| `WalkerStateFindPlayerNrvFindJump` | `WalkerStateFindPlayer::exeFindJump` |  |
| `WalkerStateFindPlayerNrvFindJumpEnd` | `WalkerStateFindPlayer::exeFindJumpEnd` |  |

## WalkerStateRunaway

src/Game/Enemy/WalkerStateRunaway.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WalkerStateRunawayNrvWait` | `WalkerStateRunaway::exeWait` |  |
| `WalkerStateRunawayNrvRunaway` | `WalkerStateRunaway::exeRunaway` |  |
| `WalkerStateRunawayNrvWallJump` | `WalkerStateRunaway::exeWallJump` |  |

## WalkerStateStagger

src/Game/Enemy/WalkerStateStagger.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WalkerStateStaggerNrvStagger` | `WalkerStateStagger::exeStagger` |  |
| `WalkerStateStaggerNrvStaggerEnd` | `WalkerStateStagger::exeStaggerEnd` |  |

## WalkerStateWander

src/Game/Enemy/WalkerStateWander.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WalkerStateWanderNrvWait` | `WalkerStateWander::exeWait` |  |
| `WalkerStateWanderNrvWalk` | `WalkerStateWander::exeWalk` |  |

## WatchTowerRotateStep

src/Game/MapObj/WatchTowerRotateStep.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WatchTowerRotateStepNrvWait` | `WatchTowerRotateStep::exeWait` |  |
| `WatchTowerRotateStepNrvMoveStart` | `WatchTowerRotateStep::exeMoveStart` |  |
| `WatchTowerRotateStepNrvMove` | `WatchTowerRotateStep::exeMove` |  |

## WaterBazooka

src/Game/Enemy/WaterBazooka.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WaterBazookaNrvWaitForBattle` | `WaterBazooka::exeWaitForBattle` |  |
| `WaterBazookaNrvWait` | `WaterBazooka::exeWait` |  |
| `WaterBazookaNrvAim` | `WaterBazooka::exeAim` |  |
| `WaterBazookaNrvAimEnd` | `WaterBazooka::exeAimEnd` |  |
| `WaterBazookaNrvShot` | `WaterBazooka::exeShot` |  |
| `WaterBazookaNrvShotNoMotion` | `WaterBazooka::exeShotNoMotion` |  |
| `WaterBazookaNrvTire` | `WaterBazooka::exeTire` |  |
| `WaterBazookaNrvDemoCrackCapsule` | `WaterBazooka::exeDemoCrackCapsule` |  |
| `WaterBazookaNrvDemoAnger` | `WaterBazooka::exeDemoAnger` |  |
| `WaterBazookaNrvDemoBreakWait` | `WaterBazooka::exeDemoBreakWait` |  |
| `WaterBazookaNrvDemoBreakSign` | `WaterBazooka::exeDemoBreakSign` |  |
| `WaterBazookaNrvDemoBreakExplosion` | `WaterBazooka::exeDemoBreakExplosion` |  |
| `WaterBazookaNrvDemoBreakEnd` | `WaterBazooka::exeDemoBreakEnd` |  |
| `WaterBazookaNrvWaitForLaugh` | `WaterBazooka::exeWaitForLaugh` |  |
| `WaterBazookaNrvPanic` | `WaterBazooka::exePanic` |  |
| `WaterBazookaNrvStorm` | `WaterBazooka::exeStorm` | `WaterBazooka::exeStorm` |

## WaterBazookaCapsule

src/Game/Enemy/WaterBazookaCapsule.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WaterBazookaCapsuleNrvNormal` | `WaterBazookaCapsule::exeNormal` |  |
| `WaterBazookaCapsuleNrvCrack` | `WaterBazookaCapsule::exeCrack` |  |
| `WaterBazookaCapsuleNrvBreak` | `WaterBazookaCapsule::exeBreak` |  |

## WaterCameraFilter

src/Game/Screen/WaterCameraFilter.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WaterCameraFilterNrvAir` | `WaterCameraFilter::exeAir` |  |
| `WaterCameraFilterNrvAirToWater` | `WaterCameraFilter::exeAirToWater` |  |
| `WaterCameraFilterNrvWater` | `WaterCameraFilter::exeWater` |  |
| `WaterCameraFilterNrvWaterToAir` | `WaterCameraFilter::exeWaterToAir` |  |

## WaterfallCaveCover

src/Game/MapObj/WaterfallCaveCover.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `HostTypeWait` | `WaterfallCaveCover::exeWait` |  |
| `HostTypeBreak` | `WaterfallCaveCover::exeBreak` |  |

## WaterLeakPipe

src/Game/MapObj/WaterLeakPipe.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WaterLeakPipeNrvWait` | `WaterLeakPipe::exeWait` |  |
| `WaterLeakPipeNrvFreeze` | `WaterLeakPipe::exeFreeze` |  |

## WaterPressureBullet

src/Game/MapObj/WaterPressureBullet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WaterPressureBulletNrvFly` | `WaterPressureBullet::exeFly` |  |
| `WaterPressureBulletNrvSpinKill` | `WaterPressureBullet::exeSpinKill` |  |

## WaterRoad

src/Game/Map/WaterRoad.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WaterRoadNrvWait` | `WaterRoad::exeWait` |  |
| `WaterRoadNrvWaitInvalid` | `WaterRoad::exeWaitInvalid` |  |
| `WaterRoadNrvRideStart` | `WaterRoad::exeRideStart` |  |
| `WaterRoadNrvRideWait` | `WaterRoad::exeRideWait` |  |
| `WaterRoadNrvRideSpin` | `WaterRoad::exeRideSpin` |  |
| `WaterRoadNrvDemoStart` | `WaterRoad::exeDemoStart` |  |
| `WaterRoadNrvDemoGrowUp` | `WaterRoad::exeDemoGrowUp` |  |
| `WaterRoadNrvDemoWaitEnd` | `WaterRoad::exeDemoWaitEnd` |  |
| `WaterRoadNrvDisappear` | `WaterRoad::exeDisappear` |  |

## WipeGameOver

src/Game/Screen/WipeGameOver.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WipeGameOverWait` | `WipeGameOver::exeWait` |  |
| `WipeGameOverActive` | `WipeGameOver::exeActive` |  |

## WipeKoopa

src/Game/Screen/WipeKoopa.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WipeKoopaNrvWait` | `WipeKoopa::exeWait` |  |
| `WipeKoopaNrvWipeOut` | `WipeKoopa::exeWipeOut` |  |
| `WipeKoopaNrvClose` | `WipeKoopa::exeClose` |  |

## WipeRing

src/Game/Screen/WipeRing.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WipeRingNrvClose` | `WipeRing::exeClose` |  |
| `WipeRingNrvWipeIn` | `WipeRing::exeWipeIn` |  |
| `WipeRingNrvOpen` | `WipeRing::exeOpen` |  |
| `WipeRingNrvWipeOut` | `WipeRing::exeWipeOut` |  |

## WoodBox

src/Game/MapObj/WoodBox.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WoodBoxNrvWait` | `WoodBox::exeWait` |  |
| `WoodBoxNrvHit` | `WoodBox::exeHit` |  |
| `WoodBoxNrvKilled` | `WoodBox::exeKilled` |  |

## WormEatenPlanet

src/Game/MapObj/WormEatenPlanet.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `WormEatenPlanetNrvFirstWait` | `WormEatenPlanet::exeFirstWait` |  |
| `WormEatenPlanetNrvSecondWait` | `WormEatenPlanet::exeSecondWait` |  |
| `WormEatenPlanetNrvThirdWait` | `WormEatenPlanet::exeThirdWait` |  |
| `WormEatenPlanetNrvFourthWait` | `WormEatenPlanet::exeFourthWait` |  |

## YesNoController

src/Game/Screen/YesNoController.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `YesNoControllerNrvSelecting` | `YesNoController::exeSelecting` |  |
| `YesNoControllerNrvDecided` | `YesNoController::exeDecided` |  |
| `YesNoControllerNrvDisappear` | `YesNoController::exeDisappear` |  |
| `YesNoControllerNrvSelected` | `YesNoController::exeSelected` |  |
| `YesNoControllerNrvNotSelected` | `YesNoController::exeNotSelected` |  |

## YesNoLayout

src/Game/Screen/YesNoLayout.cpp

| Nerve | Code | On leaving |
|---|---|---|
| `YesNoLayoutNrvSelecting` | `YesNoLayout::exeSelecting` |  |

## Further reading

* [Nerves](nerves.md)
