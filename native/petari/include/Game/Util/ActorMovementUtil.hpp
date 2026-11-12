#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LumaMarks.hpp"
#include <nebula_geometry.hpp>


class HitSensor;
class LiveActor;

namespace MR {

    struct ActorMoveParam {
        f32 mMoveSpeed;
        f32 mGravity;
        f32 mDamp;
        f32 mTurnDegree;
    };

    f32 calcDistance(const HitSensor*, const HitSensor*, Vec3f*);
    f32 calcDistance(const LiveActor*, const Vec3f&);
    f32 calcDistanceHorizontal(const LiveActor*, const Vec3f&, const Vec3f&);
    f32 calcDistanceVertical(const LiveActor*, const Vec3f&, const Vec3f&);
    f32 calcDistanceVertical(const LiveActor*, const Vec3f&);
    f32 calcDistanceToPlayer(const LiveActor*);
    f32 calcDistanceToPlayerH(const LiveActor*);
    bool isNear(const HitSensor*, const HitSensor*, f32);
    bool isNear(const LiveActor*, const LiveActor*, f32);
    bool isNear(const LiveActor*, const Vec3f&, f32);
    bool isNearPlayer(const LiveActor*, f32);
    bool isNearPlayerAnyTime(const LiveActor*, f32);
    bool isNearPlayer(const Vec3f&, f32);
    bool isNearPlayerPose(const LiveActor*, f32, f32);
    bool isNearPlayerHorizontal(const LiveActor*, f32);
    void calcFrontVec(LUMA_OUT Vec3f*, const LiveActor*);
    void calcUpVec(LUMA_OUT Vec3f*, const LiveActor*);
    void calcSideVec(LUMA_OUT Vec3f*, const LiveActor*);
    void calcPositionUpOffset(LUMA_OUT Vec3f*, const LiveActor*, f32);
    void calcVecToPlayerH(Vec3f*, const LiveActor*, const Vec3f*);
    void calcVecFromPlayerH(Vec3f*, const LiveActor*);
    void calcVecToTargetPosH(Vec3f*, const LiveActor*, const Vec3f&, const Vec3f*);
    void calcVecFromTargetPosH(Vec3f*, const LiveActor*, const Vec3f&, const Vec3f*);
    bool isFaceToTargetDegree(const LiveActor*, const Vec3f&, const Vec3f&, f32);
    bool isFaceToPlayerDegree(const LiveActor*, const Vec3f&, f32);
    bool isFaceToTargetDegree(const LiveActor*, const Vec3f&, f32);
    bool isFaceToPlayerDegree(const LiveActor*, f32);
    bool isFaceToPlayerDegreeHV(const LiveActor*, const Vec3f&, f32, f32);
    bool isFaceToTargetHorizontalDegree(const LiveActor*, const Vec3f&, const Vec3f&, f32);
    bool isFaceToPlayerHorizontalDegree(const LiveActor*, const Vec3f&, f32);
    bool isClockwiseToDir(const LiveActor*, const Vec3f&, const Vec3f&);
    bool isClockwiseToPlayer(const LiveActor*, const Vec3f&);
    bool isPlayerLeftSide(const LiveActor*);
    bool isInSightConePlayer(const LiveActor*, const Vec3f&, f32, f32);
    bool isInSightFanPlayer(const LiveActor*, const Vec3f&, f32, f32, f32);
    bool isBindedWallFront(const LiveActor*, const Vec3f&, f32);
    bool isOnPlayer(const LiveActor*);
    bool isPlayerExistSide(const LiveActor*, f32, f32);
    bool isPlayerExistUp(const LiveActor*, f32, f32);
    bool isPlayerExistDown(const LiveActor*, f32, f32);
    bool isInWater(const LiveActor*, const Vec3f&);
    bool isInDeath(const LiveActor*, const Vec3f&);
    void makeMtxTR(MtxPtr, const LiveActor*);
    void makeMtxTRS(MtxPtr, const LiveActor*);
    void makeMtxTransRotateY(MtxPtr, const LiveActor*);
    void calcMtxFromGravityAndZAxis(Pos3f*, const LiveActor*, const Vec3f&, const Vec3f&);
    void calcAndSetBaseMtxFromGravityAndZAxis(LiveActor*, const Vec3f&);
    void calcActorAxis(Vec3f*, Vec3f*, Vec3f*, const LiveActor*);
    void calcActorAxisX(Vec3f*, const LiveActor*);
    void calcActorAxisY(Vec3f*, const LiveActor*);
    void calcActorAxisZ(Vec3f*, const LiveActor*);
    bool faceToVector(Quat4f*, Vec3f, f32);
    bool faceToVector(MtxPtr, Vec3f, f32);
    bool faceToPoint(MtxPtr, Vec3f, f32);
    void makeQuatFromRotate(LUMA_OUT Quat4f*, const LiveActor*);
    void makeQuatAndFrontFromRotate(Quat4f*, Vec3f*, const LiveActor*);
    void turnQuatUpToGravity(Quat4f*, const Quat4f&, const LiveActor*);
    void blendQuatFromGroundAndFront(Quat4f*, const LiveActor*, const Vec3f&, f32, f32);
    void resetPosition(LiveActor*);
    void resetPosition(LiveActor*, const Vec3f&);
    void resetPosition(LiveActor*, const char*);
    bool makeMtxOnMapCollision(Pos3f*, LiveActor*, f32);
    void calcVelocityMoveToDirectionHorizon(LUMA_OUT Vec3f*, const LiveActor*, const Vec3f&, f32);
    void calcVelocityMoveToDirectionHorizon(LUMA_OUT Vec3f*, const LiveActor*, const Vec3f&, f32, f32, f32, f32);
    void calcVelocityMoveToDirection(LUMA_OUT Vec3f*, const LiveActor*, const Vec3f&, f32);
    void calcVelocityMoveToDirection(LUMA_OUT Vec3f*, const LiveActor*, const Vec3f&, f32, f32, f32, f32);
    void calcVelocityMoveToTarget(LUMA_OUT Vec3f*, const LiveActor*, const Vec3f&, f32);
    void addVelocityMoveToDirection(LiveActor*, const Vec3f&, f32);
    void addVelocityMoveToTarget(LiveActor*, const Vec3f&, f32);
    void addVelocityMoveToTarget(LiveActor*, const Vec3f&, f32, f32, f32, f32);
    void addVelocityAwayFromTarget(LiveActor*, const Vec3f&, f32);
    void addVelocityAwayFromTarget(LiveActor*, const Vec3f&, f32, f32, f32, f32);
    void addVelocityClockwiseToDirection(LiveActor*, const Vec3f&, f32);
    void addVelocityClockwiseToTarget(LiveActor*, const Vec3f&, f32);
    void addVelocityClockwiseToPlayer(LiveActor*, f32);
    void addVelocityJump(LiveActor*, f32);
    bool addVelocityLimit(LiveActor*, const Vec3f&);
    void setVelocityJump(LiveActor*, f32);
    void addVelocityToGravity(LiveActor*, f32);
    void addVelocityToGravityOrGround(LiveActor*, f32);
    bool addVelocityToCollisionNormal(LiveActor*, f32);
    void addVelocityKeepHeight(LiveActor*, const Vec3f&, f32, f32);
    void addVelocityKeepHeight(LiveActor*, const Vec3f&, f32, f32, f32);
    bool addVelocityKeepHeightUseShadow(LiveActor*, f32, f32, f32, const char*);
    bool addVelocityKeepHeightUseShadow(LiveActor*, f32, f32, f32, f32, const char*);
    void addVelocitySeparateHV(LiveActor*, const Vec3f&, f32, f32);
    void setVelocitySeparateHV(LiveActor*, const Vec3f&, f32, f32);
    void attenuateVelocity(LiveActor*, f32);
    void attenuateVelocityExceptDirection(LiveActor*, const Vec3f&, f32);
    void restrictVelocity(LiveActor*, f32);
    void killVelocityOnGroundH(LiveActor*);
    void killVelocityOnGroundCosH(LiveActor*, f32);
    void killVelocityToTarget(LiveActor*, const Vec3f&);
    void forceBindOnGround(LiveActor*, f32, f32);
    bool reboundVelocityFromEachCollision(LiveActor*, f32, f32, f32, f32);
    bool reboundVelocityFromCollision(LiveActor*, f32 = 0.0f, f32 = 0.0f, f32 = 1.0f);
    void zeroVelocity(LiveActor*);
    void setVelocity(LiveActor*, const Vec3f&);
    void addVelocity(LiveActor*, const Vec3f&);
    void scaleVelocity(LiveActor*, f32);
    f32 calcVelocityLength(const LiveActor*);
    f32 calcGravitySpeed(const LiveActor*);
    void applyVelocityDampAndGravity(LiveActor*, f32 gravity, f32 groundDampH, f32 airDampH, f32 dampVUp, f32 velHMin);
    void setVelocityJumpAwayFromPlayer(LiveActor*, f32, f32);
    bool sendMsgPushAndKillVelocityToTarget(LiveActor*, HitSensor*, HitSensor*);
    void addVelocityFromPush(LiveActor*, f32, HitSensor*, HitSensor*);
    void addVelocityFromPushHorizon(LiveActor*, f32, HitSensor*, HitSensor*);
    void addVelocitySeparateHV(LiveActor*, HitSensor*, HitSensor*, f32, f32);
    void setVelocitySeparateHV(LiveActor*, HitSensor*, HitSensor*, f32, f32);
    void setVelocityBlowAttack(LiveActor*, HitSensor*, HitSensor*, f32, f32, s32);
    bool reflectVelocityH(LiveActor*, HitSensor*, HitSensor*, f32);
    bool calcVelocityAreaMoveOnGround(Vec3f*, const LiveActor*);
    bool calcVelocityRailMoveOnGround(Vec3f*, const LiveActor*);
    bool calcVelocityAreaOrRailMoveOnGround(Vec3f*, const LiveActor*);
    void rotateDirectionGravityDegree(const LiveActor*, Vec3f*, f32);
    void turnDirection(const LiveActor*, LUMA_INOUT Vec3f*, const Vec3f&, f32);
    void turnDirectionDegree(const LiveActor*, LUMA_INOUT Vec3f*, const Vec3f&, f32);
    void turnDirectionToTarget(const LiveActor*, LUMA_INOUT Vec3f*, const Vec3f&, f32);
    void turnDirectionToTargetDegree(const LiveActor*, LUMA_INOUT Vec3f*, const Vec3f&, f32);
    void turnDirectionToTargetDegreeHorizon(const LiveActor*, LUMA_INOUT Vec3f*, const Vec3f&, f32);
    bool turnDirectionToTargetUseGroundNormalDegree(const LiveActor*, Vec3f*, const Vec3f&, f32);
    void turnDirectionToPlayerDegree(const LiveActor*, LUMA_INOUT Vec3f*, f32);
    void turnDirectionToPlayerDegreeHorizon(const LiveActor*, LUMA_INOUT Vec3f*, f32);
    void turnDirectionFromTargetDegree(const LiveActor*, LUMA_INOUT Vec3f*, const Vec3f&, f32);
    void turnDirectionFromTargetDegreeHorizon(const LiveActor*, LUMA_INOUT Vec3f*, const Vec3f&, f32);
    void turnDirectionFromPlayerDegree(const LiveActor*, LUMA_INOUT Vec3f*, f32);
    void turnDirectionFromPlayerDegreeHorizon(const LiveActor*, LUMA_INOUT Vec3f*, f32);
    void turnDirectionAndGravityH(Quat4f*, const LiveActor*, const Vec3f&, f32, f32);
    void turnDirectionAndGravityH(LiveActor*, const Vec3f&, f32, f32);
    void turnDirectionToGround(const LiveActor*, Vec3f*);
    void moveAndTurnToDirection(LiveActor*, Vec3f*, const Vec3f&, const ActorMoveParam&);
    void moveAndTurnToTarget(LiveActor*, Vec3f*, const Vec3f&, const ActorMoveParam&);
    void moveAndTurnToPlayer(LiveActor*, Vec3f*, const ActorMoveParam&);
    void flyAndTurnAlongRailSearchingPlayer(LiveActor*, Vec3f*, f32, const ActorMoveParam&, bool);
    void escapeFromPlayer(LiveActor*, Vec3f*, const ActorMoveParam&);
    void moveAndTurnToDirection(LiveActor*, const Vec3f&, const ActorMoveParam&);
    void moveAndTurnToTarget(LiveActor*, const Vec3f&, const ActorMoveParam&);
    void moveAndTurnToPlayer(LiveActor*, const ActorMoveParam&);
    void moveAndTurnAlongRail(LiveActor*, f32, const ActorMoveParam&, bool*);

    // NOTE: symbols dont exist for these, however the exact logic is used various places
    // If a suitable symbol is found, these can be replaced.
    inline Vec3f getVelocityHorizon(LiveActor* pActor) {
        Vec3f velH;
        velH.scaleAdd(-pActor->mGravity.dot(pActor->mVelocity), pActor->mGravity, pActor->mVelocity);
        return velH;
    }

    inline void killVelocityVertical(LiveActor* pActor) {
        pActor->mVelocity.scaleAdd(-pActor->mGravity.dot(pActor->mVelocity), pActor->mGravity, pActor->mVelocity);
    }

    inline Vec3f killGravity(LiveActor* pActor, const Vec3f& rVec) {
        Vec3f velH;
        velH.scaleAdd(-pActor->mGravity.dot(rVec), pActor->mGravity, rVec);
        return velH;
    }
};  // namespace MR
