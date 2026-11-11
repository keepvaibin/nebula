#include "Game/Camera/CameraFollow.hpp"
#include "Game/Camera/CameraParamChunk.hpp"
#include "Game/Camera/Camera.hpp"
#include "Game/Camera/CameraCalc.hpp"
#include "Game/Camera/CameraHeightArrange.hpp"
#include "Game/Camera/CameraLocalUtil.hpp"
#include "Game/Camera/CameraTargetObj.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include "Game/Util/VectorUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcRotPos_f1, "calcRotPos.f1", 0.000002f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcRotPos_f2, "calcRotPos.f2", 0.000002f);
NEBULA_TUNABLE_NAMED(f32, cNum_constrainDistance_f1, "constrainDistance.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_constrainAngleX_f1, "constrainAngleX.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_constrainAngleX_f2, "constrainAngleX.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_constrainAngleY_f1, "constrainAngleY.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeHeight_f1, "arrangeHeight.f1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeHeight_f2, "arrangeHeight.f2", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeLookUp_f1, "arrangeLookUp.f1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeLookUp_f2, "arrangeLookUp.f2", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeLookDown_f1, "arrangeLookDown.f1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeLookDown_f2, "arrangeLookDown.f2", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_arrangeLookDown_f3, "arrangeLookDown.f3", 0.02f);
NEBULA_TUNABLE_NAMED(f32, cNum_startRound_f1, "startRound.f1", 2.0f);

namespace {
    NEBULA_TUNABLE(f32, sCameraRadius, 15.0f);  // the move speed that turns the camera at the full round speed
    NEBULA_TUNABLE(f32, sSlopeFrontDist, 500.0f);
    NEBULA_TUNABLE(f32, sColCheckHeight, 100.0f);
    NEBULA_TUNABLE(f32, sRoundAddition, 0.08f);
    NEBULA_TUNABLE(f32, sRoundResetAddition, 0.2f);
    static const f32 sRoundInterval = FloatUtil::PI() / 4.0f;
    static const f32 sLookDownAngle = 75.0f * FloatUtil::PI() / 180.0f;
};  // namespace

CameraFollow::CameraFollow(const char* pName)
    : Camera(pName), mDistMax(1200.0f), mDistMin(300.0f), mAngleXMin(10.0f * MR::pi() / 180.0f), mAngleXRange(20.0f * MR::pi() / 180.0f),
      mPos(0.0f, 0.0f, 0.0f), mWatchPos(0.0f, 0.0f, 0.0f), mAngleYRoundSpeed(0.15f), mAngleXSpeed(), mCheckWall(true), mIsRounding(), mAngleYTarget(),
      mAngleYSpeed(), mIsRoundLeftValid(), mIsRoundRightValid() {
    lumacore::setClass(this, lumacore::kClassId< CameraFollow >);
    createVPanObj();
}

void CameraFollow::reset() {
    mIsRounding = false;
    mAngleXSpeed = 0.0f;

    mVPan->resetJump();
    CameraLocalUtil::setPos(this, CameraLocalUtil::getPos(mCameraMan));
    CameraLocalUtil::setWatchPos(this, CameraLocalUtil::getWatchPos(mCameraMan));
    CameraLocalUtil::setUpVecAndWatchUpVec(this, CameraLocalUtil::getTarget(this)->getUpVec());
    mVPan->reset();

    Pos3f zoneMtx = mZoneMatrix;
    zoneMtx.invert(zoneMtx);
    mPos.set(CameraLocalUtil::getPos(this));
    zoneMtx.mult(mPos, mPos);
    mWatchPos.set(CameraLocalUtil::getWatchPos(this));
    zoneMtx.mult(mWatchPos, mWatchPos);

    mIsRoundLeftValid = false;
    mIsRoundRightValid = false;
}

CameraTargetObj* CameraFollow::calc() {
    mVPan->updateJump();
    mVPan->calcWatchPos(nullptr);
    if (mIsRounding) {
        calcRotPos();
    } else {
        calcIdealPos();
    }

    arrangeHeight();
    mVPan->arrange();
    checkValidityOfRound(CameraLocalUtil::getWatchPos(this), CameraLocalUtil::getPos(this));
    return CameraLocalUtil::getTarget(this);
}

void CameraFollow::setParam(f32 maxDist, f32 minDist, f32 pitchMin, f32 pitchRange, f32 angleRoundSpeed, bool checkWall) {
    mDistMax = maxDist;
    mDistMin = minDist;
    mAngleXMin = pitchMin;
    mAngleXRange = pitchRange;
    mAngleYRoundSpeed = angleRoundSpeed;
    mCheckWall = checkWall;
}

void CameraFollow::setParamFromChunk(const CameraParamChunk* pChunk) {
    CameraGeneralParam* general = pChunk->mGeneralParam;
    setParam(general->mAxis.x, general->mAxis.y, general->mAngleA, general->mAngleB, general->mDist, general->mNum1 > 0);
}

bool CameraFollow::isEnableToRoundLeft() const {
    return isUpsideDownByRoll() ? mIsRoundRightValid : mIsRoundLeftValid;
}

bool CameraFollow::isEnableToRoundRight() const {
    return isUpsideDownByRoll() ? mIsRoundLeftValid : mIsRoundRightValid;
}

void CameraFollow::calcIdealPos() {
    const Pos3f& zoneMtx = mZoneMatrix;

    Pos3f mtx = zoneMtx;
    mtx.invert(mtx);
    Vec3f watchPos = CameraLocalUtil::getWatchPos(this);
    mtx.mult(watchPos, watchPos);
    Vec3f front = CameraLocalUtil::getTarget(this)->getFrontVec();
    mtx.mult33(front);

    Vec3f pos = mPos;
    constrainDistance(&pos, watchPos);
    f32 angleY, angleX, length;
    MR::crossToPolar(watchPos, pos, &length, &angleX, &angleY);
    constrainAngleX(&angleX);
    constrainAngleY(&angleY, front, length);
    MR::polarToCross(watchPos, &pos, length, angleX, angleY);
    checkShiftStateToRound(watchPos, pos);

    mPos.set(pos);
    mWatchPos.set(watchPos);

    zoneMtx.mult(pos, pos);
    CameraLocalUtil::setPos(this, pos);

    Vec3f up(0.0f, 1.0f, 0.0f);
    zoneMtx.mult33(up);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);
}

