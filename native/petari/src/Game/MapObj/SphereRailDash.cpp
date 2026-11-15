#include "Game/MapObj/SphereRailDash.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvSphereRailDash {
    NEW_NERVE(SphereRailDashNrvWait, SphereRailDash, Wait);
    NEW_NERVE(SphereRailDashNrvRailMove, SphereRailDash, RailMove);
};  // namespace NrvSphereRailDash

SphereRailDash::SphereRailDash(const char* pName)
    : LiveActor(pName), mBinderSensor(), mOffset(0.0f, 0.0f, 0.0f), mEndSpeed(50.0f), mCoordCubic(), mCoordQuadratic(), mCoordLinear(), mMoveStep() {
    lumacore::setClass(this, lumacore::kClassId< SphereRailDash >);
}

void SphereRailDash::init(const TableIter& rIter) {
    initNerve(GET_NERVE(SphereRailDash, SphereRailDashNrvWait));
    initRailRider(rIter);
    MR::moveCoordToStartPos(this);
    Vec3f railPosition(MR::getRailPos(this));
    initHitSensor(1);
    MR::addHitSensor(this, "body", ATYPE_SPHERE_DASH, 5, 200.0f, railPosition);
    MR::connectToSceneMapObjMovement(this);
    MR::invalidateClipping(this);
    MR::getPlacementArg1NoInit(rIter, &mEndSpeed);
    appear();
}

bool SphereRailDash::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_SPHERE_PLAYER_BINDED) {
        mBinderSensor = pSender;
        return true;
    }

    return false;
}

void SphereRailDash::exeWait() {
    if (mBinderSensor != nullptr) {
        MR::moveCoordToStartPos(this);
        const Vec3f& rPosition = mBinderSensor->mHost->mPosition;
        mOffset = rPosition - MR::getRailPos(this);
        Vec3f direction(MR::getRailDirection(this));
        Vec3f velocity(mBinderSensor->mHost->mVelocity);
        f32 speed = direction.dot(velocity);

        if (speed < 0.0f) {
            speed = 0.0f;
            mOffsetVelocity = velocity;
        } else {
            mOffsetVelocity = velocity - direction * speed;
        }

        f32 endSpeed = mEndSpeed;
        // No speed at either end (the arg is 0 and the player stands still) crosses the rail in one step.
        const f32 speedSum = speed + 2.0f * endSpeed;
        f32 duration = speedSum > 0.0f ? 3.0f * MR::getRailTotalLength(this) / speedSum : 1.0f;
        f32 acceleration = 2.0f * (endSpeed - speed) / duration;
        mCoordCubic = -acceleration / (2.0f * duration) / 3.0f;
        mCoordQuadratic = acceleration / 2.0f;
        mCoordLinear = speed;
        mMoveStep = duration + (duration - static_cast< s32 >(duration) > 0.0001f ? 1.0f : 0.0f);

        if (mMoveStep < 0) {
            mMoveStep = 0;
        }

        setNerve(GET_NERVE(SphereRailDash, SphereRailDashNrvRailMove));
    }
}

void SphereRailDash::exeRailMove() {
    if (MR::isFirstStep(this)) {
        MR::zeroVelocity(mBinderSensor->mHost);
    }

    if (getNerveStep() + 1 >= mMoveStep) {
        MR::moveCoordToEndPos(this);
    } else {
        f32 time = getNerveStep() + 1;
        MR::setRailCoord(this, time * (mCoordLinear + time * (mCoordQuadratic + mCoordCubic * time)));
    }

    // A rail with no length is already at its end.
    const f32 railLength = MR::getRailTotalLength(this);
    f32 progress = railLength > 0.0f ? MR::getRailCoord(this) / railLength : 1.0f;
    mOffset += mOffsetVelocity;
    mOffsetVelocity.mult(0.85f);
    Vec3f previousPosition(mBinderSensor->mHost->mPosition);
    Vec3f position = MR::getRailPos(this) + mOffset.multInLine2(1.0f - progress);
    Vec3f& rVelocity = mBinderSensor->mHost->mVelocity;
    rVelocity.set(position - previousPosition);

    if (MR::isRailReachedGoal(this)) {
        mBinderSensor->mHost->mPosition.set(position);
        LiveActor* pActor = mBinderSensor->mHost;
        f32 endSpeed = mEndSpeed;
        pActor->mVelocity.set(MR::getRailDirection(this) * endSpeed);
        mBinderSensor->receiveMessage(ACTMES_END_RAIL_DASH, getSensor("body"));
        mBinderSensor = nullptr;
        setNerve(GET_NERVE(SphereRailDash, SphereRailDashNrvWait));
    }
}
