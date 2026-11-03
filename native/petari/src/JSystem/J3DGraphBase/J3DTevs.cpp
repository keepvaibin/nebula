#include "JSystem/J3DGraphBase/J3DTevs.hpp"
#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTransform.hpp"





void J3DTexMtx::calc(f32 const (*param_0)[4]) {
    calcTexMtx(param_0);
}

void J3DTexMtx::calcTexMtx(const Mtx param_0) {
    Mtx44 mtx1;
    Mtx44 mtx2;

    static Mtx qMtx = {
        {0.5f, 0.0f, 0.5f, 0.0f}, {0.0f, -0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f},
    };
    static Mtx qMtx2 = {
        {0.5f, 0.0f, 0.0f, 0.5f}, {0.0f, -0.5f, 0.0f, 0.5f}, {0.0f, 0.0f, 1.0f, 0.0f},
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
        {0.5f, 0.0f, 0.5f, 0.0f}, {0.0f, -0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f},
    };
    static Mtx qMtx2 = {
        {0.5f, 0.0f, 0.0f, 0.5f}, {0.0f, -0.5f, 0.0f, 0.5f}, {0.0f, 0.0f, 1.0f, 0.0f},
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

void loadNBTScale(J3DNBTScale& NBTScale) {
    if (NBTScale.mbHasScale == true) {
        j3dSys.setNBTScale(&NBTScale.mScale);
    } else {
        j3dSys.setNBTScale(NULL);
    }
}

J3DTexCoordInfo const j3dDefaultTexCoordInfo[8] = {
    {J3D_MTX2x4, J3D_TG_TEX0, J3D_IDENTITY, 0}, {J3D_MTX2x4, J3D_TG_TEX1, J3D_IDENTITY, 0}, {J3D_MTX2x4, J3D_TG_TEX2, J3D_IDENTITY, 0},
    {J3D_MTX2x4, J3D_TG_TEX3, J3D_IDENTITY, 0}, {J3D_MTX2x4, J3D_TG_TEX4, J3D_IDENTITY, 0}, {J3D_MTX2x4, J3D_TG_TEX5, J3D_IDENTITY, 0},
    {J3D_MTX2x4, J3D_TG_TEX6, J3D_IDENTITY, 0}, {J3D_MTX2x4, J3D_TG_TEX7, J3D_IDENTITY, 0},
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

J3DIndTexMtxInfo const j3dDefaultIndTexMtxInfo = {{{0.5f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}}, 1};

const NebulaColor16 j3dDefaultRegisterColor = {0xFF, 0xFF, 0xFF, 0xFF};

J3DFogInfo const j3dDefaultFogInfo = {
    0x00,   0x00,   0x0140, 0.0f,   0.0f,   0.1f,   10000.0f, {0xFF,   0xFF,   0xFF,   0x00},
    {0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,   0x0000, 0x0000, 0x0000},
};

J3DNBTScaleInfo const j3dDefaultNBTScaleInfo = {
    0x00,
    {1.0f,
    1.0f,
    1.0f},
};

const J3DColorChanInfo j3dDefaultColorChanInfo = {
    0x00, 0x00, 0x00, 0x02, 0x02, 0x00, {0xFF, 0xFF},
};





const NebulaColor8 j3dDefaultColInfo = {0xFF, 0xFF, 0xFF, 0xFF};

const NebulaColor8 j3dDefaultAmbInfo = {0x32, 0x32, 0x32, 0x32};

const u8 j3dDefaultNumChans = 1;

const NebulaColor8 j3dDefaultConstantColor = {0xFF, 0xFF, 0xFF, 0xFF};

const J3DBlendInfo j3dDefaultBlendInfo = {J3D_BM_BLEND, J3D_BL_SRCALPHA, J3D_BL_INVSRCALPHA, J3D_LO_NOOP};



