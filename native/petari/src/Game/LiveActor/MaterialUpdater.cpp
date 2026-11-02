#include "Game/LiveActor/MaterialUpdater.hpp"
#include "Game/Animation/MaterialAnmBuffer.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util.hpp"
#include <JSystem/J3DGraphAnimator/J3DAnimation.hpp>
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <algorithm>

MaterialUpdater::MaterialUpdater(J3DModel* pModel, const ResourceHolder* pResHolder) : mModel(pModel), mFogCtrl(), mResHolder(pResHolder) {
    u16 materialNum = mModel->getModelData()->getMaterialNum();
    mPrgFlag = new u32[materialNum];
    mCurFlag = new u32[materialNum];
    MR::zeroMemory(mPrgFlag, materialNum * sizeof(u32));
    MR::zeroMemory(mCurFlag, materialNum * sizeof(u32));
    mMaterialCtrl.init(materialNum * 4);
}

void MaterialUpdater::update() {
    for (auto& item : mMaterialCtrl) {
        item->update();
    }
}

void MaterialUpdater::diff() {
    for (u16 i = 0; i < mModel->getModelData()->getMaterialNum(); i++) {
        if (mCurFlag[i]) {
            j3dSys.setMatPacket(mModel->getMatPacket(i));
            mModel->getModelData()->getMaterialNodePointer(i)->diff(getDiffFlag(i));
        }
    }
}

void MaterialUpdater::newDifferedSnapshot() {
    checkMaterial();

    for (u16 i = 0; i < mModel->getModelData()->getMaterialNum(); i++) {
        u32 flag = getDiffFlag(i);
        if (flag) {
            mModel->getMatPacket(i)->getShapePacket()->newDifferedSnapshot(flag);
        }
    }
}

bool MaterialUpdater::isValidDiff() {
    return true;
}

void MaterialUpdater::onPrgFlag(u16 materialNo, u32 flag) {
    mPrgFlag[materialNo] |= flag;
}

void MaterialUpdater::onCurFlag(u16 materialNo, u32 flag) {
    mCurFlag[materialNo] |= flag;
}

u32 MaterialUpdater::getDiffFlag(s32 materialNo) const {
    u32 flag = mPrgFlag[materialNo];
    if (mResHolder->mMaterialBuf) {
        flag |= mResHolder->mMaterialBuf->getDiffFlag(materialNo);
    }

    return flag;
}

FogCtrl* MaterialUpdater::addFogCtrl(bool useAllMaterials) {
    FogCtrl* fog = new FogCtrl(mModel->mModelData, useAllMaterials);
    push(fog);
    mFogCtrl = fog;
    onBothFlagAll(0x10000000);
    return fog;
}

MatColorCtrl* MaterialUpdater::addMatColorCtrl(const char* pName, u32 color, const J3DColor* pColor) {
    MatColorCtrl* ctrl = new MatColorCtrl(mModel->mModelData, pName, color, pColor);
    push(ctrl);
    s16 materialNo = MR::getMaterialNo(mModel->mModelData, pName);
    onBothFlag(materialNo, 1);
    return ctrl;
}

TexMtxCtrl* MaterialUpdater::addTexMtxCtrl(const char* pName) {
    TexMtxCtrl* ctrl = new TexMtxCtrl(mModel->mModelData, pName);
    push(ctrl);
    s16 materialNo = MR::getMaterialNo(mModel->mModelData, pName);
    onBothFlag(materialNo, 0x200);
    return ctrl;
}

ProjmapEffectMtxSetter* MaterialUpdater::addProjmapEffectMtxSetter() {
    ProjmapEffectMtxSetter* setter = new ProjmapEffectMtxSetter(mModel, mResHolder);
    push(setter);
    return setter;
}

MirrorReflectionMtxSetter* MaterialUpdater::addMirrorReflectionMtxSetter() {
    MirrorReflectionMtxSetter* setter = new MirrorReflectionMtxSetter(mModel, mResHolder);
    push(setter);
    return setter;
}

void MaterialUpdater::offCurFlagBpk(const J3DAnmBase* pAnm) {
    MR::offDiffFlagBpk(mCurFlag, static_cast< const J3DAnmColorKey* >(pAnm), "");
}

void MaterialUpdater::onCurFlagBtp(const J3DAnmBase* pAnm) {
    MR::onDiffFlagBtp(mCurFlag, static_cast< const J3DAnmTexPattern* >(pAnm), "");
}

void MaterialUpdater::offCurFlagBtp(const J3DAnmBase* pAnm) {
    MR::offDiffFlagBtp(mCurFlag, static_cast< const J3DAnmTexPattern* >(pAnm), "");
}

void MaterialUpdater::onCurFlagBtk(const J3DAnmBase* pAnm) {
    MR::onDiffFlagBtk(mCurFlag, static_cast< const J3DAnmTextureSRTKey* >(pAnm), "");
}

