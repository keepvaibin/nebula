#include "Game/MapObj/CollisionBlocker.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cRadius = 50.0f;
};  // namespace

CollisionBlocker::CollisionBlocker(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< CollisionBlocker >);
}

void CollisionBlocker::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initHitSensor(1);

    MR::addHitSensorEye(this, "eye", 4, ::cRadius * mScale.x, Vec3f(0.0f, 0.0f, 0.0f));
    MR::connectToSceneMapObjMovement(this);
    MR::setClippingFar50m(this);
    makeActorAppeared();

    if (MR::isValidInfo(rIter)) {
        MR::useStageSwitchSleep(this, rIter);

        if (MR::useStageSwitchReadAppear(this, rIter)) {
            makeActorDead();
            MR::syncStageSwitchAppear(this);
        }

        if (MR::useStageSwitchReadB(this, rIter)) {
            MR::listenStageSwitchOnB(this, MR::Functor(this, &CollisionBlocker::forceBreak));
        }
    }
}

void CollisionBlocker::forceBreak() {
    kill();
}

void CollisionBlocker::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        MR::sendArbitraryMsg(ACTMES_PUSH_FORCE, pReceiver, pSender);
    }
}
