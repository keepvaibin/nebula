#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>



#include <cmath>
#include <nebula_math.h>

namespace {

    // Turns of 3 degrees or less read the angles straight from the matrix.
    inline void calcEulerRadian(const Pos3f& rMtx, Vec3f* pVec, f32 turnDegree) {
        if (3.0f < turnDegree) {
            rMtx.getEulerXYZ(*pVec);
        } else {
            pVec->x = std::atan2(rMtx[2][1], rMtx[2][2]);
            pVec->z = std::atan2(rMtx[1][0], rMtx[0][0]);
            pVec->y = asin(-rMtx[2][0]);
        }
    }

};  // namespace

namespace MR {
    f32 calcDistance(const HitSensor* pSensorA, const HitSensor* pSensorB, Vec3f* pOut) {
        Vec3f toSensorB = pSensorB->mPosition - pSensorA->mPosition;

        f32 distance = toSensorB.length();

        if (pOut != nullptr) {
            if (distance > 0.0f) {
                NebulaVecScale(&toSensorB, pOut, 1.0f / distance);
            } else {
                pOut->zero();
            }
        }

        return distance;
    }

    f32 calcDistance(const LiveActor* pActor, const Vec3f& rTrans) {
        return pActor->mPosition.distance(rTrans);
    }

    // rUp: the plane's normal (unit length).
    f32 calcDistanceHorizontal(const LiveActor* pActor, const Vec3f& rTarget, const Vec3f& rUp) {
        Vec3f toTarget = rTarget - pActor->mPosition;
        MR::vecKillElement(toTarget, rUp, &toTarget);

        return toTarget.length();
    }

    f32 calcDistanceVertical(const LiveActor* pActor, const Vec3f& rTarget, const Vec3f& rUp) {
        Vec3f toTarget = rTarget - pActor->mPosition;
        toTarget.scale(rUp.dot(toTarget), rUp);

        return toTarget.length();
    }

    f32 calcDistanceVertical(const LiveActor* pActor, const Vec3f& rTarget) {
        const Vec3f& grav = pActor->mGravity;
        Vec3f toTarget = rTarget - pActor->mPosition;
        toTarget.scale(grav.dot(toTarget), grav);
        return toTarget.length();
    }

    f32 calcDistanceToPlayer(const LiveActor* pActor) {
        if (!MR::isExistSceneObj(SceneObj_MarioHolder)) {
            return FLOAT_MAX;
        }

        return pActor->mPosition.distance(*MR::getPlayerPos());
    }

    f32 calcDistanceToPlayerH(const LiveActor* pActor) {
        if (!MR::isExistSceneObj(SceneObj_MarioHolder)) {
            return FLOAT_MAX;
        }

        Vec3f toPlayer = *MR::getPlayerPos() - pActor->mPosition;
        MR::vecKillElement(toPlayer, pActor->mGravity, &toPlayer);

        return toPlayer.length();
    }

    bool isNear(const HitSensor* pSensorA, const HitSensor* pSensorB, f32 distance) {
        f32 squaredDistance = pSensorA->mPosition.squared(pSensorB->mPosition);

        return squaredDistance < distance * distance;
    }

    bool isNear(const LiveActor* pActorA, const LiveActor* pActorB, f32 distance) {
        f32 squaredDistance = pActorA->mPosition.squared(pActorB->mPosition);

        return squaredDistance < distance * distance;
    }

    bool isNear(const LiveActor* pActor, const Vec3f& rTrans, f32 distance) {
        f32 squaredDistance = pActor->mPosition.squared(rTrans);

        return squaredDistance < distance * distance;
    }

    bool isNearPlayer(const LiveActor* pActor, f32 distance) {
        if (MR::isPlayerHidden()) {
            return false;
        }

        return MR::isNearPlayerAnyTime(pActor, distance);
    }

    bool isNearPlayerAnyTime(const LiveActor* pActor, f32 distance) {
        return isNear(pActor, *MR::getPlayerPos(), distance);
    }

    bool isNearPlayer(const Vec3f& rPos, f32 distance) {
        if (MR::isPlayerHidden()) {
            return false;
        }

        f32 squaredDistance = MR::getPlayerPos()->squared(rPos);

        return squaredDistance < distance * distance;
    }

    // Within `radius` across the actor's up axis and `height` along it.
    bool isNearPlayerPose(const LiveActor* pActor, f32 radius, f32 height) {
        if (isPlayerHidden()) {
            return false;
        }

        Vec3f up;
        Vec3f across;
        MR::extractMtxYDir(pActor->getBaseMtx(), &up);
        Vec3f fromPlayer = pActor->mPosition - *MR::getPlayerPos();
        f32 along = MR::abs(vecKillElement(fromPlayer, up, &across));
        if (height < along) {
            return false;
        }

        return (across.squared() < radius * radius);
    }

    bool isNearPlayerHorizontal(const LiveActor* pActor, f32 distance) {
        Vec3f horizontal;
        if (isPlayerHidden()) {
            return false;
        }

        Vec3f fromPlayer = pActor->mPosition - *MR::getPlayerPos();
        vecKillElement(fromPlayer, pActor->mGravity, &horizontal);
        return (horizontal.squared() < distance * distance);
    }

    void calcFrontVec(Vec3f* pFrontVec, const LiveActor* pActor) {
        MtxPtr mtx = pActor->getBaseMtx();
        pFrontVec->set< f32 >(mtx[0][2], mtx[1][2], mtx[2][2]);
    }

    void calcUpVec(Vec3f* pUpVec, const LiveActor* pActor) {
        MtxPtr mtx = pActor->getBaseMtx();
        pUpVec->set< f32 >(mtx[0][1], mtx[1][1], mtx[2][1]);
    }

    void calcSideVec(Vec3f* pSideVec, const LiveActor* pActor) {
        MtxPtr mtx = pActor->getBaseMtx();
        pSideVec->set< f32 >(mtx[0][0], mtx[1][0], mtx[2][0]);
    }

    void calcPositionUpOffset(Vec3f* pPos, const LiveActor* pActor, f32 offset) {
        Vec3f up;
        MR::calcUpVec(&up, pActor);

        pPos->scaleAdd(offset, up, pActor->mPosition);
    }

    void calcVecToPlayerH(Vec3f* pToPlayerHVec, const LiveActor* pActor, const Vec3f* pUp) {
        calcVecToTargetPosH(pToPlayerHVec, pActor, *MR::getPlayerPos(), pUp);
    }

