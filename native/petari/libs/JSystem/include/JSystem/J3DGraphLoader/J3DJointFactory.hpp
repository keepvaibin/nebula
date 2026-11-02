#pragma once

#include "JSystem/J3DGraphBase/J3DTransform.hpp"

class J3DJoint;
struct J3DJointBlock;

// In host order as the importer cooks it (j3d_model, JNT1).
struct J3DJointInitData {
    u16 mKind;
    bool mScaleCompensate;
    J3DTransformInfo mTransformInfo;
    f32 mRadius;
    Vec mMin;
    Vec mMax;
};
static_assert(sizeof(J3DJointInitData) == 0x40, "the file's joint data");

struct J3DJointFactory {
    J3DJointFactory(J3DJointBlock const&);
    J3DJoint* create(int);

    J3DJointInitData* mJointInitData;
    u16* mIndexTable;

    u16 getKind(int no) const {
        return mJointInitData[mIndexTable[no]].mKind;
    }
    u8 getScaleCompensate(int no) const {
        return mJointInitData[mIndexTable[no]].mScaleCompensate;
    }
    const J3DTransformInfo& getTransformInfo(int no) const {
        return mJointInitData[mIndexTable[no]].mTransformInfo;
    }
    f32 getRadius(int no) const {
        return mJointInitData[mIndexTable[no]].mRadius;
    }
    Vec& getMin(int no) const {
        return mJointInitData[mIndexTable[no]].mMin;
    }
    Vec& getMax(int no) const {
        return mJointInitData[mIndexTable[no]].mMax;
    }
};