void CameraFollow::calcRotPos() {
    Pos3f zoneMtx = mZoneMatrix;
    Pos3f mtx = zoneMtx;
    mtx.invert(mtx);

    Vec3f watchPos = CameraLocalUtil::getWatchPos(this);
    mtx.mult(watchPos, watchPos);

    Vec3f pos = mPos;
    f32 length, angleX, angleY;
    MR::crossToPolar(watchPos, pos, &length, &angleX, &angleY);
    angleY = MR::convergeRadian(angleY, mAngleYTarget, mAngleYSpeed);
    if (angleY < mAngleYTarget + cNum_calcRotPos_f1 && angleY > mAngleYTarget - cNum_calcRotPos_f2) {
        mIsRounding = false;
    }

    Vec3f newPos;
    MR::polarToCross(watchPos, &newPos, length, angleX, angleY);

    
    mPos.set(newPos);
    mWatchPos.set(watchPos);

    zoneMtx.mult(newPos, newPos);
    CameraLocalUtil::setPos(this, newPos);

    Vec3f up(0.0f, 1.0f, 0.0f);
    zoneMtx.mult33(up);
    CameraLocalUtil::setUpVecAndWatchUpVec(this, up);
}

void CameraFollow::constrainDistance(Vec3f* pPos, const Vec3f& rWatchPos) {
    Vec3f diff = *pPos - rWatchPos;
    if (diff.length() < mDistMin) {
        if (MR::isNearZero(diff)) {
            diff.set(mPos - mWatchPos);
        }

        diff.setLength(mDistMin);
    } else if (!(diff.length() < mDistMax)) {
        f32 dist = diff.length();
        MR::normalize(&diff);
        diff.scale(mDistMax + (dist - mDistMax) * cNum_constrainDistance_f1);
    }

    *pPos = diff + rWatchPos;
}

