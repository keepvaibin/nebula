#pragma once

#include "Game/Util/JMapIdInfo.hpp"

class GalaxyMoveArgument {
public:
    GalaxyMoveArgument(u32, const char*, s32, const JMapIdInfo*);

    bool isEqualStage(char const*) const;
    bool isEqualStageScenario(char const*, int) const;

    u32 mMoveType;
    const char* mStageName;
    s32 mScenarioNo;
    s32 _C;
    JMapIdInfo mIDInfo;
};
