#include "Game/Screen/GalaxyMapBackground.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/System/LumaCore.hpp"

GalaxyMapBackground::GalaxyMapBackground() : LayoutActor("背景", true) {
    lumacore::setClass(this, lumacore::kClassId< GalaxyMapBackground >);
    initLayoutManager("MapGalaxyBg", 1);
}

void GalaxyMapBackground::appear() {
    LayoutActor::appear();
    MR::startAnim(this, "Wait", 0);
}
