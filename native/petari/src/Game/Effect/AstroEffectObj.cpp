#include "Game/Effect/AstroEffectObj.hpp"
#include "Game/Effect/SimpleEffectObj.hpp"
#include "Game/MapObj/AstroMapObjFunction.hpp"
#include "Game/Util/DemoUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include "Game/System/LumaCore.hpp"

AstroEffectObj::AstroEffectObj(const char* pName) : SimpleEffectObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< AstroEffectObj >);
    mDomeId = -1;
}

void AstroEffectObj::init(const TableIter& rIter) {
    SimpleEffectObj::init(rIter);
    mDomeId = AstroMapObjFunction::getDomeIdFromArg0(rIter);
    MR::registerDemoSimpleCastAll(this);
    selectNrvWait();
    makeActorAppeared();
}

bool AstroEffectObj::receiveOtherMsg(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

void AstroEffectObj::setStateMove() {
    if (AstroMapObjFunction::isEnableRevival(mObjName, mDomeId)) {
        SimpleEffectObj::setStateMove();
    }
}

void AstroEffectObj::selectNrvWait() {
    if (AstroMapObjFunction::isAlreadyRevival(mObjName, mDomeId)) {
        SimpleEffectObj::setStateMove();
        return;
    }
    SimpleEffectObj::setStateWait();
}
