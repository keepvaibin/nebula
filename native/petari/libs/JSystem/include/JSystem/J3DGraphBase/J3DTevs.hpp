#pragma once

#include "Inline.hpp"

#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include <NebulaGpu.h>

extern const J3DLightInfo j3dDefaultLightInfo;
extern const J3DTexCoordInfo j3dDefaultTexCoordInfo[8];

extern const J3DTexMtxInfo j3dDefaultTexMtxInfo;
extern const J3DIndTexMtxInfo j3dDefaultIndTexMtxInfo;


extern const J3DFogInfo j3dDefaultFogInfo;
extern const J3DNBTScaleInfo j3dDefaultNBTScaleInfo;

extern const NebulaColor8 j3dDefaultColInfo;
extern const NebulaColor8 j3dDefaultAmbInfo;
extern const NebulaColor16 j3dDefaultTevColor;
extern const NebulaColor8 j3dDefaultTevKColor;
extern u8 j3dAlphaCmpTable[768];
extern const u8 j3dDefaultNumChans;



extern const J3DBlendInfo j3dDefaultBlendInfo;
extern const J3DColorChanInfo j3dDefaultColorChanInfo;

extern const u16 j3dDefaultAlphaCmpID;
extern const u16 j3dDefaultZModeID;

class J3DLightObj {
public:
    J3DLightObj() {
        mInfo = j3dDefaultLightInfo;
    }

    J3DLightInfo* getLightInfo() {
        return &mInfo;
    }

    J3DLightObj& operator=(J3DLightObj const& other) {
        mInfo = other.mInfo;
        return *this;
    }

    /* 0x00 */ J3DLightInfo mInfo;
    /* 0x34 */ J3DNativeLight mLightObj;
};  // Size = 0x74

// One TEV stage as the console's functions take it: the colour and alpha inputs (GpuTevColorArg, GpuTevAlphaArg), operations
// (u8, compare operations included), bias, scale, clamp and output register, and the swap tables the
// rasterised colour and the texture read through.
void makeTexCoordTable();
void makeAlphaCmpTable();
void makeZModeTable();
void makeTevSwapTable();

extern const J3DNBTScaleInfo j3dDefaultNBTScaleInfo;

struct J3DNBTScale : public J3DNBTScaleInfo {
    J3DNBTScale() {
        mbHasScale = j3dDefaultNBTScaleInfo.mbHasScale;
        mScale.x = j3dDefaultNBTScaleInfo.mScale.x;
        mScale.y = j3dDefaultNBTScaleInfo.mScale.y;
        mScale.z = j3dDefaultNBTScaleInfo.mScale.z;
    }

    J3DNBTScale(J3DNBTScaleInfo const& info) {
        mbHasScale = info.mbHasScale;
        mScale.x = info.mScale.x;
        mScale.y = info.mScale.y;
        mScale.z = info.mScale.z;
    }

    Vec* getScale() {
        return &mScale;
    }
};


extern u8 j3dTevSwapTableTable[1024];

inline u8 calcTevSwapTableID(u8 param_0, u8 param_1, u8 param_2, u8 param_3) {
    return 0x40 * (u8)param_0 + 0x10 * (u8)param_1 + 4 * (u8)param_2 + param_3;
}

  // Size: 0x1

// One indirect stage.
struct J3DNBTScale;
struct J3DTexCoord;
void loadNBTScale(J3DNBTScale& param_0);

bool isTexNoReg(void* param_0);

extern J3DTexCoordInfo const j3dDefaultTexCoordInfo[8];

struct J3DTexCoord {
    J3DTexCoord() {
        mInfo = j3dDefaultTexCoordInfo[0];
        mTexMtxReg = mInfo.mTexGenMtx;
    }

    J3DTexCoord(const J3DTexCoordInfo& info) {
        mInfo = info;
        mTexMtxReg = mInfo.mTexGenMtx;
    }

    void setTexCoordInfo(const J3DTexCoordInfo& info) {
        memcpy(this, &info, sizeof(J3DTexCoordInfo));
    }

    u8 getTexGenType() const {
        return mInfo.mTexGenType;
    }

    u8 getTexGenSrc() const {
        return mInfo.mTexGenSrc;
    }

    u8 getTexGenMtx() const {
        return mInfo.mTexGenMtx;
    }

    u32 getTexMtxReg() const {
        return mTexMtxReg & 0xff;
    }

    void setTexGenMtx(u8 param_1) {
        mInfo.mTexGenMtx = param_1;
    }

    void setTexMtxReg(u16 reg) {
        mTexMtxReg = reg;
    }

