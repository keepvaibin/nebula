#include "Game/Camera/CameraWonderPlanet.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraMan.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(s32, cCameraResetFrame, 30);
    const f32 cWatchPointRate = 0.1f / 15.0f;
    // The camera eases up toward mAngleX by this much of the gap each frame.
    NEBULA_TUNABLE(f32, cAngleXChaseRate, 0.05f);
}  // namespace

CameraWonderPlanet::CameraWonderPlanet(const char* pName)
    : Camera(pName), mDistMin(500.0f), mDistMax(2000.0f), mAngleX(MR::pi() / 4.0f), mIsRotating(), mRotateFrame() {
    lumacore::setClass(this, lumacore::kClassId< CameraWonderPlanet >);
}

void CameraWonderPlanet::reset() {
    Vec3f watchPosCameraMan = CameraLocalUtil::getWatchPos(mCameraMan);
    Vec3f pos = CameraLocalUtil::getPos(mCameraMan);
    Vec3f up;
    mCameraMan->mMatrix.getYDir(up);

    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, CameraLocalUtil::getTarget(this), ::cWatchPointRate);

    Vec3f front = watchPosCameraMan - pos;
    MR::normalize(&front);
    Vec3f watchFront = watchPos - pos;
    MR::normalize(&watchFront);

    Quat4f frontRot;
    frontRot.setRotate(front, watchFront);

    CameraLocalUtil::setPos(this, pos);
    CameraLocalUtil::setWatchPos(this, watchPos);
    CameraLocalUtil::setUpVec(this, up);
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());

    mIsRotating = false;
    mRotateFrame = 0;
}

// Watches the target in its own frame: never lower than mAngleX, distance kept in [mDistMin, mDistMax].
CameraTargetObj* CameraWonderPlanet::calc() {
    Vec3f currWatchPos = CameraLocalUtil::getWatchPos(this);
    Vec3f pos = CameraLocalUtil::getPos(this);
    Vec3f up = CameraLocalUtil::getUpVec(this);

    Vec3f watchPos;
    CameraLocalUtil::makeWatchPoint(&watchPos, this, CameraLocalUtil::getTarget(this), ::cWatchPointRate);

    Vec3f front = currWatchPos - pos;
    MR::normalize(&front);

    Pos3f mtx;
    mtx.setXDir(CameraLocalUtil::getTarget(this)->getSideVec());
    mtx.setYDir(-CameraLocalUtil::getTarget(this)->getGravityVector());
    mtx.setZDir(CameraLocalUtil::getTarget(this)->getFrontVec());
    mtx.setTrans(watchPos);

    Pos3f inv = mtx;
    inv.invert(inv);
    inv.mult(pos, pos);

    f32 length, angleX, angleY;
    MR::crossToPolar(Vec3f(0.0f, 0.0f, 0.0f), pos, &length, &angleX, &angleY);

    if (angleX < mAngleX) {
        angleX += (mAngleX - angleX) * ::cAngleXChaseRate;
        MR::polarToCross(Vec3f(0.0f, 0.0f, 0.0f), &pos, length, angleX, angleY);
    }

    mtx.mult(pos, pos);

    Vec3f watchFront = watchPos - pos;
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
    CameraLocalUtil::setWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());

    updateCameraReset();

    return CameraLocalUtil::getTarget(this);
}

void CameraWonderPlanet::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mAxis.x, general->mAxis.y, general->mAngleA);
}

void CameraWonderPlanet::updateCameraReset() {
    if (mIsRotating) {
        rotateCamera();
    } else {
        checkReset();
    }
}

void CameraWonderPlanet::rotateCamera() {
    if (++mRotateFrame > ::cCameraResetFrame) {
        mIsRotating = false;
    }
}

void CameraWonderPlanet::checkReset() {
    if (!CameraLocalUtil::tryCameraResetTrigger()) {
        return;
    }

    if (MR::isNearZero(CameraLocalUtil::getTarget(this)->getGravityVector())) {
        return;
    }

    Vec3f offset = CameraLocalUtil::getWatchPos(this) - CameraLocalUtil::getPos(this);
    Vec3f front = offset.killElement(CameraLocalUtil::getTarget(this)->getGravityVector());
    if (MR::isNearZero(front)) {
        return;
    }
    MR::normalize(&front);

    Vec3f targetFront = CameraLocalUtil::getTarget(this)->getGravityVector().getOrthogonal(CameraLocalUtil::getTarget(this)->getFrontVec());

    if (MR::isNearZero(targetFront)) {
        return;
    }
    MR::normalize(&targetFront);

    Quat4f rot;
    rot.setRotate(front, targetFront);
    rot.transform(offset);

    Vec3f up = -CameraLocalUtil::getTarget(this)->getGravityVector();
    if (CameraLocalUtil::getUpVec(this).dot(-CameraLocalUtil::getTarget(this)->getGravityVector()) < 0.0f) {
        up.negate();
    }

    front.set(offset);
    if (MR::isNearZero(front)) {
        return;
    }
    MR::normalize(&front);

    rot.setRotate(CameraLocalUtil::getTarget(this)->getFrontVec(), front);
    rot.transform(up);

    CameraLocalUtil::setPos(this, CameraLocalUtil::getWatchPos(this) - offset);
    CameraLocalUtil::setUpVec(this, up);

    mIsRotating = true;
    mRotateFrame = 0;

    MR::startCameraInterpolation(::cCameraResetFrame);
}
