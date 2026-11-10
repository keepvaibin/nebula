#include "Game/Camera/CameraWaterPlanet.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_reset_f1, "reset.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_reset_f2, "reset.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f3, "calc.f3", 0.05f);

CameraWaterPlanet::CameraWaterPlanet(const char* pName) : Camera(pName), mDistMin(500.0f), mDistMax(2000.0f), mAngleX(MR::pi() / 4.0f) {
}

void CameraWaterPlanet::reset() {
    Vec3f watchPos = CameraLocalUtil::getWatchPos(mCameraMan);
    Vec3f pos = CameraLocalUtil::getPos(mCameraMan);
    Vec3f up = CameraLocalUtil::getUpVec(mCameraMan);

    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), cNum_reset_f1 / cNum_reset_f2);

    Vec3f front = watchPos - pos;
    if (MR::isNearZero(front)) {
        return;
    }
    MR::normalize(&front);

    Vec3f watchFront = watchPoint - pos;
    if (MR::isNearZero(watchFront)) {
        return;
    }
    MR::normalize(&watchFront);

    Quat4f frontRot;
    frontRot.setRotate(front, watchFront);
    Quat4f rotRoll;
    rotRoll.setRotate(front, CameraLocalUtil::getRoll(mCameraMan));
    rotRoll.transform(up);
    frontRot.transform(up);

    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
}

CameraTargetObj* CameraWaterPlanet::calc() {
    Vec3f currWatchPos = CameraLocalUtil::getWatchPos(this);
    Vec3f pos = CameraLocalUtil::getPos(this);
    Vec3f up = CameraLocalUtil::getUpVec(this);

    CameraTargetObj* target = CameraLocalUtil::getTarget(this);
    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, target, cNum_calc_f1 / cNum_calc_f2);

    Vec3f front = currWatchPos - pos;
    if (MR::isNearZero(front)) {
        return target;
    }
    MR::normalize(&front);

    Vec3f targetFront = target->getSideVec().cross(-target->getGravityVector());
    if (MR::isNearZero(targetFront)) {
        return target;
    }
    MR::normalize(&targetFront);

    Pos3f mtx;
    mtx.setXDir(target->getSideVec());
    mtx.setYDir(-target->getGravityVector());
    mtx.setZDir(targetFront);
    mtx.setTrans(watchPos);

    Pos3f inv = mtx;
    inv.invert(inv);
    inv.mult(pos, pos);

    f32 length, angleX, angleY;
    MR::crossToPolar(Vec3f(0.0f, 0.0f, 0.0f), pos, &length, &angleX, &angleY);

    if (angleX < mAngleX) {
        angleX += (mAngleX - angleX) * cNum_calc_f3;
        MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), &pos, length, angleX, angleY);
    }

    mtx.mult(pos, pos);

    Vec3f watchFront = watchPos - pos;
    if (MR::isNearZero(watchFront)) {
        return target;
    }
    MR::normalize(&watchFront);

    Quat4f frontRot;
    frontRot.setRotate(front, watchFront);
    frontRot.transform(up);

    Vec3f offset = pos - watchPos;
    f32 dist = offset.length();

    if (dist < mDistMin) {
        offset.setLength(mDistMin);
    } else if (dist > mDistMax) {
        offset.setLength(mDistMax);
    }

    pos.set(watchPos + offset);
    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, target->getUpVec());

    return target;
}

void CameraWaterPlanet::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mAxis.x, general->mAxis.y, general->mAngleA);
}
