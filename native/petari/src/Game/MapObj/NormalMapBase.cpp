#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include "Game/MapObj/NormalMapBase.hpp"
#include "Game/System/GameThreads.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/Animation/BtkPlayer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NativeShader.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DShapeDraw.hpp>
#include <JSystem/J3DGraphBase/J3DTransform.hpp>
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <cmath>
#include <cstring>
#include <nebula_format.h>

s16 sgrad_tex_thresh;

static s16 width = 0x100;
static s16 height = 0x100;

namespace {
    // The object's native material (runtime/src/render/materials/normal_map.hlsl): drawSetting and setTevForObject
    // set its program, each normal-mapped shape its textures, colours and indirect matrix (drawMaterialSetting).
    NebulaNativeMaterial sNormalMap;
}  // namespace

NormalMapBase::NormalMapBase(const char* pName) : LiveActor(pName) {
    mBtkPlayer = nullptr;
    _150 = 0;
    for (u32 i = 0; i < ARRAY_SIZE(_184); i++) {
        NebulaMtxIdentity(_184[i]);
    }

    _13A = 0;
    mGradTexMode = 0;
    _E8 = 0;
    _EC = 0;
    mBackLightMode = 0;
    _4C5 = 0;
}

void NormalMapBase::setup(const char* pName) {
    _4C5 = 1;
    _4C4 = 1;
    initModelManagerWithAnm(pName, nullptr, false);
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_MapObj, MR::DrawBufferType_None, MR::DrawType_WarpPodPath);
    MR::initLightCtrlNoDrawMapObj(this);
    setupLighting();
    setupTexture();
    MR::invalidateClipping(this);
    mHeight = 128;
    mWidth = 128;
    _4B8 = new (0x20) JUTTexture(mHeight, mWidth, NEBULA_TEXTURE_LINEAR_LA8);
    appear();
}

void NormalMapBase::initNormalMap() {
    _4C5 = 1;
    _4C4 = 0;
    setupLighting();
    setupTexture();
}

void NormalMapBase::setupLighting() {
    mTevColor0.r = 0;
    mTevColor0.g = 0;
    mTevColor0.b = 0;
    mTevColor0.a = 0;
    mTevColor1.r = 0;
    mTevColor1.g = 0;
    mTevColor1.b = 0;
    mTevColor1.a = 0;
    _138 = 0;
    mLightAMode = 0;
    mLightBMode = 1;
    mHardLightColorMask = 0;
    mHardLightAlphaMask = 4;
    mUseModelTevReg = 0;
    mUseModelLightReg = 0;
    mLightingHighLevel = 255;
    mLightingLowLevel = 0;
    mAmbColor = 64;
    mMatColor = 255;
    mAmbAlpha = 128;
    mMatAlpha = 255;
    char fileName[256];
    nebula_snprintf(fileName, sizeof(fileName), "%s.nrm.txt", MR::getModelResName(this));
    if (MR::getResourceHolder(this)->mFileInfoTable->isExistRes(fileName)) {
        const char* pData = static_cast< const char* >(MR::getResourceHolder(this)->mFileInfoTable->getRes(fileName));
        loadData(pData, MR::getResourceHolder(this)->mFileInfoTable->findFileInfo(fileName)->_4);
    }
}

void NormalMapBase::setupTexture() {
    _90 = nullptr;
    mNormalTex = nullptr;
    J3DTexture* pTexture = MR::getJ3DModelData(this)->getTexture();
    for (u16 i = 0; i < pTexture->getNum(); i++) {
        const ResTIMG* pImage = MR::getResTIMG(this, i);
        const char* pName = MR::getTextureName(this, i);
        if (strstr(pName, "Nswap_")) {
            char fileName[64];
            nebula_snprintf(fileName, sizeof(fileName), "%s.bti", pName + 6);
            pImage = MR::getTexFromArc(fileName, this);
            mNormalTex = new JUTTexture(pImage);
            mTextures[i] = mNormalTex;
            mTextures[i]->mMinType = NEBULA_FILTER_LINEAR;
            mTextures[i]->mMagType = NEBULA_FILTER_LINEAR;
            mTextures[i]->init();
        } else if (strstr(pName, "ormal") && !mNormalTex) {
            mNormalTex = new JUTTexture(pImage);
            mTextures[i] = mNormalTex;
        } else {
            _90 = new JUTTexture(pImage);
            mTextures[i] = _90;
        }

        if (strstr(pName, "Zormal")) {
            swapColorGB(mTextures[i]);
        }
    }

    mImagePtr = new (MR::getSceneHeap(), 0x20) u16[width * height];
    createGradTexture();
}

