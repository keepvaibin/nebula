#pragma once

#include "JSystem/JGeometry/TMatrix.hpp"
#include "JSystem/JGeometry/TVec.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

struct J3DColor : public NebulaColor8 {
    J3DColor() {
    }

    J3DColor(const J3DColor& other) : NebulaColor8(other) {
    }

    J3DColor(const NebulaColor8 color) : NebulaColor8(color) {
    }

    J3DColor& operator=(const NebulaColor8 color) {
        *(NebulaColor8*)this = color;
        return *this;
    }

    J3DColor& operator=(const J3DColor& other) {
        NebulaColor8::operator=(other);
        return *this;
    }
};

struct J3DZModeInfo {
    u8 field_0x0;
    u8 field_0x1;
    u8 field_0x2;
    u8 pad;
};

struct J3DFogAdjTable {
    u16 r[10];
};

struct J3DFogInfo {
    J3DFogInfo& operator=(J3DFogInfo const&);

    u8 mType;
    u8 mAdjEnable;
    u16 mCenter;
    f32 mStartZ;
    f32 mEndZ;
    f32 mNearZ;
    f32 mFarZ;
    NebulaColor8 mColor;
    J3DFogAdjTable mFogAdjTable;
};

struct J3DBlendInfo {
    void operator=(J3DBlendInfo const& other) {
        memcpy((void*)this, &other, sizeof(J3DBlendInfo));
    }

    u8 mType;
    u8 mSrcFactor;
    u8 mDstFactor;
    u8 mOp;
};

struct J3DTextureSRTInfo {
    f32 mScaleX;
    f32 mScaleY;
    s16 mRotation;
    f32 mTranslationX;
    f32 mTranslationY;

    inline void operator=(J3DTextureSRTInfo const& other) {
        mScaleX = other.mScaleX;
        mScaleY = other.mScaleY;
        mRotation = other.mRotation;
        mTranslationX = other.mTranslationX;
        mTranslationY = other.mTranslationY;
    }
};

enum J3DTexMtxMode {
    J3DTexMtxMode_None,
    J3DTexMtxMode_EnvmapBasic,
    J3DTexMtxMode_ProjmapBasic,
    J3DTexMtxMode_ViewProjmapBasic,
    J3DTexMtxMode_Unknown4,
    J3DTexMtxMode_Unknown5,
    J3DTexMtxMode_EnvmapOld,
    J3DTexMtxMode_Envmap,
    J3DTexMtxMode_Projmap,
    J3DTexMtxMode_ViewProjmap,
    J3DTexMtxMode_EnvmapOldEffectMtx,
    J3DTexMtxMode_EnvmapEffectMtx,
};

struct J3DTexMtxInfo {
    void operator=(const J3DTexMtxInfo&);
    void setEffectMtx(Mtx);

    u8 mProjection;
    u8 mInfo;
    u8 field_0x2;
    u8 field_0x3;
    Vec mCenter;
    J3DTextureSRTInfo mSRT;
    Mtx44 mEffectMtx;
};

class J3DLightInfo {
public:
    Vec mLightPosition;
    Vec mLightDirection;
    NebulaColor8 mColor;
    Vec mCosAtten;
    Vec mDistAtten;
};

struct J3DColorChanInfo {
    u8 mEnable;
    u8 mMatSrc;
    u8 mLightMask;
    u8 mDiffuseFn;
    u8 mAttnFn;
    u8 mAmbSrc;
    u8 pad[2];
};

struct J3DTexCoordInfo {
    u8 mTexGenType;
    u8 mTexGenSrc;
    u8 mTexGenMtx;
    u8 pad;

    J3DTexCoordInfo& operator=(const J3DTexCoordInfo& other) {
        memcpy((void*)this, &other, sizeof(u32));
        return *this;
    }
} ATTRIBUTE_ALIGN(2);

struct J3DNBTScaleInfo {
    J3DNBTScaleInfo& operator=(J3DNBTScaleInfo const&);

    u8 mbHasScale;
    Vec mScale;
};

struct J3DTevOrderInfo {
    void operator=(const J3DTevOrderInfo& other) {
        memcpy((void*)this, &other, sizeof(J3DTevOrderInfo));
    }

    u8 mTexCoord;
    u8 mTexMap;
    u8 mColorChan;
    u8 field_0x3;  // Maybe padding
};

struct J3DTevSwapModeTableInfo {
    u8 field_0x0;
    u8 field_0x1;
    u8 field_0x2;
    u8 field_0x3;
};

