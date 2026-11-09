// SPDX-License-Identifier: GPL-3.0-only
// Plugin calls into the game's save, story and scene (NebulaModCalls.hpp).
#include "Game/System/NebulaModCalls.hpp"
#include <nebula_module.h>
#include "Game/AreaObj/AreaForm.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/OceanBowl.hpp"
#include "Game/Map/OceanRing.hpp"
#include "Game/Map/OceanSphere.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/WaterPoint.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    // The stage change keeps a pointer to the name.
    char sStage[64];
    // A star given and not yet won: 0 none, 1 a Power Star, 2 a Grand Star.
    s32 sPendingStar = 0;
    s32 sPendingStarId = 0;
    // Grand Star n is the first star of its galaxy (SpecialStarGrand1-7 derive from these).
    const char* const kGrandStarGalaxies[] = {"HeavensDoorGalaxy",    "TriLegLv1Galaxy",      "KoopaBattleVs1Galaxy",
                                              "KoopaJrShipLv1Galaxy", "KoopaBattleVs2Galaxy", "FloaterOtaKingGalaxy",
                                              "KoopaBattleVs3Galaxy"};
    // The most a material's coordinates list holds (the format's own limit is 8).
    NebulaMaterialCoord sCoords[16];

    // A galaxy is played: the title's stage is none (a run that reached a game over).
    bool inGalaxy() {
        return MR::getCurrentStageName() != nullptr && !MR::isStageFileSelect();
    }
}  // namespace

namespace NebulaModCalls {
    void update(bool inPlay) {
        // What PowerStar::exeStageClearDemo does when the player takes a star, once the opening camera and starter
        // (which end in play and would replace the get demo) are over.
        if (sPendingStar != 0 && inPlay) {
            MR::sendStageResultSequenceParam(sPendingStarId);
            if (sPendingStar == 2) {
                MR::requestGrandStarGetDemo();
            } else {
                MR::requestPowerStarGetDemo();
            }
            sPendingStar = 0;
        }
    }

    void changeStage(const char* pStage, s32 scenario) {
        nebula_snprintf(sStage, sizeof(sStage), "%s", pStage);
        MR::requestChangeStageInGameMoving(sStage, scenario);
    }

    void setGrandStars(s32 count, const char* pStage) {
        for (s32 i = 0; i < count && i < 7; ++i) {
            GameDataFunction::setGameFlagPowerStarSuccess(kGrandStarGalaxies[i], 1, true);
        }
        // The scene's domes and their parts take their state from the save as it is when they are built
        // (AstroMapObjFunction::isAlreadyRevival): with the stars set mid-scene every dome would stay a shell without its
        // pillars, lamps and glass top, a state play never reaches. So the stage is built again.
        if (pStage != nullptr) {
            changeStage(pStage, 1);
        } else {
            changeStage(MR::getCurrentStageName(), MR::getCurrentScenarioNo());
        }
    }
}  // namespace NebulaModCalls

extern "C" int32_t nebula_mod_give_star(const char* pGalaxy, int32_t star, int32_t grand) {
    if (!inGalaxy() || star <= 0 || (pGalaxy != nullptr && !MR::isEqualString(pGalaxy, MR::getCurrentStageName()))) {
        return 0;
    }
    sPendingStar = grand != 0 ? 2 : 1;
    sPendingStarId = star;
    return 1;
}

extern "C" int32_t nebula_mod_give_grand_stars(int32_t count) {
    if (!inGalaxy() || count <= 0) {
        return 0;
    }
    nebula_log("mods: Grand Stars 1-%d, %s starts again\n", count < 7 ? count : 7, MR::getCurrentStageName());
    NebulaModCalls::setGrandStars(count, nullptr);
    return 1;
}

extern "C" int32_t nebula_mod_set_player_character(int32_t luigi) {
    GameDataHolder* pHolder = GameDataFunction::getCurrentGameDataHolder();
    if (pHolder == nullptr) {
        return 0;
    }
    // The file played is user 1's Mario or Luigi file; the stage built next reads it.
    pHolder->mIsMario = luigi == 0;
    pHolder->mUser = 1;
    return 1;
}