void NormalMapBase::control() {
}

void NormalMapBase::calcAnim() {
    LiveActor::calcAnim();
    updateBtkMtx();
    (MR::MutexHolder< 0 >::sMutex).lock();
    MR::getJ3DModel(this)->viewCalc();
    (MR::MutexHolder< 0 >::sMutex).unlock();
}

void NormalMapBase::updateBtkMtx() {
    if (mBtkPlayer && _150) {
        mBtkPlayer->reflectFrame();
        mBtkPlayer->update();
        J3DAnmTextureSRTKey* pAnm = static_cast< J3DAnmTextureSRTKey* >(mBtkPlayer->mAnmRes);
        if (pAnm) {
            for (u32 i = 0; i < mBtkMtxCount; i++) {
                J3DTextureSRTInfo transform;
                pAnm->getTransform(mBtkMtxIndices[i], &transform);
                J3DGetTextureMtxMayaOld(transform, _184[i]);
            }
        }
    }
}

void NormalMapBase::loadTexMtxBtk(J3DMaterial* pMaterial) const {
    if (mBtkPlayer != nullptr) {
        if (_150) {
            J3DNativeShape::setTexture(0, _184[pMaterial->mIndex], 2);
        }
    }
}

void NormalMapBase::draw() const {
    if (!MR::isDead(this)) {
        MR::loadActorLight(this);
        MR::loadViewMtx();
        MR::loadProjectionMtx();
        const TPos3f& rCameraViewMtx = MR::getCameraViewMtx();
        TPos3f lightMtx;
        Mtx mtx;
        NebulaMtxIdentity(mtx);
        MR::makeMtxTRS(mtx, this);
        MR::multMtx(mtx, mtx, rCameraViewMtx);
        NebulaMtxCopy((const MtxPtr)mtx, (MtxPtr)_F4);

        MR::makeMtxTR(lightMtx, this);
        MR::normalize(&lightMtx);
        drawSetting(lightMtx);
        shapeAnalyzeDraw(MR::getJ3DModel(this));
        standardDraw(MR::getJ3DModel(this));
    }
}

