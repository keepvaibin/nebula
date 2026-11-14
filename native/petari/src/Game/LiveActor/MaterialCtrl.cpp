#include "Game/Util/Color.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Util/MotionSignal.hpp"

char MirrorReflectionMtxSetter::sMirrorTextureName[] = "MirrorTex";

MaterialCtrl::MaterialCtrl(Model* pModel, const char* pMaterialName) : mModel(pModel), mMaterial() {
    if (pMaterialName) {
        mMaterial = pModel->ownMaterial(MR::getMaterialNo(pModel->getModelData(), pMaterialName));
    }
}

void MaterialCtrl::update() {
    if (mMaterial) {
        updateMaterial(mMaterial);
    }
}

FogCtrl::FogCtrl(Model* pModel, bool useAllMaterials) : MaterialCtrl(pModel, nullptr), mFogInfo(), mNumMaterials(), mMaterials() {
    ModelData* pModelData = pModel->getModelData();
    Material* firstMaterial = nullptr;
    for (u32 i = 0; i < pModelData->getMaterialCount(); i++) {
        Material* material = pModelData->getMaterialAt(i);
        if (useAllMaterials || material->getFog()->mType) {
            if (!firstMaterial) {
                firstMaterial = material;
            }

            mNumMaterials++;
        }
    }

    // A model with no materials (a hidden one: `{"hide": true}`, models.md section 2) has no fog to keep: none, and
    // nothing for update() to touch.
    if (!firstMaterial && pModelData->getMaterialCount() != 0) {
        firstMaterial = pModelData->getMaterialAt(0);
    }

    if (firstMaterial) {
        mFogInfo = *firstMaterial->getFog();
    }
    if (mNumMaterials > 0) {
        mMaterials = new Material*[mNumMaterials];
        s32 materialCount = 0;
        for (u32 i = 0; i < pModelData->getMaterialCount(); i++) {
            Material* material = pModelData->getMaterialAt(i);
            if (useAllMaterials || material->getFog()->mType) {
                mMaterials[materialCount++] = pModel->ownMaterial(i);
            }
        }
    }
}

void FogCtrl::update() {
    for (s32 i = 0; i < mNumMaterials; i++) {
        *mMaterials[i]->getFog() = mFogInfo;
        mMaterials[i]->touch();
    }
}

MatColorCtrl::MatColorCtrl(Model* pModel, const char* pName, u32 color, const Color8* pColor) : MaterialCtrl(pModel, pName) {
    mColorChoice = color;
    mColor = pColor;
}

void MatColorCtrl::updateMaterial(Material* pMaterial) {
    const NebulaColor8 color = *mColor;
    pMaterial->setMaterialColor(mColorChoice, &color);
}

ViewProjmapEffectMtxSetter::ViewProjmapEffectMtxSetter(Model* pModel) : MaterialCtrl(pModel, nullptr), mMatricies(), mNumMatricies() {
    ModelData* pModelData = pModel->getModelData();
    const auto each = [&](auto visit) {
        for (u32 i = 0; i < pModelData->getMaterialCount(); i++) {
            Material* material = pModelData->getMaterialAt(i);
            for (u32 j = 0; j < material->getSlotCount(); j++) {
                CoordMatrix* matrix = material->getCoordMatrix(j);
                if (matrix && matrix->getMode() == CoordMatrixMode_ViewProjmap && MR::isUseTexMtx(material, j)) {
                    visit(i, j);
                }
            }
        }
    };
    each([&](u32, u32) { ++mNumMatricies; });
    mMatricies = new CoordMatrixInfo*[mNumMatricies];
    u32 next = 0;
    each([&](u32 material, u32 slot) { mMatricies[next++] = &pModel->ownMaterial(material)->getCoordMatrix(slot)->getInfo(); });

}

