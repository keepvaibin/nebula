#pragma once

#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DSys.hpp"
#include "JSystem/J3DGraphBase/J3DTransform.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DJointTree;

class J3DMtxBuffer {
public:
    J3DMtxBuffer() {
        initialize();
    }

    void initialize();
    s32 create(J3DModelData*, u32);
    J3DError createAnmMtx(J3DModelData*);
    s32 createWeightEnvelopeMtx(J3DModelData*);
    s32 setNoUseDrawMtx();
    s32 createDoubleDrawMtx(J3DModelData*, u32);
    s32 createBumpMtxArray(J3DModelData*, u32);
    void calcWeightEnvelopeMtx();
    void calcDrawMtx(u32, Vec const&, f32 const (&)[3][4]);
    void calcNrmMtx();
    void calcBBoardMtx();

    MtxPtr getAnmMtx(int idx) {
        return mpAnmMtx[idx];
    }
    void setAnmMtx(int i, Mtx m) {
        NebulaMtxCopy(m, (MtxPtr)mpAnmMtx[i]);
    }
    MtxPtr getWeightAnmMtx(int idx) {
        return mpWeightEvlpMtx[idx];
    }
    void setWeightAnmMtx(int i, Mtx m) {
        NebulaMtxCopy(m, (MtxPtr)mpWeightEvlpMtx[i]);
    }
    MtxPtr getUserAnmMtx(int idx) {
        return mpUserAnmMtx[idx];
    }

    void setScaleFlag(int idx, u8 flag) {
        mpScaleFlagArr[idx] = flag;
    }
    u32* getCurrentViewNoPtr() {
        return &mCurrentViewNo;
    }
    u8* getScaleFlagArray() const {
        return mpScaleFlagArr;
    }
    u8 getScaleFlag(int idx) const {
        return mpScaleFlagArr[idx];
    }
    u8 getEnvScaleFlag(int idx) const {
        return mpEvlpScaleFlagArr[idx];
    }
    Mtx** getDrawMtxPtrPtr() {
        return mpDrawMtxArr[1];
    }
    Mtx* getDrawMtxPtr() {
        return mpDrawMtxArr[1][mCurrentViewNo];
    }
    Mtx* getDrawMtx(int idx) {
        return &mpDrawMtxArr[1][mCurrentViewNo][idx];
    }
    Mtx33** getNrmMtxPtrPtr() {
        return mpNrmMtxArr[1];
    }
    Mtx33* getNrmMtxPtr() {
        return mpNrmMtxArr[1][mCurrentViewNo];
    }
    Mtx33* getNrmMtx(int idx) {
        return &mpNrmMtxArr[1][mCurrentViewNo][idx];
    }
    Mtx33*** getBumpMtxPtrPtr() const {
        return mpBumpMtxArr[1];
    }
    Mtx33* getBumpMtxPtr(int idx) {
        return mpBumpMtxArr[1][idx][mCurrentViewNo];
    }
    J3DJointTree* getJointTree() const {
        return mJointTree;
    }

    void setNrmMtx(int idx, Mtx mtx) {
        f32(*dst)[3] = mpNrmMtxArr[1][mCurrentViewNo][idx];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                dst[i][j] = mtx[i][j];
            }
        }
    }

    void swapDrawMtx() {
        Mtx* tmp = mpDrawMtxArr[0][mCurrentViewNo];
        mpDrawMtxArr[0][mCurrentViewNo] = mpDrawMtxArr[1][mCurrentViewNo];
        mpDrawMtxArr[1][mCurrentViewNo] = tmp;
    }

    void swapNrmMtx() {
        Mtx33* tmp = mpNrmMtxArr[0][mCurrentViewNo];
        mpNrmMtxArr[0][mCurrentViewNo] = mpNrmMtxArr[1][mCurrentViewNo];
        mpNrmMtxArr[1][mCurrentViewNo] = tmp;
    }

    static Mtx sNoUseDrawMtx;
    static Mtx33 sNoUseNrmMtx;
    static Mtx* sNoUseDrawMtxPtr;
    static Mtx33* sNoUseNrmMtxPtr;

    J3DJointTree* mJointTree;
    u8* mpScaleFlagArr;
    u8* mpEvlpScaleFlagArr;
    Mtx* mpAnmMtx;
    Mtx* mpWeightEvlpMtx;
    Mtx** mpDrawMtxArr[2];
    Mtx33** mpNrmMtxArr[2];
    Mtx33*** mpBumpMtxArr[2];
    u32 mMtxNum;
    u32 mCurrentViewNo;
    Mtx* mpUserAnmMtx;

    virtual ~J3DMtxBuffer() {
    }
};

void J3DCalcViewBaseMtx(f32 (*param_0)[4], Vec const& param_1, f32 const (&param_2)[3][4], f32 (*param_3)[4]);
