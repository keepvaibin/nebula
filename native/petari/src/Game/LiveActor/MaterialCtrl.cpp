#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util.hpp"
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DTexture.hpp>

char MirrorReflectionMtxSetter::sMirrorTextureName[] = "MirrorTex";

MaterialCtrl::MaterialCtrl(J3DModel* pModel, const char* pMaterialName) : mModel(pModel), mMaterial() {
    if (pMaterialName) {
        mMaterial = pModel->ownMaterial(MR::getMaterialNo(pModel->getModelData(), pMaterialName));
    }
}

void MaterialCtrl::update() {
    if (mMaterial) {
        updateMaterial(mMaterial);
    }
}

FogCtrl::FogCtrl(J3DModel* pModel, bool useAllMaterials) : MaterialCtrl(pModel, nullptr), mNumMaterials(), mMaterials() {
    J3DModelData* pModelData = pModel->getModelData();
    J3DMaterial* firstMaterial = nullptr;
    for (u16 i = 0; i < pModelData->mMaterialTable.getMaterialNum(); i++) {
        J3DMaterial* material = pModelData->mMaterialTable.getMaterialNodePointer(i);
        if (useAllMaterials || material->getFog()->mType) {
            if (!firstMaterial) {
                firstMaterial = material;
            }

            mNumMaterials++;
        }
    }

    if (!firstMaterial) {
        firstMaterial = pModelData->mMaterialTable.getMaterialNodePointer(0);
    }

    mFogInfo = *firstMaterial->getFog()->getFogInfo();
    if (mNumMaterials > 0) {
        mMaterials = new J3DMaterial*[mNumMaterials];
        s32 materialCount = 0;
        for (u16 i = 0; i < pModelData->mMaterialTable.getMaterialNum(); i++) {
            J3DMaterial* material = pModelData->mMaterialTable.getMaterialNodePointer(i);
            if (useAllMaterials || material->getFog()->mType) {
                mMaterials[materialCount++] = pModel->ownMaterial(i);
            }
        }
    }
}

void FogCtrl::update() {
    for (s32 i = 0; i < mNumMaterials; i++) {
        mMaterials[i]->getFog()->setFogInfo(mFogInfo);
    }
}

MatColorCtrl::MatColorCtrl(J3DModel* pModel, const char* pName, u32 color, const J3DColor* pColor) : MaterialCtrl(pModel, pName) {
    mColorChoice = color;
    mColor = pColor;
}

void MatColorCtrl::updateMaterial(J3DMaterial* pMaterial) {
    pMaterial->setMatColor(mColorChoice, mColor);
}

ViewProjmapEffectMtxSetter::ViewProjmapEffectMtxSetter(J3DModel* pModel) : MaterialCtrl(pModel, nullptr), mMatricies(), mNumMatricies() {
    J3DModelData* pModelData = pModel->getModelData();
    J3DTexMtxInfo* matrices[64];
    for (u16 i = 0; i < pModelData->mMaterialTable.getMaterialNum(); i++) {
        J3DMaterial* material = pModelData->mMaterialTable.getMaterialNodePointer(i);
        for (u32 j = 0; j < 8; j++) {
            J3DTexMtx* texMtx = material->getTexMtx(j);
            if (texMtx && (texMtx->getTexMtxInfo().mInfo & 0x3FU) == J3DTexMtxMode_ViewProjmap && MR::isUseTexMtx(material, j)) {
                matrices[mNumMatricies] = &pModel->ownMaterial(i)->getTexMtx(j)->getTexMtxInfo();
                mNumMatricies++;
            }
        }
    }

    mMatricies = new J3DTexMtxInfo*[mNumMatricies];
    MR::copyMemory(mMatricies, matrices, sizeof(J3DTexMtxInfo*) * mNumMatricies);
}

