#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DGD.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTransform.hpp"
#include <cstdio>

#ifdef TARGET_PC
// The XF colour word, r in the top byte (the console read the four bytes as a u32).


#else

#endif



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

void J3DTevBlockNull::initialize() {
    mTexNoOffset = 0;
}

void J3DTevBlockPatched::initialize() {
    for (s32 i = 0; i < ARRAY_SIZE(mTexNo); i++) {
        mTexNo[i] = 0xFFFF;
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevStage); i++) {
        mTevStage[i].setStageNo(i);
    }

    for (s32 i = 0; i < ARRAY_SIZE(mTevColor) - 1; i++) {
        mTevColor[i] = j3dDefaultTevColor;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mTevKColor); i++) {
        mTevKColor[i] = j3dDefaultTevKColor;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mTevKColorSel); i++) {
        mTevKColorSel[i] = GX_TEV_KCSEL_K0;
    }

    mTevStageNum = 1;
    mTexNoOffset = 0;
    mTevRegOffset = 0;
}

void J3DTevBlock1::initialize() {
    mTexNo[0] = 0xFFFF;
    mTevStage[0].setStageNo(0);
    mTexNoOffset = 0;
}

void J3DTevBlock2::initialize() {
    mTexNo[0] = 0xFFFF;
    mTexNo[1] = 0xFFFF;
    mTevStageNum = 1;

    mTevStage[0].setStageNo(0);
    mTevStage[1].setStageNo(1);
    mTevKColorSel[0] = GX_TEV_KCSEL_K0;
    mTevKColorSel[1] = GX_TEV_KCSEL_K0;
    mTevKAlphaSel[0] = GX_TEV_KASEL_K0_A;
    mTevKAlphaSel[1] = GX_TEV_KASEL_K0_A;

    for (s32 i = 0; i < ARRAY_SIZE(mTevColor) - 1; i++) {
        mTevColor[i] = j3dDefaultTevColor;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mTevKColor); i++) {
        mTevKColor[i] = j3dDefaultTevKColor;
    }

    mTexNoOffset = 0;
    mTevRegOffset = 0;
}

void J3DTevBlock4::initialize() {
    mTexNo[0] = 0xFFFF;
    mTexNo[1] = 0xFFFF;
    mTexNo[2] = 0xFFFF;
    mTexNo[3] = 0xFFFF;
    mTevStageNum = 1;

    mTevStage[0].setStageNo(0);
    mTevStage[1].setStageNo(1);
    mTevStage[2].setStageNo(2);
    mTevStage[3].setStageNo(3);

    mTevKColorSel[0] = GX_TEV_KCSEL_K0;
    mTevKColorSel[1] = GX_TEV_KCSEL_K0;
    mTevKColorSel[2] = GX_TEV_KCSEL_K0;
    mTevKColorSel[3] = GX_TEV_KCSEL_K0;
    mTevKAlphaSel[0] = GX_TEV_KASEL_K0_A;
    mTevKAlphaSel[1] = GX_TEV_KASEL_K0_A;
    mTevKAlphaSel[2] = GX_TEV_KASEL_K0_A;
    mTevKAlphaSel[3] = GX_TEV_KASEL_K0_A;

    for (s32 i = 0; i < ARRAY_SIZE(mTevColor) - 1; i++) {
        mTevColor[i] = j3dDefaultTevColor;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mTevKColor); i++) {
        mTevKColor[i] = j3dDefaultTevKColor;
    }

    mTexNoOffset = 0;
    mTevRegOffset = 0;
}

void J3DTevBlock16::initialize() {
    for (s32 i = 0; i < ARRAY_SIZE(mTexNo); i++) {
        mTexNo[i] = 0xFFFF;
    }

    mTevStageNum = 1;

    for (s32 i = 0; i < ARRAY_SIZE(mTevColor) - 1; i++) {
        mTevColor[i] = j3dDefaultTevColor;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mTevKColor); i++) {
        mTevKColor[i] = j3dDefaultTevKColor;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mTevKColorSel); i++) {
        mTevKColorSel[i] = GX_TEV_KCSEL_K0;
    }

    for (s32 i = 0; i < ARRAY_SIZE(mTevKColorSel); i++) {
        mTevKAlphaSel[i] = GX_TEV_KASEL_K0_A;
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevStage); i++) {
        mTevStage[i].setStageNo(i);
    }

    mTexNoOffset = 0;
    mTevRegOffset = 0;
}

