#include <string.h>
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTransform.hpp"
#include <cstdio>

// The XF colour word, r in the top byte (the console read the four bytes as a u32).





J3DTevBlock::J3DTevBlock(u8 texCapacity, bool hasTextures, bool hasColors)
    : mTexCapacity(texCapacity), mHasTextures(hasTextures), mHasColors(hasColors) {
    for (u32 i = 0; i < ARRAY_SIZE(mTexNo); i++) {
        mTexNo[i] = 0xFFFF;
    }
    for (u32 i = 0; i < ARRAY_SIZE(mTevColor); i++) {
        mTevColor[i] = j3dDefaultTevColor;
    }
    for (u32 i = 0; i < ARRAY_SIZE(mTevKColor); i++) {
        mTevKColor[i] = j3dDefaultTevKColor;
    }
}

void J3DTevBlock::reset(J3DTevBlock* pBlock) {
    for (u32 i = 0; i < mTexCapacity; i++) {
        mTexNo[i] = pBlock->getTexNo(i);
    }
    if (mHasColors && pBlock->mHasColors) {
        memcpy(mTevColor, pBlock->mTevColor, sizeof(mTevColor));
        memcpy(mTevKColor, pBlock->mTevKColor, sizeof(mTevKColor));
    }
}

void J3DIndBlock::reset(J3DIndBlock* pBlock) {
    if (mFull && pBlock->mFull) {
        mIndTexStageNum = pBlock->mIndTexStageNum;
        memcpy(mIndTexMtx, pBlock->mIndTexMtx, sizeof(mIndTexMtx));
    }
}

void J3DColorBlockLightOff::initialize() {
    mColorChanNum = 0;

    for (s32 i = 0; i < ARRAY_SIZE(mMatColor); i++) {
        mMatColor[i] = j3dDefaultColInfo;
    }

    mMatColorOffset = 0;
    mColorChanOffset = 0;
}

void J3DColorBlockAmbientOn::initialize() {
    mColorChanNum = 0;

    for (s32 i = 0; i < ARRAY_SIZE(mMatColor); i++) {
        mMatColor[i] = j3dDefaultColInfo;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mAmbColor); i++) {
        mAmbColor[i] = j3dDefaultAmbInfo;
    }

    mMatColorOffset = 0;
    mColorChanOffset = 0;
}

void J3DColorBlockLightOn::initialize() {
    mColorChanNum = 0;

    for (s32 i = 0; i < ARRAY_SIZE(mMatColor); i++) {
        mMatColor[i] = j3dDefaultColInfo;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mAmbColor); i++) {
        mAmbColor[i] = j3dDefaultAmbInfo;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mLight); i++) {
        mLight[i] = NULL;
    }

    mMatColorOffset = 0;
    mColorChanOffset = 0;
}

void J3DTexGenBlockPatched::initialize() {
    mTexGenNum = 0;

    for (s32 i = 0; i < ARRAY_SIZE(mTexMtx); i++) {
        mTexMtx[i] = NULL;
    }

    mTexMtxOffset = 0;
}

void J3DTexGenBlock4::initialize() {
    mTexGenNum = 0;

    for (s32 i = 0; i < 4; i++) {
        mTexMtx[i] = NULL;
    }

    mTexMtxOffset = 0;
}

void J3DTexGenBlockBasic::initialize() {
    mTexGenNum = 0;

    for (s32 i = 0; i < ARRAY_SIZE(mTexMtx); i++) {
        mTexMtx[i] = NULL;
    }

    mTexMtxOffset = 0;
}

void J3DPEBlockFogOff::initialize() {
    mAlphaComp = 0xFFFF;
    mZMode = 0xFFFF;
    mZCompLoc = 0xFF;
    mDither = 1;
}

void J3DPEBlockFull::initialize() {
    mAlphaComp = 0xFFFF;
    mZMode = 0xFFFF;
    mZCompLoc = 0xFF;
    mDither = 1;
    mFogOffset = 0;
}


















static u32 SizeOfLoadMatColors = 13;

static u32 SizeOfLoadAmbColors = 13;

static u32 SizeOfLoadColorChans = 21;

static u32 SizeOfJ3DColorBlockLightOffLoad = SizeOfLoadMatColors + SizeOfLoadColorChans;


