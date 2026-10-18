#pragma once

#include "JSystem/JGeometry/TMatrix.hpp"
#include "JSystem/JGeometry/TVec.hpp"
#include "nebula_disc.hpp"
#include <revolution.h>

struct J3DGXColor : public GXColor {
    J3DGXColor() {
    }

    J3DGXColor(const J3DGXColor& other) : GXColor(other) {
    }

    J3DGXColor(const GXColor color) : GXColor(color) {
    }

    J3DGXColor& operator=(const GXColor color) {
        *(GXColor*)this = color;
        return *this;
    }

    J3DGXColor& operator=(const J3DGXColor& other) {
        GXColor::operator=(other);
        return *this;
    }
};

struct J3DZModeInfo {
    /* 0x0 */ u8 field_0x0;
    /* 0x1 */ u8 field_0x1;
    /* 0x2 */ u8 field_0x2;
    /* 0x3 */ u8 pad;
};

struct J3DFogInfo {
    J3DFogInfo& operator=(J3DFogInfo const&);

    /* 0x00 */ u8 mType;
    /* 0x01 */ u8 mAdjEnable;
    /* 0x02 */ u16 mCenter;
    /* 0x04 */ f32 mStartZ;
    /* 0x08 */ f32 mEndZ;
    /* 0x0C */ f32 mNearZ;
    /* 0x10 */ f32 mFarZ;
    /* 0x14 */ GXColor mColor;
    /* 0x18 */ GXFogAdjTable mFogAdjTable;
};  // Size: 0x2C

#ifdef TARGET_PC
template <>
struct BEData< J3DFogInfo > {
    u8 mType;
    u8 mAdjEnable;
    BE< u16 > mCenter;
    BE< f32 > mStartZ;
    BE< f32 > mEndZ;
    BE< f32 > mNearZ;
    BE< f32 > mFarZ;
    GXColor mColor;
    BE< u16 > mFogAdjTable[10];

    operator J3DFogInfo() const {
        J3DFogInfo info;
        info.mType = mType;
        info.mAdjEnable = mAdjEnable;
        info.mCenter = mCenter;
        info.mStartZ = mStartZ;
        info.mEndZ = mEndZ;
        info.mNearZ = mNearZ;
        info.mFarZ = mFarZ;
        info.mColor = mColor;
        for (int i = 0; i < 10; i++) {
            info.mFogAdjTable.r[i] = mFogAdjTable[i];
        }
        return info;
    }
};
#endif

struct J3DBlendInfo {
    void operator=(J3DBlendInfo const& other) {
        __memcpy(this, &other, sizeof(J3DBlendInfo));
    }

    /* 0x0 */ u8 mType;
    /* 0x1 */ u8 mSrcFactor;
    /* 0x2 */ u8 mDstFactor;
    /* 0x3 */ u8 mOp;
};

struct J3DTextureSRTInfo {
    /* 0x00 */ f32 mScaleX;
    /* 0x04 */ f32 mScaleY;
    /* 0x08 */ s16 mRotation;
    /* 0x0C */ f32 mTranslationX;
    /* 0x10 */ f32 mTranslationY;

    inline void operator=(J3DTextureSRTInfo const& other) {
#ifdef TARGET_PC
        mScaleX = other.mScaleX;
        mScaleY = other.mScaleY;
        mRotation = other.mRotation;
        mTranslationX = other.mTranslationX;
        mTranslationY = other.mTranslationY;
#elif defined(__MWERKS__)
        __REGISTER const f32* src = &other.mScaleX;
        __REGISTER f32* dst = &mScaleX;
        __REGISTER f32 xy;
        asm {
            psq_l xy, 0(src), 0, 0
            psq_st xy, 0(dst), 0, 0
        }
        ;

        // Unclear why there's a 4 byte copy here.
        *(u32*)&mRotation = *(u32*)&other.mRotation;
        src = &other.mTranslationX;
        dst = &mTranslationX;

        asm {
            psq_l xy, 0(src), 0, 0
            psq_st xy, 0(dst), 0, 0
        }
        ;
#endif
    }
};  // Size: 0x14

#ifdef TARGET_PC
template <>
struct BEData< J3DTextureSRTInfo > {
    BE< f32 > mScaleX;
    BE< f32 > mScaleY;
    BE< s16 > mRotation;
    BE< f32 > mTranslationX;
    BE< f32 > mTranslationY;

