#pragma once

#include "Inline.hpp"

#include "JSystem/J3DGraphBase/J3DGD.hpp"
#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include <revolution/gx.h>

extern const J3DLightInfo j3dDefaultLightInfo;
extern const J3DTexCoordInfo j3dDefaultTexCoordInfo[8];

extern const J3DTexMtxInfo j3dDefaultTexMtxInfo;
extern const J3DIndTexMtxInfo j3dDefaultIndTexMtxInfo;

extern const J3DTevStageInfo j3dDefaultTevStageInfo;
extern const J3DIndTevStageInfo j3dDefaultIndTevStageInfo;
extern const J3DTevSwapModeInfo j3dDefaultTevSwapMode;

#ifdef TARGET_PC
// The same objects: CodeWarrior gave C and C++ variables one symbol, native ABIs do not.
namespace J3DTevsDefault {
    using ::j3dDefaultIndTevStageInfo;
    using ::j3dDefaultTevStageInfo;
    using ::j3dDefaultTevSwapMode;
}  // namespace J3DTevsDefault
#else
// doing this matches. figure out why
namespace J3DTevsDefault {
    extern "C" J3DIndTevStageInfo j3dDefaultIndTevStageInfo;
    extern "C" J3DTevStageInfo j3dDefaultTevStageInfo;
    extern "C" J3DTevSwapModeInfo j3dDefaultTevSwapMode;
}  // namespace J3DTevsDefault
#endif
extern const J3DFogInfo j3dDefaultFogInfo;
extern const J3DNBTScaleInfo j3dDefaultNBTScaleInfo;

extern const GXColor j3dDefaultColInfo;
extern const GXColor j3dDefaultAmbInfo;
extern const GXColorS10 j3dDefaultTevColor;
extern const GXColor j3dDefaultTevKColor;
extern u8 j3dAlphaCmpTable[768];
extern const u8 j3dDefaultNumChans;

extern const J3DIndTexCoordScaleInfo j3dDefaultIndTexCoordScaleInfo;

extern const J3DIndTexOrderInfo j3dDefaultIndTexOrderNull;

extern const J3DTevSwapModeTableInfo j3dDefaultTevSwapModeTable;
extern const J3DBlendInfo j3dDefaultBlendInfo;
extern const J3DColorChanInfo j3dDefaultColorChanInfo;

extern const u8 j3dDefaultTevSwapTableID;
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
    /* 0x34 */ GXLightObj mLightObj;
};  // Size = 0x74

// One TEV stage as GX's functions take it: the colour and alpha inputs (GXTevColorArg, GXTevAlphaArg), operations
// (GXTevOp, compare operations included), bias, scale, clamp and output register, and the swap tables the
// rasterised colour and the texture read through.
struct J3DTevStage {
    J3DTevStage() {
#ifdef J3D_TEV_STAGE_MUTABLE_DEFAULTS
        setTevStageInfo(J3DTevsDefault::j3dDefaultTevStageInfo);
        setTevSwapModeInfo(J3DTevsDefault::j3dDefaultTevSwapMode);
#else
        setTevStageInfo(j3dDefaultTevStageInfo);
        setTevSwapModeInfo(j3dDefaultTevSwapMode);
#endif
        mStageNo = 0;
    }

    void setTevColorOp(u8 op, u8 bias, u8 scale, u8 clamp, u8 reg) {
        mColorOp = op;
        mColorBias = bias;
        mColorScale = scale;
        mColorClamp = clamp;
        mColorReg = reg;
    }

    void setTevColorAB(u8 a, u8 b) {
        mColorIn[0] = a;
        mColorIn[1] = b;
    }

    void setTevColorCD(u8 c, u8 d) {
        mColorIn[2] = c;
        mColorIn[3] = d;
    }

    void setAlphaA(u8 a) {
        mAlphaIn[0] = a;
    }

    void setAlphaB(u8 b) {
        mAlphaIn[1] = b;
    }

    void setAlphaC(u8 c) {
        mAlphaIn[2] = c;
    }

