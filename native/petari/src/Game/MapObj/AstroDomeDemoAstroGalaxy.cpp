#include "Game/MapObj/AstroDomeDemoAstroGalaxy.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 2.0f);

AstroDomeDemoAstroGalaxy::AstroDomeDemoAstroGalaxy(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< AstroDomeDemoAstroGalaxy >);
}

void AstroDomeDemoAstroGalaxy::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    MapObjActorUtil::setupInitInfoTypical(&info, mObjectName);
    initialize(rIter, info);

    const f32 frame = MR::hasGrandStar(5) ? cNum_init_f1 : MR::hasGrandStar(3) ? 1.0f : 0.0f;
    MR::setBrkFrameAndStop(this, frame);
}