    J3DTexCoord& operator=(const J3DTexCoord& other) NO_INLINE {
        memcpy(this, &other, sizeof(J3DTexCoordInfo));
        return *this;
    }

    void resetTexMtxReg() {
        mTexMtxReg = mInfo.mTexGenMtx;
    }

    /* 0x0 */ J3DTexCoordInfo mInfo;
    /* 0x4 */ u16 mTexMtxReg;
};  // Size: 0x6

struct J3DBlend : public J3DBlendInfo {
    J3DBlend() {
        // The four bytes as they are (a u32 read and store, on the console).
        memcpy(this, &j3dDefaultBlendInfo, sizeof(u32));
    }

    J3DBlend(J3DBlendInfo const& info) {
        // The four bytes as they are (a u32 read and store, on the console).
        memcpy(this, &info, sizeof(u32));
    }

    void setType(u8 i_type) {
        mType = i_type;
    }

    void setSrcFactor(u8 i_factor) {
        mSrcFactor = i_factor;
    }

    void setDstFactor(u8 i_factor) {
        mDstFactor = i_factor;
    }

    u8 getBlendMode() const {
        return (u8)mType;
    }

    u8 getSrcFactor() const {
        return (u8)mSrcFactor;
    }

    u8 getDstFactor() const {
        return (u8)mDstFactor;
    }

    u8 getLogicOp() const {
        return (u8)mOp;
    }

    void setBlendInfo(const J3DBlendInfo& i_blendInfo) {
        *static_cast< J3DBlendInfo* >(this) = i_blendInfo;
    }
};

struct J3DFog : public J3DFogInfo {
    inline J3DFog() {
        *(J3DFogInfo*)this = j3dDefaultFogInfo;
    }

    ~J3DFog() {
    }

    J3DFog* getFogInfo() {
        return this;
    }

    void setFogInfo(J3DFogInfo info) {
        *(J3DFogInfo*)this = info;
    }

    void setFogInfo(const J3DFogInfo* info) {
        *(J3DFogInfo*)this = *info;
    }
};

inline u16 calcAlphaCmpID(u8 comp0, u8 op, u8 comp1) {
    return (comp0 << 5) + (op << 3) + (comp1);
}

struct J3DAlphaComp {
    J3DAlphaComp() : mID(j3dDefaultAlphaCmpID), mRef0(), mRef1() {
    }

    J3DAlphaComp(u16 id) : mID(id), mRef0(), mRef1() {
    }

    explicit J3DAlphaComp(const J3DAlphaCompInfo& info) {
        mID = calcAlphaCmpID(info.mComp0, info.mOp, info.mComp1);
        mRef0 = info.mRef0;
        mRef1 = info.mRef1;
    }

    J3DAlphaComp& operator=(u16 id) {
        mID = id;
        return *this;
    }

    J3DAlphaComp& operator=(const J3DAlphaComp& rhs) {
        mID = rhs.mID;
        mRef0 = rhs.mRef0;
        mRef1 = rhs.mRef1;
        return *this;
    }

    J3DAlphaComp& operator=(J3DAlphaComp& rhs) {
        mID = rhs.mID;
        mRef0 = rhs.mRef0;
        mRef1 = rhs.mRef1;
        return *this;
    }

    void setAlphaCompInfo(const J3DAlphaCompInfo& info) {
        mRef0 = info.mRef0;
        mRef1 = info.mRef1;
        mID = calcAlphaCmpID(info.mComp0, info.mOp, info.mComp1);
    }

    u8 getComp0() const {
        return *(&j3dAlphaCmpTable[mID * 3] + 0);
    }

    u8 getOp() const {
        return *(&j3dAlphaCmpTable[mID * 3] + 1);
    }

    u8 getComp1() const {
        return *(&j3dAlphaCmpTable[mID * 3] + 2);
    }

    u8 getRef0() const {
        return mRef0;
    }

    u8 getRef1() const {
        return mRef1;
    }

    /* 0x00 */ u16 mID;
    /* 0x02 */ u8 mRef0;
    /* 0x03 */ u8 mRef1;
};  // Size: 0x4

static inline u32 setChanCtrlMacro(u8 enable, u8 ambSrc, u8 matSrc, u32 lightMask, u8 diffuseFn, u8 attnFn) {
    return matSrc << 0 | enable << 1 | (lightMask & 0x0F) << 2 | ambSrc << 6 | ((attnFn == J3D_AF_SPEC) ? J3D_DF_NONE : diffuseFn) << 7 |
           (attnFn != J3D_AF_NONE) << 9 | (attnFn != J3D_AF_SPEC) << 10 | (lightMask >> 4 & 0x0F) << 11;
}

