#pragma once

#include "Game/Util/PlacementId.hpp"

class GalaxyMoveArgument {
public:
    GalaxyMoveArgument(u32, const char*, s32, const PlacementId*);

    bool isEqualStage(char const*) const;
    bool isEqualStageScenario(char const*, int) const;

    u32 mMoveType;
    const char* mStageName;
    s32 mScenarioNo;
    s32 _C;
    PlacementId mIDInfo;
};
