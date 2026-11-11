#include "Game/Util/Color.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/Animation/XanimeResource.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcFogStartEnd_f1, "calcFogStartEnd.f1", 100000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFogStartEnd_f2, "calcFogStartEnd.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFogStartEnd_f3, "calcFogStartEnd.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFogStartEnd_f4, "calcFogStartEnd.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFogStartEnd_f5, "calcFogStartEnd.f5", 100000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFogStartEnd_f6, "calcFogStartEnd.f6", 100000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFogStartEnd_f7, "calcFogStartEnd.f7", 100000.0f);

namespace MR {

    XanimePlayer* newXanimePlayer(const ResourceHolder* pModelResourceHolder, const char* pChar, const ResourceHolder* /*pUnused*/, ModelFlag mdlFlag,
                                  XanimeResourceTable* pAnimeResourceTable) {
        Model* newModel = createModel(pModelResourceHolder, pChar, mdlFlag);
        return new XanimePlayer(newModel, pAnimeResourceTable);
    }

    XanimeResourceTable* newXanimeResourceTable(ResourceHolder* pResourceHolder) {
        return new XanimeResourceTable(pResourceHolder);
    }

    void updateModelManager(LiveActor* pActor) {
        pActor->mModelManager->update();
    }

    void calcAnimModelManager(LiveActor* pActor) {
        pActor->mModelManager->calcAnim();
    }

    void updateModelAnimPlayer(LiveActor* pActor) {
        XanimePlayer* pAnimePlayer = pActor->mModelManager->mXanimePlayer;
        if (pAnimePlayer == nullptr) {
            return;
        }

        pAnimePlayer->updateBeforeMovement();
        pAnimePlayer->updateAfterMovement();
    }

    void invalidateMtxCalc(ModelData* pModelData) {
        u32 jointNum = pModelData->getJointCount();
        for (u32 i = 0; i < jointNum; i++) {
            pModelData->getJointAt(i)->setMtxCalc(nullptr);
        }
    }

    void invalidateJointCallback(ModelData* pModelData) {
        u32 jointNum = pModelData->getJointCount();
        for (u32 i = 0; i < jointNum; i++) {
            pModelData->getJointAt(i)->setCallBack(nullptr);
        }
    }

    Model* getModel(const LiveActor* pActor) {
        if (pActor->mModelManager == nullptr) {
            return nullptr;
        }

        return pActor->mModelManager->getModel();
    }

    void calcModel(LiveActor* pActor) {
        getModel(pActor)->calc();
    }

    ModelData* getModelData(const LiveActor* pActor) {
        if (pActor->mModelManager == nullptr) {
            return nullptr;
        }

        return pActor->mModelManager->getModelData();
    }

    s32 getBckFrameMax(const LiveActor* pActor, const char* pBckName) {
        return static_cast< Clip* >(getResourceHolder(pActor)->mMotionResTable->getRes(pBckName))->mFrameMax;
    }

    s32 getBrkFrameMax(const LiveActor* pActor, const char* pBrkName) {
        return static_cast< Clip* >(getResourceHolder(pActor)->mBrkResTable->getRes(pBrkName))->mFrameMax;
    }

    s32 getBvaFrameMax(const LiveActor* pActor, const char* pBvaName) {
        return static_cast< Clip* >(getResourceHolder(pActor)->mBvaResTable->getRes(pBvaName))->mFrameMax;
    }

    bool isBckPlaying(XanimePlayer* pAnimePlayer, const char* pBckName) {
        // A model with no skeletal animations has no player: nothing plays.
        return pAnimePlayer != nullptr && !pAnimePlayer->isTerminate() && pAnimePlayer->isRun(pBckName) && pAnimePlayer->getRate() != 0.0f;
    }

    bool findBckNameStringInResource(const char** pOut, const ResourceHolder* pHolder, const char* pNameString) {
        for (int i = 0; i < pHolder->mMotionResTable->mCount; i++) {
            if (isEqualStringCase(pNameString, pHolder->getMotionName(i))) {
                *pOut = pHolder->getMotionName(i);
                return true;
            }
        }

        return false;
    }

    // A material name the model lacks (or a null name) is the first material.
    u32 getMaterialNo(ModelData* pModelData, const char* pMaterialName) {
        const s32 idx = pModelData->findMaterial(pMaterialName);
        return idx >= 0 ? static_cast< u32 >(idx) : 0;
    }

