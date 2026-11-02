#pragma once

#include "JSystem/J3DGraphBase/J3DVertex.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class J3DMtxBuffer;
class JUTNameTab;

struct J3DModelHierarchy {
    u16 mType;
    u16 mValue;
};

class J3DJoint;
class J3DMtxCalc;
class J3DMaterialTable;
class J3DShapeTable;

class J3DJointTree {
public:
    J3DJointTree();

    virtual void calc(J3DMtxBuffer*, const Vec&, const Mtx&);
    virtual ~J3DJointTree();

    void makeHierarchy(J3DJoint*, J3DModelHierarchy const**, J3DMaterialTable*, J3DShapeTable*);
    void findImportantMtxIndex();

    J3DModelHierarchy const* getHierarchy() {
        return mHierarchy;
    }
    void setHierarchy(J3DModelHierarchy* hierarchy) {
        mHierarchy = hierarchy;
    }
    void setBasicMtxCalc(J3DMtxCalc* calc) {
        mBasicMtxCalc = calc;
    }

    u16 getJointNum() const {
        return mJointNum;
    }
    u16 getDrawMtxNum() const {
        return mDrawMtxData.mEntryNum;
    }
    Mtx& getInvJointMtx(int idx) {
        return mInvJointMtx[idx];
    }

    u8 getDrawMtxFlag(u16 idx) const {
        return mDrawMtxData.mDrawMtxFlag[idx];
    }

    u16 getDrawMtxIndex(u16 idx) const {
        return mDrawMtxData.mDrawMtxIndex[idx];
    }
    u16 getWEvlpMtxNum() const {
        return mWEvlpMtxNum;
    }
    u16* getWEvlpImportantMtxIndex() const {
        return mWEvlpImportantMtxIdx;
    }
    u16* getWEvlpMixMtxIndex() const {
        return mWEvlpMixMtxIndex;
    }
    f32* getWEvlpMixWeight() const {
        return mWEvlpMixWeight;
    }
    u16 getDrawFullWgtMtxNum() const {
        return mDrawMtxData.mDrawFullWgtMtxNum;
    }
    u8 getWEvlpMixMtxNum(u16 idx) const {
        return mWEvlpMixMtxNum[idx];
    }

    J3DDrawMtxData* getDrawMtxData() {
        return &mDrawMtxData;
    }
    JUTNameTab* getJointName() const {
        return mJointName;
    }
    u32 getModelDataType() const {
        return mModelDataType;
    }
    void setModelDataType(u32 type) {
        mModelDataType = type;
    }
    J3DJoint* getRootNode() {
        return mRootNode;
    }
    J3DJoint* getJointNodePointer(u16 idx) const {
        return mJointNodePointer[idx];
    }

    J3DMtxCalc* getBasicMtxCalc() {
        return mBasicMtxCalc;
    }

    void setFlag(u32 flag) {
        mFlags = flag;
    }

    J3DModelHierarchy* mHierarchy;
    u32 mFlags;
    u32 mModelDataType;
    J3DJoint* mRootNode;
    J3DMtxCalc* mBasicMtxCalc;
    J3DJoint** mJointNodePointer;
    u16 mJointNum;
    u16 mWEvlpMtxNum;
    u8* mWEvlpMixMtxNum;
    // In the file (host order, as the importer cooks EVP1).
    u16* mWEvlpMixMtxIndex;
    f32* mWEvlpMixWeight;
    Mtx* mInvJointMtx;
    u16* mWEvlpImportantMtxIdx;
    J3DDrawMtxData mDrawMtxData;
    u32 field_0x40;
    JUTNameTab* mJointName;
};
