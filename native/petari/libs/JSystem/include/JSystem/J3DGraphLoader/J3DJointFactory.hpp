#pragma once

#include "JSystem/J3DGraphBase/J3DTransform.hpp"

class J3DJoint;
struct J3DJointBlock;

// In host order as the importer cooks it (j3d_model, JNT1).
struct J3DJointInitData {
    /* 0x00 */ u16 mKind;
    /* 0x02 */ bool mScaleCompensate;
    /* 0x04 */ J3DTransformInfo mTransformInfo;
    /* 0x24 */ f32 mRadius;
    /* 0x28 */ Vec mMin;
    /* 0x2C */ Vec mMax;
};  // Size: 0x40
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