void ViewProjmapEffectMtxSetter::update() {
    TMtx44f projection = MR::getCameraProjectionMtx();
    projection[2][0] = 0.0f;
    projection[2][1] = 0.0f;
    projection[2][2] = -1.0f;
    projection[2][3] = 0.0f;
    projection[3][0] = 0.0f;
    projection[3][1] = 0.0f;
    projection[3][2] = 0.0f;
    projection[3][3] = 1.0f;
    for (s32 i = 0; i < mNumMatricies; i++) {
        mMatricies[i]->setEffectMtx(projection);
    }
}

ProjmapEffectMtxSetter::ProjmapEffectMtxSetter(J3DModel* pModel, const ResourceHolder* pResourceHolder)
    : MaterialCtrl(pModel, nullptr), mUpdatingMtxInfo(), mNumUpdatingMtx() {
    J3DModelData* modelData = pModel->getModelData();
    mEffectMtx.identity();
    for (u16 i = 0; i < modelData->mMaterialTable.getMaterialNum(); i++) {
        J3DMaterial* material = modelData->mMaterialTable.getMaterialNodePointer(i);
        for (u32 j = 0; j < 8; j++) {
            J3DTexMtx* texMtx = material->getTexMtx(j);
            if (texMtx && (texMtx->getTexMtxInfo().mInfo & 0x3FU) == J3DTexMtxMode_Projmap && MR::isUseTexMtx(material, j)) {
                mNumUpdatingMtx++;
            }
        }
    }

    mUpdatingMtxInfo = new UpdateEffectMtxInfo[mNumUpdatingMtx];
    s32 matrixCount = 0;
    for (u16 i = 0; i < modelData->mMaterialTable.getMaterialNum(); i++) {
        J3DMaterial* material = modelData->mMaterialTable.getMaterialNodePointer(i);
        for (u32 j = 0; j < 8; j++) {
            J3DTexMtx* texMtx = material->getTexMtx(j);
            if (texMtx && (texMtx->getTexMtxInfo().mInfo & 0x3FU) == J3DTexMtxMode_Projmap && MR::isUseTexMtx(material, j)) {
                mUpdatingMtxInfo[matrixCount].mTexMtx = pModel->ownMaterial(i)->getTexMtx(j);
                mUpdatingMtxInfo[matrixCount].mInitEffectMtx.set(pResourceHolder->getInitEffectMtx(i, j));
                matrixCount++;
            }
        }
    }
}

void ProjmapEffectMtxSetter::update() {
    for (s32 i = 0; i < mNumUpdatingMtx; i++) {
        UpdateEffectMtxInfo* info = &mUpdatingMtxInfo[i];
        TPos3f effectMtx;
        effectMtx.concat(info->mInitEffectMtx, mEffectMtx);
        info->mTexMtx->setEffectMtx(effectMtx);
    }
}

void ProjmapEffectMtxSetter::getBaseTrans(TVec3f* pTrans) const {
    pTrans->set< f32 >(mModel->getBaseTRMtx()[0][3], mModel->getBaseTRMtx()[1][3], mModel->getBaseTRMtx()[2][3]);
}

void ProjmapEffectMtxSetter::updateMtxUseBaseMtx() {
    TPos3f baseMtx;
    baseMtx.set(mModel->getBaseTRMtx());
    mEffectMtx.invert(baseMtx);
}

void ProjmapEffectMtxSetter::updateMtxUseBaseMtxWithLocalOffset(const TVec3f& rOffset) {
    TPos3f baseMtx;
    baseMtx.set(mModel->getBaseTRMtx());
    TPos3f offsetMtx;
    offsetMtx.identity33();
    offsetMtx.setTrans(rOffset);
    baseMtx.concat(offsetMtx);
    mEffectMtx.invert(baseMtx);
}

MirrorReflectionMtxSetter::MirrorReflectionMtxSetter(J3DModel* pModel, const ResourceHolder* pResourceHolder)
    : MaterialCtrl(pModel, nullptr), mNumUpdatingTexMtx() {
    for (u16 i = 0; i < 8; i++) {
        mUpdatingTexMtx[i] = nullptr;
    }

    addUpdatingTexMtxFromName(pModel);
}

