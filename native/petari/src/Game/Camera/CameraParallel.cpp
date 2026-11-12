#include "Game/Camera/CameraParallel.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraHeightArrange.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sDistance = 3000.0f;
    const f32 sRoundDiv = 8.0f;
    const f32 sRoundAddition = 0.08f;
};  // namespace

CameraParallel::CameraParallel(const char* pName)
    : Camera(pName), mAngleX(), mAngleY(), mDist(::sDistance), mIsEnableValid(), mIsRounding(), mRoundTarget(), mRoundAngle() {
    lumacore::setClass(this, lumacore::kClassId< CameraParallel >);
    createVPanObj();
}

void CameraParallel::reset() {
    mVPan->resetJump();
    mVPan->calcWatchPos(nullptr);
    calcIdealPose();
    mVPan->reset();
    mIsRounding = false;
    mRoundTarget = 0.0f;
    mRoundAngle = 0.0f;
}

CameraTargetObj* CameraParallel::calc() {
    mVPan->updateJump();
    mVPan->calcWatchPos(nullptr);
    calcIdealPose();
    mVPan->arrange();

    return CameraLocalUtil::getTarget(this);
}

void CameraParallel::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(Vec2f(180.0f * general->mAngleB / MR::pi(), 180.0f * general->mAngleA / MR::pi()), general->mDist, general->mNum1 == 1);
}

bool CameraParallel::isEnableToRoundLeft() const {
    return mIsEnableValid;
}

bool CameraParallel::isEnableToRoundRight() const {
    return mIsEnableValid;
}

bool CameraParallel::isEnableToReset() const {
    return mIsEnableValid;
}

void CameraParallel::calcIdealPose() {
    if (mDist < 300.0f) {
        mDist = 300.0f;
    }

    Vec3f watchPos = CameraLocalUtil::getWatchPos(this);

    Vec3f pos;
    MR::polarToCrossDegree(Vec3f(0.0f, 0.0f, 0.0f), &pos, mDist, mAngleX, mAngleY - 90.0f);

    Vec3f up;
    up.set< f32 >(0.0f, 1.0f, 0.0f);
    calcRound();

    f32 length, angleX, angleY;
    MR::crossToPolar(Vec3f(0.0f, 0.0f, 0.0f), pos, &length, &angleX, &angleY);
    angleY += mRoundAngle;
    MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), &pos, length, angleX, angleY);

    mZoneMatrix.mult33(pos);
    mZoneMatrix.mult33(up);

    pos.add(watchPos);
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setWatchUpVec(this, up);
}

void CameraParallel::calcRound() {
    if (!mIsEnableValid) {
        return;
    }

    if (mIsRounding) {
        mIsRounding = CameraLocalUtil::approachRoundAngle(&mRoundAngle, mRoundTarget, ::sRoundAddition);
    } else if (CameraLocalUtil::testCameraPadTriggerReset()) {
        mRoundTarget = 0.0f;
        mIsRounding = true;
    } else if (CameraLocalUtil::testCameraPadTriggerRoundLeft()) {
        mRoundTarget = CameraLocalUtil::calcRoundTarget(&mRoundAngle, false, ::sRoundDiv);
        mIsRounding = true;
    } else if (CameraLocalUtil::testCameraPadTriggerRoundRight()) {
        mRoundTarget = CameraLocalUtil::calcRoundTarget(&mRoundAngle, true, ::sRoundDiv);
        mIsRounding = true;
    }
}
