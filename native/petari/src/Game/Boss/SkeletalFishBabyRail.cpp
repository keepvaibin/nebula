#include "Game/Boss/SkeletalFishBabyRail.hpp"
#include "Game/Boss/SkeletalFishBabyRailHolder.hpp"
#include "Game/Util/PlacementUtil.hpp"

SkeletalFishBabyRail::SkeletalFishBabyRail(const char* pName) : LiveActor(pName) {
    _8C = -1;
};

void SkeletalFishBabyRail::init(const TableIter& rIter) {
    MR::createSkeletalFishBabyRailHolder();
    initRailRider(rIter);
    MR::getPlacementArg0NoInit(rIter, &_8C);
    MR::getSkeletalFishBabyRailHolder()->add(this);
    makeActorDead();
}

SkeletalFishBabyRail::~SkeletalFishBabyRail() {
}