    void setAlphaD(u8 d) {
        mAlphaIn[3] = d;
    }

#ifdef J3D_TEV_STAGE_MUTABLE_DEFAULTS
    void setAlphaABCD(u8 a, const u8& b, u8 c, const u8& d) {
#else
    void setAlphaABCD(u8 a, u8 b, u8 c, u8 d) {
#endif
        setAlphaA(a);
        setAlphaB(b);
        setAlphaC(c);
        setAlphaD(d);
    }

    void setTevAlphaOp(u8 op, u8 bias, u8 scale, u8 clamp, u8 reg) {
        mAlphaOp = op;
        mAlphaBias = bias;
        mAlphaScale = scale;
        mAlphaClamp = clamp;
        mAlphaReg = reg;
    }

    // The model file's stage (J3DTevStageInfo; its field names follow the console's register bytes, not their use).
    void setTevStageInfo(const J3DTevStageInfo& info) {
        setTevColorOp(info.mTevAlphaOp, info.mTevAlphaAB, info.mTevSwapModeInfo, info.field_0x8, info.field_0x9);
        setTevColorAB(info.mTevColorOp, info.mTevColorAB);
        setTevColorCD(info.mTevColorCD, info.field_0x4);
        setAlphaABCD(info.field_0xa, info.field_0xb, info.field_0xc, info.field_0xd);
        setTevAlphaOp(info.field_0xe, info.field_0xf, info.field_0x10, info.field_0x11, info.field_0x12);
    }

    J3DTevStage(J3DTevStageInfo const& param_0) {
        setTevStageInfo(param_0);
        setTevSwapModeInfo(j3dDefaultTevSwapMode);
        mStageNo = 0;
    }

    void setTevSwapModeInfo(J3DTevSwapModeInfo const& param_0) {
        setTexSel(param_0.mTexSel);
        setRasSel(param_0.mRasSel);
    }

    void setStageNo(u32 param_0) {
        mStageNo = static_cast< u8 >(param_0);
    }

    void setRasSel(u8 ras_sel) {
        mRasSel = ras_sel;
    }

    void setTexSel(u8 tex_sel) {
        mTexSel = tex_sel;
    }

    // Copies the stage's settings, not its number.
    J3DTevStage& operator=(const J3DTevStage& other) {
        for (int i = 0; i < 4; i++) {
            mColorIn[i] = other.mColorIn[i];
            mAlphaIn[i] = other.mAlphaIn[i];
        }
        mColorOp = other.mColorOp;
        mColorBias = other.mColorBias;
        mColorScale = other.mColorScale;
        mColorClamp = other.mColorClamp;
        mColorReg = other.mColorReg;
        mAlphaOp = other.mAlphaOp;
        mAlphaBias = other.mAlphaBias;
        mAlphaScale = other.mAlphaScale;
        mAlphaClamp = other.mAlphaClamp;
        mAlphaReg = other.mAlphaReg;
        mRasSel = other.mRasSel;
        mTexSel = other.mTexSel;
        return *this;
    }

    u8 mStageNo;
    u8 mColorIn[4];
    u8 mColorOp;
    u8 mColorBias;
    u8 mColorScale;
    u8 mColorClamp;
    u8 mColorReg;
    u8 mAlphaIn[4];
    u8 mAlphaOp;
    u8 mAlphaBias;
    u8 mAlphaScale;
    u8 mAlphaClamp;
    u8 mAlphaReg;
    u8 mRasSel;
    u8 mTexSel;
};

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

extern const J3DTevOrderInfo j3dDefaultTevOrderInfoNull;

struct J3DTevOrder : public J3DTevOrderInfo {
    J3DTevOrder() {
#ifdef TARGET_PC
        // The four bytes as they are (a u32 read and store, on the console).
        memcpy(this, &j3dDefaultTevOrderInfoNull, sizeof(u32));
#else
        *reinterpret_cast< u32* >(this) = *reinterpret_cast< const u32* >(&j3dDefaultTevOrderInfoNull);
#endif
    }

    J3DTevOrder(const J3DTevOrderInfo& info) {
#ifdef TARGET_PC
        // The four bytes as they are (a u32 read and store, on the console).
        memcpy(this, &info, sizeof(u32));
#else
        *reinterpret_cast< u32* >(this) = *reinterpret_cast< const u32* >(&info);
#endif
    }

    J3DTevOrder& operator=(const J3DTevOrder& other) NO_INLINE;
    J3DTevOrderInfo& getTevOrderInfo() {
        return *this;
    }

    u8 getTexMap() {
        return mTexMap;
    }
};

extern u8 j3dTevSwapTableTable[1024];
extern u8 const j3dDefaultTevSwapTableID;

inline u8 calcTevSwapTableID(u8 param_0, u8 param_1, u8 param_2, u8 param_3) {
    return 0x40 * (u8)param_0 + 0x10 * (u8)param_1 + 4 * (u8)param_2 + param_3;
}

struct J3DTevSwapModeTable {
    J3DTevSwapModeTable() {
        mIdx = j3dDefaultTevSwapTableID;
    }

    J3DTevSwapModeTable(J3DTevSwapModeTableInfo const& info) {
        mIdx = calcTevSwapTableID(info.field_0x0, info.field_0x1, info.field_0x2, info.field_0x3);
    }

    J3DTevSwapModeTable& operator=(const J3DTevSwapModeTable& rhs) {
        mIdx = rhs.mIdx;
        return *this;
    }