    u32 getMaterialNo(Model* pModel, const char* pMaterialName) {
        return MR::getMaterialNo(pModel->getModelData(), pMaterialName);
    }

    Material* getMaterial(ModelData* pModelData, const char* pMaterialName) {
        return pModelData->getMaterialAt(getMaterialNo(pModelData, pMaterialName));
    }

    Material* getMaterial(ModelData* pModelData, int idx) {
        return pModelData->getMaterialAt(idx);
    }

    Material* getMaterial(Model* pModel, int idx) {
        return pModel->mModelData->getMaterialAt(idx);
    }

    Material* getMaterial(const LiveActor* pActor, int idx) {
        return getModelData(pActor)->getMaterialAt(idx);
    }

    s32 getMaterialNum(Model* pModel) {
        return pModel->mModelData->getMaterialCount();
    }

    const char* getMaterialName(const ModelData* pModelData, int idx) {
        return pModelData->getMaterialName(idx);
    }

    void updateModelDiffMaterials(LiveActor* pActor) {
        pActor->mModelManager->updateList();
    }

    void hideMaterial(Model* pModel, const char* pMaterialName) {
        MeshEntry* pckt = pModel->getMaterialEntry(getMaterialNo(pModel, pMaterialName))->getFirstMeshEntry();
        pckt->mFlags |= 0x10;
    }

    void hideMaterial(const LiveActor* pActor, const char* pMaterialName) {
        hideMaterial(getModel(pActor), pMaterialName);
    }

    void showMaterial(Model* pModel, const char* pMaterialName) {
        MeshEntry* pckt = pModel->getMaterialEntry(getMaterialNo(pModel, pMaterialName))->getFirstMeshEntry();
        pckt->mFlags &= ~0x10;
    }

    void showMaterial(const LiveActor* pActor, const char* pMaterialName) {
        showMaterial(getModel(pActor), pMaterialName);
    }

    // Linear fog whose fraction at pos's depth is `strength`: from 100, or later when the end would pass 100000.
    // Strength 0 starts the fog at pos's depth and ends it at 100000.
    void calcFogStartEnd(Vec3f pos, f32 strength, f32* pFogStart, f32* pFogEnd) {
        Vec viewPos;
        NebulaMtxMultVec(ModelSys::mViewMtx, pos, &viewPos);

        f32 depth = -viewPos.z;
        if (strength == 0.0f) {
            *pFogStart = depth;
            *pFogEnd = cNum_calcFogStartEnd_f1;
            return;
        }

        f32 fogStart = cNum_calcFogStartEnd_f2;
        f32 fogEnd = cNum_calcFogStartEnd_f3 + ((depth - cNum_calcFogStartEnd_f4) / strength);
        if (fogEnd > cNum_calcFogStartEnd_f5) {
            fogEnd = cNum_calcFogStartEnd_f6;
            fogStart = depth - (cNum_calcFogStartEnd_f7 - depth) * (strength / (1.0f - strength));
        }

        *pFogStart = fogStart;
        *pFogEnd = fogEnd;
    }

    bool isUseTex(Material* pMaterial, u32 textureIndex) {
        for (u32 idx = 0; idx < pMaterial->getSlotCount(); idx++) {
            if (pMaterial->getTextureIndex(idx) == textureIndex && pMaterial->usesMap(idx)) {
                return true;
            }
        }

        return false;
    }

    bool isNormalTexMtx(Material* pMaterial) {
        for (u32 idx = 0; idx < pMaterial->getSlotCount(); idx++) {
            CoordMatrix* texMtx = pMaterial->getCoordMatrix(idx);

            if (texMtx != nullptr && texMtx->getMode() != CoordMatrixMode_None && isUseTexMtx(pMaterial, idx)) {
                return false;
            }
        }

        return true;
    }

    bool isUseTexMtx(Material* pMaterial, u32 idx) {
        return pMaterial->getCoordMatrix(idx) != nullptr && pMaterial->usesCoord(idx);
    }

    bool isExistEffectTexMtx(ModelData* pModelData) {
        for (u32 i = 0; i < pModelData->getMaterialCount(); i++) {
            if (!isNormalTexMtx(pModelData->getMaterialAt(i))) {
                return true;
            }
        }

        return false;
    }