void J3DIndBlockFull::initialize() {
    mIndTexStageNum = 0;
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

void J3DTevBlockPatched::reset(J3DTevBlock* pBlock) {
    mTevStageNum = pBlock->getTevStageNum();

    for (u32 i = 0; i < ARRAY_SIZE(mTexNo); i++) {
        mTexNo[i] = pBlock->getTexNo(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevColor); i++) {
        mTevColor[i] = *pBlock->getTevColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevKColor); i++) {
        mTevKColor[i] = *pBlock->getTevKColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevStage); i++) {
        mTevStage[i] = *pBlock->getTevStage(i);
        mIndTevStage[i] = *pBlock->getIndTevStage(i);
    }
}

void J3DTevBlock1::reset(J3DTevBlock* pBlock) {
    mTexNo[0] = pBlock->getTexNo(0);
    mTevOrder[0].getTevOrderInfo() = pBlock->getTevOrder(0)->getTevOrderInfo();
    mTevStage[0] = *pBlock->getTevStage(0);
    mIndTevStage[0] = *pBlock->getIndTevStage(0);
}

J3DTevOrder& J3DTevOrder::operator=(const J3DTevOrder& other) {
    __memcpy(this, &other, sizeof(J3DTevOrder));
    return *this;
}

void J3DTevBlock2::reset(J3DTevBlock* pBlock) {
    mTevStageNum = pBlock->getTevStageNum();
    mTexNo[0] = pBlock->getTexNo(0);
    mTexNo[1] = pBlock->getTexNo(1);
    mTevStage[0] = *pBlock->getTevStage(0);
    mTevStage[1] = *pBlock->getTevStage(1);
    mIndTevStage[0] = *pBlock->getIndTevStage(0);
    mIndTevStage[1] = *pBlock->getIndTevStage(1);
    mTevOrder[0].getTevOrderInfo() = pBlock->getTevOrder(0)->getTevOrderInfo();
    mTevOrder[1].getTevOrderInfo() = pBlock->getTevOrder(1)->getTevOrderInfo();
    mTevKColorSel[0] = pBlock->getTevKColorSel(0);
    mTevKColorSel[1] = pBlock->getTevKColorSel(1);
    mTevKAlphaSel[0] = pBlock->getTevKAlphaSel(0);
    mTevKAlphaSel[1] = pBlock->getTevKAlphaSel(1);

    for (u32 i = 0; i < ARRAY_SIZE(mTevColor); i++) {
        mTevColor[i] = *pBlock->getTevColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevKColor); i++) {
        mTevKColor[i] = *pBlock->getTevKColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevSwapModeTable); i++) {
        mTevSwapModeTable[i] = *pBlock->getTevSwapModeTable(i);
    }
}

