#include "Game/LiveActor/MaterialUpdater.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util.hpp"
#include <JSystem/J3DGraphAnimator/J3DAnimation.hpp>
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <algorithm>

MaterialUpdater::MaterialUpdater(J3DModel* pModel, const ResourceHolder* pResHolder) : mModel(pModel), mFogCtrl(), mResHolder(pResHolder) {
    mMaterialCtrl.init(mModel->getModelData()->getMaterialNum() * 4);
}

void MaterialUpdater::update() {
    for (auto& item : mMaterialCtrl) {
        item->update();
    }
}

void MaterialUpdater::prepareMaterials() {
    checkMaterial();
}

FogCtrl* MaterialUpdater::addFogCtrl(bool useAllMaterials) {
    FogCtrl* fog = new FogCtrl(mModel, useAllMaterials);
    push(fog);
    mFogCtrl = fog;
    return fog;
}

MatColorCtrl* MaterialUpdater::addMatColorCtrl(const char* pName, u32 color, const J3DColor* pColor) {
    MatColorCtrl* ctrl = new MatColorCtrl(mModel, pName, color, pColor);
    push(ctrl);
    return ctrl;
}

TexMtxCtrl* MaterialUpdater::addTexMtxCtrl(const char* pName) {
    TexMtxCtrl* ctrl = new TexMtxCtrl(mModel, pName);
    push(ctrl);
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

void MaterialUpdater::push(MaterialCtrl* pCtrl) {
    mMaterialCtrl.push_back(pCtrl);
}

void MaterialUpdater::checkMaterial() {
    checkTexture();

    for (u16 i = 0; i < mModel->getModelData()->getMaterialNum(); i++) {
        if (!MR::isNormalTexMtx(mModel->getModelData()->getMaterialNodePointer(i))) {
            mModel->ownMaterial(i);
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
    ViewProjmapEffectMtxSetter* setter = new ViewProjmapEffectMtxSetter(mModel);
    push(setter);
    return setter;
}

MarioShadowProjmapMtxSetter* MaterialUpdater::addMarioShadowProjmapMtxSetter() {
    MarioShadowProjmapMtxSetter* setter = new MarioShadowProjmapMtxSetter(mModel, mResHolder);
    push(setter);
    return setter;
}

void MaterialUpdater::checkTexture() {
    bool hasShadowSetter = false;
    for (u16 i = 0; i < mModel->getModelData()->getTexture()->getNum(); i++) {
        const char* name = mModel->getModelData()->getTextureName()->getName(i);
        if (MR::isEqualString(name, "IndDummy")) {
            mModel->getModelData()->getTexture()->setResTIMG(i, *MR::getScreenResTIMG());
        }

        if (MR::isEqualString(name, "ShadowProjDummy")) {
            const JUTTexture* texture = MR::getMarioShadowTex();
            mModel->getModelData()->getTexture()->setResTIMG(i, *texture->getTexInfo());

            if (!hasShadowSetter) {
                addMarioShadowProjmapMtxSetter();
                hasShadowSetter = true;
            }
        }
    }
}

bool MaterialUpdater::hasMovingMaterial(const J3DModelData* pModelData) {
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
