#pragma once

#include "JSystem/J3DGraphBase/J3DNativeShape.hpp"
#include "nebula_disc.hpp"
#include <revolution.h>

class J3DMaterial;
class J3DShapeMtx;
class J3DShapeDraw;
class J3DVertexData;
class J3DDrawMtxData;

enum J3DShpFlag {
    J3DShpFlag_Visible = 0x0001,
    J3DShpFlag_SkinPosCpu = 0x0004,
    J3DShpFlag_SkinNrmCpu = 0x0008,
    J3DShpFlag_Hidden = 0x0010,
    J3DShpFlag_EnableLod = 0x0100,
    J3DShpFlag_NoMtx = 0x0200,
};

class J3DCurrentMtxInfo {
public:
    /* 0x00 */ u32 mMtxIdxRegA;
    /* 0x04 */ u32 mMtxIdxRegB;
};

#ifdef TARGET_PC
template <>
struct BEData< J3DCurrentMtxInfo > {
    BE< u32 > mMtxIdxRegA;
    BE< u32 > mMtxIdxRegB;

    operator J3DCurrentMtxInfo() const {
        J3DCurrentMtxInfo info;
        info.mMtxIdxRegA = mMtxIdxRegA;
        info.mMtxIdxRegB = mMtxIdxRegB;
        return info;
    }
};
#endif

// The matrices a shape's corners take when they carry none: its position matrix's id and texture coordinates 0-7's
// (GX_IDENTITY where a coordinate has no matrix).
class J3DCurrentMtx {
public:
    J3DCurrentMtx() {
        mPosition = 0;
        for (int i = 0; i < 8; i++) {
            mTexture[i] = GX_IDENTITY;
        }
    }

    // The model file's form: six bits per id, the position's and four texture coordinates' in the first word, the
    // other four in the second.
    J3DCurrentMtx& operator=(J3DCurrentMtxInfo const& info) {
        mPosition = info.mMtxIdxRegA & 0x3F;
        for (int i = 0; i < 4; i++) {
            mTexture[i] = (info.mMtxIdxRegA >> (6 * (i + 1))) & 0x3F;
            mTexture[4 + i] = (info.mMtxIdxRegB >> (6 * i)) & 0x3F;
        }
        return *this;
    }

    void load() const {
        J3DNativeShape::setCurrent(mPosition, mTexture);
    }

    inline void setCurrentTexMtx(u8 param_1, u8 param_2, u8 param_3, u8 param_4, u8 param_5, u8 param_6, u8 param_7, u8 param_8) {
        mPosition = 0;
        mTexture[0] = param_1;
        mTexture[1] = param_2;
        mTexture[2] = param_3;
        mTexture[3] = param_4;
        mTexture[4] = param_5;
        mTexture[5] = param_6;
        mTexture[6] = param_7;
        mTexture[7] = param_8;
    }

    /* 0x00 */ u8 mPosition;
    /* 0x01 */ u8 mTexture[8];
};

typedef void (J3DShapeMtx::*J3DShapeMtx_LoadFunc)(int, u16) const;

class J3DShapeMtx {
public:
    J3DShapeMtx(u16 useMtxIndex) : mUseMtxIndex(useMtxIndex) {
    }

    void loadMtxIndx_PNGP(int, u16) const;
    void loadMtxIndx_PCPU(int, u16) const;
    void loadMtxIndx_NCPU(int, u16) const;
    void loadMtxIndx_PNCPU(int, u16) const;

    virtual inline ~J3DShapeMtx();
    virtual inline u32 getType() const;
    virtual u16 getUseMtxNum() const {
        return 1;
    }
    virtual u16 getUseMtxIndex(u16) const {
        return mUseMtxIndex;
    }
    virtual void load() const;
    virtual void calcNBTScale(Vec const&, f32 (*)[3][3], f32 (*)[3][3]);

    static J3DShapeMtx_LoadFunc sMtxLoadPipeline[4];
    static u16 sMtxLoadCache[10];
    static u32 sCurrentPipeline;
    static u8* sCurrentScaleFlag;
    static bool sNBTFlag;
    static bool sLODFlag;
    static u32 sTexMtxLoadType;

    static void setCurrentPipeline(u32 pipeline) {
        sCurrentPipeline = pipeline;
    }

    static void setLODFlag(bool flag) {
        sLODFlag = flag;
    }
    static u32 getLODFlag() {
        return sLODFlag;
    }
    static void resetMtxLoadCache();

protected:
    /* 0x04 */ u16 mUseMtxIndex;
};

class J3DShape {
public:
    J3DShape() {
        initialize();
    }

