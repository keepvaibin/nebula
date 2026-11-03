#include <string.h>
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTransform.hpp"
#include <cstdio>

// The colour word, r in the top byte.





J3DRegisterBlock::J3DRegisterBlock(u8 texCapacity, bool hasTextures, bool hasColors)
    : mTexCapacity(texCapacity), mHasTextures(hasTextures), mHasColors(hasColors) {
    for (u32 i = 0; i < ARRAY_SIZE(mTexNo); i++) {
        mTexNo[i] = 0xFFFF;
    }
    for (u32 i = 0; i < ARRAY_SIZE(mRegisterColor); i++) {
        mRegisterColor[i] = j3dDefaultRegisterColor;
    }
    for (u32 i = 0; i < ARRAY_SIZE(mConstantColor); i++) {
        mConstantColor[i] = j3dDefaultConstantColor;
    }
}

void J3DRegisterBlock::reset(J3DRegisterBlock* pBlock) {
    for (u32 i = 0; i < mTexCapacity; i++) {
        mTexNo[i] = pBlock->getTexNo(i);
    }
    if (mHasColors && pBlock->mHasColors) {
        memcpy(mRegisterColor, pBlock->mRegisterColor, sizeof(mRegisterColor));
        memcpy(mConstantColor, pBlock->mConstantColor, sizeof(mConstantColor));
    }
}

void J3DIndBlock::reset(J3DIndBlock* pBlock) {
    if (mFull && pBlock->mFull) {
        mIndTexStageNum = pBlock->mIndTexStageNum;
        memcpy(mIndTexMtx, pBlock->mIndTexMtx, sizeof(mIndTexMtx));
    }
}

J3DColorBlock::J3DColorBlock(Kind kind) : mKind(kind), mColorChanNum(0), mCullMode(0) {
    for (s32 i = 0; i < ARRAY_SIZE(mMatColor); i++) {
        mMatColor[i] = j3dDefaultColInfo;
    }
    for (s32 i = 0; i < ARRAY_SIZE(mAmbColor); i++) {
        mAmbColor[i] = j3dDefaultAmbInfo;
    }
    for (s32 i = 0; i < ARRAY_SIZE(mLight); i++) {
        mLight[i] = NULL;
    }
}

void J3DColorBlock::reset(J3DColorBlock* pBlock) {
    mColorChanNum = pBlock->getColorChanNum();
    for (u32 i = 0; i < ARRAY_SIZE(mMatColor); i++) {
        mMatColor[i] = *pBlock->getMatColor(i);
    }
    for (u32 i = 0; i < ARRAY_SIZE(mColorChan); i++) {
        mColorChan[i] = *pBlock->getColorChan(i);
    }
    if (mKind != Kind_LightOff && pBlock->mKind != Kind_LightOff) {
        for (u32 i = 0; i < ARRAY_SIZE(mAmbColor); i++) {
            mAmbColor[i] = pBlock->mAmbColor[i];
        }
    }
}

J3DTexGenBlock::J3DTexGenBlock(u32 capacity, bool hasScale)
    : mCapacity(capacity), mHasScale(hasScale), mTexGenNum(0), mNBTScale() {
    for (s32 i = 0; i < ARRAY_SIZE(mTexMtx); i++) {
        mTexMtx[i] = NULL;
    }
}

void J3DTexGenBlock::reset(J3DTexGenBlock* pBlock) {
    mTexGenNum = pBlock->getTexGenNum();
    for (u32 i = 0; i < mCapacity; i++) {
        mTexCoord[i] = *pBlock->getTexCoord(i);
        if (pBlock->getTexMtx(i) != NULL && mTexMtx[i] != NULL) {
            memcpy((void*)mTexMtx[i], pBlock->getTexMtx(i), sizeof(*mTexMtx[i]));
        }
    }
    if (mHasScale && pBlock->mHasScale) {
        mNBTScale = pBlock->mNBTScale;
    }
}

J3DOutputBlock::J3DOutputBlock(Kind kind) : mKind(kind) {
    mZCompLoc = 0xFF;
}

