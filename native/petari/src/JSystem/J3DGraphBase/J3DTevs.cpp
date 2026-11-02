#include "JSystem/J3DGraphBase/J3DTevs.hpp"
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTransform.hpp"
#include <NebulaGpuEnums.h>





void J3DTexMtx::calc(f32 const (*param_0)[4]) {
    calcTexMtx(param_0);
}

void J3DTexMtx::calcTexMtx(const Mtx param_0) {
    Mtx44 mtx1;
    Mtx44 mtx2;

    static Mtx qMtx = {
        0.5f, 0.0f, 0.5f, 0.0f, 0.0f, -0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    };
    static Mtx qMtx2 = {
        0.5f, 0.0f, 0.0f, 0.5f, 0.0f, -0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f,
    };

    u32 r28 = mTexMtxInfo.mInfo & 0x3f;
    u32 r30 = (mTexMtxInfo.mInfo >> 7) & 1;
    switch (r28) {
    case 8:
    case 9:
    case 11:
        if (r30 == 0) {
            J3DGetTextureMtx(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            J3DGetTextureMtxMaya(mTexMtxInfo.mSRT, mtx2);
        }
        NebulaMtxConcat(mtx2, qMtx, mtx2);
        J3DMtxProjConcat(mtx2, mTexMtxInfo.mEffectMtx, mtx1);
        NebulaMtxConcat(mtx1, param_0, mMtx);
        break;
    case 7:
        if (r30 == 0) {
            J3DGetTextureMtx(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx1);
        } else if (r30 == 1) {
            J3DGetTextureMtxMaya(mTexMtxInfo.mSRT, mtx1);
        }
        NebulaMtxConcat(mtx1, qMtx, mtx1);
        NebulaMtxConcat(mtx1, param_0, mMtx);
        break;
    case 10:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mtx2);
        }
        NebulaMtxConcat(mtx2, qMtx2, mtx2);
        J3DMtxProjConcat(mtx2, mTexMtxInfo.mEffectMtx, mtx1);
        NebulaMtxConcat(mtx1, param_0, mMtx);
        break;
    case 6:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx1);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mtx1);
        }
        NebulaMtxConcat(mtx1, qMtx2, mtx1);
        NebulaMtxConcat(mtx1, param_0, mMtx);
        break;
    case 1:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx1);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mtx1);
        }
        NebulaMtxConcat(mtx1, param_0, mMtx);
        break;
    case 2:
    case 3:
    case 5:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mtx2);
        }
        J3DMtxProjConcat(mtx2, mTexMtxInfo.mEffectMtx, mtx1);
        NebulaMtxConcat(mtx1, param_0, mMtx);
        break;
    case 4:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mtx2);
        }
        J3DMtxProjConcat(mtx2, mTexMtxInfo.mEffectMtx, mMtx);
        break;
    default:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mMtx);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mMtx);
        }
        break;
    }
}

void J3DTexMtx::calcPostTexMtx(const Mtx param_0) {
    Mtx44 mtx1;
    Mtx44 mtx2;

    static Mtx qMtx = {
        0.5f, 0.0f, 0.5f, 0.0f, 0.0f, -0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    };
    static Mtx qMtx2 = {
        0.5f, 0.0f, 0.0f, 0.5f, 0.0f, -0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f,
    };

    u32 r29 = mTexMtxInfo.mInfo & 0x3f;
    u32 r30 = (mTexMtxInfo.mInfo >> 7) & 1;
    switch (r29) {
    case 8:
    case 11:
        if (r30 == 0) {
            J3DGetTextureMtx(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            J3DGetTextureMtxMaya(mTexMtxInfo.mSRT, mtx2);
        }
        NebulaMtxConcat(mtx2, qMtx, mtx2);
        J3DMtxProjConcat(mtx2, mTexMtxInfo.mEffectMtx, mtx1);
        NebulaMtxConcat(mtx1, param_0, mMtx);
        break;
    case 9:
        if (r30 == 0) {
            J3DGetTextureMtx(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            J3DGetTextureMtxMaya(mTexMtxInfo.mSRT, mtx2);
        }
        NebulaMtxConcat(mtx2, qMtx, mtx2);
        J3DMtxProjConcat(mtx2, mTexMtxInfo.mEffectMtx, mMtx);
        break;
    case 7:
        if (r30 == 0) {
            J3DGetTextureMtx(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx1);
        } else if (r30 == 1) {
            J3DGetTextureMtxMaya(mTexMtxInfo.mSRT, mtx1);
        }
        NebulaMtxConcat(mtx1, qMtx, mMtx);
        break;
    case 10:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mtx2);
        }
        NebulaMtxConcat(mtx2, qMtx2, mtx2);
        J3DMtxProjConcat(mtx2, mTexMtxInfo.mEffectMtx, mtx1);
        NebulaMtxConcat(mtx1, param_0, mMtx);
        break;
    case 6:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx1);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mtx1);
        }
        NebulaMtxConcat(mtx1, qMtx2, mMtx);
        break;
    case 1:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mMtx);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mMtx);
        }
        break;
    case 2:
    case 5:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mtx2);
        }
        J3DMtxProjConcat(mtx2, mTexMtxInfo.mEffectMtx, mtx1);
        NebulaMtxConcat(mtx1, param_0, mMtx);
        break;
    case 3:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mtx2);
        }
        J3DMtxProjConcat(mtx2, mTexMtxInfo.mEffectMtx, mMtx);
        break;
    case 4:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mtx2);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mtx2);
        }
        J3DMtxProjConcat(mtx2, mTexMtxInfo.mEffectMtx, mMtx);
        break;
    default:
        if (r30 == 0) {
            J3DGetTextureMtxOld(mTexMtxInfo.mSRT, mTexMtxInfo.mCenter, mMtx);
        } else if (r30 == 1) {
            J3DGetTextureMtxMayaOld(mTexMtxInfo.mSRT, mMtx);
        }
        break;
    }
}

