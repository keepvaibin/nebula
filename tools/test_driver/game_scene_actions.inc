// SPDX-License-Identifier: GPL-3.0-only
// What a hidden test run asks of GameScene::draw each frame.
    NebulaTestAction action;
    if (NebulaHost::testSceneFrame(&action)) {
        static char sStage[sizeof(action.stage)];  // the stage change keeps a pointer to the name
        if (action.kind == NEBULA_TEST_CHANGE_STAGE) {
            nebula_snprintf(sStage, sizeof(sStage), "%s", action.stage);
            MR::requestChangeStageInGameMoving(sStage, action.scenario);
        } else if (action.kind == NEBULA_TEST_CIRCLE_WIPE) {
            if (action.scenario != 0) MR::openWipeCircle(60);
            else MR::closeWipeCircle(60);
            nebula_log("test: circle %s, 60 frames\n", action.scenario != 0 ? "open" : "close");
        } else if (action.kind == NEBULA_TEST_PLACE_PLAYER) {
            MR::setPlayerPos(Vec3f(action.position[0], action.position[1], action.position[2]));
        } else if (action.kind == NEBULA_TEST_KILL_PLAYER) {
            MR::forceKillPlayerByAbyss();
        } else if (action.kind == NEBULA_TEST_GET_STAR || action.kind == NEBULA_TEST_GET_GRAND_STAR) {
            // The title's stage is no galaxy (a run that reached a game over): no star belongs to it.
            if (!MR::isStageFileSelect()) {
                sPendingStar = action.kind;
                sPendingStarId = action.scenario;
            }
        } else if (action.kind == NEBULA_TEST_SET_PLAYER) {
            // The file played is user 1's Mario or Luigi file.
            GameDataFunction::getCurrentGameDataHolder()->mIsMario = action.scenario == 0;
            GameDataFunction::getCurrentGameDataHolder()->mUser = 1;
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
                nebula_snprintf(sStage, sizeof(sStage), "%s", action.stage);
                MR::requestChangeStageInGameMoving(sStage, 1);
            } else {
                nebula_snprintf(sStage, sizeof(sStage), "%s", MR::getCurrentStageName());
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
                // The file played as user 1's Luigi file from here (the stage built next reads it).
                GameDataFunction::getCurrentGameDataHolder()->mIsMario = false;
                GameDataFunction::getCurrentGameDataHolder()->mUser = 1;
            } else if (strcmp(action.stage, "SpinAndStarPointer") == 0) {
                // The story's grant of the spin and the pointer (the Gateway's Luma), which an early save lacks.
                MR::onGameEventFlagEnableToSpinAndStarPointer();
            } else if (equals != nullptr) {
                static char sName[sizeof(action.stage)];
                nebula_snprintf(sName, sizeof(sName), "%.*s", (int)(equals - action.stage), action.stage);
                GameDataFunction::setGameEventValue(sName, (u16)atoi(equals + 1));
            } else if (const GameEventFlag* pFlag = GameEventFlagTable::findFlag(action.stage)) {
                GameDataFunction::getCurrentGameDataHolder()->mEventFlagChecker->getChunk()->set(pFlag, true);
            } else {
                nebula_log("test: no event flag %s\n", action.stage);
            }
        } else if (action.kind == NEBULA_TEST_LIST_ACTORS) {
            AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
            for (s32 i = 0; pGroup != nullptr && i < pGroup->getObjNum(); ++i) {
                LiveActor* pActor = pGroup->getActor(i);
                if (pActor == nullptr || pActor->mModelManager == nullptr || MR::isDead(pActor)) {
                    continue;
                }
                // With the animation it plays (none: no BCK started), its frame and rate; whether its model is shown;
                // whether the game clips it.
                const char* pBck = MR::getPlayingBckName(pActor);
                nebula_log("actor %s %.0f %.0f %.0f bck=%s frame=%.1f rate=%.2f stopped=%d hidden=%d clipped=%d\n",
                         MR::getModelResName(pActor), pActor->mPosition.x, pActor->mPosition.y, pActor->mPosition.z,
                         pBck != nullptr ? pBck : "none", pBck != nullptr ? MR::getBckFrame(pActor) : 0.0f,
                         pBck != nullptr ? MR::getBckRate(pActor) : 0.0f, pBck != nullptr && MR::isBckStopped(pActor) ? 1 : 0,
                         MR::isHiddenModel(pActor) ? 1 : 0, MR::isClipped(pActor) ? 1 : 0);
            }
        } else if (action.kind == NEBULA_TEST_PLACE_PLAYER_WATER || action.kind == NEBULA_TEST_PLACE_PLAYER_ACTOR) {
            testWalkPlace(action.kind, action.scenario, action.position[0] != 0.0f);
        } else if (action.kind == NEBULA_TEST_APPEAR_ACTOR) {
            AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
            for (s32 i = 0; pGroup != nullptr && i < pGroup->getObjNum(); ++i) {
                LiveActor* pActor = pGroup->getActor(i);
                if (pActor != nullptr && pActor->mModelManager != nullptr && MR::isDead(pActor) &&
                    MR::isEqualStringCase(MR::getModelResName(pActor), action.stage)) {
                    nebula_log("test: appear %s %.0f %.0f %.0f\n", action.stage, pActor->mPosition.x, pActor->mPosition.y,
                             pActor->mPosition.z);
                    // An actor a stage switch holds back follows that switch.
                    if (MR::isValidSwitchAppear(pActor)) {
                        StageSwitchFunction::onSwitchBySwitchIdInfo(*pActor->mStageSwitchCtrl->mSwitchAppear);
                    } else {
                        pActor->appear();
                    }
                }
            }
        } else if (action.kind == NEBULA_TEST_LIST_MATERIALS) {
            AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
            for (s32 i = 0; pGroup != nullptr && i < pGroup->getObjNum(); ++i) {
                LiveActor* pActor = pGroup->getActor(i);
                if (pActor != nullptr && pActor->mModelManager != nullptr &&
                    MR::isEqualStringCase(MR::getModelResName(pActor), action.stage)) {
                    ModelData* pData = MR::getModelData(pActor);
                    for (u16 m = 0; pData != nullptr && m < pData->getMaterialCount(); ++m) {
                        Material* pMaterial = pData->getMaterialAt(m);
                        nebula_log("test: material %s %u %s\n", action.stage, m, pData->getMaterialName(m));
                        for (u32 c = 0; c < pMaterial->getCoordCount(); ++c) {
                            MaterialCoord* pCoord = pMaterial->getCoord(c);
                            CoordMatrix* pMtx = pMaterial->getCoordMatrix(c);
                            nebula_log("test: coordinate %u %u type %u source %u matrix %u info %u projection %u used %u\n", m, c,
                                         pCoord->mType, pCoord->mSource, pCoord->mMatrix,
                                         pMtx != nullptr ? pMtx->getInfo().mInfo : 255u,
                                         pMtx != nullptr ? pMtx->getInfo().mProjection : 255u,
                                         pMaterial->usesCoord(c) ? 1u : 0u);
                        }
                    }
                    break;
                }
            }
        } else if (action.kind == NEBULA_TEST_KOOPA_HIT) {
            AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
            for (s32 i = 0; pGroup != nullptr && i < pGroup->getObjNum(); ++i) {
                LiveActor* pActor = pGroup->getActor(i);
                if (pActor != nullptr && pActor->mModelManager != nullptr && !MR::isDead(pActor) &&
                    MR::isEqualStringCase(MR::getModelResName(pActor), "koopa")) {
                    MR::incPlayerLife(8);
                    static_cast< Koopa* >(pActor)->mSequencer->testHit(MarioAccess::getPlayerActor()->getSensor("body"));
                }
            }
        } else if (action.kind == NEBULA_TEST_KILL_ACTOR) {
            // Killed as their defeat ends (LiveActor::kill): what they leave appears, a KuriboChief's key.
            AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
            for (s32 i = 0; pGroup != nullptr && i < pGroup->getObjNum(); ++i) {
                LiveActor* pActor = pGroup->getActor(i);
                if (pActor != nullptr && pActor->mModelManager != nullptr && !MR::isDead(pActor) &&
                    MR::isEqualStringCase(MR::getModelResName(pActor), action.stage)) {
                    nebula_log("test: kill %s %.0f %.0f %.0f\n", action.stage, pActor->mPosition.x, pActor->mPosition.y,
                             pActor->mPosition.z);
                    pActor->kill();
                }
            }
        } else if (action.kind == NEBULA_TEST_DOME_LAUNCH) {
            // What the galaxy selector's confirm asks (AstroDomeGalaxySelector::exeGalaxyConfirm), the player its requester.
            const bool started = MR::requestStartTimeKeepDemoMarioPuppetable(MarioAccess::getPlayerActor(), "マリオ飛び出し", nullptr,
                                                                             nullptr, nullptr);
            nebula_log("test: dome launch %s\n", started ? "started" : "queued");
        } else if (action.kind == NEBULA_TEST_SHAKE_CAMERA) {
            // The shaker keeps its owner only to stop it; nothing stops this one.
            MR::shakeCameraInfinity(nullptr, 0.2f, 2.0f);
        } else if (action.kind == NEBULA_TEST_START_MOVIE) {
            if (MR::isExistSceneObj(SceneObj_MoviePlayingSequenceHolder) &&
                MR::getSceneObj< MoviePlayingSequenceHolder >(SceneObj_MoviePlayingSequenceHolder)->getSequence(action.scenario) != nullptr) {
                MR::startMovie(action.scenario);
            } else {
                nebula_log("test: no movie sequence in this stage\n");
            }
        }
    }