    void calcVecFromPlayerH(Vec3f* pFromPlayerHVec, const LiveActor* pActor) {
        calcVecToTargetPosH(pFromPlayerHVec, pActor, *MR::getPlayerPos(), nullptr);
        pFromPlayerHVec->scale(-1.0f);
    }

    // pUp: the plane's normal (null: gravity).
    void calcVecToTargetPosH(Vec3f* pToTargetHVec, const LiveActor* pActor, const Vec3f& rTarget, const Vec3f* pUp) {
        pToTargetHVec->set(rTarget);
        pToTargetHVec->sub(pActor->mPosition);

        if (pUp == nullptr) {
            MR::vecKillElement(*pToTargetHVec, pActor->mGravity, pToTargetHVec);
        } else {
            MR::vecKillElement(*pToTargetHVec, *pUp, pToTargetHVec);
        }

        MR::normalizeOrZero(pToTargetHVec);
    }

    void calcVecFromTargetPosH(Vec3f* pFromTargetHVec, const LiveActor* pActor, const Vec3f& rTarget, const Vec3f* pUp) {
        calcVecToTargetPosH(pFromTargetHVec, pActor, rTarget, pUp);
        pFromTargetHVec->scale(-1.0f);
    }

    bool isFaceToTargetDegree(const LiveActor* pActor, const Vec3f& rTarget, const Vec3f& rFront, f32 angle) {
        return MR::isNearAngleDegree(rTarget - pActor->mPosition, rFront, angle);
    }

    bool isFaceToPlayerDegree(const LiveActor* pActor, const Vec3f& rFront, f32 angle) {
        return isFaceToTargetDegree(pActor, *MR::getPlayerPos(), rFront, angle);
    }

    bool isFaceToTargetDegree(const LiveActor* pActor, const Vec3f& rTarget, f32 angle) {
        Vec3f frontVec;
        MR::calcFrontVec(&frontVec, pActor);

        return isFaceToTargetDegree(pActor, rTarget, frontVec, angle);
    }

    bool isFaceToPlayerDegree(const LiveActor* pActor, f32 angle) {
        Vec3f frontVec;
        MR::calcFrontVec(&frontVec, pActor);

        return isFaceToTargetDegree(pActor, *MR::getPlayerPos(), frontVec, angle);
    }

    bool isFaceToPlayerDegreeHV(const LiveActor* pActor, const Vec3f& rFront, f32 angleH, f32 angleV) {
        return MR::isNearAngleDegreeHV(*MR::getPlayerPos() - pActor->mPosition, rFront, pActor->mGravity, angleH, angleV);
    }

    bool isFaceToTargetHorizontalDegree(const LiveActor* pActor, const Vec3f& rTarget, const Vec3f& rFront, f32 angle) {
        Vec3f toTarget = (rTarget - pActor->mPosition).killElement(pActor->mGravity);
        Vec3f front = rFront.killElement(pActor->mGravity);

        return MR::isNearAngleDegree(toTarget, front, angle);
    }

    bool isFaceToPlayerHorizontalDegree(const LiveActor* pActor, const Vec3f& rFront, f32 angle) {
        return MR::isFaceToTargetHorizontalDegree(pActor, *MR::getPlayerPos(), rFront, angle);
    }

    bool isClockwiseToDir(const LiveActor* pActor, const Vec3f& rFront, const Vec3f& rDir) {
        Vec3f side;
        Vec3f cross = pActor->mGravity.cross(rDir);
        normalizeOrZero(cross, &side);
        return (side.dot(rFront) >= 0.0f);
    }

    bool isClockwiseToPlayer(const LiveActor* pActor, const Vec3f& rFront) {
        Vec3f toPlayer = *MR::getPlayerPos() - pActor->mPosition;
        Vec3f cross = pActor->mGravity.cross(toPlayer);
        Vec3f side;
        normalizeOrZero(cross, &side);
        return (side.dot(rFront) >= 0.0f);
    }

    bool isPlayerLeftSide(const LiveActor* pActor) {
        MtxPtr mtx = pActor->getBaseMtx();
        Vec3f sideVec = Vec3f(mtx[0][0], mtx[1][0], mtx[2][0]);
        Vec3f toPlayer = *MR::getPlayerPos() - pActor->mPosition;
        return (toPlayer.dot(sideVec) >= 0.0f);
    }

    bool isInSightConePlayer(const LiveActor* pActor, const Vec3f& rFront, f32 distance, f32 angle) {
        return isNearPlayerAnyTime(pActor, distance) && isFaceToPlayerDegree(pActor, rFront, angle);
    }

    bool isInSightFanPlayer(const LiveActor* pActor, const Vec3f& rFront, f32 distance, f32 angleH, f32 angleV) {
        return isNearPlayerAnyTime(pActor, distance) && isFaceToPlayerDegreeHV(pActor, rFront, angleH, angleV);
    }

    // A wall hit while moving along rDir, at least `cosine` head-on.
    bool isBindedWallFront(const LiveActor* pActor, const Vec3f& rDir, f32 cosine) {
        if (isBindedWall(pActor)) {
            Vec3f wallNormal(*getWallNormal(pActor));
            if (rDir.dot(wallNormal) < -cosine) {
                return true;
            }
        }

        return false;
    }

    bool isOnPlayer(const LiveActor* pActor) {
        return isActorOnPlayer(pActor);
    }

    // From a point `offset` up the actor, the player's centre is within `cosine` of the side plane.
    bool isPlayerExistSide(const LiveActor* pActor, f32 offset, f32 cosine) {
        Vec3f toPlayer = *getPlayerCenterPos();
        Vec3f upVec;
        calcUpVec(&upVec, pActor);
        Vec3f origin = pActor->mPosition + upVec * offset;
        toPlayer.sub(origin);
        normalizeOrZero(&toPlayer);
        f32 along = upVec.dot(toPlayer);
        if (along < 0.0f) {
            along = -along;
        }

        if (along < cosine) {
            return true;
        }

        return false;
    }

    bool isPlayerExistUp(const LiveActor* pActor, f32 offset, f32 cosine) {
        Vec3f toPlayer = *getPlayerCenterPos();
        Vec3f upVec;
        calcUpVec(&upVec, pActor);
        Vec3f origin = pActor->mPosition + upVec * offset;
        toPlayer.sub(origin);
        normalizeOrZero(&toPlayer);
        return (cosine < upVec.dot(toPlayer));
    }

