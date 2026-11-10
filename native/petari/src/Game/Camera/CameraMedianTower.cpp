#include "Game/Camera/CameraMedianTower.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFovyPos_f1, "calcFovyPos.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcFovyPos_f2, "calcFovyPos.f2", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateReset_f1, "updateReset.f1", 140.0f);

namespace {
    NEBULA_TUNABLE(f32, sPartialRate, 0.5f);
    NEBULA_TUNABLE(f32, sAngularVel, 0.5f);
    NEBULA_TUNABLE(s32, sInterpolate, 120);
};  // namespace

CameraMedianTower::CameraMedianTower(const char* pName)
    : Camera(pName), mString(), mWPoint(0.0f, 0.0f, 0.0f), mAxis(0.0f, 1.0f, 0.0f), mAngleA(MR::toRadian(30.0f)), mDist(1200.0f), mMaxFocusDist(),
      mPartialRate(::sPartialRate), mBaseFront(0.0f, 0.0f, 1.0f), mIsResetting(true), mInterpolateTime(::sInterpolate) {
    lumacore::setClass(this, lumacore::kClassId< CameraMedianTower >);
}

void CameraMedianTower::reset() {
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setUpVecAndWatchUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
    mIsResetting = true;
}

CameraTargetObj* CameraMedianTower::calc() {
    CameraTargetObj* target = CameraLocalUtil::getTarget(this);

    Vec3f watchOffset;
    CameraLocalUtil::makeWatchOffset(&watchOffset, this, target, cNum_calc_f1 / cNum_calc_f2);

    Vec3f pos;
    pos.set(target->getPosition() * (1.0f - mPartialRate) + CameraLocalUtil::getVecReg(mString) * mPartialRate);

    Vec3f watchPos = mWPoint;
    mZoneMatrix.mult(watchPos, watchPos);

    Vec3f axis = mAxis;
    mZoneMatrix.mult33(axis, axis);

    pos.add(watchOffset);

    Pos3f towerMtx;
    if (!CameraLocalUtil::makeTowerCameraMtx(&towerMtx, mZoneMatrix, mWPoint, mAxis, pos)) {
        return nullptr;
    }

    Pos3f transMtx;
    transMtx.makeTrans(0.0f, 0.0f, mDist);

    Pos3f rotX;
    rotX.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), -mAngleA);

    Pos3f mtx;
    mtx.concat(rotX, transMtx);
    mtx.concat(towerMtx, mtx);

    Vec3f newPos;
    mtx.getTrans(newPos);
    Vec3f up;
    mtx.getYDir(up);

    limitAngularVelocity(&newPos, &up, pos, watchPos, axis);
    calcDistance(&newPos, pos);

    CameraLocalUtil::setWatchPos(this, pos);
    CameraLocalUtil::setPos(this, newPos);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);
    updateReset(newPos, watchPos, axis);

    return target;
}

void CameraMedianTower::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mString.getCharPtr(), general->mWPoint, general->mAxis, general->mAngleA, general->mUp.x, general->mUp.y, general->mUp.z);
}

void CameraMedianTower::calcFovyPos(Vec3f* pFovPos, const Vec3f& rFront, const Vec3f& rVec, const Vec3f& rPos) {
    Vec3f projZ = rFront * rVec.dot(rFront);
    Vec3f vecOrtho = rVec - projZ;

    f32 halfFov = MR::getFovy() * cNum_calcFovyPos_f1;
    Vec3f focusOffset = projZ - rFront * (vecOrtho.length() / MR::tanDegree(halfFov));

    pFovPos->set(rPos + focusOffset);
    pFovPos->add(rFront * (-cNum_calcFovyPos_f2 / MR::sinDegree(halfFov)));
}

void CameraMedianTower::limitAngularVelocity(Vec3f* pPos, Vec3f* pUp, const Vec3f& rPos, const Vec3f& rWatchPos, const Vec3f& rAxis) {
    Vec3f outDir = *pPos - rWatchPos;
    outDir.orthogonalize(rAxis);
    if (MR::isNearZero(outDir)) {
        outDir.set(mBaseFront);
    } else {
        MR::normalize(&outDir);
    }

    if (mIsResetting) {
        mBaseFront = outDir;
        mIsResetting = false;
        return;
    }

    f32 angle = outDir.angle(mBaseFront);
    f32 angleMax = MR::toRadian(::sAngularVel);
    f32 rate = 0.0f;
    if (angle > angleMax) {
        rate = 1.0f - (angleMax / angle);
    }

    Quat4f rot;
    rot.setRotate(outDir, mBaseFront, rate);

    Vec3f offset = *pPos - rPos;
    rot.transform(offset);
    *pPos = rPos + offset;

    rot.transform(*pUp);
    rot.transform(outDir);
    mBaseFront.set(outDir);
}

void CameraMedianTower::calcDistance(Vec3f* pPos, const Vec3f& rPos) {
    Vec3f dir = rPos - *pPos;
    MR::normalize(&dir);
    Vec3f regDiff = CameraLocalUtil::getVecReg(mString) - *pPos;
    Vec3f posDiff = CameraLocalUtil::getTarget(this)->getPosition() - *pPos;
    Vec3f fovPos;
    if (regDiff.squared() > posDiff.squared()) {
        calcFovyPos(&fovPos, dir, posDiff, *pPos);
    } else {
        calcFovyPos(&fovPos, dir, regDiff, *pPos);
    }

    Vec3f focusDiff;
    focusDiff.sub(fovPos, rPos);
    f32 fovDist = focusDiff.length();
    if (fovDist > mDist) {
        if (mDist <= mMaxFocusDist && fovDist > mMaxFocusDist) {
            focusDiff.setLength(mMaxFocusDist);
        }
        *pPos = rPos + focusDiff;
    }
}

void CameraMedianTower::updateReset(const Vec3f& rPos, const Vec3f& rWatchPos, const Vec3f& rAxis) {
    mInterpolateTime--;
    if (mInterpolateTime < 0) {
        mInterpolateTime = 0;
    }

    Vec3f outDir = rPos - rWatchPos;
    outDir.orthogonalize(rAxis);
    MR::normalizeOrZero(&outDir);

    Vec3f targetDir = CameraLocalUtil::getTarget(this)->getPosition() - rWatchPos;
    targetDir.orthogonalize(rAxis);
    MR::normalizeOrZero(&targetDir);

    if (outDir.dot(targetDir) < MR::cosDegree(cNum_updateReset_f1)) {
        if (mInterpolateTime == 0) {
            mInterpolateTime = ::sInterpolate;
            MR::startCameraInterpolation(::sInterpolate);
            reset();
        }
    }
}