static u32 SizeOfJ3DColorBlockAmbientOnLoad = SizeOfLoadMatColors + SizeOfLoadAmbColors + SizeOfLoadColorChans;























































































void J3DColorBlockLightOff::reset(J3DColorBlock* pBlock) {
    mColorChanNum = pBlock->getColorChanNum();

    for (u32 i = 0; i < ARRAY_SIZE(mMatColor); i++) {
        mMatColor[i] = *pBlock->getMatColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mColorChan); i++) {
        mColorChan[i] = *pBlock->getColorChan(i);
    }
}

void J3DColorBlockAmbientOn::reset(J3DColorBlock* pBlock) {
    mColorChanNum = pBlock->getColorChanNum();

    for (u32 i = 0; i < ARRAY_SIZE(mMatColor); i++) {
        mMatColor[i] = *pBlock->getMatColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mColorChan); i++) {
        mColorChan[i] = *pBlock->getColorChan(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mAmbColor); i++) {
        if (pBlock->getAmbColor(i) != NULL) {
            mAmbColor[i] = *pBlock->getAmbColor(i);
        }
    }
}

void J3DColorBlockLightOn::reset(J3DColorBlock* pBlock) {
    mColorChanNum = pBlock->getColorChanNum();

    for (u32 i = 0; i < ARRAY_SIZE(mMatColor); i++) {
        mMatColor[i] = *pBlock->getMatColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mColorChan); i++) {
        mColorChan[i] = *pBlock->getColorChan(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mAmbColor); i++) {
        if (pBlock->getAmbColor(i) != NULL) {
            mAmbColor[i] = *pBlock->getAmbColor(i);
        }
    }
}

void J3DTexGenBlockPatched::reset(J3DTexGenBlock* pBlock) {
    mTexGenNum = pBlock->getTexGenNum();

    for (u32 i = 0; i < ARRAY_SIZE(mTexCoord); i++) {
        mTexCoord[i] = *pBlock->getTexCoord(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTexMtx); i++) {
        if (pBlock->getTexMtx(i) != NULL) {
            if (mTexMtx[i] != NULL) {
                memcpy(mTexMtx[i], pBlock->getTexMtx(i), sizeof(*mTexMtx[i]));
            }
        }
    }
}

void J3DTexGenBlock4::reset(J3DTexGenBlock* pBlock) {
    mTexGenNum = pBlock->getTexGenNum();

    for (u32 i = 0; i < 4; i++) {
        mTexCoord[i] = *pBlock->getTexCoord(i);
    }

    for (u32 i = 0; i < 4; i++) {
        if (pBlock->getTexMtx(i) != NULL) {
            if (mTexMtx[i] != NULL) {
                memcpy(mTexMtx[i], pBlock->getTexMtx(i), sizeof(*mTexMtx[i]));
            }
        }
    }

    mNBTScale = *pBlock->getNBTScale();
}

void J3DTexGenBlockBasic::reset(J3DTexGenBlock* pBlock) {
    mTexGenNum = pBlock->getTexGenNum();

    for (u32 i = 0; i < ARRAY_SIZE(mTexCoord); i++) {
        mTexCoord[i] = *pBlock->getTexCoord(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTexMtx); i++) {
        if (pBlock->getTexMtx(i) != NULL) {
            if (mTexMtx[i] != NULL) {
                memcpy(mTexMtx[i], pBlock->getTexMtx(i), sizeof(*mTexMtx[i]));
            }
        }
    }

    mNBTScale = *pBlock->getNBTScale();
}

void J3DPEBlockFogOff::reset(J3DPEBlock* pBlock) {
    switch (pBlock->getType()) {
    case 'PEFL':
    case 'PEFG':
        mAlphaComp = *pBlock->getAlphaComp();
        mBlend = *pBlock->getBlend();
        mZMode = *pBlock->getZMode();
        mZCompLoc = pBlock->getZCompLoc();
        break;
    }
}

void J3DPEBlockFull::reset(J3DPEBlock* pBlock) {
    if (pBlock->getFog() != NULL) {
        mFog.setFogInfo(pBlock->getFog()->getFogInfo());
    }

    switch (pBlock->getType()) {
    case 'PEFL':
    case 'PEFG':
        mAlphaComp = *pBlock->getAlphaComp();
        mBlend = *pBlock->getBlend();
        mZMode = *pBlock->getZMode();
        mZCompLoc = pBlock->getZCompLoc();
        break;
    }
}