    bool isExistEffectTexMtx(LiveActor* pActor) {
        return isExistEffectTexMtx(getModelData(pActor));
    }

    ResTIMG* getResTIMG(const LiveActor* pActor, int idx) {
        return getResTIMG(getModelData(pActor), idx);
    }

    ResTIMG* getResTIMG(const ModelData* pModelData, int idx) {
        return pModelData->getTexture(idx);
    }

    ResTIMG* getTexture(ResourceHolder* pResourceHolder, const char* pTextureName) {
        return static_cast< ResTIMG* >(pResourceHolder->mFileInfoTable->getRes(pTextureName));
    }

    const char* getTextureName(const LiveActor* pActor, int idx) {
        return getModelData(pActor)->getTextureName(idx);
    }

    void copyJointAnimation(Model* pCopyTo, Model* pCopyFrom) {
        u32 jointNum = pCopyTo->getModelData()->getJointCount();
        for (u32 i = 0; i < jointNum; i++) {
            pCopyTo->setAnmMtx(i, pCopyFrom->getAnmMtx(i));
        }
    }

    void copyJointAnimation(LiveActor* pCopyTo, const LiveActor* pCopyFrom) {
        return copyJointAnimation(getModel(pCopyTo), getModel(pCopyFrom));
    }

    void syncMaterialAnimation(Model* pModel1, Model* pModel2) {
        u32 materialNum = pModel1->getModelData()->getMaterialCount();
        u32 i;
        u32 j;
        u32 idx;
        for (i = 0; i < materialNum; i++) {
            MeshEntry* shapePacketModel2 = pModel2->getMeshEntry(i);
            idx = shapePacketModel2->getMesh()->getMaterial()->getIndex();
            for (j = 0; j < materialNum; j++) {
                MeshEntry* shapePacketModel1 = pModel1->getMeshEntry(j);
                if (shapePacketModel1->getMesh()->getMaterial()->getIndex() == idx) {
                    shapePacketModel1->mMaterial = shapePacketModel2->mMaterial;
                    break;
                }
            }
        }
    }

    void syncMaterialAnimation(LiveActor* pActor1, const LiveActor* pActor2) {
        return syncMaterialAnimation(getModel(pActor1), getModel(pActor2));
    }

    void getLightNum(Material* pMaterial, s32* pChan1, s32* pChan2, s32* pChan3, s32* pChan4) {
        for (int i = 0; i < 8; i++) {
            if (pMaterial->usesLight(0, i)) {
                *pChan1 = *pChan1 + 1;
            }

            if (pMaterial->usesLight(1, i)) {
                *pChan2 = *pChan2 + 1;
            }
        }

        for (int i = 0; i < 8; i++) {
            if (pMaterial->usesLight(2, i)) {
                *pChan3 = *pChan3 + 1;
            }

            if (pMaterial->usesLight(3, i)) {
                *pChan4 = *pChan4 + 1;
            }
        }
    }

    s32 getLightNum(Material* pMaterial) {
        s32 chan1 = 0;
        s32 chan2 = 0;
        s32 chan3 = 0;
        s32 chan4 = 0;
        getLightNum(pMaterial, &chan1, &chan2, &chan3, &chan4);
        return chan1 + chan2 + chan3 + chan4;
    }

    s32 getLightNumMax(const LiveActor* pActor) {
        return getLightNumMax(getModelData(pActor));
    }

    s32 getLightNumMax(ModelData* pModelData) {
        int maxNum = 0;
        for (int i = 0; i < pModelData->getMaterialCount(); i++) {
            int currentNum = getLightNum(pModelData->getMaterialAt(i));

            if (maxNum < currentNum) {
                maxNum = currentNum;
            }
        }

        return maxNum;
    }

    void calcModelBoundingBox(Box3f* pOut, const LiveActor* pActor) {
        s32 jointNum = getJointNum(pActor);
        for (u32 i = 0; i < jointNum; i++) {
            Pos3f jointMtx;
            Box3f bounds;
            Joint* pJoint = getJoint(pActor, i);
            Vec3f jointMin(*pJoint->getMin());
            Vec3f jointMax(*pJoint->getMax());
            jointMtx.setInline(getJointMtx(pActor, i));
            Vec3f translation;
            jointMtx.getTrans(translation);
            Vec3f scale;
            jointMtx.getScale(scale);
            jointMin.mul(scale);
            jointMax.mul(scale);
            bounds.set(translation + jointMin, translation + jointMax);
            if (i == 0) {
                pOut->set(bounds.i, bounds.f);
                continue;
            }

            pOut->extend(bounds.i, bounds.f);
        }

        pOut->add(-pActor->mPosition);
    }

