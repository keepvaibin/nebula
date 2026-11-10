#include "Game/Render/FrameList.hpp"
#include "Game/MapObj/NormalMapBase.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Model.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/Transform.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include <cmath>
#include <cstring>
#include <nebula_format.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f1, "updateLightMtx.f1", 100000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f2, "updateLightMtx.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f3, "updateLightMtx.f3", 1.0077f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f4, "updateLightMtx.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f5, "updateLightMtx.f5", 1.0077f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f6, "updateLightMtx.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f7, "updateLightMtx.f7", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f8, "updateLightMtx.f8", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f9, "updateLightMtx.f9", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f10, "updateLightMtx.f10", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateLightMtx_f11, "updateLightMtx.f11", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawMaterialSetting_f1, "drawMaterialSetting.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawMaterialSetting_f2, "drawMaterialSetting.f2", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawMaterialSetting_f3, "drawMaterialSetting.f3", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawMaterialSetting_f4, "drawMaterialSetting.f4", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawMaterialSetting_f5, "drawMaterialSetting.f5", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawMaterialSetting_f6, "drawMaterialSetting.f6", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawMaterialSetting_f7, "drawMaterialSetting.f7", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawMaterialSetting_f8, "drawMaterialSetting.f8", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSetting_f1, "drawSetting.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSetting_f2, "drawSetting.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setStageForObject_f1, "setStageForObject.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setStageForObject_f2, "setStageForObject.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_gradLevel_f1, "gradLevel.f1", 256.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_gradLevel_f2, "gradLevel.f2", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_gradLevel_f3, "gradLevel.f3", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_gradLevel_f4, "gradLevel.f4", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createGradTexture_f1, "createGradTexture.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createGradTexture_f2, "createGradTexture.f2", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_swapColorGB_f1, "swapColorGB.f1", 128.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_swapColorGB_f2, "swapColorGB.f2", 128.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_swapColorGB_f3, "swapColorGB.f3", 128.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_swapColorGB_f4, "swapColorGB.f4", 128.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_swapColorGB_f5, "swapColorGB.f5", 127.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_swapColorGB_f6, "swapColorGB.f6", 128.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_swapColorGB_f7, "swapColorGB.f7", 127.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_swapColorGB_f8, "swapColorGB.f8", 128.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_swapColorGB_f9, "swapColorGB.f9", 127.0f);

namespace {
    // The gradient texture's size in texels.
    const u32 cGradTexWidth = 0x100;
    const u32 cGradTexHeight = 0x100;

    // The object's native material (runtime/src/render/materials/normal_map.hlsl): drawSetting and setStageForObject
    // set its program, each normal-mapped shape its textures, colours and indirect matrix (drawMaterialSetting).
    SiteMaterial sNormalMap;
}  // namespace

NormalMapBase::NormalMapBase(const char* pName) : LiveActor(pName), mTextures(nullptr) {
    mTexFlags = 0;
    mGradTexMode = 0;
    mBackLightMode = 0;
}

void NormalMapBase::setup(const char* pName) {
    initModelManagerWithAnm(pName, nullptr, false);
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_MapObj, MR::DrawBufferType_None, MR::DrawType_NormalMap);
    MR::initLightCtrlNoDrawMapObj(this);
    setupLighting();
    setupTexture();
    MR::invalidateClipping(this);
    appear();
}

void NormalMapBase::initNormalMap() {
    setupLighting();
    setupTexture();
}

void NormalMapBase::setupLighting() {
    mStageColor0.r = 0;
    mStageColor0.g = 0;
    mStageColor0.b = 0;
    mStageColor0.a = 0;
    mStageColor1.r = 0;
    mStageColor1.g = 0;
    mStageColor1.b = 0;
    mStageColor1.a = 0;
    mDrawFlags = 0;
    mLightAMode = 0;
    mLightBMode = 1;
    mHardLightColorMask = 0;
    mHardLightAlphaMask = 4;
    mUseModelStageColor = 0;
    mUseModelLightReg = 0;
    mLightingHighLevel = 255;
    mLightingLowLevel = 0;
    mAmbColor = 64;
    mMatColor = 255;
    mAmbAlpha = 128;
    mMatAlpha = 255;
    MR::FormattedName fileName("%s.nrm.txt", MR::getModelResName(this));
    if (MR::getResourceHolder(this)->mFileInfoTable->isExistRes(fileName.c_str())) {
        const char* pData = static_cast< const char* >(MR::getResourceHolder(this)->mFileInfoTable->getRes(fileName.c_str()));
        loadData(pData, MR::getResourceHolder(this)->mFileInfoTable->findFileInfo(fileName.c_str())->mSize);
    }
}

