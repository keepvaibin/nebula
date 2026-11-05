#include "Game/Util/ModelUtil.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/Animation/XanimeResource.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Player/J3DModelX.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/System/ShapePacketUserData.hpp"
#include "Game/Util.hpp"
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <cstdio>
#include <nebula_cooked_material.h>

namespace {
    inline bool isUseLightChanNo(J3DMaterial* pMaterial, int channel, int index) {
        const J3DColorChan* pChan = pMaterial->getColorChan(channel);
        if (pChan->getEnable() == 0) {
            return false;
        }

        return pChan->getLightMask() & (1 << index);
    }
};  // namespace

namespace MR {

    XanimePlayer* newXanimePlayer(const ResourceHolder* pModelResourceHolder, const char* pChar, const ResourceHolder* pUnused, J3DMdlFlag mdlFlag,
                                  XanimeResourceTable* pAnimeResourceTable) {
        J3DModel* newModel = newJ3DModel(pModelResourceHolder, pChar, mdlFlag);
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
        pAnimePlayer->updateBeforeMovement();
        pAnimePlayer->updateAfterMovement();
    }

    void invalidateMtxCalc(J3DModelData* pModelData) {
        u16 jointNum = pModelData->getJointNum();
        for (u16 i = 0; i < jointNum; i++) {
            pModelData->getJointNodePointer(i)->setMtxCalc(nullptr);
        }
    }

    void invalidateJointCallback(J3DModelData* pModelData) {
        u16 jointNum = pModelData->getJointNum();
        for (u16 i = 0; i < jointNum; i++) {
            pModelData->getJointNodePointer(i)->setCallBack(nullptr);
        }
    }

    J3DModel* getJ3DModel(const LiveActor* pActor) {
        if (pActor->mModelManager == nullptr) {
            return nullptr;
        }

        return pActor->mModelManager->getJ3DModel();
    }

    void calcJ3DModel(LiveActor* pActor) {
        (MR::MutexHolder< 0 >::sMutex).lock();
        getJ3DModel(pActor)->calc();
        (MR::MutexHolder< 0 >::sMutex).unlock();
    }

    J3DModelData* getJ3DModelData(const LiveActor* pActor) {
        if (pActor->mModelManager == nullptr) {
            return nullptr;
        }

        return pActor->mModelManager->getJ3DModelData();
    }

    s16 getBckFrameMax(const LiveActor* pActor, const char* pBckName) {
        return static_cast< J3DAnmBase* >(getResourceHolder(pActor)->mMotionResTable->getRes(pBckName))->mFrameMax;
    }

    s16 getBrkFrameMax(const LiveActor* pActor, const char* pBrkName) {
        return static_cast< J3DAnmBase* >(getResourceHolder(pActor)->mBrkResTable->getRes(pBrkName))->mFrameMax;
    }

    s16 getBvaFrameMax(const LiveActor* pActor, const char* pBvaName) {
        return static_cast< J3DAnmBase* >(getResourceHolder(pActor)->mBvaResTable->getRes(pBvaName))->mFrameMax;
    }

