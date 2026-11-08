#include "Game/Enemy/StinkBugBase.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <nebula_geometry.hpp>


StinkBugBase::StinkBugBase(const char* pName)
    : LiveActor(pName), mFront(0.0f, 0.0f, 1.0f), mInitPos(0.0f, 0.0f, 0.0f), mInitFront(0.0f, 0.0f, 1.0f), mTerritoryAngle(180.0f), mRadius(1000.0f), mIsTurnLeft(false) {
}

void StinkBugBase::setDashVelocity(f32 velocity) {
    Vec3f result;
    f32 radius = mRadius;
    result.scaleAdd(radius, mFront, mInitPos);
    f32 distance = result.distance(mPosition);
    distance = MR::min(velocity, distance);
    mVelocity.scale(distance, mFront);
}

void StinkBugBase::fixInitPos() {
    mVelocity.zero();
    mPosition.set(mInitPos);
}

void StinkBugBase::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    const char* objectName;
    MR::getObjectName(&objectName, rIter);
    initModelManagerWithAnm(objectName, nullptr, false);
    MR::connectToSceneCollisionEnemyNoShadowedMapObjStrongLight(this);
    MR::initLightCtrl(this);
    MR::initShadowFromCSV(this, "Shadow");
    initEffectKeeper(2, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    MR::useStageSwitchWriteDead(this, rIter);
    mInitPos.set(mPosition);
    MR::calcFrontVec(&mFront, this);
    mInitFront.set(mFront);
    Vec3f upVec;
    MR::calcUpVec(&upVec, this);
    mGravity.negate(upVec);
}

bool StinkBugBase::isPlayerInTerritory(f32 arg1, f32 arg2, f32 arg3, f32 arg4) const {
    if (!MR::isNearPlayer(this, mRadius + arg1)) {
        return false;
    }

    const Vec3f& rPos = mPosition;
    Vec3f v1;
    v1.sub(*MR::getPlayerPos(), rPos);

    Vec3f upVec;
    MR::calcUpVec(&upVec, this);

    f32 dot = v1.dot(upVec);

    if (MR::abs(dot) > arg3) {
        return false;
    }

    Vec3f sideVec;
    MR::calcSideVec(&sideVec, this);

    dot = v1.dot(sideVec);

    if (MR::abs(dot) > arg2) {
        return false;
    }

    if (mTerritoryAngle == 0.0f) {
        return v1.dot(mFront) > 0.0f;
    }

    if (mTerritoryAngle == 180.0f) {
        return true;
    }

    f32 f1 = MR::abs(MR::sinDegree(mTerritoryAngle));

    const Vec3f& rPos2 = mPosition;
    Vec3f scaledAdded;
    scaledAdded.scaleAdd(-arg4 / f1, mInitFront, rPos2);

    Vec3f v3;
    v3.sub(*MR::getPlayerPos(), scaledAdded);

    Vec3f v5 = v3.killElement(mGravity);

    return MR::isNearAngleDegree(v5, mInitFront, mTerritoryAngle);
}

bool StinkBugBase::tryTurnSearch(f32 angle) {
    if (mIsTurnLeft) {
        MR::rotateVecDegree(&mFront, mGravity, angle);
    } else {
        MR::rotateVecDegree(&mFront, mGravity, -angle);
    }

    if (!MR::isNearAngleDegree(mFront, mInitFront, mTerritoryAngle)) {
        MR::clampVecAngleDeg(&mFront, mInitFront, mTerritoryAngle);
        mIsTurnLeft = !mIsTurnLeft;
        return true;
    }

    return false;
}

bool StinkBugBase::tryTurnDashSign(f32 angle) {
    Vec3f* playerPos = MR::getPlayerPos();
    MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFront, *playerPos, angle);
    MR::clampVecAngleDeg(&mFront, mInitFront, mTerritoryAngle);
    return true;
}

bool StinkBugBase::isHitHorn(HitSensor* pSender, HitSensor* pReceiver, f32 arg3) const {
    Vec3f toReceiver;
    toReceiver.sub(pReceiver->mPosition, pSender->mPosition);

    if (toReceiver.dot(mFront) < 0.0f) {
        return false;
    }

    Vec3f up;
    MR::calcUpVec(&up, this);

    Vec3f v2;
    v2.scale(up.dot(toReceiver), up);

    f32 radius = pReceiver->mRadius;

    return v2.length() <= arg3 + radius;
}
