#include "Game/Camera/CameraParallel.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraHeightArrange.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setParamFromChunk_f1, "setParamFromChunk.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setParamFromChunk_f2, "setParamFromChunk.f2", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f1, "calcIdealPose.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f2, "calcIdealPose.f2", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcIdealPose_f3, "calcIdealPose.f3", 90.0f);

namespace {
    NEBULA_TUNABLE(f32, sDistance, 3000.0f);
    NEBULA_TUNABLE(f32, sRoundDiv, 8.0f);
    NEBULA_TUNABLE(f32, sRoundAddition, 0.08f);
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
    setParam(Vec2f(cNum_setParamFromChunk_f1 * general->mAngleB / MR::pi(), cNum_setParamFromChunk_f2 * general->mAngleA / MR::pi()), general->mDist, general->mNum1 == 1);
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
    if (mDist < cNum_calcIdealPose_f1) {
        mDist = cNum_calcIdealPose_f2;
    }

    Vec3f watchPos = CameraLocalUtil::getWatchPos(this);

    Vec3f pos;
    MR::polarToCrossDegree(Vec3f(0.0f, 0.0f, 0.0f), &pos, mDist, mAngleX, mAngleY - cNum_calcIdealPose_f3);

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
