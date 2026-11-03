#pragma once


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
extern const NebulaColor16 j3dDefaultRegisterColor;
extern const NebulaColor8 j3dDefaultConstantColor;
extern const u8 j3dDefaultNumChans;



extern const J3DBlendInfo j3dDefaultBlendInfo;
extern const J3DColorChanInfo j3dDefaultColorChanInfo;


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

    J3DLightInfo mInfo;
    J3DNativeLight mLightObj;
};


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





struct J3DNBTScale;
struct J3DTexCoord;
void loadNBTScale(J3DNBTScale& param_0);


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
        memcpy((void*)this, &info, sizeof(J3DTexCoordInfo));
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

    J3DTexCoord& operator=(const J3DTexCoord& other) {
        memcpy((void*)this, &other, sizeof(J3DTexCoordInfo));
        return *this;
    }

    void resetTexMtxReg() {
        mTexMtxReg = mInfo.mTexGenMtx;
    }

    J3DTexCoordInfo mInfo;
    u16 mTexMtxReg;
};

struct J3DBlend : public J3DBlendInfo {
    J3DBlend() {
        // The four bytes as they are.
        memcpy((void*)this, &j3dDefaultBlendInfo, sizeof(u32));
    }

    J3DBlend(J3DBlendInfo const& info) {
        // The four bytes as they are.
        memcpy((void*)this, &info, sizeof(u32));
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

struct J3DAlphaComp {
    J3DAlphaComp() : mComp0(NEBULA_COMPARE_ALWAYS), mOp(0), mComp1(NEBULA_COMPARE_ALWAYS), mRef0(), mRef1() {
    }
    explicit J3DAlphaComp(const J3DAlphaCompInfo& info) {
        setAlphaCompInfo(info);
    }

    void setAlphaCompInfo(const J3DAlphaCompInfo& info) {
        mComp0 = info.mComp0;
        mOp = info.mOp;
        mComp1 = info.mComp1;
        mRef0 = info.mRef0;
        mRef1 = info.mRef1;
    }
    u8 getComp0() const {
        return mComp0;
    }
    u8 getOp() const {
        return mOp;
    }
    u8 getComp1() const {
        return mComp1;
    }
    u8 getRef0() const {
        return mRef0;
    }
    u8 getRef1() const {
        return mRef1;
    }

    u8 mComp0;
    u8 mOp;
    u8 mComp1;
    u8 mRef0;
    u8 mRef1;
};


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
        set(info.mEnable, info.mMatSrc, info.mLightMask, info.mDiffuseFn, info.mAttnFn,
            info.mAmbSrc);
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

struct J3DZMode {
    J3DZMode() : mCompare(1), mFunc(NEBULA_COMPARE_LEQUAL), mUpdate(1) {
    }
    J3DZMode(J3DZModeInfo const& info) {
        setZModeInfo(info);
    }

    void setZModeInfo(const J3DZModeInfo& info) {
        mCompare = info.field_0x0;
        mFunc = info.field_0x1;
        mUpdate = info.field_0x2;
    }
    void setCompareEnable(u8 i_compare) {
        mCompare = i_compare;
    }
    void setFunc(u8 i_func) {
        mFunc = i_func;
    }
    void setUpdateEnable(u8 i_enable) {
        mUpdate = i_enable;
    }
    u8 getCompareEnable() const {
        return mCompare;
    }
    u8 getFunc() const {
        return mFunc;
    }
    u8 getUpdateEnable() const {
        return mUpdate;
    }

    u8 mCompare;
    u8 mFunc;
    u8 mUpdate;
};

