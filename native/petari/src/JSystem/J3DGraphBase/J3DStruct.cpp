#include "JSystem/J3DGraphBase/J3DStruct.hpp"
#include "JSystem/JMath/JMath.hpp"

namespace {
    extern const f32 kIdentityW;
    extern const f32 kIdentityZero;
};  // namespace

void J3DTexMtxInfo::operator=(J3DTexMtxInfo const& param_0) {
    mProjection = param_0.mProjection;
    mInfo = param_0.mInfo;
    JMath::copyFloats3(&mCenter, &param_0.mCenter);
    mSRT = param_0.mSRT;
    JMath::copyFloats16(&mEffectMtx, &param_0.mEffectMtx);
}

void J3DTexMtxInfo::setEffectMtx(Mtx param_0) {
    JMath::copyFloats12(&mEffectMtx, param_0);
    mEffectMtx[3][0] = kIdentityZero;
    mEffectMtx[3][1] = kIdentityZero;
    mEffectMtx[3][2] = kIdentityZero;
    mEffectMtx[3][3] = kIdentityW;
}

J3DIndTexMtxInfo& J3DIndTexMtxInfo::operator=(J3DIndTexMtxInfo const& param_0) {
    JMath::copyFloats6(field_0x0, param_0.field_0x0);
    field_0x18 = param_0.field_0x18;
    return *this;
}

J3DFogInfo& J3DFogInfo::operator=(const J3DFogInfo& param_0) {
    mType = param_0.mType;
    mAdjEnable = param_0.mAdjEnable;
    mCenter = param_0.mCenter;
    mStartZ = param_0.mStartZ;
    mEndZ = param_0.mEndZ;
    mNearZ = param_0.mNearZ;
    mFarZ = param_0.mFarZ;
    mColor.r = param_0.mColor.r;
    mColor.g = param_0.mColor.g;
    mColor.b = param_0.mColor.b;
    mColor.a = param_0.mColor.a;
    for (int i = 0; i < 10; i++) {
        mFogAdjTable.r[i] = param_0.mFogAdjTable.r[i];
    }
    return *this;
}

J3DNBTScaleInfo& J3DNBTScaleInfo::operator=(const J3DNBTScaleInfo& other) {
    mbHasScale = other.mbHasScale;
    mScale.x = other.mScale.x;
    mScale.y = other.mScale.y;
    mScale.z = other.mScale.z;
    return *this;
}

namespace {
    const f32 kIdentityW = 1.0f;
    const f32 kIdentityZero = 0.0f;
}  // namespace
