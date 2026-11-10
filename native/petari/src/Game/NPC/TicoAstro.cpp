#include "Game/NPC/TicoAstro.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/System/LumaCore.hpp"

TicoAstro::TicoAstro(const char* pName) : Tico(pName) {
    lumacore::setClass(this, lumacore::kClassId< TicoAstro >);
}

void TicoAstro::init(const TableIter& rIter) {
    Tico::init(rIter);
    // Present while the green star count is above this (0-2).
    s32 greenStarThreshold = -1;
    MR::getPlacementArg2NoInit(rIter, &greenStarThreshold);

    switch (greenStarThreshold) {
    case 0:
        if (MR::calcCurrentGreenStarNum() <= 0) {
            makeActorDead();
        }
        break;
    case 1:
        if (MR::calcCurrentGreenStarNum() > 1) {
            return;
        }

        makeActorDead();
        break;
    case 2:
        if (MR::calcCurrentGreenStarNum() <= 2) {
            makeActorDead();
        }
        break;
    }
}