void MirrorReflectionMtxSetter::addUpdatingTexMtxFromTexNo(J3DModel* pModel, u16 texNo) {
    J3DModelData* pModelData = pModel->getModelData();
    u16 materialNum = pModelData->mMaterialTable.getMaterialNum();
    for (u16 i = 0; i < materialNum; i++) {
        if (MR::isUseTex(pModelData->mMaterialTable.getMaterialNodePointer(i), texNo)) {
            addUpdatingTexMtxFromTexCoord(pModel->ownMaterial(i));
        }
    }
}

void MirrorReflectionMtxSetter::addUpdatingTexMtxFromTexCoord(J3DMaterial* pMaterial) {
    for (u32 i = 0; i < 8; i++) {
        J3DTexMtx* texMtx = pMaterial->getTexMtx(i);
        if (texMtx && (texMtx->getTexMtxInfo().mInfo & 0x3FU) == J3DTexMtxMode_Projmap && MR::isUseTexMtx(pMaterial, i)) {
            addUpdatingTexMtx(texMtx);
        }
    }
}

void MirrorReflectionMtxSetter::addUpdatingTexMtx(J3DTexMtx* pTexMtx) {
    mUpdatingTexMtx.at(mNumUpdatingTexMtx) = pTexMtx;
    mNumUpdatingTexMtx++;
}

void MirrorReflectionMtxSetter::update() {
    for (s32 i = 0; i < mNumUpdatingTexMtx; i++) {
        mUpdatingTexMtx[i]->setEffectMtx(const_cast< MtxPtr >(MR::getMirrorModelTexMtx().mMtx));
    }
}

MarioShadowProjmapMtxSetter::MarioShadowProjmapMtxSetter(J3DModel* pModel, const ResourceHolder* pResourceHolder)
    : MaterialCtrl(pModel, nullptr), mProjmapMtxSetter() {
    mProjmapMtxSetter = new ProjmapEffectMtxSetter(pModel, pResourceHolder);
}

void MarioShadowProjmapMtxSetter::update() {
    TVec3f playerPos(*MR::getPlayerPos());
    TVec3f baseTrans;
    mProjmapMtxSetter->getBaseTrans(&baseTrans);
    TVec3f offset(baseTrans);
    offset.sub(playerPos);
    TVec3f shadowVec(MR::getMarioShadowVec());
    f32 height = -1.0f;
    if (MR::isNearZero(shadowVec.length() - 1.0f)) {
        height = MR::vecKillElement(offset, shadowVec, &offset);
    }

    TPos3f effectMtx;
    TPos3f translationMtx;
    if (height > 0.0f) {
        NebulaMtxTrans(translationMtx, -playerPos.x, -playerPos.y, -playerPos.z);
    } else {
        NebulaMtxTrans(translationMtx, 1000000.0f, 1000000.0f, 1000000.0f);
    }

    TPos3f rotationMtx;
    MR::makeMtxRotate(rotationMtx, -*MR::getPlayerShadowRotate());
    NebulaMtxConcat(rotationMtx, translationMtx, effectMtx);
    mProjmapMtxSetter->mEffectMtx.set(effectMtx);
    mProjmapMtxSetter->update();
}

TexMtxCtrl::TexMtxCtrl(J3DModel* pModel, const char* pMaterialName) : MaterialCtrl(pModel, pMaterialName) {
    for (u32 i = 0; i < 8; i++) {
        mMatricies[i] = nullptr;
    }
}

void TexMtxCtrl::setTexMtx(u32 index, J3DTexMtx* pTexMtx) {
    mMatricies[index] = pTexMtx;
}

void TexMtxCtrl::updateMaterial(J3DMaterial* pMaterial) {
    for (u32 i = 0; i < 8; i++) {
        if (mMatricies[i]) {
            pMaterial->setTexMtx(i, *mMatricies[i]);
        }
    }
}

void MaterialCtrl::updateMaterial(J3DMaterial* pMaterial) {
}
