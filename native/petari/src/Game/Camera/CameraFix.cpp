#include "Game/Camera/CameraFix.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.98f);

CameraFix::CameraFix(const char* pName)
    : Camera(pName), mWPoint(0.0f, 0.0f, 0.0f), mAxis(0.0f, 0.0f, -2500.0f), mUp(0.0f, 1.0f, 0.0f), mTarget(new CameraTargetMtx("カメラターゲットダミー")) {
    lumacore::setClass(this, lumacore::kClassId< CameraFix >);
}

void CameraFix::reset() {
    calc();
}

CameraTargetObj* CameraFix::calc() {
    Vec3f axis = mAxis;
    Vec3f watchPos = mWPoint;
    Vec3f up = mUp;
    mZoneMatrix.mult33(axis);
    mZoneMatrix.mult(watchPos, watchPos);
    mZoneMatrix.mult33(up);

    CameraLocalUtil::setLocalOffset(this, Vec3f(0.0f, 0.0f, 0.0f));
    mTarget->setMtx(mZoneMatrix);
    mTarget->mMatrix.setTrans(watchPos);
    mTarget->movement();

    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPointImm(&watchPoint, this, mTarget);

    Vec3f front = axis;
    if (MR::isNearZero(front)) {
        return nullptr;
    }
    MR::normalize(&front);

    if (MR::abs(front.dot(up)) > cNum_calc_f1) {
        up.set< f32 >(front.y, front.z, front.x);
    }

    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setWatchUpVec(this, up);
    CameraLocalUtil::setPos(this, watchPos - axis);
    CameraLocalUtil::setUpVec(this, up);
    return mTarget;
}

void CameraFix::setParam(const Vec3f& rWPoint, const Vec3f& rAxis, const Vec3f& rUp) {
    mWPoint.set(rWPoint);
    mAxis.set(rAxis);
    mUp.set(rUp);
}

void CameraFix::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    Vec3f offset;
    MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), &offset, general->mDist, general->mAxis.x, general->mAxis.y);
    setParam(general->mWPoint, -offset, general->mUp);
}
