#include "Game/Camera/CameraMedianPlanet.hpp"
#include "Game/Camera/CamTranslatorMedianPlanet.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

namespace {
    static const f32 sAngleConstrainRate = 0.2f;
    static const f32 sPartialRate = 0.5f;
};  // namespace

CameraMedianPlanet::~CameraMedianPlanet() {
}

CameraMedianPlanet::CameraMedianPlanet(const char* pName)
    : Camera(pName), mString(), mDistMin(1200.0f), mDistMax(3000.0f), mAngleMin(MR::pi() / 4.0f), mPeepAngle(), mDist(2000.0f),
      mAngleConstrainRate(::sAngleConstrainRate), mPartialRate(::sPartialRate) {
}

void CameraMedianPlanet::reset() {
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setUpVec(this, CameraLocalUtil::getUpVec(mCameraMan));
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
}

CameraTargetObj* CameraMedianPlanet::calc() {
    Vec3f pos;
    MR::getCameraInvViewMtx().getTrans(pos);
    constrainDipAngle(&pos);
    constrainLength(&pos);
    Vec3f watchPos;
    calcWatchPos(&watchPos);
    Vec3f up;
    if (!calcUpVec(&up, watchPos, pos)) {
        return CameraLocalUtil::getTarget(this);
    }

    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
    return CameraLocalUtil::getTarget(this);
}

CamTranslatorBase* CameraMedianPlanet::createTranslator() {
    return new CamTranslatorMedianPlanet(this);
}

void CameraMedianPlanet::getPrevFront(Vec3f* pPrevFront) {
    MR::getCameraInvViewMtx().getZDir(*pPrevFront);
    pPrevFront->negate();
}

void CameraMedianPlanet::constrainDipAngle(Vec3f* pPos) {
    Pos3f targetMtx;
    makeTargetMtx(&targetMtx);
    peepMtx(&targetMtx);

    Pos3f inv = targetMtx;
    inv.invert(inv);
    inv.mult(*pPos, *pPos);

    f32 length, angleX, angleY;
    MR::crossToPolar(Vec3f(0.0f, 0.0f, 0.0f), *pPos, &length, &angleX, &angleY);

    if (angleX < mAngleMin) {
        angleX += (mAngleMin - angleX) * (CameraLocalUtil::isForceCameraChange() ? 1.0f : mAngleConstrainRate);
        MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), pPos, length, angleX, angleY);
    }
    targetMtx.mult(*pPos, *pPos);
}

void CameraMedianPlanet::makeTargetMtx(Pos3f* pMtx) {
    pMtx->setXDir(CameraLocalUtil::getTarget(this)->getSideVec());
    pMtx->setYDir(-CameraLocalUtil::getTarget(this)->getGravityVector());
    pMtx->setZDir(CameraLocalUtil::getTarget(this)->getFrontVec());
    pMtx->setTrans(CameraLocalUtil::getTarget(this)->getPosition());
}

void CameraMedianPlanet::peepMtx(Pos3f* pMtx) {
    Vec3f up;
    pMtx->getYDir(up);

    Vec3f front = CameraLocalUtil::getTarget(this)->getPosition() - CameraLocalUtil::getVecReg(mString);
    if (MR::isNearZero(front)) {
        return;
    }

    f32 dist = front.length();
    if (dist >= mDist) {
        return;
    }

    Vec3f side = up.cross(front);
    if (MR::isNearZero(side)) {
        return;
    }
    MR::normalize(&side);

    rotate33(pMtx, side, mPeepAngle * (mDist - dist) / mDist);
}

void CameraMedianPlanet::rotate33(Pos3f* pMtx, const Vec3f& axis, f32 angle) {
    Vec3f trans;
    pMtx->getTrans(trans);
    pMtx->setTrans(0.0f, 0.0f, 0.0f);
    Pos3f rot;
    rot.makeRotate(axis, angle);
    pMtx->concat(rot, *pMtx);
    pMtx->setTrans(trans);
}

void CameraMedianPlanet::constrainLength(Vec3f* pPos) {
    f32 rate = getMaxRate();
    Vec3f offset = *pPos - CameraLocalUtil::getTarget(this)->getPosition();
    offset.setLength(mDistMin * (1.0f - rate) + mDistMax * rate);
    pPos->set(CameraLocalUtil::getTarget(this)->getPosition() + offset);
}

f32 CameraMedianPlanet::getMaxRate() {
    Vec3f up;
    getRegisterUpVec(&up);
    f32 rate = up.angle(CameraLocalUtil::getTarget(this)->getUpVec()) / MR::pi();
    if (rate > 1.0f) {
        rate = 1.0f;
    }
    if (rate < 0.0f) {
        rate = 0.0f;
    }
    return rate;
}

void CameraMedianPlanet::getRegisterUpVec(Vec3f* pUp) {
    MR::calcGravityVector(this, CameraLocalUtil::getVecReg(mString), pUp, nullptr, 0);
    pUp->negate();
    if (MR::isNearZero(*pUp)) {
        pUp->set(CameraLocalUtil::getTarget(this)->getUpVec());
    }
}

void CameraMedianPlanet::calcWatchPos(Vec3f* pWatchPos) {
    Vec3f watchOffset;
    CameraLocalUtil::makeWatchPoint(&watchOffset, this, CameraLocalUtil::getTarget(this), 0.1f / 15.0f);
    watchOffset.sub(CameraLocalUtil::getTarget(this)->getPosition());

    pWatchPos->set(CameraLocalUtil::getTarget(this)->getPosition() * (1.0f - mPartialRate) + CameraLocalUtil::getVecReg(mString) * mPartialRate +
                   watchOffset);
}

bool CameraMedianPlanet::calcUpVec(Vec3f* pUp, const Vec3f& watchPos, const Vec3f& pos) {
    MR::getCameraInvViewMtx().getYDir(*pUp);
    Vec3f prevFront;
    getPrevFront(&prevFront);

    Vec3f front = watchPos - pos;
    if (MR::isNearZero(front)) {
        return false;
    }
    MR::normalize(&front);

    Quat4f rot;
    rot.setRotate(prevFront, front);
    rot.transform(*pUp);

    return true;
}