void NormalMapBase::updateLightMtx(MtxPtr pMtx) const {
    TPos3f inverse;
    f32 indMtx[2][3];
    TVec3f lightA;
    TVec3f lightB;
    TVec3f localA;
    TVec3f localB;
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
        lightB = mPosition - MR::getCamZdir() * 100000.0f;
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
        TVec3f light(localA * 0.5f);
        light *= 1.0077f;
        indMtx[0][0] = light.x * pMtx[0][0] + light.y * pMtx[1][0] + light.z * pMtx[2][0];
        indMtx[0][1] = light.x * pMtx[0][1] + light.y * pMtx[1][1] + light.z * pMtx[2][1];
        indMtx[0][2] = light.x * pMtx[0][2] + light.y * pMtx[1][2] + light.z * pMtx[2][2];
        light = localB * 0.5f;
        light *= 1.0077f;
        indMtx[1][0] = light.x * pMtx[0][0] + light.y * pMtx[1][0] + light.z * pMtx[2][0];
        indMtx[1][1] = light.x * pMtx[0][1] + light.y * pMtx[1][1] + light.z * pMtx[2][1];
        indMtx[1][2] = light.x * pMtx[0][2] + light.y * pMtx[1][2] + light.z * pMtx[2][2];
    } else {
        indMtx[0][0] = 0.5f * -localA.x;
        indMtx[0][1] = 0.5f * -localA.y;
        indMtx[0][2] = 0.5f * -localA.z;
        indMtx[1][0] = 0.5f * -localB.x;
        indMtx[1][1] = 0.5f * -localB.y;
        indMtx[1][2] = 0.5f * -localB.z;
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

void NormalMapBase::standardDraw(J3DModel* pModel) const {
    if ((_138 & 0x10) == 0) {
        for (u16 i = 0; i < pModel->mModelData->mMaterialTable.getMaterialNum(); i++) {
            J3DMaterial* pMaterial = pModel->mModelData->getMaterialNodePointer(i);
            const char* pMaterialName = MR::getMaterialName(pModel->mModelData, pMaterial->mIndex);
            if (!isNormalMapMaterial(pMaterialName)) {
                MR::simpleDraw(pModel, pMaterial);
            }
        }
    }
}

void NormalMapBase::shapeAnalyzeDraw(J3DModel* pModel) const {
    for (u16 i = 0; i < pModel->mModelData->getShapeNum(); i++) {
        J3DShape* pShape = pModel->mModelData->getShapeNodePointer(i);
        bool found = false;
        for (u16 j = 0; j < pModel->mModelData->getJointNum(); j++) {
            J3DJoint* pJoint = pModel->mModelData->getJointNodePointer(j);
            J3DMaterial* pMaterial = pJoint->getMesh();
            while (pMaterial) {
                const char* pName = MR::getMaterialName(pModel->mModelData, pMaterial->mIndex);
                if (!isNormalMapMaterial(pName)) {
                    pMaterial = pMaterial->mNext;
                    continue;
                }

                if (pMaterial->mShape == pShape) {
                    setTevForObject_Material();
                    TPos3f localMtx;
                    TPos3f viewMtx;
                    const J3DTransformInfo& rTransform = pJoint->getTransformInfo();
                    f32 rotationZ = rTransform.mRotation.z * (180.0f / 32768.0f);
                    f32 rotationY = rTransform.mRotation.y * (180.0f / 32768.0f);
                    f32 rotationX = rTransform.mRotation.x * (180.0f / 32768.0f);
                    MR::makeMtxTRS(localMtx, rTransform.mTranslate.x, rTransform.mTranslate.y, rTransform.mTranslate.z, rotationX, rotationY,
                                   rotationZ, rTransform.mScale.x, rTransform.mScale.y, rTransform.mScale.z);
                    NebulaMtxConcat(_F4, localMtx, viewMtx);
                    if (pJoint->getMtxType() == 1) {
                        J3DCalcBBoardMtx(viewMtx);
                        TPos3f inverse;
                        NebulaMtxInverse(_F4, inverse);
                        NebulaMtxConcat(inverse, viewMtx, localMtx);
                    } else if (pJoint->getMtxType() == 2) {
                        J3DCalcYBBoardMtx(viewMtx);
                        TPos3f inverse;
                        NebulaMtxInverse(_F4, inverse);
                        NebulaMtxConcat(inverse, viewMtx, localMtx);
                    }

                    J3DNativeShape::setPosition(0, viewMtx);
                    J3DNativeShape::setNormal(0, viewMtx);
                    MR::normalize(&localMtx);
                    updateLightMtx(localMtx);
                    drawMaterialSetting(pMaterial);
                    pMaterial->mCurrentMtx.load();
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

void NormalMapBase::drawShape(J3DModel* pModel, J3DShape* pShape) const {
    pShape->prepareGeometry();
    u16 groupCount = pShape->getMtxGroupNum();
    for (u16 i = 0; i < groupCount; i++) {
        if (pShape->getShapeDraw(i)) {
            J3DShapeMtx* pShapeMtx = pShape->getShapeMtx(i);
            u16 count = pShapeMtx->getUseMtxNum();
            for (u16 j = 0; j < count; j++) {
                u16 index = pShapeMtx->getUseMtxIndex(j);
                if (index != 0xFFFF) {
                    MtxPtr pMtx = pModel->getDrawMtx(index);
                    J3DNativeShape::setPosition(j, pMtx);
                    J3DNativeShape::setNormal(j, pMtx);
                }
            }

            pShape->getShapeDraw(i)->draw();
        }
    }
}

void NormalMapBase::loadDiffuseGradTex(u32 slot) const {
    J3DNativeTexObj obj;
    J3DNativeTexture::init(&obj, mImagePtr, width, height, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);

    if (_13A & 1) {
        J3DNativeTexture::setFilter(&obj, NEBULA_FILTER_NEAREST, NEBULA_FILTER_NEAREST, 0.0f, 0.0f, 0.0f, false, 0);
    } else {
        J3DNativeTexture::setFilter(&obj, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR, 0.0f, 0.0f, 0.0f, false, 0);
    }

    NativeShader::setTexObj(sNormalMap, slot, &obj);
}

void NormalMapBase::loadNormalTex(const JUTTexture* pTex, u32 slot) const {
    J3DNativeTexObj obj;
    J3DNativeTexture::init(&obj, pTex->mImage, pTex->mTIMG->mWidth, pTex->mTIMG->mHeight, NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
    if ((_13A & 2) != 0 && (_13A & 0x4) != 0) {
        J3DNativeTexture::setFilter(&obj, NEBULA_FILTER_NEAREST, NEBULA_FILTER_NEAREST, 0.0f, 0.0f, 0.0f, false, 0);
    } else if ((_13A & 0x2) != 0) {
        J3DNativeTexture::setFilter(&obj, NEBULA_FILTER_NEAREST, NEBULA_FILTER_LINEAR, 0.0, 0.0, 0.0, 0, 0);
    } else if ((_13A & 0x4) != 0) {
        J3DNativeTexture::setFilter(&obj, NEBULA_FILTER_LINEAR, NEBULA_FILTER_NEAREST, 0.0, 0.0, 0.0, 0, 0);
    } else {
        J3DNativeTexture::setFilter(&obj, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR, 0.0, 0.0, 0.0, 0, 0);
    }

    NativeShader::setTexObj(sNormalMap, slot, &obj);
}

void NormalMapBase::loadTexture1(J3DMaterial* pMaterial) const {
    J3DRegisterBlock* pBlock = pMaterial->getRegisterBlock();
    const u32 count = pBlock->getTexCapacity();

    u16 textures[8];
    for (u32 i = 0; i < count; i++) {
        textures[i] = pBlock->getTexNo(i);
    }

    NativeShader::setTexture(sNormalMap, 2, mTextures[textures[0]]);
    loadNormalTex(mTextures[textures[1]], 0);
}

void NormalMapBase::drawMaterialSetting(J3DMaterial* pMaterial) const {
    loadTexture1(pMaterial);
    loadTexMtxBtk(pMaterial);
    J3DNativeShape::setTexture(2, pMaterial->getTexMtx(0)->getMtx(), 2);
    Color8 ambient;
    Color8 material;
    ambient.set(mAmbColor, mAmbColor, mAmbColor, mAmbAlpha);
    material.set(mMatColor, mMatColor, mMatColor, mMatAlpha);
    if (mUseModelLightReg & 1) {
        NebulaColor8* pColor = pMaterial->getColorBlock()->getAmbColor(0);
        ambient.r = pColor->r;
        ambient.g = pColor->g;
        ambient.b = pColor->b;
    }

    if (mUseModelLightReg & 2) {
        ambient.a = pMaterial->getColorBlock()->getAmbColor(0)->a;
    }

    if (mUseModelLightReg & 4) {
        NebulaColor8* pColor = pMaterial->getColorBlock()->getMatColor(0);
        material.r = pColor->r;
        material.g = pColor->g;
        material.b = pColor->b;
    }

    if (mUseModelLightReg & 8) {
        material.a = pMaterial->getColorBlock()->getMatColor(0)->a;
    }

    NativeShader::setColor(sNormalMap, 0, material.r, material.g, material.b, material.a);
    NativeShader::setColor(sNormalMap, 4, ambient.r, ambient.g, ambient.b, ambient.a);
    const NebulaColor16& rColor0 = mUseModelTevReg ? static_cast< const NebulaColor16& >(*pMaterial->getRegisterBlock()->getRegisterColor(0))
                                                : static_cast< const NebulaColor16& >(mTevColor0);
    const NebulaColor16& rColor1 = mUseModelTevReg ? static_cast< const NebulaColor16& >(*pMaterial->getRegisterBlock()->getRegisterColor(1))
                                                : static_cast< const NebulaColor16& >(mTevColor1);
    // C0 and C1 as the combiner takes a register (its low 8 bits).
    sNormalMap.constants[8] = static_cast< u8 >(rColor0.r) / 255.0f;
    sNormalMap.constants[9] = static_cast< u8 >(rColor0.g) / 255.0f;
    sNormalMap.constants[10] = static_cast< u8 >(rColor0.b) / 255.0f;
    sNormalMap.constants[11] = static_cast< u8 >(rColor0.a) / 255.0f;
    sNormalMap.constants[12] = static_cast< u8 >(rColor1.r) / 255.0f;
    sNormalMap.constants[13] = static_cast< u8 >(rColor1.g) / 255.0f;
    sNormalMap.constants[14] = static_cast< u8 >(rColor1.b) / 255.0f;
    sNormalMap.constants[15] = static_cast< u8 >(rColor1.a) / 255.0f;
    NativeShader::send(sNormalMap);
}

void NormalMapBase::drawSetting(MtxPtr pMtx) const {
    NativeShader::init(sNormalMap, NEBULA_SHADER_NORMAL_MAP, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_BACK);
    sNormalMap.color_write = 1;
    sNormalMap.alpha_write = 0;
    updateLightMtx(pMtx);
    Mtx scale_mtx;
    NebulaMtxScale(scale_mtx, 0.0f, 0.0f, 0.0f);
    MR::setMtxTrans(scale_mtx, 0.5f, 0.5f, 0.0f);
    J3DNativeShape::setTexture(1, scale_mtx, 2);

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
    setTevForObject();
    loadDiffuseGradTex(1);
}

// Coordinate 0 through the BTK's matrix (texture matrix 0) when it plays, else the vertex's.
void NormalMapBase::setTevForObject_Material() const {
    sNormalMap.constants[32] = mBtkPlayer && _150 ? 1.0f : 0.0f;
}

// The stages' selections (normal_map.hlsl): light A and B's colours, the raster, light B's alpha, the back-light mode.
void NormalMapBase::setTevForObject() const {
    setTevForObject_Material();

    // Light A: one, none where the colour mask leaves light 0 out, the gradient for modes 0 and 1. Light B: one, none
    // where the mask leaves light 1 out, stage 0's alpha for mode 1.
    f32 lightA = (mHardLightColorMask & 0x1) == 0 ? 0.0f : 1.0f;
    f32 lightB = (mHardLightColorMask & 0x2) == 0 ? 0.0f : 1.0f;
    if (mLightAMode <= 1) {
        lightA = 2.0f;
    }
    if (mLightBMode == 1) {
        lightB = 2.0f;
    }
    sNormalMap.constants[27] = mBackLightMode;
    sNormalMap.constants[28] = lightA;
    sNormalMap.constants[29] = lightB;
    sNormalMap.constants[30] = mBackLightMode == 4 ? 1.0f : 0.0f;
    sNormalMap.constants[31] = mLightBMode == 2 ? 1.0f : 0.0f;
    sNormalMap.constants[33] = 1.0f;
    sNormalMap.constants[34] = width;
    sNormalMap.constants[35] = height;
}

namespace {
    // The gradient's level at `index` of `count` steps (the light's ramp between the low and high levels).
    s32 gradLevel(u32 index, u32 count, s32 low, s32 high, s32 mode) {
        s32 value = index * (256.0f / count);
        if (value < low) {
            return 0;
        }
        if (value >= high) {
            return 255;
        }
        f32 rate = 1.0f - static_cast< f32 >(high - value) / (high - low);
        switch (mode) {
        case 0:
            value = 255.0f * rate;
            break;
        case 1:
            value = 255.0f * (rate * rate);
            break;
        case 2:
            value = 255.0f * MR::sqrt(rate);
            break;
        }
        return value;
    }

    u8 sGradIntensity[0x100];
    u8 sGradAlpha[0x100];
}  // namespace

// The light gradient as LA8 rows (NEBULA_TEXTURE_LINEAR_LA8): intensity by column (the ambient-to-material ramp),
// alpha by row (the same ramp down the rows, none when _138 & 1), laid out as the picture the game has always shown.
// The game wrote both ramps as two-channel texels in rows and ran its tile conversion on them, which leaves the
// first 2 KB as each 4-row band's first eight intensities (two to a texel: alpha, then intensity) and the rest as the
// rows' bytes read as 4x4 tiles: texel (x, y) here is what those tiles showed there.
void NormalMapBase::createGradTexture() {
    for (u32 x = 0; x < width; x++) {
        const f32 ratio = gradLevel(x, width, mLightingLowLevel, mLightingHighLevel, mGradTexMode) / 255.0f;
        sGradIntensity[x] = static_cast< u8 >(static_cast< s32 >(mAmbColor + ratio * (mMatColor - mAmbColor)));
    }
    for (u32 y = 0; y < height; y++) {
        if (_138 & 1) {
            sGradAlpha[y] = 0;
            continue;
        }
        const f32 ratio = gradLevel(y, height, mLightingLowLevel, mLightingHighLevel, mGradTexMode) / 255.0f;
        sGradAlpha[y] = static_cast< u8 >(static_cast< s32 >(mAmbColor + ratio * (mMatColor - mAmbColor)));
    }

    u8* pTexels = static_cast< u8* >(mImagePtr);
    const u32 tilesAcross = width / 4;
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            const u32 tile = (y / 4) * tilesAcross + x / 4;
            const u32 inTile = (y % 4) * 4 + x % 4;
            u8 alpha;
            u8 intensity;
            if (tile < static_cast< u32 >(height / 4)) {
                // Band `tile`'s entries 2 * inTile and 2 * inTile + 1: columns 0-3 of its rows, then columns 4-7.
                const u32 first = 2 * inTile;
                const u32 second = first + 1;
                alpha = sGradIntensity[first < 16 ? first % 4 : 4 + (first - 16) % 4];
                intensity = sGradIntensity[second < 16 ? second % 4 : 4 + (second - 16) % 4];
            } else {
                const u32 written = tile * 16 + inTile;
                alpha = sGradAlpha[written / width];
                intensity = sGradIntensity[written % width];
            }
            u8* pTexel = pTexels + 2 * (y * width + x);
            pTexel[0] = intensity;
            pTexel[1] = alpha;
        }
    }

    J3DNativeTexture::registerLinear(mImagePtr, NEBULA_TEXTURE_LINEAR_LA8, width, height, 1);
}

void NormalMapBase::swapColorGB(JUTTexture* pTexture) {
    const NebulaTexture* pRecord = nebula_texture_record(pTexture->mImage);
    if (pRecord == nullptr || pRecord->layout != NEBULA_TEXTURE_RGBA8) {
        return;
    }
    // The file's own texels stay as they are.
    u8* pCopy = new (32) u8[pRecord->size];
    memcpy(pCopy, pRecord, pRecord->size);
    u8* pTexel = pCopy + NEBULA_TEXTURE_HEADER;
    for (u32 level = 0; level < pRecord->levels; level++) {
        const u32 levelWidth = pRecord->width >> level != 0 ? pRecord->width >> level : 1;
        const u32 levelHeight = pRecord->height >> level != 0 ? pRecord->height >> level : 1;
        for (u32 at = 0; at < levelWidth * levelHeight; at++, pTexel += 4) {
            const u32 color = pTexel[0] << 24 | pTexel[1] << 16 | pTexel[2] << 8 | pTexel[3];
                TVec3f normal;
                normal.set(static_cast< s8 >((color & 0xFF) - 128) / 128.0f, static_cast< s8 >(((color >> 8) & 0xFF) - 128) / 128.0f,
                           static_cast< s8 >(((color >> 16) & 0xFF) - 128) / 128.0f);
                u8 r = (s32)(128.0f + 127.0f * normal.x);
                u8 g = (s32)(128.0f + 127.0f * normal.y);
                u8 b = (s32)(128.0f + 127.0f * normal.z);
            pTexel[0] = 0;
            pTexel[1] = g;
            pTexel[2] = b;
            pTexel[3] = r;
        }
    }

    pTexture->mImage = pCopy;
    pTexture->mObj.mImage = pCopy;
    J3DNativeTexture::registerRecord(pCopy);
}

void NormalMapBase::loadData(const char* pData, u32 size) {
    char line[256];
    const char* pCursor = pData;
    u32 offset = 0;
    while (true) {
        if (offset >= size) {
            break;
        }

        u32 length = 0;
        do {
            if (*pCursor == '\n' || *pCursor == '\r') {
                break;
            }

            offset++;
            line[length] = *pCursor;
            length++;
            pCursor++;
        } while (offset < size);
        line[length] = '\0';
        while (true) {
            if (offset >= size) {
                break;
            }

            if (*pCursor != '\n' && *pCursor != '\r') {
                break;
            }

            pCursor++;
            offset++;
        }

        MR::scan8(line, "mLightingHighLevel", &mLightingHighLevel);
        MR::scan8(line, "mLightingLowLevel", &mLightingLowLevel);
        MR::scan8(line, "mLightAMode", &mLightAMode);
        MR::scan8(line, "mLightBMode", &mLightBMode);
        MR::scan16(line, "mUseModelLightReg", &mUseModelLightReg);
        MR::scan16(line, "mUseModelTevReg", &mUseModelTevReg);
        MR::scan16(line, "mHardLightColorMask", &mHardLightColorMask);
        MR::scan16(line, "mHardLightAlphaMask", &mHardLightAlphaMask);
        MR::scan8(line, "mBackLightMode", &mBackLightMode);
        MR::scan32(line, "mGradTexMode", &mGradTexMode);
        MR::scan8(line, "mAmbColor", &mAmbColor);
        MR::scan8(line, "mMatColor", &mMatColor);
        MR::scan8(line, "mAmbAlpha", &mAmbAlpha);
        MR::scan8(line, "mMatAlpha", &mMatAlpha);
        MR::scans16x4(line, "mTevColor0", reinterpret_cast< s16* >(&mTevColor0));
        MR::scans16x4(line, "mTevColor1", reinterpret_cast< s16* >(&mTevColor1));
    }
}

NormalMapBase::~NormalMapBase() {
}