    bool isPlayerExistDown(const LiveActor* pActor, f32 offset, f32 cosine) {
        Vec3f toPlayer = *getPlayerCenterPos();
        Vec3f upVec;
        calcUpVec(&upVec, pActor);
        upVec = -upVec;
        toPlayer.sub(pActor->mPosition + upVec * offset);
        normalizeOrZero(&toPlayer);
        return (cosine < upVec.dot(toPlayer));
    }

    bool isInWater(const LiveActor* pActor, const Vec3f& rOffset) {
        return MR::isInWater(pActor->mPosition + rOffset);
    }

    bool isInDeath(const LiveActor* pActor, const Vec3f& rOffset) {
        return MR::isInDeath(pActor->mPosition + rOffset);
    }

    void makeMtxTR(MtxPtr pMtx, const LiveActor* pActor) {
        MR::makeMtxTR(pMtx, pActor->mPosition.x, pActor->mPosition.y, pActor->mPosition.z, pActor->mRotation.x, pActor->mRotation.y,
                      pActor->mRotation.z);
    }

    void makeMtxTRS(MtxPtr pMtx, const LiveActor* pActor) {
        MR::makeMtxTRS(pMtx, pActor->mPosition.x, pActor->mPosition.y, pActor->mPosition.z, pActor->mRotation.x, pActor->mRotation.y,
                       pActor->mRotation.z, pActor->mScale.x, pActor->mScale.y, pActor->mScale.z);
    }

    void makeMtxTransRotateY(MtxPtr pMtx, const LiveActor* pActor) {
        MR::makeMtxTransRotateY(pMtx, pActor->mPosition.x, pActor->mPosition.y, pActor->mPosition.z, pActor->mRotation.y);
    }

    void calcMtxFromGravityAndZAxis(Pos3f* pResult, const LiveActor* pActor, const Vec3f& rVGravity, const Vec3f& rZAxis) {
        Pos3f pos;
        pos.setInline(pActor->getBaseMtx());
        Vec3f yDir;
        Vec3f zDir;
        Vec3f xDir;
        if (!isNearZero(rVGravity)) {
            yDir = -rVGravity;
        } else {
            pos.getYDir(yDir);
        }

        if (!isNearZero(rZAxis)) {
            zDir = rZAxis;
        } else {
            pos.getZDir(zDir);
        }

        xDir.cross(zDir, yDir);
        if (isNearZero(xDir)) {
            pos.getXDir(xDir);
            zDir.cross(xDir, yDir);
        }

        makeMtxUpFrontPos(pResult, yDir, zDir, pActor->mPosition);
    }

    void calcAndSetBaseMtxFromGravityAndZAxis(LiveActor* pActor, const Vec3f& rZAxis) {
        Pos3f mtx;
        mtx.identity();
        calcMtxFromGravityAndZAxis(&mtx, pActor, pActor->mGravity, rZAxis);
        setBaseTRMtx(pActor, mtx);
    }

    void calcActorAxis(Vec3f* pAxisX, Vec3f* pAxisY, Vec3f* pAxisZ, const LiveActor* pActor) {
        Rot3f mtx;
        mtx.identity();
        makeMtxTR(mtx, pActor);

        mtx.getXDir(*pAxisX);
        mtx.getYDir(*pAxisY);
        mtx.getZDir(*pAxisZ);
    }

    void calcActorAxisX(Vec3f* pAxisX, const LiveActor* pActor) {
        Rot3f mtx;
        mtx.identity();
        makeMtxTR(mtx, pActor);

        mtx.getXDir(*pAxisX);
    }

    void calcActorAxisY(Vec3f* pAxisY, const LiveActor* pActor) {
        Rot3f mtx;
        mtx.identity();
        makeMtxTR(mtx, pActor);

        mtx.getYDir(*pAxisY);
    }

    void calcActorAxisZ(Vec3f* pAxisZ, const LiveActor* pActor) {
        Rot3f mtx;
        mtx.identity();
        makeMtxTR(mtx, pActor);

        mtx.getZDir(*pAxisZ);
    }

    bool faceToVector(Quat4f* pQuat, Vec3f direction, f32 degree) {
        Vec3f yDir;
        pQuat->getYDir(yDir);
        normalizeOrZero(&direction);
        if (vecKillElement(direction, yDir, &direction) > 0.95f) {
            return false;
        }

        return turnQuatZDirRad(pQuat, *pQuat, direction, degree * PI_180);
    }

    bool faceToVector(MtxPtr pMtx, Vec3f direction, f32 degree) {
        Vec3f front(pMtx[0][2], pMtx[1][2], pMtx[2][2]);
        Vec3f up(pMtx[0][1], pMtx[1][1], pMtx[2][1]);
        Vec3f axis(0.0f, 1.0f, 0.0f);
        normalizeOrZero(&direction);
        if (vecKillElement(direction, up, &direction) > 0.95f) {
            return true;
        }

        normalizeOrZero(&direction);
        if (direction.dot(front) > MR::cos(PI_180 * (degree / 2.0f))) {
            return true;
        }

        Vec3f cross;
        cross.cross(direction, front);
        Mtx rotate;
        if (up.dot(cross) < 0.0f) {
            NebulaMtxRotAxisRad(rotate, &axis, PI_180 * degree);
        } else {
            NebulaMtxRotAxisRad(rotate, &axis, -(PI_180 * degree));
        }

        NebulaMtxConcat(pMtx, rotate, pMtx);
        return false;
    }

    bool faceToPoint(MtxPtr pMtx, Vec3f point, f32 degree) {
        point.sub(Vec3f(pMtx[0][3], pMtx[1][3], pMtx[2][3]));

        return faceToVector(pMtx, point, degree);
    }

    void makeQuatFromRotate(Quat4f* pQuat, const LiveActor* pActor) {
        makeQuatRotateDegree(pQuat, pActor->mRotation);
    }

    void makeQuatAndFrontFromRotate(Quat4f* pQuat, Vec3f* pFront, const LiveActor* pActor) {
        makeQuatRotateDegree(pQuat, pActor->mRotation);
        pQuat->getZDir(*pFront);
    }

    void turnQuatUpToGravity(Quat4f* pQuatDest, const Quat4f& rQuatSrc, const LiveActor* pActor) {
        Vec3f yDir;
        rQuatSrc.getYDir(yDir);

        Quat4f turn;
        if (yDir.dot(-pActor->mGravity) <= -0.999f) {
            Vec3f xDir;
            rQuatSrc.getXDir(xDir);
            turn.setRotate(xDir, PI);
        } else {
            turn.setRotate(yDir, -pActor->mGravity);
        }

        NebulaQuatMultiply(reinterpret_cast< const Quaternion* >(&turn), reinterpret_cast< const Quaternion* >(&rQuatSrc),
                       reinterpret_cast< Quaternion* >(pQuatDest));
        pQuatDest->normalize();
    }