void CameraFollow::constrainAngleX(f32* pAngleX) {
    if (*pAngleX > mAngleXMin + mAngleXRange) {
        *pAngleX = *pAngleX + (mAngleXMin + mAngleXRange - *pAngleX) * cNum_constrainAngleX_f1;
    }

    if (*pAngleX < mAngleXMin) {
        *pAngleX = *pAngleX + (mAngleXMin - *pAngleX) * cNum_constrainAngleX_f2;
    }
}

void CameraFollow::constrainAngleY(f32* pAngleY, const Vec3f& rDir, f32 vecLength) {
    f32 length, angleX, angleY;
    Vec3f vec = rDir * -vecLength;
    MR::crossToPolar(Vec3f(0.0f, 0.0f, 0.0f), vec, &length, &angleX, &angleY);

    if (CameraLocalUtil::tryCameraResetTrigger()) {
        startRound(angleY, ::sRoundResetAddition);
    } else {
        *pAngleY = MR::convergeRadian(*pAngleY, angleY, getMagnitudeOfMovingRound() * MR::pi() / cNum_constrainAngleY_f1);
    }
}

f32 CameraFollow::getMagnitudeOfMovingRound() const {
    const Vec3f& move = CameraLocalUtil::getTarget(this)->getLastMove();

    Vec3f moveH = move.killElement(CameraLocalUtil::getTarget(this)->getUpVec());
    if (CameraLocalUtil::getTarget(this)->isJumping()) {
        moveH.zero();
    }

    return mAngleYRoundSpeed * MR::clamp(moveH.length() / ::sCameraRadius, 0.0f, 1.0f);
}

void CameraFollow::checkShiftStateToRound(const Vec3f& rWatchPos, const Vec3f& rPos) {
    bool left, right;
    if (isUpsideDownByRoll()) {
        left = CameraLocalUtil::testCameraPadTriggerRoundRight() && mIsRoundLeftValid;
        right = CameraLocalUtil::testCameraPadTriggerRoundLeft() && mIsRoundRightValid;
    } else {
        left = CameraLocalUtil::testCameraPadTriggerRoundLeft() && mIsRoundLeftValid;
        right = CameraLocalUtil::testCameraPadTriggerRoundRight() && mIsRoundRightValid;
    }

    if (left || right) {
        f32 length, angleX, angleY;
        MR::crossToPolar(rWatchPos, rPos, &length, &angleX, &angleY);

        angleY += left ? -::sRoundInterval : ::sRoundInterval;
        startRound(angleY, ::sRoundAddition);
    }
}

void CameraFollow::arrangeHeight() {
    reflectRotOffset();
    if (!mCheckWall) {
        mAngleXSpeed *= cNum_arrangeHeight_f1;
    } else {
        Vec3f camDir = CameraLocalUtil::getWatchPos(this) - CameraLocalUtil::getPos(this);
        if (camDir.dot(CameraLocalUtil::getTarget(this)->getFrontVec()) < 0.0f) {
            mAngleXSpeed *= cNum_arrangeHeight_f2;
        } else {
            if (isWallForward()) {
                arrangeLookUp();
            } else {
                arrangeLookDown();
            }
        }
    }
}

void CameraFollow::reflectRotOffset() {
    f32 length, angleX, angleY;
    MR::crossToPolar(CameraLocalUtil::getWatchPos(this), CameraLocalUtil::getPos(this), &length, &angleX, &angleY);

    angleX += mAngleXSpeed;
    if (angleX > ::sLookDownAngle) {
        angleX = ::sLookDownAngle;
    }

    Vec3f pos;
    MR::polarToCross(CameraLocalUtil::getWatchPos(this), &pos, length, angleX, angleY);
    CameraLocalUtil::setPos(this, pos);
}

bool CameraFollow::isWallForward() const {
    Vec3f pos = CameraLocalUtil::getTarget(this)->getPosition() + CameraLocalUtil::getTarget(this)->getUpVec() * ::sColCheckHeight;
    Vec3f front = CameraLocalUtil::getTarget(this)->getFrontVec() * ::sSlopeFrontDist;
    return Collision::checkStrikeLineToMap(pos, front, 0, nullptr, nullptr) != 0;
}