void NormalMapBase::setupTexture() {
    mNormalTex = nullptr;
    const u32 textureCount = MR::getModelData(this)->getTextureCount();
    delete[] mTextures;
    mTextures = new GameTexture*[textureCount];
    for (u32 i = 0; i < textureCount; i++) {
        const ResTIMG* pImage = MR::getResTIMG(this, i);
        const char* pName = MR::getTextureName(this, i);
        if (strstr(pName, "Nswap_")) {
            MR::FormattedName fileName("%s.bti", pName + 6);
            // The model's own picture stays when the archive lacks the swap file.
            const ResTIMG* pSwapImage = MR::getTexFromArc(fileName.c_str(), this);
            if (pSwapImage != nullptr) {
                pImage = pSwapImage;
            }
            mNormalTex = new GameTexture(pImage);
            mTextures[i] = mNormalTex;
            mTextures[i]->setFilter(NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
        } else if (strstr(pName, "ormal") && !mNormalTex) {
            mNormalTex = new GameTexture(pImage);
            mTextures[i] = mNormalTex;
        } else {
            mTextures[i] = new GameTexture(pImage);
        }

        if (strstr(pName, "Zormal")) {
            swapColorGB(mTextures[i]);
        }
    }

    mImagePtr = new (Arenas::scene(), 0) u16[::cGradTexWidth * ::cGradTexHeight];
    createGradTexture();
}

void NormalMapBase::control() {
}

void NormalMapBase::calcAnim() {
    LiveActor::calcAnim();
    MR::getModel(this)->viewCalc();
}

void NormalMapBase::draw() const {
    if (!MR::isDead(this)) {
        MR::loadActorLight(this);
        MR::loadViewMtx();
        MR::loadProjectionMtx();
        const Pos3f& rCameraViewMtx = MR::getCameraViewMtx();
        Pos3f lightMtx;
        Mtx mtx;
        NebulaMtxIdentity(mtx);
        MR::makeMtxTRS(mtx, this);
        MR::multMtx(mtx, mtx, rCameraViewMtx);
        NebulaMtxCopy(mtx, mDrawMtx);

        MR::makeMtxTR(lightMtx, this);
        MR::normalize(&lightMtx);
        drawSetting(lightMtx);
        shapeAnalyzeDraw(MR::getModel(this));
        standardDraw(MR::getModel(this));
    }
}

void NormalMapBase::updateLightMtx(MtxPtr pMtx) const {
    Pos3f inverse;
    f32 indMtx[2][3];
    Vec3f lightA;
    Vec3f lightB;
    Vec3f localA;
    Vec3f localB;
    switch (mLightAMode) {
    case 0:
        MR::calcLightPos0(&lightA, this);
        break;
    case 1:
        MR::calcLightPos1(&lightA, this);
        break;
    default:
        lightA.zero();
        break;
    }

    MR::normalizeOrZero(&lightA);
    switch (mLightBMode) {
    case 1:
        MR::calcLightPos1(&lightB, this);
        break;
    case 2:
        lightB = mPosition - MR::getCamZdir() * cNum_updateLightMtx_f1;
        break;
    default:
        lightB.zero();
        break;
    }

    MR::normalizeOrZero(&lightB);
    localA = lightA;
    localB = lightB;
    NebulaMtxInverse(getBaseMtx(), inverse);
    MR::normalize(&inverse);
    NebulaMtxMultVecSR(inverse, &localA, &localA);
    NebulaMtxMultVecSR(inverse, &localB, &localB);
    if (pMtx) {
        Vec3f light(localA * cNum_updateLightMtx_f2);
        light *= cNum_updateLightMtx_f3;
        indMtx[0][0] = light.x * pMtx[0][0] + light.y * pMtx[1][0] + light.z * pMtx[2][0];
        indMtx[0][1] = light.x * pMtx[0][1] + light.y * pMtx[1][1] + light.z * pMtx[2][1];
        indMtx[0][2] = light.x * pMtx[0][2] + light.y * pMtx[1][2] + light.z * pMtx[2][2];
        light = localB * cNum_updateLightMtx_f4;
        light *= cNum_updateLightMtx_f5;
        indMtx[1][0] = light.x * pMtx[0][0] + light.y * pMtx[1][0] + light.z * pMtx[2][0];
        indMtx[1][1] = light.x * pMtx[0][1] + light.y * pMtx[1][1] + light.z * pMtx[2][1];
        indMtx[1][2] = light.x * pMtx[0][2] + light.y * pMtx[1][2] + light.z * pMtx[2][2];
    } else {
        indMtx[0][0] = cNum_updateLightMtx_f6 * -localA.x;
        indMtx[0][1] = cNum_updateLightMtx_f7 * -localA.y;
        indMtx[0][2] = cNum_updateLightMtx_f8 * -localA.z;
        indMtx[1][0] = cNum_updateLightMtx_f9 * -localB.x;
        indMtx[1][1] = cNum_updateLightMtx_f10 * -localB.y;
        indMtx[1][2] = cNum_updateLightMtx_f11 * -localB.z;
    }

    // The indirect matrix (scale exponent 0) as the material's rows.
    for (u32 c = 0; c < 3; c++) {
        sNormalMap.constants[16 + c] = indMtx[0][c];
        sNormalMap.constants[20 + c] = indMtx[1][c];
    }
}

bool NormalMapBase::isNormalMapMaterial(const char* pName) const {
    if (strstr(pName, "Normal")) {
        return true;
    }

    return strstr(pName, "Nrm") != 0;
}

void NormalMapBase::standardDraw(Model* pModel) const {
    if ((mDrawFlags & 0x10) == 0) {
        for (u32 i = 0; i < pModel->mModelData->getMaterialCount(); i++) {
            Material* pMaterial = pModel->mModelData->getMaterialAt(i);
            const char* pMaterialName = MR::getMaterialName(pModel->mModelData, pMaterial->mIndex);
            if (!isNormalMapMaterial(pMaterialName)) {
                MR::simpleDraw(pModel, pMaterial);
            }
        }
    }
}

void NormalMapBase::shapeAnalyzeDraw(Model* pModel) const {
    for (u32 i = 0; i < pModel->mModelData->getMeshCount(); i++) {
        Mesh* pShape = pModel->mModelData->getMeshAt(i);
        bool found = false;
        for (u32 j = 0; j < pModel->mModelData->getJointCount(); j++) {
            Joint* pJoint = pModel->mModelData->getJointAt(j);
            Material* pMaterial = pJoint->getMesh();
            while (pMaterial) {
                const char* pName = MR::getMaterialName(pModel->mModelData, pMaterial->mIndex);
                if (!isNormalMapMaterial(pName)) {
                    pMaterial = pMaterial->mNext;
                    continue;
                }

                if (pMaterial->getMesh() == pShape) {
                    Pos3f localMtx;
                    Pos3f viewMtx;
                    const TransformInfo& rTransform = pJoint->getTransformInfo();
                    f32 rotationZ = rTransform.mRotation.z * Angle::cRadianToDegree;
                    f32 rotationY = rTransform.mRotation.y * Angle::cRadianToDegree;
                    f32 rotationX = rTransform.mRotation.x * Angle::cRadianToDegree;
                    MR::makeMtxTRS(localMtx, rTransform.mTranslate.x, rTransform.mTranslate.y, rTransform.mTranslate.z, rotationX, rotationY,
                                   rotationZ, rTransform.mScale.x, rTransform.mScale.y, rTransform.mScale.z);
                    NebulaMtxConcat(mDrawMtx, localMtx, viewMtx);
                    if (pJoint->getMtxType() == 1) {
                        billboardMtx(viewMtx);
                        Pos3f inverse;
                        NebulaMtxInverse(mDrawMtx, inverse);
                        NebulaMtxConcat(inverse, viewMtx, localMtx);
                    } else if (pJoint->getMtxType() == 2) {
                        billboardYMtx(viewMtx);
                        Pos3f inverse;
                        NebulaMtxInverse(mDrawMtx, inverse);
                        NebulaMtxConcat(inverse, viewMtx, localMtx);
                    }

                    MR::normalize(&localMtx);
                    updateLightMtx(localMtx);
                    drawMaterialSetting(pModel->getMaterial(pMaterial->mIndex));
                    found = true;
                    break;
                }

                pMaterial = pMaterial->mNext;
            }

            if (found) {
                break;
            }
        }

        if (found) {
            drawShape(pModel, pShape);
        }
    }
}

void NormalMapBase::drawShape(Model* pModel, Mesh* pShape) const {
    TypedDraw site(sNormalMap, nullptr, MR::getCameraViewMtx());
    site.setModelBlock(pModel->getModelData(), pModel->block(pShape->getMaterial()));
    site.drawMesh(pShape);
}

void NormalMapBase::loadDiffuseGradTex(u32 slot) const {
    SiteTexture obj;
    SiteTextures::init(&obj, mImagePtr, ::cGradTexWidth, ::cGradTexHeight, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);

    if (mTexFlags & 1) {
        SiteTextures::setFilter(&obj, NEBULA_FILTER_NEAREST, NEBULA_FILTER_NEAREST, 0.0f, 0.0f, 0.0f, false, 0);
    } else {
        SiteTextures::setFilter(&obj, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR, 0.0f, 0.0f, 0.0f, false, 0);
    }

    sNormalMap.setTexture(slot, obj);
}

void NormalMapBase::loadNormalTex(const GameTexture* pTex, u32 slot) const {
    SiteTexture obj;
    SiteTextures::init(&obj, pTex->image(), pTex->info()->mWidth, pTex->info()->mHeight, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
    if ((mTexFlags & 2) != 0 && (mTexFlags & 0x4) != 0) {
        SiteTextures::setFilter(&obj, NEBULA_FILTER_NEAREST, NEBULA_FILTER_NEAREST, 0.0f, 0.0f, 0.0f, false, 0);
    } else if ((mTexFlags & 0x2) != 0) {
        SiteTextures::setFilter(&obj, NEBULA_FILTER_NEAREST, NEBULA_FILTER_LINEAR, 0.0, 0.0, 0.0, 0, 0);
    } else if ((mTexFlags & 0x4) != 0) {
        SiteTextures::setFilter(&obj, NEBULA_FILTER_LINEAR, NEBULA_FILTER_NEAREST, 0.0, 0.0, 0.0, 0, 0);
    } else {
        SiteTextures::setFilter(&obj, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR, 0.0, 0.0, 0.0, 0, 0);
    }

    sNormalMap.setTexture(slot, obj);
}

void NormalMapBase::loadTexture1(Material* pMaterial) const {
    // A material without the two pictures keeps the ones the last material set.
    const u32 textureCount = MR::getModelData(this)->getTextureCount();
    const u32 baseIndex = pMaterial->getTextureIndex(0);
    const u32 normalIndex = pMaterial->getTextureIndex(1);

    if (baseIndex < textureCount) {
        sNormalMap.setTexture(2, mTextures[baseIndex]);
    }

    if (normalIndex < textureCount) {
        loadNormalTex(mTextures[normalIndex], 0);
    }
}

void NormalMapBase::drawMaterialSetting(Material* pMaterial) const {
    loadTexture1(pMaterial);
    // A material with no texture matrix uses the identity.
    CoordMatrix* pCoordMatrix = pMaterial->getCoordMatrix(0);
    if (pCoordMatrix != nullptr) {
        sNormalMap.setMatrix(9, 2, pCoordMatrix->getMtx());
    } else {
        Mtx identity;
        NebulaMtxIdentity(identity);
        sNormalMap.setMatrix(9, 2, identity);
    }
    Color8 ambient;
    Color8 material;
    ambient.set(mAmbColor, mAmbColor, mAmbColor, mAmbAlpha);
    material.set(mMatColor, mMatColor, mMatColor, mMatAlpha);
    if (mUseModelLightReg & 1) {
        NebulaColor8* pColor = pMaterial->getAmbientColor(0);
        ambient.r = pColor->r;
        ambient.g = pColor->g;
        ambient.b = pColor->b;
    }

    if (mUseModelLightReg & 2) {
        ambient.a = pMaterial->getAmbientColor(0)->a;
    }

    if (mUseModelLightReg & 4) {
        NebulaColor8* pColor = pMaterial->getMaterialColor(0);
        material.r = pColor->r;
        material.g = pColor->g;
        material.b = pColor->b;
    }

    if (mUseModelLightReg & 8) {
        material.a = pMaterial->getMaterialColor(0)->a;
    }

    sNormalMap.setColor(0, material.r, material.g, material.b, material.a);
    sNormalMap.setColor(4, ambient.r, ambient.g, ambient.b, ambient.a);
    const NebulaColor16& rColor0 = mUseModelStageColor ? static_cast< const NebulaColor16& >(*pMaterial->getRegisterColor(0))
                                                : static_cast< const NebulaColor16& >(mStageColor0);
    const NebulaColor16& rColor1 = mUseModelStageColor ? static_cast< const NebulaColor16& >(*pMaterial->getRegisterColor(1))
                                                : static_cast< const NebulaColor16& >(mStageColor1);
    // The two stage colours as the shader reads them: the low 8 bits of each channel, 0 to 1.
    sNormalMap.constants[8] = static_cast< u8 >(rColor0.r) / cNum_drawMaterialSetting_f1;
    sNormalMap.constants[9] = static_cast< u8 >(rColor0.g) / cNum_drawMaterialSetting_f2;
    sNormalMap.constants[10] = static_cast< u8 >(rColor0.b) / cNum_drawMaterialSetting_f3;
    sNormalMap.constants[11] = static_cast< u8 >(rColor0.a) / cNum_drawMaterialSetting_f4;
    sNormalMap.constants[12] = static_cast< u8 >(rColor1.r) / cNum_drawMaterialSetting_f5;
    sNormalMap.constants[13] = static_cast< u8 >(rColor1.g) / cNum_drawMaterialSetting_f6;
    sNormalMap.constants[14] = static_cast< u8 >(rColor1.b) / cNum_drawMaterialSetting_f7;
    sNormalMap.constants[15] = static_cast< u8 >(rColor1.a) / cNum_drawMaterialSetting_f8;
}

void NormalMapBase::drawSetting(MtxPtr pMtx) const {
    sNormalMap.init(NEBULA_SHADER_NORMAL_MAP, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_BACK);
    sNormalMap.mState.color_write = 1;
    sNormalMap.mState.alpha_write = 0;
    updateLightMtx(pMtx);
    Mtx scale_mtx;
    NebulaMtxScale(scale_mtx, 0.0f, 0.0f, 0.0f);
    MR::setMtxTrans(scale_mtx, cNum_drawSetting_f1, cNum_drawSetting_f2, 0.0f);
    sNormalMap.setMatrix(9, 1, scale_mtx);

    // Channel 0: lit by the hard light mask with the ambient colour from the material and the material colour from
    // the vertex; else by no lights from the material's colours (the model's light registers) or the vertex's. Its
    // alpha: the alpha mask's lights, signed, from the material.
    u32 sources;
    if (mHardLightColorMask) {
        sources = 1;
    } else if (mUseModelLightReg) {
        sources = 0;
    } else {
        sources = 3;
    }
    sNormalMap.constants[24] = static_cast< f32 >(mHardLightColorMask & 0xFF);
    sNormalMap.constants[25] = static_cast< f32 >(mHardLightAlphaMask & 0xFF);
    sNormalMap.constants[26] = static_cast< f32 >(sources);
    setStageForObject();
    loadDiffuseGradTex(1);
}

// The stages' selections (normal_map.hlsl): light A and B's colours, the raster, light B's alpha, the back-light mode.
void NormalMapBase::setStageForObject() const {
    // Light A: one, none where the colour mask leaves light 0 out, the gradient for modes 0 and 1. Light B: one, none
    // where the mask leaves light 1 out, stage 0's alpha for mode 1.
    f32 lightA = (mHardLightColorMask & 0x1) == 0 ? 0.0f : 1.0f;
    f32 lightB = (mHardLightColorMask & 0x2) == 0 ? 0.0f : 1.0f;
    if (mLightAMode <= 1) {
        lightA = cNum_setStageForObject_f1;
    }
    if (mLightBMode == 1) {
        lightB = cNum_setStageForObject_f2;
    }
    sNormalMap.constants[27] = mBackLightMode;
    sNormalMap.constants[28] = lightA;
    sNormalMap.constants[29] = lightB;
    sNormalMap.constants[30] = mBackLightMode == 4 ? 1.0f : 0.0f;
    sNormalMap.constants[31] = mLightBMode == 2 ? 1.0f : 0.0f;
    sNormalMap.constants[33] = 1.0f;
    sNormalMap.constants[34] = ::cGradTexWidth;
    sNormalMap.constants[35] = ::cGradTexHeight;
}

namespace {
    // The gradient's level at `index` of `count` steps (the light's ramp between the low and high levels).
    s32 gradLevel(u32 index, u32 count, s32 low, s32 high, s32 mode) {
        s32 value = index * (cNum_gradLevel_f1 / count);
        if (value < low) {
            return 0;
        }
        if (value >= high) {
            return 255;
        }
        f32 rate = 1.0f - static_cast< f32 >(high - value) / (high - low);
        switch (mode) {
        case 0:
            value = cNum_gradLevel_f2 * rate;
            break;
        case 1:
            value = cNum_gradLevel_f3 * (rate * rate);
            break;
        case 2:
            value = cNum_gradLevel_f4 * MR::sqrt(rate);
            break;
        }
        return value;
    }

    u8 sGradIntensity[0x100];
    u8 sGradAlpha[0x100];
}  // namespace

// The light gradient as LA8 rows (NEBULA_TEXTURE_LINEAR_LA8): intensity by column (the ambient-to-material ramp),
// alpha by row (the same ramp down the rows, none when mDrawFlags & 1), laid out as the picture the game has always shown.
// The game wrote both ramps as two-channel texels in rows and ran its tile conversion on them, which leaves the
// first 2 KB as each 4-row band's first eight intensities (two to a texel: alpha, then intensity) and the rest as the
// rows' bytes read as 4x4 tiles: texel (x, y) here is what those tiles showed there.
void NormalMapBase::createGradTexture() {
    for (u32 x = 0; x < ::cGradTexWidth; x++) {
        const f32 ratio = gradLevel(x, ::cGradTexWidth, mLightingLowLevel, mLightingHighLevel, mGradTexMode) / cNum_createGradTexture_f1;
        sGradIntensity[x] = static_cast< u8 >(static_cast< s32 >(mAmbColor + ratio * (mMatColor - mAmbColor)));
    }
    for (u32 y = 0; y < ::cGradTexHeight; y++) {
        if (mDrawFlags & 1) {
            sGradAlpha[y] = 0;
            continue;
        }
        const f32 ratio = gradLevel(y, ::cGradTexHeight, mLightingLowLevel, mLightingHighLevel, mGradTexMode) / cNum_createGradTexture_f2;
        sGradAlpha[y] = static_cast< u8 >(static_cast< s32 >(mAmbColor + ratio * (mMatColor - mAmbColor)));
    }

    u8* pTexels = static_cast< u8* >(mImagePtr);
    const u32 tilesAcross = ::cGradTexWidth / 4;
    for (u32 y = 0; y < ::cGradTexHeight; y++) {
        for (u32 x = 0; x < ::cGradTexWidth; x++) {
            const u32 tile = (y / 4) * tilesAcross + x / 4;
            const u32 inTile = (y % 4) * 4 + x % 4;
            u8 alpha;
            u8 intensity;
            if (tile < ::cGradTexHeight / 4) {
                // Band `tile`'s entries 2 * inTile and 2 * inTile + 1: columns 0-3 of its rows, then columns 4-7.
                const u32 first = 2 * inTile;
                const u32 second = first + 1;
                alpha = sGradIntensity[first < 16 ? first % 4 : 4 + (first - 16) % 4];
                intensity = sGradIntensity[second < 16 ? second % 4 : 4 + (second - 16) % 4];
            } else {
                const u32 written = tile * 16 + inTile;
                alpha = sGradAlpha[written / ::cGradTexWidth];
                intensity = sGradIntensity[written % ::cGradTexWidth];
            }
            u8* pTexel = pTexels + 2 * (y * ::cGradTexWidth + x);
            pTexel[0] = intensity;
            pTexel[1] = alpha;
        }
    }

    RenderTexture::linear(mImagePtr, NEBULA_TEXTURE_LINEAR_LA8, ::cGradTexWidth, ::cGradTexHeight, 1);
}

void NormalMapBase::swapColorGB(GameTexture* pTexture) {
    const NebulaTexture* pRecord = nebula_texture_record(pTexture->image());
    if (pRecord == nullptr || pRecord->layout != NEBULA_TEXTURE_RGBA8) {
        return;
    }
    // The file's own texels stay as they are.
    u8* pCopy = new u8[pRecord->size];
    memcpy(pCopy, pRecord, pRecord->size);
    u8* pTexel = pCopy + NEBULA_TEXTURE_HEADER;
    for (u32 level = 0; level < pRecord->levels; level++) {
        const u32 levelWidth = pRecord->width >> level != 0 ? pRecord->width >> level : 1;
        const u32 levelHeight = pRecord->height >> level != 0 ? pRecord->height >> level : 1;
        for (u32 at = 0; at < levelWidth * levelHeight; at++, pTexel += 4) {
            const u32 color = pTexel[0] << 24 | pTexel[1] << 16 | pTexel[2] << 8 | pTexel[3];
                Vec3f normal;
                normal.set(static_cast< s8 >((color & 0xFF) - 128) / cNum_swapColorGB_f1, static_cast< s8 >(((color >> 8) & 0xFF) - 128) / cNum_swapColorGB_f2,
                           static_cast< s8 >(((color >> 16) & 0xFF) - 128) / cNum_swapColorGB_f3);
                u8 r = (s32)(cNum_swapColorGB_f4 + cNum_swapColorGB_f5 * normal.x);
                u8 g = (s32)(cNum_swapColorGB_f6 + cNum_swapColorGB_f7 * normal.y);
                u8 b = (s32)(cNum_swapColorGB_f8 + cNum_swapColorGB_f9 * normal.z);
            pTexel[0] = 0;
            pTexel[1] = g;
            pTexel[2] = b;
            pTexel[3] = r;
        }
    }

    pTexture->setImage(pCopy);
}

// One setting per line: the number leads the line and the name anywhere on it picks the setting.
void NormalMapBase::loadData(const char* pData, u32 size) {
    const char* const pEnd = pData + size;
    const char* pCursor = pData;
    MR::FormattedName lineText;
    while (pCursor < pEnd) {
        const char* pLineEnd = pCursor;
        while (pLineEnd < pEnd && *pLineEnd != '\n' && *pLineEnd != '\r') {
            pLineEnd++;
        }

        const u32 length = static_cast< u32 >(pLineEnd - pCursor);
        char* const line = lineText.room(length + 1);
        memcpy(line, pCursor, length);
        line[length] = '\0';

        pCursor = pLineEnd;
        while (pCursor < pEnd && (*pCursor == '\n' || *pCursor == '\r')) {
            pCursor++;
        }

        MR::scan8(line, "mLightingHighLevel", &mLightingHighLevel);
        MR::scan8(line, "mLightingLowLevel", &mLightingLowLevel);
        MR::scan8(line, "mLightAMode", &mLightAMode);
        MR::scan8(line, "mLightBMode", &mLightBMode);
        MR::scan16(line, "mUseModelLightReg", &mUseModelLightReg);
        MR::scan16(line, "mUseModelTevReg", &mUseModelStageColor);
        MR::scan16(line, "mHardLightColorMask", &mHardLightColorMask);
        MR::scan16(line, "mHardLightAlphaMask", &mHardLightAlphaMask);
        MR::scan8(line, "mBackLightMode", &mBackLightMode);
        MR::scan32(line, "mGradTexMode", &mGradTexMode);
        MR::scan8(line, "mAmbColor", &mAmbColor);
        MR::scan8(line, "mMatColor", &mMatColor);
        MR::scan8(line, "mAmbAlpha", &mAmbAlpha);
        MR::scan8(line, "mMatAlpha", &mMatAlpha);
        MR::scans16x4(line, "mTevColor0", reinterpret_cast< s16* >(&mStageColor0));
        MR::scans16x4(line, "mTevColor1", reinterpret_cast< s16* >(&mStageColor1));
    }
}
