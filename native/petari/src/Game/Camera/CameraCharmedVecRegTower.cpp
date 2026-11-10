#include "Game/Camera/CameraCharmedVecRegTower.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f3, "calc.f3", 200.0f);

CameraCharmedVecRegTower::CameraCharmedVecRegTower(const char* pName)
    : Camera(pName), mWPoint(0.0f, 0.0f, 0.0f), mAxis(0.0f, 0.0f, 0.0f), mAngleX(), mAngleYMax(), mDist(), mArrange(), mName(nullptr) {
}

void CameraCharmedVecRegTower::reset() {
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setUpVecAndWatchUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
}

CameraTargetObj* CameraCharmedVecRegTower::calc() {
    CameraTargetObj* target = CameraLocalUtil::getTarget(this);

    Vec3f watchOffset;
    CameraLocalUtil::makeWatchOffset(&watchOffset, this, target, cNum_calc_f1 / cNum_calc_f2);

    Vec3f wPoint = mWPoint;
    Vec3f axis = mAxis;

    Pos3f zoneMtx = mZoneMatrix;
    zoneMtx.mult(wPoint, wPoint);
    zoneMtx.mult33(axis, axis);

    Vec3f watchDir = target->getPosition() - wPoint;
    if (MR::isNearZero(watchDir)) {
        return nullptr;
    }
    MR::normalize(&watchDir);

    Vec3f front = watchDir.killElement(axis);
    if (MR::isNearZero(front)) {
        return nullptr;
    }
    MR::normalize(&front);

    Vec3f side = axis.cross(front);

    Pos3f mtx;
    mtx.setXYZDir(side, axis, front);
    mtx.setTrans(target->getPosition());

    Vec3f posOffs(0.0f, 0.0f, mDist);
    mtx.mult(posOffs, posOffs);

    Vec3f regDir = CameraLocalUtil::getVecReg(mName) - posOffs;
    regDir.orthogonalize(axis);
    MR::normalizeOrZero(&regDir);

    f32 angle = regDir.angle(-front);
    if (angle > mAngleYMax) {
        angle = mAngleYMax;
    }

    if (regDir.orientation(axis, -front)) {
        angle = -angle;
    }

    Pos3f transMtx;
    transMtx.makeTrans(0.0f, 0.0f, mDist);

    Pos3f rotX;
    rotX.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), -mAngleX);

    Pos3f rotY;
    rotY.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), angle);

    Pos3f camMtx;
    camMtx.concat(rotX, transMtx);
    camMtx.concat(rotY, camMtx);
    camMtx.concat(mtx, camMtx);

    Vec3f pos;
    camMtx.getTrans(pos);

    CameraLocalUtil::setWatchPos(this, target->getPosition() + watchOffset);
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, axis);

    if (mArrange) {
        CameraLocalUtil::arrangeDistanceByFovy(this, CameraLocalUtil::getVecReg(mName), cNum_calc_f3);
    }

    return target;
}

void CameraCharmedVecRegTower::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    Vec3f axis = general->mAxis;
    MR::normalizeOrZero(&axis);
    if (MR::isNearZero(axis)) {
        axis.set< f32 >(0.0f, 1.0f, 0.0f);
    }
    setParam(general->mString.getCharPtr(), general->mWPoint, axis, general->mAngleA, general->mAngleB, general->mDist, general->mNum1 != 0);
}