    void calcModelBoundingRadius(f32* pOut, const LiveActor* pActor) {
        MR::getJointNum(pActor);
        Box3f boundingBox;
        calcModelBoundingBox(&boundingBox, pActor);
        Vec3f i = boundingBox.i;
        Vec3f f = boundingBox.f;

        Vec3f max;
        max.set(MR::max(MR::abs(i.x), MR::abs(f.x)), MR::max(MR::abs(i.y), MR::abs(f.y)), MR::max(MR::abs(i.z), MR::abs(f.z)));

        *pOut = max.length();
    }

    Model* createModel(const ResourceHolder* pResourceHolder, const char* pChar, ModelFlag mdlFlag) {
        ModelData* modelData = static_cast< ModelData* >(pResourceHolder->mModelResTable->getRes(pChar));
        Model* newModel;
        invalidateMtxCalc(modelData);
        invalidateJointCallback(modelData);

        for (u32 i = 0; i < modelData->getMeshCount(); i++) {
            modelData->getMeshAt(i)->mFlags &= ~0x1;
        }

        if (MR::isEqualString(pChar, "Mario") || MR::isEqualString(pChar, "Luigi") || MR::isEqualString(pChar, "MarioHandL") ||
            MR::isEqualString(pChar, "MarioHandR") || MR::isEqualString(pChar, "MarioFace") || MR::isEqualString(pChar, "LuigiFace") ||
            MR::isEqualString(pChar, "MarioShadow") || MR::isEqualString(pChar, "LuigiShadow") || MR::isEqualString(pChar, "IceMario") ||
            MR::isEqualString(pChar, "IceLuigi") || MR::isEqualString(pChar, "IceMarioHandL") || MR::isEqualString(pChar, "IceMarioHandR") ||
            MR::isEqualString(pChar, "InvincibleMarioHandL") || MR::isEqualString(pChar, "InvincibleMarioHandR") ||
            MR::isEqualString(pChar, "InvincibleMario") || MR::isEqualString(pChar, "InvincibleLuigi") || MR::isEqualString(pChar, "BeeMario") ||
            MR::isEqualString(pChar, "BeeLuigi") || MR::isEqualString(pChar, "HopperMario") || MR::isEqualString(pChar, "HopperLuigi") ||
            MR::isEqualString(pChar, "BoneMario") || MR::isEqualString(pChar, "BoneLuigi") || MR::isEqualString(pChar, "SearchLightCone") ||
            MR::isEqualSubString(pChar, "FlexibleSphere") || MR::isEqualSubString(pChar, "FlexibleSandPlanetParts") || MR::isEqualString(pChar, "ScaleDownRelayPlanet") ||
            MR::isEqualString(pChar, "GhostMario") || MR::isEqualString(pChar, "GhostLuigi") || MR::isEqualString(pChar, "Koura")) {
            newModel = static_cast< Model* >(new PlayerModel(modelData, mdlFlag, 1));
        } else {
            newModel = new Model(modelData, mdlFlag, 1);
        }

        Mtx mtx;
        NebulaMtxIdentity(mtx);
        NebulaMtxCopy(mtx, ModelSys::mViewMtx);
        newModel->calc();
        newModel->viewCalc();
        newModel->viewCalc();

        return newModel;
    }

    bool isExistCollisionResource(const LiveActor* pActor, const char* pName) {
        MR::FormattedName buff("%s.kcl", pName);
        return getResourceHolder(pActor)->mFileInfoTable->findFileInfo(buff.c_str()) != nullptr;
    }

    bool isExistModel(const char* pName) {
        MR::FormattedName buff("/ObjectData/%s", pName);
        return isFileExist(buff.c_str(), false);
    }

    bool isExistSubModel(const char* pModelName, const char* pSubModelName) {
        MR::FormattedName buff("%s%s", pModelName, pSubModelName);
        return isExistModel(buff.c_str());
    }

    ModelData* getModelData(const char* pName) {
        return static_cast< ModelData* >(createAndAddResourceHolder(pName)->mModelResTable->getRes(pName));
    }

};  // namespace MR
