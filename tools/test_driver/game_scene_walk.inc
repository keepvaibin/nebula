// SPDX-License-Identifier: GPL-3.0-only
// GameScene.cpp's anonymous namespace in a test build.
    // Tests (GALAXY_TEST_WALK): the player placed in each body of water, or at each live actor, to play there.
    bool sTestWalkActive = false;
    u32 sTestSwimFrames = 0;

    // A point inside one of the scene's bodies of water: the oceans first, then the water areas.
    bool testWaterPoint(s32 index, TVec3f* pPos, const char** ppKind, s32* pCount) {
        s32 count = 0;
        bool found = false;
        const auto offer = [&](const TVec3f& rPos, const char* pKind) {
            if (count == index) {
                *pPos = rPos;
                *ppKind = pKind;
                found = true;
            }
            ++count;
        };
        const auto first = [&](s32 total) {
            *pCount = total;
            index = total > 0 ? index % total : 0;
        };
        WaterAreaHolder* pHolder = MR::isExistSceneObj(SceneObj_WaterAreaHolder) ? MR::getSceneObj< WaterAreaHolder >(SceneObj_WaterAreaHolder) : nullptr;
        AreaObjMgr* pAreas = MR::getAreaObjManager("Water");
        s32 total = pAreas != nullptr ? pAreas->getNumAreaObj() : 0;
        if (pHolder != nullptr) {
            total += pHolder->mNumOceanBowls + pHolder->mNumOceanSpheres + pHolder->mNumOceanRings * 4;
        }
        first(total);
        if (pHolder != nullptr) {
            for (s32 i = 0; i < pHolder->mNumOceanBowls; ++i) {
                OceanBowl* pBowl = pHolder->mOceanBowls[i];
                TVec3f pos(pBowl->mPosition);
                pos -= pBowl->mUp * (pBowl->mRadius * 0.25f < 400.0f ? pBowl->mRadius * 0.25f : 400.0f);
                offer(pos, "bowl");
            }
            for (s32 i = 0; i < pHolder->mNumOceanSpheres; ++i) {
                OceanSphere* pSphere = pHolder->mOceanSpheres[i];
                TVec3f pos(pSphere->mPosition);
                pos.y += pSphere->mRadius * 0.5f;
                offer(pos, "sphere");
            }
            for (s32 i = 0; i < pHolder->mNumOceanRings; ++i) {
                OceanRing* pRing = pHolder->mOceanRings[i];
                for (s32 j = 0; j < 4; ++j) {
                    WaterPoint* pPoint = pRing->getPoint(j * pRing->mWaterPointNum / 4);
                    TVec3f pos(pPoint->mPosition);
                    pos -= pPoint->mUpVec * 150.0f;
                    offer(pos, "ring");
                }
            }
        }
        for (s32 i = 0; pAreas != nullptr && i < pAreas->getNumAreaObj(); ++i) {
            AreaObj* pArea = pAreas->getAreaObj(i);
            TVec3f pos(0.0f, 0.0f, 0.0f);
            switch (pArea->mFormType) {
            case AreaForm::Type_Cube1:
            case AreaForm::Type_Cube2:
                static_cast< AreaFormCube* >(pArea->mForm)->calcWorldPos(&pos);
                break;
            case AreaForm::Type_Sphere:
                static_cast< AreaFormSphere* >(pArea->mForm)->calcPos(&pos);
                break;
            case AreaForm::Type_Cylinder:
                static_cast< AreaFormCylinder* >(pArea->mForm)->calcCenterPos(&pos);
                break;
            default:
                pos = static_cast< AreaFormBowl* >(pArea->mForm)->mTranslation;
                break;
            }
            // The form's origin may be its base: a little up each try until the area holds it.
            for (s32 up = 0; up < 8 && !pArea->isInVolume(pos); ++up) {
                pos.y += 150.0f;
            }
            offer(pos, "area");
        }
        return found;
    }

    void testWalkPlace(s32 kind, s32 index, bool forms) {
        static s32 sLastIndex = -1;
        const bool fresh = index != sLastIndex || !sTestWalkActive;
        sTestWalkActive = true;
        sLastIndex = index;
        if (fresh && forms) {
            // Normal, rainbow, fire, ice, bee, spring, Boo, flying, tornado (PlayerMode).
            static const u16 kForms[] = {0, 1, 2, 3, 4, 5, 6, 7, 9};
            const u16 form = kForms[static_cast< u32 >(index) % (sizeof(kForms) / sizeof(kForms[0]))];
            // Only what this stage loaded: a form's models come with the stage's item, as in play.
            MarioActor* pPlayer = MarioAccess::getPlayerActor();
            const bool loaded = form == PlayerMode_Invincible ? pPlayer->_9C8 != nullptr
                                : form == PlayerMode_Ice      ? pPlayer->_9C0 != nullptr
                                : form == PlayerMode_Bee      ? pPlayer->_9E4 != nullptr
                                : form == PlayerMode_Teresa   ? pPlayer->_9A4 != nullptr
                                : form == PlayerMode_Hopper   ? pPlayer->_A00 != nullptr
                                : form == PlayerMode_Tornado  ? pPlayer->mTornadoMario != nullptr
                                                              : true;
            if (loaded) {
                JUTNativeLog("test: form %u\n", form);
                pPlayer->setPlayerMode(form, true);
            }
        }
        if (kind == NEBULA_TEST_PLACE_PLAYER_WATER) {
            TVec3f pos;
            const char* pKind = "";
            s32 count = 0;
            if (testWaterPoint(index, &pos, &pKind, &count)) {
                if (fresh) {
                    JUTNativeLog("test: water %d/%d %s %.0f %.0f %.0f in %d swam %u\n", count > 0 ? index % count : 0, count, pKind, pos.x, pos.y, pos.z, MR::isInWater(pos) ? 1 : 0, sTestSwimFrames);
                }
                MR::setPlayerPos(pos);
            } else if (fresh) {
                JUTNativeLog("test: water none swam %u\n", sTestSwimFrames);
            }
            return;
        }
        AllLiveActorGroup* pGroup = MR::getAllLiveActorGroup();
        if (pGroup == nullptr || pGroup->getObjNum() == 0) {
            return;
        }
        // In a stride through the list, so each stop visits actors of every part of the level; dead ones are skipped.
        static LiveActor* sTarget = nullptr;
        static const char* sVisited[256];
        static s32 sVisitedNum = 0;
        const s32 total = pGroup->getObjNum();
        // A scene that restarted (the player's death) took the target and the visited names with it.
        bool targetHere = sTarget == nullptr;
        for (s32 i = 0; i < total && !targetHere; ++i) {
            targetHere = pGroup->getActor(i) == sTarget;
        }
        if (!targetHere) {
            sTarget = nullptr;
            sVisitedNum = 0;
        }
        if (fresh) {
            // Each kind of actor once a scene while kinds remain; the player's own models are not places.
            static const AllLiveActorGroup* sVisitedGroup = nullptr;
            if (sVisitedGroup != pGroup) {
                sVisitedGroup = pGroup;
                sVisitedNum = 0;
            }
            const auto visited = [&](const char* pName) {
                for (s32 i = 0; i < sVisitedNum; ++i) {
                    if (MR::isEqualString(sVisited[i], pName)) {
                        return true;
                    }
                }
                return false;
            };
            sTarget = nullptr;
            for (s32 pass = 0; pass < 2 && sTarget == nullptr; ++pass) {
                for (s32 i = 0; i < total && sTarget == nullptr; ++i) {
                    LiveActor* pActor = pGroup->getActor((static_cast< u32 >(index) * 2654435761u + static_cast< u32 >(i)) % static_cast< u32 >(total));
                    if (pActor == nullptr || MR::isDead(pActor) || pActor->mModelManager == nullptr || MR::calcDistanceToPlayer(pActor) < 1.0f) {
                        continue;
                    }
                    const char* pName = MR::getModelResName(pActor);
                    if (strncmp(pName, "Mario", 5) == 0 || strncmp(pName, "mario", 5) == 0 || (pass == 0 && visited(pName))) {
                        continue;
                    }
                    sTarget = pActor;
                    if (pass == 0 && sVisitedNum < 256) {
                        sVisited[sVisitedNum++] = pName;
                    }
                }
            }
            if (sTarget != nullptr) {
                JUTNativeLog("test: actor %s %.0f %.0f %.0f\n", MR::getModelResName(sTarget), sTarget->mPosition.x, sTarget->mPosition.y, sTarget->mPosition.z);
            }
        }
        if (sTarget != nullptr && !MR::isDead(sTarget)) {
            TVec3f pos(sTarget->mPosition);
            pos -= sTarget->mGravity * 150.0f;
            MR::setPlayerPos(pos);
        }
    }