    operator J3DTextureSRTInfo() const {
        J3DTextureSRTInfo info;
        info.mScaleX = mScaleX;
        info.mScaleY = mScaleY;
        info.mRotation = mRotation;
        info.mTranslationX = mTranslationX;
        info.mTranslationY = mTranslationY;
        return info;
    }
};
#endif

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

    /* 0x00 */ u8 mProjection;
    /* 0x01 */ u8 mInfo;
    /* 0x02 */ u8 field_0x2;
    /* 0x03 */ u8 field_0x3;
    /* 0x04 */ Vec mCenter;
    /* 0x10 */ J3DTextureSRTInfo mSRT;
    /* 0x24 */ Mtx44 mEffectMtx;
};

class J3DLightInfo {
public:
    /* 0x00 */ Vec mLightPosition;
    /* 0x0C */ Vec mLightDirection;
    /* 0x18 */ GXColor mColor;
    /* 0x1C */ Vec mCosAtten;
    /* 0x28 */ Vec mDistAtten;
};

#ifdef TARGET_PC
template <>
struct BEData< J3DTexMtxInfo > {
    u8 mProjection;
    u8 mInfo;
    u8 field_0x2;
    u8 field_0x3;
    BEData< Vec > mCenter;
    BEData< J3DTextureSRTInfo > mSRT;
    BEData< Mtx44 > mEffectMtx;

    operator J3DTexMtxInfo() const {
        J3DTexMtxInfo info;
        info.mProjection = mProjection;
        info.mInfo = mInfo;
        info.field_0x2 = field_0x2;
        info.field_0x3 = field_0x3;
        info.mCenter = mCenter;
        info.mSRT = mSRT;
        mEffectMtx.copyTo(info.mEffectMtx);
        return info;
    }
};

template <>
struct BEData< J3DLightInfo > {
    BEData< Vec > mLightPosition;
    BEData< Vec > mLightDirection;
    GXColor mColor;
    BEData< Vec > mCosAtten;
    BEData< Vec > mDistAtten;

    operator J3DLightInfo() const {
        J3DLightInfo info;
        info.mLightPosition = mLightPosition;
        info.mLightDirection = mLightDirection;
        info.mColor = mColor;
        info.mCosAtten = mCosAtten;
        info.mDistAtten = mDistAtten;
        return info;
    }
};
#endif

struct J3DColorChanInfo {
    /* 0x0 */ u8 mEnable;
    /* 0x1 */ u8 mMatSrc;
    /* 0x2 */ u8 mLightMask;
    /* 0x3 */ u8 mDiffuseFn;
    /* 0x4 */ u8 mAttnFn;
    /* 0x5 */ u8 mAmbSrc;
    /* 0x6 */ u8 pad[2];
};

struct J3DTexCoordInfo {
    /* 0x0 */ u8 mTexGenType;
    /* 0x1 */ u8 mTexGenSrc;
    /* 0x2 */ u8 mTexGenMtx;
    /* 0x3 */ u8 pad;

    J3DTexCoordInfo& operator=(const J3DTexCoordInfo& other) {
#ifdef TARGET_PC
        memcpy(this, &other, sizeof(u32));
#else
        *reinterpret_cast< u32* >(this) = *reinterpret_cast< const u32* >(&other);
#endif
        return *this;
    }
} ATTRIBUTE_ALIGN(2);

struct J3DNBTScaleInfo {
    J3DNBTScaleInfo& operator=(J3DNBTScaleInfo const&);

    /* 0x0 */ u8 mbHasScale;
    /* 0x4 */ Vec mScale;
};  // Size: 0x10

#ifdef TARGET_PC
template <>
struct BEData< J3DNBTScaleInfo > {
    u8 mbHasScale;
    BEData< Vec > mScale;

    operator J3DNBTScaleInfo() const {
        J3DNBTScaleInfo info;
        info.mbHasScale = mbHasScale;
        info.mScale = mScale;
        return info;
    }
};
#endif

struct J3DTevOrderInfo {
    void operator=(const J3DTevOrderInfo& other) {
        __memcpy(this, &other, sizeof(J3DTevOrderInfo));
    }

    /* 0x0 */ u8 mTexCoord;
    /* 0x1 */ u8 mTexMap;
    /* 0x2 */ u8 mColorChan;
    /* 0x3 */ u8 field_0x3;  // Maybe padding
};

struct J3DTevSwapModeTableInfo {
    /* 0x0 */ u8 field_0x0;
    /* 0x1 */ u8 field_0x1;
    /* 0x2 */ u8 field_0x2;
    /* 0x3 */ u8 field_0x3;
};  // Size: 0x4