    void blendQuatFromGroundAndFront(Quat4f* pDst, const LiveActor* pActor, const Vec3f& rFrontVec, f32 upVecBlendRate, f32 frontVecBlendRate) {
        MR::blendQuatUpFront(pDst, MR::isBindedGround(pActor) ? *MR::getGroundNormal(pActor) : -pActor->mGravity, rFrontVec, upVecBlendRate,
                             frontVecBlendRate);
    }

    void resetPosition(LiveActor* pActor) {
        MotionSignal::newLife(pActor);
        MR::clearHitSensors(pActor);

        if (pActor->mBinder != nullptr) {
            pActor->mBinder->clear();
        }

        if (MR::isCalcGravity(pActor)) {
            MR::calcGravity(pActor);
        }

        MR::calcAnimDirect(pActor);

        if (pActor->mCollisionParts != nullptr) {
            MR::resetAllCollisionMtx(pActor);
        }

        MR::requestCalcActorShadowAppear(pActor);
    }

    void resetPosition(LiveActor* pActor, const Vec3f& rPosition) {
        pActor->mPosition.set(rPosition);

        MR::resetPosition(pActor);
    }

    void resetPosition(LiveActor* pActor, const char* pPosName) {
        Vec3f pos(0.0f, 0.0f, 0.0f);
        Vec3f rot(0.0f, 0.0f, 0.0f);
        MR::findNamePos(pPosName, &pos, &rot);

        pActor->mPosition.set(pos);
        pActor->mRotation.set(rot);

        MR::resetPosition(pActor);
    }

    bool makeMtxOnMapCollision(Pos3f* pMtx, LiveActor* pActor, f32 length) {
        calcGravity(pActor);
        Vec3f direction(pActor->mGravity);
        direction.scale(length);
        Vec3f normal = -pActor->mGravity;
        Vec3f position(pActor->mPosition);
        if (!getFirstPolyNormalOnLineToMap(&normal, pActor->mPosition, direction, &position, nullptr)) {
            return false;
        }

        MtxPtr pBaseMtx = pActor->getBaseMtx();
        Vec3f front(pBaseMtx[0][2], pBaseMtx[1][2], pBaseMtx[2][2]);
        makeMtxUpFrontPos(pMtx, normal, front, position);
        return true;
    }

    void calcVelocityMoveToDirectionHorizon(Vec3f* pVel, const LiveActor* pActor, const Vec3f& rDir, f32 speed) {
        pVel->killElement(rDir, pActor->mGravity);
        normalizeOrZero(pVel);
        pVel->scale(speed);
    }

    // The speed runs from speedMin to speedMax as the horizontal length runs from lengthMin to lengthMax.
    void calcVelocityMoveToDirectionHorizon(Vec3f* pVel, const LiveActor* pActor, const Vec3f& rDir, f32 speedMin, f32 speedMax, f32 lengthMin,
                                            f32 lengthMax) {
        pVel->killElement(rDir, pActor->mGravity);

        f32 length;
        separateScalarAndDirection(&length, pVel, *pVel);

        pVel->scale(getInterpolateValue(normalize(length, lengthMin, lengthMax), speedMin, speedMax));
    }

    void calcVelocityMoveToDirection(Vec3f* pVel, const LiveActor* pActor, const Vec3f& rDir, f32 speed) {
        calcVelocityMoveToDirectionHorizon(pVel, pActor, rDir, speed);

        if (isOnGround(pActor)) {
            pVel->orthogonalize(*getGroundNormal(pActor));
        }
    }

    void calcVelocityMoveToDirection(Vec3f* pVel, const LiveActor* pActor, const Vec3f& rDir, f32 speedMin, f32 speedMax, f32 lengthMin,
                                     f32 lengthMax) {
        calcVelocityMoveToDirectionHorizon(pVel, pActor, rDir, speedMin, speedMax, lengthMin, lengthMax);

        if (isOnGround(pActor)) {
            pVel->orthogonalize(*getGroundNormal(pActor));
        }
    }

    void calcVelocityMoveToTarget(Vec3f* pVel, const LiveActor* pActor, const Vec3f& rTarget, f32 speed) {
        calcVelocityMoveToDirection(pVel, pActor, rTarget - pActor->mPosition, speed);
    }

    void addVelocityMoveToDirection(LiveActor* pActor, const Vec3f& rDir, f32 speed) {
        Vec3f accel;
        calcVelocityMoveToDirection(&accel, pActor, rDir, speed);

        addVelocity(pActor, accel);
    }

    void addVelocityMoveToTarget(LiveActor* pActor, const Vec3f& rTarget, f32 speed) {
        Vec3f toTarget = rTarget - pActor->mPosition;
        Vec3f accel;
        calcVelocityMoveToDirection(&accel, pActor, toTarget, speed);

        addVelocity(pActor, accel);
    }

    void addVelocityMoveToTarget(LiveActor* pActor, const Vec3f& rTarget, f32 speedMin, f32 speedMax, f32 lengthMin, f32 lengthMax) {
        Vec3f toTarget = rTarget - pActor->mPosition;

        Vec3f accel;
        calcVelocityMoveToDirection(&accel, pActor, toTarget, speedMin, speedMax, lengthMin, lengthMax);

        addVelocity(pActor, accel);
    }

    void addVelocityAwayFromTarget(LiveActor* pActor, const Vec3f& rTarget, f32 speed) {
        Vec3f fromTarget = pActor->mPosition - rTarget;

        Vec3f accel;
        calcVelocityMoveToDirection(&accel, pActor, fromTarget, speed);

        addVelocity(pActor, accel);
    }

    void addVelocityAwayFromTarget(LiveActor* pActor, const Vec3f& rTarget, f32 speedMin, f32 speedMax, f32 lengthMin, f32 lengthMax) {
        Vec3f fromTarget = pActor->mPosition - rTarget;

        Vec3f accel;
        calcVelocityMoveToDirection(&accel, pActor, fromTarget, speedMin, speedMax, lengthMin, lengthMax);

        addVelocity(pActor, accel);
    }

    void addVelocityClockwiseToDirection(LiveActor* pActor, const Vec3f& rDir, f32 speed) {
        Vec3f side;
        if (normalizeOrZero(rDir, &side)) {
            return;
        }

        side.cross(pActor->mGravity, side);
        Vec3f accel;
        calcVelocityMoveToDirection(&accel, pActor, side, speed);
        pActor->mVelocity.add(accel);
    }