    void initialize();
    void addTexMtxIndexInDL(_GXAttr, u32);
    void addTexMtxIndexInVcd(_GXAttr);
    void calcNBTScale(Vec const&, f32 (*)[3][3], f32 (*)[3][3]);
    u16 countBumpMtxNum() const;
    void initVtxAttr();
    void loadPreDrawSetting() const;
    void setArrayAndBindPipeline() const;

    virtual void draw() const;
    virtual void drawFast() const;
    virtual void simpleDraw() const;
    virtual void simpleDrawCache() const;

    void loadCurrentMtx() const;

    void onFlag(u32 flag) {
        mFlags |= flag;
    }
    void offFlag(u32 flag) {
        mFlags &= ~flag;
    }
    bool checkFlag(u32 flag) const {
        return (mFlags & flag) ? true : false;
    }

    void setMaterial(J3DMaterial* pMaterial) {
        mMaterial = pMaterial;
    }

    void setDrawMtxDataPointer(J3DDrawMtxData* pMtxData) {
        mDrawMtxData = pMtxData;
    }

    void setVertexDataPointer(J3DVertexData* pVtxData) {
        mVertexData = pVtxData;
    }

    // Its groups' geometry and its model's arrays registered with the renderer before its first draw.
    void prepareGeometry() const {
        if (!mNebulaRegistered) {
            J3DNativeShape::registerShape(const_cast< J3DShape* >(this));
        }
    }
    void show() {
        offFlag(J3DShpFlag_Visible);
    }
    void hide() {
        onFlag(J3DShpFlag_Visible);
    }

    void setCurrentViewNoPtr(u32* pViewNoPtr) {
        mCurrentViewNo = pViewNoPtr;
    }

    void setCurrentMtx(J3DCurrentMtx& mtx) {
        mCurrentMtx = mtx;
    }
#ifdef TARGET_PC
    const J3DCurrentMtx& getCurrentMtx() const {
        return mCurrentMtx;
    }
#endif

    void setScaleFlagArray(u8* pScaleFlagArray) {
        mScaleFlagArray = pScaleFlagArray;
    }

    void setDrawMtx(Mtx** pDrawMtx) {
        mDrawMtx = pDrawMtx;
    }
    void setNrmMtx(Mtx33** pNrmMtx) {
        mNrmMtx = pNrmMtx;
    }
    void setTexMtxLoadType(u32 type) {
        mFlags = (mFlags & 0xFFFF0FFF) | type;
    }
    bool getNBTFlag() const {
        return mHasNBT;
    }
    u32 getBumpMtxOffset() const {
        return mBumpMtxOffset;
    }
    void setBumpMtxOffset(u32 offset) {
        mBumpMtxOffset = offset;
    }
    GXVtxDescList* getVtxDesc() {
        return mVtxDesc;
    }

    J3DMaterial* getMaterial() const {
        return mMaterial;
    }
    u16 getIndex() const {
        return mIndex;
    }
    u32 getTexMtxLoadType() const {
        return mFlags & 0xF000;
    }
    u32 getMtxGroupNum() const {
        return mMtxGroupNum;
    }
    J3DShapeDraw* getShapeDraw(u16 idx) {
        return mShapeDraw[idx];
    }
    J3DShapeMtx* getShapeMtx(u16 idx) {
        return mShapeMtx[idx];
    }
    Vec* getMin() {
        return &mMin;
    }
    Vec* getMax() {
        return &mMax;
    }

    static bool sEnvelopeFlag;
    friend struct J3DShapeFactory;
    friend class J3DJointTree;

    /* 0x04 */ J3DMaterial* mMaterial;
    /* 0x08 */ u16 mIndex;
    /* 0x0A */ u16 mMtxGroupNum;
    /* 0x0C */ u32 mFlags;
    /* 0x10 */ f32 mRadius;
    /* 0x14 */ Vec mMin;
    /* 0x20 */ Vec mMax;
    /* 0x2C */ bool mNebulaRegistered;
    /* 0x30 */ GXVtxDescList* mVtxDesc;
    /* 0x34 */ bool mHasNBT;
    /* 0x38 */ J3DShapeMtx** mShapeMtx;
    /* 0x3C */ J3DShapeDraw** mShapeDraw;
    /* 0x40 */ J3DCurrentMtx mCurrentMtx;
    /* 0x48 */ bool mHasPNMTXIdx;
    /* 0x4C */ J3DVertexData* mVertexData;
    /* 0x50 */ J3DDrawMtxData* mDrawMtxData;
    /* 0x54 */ u8* mScaleFlagArray;
    /* 0x58 */ Mtx** mDrawMtx;
    /* 0x5C */ Mtx33** mNrmMtx;
    /* 0x60 */ u32* mCurrentViewNo;
    /* 0x64 */ u32 mBumpMtxOffset;
};