extern "C" int32_t nebula_mod_list_materials(const char* pModel, NebulaMaterialCallback callback, void* pUser) {
    AllLiveActorGroup* pGroup = pModel != nullptr && callback != nullptr ? MR::getAllLiveActorGroup() : nullptr;
    for (s32 i = 0; pGroup != nullptr && i < pGroup->getObjNum(); ++i) {
        LiveActor* pActor = pGroup->getActor(i);
        if (pActor == nullptr || pActor->mModelManager == nullptr || MR::isDead(pActor) ||
            !MR::isEqualStringCase(MR::getModelResName(pActor), pModel)) {
            continue;
        }
        ModelData* pData = MR::getModelData(pActor);
        const s32 count = pData != nullptr ? static_cast< s32 >(pData->getMaterialCount()) : 0;
        for (s32 m = 0; m < count; ++m) {
            Material* pMaterial = pData->getMaterialAt(static_cast< u32 >(m));
            NebulaMaterialInfo info;
            info.size = sizeof(info);
            info.index = m;
            info.name = pData->getMaterialName(static_cast< u32 >(m));
            info.coord_count = 0;
            for (u32 c = 0; c < pMaterial->getCoordCount() && c < sizeof(sCoords) / sizeof(sCoords[0]); ++c) {
                const MaterialCoord* pCoord = pMaterial->getCoord(c);
                CoordMatrix* pMtx = pMaterial->getCoordMatrix(c);
                NebulaMaterialCoord& rOut = sCoords[c];
                rOut.type = pCoord->mType;
                rOut.source = pCoord->mSource;
                rOut.matrix = pCoord->mMatrix;
                rOut.matrix_kind = pMtx != nullptr ? pMtx->getInfo().mInfo : 255u;
                rOut.projection = pMtx != nullptr ? pMtx->getInfo().mProjection : 255u;
                rOut.used = pMaterial->usesCoord(c) ? 1u : 0u;
                info.coord_count = static_cast< int32_t >(c + 1);
            }
            info.coords = sCoords;
            callback(&info, pUser);
        }
        return count;
    }
    return 0;
}

extern "C" int32_t nebula_mod_water_point(int32_t index, float* pXyz) {
    WaterAreaHolder* pHolder =
        MR::isExistSceneObj(SceneObj_WaterAreaHolder) ? MR::getSceneObj< WaterAreaHolder >(SceneObj_WaterAreaHolder) : nullptr;
    AreaObjMgr* pAreas = MR::getAreaObjManager("Water");
    s32 total = pAreas != nullptr ? pAreas->getNumAreaObj() : 0;
    if (pHolder != nullptr) {
        total += pHolder->mNumOceanBowls + pHolder->mNumOceanSpheres + pHolder->mNumOceanRings * 4;
    }
    if (total <= 0 || pXyz == nullptr) {
        return total > 0 ? total : 0;
    }
    s32 at = index % total;
    if (at < 0) {
        at += total;
    }
    Vec3f pos(0.0f, 0.0f, 0.0f);
    if (pHolder != nullptr && at < pHolder->mNumOceanBowls) {
        OceanBowl* pBowl = pHolder->mOceanBowls[at];
        pos = pBowl->mPosition;
        pos -= pBowl->mUp * (pBowl->mRadius * 0.25f < 400.0f ? pBowl->mRadius * 0.25f : 400.0f);
    } else if (pHolder != nullptr && (at -= pHolder->mNumOceanBowls) < pHolder->mNumOceanSpheres) {
        OceanSphere* pSphere = pHolder->mOceanSpheres[at];
        pos = pSphere->mPosition;
        pos.y += pSphere->mRadius * 0.5f;
    } else if (pHolder != nullptr && (at -= pHolder->mNumOceanSpheres) < pHolder->mNumOceanRings * 4) {
        OceanRing* pRing = pHolder->mOceanRings[at / 4];
        WaterPoint* pPoint = pRing->getPoint((at % 4) * pRing->mWaterPointNum / 4);
        pos = pPoint->mPosition;
        pos -= pPoint->mUpVec * 150.0f;
    } else {
        if (pHolder != nullptr) {
            at -= pHolder->mNumOceanRings * 4;
        }
        AreaObj* pArea = pAreas->getAreaObj(at);
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
    }
    pXyz[0] = pos.x;
    pXyz[1] = pos.y;
    pXyz[2] = pos.z;
    return total;
}

extern "C" int32_t nebula_mod_circle_wipe(int32_t open, int32_t frames) {
    if (!inGalaxy()) {
        return 0;
    }
    if (open != 0) {
        MR::openWipeCircle(frames);
    } else {
        MR::closeWipeCircle(frames);
    }
    return 1;
}

extern "C" int32_t nebula_mod_grant_spin_and_pointer(void) {
    if (!inGalaxy()) {
        return 0;
    }
    MR::onGameEventFlagEnableToSpinAndStarPointer();
    return 1;
}