    void addVelocityClockwiseToTarget(LiveActor* pActor, const Vec3f& rTarget, f32 speed) {
        addVelocityClockwiseToDirection(pActor, rTarget - pActor->mPosition, speed);
    }

    void addVelocityClockwiseToPlayer(LiveActor* pActor, f32 speed) {
        addVelocityClockwiseToDirection(pActor, *getPlayerPos() - pActor->mPosition, speed);
    }

    void addVelocityJump(LiveActor* pActor, f32 speed) {
        addVelocity(pActor, pActor->mGravity * -speed);
    }

    // rLimit: direction and the speed along it the velocity is raised to.
    bool addVelocityLimit(LiveActor* pActor, const Vec3f& rLimit) {
        Vec3f dir;
        f32 limit;
        separateScalarAndDirection(&limit, &dir, rLimit);

        if (isNearZero(dir)) {
            return false;
        }

        f32 along = dir.dot(pActor->mVelocity);

        if (limit <= along) {
            return false;
        }

        pActor->mVelocity.add(dir * (limit - along));

        return true;
    }

    void setVelocityJump(LiveActor* pActor, f32 speed) {
        setVelocity(pActor, pActor->mGravity * -speed);
    }

    void addVelocityToGravity(LiveActor* pActor, f32 speed) {
        addVelocity(pActor, pActor->mGravity * speed);
    }

    void addVelocityToGravityOrGround(LiveActor* pActor, f32 speed) {
        if (isBindedGround(pActor)) {
            pActor->mVelocity.scaleAdd(-speed, *getGroundNormal(pActor), pActor->mVelocity);
        } else {
            addVelocityToGravity(pActor, speed);
        }
    }

    bool addVelocityToCollisionNormal(LiveActor* pActor, f32 speed) {
        if (!isBinded(pActor)) {
            return false;
        }

        Vec3f normal(*getBindedFixReactionVector(pActor));

        if (isNearZero(normal)) {
            return false;
        }

        normalize(&normal);
        pActor->mVelocity.add(normal * speed);

        return true;
    }

    void addVelocityKeepHeight(LiveActor* pActor, const Vec3f& rTarget, f32 speed, f32 range) {
        addVelocityKeepHeight(pActor, rTarget, 0.0f, speed, range);
    }

    // Pulls along gravity toward rTarget's height less offset, full speed at range or more.
    void addVelocityKeepHeight(LiveActor* pActor, const Vec3f& rTarget, f32 offset, f32 speed, f32 range) {
        Vec3f toTarget = rTarget - pActor->mPosition;
        addVelocityToGravity(pActor, speed * normalizeAbs(pActor->mGravity.dot(toTarget) - offset, -range, range));
    }

    bool addVelocityKeepHeightUseShadow(LiveActor* pActor, f32 height, f32 speed, f32 range, const char* pName) {
        if (pName != nullptr) {
            if (!isShadowProjected(pActor, pName)) {
                return false;
            }
        } else {
            if (!isShadowProjectedAny(pActor)) {
                return false;
            }
        }

        f32 shadowLength;
        if (pName != nullptr) {
            shadowLength = getShadowProjectionLength(pActor, pName);
        } else {
            shadowLength = getShadowNearProjectionLength(pActor);
        }

        shadowLength -= height;
        addVelocityToGravity(pActor, speed * normalizeAbs(shadowLength, -range, range));
        return true;
    }

    bool addVelocityKeepHeightUseShadow(LiveActor* pActor, f32 height, f32 speedDown, f32 speedUp, f32 range, const char* pName) {
        if (pName != nullptr) {
            if (!isShadowProjected(pActor, pName)) {
                return false;
            }
        } else {
            if (!isShadowProjectedAny(pActor)) {
                return false;
            }
        }

        f32 over;
        if (pName != nullptr) {
            over = getShadowProjectionLength(pActor, pName);
        } else {
            over = getShadowNearProjectionLength(pActor);
        }

        over -= height;
        over = normalizeAbs(over, -range, range);
        if (over >= 0.0f) {
            addVelocityToGravity(pActor, speedDown * over);
        } else {
            addVelocityToGravity(pActor, speedUp * over);
        }

        return true;
    }

    void addVelocitySeparateHV(LiveActor* pActor, const Vec3f& rDir, f32 velH, f32 velV) {
        Vec3f horizontal;
        horizontal.killElement(rDir, pActor->mGravity);
        normalizeOrZero(&horizontal);

        addVelocity(pActor, horizontal * velH - pActor->mGravity * velV);
    }

    void setVelocitySeparateHV(LiveActor* pActor, const Vec3f& rDir, f32 velH, f32 velV) {
        Vec3f horizontal;
        horizontal.killElement(rDir, pActor->mGravity);
        normalizeOrZero(&horizontal);

        setVelocity(pActor, horizontal * velH - pActor->mGravity * velV);
    }

    void attenuateVelocity(LiveActor* pActor, f32 friction) {
        pActor->mVelocity.scale(friction);
    }

    void attenuateVelocityExceptDirection(LiveActor* pActor, const Vec3f& rDir, f32 rate) {
        Vec3f across;
        across.killElement(pActor->mVelocity, rDir);
        across.scale(rate);
        f32 along = rDir.dot(pActor->mVelocity);
        pActor->mVelocity.scale(along, rDir);
        pActor->mVelocity.add(across);
    }

    void restrictVelocity(LiveActor* pActor, f32 speed) {
        if (pActor->mVelocity.squared() > speed * speed) {
            pActor->mVelocity.setLength(speed);
        }
    }

    void killVelocityOnGroundH(LiveActor* pActor) {
        if (isOnGround(pActor)) {
            const Vec3f* pNormal = getGroundNormal(pActor);
            pActor->mVelocity.scale(pNormal->dot(pActor->mVelocity), *pNormal);
        }
    }

    void killVelocityOnGroundCosH(LiveActor* pActor, f32 cosine) {
        if (isOnGround(pActor) && isFloorPolygonCos(*getGroundNormal(pActor), pActor->mGravity, cosine)) {
            const Vec3f* pNormal = getGroundNormal(pActor);
            pActor->mVelocity.scale(pNormal->dot(pActor->mVelocity), *pNormal);
        }
    }

    void killVelocityToTarget(LiveActor* pActor, const Vec3f& rTarget) {
        Vec3f toTarget;
        toTarget.sub(rTarget, pActor->mPosition);
        normalize(&toTarget);
        if (pActor->mVelocity.dot(toTarget) > 0.0f) {
            pActor->mVelocity.killElement(pActor->mVelocity, toTarget);
        }
    }