void J3DTexGenBlockPatched::calc(const Mtx modelMtx) {
    Mtx viewMtx;

    for (s32 i = 0; i < ARRAY_SIZE(mTexMtx); i++) {
        if (mTexMtx[i] != NULL) {
            u32 texMtxMode = mTexMtx[i]->getTexMtxInfo().mInfo & 0x3f;
            mTexCoord[i].resetTexMtxReg();

            switch (texMtxMode) {
            case J3DTexMtxMode_EnvmapBasic:
            case J3DTexMtxMode_EnvmapOld:
            case J3DTexMtxMode_Envmap:

                if (j3dSys.checkFlag(8) == 0) {
                    NebulaMtxConcat(j3dSys.getViewMtx(), modelMtx, viewMtx);
                } else {
                    NebulaMtxCopy(j3dSys.getViewMtx(), viewMtx);
                }
                viewMtx[0][3] = 0.0f;
                viewMtx[1][3] = 0.0f;
                viewMtx[2][3] = 0.0f;
                mTexMtx[i]->calc(viewMtx);
                break;
            case J3DTexMtxMode_ProjmapBasic:
            case J3DTexMtxMode_Projmap:
                if (j3dSys.checkFlag(4) == 0) {
                    mTexMtx[i]->calc(modelMtx);
                } else {
                    mTexMtx[i]->calc(j3dDefaultMtx);
                }
                break;
            case J3DTexMtxMode_ViewProjmapBasic:
            case J3DTexMtxMode_ViewProjmap:
                if (j3dSys.checkFlag(4) == 0) {
                    NebulaMtxConcat(j3dSys.getViewMtx(), modelMtx, viewMtx);
                    mTexMtx[i]->calc(viewMtx);
                } else {
                    mTexMtx[i]->calc(j3dSys.getViewMtx());
                }
                break;
            case J3DTexMtxMode_Unknown5:
            case J3DTexMtxMode_EnvmapOldEffectMtx:
            case J3DTexMtxMode_EnvmapEffectMtx:
                if (j3dSys.checkFlag(8) == 0) {
                    NebulaMtxCopy(modelMtx, viewMtx);
                    viewMtx[0][3] = 0.0f;
                    viewMtx[1][3] = 0.0f;
                    viewMtx[2][3] = 0.0f;
                    mTexMtx[i]->calc(viewMtx);
                } else {
                    mTexMtx[i]->calc(j3dDefaultMtx);
                }
                break;
            default:
                mTexMtx[i]->calc(j3dDefaultMtx);
                break;
            }
        }
    }
}

void J3DTexGenBlockPatched::calcWithoutViewMtx(const Mtx modelMtx) {
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
                if (j3dSys.checkFlag(4) == 0) {
                    mTexMtx[i]->calc(modelMtx);
                } else {
                    mTexMtx[i]->calc(j3dDefaultMtx);
                }
                break;
            case J3DTexMtxMode_ViewProjmapBasic:
            case J3DTexMtxMode_ViewProjmap:
                mTexMtx[i]->calc(j3dDefaultMtx);
                break;
            case J3DTexMtxMode_Unknown5:
            case J3DTexMtxMode_EnvmapOldEffectMtx:
            case J3DTexMtxMode_EnvmapEffectMtx:
                if (j3dSys.checkFlag(8) == 0) {
                    NebulaMtxCopy(modelMtx, mtx);
                    mtx[0][3] = 0.0f;
                    mtx[1][3] = 0.0f;
                    mtx[2][3] = 0.0f;
                    mTexMtx[i]->calc(mtx);
                } else {
                    mTexMtx[i]->calc(j3dDefaultMtx);
                }
                break;
            default:
                mTexMtx[i]->calc(j3dDefaultMtx);
                break;
            }
        }
    }
}

void J3DTexGenBlockPatched::calcPostTexMtx(const Mtx modelMtx) {
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

void J3DTexGenBlockPatched::calcPostTexMtxWithoutViewMtx(f32 const (*param_0)[4]) {
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
