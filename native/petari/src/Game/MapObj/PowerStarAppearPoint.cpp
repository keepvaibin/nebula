#include "Game/MapObj/PowerStarAppearPoint.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

PowerStarAppearPoint::PowerStarAppearPoint(const char* pName) : LiveActor(pName), mCameraInfo() {
    lumacore::setClass(this, lumacore::kClassId< PowerStarAppearPoint >);
}

void PowerStarAppearPoint::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::joinToGroupArray(this, rIter, "パワースター出現ポイントグループ", 16);
    MR::initActorCamera(this, rIter, &mCameraInfo);
    makeActorAppeared();
}