    J3DTevSwapModeTable& operator=(J3DTevSwapModeTable& other) {
        mIdx = other.mIdx;
        return *this;
    }

    u8 getR() const {
        return *(&j3dTevSwapTableTable[mIdx * 4] + 0);
    }

    u8 getG() const {
        return *(&j3dTevSwapTableTable[mIdx * 4] + 1);
    }

    u8 getB() const {
        return *(&j3dTevSwapTableTable[mIdx * 4] + 2);
    }

    u8 getA() const {
        return *(&j3dTevSwapTableTable[mIdx * 4] + 3);
    }

    /* 0x0 */ u8 mIdx;
};  // Size: 0x1

// One indirect TEV stage as GXSetTevIndirect takes it.
struct J3DIndTevStage {
    J3DIndTevStage() {
        setIndTevStageInfo(J3DTevsDefault::j3dDefaultIndTevStageInfo);
    }

    J3DIndTevStage(J3DIndTevStageInfo const& info) {
        setIndTevStageInfo(info);
    }

    void setIndTevStageInfo(J3DIndTevStageInfo const& info) {
        setIndStage(info.mIndStage);
        setIndFormat(info.mIndFormat);
        setBiasSel(info.mBiasSel);
        setMtxSel(info.mMtxSel);
        setWrapS(info.mWrapS);
        setWrapT(info.mWrapT);
        setPrev(info.mPrev);
        setLod(info.mLod);
        setAlphaSel(info.mAlphaSel);
    }

    void setIndStage(u8 indStage) {
        mIndStage = indStage;
    }

    void setIndFormat(u8 indFormat) {
        mIndFormat = indFormat;
    }

    void setBiasSel(u8 biasSel) {
        mBiasSel = biasSel;
    }

    void setMtxSel(u8 mtxSel) {
        mMtxSel = mtxSel;
    }

    void setWrapS(u8 wrapS) {
        mWrapS = wrapS;
    }

    void setWrapT(u8 wrapT) {
        mWrapT = wrapT;
    }

    void setPrev(u8 prev) {
        mPrev = prev;
    }

    void setLod(u8 lod) {
        mLod = lod;
    }

    void setAlphaSel(u8 alphaSel) {
        mAlphaSel = alphaSel;
    }

    u8 mIndStage;
    u8 mIndFormat;
    u8 mBiasSel;
    u8 mMtxSel;
    u8 mWrapS;
    u8 mWrapT;
    u8 mPrev;
    u8 mLod;
    u8 mAlphaSel;
};

struct J3DNBTScale;
struct J3DTexCoord;
void loadNBTScale(J3DNBTScale& param_0);

bool isTexNoReg(void* param_0);
u16 getTexNoReg(void* param_0);

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
        __memcpy(this, &info, sizeof(J3DTexCoordInfo));
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
        __memcpy(this, &other, sizeof(J3DTexCoordInfo));
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
#ifdef TARGET_PC
        // The four bytes as they are (a u32 read and store, on the console).
        memcpy(this, &j3dDefaultBlendInfo, sizeof(u32));
#else
        *reinterpret_cast< u32* >(this) = *reinterpret_cast< const u32* >(&j3dDefaultBlendInfo);
#endif
    }

    J3DBlend(J3DBlendInfo const& info) {
#ifdef TARGET_PC
        // The four bytes as they are (a u32 read and store, on the console).
        memcpy(this, &info, sizeof(u32));
#else
        *reinterpret_cast< u32* >(this) = *reinterpret_cast< const u32* >(&info);
#endif
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

    GXBlendMode getBlendMode() const {
        return (GXBlendMode)mType;
    }

    GXBlendFactor getSrcFactor() const {
        return (GXBlendFactor)mSrcFactor;
    }

    GXBlendFactor getDstFactor() const {
        return (GXBlendFactor)mDstFactor;
    }

    GXLogicOp getLogicOp() const {
        return (GXLogicOp)mOp;
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

static inline u32 setChanCtrlMacro(u8 enable, GXColorSrc ambSrc, GXColorSrc matSrc, u32 lightMask, GXDiffuseFn diffuseFn, GXAttnFn attnFn) {
    return matSrc << 0 | enable << 1 | (lightMask & 0x0F) << 2 | ambSrc << 6 | ((attnFn == GX_AF_SPEC) ? GX_DF_NONE : diffuseFn) << 7 |
           (attnFn != GX_AF_NONE) << 9 | (attnFn != GX_AF_SPEC) << 10 | (lightMask >> 4 & 0x0F) << 11;
}

// One colour channel as GXSetChanCtrl takes it (a specular channel has no diffuse function).
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
        mDiffuseFn = attnFn == GX_AF_SPEC ? 0 : diffuseFn & 3;
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