    // Raises the velocity toward the shadow's ground, rate of the distance to it less offset.
    void forceBindOnGround(LiveActor* pActor, f32 offset, f32 rate) {
        if (isShadowProjected(pActor, nullptr)) {
            Vec3f toGround;
            getShadowProjectionPos(pActor, nullptr, &toGround);
            toGround.sub(pActor->mPosition);
            f32 depth = toGround.dot(pActor->mGravity) - offset;

            toGround.set(pActor->mGravity * depth);
            addVelocityLimit(pActor, toGround * rate);
        }
    }

    // Each rebound is the share of the speed into the surface given back; slower hits only stop.
    bool reboundVelocityFromEachCollision(LiveActor* pActor, f32 floorRebound, f32 wallRebound, f32 ceilingRebound, f32 minSpeed) {
        if (!isBinded(pActor)) {
            return false;
        }

        Vec3f normal = *getBindedFixReactionVector(pActor);
        if (isNearZero(normal)) {
            return false;
        }

        normalize(&normal);
        f32 cosGravity = normal.dot(pActor->mGravity);
        f32 rebound = 0.0f;
        if (isFloorPolygon(cosGravity)) {
            rebound = floorRebound;
        } else if (isWallPolygon(cosGravity)) {
            rebound = wallRebound;
        } else if (isCeilingPolygon(cosGravity)) {
            rebound = ceilingRebound;
        }

        f32 into = normal.dot(pActor->mVelocity);
        if (into < -minSpeed) {
            pActor->mVelocity.sub(normal * into * (1.0f + rebound));
            return true;
        }

        if (into < 0.0f) {
            pActor->mVelocity.sub(normal * into);
        }

        return false;
    }

    // A hit faster than minSpeed keeps keepRate of the speed along the surface and rebound of the speed into it.
    bool reboundVelocityFromCollision(LiveActor* pActor, f32 rebound, f32 minSpeed, f32 keepRate) {
        if (!isBinded(pActor)) {
            return false;
        }

        Vec3f normal = *getBindedFixReactionVector(pActor);
        if (isNearZero(normal)) {
            return false;
        }

        normalize(&normal);
        f32 into = normal.dot(pActor->mVelocity);
        if (into < -minSpeed) {
            pActor->mVelocity.sub(normal * into);
            pActor->mVelocity.scale(keepRate);
            pActor->mVelocity.sub(normal * into * rebound);
            return true;
        }

        if (into < 0.0f) {
            pActor->mVelocity.sub(normal * into);
        }

        return false;
    }

    void zeroVelocity(LiveActor* pActor) {
        pActor->mVelocity.zero();
    }

    void setVelocity(LiveActor* pActor, const Vec3f& rVelocity) {
        pActor->mVelocity.set(rVelocity);
    }

    void addVelocity(LiveActor* pActor, const Vec3f& rVelocity) {
        pActor->mVelocity.add(rVelocity);
    }

    void scaleVelocity(LiveActor* pActor, f32 scale) {
        pActor->mVelocity.scale(scale);
    }

    f32 calcVelocityLength(const LiveActor* pActor) {
        return pActor->mVelocity.length();
    }

    f32 calcGravitySpeed(const LiveActor* pActor) {
        return pActor->mVelocity.dot(pActor->mGravity);
    }

    // Rising speed is kept whole; on the ground, a slide slower than stopSpeed stops.
    void applyVelocityDampAndGravity(LiveActor* pActor, f32 gravity, f32 groundedScalar, f32 airborneScalar, f32 fallingScalar, f32 stopSpeed) {
        Vec3f horizontal;
        Vec3f vertical;
        if (!isBindedGround(pActor)) {
            pActor->mVelocity.add(pActor->mGravity * gravity);
        }

        horizontal.killElement(pActor->mVelocity, pActor->mGravity);
        f32 along = pActor->mGravity.dot(pActor->mVelocity);
        vertical.scale(along, pActor->mGravity);
        if (isOnGround(pActor)) {
            horizontal.scale(groundedScalar);
        } else {
            horizontal.scale(airborneScalar);
        }

        if (vertical.dot(pActor->mGravity) < 0.0f) {
            vertical.scale(fallingScalar);
        }

        pActor->mVelocity.set(horizontal + vertical);
        if (isOnGround(pActor)) {
            Vec3f normal(*getGroundNormal(pActor));
            horizontal.killElement(pActor->mVelocity, normal);
            if (horizontal.squared() < stopSpeed * stopSpeed) {
                pActor->mVelocity.scale(normal.dot(pActor->mVelocity), normal);
            }
        }
    }

    void setVelocityJumpAwayFromPlayer(LiveActor* pActor, f32 speedH, f32 speedJump) {
        Vec3f* pVelocity = &pActor->mVelocity;
        calcVecToTargetPosH(pVelocity, pActor, *getPlayerPos(), nullptr);
        pVelocity->scale(-speedH);
        addVelocityJump(pActor, speedJump);
    }

    bool sendMsgPushAndKillVelocityToTarget(LiveActor* pActor, HitSensor* pSender, HitSensor* pReceiver) {
        if (sendMsgPush(pSender, pReceiver)) {
            Vec3f toReceiver = pReceiver->mPosition - pSender->mPosition;
            normalizeOrZero(&toReceiver);
            if (pActor->mVelocity.dot(toReceiver) < 0.0f) {
                pActor->mVelocity.killElement(pActor->mVelocity, toReceiver);
            }

            return true;
        }

        return false;
    }

    // Pushes from pSender toward pReceiver; while rising faster than speed the push is horizontal.
    void addVelocityFromPush(LiveActor* pActor, f32 speed, HitSensor* pSender, HitSensor* pReceiver) {
        Vec3f push = pReceiver->mPosition - pSender->mPosition;
        if (speed < pActor->mVelocity.dot(-pActor->mGravity)) {
            vecKillElement(push, pActor->mGravity, &push);
        }

        push.setLength(speed);
        addVelocityLimit(pActor, push);
    }

    void addVelocityFromPushHorizon(LiveActor* pActor, f32 speed, HitSensor* pSender, HitSensor* pReceiver) {
        Vec3f push = pReceiver->mPosition - pSender->mPosition;
        vecKillElement(push, pActor->mGravity, &push);
        push.setLength(speed);
        pActor->mVelocity.add(push);
    }

    void addVelocitySeparateHV(LiveActor* pActor, HitSensor* pSender, HitSensor* pReceiver, f32 velH, f32 velV) {
        addVelocitySeparateHV(pActor, pReceiver->mPosition - pSender->mPosition, velH, velV);
    }