struct J3DTevStageInfo {
    /* 0x0 */ u8 field_0x0;
    /* 0x1 */ u8 mTevColorOp;
    /* 0x2 */ u8 mTevColorAB;
    /* 0x3 */ u8 mTevColorCD;
    /* 0x4 */ u8 field_0x4;
    /* 0x5 */ u8 mTevAlphaOp;
    /* 0x6 */ u8 mTevAlphaAB;
    /* 0x7 */ u8 mTevSwapModeInfo;
    /* 0x8 */ u8 field_0x8;
    /* 0x8 */ u8 field_0x9;
    /* 0x8 */ u8 field_0xa;
    /* 0x8 */ u8 field_0xb;
    /* 0x8 */ u8 field_0xc;
    /* 0x8 */ u8 field_0xd;
    /* 0x8 */ u8 field_0xe;
    /* 0x8 */ u8 field_0xf;
    /* 0x8 */ u8 field_0x10;
    /* 0x8 */ u8 field_0x11;
    /* 0x8 */ u8 field_0x12;
    /* 0x8 */ u8 field_0x13;
};

struct J3DIndTevStageInfo {
    /* 0x0 */ u8 mIndStage;
    /* 0x1 */ u8 mIndFormat;
    /* 0x2 */ u8 mBiasSel;
    /* 0x3 */ u8 mMtxSel;
    /* 0x4 */ u8 mWrapS;
    /* 0x5 */ u8 mWrapT;
    /* 0x6 */ u8 mPrev;
    /* 0x7 */ u8 mLod;
    /* 0x8 */ u8 mAlphaSel;
    /* 0x9 */ u8 pad[3];
};

struct J3DTevSwapModeInfo {
    /* 0x0 */ u8 mRasSel;
    /* 0x1 */ u8 mTexSel;
    /* 0x2 */ u8 field_0x2;
    /* 0x3 */ u8 field_0x3;
};  // Size: 0x4

struct J3DIndTexOrderInfo {
    /* 0x0 */ u8 mCoord;
    /* 0x1 */ u8 mMap;
    /* 0x2 */ u8 field_0x2;
    /* 0x3 */ u8 field_0x3;

    void operator=(J3DIndTexOrderInfo const& other) {
        __memcpy(this, &other, sizeof(J3DIndTexOrderInfo));
    }
};  // Size: 0x04

struct J3DIndTexMtxInfo {
    J3DIndTexMtxInfo& operator=(J3DIndTexMtxInfo const&);
    /* 0x00 */ Mtx23 field_0x0;
    /* 0x18 */ u8 field_0x18;
};  // Size: 0x1C

#ifdef TARGET_PC
template <>
struct BEData< J3DIndTexMtxInfo > {
    BEData< Mtx23 > field_0x0;
    u8 field_0x18;

    operator J3DIndTexMtxInfo() const {
        J3DIndTexMtxInfo info;
        field_0x0.copyTo(info.field_0x0);
        info.field_0x18 = field_0x18;
        return info;
    }
};
#endif

struct J3DIndTexCoordScaleInfo {
    /* 0x0 */ u8 mScaleS;
    /* 0x1 */ u8 mScaleT;
    /* 0x2 */ u8 field_0x2;
    /* 0x3 */ u8 field_0x3;

    J3DIndTexCoordScaleInfo& operator=(const J3DIndTexCoordScaleInfo& other) {
        __memcpy(this, &other, sizeof(J3DIndTexCoordScaleInfo));
        return *this;
    }
};  // Size: 0x4

struct J3DAlphaCompInfo {
    /* 0x0 */ u8 mComp0;
    /* 0x1 */ u8 mRef0;
    /* 0x2 */ u8 mOp;
    /* 0x3 */ u8 mComp1;
    /* 0x4 */ u8 mRef1;
    /* 0x5 */ u8 field_0x5;
    /* 0x6 */ u8 field_0x6;
    /* 0x7 */ u8 field_0x7;

    J3DAlphaCompInfo& operator=(const J3DAlphaCompInfo& other) {
        mComp0 = other.mComp0;
        mRef0 = other.mRef0;
        mOp = other.mOp;
        mComp1 = other.mComp1;
        mRef1 = other.mRef1;
        return *this;
    }
};

struct J3DGXColorS10 : public GXColorS10 {
    J3DGXColorS10() {
    }

    J3DGXColorS10(const J3DGXColorS10& other) : GXColorS10(other) {
    }

    J3DGXColorS10(const GXColorS10& color) : GXColorS10(color) {
    }

    J3DGXColorS10& operator=(const GXColorS10& color) NO_INLINE {
        __memcpy(this, &color, sizeof(GXColorS10));
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
    reg = (reg & ~0x0180) | (attnFn == GX_AF_SPEC ? 0 : diffuseFn) << 7;
    reg = (reg & ~0x0200) | (attnFn != GX_AF_NONE) << 9;
    reg = (reg & ~0x0400) | (attnFn != GX_AF_SPEC) << 10;
    return reg;
}
