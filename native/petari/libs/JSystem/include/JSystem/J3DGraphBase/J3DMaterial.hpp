#pragma once

#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include "JSystem/J3DGraphBase/J3DTevs.hpp"
#include "JSystem/J3DGraphBase/J3DTexture.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DShape;
class J3DJoint;
struct NebulaCookedMaterial;

struct J3DIndTexMtx : public J3DIndTexMtxInfo {
    J3DIndTexMtx() {
        *(J3DIndTexMtxInfo*)this = j3dDefaultIndTexMtxInfo;
    }

    J3DIndTexMtx(const J3DIndTexMtxInfo& info) {
        *(J3DIndTexMtxInfo*)this = info;
    }

    J3DIndTexMtx(const J3DIndTexMtx& other) {
        memcpy((void*)this, &other, sizeof(J3DIndTexMtx));
    }

    J3DIndTexMtx& operator=(const J3DIndTexMtx& other) {
        memcpy((void*)this, &other, sizeof(J3DIndTexMtx));
        return *this;
    }

    ~J3DIndTexMtx() {
    }
};

// A material: everything its draw reads, as plain values. A model data's materials are shared by its models; a model
// whose material moves (animation, a controller, a matrix that follows it) draws its own copy (J3DModel::ownMaterial).
class J3DMaterial {
public:
    J3DMaterial() {
        initialize();
    }

    void initialize();
    // The texture matrices and the coordinates' matrix places at the joint matrix given.
    void calc(f32 const (*)[4]);
    void setCurrentMtx();
    void load();
    // Sent as the material of the draws that follow.
    void useForDraw();

    bool isDrawModeOpaTexEdge() const {
        return (mMaterialMode & 3) == 0;
    }
    u16 getIndex() const {
        return mIndex;
    }
    u32 getID() const {
        return mID;
    }

    J3DTexMtx* getTexMtx(u32 idx) {
        return (mTexMtxUsed & (1u << idx)) != 0 ? &mTexMtx[idx] : nullptr;
    }
    void setTexMtx(u32 idx, const J3DTexMtx& rMtx) {
        mTexMtx[idx] = rMtx;
        mTexMtxUsed |= static_cast< u8 >(1u << idx);
    }
    J3DNBTScale* getNBTScale() {
        return &mNBTScale;
    }
    J3DTexCoord* getTexCoord(u32 idx) {
        return &mTexCoord[idx];
    }
    u32 getTexGenNum() const {
        return mTexGenNum;
    }

    J3DMaterial* getNext() {
        return mNext;
    }
    J3DShape* getShape() {
        return mShape;
    }
    J3DJoint* getJoint() {
        return mJoint;
    }
    void setNext(J3DMaterial* pMaterial) {
        mNext = pMaterial;
    }
    void addShape(J3DShape* pShape) {
        mShape = pShape;
    }
    void setJoint(J3DJoint* pJoint) {
        mJoint = pJoint;
    }

    u16 getTexNo(u32 idx) const {
        return mTexNo[idx];
    }
    void setTexNo(u32 idx, u16 texNo) {
        mTexNo[idx] = texNo;
    }
    J3DColor* getMatColor(u32 idx) {
        return &mMatColor[idx];
    }
    void setMatColor(u32 idx, const J3DColor* pColor) {
        mMatColor[idx] = *pColor;
    }
    J3DColor* getAmbColor(u32 idx) {
        return &mAmbColor[idx];
    }
    J3DColorChan* getColorChan(u32 idx) {
        return &mColorChan[idx];
    }
    J3DColorWide* getRegisterColor(u32 idx) {
        return &mRegisterColor[idx];
    }
    J3DColor* getConstantColor(u32 idx) {
        return &mConstantColor[idx];
    }
    J3DFog* getFog() {
        return &mFog;
    }
    u8 getIndTexStageNum() const {
        return mIndTexStageNum;
    }

    J3DMaterial* mNext;
    J3DShape* mShape;
    J3DJoint* mJoint;
    u32 mMaterialMode;
    u16 mIndex;
    u32 mInvalid;
    // Materials with the same ID are the same in everything: they draw together. Bit 31: this one never joins another.
    u32 mID;
    J3DCurrentMtx mCurrentMtx;
    const NebulaCookedMaterial* mNativeMaterial;
    J3DTexture* mTexture;
    // The model's coordinates go through post-transform matrices.
    bool mPostTexMtx;

    // What the file gives it: without these the neutral values are drawn.
    bool mHasAmbient;
    bool mHasStageColors;
    bool mHasIndirect;
    bool mHasFog;

    J3DColor mMatColor[2];
    J3DColor mAmbColor[2];
    u8 mColorChanNum;
    J3DColorChan mColorChan[4];
    u8 mCull;  // NEBULA_CULL_*

    u32 mTexGenNum;
    J3DTexCoord mTexCoord[8];
    u8 mTexMtxUsed;  // bit i: mTexMtx[i] is in use
    J3DTexMtx mTexMtx[8];
    J3DNBTScale mNBTScale;

    u16 mTexNo[8];
    J3DColorWide mRegisterColor[4];
    J3DColor mConstantColor[4];

    u8 mIndTexStageNum;
    J3DIndTexMtx mIndTexMtx[3];

    J3DFog mFog;
    struct { u8 comp0, op, comp1, ref0, ref1; } mAlphaCompare;
    struct { u8 type, source, destination; } mBlend;  // NEBULA_BLEND_*, NEBULA_FACTOR_*
    struct { u8 enable, func, update; } mDepth;
    u8 mEarlyDepth;
};