    void setVelocitySeparateHV(LiveActor* pActor, HitSensor* pSender, HitSensor* pReceiver, f32 velH, f32 velV) {
        setVelocitySeparateHV(pActor, pReceiver->mPosition - pSender->mPosition, velH, velV);
    }

    void setVelocityBlowAttack(LiveActor* pActor, HitSensor* pSender, HitSensor* pReceiver, f32 velH, f32 velV, s32 stopFrames) {
        setVelocitySeparateHV(pActor, pSender, pReceiver, velH, velV);

        if (isNearPlayerAnyTime(pActor, 1000.0f)) {
            stopSceneForDefaultHit(stopFrames);
        }
    }

    bool reflectVelocityH(LiveActor* pActor, HitSensor* pSender, HitSensor* pReceiver, f32 rebound) {
        Vec3f normal;
        calcSensorHorizonNormalize(&normal, pActor->mGravity, pReceiver, pSender);
        f32 push = (1.0f + rebound) * pActor->mVelocity.dot(normal);
        if (push > 0.0f) {
            return false;
        }

        pActor->mVelocity.sub(normal * push);
        return true;
    }

    bool calcVelocityAreaMoveOnGround(Vec3f* pVel, const LiveActor* pActor) {
        pVel->zero();
        if (!isOnGround(pActor) || !isBindedGroundAreaMove(pActor)) {
            return false;
        }

        calcAreaMoveVelocity(pVel, pActor->mPosition);
        return true;
    }

    bool calcVelocityRailMoveOnGround(Vec3f* pVelocity, const LiveActor* pActor) {
        pVelocity->zero();
        if (!isOnGround(pActor) || !isBindedGroundRailMove(pActor)) {
            return false;
        }

        LiveActor* pGroundActor = getGroundSensor(pActor)->mHost;
        calcNearestRailDirection(pVelocity, pGroundActor, pActor->mPosition);
        s32 railArg = -1;
        getRailArg3NoInit(pGroundActor, &railArg);
        pVelocity->scale(railArg == -1 ? 10 : railArg);
        return true;
    }

    bool calcVelocityAreaOrRailMoveOnGround(Vec3f* pVel, const LiveActor* pActor) {
        return calcVelocityAreaMoveOnGround(pVel, pActor) || calcVelocityRailMoveOnGround(pVel, pActor);
    }

    void rotateDirectionGravityDegree(const LiveActor* pActor, Vec3f* pDir, f32 angle) {
        rotateVecRadian(pDir, pActor->mGravity, PI_180 * angle);
    }

    void turnDirection(const LiveActor* pActor, Vec3f* pDir, const Vec3f& rTargetDir, f32 cosine) {
        turnVecToVecCosOnPlane(pDir, rTargetDir, pActor->mGravity, cosine);
    }

    void turnDirectionDegree(const LiveActor* pActor, Vec3f* pDir, const Vec3f& rTargetDir, f32 degree) {
        turnVecToVecCosOnPlane(pDir, rTargetDir, pActor->mGravity, cosDegree(degree));
    }

    void turnDirectionToTarget(const LiveActor* pActor, Vec3f* pDir, const Vec3f& rTarget, f32 cosine) {
        Vec3f toTarget = rTarget - pActor->mPosition;
        turnVecToVecCosOnPlane(pDir, toTarget, pActor->mGravity, cosine);
    }

    void turnDirectionToTargetDegree(const LiveActor* pActor, Vec3f* pDir, const Vec3f& rTarget, f32 degree) {
        Vec3f toTarget = rTarget - pActor->mPosition;
        turnVecToVecCosOnPlane(pDir, toTarget, pActor->mGravity, cosDegree(degree));
    }

    void turnDirectionToTargetDegreeHorizon(const LiveActor* pActor, Vec3f* pDir, const Vec3f& rTarget, f32 degree) {
        Vec3f toTarget = rTarget - pActor->mPosition;
        vecKillElement(toTarget, pActor->mGravity, &toTarget);
        turnVecToVecCosOnPlane(pDir, toTarget, pActor->mGravity, cosDegree(degree));
    }

    bool turnDirectionToTargetUseGroundNormalDegree(const LiveActor* pActor, Vec3f* pDir, const Vec3f& rTarget, f32 degree) {
        Vec3f toTarget = rTarget - pActor->mPosition;
        return turnVecToVecCosOnPlane(pDir, toTarget, isBindedGround(pActor) ? *getGroundNormal(pActor) : pActor->mGravity, cosDegree(degree));
    }

    void turnDirectionToPlayerDegree(const LiveActor* pActor, Vec3f* pDir, f32 degree) {
        turnDirectionToTargetDegree(pActor, pDir, *getPlayerPos(), degree);
    }

    void turnDirectionToPlayerDegreeHorizon(const LiveActor* pActor, Vec3f* pDir, f32 degree) {
        turnDirectionToTargetDegreeHorizon(pActor, pDir, *getPlayerPos(), degree);
    }

    void turnDirectionFromTargetDegree(const LiveActor* pActor, Vec3f* pDir, const Vec3f& rTarget, f32 degree) {
        Vec3f fromTarget = pActor->mPosition - rTarget;
        turnVecToVecCosOnPlane(pDir, fromTarget, pActor->mGravity, cosDegree(degree));
    }

    void turnDirectionFromTargetDegreeHorizon(const LiveActor* pActor, Vec3f* pDir, const Vec3f& rTarget, f32 degree) {
        Vec3f fromTarget = pActor->mPosition - rTarget;
        vecKillElement(fromTarget, pActor->mGravity, &fromTarget);
        turnVecToVecCosOnPlane(pDir, fromTarget, pActor->mGravity, cosDegree(degree));
    }

    void turnDirectionFromPlayerDegree(const LiveActor* pActor, Vec3f* pDir, f32 degree) {
        turnDirectionFromTargetDegree(pActor, pDir, *getPlayerPos(), degree);
    }

    void turnDirectionFromPlayerDegreeHorizon(const LiveActor* pActor, Vec3f* pDir, f32 degree) {
        turnDirectionFromTargetDegreeHorizon(pActor, pDir, *getPlayerPos(), degree);
    }

