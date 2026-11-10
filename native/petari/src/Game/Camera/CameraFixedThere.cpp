#include "Game/Camera/CameraFixedThere.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/VectorUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);

namespace {
    NEBULA_TUNABLE(f32, sGroundFixAngle, 30.0f);
};  // namespace

CameraFixedThere::CameraFixedThere(const char* pName)
    : Camera(pName), mCameraType(), mIsFovyFixed(), mUp(0.0f, 1.0f, 0.0f), mAxis(0.0f, 0.0f, 1.0f), mRoll() {
}

void CameraFixedThere::reset() {
    copyStatusFromPrevCamera();
    makeAxisAndRoll();
    CameraLocalUtil::setUpVec(this, mUp);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
}

CameraTargetObj* CameraFixedThere::calc() {
    Vec3f eyeDir;
    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, CameraLocalUtil::getTarget(this), cNum_calc_f1 / cNum_calc_f2);
    CameraLocalUtil::setWatchPos(this, watchPos);

    if (!calcEyeDir(&eyeDir)) {
        return CameraLocalUtil::getTarget(this);
    }

    updateUpVec(eyeDir);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
    if (!mIsFovyFixed) {
        CameraLocalUtil::setFovy(this, CameraLocalUtil::getFovy(mCameraMan));
    }
    return CameraLocalUtil::getTarget(this);
}

void CameraFixedThere::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mNum1, general->mNum2 == 1);
}

void CameraFixedThere::copyStatusFromPrevCamera() {
    const Pos3f& mtx = mCameraMan->mMatrix;

    Vec3f zoneFront;
    Vec3f pos;
    mtx.getZDir(zoneFront);
    zoneFront.negate();

    mtx.getTrans(pos);
    CameraLocalUtil::setPos(this, pos);

    Vec3f front = CameraLocalUtil::getWatchPos(mCameraMan) - pos;
    if (MR::isNearZero(front)) {
        front = zoneFront;
    } else {
        MR::normalize(&front);
    }
    mAxis.set(front);

    Quat4f rot;
    rot.setRotate(zoneFront, front);
    Vec3f up;
    mtx.getYDir(up);
    rot.transform(up);
    CameraLocalUtil::setUpVec(this, up);
    if (!mIsFovyFixed) {
        CameraLocalUtil::setFovy(this, CameraLocalUtil::getFovy(mCameraMan));
    }
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
}

bool CameraFixedThere::calcEyeDir(Vec3f* pDir) {
    pDir->set(CameraLocalUtil::getWatchPos(this) - CameraLocalUtil::getPos(this));
    if (MR::isNearZero(*pDir)) {
        return false;
    }
    MR::normalize(pDir);
    return true;
}

void CameraFixedThere::makeAxisAndRoll() {
    mRoll = 0.0f;
    if (mCameraType != CameraType_GravityUp) {
        mUp = CameraLocalUtil::getUpVec(this);
        return;
    }

    mUp = -CameraLocalUtil::getTarget(this)->getGravityVector();
    if (MR::isNearZero(mUp)) {
        mUp = CameraLocalUtil::getUpVec(this);
        return;
    }

    if (MR::abs(mAxis.dot(mUp)) > MR::cosDegree(::sGroundFixAngle)) {
        mUp = CameraLocalUtil::getUpVec(this);
        return;
    }

    if (!MR::diffRadianAroundAxis(&mRoll, mAxis, mUp, CameraLocalUtil::getUpVec(this))) {
        mUp = CameraLocalUtil::getUpVec(this);
        return;
    }
}

void CameraFixedThere::updateUpVec(const Vec3f& rAxis) {
    switch (mCameraType) {
    case CameraType_GravityUp: {
        updateNormalUpVec(rAxis);
        break;
    }
    default: {
        Quat4f rot;
        rot.setRotate(mAxis, rAxis);
        rot.transform(mUp);
        CameraLocalUtil::setUpVec(this, mUp);
    }
    }
    mAxis.set(rAxis);
}

void CameraFixedThere::updateNormalUpVec(const Vec3f& rFront) {
    Vec3f up = mUp;
    Vec3f side = up.cross(rFront);
    up.cross(mAxis, side);

    MR::normalize(&up);

    Pos3f rot;
    rot.makeRotate(rFront, -mRoll);
    rot.mult33(up, up);
    CameraLocalUtil::setUpVec(this, up);
}
