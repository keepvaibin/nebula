#include "Game/MapObj/TimeAppearObj.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sDefaultTimer = NebulaFrameClock::stepsOfMilliseconds(500);
};  // namespace

namespace NrvTimeAppearObj {
    NEW_NERVE(TimeAppearObjNrvHide, TimeAppearObj, Hide);
    NEW_NERVE(TimeAppearObjNrvEnd, TimeAppearObj, End);
};  // namespace NrvTimeAppearObj

TimeAppearObj::TimeAppearObj(const char* pName) : MapObjActor(pName), mTimer(::sDefaultTimer) {
    lumacore::setClass(this, lumacore::kClassId< TimeAppearObj >);
}

void TimeAppearObj::init(const TableIter& rIter) {
    MapObjActorInitInfo info;
    info.setupModelName("KoopaBattleMapStairTurn");
    info.setupSound(4);
    info.setupNerve(GET_NERVE(TimeAppearObj, TimeAppearObjNrvHide));
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    initialize(rIter, info);
    MR::getPlacementArg0NoInit(rIter, &mTimer);
    makeActorDead();
}

void TimeAppearObj::exeHide() {
    if (MR::isStep(this, mTimer)) {
        setNerve(GET_NERVE(TimeAppearObj, TimeAppearObjNrvEnd));
    }
}

void TimeAppearObj::exeEnd() {
    if (MR::isFirstStep(this)) {
        if (MR::isRegisteredEffect(this, "Appear")) {
            MR::emitEffect(this, "Appear");
        }

        if (MR::isEqualString(mObjectName, "KoopaBattleMapStairTurn")) {
            MR::startSound(this, "SE_OJ_KP_BTL_ST_TURN_APPEAR");
        }

        MR::showModel(this);
        MR::validateCollisionParts(this);
    }
}

void TimeAppearObj::appear() {
    MapObjActor::appear();
    MR::hideModel(this);
    MR::invalidateCollisionParts(this);
    setNerve(GET_NERVE(TimeAppearObj, TimeAppearObjNrvHide));
}
