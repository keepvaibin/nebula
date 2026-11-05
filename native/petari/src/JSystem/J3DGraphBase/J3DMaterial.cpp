#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTransform.hpp"

void J3DMaterial::initialize() {
    mShape = NULL;
    mNext = NULL;
    mJoint = NULL;
    mMaterialMode = 1;
    mIndex = -1;
    mInvalid = 0;
    mID = 0;
    mNativeMaterial = NULL;
    mTexture = NULL;
    mHasAmbient = false;
    mHasStageColors = false;
    mHasIndirect = false;
    mHasFog = false;
    mColorChanNum = 0;
    mCull = 0;
    mTexGenNum = 0;
    mTexMtxUsed = 0;
    mIndTexStageNum = 0;
    mEarlyDepth = 0;
    for (u32 i = 0; i < 2; i++) {
        mMatColor[i] = j3dDefaultColInfo;
        mAmbColor[i] = j3dDefaultAmbInfo;
    }
    for (u32 i = 0; i < 8; i++) {
        mTexNo[i] = 0xFFFF;
    }
    for (u32 i = 0; i < 4; i++) {
        mRegisterColor[i] = j3dDefaultRegisterColor;
        mConstantColor[i] = j3dDefaultConstantColor;
    }
    memset(&mAlphaCompare, 0, sizeof(mAlphaCompare));
    memset(&mBlend, 0, sizeof(mBlend));
    memset(&mDepth, 0, sizeof(mDepth));
}

void J3DMaterial::load() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(2)) {
        loadNBTScale(mNBTScale);
    }
}

void J3DMaterial::useForDraw() {
    j3dSys.setMaterialMode(mMaterialMode);
    if (!j3dSys.checkFlag(2)) {
        J3DNativeMaterial::send(this);
        loadNBTScale(mNBTScale);
    }
}

void J3DMaterial::calc() {
    for (u32 i = 0; i < 8; i++) {
        J3DTexMtx* pTexMtx = getTexMtx(i);
        if (pTexMtx != NULL) {
            pTexMtx->calc(j3dDefaultMtx);
        }
    }
}
