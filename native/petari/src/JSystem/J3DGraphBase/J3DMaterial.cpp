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
    mPostTexMtx = false;
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

void J3DMaterial::calc(f32 const (*pModelMtx)[4]) {
    Mtx mtx;
    for (u32 i = 0; i < 8; i++) {
        J3DTexMtx* pTexMtx = getTexMtx(i);
        if (pTexMtx == NULL) {
            continue;
        }
        const u32 mode = pTexMtx->getTexMtxInfo().mInfo & 0x3f;
        mTexCoord[i].resetTexMtxReg();
        if (mPostTexMtx) {
            switch (mode) {
            case J3DTexMtxMode_EnvmapBasic:
            case J3DTexMtxMode_EnvmapOld:
            case J3DTexMtxMode_Envmap:
                mTexCoord[i].setTexMtxReg(30);
                pTexMtx->calcPostTexMtx(j3dDefaultMtx);
                break;
            case J3DTexMtxMode_ProjmapBasic:
            case J3DTexMtxMode_Projmap:
                NebulaMtxInverse(j3dSys.getViewMtx(), mtx);
                mTexCoord[i].setTexMtxReg(0);
                pTexMtx->calcPostTexMtx(mtx);
                break;
            case J3DTexMtxMode_ViewProjmapBasic:
            case J3DTexMtxMode_ViewProjmap:
                mTexCoord[i].setTexMtxReg(0);
                pTexMtx->calcPostTexMtx(j3dDefaultMtx);
                break;
            case J3DTexMtxMode_Unknown5:
            case J3DTexMtxMode_EnvmapOldEffectMtx:
            case J3DTexMtxMode_EnvmapEffectMtx:
                NebulaMtxInverse(j3dSys.getViewMtx(), mtx);
                mtx[0][3] = 0.0f;
                mtx[1][3] = 0.0f;
                mtx[2][3] = 0.0f;
                mTexCoord[i].setTexMtxReg(30);
                pTexMtx->calcPostTexMtx(mtx);
                break;
            default:
                mTexCoord[i].setTexMtxReg(60);
                pTexMtx->calcPostTexMtx(j3dDefaultMtx);
                break;
            }
        } else {
            switch (mode) {
            case J3DTexMtxMode_EnvmapBasic:
            case J3DTexMtxMode_EnvmapOld:
            case J3DTexMtxMode_Envmap:
                NebulaMtxConcat(j3dSys.getViewMtx(), pModelMtx, mtx);
                mtx[0][3] = 0.0f;
                mtx[1][3] = 0.0f;
                mtx[2][3] = 0.0f;
                pTexMtx->calc(mtx);
                break;
            case J3DTexMtxMode_ProjmapBasic:
            case J3DTexMtxMode_Projmap:
                pTexMtx->calc(pModelMtx);
                break;
            case J3DTexMtxMode_ViewProjmapBasic:
            case J3DTexMtxMode_ViewProjmap:
                NebulaMtxConcat(j3dSys.getViewMtx(), pModelMtx, mtx);
                pTexMtx->calc(mtx);
                break;
            case J3DTexMtxMode_Unknown5:
            case J3DTexMtxMode_EnvmapOldEffectMtx:
            case J3DTexMtxMode_EnvmapEffectMtx:
                NebulaMtxCopy(pModelMtx, mtx);
                mtx[0][3] = 0.0f;
                mtx[1][3] = 0.0f;
                mtx[2][3] = 0.0f;
                pTexMtx->calc(mtx);
                break;
            default:
                pTexMtx->calc(j3dDefaultMtx);
                break;
            }
        }
    }
    if (!mPostTexMtx) {
        mCurrentMtx.setCurrentTexMtx(mTexCoord[0].getTexGenMtx(), mTexCoord[1].getTexGenMtx(), mTexCoord[2].getTexGenMtx(),
                                     mTexCoord[3].getTexGenMtx(), mTexCoord[4].getTexGenMtx(), mTexCoord[5].getTexGenMtx(),
                                     mTexCoord[6].getTexGenMtx(), mTexCoord[7].getTexGenMtx());
    } else {
        mCurrentMtx.setCurrentTexMtx(mTexCoord[0].getTexMtxReg(), mTexCoord[1].getTexMtxReg(), mTexCoord[2].getTexMtxReg(),
                                     mTexCoord[3].getTexMtxReg(), mTexCoord[4].getTexMtxReg(), mTexCoord[5].getTexMtxReg(),
                                     mTexCoord[6].getTexMtxReg(), mTexCoord[7].getTexMtxReg());
    }
    setCurrentMtx();
}

void J3DMaterial::setCurrentMtx() {
    mShape->setCurrentMtx(mCurrentMtx);
}