void ViewProjmapEffectMtxSetter::update() {
    Mtx44f projection = MR::getCameraProjectionMtx();
    projection[2][0] = 0.0f;
    projection[2][1] = 0.0f;
    projection[2][2] = -1.0f;
    projection[2][3] = 0.0f;
    projection[3][0] = 0.0f;
    projection[3][1] = 0.0f;
    projection[3][2] = 0.0f;
    projection[3][3] = 1.0f;
    for (s32 i = 0; i < mNumMatricies; i++) {
        memcpy(mMatricies[i]->mEffectMtx, &projection, sizeof(Mtx44));
    }
}

ProjmapEffectMtxSetter::ProjmapEffectMtxSetter(Model* pModel, const ResourceHolder* pResourceHolder)
    : MaterialCtrl(pModel, nullptr), mUpdatingMtxInfo(), mNumUpdatingMtx() {
    ModelData* modelData = pModel->getModelData();
    mEffectMtx.identity();
    for (u32 i = 0; i < modelData->getMaterialCount(); i++) {
        Material* material = modelData->getMaterialAt(i);
        for (u32 j = 0; j < material->getSlotCount(); j++) {
            CoordMatrix* texMtx = material->getCoordMatrix(j);
            if (texMtx && texMtx->getMode() == CoordMatrixMode_Projmap && MR::isUseTexMtx(material, j)) {
                mNumUpdatingMtx++;
            }
        }
    }

    mUpdatingMtxInfo = new UpdateEffectMtxInfo[mNumUpdatingMtx];
    s32 matrixCount = 0;
    for (u32 i = 0; i < modelData->getMaterialCount(); i++) {
        Material* material = modelData->getMaterialAt(i);
        for (u32 j = 0; j < material->getSlotCount(); j++) {
            CoordMatrix* texMtx = material->getCoordMatrix(j);
            if (texMtx && texMtx->getMode() == CoordMatrixMode_Projmap && MR::isUseTexMtx(material, j)) {
                mUpdatingMtxInfo[matrixCount].mTexMtx = pModel->ownMaterial(i)->getCoordMatrix(j);
                mUpdatingMtxInfo[matrixCount].mInitEffectMtx.set(pResourceHolder->getInitEffectMtx(i, j));
                matrixCount++;
            }
        }
    }
}

void ProjmapEffectMtxSetter::update() {
    for (s32 i = 0; i < mNumUpdatingMtx; i++) {
        UpdateEffectMtxInfo* info = &mUpdatingMtxInfo[i];
        Pos3f effectMtx;
        effectMtx.concat(info->mInitEffectMtx, mEffectMtx);
        info->mTexMtx->setEffectMtx(effectMtx);
    }
}

void ProjmapEffectMtxSetter::getBaseTrans(Vec3f* pTrans) const {
    pTrans->set< f32 >(mModel->getBaseTRMtx()[0][3], mModel->getBaseTRMtx()[1][3], mModel->getBaseTRMtx()[2][3]);
}

void ProjmapEffectMtxSetter::updateMtxUseBaseMtx() {
    Pos3f baseMtx;
    baseMtx.set(mModel->getBaseTRMtx());
    mEffectMtx.invert(baseMtx);
}

void ProjmapEffectMtxSetter::updateMtxUseBaseMtxWithLocalOffset(const Vec3f& rOffset) {
    Pos3f baseMtx;
    baseMtx.set(mModel->getBaseTRMtx());
    Pos3f offsetMtx;
    offsetMtx.identity33();
    offsetMtx.setTrans(rOffset);
    baseMtx.concat(offsetMtx);
    mEffectMtx.invert(baseMtx);
}

MirrorReflectionMtxSetter::MirrorReflectionMtxSetter(Model* pModel, const ResourceHolder* pResourceHolder)
    : MaterialCtrl(pModel, nullptr), mNumUpdatingTexMtx() {
    addUpdatingTexMtxFromName(pModel);
}

void MirrorReflectionMtxSetter::addUpdatingTexMtxFromTexNo(Model* pModel, u32 texNo) {
    ModelData* pModelData = pModel->getModelData();
    u32 materialNum = pModelData->getMaterialCount();
    for (u32 i = 0; i < materialNum; i++) {
        if (MR::isUseTex(pModelData->getMaterialAt(i), texNo)) {
            addUpdatingTexMtxFromTexCoord(pModel->ownMaterial(i));
        }
    }
}