void CameraFollow::arrangeLookUp() {
    Vec3f mapPos;
    if (!checkAscent(&mapPos)) {
        mAngleXSpeed *= cNum_arrangeLookUp_f1;
        return;
    }

    mAngleXSpeed *= cNum_arrangeLookUp_f2;
}

bool CameraFollow::checkAscent(Vec3f* pVec) const {
    Vec3f pos = CameraLocalUtil::getTarget(this)->getPosition();
    pos += CameraLocalUtil::getTarget(this)->getFrontVec() * ::sSlopeFrontDist;
    pos += CameraLocalUtil::getTarget(this)->getUpVec() * ::sSlopeFrontDist;
    Vec3f dir = CameraLocalUtil::getTarget(this)->getUpVec() * -::sSlopeFrontDist;

    if (Collision::checkStrikeLineToMap(pos, dir, 0, nullptr, nullptr) == 0) {
        return false;
    }

    pVec->set(Collision::getStrikeInfoMap(0)->mHitPos);
    return true;
}

void CameraFollow::arrangeLookDown() {
    Vec3f mapPos;
    if (checkDescent(&mapPos)) {
        mAngleXSpeed *= cNum_arrangeLookDown_f1;
        return;
    }

    f32 length, angleX, angleY;
    MR::crossToPolar(CameraLocalUtil::getWatchPos(this), CameraLocalUtil::getPos(this), &length, &angleX, &angleY);

    if (angleX > ::sLookDownAngle) {
        mAngleXSpeed *= cNum_arrangeLookDown_f2;
    } else {
        mAngleXSpeed += (::sLookDownAngle - angleX - mAngleXSpeed) * cNum_arrangeLookDown_f3;
    }
}

bool CameraFollow::checkDescent(Vec3f* pVec) const {
    Vec3f pos = CameraLocalUtil::getTarget(this)->getPosition();
    pos += CameraLocalUtil::getTarget(this)->getFrontVec() * ::sSlopeFrontDist;
    pos += CameraLocalUtil::getTarget(this)->getUpVec() * ::sColCheckHeight;
    Vec3f dir = CameraLocalUtil::getTarget(this)->getUpVec() * -::sSlopeFrontDist;

    if (Collision::checkStrikeLineToMap(pos, dir, 0, nullptr, nullptr) == 0) {
        return false;
    }

    pVec->set(Collision::getStrikeInfoMap(0)->mHitPos);
    return true;
}

void CameraFollow::startRound(f32 angle, f32 rate) {
    mAngleYTarget = MR::repeat(angle, 0.0f, MR::pi() * cNum_startRound_f1);
    mAngleYSpeed = rate;
    mIsRounding = true;
}

void CameraFollow::checkValidityOfRound(const Vec3f& rWatchPos, const Vec3f& rPos) {
    // const Pos3f& zoneMtx = mZoneMatrix;

    Pos3f mtx = mZoneMatrix;
    mtx.invert(mtx);
    Vec3f watchPos = rWatchPos;
    mtx.mult(watchPos, watchPos);
    Vec3f pos = rPos;
    mtx.mult(pos, pos);

    f32 length, angleX, angleY;
    MR::crossToPolar(watchPos, pos, &length, &angleX, &angleY);

    Vec3f newPosLeft;
    MR::polarToCross(watchPos, &newPosLeft, length, angleX, angleY - ::sRoundInterval);
    mZoneMatrix.mult(newPosLeft, newPosLeft);
    if (Collision::checkStrikeLineToMap(rPos, newPosLeft - rPos, 0, nullptr, nullptr) != 0) {
        mIsRoundLeftValid = false;
    } else {
        mIsRoundLeftValid = true;
    }

    Vec3f newPosRight;
    MR::polarToCross(watchPos, &newPosRight, length, angleX, angleY + ::sRoundInterval);
    mZoneMatrix.mult(newPosRight, newPosRight);
    if (Collision::checkStrikeLineToMap(rPos, newPosRight - rPos, 0, nullptr, nullptr) != 0) {
        mIsRoundRightValid = false;
    } else {
        mIsRoundRightValid = true;
    }
}

bool CameraFollow::isUpsideDownByRoll() const {
    return MR::isOppositeDirectionRadian(CameraLocalUtil::getRoll(this), -MR::pi());
}
