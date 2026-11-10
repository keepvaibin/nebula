#include "Game/MapObj/IceVolcanoUpDownPlane.hpp"
#include "Game/MapObj/StageEffectDataTable.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_tryCalcNearestPosToPlayer_f1, "tryCalcNearestPosToPlayer.f1", 380.0f);

IceVolcanoUpDownPlane::IceVolcanoUpDownPlane(const char* pName) : RailMoveObj(pName), mNearestPosToPlayer(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< IceVolcanoUpDownPlane >);
}

void IceVolcanoUpDownPlane::move() {
    bool success = tryCalcNearestPosToPlayer(&mNearestPosToPlayer);

    if (isMoving() && success) {
        MR::StageEffect::tryStageEffectMoving(this, mObjectName);
    }
}

bool IceVolcanoUpDownPlane::tryCalcNearestPosToPlayer(Vec3f* pDst) const {
    Vec3f dirVec;
    dirVec.sub(*MR::getPlayerPos(), mPosition);

    Vec3f upVec;
    MR::calcUpVec(&upVec, this);

    if (dirVec.dot(upVec) <= -cNum_tryCalcNearestPosToPlayer_f1) {
        return false;
    }

    Vec3f offset = dirVec.killElement(upVec);

    f32 radius;
    MR::calcModelBoundingRadius(&radius, this);

    if (offset.length() > radius) {
        offset.setLength(radius);
    }

    pDst->add(offset, mPosition);
    return true;
}

Vec3f* IceVolcanoUpDownPlane::getSoundCalcPos() {
    return &mNearestPosToPlayer;
}