struct J3DTevStageInfo {
    u8 field_0x0;
    u8 mTevColorOp;
    u8 mTevColorAB;
    u8 mTevColorCD;
    u8 field_0x4;
    u8 mTevAlphaOp;
    u8 mTevAlphaAB;
    u8 mTevSwapModeInfo;
    u8 field_0x8;
    u8 field_0x9;
    u8 field_0xa;
    u8 field_0xb;
    u8 field_0xc;
    u8 field_0xd;
    u8 field_0xe;
    u8 field_0xf;
    u8 field_0x10;
    u8 field_0x11;
    u8 field_0x12;
    u8 field_0x13;
};

struct J3DIndTevStageInfo {
    u8 mIndStage;
    u8 mIndFormat;
    u8 mBiasSel;
    u8 mMtxSel;
    u8 mWrapS;
    u8 mWrapT;
    u8 mPrev;
    u8 mLod;
    u8 mAlphaSel;
    u8 pad[3];
};

struct J3DTevSwapModeInfo {
    u8 mRasSel;
    u8 mTexSel;
    u8 field_0x2;
    u8 field_0x3;
};

struct J3DIndTexOrderInfo {
    u8 mCoord;
    u8 mMap;
    u8 field_0x2;
    u8 field_0x3;

    void operator=(J3DIndTexOrderInfo const& other) {
        memcpy((void*)this, &other, sizeof(J3DIndTexOrderInfo));
    }
};

struct J3DIndTexMtxInfo {
    J3DIndTexMtxInfo& operator=(J3DIndTexMtxInfo const&);
    Mtx23 field_0x0;
    u8 field_0x18;
};

struct J3DIndTexCoordScaleInfo {
    u8 mScaleS;
    u8 mScaleT;
    u8 field_0x2;
    u8 field_0x3;

    J3DIndTexCoordScaleInfo& operator=(const J3DIndTexCoordScaleInfo& other) {
        memcpy((void*)this, &other, sizeof(J3DIndTexCoordScaleInfo));
        return *this;
    }
};

struct J3DAlphaCompInfo {
    u8 mComp0;
    u8 mRef0;
    u8 mOp;
    u8 mComp1;
    u8 mRef1;
    u8 field_0x5;
    u8 field_0x6;
    u8 field_0x7;

    J3DAlphaCompInfo& operator=(const J3DAlphaCompInfo& other) {
        mComp0 = other.mComp0;
        mRef0 = other.mRef0;
        mOp = other.mOp;
        mComp1 = other.mComp1;
        mRef1 = other.mRef1;
        return *this;
    }
};

struct J3DColorWide : public NebulaColor16 {
    J3DColorWide() {
    }

    J3DColorWide(const J3DColorWide& other) : NebulaColor16(other) {
    }

    J3DColorWide(const NebulaColor16& color) : NebulaColor16(color) {
    }

    J3DColorWide& operator=(const NebulaColor16& color) {
        memcpy((void*)this, &color, sizeof(NebulaColor16));
        return *this;
    }
};

inline u16 calcColorChanID(u16 enable, u8 matSrc, u8 lightMask, u8 diffuseFn, u8 attnFn, u8 ambSrc) {
    u32 reg = 0;
    reg = (reg & ~0x0002) | enable << 1;
    reg = (reg & ~0x0001) | matSrc;
    reg = (reg & ~0x0040) | ambSrc << 6;
    reg = (reg & ~0x0004) | bool(lightMask & 0x01) << 2;
    reg = (reg & ~0x0008) | bool(lightMask & 0x02) << 3;
    reg = (reg & ~0x0010) | bool(lightMask & 0x04) << 4;
    reg = (reg & ~0x0020) | bool(lightMask & 0x08) << 5;
    reg = (reg & ~0x0800) | bool(lightMask & 0x10) << 11;
    reg = (reg & ~0x1000) | bool(lightMask & 0x20) << 12;
    reg = (reg & ~0x2000) | bool(lightMask & 0x40) << 13;
    reg = (reg & ~0x4000) | bool(lightMask & 0x80) << 14;
    reg = (reg & ~0x0180) | (attnFn == J3D_AF_SPEC ? 0 : diffuseFn) << 7;
    reg = (reg & ~0x0200) | (attnFn != J3D_AF_NONE) << 9;
    reg = (reg & ~0x0400) | (attnFn != J3D_AF_SPEC) << 10;
    return reg;
}