void J3DOutputBlock::reset(J3DOutputBlock* pBlock) {
    if (mKind == Kind_Full && pBlock->getFog() != NULL) {
        mFog.setFogInfo(pBlock->getFog()->getFogInfo());
    }
    if (!isPreset() && !pBlock->isPreset()) {
        mAlphaComp = pBlock->mAlphaComp;
        mBlend = pBlock->mBlend;
        mZMode = pBlock->mZMode;
        mZCompLoc = pBlock->mZCompLoc;
    }
}

void J3DTexGenBlock::calc(const Mtx modelMtx) {
    Mtx viewMtx;

    for (s32 i = 0; i < ARRAY_SIZE(mTexMtx); i++) {
        if (mTexMtx[i] != NULL) {
            u32 texMtxMode = mTexMtx[i]->getTexMtxInfo().mInfo & 0x3f;
            mTexCoord[i].resetTexMtxReg();

            switch (texMtxMode) {
            case J3DTexMtxMode_EnvmapBasic:
            case J3DTexMtxMode_EnvmapOld:
            case J3DTexMtxMode_Envmap:

                NebulaMtxConcat(j3dSys.getViewMtx(), modelMtx, viewMtx);
                viewMtx[0][3] = 0.0f;
                viewMtx[1][3] = 0.0f;
                viewMtx[2][3] = 0.0f;
                mTexMtx[i]->calc(viewMtx);
                break;
            case J3DTexMtxMode_ProjmapBasic:
            case J3DTexMtxMode_Projmap:
                mTexMtx[i]->calc(modelMtx);
                break;
            case J3DTexMtxMode_ViewProjmapBasic:
            case J3DTexMtxMode_ViewProjmap:
                NebulaMtxConcat(j3dSys.getViewMtx(), modelMtx, viewMtx);
                mTexMtx[i]->calc(viewMtx);
                break;
            case J3DTexMtxMode_Unknown5:
            case J3DTexMtxMode_EnvmapOldEffectMtx:
            case J3DTexMtxMode_EnvmapEffectMtx:
                NebulaMtxCopy(modelMtx, viewMtx);
                viewMtx[0][3] = 0.0f;
                viewMtx[1][3] = 0.0f;
                viewMtx[2][3] = 0.0f;
                mTexMtx[i]->calc(viewMtx);
                break;
            default:
                mTexMtx[i]->calc(j3dDefaultMtx);
                break;
            }
        }
    }
}

void J3DTexGenBlock::calcWithoutViewMtx(const Mtx modelMtx) {
    Mtx mtx;

    for (s32 i = 0; i < ARRAY_SIZE(mTexMtx); i++) {
        if (mTexMtx[i] != NULL) {
            u32 texMtxMode = mTexMtx[i]->getTexMtxInfo().mInfo & 0x3f;
            mTexCoord[i].resetTexMtxReg();
            switch (texMtxMode) {
            case J3DTexMtxMode_EnvmapBasic:
            case J3DTexMtxMode_EnvmapOld:
            case J3DTexMtxMode_Envmap:
                mTexMtx[i]->calc(j3dDefaultMtx);
                break;
            case J3DTexMtxMode_ProjmapBasic:
            case J3DTexMtxMode_Projmap:
                mTexMtx[i]->calc(modelMtx);
                break;
            case J3DTexMtxMode_ViewProjmapBasic:
            case J3DTexMtxMode_ViewProjmap:
                mTexMtx[i]->calc(j3dDefaultMtx);
                break;
            case J3DTexMtxMode_Unknown5:
            case J3DTexMtxMode_EnvmapOldEffectMtx:
            case J3DTexMtxMode_EnvmapEffectMtx:
                NebulaMtxCopy(modelMtx, mtx);
                mtx[0][3] = 0.0f;
                mtx[1][3] = 0.0f;
                mtx[2][3] = 0.0f;
                mTexMtx[i]->calc(mtx);
                break;
            default:
                mTexMtx[i]->calc(j3dDefaultMtx);
                break;
            }
        }
    }
}

