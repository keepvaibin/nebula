#pragma once

#include "JSystem/J3DGraphBase/J3DMatBlock.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "JSystem/J3DGraphBase/J3DShape.hpp"
#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DShape;
class J3DJoint;
class J3DOutputBlock;
class J3DMaterialAnm;
class J3DMaterialSnapshot;
struct NebulaCookedMaterial;

class J3DMaterial {
public:
    J3DMaterial() {
        initialize();
    }
    ~J3DMaterial() {
    }
    virtual void calc(f32 const (*)[4]);
    virtual void calcDiffTexMtx(f32 const (*)[4]);
    virtual void makeSnapshot();
    virtual void makeSharedSnapshot();
    virtual void load();
    virtual void loadSharedMaterial();
    virtual void patch();
    virtual void diff(u32);
    virtual void reset();
    virtual void change();

    static J3DColorBlock* createColorBlock(u32);
    static J3DTexGenBlock* createTexGenBlock(u32);
    static J3DRegisterBlock* createRegisterBlock(int);
    static J3DIndBlock* createIndBlock(int);
    static J3DOutputBlock* createOutputBlock(u32, u32);
    void initialize();
    void setCurrentMtx();
    void calcCurrentMtx();
    void copy(J3DMaterial*);
    s32 newSharedSnapshot();

    inline J3DMaterialAnm* getMaterialAnm() {
        // The field only ever holds NULL or a MaterialAnmBuffer entry.
        return mMaterialAnm;
    }

    bool isDrawModeOpaTexEdge() {
        return (mMaterialMode & 3) == 0;
    }

    u16 getIndex() {
        return mIndex;
    }

    u32 getDiffFlag() {
        return mDiffFlag;
    }

    J3DTexMtx* getTexMtx(u32 idx) {
        return mTexGenBlock->getTexMtx(idx);
    }
    void setTexMtx(u32 idx, J3DTexMtx* mtx) {
        mTexGenBlock->setTexMtx(idx, mtx);
    }

    J3DNBTScale* getNBTScale() {
        return mTexGenBlock->getNBTScale();
    }

    J3DTexCoord* getTexCoord(u32 idx) {
        return mTexGenBlock->getTexCoord(idx);
    }

    u32 getTexGenNum() const {
        return mTexGenBlock->getTexGenNum();
    }

    J3DMaterial* getNext() {
        return mNext;
    }
    J3DShape* getShape() {
        return mShape;
    }
    J3DRegisterBlock* getRegisterBlock() {
        return mRegisterBlock;
    }
    J3DColorBlock* getColorBlock() {
        return mColorBlock;
    }
    J3DTexGenBlock* getTexGenBlock() {
        return mTexGenBlock;
    }
    J3DMaterialSnapshot* getSharedSnapshot() {
        return mSharedSnapshot;
    }
    J3DIndBlock* getIndBlock() {
        return mIndBlock;
    }
    J3DJoint* getJoint() {
        return mJoint;
    }
    J3DOutputBlock* getOutputBlock() {
        return mOutputBlock;
    }

    u16 getTexNo(u32 idx) {
        return mRegisterBlock->getTexNo(idx);
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

    J3DMaterial* mNext;
    J3DShape* mShape;
    J3DJoint* mJoint;
    u32 mMaterialMode;
    u16 mIndex;
    u32 mInvalid;
    u32 field_0x1c;
    u32 mDiffFlag;
    J3DColorBlock* mColorBlock;
    J3DTexGenBlock* mTexGenBlock;
    J3DRegisterBlock* mRegisterBlock;
    J3DIndBlock* mIndBlock;
    J3DOutputBlock* mOutputBlock;
    J3DMaterial* mpOrigMaterial;
    J3DMaterialAnm* mMaterialAnm;
    J3DCurrentMtx mCurrentMtx;
    J3DMaterialSnapshot* mSharedSnapshot;
    const NebulaCookedMaterial* mNativeMaterial;
};