    void turnDirectionAndGravityH(Quat4f* pDst, const LiveActor* pActor, const Vec3f& rDir, f32 radian, f32 upVecBlendRate) {
        Vec3f dirH(rDir);
        vecKillElement(dirH, pActor->mGravity, &dirH);
        normalizeOrZero(&dirH);
        Vec3f frontVec;
        pDst->getZDir(frontVec);
        f32 frontVecBlendRate = 1.0f;

        if (!isNearZero(dirH)) {
            turnVecToVecRadian(&frontVec, frontVec, dirH, radian, pActor->mGravity);
        } else {
            upVecBlendRate = 1.0f;
            frontVecBlendRate = 0.1f;
        }

        blendQuatUpFront(pDst, -pActor->mGravity, frontVec, upVecBlendRate, frontVecBlendRate);
        pDst->normalize();
    }

    void turnDirectionAndGravityH(LiveActor* pActor, const Vec3f& rDir, f32 radian, f32 upVecBlendRate) {
        Quat4f rotation;
        makeQuatRotateDegree(&rotation, pActor->mRotation);
        turnDirectionAndGravityH(&rotation, pActor, rDir, radian, upVecBlendRate);
        rotation.getEuler(pActor->mRotation);

        const Vec3f euler = pActor->mRotation;
        pActor->mRotation.set< f32 >(_180_PI * euler.x, _180_PI * euler.y, _180_PI * euler.z);
    }

    void turnDirectionToGround(const LiveActor* pActor, Vec3f* pDir) {
        Vec3f down;
        if (isBindedGround(pActor)) {
            down.scale(-1.0f, *getGroundNormal(pActor));
        } else {
            down.set(pActor->mGravity);
        }

        pDir->orthogonalize(down);
        normalize(pDir);
    }

    void moveAndTurnToDirection(LiveActor* pActor, Vec3f* pFront, const Vec3f& rDir, const ActorMoveParam& rParam) {
        turnVecToVecCosOnPlane(pFront, rDir, pActor->mGravity, cosDegree(rParam.mTurnDegree));
        turnDirectionToGround(pActor, pFront);
        Vec3f move;
        calcVelocityMoveToDirection(&move, pActor, *pFront, rParam.mMoveSpeed);
        pActor->mVelocity.add(move);
        pActor->mVelocity.scale(rParam.mDamp);
        reboundVelocityFromCollision(pActor, 0.0f, 0.0f, 1.0f);
        addVelocityToGravityOrGround(pActor, rParam.mGravity);
    }

    void moveAndTurnToTarget(LiveActor* pActor, Vec3f* pFront, const Vec3f& rTarget, const ActorMoveParam& rParam) {
        moveAndTurnToDirection(pActor, pFront, rTarget - pActor->mPosition, rParam);
    }

    void moveAndTurnToPlayer(LiveActor* pActor, Vec3f* pFront, const ActorMoveParam& rParam) {
        moveAndTurnToDirection(pActor, pFront, *getPlayerPos() - pActor->mPosition, rParam);
    }

    void flyAndTurnAlongRailSearchingPlayer(LiveActor* pActor, Vec3f* pFront, f32 curveRatio, const ActorMoveParam& rParam, bool horizontal) {
        setRailDirectionCloseToNearestPos(pActor, *getPlayerCenterPos());
        Vec3f railDir;
        calcMovingDirectionAlongRail(pActor, &railDir, pActor->mPosition, curveRatio, horizontal, nullptr);
        turnVecToVecDegree(pFront, *pFront, railDir, rParam.mTurnDegree, Vec3f(0, 1, 0));
        pActor->mVelocity.add(*pFront * rParam.mMoveSpeed);
        pActor->mVelocity.scale(rParam.mDamp);
        reboundVelocityFromCollision(pActor, 0.0f, 0.0f, 1.0f);
        addVelocityToGravityOrGround(pActor, rParam.mGravity);
    }

    void escapeFromPlayer(LiveActor* pActor, Vec3f* pFront, const ActorMoveParam& rParam) {
        moveAndTurnToDirection(pActor, pFront, pActor->mPosition - *MR::getPlayerPos(), rParam);
    }

};  // namespace MR

namespace {
    // Blends the up vector a tenth of the way to the ground (or against gravity) and faces rFront.
    void calcRotate(LiveActor* pActor, const Vec3f& rFront, f32 turnDegree) {
        Pos3f mtx;
        Vec3f targetUp;
        Vec3f up;
        Vec3f blendUp;
        Vec3f euler;
        if (MR::isBindedGround(pActor)) {
            targetUp.set(*MR::getGroundNormal(pActor));
        } else {
            targetUp.set(-pActor->mGravity);
        }

        MR::calcUpVec(&up, pActor);
        MR::vecBlend(up, targetUp, &blendUp, 0.1f);
        MR::normalizeOrZero(&blendUp);
        if (MR::isNearZero(blendUp)) {
            blendUp.set(up);
        }

        MR::makeMtxUpFront(&mtx, blendUp, rFront);

        calcEulerRadian(mtx, &euler, turnDegree);

        pActor->mRotation.set< f32 >(euler.x * _180_PI, euler.y * _180_PI, euler.z * _180_PI);
    }
};  // namespace

namespace MR {
    void moveAndTurnToDirection(LiveActor* pActor, const Vec3f& rDir, const ActorMoveParam& rParam) {
        Vec3f front;
        calcFrontVec(&front, pActor);
        moveAndTurnToDirection(pActor, &front, rDir, rParam);
        ::calcRotate(pActor, front, rParam.mTurnDegree);
    }

    void moveAndTurnToTarget(LiveActor* pActor, const Vec3f& rTarget, const ActorMoveParam& rParam) {
        Vec3f front;
        calcFrontVec(&front, pActor);
        normalizeOrZero(&front);
        moveAndTurnToDirection(pActor, &front, rTarget - pActor->mPosition, rParam);
        ::calcRotate(pActor, front, rParam.mTurnDegree);
    }

    void moveAndTurnToPlayer(LiveActor* pActor, const ActorMoveParam& rParam) {
        moveAndTurnToTarget(pActor, *getPlayerPos(), rParam);
    }

    void moveAndTurnAlongRail(LiveActor* pActor, f32 curveRatio, const ActorMoveParam& rParam, bool* pReachedGoal) {
        Quat4f rotation;
        Vec3f front;
        makeQuatRotateDegree(&rotation, pActor->mRotation);
        rotation.getZDir(front);
        Vec3f railDir(0, 0, 0);
        calcMovingDirectionAlongRailH(pActor, &railDir, pActor->mPosition, curveRatio, pReachedGoal);
        moveAndTurnToDirection(pActor, &front, railDir, rParam);
        ::calcRotate(pActor, front, rParam.mTurnDegree);
    }
};  // namespace MR