void J3DTexGenBlock::calcPostTexMtx(const Mtx modelMtx) {
    Mtx mtx1, mtx2;

    for (s32 i = 0; i < ARRAY_SIZE(mTexMtx); i++) {
        if (mTexMtx[i] != NULL) {
            u32 texMtxMode = mTexMtx[i]->getTexMtxInfo().mInfo & 0x3f;
            mTexCoord[i].resetTexMtxReg();
            switch (texMtxMode) {
            case J3DTexMtxMode_EnvmapBasic:
            case J3DTexMtxMode_EnvmapOld:
            case J3DTexMtxMode_Envmap:
                mTexCoord[i].setTexMtxReg(30);
                mTexMtx[i]->calcPostTexMtx(j3dDefaultMtx);
                break;
            case J3DTexMtxMode_ProjmapBasic:
            case J3DTexMtxMode_Projmap:
                NebulaMtxInverse(j3dSys.getViewMtx(), mtx1);
                mTexCoord[i].setTexMtxReg(0);
                mTexMtx[i]->calcPostTexMtx(mtx1);
                break;
            case J3DTexMtxMode_ViewProjmapBasic:
            case J3DTexMtxMode_ViewProjmap:
                mTexCoord[i].setTexMtxReg(0);
                mTexMtx[i]->calcPostTexMtx(j3dDefaultMtx);
                break;
            case J3DTexMtxMode_Unknown5:
            case J3DTexMtxMode_EnvmapOldEffectMtx:
            case J3DTexMtxMode_EnvmapEffectMtx:
                NebulaMtxInverse(j3dSys.getViewMtx(), mtx2);
                mtx2[0][3] = 0.0f;
                mtx2[1][3] = 0.0f;
                mtx2[2][3] = 0.0f;
                mTexCoord[i].setTexMtxReg(30);
                mTexMtx[i]->calcPostTexMtx(mtx2);
                break;
            default:
                mTexCoord[i].setTexMtxReg(60);
                mTexMtx[i]->calcPostTexMtx(j3dDefaultMtx);
                continue;
            }
        }
    }
}

void J3DTexGenBlock::calcPostTexMtxWithoutViewMtx(f32 const (*param_0)[4]) {
    for (s32 i = 0; i < ARRAY_SIZE(mTexMtx); i++) {
        if (mTexMtx[i] != NULL) {
            int texMtxMode = mTexMtx[i]->getTexMtxInfo().mInfo & 0x3f;
            mTexCoord[i].resetTexMtxReg();
            switch (texMtxMode) {
            case J3DTexMtxMode_EnvmapBasic:
            case J3DTexMtxMode_EnvmapOld:
            case J3DTexMtxMode_Envmap:
                mTexCoord[i].setTexMtxReg(30);
                mTexMtx[i]->calcPostTexMtx(j3dDefaultMtx);
                break;
            case J3DTexMtxMode_ProjmapBasic:
            case J3DTexMtxMode_Projmap:
                mTexCoord[i].setTexMtxReg(0);
                mTexMtx[i]->calcPostTexMtx(j3dDefaultMtx);
                break;
            case J3DTexMtxMode_ViewProjmapBasic:
            case J3DTexMtxMode_ViewProjmap:
                mTexCoord[i].setTexMtxReg(0);
                mTexMtx[i]->calcPostTexMtx(j3dDefaultMtx);
                break;
            case J3DTexMtxMode_Unknown5:
            case J3DTexMtxMode_EnvmapOldEffectMtx:
            case J3DTexMtxMode_EnvmapEffectMtx:
                mTexCoord[i].setTexMtxReg(30);
                mTexMtx[i]->calcPostTexMtx(j3dDefaultMtx);
                break;
            default:
                mTexCoord[i].setTexMtxReg(60);
                mTexMtx[i]->calcPostTexMtx(j3dDefaultMtx);
                break;
            }
        }
    }
}