void J3DTevBlock4::reset(J3DTevBlock* pBlock) {
    mTevStageNum = pBlock->getTevStageNum();
    mTexNo[0] = pBlock->getTexNo(0);
    mTexNo[1] = pBlock->getTexNo(1);
    mTexNo[2] = pBlock->getTexNo(2);
    mTexNo[3] = pBlock->getTexNo(3);
    mTevStage[0] = *pBlock->getTevStage(0);
    mTevStage[1] = *pBlock->getTevStage(1);
    mTevStage[2] = *pBlock->getTevStage(2);
    mTevStage[3] = *pBlock->getTevStage(3);
    mIndTevStage[0] = *pBlock->getIndTevStage(0);
    mIndTevStage[1] = *pBlock->getIndTevStage(1);
    mIndTevStage[2] = *pBlock->getIndTevStage(2);
    mIndTevStage[3] = *pBlock->getIndTevStage(3);
    mTevOrder[0].getTevOrderInfo() = pBlock->getTevOrder(0)->getTevOrderInfo();
    mTevOrder[1].getTevOrderInfo() = pBlock->getTevOrder(1)->getTevOrderInfo();
    mTevOrder[2].getTevOrderInfo() = pBlock->getTevOrder(2)->getTevOrderInfo();
    mTevOrder[3].getTevOrderInfo() = pBlock->getTevOrder(3)->getTevOrderInfo();
    mTevKColorSel[0] = pBlock->getTevKColorSel(0);
    mTevKColorSel[1] = pBlock->getTevKColorSel(1);
    mTevKColorSel[2] = pBlock->getTevKColorSel(2);
    mTevKColorSel[3] = pBlock->getTevKColorSel(3);
    mTevKAlphaSel[0] = pBlock->getTevKAlphaSel(0);
    mTevKAlphaSel[1] = pBlock->getTevKAlphaSel(1);
    mTevKAlphaSel[2] = pBlock->getTevKAlphaSel(2);
    mTevKAlphaSel[3] = pBlock->getTevKAlphaSel(3);

    for (u32 i = 0; i < ARRAY_SIZE(mTevColor); i++) {
        mTevColor[i] = *pBlock->getTevColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevKColor); i++) {
        mTevKColor[i] = *pBlock->getTevKColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevSwapModeTable); i++) {
        mTevSwapModeTable[i] = *pBlock->getTevSwapModeTable(i);
    }
}

void J3DTevBlock16::reset(J3DTevBlock* pBlock) {
    mTevStageNum = pBlock->getTevStageNum();

    for (u32 i = 0; i < ARRAY_SIZE(mTexNo); i++) {
        mTexNo[i] = pBlock->getTexNo(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevOrder); i++) {
        mTevOrder[i] = *pBlock->getTevOrder(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevStage); i++) {
        mTevStage[i] = *pBlock->getTevStage(i);
        mIndTevStage[i] = *pBlock->getIndTevStage(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevColor); i++) {
        mTevColor[i] = *pBlock->getTevColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevKColor); i++) {
        mTevKColor[i] = *pBlock->getTevKColor(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevKColorSel); i++) {
        mTevKColorSel[i] = pBlock->getTevKColorSel(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevKAlphaSel); i++) {
        mTevKAlphaSel[i] = pBlock->getTevKAlphaSel(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mTevSwapModeTable); i++) {
        mTevSwapModeTable[i] = *pBlock->getTevSwapModeTable(i);
    }
}

void J3DIndBlockFull::reset(J3DIndBlock* pBlock) {
    mIndTexStageNum = pBlock->getIndTexStageNum();

    for (u32 i = 0; i < ARRAY_SIZE(mIndTexOrder); i++) {
        mIndTexOrder[i] = *pBlock->getIndTexOrder(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mIndTexMtx); i++) {
        mIndTexMtx[i] = *pBlock->getIndTexMtx(i);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mIndTexCoordScale); i++) {
        mIndTexCoordScale[i] = *pBlock->getIndTexCoordScale(i);
    }
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
                    PSMTXConcat(j3dSys.getViewMtx(), modelMtx, viewMtx);
                } else {
                    PSMTXCopy(j3dSys.getViewMtx(), viewMtx);
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
                    PSMTXConcat(j3dSys.getViewMtx(), modelMtx, viewMtx);
                    mTexMtx[i]->calc(viewMtx);
                } else {
                    mTexMtx[i]->calc(j3dSys.getViewMtx());
                }
                break;
            case J3DTexMtxMode_Unknown5:
            case J3DTexMtxMode_EnvmapOldEffectMtx:
            case J3DTexMtxMode_EnvmapEffectMtx:
                if (j3dSys.checkFlag(8) == 0) {
                    PSMTXCopy(modelMtx, viewMtx);
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
                    PSMTXCopy(modelMtx, mtx);
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
                PSMTXInverse(j3dSys.getViewMtx(), mtx1);
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
                PSMTXInverse(j3dSys.getViewMtx(), mtx2);
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
