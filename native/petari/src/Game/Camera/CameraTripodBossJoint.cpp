#include "Game/Camera/CameraTripodBossJoint.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sDistance = 300.0f;
    const f32 sAttenuation = 0.7f;
    const f32 sRoundDiv = 8.0f;
    const f32 sRoundAddition = 0.08f;
};  // namespace

CameraTripodBossJoint::CameraTripodBossJoint(const char* pName)
    : Camera(pName), mAngleX(), mAngleY(), mDist(3000.0f), mIsRounding(), mRoundTarget(), mRoundAngle() {
    lumacore::setClass(this, lumacore::kClassId< CameraTripodBossJoint >);
}

void CameraTripodBossJoint::reset() {
    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);
    CameraLocalUtil::setWatchPos(this, watchPos);
    calcIdealPose();
    mIsRounding = false;
    mRoundTarget = 0.0f;
    mRoundAngle = 0.0f;
}

CameraTargetObj* CameraTripodBossJoint::calc() {
    calcIdealPose();
    return CameraLocalUtil::getTarget(this);
}

void CameraTripodBossJoint::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(Vec2f(180.0f * general->mAngleB / MR::pi(), 180.0f * general->mAngleA / MR::pi()), general->mDist, general->mNum1, general->mAxis);
}

void CameraTripodBossJoint::calcIdealPose() {
    if (mDist < ::sDistance) {
        mDist = ::sDistance;
    }

    if (mIsRounding) {
        mIsRounding = CameraLocalUtil::approachRoundAngle(&mRoundAngle, mRoundTarget, ::sRoundAddition);
    } else if (CameraLocalUtil::tryCameraReset()) {
        mRoundAngle *= ::sAttenuation;
        mRoundTarget = mRoundAngle;
    } else if (CameraLocalUtil::testCameraPadTriggerRoundLeft()) {
        mRoundTarget = CameraLocalUtil::calcRoundTarget(&mRoundAngle, false, ::sRoundDiv);
        mIsRounding = true;
    } else if (CameraLocalUtil::testCameraPadTriggerRoundRight()) {
        mRoundTarget = CameraLocalUtil::calcRoundTarget(&mRoundAngle, true, ::sRoundDiv);
        mIsRounding = true;
    }

    Vec3f pos;
    MR::polarToCrossDegree(Vec3f(0.0f, 0.0f, 0.0f), &pos, mDist, mAngleX, (mAngleY - 90.0f) + mRoundAngle);

    Vec3f up;
    if (MR::isCreatedTripodBoss()) {
        Pos3f mtx;
        MR::getTripodBossJointMatrix(&mtx, mJointID);
        mtx.mult33(pos, pos);
        mtx.getYDir(up);
    } else {
        up.set< f32 >(0.0f, 1.0f, 0.0f);
    }

    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);

    if (MR::isCreatedTripodBoss()) {
        Pos3f mtx;
        MR::getTripodBossJointMatrix(&mtx, mJointID);
        Vec3f offset;
        mtx.mult33(mAxis, offset);
        watchPoint.add(offset);
    }

    pos.add(watchPoint);
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setWatchUpVec(this, up);
}
