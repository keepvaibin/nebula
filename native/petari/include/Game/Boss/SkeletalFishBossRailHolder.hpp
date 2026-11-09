#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"

class SkeletalFishBossRail;

class SkeletalFishBossRailHolder : public NameObj {
public:
    SkeletalFishBossRailHolder(const char*);

    void add(SkeletalFishBossRail*);
    SkeletalFishBossRail* getInOrder(s32) const;
    SkeletalFishBossRail* getByID(s32) const;

    // Room for the boss's three levels in itself; a stage with more rails moves it to a larger store.
    MR::InlineVector< SkeletalFishBossRail*, 3 > mRails;
};

namespace MR {
    SkeletalFishBossRailHolder* getSkeletalFishBossRailHolder();
    void createSkeletalFishBossRailHolder();
};  // namespace MR
