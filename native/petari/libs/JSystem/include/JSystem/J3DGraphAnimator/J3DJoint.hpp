#pragma once

#include "JSystem/J3DGraphAnimator/J3DMtxCalc.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphBase/J3DTransform.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DAnmTransform;
class J3DJoint;
class J3DMaterial;
class J3DMtxBuffer;

typedef int (*J3DJointCallBack)(J3DJoint*, int);

class J3DJoint {
public:
    void appendChild(J3DJoint*);
    J3DJoint();
    void entryIn();
    void recursiveCalc();

    u32 getType() const {
        return 'NJNT';
    }

    J3DMaterial* getMesh() {
        return mMesh;
    }

    inline void addMesh(J3DMaterial* pMesh) {
        if (mMesh != NULL) {
            pMesh->setNext(mMesh);
        }

        mMesh = pMesh;
    }

    u16 getJntNo() const {
        return mJntNo;
    }
    u8 getScaleCompensate() const {
        return mScaleCompensate;
    }
    J3DJoint* getYounger() {
        return mYounger;
    }
    void setYounger(J3DJoint* pYounger) {
        mYounger = pYounger;
    }
    void setCurrentMtxCalc(J3DMtxCalc* pMtxCalc) {
        mCurrentMtxCalc = pMtxCalc;
    }
    J3DTransformInfo& getTransformInfo() {
        return mTransformInfo;
    }
    void setTransformInfo(const J3DTransformInfo& i_info) {
        mTransformInfo = i_info;
    }
    Vec* getMax() {
        return &mMax;
    }
    Vec* getMin() {
        return &mMin;
    }
    void setCallBack(J3DJointCallBack callback) {
        mCallBack = callback;
    }
    J3DJointCallBack getCallBack() {
        return mCallBack;
    }
    void setMtxCalc(J3DMtxCalc* i_mtxCalc) {
        mMtxCalc = i_mtxCalc;
    }
    J3DMtxCalc* getMtxCalc() {
        return mMtxCalc;
    }
    J3DMtxCalc* getCurrentMtxCalc() {
        return mCurrentMtxCalc;
    };
    J3DJoint* getChild() {
        return mChild;
    }
    u8 getMtxType() const {
        return (mKind & 0xf0) >> 4;
    }
    void setMtxType(u8 type) {
        mKind = (mKind & ~0xf0) | (type << 4);
    }
    f32 getRadius() const {
        return mBoundingSphereRadius;
    }

    static J3DMtxCalc* mCurrentMtxCalc;

    u8 getKind() const {
        return mKind & 15;
    }

    friend struct J3DJointFactory;
    friend class J3DJointTree;

    void* mCallBackUserData;
    J3DJointCallBack mCallBack;
    void* field_0x8;
    J3DJoint* mChild;
    J3DJoint* mYounger;
    u16 mJntNo;
    u8 mKind;
    u8 mScaleCompensate;
    J3DTransformInfo mTransformInfo;
    f32 mBoundingSphereRadius;
    Vec mMin;
    Vec mMax;
    J3DMtxCalc* mMtxCalc;
    J3DMaterial* mMesh;
};

class J3DMtxCalcNoAnmBase : public J3DMtxCalc {
public:
    J3DMtxCalcNoAnmBase();
    virtual ~J3DMtxCalcNoAnmBase() {
    }
};

template < class A, class B >
class J3DMtxCalcNoAnm : public J3DMtxCalcNoAnmBase {
public:
    J3DMtxCalcNoAnm() {
    }
    virtual ~J3DMtxCalcNoAnm() {
    }
    virtual void init(const Vec& param_0, const Mtx& param_1) {
        B::init(param_0, param_1);
    }
    virtual void calc() {
        J3DTransformInfo& transInfo = getJoint()->getTransformInfo();
        A::calcTransform(transInfo);
    }
};

struct J3DMtxCalcJ3DSysInitSoftimage {
    static void init(const Vec& param_0, const Mtx& param_1) {
        J3DSys::mCurrentS = param_0;
        NebulaMtxCopy(param_1, J3DSys::mCurrentMtx);
    }
};

struct J3DMtxCalcJ3DSysInitMaya {
    static void init(const Vec&, const Mtx& param_1);
};

struct J3DMtxCalcJ3DSysInitBasic {
    static void init(const Vec&, const Mtx& param_1);
};

struct J3DMtxCalcCalcTransformSoftimage {
    static void calcTransform(J3DTransformInfo const&);
};

struct J3DMtxCalcCalcTransformMaya {
    static void calcTransform(J3DTransformInfo const&);
};

struct J3DMtxCalcCalcTransformBasic {
    static void calcTransform(J3DTransformInfo const&);
};
