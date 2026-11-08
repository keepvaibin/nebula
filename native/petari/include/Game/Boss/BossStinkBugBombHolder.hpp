#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"

class BossStinkBugBomb;

class BossStinkBugBombHolder : public DeriveActorGroup< BossStinkBugBomb > {
public:
    /// @param pHost A pointer to the owning actor instance.
    BossStinkBugBombHolder(LiveActor* pHost);
};