void MirrorReflectionMtxSetter::addUpdatingTexMtxFromTexCoord(Material* pMaterial) {
    for (u32 i = 0; i < pMaterial->getSlotCount(); i++) {
        CoordMatrix* texMtx = pMaterial->getCoordMatrix(i);
        if (texMtx && texMtx->getMode() == CoordMatrixMode_Projmap && MR::isUseTexMtx(pMaterial, i)) {
            addUpdatingTexMtx(texMtx);
        }
    }
}

void MirrorReflectionMtxSetter::addUpdatingTexMtx(CoordMatrix* pTexMtx) {
    mUpdatingTexMtx.at(mNumUpdatingTexMtx) = pTexMtx;
    mNumUpdatingTexMtx++;
}

void MirrorReflectionMtxSetter::update() {
    for (s32 i = 0; i < mNumUpdatingTexMtx; i++) {
        mUpdatingTexMtx[i]->setEffectMtx(const_cast< MtxPtr >(MR::getMirrorModelTexMtx().mMtx));
    }
}

MarioShadowProjmapMtxSetter::MarioShadowProjmapMtxSetter(Model* pModel, const ResourceHolder* pResourceHolder)
    : MaterialCtrl(pModel, nullptr), mProjmapMtxSetter(), mPlayerLife(0), mHeightState(-1) {
    mProjmapMtxSetter = new ProjmapEffectMtxSetter(pModel, pResourceHolder);
}

void MarioShadowProjmapMtxSetter::update() {
    Vec3f playerPos(*MR::getPlayerPos());
    Vec3f baseTrans;
    mProjmapMtxSetter->getBaseTrans(&baseTrans);
    Vec3f offset(baseTrans);
    offset.sub(playerPos);
    Vec3f shadowVec(MR::getMarioShadowVec());
    f32 height = -1.0f;
    if (MR::isNearZero(shadowVec.length() - 1.0f)) {
        height = MR::vecKillElement(offset, shadowVec, &offset);
    }

    // The matrix is a projection about the player: a generated frame blends it with the step before. A player placed far
    // away (MotionSignal::playerPlaced: a new life) would slide the projection across the receiver, and the 1e6
    // translation of a receiver that is not below the player would sweep the shadow through it; either starts new item
    // numbers (the model's pose keeps its pair), so that step's constants draw as they are.
    const u32 playerLife = MotionSignal::playerLife();
    const s32 heightState = height > 0.0f ? 1 : 0;
    if (mHeightState >= 0 && (playerLife != mPlayerLife || heightState != mHeightState)) {
        ++mModel->mItemJumps;
    }
    mPlayerLife = playerLife;
    mHeightState = heightState;

    Pos3f effectMtx;
    Pos3f translationMtx;
    if (height > 0.0f) {
        NebulaMtxTrans(translationMtx, -playerPos.x, -playerPos.y, -playerPos.z);
    } else {
        NebulaMtxTrans(translationMtx, 1000000.0f, 1000000.0f, 1000000.0f);
    }

    Pos3f rotationMtx;
    MR::makeMtxRotate(rotationMtx, -*MR::getPlayerShadowRotate());
    NebulaMtxConcat(rotationMtx, translationMtx, effectMtx);
    mProjmapMtxSetter->mEffectMtx.set(effectMtx);
    mProjmapMtxSetter->update();
}

TexMtxCtrl::TexMtxCtrl(Model* pModel, const char* pMaterialName) : MaterialCtrl(pModel, pMaterialName) {
}

void TexMtxCtrl::setTexMtx(u32 index, CoordMatrix* pTexMtx) {
    mMatricies.at(static_cast< s32 >(index)) = pTexMtx;
}

void TexMtxCtrl::updateMaterial(Material* pMaterial) {
    for (s32 i = 0; i < mMatricies.size(); i++) {
        if (mMatricies[i]) {
            pMaterial->setCoordMatrix(i, *mMatricies[i]);
        }
    }
}

void MaterialCtrl::updateMaterial(Material* /*pMaterial*/) {
}
