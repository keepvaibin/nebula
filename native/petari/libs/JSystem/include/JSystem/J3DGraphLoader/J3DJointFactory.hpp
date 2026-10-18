#pragma once

#include "JSystem/J3DGraphBase/J3DTransform.hpp"

class J3DJoint;
struct J3DJointBlock;

struct J3DJointInitData {
    /* 0x00 */ NEBULA_BE(u16) mKind;
    /* 0x02 */ bool mScaleCompensate;
    /* 0x04 */ NEBULA_DISC(J3DTransformInfo) mTransformInfo;
    /* 0x24 */ NEBULA_BE(f32) mRadius;
    /* 0x28 */ NEBULA_DISC(Vec) mMin;
    /* 0x2C */ NEBULA_DISC(Vec) mMax;
};  // Size: 0x30

struct J3DJointFactory {
    J3DJointFactory(J3DJointBlock const&);
    J3DJoint* create(int);

    J3DJointInitData* mJointInitData;
    NEBULA_BE(u16)* mIndexTable;

    u16 getKind(int no) const {
        return mJointInitData[mIndexTable[no]].mKind;
    }
    u8 getScaleCompensate(int no) const {
        return mJointInitData[mIndexTable[no]].mScaleCompensate;
    }
    const NEBULA_DISC(J3DTransformInfo)& getTransformInfo(int no) const {
        return mJointInitData[mIndexTable[no]].mTransformInfo;
    }
    f32 getRadius(int no) const {
        return mJointInitData[mIndexTable[no]].mRadius;
    }
    NEBULA_DISC(Vec)& getMin(int no) const {
        return mJointInitData[mIndexTable[no]].mMin;
    }
    NEBULA_DISC(Vec)& getMax(int no) const {
        return mJointInitData[mIndexTable[no]].mMax;
    }
};
