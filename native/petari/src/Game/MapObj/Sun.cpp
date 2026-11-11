#include "Game/MapObj/Sun.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

Sun::Sun(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< Sun >);
}

void Sun::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("Sun", nullptr, false);
    MR::connectToSceneSun(this);
    MR::invalidateClipping(this);
    makeActorAppeared();
}