bool isTexNoReg(void* pList) {
    u8 r31 = ((u8*)pList)[1];
    if (r31 >= 0x80 && r31 <= 0xbb) {
        return true;
    }
    return false;
}

void loadNBTScale(J3DNBTScale& NBTScale) {
    if (NBTScale.mbHasScale == true) {
        j3dSys.setNBTScale(&NBTScale.mScale);
    } else {
        j3dSys.setNBTScale(NULL);
    }
}

J3DTexCoordInfo const j3dDefaultTexCoordInfo[8] = {
    {GPU_MTX2x4, GPU_TG_TEX0, GPU_IDENTITY, 0}, {GPU_MTX2x4, GPU_TG_TEX1, GPU_IDENTITY, 0}, {GPU_MTX2x4, GPU_TG_TEX2, GPU_IDENTITY, 0},
    {GPU_MTX2x4, GPU_TG_TEX3, GPU_IDENTITY, 0}, {GPU_MTX2x4, GPU_TG_TEX4, GPU_IDENTITY, 0}, {GPU_MTX2x4, GPU_TG_TEX5, GPU_IDENTITY, 0},
    {GPU_MTX2x4, GPU_TG_TEX6, GPU_IDENTITY, 0}, {GPU_MTX2x4, GPU_TG_TEX7, GPU_IDENTITY, 0},
};

J3DTexMtxInfo const j3dDefaultTexMtxInfo = {
    0x01,
    0x00,
    0xFF,
    0xFF,
    {0.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 0, 0.0f, 0.0f},
    {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}},
};

J3DIndTexMtxInfo const j3dDefaultIndTexMtxInfo = {0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 1};

const NebulaColor16 j3dDefaultTevColor = {0xFF, 0xFF, 0xFF, 0xFF};

J3DFogInfo const j3dDefaultFogInfo = {
    0x00,   0x00,   0x0140, 0.0f,   0.0f,   0.1f,   10000.0f, 0xFF,   0xFF,   0xFF,   0x00,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,   0x0000, 0x0000, 0x0000,
};

J3DNBTScaleInfo const j3dDefaultNBTScaleInfo = {
    0x00,
    1.0f,
    1.0f,
    1.0f,
};

const J3DColorChanInfo j3dDefaultColorChanInfo = {
    0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0xFF, 0xFF,
};

static u8 j3dTexCoordTable[7623];

void makeTexCoordTable() {
    u8 texMtx[] = {
        GPU_TEXMTX0, GPU_TEXMTX1, GPU_TEXMTX2, GPU_TEXMTX3, GPU_TEXMTX4, GPU_TEXMTX5, GPU_TEXMTX6, GPU_TEXMTX7, GPU_TEXMTX8, GPU_TEXMTX9, GPU_IDENTITY,
    };

    u8* table = j3dTexCoordTable;
    for (u32 i = 0; i < 11; i++) {
        for (u32 j = 0; j < 21; j++) {
            for (int k = 0; k < 0xB; k++) {
                u32 idx = j * 11 + i * 0xe7 + k;
                table[idx * 3 + 0] = i;
                table[idx * 3 + 1] = j;
                table[idx * 3 + 2] = texMtx[k];
            }
        }
    }
}

u8 j3dTevSwapTableTable[1024];

u8 j3dAlphaCmpTable[768];

void makeAlphaCmpTable() {
    u8* table = j3dAlphaCmpTable;
    for (u32 i = 0; i < 8; i++) {
        for (int j = 0; j < 4; j++) {
            for (u32 k = 0; k < 8; k++) {
                u32 idx = i * 32 + j * 8 + k;
                table[idx * 3] = i;
                table[idx * 3 + 1] = j;
                table[idx * 3 + 2] = k;
            }
        }
    }
}

u8 j3dZModeTable[96];

void makeZModeTable() {
    u8* table = j3dZModeTable;
    for (int i = 0; i < 2; i++) {
        for (u32 j = 0; j < 8; j++) {
            for (int k = 0; k < 2; k++) {
                u32 idx = j * 2 + i * 16 + k;
                table[idx * 3 + 0] = i;
                table[idx * 3 + 1] = j;
                table[idx * 3 + 2] = k;
            }
        }
    }
}

void makeTevSwapTable() {
    u8* table = j3dTevSwapTableTable;
    int i = 0;
    do {
        table[0] = i >> 6;
        table[1] = (i >> 4) & 3;
        table[2] = (i >> 2) & 3;
        table[3] = i & 3;
        i++;
        table += 4;
    } while (i < 256);
}





const NebulaColor8 j3dDefaultColInfo = {0xFF, 0xFF, 0xFF, 0xFF};

const NebulaColor8 j3dDefaultAmbInfo = {0x32, 0x32, 0x32, 0x32};

const u8 j3dDefaultNumChans = 1;

const NebulaColor8 j3dDefaultTevKColor = {0xFF, 0xFF, 0xFF, 0xFF};

const J3DBlendInfo j3dDefaultBlendInfo = {GPU_BM_BLEND, GPU_BL_SRCALPHA, GPU_BL_INVSRCALPHA, GPU_LO_NOOP};

const u16 j3dDefaultAlphaCmpID = 0x00E7;

const u16 j3dDefaultZModeID = 0x0017;

