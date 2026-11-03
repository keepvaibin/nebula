#pragma once

#include "JSystem/J3DGraphAnimator/J3DMtxBuffer.hpp"
#include "JSystem/J3DGraphAnimator/J3DVtxShader.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DVertex.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DModelData;
class J3DMtxBuffer;
class J3DShapePacket;

enum J3DMdlFlag {
    J3DMdlFlag_None = 0x0,
    J3DMdlFlag_Unk1 = 0x1,
    J3DMdlFlag_UseDefaultJ3D = 0x2,
    J3DMdlFlag_UseSharedSnapshot = 0x20000,
    J3DMdlFlag_UseSingleSnapshot = 0x40000,
    J3DMdlFlag_DifferedSnapshots = 0x80000,
};

typedef void (*J3DCalcCallBack)(J3DModel*, u32 timing);

class J3DModel {
public:
    J3DModel() {
        initialize();
    }

    J3DModel(J3DModelData* pModelData, u32 mdlFlags, u32 mtxBufferFlag) {
        initialize();
        entryModelData(pModelData, mdlFlags, mtxBufferFlag);
    }

    void initialize();
    s32 entryModelData(J3DModelData*, u32, u32);
    s32 createShapePacket(J3DModelData*);
    s32 createMatPacket(J3DModelData*, u32);
    s32 newDifferedSnapshot(u32);
    void lock();
    void unlock();
    void diff();
    void calcAnmMtx();
    void calcWeightEnvelopeMtx();
    inline void calcNrmMtx();
    void calcBumpMtx();
    void calcBBoardMtx();
    void prepareShapePackets();
    void ptrToIndex();
    void makeMesh();
    virtual void update();
    virtual void entry();
    virtual void calc();
    virtual void calcMaterial();
    virtual void calcDiffTexMtx();
    virtual void viewCalc();
    virtual ~J3DModel() {
    }

    J3DModelData* getModelData() {
        return mModelData;
    }

    void onFlag(u32 flag) {
        mFlags |= flag;
    }

    void offFlag(u32 flag) {
        mFlags &= ~flag;
    }

    bool checkFlag(u32 flag) const {
        return (mFlags & flag) ? true : false;
    }

    Mtx& getBaseTRMtx() {
        return mBaseTransformMtx;
    }

    void setBaseTRMtx(Mtx m) {
        NebulaMtxCopy(m, mBaseTransformMtx);
    }

    u32 getMtxCalcMode() {
        return mFlags & (J3DMdlFlag_Unk1 | J3DMdlFlag_UseDefaultJ3D);
    }

    J3DVertexBuffer* getVertexBuffer() {
        return (J3DVertexBuffer*)&mVertexBuffer;
    }

    J3DMatPacket* getMatPacket(u16 idx) const {
        return &mMatPacket[idx];
    }

    J3DShapePacket* getShapePacket(u16 idx) const {
        return &mShapePacket[idx];
    }

    J3DMtxBuffer* getMtxBuffer() {
        return mMtxBuffer;
    }

    void setScaleFlag(int idx, u8 flag) {
        mMtxBuffer->setScaleFlag(idx, flag);
    }

    Mtx33* getBumpMtxPtr(int idx);
    Mtx33* getNrmMtxPtr();
    Mtx* getDrawMtxPtr() {
        return mMtxBuffer->getDrawMtxPtr();
    }

    MtxPtr getDrawMtx(int index) {
        return mMtxBuffer->getDrawMtxPtr()[index];
    }

    void setBaseScale(const Vec& scale) {
        mBaseScale = scale;
    }

    void setUserArea(uintptr_t area) {
        mUserArea = area;
    }

    uintptr_t getUserArea() const {
        return mUserArea;
    }

    Vec* getBaseScale() {
        return &mBaseScale;
    }

    void setAnmMtx(int jointNo, Mtx m) {
        mMtxBuffer->setAnmMtx(jointNo, m);
    }

    void setWeightAnmMtx(int i, Mtx m) {
        mMtxBuffer->setWeightAnmMtx(i, m);
    }

    MtxPtr getAnmMtx(int jointNo) {
        return mMtxBuffer->getAnmMtx(jointNo);
    }

    MtxPtr getWeightAnmMtx(int i) {
        return mMtxBuffer->getWeightAnmMtx(i);
    }

    J3DModelData* mModelData;
    u32 mFlags;
    u32 mDiffFlag;
    J3DCalcCallBack mCalcCallBack;
    uintptr_t mUserArea;
    Vec mBaseScale;
    Mtx mBaseTransformMtx;
    Mtx mInternalView;
    J3DMtxBuffer* mMtxBuffer;
    J3DVertexBuffer mVertexBuffer;
    J3DMatPacket* mMatPacket;
    J3DShapePacket* mShapePacket;
    J3DVtxShader* mUnkCalc1;
    u16 mMotionEpoch;  // counts the model's lives (MotionSignal)
};
