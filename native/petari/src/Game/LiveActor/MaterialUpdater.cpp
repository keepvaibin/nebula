#include "Game/Util/Color.hpp"
#include "Game/LiveActor/MaterialUpdater.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/Material.hpp"

MaterialUpdater::MaterialUpdater(Model* pModel, const ResourceHolder* pResHolder) : mModel(pModel), mFogCtrl(), mResHolder(pResHolder) {
    mMaterialCtrl.init(mModel->getModelData()->getMaterialCount() * 4);
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

MatColorCtrl* MaterialUpdater::addMatColorCtrl(const char* pName, u32 color, const Color8* pColor) {
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

    checkViewProjmapEffectMtx();
}

void MaterialUpdater::checkViewProjmapEffectMtx() {
    Material* material;
    for (u32 i = 0; i < mModel->getModelData()->getMaterialCount(); i++) {
        material = mModel->getModelData()->getMaterialAt(i);
        for (u32 j = 0; j < material->getSlotCount(); j++) {
            CoordMatrix* texMtx = material->getCoordMatrix(j);
            if (texMtx && texMtx->getMode() == CoordMatrixMode_ViewProjmap && MR::isUseTexMtx(material, j)) {
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
    for (u32 i = 0; i < mModel->getModelData()->getTextureCount(); i++) {
        const char* name = mModel->getModelData()->getTextureName(i);
        if (MR::isEqualString(name, "IndDummy")) {
            mModel->getModelData()->setTexture(i, *MR::getScreenResTIMG());
        }

        if (MR::isEqualString(name, "ShadowProjDummy")) {
            const GameTexture* texture = MR::getMarioShadowTex();
            mModel->getModelData()->setTexture(i, *texture->info());

            if (!hasShadowSetter) {
                addMarioShadowProjmapMtxSetter();
                hasShadowSetter = true;
            }
        }
    }
}

bool MaterialUpdater::hasMovingMaterial(const ModelData* pModelData) {
    if (pModelData->findTexture("IndDummy") >= 0) {
        return true;
    }

    if (pModelData->findTexture("ShadowDummy") >= 0) {
        return true;
    }

    for (u32 i = 0; i < pModelData->getMaterialCount(); i++) {
        if (!MR::isNormalTexMtx(pModelData->getMaterialAt(i))) {
            return true;
        }
    }

    return false;
}
