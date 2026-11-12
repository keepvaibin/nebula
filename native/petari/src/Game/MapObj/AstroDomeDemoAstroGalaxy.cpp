#include "Game/MapObj/AstroDomeDemoAstroGalaxy.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

AstroDomeDemoAstroGalaxy::AstroDomeDemoAstroGalaxy(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< AstroDomeDemoAstroGalaxy >);
}

void AstroDomeDemoAstroGalaxy::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    MapObjActorUtil::setupInitInfoTypical(&info, mObjectName);
    initialize(rIter, info);

    const f32 frame = MR::hasGrandStar(5) ? 2.0f : MR::hasGrandStar(3) ? 1.0f : 0.0f;
    MR::setBrkFrameAndStop(this, frame);
}