    bool isBckPlaying(XanimePlayer* pAnimePlayer, const char* pBckName) {
        bool running = pAnimePlayer->_24[pAnimePlayer->_54].checkState(1) ? false : true;
        return running && pAnimePlayer->isRun(pBckName) && pAnimePlayer->getRate() != 0.0f;
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

    u16 getMaterialNo(J3DModelData* pModelData, const char* pMaterialName) {
        return pModelData->getMaterialName()->getIndex(pMaterialName);
    }

    u16 getMaterialNo(J3DModel* pModel, const char* pMaterialName) {
        JUTNameTab* materialName = pModel->getModelData()->getMaterialName();
        return materialName->getIndex(pMaterialName);
    }

    J3DMaterial* getMaterial(J3DModelData* pModelData, const char* pMaterialName) {
        return pModelData->getMaterialNodePointer(getMaterialNo(pModelData, pMaterialName));
    }

    J3DMaterial* getMaterial(J3DModelData* pModelData, int idx) {
        return pModelData->getMaterialNodePointer(idx);
    }

    J3DMaterial* getMaterial(J3DModel* pModel, int idx) {
        return pModel->mModelData->getMaterialNodePointer(idx);
    }

    J3DMaterial* getMaterial(const LiveActor* pActor, int idx) {
        return getJ3DModelData(pActor)->getMaterialNodePointer(idx);
    }

    s32 getMaterialNum(J3DModel* pModel) {
        return pModel->mModelData->getMaterialNum();
    }

    const char* getMaterialName(const J3DModelData* pModelData, int idx) {
        return pModelData->getMaterialName()->getName(idx);
    }

    void updateModelDiffMaterials(LiveActor* pActor) {
        pActor->mModelManager->updateList();
    }

    bool isEnvelope(J3DMaterial* pMaterial) {
        return pMaterial->getShape()->mHasPNMTXIdx;
    }

    void hideMaterial(J3DModel* pModel, const char* pMaterialName) {
        J3DShapePacket* pckt = pModel->getMatPacket(getMaterialNo(pModel, pMaterialName))->getShapePacket();
        pckt->mFlags |= 0x10;
    }

    void hideMaterial(const LiveActor* pActor, const char* pMaterialName) {
        hideMaterial(getJ3DModel(pActor), pMaterialName);
    }

    void showMaterial(J3DModel* pModel, const char* pMaterialName) {
        J3DShapePacket* pckt = pModel->getMatPacket(getMaterialNo(pModel, pMaterialName))->getShapePacket();
        pckt->mFlags &= ~0x10;
    }

    void showMaterial(const LiveActor* pActor, const char* pMaterialName) {
        showMaterial(getJ3DModel(pActor), pMaterialName);
    }

    void calcFogStartEnd(TVec3f vec, f32 f1, f32* pFogStart, f32* pFogEnd) {
        Vec multVec;
        NebulaMtxMultVec(j3dSys.mViewMtx, vec, &multVec);

        f32 flt = -multVec.z;
        if (f1 == 0.0f) {
            *pFogStart = flt;
            *pFogEnd = 100000.0f;
            return;
        }

        f32 fogStart = 100.0f;
        f32 fogEnd = 100.0f + ((flt - 100.0f) / f1);
        if (fogEnd > 100000.0f) {
            fogEnd = 100000.0f;
            fogStart = flt - (100000.0f - flt) * (f1 / (1.0f - f1));
        }

        *pFogStart = fogStart;
        *pFogEnd = fogEnd;
    }

    bool isUseTex(J3DMaterial* pMaterial, u16 a2) {
        const NebulaCookedMaterial* pCooked = pMaterial->mNativeMaterial;
        for (u32 idx = 0; pCooked != nullptr && idx < 8; idx++) {
            if (pMaterial->getTexNo(idx) == a2 && (pCooked->used_maps & (1u << idx)) != 0) {
                return true;
            }
        }

        return false;
    }

    bool isNormalTexMtx(J3DMaterial* pMaterial) {
        for (u32 idx = 0; idx < 8; idx++) {
            J3DTexMtx* texMtx = pMaterial->getTexMtx(idx);

            if (texMtx != nullptr && (texMtx->getTexMtxInfo().mInfo & 0x3F) != 0 && isUseTexMtx(pMaterial, idx)) {
                return false;
            }
        }

        return true;
    }

    bool isUseTexMtx(J3DMaterial* pMaterial, u32 idx) {
        const NebulaCookedMaterial* pCooked = pMaterial->mNativeMaterial;
        return pMaterial->getTexMtx(idx) != nullptr && pCooked != nullptr &&
               (pCooked->used_coords & (1u << idx)) != 0;
    }

    bool isExistEffectTexMtx(J3DModelData* pModelData) {
        for (u16 i = 0; i < pModelData->getMaterialNum(); i++) {
            if (!isNormalTexMtx(pModelData->getMaterialNodePointer(i))) {
                return true;
            }
        }

        return false;
    }

    bool isExistEffectTexMtx(LiveActor* pActor) {
        return isExistEffectTexMtx(getJ3DModelData(pActor));
    }

    bool isUseTexMtxEnvMap(J3DMaterial* pMaterial) {
        for (u32 idx = 0; idx < 8; idx++) {
            J3DTexMtx* texMtx = pMaterial->getTexMtx(idx);
            if (texMtx == nullptr) {
                continue;
            }

            u32 temp = texMtx->getTexMtxInfo().mInfo & 0x3F;
            if (temp == 1 || temp == 6 || temp == 7) {
                if (!isUseTexMtx(pMaterial, idx)) {
                    return false;
                }

                return true;
            }
        }

        return false;
    }

    bool isUseTexMtxProjMap(J3DMaterial* pMaterial) {
        for (u32 idx = 0; idx < 8; idx++) {
            J3DTexMtx* texMtx = pMaterial->getTexMtx(idx);
            if (texMtx == nullptr) {
                continue;
            }

            u32 temp = texMtx->getTexMtxInfo().mInfo & 0x3F;
            if (temp == 2 || temp == 3 || temp == 8 || temp == 9) {
                return true;
            }
        }

        return false;
    }

    void initEnvelopeAndEnvMapOrProjMapModelData(J3DModelData* pModelData) {


        for (int i = 0; i < pModelData->getMaterialNum(); i++) {
            J3DMaterial* material = pModelData->getMaterialNodePointer(i);
            if (!isEnvelope(material)) {
                continue;
            }

            bool isUseEnvMap = isUseTexMtxEnvMap(material);
            bool isUseProjMap = isUseTexMtxProjMap(material);
            if (!isUseEnvMap && !isUseProjMap) {
                continue;
            }

            J3DShape* shape = material->getShape();
            if (isUseEnvMap) {
                shape->setTexMtxLoadType(0x2000);
            }

            for (u32 idx = 0; idx < 8; idx++) {
                J3DTexCoord* texCoord = material->getTexCoord(idx);
                if (texCoord->getTexGenSrc() == 1) {
                    shape->addTexMtxIndex(idx, 30);
                } else if (texCoord->getTexGenSrc() == 0) {
                    // In a material with indirect stages: texture place 0, as v1 and the original draw it (the manta ray's body,
                    // runs s4_rC against s4_g5 and v1). Otherwise the corner's position (Rosalina's fade, ros_rc1).
                    const bool isIndirect = material->getIndTexStageNum() != 0;
                    shape->addTexMtxIndex(idx, isIndirect ? J3DShape::cTexMtxBasePlaceZero : 0);
                }
            }
        }
    }

    ResTIMG* getResTIMG(const LiveActor* pActor, int idx) {
        return getResTIMG(getJ3DModelData(pActor), idx);
    }

    ResTIMG* getResTIMG(const J3DModelData* pModelData, int idx) {
        return pModelData->mMaterialTable.getTexture()->getResTIMG(idx);
    }

    ResTIMG* getTexture(ResourceHolder* pResourceHolder, const char* pTextureName) {
        return static_cast< ResTIMG* >(pResourceHolder->mFileInfoTable->getRes(pTextureName));
    }

    const char* getTextureName(const LiveActor* pActor, int idx) {
        return getJ3DModelData(pActor)->getTextureName()->getName(idx);
    }

    int findPosNrmMtxIndexFromModel(J3DModelData* pModelData, u16 a2) {
        u16 jointNum = pModelData->getJointNum();

        for (u16 i = 0; i < jointNum; i++) {
            J3DMaterial* mesh = pModelData->getJointNodePointer(i)->getMesh();
            while (mesh != nullptr) {
                int idx = findPosNrmMtxIndexFromShape(mesh->getShape(), a2);
                if (idx != -1) {
                    return idx;
                }

                mesh = mesh->mNext;
            }
        }

        return -1;
    }

    int findPosNrmMtxIndexFromShape(J3DShape* pShape, u16 a2) {
        u16 mtxGroupNum = pShape->getMtxGroupNum();
        for (u16 i = 0; i < mtxGroupNum; i++) {
            J3DShapeMtx* shapeMtx = pShape->getShapeMtx(i);
            u16 useMtxnum = shapeMtx->getUseMtxNum();

            for (u16 j = 0; j < useMtxnum; j++) {
                if (shapeMtx->getUseMtxIndex(j) == a2) {
                    return j * 3;
                }
            }
        }

        return -1;
    }

    void copyJointAnimation(J3DModel* pCopyTo, J3DModel* pCopyFrom) {
        u16 jointNum = pCopyTo->getModelData()->getJointNum();
        for (u16 i = 0; i < jointNum; i++) {
            pCopyTo->setAnmMtx(i, pCopyFrom->getAnmMtx(i));
        }

        u16 wEvlpMtxNum = pCopyTo->getModelData()->getWEvlpMtxNum();
        for (u16 i = 0; i < wEvlpMtxNum; i++) {
            pCopyTo->setWeightAnmMtx(i, pCopyFrom->getWeightAnmMtx(i));
        }
    }

    void copyJointAnimation(LiveActor* pCopyTo, const LiveActor* pCopyFrom) {
        return copyJointAnimation(getJ3DModel(pCopyTo), getJ3DModel(pCopyFrom));
    }

    void syncMaterialAnimation(J3DModel* pModel1, J3DModel* pModel2) {
        u16 materialNum = pModel1->getModelData()->getMaterialNum();
        u16 i;
        u16 j;
        u16 idx;
        for (i = 0; i < materialNum; i++) {
            J3DShapePacket* shapePacketModel2 = pModel2->getShapePacket(i);
            idx = shapePacketModel2->getShape()->getMaterial()->getIndex();
            for (j = 0; j < materialNum; j++) {
                J3DShapePacket* shapePacketModel1 = pModel1->getShapePacket(j);
                if (shapePacketModel1->getShape()->getMaterial()->getIndex() == idx) {
                    shapePacketModel1->mpMaterial = shapePacketModel2->mpMaterial;
                    break;
                }
            }
        }
    }

    void syncMaterialAnimation(LiveActor* pActor1, const LiveActor* pActor2) {
        return syncMaterialAnimation(getJ3DModel(pActor1), getJ3DModel(pActor2));
    }

    void getLightNum(J3DMaterial* pMaterial, s32* pChan1, s32* pChan2, s32* pChan3, s32* pChan4) {
        for (int i = 0; i < 8; i++) {
            if (::isUseLightChanNo(pMaterial, 0, i)) {
                *pChan1 = *pChan1 + 1;
            }

            if (::isUseLightChanNo(pMaterial, 1, i)) {
                *pChan2 = *pChan2 + 1;
            }
        }

        for (int i = 0; i < 8; i++) {
            if (::isUseLightChanNo(pMaterial, 2, i)) {
                *pChan3 = *pChan3 + 1;
            }

            if (::isUseLightChanNo(pMaterial, 3, i)) {
                *pChan4 = *pChan4 + 1;
            }
        }
    }

    s32 getLightNum(J3DMaterial* pMaterial) {
        s32 chan1 = 0;
        s32 chan2 = 0;
        s32 chan3 = 0;
        s32 chan4 = 0;
        getLightNum(pMaterial, &chan1, &chan2, &chan3, &chan4);
        return chan1 + chan2 + chan3 + chan4;
    }

    s32 getLightNumMax(const LiveActor* pActor) {
        return getLightNumMax(getJ3DModelData(pActor));
    }

    s32 getLightNumMax(J3DModelData* pModelData) {
        int maxNum = 0;
        for (int i = 0; i < pModelData->mMaterialTable.mMaterialNum; i++) {
            int currentNum = getLightNum(pModelData->mMaterialTable.mMaterialNodePointer[static_cast< u16 >(i)]);

            if (maxNum < currentNum) {
                maxNum = currentNum;
            }
        }

        return maxNum;
    }

    void calcModelBoundingBox(TBox3f* pOut, const LiveActor* pActor) {
        s32 jointNum = getJointNum(pActor);
        for (u16 i = 0; i < jointNum; i++) {
            TPos3f jointMtx;
            TBox3f bounds;
            J3DJoint* pJoint = getJoint(pActor, i);
            TVec3f jointMin(*pJoint->getMin());
            TVec3f jointMax(*pJoint->getMax());
            jointMtx.setInline(getJointMtx(pActor, i));
            TVec3f translation;
            jointMtx.getTrans(translation);
            TVec3f scale;
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
        TBox3f boundingBox;
        calcModelBoundingBox(&boundingBox, pActor);
        TVec3f i = boundingBox.i;
        TVec3f f = boundingBox.f;

        TVec3f max;
        max.set(MR::max(MR::abs(i.x), MR::abs(f.x)), MR::max(MR::abs(i.y), MR::abs(f.y)), MR::max(MR::abs(i.z), MR::abs(f.z)));

        *pOut = max.length();
    }

    J3DModel* newJ3DModel(const ResourceHolder* pResourceHolder, const char* pChar, J3DMdlFlag mdlFlag) {
        J3DModelData* modelData = static_cast< J3DModelData* >(pResourceHolder->mModelResTable->getRes(pChar));
        J3DModel* newModel;
        invalidateMtxCalc(modelData);
        invalidateJointCallback(modelData);

        for (u16 i = 0; i < modelData->getShapeNum(); i++) {
            modelData->getShapeNodePointer(i)->mFlags &= ~0x1;
        }

        if (MR::isEqualString(pChar, "Mario") || MR::isEqualString(pChar, "Luigi") || MR::isEqualString(pChar, "MarioHandL") ||
            MR::isEqualString(pChar, "MarioHandR") || MR::isEqualString(pChar, "MarioFace") || MR::isEqualString(pChar, "LuigiFace") ||
            MR::isEqualString(pChar, "MarioShadow") || MR::isEqualString(pChar, "LuigiShadow") || MR::isEqualString(pChar, "IceMario") ||
            MR::isEqualString(pChar, "IceLuigi") || MR::isEqualString(pChar, "IceMarioHandL") || MR::isEqualString(pChar, "IceMarioHandR") ||
            MR::isEqualString(pChar, "InvincibleMarioHandL") || MR::isEqualString(pChar, "InvincibleMarioHandR") ||
            MR::isEqualString(pChar, "InvincibleMario") || MR::isEqualString(pChar, "InvincibleLuigi") || MR::isEqualString(pChar, "BeeMario") ||
            MR::isEqualString(pChar, "BeeLuigi") || MR::isEqualString(pChar, "HopperMario") || MR::isEqualString(pChar, "HopperLuigi") ||
            MR::isEqualString(pChar, "BoneMario") || MR::isEqualString(pChar, "BoneLuigi") || MR::isEqualString(pChar, "SearchLightCone") ||
            strstr(pChar, "FlexibleSphere") || strstr(pChar, "FlexibleSandPlanetParts") || MR::isEqualString(pChar, "ScaleDownRelayPlanet") ||
            MR::isEqualString(pChar, "GhostMario") || MR::isEqualString(pChar, "GhostLuigi") || MR::isEqualString(pChar, "Koura")) {
            (MR::MutexHolder< 0 >::sMutex).lock();
            newModel = static_cast< J3DModel* >(new J3DModelX(modelData, mdlFlag, 1));
            (MR::MutexHolder< 0 >::sMutex).unlock();
        } else {
            (MR::MutexHolder< 0 >::sMutex).lock();
            newModel = new J3DModel(modelData, mdlFlag, 1);
            (MR::MutexHolder< 0 >::sMutex).unlock();
        }

        initJ3DShapePacketUserData(newModel);
        Mtx mtx;
        NebulaMtxIdentity(mtx);
        newModel->unlock();
        (MR::MutexHolder< 0 >::sMutex).lock();
        NebulaMtxCopy(mtx, j3dSys.mViewMtx);
        newModel->calc();
        newModel->calcAllMaterials();
        newModel->viewCalc();
        newModel->viewCalc();
        (MR::MutexHolder< 0 >::sMutex).unlock();

        newModel->lock();
        return newModel;
    }

    bool isExistCollisionResource(const LiveActor* pActor, const char* pName) {
        char buff[256];
        nebula_snprintf(buff, sizeof(buff), "%s.kcl", pName);
        return getResourceHolder(pActor)->mFileInfoTable->findFileInfo(buff) != nullptr;
    }

    bool isExistModel(const char* pName) {
        char buff[0x100];
        nebula_snprintf(buff, sizeof(buff), "/ObjectData/%s.arc", pName);
        return isFileExist(buff, false);
    }

    bool isExistSubModel(const char* pModelName, const char* pSubModelName) {
        char buff[0x100];
        nebula_snprintf(buff, sizeof(buff), "%s%s", pModelName, pSubModelName);
        return isExistModel(buff);
    }

    J3DModelData* getJ3DModelData(const char* pName) {
        char buff[0x100];
        nebula_snprintf(buff, sizeof(buff), "%s.arc", pName);
        return static_cast< J3DModelData* >(createAndAddResourceHolder(buff)->mModelResTable->getRes(pName));
    }

};  // namespace MR