// One colour channel (a specular channel has no diffuse function).
class J3DColorChan {
public:
    J3DColorChan() {
        setColorChanInfo(j3dDefaultColorChanInfo);
    }

    J3DColorChan(J3DColorChanInfo const& info) {
        set(info.mEnable, info.mMatSrc, info.mLightMask, info.mDiffuseFn, info.mAttnFn,
            info.mAmbSrc == 0xFF ? 0 : info.mAmbSrc);
    }

    inline void setColorChanInfo(J3DColorChanInfo const& info) {
        // !@bug: It compares info.mAmbSrc (an 8 bit integer) with 0xFFFF instead of 0xFF.
        // This inline is only called by the default constructor J3DColorChan().
        // The J3DColorChan(const J3DColorChanInfo&) constructor does not call this inline, and instead duplicates the
        // same logic but without the bug.
        // See J3DMaterialFactory::newColorChan - both the bugged and correct behavior are present there, as it calls
        // both constructors.
        set(info.mEnable, info.mMatSrc, info.mLightMask, info.mDiffuseFn, info.mAttnFn,
            info.mAmbSrc == 0xFFFF ? 0 : info.mAmbSrc);
    }

    void set(u8 enable, u8 matSrc, u8 lightMask, u8 diffuseFn, u8 attnFn, u8 ambSrc) {
        mEnable = enable & 1;
        mMatSrc = matSrc & 1;
        mAmbSrc = ambSrc & 1;
        mLightMask = lightMask;
        mDiffuseFn = attnFn == J3D_AF_SPEC ? 0 : diffuseFn & 3;
        mAttnFn = attnFn;
    }

    u8 getLightMask() const {
        return mLightMask;
    }

    void setLightMask(u8 param_1) {
        mLightMask = param_1;
    }

    u8 getEnable() const {
        return mEnable;
    }

    u8 getAmbSrc() const {
        return mAmbSrc;
    }

    u8 getMatSrc() const {
        return mMatSrc;
    }

    u8 getDiffuseFn() const {
        return mDiffuseFn;
    }

    inline u8 getAttnFn() const {
        return mAttnFn;
    }

    J3DColorChan& operator=(const J3DColorChan& other) {
        mEnable = other.mEnable;
        mMatSrc = other.mMatSrc;
        mAmbSrc = other.mAmbSrc;
        mLightMask = other.mLightMask;
        mDiffuseFn = other.mDiffuseFn;
        mAttnFn = other.mAttnFn;
        return *this;
    }

    u8 mEnable;
    u8 mMatSrc;
    u8 mAmbSrc;
    u8 mLightMask;
    u8 mDiffuseFn;
    u8 mAttnFn;
};

inline u16 calcZModeID(u8 param_0, u8 param_1, u8 param_2) {
    return param_1 * 2 + param_0 * 0x10 + param_2;
}

extern u8 j3dZModeTable[96];

struct J3DZMode {
    J3DZMode() : mZModeID(j3dDefaultZModeID) {
    }

    J3DZMode(J3DZModeInfo const& info) : mZModeID(calcZModeID(info.field_0x0, info.field_0x1, info.field_0x2)) {
    }

    J3DZMode& operator=(u16 zModeID) {
        mZModeID = zModeID;
        return *this;
    }

    J3DZMode& operator=(const J3DZMode& other) {
        mZModeID = other.mZModeID;
        return *this;
    }

    void setZModeInfo(const J3DZModeInfo& info) {
        mZModeID = calcZModeID(info.field_0x0, info.field_0x1, info.field_0x2);
    }

    void setCompareEnable(u8 i_compare) {
        mZModeID = calcZModeID(i_compare, j3dZModeTable[mZModeID * 3 + 1], j3dZModeTable[mZModeID * 3 + 2]);
    }

    void setFunc(u8 i_func) {
        mZModeID = calcZModeID(j3dZModeTable[mZModeID * 3], i_func, j3dZModeTable[mZModeID * 3 + 2]);
    }

    void setUpdateEnable(u8 i_enable) {
        mZModeID = calcZModeID(j3dZModeTable[mZModeID * 3], j3dZModeTable[mZModeID * 3 + 1], i_enable);
    }

    u8 getCompareEnable() const {
        return *(&j3dZModeTable[mZModeID * 3] + 0);
    }

    u8 getFunc() const {
        return *(&j3dZModeTable[mZModeID * 3] + 1);
    }

    u8 getUpdateEnable() const {
        return *(&j3dZModeTable[mZModeID * 3] + 2);
    }

    /* 0x0 */ u16 mZModeID;
};

