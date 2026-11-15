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
#include "Game/System/LumaCore.hpp"

StinkBugBase::StinkBugBase(const char* pName)
    : LiveActor(pName), mFront(0.0f, 0.0f, 1.0f), mInitPos(0.0f, 0.0f, 0.0f), mInitFront(0.0f, 0.0f, 1.0f), mTerritoryAngle(180.0f), mRadius(1000.0f), mIsTurnLeft(false) {
    lumacore::setClass(this, lumacore::kClassId< StinkBugBase >);
}

// Toward the end of the dash (the radius ahead of home), never past it.
void StinkBugBase::setDashVelocity(f32 speed) {
    Vec3f dashEnd;
    dashEnd.scaleAdd(mRadius, mFront, mInitPos);
    f32 step = dashEnd.distance(mPosition);
    step = MR::min(speed, step);
    mVelocity.scale(step, mFront);
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
    Vec3f up;
    MR::calcUpVec(&up, this);
    mGravity.negate(up);
}

// The player is within the radius plus `reachMargin`, within the side and up half sizes, and inside the territory cone
// (apex `backOffset` / sin(angle) behind it on the home front). Angle 0 means anywhere ahead, 180 anywhere at all.
bool StinkBugBase::isPlayerInTerritory(f32 reachMargin, f32 sideHalfSize, f32 upHalfSize, f32 backOffset) const {
    if (!MR::isNearPlayer(this, mRadius + reachMargin)) {
        return false;
    }

    Vec3f toPlayer;
    toPlayer.sub(*MR::getPlayerPos(), mPosition);

    Vec3f up;
    MR::calcUpVec(&up, this);
    if (MR::abs(toPlayer.dot(up)) > upHalfSize) {
        return false;
    }

    Vec3f side;
    MR::calcSideVec(&side, this);
    if (MR::abs(toPlayer.dot(side)) > sideHalfSize) {
        return false;
    }

    if (mTerritoryAngle == 0.0f) {
        return toPlayer.dot(mFront) > 0.0f;
    }

    if (mTerritoryAngle == 180.0f) {
        return true;
    }

    f32 sinAngle = MR::abs(MR::sinDegree(mTerritoryAngle));
    Vec3f apex;
    apex.scaleAdd(-backOffset / sinAngle, mInitFront, mPosition);

    Vec3f fromApex;
    fromApex.sub(*MR::getPlayerPos(), apex);
    Vec3f fromApexFlat = fromApex.killElement(mGravity);

    return MR::isNearAngleDegree(fromApexFlat, mInitFront, mTerritoryAngle);
}

// Sweeps the front side to side within the territory angle; true at each edge.
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

void StinkBugBase::turnDashSign(f32 angle) {
    MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFront, *MR::getPlayerPos(), angle);
    MR::clampVecAngleDeg(&mFront, mInitFront, mTerritoryAngle);
}

// The receiver is ahead and within `margin` plus its radius of the horn's height.
bool StinkBugBase::isHitHorn(HitSensor* pSender, HitSensor* pReceiver, f32 margin) const {
    Vec3f toReceiver;
    toReceiver.sub(pReceiver->mPosition, pSender->mPosition);

    if (toReceiver.dot(mFront) < 0.0f) {
        return false;
    }

    Vec3f up;
    MR::calcUpVec(&up, this);

    Vec3f upPart;
    upPart.scale(up.dot(toReceiver), up);

    return upPart.length() <= margin + pReceiver->mRadius;
}