void MaterialUpdater::offCurFlagBtk(const J3DAnmBase* pAnm) {
    MR::offDiffFlagBtk(mCurFlag, static_cast< const J3DAnmTextureSRTKey* >(pAnm), "");
}

void MaterialUpdater::onCurFlagBrk(const J3DAnmBase* pAnm) {
    MR::onDiffFlagBrk(mCurFlag, static_cast< const J3DAnmTevRegKey* >(pAnm), "");
}

void MaterialUpdater::offCurFlagBrk(const J3DAnmBase* pAnm) {
    MR::offDiffFlagBrk(mCurFlag, static_cast< const J3DAnmTevRegKey* >(pAnm), "");
}

void MaterialUpdater::push(MaterialCtrl* pCtrl) {
    mMaterialCtrl.push_back(pCtrl);
}

void MaterialUpdater::onBothFlag(u16 materialNo, u32 flag) {
    onPrgFlag(materialNo, flag);
    onCurFlag(materialNo, flag);
}

void MaterialUpdater::onBothFlagAll(u32 flag) {
    for (u16 i = 0; i < mModel->getModelData()->getMaterialNum(); i++) {
        onBothFlag(i, flag);
    }
}

void MaterialUpdater::checkMaterial() {
    checkTexture();

    for (u16 i = 0; i < mModel->getModelData()->getMaterialNum(); i++) {
        if (!MR::isNormalTexMtx(mModel->getModelData()->getMaterialNodePointer(i))) {
            onBothFlag(i, 0x200);
        }
    }

    checkViewProjmapEffectMtx();
}

void MaterialUpdater::checkViewProjmapEffectMtx() {
    J3DMaterial* material;
    for (u16 i = 0; i < mModel->getModelData()->getMaterialNum(); i++) {
        material = mModel->getModelData()->getMaterialNodePointer(i);
        for (u32 j = 0; j < 8; j++) {
            J3DTexMtx* texMtx = material->getTexMtx(j);
            if (texMtx && (texMtx->getTexMtxInfo().mInfo & 0x3FU) == J3DTexMtxMode_ViewProjmap && MR::isUseTexMtx(material, j)) {
                addViewProjmapEffectMtxSetter();
                return;
            }
        }
    }
}

ViewProjmapEffectMtxSetter* MaterialUpdater::addViewProjmapEffectMtxSetter() {
    ViewProjmapEffectMtxSetter* setter = new ViewProjmapEffectMtxSetter(mModel->getModelData());
    push(setter);
    return setter;
}

MarioShadowProjmapMtxSetter* MaterialUpdater::addMarioShadowProjmapMtxSetter() {
    MarioShadowProjmapMtxSetter* setter = new MarioShadowProjmapMtxSetter(mModel, mResHolder);
    push(setter);
    return setter;
}

void MaterialUpdater::onCurFlagBpk(const J3DAnmBase* pAnm) {
    MR::onDiffFlagBpk(mCurFlag, static_cast< const J3DAnmColorKey* >(pAnm), "");
}

void MaterialUpdater::checkTexture() {
    bool hasShadowSetter = false;
    for (u16 i = 0; i < mModel->getModelData()->getTexture()->getNum(); i++) {
        const char* name = mModel->getModelData()->getTextureName()->getName(i);
        if (MR::isEqualString(name, "IndDummy")) {
            for (u16 j = 0; j < mModel->getModelData()->getMaterialNum(); j++) {
                if (MR::isUseTex(mModel->getModelData()->getMaterialNodePointer(j), i)) {
                    onBothFlag(j, 0x4020000);
                }
            }

            mModel->getModelData()->getTexture()->setResTIMG(i, *MR::getScreenResTIMG());
        }

        if (MR::isEqualString(name, "ShadowProjDummy")) {
            for (u16 j = 0; j < mModel->getModelData()->getMaterialNum(); j++) {
                if (MR::isUseTex(mModel->getModelData()->getMaterialNodePointer(j), i)) {
                    onBothFlag(j, 0x4020000);
                }
            }

            const JUTTexture* texture = MR::getMarioShadowTex();
            mModel->getModelData()->getTexture()->setResTIMG(i, *texture->getTexInfo());

            if (!hasShadowSetter) {
                addMarioShadowProjmapMtxSetter();
                hasShadowSetter = true;
            }
        }
    }
}

bool MaterialUpdater::isExistDiffMaterial(const J3DModelData* pModelData) {
    if (pModelData->getTextureName()->getIndex("IndDummy") != -1) {
        return true;
    }

    if (pModelData->getTextureName()->getIndex("ShadowDummy") != -1) {
        return true;
    }

    for (u16 i = 0; i < pModelData->getMaterialNum(); i++) {
        if (!MR::isNormalTexMtx(pModelData->getMaterialNodePointer(i))) {
            return true;
        }
    }

    return false;
}
