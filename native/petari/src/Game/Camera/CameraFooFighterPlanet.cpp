#include "Game/Camera/CameraFooFighterPlanet.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/VectorUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_reset_f1, "reset.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_reset_f2, "reset.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f1, "calc.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f2, "calc.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calc_f3, "calc.f3", 0.05f);

namespace {
    NEBULA_TUNABLE(f32, sRateWhenBraking, 0.05f);
};  // namespace

CameraFooFighterPlanet::CameraFooFighterPlanet(const char* pName) : Camera(pName), mDistMin(500.0f), mDistMax(2000.0f), mPitchMax(MR::pi() / 4.0f) {
    lumacore::setClass(this, lumacore::kClassId< CameraFooFighterPlanet >);
}

void CameraFooFighterPlanet::reset() {
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

    Vec3f newFront = watchPoint - pos;
    if (MR::isNearZero(newFront)) {
        return;
    }
    MR::normalize(&newFront);

    Quat4f upRot;
    upRot.setRotate(front, newFront);
    Quat4f rollRot;
    rollRot.setRotate(front, CameraLocalUtil::getRoll(mCameraMan));

    rollRot.transform(up);
    upRot.transform(up);

    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
}

CameraTargetObj* CameraFooFighterPlanet::calc() {
    Vec3f watchPos = CameraLocalUtil::getWatchPos(this);
    Vec3f pos = CameraLocalUtil::getPos(this);
    Vec3f up = CameraLocalUtil::getUpVec(this);

    Vec3f watchPoint;
    CameraLocalUtil::makeWatchPoint(&watchPoint, this, CameraLocalUtil::getTarget(this), cNum_calc_f1 / cNum_calc_f2);

    Vec3f front = watchPos - pos;
    if (MR::isNearZero(front)) {
        return CameraLocalUtil::getTarget(this);
    }
    MR::normalize(&front);

    Vec3f planeFront = CameraLocalUtil::getTarget(this)->getSideVec().cross(-CameraLocalUtil::getTarget(this)->getGravityVector());
    if (MR::isNearZero(planeFront)) {
        return CameraLocalUtil::getTarget(this);
    }
    MR::normalize(&planeFront);

    Pos3f targetFrame;
    targetFrame.setXDir(CameraLocalUtil::getTarget(this)->getSideVec());
    targetFrame.setYDir(-CameraLocalUtil::getTarget(this)->getGravityVector());
    targetFrame.setZDir(planeFront);
    targetFrame.setTrans(watchPoint);

    Pos3f inv = targetFrame;
    inv.invert(inv);
    inv.mult(pos, pos);

    f32 length, angleX, angleY;
    MR::crossToPolar(Vec3f(0.0f, 0.0f, 0.0f), pos, &length, &angleX, &angleY);
    if (angleX < mPitchMax) {
        angleX += (mPitchMax - angleX) * cNum_calc_f3;
    }
    MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), &pos, length, angleX, angleY);
    targetFrame.mult(pos, pos);

    Vec3f newFront = watchPoint - pos;
    if (MR::isNearZero(newFront)) {
        return CameraLocalUtil::getTarget(this);
    }
    MR::normalize(&newFront);

    Quat4f upRot;
    upRot.setRotate(front, newFront);
    upRot.transform(up);

    Vec3f camOffs = pos - watchPoint;
    f32 dist = camOffs.length();
    if (dist < mDistMin) {
        camOffs.setLength(mDistMin);
    } else if (dist > mDistMax) {
        camOffs.setLength(mDistMax);
    }
    pos.set(watchPoint + camOffs);

    if (MR::isPlayerNeedBrakingCamera()) {
        goRoundBehind(pos, watchPoint, up);
    }

    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setWatchPos(this, watchPoint);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
    return CameraLocalUtil::getTarget(this);
}

void CameraFooFighterPlanet::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mAxis.x, general->mAxis.y, general->mAngleA);
}

void CameraFooFighterPlanet::goRoundBehind(Vec3f& rPos, Vec3f& rWatchPos, Vec3f& rUp) {
    Vec3f front = rWatchPos - rPos;
    f32 dist = front.length();
    if (MR::isNearZero(front)) {
        return;
    }
    MR::normalize(&front);

    rUp.orthogonalize(front);
    if (MR::isNearZero(rUp)) {
        return;
    }
    MR::normalize(&rUp);

    Vec3f side = rUp.cross(front);

    Pos3f rotMtx;
    rotMtx.identity();
    rotMtx.setXDir(side);
    rotMtx.setYDir(rUp);
    rotMtx.setZDir(front);

    Quat4f rot1;
    rotMtx.getQuat(rot1);

    Pos3f targetMtx;
    targetMtx.identity();
    targetMtx.setXDir(CameraLocalUtil::getTarget(this)->getSideVec());
    targetMtx.setYDir(-CameraLocalUtil::getTarget(this)->getFrontVec());
    targetMtx.setZDir(CameraLocalUtil::getTarget(this)->getUpVec());

    Quat4f targetRot;
    targetMtx.getQuat(targetRot);

    Quat4f rot;
    rot.slerp(rot1, targetRot, ::sRateWhenBraking);
    rotMtx.makeQuat(rot);
    rotMtx.getZDir(front);
    rotMtx.getYDir(rUp);
    rPos = rWatchPos - front * dist;
}
