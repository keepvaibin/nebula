#pragma once

#include "Game/Util/PlacementId.hpp"

class GalaxyMoveArgument {
public:
    // Why the stage changes.
    enum Type : u32 {
        Type_InGame = 0,  // a warp, launch star or hub door
        Type_ScenarioSelect = 2,
        Type_GoBackAstroDome = 3,
        Type_AfterStageClear = 4,
        Type_AfterMiss = 5,
        Type_AfterLoadingGameData = 6,
        Type_Title = 7,
    };

    GalaxyMoveArgument(u32 moveType, const char* pStageName, s32 scenarioNo, const PlacementId* pIdInfo);

    bool isEqualStage(char const*) const;
    bool isEqualStageScenario(char const*, int) const;

    u32 mMoveType;
    const char* mStageName;
    s32 mScenarioNo;
    s32 mSelectedScenarioNo;
    PlacementId mIDInfo;
};
