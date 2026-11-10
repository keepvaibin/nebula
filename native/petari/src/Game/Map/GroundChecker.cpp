#include "Game/Map/GroundChecker.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

GroundChecker::GroundChecker(const char* pName, f32 radius, f32 offsetY) : LiveActor(pName), mFriction(1.0f) {
    lumacore::setClass(this, lumacore::kClassId< GroundChecker >);
    MR::invalidateClipping(this);
    initBinder(radius, offsetY, 0);
    makeActorAppeared();
}

void GroundChecker::bringFriction(f32 target, f32 rate) {
    mFriction = mFriction * (1.0f - rate) + (target * rate);
}
